/*
 *  linux/fs/ext3/balloc.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  Enhanced block allocation by Stephen Tweedie (sct@redhat.com), 1993
 *  Big-endian to little-endian byte-swapping/bitmaps by
 *        David S. Miller (davem@caip.rutgers.edu), 1995
 */

#include <linux/config.h>
#include <linux/time.h>
#include <linux/fs.h>
#include <linux/jbd.h>
#include <linux/ext3_fs.h>
#include <linux/ext3_jbd.h>
#include <linux/quotaops.h>
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
 * when a file system is mounted (see ext3_read_super).
 */


#define in_range(b, first, len)	((b) >= (first) && (b) <= (first) + (len) - 1)

struct ext3_group_desc * ext3_get_group_desc(struct super_block * sb,
					     unsigned int block_group,
					     struct buffer_head ** bh)
{
	unsigned long group_desc;
	unsigned long desc;
	struct ext3_group_desc * gdp;

	if (block_group >= EXT3_SB(sb)->s_groups_count) {
		ext3_error (sb, "ext3_get_group_desc",
			    "block_group >= groups_count - "
			    "block_group = %d, groups_count = %lu",
			    block_group, EXT3_SB(sb)->s_groups_count);

		return NULL;
	}

	group_desc = block_group / EXT3_DESC_PER_BLOCK(sb);
	desc = block_group % EXT3_DESC_PER_BLOCK(sb);
	if (!EXT3_SB(sb)->s_group_desc[group_desc]) {
		ext3_error (sb, "ext3_get_group_desc",
			    "Group descriptor not loaded - "
			    "block_group = %d, group_desc = %lu, desc = %lu",
			     block_group, group_desc, desc);
		return NULL;
	}

	gdp = (struct ext3_group_desc *) 
	      EXT3_SB(sb)->s_group_desc[group_desc]->b_data;
	if (bh)
		*bh = EXT3_SB(sb)->s_group_desc[group_desc];
	return gdp + desc;
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
	struct ext3_group_desc * desc;
	struct buffer_head * bh = NULL;

	desc = ext3_get_group_desc (sb, block_group, NULL);
	if (!desc)
		goto error_out;
	bh = sb_bread(sb, le32_to_cpu(desc->bg_block_bitmap));
	if (!bh)
		ext3_error (sb, "read_block_bitmap",
			    "Cannot read block bitmap - "
			    "block_group = %d, block_bitmap = %lu",
			    block_group, (unsigned long) desc->bg_block_bitmap);
error_out:
	return bh;
}

