
extern struct vfsmount * sysfs_mount;

extern struct inode *sysfs_get_inode(struct super_block *sb, int mode, int dev);

extern int sysfs_mknod(struct inode *, struct dentry *, int, dev_t);

extern struct dentry * sysfs_get_dentry(struct dentry *, char *);

extern void sysfs_hash_and_remove(struct dentry * dir, const char * name);

