
extern struct vfsmount * sysfs_mount;

extern struct inode * sysfs_new_inode(mode_t mode);
extern int sysfs_create(struct dentry *, int mode, int (*init)(struct inode *));

extern struct dentry * sysfs_get_dentry(struct dentry *, const char *);

extern int sysfs_add_file(struct dentry * dir, const struct attribute * attr);
extern void sysfs_hash_and_remove(struct dentry * dir, const char * name);

extern int sysfs_create_subdir(struct kobject *, const char *, struct dentry **);
extern void sysfs_remove_subdir(struct dentry *);

extern int sysfs_follow_link(struct dentry *, struct nameidata *);
extern void sysfs_put_link(struct dentry *, struct nameidata *);
extern struct rw_semaphore sysfs_rename_sem;

static inline struct kobject * to_kobj(struct dentry * dentry)
{
	return ((struct kobject *) dentry->d_fsdata);
}

static inline struct attribute * to_attr(struct dentry * dentry)
{
	return ((struct attribute *) dentry->d_fsdata);
}

static inline struct bin_attribute * to_bin_attr(struct dentry * dentry)
{
	return ((struct bin_attribute *) dentry->d_fsdata);
}

static inline struct kobject *sysfs_get_kobject(struct dentry *dentry)
{
	struct kobject * kobj = NULL;

	spin_lock(&dcache_lock);
	if (!d_unhashed(dentry))
		kobj = kobject_get(to_kobj(dentry));
	spin_unlock(&dcache_lock);

	return kobj;
}