/* Free given blocks, update quota and i_blocks field */
void ext3_free_blocks (handle_t *handle, struct inode * inode,
			unsigned long block, unsigned long count)
{
	struct buffer_head *bitmap_bh = NULL;
	struct buffer_head *gd_bh;
	unsigned long block_group;
	unsigned long bit;
	unsigned long i;
	unsigned long overflow;
	struct super_block * sb;
	struct ext3_group_desc * gdp;
	struct ext3_super_block * es;
	struct ext3_sb_info *sbi;
	int err = 0, ret;
	int dquot_freed_blocks = 0;

	sb = inode->i_sb;
	if (!sb) {
		printk ("ext3_free_blocks: nonexistent device");
		return;
	}
	sbi = EXT3_SB(sb);
	es = EXT3_SB(sb)->s_es;
	if (block < le32_to_cpu(es->s_first_data_block) ||
	    block + count < block ||
	    block + count > le32_to_cpu(es->s_blocks_count)) {
		ext3_error (sb, "ext3_free_blocks",
			    "Freeing blocks not in datazone - "
			    "block = %lu, count = %lu", block, count);
		goto error_return;
	}

	ext3_debug ("freeing block %lu\n", block);

do_more:
	overflow = 0;
	block_group = (block - le32_to_cpu(es->s_first_data_block)) /
		      EXT3_BLOCKS_PER_GROUP(sb);
	bit = (block - le32_to_cpu(es->s_first_data_block)) %
		      EXT3_BLOCKS_PER_GROUP(sb);
	/*
	 * Check to see if we are freeing blocks across a group
	 * boundary.
	 */
	if (bit + count > EXT3_BLOCKS_PER_GROUP(sb)) {
		overflow = bit + count - EXT3_BLOCKS_PER_GROUP(sb);
		count -= overflow;
	}
	brelse(bitmap_bh);
	bitmap_bh = read_block_bitmap(sb, block_group);
	if (!bitmap_bh)
		goto error_return;
	gdp = ext3_get_group_desc (sb, block_group, &gd_bh);
	if (!gdp)
		goto error_return;

	if (in_range (le32_to_cpu(gdp->bg_block_bitmap), block, count) ||
	    in_range (le32_to_cpu(gdp->bg_inode_bitmap), block, count) ||
	    in_range (block, le32_to_cpu(gdp->bg_inode_table),
		      EXT3_SB(sb)->s_itb_per_group) ||
	    in_range (block + count - 1, le32_to_cpu(gdp->bg_inode_table),
		      EXT3_SB(sb)->s_itb_per_group))
		ext3_error (sb, "ext3_free_blocks",
			    "Freeing blocks in system zones - "
			    "Block = %lu, count = %lu",
			    block, count);

	/*
	 * We are about to start releasing blocks in the bitmap,
	 * so we need undo access.
	 */
	/* @@@ check errors */
	BUFFER_TRACE(bitmap_bh, "getting undo access");
	err = ext3_journal_get_undo_access(handle, bitmap_bh, NULL);
	if (err)
		goto error_return;

	/*
	 * We are about to modify some metadata.  Call the journal APIs
	 * to unshare ->b_data if a currently-committing transaction is
	 * using it
	 */
	BUFFER_TRACE(gd_bh, "get_write_access");
	err = ext3_journal_get_write_access(handle, gd_bh);
	if (err)
		goto error_return;

	jbd_lock_bh_state(bitmap_bh);

	for (i = 0; i < count; i++) {
		/*
		 * An HJ special.  This is expensive...
		 */
#ifdef CONFIG_JBD_DEBUG
		jbd_unlock_bh_state(bitmap_bh);
		{
			struct buffer_head *debug_bh;
			debug_bh = sb_find_get_block(sb, block + i);
			if (debug_bh) {
				BUFFER_TRACE(debug_bh, "Deleted!");
				if (!bh2jh(bitmap_bh)->b_committed_data)
					BUFFER_TRACE(debug_bh,
						"No commited data in bitmap");
				BUFFER_TRACE2(debug_bh, bitmap_bh, "bitmap");
				__brelse(debug_bh);
			}
		}
		jbd_lock_bh_state(bitmap_bh);
#endif
		/* @@@ This prevents newly-allocated data from being
		 * freed and then reallocated within the same
		 * transaction. 
		 * 
		 * Ideally we would want to allow that to happen, but to
		 * do so requires making journal_forget() capable of
		 * revoking the queued write of a data block, which
		 * implies blocking on the journal lock.  *forget()
		 * cannot block due to truncate races.
		 *
		 * Eventually we can fix this by making journal_forget()
		 * return a status indicating whether or not it was able
		 * to revoke the buffer.  On successful revoke, it is
		 * safe not to set the allocation bit in the committed
		 * bitmap, because we know that there is no outstanding
		 * activity on the buffer any more and so it is safe to
		 * reallocate it.  
		 */
		BUFFER_TRACE(bitmap_bh, "set in b_committed_data");
		J_ASSERT_BH(bitmap_bh,
				bh2jh(bitmap_bh)->b_committed_data != NULL);
		ext3_set_bit_atomic(sb_bgl_lock(sbi, block_group), bit + i,
				bh2jh(bitmap_bh)->b_committed_data);

		/*
		 * We clear the bit in the bitmap after setting the committed
		 * data bit, because this is the reverse order to that which
		 * the allocator uses.
		 */
		BUFFER_TRACE(bitmap_bh, "clear bit");
		if (!ext3_clear_bit_atomic(sb_bgl_lock(sbi, block_group),
						bit + i, bitmap_bh->b_data)) {
			ext3_error (sb, __FUNCTION__,
				      "bit already cleared for block %lu", 
				      block + i);
			BUFFER_TRACE(bitmap_bh, "bit already cleared");
		} else {
			dquot_freed_blocks++;
		}
	}
	jbd_unlock_bh_state(bitmap_bh);

	spin_lock(sb_bgl_lock(sbi, block_group));
	gdp->bg_free_blocks_count =
		cpu_to_le16(le16_to_cpu(gdp->bg_free_blocks_count) +
			dquot_freed_blocks);
	spin_unlock(sb_bgl_lock(sbi, block_group));
	percpu_counter_mod(&sbi->s_freeblocks_counter, count);

	/* We dirtied the bitmap block */
	BUFFER_TRACE(bitmap_bh, "dirtied bitmap block");
	err = ext3_journal_dirty_metadata(handle, bitmap_bh);

	/* And the group descriptor block */
	BUFFER_TRACE(gd_bh, "dirtied group descriptor block");
	ret = ext3_journal_dirty_metadata(handle, gd_bh);
	if (!err) err = ret;

	if (overflow && !err) {
		block += count;
		count = overflow;
		goto do_more;
	}
	sb->s_dirt = 1;
error_return:
	brelse(bitmap_bh);
	ext3_std_error(sb, err);
	if (dquot_freed_blocks)
		DQUOT_FREE_BLOCK(inode, dquot_freed_blocks);
	return;
}

