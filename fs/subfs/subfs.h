/*
 *  subfs.h
 *
 *  Copyright (C) 2003-2004 Eugene S. Weiss <eweiss@sbclobal.net>
 *
 *  Distributed under the terms of the GNU General Public License version 2
 *  or above.
 */


#define SUBFS_MAGIC 0x2c791058

#define SUBMOUNTD_PATH "/sbin/submountd"

#define ERR(args...) \
	printk(KERN_ERR args)

#define SUBFS_VER "0.9"

#define ROOT_MODE 0777

struct subfs_mount {
	char *device;
	char *options;
	char *req_fs;
        char *helper_prog;
	unsigned long flags;
	struct super_block *sb;
	struct vfsmount *mount;
	struct semaphore sem;
	int procuid;
};


static void subfs_kill_super(struct super_block *sb);
static struct super_block *subfs_get_super(struct file_system_type *fst,
		int flags, const char *devname, void *data);
static struct vfsmount *get_subfs_vfsmount(struct super_block *sb);
static int subfs_fill_super(struct super_block *sb, void *data,
			    int silent);
static struct inode *subfs_make_inode(struct super_block *sb, int mode);
static int subfs_open(struct inode *inode, struct file *filp);
static struct dentry *subfs_lookup(struct inode *dir,
				   struct dentry *dentry, struct nameidata *nd);
static struct vfsmount *get_child_mount(struct subfs_mount *sfs_mnt);
static int mount_real_fs(struct subfs_mount *sfs_mnt);
static void subfs_send_signal(void);
static void subfs_set_fs_pwd(struct fs_struct *fs, struct vfsmount *mnt,
			     struct dentry *dentry);
static int subfs_statfs(struct super_block *sb, struct kstatfs *buf);


static struct file_system_type subfs_type = {
	.owner = THIS_MODULE,
	.name = "subfs",
	.get_sb = subfs_get_super,
	.kill_sb = subfs_kill_super,
};


static struct super_operations subfs_s_ops = {
	.statfs = subfs_statfs,
	.drop_inode = generic_delete_inode,
};


static struct inode_operations subfs_dir_inode_operations = {
	.lookup = subfs_lookup,
};


static struct file_operations subfs_file_ops = {
	.open = subfs_open,
};
