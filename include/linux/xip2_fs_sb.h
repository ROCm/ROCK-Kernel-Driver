/*
 *  linux/include/linux/xip2_fs_sb.h, Version 1
 *
 * (C) Copyright IBM Corp. 2002,2004
 * Author(s): Carsten Otte <cotte@de.ibm.com>
 * derived from second extended filesystem (ext2)
 */
#ifndef _LINUX_XIP2_FS_SB
#define _LINUX_XIP2_FS_SB

#ifndef _LINUX_EXT2_FS_SB
#include <linux/blockgroup_lock.h>
#include <linux/percpu_counter.h>
#endif

struct xip2_mem_area_t {
	char 		*name;
	unsigned long 	start;
	unsigned long	end;
};

/*
 * second extended-fs super-block data in memory
 */
struct xip2_sb_info {
	unsigned long s_frag_size;	/* Size of a fragment in bytes */
	unsigned long s_frags_per_block;/* Number of fragments per block */
	unsigned long s_inodes_per_block;/* Number of inodes per block */
	unsigned long s_frags_per_group;/* Number of fragments in a group */
	unsigned long s_blocks_per_group;/* Number of blocks in a group */
	unsigned long s_inodes_per_group;/* Number of inodes in a group */
	unsigned long s_itb_per_group;	/* Number of inode table blocks per group */
	unsigned long s_gdb_count;	/* Number of group descriptor blocks */
	unsigned long s_desc_per_block;	/* Number of group descriptors per block */
	unsigned long s_groups_count;	/* Number of groups in the fs */
	struct ext2_super_block * s_es;	/* Pointer to the super block in the buffer */
	void ** s_group_desc;
	unsigned long  s_mount_opt;
	uid_t s_resuid;
	gid_t s_resgid;
	unsigned short s_mount_state;
	unsigned short s_pad;
	int s_addr_per_block_bits;
	int s_desc_per_block_bits;
	int s_inode_size;
	int s_first_ino;
	spinlock_t s_next_gen_lock;
	u32 s_next_generation;
	unsigned long s_dir_count;
	u8 *s_debts;
	struct percpu_counter s_freeblocks_counter;
	struct percpu_counter s_freeinodes_counter;
	struct percpu_counter s_dirs_counter;
	struct blockgroup_lock s_blockgroup_lock;
	struct xip2_mem_area_t mem_area;
};

#endif	/* _LINUX_EXT2_FS_SB */
