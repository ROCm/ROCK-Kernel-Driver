/*
 * Directory notification for Linux
 *
 * Copyright 2000 (C) Stephen Rothwell
 */

struct dnotify_struct {
	struct dnotify_struct *	dn_next;
	int			dn_magic;
	unsigned long		dn_mask;	/* Events to be notified
						   see linux/fcntl.h */
	int			dn_fd;
	struct file *		dn_filp;
};

#define DNOTIFY_MAGIC	0x444E4F54

extern void __inode_dir_notify(struct inode *, unsigned long);
extern int fcntl_dirnotify(int, struct file *, unsigned long);

static inline void inode_dir_notify(struct inode *inode, unsigned long event)
{
	if ((inode)->i_dnotify_mask & (event))
		__inode_dir_notify(inode, event);
}
