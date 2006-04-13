/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * file.c
 *
 * File open, close, extend, truncate
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
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/uio.h>

#define MLOG_MASK_PREFIX ML_INODE
#include <cluster/masklog.h>

#include "ocfs2.h"

#include "aio.h"
#include "alloc.h"
#include "dir.h"
#include "dlmglue.h"
#include "extent_map.h"
#include "file.h"
#include "sysfile.h"
#include "inode.h"
#include "journal.h"
#include "mmap.h"
#include "suballoc.h"
#include "super.h"

#include "buffer_head_io.h"

static int ocfs2_zero_extend(struct inode *inode);
static int ocfs2_orphan_for_truncate(struct ocfs2_super *osb,
				     struct inode *inode,
				     struct buffer_head *fe_bh,
				     u64 new_i_size);

int ocfs2_sync_inode(struct inode *inode)
{
	filemap_fdatawrite(inode->i_mapping);
	return sync_mapping_buffers(inode->i_mapping);
}

static int ocfs2_file_open(struct inode *inode, struct file *file)
{
	int status;
	int mode = file->f_flags;
	struct ocfs2_inode_info *oi = OCFS2_I(inode);

	mlog_entry("(0x%p, 0x%p, '%.*s')\n", inode, file,
		   file->f_dentry->d_name.len, file->f_dentry->d_name.name);

	spin_lock(&oi->ip_lock);

	/* Check that the inode hasn't been wiped from disk by another
	 * node. If it hasn't then we're safe as long as we hold the
	 * spin lock until our increment of open count. */
	if (OCFS2_I(inode)->ip_flags & OCFS2_INODE_DELETED) {
		spin_unlock(&oi->ip_lock);

		status = -ENOENT;
		goto leave;
	}

	if (mode & O_DIRECT)
		oi->ip_flags |= OCFS2_INODE_OPEN_DIRECT;

	oi->ip_open_count++;
	spin_unlock(&oi->ip_lock);
	status = 0;
leave:
	mlog_exit(status);
	return status;
}

static int ocfs2_file_release(struct inode *inode, struct file *file)
{
	struct ocfs2_inode_info *oi = OCFS2_I(inode);

	mlog_entry("(0x%p, 0x%p, '%.*s')\n", inode, file,
		       file->f_dentry->d_name.len,
		       file->f_dentry->d_name.name);

	spin_lock(&oi->ip_lock);
#ifdef OCFS2_DELETE_INODE_WORKAROUND
	/* Do the sync *before* decrementing ip_open_count as
	 * otherwise the voting code might allow this inode to be
	 * wiped. */
	if (oi->ip_open_count == 1 &&
	    oi->ip_flags & OCFS2_INODE_MAYBE_ORPHANED) {
		spin_unlock(&oi->ip_lock);
		write_inode_now(inode, 1);
		spin_lock(&oi->ip_lock);
	}
#endif
	if (!--oi->ip_open_count)
		oi->ip_flags &= ~OCFS2_INODE_OPEN_DIRECT;
	spin_unlock(&oi->ip_lock);

	mlog_exit(0);

	return 0;
}

static int ocfs2_sync_file(struct file *file,
			   struct dentry *dentry,
			   int datasync)
{
	int err = 0;
	journal_t *journal;
	struct inode *inode = dentry->d_inode;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);

	mlog_entry("(0x%p, 0x%p, %d, '%.*s')\n", file, dentry, datasync,
		   dentry->d_name.len, dentry->d_name.name);

	err = ocfs2_sync_inode(dentry->d_inode);
	if (err)
		goto bail;

	journal = osb->journal->j_journal;
	err = journal_force_commit(journal);

bail:
	mlog_exit(err);

	return (err < 0) ? -EIO : 0;
}

static void ocfs2_update_inode_size(struct inode *inode,
				    u64 new_size)
{
	i_size_write(inode, new_size);
	inode->i_blocks = ocfs2_align_bytes_to_sectors(new_size);
}

void ocfs2_file_finish_extension(struct inode *inode,
				 loff_t newsize,
				 unsigned direct_extend)
{
	int status;

	mlog(0, "inode %"MLFu64", newsize = %lld, direct_extend = %u\n",
	     OCFS2_I(inode)->ip_blkno, (long long)newsize, direct_extend);

	ocfs2_update_inode_size(inode, newsize);

#ifdef OCFS2_ORACORE_WORKAROUNDS
	if (direct_extend) {
		/*
		 * This leaves dirty data in holes.
		 * Caveat Emptor.
		 */
		OCFS2_I(inode)->ip_mmu_private = newsize;
		return;
	}
#endif

	status = ocfs2_zero_extend(inode);
	/*
	 * Don't overwrite the result of
	 * generic_file_write
	 */
	if (status)
		mlog(ML_ERROR, "Unable to pre-zero extension of inode "
		     "(%d)\n", status);
}

