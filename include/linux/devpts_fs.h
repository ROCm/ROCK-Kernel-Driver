/* -*- linux-c -*- --------------------------------------------------------- *
 *
 * linux/include/linux/devpts_fs.h
 *
 *  Copyright 1998 H. Peter Anvin -- All Rights Reserved
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License, version 2, or at your
 * option, any later version, incorporated herein by reference.
 *
 * ------------------------------------------------------------------------- */

#ifndef _LINUX_DEVPTS_FS_H
#define _LINUX_DEVPTS_FS_H 1

#ifdef CONFIG_DEVPTS_FS

void devpts_pty_new(int, dev_t);	/* mknod in devpts */
void devpts_pty_kill(int);		/* unlink */

#else

static inline void devpts_pty_new(int line, dev_t device)
{
}

static inline void devpts_pty_kill(int line)
{
}

#endif

#endif /* _LINUX_DEVPTS_FS_H */
