/*
 * linux/fs/hfs/super.c
 *
 * Copyright (C) 1995-1997  Paul H. Hargrove
 * This file may be distributed under the terms of the GNU General Public License.
 *
 * This file contains hfs_read_super(), some of the super_ops and
 * init_module() and cleanup_module().	The remaining super_ops are in
 * inode.c since they deal with inodes.
 *
 * Based on the minix file system code, (C) 1991, 1992 by Linus Torvalds
 *
 * "XXX" in a comment is a note to myself to consider changing something.
 *
 * In function preconditions the term "valid" applied to a pointer to
 * a structure means that the pointer is non-NULL and the structure it
 * points to has all fields initialized to consistent values.
 *
 * The code in this file initializes some structures which contain
 * pointers by calling memset(&foo, 0, sizeof(foo)).
 * This produces the desired behavior only due to the non-ANSI
 * assumption that the machine representation of NULL is all zeros.
 */

#include "hfs.h"
#include <linux/hfs_fs_sb.h>
#include <linux/hfs_fs_i.h>
#include <linux/hfs_fs.h>

#include <linux/config.h> /* for CONFIG_MAC_PARTITION */
#include <linux/blkdev.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/parser.h>
#include <linux/smp_lock.h>
#include <linux/vfs.h>

MODULE_LICENSE("GPL");

/*================ Forward declarations ================*/

static void hfs_read_inode(struct inode *);
static void hfs_put_super(struct super_block *);
static int hfs_statfs(struct super_block *, struct kstatfs *);
static void hfs_write_super(struct super_block *);

static kmem_cache_t * hfs_inode_cachep;

static struct inode *hfs_alloc_inode(struct super_block *sb)
{
	struct hfs_inode_info *ei;
	ei = (struct hfs_inode_info *)kmem_cache_alloc(hfs_inode_cachep, SLAB_KERNEL);
	if (!ei)
		return NULL;
	return &ei->vfs_inode;
}

static void hfs_destroy_inode(struct inode *inode)
{
	kmem_cache_free(hfs_inode_cachep, HFS_I(inode));
}

static void init_once(void * foo, kmem_cache_t * cachep, unsigned long flags)
{
	struct hfs_inode_info *ei = (struct hfs_inode_info *) foo;

	if ((flags & (SLAB_CTOR_VERIFY|SLAB_CTOR_CONSTRUCTOR)) ==
	    SLAB_CTOR_CONSTRUCTOR)
		inode_init_once(&ei->vfs_inode);
}
 
static int init_inodecache(void)
{
	hfs_inode_cachep = kmem_cache_create("hfs_inode_cache",
					     sizeof(struct hfs_inode_info),
					     0, SLAB_HWCACHE_ALIGN|SLAB_RECLAIM_ACCOUNT,
					     init_once, NULL);
	if (hfs_inode_cachep == NULL)
		return -ENOMEM;
	return 0;
}

static void destroy_inodecache(void)
{
	if (kmem_cache_destroy(hfs_inode_cachep))
		printk(KERN_INFO "hfs_inode_cache: not all structures were freed\n");
}

/*================ Global variables ================*/

static struct super_operations hfs_super_operations = { 
	.alloc_inode	= hfs_alloc_inode,
	.destroy_inode	= hfs_destroy_inode,
	.read_inode	= hfs_read_inode,
	.put_inode	= hfs_put_inode,
	.put_super	= hfs_put_super,
	.write_super	= hfs_write_super,
	.statfs		= hfs_statfs,
};

/*================ File-local variables ================*/

static struct super_block *hfs_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	return get_sb_bdev(fs_type, flags, dev_name, data, hfs_fill_super);
}

static struct file_system_type hfs_fs = {
	.owner		= THIS_MODULE,
	.name		= "hfs",
	.get_sb		= hfs_get_sb,
	.kill_sb	= kill_block_super,
	.fs_flags	= FS_REQUIRES_DEV,
};

/*================ File-local functions ================*/

