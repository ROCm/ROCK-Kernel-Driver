/*
 *  linux/fs/compat.c
 *
 *  Kernel compatibililty routines for e.g. 32 bit syscall support
 *  on 64 bit kernels.
 *
 *  Copyright (C) 2002 Stephen Rothwell, IBM Corporation
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/linkage.h>
#include <linux/compat.h>
#include <linux/errno.h>
#include <linux/time.h>
#include <linux/fs.h>

#include <asm/uaccess.h>

/*
 * Not all architectures have sys_utime, so implement this in terms
 * of sys_utimes.
 */
asmlinkage long compat_sys_utime(char *filename, struct compat_utimbuf *t)
{
	struct timeval tv[2];

	if (t) {
		if (get_user(tv[0].tv_sec, &t->actime) ||
		    get_user(tv[1].tv_sec, &t->modtime))
			return -EFAULT;
		tv[0].tv_usec = 0;
		tv[1].tv_usec = 0;
	}
	return do_utimes(filename, t ? tv : NULL);
}


asmlinkage long compat_sys_newstat(char * filename,
		struct compat_stat *statbuf)
{
	struct kstat stat;
	int error = vfs_stat(filename, &stat);

	if (!error)
		error = cp_compat_stat(&stat, statbuf);
	return error;
}

asmlinkage long compat_sys_newlstat(char * filename,
		struct compat_stat *statbuf)
{
	struct kstat stat;
	int error = vfs_lstat(filename, &stat);

	if (!error)
		error = cp_compat_stat(&stat, statbuf);
	return error;
}

asmlinkage long compat_sys_newfstat(unsigned int fd,
		struct compat_stat * statbuf)
{
	struct kstat stat;
	int error = vfs_fstat(fd, &stat);

	if (!error)
		error = cp_compat_stat(&stat, statbuf);
	return error;
}
