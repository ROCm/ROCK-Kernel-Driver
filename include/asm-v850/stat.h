/*
 * include/asm-v850/stat.h -- v850 stat structure
 *
 *  Copyright (C) 2001,02,03  NEC Corporation
 *  Copyright (C) 2001,02,03  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#ifndef __V850_STAT_H__
#define __V850_STAT_H__

#include <asm/posix_types.h>

struct stat {
	__kernel_dev_t	st_dev;
	__kernel_ino_t	st_ino;
	__kernel_mode_t	st_mode;
	__kernel_nlink_t st_nlink;
	__kernel_uid_t 	st_uid;
	__kernel_gid_t 	st_gid;
	__kernel_dev_t	st_rdev;
	__kernel_off_t	st_size;
	unsigned long	st_blksize;
	unsigned long	st_blocks;
	unsigned long	st_atime;
	unsigned long	__unused1;
	unsigned long	st_mtime;
	unsigned long	__unused2;
	unsigned long	st_ctime;
	unsigned long	__unused3;
	unsigned long	__unused4;
	unsigned long	__unused5;
};

struct stat64 {
	__kernel_dev_t	st_dev;
	unsigned long	__unused0;
	unsigned long	__unused1;

	__kernel_ino64_t st_ino;

	__kernel_mode_t	st_mode;
	__kernel_nlink_t st_nlink;

	__kernel_uid_t	st_uid;
	__kernel_gid_t	st_gid;

	__kernel_dev_t	st_rdev;
	unsigned long	__unused2;
	unsigned long	__unused3;

	__kernel_loff_t	st_size;
	unsigned long	st_blksize;

	unsigned long	st_blocks; /* No. of 512-byte blocks allocated */
	unsigned long	__unused4; /* future possible st_blocks high bits */

	unsigned long	st_atime;
	unsigned long	st_atime_nsec;

	unsigned long	st_mtime;
	unsigned long	st_mtime_nsec;

	unsigned long	st_ctime;
	unsigned long	st_ctime_nsec;

	unsigned long	__unused8;
};

#endif /* __V850_STAT_H__ */