/* 
 * hfs_read_inode()
 *
 * this doesn't actually do much. hfs_iget actually fills in the 
 * necessary inode information.
 */
static void hfs_read_inode(struct inode *inode)
{
  inode->i_mode = 0;
}

/*
 * hfs_write_super()
 *
 * Description:
 *   This function is called by the VFS only. When the filesystem
 *   is mounted r/w it updates the MDB on disk.
 * Input Variable(s):
 *   struct super_block *sb: Pointer to the hfs superblock
 * Output Variable(s):
 *   NONE
 * Returns:
 *   void
 * Preconditions:
 *   'sb' points to a "valid" (struct super_block).
 * Postconditions:
 *   The MDB is marked 'unsuccessfully unmounted' by clearing bit 8 of drAtrb
 *   (hfs_put_super() must set this flag!). Some MDB fields are updated
 *   and the MDB buffer is written to disk by calling hfs_mdb_commit().
 */
static void hfs_write_super(struct super_block *sb)
{
	struct hfs_mdb *mdb = HFS_SB(sb)->s_mdb;
	lock_kernel();
	/* is this a valid hfs superblock? */
	if (!sb || sb->s_magic != HFS_SUPER_MAGIC) {
		unlock_kernel();
		return;
	}

	if (!(sb->s_flags & MS_RDONLY)) {
		/* sync everything to the buffers */
		hfs_mdb_commit(mdb, 0);
	}
	sb->s_dirt = 0;
	unlock_kernel();
}

/*
 * hfs_put_super()
 *
 * This is the put_super() entry in the super_operations structure for
 * HFS filesystems.  The purpose is to release the resources
 * associated with the superblock sb.
 */
static void hfs_put_super(struct super_block *sb)
{
	struct hfs_mdb *mdb = HFS_SB(sb)->s_mdb;
 
	if (!(sb->s_flags & MS_RDONLY)) {
		hfs_mdb_commit(mdb, 0);
		sb->s_dirt = 0;
	}

	/* release the MDB's resources */
	hfs_mdb_put(mdb, sb->s_flags & MS_RDONLY);

	kfree(sb->s_fs_info);
	sb->s_fs_info = NULL;
}

/*
 * hfs_statfs()
 *
 * This is the statfs() entry in the super_operations structure for
 * HFS filesystems.  The purpose is to return various data about the
 * filesystem.
 *
 * changed f_files/f_ffree to reflect the fs_ablock/free_ablocks.
 */
static int hfs_statfs(struct super_block *sb, struct kstatfs *buf)
{
	struct hfs_mdb *mdb = HFS_SB(sb)->s_mdb;

	buf->f_type = HFS_SUPER_MAGIC;
	buf->f_bsize = HFS_SECTOR_SIZE;
	buf->f_blocks = mdb->alloc_blksz * mdb->fs_ablocks;
	buf->f_bfree = mdb->alloc_blksz * mdb->free_ablocks;
	buf->f_bavail = buf->f_bfree;
	buf->f_files = mdb->fs_ablocks;  
	buf->f_ffree = mdb->free_ablocks;
	buf->f_namelen = HFS_NAMELEN;

	return 0;
}

enum {
	Opt_version, Opt_uid, Opt_gid, Opt_umask, Opt_part,
	Opt_type, Opt_creator, Opt_quiet, Opt_afpd,
	Opt_names_netatalk, Opt_names_trivial, Opt_names_alpha, Opt_names_latin,
	Opt_names_7bit, Opt_names_8bit, Opt_names_cap,
	Opt_fork_netatalk, Opt_fork_single, Opt_fork_double, Opt_fork_cap,
	Opt_case_lower, Opt_case_asis,
	Opt_conv_binary, Opt_conv_text, Opt_conv_auto,
};