static ssize_t ocfs2_file_write(struct file *filp,
				const char __user *buf,
				size_t count,
				loff_t *ppos)
{
	struct iovec local_iov = { .iov_base = (void __user *)buf,
				   .iov_len = count };
	int ret = 0;
	struct ocfs2_super *osb = NULL;
	struct dentry *dentry = filp->f_dentry;
	struct inode *inode = dentry->d_inode;
	struct ocfs2_write_lock_info info = {0, };
	DECLARE_BUFFER_LOCK_CTXT(ctxt);

	mlog_entry("(0x%p, 0x%p, %u, '%.*s')\n", filp, buf,
		   (unsigned int)count,
		   filp->f_dentry->d_name.len,
		   filp->f_dentry->d_name.name);

	/* happy write of zero bytes */
	if (count == 0) {
		ret = 0;
		goto bail;
	}

	if (!inode) {
		mlog(0, "bad inode\n");
		ret = -EIO;
		goto bail;
	}

	osb = OCFS2_SB(inode->i_sb);

	ret = ocfs2_write_lock_maybe_extend(filp, buf, count, ppos, &info,
					    &ctxt);
	if (ret)
		goto bail;

	down_read(&OCFS2_I(inode)->ip_alloc_sem);

#ifdef OCFS2_ORACORE_WORKAROUNDS
	if (osb->s_mount_opt & OCFS2_MOUNT_COMPAT_OCFS) {
		unsigned int saved_flags = filp->f_flags;

		if (info.wl_do_direct_io)
			filp->f_flags |= O_DIRECT;
		else
			filp->f_flags &= ~O_DIRECT;

		ret = generic_file_write_nolock(filp, &local_iov, 1, ppos);

		filp->f_flags = saved_flags;
	} else
#endif
		ret = generic_file_write_nolock(filp, &local_iov, 1, ppos);

	up_read(&OCFS2_I(inode)->ip_alloc_sem);

bail:
	/* we might have to finish up extentions that were performed before
	 * an error was returned by, say, data locking */
	if (info.wl_extended)
		ocfs2_file_finish_extension(inode, info.wl_newsize,
					    info.wl_do_direct_io);
	if (info.wl_unlock_ctxt)
		ocfs2_unlock_buffer_inodes(&ctxt);
	if (info.wl_have_i_mutex)
		mutex_unlock(&inode->i_mutex);
	mlog_exit(ret);

	return ret;
}

static ssize_t ocfs2_file_read(struct file *filp,
			       char __user *buf,
			       size_t count,
			       loff_t *ppos)
{
	int ret = 0;
	struct ocfs2_super *osb = NULL;
	struct dentry *dentry = filp->f_dentry;
	struct inode *inode = dentry->d_inode;
	struct ocfs2_backing_inode *target_binode;
	DECLARE_BUFFER_LOCK_CTXT(ctxt);

	mlog_entry("(0x%p, 0x%p, %u, '%.*s')\n", filp, buf,
		   (unsigned int)count,
		   filp->f_dentry->d_name.len,
		   filp->f_dentry->d_name.name);

	if (!inode) {
		ret = -EINVAL;
		mlog_errno(ret);
		goto bail;
	}

	osb = OCFS2_SB(inode->i_sb);

#ifdef OCFS2_ORACORE_WORKAROUNDS
	if (osb->s_mount_opt & OCFS2_MOUNT_COMPAT_OCFS) {
		if (filp->f_flags & O_DIRECT) {
			int sector_size = 1 << osb->s_sectsize_bits;

			if (((*ppos) & (sector_size - 1)) ||
			    (count & (sector_size - 1)) ||
			    ((unsigned long)buf & (sector_size - 1)) ||
			    (i_size_read(inode) & (sector_size -1))) {
				filp->f_flags &= ~O_DIRECT;
			}
		}
	}
#endif

	ret = ocfs2_setup_io_locks(inode->i_sb, inode, buf, count, &ctxt,
				   &target_binode);
	if (ret < 0) {
		mlog_errno(ret);
		goto bail;
	}

	target_binode->ba_lock_data = (filp->f_flags & O_DIRECT) ? 0 : 1;

	ret = ocfs2_lock_buffer_inodes(&ctxt, NULL);
	if (ret < 0) {
		mlog_errno(ret);
		goto bail_unlock;
	}

	down_read(&OCFS2_I(inode)->ip_alloc_sem);

	ret = generic_file_read(filp, buf, count, ppos);

	up_read(&OCFS2_I(inode)->ip_alloc_sem);

	if (ret == -EINVAL)
		mlog(ML_ERROR, "Generic_file_read returned -EINVAL\n");

bail_unlock:
	ocfs2_unlock_buffer_inodes(&ctxt);

bail:
	mlog_exit(ret);

	return ret;
}

