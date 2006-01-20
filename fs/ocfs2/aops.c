/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <asm/byteorder.h>

#define MLOG_MASK_PREFIX ML_FILE_IO
#include <cluster/masklog.h>

#include "ocfs2.h"

#include "alloc.h"
#include "dlmglue.h"
#include "extent_map.h"
#include "file.h"
#include "inode.h"
#include "journal.h"
#include "symlink.h"

#include "buffer_head_io.h"

static int ocfs2_symlink_get_block(struct inode *inode, sector_t iblock,
				   struct buffer_head *bh_result, int create)
{
	int err = -EIO;
	int status;
	ocfs2_dinode *fe = NULL;
	struct buffer_head *bh = NULL;
	struct buffer_head *buffer_cache_bh = NULL;
	ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	void *kaddr;

	mlog_entry("(0x%p, %llu, 0x%p, %d)\n", inode,
		   (unsigned long long)iblock, bh_result, create);

	BUG_ON(ocfs2_inode_is_fast_symlink(inode));

	if ((iblock << inode->i_sb->s_blocksize_bits) > PATH_MAX + 1) {
		mlog(ML_ERROR, "block offset > PATH_MAX: %llu",
		     (unsigned long long)iblock);
		goto bail;
	}

	status = ocfs2_read_block(OCFS2_SB(inode->i_sb),
				  OCFS2_I(inode)->ip_blkno,
				  &bh, OCFS2_BH_CACHED, inode);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}
	fe = (ocfs2_dinode *) bh->b_data;

	if (!OCFS2_IS_VALID_DINODE(fe)) {
		mlog(ML_ERROR, "Invalid dinode #%"MLFu64": signature = %.*s\n",
		     fe->i_blkno, 7, fe->i_signature);
		goto bail;
	}

	if ((u64)iblock >= ocfs2_clusters_to_blocks(inode->i_sb,
						    le32_to_cpu(fe->i_clusters))) {
		mlog(ML_ERROR, "block offset is outside the allocated size: "
		     "%llu\n", (unsigned long long)iblock);
		goto bail;
	}

	/* We don't use the page cache to create symlink data, so if
	 * need be, copy it over from the buffer cache. */
	if (!buffer_uptodate(bh_result) && ocfs2_inode_is_new(inode)) {
		u64 blkno = le64_to_cpu(fe->id2.i_list.l_recs[0].e_blkno) +
			    iblock;
		buffer_cache_bh = sb_getblk(osb->sb, blkno);
		if (!buffer_cache_bh) {
			mlog(ML_ERROR, "couldn't getblock for symlink!\n");
			goto bail;
		}

		/* we haven't locked out transactions, so a commit
		 * could've happened. Since we've got a reference on
		 * the bh, even if it commits while we're doing the
		 * copy, the data is still good. */
		if (buffer_jbd(buffer_cache_bh)
		    && ocfs2_inode_is_new(inode)) {
			kaddr = kmap_atomic(bh_result->b_page, KM_USER0);
			if (!kaddr) {
				mlog(ML_ERROR, "couldn't kmap!\n");
				goto bail;
			}
			memcpy(kaddr + (bh_result->b_size * iblock),
			       buffer_cache_bh->b_data,
			       bh_result->b_size);
			kunmap_atomic(kaddr, KM_USER0);
			set_buffer_uptodate(bh_result);
		}
		brelse(buffer_cache_bh);
	}

	map_bh(bh_result, inode->i_sb,
	       le64_to_cpu(fe->id2.i_list.l_recs[0].e_blkno) + iblock);

	err = 0;

bail:
	if (bh)
		brelse(bh);

	mlog_exit(err);
	return err;
}

static int ocfs2_get_block(struct inode *inode, sector_t iblock,
			   struct buffer_head *bh_result, int create)
{
	int err = -EIO;
	u64 vbo = 0;
	u64 p_blkno;

	mlog_entry("(0x%p, %llu, 0x%p, %d)\n", inode,
		   (unsigned long long)iblock, bh_result, create);

	if (OCFS2_I(inode)->ip_flags & OCFS2_INODE_SYSTEM_FILE) {
		mlog(ML_NOTICE, "get_block on system inode 0x%p (%lu)\n",
		     inode, inode->i_ino);
	}