static match_table_t tokens = {
	{Opt_version, "version=%u"},
	{Opt_uid, "uid=%u"},
	{Opt_gid, "gid=%u"},
	{Opt_umask, "umask=%o"},
	{Opt_part, "part=%u"},
	{Opt_type, "type=%s"},
	{Opt_creator, "creator=%s"},
	{Opt_quiet, "quiet"},
	{Opt_afpd, "afpd"},
	{Opt_names_netatalk, "names=netatalk"},
	{Opt_names_trivial, "names=trivial"},
	{Opt_names_alpha, "names=alpha"},
	{Opt_names_latin, "names=latin"},
	{Opt_names_7bit, "names=7bit"},
	{Opt_names_8bit, "names=8bit"},
	{Opt_names_cap, "names=cap"},
	{Opt_names_netatalk, "names=n"},
	{Opt_names_trivial, "names=t"},
	{Opt_names_alpha, "names=a"},
	{Opt_names_latin, "names=l"},
	{Opt_names_7bit, "names=7"},
	{Opt_names_8bit, "names=8"},
	{Opt_names_cap, "names=c"},
	{Opt_fork_netatalk, "fork=netatalk"},
	{Opt_fork_single, "fork=single"},
	{Opt_fork_double, "fork=double"},
	{Opt_fork_cap, "fork=cap"},
	{Opt_fork_netatalk, "fork=n"},
	{Opt_fork_single, "fork=s"},
	{Opt_fork_double, "fork=d"},
	{Opt_fork_cap, "fork=c"},
	{Opt_case_lower, "case=lower"},
	{Opt_case_asis, "case=asis"},
	{Opt_case_lower, "case=l"},
	{Opt_case_asis, "case=a"},
	{Opt_conv_binary, "conv=binary"},
	{Opt_conv_text, "conv=text"},
	{Opt_conv_auto, "conv=auto"},
	{Opt_conv_binary, "conv=b"},
	{Opt_conv_text, "conv=t"},
	{Opt_conv_auto, "conv=a"},
};

/*
 * parse_options()
 * 
 * adapted from linux/fs/msdos/inode.c written 1992,93 by Werner Almesberger
 * This function is called by hfs_read_super() to parse the mount options.
 */
