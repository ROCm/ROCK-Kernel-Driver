/*
 *  linux/fs/sysv/balloc.c
 *
 *  minix/bitmap.c
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  ext/freelists.c
 *  Copyright (C) 1992  Remy Card (card@masi.ibp.fr)
 *
 *  xenix/alloc.c
 *  Copyright (C) 1992  Doug Evans
 *
 *  coh/alloc.c
 *  Copyright (C) 1993  Pascal Haible, Bruno Haible
 *
 *  sysv/balloc.c
 *  Copyright (C) 1993  Bruno Haible
 *
 *  This file contains code for allocating/freeing blocks.
 */

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/sysv_fs.h>
#include <linux/string.h>
#include <linux/locks.h>

/* We don't trust the value of
   sb->sv_sbd2->s_tfree = *sb->sv_sb_total_free_blocks
   but we nevertheless keep it up to date. */

void sysv_free_block(struct super_block * sb, unsigned int block)
{
	struct buffer_head * bh;
	char * bh_data;

	if (!sb) {
		printk("sysv_free_block: trying to free block on nonexistent device\n");
		return;
	}
	if (block < sb->sv_firstdatazone || block >= sb->sv_nzones) {
		printk("sysv_free_block: trying to free block not in datazone\n");
		return;
	}
	lock_super(sb);
	if (*sb->sv_sb_flc_count > sb->sv_flc_size) {
		printk("sysv_free_block: flc_count > flc_size\n");
		unlock_super(sb);
		return;
	}
	/* If the free list head in super-block is full, it is copied
	 * into this block being freed:
	 */
	if (*sb->sv_sb_flc_count == sb->sv_flc_size) {
		u16 * flc_count;
		u32 * flc_blocks;

		bh = sv_getblk(sb, sb->s_dev, block);
		if (!bh) {
			printk("sysv_free_block: getblk() failed\n");
			unlock_super(sb);
			return;
		}
		bh_data = bh->b_data;
		switch (sb->sv_type) {
			case FSTYPE_XENIX:
				flc_count = &((struct xenix_freelist_chunk *) bh_data)->fl_nfree;
				flc_blocks = &((struct xenix_freelist_chunk *) bh_data)->fl_free[0];
				break;
			case FSTYPE_SYSV4:
				flc_count = &((struct sysv4_freelist_chunk *) bh_data)->fl_nfree;
				flc_blocks = &((struct sysv4_freelist_chunk *) bh_data)->fl_free[0];
				break;
			case FSTYPE_SYSV2:
				flc_count = &((struct sysv2_freelist_chunk *) bh_data)->fl_nfree;
				flc_blocks = &((struct sysv2_freelist_chunk *) bh_data)->fl_free[0];
				break;
			case FSTYPE_COH:
				flc_count = &((struct coh_freelist_chunk *) bh_data)->fl_nfree;
				flc_blocks = &((struct coh_freelist_chunk *) bh_data)->fl_free[0];
				break;
			default: panic("sysv_free_block: invalid fs type\n");
		}
		*flc_count = *sb->sv_sb_flc_count; /* = sb->sv_flc_size */
		memcpy(flc_blocks, sb->sv_sb_flc_blocks, *flc_count * sizeof(sysv_zone_t));
		mark_buffer_dirty(bh);
		mark_buffer_uptodate(bh, 1);
		brelse(bh);
		*sb->sv_sb_flc_count = 0;
	} else
	/* If the free list head in super-block is empty, create a new head
	 * in this block being freed:
	 */
	if (*sb->sv_sb_flc_count == 0) { /* Applies only to Coherent FS */
		bh = sv_getblk(sb, sb->s_dev, block);
		if (!bh) {
			printk("sysv_free_block: getblk() failed\n");
			unlock_super(sb);
			return;
		}
		memset(bh->b_data, 0, sb->sv_block_size);
		/* this implies ((struct ..._freelist_chunk *) bh->b_data)->flc_count = 0; */
		mark_buffer_dirty(bh);
		mark_buffer_uptodate(bh, 1);
		brelse(bh);
		/* still *sb->sv_sb_flc_count = 0 */
	} else {
		/* Throw away block's contents */
		bh = sv_get_hash_table(sb, sb->s_dev, block);
		if (bh)
			mark_buffer_clean(bh);
		brelse(bh);
	}
	if (sb->sv_convert)
		block = to_coh_ulong(block);
	sb->sv_sb_flc_blocks[(*sb->sv_sb_flc_count)++] = block;
	if (sb->sv_convert)
		*sb->sv_sb_total_free_blocks =
		  to_coh_ulong(from_coh_ulong(*sb->sv_sb_total_free_blocks) + 1);
	else
		*sb->sv_sb_total_free_blocks = *sb->sv_sb_total_free_blocks + 1;
	mark_buffer_dirty(sb->sv_bh1); /* super-block has been modified */
	if (sb->sv_bh1 != sb->sv_bh2) mark_buffer_dirty(sb->sv_bh2);
	sb->s_dirt = 1; /* and needs time stamp */
	unlock_super(sb);
}

