/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 1999, 2000 Ralf Baechle
 * Copyright (C) 2000 Silicon Graphics, Inc.
 */
#ifndef _ASM_STAT_H
#define _ASM_STAT_H

#include <linux/types.h>

/* The memory layout is the same as of struct stat64 of the 32-bit kernel.  */
struct stat {
	dev_t			st_dev;
	unsigned int		st_pad0[3]; /* Reserved for st_dev expansion */

	unsigned long		st_ino;

	mode_t			st_mode;
	nlink_t			st_nlink;

	uid_t			st_uid;
	gid_t			st_gid;

	dev_t			st_rdev;
	unsigned int		st_pad1[3]; /* Reserved for st_rdev expansion */

	off_t			st_size;

	/*
	 * Actually this should be timestruc_t st_atime, st_mtime and st_ctime
	 * but we don't have it under Linux.
	 */
	unsigned int		st_atime;
	unsigned int		st_atime_nsec;

	unsigned int		st_mtime;
	unsigned int		st_mtime_nsec;

	unsigned int		st_ctime;
	unsigned int		st_ctime_nsec;

	unsigned int		st_blksize;
	unsigned int		st_pad2;

	unsigned long		st_blocks;
};

#define STAT_HAVE_NSEC 1

#endif /* _ASM_STAT_H */
