#ifndef _LINUX_SYSV_FS_H
#define _LINUX_SYSV_FS_H

/*
 * The SystemV/Coherent filesystem constants/structures/macros
 */


/* This code assumes
   - a little endian processor like 386,
   - sizeof(short) = 2, sizeof(int) = 4, sizeof(long) = 4,
   - alignof(short) = 2, alignof(long) = 4.
*/

#ifdef __GNUC__
#define __packed2__  __attribute__ ((packed, aligned(2)))
#else
#error I want gcc!
#endif

#include <linux/stat.h>		/* declares S_IFLNK etc. */
#include <linux/sched.h>	/* declares wake_up() */
#include <linux/sysv_fs_sb.h>	/* defines the sv_... shortcuts */


/* Layout on disk */
/* ============== */


/* The block size is sb->sv_block_size which may be smaller than BLOCK_SIZE. */

/* zones (= data allocation units) are blocks */

/* On Coherent FS, 32 bit quantities are stored using (I quote the Coherent
   manual) a "canonical byte ordering". This is the PDP-11 byte ordering:
   x = 2^24 * byte3 + 2^16 * byte2 + 2^8 * byte1 + byte0 is stored
   as { byte2, byte3, byte0, byte1 }. We need conversions.
*/

typedef u32 coh_ulong;

static inline coh_ulong to_coh_ulong (u32 x)
{
	return ((x & 0xffff) << 16) | ((x & 0xffff0000) >> 16);
}

static inline u32 from_coh_ulong (coh_ulong x)
{
	return ((x & 0xffff) << 16) | ((x & 0xffff0000) >> 16);
}

/* inode numbers are 16 bit */

typedef u16 sysv_ino_t;

/* Block numbers are 24 bit, sometimes stored in 32 bit.
   On Coherent FS, they are always stored in PDP-11 manner: the least
   significant 16 bits come last.
*/

typedef u32 sysv_zone_t;

/* Among the blocks ... */
/* Xenix FS, Coherent FS: block 0 is the boot block, block 1 the super-block.
   SystemV FS: block 0 contains both the boot sector and the super-block. */
/* The first inode zone is sb->sv_firstinodezone (1 or 2). */

/* Among the inodes ... */
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

/* Xenix free list block on disk */
struct xenix_freelist_chunk {
	u16	fl_nfree;	/* number of free blocks in fl_free, <= XENIX_NICFREE] */
	u32	fl_free[XENIX_NICFREE] __packed2__;
};