	if (S_ISLNK(inode->i_mode)) {
		/* this always does I/O for some reason. */
		err = ocfs2_symlink_get_block(inode, iblock, bh_result, create);
		goto bail;
	}

	vbo = (u64)iblock << inode->i_sb->s_blocksize_bits;

	/* this can happen if another node truncs after our extend! */
	spin_lock(&OCFS2_I(inode)->ip_lock);
	if (iblock >=
	    ocfs2_clusters_to_blocks(inode->i_sb,
				     OCFS2_I(inode)->ip_clusters)) {
		spin_unlock(&OCFS2_I(inode)->ip_lock);
		err = -EIO;
		goto bail;
	}
	spin_unlock(&OCFS2_I(inode)->ip_lock);

	err = ocfs2_extent_map_get_blocks(inode, iblock, 1, &p_blkno,
					  NULL);

	if (err) {
		mlog(ML_ERROR, "Error %d from get_blocks(0x%p, %llu, 1, "
		     "%"MLFu64", NULL)\n", err, inode,
		     (unsigned long long)iblock, p_blkno);
		goto bail;
	}

	map_bh(bh_result, inode->i_sb, p_blkno);

	err = 0;

	if (bh_result->b_blocknr == 0) {
		err = -EIO;
		mlog(ML_ERROR, "iblock = %llu p_blkno = %"MLFu64" "
		     "blkno=(%"MLFu64")\n", (unsigned long long)iblock,
		     p_blkno, OCFS2_I(inode)->ip_blkno);
	}

	if (vbo < OCFS2_I(inode)->ip_mmu_private)
		goto bail;
	if (!create)
		goto bail;
	if (vbo != OCFS2_I(inode)->ip_mmu_private) {
		mlog(ML_ERROR, "Uh-oh, vbo = %"MLFi64", i_size = %lld, "
		     "mmu = %lld, inode = %"MLFu64"\n", vbo,
		     i_size_read(inode), OCFS2_I(inode)->ip_mmu_private,
		     OCFS2_I(inode)->ip_blkno);
		BUG();
		err = -EIO;
		goto bail;
	}

	set_buffer_new(bh_result);
	OCFS2_I(inode)->ip_mmu_private += inode->i_sb->s_blocksize;

bail:
	if (err < 0)
		err = -EIO;

	mlog_exit(err);
	return err;
}

static int ocfs2_readpage(struct file *file, struct page *page)
{
	int ret;

	mlog_entry("(0x%p, %lu)\n", file, (page ? page->index : 0));

	ret = block_read_full_page(page, ocfs2_get_block);

	mlog_exit(ret);

	return ret;
}

static int ocfs2_writepage(struct page *page, struct writeback_control *wbc)
{
	int ret;

	mlog_entry("(0x%p)\n", page);

	ret = block_write_full_page(page, ocfs2_get_block, wbc);

	mlog_exit(ret);

	return ret;
}

static int ocfs2_prepare_write(struct file *file, struct page *page,
		unsigned from, unsigned to)
{
	int ret;

	mlog_entry("(0x%p, 0x%p, %u, %u)\n", file, page, from, to);

	ret = cont_prepare_write(page, from, to, ocfs2_get_block,
		&(OCFS2_I(page->mapping->host)->ip_mmu_private));

	mlog_exit(ret);

	return ret;
}

static int ocfs2_commit_write(struct file *file, struct page *page,
			      unsigned from, unsigned to)
{
	int ret;

	mlog_entry("(0x%p, 0x%p, %u, %u)\n", file, page, from, to);

	ret = generic_commit_write(file, page, from, to);

	mlog_exit(ret);

	return ret;
}