static int parse_options(char *options, struct hfs_sb_info *hsb, int *part)
{
	char *p;
	char names, fork;
	substring_t args[MAX_OPT_ARGS];
	int option;

	/* initialize the sb with defaults */
	memset(hsb, 0, sizeof(*hsb));
	hsb->magic = HFS_SB_MAGIC;
	hsb->s_uid   = current->uid;
	hsb->s_gid   = current->gid;
	hsb->s_umask = current->fs->umask;
	hsb->s_type    = 0x3f3f3f3f;	/* == '????' */
	hsb->s_creator = 0x3f3f3f3f;	/* == '????' */
	hsb->s_lowercase = 0;
	hsb->s_quiet     = 0;
	hsb->s_afpd      = 0;
        /* default version. 0 just selects the defaults */
	hsb->s_version   = 0; 
	hsb->s_conv = 'b';
	names = '?';
	fork = '?';
	*part = 0;

	if (!options) {
		goto done;
	}
	while ((p = strsep(&options,",")) != NULL) {
		int token;
		if (!*p)
			continue;

		token = match_token(p, tokens, args);
		switch (token) {
		/* Numeric-valued options */
		case Opt_version:
			if (match_int(&args[0], &option))
				return 0;
			hsb->s_version = option;
			break;
		case Opt_uid:
			if (match_int(&args[0], &option))
				return 0;
			hsb->s_uid = option;
			break;
		case Opt_gid:
			if (match_int(&args[0], &option))
				return 0;
			hsb->s_gid = option;
			break;
		case Opt_umask:
			if (match_octal(&args[0], &option))
				return 0;
			hsb->s_umask = option;
			break;
		case Opt_part:
			if (match_int(&args[0], &option))
				return 0;
			*part = option;
			break;
		/* String-valued options */
		case Opt_type:
			if (strlen(args[0].from) != 4) {
				return 0;
			}
			hsb->s_type = hfs_get_nl(args[0].from);
			break;
		case Opt_creator:
			if (strlen(args[0].from) != 4) {
				return 0;
			}
			hsb->s_creator = hfs_get_nl(args[0].from);
			break;
		/* Boolean-valued options */
		case Opt_quiet:
			hsb->s_quiet = 1;
			break;
		case Opt_afpd:
			hsb->s_afpd = 1;
			break;
		/* Multiple choice options */
		case Opt_names_netatalk:
			names = 'n';
			break;
		case Opt_names_trivial:
			names = 't';
			break;
		case Opt_names_alpha:
			names = 'a';
			break;
		case Opt_names_latin:
			names = 'l';
			break;
		case Opt_names_7bit:
			names = '7';
			break;
		case Opt_names_8bit:
			names = '8';
			break;
		case Opt_names_cap:
			names = 'c';
			break;
		case Opt_fork_netatalk:
			fork = 'n';
			break;
		case Opt_fork_single:
			fork = 's';
			break;
		case Opt_fork_double:
			fork = 'd';
			break;
		case Opt_fork_cap:
			fork = 'c';
			break;
		case Opt_case_lower:
			hsb->s_lowercase = 1;
			break;
		case Opt_case_asis:
			hsb->s_lowercase = 0;
			break;
		case Opt_conv_binary:
			hsb->s_conv = 'b';
			break;
		case Opt_conv_text:
			hsb->s_conv = 't';
			break;
		case Opt_conv_auto:
			hsb->s_conv = 'a';
			break;
		default:
			return 0;
		}
	}

done:
	/* Parse the "fork" and "names" options */
	if (fork == '?') {
		fork = hsb->s_afpd ? 'n' : 'c';
	}
	switch (fork) {
	default:
	case 'c':
		hsb->s_ifill = hfs_cap_ifill;
		hsb->s_reserved1 = hfs_cap_reserved1;
		hsb->s_reserved2 = hfs_cap_reserved2;
		break;

	case 's':
		hfs_warn("hfs_fs: AppleSingle not yet implemented.\n");
		return 0;
		/* break; */
	
	case 'd':
		hsb->s_ifill = hfs_dbl_ifill;
		hsb->s_reserved1 = hfs_dbl_reserved1;
		hsb->s_reserved2 = hfs_dbl_reserved2;
		break;

	case 'n':
		hsb->s_ifill = hfs_nat_ifill;
		hsb->s_reserved1 = hfs_nat_reserved1;
		hsb->s_reserved2 = hfs_nat_reserved2;
		break;
	}

	if (names == '?') {
		names = fork;
	}
	switch (names) {
	default:
	case 'n':
		hsb->s_nameout = hfs_colon2mac;
		hsb->s_namein = hfs_mac2nat;
		break;

	case 'c':
		hsb->s_nameout = hfs_colon2mac;
		hsb->s_namein = hfs_mac2cap;
		break;

	case 't':
		hsb->s_nameout = hfs_triv2mac;
		hsb->s_namein = hfs_mac2triv;
		break;

	case '7':
		hsb->s_nameout = hfs_prcnt2mac;
		hsb->s_namein = hfs_mac2seven;
		break;

	case '8':
		hsb->s_nameout = hfs_prcnt2mac;
		hsb->s_namein = hfs_mac2eight;
		break;

	case 'l':
		hsb->s_nameout = hfs_latin2mac;
		hsb->s_namein = hfs_mac2latin;
		break;

 	case 'a':	/* 's' and 'd' are unadvertised aliases for 'alpha', */
 	case 's':	/* since 'alpha' is the default if fork=s or fork=d. */
 	case 'd':	/* (It is also helpful for poor typists!)           */
		hsb->s_nameout = hfs_prcnt2mac;
		hsb->s_namein = hfs_mac2alpha;
		break;
	}

	return 1;
}

/*================ Global functions ================*/