/*
 * For ext3 allocations, we must not reuse any blocks which are
 * allocated in the bitmap buffer's "last committed data" copy.  This
 * prevents deletes from freeing up the page for reuse until we have
 * committed the delete transaction.
 *
 * If we didn't do this, then deleting something and reallocating it as
 * data would allow the old block to be overwritten before the
 * transaction committed (because we force data to disk before commit).
 * This would lead to corruption if we crashed between overwriting the
 * data and committing the delete. 
 *
 * @@@ We may want to make this allocation behaviour conditional on
 * data-writes at some point, and disable it for metadata allocations or
 * sync-data inodes.
 */
static inline int ext3_test_allocatable(int nr, struct buffer_head *bh)
{
	int ret;
	struct journal_head *jh = bh2jh(bh);

	if (ext3_test_bit(nr, bh->b_data))
		return 0;

	jbd_lock_bh_state(bh);
	if (!jh->b_committed_data)
		ret = 1;
	else
		ret = !ext3_test_bit(nr, jh->b_committed_data);
	jbd_unlock_bh_state(bh);
	return ret;
}

/*
 * Find an allocatable block in a bitmap.  We honour both the bitmap and
 * its last-committed copy (if that exists), and perform the "most
 * appropriate allocation" algorithm of looking for a free block near
 * the initial goal; then for a free byte somewhere in the bitmap; then
 * for any free bit in the bitmap.
 */
static int
find_next_usable_block(int start, struct buffer_head *bh, int maxblocks)
{
	int here, next;
	char *p, *r;
	struct journal_head *jh = bh2jh(bh);

	if (start > 0) {
		/*
		 * The goal was occupied; search forward for a free 
		 * block within the next XX blocks.
		 *
		 * end_goal is more or less random, but it has to be
		 * less than EXT3_BLOCKS_PER_GROUP. Aligning up to the
		 * next 64-bit boundary is simple..
		 */
		int end_goal = (start + 63) & ~63;
		here = ext3_find_next_zero_bit(bh->b_data, end_goal, start);
		if (here < end_goal && ext3_test_allocatable(here, bh))
			return here;
		ext3_debug("Bit not found near goal\n");
	}

	here = start;
	if (here < 0)
		here = 0;

	p = ((char *)bh->b_data) + (here >> 3);
	r = memscan(p, 0, (maxblocks - here + 7) >> 3);
	next = (r - ((char *)bh->b_data)) << 3;

	if (next < maxblocks && ext3_test_allocatable(next, bh))
		return next;

	/*
	 * The bitmap search --- search forward alternately through the actual
	 * bitmap and the last-committed copy until we find a bit free in
	 * both
	 */
	while (here < maxblocks) {
		next = ext3_find_next_zero_bit(bh->b_data, maxblocks, here);
		if (next >= maxblocks)
			return -1;
		if (ext3_test_allocatable(next, bh))
			return next;
		jbd_lock_bh_state(bh);
		if (jh->b_committed_data)
			here = ext3_find_next_zero_bit(jh->b_committed_data,
						 	maxblocks, next);
		jbd_unlock_bh_state(bh);
	}
	return -1;
}

/*
 * We think we can allocate this block in this bitmap.  Try to set the bit.
 * If that succeeds then check that nobody has allocated and then freed the
 * block since we saw that is was not marked in b_committed_data.  If it _was_
 * allocated and freed then clear the bit in the bitmap again and return
 * zero (failure).
 */
