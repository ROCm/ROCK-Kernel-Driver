#ifndef _PARISC_STAT_H
#define _PARISC_STAT_H

#include <linux/types.h>

struct stat {
	dev_t		st_dev;		/* dev_t is 32 bits on parisc */
	ino_t		st_ino;		/* 32 bits */
	mode_t		st_mode;	/* 16 bits */
	nlink_t		st_nlink;	/* 16 bits */
	unsigned short	st_reserved1;	/* old st_uid */
	unsigned short	st_reserved2;	/* old st_gid */
	dev_t		st_rdev;
	off_t		st_size;
	time_t		st_atime;
	unsigned int	st_spare1;
	time_t		st_mtime;
	unsigned int	st_spare2;
	time_t		st_ctime;
	unsigned int	st_spare3;
	int		st_blksize;
	int		st_blocks;
	unsigned int	__unused1;	/* ACL stuff */
	dev_t		__unused2;	/* network */
	ino_t		__unused3;	/* network */
	unsigned int	__unused4;	/* cnodes */
	unsigned short	__unused5;	/* netsite */
	short		st_fstype;
	dev_t		st_realdev;
	unsigned short	st_basemode;
	unsigned short	st_spareshort;
	uid_t		st_uid;
	gid_t		st_gid;
	unsigned int	st_spare4[3];
};

typedef __kernel_off64_t	off64_t;

struct hpux_stat64 {
	dev_t		st_dev;		/* dev_t is 32 bits on parisc */
	ino_t           st_ino;         /* 32 bits */
	mode_t		st_mode;	/* 16 bits */
	nlink_t		st_nlink;	/* 16 bits */
	unsigned short	st_reserved1;	/* old st_uid */
	unsigned short	st_reserved2;	/* old st_gid */
	dev_t		st_rdev;
	off64_t		st_size;
	time_t		st_atime;
	unsigned int	st_spare1;
	time_t		st_mtime;
	unsigned int	st_spare2;
	time_t		st_ctime;
	unsigned int	st_spare3;
	int		st_blksize;
	__u64		st_blocks;
	unsigned int	__unused1;	/* ACL stuff */
	dev_t		__unused2;	/* network */
	ino_t           __unused3;      /* network */
	unsigned int	__unused4;	/* cnodes */
	unsigned short	__unused5;	/* netsite */
	short		st_fstype;
	dev_t		st_realdev;
	unsigned short	st_basemode;
	unsigned short	st_spareshort;
	uid_t		st_uid;
	gid_t		st_gid;
	unsigned int	st_spare4[3];
};
#define stat64	hpux_stat64

#endif
