/*
 *  linux/fs/ext2/balloc.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  Enhanced block allocation by Stephen Tweedie (sct@dcs.ed.ac.uk), 1993
 *  Big-endian to little-endian byte-swapping/bitmaps by
 *        David S. Miller (davem@caip.rutgers.edu), 1995
 */

#include <linux/config.h>
#include "ext2.h"
#include <linux/quotaops.h>
#include <linux/sched.h>
#include <linux/buffer_head.h>

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
 * when a file system is mounted (see ext2_read_super).
 */


#define in_range(b, first, len)	((b) >= (first) && (b) <= (first) + (len) - 1)

struct ext2_group_desc * ext2_get_group_desc(struct super_block * sb,
					     unsigned int block_group,
					     struct buffer_head ** bh)
{
	unsigned long group_desc;
	unsigned long offset;
	struct ext2_group_desc * desc;
	struct ext2_sb_info *sbi = EXT2_SB(sb);

	if (block_group >= sbi->s_groups_count) {
		ext2_error (sb, "ext2_get_group_desc",
			    "block_group >= groups_count - "
			    "block_group = %d, groups_count = %lu",
			    block_group, sbi->s_groups_count);

		return NULL;
	}
	
	group_desc = block_group / EXT2_DESC_PER_BLOCK(sb);
	offset = block_group % EXT2_DESC_PER_BLOCK(sb);
	if (!sbi->s_group_desc[group_desc]) {
		ext2_error (sb, "ext2_get_group_desc",
			    "Group descriptor not loaded - "
			    "block_group = %d, group_desc = %lu, desc = %lu",
			     block_group, group_desc, offset);
		return NULL;
	}
	
	desc = (struct ext2_group_desc *) sbi->s_group_desc[group_desc]->b_data;
	if (bh)
		*bh = sbi->s_group_desc[group_desc];
	return desc + offset;
}

/*
 * Read the bitmap for a given block_group, reading into the specified 
 * slot in the superblock's bitmap cache.
 *
 * Return buffer_head on success or NULL in case of failure.
 */
static struct buffer_head *
read_block_bitmap(struct super_block *sb, unsigned int block_group)
{
	struct ext2_group_desc * desc;
	struct buffer_head * bh = NULL;
	
	desc = ext2_get_group_desc (sb, block_group, NULL);
	if (!desc)
		goto error_out;
	bh = sb_bread(sb, le32_to_cpu(desc->bg_block_bitmap));
	if (!bh)
		ext2_error (sb, "read_block_bitmap",
			    "Cannot read block bitmap - "
			    "block_group = %d, block_bitmap = %lu",
			    block_group, (unsigned long) desc->bg_block_bitmap);
error_out:
	return bh;
}

static inline int reserve_blocks(struct super_block *sb, int count)
{
	struct ext2_sb_info * sbi = EXT2_SB(sb);
	struct ext2_super_block * es = sbi->s_es;
	unsigned free_blocks = le32_to_cpu(es->s_free_blocks_count);
	unsigned root_blocks = le32_to_cpu(es->s_r_blocks_count);

	if (free_blocks < count)
		count = free_blocks;

	if (free_blocks < root_blocks + count && !capable(CAP_SYS_RESOURCE) &&
	    sbi->s_resuid != current->fsuid &&
	    (sbi->s_resgid == 0 || !in_group_p (sbi->s_resgid))) {
		/*
		 * We are too close to reserve and we are not privileged.
		 * Can we allocate anything at all?
		 */
		if (free_blocks > root_blocks)
			count = free_blocks - root_blocks;
		else
			return 0;
	}

	es->s_free_blocks_count = cpu_to_le32(free_blocks - count);
	mark_buffer_dirty(sbi->s_sbh);
	sb->s_dirt = 1;
	return count;
}

static inline void release_blocks(struct super_block *sb, int count)
{
	if (count) {
		struct ext2_sb_info * sbi = EXT2_SB(sb);
		struct ext2_super_block * es = sbi->s_es;
		unsigned free_blocks = le32_to_cpu(es->s_free_blocks_count);
		es->s_free_blocks_count = cpu_to_le32(free_blocks + count);
		mark_buffer_dirty(sbi->s_sbh);
		sb->s_dirt = 1;
	}
}

static inline int group_reserve_blocks(struct ext2_group_desc *desc,
				    struct buffer_head *bh, int count)
{
	unsigned free_blocks;

	if (!desc->bg_free_blocks_count)
		return 0;

