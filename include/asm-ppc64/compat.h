#ifndef _ASM_PPC64_COMPAT_H
#define _ASM_PPC64_COMPAT_H
/*
 * Architecture specific compatibility types
 */
#include <linux/types.h>

#define COMPAT_USER_HZ	100

typedef u32		compat_size_t;
typedef s32		compat_ssize_t;
typedef s32		compat_time_t;
typedef s32		compat_clock_t;
typedef s32		compat_pid_t;
typedef u32		compat_uid_t;
typedef u32		compat_gid_t;
typedef u32		compat_mode_t;
typedef u32		compat_ino_t;
typedef u32		compat_dev_t;
typedef s32		compat_off_t;
typedef s64		compat_loff_t;
typedef s16		compat_nlink_t;
typedef u16		compat_ipc_pid_t;
typedef s32		compat_daddr_t;
typedef u32		compat_caddr_t;
typedef __kernel_fsid_t	compat_fsid_t;

struct compat_timespec {
	compat_time_t	tv_sec;
	s32		tv_nsec;
};

struct compat_timeval {
	compat_time_t	tv_sec;
	s32		tv_usec;
};

struct compat_stat {
	compat_dev_t	st_dev;
	compat_ino_t	st_ino;
	compat_mode_t	st_mode;
	compat_nlink_t	st_nlink;	
	compat_uid_t	st_uid;
	compat_gid_t	st_gid;
	compat_dev_t	st_rdev;
	compat_off_t	st_size;
	compat_off_t	st_blksize;
	compat_off_t	st_blocks;
	compat_time_t	st_atime;
	u32		__unused1;
	compat_time_t	st_mtime;
	u32		__unused2;
	compat_time_t	st_ctime;
	u32		__unused3;
	u32		__unused4[2];
};

struct compat_flock {
	short		l_type;
	short		l_whence;
	compat_off_t	l_start;
	compat_off_t	l_len;
	compat_pid_t	l_pid;
	short		__unused;
};

struct compat_statfs {
	int		f_type;
	int		f_bsize;
	int		f_blocks;
	int		f_bfree;
	int		f_bavail;
	int		f_files;
	int		f_ffree;
	compat_fsid_t	f_fsid;
	int		f_namelen;	/* SunOS ignores this field. */
	int		f_spare[6];
};

#endif /* _ASM_PPC64_COMPAT_H */