static inline int
claim_block(spinlock_t *lock, int block, struct buffer_head *bh)
{
	struct journal_head *jh = bh2jh(bh);
	int ret;

	if (ext3_set_bit_atomic(lock, block, bh->b_data))
		return 0;
	jbd_lock_bh_state(bh);
	if (jh->b_committed_data && ext3_test_bit(block,jh->b_committed_data)) {
		ext3_clear_bit_atomic(lock, block, bh->b_data);
		ret = 0;
	} else {
		ret = 1;
	}
	jbd_unlock_bh_state(bh);
	return ret;
}

/*
 * If we failed to allocate the desired block then we may end up crossing to a
 * new bitmap.  In that case we must release write access to the old one via
 * ext3_journal_release_buffer(), else we'll run out of credits.
 */
static int
ext3_try_to_allocate(struct super_block *sb, handle_t *handle, int group,
		struct buffer_head *bitmap_bh, int goal, int *errp)
{
	int i;
	int fatal;
	int credits = 0;

	*errp = 0;

	/*
	 * Make sure we use undo access for the bitmap, because it is critical
	 * that we do the frozen_data COW on bitmap buffers in all cases even
	 * if the buffer is in BJ_Forget state in the committing transaction.
	 */
	BUFFER_TRACE(bitmap_bh, "get undo access for new block");
	fatal = ext3_journal_get_undo_access(handle, bitmap_bh, &credits);
	if (fatal) {
		*errp = fatal;
		goto fail;
	}

repeat:
	if (goal < 0 || !ext3_test_allocatable(goal, bitmap_bh)) {
		goal = find_next_usable_block(goal, bitmap_bh,
					EXT3_BLOCKS_PER_GROUP(sb));
		if (goal < 0)
			goto fail_access;

		for (i = 0; i < 7 && goal > 0 &&
				ext3_test_allocatable(goal - 1, bitmap_bh);
			i++, goal--);
	}

	if (!claim_block(sb_bgl_lock(EXT3_SB(sb), group), goal, bitmap_bh)) {
		/*
		 * The block was allocated by another thread, or it was
		 * allocated and then freed by another thread
		 */
		goal++;
		if (goal >= EXT3_BLOCKS_PER_GROUP(sb))
			goto fail_access;
		goto repeat;
	}

	BUFFER_TRACE(bitmap_bh, "journal_dirty_metadata for bitmap block");
	fatal = ext3_journal_dirty_metadata(handle, bitmap_bh);
	if (fatal) {
		*errp = fatal;
		goto fail;
	}
	return goal;

fail_access:
	BUFFER_TRACE(bitmap_bh, "journal_release_buffer");
	ext3_journal_release_buffer(handle, bitmap_bh, credits);
fail:
	return -1;
}

/*
 * ext3_new_block uses a goal block to assist allocation.  If the goal is
 * free, or there is a free block within 32 blocks of the goal, that block
 * is allocated.  Otherwise a forward search is made for a free block; within 
 * each block group the search first looks for an entire free byte in the block
 * bitmap, and then for any free bit if that fails.
 * This function also updates quota and i_blocks field.
 */
