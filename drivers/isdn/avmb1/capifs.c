/*
 * $Id: capifs.c,v 1.14.6.1 2000/11/28 12:02:45 kai Exp $
 * 
 * (c) Copyright 2000 by Carsten Paeth (calle@calle.de)
 *
 * Heavily based on devpts filesystem from H. Peter Anvin
 * 
 * $Log: capifs.c,v $
 * Revision 1.14.6.1  2000/11/28 12:02:45  kai
 * MODULE_DEVICE_TABLE for 2.4
 *
 * Revision 1.14.2.1  2000/11/26 17:47:53  kai
 * added PCI_DEV_TABLE for 2.4
 *
 * Revision 1.14  2000/11/23 20:45:14  kai
 * fixed module_init/exit stuff
 * Note: compiled-in kernel doesn't work pre 2.2.18 anymore.
 *
 * Revision 1.13  2000/11/18 16:17:25  kai
 * change from 2.4 tree
 *
 * Revision 1.12  2000/11/01 14:05:02  calle
 * - use module_init/module_exit from linux/init.h.
 * - all static struct variables are initialized with "membername:" now.
 * - avm_cs.c, let it work with newer pcmcia-cs.
 *
 * Revision 1.11  2000/10/24 15:08:47  calle
 * Too much includes.
 *
 * Revision 1.10  2000/10/12 10:12:35  calle
 * Bugfix: second iput(inode) on umount, destroies a foreign inode.
 *
 * Revision 1.9  2000/08/20 07:30:13  keil
 * changes for 2.4
 *
 * Revision 1.8  2000/07/20 10:23:13  calle
 * Include isdn_compat.h for people that don't use -p option of std2kern.
 *
 * Revision 1.7  2000/06/18 16:09:54  keil
 * more changes for 2.4
 *
 * Revision 1.6  2000/04/03 13:29:25  calle
 * make Tim Waugh happy (module unload races in 2.3.99-pre3).
 * no real problem there, but now it is much cleaner ...
 *
 * Revision 1.5  2000/03/13 17:49:52  calle
 * make it running with 2.3.51.
 *
 * Revision 1.4  2000/03/08 17:06:33  calle
 * - changes for devfs and 2.3.49
 * - capifs now configurable (no need with devfs)
 * - New Middleware ioctl CAPI_NCCI_GETUNIT
 * - Middleware again tested with 2.2.14 and 2.3.49 (with and without devfs)
 *
 * Revision 1.3  2000/03/06 18:00:23  calle
 * - Middleware extention now working with 2.3.49 (capifs).
 * - Fixed typos in debug section of capi.c
 * - Bugfix: Makefile corrected for b1pcmcia.c
 *
 * Revision 1.2  2000/03/06 09:17:07  calle
 * - capifs: fileoperations now in inode (change for 2.3.49)
 * - Config.in: Middleware extention not a tristate, uups.
 *
 * Revision 1.1  2000/03/03 16:48:38  calle
 * - Added CAPI2.0 Middleware support (CONFIG_ISDN_CAPI)
 *   It is now possible to create a connection with a CAPI2.0 applikation
 *   and than to handle the data connection from /dev/capi/ (capifs) and also
 *   using async or sync PPP on this connection.
 *   The two major device number 190 and 191 are not confirmed yet,
 *   but I want to save the code in cvs, before I go on.
 *
 *
 */

#include <linux/version.h>
#include <linux/fs.h>
#include <linux/tty.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/stat.h>
#include <linux/param.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/kdev_t.h>
#include <linux/kernel.h>
#include <linux/locks.h>
#include <linux/major.h>
#include <linux/malloc.h>
#include <linux/ctype.h>
#include <asm/bitops.h>
#include <asm/uaccess.h>

MODULE_AUTHOR("Carsten Paeth <calle@calle.de>");

static char *revision = "$Revision: 1.14.6.1 $";

struct capifs_ncci {
	struct inode *inode;
	char used;
	char type;
	unsigned int num;
	kdev_t kdev;
};

struct capifs_sb_info {
	u32 magic;
	struct super_block *next;
	struct super_block **back;
	int setuid;
	int setgid;
	uid_t   uid;
	gid_t   gid;
	umode_t mode;

	unsigned int max_ncci;
	struct capifs_ncci *nccis;
};

#define CAPIFS_SUPER_MAGIC (('C'<<8)|'N')
#define CAPIFS_SBI_MAGIC   (('C'<<24)|('A'<<16)|('P'<<8)|'I')

static inline struct capifs_sb_info *SBI(struct super_block *sb)
{
	return (struct capifs_sb_info *)(sb->u.generic_sbp);
}

/* ------------------------------------------------------------------ */