	free_blocks = le16_to_cpu(desc->bg_free_blocks_count);
	if (free_blocks < count)
		count = free_blocks;
	desc->bg_free_blocks_count = cpu_to_le16(free_blocks - count);
	mark_buffer_dirty(bh);
	return count;
}

static inline void group_release_blocks(struct ext2_group_desc *desc,
				    struct buffer_head *bh, int count)
{
	if (count) {
		unsigned free_blocks = le16_to_cpu(desc->bg_free_blocks_count);
		desc->bg_free_blocks_count = cpu_to_le16(free_blocks + count);
		mark_buffer_dirty(bh);
	}
}

/* Free given blocks, update quota and i_blocks field */
void ext2_free_blocks (struct inode * inode, unsigned long block,
		       unsigned long count)
{
	struct buffer_head *bitmap_bh = NULL;
	struct buffer_head * bh2;
	unsigned long block_group;
	unsigned long bit;
	unsigned long i;
	unsigned long overflow;
	struct super_block * sb = inode->i_sb;
	struct ext2_group_desc * desc;
	struct ext2_super_block * es;
	unsigned freed = 0, group_freed;

	lock_super (sb);
	es = EXT2_SB(sb)->s_es;
	if (block < le32_to_cpu(es->s_first_data_block) || 
	    (block + count) > le32_to_cpu(es->s_blocks_count)) {
		ext2_error (sb, "ext2_free_blocks",
			    "Freeing blocks not in datazone - "
			    "block = %lu, count = %lu", block, count);
		goto error_return;
	}

	ext2_debug ("freeing block(s) %lu-%lu\n", block, block + count - 1);

do_more:
	overflow = 0;
	block_group = (block - le32_to_cpu(es->s_first_data_block)) /
		      EXT2_BLOCKS_PER_GROUP(sb);
	bit = (block - le32_to_cpu(es->s_first_data_block)) %
		      EXT2_BLOCKS_PER_GROUP(sb);
	/*
	 * Check to see if we are freeing blocks across a group
	 * boundary.
	 */
	if (bit + count > EXT2_BLOCKS_PER_GROUP(sb)) {
		overflow = bit + count - EXT2_BLOCKS_PER_GROUP(sb);
		count -= overflow;
	}
	brelse(bitmap_bh);
	bitmap_bh = read_block_bitmap(sb, block_group);
	if (!bitmap_bh)
		goto error_return;

	desc = ext2_get_group_desc (sb, block_group, &bh2);
	if (!desc)
		goto error_return;

	if (in_range (le32_to_cpu(desc->bg_block_bitmap), block, count) ||
	    in_range (le32_to_cpu(desc->bg_inode_bitmap), block, count) ||
	    in_range (block, le32_to_cpu(desc->bg_inode_table),
		      EXT2_SB(sb)->s_itb_per_group) ||
	    in_range (block + count - 1, le32_to_cpu(desc->bg_inode_table),
		      EXT2_SB(sb)->s_itb_per_group))
		ext2_error (sb, "ext2_free_blocks",
			    "Freeing blocks in system zones - "
			    "Block = %lu, count = %lu",
			    block, count);

	for (i = 0, group_freed = 0; i < count; i++) {
		if (!ext2_clear_bit(bit + i, bitmap_bh->b_data))
			ext2_error (sb, "ext2_free_blocks",
				      "bit already cleared for block %lu",
				      block + i);
		else
			group_freed++;
	}

	mark_buffer_dirty(bitmap_bh);
	if (sb->s_flags & MS_SYNCHRONOUS) {
		ll_rw_block(WRITE, 1, &bitmap_bh);
		wait_on_buffer(bitmap_bh);
	}

	group_release_blocks(desc, bh2, group_freed);
	freed += group_freed;

	if (overflow) {
		block += count;
		count = overflow;
		goto do_more;
	}
error_return:
	brelse(bitmap_bh);
	release_blocks(sb, freed);
	unlock_super (sb);
	DQUOT_FREE_BLOCK(inode, freed);
}

