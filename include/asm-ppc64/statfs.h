#ifndef _PPC64_STATFS_H
#define _PPC64_STATFS_H

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef __KERNEL_STRICT_NAMES
#include <linux/types.h>
typedef __kernel_fsid_t	fsid_t;
#endif

/*
 * We're already 64-bit, so duplicate the definition
 */
struct statfs {
	long f_type;
	long f_bsize;
	long f_blocks;
	long f_bfree;
	long f_bavail;
	long f_files;
	long f_ffree;
	__kernel_fsid_t f_fsid;
	long f_namelen;
	long f_frsize;
	long f_spare[5];
};

struct statfs64 {
	long f_type;
	long f_bsize;
	long f_blocks;
	long f_bfree;
	long f_bavail;
	long f_files;
	long f_ffree;
	__kernel_fsid_t f_fsid;
	long f_namelen;
	long f_frsize;
	long f_spare[5];
};

#endif  /* _PPC64_STATFS_H */
