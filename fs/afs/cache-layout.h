/* cache-layout.h: AFS cache layout
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 *
 * The cache is stored on a block device and is laid out as:
 *
 *  0	+------------------------------------------------
 *	|
 *	|  SuperBlock
 *	|
 *  1	+------------------------------------------------
 *	|
 *	|  file-meta-data File: Data block #0
 *	|  - file-meta-data file (volix #0 file #0) : Meta-data block
 *	|    - contains direct pointers to first 64 file data blocks
 *	|  - Cached cell catalogue file (volix #0 file #1) file: Meta-data block
 *	|  - Cached volume location catalogue file (volix #0 file #2): Meta-data block
 *	|  - Vnode catalogue hash bucket #n file: Meta-data block
 *	|
 *  2	+------------------------------------------------
 *	|
 *	|  Bitmap Block Allocation Bitmap
 *	|  - 1 bit per block in the bitmap block
 *      |  - bit 0 of dword 0 refers to the bitmap block 0
 *	|    - set if the bitmap block is full
 *      |  - 32768 bits per block, requiring 4 blocks for a 16Tb cache
 *	|  - bitmap bitmap blocks are cleared initially
 *	|  - not present if <4 bitmap blocks
 *	|
 *	+------------------------------------------------
 *	|
 *	|  File Block Allocation Bitmap
 *	|  - 1 bit per block in the cache
 *      |  - bit 0 of dword 0 refers to the first block of the data cache
 *	|    - set if block is allocated
 *      |  - 32768 bits per block, requiring 131072 blocks for a 16Tb cache
 *	|  - bitmap blocks are cleared lazily (sb->bix_bitmap_unready)
 *	|
 *	+------------------------------------------------
 *	|
 *	|  Data Cache
 *	|
 *  End	+------------------------------------------------
 *
 * Blocks are indexed by an unsigned 32-bit word, meaning that the cache can hold up to 2^32 pages,
 * or 16Tb in total.
 *
 * Credentials will be cached in memory, since they are subject to change without notice, and are
 * difficult to derive manually, being constructed from the following information:
 * - per vnode user ID and mode mask
 * - parent directory ACL
 * - directory ACL (dirs only)
 * - group lists from ptserver
 */

#ifndef _LINUX_AFS_CACHE_LAYOUT_H
#define _LINUX_AFS_CACHE_LAYOUT_H

#include "types.h"

typedef u32 afsc_blockix_t;
typedef u32 afsc_cellix_t;

/* Cached volume index
 * - afsc_volix_t/4 is the index into the volume cache
 * - afsc_volix_t%4 is 0 for R/W, 1 for R/O and 2 for Bak (3 is not used)
 * - afsc_volix_t==0-3 refers to a "virtual" volume that stores meta-data about the cache
 */
typedef struct {
	u32 index;
} afsc_volix_t;

#define AFSC_VNCAT_HASH_NBUCKETS	128

/* special meta file IDs (all cell 0 vol 0) */
enum afsc_meta_fids {
	AFSC_META_FID_METADATA		= 0,
	AFSC_META_FID_CELL_CATALOGUE	= 1,
	AFSC_META_FID_VLDB_CATALOGUE	= 2,
	AFSC_META_FID_VNODE_CATALOGUE0	= 3,
	AFSC_META_FID__COUNT		= AFSC_VNCAT_HASH_NBUCKETS + 3
};

/*****************************************************************************/
/*
 * cache superblock block layout
 * - the blockdev is prepared for initialisation by 'echo "kafsuninit" >/dev/hdaXX' before mounting
 * - when initialised, the magic number is changed to "kafs-cache"
 */
struct afsc_super_block
{
	char			magic[10];	/* magic number */
#define AFSC_SUPER_MAGIC "kafs-cache"
#define AFSC_SUPER_MAGIC_NEEDS_INIT "kafsuninit"
#define AFSC_SUPER_MAGIC_SIZE 10

	unsigned short		endian;		/* 0x1234 stored CPU-normal order */
#define AFSC_SUPER_ENDIAN 0x1234

	unsigned		version;	/* format version */
#define AFSC_SUPER_VERSION 1