static ssize_t ocfs2_file_sendfile(struct file *in_file,
				   loff_t *ppos,
				   size_t count,
				   read_actor_t actor,
				   void *target)
{
	int ret;
	struct inode *inode = in_file->f_mapping->host;

	mlog_entry("inode %"MLFu64", ppos %lld, count = %u\n",
		   OCFS2_I(inode)->ip_blkno, (long long) *ppos,
		   (unsigned int) count);

	/* Obviously, there is no user buffer to worry about here --
	 * this simplifies locking, so no need to walk vmas a la
	 * read/write. We take a simple set of cluster locks against
	 * the inode and call generic_file_sendfile. */
	ret = ocfs2_meta_lock(inode, NULL, NULL, 0);
	if (ret < 0) {
		mlog_errno(ret);
		goto bail;
	}

	ret = ocfs2_data_lock(inode, 0);
	if (ret < 0) {
		mlog_errno(ret);
		goto bail_unlock_meta;
	}

	down_read(&OCFS2_I(inode)->ip_alloc_sem);

	ret = generic_file_sendfile(in_file, ppos, count, actor, target);
	if (ret < 0)
		mlog_errno(ret);

	up_read(&OCFS2_I(inode)->ip_alloc_sem);

	ocfs2_data_unlock(inode, 0);
bail_unlock_meta:
	ocfs2_meta_unlock(inode, 0);

bail:
	mlog_exit(ret);
	return ret;
}

struct file_operations ocfs2_fops = {
	.read		= ocfs2_file_read,
	.write		= ocfs2_file_write,
	.sendfile	= ocfs2_file_sendfile,
	.mmap		= ocfs2_mmap,
	.fsync		= ocfs2_sync_file,
	.release	= ocfs2_file_release,
	.open		= ocfs2_file_open,
	.aio_read	= ocfs2_file_aio_read,
	.aio_write	= ocfs2_file_aio_write,
};

struct file_operations ocfs2_dops = {
	.read		= generic_read_dir,
	.readdir	= ocfs2_readdir,
	.fsync		= ocfs2_sync_file,
};

int ocfs2_set_inode_size(struct ocfs2_journal_handle *handle,
			 struct inode *inode,
			 struct buffer_head *fe_bh,
			 u64 new_i_size)
{
	int status, grow;

	mlog_entry_void();

	grow = new_i_size > inode->i_size;
	i_size_write(inode, new_i_size);
	inode->i_blocks = ocfs2_align_bytes_to_sectors(new_i_size);
	inode->i_ctime = inode->i_mtime = CURRENT_TIME;

	status = ocfs2_mark_inode_dirty(handle, inode, fe_bh);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	/* FIXME: I think this should all be in the caller */
	spin_lock(&OCFS2_I(inode)->ip_lock);
	if (!grow)
		OCFS2_I(inode)->ip_mmu_private = i_size_read(inode);
	spin_unlock(&OCFS2_I(inode)->ip_lock);

bail:
	mlog_exit(status);
	return status;
}

static int ocfs2_orphan_for_truncate(struct ocfs2_super *osb,
				     struct inode *inode,
				     struct buffer_head *fe_bh,
				     u64 new_i_size)
{
	int status;
	struct ocfs2_journal_handle *handle;

	mlog_entry_void();

	/* TODO: This needs to actually orphan the inode in this
	 * transaction. */

	handle = ocfs2_start_trans(osb, NULL, OCFS2_INODE_UPDATE_CREDITS);
	if (IS_ERR(handle)) {
		status = PTR_ERR(handle);
		mlog_errno(status);
		goto out;
	}

	status = ocfs2_set_inode_size(handle, inode, fe_bh, new_i_size);
	if (status < 0)
		mlog_errno(status);

	ocfs2_commit_trans(handle);
out:
	mlog_exit(status);
	return status;
}

static int ocfs2_truncate_file(struct ocfs2_super *osb,
			       u64 new_i_size,
			       struct inode *inode)
{
	int status = 0;
	struct ocfs2_dinode *fe = NULL;
	struct buffer_head *fe_bh = NULL;
	struct ocfs2_journal_handle *handle = NULL;
	struct ocfs2_truncate_context *tc = NULL;