/*
 * hfs_read_super()
 *
 * This is the function that is responsible for mounting an HFS
 * filesystem.	It performs all the tasks necessary to get enough data
 * from the disk to read the root inode.  This includes parsing the
 * mount options, dealing with Macintosh partitions, reading the
 * superblock and the allocation bitmap blocks, calling
 * hfs_btree_init() to get the necessary data about the extents and
 * catalog B-trees and, finally, reading the root inode into memory.
 */
int hfs_fill_super(struct super_block *s, void *data, int silent)
{
	struct hfs_sb_info *sbi;
	struct hfs_mdb *mdb;
	struct hfs_cat_key key;
	hfs_s32 part_size, part_start;
	struct inode *root_inode;
	int part;

	sbi = kmalloc(sizeof(struct hfs_sb_info), GFP_KERNEL);
	if (!sbi)
		return -ENOMEM;
	s->s_fs_info = sbi;
	memset(sbi, 0, sizeof(struct hfs_sb_info));

	if (!parse_options((char *)data, sbi, &part)) {
		hfs_warn("hfs_fs: unable to parse mount options.\n");
		goto bail2;
	}

	/* set the device driver to 512-byte blocks */
	sb_set_blocksize(s, HFS_SECTOR_SIZE);

#ifdef CONFIG_MAC_PARTITION
	/* check to see if we're in a partition */
	mdb = hfs_mdb_get(s, s->s_flags & MS_RDONLY, 0);

	/* erk. try parsing the partition table ourselves */
	if (!mdb) {
		if (hfs_part_find(s, part, silent, &part_size, &part_start)) {
	    		goto bail2;
	  	}
	  	mdb = hfs_mdb_get(s, s->s_flags & MS_RDONLY, part_start);
	}
#else
	if (hfs_part_find(s, part, silent, &part_size, &part_start)) {
		goto bail2;
	}

	mdb = hfs_mdb_get(s, s->s_flags & MS_RDONLY, part_start);
#endif

	if (!mdb) {
		if (!silent) {
			hfs_warn("VFS: Can't find a HFS filesystem on dev %s.\n",
			       s->s_id);
		}
		goto bail2;
	}

	sbi->s_mdb = mdb;
	if (HFS_ITYPE(mdb->next_id) != 0) {
		hfs_warn("hfs_fs: too many files.\n");
		goto bail1;
	}

	s->s_magic = HFS_SUPER_MAGIC;
	s->s_op = &hfs_super_operations;

	/* try to get the root inode */
	hfs_cat_build_key(htonl(HFS_POR_CNID),
			  (struct hfs_name *)(mdb->vname), &key);

	root_inode = hfs_iget(hfs_cat_get(mdb, &key), HFS_ITYPE_NORM, NULL);
	if (!root_inode) 
		goto bail_no_root;
	  
	s->s_root = d_alloc_root(root_inode);
	if (!s->s_root) 
		goto bail_no_root;

	/* fix up pointers. */
	HFS_I(root_inode)->entry->sys_entry[HFS_ITYPE_TO_INT(HFS_ITYPE_NORM)] =
	  s->s_root;
	s->s_root->d_op = &hfs_dentry_operations;

	/* everything's okay */
	return 0;

bail_no_root: 
	hfs_warn("hfs_fs: get root inode failed.\n");
	iput(root_inode);
bail1:
	hfs_mdb_put(mdb, s->s_flags & MS_RDONLY);
bail2:
	kfree(sbi);
	s->s_fs_info = NULL;
	return -EINVAL;	
}

static int __init init_hfs_fs(void)
{
	int err = init_inodecache();
	if (err)
		goto out1;
        hfs_cat_init();
	err = register_filesystem(&hfs_fs);
	if (err)
		goto out;
	return 0;
out:
	hfs_cat_free();
	destroy_inodecache();
out1:
	return err;
}

static void __exit exit_hfs_fs(void) {
	hfs_cat_free();
	unregister_filesystem(&hfs_fs);
	destroy_inodecache();
}

module_init(init_hfs_fs)
module_exit(exit_hfs_fs)

#if defined(DEBUG_ALL) || defined(DEBUG_MEM)
long int hfs_alloc = 0;
#endif
