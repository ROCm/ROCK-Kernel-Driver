/*
 *  linux/fs/xip2fs/balloc.c, Version 1
 *
 * (C) Copyright IBM Corp. 2002,2004
 * Author(s): Carsten Otte <cotte@de.ibm.com>
 *            Gerald Schaefer <geraldsc@de.ibm.com>
 * derived from second extended filesystem (ext2)
 */

#include <linux/config.h>
#include "xip2.h"
#include <linux/quotaops.h>
#include <linux/sched.h>

/*
 * balloc.c contains the blocks allocation and deallocation routines
 */

/*
 * The free blocks are managed by bitmaps.  A file system contains several
 * blocks groups.  Each group contains 1 bitmap block for blocks, 1 bitmap
 * block for inodes, N blocks for the inode table and data blocks.
 *
 * The file system contains group descriptors which are located after the
 * super block.  Each descriptor contains the number of the bitmap block and
 * the free blocks count in the block.  The descriptors are loaded in memory
 * when a file system is mounted (see xip2_read_super).
 */


#define in_range(b, first, len)	((b) >= (first) && (b) <= (first) + (len) - 1)

struct ext2_group_desc * xip2_get_group_desc(struct super_block * sb,
					     unsigned int block_group,
					     void ** block_ptr)
{
	unsigned long group_desc;
	unsigned long offset;
	struct ext2_group_desc * desc;
	struct xip2_sb_info *sbi = XIP2_SB(sb);

	if (block_group >= sbi->s_groups_count) {
		xip2_error (sb, "xip2_get_group_desc",
			    "block_group >= groups_count - "
			    "block_group = %d, groups_count = %lu",
			    block_group, sbi->s_groups_count);

		return NULL;
	}

	group_desc = block_group / XIP2_DESC_PER_BLOCK(sb);
	offset = block_group % XIP2_DESC_PER_BLOCK(sb);
	if (!sbi->s_group_desc[group_desc]) {
		xip2_error (sb, "xip2_get_group_desc",
			    "Group descriptor not loaded - "
			    "block_group = %d, group_desc = %lu, desc = %lu",
			     block_group, group_desc, offset);
		return NULL;
	}

	desc = (struct ext2_group_desc *) sbi->s_group_desc[group_desc];
	if (block_ptr)
		*block_ptr = sbi->s_group_desc[group_desc];
	return desc + offset;
}

/*
 * Read the bitmap for a given block_group, reading into the specified
 * slot in the superblock's bitmap cache.
 *
 * Return buffer_head on success or NULL in case of failure.
 */
void *
read_block_bitmap(struct super_block *sb, unsigned int block_group)
{
	struct ext2_group_desc * desc;
	void *bitmap_data = NULL;

	desc = xip2_get_group_desc (sb, block_group, NULL);
	if (!desc)
		goto error_out;
	bitmap_data = xip2_sb_bread(sb, le32_to_cpu(desc->bg_block_bitmap));
	if (!bitmap_data)
		xip2_error (sb, "read_block_bitmap",
			    "Cannot read block bitmap - "
			    "block_group = %d, block_bitmap = %lu",
			    block_group, (unsigned long) desc->bg_block_bitmap);
error_out:
	return bitmap_data;
}


unsigned long xip2_count_free_blocks (struct super_block * sb)
{
	struct ext2_group_desc * desc;
	unsigned long desc_count = 0;
	int i;
#ifdef EXT2FS_DEBUG
	unsigned long bitmap_count, x;
	struct ext2_super_block *es;

	lock_super (sb);
	es = XIP2_SB(sb)->s_es;
	desc_count = 0;
	bitmap_count = 0;
	desc = NULL;
	for (i = 0; i < XIP2_SB(sb)->s_groups_count; i++) {
		void *bitmap_data;
		desc = xip2_get_group_desc (sb, i, NULL);
		if (!desc)
			continue;
		desc_count += le16_to_cpu(desc->bg_free_blocks_count);
		bitmap_data = read_block_bitmap(sb, i);
		if (!bitmap_data)
			continue;

		x = xip2_count_free(bitmap_data, sb->s_blocksize);
		printk ("group %d: stored = %d, counted = %lu\n",
			i, le16_to_cpu(desc->bg_free_blocks_count), x);
		bitmap_count += x;
	}
	printk("xip2_count_free_blocks: stored = %lu, computed = %lu, %lu\n",
		(long)le32_to_cpu(es->s_free_blocks_count),
		desc_count, bitmap_count);
	unlock_super (sb);
	return bitmap_count;
#else
	for (i = 0; i < XIP2_SB(sb)->s_groups_count; i++) {
		desc = xip2_get_group_desc (sb, i, NULL);
		if (!desc)
			continue;
		desc_count += le16_to_cpu(desc->bg_free_blocks_count);
	}
	return desc_count;
#endif
}

