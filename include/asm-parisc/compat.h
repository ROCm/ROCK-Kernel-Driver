#ifndef _ASM_PARISC_COMPAT_H
#define _ASM_PARISC_COMPAT_H
/*
 * Architecture specific compatibility types
 */
#include <linux/types.h>

#define COMPAT_USER_HZ 100

typedef u32	compat_size_t;
typedef s32	compat_ssize_t;
typedef s32	compat_time_t;
typedef s32	compat_clock_t;
typedef s32	compat_pid_t;
typedef u32	compat_uid_t;
typedef u32	compat_gid_t;
typedef u16	compat_mode_t;
typedef u32	compat_ino_t;
typedef u32	compat_dev_t;
typedef s32	compat_off_t;
typedef u16	compat_nlink_t;
typedef u16	compat_ipc_pid_t;
typedef s32	compat_daddr_t;
typedef u32	compat_caddr_t;

struct compat_timespec {
	compat_time_t		tv_sec;
	s32			tv_nsec;
};

struct compat_timeval {
	compat_time_t		tv_sec;
	s32			tv_usec;
};

struct compat_stat {
	compat_dev_t		st_dev;	/* dev_t is 32 bits on parisc */
	compat_ino_t		st_ino;	/* 32 bits */
	compat_mode_t		st_mode;	/* 16 bits */
	compat_nlink_t  	st_nlink;	/* 16 bits */
	u16			st_reserved1;	/* old st_uid */
	u16			st_reserved2;	/* old st_gid */
	compat_dev_t		st_rdev;
	compat_off_t		st_size;
	compat_time_t		st_atime;
	u32			st_atime_nsec;
	compat_time_t		st_mtime;
	u32			st_mtime_nsec;
	compat_time_t		st_ctime;
	u32			st_ctime_nsec;
	s32			st_blksize;
	s32			st_blocks;
	u32			__unused1;	/* ACL stuff */
	compat_dev_t		__unused2;	/* network */
	compat_ino_t		__unused3;	/* network */
	u32			__unused4;	/* cnodes */
	u16			__unused5;	/* netsite */
	short			st_fstype;
	compat_dev_t		st_realdev;
	u16			st_basemode;
	u16			st_spareshort;
	compat_uid_t		st_uid;
	compat_gid_t		st_gid;
	u32			st_spare4[3];
};

struct compat_flock {
	short			l_type;
	short			l_whence;
	compat_off_t		l_start;
	compat_off_t		l_len;
	compat_pid_t		l_pid;
};

#endif /* _ASM_PARISC_COMPAT_H */
