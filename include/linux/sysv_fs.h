#ifndef _LINUX_SYSV_FS_H
#define _LINUX_SYSV_FS_H

#if defined(__GNUC__)
# define __packed2__	__attribute__((packed, aligned(2)))
#else
>> I want to scream! <<
#endif


/* inode numbers are 16 bit */
typedef u16 sysv_ino_t;

/* Block numbers are 24 bit, sometimes stored in 32 bit.
   On Coherent FS, they are always stored in PDP-11 manner: the least
   significant 16 bits come last. */
typedef u32 sysv_zone_t;

/* 0 is non-existent */
#define SYSV_BADBL_INO	1	/* inode of bad blocks file */
#define SYSV_ROOT_INO	2	/* inode of root directory */


/* Xenix super-block data on disk */
#define XENIX_NICINOD	100	/* number of inode cache entries */
#define XENIX_NICFREE	100	/* number of free block list chunk entries */
struct xenix_super_block {
	u16		s_isize; /* index of first data zone */
	u32		s_fsize __packed2__; /* total number of zones of this fs */
	/* the start of the free block list: */
	u16		s_nfree;	/* number of free blocks in s_free, <= XENIX_NICFREE */
	u32		s_free[XENIX_NICFREE]; /* first free block list chunk */
	/* the cache of free inodes: */
	u16		s_ninode; /* number of free inodes in s_inode, <= XENIX_NICINOD */
	sysv_ino_t	s_inode[XENIX_NICINOD]; /* some free inodes */
	/* locks, not used by Linux: */
	char		s_flock;	/* lock during free block list manipulation */
	char		s_ilock;	/* lock during inode cache manipulation */
	char		s_fmod;		/* super-block modified flag */
	char		s_ronly;	/* flag whether fs is mounted read-only */
	u32		s_time __packed2__; /* time of last super block update */
	u32		s_tfree __packed2__; /* total number of free zones */
	u16		s_tinode;	/* total number of free inodes */
	s16		s_dinfo[4];	/* device information ?? */
	char		s_fname[6];	/* file system volume name */
	char		s_fpack[6];	/* file system pack name */
	char		s_clean;	/* set to 0x46 when filesystem is properly unmounted */
	char		s_fill[371];
	s32		s_magic;	/* version of file system */
	s32		s_type;		/* type of file system: 1 for 512 byte blocks
								2 for 1024 byte blocks
								3 for 2048 byte blocks */
								
};

/*
 * SystemV FS comes in two variants:
 * sysv2: System V Release 2 (e.g. Microport), structure elements aligned(2).
 * sysv4: System V Release 4 (e.g. Consensys), structure elements aligned(4).
 */
#define SYSV_NICINOD	100	/* number of inode cache entries */
#define SYSV_NICFREE	50	/* number of free block list chunk entries */

/* SystemV4 super-block data on disk */
struct sysv4_super_block {
	u16	s_isize;	/* index of first data zone */
	u16	s_pad0;
	u32	s_fsize;	/* total number of zones of this fs */
	/* the start of the free block list: */
	u16	s_nfree;	/* number of free blocks in s_free, <= SYSV_NICFREE */
	u16	s_pad1;
	u32	s_free[SYSV_NICFREE]; /* first free block list chunk */
	/* the cache of free inodes: */
	u16	s_ninode;	/* number of free inodes in s_inode, <= SYSV_NICINOD */
	u16	s_pad2;
	sysv_ino_t     s_inode[SYSV_NICINOD]; /* some free inodes */
	/* locks, not used by Linux: */
	char	s_flock;	/* lock during free block list manipulation */
	char	s_ilock;	/* lock during inode cache manipulation */
	char	s_fmod;		/* super-block modified flag */
	char	s_ronly;	/* flag whether fs is mounted read-only */
	u32	s_time;		/* time of last super block update */
	s16	s_dinfo[4];	/* device information ?? */
	u32	s_tfree;	/* total number of free zones */
	u16	s_tinode;	/* total number of free inodes */
	u16	s_pad3;
	char	s_fname[6];	/* file system volume name */
	char	s_fpack[6];	/* file system pack name */
	s32	s_fill[12];
	s32	s_state;	/* file system state: 0x7c269d38-s_time means clean */
	s32	s_magic;	/* version of file system */
	s32	s_type;		/* type of file system: 1 for 512 byte blocks
								2 for 1024 byte blocks */
};