static int grab_block(char *map, unsigned size, int goal)
{
	int k;
	char *p, *r;

	if (!ext2_test_bit(goal, map))
		goto got_it;
	if (goal) {
		/*
		 * The goal was occupied; search forward for a free 
		 * block within the next XX blocks.
		 *
		 * end_goal is more or less random, but it has to be
		 * less than EXT2_BLOCKS_PER_GROUP. Aligning up to the
		 * next 64-bit boundary is simple..
		 */
		k = (goal + 63) & ~63;
		goal = ext2_find_next_zero_bit(map, k, goal);
		if (goal < k)
			goto got_it;
		/*
		 * Search in the remainder of the current group.
		 */
	}

	p = map + (goal >> 3);
	r = memscan(p, 0, (size - goal + 7) >> 3);
	k = (r - map) << 3;
	if (k < size) {
		/* 
		 * We have succeeded in finding a free byte in the block
		 * bitmap.  Now search backwards to find the start of this
		 * group of free blocks - won't take more than 7 iterations.
		 */
		for (goal = k; goal && !ext2_test_bit (goal - 1, map); goal--)
			;
		goto got_it;
	}

	k = ext2_find_next_zero_bit ((u32 *)map, size, goal);
	if (k < size) {
		goal = k;
		goto got_it;
	}
	return -1;
got_it:
	ext2_set_bit(goal, map);
	return goal;
}

/*
 * ext2_new_block uses a goal block to assist allocation.  If the goal is
 * free, or there is a free block within 32 blocks of the goal, that block
 * is allocated.  Otherwise a forward search is made for a free block; within 
 * each block group the search first looks for an entire free byte in the block
 * bitmap, and then for any free bit if that fails.
 * This function also updates quota and i_blocks field.
 */
int ext2_new_block (struct inode * inode, unsigned long goal,
    u32 * prealloc_count, u32 * prealloc_block, int * err)
{
	struct buffer_head *bitmap_bh = NULL;
	struct buffer_head *bh2;
	struct ext2_group_desc *desc;
	int i, j, k, tmp;
	int block = 0;
	struct super_block *sb = inode->i_sb;
	struct ext2_sb_info *sbi = EXT2_SB(sb);
	struct ext2_super_block *es = sbi->s_es;
	unsigned group_size = EXT2_BLOCKS_PER_GROUP(sb);
	unsigned prealloc_goal = es->s_prealloc_blocks;
	unsigned group_alloc = 0, es_alloc, dq_alloc;

	if (!prealloc_goal--)
		prealloc_goal = EXT2_DEFAULT_PREALLOC_BLOCKS - 1;
	if (!prealloc_count || *prealloc_count)
		prealloc_goal = 0;

	*err = -EDQUOT;
	if (DQUOT_ALLOC_BLOCK(inode, 1))
		goto out;

	while (prealloc_goal && DQUOT_PREALLOC_BLOCK(inode, prealloc_goal))
		prealloc_goal--;

	dq_alloc = prealloc_goal + 1;

	*err = -ENOSPC;

	lock_super (sb);

	es_alloc = reserve_blocks(sb, dq_alloc);
	if (!es_alloc)
		goto out_unlock;

	ext2_debug ("goal=%lu.\n", goal);

	if (goal < le32_to_cpu(es->s_first_data_block) ||
	    goal >= le32_to_cpu(es->s_blocks_count))
		goal = le32_to_cpu(es->s_first_data_block);
	i = (goal - le32_to_cpu(es->s_first_data_block)) / group_size;
	desc = ext2_get_group_desc (sb, i, &bh2);
	if (!desc)
		goto io_error;

	group_alloc = group_reserve_blocks(desc, bh2, es_alloc);
	if (group_alloc) {
		j = ((goal - le32_to_cpu(es->s_first_data_block)) % group_size);
		brelse(bitmap_bh);
		bitmap_bh = read_block_bitmap(sb, i);
		if (!bitmap_bh)
			goto io_error;
		
		ext2_debug ("goal is at %d:%d.\n", i, j);

		j = grab_block(bitmap_bh->b_data, group_size, j);
		if (j >= 0)
			goto got_block;
		group_release_blocks(desc, bh2, group_alloc);
		group_alloc = 0;
	}

	ext2_debug ("Bit not found in block group %d.\n", i);

	/*
	 * Now search the rest of the groups.  We assume that 
	 * i and desc correctly point to the last group visited.
	 */
	for (k = 0; !group_alloc && k < sbi->s_groups_count; k++) {
		i++;
		if (i >= sbi->s_groups_count)
			i = 0;
		desc = ext2_get_group_desc (sb, i, &bh2);
		if (!desc)
			goto io_error;
		group_alloc = group_reserve_blocks(desc, bh2, es_alloc);
	}
	if (k >= sbi->s_groups_count)
		goto out_release;
	brelse(bitmap_bh);
	bitmap_bh = read_block_bitmap(sb, i);
	if (!bitmap_bh)
		goto io_error;
	
	j = grab_block(bitmap_bh->b_data, group_size, 0);
	if (j < 0) {
		ext2_error (sb, "ext2_new_block",
			    "Free blocks count corrupted for block group %d", i);
		group_alloc = 0;
		goto out_release;
	}

got_block:
	ext2_debug("using block group %d(%d)\n", i, desc->bg_free_blocks_count);

	tmp = j + i * group_size + le32_to_cpu(es->s_first_data_block);

	if (tmp == le32_to_cpu(desc->bg_block_bitmap) ||
	    tmp == le32_to_cpu(desc->bg_inode_bitmap) ||
	    in_range (tmp, le32_to_cpu(desc->bg_inode_table),
		      sbi->s_itb_per_group))
		ext2_error (sb, "ext2_new_block",
			    "Allocating block in system zone - "
			    "block = %u", tmp);

	if (tmp >= le32_to_cpu(es->s_blocks_count)) {
		ext2_error (sb, "ext2_new_block",
			    "block(%d) >= blocks count(%d) - "
			    "block_group = %d, es == %p ",j,
			le32_to_cpu(es->s_blocks_count), i, es);
		goto out_release;
	}
	block = tmp;

	/* OK, we _had_ allocated something */
	ext2_debug ("found bit %d\n", j);

	dq_alloc--;
	es_alloc--;
	group_alloc--;

	/*
	 * Do block preallocation now if required.
	 */
	write_lock(&EXT2_I(inode)->i_meta_lock);
	if (group_alloc && !*prealloc_count) {
		unsigned n;

		for (n = 0; n < group_alloc && ++j < group_size; n++) {
			if (ext2_set_bit(j, bitmap_bh->b_data))
 				break;
		}
		*prealloc_block = block + 1;
		*prealloc_count = n;
		es_alloc -= n;
		dq_alloc -= n;
		group_alloc -= n;
	}
	write_unlock(&EXT2_I(inode)->i_meta_lock);

	mark_buffer_dirty(bitmap_bh);
	if (sb->s_flags & MS_SYNCHRONOUS) {
		ll_rw_block(WRITE, 1, &bitmap_bh);
		wait_on_buffer(bitmap_bh);
	}

	ext2_debug ("allocating block %d. ", block);

out_release:
	group_release_blocks(desc, bh2, group_alloc);
	release_blocks(sb, es_alloc);
	*err = 0;
out_unlock:
	unlock_super (sb);
	DQUOT_FREE_BLOCK(inode, dq_alloc);
out:
	brelse(bitmap_bh);
	return block;

io_error:
	*err = -EIO;
	goto out_release;
}