int
ext3_new_block(handle_t *handle, struct inode *inode, unsigned long goal,
		u32 *prealloc_count, u32 *prealloc_block, int *errp)
{
	struct buffer_head *bitmap_bh = NULL;	/* bh */
	struct buffer_head *gdp_bh;		/* bh2 */
	int group_no;				/* i */
	int ret_block;				/* j */
	int bgi;				/* blockgroup iteration index */
	int target_block;			/* tmp */
	int fatal = 0, err;
	int performed_allocation = 0;
	int free_blocks, root_blocks;
	struct super_block *sb;
	struct ext3_group_desc *gdp;
	struct ext3_super_block *es;
	struct ext3_sb_info *sbi;
#ifdef EXT3FS_DEBUG
	static int goal_hits, goal_attempts;
#endif
	*errp = -ENOSPC;
	sb = inode->i_sb;
	if (!sb) {
		printk("ext3_new_block: nonexistent device");
		return 0;
	}

	/*
	 * Check quota for allocation of this block.
	 */
	if (DQUOT_ALLOC_BLOCK(inode, 1)) {
		*errp = -EDQUOT;
		return 0;
	}

	sbi = EXT3_SB(sb);
	es = EXT3_SB(sb)->s_es;
	ext3_debug("goal=%lu.\n", goal);

	free_blocks = percpu_counter_read_positive(&sbi->s_freeblocks_counter);
	root_blocks = le32_to_cpu(es->s_r_blocks_count);
	if (free_blocks < root_blocks + 1 && !capable(CAP_SYS_RESOURCE) &&
		sbi->s_resuid != current->fsuid &&
		(sbi->s_resgid == 0 || !in_group_p (sbi->s_resgid))) {
		*errp = -ENOSPC;
		return 0;
	}

	/*
	 * First, test whether the goal block is free.
	 */
	if (goal < le32_to_cpu(es->s_first_data_block) ||
	    goal >= le32_to_cpu(es->s_blocks_count))
		goal = le32_to_cpu(es->s_first_data_block);
	group_no = (goal - le32_to_cpu(es->s_first_data_block)) /
			EXT3_BLOCKS_PER_GROUP(sb);
	gdp = ext3_get_group_desc(sb, group_no, &gdp_bh);
	if (!gdp)
		goto io_error;

	free_blocks = le16_to_cpu(gdp->bg_free_blocks_count);
	if (free_blocks > 0) {
		ret_block = ((goal - le32_to_cpu(es->s_first_data_block)) %
				EXT3_BLOCKS_PER_GROUP(sb));
		bitmap_bh = read_block_bitmap(sb, group_no);
		if (!bitmap_bh)
			goto io_error;
		ret_block = ext3_try_to_allocate(sb, handle, group_no,
					bitmap_bh, ret_block, &fatal);
		if (fatal)
			goto out;
		if (ret_block >= 0)
			goto allocated;
	}

	/*
	 * Now search the rest of the groups.  We assume that 
	 * i and gdp correctly point to the last group visited.
	 */
	for (bgi = 0; bgi < EXT3_SB(sb)->s_groups_count; bgi++) {
		group_no++;
		if (group_no >= EXT3_SB(sb)->s_groups_count)
			group_no = 0;
		gdp = ext3_get_group_desc(sb, group_no, &gdp_bh);
		if (!gdp) {
			*errp = -EIO;
			goto out;
		}
		free_blocks = le16_to_cpu(gdp->bg_free_blocks_count);
		if (free_blocks <= 0)
			continue;

		brelse(bitmap_bh);
		bitmap_bh = read_block_bitmap(sb, group_no);
		if (!bitmap_bh)
			goto io_error;
		ret_block = ext3_try_to_allocate(sb, handle, group_no,
						bitmap_bh, -1, &fatal);
		if (fatal)
			goto out;
		if (ret_block >= 0) 
			goto allocated;
	}

	/* No space left on the device */
	*errp = -ENOSPC;
	goto out;

allocated:

	ext3_debug("using block group %d(%d)\n",
			group_no, gdp->bg_free_blocks_count);

	BUFFER_TRACE(gdp_bh, "get_write_access");
	fatal = ext3_journal_get_write_access(handle, gdp_bh);
	if (fatal)
		goto out;

	target_block = ret_block + group_no * EXT3_BLOCKS_PER_GROUP(sb)
				+ le32_to_cpu(es->s_first_data_block);

	if (target_block == le32_to_cpu(gdp->bg_block_bitmap) ||
	    target_block == le32_to_cpu(gdp->bg_inode_bitmap) ||
	    in_range(target_block, le32_to_cpu(gdp->bg_inode_table),
		      EXT3_SB(sb)->s_itb_per_group))
		ext3_error(sb, "ext3_new_block",
			    "Allocating block in system zone - "
			    "block = %u", target_block);

	performed_allocation = 1;

#ifdef CONFIG_JBD_DEBUG
	{
		struct buffer_head *debug_bh;

		/* Record bitmap buffer state in the newly allocated block */
		debug_bh = sb_find_get_block(sb, target_block);
		if (debug_bh) {
			BUFFER_TRACE(debug_bh, "state when allocated");
			BUFFER_TRACE2(debug_bh, bitmap_bh, "bitmap state");
			brelse(debug_bh);
		}
	}
	jbd_lock_bh_state(bitmap_bh);
	spin_lock(sb_bgl_lock(sbi, group_no));
	if (buffer_jbd(bitmap_bh) && bh2jh(bitmap_bh)->b_committed_data) {
		if (ext3_test_bit(ret_block,
				bh2jh(bitmap_bh)->b_committed_data)) {
			printk("%s: block was unexpectedly set in "
				"b_committed_data\n", __FUNCTION__);
		}
	}
	ext3_debug("found bit %d\n", ret_block);
	spin_unlock(sb_bgl_lock(sbi, group_no));
	jbd_unlock_bh_state(bitmap_bh);
#endif

	/* ret_block was blockgroup-relative.  Now it becomes fs-relative */
	ret_block = target_block;

	if (ret_block >= le32_to_cpu(es->s_blocks_count)) {
		ext3_error(sb, "ext3_new_block",
			    "block(%d) >= blocks count(%d) - "
			    "block_group = %d, es == %p ", ret_block,
			le32_to_cpu(es->s_blocks_count), group_no, es);
		goto out;
	}

	/*
	 * It is up to the caller to add the new buffer to a journal
	 * list of some description.  We don't know in advance whether
	 * the caller wants to use it as metadata or data.
	 */
	ext3_debug("allocating block %d. Goal hits %d of %d.\n",
			ret_block, goal_hits, goal_attempts);

	spin_lock(sb_bgl_lock(sbi, group_no));
	gdp->bg_free_blocks_count =
			cpu_to_le16(le16_to_cpu(gdp->bg_free_blocks_count) - 1);
	spin_unlock(sb_bgl_lock(sbi, group_no));
	percpu_counter_mod(&sbi->s_freeblocks_counter, -1);

	BUFFER_TRACE(gdp_bh, "journal_dirty_metadata for group descriptor");
	err = ext3_journal_dirty_metadata(handle, gdp_bh);
	if (!fatal)
		fatal = err;

	sb->s_dirt = 1;
	if (fatal)
		goto out;

	*errp = 0;
	brelse(bitmap_bh);
	return ret_block;

io_error:
	*errp = -EIO;
out:
	if (fatal) {
		*errp = fatal;
		ext3_std_error(sb, fatal);
	}
	/*
	 * Undo the block allocation
	 */
	if (!performed_allocation)
		DQUOT_FREE_BLOCK(inode, 1);
	brelse(bitmap_bh);
	return 0;
}

