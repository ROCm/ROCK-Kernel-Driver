/*
 * Directory notification for Linux
 *
 * Copyright (C) 2000,2002 Stephen Rothwell
 */

#include <linux/fs.h>

struct dnotify_struct {
	struct dnotify_struct *	dn_next;
	unsigned long		dn_mask;	/* Events to be notified
						   see linux/fcntl.h */
	int			dn_fd;
	struct file *		dn_filp;
	fl_owner_t		dn_owner;
};

extern void __inode_dir_notify(struct inode *, unsigned long);
extern void dnotify_flush(struct file *filp, fl_owner_t id);
extern int fcntl_dirnotify(int, struct file *, unsigned long);

static inline void inode_dir_notify(struct inode *inode, unsigned long event)
{
	if ((inode)->i_dnotify_mask & (event))
		__inode_dir_notify(inode, event);
}

/*
 * This is hopelessly wrong, but unfixable without API changes.  At
 * least it doesn't oops the kernel...
 */
static inline void dnotify_parent(struct dentry *dentry, unsigned long event)
{
	struct dentry *parent;
	read_lock(&dparent_lock);
	parent = dentry->d_parent;
	if (parent->d_inode->i_dnotify_mask & event) {
		dget(parent);
		read_unlock(&dparent_lock);
		__inode_dir_notify(parent->d_inode, event);
		dput(parent);
	} else
		read_unlock(&dparent_lock);
}