static int capifs_root_readdir(struct file *,void *,filldir_t);
static struct dentry *capifs_root_lookup(struct inode *,struct dentry *);
static int capifs_revalidate(struct dentry *, int);

static struct file_operations capifs_root_operations = {
	read:		generic_read_dir,
	readdir:	capifs_root_readdir,
};

struct inode_operations capifs_root_inode_operations = {
	lookup: capifs_root_lookup,
};

static struct dentry_operations capifs_dentry_operations = {
	d_revalidate: capifs_revalidate,
};

/*
 * /dev/capi/%d
 * /dev/capi/r%d
 */

static int capifs_root_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	struct inode * inode = filp->f_dentry->d_inode;
	struct capifs_sb_info * sbi = SBI(filp->f_dentry->d_inode->i_sb);
	off_t nr;
	char numbuf[32];

	nr = filp->f_pos;

	switch(nr)
	{
	case 0:
		if (filldir(dirent, ".", 1, nr, inode->i_ino, DT_DIR) < 0)
			return 0;
		filp->f_pos = ++nr;
		/* fall through */
	case 1:
		if (filldir(dirent, "..", 2, nr, inode->i_ino, DT_DIR) < 0)
			return 0;
		filp->f_pos = ++nr;
		/* fall through */
	default:
		while (nr < sbi->max_ncci) {
			int n = nr - 2;
			struct capifs_ncci *np = &sbi->nccis[n];
			if (np->inode && np->used) {
				char *p = numbuf;
				if (np->type) *p++ = np->type;
				sprintf(p, "%u", np->num);
				if ( filldir(dirent, numbuf, strlen(numbuf), nr, nr, DT_UNKNOWN) < 0 )
					return 0;
			}
			filp->f_pos = ++nr;
		}
		break;
	}

	return 0;
}

/*
 * Revalidate is called on every cache lookup.  We use it to check that
 * the ncci really does still exist.  Never revalidate negative dentries;
 * for simplicity (fix later?)
 */
static int capifs_revalidate(struct dentry * dentry, int flags)
{
	struct capifs_sb_info *sbi;

	if ( !dentry->d_inode )
		return 0;

	sbi = SBI(dentry->d_inode->i_sb);

	return ( sbi->nccis[dentry->d_inode->i_ino - 2].inode == dentry->d_inode );
}

static struct dentry *capifs_root_lookup(struct inode * dir, struct dentry * dentry)
{
	struct capifs_sb_info *sbi = SBI(dir->i_sb);
	struct capifs_ncci *np;
	unsigned int i;
	char numbuf[32];
	char *p, *tmp;
	unsigned int num;
	char type = 0;

	dentry->d_inode = NULL;	/* Assume failure */
	dentry->d_op    = &capifs_dentry_operations;

	if (dentry->d_name.len >= sizeof(numbuf) )
		return NULL;
	strncpy(numbuf, dentry->d_name.name, dentry->d_name.len);
	numbuf[dentry->d_name.len] = 0;
        p = numbuf;
	if (!isdigit(*p)) type = *p++;
	tmp = p;
	num = (unsigned int)simple_strtoul(p, &tmp, 10);
	if (tmp == p || *tmp)
		return NULL;

	for (i = 0, np = sbi->nccis ; i < sbi->max_ncci; i++, np++) {
		if (np->used && np->num == num && np->type == type)
			break;
	}

	if ( i >= sbi->max_ncci )
		return NULL;

	dentry->d_inode = np->inode;
	if ( dentry->d_inode )
		atomic_inc(&dentry->d_inode->i_count);
	
	d_add(dentry, dentry->d_inode);

	return NULL;
}

/* ------------------------------------------------------------------ */

static struct super_block *mounts = NULL;

static void capifs_put_super(struct super_block *sb)
{
	struct capifs_sb_info *sbi = SBI(sb);
	struct inode *inode;
	int i;

	for ( i = 0 ; i < sbi->max_ncci ; i++ ) {
		if ( (inode = sbi->nccis[i].inode) ) {
			if (atomic_read(&inode->i_count) != 1 )
				printk("capifs_put_super: badness: entry %d count %d\n",
				       i, (unsigned)atomic_read(&inode->i_count));
			inode->i_nlink--;
			iput(inode);
		}
	}

	*sbi->back = sbi->next;
	if ( sbi->next )
		SBI(sbi->next)->back = sbi->back;

	kfree(sbi->nccis);
	kfree(sbi);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,51)
	MOD_DEC_USE_COUNT;
#endif
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,51)
static int capifs_statfs(struct super_block *sb, struct statfs *buf, int bufsiz);
static void capifs_write_inode(struct inode *inode) { };
#else
static int capifs_statfs(struct super_block *sb, struct statfs *buf);
#endif
static void capifs_read_inode(struct inode *inode);

