/* -*- linux-c -*- --------------------------------------------------------- *
 *
 * linux/include/linux/devpts_fs.h
 *
 *  Copyright 1998-2004 H. Peter Anvin -- All Rights Reserved
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License, version 2, or at your
 * option, any later version, incorporated herein by reference.
 *
 * ------------------------------------------------------------------------- */

#ifndef _LINUX_DEVPTS_FS_H
#define _LINUX_DEVPTS_FS_H

#include <linux/errno.h>

#ifdef CONFIG_UNIX98_PTYS

#ifdef CONFIG_DEVPTS_POSIX_ACL
struct devpts_inode_info {
	struct posix_acl	*i_acl;
	struct inode		vfs_inode;
};

static inline struct devpts_inode_info *DEVPTS_I(struct inode *inode)
{
	return container_of(inode, struct devpts_inode_info, vfs_inode);
}

/* acl.c */
int devpts_setattr(struct dentry *, struct iattr *);
int devpts_permission(struct inode *, int, struct nameidata *);
#endif  /* CONFIG_DEVPTS_POSIX_ACL */

/* inode.c */
int devpts_pty_new(struct tty_struct *tty);      /* mknod in devpts */
struct tty_struct *devpts_get_tty(int number);	 /* get tty structure */
void devpts_pty_kill(int number);		 /* unlink */

#else

/* Dummy stubs in the no-pty case */
static inline int devpts_pty_new(struct tty_struct *tty) { return -EINVAL; }
static inline struct tty_struct *devpts_get_tty(int number) { return NULL; }
static inline void devpts_pty_kill(int number) { }

#endif


#endif /* _LINUX_DEVPTS_FS_H */