	mlog_entry("(inode = %"MLFu64", new_i_size = %"MLFu64"\n",
		   OCFS2_I(inode)->ip_blkno, new_i_size);

	truncate_inode_pages(inode->i_mapping, new_i_size);

	status = ocfs2_read_block(osb, OCFS2_I(inode)->ip_blkno, &fe_bh,
				  OCFS2_BH_CACHED, inode);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	fe = (struct ocfs2_dinode *) fe_bh->b_data;
	if (!OCFS2_IS_VALID_DINODE(fe)) {
		OCFS2_RO_ON_INVALID_DINODE(inode->i_sb, fe);
		status = -EIO;
		goto bail;
	}
	mlog_bug_on_msg(le64_to_cpu(fe->i_size) != i_size_read(inode),
			"Inode %"MLFu64", inode i_size = %lld != di "
			"i_size = %"MLFu64", i_flags = 0x%x\n",
			OCFS2_I(inode)->ip_blkno,
			i_size_read(inode),
			le64_to_cpu(fe->i_size), le32_to_cpu(fe->i_flags));

	if (new_i_size > le64_to_cpu(fe->i_size)) {
		mlog(0, "asked to truncate file with size (%"MLFu64") "
		     "to size (%"MLFu64")!\n",
		     le64_to_cpu(fe->i_size), new_i_size);
		status = -EINVAL;
		mlog_errno(status);
		goto bail;
	}

	mlog(0, "inode %"MLFu64", i_size = %"MLFu64", new_i_size = %"MLFu64"\n",
	     le64_to_cpu(fe->i_blkno), le64_to_cpu(fe->i_size), new_i_size);

	/* lets handle the simple truncate cases before doing any more
	 * cluster locking. */
	if (new_i_size == le64_to_cpu(fe->i_size))
		goto bail;

	/* This forces other nodes to sync and drop their pages. Do
	 * this even if we have a truncate without allocation change -
	 * ocfs2 cluster sizes can be much greater than page size, so
	 * we have to truncate them anyway.  */
	status = ocfs2_data_lock(inode, 1);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}
	ocfs2_data_unlock(inode, 1);

	if (le32_to_cpu(fe->i_clusters) ==
	    ocfs2_clusters_for_bytes(osb->sb, new_i_size)) {
		mlog(0, "fe->i_clusters = %u, so we do a simple truncate\n",
		     fe->i_clusters);
		/* No allocation change is required, so lets fast path
		 * this truncate. */
		handle = ocfs2_start_trans(osb, NULL,
					  OCFS2_INODE_UPDATE_CREDITS);
		if (IS_ERR(handle)) {
			status = PTR_ERR(handle);
			handle = NULL;
			mlog_errno(status);
			goto bail;
		}

		status = ocfs2_set_inode_size(handle, inode, fe_bh,
					      new_i_size);
		if (status < 0)
			mlog_errno(status);
		goto bail;
	}

	/* alright, we're going to need to do a full blown alloc size
	 * change. Orphan the inode so that recovery can complete the
	 * truncate if necessary. This does the task of marking
	 * i_size. */
	status = ocfs2_orphan_for_truncate(osb, inode, fe_bh, new_i_size);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	status = ocfs2_prepare_truncate(osb, inode, fe_bh, &tc);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	status = ocfs2_commit_truncate(osb, inode, fe_bh, tc);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	/* TODO: orphan dir cleanup here. */
bail:
	if (handle)
		ocfs2_commit_trans(handle);

	if (fe_bh)
		brelse(fe_bh);

	mlog_exit(status);
	return status;
}

static int ocfs2_zero_extend(struct inode *inode)
{
	struct address_space *mapping = inode->i_mapping;
	struct page *page;
	u64 size = i_size_read(inode) - 1;
	unsigned int offset;
	int res = 0;

	/* Start the zeroing of blocks */
	if (i_size_read(inode) > OCFS2_I(inode)->ip_mmu_private) {
		page = grab_cache_page(mapping,
				       size >> PAGE_CACHE_SHIFT);
		if (!page) {
			res = -ENOMEM;
			mlog_errno(res);
			return res;
		}
		offset = (unsigned int)(size & (PAGE_CACHE_SIZE - 1)) + 1;
		res = mapping->a_ops->prepare_write(NULL, page, offset,
						    offset);
		if (res < 0) {
			mlog_errno(res);
			goto bail_unlock;
		}

		res = mapping->a_ops->commit_write(NULL, page, offset, offset);
		if (res < 0)
			mlog_errno(res);

bail_unlock:
		unlock_page(page);
		page_cache_release(page);
		mark_inode_dirty(inode);
	}

	return res;
}