unsigned long ext2_count_free_blocks (struct super_block * sb)
{
#ifdef EXT2FS_DEBUG
	struct ext2_super_block * es;
	unsigned long desc_count, bitmap_count, x;
	struct ext2_group_desc * desc;
	int i;
	
	lock_super (sb);
	es = EXT2_SB(sb)->s_es;
	desc_count = 0;
	bitmap_count = 0;
	desc = NULL;
	for (i = 0; i < EXT2_SB(sb)->s_groups_count; i++) {
		struct buffer_head *bitmap_bh;
		desc = ext2_get_group_desc (sb, i, NULL);
		if (!desc)
			continue;
		desc_count += le16_to_cpu(desc->bg_free_blocks_count);
		bitmap_bh = read_block_bitmap(sb, i);
		if (!bitmap_bh)
			continue;
		
		x = ext2_count_free(bitmap_bh, sb->s_blocksize);
		printk ("group %d: stored = %d, counted = %lu\n",
			i, le16_to_cpu(desc->bg_free_blocks_count), x);
		bitmap_count += x;
		brelse(bitmap_bh);
	}
	printk("ext2_count_free_blocks: stored = %lu, computed = %lu, %lu\n",
	       le32_to_cpu(es->s_free_blocks_count), desc_count, bitmap_count);
	unlock_super (sb);
	return bitmap_count;
#else
	return le32_to_cpu(EXT2_SB(sb)->s_es->s_free_blocks_count);
#endif
}

