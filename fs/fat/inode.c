/*
 *  linux/fs/fat/inode.c
 *
 *  Written 1992,1993 by Werner Almesberger
 *  VFAT extensions by Gordon Chaffee, merged with msdos fs by Henrik Storner
 *  Rewritten for the constant inumbers support by Al Viro
 *
 *  Fixes:
 *
 *  	Max Cohan: Fixed invalid FSINFO offset when info_sector is 0
 */

#include <linux/module.h>
#include <linux/time.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/seq_file.h>
#include <linux/msdos_fs.h>
#include <linux/pagemap.h>
#include <linux/buffer_head.h>
#include <linux/mount.h>
#include <linux/vfs.h>
#include <asm/unaligned.h>

/*
 * New FAT inode stuff. We do the following:
 *	a) i_ino is constant and has nothing with on-disk location.
 *	b) FAT manages its own cache of directory entries.
 *	c) *This* cache is indexed by on-disk location.
 *	d) inode has an associated directory entry, all right, but
 *		it may be unhashed.
 *	e) currently entries are stored within struct inode. That should
 *		change.
 *	f) we deal with races in the following way:
 *		1. readdir() and lookup() do FAT-dir-cache lookup.
 *		2. rename() unhashes the F-d-c entry and rehashes it in
 *			a new place.
 *		3. unlink() and rmdir() unhash F-d-c entry.
 *		4. fat_write_inode() checks whether the thing is unhashed.
 *			If it is we silently return. If it isn't we do bread(),
 *			check if the location is still valid and retry if it
 *			isn't. Otherwise we do changes.
 *		5. Spinlock is used to protect hash/unhash/location check/lookup
 *		6. fat_clear_inode() unhashes the F-d-c entry.
 *		7. lookup() and readdir() do igrab() if they find a F-d-c entry
 *			and consider negative result as cache miss.
 */

#define FAT_HASH_BITS	8
#define FAT_HASH_SIZE	(1UL << FAT_HASH_BITS)
#define FAT_HASH_MASK	(FAT_HASH_SIZE-1)
static struct list_head fat_inode_hashtable[FAT_HASH_SIZE];
spinlock_t fat_inode_lock = SPIN_LOCK_UNLOCKED;

void fat_hash_init(void)
{
	int i;
	for(i = 0; i < FAT_HASH_SIZE; i++) {
		INIT_LIST_HEAD(&fat_inode_hashtable[i]);
	}
}

static inline unsigned long fat_hash(struct super_block *sb, int i_pos)
{
	unsigned long tmp = (unsigned long)i_pos | (unsigned long) sb;
	tmp = tmp + (tmp >> FAT_HASH_BITS) + (tmp >> FAT_HASH_BITS * 2);
	return tmp & FAT_HASH_MASK;
}

void fat_attach(struct inode *inode, int i_pos)
{
	spin_lock(&fat_inode_lock);
	MSDOS_I(inode)->i_location = i_pos;
	list_add(&MSDOS_I(inode)->i_fat_hash,
		fat_inode_hashtable + fat_hash(inode->i_sb, i_pos));
	spin_unlock(&fat_inode_lock);
}

void fat_detach(struct inode *inode)
{
	spin_lock(&fat_inode_lock);
	MSDOS_I(inode)->i_location = 0;
	list_del_init(&MSDOS_I(inode)->i_fat_hash);
	spin_unlock(&fat_inode_lock);
}

struct inode *fat_iget(struct super_block *sb, int i_pos)
{
	struct list_head *p = fat_inode_hashtable + fat_hash(sb, i_pos);
	struct list_head *walk;
	struct msdos_inode_info *i;
	struct inode *inode = NULL;

	spin_lock(&fat_inode_lock);
	list_for_each(walk, p) {
		i = list_entry(walk, struct msdos_inode_info, i_fat_hash);
		if (i->vfs_inode.i_sb != sb)
			continue;
		if (i->i_location != i_pos)
			continue;
		inode = igrab(&i->vfs_inode);
		if (inode)
			break;
	}
	spin_unlock(&fat_inode_lock);
	return inode;
}

static int fat_fill_inode(struct inode *inode, struct msdos_dir_entry *de);

struct inode *fat_build_inode(struct super_block *sb,
				struct msdos_dir_entry *de, int ino, int *res)
{
	struct inode *inode;
	*res = 0;
	inode = fat_iget(sb, ino);
	if (inode)
		goto out;
	inode = new_inode(sb);
	*res = -ENOMEM;
	if (!inode)
		goto out;
	inode->i_ino = iunique(sb, MSDOS_ROOT_INO);
	inode->i_version = 1;
	*res = fat_fill_inode(inode, de);
	if (*res < 0) {
		iput(inode);
		inode = NULL;
		goto out;
	}
	fat_attach(inode, ino);
	insert_inode_hash(inode);
out:
	return inode;
}

void fat_delete_inode(struct inode *inode)
{
	if (!is_bad_inode(inode)) {
		inode->i_size = 0;
		fat_truncate(inode);
	}
	clear_inode(inode);
}

void fat_clear_inode(struct inode *inode)
{
	if (is_bad_inode(inode))
		return;
	lock_kernel();
	spin_lock(&fat_inode_lock);
	fat_cache_inval_inode(inode);
	list_del_init(&MSDOS_I(inode)->i_fat_hash);
	spin_unlock(&fat_inode_lock);
	unlock_kernel();
}