/* SystemV FS comes in two variants:
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

/* SystemV4 free list block on disk */
struct sysv4_freelist_chunk {
	u16 fl_nfree;	/* number of free blocks in fl_free, <= SYSV_NICFREE] */
	u32  fl_free[SYSV_NICFREE];
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

/* SystemV2 free list block on disk */
struct sysv2_freelist_chunk {
	u16	fl_nfree;	/* number of free blocks in fl_free, <= SYSV_NICFREE] */
	u32	fl_free[SYSV_NICFREE] __packed2__;
};

/* Coherent super-block data on disk */
#define COH_NICINOD	100	/* number of inode cache entries */
#define COH_NICFREE	64	/* number of free block list chunk entries */
struct coh_super_block {
	u16		s_isize;	/* index of first data zone */
	coh_ulong	s_fsize __packed2__; /* total number of zones of this fs */
	/* the start of the free block list: */
	u16 s_nfree;	/* number of free blocks in s_free, <= COH_NICFREE */
	coh_ulong	s_free[COH_NICFREE] __packed2__; /* first free block list chunk */
	/* the cache of free inodes: */
	u16		s_ninode;	/* number of free inodes in s_inode, <= COH_NICINOD */
	sysv_ino_t	s_inode[COH_NICINOD]; /* some free inodes */
	/* locks, not used by Linux: */
	char		s_flock;	/* lock during free block list manipulation */
	char		s_ilock;	/* lock during inode cache manipulation */
	char		s_fmod;		/* super-block modified flag */
	char		s_ronly;	/* flag whether fs is mounted read-only */
	coh_ulong	s_time __packed2__; /* time of last super block update */
	coh_ulong	s_tfree __packed2__; /* total number of free zones */
	u16		s_tinode;	/* total number of free inodes */
	u16		s_interleave_m;	/* interleave factor */
	u16		s_interleave_n;
	char		s_fname[6];	/* file system volume name */
	char		s_fpack[6];	/* file system pack name */
	u32		s_unique;	/* zero, not used */
};

/* Coherent free list block on disk */
struct coh_freelist_chunk {
	u16 fl_nfree;	/* number of free blocks in fl_free, <= COH_NICFREE] */
	u32  fl_free[COH_NICFREE] __packed2__;
};


/* SystemV/Coherent inode data on disk */

struct sysv_inode {
	u16 i_mode;
	u16 i_nlink;
	u16 i_uid;
	u16 i_gid;
	u32 i_size;
	union { /* directories, regular files, ... */
		unsigned char i_addb[3*(10+1+1+1)+1]; /* zone numbers: max. 10 data blocks,
					      * then 1 indirection block,
					      * then 1 double indirection block,
					      * then 1 triple indirection block.
					      * Then maybe a "file generation number" ??
					      */
		/* devices */
		dev_t i_rdev;
		/* named pipes on Coherent */
		struct {
			char p_addp[30];
			s16 p_pnc;
			s16 p_prx;
			s16 p_pwx;
		} i_p;
	} i_a;
	u32 i_atime;	/* time of last access */
	u32 i_mtime;	/* time of last modification */
	u32 i_ctime;	/* time of creation */
};

/* The admissible values for i_mode are listed in <linux/stat.h> :
 * #define S_IFMT  00170000  mask for type
 * #define S_IFREG  0100000  type = regular file
 * #define S_IFBLK  0060000  type = block device
 * #define S_IFDIR  0040000  type = directory
 * #define S_IFCHR  0020000  type = character device
 * #define S_IFIFO  0010000  type = named pipe
 * #define S_ISUID  0004000  set user id
 * #define S_ISGID  0002000  set group id
 * #define S_ISVTX  0001000  save swapped text even after use
 * Additionally for SystemV:
 * #define S_IFLNK  0120000  type = symbolic link
 * #define S_IFNAM  0050000  type = XENIX special named file ??
 * Additionally for Coherent:
 * #define S_IFMPB  0070000  type = multiplexed block device ??
 * #define S_IFMPC  0030000  type = multiplexed character device ??
 *
 * Since Coherent doesn't know about symbolic links, we use a kludgey
 * implementation of symbolic links: i_mode = COH_KLUDGE_SYMLINK_MODE
 * denotes a symbolic link. When a regular file should get this mode by
 * accident, it is automatically converted to COH_KLUDGE_NOT_SYMLINK.
 * We use S_IFREG because only regular files (and Coherent pipes...) can have
 * data blocks with arbitrary contents associated with them, and S_ISVTX
 * ("save swapped text after use") because it is unused on both Linux and
 * Coherent: Linux does much more intelligent paging, and Coherent hasn't
 * virtual memory at all.
 * Same trick for Xenix.
 */
#define COH_KLUDGE_SYMLINK_MODE	(S_IFREG | S_ISVTX)
#define COH_KLUDGE_NOT_SYMLINK	(S_IFREG | S_ISVTX | S_IRUSR) /* force read access */
extern inline mode_t from_coh_imode(unsigned short mode)
{
	if (mode == COH_KLUDGE_SYMLINK_MODE)
		return (S_IFLNK | 0777);
	else
		return mode;
}
extern inline unsigned short to_coh_imode(mode_t mode)
{
	if (S_ISLNK(mode))
		return COH_KLUDGE_SYMLINK_MODE;
	else if (mode == COH_KLUDGE_SYMLINK_MODE)
		return COH_KLUDGE_NOT_SYMLINK;
	else
		return mode;
}

/* Admissible values for i_nlink: 0.._LINK_MAX */
#define XENIX_LINK_MAX	126	/* ?? */
#define SYSV_LINK_MAX	126	/* 127? 251? */
#define COH_LINK_MAX	10000	/* max number of hard links to an inode */

/* The number of inodes per block is
   sb->sv_inodes_per_block = block_size / sizeof(struct sysv_inode) */
/* The number of indirect pointers per block is
   sb->sv_ind_per_block = block_size / sizeof(u32) */


/* SystemV/Coherent directory entry on disk */

#define SYSV_NAMELEN	14	/* max size of name in struct sysv_dir_entry */

struct sysv_dir_entry {
	sysv_ino_t inode;
	char name[SYSV_NAMELEN]; /* up to 14 characters, the rest are zeroes */
};

#define SYSV_DIRSIZE	sizeof(struct sysv_dir_entry)	/* size of every directory entry */


/* Operations */
/* ========== */


/* identify the FS in memory */
#define FSTYPE_XENIX	1
#define FSTYPE_SYSV4	2
#define FSTYPE_SYSV2	3
#define FSTYPE_COH	4

#define SYSV_MAGIC_BASE		0x012FF7B3

#define XENIX_SUPER_MAGIC	(SYSV_MAGIC_BASE+FSTYPE_XENIX)
#define SYSV4_SUPER_MAGIC	(SYSV_MAGIC_BASE+FSTYPE_SYSV4)
#define SYSV2_SUPER_MAGIC	(SYSV_MAGIC_BASE+FSTYPE_SYSV2)
#define COH_SUPER_MAGIC		(SYSV_MAGIC_BASE+FSTYPE_COH)

#ifdef __KERNEL__

/* sv_get_hash_table(sb,dev,block) is equivalent to  get_hash_table(dev,block,block_size)  */
static inline struct buffer_head *
sv_get_hash_table (struct super_block *sb, kdev_t dev, unsigned int block)
{
	return get_hash_table (dev, block + sb->sv_block_base, sb->sv_block_size);
}

/* sv_getblk(sb,dev,block) is equivalent to  getblk(dev,block,block_size)  */
static inline struct buffer_head *
sv_getblk (struct super_block *sb, kdev_t dev, unsigned int block)
{
	return getblk (dev, block + sb->sv_block_base, sb->sv_block_size);
}

/* sv_bread(sb,dev,block) is equivalent to  bread(dev,block,block_size)  */
static inline struct buffer_head *
sv_bread (struct super_block *sb, kdev_t dev, unsigned int block)
{
	return bread (dev, block + sb->sv_block_base, sb->sv_block_size);
}


/*
 * Function prototypes
 */

extern struct inode * sysv_new_inode(const struct inode * dir);
extern void sysv_free_inode(struct inode * inode);
extern unsigned long sysv_count_free_inodes(struct super_block *sb);
extern int sysv_new_block(struct super_block * sb);
extern void sysv_free_block(struct super_block * sb, unsigned int block);
extern unsigned long sysv_count_free_blocks(struct super_block *sb);

extern struct buffer_head * sysv_file_bread(struct inode *, int, int);

extern void sysv_truncate(struct inode *);
extern void sysv_write_inode(struct inode *, int);
extern int sysv_sync_inode(struct inode *);
extern int sysv_sync_file(struct file *, struct dentry *, int);
extern int sysv_notify_change(struct dentry *, struct iattr *);

extern struct inode_operations sysv_file_inode_operations;
extern struct inode_operations sysv_symlink_inode_operations;
extern struct inode_operations sysv_dir_inode_operations;
extern struct file_operations sysv_file_operations;
extern struct file_operations sysv_dir_operations;
extern struct address_space_operations sysv_aops;

#endif /* __KERNEL__ */

#endif

