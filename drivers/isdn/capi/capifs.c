/* $Id: capifs.c,v 1.14.6.8 2001/09/23 22:24:33 kai Exp $
 * 
 * Copyright 2000 by Carsten Paeth <calle@calle.de>
 *
 * Heavily based on devpts filesystem from H. Peter Anvin
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/ctype.h>

MODULE_DESCRIPTION("CAPI4Linux: /dev/capi/ filesystem");
MODULE_AUTHOR("Carsten Paeth");
MODULE_LICENSE("GPL");

static char *revision = "$Revision: 1.14.6.8 $";

struct options {
	int setuid;
	int setgid;
	uid_t   uid;
	gid_t   gid;
	umode_t mode;
};
static struct options options = {.mode = 0600};

#define CAPIFS_SUPER_MAGIC (('C'<<8)|'N')

/* ------------------------------------------------------------------ */


static int capifs_parse_options(char *s, struct options *p)
{
	int setuid = 0;
	int setgid = 0;
	uid_t uid = 0;
	gid_t gid = 0;
	umode_t mode = 0600;
	char *this_char, *value;

	if (!s)
		return 0;

	while ((this_char = strsep(&s, ",")) != NULL) {
		if (!*this_char)
			continue;
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
	}
	p->setuid   = setuid;
	p->setgid   = setgid;
	p->uid      = uid;
	p->gid      = gid;
	p->mode     = mode & ~S_IFMT;

	return 0;
}

static int capifs_remount(struct super_block *s, int *flags, char *data)
{
	struct options new;
	if (capifs_parse_options(data, &new)) {
		printk("capifs: called with bogus options\n");
		return -EINVAL;
	}
	options = new;
	return 0;
}


static struct super_operations capifs_sops =
{
	.statfs		= simple_statfs,
	.remount_fs	= capifs_remount,
};

static int capifs_fill_super(struct super_block *s, void *data, int silent)
{
	struct inode * inode;

	if (capifs_parse_options(data, &options)) {
		printk("capifs: called with bogus options\n");
		return -EINVAL;
	}

	s->s_blocksize = 1024;
	s->s_blocksize_bits = 10;
	s->s_magic = CAPIFS_SUPER_MAGIC;
	s->s_op = &capifs_sops;

	inode = new_inode(s);
	if (!inode)
		return -ENOMEM;
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;
	inode->i_blocks = 0;
	inode->i_blksize = 1024;
	inode->i_uid = inode->i_gid = 0;
	inode->i_ino = 1;
	inode->i_mode = S_IFDIR | S_IRUGO | S_IXUGO | S_IWUSR;
	inode->i_op = &simple_dir_inode_operations;
	inode->i_fop = &simple_dir_operations;
	inode->i_nlink = 2;
	s->s_root = d_alloc_root(inode);

	if (!s->s_root) {
		printk("capifs: get root dentry failed\n");
		iput(inode);
		return -ENOMEM;
	}
	return 0;
}

static struct super_block *capifs_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	return get_sb_single(fs_type, flags, data, capifs_fill_super);
}

static struct file_system_type capifs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "capifs",
	.get_sb		= capifs_get_sb,
	.kill_sb	= kill_anon_super,
};

static struct vfsmount *capifs_mnt;
static int entry_count;

static int grab_instance(void)
{
	return simple_pin_fs("capifs", &capifs_mnt, &entry_count);
}

static void drop_instance(void)
{
	return simple_release_fs(&capifs_mnt, &entry_count);
}

static struct dentry *get_node(int type, int num)
{
	char s[10];
	int len;
	struct dentry *root = capifs_mnt->mnt_root;
	if (type)
		len = sprintf(s, "%d", num);
	else
		len = sprintf(s, "%c%d", type, num);
	down(&root->d_inode->i_sem);
	return lookup_one_len(s, root, len);
}

void capifs_new_ncci(char type, unsigned int num, dev_t device)
{
	struct super_block *sb;
	struct dentry *dentry;
	struct inode *inode;

	if (grab_instance() < 0)
		return;
	sb = capifs_mnt->mnt_sb;
	inode = new_inode(sb);
	if (inode) {
		inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;
		inode->i_blocks = 0;
		inode->i_blksize = 1024;
		inode->i_uid = options.setuid ? options.uid : current->fsuid;
		inode->i_gid = options.setgid ? options.gid : current->fsgid;
		inode->i_nlink = 1;
		init_special_inode(inode, S_IFCHR | options.mode, device);
		dentry = get_node(type, num);
		if (!IS_ERR(dentry) && !dentry->d_inode) {
			grab_instance();
			d_instantiate(dentry, inode);
		} else
			iput(inode);
		up(&sb->s_root->d_inode->i_sem);
	}
	drop_instance();
}

void capifs_free_ncci(char type, unsigned int num)
{
	if (grab_instance() == 0) {
		struct dentry *dentry = get_node(type, num);
		if (!IS_ERR(dentry)) {
			struct inode *inode = dentry->d_inode;
			if (inode) {
				inode->i_nlink--;
				d_delete(dentry);
				dput(dentry);
				drop_instance();
			}
			dput(dentry);
		}
		up(&capifs_mnt->mnt_root->d_inode->i_sem);
		drop_instance();
	}
}

static int __init capifs_init(void)
{
	char rev[32];
	char *p;
	int err;

	if ((p = strchr(revision, ':')) != 0 && p[1]) {
		strlcpy(rev, p + 2, sizeof(rev));
		if ((p = strchr(rev, '$')) != 0 && p > rev)
		   *(p-1) = 0;
	} else
		strcpy(rev, "1.0");

	err = register_filesystem(&capifs_fs_type);
	if (!err)
		printk(KERN_NOTICE "capifs: Rev %s\n", rev);
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