void fat_put_super(struct super_block *sb)
{
	struct msdos_sb_info *sbi = MSDOS_SB(sb);

	fat_clusters_flush(sb);
	fat_cache_inval_dev(sb);
	if (sbi->nls_disk) {
		unload_nls(sbi->nls_disk);
		sbi->nls_disk = NULL;
		sbi->options.codepage = 0;
	}
	if (sbi->nls_io) {
		unload_nls(sbi->nls_io);
		sbi->nls_io = NULL;
	}
	/*
	 * Note: the iocharset option might have been specified
	 * without enabling nls_io, so check for it here.
	 */
	if (sbi->options.iocharset) {
		kfree(sbi->options.iocharset);
		sbi->options.iocharset = NULL;
	}
	sb->s_fs_info = NULL;
	kfree(sbi);
}

static int simple_getbool(char *s, int *setval)
{
	if (s) {
		if (!strcmp(s,"1") || !strcmp(s,"yes") || !strcmp(s,"true"))
			*setval = 1;
		else if (!strcmp(s,"0") || !strcmp(s,"no") || !strcmp(s,"false"))
			*setval = 0;
		else
			return 0;
	} else
		*setval = 1;
	return 1;
}

static int fat_show_options(struct seq_file *m, struct vfsmount *mnt)
{
	struct msdos_sb_info *sbi = MSDOS_SB(mnt->mnt_sb);
	struct fat_mount_options *opts = &sbi->options;
	int isvfat = opts->isvfat;

	if (opts->fs_uid != 0)
		seq_printf(m, ",uid=%d", opts->fs_uid);
	if (opts->fs_gid != 0)
		seq_printf(m, ",gid=%d", opts->fs_gid);
	seq_printf(m, ",fmask=%04o", opts->fs_fmask);
	seq_printf(m, ",dmask=%04o", opts->fs_dmask);
	if (sbi->nls_disk)
		seq_printf(m, ",codepage=%s", sbi->nls_disk->charset);
	if (isvfat) {
		if (sbi->nls_io
		    && strcmp(sbi->nls_io->charset, CONFIG_NLS_DEFAULT))
			seq_printf(m, ",iocharset=%s", sbi->nls_io->charset);

		switch (opts->shortname) {
		case VFAT_SFN_DISPLAY_WIN95 | VFAT_SFN_CREATE_WIN95:
			seq_puts(m, ",shortname=win95");
			break;
		case VFAT_SFN_DISPLAY_WINNT | VFAT_SFN_CREATE_WINNT:
			seq_puts(m, ",shortname=winnt");
			break;
		case VFAT_SFN_DISPLAY_WINNT | VFAT_SFN_CREATE_WIN95:
			seq_puts(m, ",shortname=mixed");
			break;
		case VFAT_SFN_DISPLAY_LOWER | VFAT_SFN_CREATE_WIN95:
			/* seq_puts(m, ",shortname=lower"); */
			break;
		default:
			seq_puts(m, ",shortname=unknown");
			break;
		}
	}
	if (opts->name_check != 'n')
		seq_printf(m, ",check=%c", opts->name_check);
	if (opts->quiet)
		seq_puts(m, ",quiet");
	if (opts->showexec)
		seq_puts(m, ",showexec");
	if (opts->sys_immutable)
		seq_puts(m, ",sys_immutable");
	if (!isvfat) {
		if (opts->dotsOK)
			seq_puts(m, ",dotsOK=yes");
		if (opts->nocase)
			seq_puts(m, ",nocase");
	} else {
		if (opts->utf8)
			seq_puts(m, ",utf8");
		if (opts->unicode_xlate)
			seq_puts(m, ",uni_xlate");
		if (!opts->numtail)
			seq_puts(m, ",nonumtail");
	}

	return 0;
}

static int parse_options(char *options, int is_vfat, int *debug,
			 struct fat_mount_options *opts)
{
	char *this_char, *value, *p;
	int ret = 1, val, len;

	opts->isvfat = is_vfat;

	opts->fs_uid = current->uid;
	opts->fs_gid = current->gid;
	opts->fs_fmask = opts->fs_dmask = current->fs->umask;
	opts->codepage = 0;
	opts->iocharset = NULL;
	if (is_vfat)
		opts->shortname = VFAT_SFN_DISPLAY_LOWER|VFAT_SFN_CREATE_WIN95;
	else
		opts->shortname = 0;
	opts->name_check = 'n';
	opts->quiet = opts->showexec = opts->sys_immutable = opts->dotsOK =  0;
	opts->utf8 = opts->unicode_xlate = 0;
	opts->numtail = 1;
	opts->nocase = 0;
	*debug = 0;

