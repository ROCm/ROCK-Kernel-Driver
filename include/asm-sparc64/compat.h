#ifndef _ASM_SPARC64_COMPAT_H
#define _ASM_SPARC64_COMPAT_H
/*
 * Architecture specific compatibility types
 */
#include <linux/types.h>

#define COMPAT_USER_HZ 100

typedef u32		compat_size_t;
typedef s32		compat_ssize_t;
typedef s32		compat_time_t;
typedef s32		compat_clock_t;

struct compat_stat {
	__kernel_dev_t32	st_dev;
	__kernel_ino_t32	st_ino;
	__kernel_mode_t32	st_mode;
	s16			st_nlink;
	__kernel_uid_t32	st_uid;
	__kernel_gid_t32	st_gid;
	__kernel_dev_t32	st_rdev;
	__kernel_off_t32	st_size;
	compat_time_t		st_atime;
	u32			__unused1;
	compat_time_t		st_mtime;
	u32			__unused2;
	compat_time_t		st_ctime;
	u32			__unused3;
	__kernel_off_t32	st_blksize;
	__kernel_off_t32	st_blocks;
	u32			__unused4[2];
};

struct compat_timespec {
	compat_time_t	tv_sec;
	s32		tv_nsec;
};

struct compat_timeval {
	compat_time_t	tv_sec;
	s32		tv_usec;
};

#endif /* _ASM_SPARC64_COMPAT_H */