static inline int
block_in_use(unsigned long block, struct super_block *sb, unsigned char *map)
{
	return xip2_test_bit((block -
			le32_to_cpu(XIP2_SB(sb)->s_es->s_first_data_block)) %
			XIP2_BLOCKS_PER_GROUP(sb), map);
}

static inline int test_root(int a, int b)
{
	if (a == 0)
		return 1;
	while (1) {
		if (a == 1)
			return 1;
		if (a % b)
			return 0;
		a = a / b;
	}
}

static int xip2_group_sparse(int group)
{
	return (test_root(group, 3) || test_root(group, 5) ||
		test_root(group, 7));
}

/**
 *	xip2_bg_has_super - number of blocks used by the superblock in group
 *	@sb: superblock for filesystem
 *	@group: group number to check
 *
 *	Return the number of blocks used by the superblock (primary or backup)
 *	in this group.  Currently this will be only 0 or 1.
 */
int xip2_bg_has_super(struct super_block *sb, int group)
{
	if (XIP2_HAS_RO_COMPAT_FEATURE(sb,EXT2_FEATURE_RO_COMPAT_SPARSE_SUPER)&&
	    !xip2_group_sparse(group))
		return 0;
	return 1;
}

/**
 *	xip2_bg_num_gdb - number of blocks used by the group table in group
 *	@sb: superblock for filesystem
 *	@group: group number to check
 *
 *	Return the number of blocks used by the group descriptor table
 *	(primary or backup) in this group.  In the future there may be a
 *	different number of descriptor blocks in each group.
 */
unsigned long xip2_bg_num_gdb(struct super_block *sb, int group)
{
	if (XIP2_HAS_RO_COMPAT_FEATURE(sb,EXT2_FEATURE_RO_COMPAT_SPARSE_SUPER)&&
	    !xip2_group_sparse(group))
		return 0;
	return XIP2_SB(sb)->s_gdb_count;
}

#ifdef CONFIG_XIP2_CHECK
/* Called at mount-time, super-block is locked */
void xip2_check_blocks_bitmap (struct super_block * sb)
{
	void *bitmap_data = NULL;
	struct ext2_super_block * es;
	unsigned long desc_count, bitmap_count, x, j;
	unsigned long desc_blocks;
	struct ext2_group_desc * desc;
	int i;

	es = XIP2_SB(sb)->s_es;
	desc_count = 0;
	bitmap_count = 0;
	desc = NULL;
	for (i = 0; i < XIP2_SB(sb)->s_groups_count; i++) {
		desc = xip2_get_group_desc (sb, i, NULL);
		if (!desc)
			continue;
		desc_count += le16_to_cpu(desc->bg_free_blocks_count);
		bitmap_data = read_block_bitmap(sb, i);
		if (!bitmap_data)
			continue;

		if (xip2_bg_has_super(sb, i) &&
				!xip2_test_bit(0, bitmap_data))
			xip2_error(sb, __FUNCTION__,
				   "Superblock in group %d is marked free", i);

		desc_blocks = xip2_bg_num_gdb(sb, i);
		for (j = 0; j < desc_blocks; j++)
			if (!xip2_test_bit(j + 1, bitmap_data))
				xip2_error(sb, __FUNCTION__,
					   "Descriptor block #%ld in group "
					   "%d is marked free", j, i);

		if (!block_in_use(le32_to_cpu(desc->bg_block_bitmap),
					sb, bitmap_data))
			xip2_error(sb, "xip2_check_blocks_bitmap",
				    "Block bitmap for group %d is marked free",
				    i);

		if (!block_in_use(le32_to_cpu(desc->bg_inode_bitmap),
					sb, bitmap_data))
			xip2_error(sb, "xip2_check_blocks_bitmap",
				    "Inode bitmap for group %d is marked free",
				    i);

		for (j = 0; j < XIP2_SB(sb)->s_itb_per_group; j++)
			if (!block_in_use(le32_to_cpu(desc->bg_inode_table) + j,
						sb, bitmap_data))
				xip2_error (sb, "xip2_check_blocks_bitmap",
					    "Block #%ld of the inode table in "
					    "group %d is marked free", j, i);

		x = xip2_count_free(bitmap_data, sb->s_blocksize);
		if (le16_to_cpu(desc->bg_free_blocks_count) != x)
			xip2_error (sb, "xip2_check_blocks_bitmap",
				    "Wrong free blocks count for group %d, "
				    "stored = %d, counted = %lu", i,
				    le16_to_cpu(desc->bg_free_blocks_count), x);
		bitmap_count += x;
	}
	if (le32_to_cpu(es->s_free_blocks_count) != bitmap_count)
		xip2_error (sb, "xip2_check_blocks_bitmap",
			"Wrong free blocks count in super block, "
			"stored = %lu, counted = %lu",
			(unsigned long)le32_to_cpu(es->s_free_blocks_count),
			bitmap_count);
}
#endif