	if (!options)
		goto out;
	while ((this_char = strsep(&options,",")) != NULL) {
		if (!*this_char)
			continue;
		if ((value = strchr(this_char,'=')) != NULL)
			*value++ = 0;

		if (!strcmp(this_char,"check") && value) {
			if (value[0] && !value[1] && strchr("rns",*value))
				opts->name_check = *value;
			else if (!strcmp(value,"relaxed"))
				opts->name_check = 'r';
			else if (!strcmp(value,"normal"))
				opts->name_check = 'n';
			else if (!strcmp(value,"strict"))
				opts->name_check = 's';
			else ret = 0;
		}
		else if (!strcmp(this_char,"conv") && value) {
			printk(KERN_INFO "FAT: conv option is obsolete, "
			       "not supported now\n");
		}
		else if (!strcmp(this_char,"nocase")) {
			if (!is_vfat)
				opts->nocase = 1;
			else {
				/* for backward compatible */
				opts->shortname = VFAT_SFN_DISPLAY_WIN95
					| VFAT_SFN_CREATE_WIN95;
			}
		}
		else if (!strcmp(this_char,"showexec")) {
			opts->showexec = 1;
		}
		else if (!strcmp(this_char,"uid")) {
			if (!value || !*value) ret = 0;
			else {
				opts->fs_uid = simple_strtoul(value,&value,0);
				if (*value) ret = 0;
			}
		}
		else if (!strcmp(this_char,"gid")) {
			if (!value || !*value) ret= 0;
			else {
				opts->fs_gid = simple_strtoul(value,&value,0);
				if (*value) ret = 0;
			}
		}
		else if (!strcmp(this_char,"umask")) {
			if (!value || !*value) ret = 0;
			else {
				opts->fs_fmask = opts->fs_dmask =
					simple_strtoul(value,&value,8);
				if (*value) ret = 0;
			}
		}
		else if (!strcmp(this_char,"fmask")) {
			if (!value || !*value) ret = 0;
			else {
				opts->fs_fmask = simple_strtoul(value,&value,8);
				if (*value) ret = 0;
			}
		}
		else if (!strcmp(this_char,"dmask")) {
			if (!value || !*value) ret = 0;
			else {
				opts->fs_dmask = simple_strtoul(value,&value,8);
				if (*value) ret = 0;
			}
		}
		else if (!strcmp(this_char,"debug")) {
			if (value) ret = 0;
			else *debug = 1;
		}
		else if (!strcmp(this_char,"fat")) {
			printk(KERN_INFO "FAT: fat option is obsolete, "
			       "not supported now\n");
		}
		else if (!strcmp(this_char,"quiet")) {
			if (value) ret = 0;
			else opts->quiet = 1;
		}
		else if (!strcmp(this_char,"blocksize")) {
			printk(KERN_INFO "FAT: blocksize option is obsolete, "
			       "not supported now\n");
		}
		else if (!strcmp(this_char,"sys_immutable")) {
			if (value) ret = 0;
			else opts->sys_immutable = 1;
		}
		else if (!strcmp(this_char,"codepage") && value) {
			opts->codepage = simple_strtoul(value,&value,0);
			if (*value) ret = 0;
		}

		/* msdos specific */
		else if (!is_vfat && !strcmp(this_char,"dots")) {
			opts->dotsOK = 1;
		}
		else if (!is_vfat && !strcmp(this_char,"nodots")) {
			opts->dotsOK = 0;
		}
		else if (!is_vfat && !strcmp(this_char,"dotsOK") && value) {
			if (!strcmp(value,"yes")) opts->dotsOK = 1;
			else if (!strcmp(value,"no")) opts->dotsOK = 0;
			else ret = 0;
		}

		/* vfat specific */
		else if (is_vfat && !strcmp(this_char,"iocharset") && value) {
			p = value;
			while (*value && *value != ',')
				value++;
			len = value - p;
			if (len) {
				char *buffer;

				if (opts->iocharset != NULL) {
					kfree(opts->iocharset);
					opts->iocharset = NULL;
				}
				buffer = kmalloc(len + 1, GFP_KERNEL);
				if (buffer != NULL) {
					opts->iocharset = buffer;
					memcpy(buffer, p, len);
					buffer[len] = 0;
				} else
					ret = 0;
			}
		}
		else if (is_vfat && !strcmp(this_char,"utf8")) {
			ret = simple_getbool(value, &val);
			if (ret) opts->utf8 = val;
		}
		else if (is_vfat && !strcmp(this_char,"uni_xlate")) {
			ret = simple_getbool(value, &val);
			if (ret) opts->unicode_xlate = val;
		}
		else if (is_vfat && !strcmp(this_char,"posix")) {
			printk(KERN_INFO "FAT: posix option is obsolete, "
			       "not supported now\n");
		}
		else if (is_vfat && !strcmp(this_char,"nonumtail")) {
			ret = simple_getbool(value, &val);
			if (ret) {
				opts->numtail = !val;
			}
		}
		else if (is_vfat && !strcmp(this_char, "shortname")) {
			if (!strcmp(value, "lower"))
				opts->shortname = VFAT_SFN_DISPLAY_LOWER
						| VFAT_SFN_CREATE_WIN95;
			else if (!strcmp(value, "win95"))
				opts->shortname = VFAT_SFN_DISPLAY_WIN95
						| VFAT_SFN_CREATE_WIN95;
			else if (!strcmp(value, "winnt"))
				opts->shortname = VFAT_SFN_DISPLAY_WINNT
						| VFAT_SFN_CREATE_WINNT;
			else if (!strcmp(value, "mixed"))
				opts->shortname = VFAT_SFN_DISPLAY_WINNT
						| VFAT_SFN_CREATE_WIN95;
			else
				ret = 0;
		} else {
			printk(KERN_ERR "FAT: Unrecognized mount option %s\n",
			       this_char);
			ret = 0;
		}

		if (ret == 0)
			break;
	}
out:
	if (opts->unicode_xlate)
		opts->utf8 = 0;
	
	return ret;
}

static int fat_calc_dir_size(struct inode *inode)
{
	struct super_block *sb = inode->i_sb;
	int nr;

	inode->i_size = 0;
	if (MSDOS_I(inode)->i_start == 0)
		return 0;

	nr = MSDOS_I(inode)->i_start;
	do {
		inode->i_size += 1 << MSDOS_SB(sb)->cluster_bits;
		nr = fat_access(sb, nr, -1);
		if (nr < 0)
			return nr;
		else if (nr == FAT_ENT_FREE) {
			fat_fs_panic(sb, "Directory %lu: invalid cluster chain",
				     inode->i_ino);
			return -EIO;
		}
		if (inode->i_size > FAT_MAX_DIR_SIZE) {
			fat_fs_panic(sb, "Directory %lu: "
				     "exceeded the maximum size of directory",
				     inode->i_ino);
			inode->i_size = FAT_MAX_DIR_SIZE;
			break;
		}
	} while (nr != FAT_ENT_EOF);

	return 0;
}