	/* layout */
	unsigned		bsize;			/* cache block size */
	afsc_blockix_t		bix_bitmap_fullmap;	/* block ix of bitmap full bitmap */
	afsc_blockix_t		bix_bitmap;		/* block ix of alloc bitmap */
	afsc_blockix_t		bix_bitmap_unready;	/* block ix of unready area of bitmap */
	afsc_blockix_t		bix_cache;		/* block ix of data cache */
	afsc_blockix_t		bix_end;		/* block ix of end of cache */
};

/*****************************************************************************/
/*
 * vnode (inode) metadata cache record
 * - padded out to 512 bytes and stored eight to a page
 * - only the data version is necessary
 *   - disconnected operation is not supported
 *   - afs_iget() contacts the server to get the meta-data _anyway_ when an inode is first brought
 *     into memory
 * - at least 64 direct block pointers will be available (a directory is max 256Kb)
 * - any block pointer which is 0 indicates an uncached page
 */
struct afsc_vnode_meta
{
	/* file ID */
	afsc_volix_t		volume_ix;	/* volume catalogue index */
	unsigned		vnode;		/* vnode number */
	unsigned		unique;		/* FID unique */
	unsigned		size;		/* size of file */
	time_t			mtime;		/* last modification time */

	/* file status */
	afs_dataversion_t	version;	/* current data version */

	/* file contents */
	afsc_blockix_t		dbl_indirect;	/* double indirect block index */
	afsc_blockix_t		indirect;	/* single indirect block 0 index */
	afsc_blockix_t		direct[0];	/* direct block index (#AFSC_VNODE_META_DIRECT) */
};

#define AFSC_VNODE_META_RECSIZE	512	/* record size */

#define AFSC_VNODE_META_DIRECT	\
	((AFSC_VNODE_META_RECSIZE-sizeof(struct afsc_vnode_meta))/sizeof(afsc_blockix_t))

#define AFSC_VNODE_META_PER_PAGE	(PAGE_SIZE / AFSC_VNODE_META_RECSIZE)

/*****************************************************************************/
/*
 * entry in the cached cell catalogue
 */
struct afsc_cell_record
{
	char			name[64];	/* cell name (padded with NULs) */
	struct in_addr		servers[16];	/* cached cell servers */
};

/*****************************************************************************/
/*
 * entry in the cached volume location catalogue
 * - indexed by afsc_volix_t/4
 */
struct afsc_vldb_record
{
	char			name[64];	/* volume name (padded with NULs) */
	afs_volid_t		vid[3];		/* volume IDs for R/W, R/O and Bak volumes */
	unsigned char		vidmask;	/* voltype mask for vid[] */
	unsigned char		_pad[1];
	unsigned short		nservers;	/* number of entries used in servers[] */
	struct in_addr		servers[8];	/* fileserver addresses */
	unsigned char		srvtmask[8];	/* voltype masks for servers[] */
#define AFSC_VOL_STM_RW	0x01 /* server holds a R/W version of the volume */
#define AFSC_VOL_STM_RO	0x02 /* server holds a R/O version of the volume */
#define AFSC_VOL_STM_BAK	0x04 /* server holds a backup version of the volume */

	afsc_cellix_t		cell_ix;	/* cell catalogue index (MAX_UINT if unused) */
	time_t			ctime;		/* time at which cached */
};

/*****************************************************************************/
/*
 * vnode catalogue entry
 * - must be 2^x size so that do_generic_file_read doesn't present them split across pages
 */
struct afsc_vnode_catalogue
{
	afsc_volix_t		volume_ix;	/* volume catalogue index */
	afs_vnodeid_t		vnode;		/* vnode ID */
	u32			meta_ix;	/* metadata file index */
	u32			atime;		/* last time entry accessed */
} __attribute__((packed));

#define AFSC_VNODE_CATALOGUE_PER_BLOCK ((size_t)(PAGE_SIZE/sizeof(struct afsc_vnode_catalogue)))

/*****************************************************************************/
/*
 * vnode data "page directory" block
 * - first 1024 pages don't map through here
 * - PAGE_SIZE in size
 */
struct afsc_indirect_block
{
	afsc_blockix_t		pt_bix[1024];	/* "page table" block indices */
};

/*****************************************************************************/
/*
 * vnode data "page table" block
 * - PAGE_SIZE in size
 */
struct afsc_dbl_indirect_block
{
	afsc_blockix_t		page_bix[1024];	/* "page" block indices */
};


#endif /* _LINUX_AFS_CACHE_LAYOUT_H */