static sector_t ocfs2_bmap(struct address_space *mapping, sector_t block)
{
	sector_t status;
	u64 p_blkno = 0;
	int err = 0;
	struct inode *inode = mapping->host;

	mlog_entry("(block = %llu)\n", (unsigned long long)block);

	/* We don't need to lock journal system files, since they aren't
	 * accessed concurrently from multiple nodes.
	 */
	if (!INODE_JOURNAL(inode)) {
		err = ocfs2_meta_lock(inode, NULL, NULL, 0);
		if (err) {
			if (err != -ENOENT)
				mlog_errno(err);
			goto bail;
		}
		down_read(&OCFS2_I(inode)->ip_alloc_sem);
	}

	err = ocfs2_extent_map_get_blocks(inode, block, 1, &p_blkno,
					  NULL);

	if (!INODE_JOURNAL(inode)) {
		up_read(&OCFS2_I(inode)->ip_alloc_sem);
		ocfs2_meta_unlock(inode, 0);
	}

	if (err) {
		mlog(ML_ERROR, "get_blocks() failed, block = %llu\n",
		     (unsigned long long)block);
		mlog_errno(err);
		goto bail;
	}


bail:
	status = err ? 0 : p_blkno;

	mlog_exit((int)status);

	return status;
}

/*
 * TODO: Make this into a generic get_blocks function.
 *
 * From do_direct_io in direct-io.c:
 *  "So what we do is to permit the ->get_blocks function to populate
 *   bh.b_size with the size of IO which is permitted at this offset and
 *   this i_blkbits."
 *
 * This function is called directly from get_more_blocks in direct-io.c.
 *
 * called like this: dio->get_blocks(dio->inode, fs_startblk,
 * 					fs_count, map_bh, dio->rw == WRITE);
 */
static int ocfs2_direct_IO_get_blocks(struct inode *inode, sector_t iblock,
				     unsigned long max_blocks,
				     struct buffer_head *bh_result, int create)
{
	int ret;
	u64 vbo_max; /* file offset, max_blocks from iblock */
	u64 p_blkno;
	int contig_blocks;
	unsigned char blocksize_bits;

	if (!inode || !bh_result) {
		mlog(ML_ERROR, "inode or bh_result is null\n");
		return -EIO;
	}

	blocksize_bits = inode->i_sb->s_blocksize_bits;

	/* This function won't even be called if the request isn't all
	 * nicely aligned and of the right size, so there's no need
	 * for us to check any of that. */

	vbo_max = ((u64)iblock + max_blocks) << blocksize_bits;

	spin_lock(&OCFS2_I(inode)->ip_lock);
	if ((iblock + max_blocks) >
	    ocfs2_clusters_to_blocks(inode->i_sb,
				     OCFS2_I(inode)->ip_clusters)) {
		spin_unlock(&OCFS2_I(inode)->ip_lock);
		ret = -EIO;
		goto bail;
	}
	spin_unlock(&OCFS2_I(inode)->ip_lock);

	/* This figures out the size of the next contiguous block, and
	 * our logical offset */
	ret = ocfs2_extent_map_get_blocks(inode, iblock, 1, &p_blkno,
					  &contig_blocks);
	if (ret) {
		mlog(ML_ERROR, "get_blocks() failed iblock=%llu\n",
		     (unsigned long long)iblock);
		ret = -EIO;
		goto bail;
	}

	map_bh(bh_result, inode->i_sb, p_blkno);

	/* make sure we don't map more than max_blocks blocks here as
	   that's all the kernel will handle at this point. */
	if (max_blocks < contig_blocks)
		contig_blocks = max_blocks;
	bh_result->b_size = contig_blocks << blocksize_bits;
bail:
	return ret;
}

static ssize_t ocfs2_direct_IO(int rw,
			       struct kiocb *iocb,
			       const struct iovec *iov,
			       loff_t offset,
			       unsigned long nr_segs)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file->f_dentry->d_inode->i_mapping->host;
	int ret;

	mlog_entry_void();

	/* blockdev_direct_IO checks alignment for us, using */
	ret = blockdev_direct_IO_no_locking(rw, iocb, inode,
					    inode->i_sb->s_bdev, iov, offset,
					    nr_segs, ocfs2_direct_IO_get_blocks,
					    NULL);

	mlog_exit(ret);
	return ret;
}

struct address_space_operations ocfs2_aops = {
	.readpage	= ocfs2_readpage,
	.writepage	= ocfs2_writepage,
	.prepare_write	= ocfs2_prepare_write,
	.commit_write	= ocfs2_commit_write,
	.bmap		= ocfs2_bmap,
	.sync_page	= block_sync_page,
	.direct_IO	= ocfs2_direct_IO
};