static int fat_read_root(struct inode *inode)
{
	struct super_block *sb = inode->i_sb;
	struct msdos_sb_info *sbi = MSDOS_SB(sb);
	int error;

	MSDOS_I(inode)->i_location = 0;
	inode->i_uid = sbi->options.fs_uid;
	inode->i_gid = sbi->options.fs_gid;
	inode->i_version++;
	inode->i_generation = 0;
	inode->i_mode = (S_IRWXUGO & ~sbi->options.fs_dmask) | S_IFDIR;
	inode->i_op = sbi->dir_ops;
	inode->i_fop = &fat_dir_operations;
	if (sbi->fat_bits == 32) {
		MSDOS_I(inode)->i_start = sbi->root_cluster;
		error = fat_calc_dir_size(inode);
		if (error < 0)
			return error;
	} else {
		MSDOS_I(inode)->i_start = 0;
		inode->i_size = sbi->dir_entries * sizeof(struct msdos_dir_entry);
	}
	inode->i_blksize = 1 << sbi->cluster_bits;
	inode->i_blocks = ((inode->i_size + inode->i_blksize - 1)
			   & ~((loff_t)inode->i_blksize - 1)) >> 9;
	MSDOS_I(inode)->i_logstart = 0;
	MSDOS_I(inode)->mmu_private = inode->i_size;

	MSDOS_I(inode)->i_attrs = 0;
	inode->i_mtime.tv_sec = inode->i_atime.tv_sec = inode->i_ctime.tv_sec = 0;
	inode->i_mtime.tv_nsec = inode->i_atime.tv_nsec = inode->i_ctime.tv_nsec = 0;
	MSDOS_I(inode)->i_ctime_ms = 0;
	inode->i_nlink = fat_subdirs(inode)+2;

	return 0;
}

/*
 * a FAT file handle with fhtype 3 is
 *  0/  i_ino - for fast, reliable lookup if still in the cache
 *  1/  i_generation - to see if i_ino is still valid
 *          bit 0 == 0 iff directory
 *  2/  i_location - if ino has changed, but still in cache
 *  3/  i_logstart - to semi-verify inode found at i_location
 *  4/  parent->i_logstart - maybe used to hunt for the file on disc
 *
 */

struct dentry *fat_decode_fh(struct super_block *sb, __u32 *fh,
			     int len, int fhtype, 
			     int (*acceptable)(void *context, struct dentry *de),
			     void *context)
{

	if (fhtype != 3)
		return ERR_PTR(-ESTALE);
	if (len < 5)
		return ERR_PTR(-ESTALE);

	return sb->s_export_op->find_exported_dentry(sb, fh, NULL, acceptable, context);
}

struct dentry *fat_get_dentry(struct super_block *sb, void *inump)
{
	struct inode *inode = NULL;
	struct dentry *result;
	__u32 *fh = inump;

	inode = iget(sb, fh[0]);
	if (!inode || is_bad_inode(inode) ||
	    inode->i_generation != fh[1]) {
		if (inode) iput(inode);
		inode = NULL;
	}
	if (!inode) {
		/* try 2 - see if i_location is in F-d-c
		 * require i_logstart to be the same
		 * Will fail if you truncate and then re-write
		 */

		inode = fat_iget(sb, fh[2]);
		if (inode && MSDOS_I(inode)->i_logstart != fh[3]) {
			iput(inode);
			inode = NULL;
		}
	}
	if (!inode) {
		/* For now, do nothing
		 * What we could do is:
		 * follow the file starting at fh[4], and record
		 * the ".." entry, and the name of the fh[2] entry.
		 * The follow the ".." file finding the next step up.
		 * This way we build a path to the root of
		 * the tree. If this works, we lookup the path and so
		 * get this inode into the cache.
		 * Finally try the fat_iget lookup again
		 * If that fails, then weare totally out of luck
		 * But all that is for another day
		 */
	}
	if (!inode)
		return ERR_PTR(-ESTALE);

	
	/* now to find a dentry.
	 * If possible, get a well-connected one
	 */
	result = d_alloc_anon(inode);
	if (result == NULL) {
		iput(inode);
		return ERR_PTR(-ENOMEM);
	}
	result->d_op = sb->s_root->d_op;
	result->d_vfs_flags |= DCACHE_REFERENCED;
	return result;
}

int fat_encode_fh(struct dentry *de, __u32 *fh, int *lenp, int connectable)
{
	int len = *lenp;
	struct inode *inode =  de->d_inode;
	
	if (len < 5)
		return 255; /* no room */
	*lenp = 5;
	fh[0] = inode->i_ino;
	fh[1] = inode->i_generation;
	fh[2] = MSDOS_I(inode)->i_location;
	fh[3] = MSDOS_I(inode)->i_logstart;
	read_lock(&dparent_lock);
	fh[4] = MSDOS_I(de->d_parent->d_inode)->i_logstart;
	read_unlock(&dparent_lock);
	return 3;
}

struct dentry *fat_get_parent(struct dentry *child)
{
	struct buffer_head *bh=NULL;
	struct msdos_dir_entry *de = NULL;
	struct dentry *parent = NULL;
	int res;
	int ino = 0;
	struct inode *inode;

	lock_kernel();
	res = fat_scan(child->d_inode, MSDOS_DOTDOT, &bh, &de, &ino);

	if (res < 0)
		goto out;
	inode = fat_build_inode(child->d_sb, de, ino, &res);
	if (res)
		goto out;
	if (!inode)
		res = -EACCES;
	else {
		parent = d_alloc_anon(inode);
		if (!parent) {
			iput(inode);
			res = -ENOMEM;
		}
	}

