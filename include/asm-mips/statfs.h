/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 1999 by Ralf Baechle
 */
#ifndef _ASM_STATFS_H
#define _ASM_STATFS_H

#include <linux/posix_types.h>

#ifndef __KERNEL_STRICT_NAMES

#include <linux/types.h>

typedef __kernel_fsid_t        fsid_t;

#endif

struct statfs {
	long		f_type;
#define f_fstyp f_type
	long		f_bsize;
	long		f_frsize;	/* Fragment size - unsupported */
	long		f_blocks;
	long		f_bfree;
	long		f_files;
	long		f_ffree;

	/* Linux specials */
	long	f_bavail;
	__kernel_fsid_t	f_fsid;
	long		f_namelen;
	long		f_spare[6];
};

/*
 * Unlike the 32-bit version the 64-bit version has none of the ABI baggage.
 */
struct statfs64 {
	__u32	f_type;
	__u32	f_bsize;
	__u64	f_blocks;
	__u64	f_bfree;
	__u64	f_bavail;
	__u64	f_files;
	__u64	f_ffree;
	__kernel_fsid_t f_fsid;
	__u32	f_namelen;
	__u32	f_frsize;
	__u32	f_spare[5];
};

#endif /* _ASM_STATFS_H */