/* SystemV2 super-block data on disk */
struct sysv2_super_block {
	u16	s_isize; 		/* index of first data zone */
	u32	s_fsize __packed2__;	/* total number of zones of this fs */
	/* the start of the free block list: */
	u16	s_nfree;		/* number of free blocks in s_free, <= SYSV_NICFREE */
	u32	s_free[SYSV_NICFREE];	/* first free block list chunk */
	/* the cache of free inodes: */
	u16	s_ninode;		/* number of free inodes in s_inode, <= SYSV_NICINOD */
	sysv_ino_t     s_inode[SYSV_NICINOD]; /* some free inodes */
	/* locks, not used by Linux: */
	char	s_flock;		/* lock during free block list manipulation */
	char	s_ilock;		/* lock during inode cache manipulation */
	char	s_fmod;			/* super-block modified flag */
	char	s_ronly;		/* flag whether fs is mounted read-only */
	u32	s_time __packed2__;	/* time of last super block update */
	s16	s_dinfo[4];		/* device information ?? */
	u32	s_tfree __packed2__;	/* total number of free zones */
	u16	s_tinode;		/* total number of free inodes */
	char	s_fname[6];		/* file system volume name */
	char	s_fpack[6];		/* file system pack name */
	s32	s_fill[14];
	s32	s_state;		/* file system state: 0xcb096f43 means clean */
	s32	s_magic;		/* version of file system */
	s32	s_type;			/* type of file system: 1 for 512 byte blocks
								2 for 1024 byte blocks */
};

/* V7 super-block data on disk */
#define V7_NICINOD     100     /* number of inode cache entries */
#define V7_NICFREE     50      /* number of free block list chunk entries */
struct v7_super_block {
	u16    s_isize;        /* index of first data zone */
	u32    s_fsize __packed2__; /* total number of zones of this fs */
	/* the start of the free block list: */
	u16    s_nfree;        /* number of free blocks in s_free, <= V7_NICFREE */
	u32    s_free[V7_NICFREE]; /* first free block list chunk */
	/* the cache of free inodes: */
	u16    s_ninode;       /* number of free inodes in s_inode, <= V7_NICINOD */
	sysv_ino_t      s_inode[V7_NICINOD]; /* some free inodes */
	/* locks, not used by Linux or V7: */
	char    s_flock;        /* lock during free block list manipulation */
	char    s_ilock;        /* lock during inode cache manipulation */
	char    s_fmod;         /* super-block modified flag */
	char    s_ronly;        /* flag whether fs is mounted read-only */
	u32     s_time __packed2__; /* time of last super block update */
	/* the following fields are not maintained by V7: */
	u32     s_tfree __packed2__; /* total number of free zones */
	u16     s_tinode;       /* total number of free inodes */
	u16     s_m;            /* interleave factor */
	u16     s_n;            /* interleave factor */
	char    s_fname[6];     /* file system name */
	char    s_fpack[6];     /* file system pack name */
};

/* Coherent super-block data on disk */
#define COH_NICINOD	100	/* number of inode cache entries */
#define COH_NICFREE	64	/* number of free block list chunk entries */
struct coh_super_block {
	u16		s_isize;	/* index of first data zone */
	u32		s_fsize __packed2__; /* total number of zones of this fs */
	/* the start of the free block list: */
	u16 s_nfree;	/* number of free blocks in s_free, <= COH_NICFREE */
	u32		s_free[COH_NICFREE] __packed2__; /* first free block list chunk */
	/* the cache of free inodes: */
	u16		s_ninode;	/* number of free inodes in s_inode, <= COH_NICINOD */
	sysv_ino_t	s_inode[COH_NICINOD]; /* some free inodes */
	/* locks, not used by Linux: */
	char		s_flock;	/* lock during free block list manipulation */
	char		s_ilock;	/* lock during inode cache manipulation */
	char		s_fmod;		/* super-block modified flag */
	char		s_ronly;	/* flag whether fs is mounted read-only */
	u32		s_time __packed2__; /* time of last super block update */
	u32		s_tfree __packed2__; /* total number of free zones */
	u16		s_tinode;	/* total number of free inodes */
	u16		s_interleave_m;	/* interleave factor */
	u16		s_interleave_n;
	char		s_fname[6];	/* file system volume name */
	char		s_fpack[6];	/* file system pack name */
	u32		s_unique;	/* zero, not used */
};

/* SystemV/Coherent inode data on disk */
struct sysv_inode {
	u16 i_mode;
	u16 i_nlink;
	u16 i_uid;
	u16 i_gid;
	u32 i_size;
	u8  i_data[3*(10+1+1+1)];
	u8  i_gen;
	u32 i_atime;	/* time of last access */
	u32 i_mtime;	/* time of last modification */
	u32 i_ctime;	/* time of creation */
};

/* SystemV/Coherent directory entry on disk */
#define SYSV_NAMELEN	14	/* max size of name in struct sysv_dir_entry */
struct sysv_dir_entry {
	sysv_ino_t inode;
	char name[SYSV_NAMELEN]; /* up to 14 characters, the rest are zeroes */
};

#define SYSV_DIRSIZE	sizeof(struct sysv_dir_entry)	/* size of every directory entry */

#endif /* _LINUX_SYSV_FS_H */