 out:
	if(bh)
		brelse(bh);
	unlock_kernel();
	if (res)
		return ERR_PTR(res);
	else
		return parent;
}

static kmem_cache_t *fat_inode_cachep;

static struct inode *fat_alloc_inode(struct super_block *sb)
{
	struct msdos_inode_info *ei;
	ei = (struct msdos_inode_info *)kmem_cache_alloc(fat_inode_cachep, SLAB_KERNEL);
	if (!ei)
		return NULL;
	return &ei->vfs_inode;
}

static void fat_destroy_inode(struct inode *inode)
{
	kmem_cache_free(fat_inode_cachep, MSDOS_I(inode));
}

static void init_once(void * foo, kmem_cache_t * cachep, unsigned long flags)
{
	struct msdos_inode_info *ei = (struct msdos_inode_info *) foo;

	if ((flags & (SLAB_CTOR_VERIFY|SLAB_CTOR_CONSTRUCTOR)) ==
	    SLAB_CTOR_CONSTRUCTOR) {
		INIT_LIST_HEAD(&ei->i_fat_hash);
		inode_init_once(&ei->vfs_inode);
	}
}
 
int __init fat_init_inodecache(void)
{
	fat_inode_cachep = kmem_cache_create("fat_inode_cache",
					     sizeof(struct msdos_inode_info),
					     0, SLAB_HWCACHE_ALIGN,
					     init_once, NULL);
	if (fat_inode_cachep == NULL)
		return -ENOMEM;
	return 0;
}

void __exit fat_destroy_inodecache(void)
{
	if (kmem_cache_destroy(fat_inode_cachep))
		printk(KERN_INFO "fat_inode_cache: not all structures were freed\n");
}

static struct super_operations fat_sops = { 
	.alloc_inode	= fat_alloc_inode,
	.destroy_inode	= fat_destroy_inode,
	.write_inode	= fat_write_inode,
	.delete_inode	= fat_delete_inode,
	.put_super	= fat_put_super,
	.statfs		= fat_statfs,
	.clear_inode	= fat_clear_inode,

	.read_inode	= make_bad_inode,

	.show_options	= fat_show_options,
};

static struct export_operations fat_export_ops = {
	.decode_fh	= fat_decode_fh,
	.encode_fh	= fat_encode_fh,
	.get_dentry	= fat_get_dentry,
	.get_parent	= fat_get_parent,
};

/*
 * Read the super block of an MS-DOS FS.
 */
int fat_fill_super(struct super_block *sb, void *data, int silent,
		   struct inode_operations *fs_dir_inode_ops, int isvfat)
{
	struct inode *root_inode = NULL;
	struct buffer_head *bh;
	struct fat_boot_sector *b;
	struct msdos_sb_info *sbi;
	int logical_sector_size, fat_clusters, debug, cp, first;
	unsigned int total_sectors, rootdir_sectors;
	unsigned char media;
	long error;
	char buf[50];

	sbi = kmalloc(sizeof(struct msdos_sb_info), GFP_KERNEL);
	if (!sbi)
		return -ENOMEM;
	sb->s_fs_info = sbi;
	memset(sbi, 0, sizeof(struct msdos_sb_info));

	sb->s_magic = MSDOS_SUPER_MAGIC;
	sb->s_op = &fat_sops;
	sb->s_export_op = &fat_export_ops;
	sbi->dir_ops = fs_dir_inode_ops;

	error = -EINVAL;
	if (!parse_options(data, isvfat, &debug, &sbi->options))
		goto out_fail;

	fat_cache_init();
	/* set up enough so that it can read an inode */
	init_MUTEX(&sbi->fat_lock);

	error = -EIO;
	sb_min_blocksize(sb, 512);
	bh = sb_bread(sb, 0);
	if (bh == NULL) {
		printk(KERN_ERR "FAT: unable to read boot sector\n");
		goto out_fail;
	}

	b = (struct fat_boot_sector *) bh->b_data;
	if (!b->reserved) {
		if (!silent)
			printk(KERN_ERR "FAT: bogus number of reserved sectors\n");
		brelse(bh);
		goto out_invalid;
	}
	if (!b->fats) {
		if (!silent)
			printk(KERN_ERR "FAT: bogus number of FAT structure\n");
		brelse(bh);
		goto out_invalid;
	}
	if (!b->secs_track) {
		if (!silent)
			printk(KERN_ERR "FAT: bogus sectors-per-track value\n");
		brelse(bh);
		goto out_invalid;
	}
	if (!b->heads) {
		if (!silent)
			printk(KERN_ERR "FAT: bogus number-of-heads value\n");
		brelse(bh);
		goto out_invalid;
	}
	media = b->media;
	if (!FAT_VALID_MEDIA(media)) {
		if (!silent)
			printk(KERN_ERR "FAT: invalid media value (0x%02x)\n",
			       media);
		brelse(bh);
		goto out_invalid;
	}
	logical_sector_size =
		CF_LE_W(get_unaligned((unsigned short *) &b->sector_size));
	if (!logical_sector_size
	    || (logical_sector_size & (logical_sector_size - 1))
	    || (logical_sector_size < 512)
	    || (PAGE_CACHE_SIZE < logical_sector_size)) {
		if (!silent)
			printk(KERN_ERR "FAT: bogus logical sector size %d\n",
			       logical_sector_size);
		brelse(bh);
		goto out_invalid;
	}
	sbi->cluster_size = b->cluster_size;
	if (!sbi->cluster_size
	    || (sbi->cluster_size & (sbi->cluster_size - 1))) {
		if (!silent)
			printk(KERN_ERR "FAT: bogus cluster size %d\n",
			       sbi->cluster_size);
		brelse(bh);
		goto out_invalid;
	}