static struct super_operations capifs_sops = {
	read_inode:	capifs_read_inode,
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,51)
	write_inode:	capifs_write_inode,
#endif
	put_super:	capifs_put_super,
	statfs:		capifs_statfs,
};

static int capifs_parse_options(char *options, struct capifs_sb_info *sbi)
{
	int setuid = 0;
	int setgid = 0;
	uid_t uid = 0;
	gid_t gid = 0;
	umode_t mode = 0600;
	unsigned int maxncci = 512;
	char *this_char, *value;

	this_char = NULL;
	if ( options )
		this_char = strtok(options,",");
	for ( ; this_char; this_char = strtok(NULL,",")) {
		if ((value = strchr(this_char,'=')) != NULL)
			*value++ = 0;
		if (!strcmp(this_char,"uid")) {
			if (!value || !*value)
				return 1;
			uid = simple_strtoul(value,&value,0);
			if (*value)
				return 1;
			setuid = 1;
		}
		else if (!strcmp(this_char,"gid")) {
			if (!value || !*value)
				return 1;
			gid = simple_strtoul(value,&value,0);
			if (*value)
				return 1;
			setgid = 1;
		}
		else if (!strcmp(this_char,"mode")) {
			if (!value || !*value)
				return 1;
			mode = simple_strtoul(value,&value,8);
			if (*value)
				return 1;
		}
		else if (!strcmp(this_char,"maxncci")) {
			if (!value || !*value)
				return 1;
			maxncci = simple_strtoul(value,&value,8);
			if (*value)
				return 1;
		}
		else
			return 1;
	}
	sbi->setuid   = setuid;
	sbi->setgid   = setgid;
	sbi->uid      = uid;
	sbi->gid      = gid;
	sbi->mode     = mode & ~S_IFMT;
	sbi->max_ncci = maxncci;

	return 0;
}

struct super_block *capifs_read_super(struct super_block *s, void *data,
				      int silent)
{
	struct inode * root_inode;
	struct dentry * root;
	struct capifs_sb_info *sbi;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,51)
	MOD_INC_USE_COUNT;
	lock_super(s);
#endif
	/* Super block already completed? */
	if (s->s_root)
		goto out;

	sbi = (struct capifs_sb_info *) kmalloc(sizeof(struct capifs_sb_info), GFP_KERNEL);
	if ( !sbi )
		goto fail;

	memset(sbi, 0, sizeof(struct capifs_sb_info));
	sbi->magic  = CAPIFS_SBI_MAGIC;

	if ( capifs_parse_options(data,sbi) ) {
		kfree(sbi);
		printk("capifs: called with bogus options\n");
		goto fail;
	}

	sbi->nccis = kmalloc(sizeof(struct capifs_ncci) * sbi->max_ncci, GFP_KERNEL);
	if ( !sbi->nccis ) {
		kfree(sbi);
		goto fail;
	}
	memset(sbi->nccis, 0, sizeof(struct capifs_ncci) * sbi->max_ncci);

	s->u.generic_sbp = (void *) sbi;
	s->s_blocksize = 1024;
	s->s_blocksize_bits = 10;
	s->s_magic = CAPIFS_SUPER_MAGIC;
	s->s_op = &capifs_sops;
	s->s_root = NULL;

	/*
	 * Get the root inode and dentry, but defer checking for errors.
	 */
	root_inode = iget(s, 1); /* inode 1 == root directory */
	root = d_alloc_root(root_inode);

	/*
	 * Check whether somebody else completed the super block.
	 */
	if (s->s_root) {
		if (root) dput(root);
		else iput(root_inode);
		goto out;
	}

	if (!root) {
		printk("capifs: get root dentry failed\n");
		/*
	 	* iput() can block, so we clear the super block first.
	 	*/
		iput(root_inode);
		kfree(sbi->nccis);
		kfree(sbi);
		goto fail;
	}

	/*
	 * Check whether somebody else completed the super block.
	 */
	if (s->s_root)
		goto out;
	
	/*
	 * Success! Install the root dentry now to indicate completion.
	 */
	s->s_root = root;

	sbi->next = mounts;
	if ( sbi->next )
		SBI(sbi->next)->back = &(sbi->next);
	sbi->back = &mounts;
	mounts = s;

out:	/* Success ... somebody else completed the super block for us. */ 
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,51)
	unlock_super(s);
#endif
	return s;
fail:
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,51)
	unlock_super(s);
	MOD_DEC_USE_COUNT;
#endif
	return NULL;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,51)