/*
 * extend allocation only here.
 * we'll update all the disk stuff, and oip->alloc_size
 *
 * expect stuff to be locked, a transaction started and enough data /
 * metadata reservations in the contexts. I'll return -EAGAIN, if we
 * run out of transaction credits, so the caller can restart us.
 */
int ocfs2_extend_allocation(struct ocfs2_super *osb,
			    struct inode *inode,
			    u32 clusters_to_add,
			    struct buffer_head *fe_bh,
			    struct ocfs2_journal_handle *handle,
			    struct ocfs2_alloc_context *data_ac,
			    struct ocfs2_alloc_context *meta_ac,
			    enum ocfs2_alloc_restarted *reason)
{
	int status = 0;
	int free_extents;
	struct ocfs2_dinode *fe = (struct ocfs2_dinode *) fe_bh->b_data;
	u32 bit_off, num_bits;
	u64 block;

	BUG_ON(!clusters_to_add);

	free_extents = ocfs2_num_free_extents(osb, inode, fe);
	if (free_extents < 0) {
		status = free_extents;
		mlog_errno(status);
		goto leave;
	}

	/* there are two cases which could cause us to EAGAIN in the
	 * we-need-more-metadata case:
	 * 1) we haven't reserved *any*
	 * 2) we are so fragmented, we've needed to add metadata too
	 *    many times. */
	if (!free_extents && !meta_ac) {
		mlog(0, "we haven't reserved any metadata!\n");
		status = -EAGAIN;
		if (reason)
			*reason = RESTART_META;
		goto leave;
	} else if ((!free_extents)
		   && (ocfs2_alloc_context_bits_left(meta_ac)
		       < ocfs2_extend_meta_needed(fe))) {
		mlog(0, "filesystem is really fragmented...\n");
		status = -EAGAIN;
		if (reason)
			*reason = RESTART_META;
		goto leave;
	}

	status = ocfs2_claim_clusters(osb, handle, data_ac, 1,
				      &bit_off, &num_bits);
	if (status < 0) {
		if (status != -ENOSPC)
			mlog_errno(status);
		goto leave;
	}

	BUG_ON(num_bits > clusters_to_add);