	if (logical_sector_size < sb->s_blocksize) {
		printk(KERN_ERR "FAT: logical sector size too small for device"
		       " (logical sector size = %d)\n", logical_sector_size);
		brelse(bh);
		goto out_fail;
	}
	if (logical_sector_size > sb->s_blocksize) {
		brelse(bh);

		if (!sb_set_blocksize(sb, logical_sector_size)) {
			printk(KERN_ERR "FAT: unable to set blocksize %d\n",
			       logical_sector_size);
			goto out_fail;
		}
		bh = sb_bread(sb, 0);
		if (bh == NULL) {
			printk(KERN_ERR "FAT: unable to read boot sector"
			       " (logical sector size = %lu)\n",
			       sb->s_blocksize);
			goto out_fail;
		}
		b = (struct fat_boot_sector *) bh->b_data;
	}

	sbi->cluster_bits = ffs(sb->s_blocksize * sbi->cluster_size) - 1;
	sbi->fats = b->fats;
	sbi->fat_bits = 0;		/* Don't know yet */
	sbi->fat_start = CF_LE_W(b->reserved);
	sbi->fat_length = CF_LE_W(b->fat_length);
	sbi->root_cluster = 0;
	sbi->free_clusters = -1;	/* Don't know yet */
	sbi->prev_free = 0;

	if (!sbi->fat_length && b->fat32_length) {
		struct fat_boot_fsinfo *fsinfo;
		struct buffer_head *fsinfo_bh;

		/* Must be FAT32 */
		sbi->fat_bits = 32;
		sbi->fat_length = CF_LE_L(b->fat32_length);
		sbi->root_cluster = CF_LE_L(b->root_cluster);

		sb->s_maxbytes = 0xffffffff;
		
		/* MC - if info_sector is 0, don't multiply by 0 */
		sbi->fsinfo_sector = CF_LE_W(b->info_sector);
		if (sbi->fsinfo_sector == 0)
			sbi->fsinfo_sector = 1;

		fsinfo_bh = sb_bread(sb, sbi->fsinfo_sector);
		if (fsinfo_bh == NULL) {
			printk(KERN_ERR "FAT: bread failed, FSINFO block"
			       " (sector = %lu)\n", sbi->fsinfo_sector);
			brelse(bh);
			goto out_fail;
		}

		fsinfo = (struct fat_boot_fsinfo *)fsinfo_bh->b_data;
		if (!IS_FSINFO(fsinfo)) {
			printk(KERN_WARNING
			       "FAT: Did not find valid FSINFO signature.\n"
			       "     Found signature1 0x%08x signature2 0x%08x"
			       " (sector = %lu)\n",
			       CF_LE_L(fsinfo->signature1),
			       CF_LE_L(fsinfo->signature2),
			       sbi->fsinfo_sector);
		} else {
			sbi->free_clusters = CF_LE_L(fsinfo->free_clusters);
		}

		brelse(fsinfo_bh);
	}

	sbi->dir_per_block = sb->s_blocksize / sizeof(struct msdos_dir_entry);
	sbi->dir_per_block_bits = ffs(sbi->dir_per_block) - 1;

	sbi->dir_start = sbi->fat_start + sbi->fats * sbi->fat_length;
	sbi->dir_entries =
		CF_LE_W(get_unaligned((unsigned short *)&b->dir_entries));
	if (sbi->dir_entries & (sbi->dir_per_block - 1)) {
		printk(KERN_ERR "FAT: bogus directroy-entries per block\n");
		brelse(bh);
		goto out_invalid;
	}

	rootdir_sectors = sbi->dir_entries
		* sizeof(struct msdos_dir_entry) / sb->s_blocksize;
	sbi->data_start = sbi->dir_start + rootdir_sectors;
	total_sectors = CF_LE_W(get_unaligned((unsigned short *)&b->sectors));
	if (total_sectors == 0)
		total_sectors = CF_LE_L(b->total_sect);
	sbi->clusters = (total_sectors - sbi->data_start) / sbi->cluster_size;

	if (sbi->fat_bits != 32)
		sbi->fat_bits = (sbi->clusters > MSDOS_FAT12) ? 16 : 12;

	/* check that FAT table does not overflow */
	fat_clusters = sbi->fat_length * sb->s_blocksize * 8 / sbi->fat_bits;
	if (sbi->clusters > fat_clusters - 2)
		sbi->clusters = fat_clusters - 2;

	brelse(bh);

	/* validity check of FAT */
	first = __fat_access(sb, 0, -1);
	if (first < 0) {
		error = first;
		goto out_fail;
	}
	if (FAT_FIRST_ENT(sb, media) != first
	    && (media != 0xf8 || FAT_FIRST_ENT(sb, 0xfe) != first)) {
		if (!silent) {
			printk(KERN_ERR "FAT: invalid first entry of FAT "
			       "(0x%x != 0x%x)\n",
			       FAT_FIRST_ENT(sb, media), first);
		}
		goto out_invalid;
	}

	error = -EINVAL;
	cp = sbi->options.codepage ? sbi->options.codepage : 437;
	sprintf(buf, "cp%d", cp);
	sbi->nls_disk = load_nls(buf);
	if (!sbi->nls_disk) {
		/* Fail only if explicit charset specified */
		if (sbi->options.codepage != 0) {
			printk(KERN_ERR "FAT: codepage %s not found\n", buf);
			goto out_fail;
		}
		sbi->options.codepage = 0; /* already 0?? */
		sbi->nls_disk = load_nls_default();
	}

