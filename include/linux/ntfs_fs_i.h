#ifndef _LINUX_NTFS_FS_I_H
#define _LINUX_NTFS_FS_I_H

/*
 * Clusters are signed 64-bit values on NTFS volumes. We define two types, LCN
 * and VCN, to allow for type checking and better code readability.
 */
typedef __s64 VCN;
typedef __s64 LCN;

/**
 * run_list - in memory vcn to lcn mapping array
 * @vcn:	starting vcn of the current array element
 * @lcn:	starting lcn of the current array element
 * @length:	length in clusters of the current array element
 * 
 * The last vcn (in fact the last vcn + 1) is reached when length == 0.
 * 
 * When lcn == -1 this means that the count vcns starting at vcn are not 
 * physically allocated (i.e. this is a hole / data is sparse).
 */
typedef struct {	/* In memory vcn to lcn mapping structure element. */
	VCN vcn;	/* vcn = Starting virtual cluster number. */
	LCN lcn;	/* lcn = Starting logical cluster number. */
	__s64 length;	/* Run length in clusters. */
} run_list;

/*
 * The NTFS in-memory inode structure. It is just used as an extension to the
 * fields already provided in the VFS inode.
 */
struct ntfs_inode_info {
	struct inode *inode;	/* Pointer to the inode structure of this
				   ntfs_inode_info structure. */
	unsigned long state;	/* NTFS specific flags describing this inode.
				   See fs/ntfs/ntfs.h:ntfs_inode_state_bits. */
	run_list *run_list;	/* If state has the NI_NonResident bit set,
				   the run list of the unnamed data attribute
				   (if a file) or of the index allocation
				   attribute (directory). If run_list is NULL,
				   the run list has not been read in or has
				   been unmapped. If NI_NonResident is clear,
				   the unnamed data attribute is resident (file)
				   or there is no $I30 index allocation
				   attribute (directory). In that case run_list
				   is always NULL.*/
	__s32 nr_extents;	/* The number of extents[], if this is a base
				   mft record, -1 if this is an extent record,
				   and 0 if there are no extents. */
	struct rw_semaphore mrec_lock;	/* Lock for serializing access to the
				   mft record belonging to this inode. */
	struct page *page;	/* The page containing the mft record of the
				   inode. This should only be touched by the
				   (un)map_mft_record_for_*() functions. Do NOT
				   touch from anywhere else or the ntfs divil
				   will appear and take your heart out with a
				   blunt spoon! You have been warned. (-8 */
	union {
		struct { /* It is a directory. */
			__u32 index_block_size;	/* Size of an index block. */
			__u8 index_block_size_bits; /* Log2 of the size of an
						       an index block. */
			__s64 bmp_size;		/* Size of the $I30 bitmap. */
			run_list *bmp_rl;	/* Run list for the $I30 bitmap
						   if it is non-resident. */
		};
		struct { /* It is a compressed file. */
			__u32 compression_block_size; /* Size of a compression
						         block (cb). */
			__u8 compression_block_size_bits; /* Log2 of the size
							     of a cb. */
			__u8 compression_block_clusters; /* Number of clusters
							    per compression
							    block. */
		};
	};
	union {		/* This union is only used if nr_extents != 0. */
		struct {	/* nr_extents > 0 */
			__s64 i_ino;		/* The inode number of the
						   extent mft record. */
			__u32 i_generation;	/* The i_generation of the
						   extent mft record. */
		} *extents;	/* The currently known of extents, sorted in
				   ascending order. */
		struct {	/* nr_exents == -1 */
			__s64 i_ino;		/* The inode number of the base
						   mft record of this extent. */
			__u32 i_generation;	/* The i_generation of the base
						   mft record. */
		} base;		/* The base mft record of this extent. */
	};
};

#endif /* _LINUX_NTFS_FS_I_H */