unsigned long ext3_count_free_blocks(struct super_block *sb)
{
	unsigned long desc_count;
	struct ext3_group_desc *gdp;
	int i;
#ifdef EXT3FS_DEBUG
	struct ext3_super_block *es;
	unsigned long bitmap_count, x;
	struct buffer_head *bitmap_bh = NULL;

	lock_super(sb);
	es = EXT3_SB(sb)->s_es;
	desc_count = 0;
	bitmap_count = 0;
	gdp = NULL;
	for (i = 0; i < EXT3_SB(sb)->s_groups_count; i++) {
		gdp = ext3_get_group_desc(sb, i, NULL);
		if (!gdp)
			continue;
		desc_count += le16_to_cpu(gdp->bg_free_blocks_count);
		brelse(bitmap_bh);
		bitmap_bh = read_block_bitmap(sb, i);
		if (bitmap_bh == NULL)
			continue;

		x = ext3_count_free(bitmap_bh, sb->s_blocksize);
		printk("group %d: stored = %d, counted = %lu\n",
			i, le16_to_cpu(gdp->bg_free_blocks_count), x);
		bitmap_count += x;
	}
	brelse(bitmap_bh);
	printk("ext3_count_free_blocks: stored = %u, computed = %lu, %lu\n",
	       le32_to_cpu(es->s_free_blocks_count), desc_count, bitmap_count);
	unlock_super(sb);
	return bitmap_count;
#else
	desc_count = 0;
	for (i = 0; i < EXT3_SB(sb)->s_groups_count; i++) {
		gdp = ext3_get_group_desc(sb, i, NULL);
		if (!gdp)
			continue;
		desc_count += le16_to_cpu(gdp->bg_free_blocks_count);
	}

	return desc_count;
#endif
}