	/* FIXME: utf8 is using iocharset for upper/lower conversion */
	if (sbi->options.isvfat) {
		if (sbi->options.iocharset != NULL) {
			sbi->nls_io = load_nls(sbi->options.iocharset);
			if (!sbi->nls_io) {
				printk(KERN_ERR
				       "FAT: IO charset %s not found\n",
				       sbi->options.iocharset);
				goto out_fail;
			}
		} else
			sbi->nls_io = load_nls_default();
	}

	error = -ENOMEM;
	root_inode = new_inode(sb);
	if (!root_inode)
		goto out_fail;
	root_inode->i_ino = MSDOS_ROOT_INO;
	root_inode->i_version = 1;
	error = fat_read_root(root_inode);
	if (error < 0)
		goto out_fail;
	error = -ENOMEM;
	insert_inode_hash(root_inode);
	sb->s_root = d_alloc_root(root_inode);
	if (!sb->s_root) {
		printk(KERN_ERR "FAT: get root inode failed\n");
		goto out_fail;
	}

	return 0;

out_invalid:
	error = -EINVAL;
	if (!silent)
		printk(KERN_INFO "VFS: Can't find a valid FAT filesystem"
		       " on dev %s.\n", sb->s_id);

out_fail:
	if (root_inode)
		iput(root_inode);
	if (sbi->nls_io)
		unload_nls(sbi->nls_io);
	if (sbi->nls_disk)
		unload_nls(sbi->nls_disk);
	if (sbi->options.iocharset)
		kfree(sbi->options.iocharset);
	sb->s_fs_info = NULL;
	kfree(sbi);
	return error;
}

int fat_statfs(struct super_block *sb,struct statfs *buf)
{
	int free,nr;
       
	lock_fat(sb);
	if (MSDOS_SB(sb)->free_clusters != -1)
		free = MSDOS_SB(sb)->free_clusters;
	else {
		free = 0;
		for (nr = 2; nr < MSDOS_SB(sb)->clusters + 2; nr++)
			if (fat_access(sb, nr, -1) == FAT_ENT_FREE)
				free++;
		MSDOS_SB(sb)->free_clusters = free;
	}
	unlock_fat(sb);

	buf->f_type = sb->s_magic;
	buf->f_bsize = 1 << MSDOS_SB(sb)->cluster_bits;
	buf->f_blocks = MSDOS_SB(sb)->clusters;
	buf->f_bfree = free;
	buf->f_bavail = free;
	buf->f_namelen = MSDOS_SB(sb)->options.isvfat ? 260 : 12;

	return 0;
}

static int is_exec(char *extension)
{
	char *exe_extensions = "EXECOMBAT", *walk;

	for (walk = exe_extensions; *walk; walk += 3)
		if (!strncmp(extension, walk, 3))
			return 1;
	return 0;
}

static int fat_writepage(struct page *page, struct writeback_control *wbc)
{
	return block_write_full_page(page,fat_get_block, wbc);
}
static int fat_readpage(struct file *file, struct page *page)
{
	return block_read_full_page(page,fat_get_block);
}

static int
fat_prepare_write(struct file *file, struct page *page,
			unsigned from, unsigned to)
{
	kmap(page);
	return cont_prepare_write(page,from,to,fat_get_block,
		&MSDOS_I(page->mapping->host)->mmu_private);
}

static int
fat_commit_write(struct file *file, struct page *page,
			unsigned from, unsigned to)
{
	kunmap(page);
	return generic_commit_write(file, page, from, to);
}

static sector_t _fat_bmap(struct address_space *mapping, sector_t block)
{
	return generic_block_bmap(mapping,block,fat_get_block);
}
static struct address_space_operations fat_aops = {
	.readpage = fat_readpage,
	.writepage = fat_writepage,
	.sync_page = block_sync_page,
	.prepare_write = fat_prepare_write,
	.commit_write = fat_commit_write,
	.bmap = _fat_bmap
};

/* doesn't deal with root inode */
static int fat_fill_inode(struct inode *inode, struct msdos_dir_entry *de)
{
	struct super_block *sb = inode->i_sb;
	struct msdos_sb_info *sbi = MSDOS_SB(sb);
	int error;

	MSDOS_I(inode)->i_location = 0;
	inode->i_uid = sbi->options.fs_uid;
	inode->i_gid = sbi->options.fs_gid;
	inode->i_version++;
	inode->i_generation = get_seconds();
	
	if ((de->attr & ATTR_DIR) && !IS_FREE(de->name)) {
		inode->i_generation &= ~1;
		inode->i_mode = MSDOS_MKMODE(de->attr,
			S_IRWXUGO & ~sbi->options.fs_dmask) | S_IFDIR;
		inode->i_op = sbi->dir_ops;
		inode->i_fop = &fat_dir_operations;

		MSDOS_I(inode)->i_start = CF_LE_W(de->start);
		if (sbi->fat_bits == 32) {
			MSDOS_I(inode)->i_start |=
				(CF_LE_W(de->starthi) << 16);
		}
		MSDOS_I(inode)->i_logstart = MSDOS_I(inode)->i_start;
		error = fat_calc_dir_size(inode);
		if (error < 0)
			return error;
		MSDOS_I(inode)->mmu_private = inode->i_size;

		inode->i_nlink = fat_subdirs(inode);
	} else { /* not a directory */
		inode->i_generation |= 1;
		inode->i_mode = MSDOS_MKMODE(de->attr,
		    ((sbi->options.showexec &&
		       !is_exec(de->ext))
		    	? S_IRUGO|S_IWUGO : S_IRWXUGO)
		    & ~sbi->options.fs_fmask) | S_IFREG;
		MSDOS_I(inode)->i_start = CF_LE_W(de->start);
		if (sbi->fat_bits == 32) {
			MSDOS_I(inode)->i_start |=
				(CF_LE_W(de->starthi) << 16);
		}
		MSDOS_I(inode)->i_logstart = MSDOS_I(inode)->i_start;
		inode->i_size = CF_LE_L(de->size);
	        inode->i_op = &fat_file_inode_operations;
	        inode->i_fop = &fat_file_operations;
		inode->i_mapping->a_ops = &fat_aops;
		MSDOS_I(inode)->mmu_private = inode->i_size;
	}
	if(de->attr & ATTR_SYS)
		if (sbi->options.sys_immutable)
			inode->i_flags |= S_IMMUTABLE;
	MSDOS_I(inode)->i_attrs = de->attr & ATTR_UNUSED;
	/* this is as close to the truth as we can get ... */
	inode->i_blksize = 1 << sbi->cluster_bits;
	inode->i_blocks = ((inode->i_size + inode->i_blksize - 1)
			   & ~((loff_t)inode->i_blksize - 1)) >> 9;
	inode->i_mtime.tv_sec = inode->i_atime.tv_sec =
		date_dos2unix(CF_LE_W(de->time),CF_LE_W(de->date));
	inode->i_mtime.tv_nsec = inode->i_atime.tv_nsec = 0;
	inode->i_ctime.tv_sec =
		MSDOS_SB(sb)->options.isvfat
		? date_dos2unix(CF_LE_W(de->ctime),CF_LE_W(de->cdate))
		: inode->i_mtime.tv_sec;
	inode->i_ctime.tv_nsec = de->ctime_ms * 1000000;
	MSDOS_I(inode)->i_ctime_ms = de->ctime_ms;

	return 0;
}