int sysv_new_block(struct super_block * sb)
{
	unsigned int block;
	struct buffer_head * bh;
	char * bh_data;

	if (!sb) {
		printk("sysv_new_block: trying to get new block from nonexistent device\n");
		return 0;
	}
	lock_super(sb);
	if (*sb->sv_sb_flc_count == 0) { /* Applies only to Coherent FS */
		unlock_super(sb);
		return 0;		/* no blocks available */
	}
	block = sb->sv_sb_flc_blocks[(*sb->sv_sb_flc_count)-1];
	if (sb->sv_convert)
		block = from_coh_ulong(block);
	if (block == 0) { /* Applies only to Xenix FS, SystemV FS */
		unlock_super(sb);
		return 0;		/* no blocks available */
	}
	(*sb->sv_sb_flc_count)--;
	if (block < sb->sv_firstdatazone || block >= sb->sv_nzones) {
		printk("sysv_new_block: new block %d is not in data zone\n",block);
		unlock_super(sb);
		return 0;
	}
	if (*sb->sv_sb_flc_count == 0) { /* the last block continues the free list */
		u16 * flc_count;
		u32 * flc_blocks;

		if (!(bh = sv_bread(sb, sb->s_dev, block))) {
			printk("sysv_new_block: cannot read free-list block\n");
			/* retry this same block next time */
			(*sb->sv_sb_flc_count)++;
			unlock_super(sb);
			return 0;
		}
		bh_data = bh->b_data;
		switch (sb->sv_type) {
			case FSTYPE_XENIX:
				flc_count = &((struct xenix_freelist_chunk *) bh_data)->fl_nfree;
				flc_blocks = &((struct xenix_freelist_chunk *) bh_data)->fl_free[0];
				break;
			case FSTYPE_SYSV4:
				flc_count = &((struct sysv4_freelist_chunk *) bh_data)->fl_nfree;
				flc_blocks = &((struct sysv4_freelist_chunk *) bh_data)->fl_free[0];
				break;
			case FSTYPE_SYSV2:
				flc_count = &((struct sysv2_freelist_chunk *) bh_data)->fl_nfree;
				flc_blocks = &((struct sysv2_freelist_chunk *) bh_data)->fl_free[0];
				break;
			case FSTYPE_COH:
				flc_count = &((struct coh_freelist_chunk *) bh_data)->fl_nfree;
				flc_blocks = &((struct coh_freelist_chunk *) bh_data)->fl_free[0];
				break;
			default: panic("sysv_new_block: invalid fs type\n");
		}
		if (*flc_count > sb->sv_flc_size) {
			printk("sysv_new_block: free-list block with >flc_size entries\n");
			brelse(bh);
			unlock_super(sb);
			return 0;
		}
		*sb->sv_sb_flc_count = *flc_count;
		memcpy(sb->sv_sb_flc_blocks, flc_blocks, *flc_count * sizeof(sysv_zone_t));
		brelse(bh);
	}
	/* Now the free list head in the superblock is valid again. */
	bh = sv_getblk(sb, sb->s_dev, block);
	if (!bh) {
		printk("sysv_new_block: getblk() failed\n");
		unlock_super(sb);
		return 0;
	}
	if (atomic_read(&bh->b_count) != 1) {
		printk("sysv_new_block: block already in use\n");
		unlock_super(sb);
		return 0;
	}
	memset(bh->b_data, 0, sb->sv_block_size);
	mark_buffer_dirty(bh);
	mark_buffer_uptodate(bh, 1);
	brelse(bh);
	if (sb->sv_convert)
		*sb->sv_sb_total_free_blocks =
		  to_coh_ulong(from_coh_ulong(*sb->sv_sb_total_free_blocks) - 1);
	else
		*sb->sv_sb_total_free_blocks = *sb->sv_sb_total_free_blocks - 1;
	mark_buffer_dirty(sb->sv_bh1); /* super-block has been modified */
	if (sb->sv_bh1 != sb->sv_bh2) mark_buffer_dirty(sb->sv_bh2);
	sb->s_dirt = 1; /* and needs time stamp */
	unlock_super(sb);
	return block;
}

