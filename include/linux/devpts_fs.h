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
#define _LINUX_DEVPTS_FS_H 1

#include <linux/errno.h>

#if CONFIG_UNIX98_PTYS

int devpts_pty_new(struct tty_struct *); /* mknod in devpts */
struct tty_struct *devpts_get_tty(int);	 /* get tty structure */
void devpts_pty_kill(int);		 /* unlink */

#else

/* Dummy stubs in the no-pty case */
static inline int devpts_pty_new(struct tty_struct *) { return -EINVAL; }
static inline struct tty_struct *devpts_get_tty(int)  { return NULL; }
static inline void devpts_pty_kill(int) { }

#endif


#endif /* _LINUX_DEVPTS_FS_H */
