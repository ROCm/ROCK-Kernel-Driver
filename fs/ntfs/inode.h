/*
 * inode.h - Defines for inode structures NTFS Linux kernel driver. Part of
 *	     the Linux-NTFS project.
 *
 * Copyright (c) 2001,2002 Anton Altaparmakov.
 * Copyright (C) 2002 Richard Russon.
 *
 * This program/include file is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program/include file is distributed in the hope that it will be 
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty 
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the Linux-NTFS 
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation,Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef _LINUX_NTFS_INODE_H
#define _LINUX_NTFS_INODE_H

#include <linux/seq_file.h>

#include "volume.h"

typedef struct _ntfs_inode ntfs_inode;

/*
 * The NTFS in-memory inode structure. It is just used as an extension to the
 * fields already provided in the VFS inode.
 */
struct _ntfs_inode {
	s64 initialized_size;	/* Copy from $DATA/$INDEX_ALLOCATION. */
	s64 allocated_size;	/* Copy from $DATA/$INDEX_ALLOCATION. */
	unsigned long state;	/* NTFS specific flags describing this inode.
				   See fs/ntfs/ntfs.h:ntfs_inode_state_bits. */
	u64 mft_no;		/* Mft record number (inode number). */
	u16 seq_no;		/* Sequence number of the mft record. */
	atomic_t count;		/* Inode reference count for book keeping. */
	ntfs_volume *vol;	/* Pointer to the ntfs volume of this inode. */
	run_list run_list;	/* If state has the NI_NonResident bit set,
				   the run list of the unnamed data attribute
				   (if a file) or of the index allocation
				   attribute (directory). If run_list.rl is
				   NULL, the run list has not been read in or
				   has been unmapped. If NI_NonResident is
				   clear, the unnamed data attribute is
				   resident (file) or there is no $I30 index
				   allocation attribute (directory). In that
				   case run_list.rl is always NULL.*/
	struct rw_semaphore mrec_lock;	/* Lock for serializing access to the
				   mft record belonging to this inode. */
	atomic_t mft_count;	/* Mapping reference count for book keeping. */
	struct page *page;	/* The page containing the mft record of the
				   inode. This should only be touched by the
				   (un)map_mft_record*() functions. */
	int page_ofs;		/* Offset into the page at which the mft record
				   begins. This should only be touched by the
				   (un)map_mft_record*() functions. */
	/*
	 * Attribute list support (only for use by the attribute lookup
	 * functions). Setup during read_inode for all inodes with attribute
	 * lists. Only valid if NI_AttrList is set in state, and attr_list_rl is
	 * further only valid if NI_AttrListNonResident is set.
	 */
	u32 attr_list_size;	/* Length of attribute list value in bytes. */
	u8 *attr_list;		/* Attribute list value itself. */
	run_list attr_list_rl;	/* Run list for the attribute list value. */
	union {
		struct { /* It is a directory or $MFT. */
			u32 index_block_size;	/* Size of an index block. */
			u8 index_block_size_bits; /* Log2 of the above. */
			u32 index_vcn_size;	/* Size of a vcn in this
						   directory index. */
			u8 index_vcn_size_bits;	/* Log2 of the above. */
			s64 bmp_size;		/* Size of the $I30 bitmap. */
			s64 bmp_initialized_size; /* Copy from $I30 bitmap. */
			s64 bmp_allocated_size;	/* Copy from $I30 bitmap. */
			run_list bmp_rl;	/* Run list for the $I30 bitmap
						   if it is non-resident. */
		} SN(idm);
		struct { /* It is a compressed file. */
			u32 compression_block_size;     /* Size of a compression
						           block (cb). */
			u8 compression_block_size_bits; /* Log2 of the size of
							   a cb. */
			u8 compression_block_clusters;  /* Number of clusters
							   per compression
							   block. */
			s64 compressed_size;		/* Copy from $DATA. */
		} SN(icf);
	} SN(idc);
	struct semaphore extent_lock;	/* Lock for accessing/modifying the
					   below . */
	s32 nr_extents;	/* For a base mft record, the number of attached extent
			   inodes (0 if none), for extent records this is -1. */
	union {		/* This union is only used if nr_extents != 0. */
		ntfs_inode **extent_ntfs_inos;	/* For nr_extents > 0, array of
						   the ntfs inodes of the extent
						   mft records belonging to
						   this base inode which have
						   been loaded. */
		ntfs_inode *base_ntfs_ino;	/* For nr_extents == -1, the
						   ntfs inode of the base mft
						   record. */
	} SN(ine);
};

#define _IDM(X)  SC(idc.idm,X)
#define _ICF(X)  SC(idc.icf,X)
#define _INE(X)  SC(ine,X)

typedef struct {
	ntfs_inode ntfs_inode;
	struct inode vfs_inode;		/* The vfs inode structure. */
} big_ntfs_inode;

/**
 * NTFS_I - return the ntfs inode given a vfs inode
 * @inode:	VFS inode
 *
 * NTFS_I() returns the ntfs inode associated with the VFS @inode.
 */
static inline ntfs_inode *NTFS_I(struct inode *inode)
{
	return (ntfs_inode *)list_entry(inode, big_ntfs_inode, vfs_inode);
}

static inline struct inode *VFS_I(ntfs_inode *ni)
{
	return &((big_ntfs_inode*)ni)->vfs_inode;
}

extern struct inode *ntfs_alloc_big_inode(struct super_block *sb);
extern void ntfs_destroy_big_inode(struct inode *inode);
extern void ntfs_clear_big_inode(struct inode *vi);

extern ntfs_inode *ntfs_new_inode(struct super_block *sb);
extern void ntfs_clear_inode(ntfs_inode *ni);

extern void ntfs_read_inode(struct inode *vi);
extern void ntfs_read_inode_mount(struct inode *vi);

extern void ntfs_dirty_inode(struct inode *vi);

extern int ntfs_show_options(struct seq_file *sf, struct vfsmount *mnt);

#endif /* _LINUX_NTFS_FS_INODE_H */