unsigned long sysv_count_free_blocks(struct super_block * sb)
{
#if 1 /* test */
	int count, old_count;
	unsigned int block;
	struct buffer_head * bh;
	char * bh_data;
	int i;

	/* this causes a lot of disk traffic ... */
	count = 0;
	lock_super(sb);
	if (*sb->sv_sb_flc_count > 0) {
		for (i = *sb->sv_sb_flc_count ; /* i > 0 */ ; ) {
			block = sb->sv_sb_flc_blocks[--i];
			if (sb->sv_convert)
				block = from_coh_ulong(block);
			if (block == 0) /* block 0 terminates list */
				goto done;
			count++;
			if (i == 0)
				break;
		}
		/* block = sb->sv_sb_flc_blocks[0], the last block continues the free list */
		while (1) {
			u16 * flc_count;
			u32 * flc_blocks;

			if (block < sb->sv_firstdatazone || block >= sb->sv_nzones) {
				printk("sysv_count_free_blocks: new block %d is not in data zone\n",block);
				break;
			}
			if (!(bh = sv_bread(sb, sb->s_dev, block))) {
				printk("sysv_count_free_blocks: cannot read free-list block\n");
				break;
			}
			bh_data = bh->b_data;
			switch (sb->sv_type) {
				case FSTYPE_XENIX:
					flc_count = &((struct xenix_freelist_chunk *) bh_data)->fl_nfree;
					flc_blocks = &((struct xenix_freelist_chunk *) bh_data)->fl_free[0];
					break;
				case FSTYPE_SYSV4:
					flc_count = &((struct sysv4_freelist_chunk *) bh_data)->fl_nfree;
					flc_blocks = &((struct sysv4_freelist_chunk *) bh_data)->fl_free[0];
					break;
				case FSTYPE_SYSV2:
					flc_count = &((struct sysv2_freelist_chunk *) bh_data)->fl_nfree;
					flc_blocks = &((struct sysv2_freelist_chunk *) bh_data)->fl_free[0];
					break;
				case FSTYPE_COH:
					flc_count = &((struct coh_freelist_chunk *) bh_data)->fl_nfree;
					flc_blocks = &((struct coh_freelist_chunk *) bh_data)->fl_free[0];
					break;
				default: panic("sysv_count_free_blocks: invalid fs type\n");
			}
			if (*flc_count > sb->sv_flc_size) {
				printk("sysv_count_free_blocks: free-list block with >flc_size entries\n");
				brelse(bh);
				break;
			}
			if (*flc_count == 0) { /* Applies only to Coherent FS */
				brelse(bh);
				break;
			}
			for (i = *flc_count ; /* i > 0 */ ; ) {
				block = flc_blocks[--i];
				if (sb->sv_convert)
					block = from_coh_ulong(block);
				if (block == 0) /* block 0 terminates list */
					break;
				count++;
				if (i == 0)
					break;
			}
			/* block = flc_blocks[0], the last block continues the free list */
			brelse(bh);
			if (block == 0) /* Applies only to Xenix FS and SystemV FS */
				break;
		}
		done: ;
	}
	old_count = *sb->sv_sb_total_free_blocks;
	if (sb->sv_convert)
		old_count = from_coh_ulong(old_count);
	if (count != old_count) {
		printk("sysv_count_free_blocks: free block count was %d, correcting to %d\n",old_count,count);
		if (!(sb->s_flags & MS_RDONLY)) {
			*sb->sv_sb_total_free_blocks = (sb->sv_convert ? to_coh_ulong(count) : count);
			mark_buffer_dirty(sb->sv_bh2); /* super-block has been modified */
			sb->s_dirt = 1; /* and needs time stamp */
		}
	}
	unlock_super(sb);
	return count;
#else
	int count;

	count = *sb->sv_sb_total_free_blocks;
	if (sb->sv_convert)
		count = from_coh_ulong(count);
	return count;
#endif
}

