/*
 *  linux/fs/sysv/truncate.c
 *
 *  minix/truncate.c
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  coh/truncate.c
 *  Copyright (C) 1993  Pascal Haible, Bruno Haible
 *
 *  sysv/truncate.c
 *  Copyright (C) 1993  Bruno Haible
 */

#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/sysv_fs.h>
#include <linux/stat.h>


/* Linus' implementation of truncate.
 * It doesn't need locking because it can tell from looking at bh->b_count
 * whether a given block is in use elsewhere.
 */

/*
 * Truncate has the most races in the whole filesystem: coding it is
 * a pain in the a**, especially as I don't do any locking.
 *
 * The code may look a bit weird, but that's just because I've tried to
 * handle things like file-size changes in a somewhat graceful manner.
 * Anyway, truncating a file at the same time somebody else writes to it
 * is likely to result in pretty weird behaviour...
 *
 * The new code handles normal truncates (size = 0) as well as the more
 * general case (size = XXX). I hope.
 */

#define DATA_BUFFER_USED(bh) \
	(atomic_read(&bh->b_count)>1 || buffer_locked(bh))

/* We throw away any data beyond inode->i_size. */

static int trunc_direct(struct inode * inode)
{
	struct super_block * sb;
	unsigned int i;
	u32 * p;
	u32 block;
	struct buffer_head * bh;
	int retry = 0;

	sb = inode->i_sb;
repeat:
	for (i = ((unsigned long) inode->i_size + sb->sv_block_size_1) >> sb->sv_block_size_bits; i < 10; i++) {
		p = inode->u.sysv_i.i_data + i;
		block = *p;
		if (!block)
			continue;
		bh = sv_get_hash_table(sb, inode->i_dev, block);
		if ((i << sb->sv_block_size_bits) < inode->i_size) {
			brelse(bh);
			goto repeat;
		}
		if ((bh && DATA_BUFFER_USED(bh)) || (block != *p)) {
			retry = 1;
			brelse(bh);
			continue;
		}
		*p = 0;
		mark_inode_dirty(inode);
		brelse(bh);
		sysv_free_block(sb,block);
	}
	return retry;
}

static int trunc_indirect(struct inode * inode, unsigned long offset, sysv_zone_t * p, int convert, unsigned char * dirt)
{
	unsigned long indtmp, indblock;
	struct super_block * sb;
	struct buffer_head * indbh;
	unsigned int i;
	sysv_zone_t * ind;
	unsigned long tmp, block;
	struct buffer_head * bh;
	int retry = 0;

	indblock = indtmp = *p;
	if (convert)
		indblock = from_coh_ulong(indblock);
	if (!indblock)
		return 0;
	sb = inode->i_sb;
	indbh = sv_bread(sb, inode->i_dev, indblock);
	if (indtmp != *p) {
		brelse(indbh);
		return 1;
	}
	if (!indbh) {
		*p = 0;
		*dirt = 1;
		return 0;
	}
repeat:
	if (inode->i_size < offset)
		i = 0;
	else
		i = (inode->i_size - offset + sb->sv_block_size_1) >> sb->sv_block_size_bits;
	for (; i < sb->sv_ind_per_block; i++) {
		ind = ((sysv_zone_t *) indbh->b_data) + i;
		block = tmp = *ind;
		if (sb->sv_convert)
			block = from_coh_ulong(block);
		if (!block)
			continue;
		bh = sv_get_hash_table(sb, inode->i_dev, block);
		if ((i << sb->sv_block_size_bits) + offset < inode->i_size) {
			brelse(bh);
			goto repeat;
		}
		if ((bh && DATA_BUFFER_USED(bh)) || (tmp != *ind)) {
			retry = 1;
			brelse(bh);
			continue;
		}
		*ind = 0;
		mark_buffer_dirty(indbh);
		brelse(bh);
		sysv_free_block(sb,block);
	}
	for (i = 0; i < sb->sv_ind_per_block; i++)
		if (((sysv_zone_t *) indbh->b_data)[i])
			goto done;
	if (DATA_BUFFER_USED(indbh) || (indtmp != *p)) {
		brelse(indbh);
		return 1;
	}
	*p = 0;
	*dirt = 1;
	sysv_free_block(sb,indblock);
done:
	brelse(indbh);
	return retry;
}