static inline int block_in_use(unsigned long block,
				struct super_block * sb,
				unsigned char * map)
{
	return ext3_test_bit ((block -
		le32_to_cpu(EXT3_SB(sb)->s_es->s_first_data_block)) %
			 EXT3_BLOCKS_PER_GROUP(sb), map);
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

int ext3_group_sparse(int group)
{
	return (test_root(group, 3) || test_root(group, 5) ||
		test_root(group, 7));
}

/**
 *	ext3_bg_has_super - number of blocks used by the superblock in group
 *	@sb: superblock for filesystem
 *	@group: group number to check
 *
 *	Return the number of blocks used by the superblock (primary or backup)
 *	in this group.  Currently this will be only 0 or 1.
 */
int ext3_bg_has_super(struct super_block *sb, int group)
{
	if (EXT3_HAS_RO_COMPAT_FEATURE(sb,EXT3_FEATURE_RO_COMPAT_SPARSE_SUPER)&&
	    !ext3_group_sparse(group))
		return 0;
	return 1;
}

/**
 *	ext3_bg_num_gdb - number of blocks used by the group table in group
 *	@sb: superblock for filesystem
 *	@group: group number to check
 *
 *	Return the number of blocks used by the group descriptor table
 *	(primary or backup) in this group.  In the future there may be a
 *	different number of descriptor blocks in each group.
 */
unsigned long ext3_bg_num_gdb(struct super_block *sb, int group)
{
	if (EXT3_HAS_RO_COMPAT_FEATURE(sb,EXT3_FEATURE_RO_COMPAT_SPARSE_SUPER)&&
	    !ext3_group_sparse(group))
		return 0;
	return EXT3_SB(sb)->s_gdb_count;
}

#ifdef CONFIG_EXT3_CHECK
/* Called at mount-time, super-block is locked */
void ext3_check_blocks_bitmap (struct super_block * sb)
{
	struct ext3_super_block *es;
	unsigned long desc_count, bitmap_count, x, j;
	unsigned long desc_blocks;
	struct buffer_head *bitmap_bh = NULL;
	struct ext3_group_desc *gdp;
	int i;

	es = EXT3_SB(sb)->s_es;
	desc_count = 0;
	bitmap_count = 0;
	gdp = NULL;
	for (i = 0; i < EXT3_SB(sb)->s_groups_count; i++) {
		gdp = ext3_get_group_desc (sb, i, NULL);
		if (!gdp)
			continue;
		desc_count += le16_to_cpu(gdp->bg_free_blocks_count);
		brelse(bitmap_bh);
		bitmap_bh = read_block_bitmap(sb, i);
		if (bitmap_bh == NULL)
			continue;

		if (ext3_bg_has_super(sb, i) &&
				!ext3_test_bit(0, bitmap_bh->b_data))
			ext3_error(sb, __FUNCTION__,
				   "Superblock in group %d is marked free", i);

		desc_blocks = ext3_bg_num_gdb(sb, i);
		for (j = 0; j < desc_blocks; j++)
			if (!ext3_test_bit(j + 1, bitmap_bh->b_data))
				ext3_error(sb, __FUNCTION__,
					   "Descriptor block #%ld in group "
					   "%d is marked free", j, i);

		if (!block_in_use (le32_to_cpu(gdp->bg_block_bitmap),
						sb, bitmap_bh->b_data))
			ext3_error (sb, "ext3_check_blocks_bitmap",
				    "Block bitmap for group %d is marked free",
				    i);

		if (!block_in_use (le32_to_cpu(gdp->bg_inode_bitmap),
						sb, bitmap_bh->b_data))
			ext3_error (sb, "ext3_check_blocks_bitmap",
				    "Inode bitmap for group %d is marked free",
				    i);

		for (j = 0; j < EXT3_SB(sb)->s_itb_per_group; j++)
			if (!block_in_use (le32_to_cpu(gdp->bg_inode_table) + j,
							sb, bitmap_bh->b_data))
				ext3_error (sb, "ext3_check_blocks_bitmap",
					    "Block #%d of the inode table in "
					    "group %d is marked free", j, i);

		x = ext3_count_free(bitmap_bh, sb->s_blocksize);
		if (le16_to_cpu(gdp->bg_free_blocks_count) != x)
			ext3_error (sb, "ext3_check_blocks_bitmap",
				    "Wrong free blocks count for group %d, "
				    "stored = %d, counted = %lu", i,
				    le16_to_cpu(gdp->bg_free_blocks_count), x);
		bitmap_count += x;
	}
	brelse(bitmap_bh);
	if (le32_to_cpu(es->s_free_blocks_count) != bitmap_count)
		ext3_error (sb, "ext3_check_blocks_bitmap",
			"Wrong free blocks count in super block, "
			"stored = %lu, counted = %lu",
			(unsigned long)le32_to_cpu(es->s_free_blocks_count),
			bitmap_count);
}
#endif