static inline int block_in_use (unsigned long block,
				struct super_block * sb,
				unsigned char * map)
{
	return ext2_test_bit ((block - le32_to_cpu(EXT2_SB(sb)->s_es->s_first_data_block)) %
			 EXT2_BLOCKS_PER_GROUP(sb), map);
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

static int ext2_group_sparse(int group)
{
	return (test_root(group, 3) || test_root(group, 5) ||
		test_root(group, 7));
}

/**
 *	ext2_bg_has_super - number of blocks used by the superblock in group
 *	@sb: superblock for filesystem
 *	@group: group number to check
 *
 *	Return the number of blocks used by the superblock (primary or backup)
 *	in this group.  Currently this will be only 0 or 1.
 */
int ext2_bg_has_super(struct super_block *sb, int group)
{
	if (EXT2_HAS_RO_COMPAT_FEATURE(sb,EXT2_FEATURE_RO_COMPAT_SPARSE_SUPER)&&
	    !ext2_group_sparse(group))
		return 0;
	return 1;
}

/**
 *	ext2_bg_num_gdb - number of blocks used by the group table in group
 *	@sb: superblock for filesystem
 *	@group: group number to check
 *
 *	Return the number of blocks used by the group descriptor table
 *	(primary or backup) in this group.  In the future there may be a
 *	different number of descriptor blocks in each group.
 */
unsigned long ext2_bg_num_gdb(struct super_block *sb, int group)
{
	if (EXT2_HAS_RO_COMPAT_FEATURE(sb,EXT2_FEATURE_RO_COMPAT_SPARSE_SUPER)&&
	    !ext2_group_sparse(group))
		return 0;
	return EXT2_SB(sb)->s_gdb_count;
}

#ifdef CONFIG_EXT2_CHECK
/* Called at mount-time, super-block is locked */
void ext2_check_blocks_bitmap (struct super_block * sb)
{
	struct buffer_head *bitmap_bh = NULL;
	struct ext2_super_block * es;
	unsigned long desc_count, bitmap_count, x, j;
	unsigned long desc_blocks;
	struct ext2_group_desc * desc;
	int i;

	es = EXT2_SB(sb)->s_es;
	desc_count = 0;
	bitmap_count = 0;
	desc = NULL;
	for (i = 0; i < EXT2_SB(sb)->s_groups_count; i++) {
		desc = ext2_get_group_desc (sb, i, NULL);
		if (!desc)
			continue;
		desc_count += le16_to_cpu(desc->bg_free_blocks_count);
		brelse(bitmap_bh);
		bitmap_bh = read_block_bitmap(sb, i);
		if (!bitmap_bh)
			continue;

		if (ext2_bg_has_super(sb, i) &&
				!ext2_test_bit(0, bitmap_bh->b_data))
			ext2_error(sb, __FUNCTION__,
				   "Superblock in group %d is marked free", i);

		desc_blocks = ext2_bg_num_gdb(sb, i);
		for (j = 0; j < desc_blocks; j++)
			if (!ext2_test_bit(j + 1, bitmap_bh->b_data))
				ext2_error(sb, __FUNCTION__,
					   "Descriptor block #%ld in group "
					   "%d is marked free", j, i);

		if (!block_in_use(le32_to_cpu(desc->bg_block_bitmap),
					sb, bitmap_bh->b_data))
			ext2_error(sb, "ext2_check_blocks_bitmap",
				    "Block bitmap for group %d is marked free",
				    i);

		if (!block_in_use(le32_to_cpu(desc->bg_inode_bitmap),
					sb, bitmap_bh->b_data))
			ext2_error(sb, "ext2_check_blocks_bitmap",
				    "Inode bitmap for group %d is marked free",
				    i);

		for (j = 0; j < EXT2_SB(sb)->s_itb_per_group; j++)
			if (!block_in_use(le32_to_cpu(desc->bg_inode_table) + j,
						sb, bitmap_bh->b_data))
				ext2_error (sb, "ext2_check_blocks_bitmap",
					    "Block #%ld of the inode table in "
					    "group %d is marked free", j, i);

		x = ext2_count_free(bitmap_bh, sb->s_blocksize);
		if (le16_to_cpu(desc->bg_free_blocks_count) != x)
			ext2_error (sb, "ext2_check_blocks_bitmap",
				    "Wrong free blocks count for group %d, "
				    "stored = %d, counted = %lu", i,
				    le16_to_cpu(desc->bg_free_blocks_count), x);
		bitmap_count += x;
	}
	if (le32_to_cpu(es->s_free_blocks_count) != bitmap_count)
		ext2_error (sb, "ext2_check_blocks_bitmap",
			"Wrong free blocks count in super block, "
			"stored = %lu, counted = %lu",
			(unsigned long)le32_to_cpu(es->s_free_blocks_count),
			bitmap_count);
	brelse(bitmap_bh);
}
#endif