void fat_write_inode(struct inode *inode, int wait)
{
	struct super_block *sb = inode->i_sb;
	struct buffer_head *bh;
	struct msdos_dir_entry *raw_entry;
	unsigned int i_pos;

retry:
	i_pos = MSDOS_I(inode)->i_location;
	if (inode->i_ino == MSDOS_ROOT_INO || !i_pos) {
		return;
	}
	lock_kernel();
	if (!(bh = sb_bread(sb, i_pos >> MSDOS_SB(sb)->dir_per_block_bits))) {
		fat_fs_panic(sb, "unable to read i-node block (ino %lu)",
			     i_pos);
		unlock_kernel();
		return;
	}
	spin_lock(&fat_inode_lock);
	if (i_pos != MSDOS_I(inode)->i_location) {
		spin_unlock(&fat_inode_lock);
		brelse(bh);
		unlock_kernel();
		goto retry;
	}

	raw_entry = &((struct msdos_dir_entry *) (bh->b_data))
	    [i_pos & (MSDOS_SB(sb)->dir_per_block - 1)];
	if (S_ISDIR(inode->i_mode)) {
		raw_entry->attr = ATTR_DIR;
		raw_entry->size = 0;
	}
	else {
		raw_entry->attr = ATTR_NONE;
		raw_entry->size = CT_LE_L(inode->i_size);
	}
	raw_entry->attr |= MSDOS_MKATTR(inode->i_mode) |
	    MSDOS_I(inode)->i_attrs;
	raw_entry->start = CT_LE_W(MSDOS_I(inode)->i_logstart);
	raw_entry->starthi = CT_LE_W(MSDOS_I(inode)->i_logstart >> 16);
	fat_date_unix2dos(inode->i_mtime.tv_sec,&raw_entry->time,&raw_entry->date);
	raw_entry->time = CT_LE_W(raw_entry->time);
	raw_entry->date = CT_LE_W(raw_entry->date);
	if (MSDOS_SB(sb)->options.isvfat) {
		fat_date_unix2dos(inode->i_ctime.tv_sec,&raw_entry->ctime,&raw_entry->cdate);
		raw_entry->ctime_ms = MSDOS_I(inode)->i_ctime_ms; /* use i_ctime.tv_nsec? */
		raw_entry->ctime = CT_LE_W(raw_entry->ctime);
		raw_entry->cdate = CT_LE_W(raw_entry->cdate);
	}
	spin_unlock(&fat_inode_lock);
	mark_buffer_dirty(bh);
	brelse(bh);
	unlock_kernel();
}


int fat_notify_change(struct dentry * dentry, struct iattr * attr)
{
	struct msdos_sb_info *sbi = MSDOS_SB(dentry->d_sb);
	struct inode *inode = dentry->d_inode;
	int mask, error = 0;

	lock_kernel();

	/* FAT cannot truncate to a longer file */
	if (attr->ia_valid & ATTR_SIZE) {
		if (attr->ia_size > inode->i_size) {
			error = -EPERM;
			goto out;
		}
	}

	error = inode_change_ok(inode, attr);
	if (error) {
		if (sbi->options.quiet)
			error = 0;
 		goto out;
	}

	if (((attr->ia_valid & ATTR_UID) && 
	     (attr->ia_uid != sbi->options.fs_uid)) ||
	    ((attr->ia_valid & ATTR_GID) && 
	     (attr->ia_gid != sbi->options.fs_gid)) ||
	    ((attr->ia_valid & ATTR_MODE) &&
	     (attr->ia_mode & ~MSDOS_VALID_MODE)))
		error = -EPERM;

	if (error) {
		if (sbi->options.quiet)  
			error = 0;
		goto out;
	}
	error = inode_setattr(inode, attr);
	if (error)
		goto out;

	if (S_ISDIR(inode->i_mode))
		mask = sbi->options.fs_dmask;
	else
		mask = sbi->options.fs_fmask;
	inode->i_mode &= S_IFMT | (S_IRWXUGO & ~mask);
out:
	unlock_kernel();
	return error;
}
MODULE_LICENSE("GPL");