static int capifs_statfs(struct super_block *sb, struct statfs *buf, int bufsiz)
{
	struct statfs tmp;

	tmp.f_type = CAPIFS_SUPER_MAGIC;
	tmp.f_bsize = 1024;
	tmp.f_blocks = 0;
	tmp.f_bfree = 0;
	tmp.f_bavail = 0;
	tmp.f_files = 0;
	tmp.f_ffree = 0;
	tmp.f_namelen = NAME_MAX;
	return copy_to_user(buf, &tmp, bufsiz) ? -EFAULT : 0;
}
#else
static int capifs_statfs(struct super_block *sb, struct statfs *buf)
{
	buf->f_type = CAPIFS_SUPER_MAGIC;
	buf->f_bsize = 1024;
	buf->f_blocks = 0;
	buf->f_bfree = 0;
	buf->f_bavail = 0;
	buf->f_files = 0;
	buf->f_ffree = 0;
	buf->f_namelen = NAME_MAX;
	return 0;
}
#endif

static void capifs_read_inode(struct inode *inode)
{
	ino_t ino = inode->i_ino;
	struct capifs_sb_info *sbi = SBI(inode->i_sb);

	inode->i_mode = 0;
	inode->i_nlink = 0;
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;
	inode->i_blocks = 0;
	inode->i_blksize = 1024;
	inode->i_uid = inode->i_gid = 0;

	if ( ino == 1 ) {
		inode->i_mode = S_IFDIR | S_IRUGO | S_IXUGO | S_IWUSR;
		inode->i_op = &capifs_root_inode_operations;
		inode->i_fop = &capifs_root_operations;
		inode->i_nlink = 2;
		return;
	} 

	ino -= 2;
	if ( ino >= sbi->max_ncci )
		return;		/* Bogus */
	
	init_special_inode(inode, S_IFCHR, 0);

	return;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,51)
static struct file_system_type capifs_fs_type = {
	"capifs",
	0,
	capifs_read_super,
	NULL
};
#else
static DECLARE_FSTYPE(capifs_fs_type, "capifs", capifs_read_super, 0);
#endif

void capifs_new_ncci(char type, unsigned int num, kdev_t device)
{
	struct super_block *sb;
	struct capifs_sb_info *sbi;
	struct capifs_ncci *np;
	ino_t ino;

	for ( sb = mounts ; sb ; sb = sbi->next ) {
		sbi = SBI(sb);

		for (ino = 0, np = sbi->nccis ; ino < sbi->max_ncci; ino++, np++) {
			if (np->used == 0) {
				np->used = 1;
				np->type = type;
				np->num = num;
				np->kdev = device;
				break;
			}
		}

		if ((np->inode = iget(sb, ino+2)) != 0) {
			struct inode *inode = np->inode;
			inode->i_uid = sbi->setuid ? sbi->uid : current->fsuid;
			inode->i_gid = sbi->setgid ? sbi->gid : current->fsgid;
			inode->i_mode = sbi->mode | S_IFCHR;
			inode->i_rdev = np->kdev;
			inode->i_nlink++;
		}
	}
}

void capifs_free_ncci(char type, unsigned int num)
{
	struct super_block *sb;
	struct capifs_sb_info *sbi;
	struct inode *inode;
	struct capifs_ncci *np;
	ino_t ino;

	for ( sb = mounts ; sb ; sb = sbi->next ) {
		sbi = SBI(sb);

		for (ino = 0, np = sbi->nccis ; ino < sbi->max_ncci; ino++, np++) {
			if (!np->used || np->type != type || np->num != num)
				continue;
			if (np->inode) {
				inode = np->inode;
				np->inode = 0;
				np->used = 0;
				inode->i_nlink--;
				iput(inode);
				break;
			}
		}
	}
}

static int __init capifs_init(void)
{
	char rev[10];
	char *p;
	int err;

	MOD_INC_USE_COUNT;

	if ((p = strchr(revision, ':'))) {
		strcpy(rev, p + 1);
		p = strchr(rev, '$');
		*p = 0;
	} else
		strcpy(rev, "1.0");

	err = register_filesystem(&capifs_fs_type);
	if (err) {
		MOD_DEC_USE_COUNT;
		return err;
	}
#ifdef MODULE
        printk(KERN_NOTICE "capifs: Rev%s: loaded\n", rev);
#else
	printk(KERN_NOTICE "capifs: Rev%s: started\n", rev);
#endif
	MOD_DEC_USE_COUNT;
	return 0;
}

static void __exit capifs_exit(void)
{
	unregister_filesystem(&capifs_fs_type);
}

EXPORT_SYMBOL(capifs_new_ncci);
EXPORT_SYMBOL(capifs_free_ncci);

module_init(capifs_init);
module_exit(capifs_exit);
