/*
 *  linux/fs/xip2fs/ialloc.c, Version 1
 *
 * (C) Copyright IBM Corp. 2002,2004
 * Author(s): Carsten Otte <cotte@de.ibm.com>
 * derived from second extended filesystem (ext2)
 */


#include <linux/config.h>
#include <linux/quotaops.h>
#include <linux/sched.h>
#include <linux/backing-dev.h>
#include <linux/random.h>
#include "xip2.h"
#include "xattr.h"
#include "acl.h"

/*
 * ialloc.c contains the inodes allocation and deallocation routines
 */

/* 
 * Orlov's allocator for directories. 
 * 
 * We always try to spread first-level directories.
 *
 * If there are blockgroups with both free inodes and free blocks counts 
 * not worse than average we return one with smallest directory count. 
 * Otherwise we simply return a random group. 
 * 
 * For the rest rules look so: 
 * 
 * It's OK to put directory into a group unless 
 * it has too many directories already (max_dirs) or 
 * it has too few free inodes left (min_inodes) or 
 * it has too few free blocks left (min_blocks) or 
 * it's already running too large debt (max_debt). 
 * Parent's group is prefered, if it doesn't satisfy these 
 * conditions we search cyclically through the rest. If none 
 * of the groups look good we just look for a group with more 
 * free inodes than average (starting at parent's group). 
 * 
 * Debt is incremented each time we allocate a directory and decremented 
 * when we allocate an inode, within 0--255. 
 */ 

#ifdef EXT2FS_DEBUG
/*
 * Read the inode allocation bitmap for a given block_group, reading
 * into the specified slot in the superblock's bitmap cache.
 *
 * Return buffer_head of bitmap on success or NULL.
 */
static void *
read_inode_bitmap(struct super_block * sb, unsigned long block_group)
{
	struct ext2_group_desc *desc;
	void *bitmap_data = NULL;

	desc = xip2_get_group_desc(sb, block_group, NULL);
	if (!desc)
		goto error_out;

	bitmap_data = xip2_sb_bread(sb, le32_to_cpu(desc->bg_inode_bitmap));
	if (!bitmap_data)
		xip2_error(sb, "read_inode_bitmap",
			    "Cannot read inode bitmap - "
			    "block_group = %lu, inode_bitmap = %lu",
			    block_group, (unsigned long) desc->bg_inode_bitmap);
error_out:
	return bitmap_data;
}
#endif

unsigned long xip2_count_free_inodes (struct super_block * sb)
{
	struct ext2_group_desc *desc;
	unsigned long desc_count = 0;
	int i;	

#ifdef EXT2FS_DEBUG
	struct ext2_super_block *es;
	unsigned long bitmap_count = 0;
	void *bitmap_data = NULL;

	lock_super (sb);
	es = XIP2_SB(sb)->s_es;
	for (i = 0; i < XIP2_SB(sb)->s_groups_count; i++) {
		unsigned x;

		desc = xip2_get_group_desc (sb, i, NULL);
		if (!desc)
			continue;
		desc_count += le16_to_cpu(desc->bg_free_inodes_count);
		bitmap_data = read_inode_bitmap(sb, i);
		if (!bitmap_data)
			continue;

		x = xip2_count_free(bitmap_data, XIP2_INODES_PER_GROUP(sb) / 8);
		printk("group %d: stored = %d, counted = %u\n",
			i, le16_to_cpu(desc->bg_free_inodes_count), x);
		bitmap_count += x;
	}
	printk("xip2_count_free_inodes: stored = %lu, computed = %lu, %lu\n",
		percpu_counter_read(XIP2_SB(sb)->s_freeinodes_counter),
		desc_count, bitmap_count);
	unlock_super(sb);
	return desc_count;
#else
	for (i = 0; i < XIP2_SB(sb)->s_groups_count; i++) {
		desc = xip2_get_group_desc (sb, i, NULL);
		if (!desc)
			continue;
		desc_count += le16_to_cpu(desc->bg_free_inodes_count);
	}
	return desc_count;
#endif
}

/* Called at mount-time, super-block is locked */
unsigned long xip2_count_dirs (struct super_block * sb)
{
	unsigned long count = 0;
	int i;

	for (i = 0; i < XIP2_SB(sb)->s_groups_count; i++) {
		struct ext2_group_desc *gdp = xip2_get_group_desc (sb, i, NULL);
		if (!gdp)
			continue;
		count += le16_to_cpu(gdp->bg_used_dirs_count);
	}
	return count;
}

#ifdef CONFIG_XIP2_CHECK
/* Called at mount-time, super-block is locked */
void xip2_check_inodes_bitmap (struct super_block * sb)
{
	struct ext2_super_block * es = XIP2_SB(sb)->s_es;
	unsigned long desc_count = 0, bitmap_count = 0;
	void *bitmap_data = NULL;
	int i;

	for (i = 0; i < XIP2_SB(sb)->s_groups_count; i++) {
		struct ext2_group_desc *desc;
		unsigned x;

		desc = xip2_get_group_desc(sb, i, NULL);
		if (!desc)
			continue;
		desc_count += le16_to_cpu(desc->bg_free_inodes_count);
		bitmap_data = read_inode_bitmap(sb, i);
		if (!bitmap_data)
			continue;
		
		x = xip2_count_free(bitmap_data, XIP2_INODES_PER_GROUP(sb) / 8);
		if (le16_to_cpu(desc->bg_free_inodes_count) != x)
			xip2_error (sb, "xip2_check_inodes_bitmap",
				    "Wrong free inodes count in group %d, "
				    "stored = %d, counted = %lu", i,
				    le16_to_cpu(desc->bg_free_inodes_count), x);
		bitmap_count += x;
	}
	if (percpu_counter_read(XIP2_SB(sb)->s_freeinodes_counter) !=
				bitmap_count)
		xip2_error(sb, "xip2_check_inodes_bitmap",
			    "Wrong free inodes count in super block, "
			    "stored = %lu, counted = %lu",
			    (unsigned long)le32_to_cpu(es->s_free_inodes_count),
			    bitmap_count);
}
#endif