	/* reserve our write early -- insert_extent may update the inode */
	status = ocfs2_journal_access(handle, inode, fe_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (status < 0) {
		mlog_errno(status);
		goto leave;
	}

	block = ocfs2_clusters_to_blocks(osb->sb, bit_off);
	mlog(0, "Allocating %u clusters at block %u for inode %"MLFu64"\n",
	     num_bits, bit_off, OCFS2_I(inode)->ip_blkno);
	status = ocfs2_insert_extent(osb, handle, inode, fe_bh, block,
				     num_bits, meta_ac);
	if (status < 0) {
		mlog_errno(status);
		goto leave;
	}

	le32_add_cpu(&fe->i_clusters, num_bits);
	spin_lock(&OCFS2_I(inode)->ip_lock);
	OCFS2_I(inode)->ip_clusters = le32_to_cpu(fe->i_clusters);
	spin_unlock(&OCFS2_I(inode)->ip_lock);

	status = ocfs2_journal_dirty(handle, fe_bh);
	if (status < 0) {
		mlog_errno(status);
		goto leave;
	}

	clusters_to_add -= num_bits;

	if (clusters_to_add) {
		mlog(0, "need to alloc once more, clusters = %u, wanted = "
		     "%u\n", fe->i_clusters, clusters_to_add);
		status = -EAGAIN;
		if (reason)
			*reason = RESTART_TRANS;
	}

leave:
	mlog_exit(status);
	return status;
}

/*
 * Ok, this function is heavy on the goto's - we need to clean it up a
 * bit.
 *
 * *bytes_extended is a measure of how much was added to
 * dinode->i_size, NOT how much allocated was actually added to the
 * file. It will always be correct, even when we return an error.
 */
int ocfs2_extend_file(struct ocfs2_super *osb,
		      struct inode *inode,
		      u64 new_i_size,
		      u64 *bytes_extended)
{
	int status = 0;
	int restart_func = 0;
	int drop_alloc_sem = 0;
	int credits, num_free_extents;
	u32 clusters_to_add;
	u64 new_fe_size;
	struct buffer_head *bh = NULL;
	struct ocfs2_dinode *fe;
	struct ocfs2_journal_handle *handle = NULL;
	struct ocfs2_alloc_context *data_ac = NULL;
	struct ocfs2_alloc_context *meta_ac = NULL;
	enum ocfs2_alloc_restarted why;

	mlog_entry("(Inode %"MLFu64" new_i_size=%"MLFu64")\n",
		   OCFS2_I(inode)->ip_blkno, new_i_size);

	*bytes_extended = 0;

	/* setattr sometimes calls us like this. */
	if (new_i_size == 0)
		goto leave;

restart_all:
	handle = ocfs2_alloc_handle(osb);
	if (handle == NULL) {
		status = -ENOMEM;
		mlog_errno(status);
		goto leave;
	}

	status = ocfs2_read_block(osb, OCFS2_I(inode)->ip_blkno, &bh,
				  OCFS2_BH_CACHED, inode);
	if (status < 0) {
		mlog_errno(status);
		goto leave;
	}

	fe = (struct ocfs2_dinode *) bh->b_data;
	if (!OCFS2_IS_VALID_DINODE(fe)) {
		OCFS2_RO_ON_INVALID_DINODE(inode->i_sb, fe);
		status = -EIO;
		goto leave;
	}
	mlog_bug_on_msg(i_size_read(inode) !=
			(le64_to_cpu(fe->i_size) - *bytes_extended),
			"Inode %"MLFu64" i_size = %lld, dinode i_size "
			"= %"MLFu64", bytes_extended = %"MLFu64", new_i_size "
			"= %"MLFu64"\n", OCFS2_I(inode)->ip_blkno,
			i_size_read(inode), le64_to_cpu(fe->i_size),
			*bytes_extended, new_i_size);
	mlog_bug_on_msg(new_i_size < i_size_read(inode),
			"Inode %"MLFu64", i_size = %lld, new sz = %"MLFu64"\n",
			OCFS2_I(inode)->ip_blkno, i_size_read(inode),
			new_i_size);

	if (i_size_read(inode) == new_i_size)
  		goto leave;

	clusters_to_add = ocfs2_clusters_for_bytes(osb->sb, new_i_size) -
			  le32_to_cpu(fe->i_clusters);

	mlog(0, "extend inode %"MLFu64", new_i_size = %"MLFu64", "
		"i_size = %lld, fe->i_clusters = %u, clusters_to_add = %u\n",
	     OCFS2_I(inode)->ip_blkno, new_i_size, i_size_read(inode),
	     fe->i_clusters, clusters_to_add);

	if (!clusters_to_add)
		goto do_start_trans;

	num_free_extents = ocfs2_num_free_extents(osb,
						  inode,
						  fe);
	if (num_free_extents < 0) {
		status = num_free_extents;
		mlog_errno(status);
		goto leave;
	}

	if (!num_free_extents) {
		status = ocfs2_reserve_new_metadata(osb,
						    handle,
						    fe,
						    &meta_ac);
		if (status < 0) {
			if (status != -ENOSPC)
				mlog_errno(status);
			goto leave;
		}
	}

	status = ocfs2_reserve_clusters(osb,
					handle,
					clusters_to_add,
					&data_ac);
	if (status < 0) {
		if (status != -ENOSPC)
			mlog_errno(status);
		goto leave;
	}

	/* blocks peope in read/write from reading our allocation
	 * until we're done changing it. We depend on i_mutex to block
	 * other extend/truncate calls while we're here. Ordering wrt
	 * start_trans is important here -- always do it before! */
	down_write(&OCFS2_I(inode)->ip_alloc_sem);
	drop_alloc_sem = 1;
do_start_trans:
	credits = ocfs2_calc_extend_credits(osb->sb, fe, clusters_to_add);
	handle = ocfs2_start_trans(osb, handle, credits);
	if (IS_ERR(handle)) {
		status = PTR_ERR(handle);
		handle = NULL;
		mlog_errno(status);
		goto leave;
	}

restarted_transaction:
	/* reserve a write to the file entry early on - that we if we
	 * run out of credits in the allocation path, we can still
	 * update i_size. */
	status = ocfs2_journal_access(handle, inode, bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (status < 0) {
		mlog_errno(status);
		goto leave;
	}

	if (!clusters_to_add)
		goto no_alloc;

	status = ocfs2_extend_allocation(osb,
					 inode,
					 clusters_to_add,
					 bh,
					 handle,
					 data_ac,
					 meta_ac,
					 &why);
	if ((status < 0) && (status != -EAGAIN)) {
		if (status != -ENOSPC)
			mlog_errno(status);
		goto leave;
	}

	if (status == -EAGAIN && (new_i_size >
	    ocfs2_clusters_to_bytes(osb->sb, le32_to_cpu(fe->i_clusters)))) {

		if (why == RESTART_META) {
			mlog(0, "Inode %"MLFu64" restarting function.\n",
			     OCFS2_I(inode)->ip_blkno);
			restart_func = 1;
		} else {
			BUG_ON(why != RESTART_TRANS);

			new_fe_size = ocfs2_clusters_to_bytes(osb->sb,
						le32_to_cpu(fe->i_clusters));
			*bytes_extended += new_fe_size -
					   le64_to_cpu(fe->i_size);
			/* update i_size in case we crash after the
			 * extend_trans */
			fe->i_size = cpu_to_le64(new_fe_size);

			fe->i_mtime = cpu_to_le64(CURRENT_TIME.tv_sec);
			fe->i_mtime_nsec = cpu_to_le32(CURRENT_TIME.tv_nsec);

			status = ocfs2_journal_dirty(handle, bh);
			if (status < 0) {
				mlog_errno(status);
				goto leave;
			}

			clusters_to_add =
				ocfs2_clusters_for_bytes(osb->sb,
							 new_i_size)
				- le32_to_cpu(fe->i_clusters);
			mlog(0, "Inode %"MLFu64" restarting transaction.\n",
			     OCFS2_I(inode)->ip_blkno);
			/* TODO: This can be more intelligent. */
			credits = ocfs2_calc_extend_credits(osb->sb,
							    fe,
							    clusters_to_add);
			status = ocfs2_extend_trans(handle, credits);
			if (status < 0) {
				/* handle still has to be committed at
				 * this point. */
				status = -ENOMEM;
				mlog_errno(status);
				goto leave;
			}
			goto restarted_transaction;
		}
	}
	status = 0;

no_alloc:
	/* this may not be the end of our allocation so only update
	 * i_size to what's appropriate. */
	new_fe_size = ocfs2_clusters_to_bytes(osb->sb,
					      le32_to_cpu(fe->i_clusters));
	if (new_i_size < new_fe_size)
		new_fe_size = new_i_size;

	*bytes_extended += new_fe_size - le64_to_cpu(fe->i_size);
	fe->i_size = cpu_to_le64(new_fe_size);

	mlog(0, "fe: i_clusters = %u, i_size=%"MLFu64"\n",
	     le32_to_cpu(fe->i_clusters), le64_to_cpu(fe->i_size));

	mlog(0, "inode: ip_clusters=%u, i_size=%lld\n",
	     OCFS2_I(inode)->ip_clusters, i_size_read(inode));

	fe->i_ctime = fe->i_mtime = cpu_to_le64(CURRENT_TIME.tv_sec);
	fe->i_ctime_nsec = fe->i_mtime_nsec = cpu_to_le32(CURRENT_TIME.tv_nsec);

	status = ocfs2_journal_dirty(handle, bh);
	if (status < 0) {
		mlog_errno(status);
		goto leave;
	}

leave:
	if (drop_alloc_sem) {
		up_write(&OCFS2_I(inode)->ip_alloc_sem);
		drop_alloc_sem = 0;
	}
	if (handle) {
		ocfs2_commit_trans(handle);
		handle = NULL;
	}
	if (data_ac) {
		ocfs2_free_alloc_context(data_ac);
		data_ac = NULL;
	}
	if (meta_ac) {
		ocfs2_free_alloc_context(meta_ac);
		meta_ac = NULL;
	}
	if (bh) {
		brelse(bh);
		bh = NULL;
	}
	if ((!status) && restart_func) {
		restart_func = 0;
		goto restart_all;
	}

	mlog_exit(status);
	return status;
}

int ocfs2_setattr(struct dentry *dentry, struct iattr *attr)
{
	int status = 0;
	u64 newsize, bytes_added;
	struct inode *inode = dentry->d_inode;
	struct super_block *sb = inode->i_sb;
	struct ocfs2_super *osb = OCFS2_SB(sb);
	struct buffer_head *bh = NULL;
	struct ocfs2_journal_handle *handle = NULL;

	mlog_entry("(0x%p, '%.*s', inode %"MLFu64")\n", dentry,
	           dentry->d_name.len, dentry->d_name.name,
		   OCFS2_I(inode)->ip_blkno);

	if (attr->ia_valid & ATTR_MODE)
		mlog(0, "mode change: %d\n", attr->ia_mode);
	if (attr->ia_valid & ATTR_UID)
		mlog(0, "uid change: %d\n", attr->ia_uid);
	if (attr->ia_valid & ATTR_GID)
		mlog(0, "gid change: %d\n", attr->ia_gid);
	if (attr->ia_valid & ATTR_SIZE)
		mlog(0, "size change...\n");
	if (attr->ia_valid & (ATTR_ATIME | ATTR_MTIME | ATTR_CTIME))
		mlog(0, "time change...\n");

#define OCFS2_VALID_ATTRS (ATTR_ATIME | ATTR_MTIME | ATTR_CTIME | ATTR_SIZE \
			   | ATTR_GID | ATTR_UID | ATTR_MODE)
	if (!(attr->ia_valid & OCFS2_VALID_ATTRS)) {
		mlog(0, "can't handle attrs: 0x%x\n", attr->ia_valid);
		return 0;
	}

	status = inode_change_ok(inode, attr);
	if (status)
		return status;

	newsize = attr->ia_size;

	status = ocfs2_meta_lock(inode, NULL, &bh, 1);
	if (status < 0) {
		if (status != -ENOENT)
			mlog_errno(status);
		goto bail;
	}

	if (S_ISREG(inode->i_mode) &&
	    attr->ia_valid & ATTR_SIZE &&
	    newsize != i_size_read(inode)) {
		bytes_added = 0;

		if (i_size_read(inode) > newsize)
			status = ocfs2_truncate_file(osb, newsize, inode);
		else
			status = ocfs2_extend_file(osb, inode, newsize,
						   &bytes_added);
		if (status < 0 && (!bytes_added)) {
			if (status != -ENOSPC)
				mlog_errno(status);
			status = -ENOSPC;
			goto bail_unlock;
		}

		/* partial extend, we continue with what we've got. */
		if (status < 0
		    && status != -ENOSPC
		    && status != -EINTR
		    && status != -ERESTARTSYS)
			mlog(ML_ERROR,
			     "status return of %d extending inode "
			     "%"MLFu64"\n", status,
			     OCFS2_I(inode)->ip_blkno);
		status = 0;

		newsize = bytes_added + i_size_read(inode);
		if (bytes_added)
			ocfs2_update_inode_size(inode, newsize);

#ifdef OCFS2_ORACORE_WORKAROUNDS
		spin_lock(&OCFS2_I(inode)->ip_lock);
		if (OCFS2_I(inode)->ip_flags & OCFS2_INODE_OPEN_DIRECT) {
			/* This is a total broken hack for O_DIRECT crack */
			OCFS2_I(inode)->ip_mmu_private = i_size_read(inode);
		}
		spin_unlock(&OCFS2_I(inode)->ip_lock);
#endif
		status = ocfs2_zero_extend(inode);
		if (status < 0) {
			mlog_errno(status);
			goto bail_unlock;
		}
	}

	handle = ocfs2_start_trans(osb, NULL, OCFS2_INODE_UPDATE_CREDITS);
	if (IS_ERR(handle)) {
		status = PTR_ERR(handle);
		mlog_errno(status);
		goto bail_unlock;
	}

	status = inode_setattr(inode, attr);
	if (status < 0) {
		mlog_errno(status);
		goto bail_commit;
	}

	status = ocfs2_mark_inode_dirty(handle, inode, bh);
	if (status < 0)
		mlog_errno(status);

bail_commit:
	ocfs2_commit_trans(handle);
bail_unlock:
	ocfs2_meta_unlock(inode, 1);
bail:
	if (bh)
		brelse(bh);

	mlog_exit(status);
	return status;
}

int ocfs2_getattr(struct vfsmount *mnt,
		  struct dentry *dentry,
		  struct kstat *stat)
{
	struct inode *inode = dentry->d_inode;
	struct super_block *sb = dentry->d_inode->i_sb;
	struct ocfs2_super *osb = sb->s_fs_info;
	int err;

	mlog_entry_void();

	err = ocfs2_inode_revalidate(dentry);
	if (err) {
		if (err != -ENOENT)
			mlog_errno(err);
		goto bail;
	}

	generic_fillattr(inode, stat);

	/* We set the blksize from the cluster size for performance */
	stat->blksize = osb->s_clustersize;

bail:
	mlog_exit(err);

	return err;
}

struct inode_operations ocfs2_file_iops = {
	.setattr	= ocfs2_setattr,
	.getattr	= ocfs2_getattr,
};

struct inode_operations ocfs2_special_file_iops = {
	.setattr	= ocfs2_setattr,
	.getattr	= ocfs2_getattr,
};