static int trunc_dindirect(struct inode * inode, unsigned long offset, sysv_zone_t * p, int convert, unsigned char * dirt)
{
	u32 indtmp, indblock;
	struct super_block * sb;
	struct buffer_head * indbh;
	unsigned int i;
	sysv_zone_t * ind;
	u32 tmp, block;
	int retry = 0;

	indblock = indtmp = *p;
	if (convert)
		indblock = from_coh_ulong(indblock);
	if (!indblock)
		return 0;
	sb = inode->i_sb;
	indbh = sv_bread(sb, inode->i_dev, indblock);
	if (indtmp != *p) {
		brelse(indbh);
		return 1;
	}
	if (!indbh) {
		*p = 0;
		*dirt = 1;
		return 0;
	}
	if (inode->i_size < offset)
		i = 0;
	else
		i = (inode->i_size - offset + sb->sv_ind_per_block_block_size_1) >> sb->sv_ind_per_block_block_size_bits;
	for (; i < sb->sv_ind_per_block; i++) {
		unsigned char dirty = 0;
		ind = ((sysv_zone_t *) indbh->b_data) + i;
		block = tmp = *ind;
		if (sb->sv_convert)
			block = from_coh_ulong(block);
		if (!block)
			continue;
		retry |= trunc_indirect(inode,offset+(i<<sb->sv_ind_per_block_bits),ind,sb->sv_convert,&dirty);
		if (dirty)
			mark_buffer_dirty(indbh);
	}
	for (i = 0; i < sb->sv_ind_per_block; i++)
		if (((sysv_zone_t *) indbh->b_data)[i])
			goto done;
	if (DATA_BUFFER_USED(indbh) || (indtmp != *p)) {
		brelse(indbh);
		return 1;
	}
	*p = 0;
	*dirt = 1;
	sysv_free_block(sb,indblock);
done:
	brelse(indbh);
	return retry;
}

static int trunc_tindirect(struct inode * inode, unsigned long offset, sysv_zone_t * p, int convert, unsigned char * dirt)
{
	u32 indtmp, indblock;
	struct super_block * sb;
	struct buffer_head * indbh;
	unsigned int i;
	sysv_zone_t * ind;
	u32 tmp, block;
	int retry = 0;

	indblock = indtmp = *p;
	if (convert)
		indblock = from_coh_ulong(indblock);
	if (!indblock)
		return 0;
	sb = inode->i_sb;
	indbh = sv_bread(sb, inode->i_dev, indblock);
	if (indtmp != *p) {
		brelse(indbh);
		return 1;
	}
	if (!indbh) {
		*p = 0;
		*dirt = 1;
		return 0;
	}
	if (inode->i_size < offset)
		i = 0;
	else
		i = (inode->i_size - offset + sb->sv_ind_per_block_2_block_size_1) >> sb->sv_ind_per_block_2_block_size_bits;
	for (; i < sb->sv_ind_per_block; i++) {
		unsigned char dirty = 0;
		ind = ((sysv_zone_t *) indbh->b_data) + i;
		block = tmp = *ind;
		if (sb->sv_convert)
			block = from_coh_ulong(block);
		if (!block)
			continue;
		retry |= trunc_dindirect(inode,offset+(i<<sb->sv_ind_per_block_2_bits),ind,sb->sv_convert,&dirty);
		if (dirty)
			mark_buffer_dirty(indbh);
	}
	for (i = 0; i < sb->sv_ind_per_block; i++)
		if (((sysv_zone_t *) indbh->b_data)[i])
			goto done;
	if (DATA_BUFFER_USED(indbh) || (indtmp != *p)) {
		brelse(indbh);
		return 1;
	}
	*p = 0;
	*dirt = 1;
	sysv_free_block(sb,indblock);
done:
	brelse(indbh);
	return retry;
}

static int trunc_all(struct inode * inode)
{
	struct super_block * sb;
	char dirty;

	sb = inode->i_sb;
	return trunc_direct(inode)
	     | trunc_indirect(inode,sb->sv_ind0_size,&inode->u.sysv_i.i_data[10],0,&dirty)
	     | trunc_dindirect(inode,sb->sv_ind1_size,&inode->u.sysv_i.i_data[11],0,&dirty)
	     | trunc_tindirect(inode,sb->sv_ind2_size,&inode->u.sysv_i.i_data[12],0,&dirty);
}


void sysv_truncate(struct inode * inode)
{
	/* If this is called from sysv_put_inode, we needn't worry about
	 * races as we are just losing the last reference to the inode.
	 * If this is called from another place, let's hope it's a regular
	 * file.
	 * Truncating symbolic links is strange. We assume we don't truncate
	 * a directory we are just modifying. We ensure we don't truncate
	 * a regular file we are just writing to, by use of a lock.
	 */
	if (S_ISLNK(inode->i_mode))
		printk("sysv_truncate: truncating symbolic link\n");
	else if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode)))
		return;
	while (trunc_all(inode)) {
		current->counter = 0;
		schedule();
	}
	inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	mark_inode_dirty(inode);
}
