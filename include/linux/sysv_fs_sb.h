#ifndef _SYSV_FS_SB
#define _SYSV_FS_SB

/*
 * SystemV/V7/Coherent super-block data in memory
 * The SystemV/V7/Coherent superblock contains dynamic data (it gets modified
 * while the system is running). This is in contrast to the Minix and Berkeley
 * filesystems (where the superblock is never modified). This affects the
 * sync() operation: we must keep the superblock in a disk buffer and use this
 * one as our "working copy".
 */

struct sysv_sb_info {
	struct super_block *s_sb;	/* VFS superblock */
	int	       s_type;		/* file system type: FSTYPE_{XENIX|SYSV|COH} */
	char	       s_bytesex;	/* bytesex (le/be/pdp) */
	char	       s_truncate;	/* if 1: names > SYSV_NAMELEN chars are truncated */
					/* if 0: they are disallowed (ENAMETOOLONG) */
	nlink_t        s_link_max;	/* max number of hard links to a file */
	unsigned int   s_inodes_per_block;	/* number of inodes per block */
	unsigned int   s_inodes_per_block_1;	/* inodes_per_block - 1 */
	unsigned int   s_inodes_per_block_bits;	/* log2(inodes_per_block) */
	unsigned int   s_ind_per_block;		/* number of indirections per block */
	unsigned int   s_ind_per_block_bits;	/* log2(ind_per_block) */
	unsigned int   s_ind_per_block_2;	/* ind_per_block ^ 2 */
	unsigned int   s_toobig_block;		/* 10 + ipb + ipb^2 + ipb^3 */
	unsigned int   s_block_base;	/* physical block number of block 0 */
	unsigned short s_fic_size;	/* free inode cache size, NICINOD */
	unsigned short s_flc_size;	/* free block list chunk size, NICFREE */
	/* The superblock is kept in one or two disk buffers: */
	struct buffer_head *s_bh1;
	struct buffer_head *s_bh2;
	/* These are pointers into the disk buffer, to compensate for
	   different superblock layout. */
	char *         s_sbd1;		/* entire superblock data, for part 1 */
	char *         s_sbd2;		/* entire superblock data, for part 2 */
	u16            *s_sb_fic_count;	/* pointer to s_sbd->s_ninode */
        u16            *s_sb_fic_inodes; /* pointer to s_sbd->s_inode */
	u16            *s_sb_total_free_inodes; /* pointer to s_sbd->s_tinode */
	u16            *s_bcache_count;	/* pointer to s_sbd->s_nfree */
	u32	       *s_bcache;	/* pointer to s_sbd->s_free */
	u32            *s_free_blocks;	/* pointer to s_sbd->s_tfree */
	u32            *s_sb_time;	/* pointer to s_sbd->s_time */
	u32            *s_sb_state;	/* pointer to s_sbd->s_state, only FSTYPE_SYSV */
	/* We keep those superblock entities that don't change here;
	   this saves us an indirection and perhaps a conversion. */
	u32            s_firstinodezone; /* index of first inode zone */
	u32            s_firstdatazone;	/* same as s_sbd->s_isize */
	u32            s_ninodes;	/* total number of inodes */
	u32            s_ndatazones;	/* total number of data zones */
	u32            s_nzones;	/* same as s_sbd->s_fsize */
	u16	       s_namelen;       /* max length of dir entry */
};
/* The field s_toobig_block is currently unused. */

#endif
