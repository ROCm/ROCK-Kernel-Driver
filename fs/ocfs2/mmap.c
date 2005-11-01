/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * mmap.c
 *
 * Code to deal with the mess that is clustered mmap.
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
#include <linux/signal.h>
#include <linux/rbtree.h>

#define MLOG_MASK_PREFIX ML_FILE_IO
#include <cluster/masklog.h>

#include "ocfs2.h"

#include "dlmglue.h"
#include "file.h"
#include "inode.h"
#include "mmap.h"

static inline u64 ocfs2_binode_blkno(struct ocfs2_backing_inode *binode);
static inline struct rb_node * __ocfs2_buffer_lock_ctxt_root(
	struct ocfs2_buffer_lock_ctxt *ctxt);
static int ocfs2_buffer_lock_ctxt_insert(struct ocfs2_buffer_lock_ctxt *ctxt,
					 struct inode *inode,
					 struct ocfs2_backing_inode **binode_ret);
static int ocfs2_fill_ctxt_from_buf(struct super_block *sb,
				    struct inode *target_inode,
				    char __user *buf,
				    size_t size,
				    struct ocfs2_buffer_lock_ctxt *ctxt);

static struct page *ocfs2_nopage(struct vm_area_struct * area,
				 unsigned long address,
				 int *type)
{
	int status, tmpstat, locked;
	struct inode *inode = area->vm_file->f_dentry->d_inode;
	struct page *page;
	sigset_t blocked, oldset;
	DECLARE_IO_MARKER(io_marker);

	mlog_entry("(inode %lu, address %lu)\n", inode->i_ino,
		   address);

	locked = ocfs2_is_in_io_marker_list(inode, current);

	if (!locked) {
		/* For lack of a better error... Unfortunately returns
		 * from nopage aren't very expressive right now. */
		page = NOPAGE_SIGBUS;

		/* The best way to deal with signals in this path is
		 * to block them upfront, rather than allowing the
		 * locking paths to return -ERESTARTSYS. */
		sigfillset(&blocked);

		/* We should technically never get a bad status return
		 * from sigprocmask */
		status = sigprocmask(SIG_BLOCK, &blocked, &oldset);
		if (status < 0) {
			mlog_errno(status);
			goto bail;
		}

		/* Since we don't allow shared writable, we need only
		 * worry about read locking here. */
		status = ocfs2_meta_lock(inode, NULL, NULL, 0);
		if (status < 0) {
			mlog_errno(status);

			if (status == -ENOMEM)
				page = NOPAGE_OOM;
			goto bail_setmask;
		}

		status = ocfs2_data_lock(inode, 0);
		if (status < 0) {
			mlog_errno(status);

			if (status == -ENOMEM)
				page = NOPAGE_OOM;
			goto bail_unlock;
		}

		tmpstat = sigprocmask(SIG_SETMASK, &oldset, NULL);
		if (tmpstat < 0)
			mlog_errno(tmpstat);

		/* I'm not sure if we can somehow recurse back into
		 * nopage or not, but this doesn't cost us anything,
		 * so lets do it for now. */
		ocfs2_add_io_marker(inode, &io_marker);
	}

	page = filemap_nopage(area, address, type);

	if (!locked) {
		ocfs2_del_io_marker(inode, &io_marker);
		ocfs2_data_unlock(inode, 0);
		ocfs2_meta_unlock(inode, 0);
	}
bail:
	mlog_exit_ptr(page);
	return page;

bail_unlock:
	ocfs2_meta_unlock(inode, 0);

bail_setmask:
	tmpstat = sigprocmask(SIG_SETMASK, &oldset, NULL);
	if (tmpstat < 0)
		mlog_errno(tmpstat);

	mlog_exit_ptr(page);
	return page;
}

static struct vm_operations_struct ocfs2_file_vm_ops = {
	.nopage = ocfs2_nopage,
};

int ocfs2_mmap(struct file *file,
	       struct vm_area_struct *vma)
{
	struct address_space *mapping = file->f_dentry->d_inode->i_mapping;
	struct inode *inode = mapping->host;

	/* We don't want to support shared writable mappings yet. */
	if (((vma->vm_flags & VM_SHARED) || (vma->vm_flags & VM_MAYSHARE))
	    && ((vma->vm_flags & VM_WRITE) || (vma->vm_flags & VM_MAYWRITE))) {
		mlog(0, "disallow shared writable mmaps %lx\n", vma->vm_flags);
		/* This is -EINVAL because generic_file_readonly_mmap
		 * returns it in a similar situation. */
		return -EINVAL;
	}

	update_atime(inode);
	vma->vm_ops = &ocfs2_file_vm_ops;
	return 0;
}

static inline u64 ocfs2_binode_blkno(struct ocfs2_backing_inode *binode)
{
	struct inode *inode = binode->ba_inode;

	BUG_ON(!inode);

	return OCFS2_I(inode)->ip_blkno;
}

static inline struct rb_node * __ocfs2_buffer_lock_ctxt_root(
	struct ocfs2_buffer_lock_ctxt *ctxt)
{
	return ctxt->b_inodes.rb_node;
}

static int ocfs2_buffer_lock_ctxt_insert(struct ocfs2_buffer_lock_ctxt *ctxt,
					 struct inode *inode,
					 struct ocfs2_backing_inode **binode_ret)
{
	u64 blkno;
	struct ocfs2_backing_inode *tmp, *binode;
	struct rb_node * parent = NULL;
	struct rb_node ** p = &ctxt->b_inodes.rb_node;

	BUG_ON(!ctxt);
	BUG_ON(!inode);

	blkno = OCFS2_I(inode)->ip_blkno;

	while(*p) {
		parent = *p;
		tmp = rb_entry(parent, struct ocfs2_backing_inode, ba_node);

		if (blkno < ocfs2_binode_blkno(tmp))
			p = &(*p)->rb_left;
		else if (blkno > ocfs2_binode_blkno(tmp))
			p = &(*p)->rb_right;
		else
			return 0; /* Don't insert duplicates */
	}

	binode = kcalloc(1, sizeof(struct ocfs2_backing_inode), GFP_KERNEL);
	if (!binode)
		return -ENOMEM;
	binode->ba_inode = inode;
	ocfs2_init_io_marker(&binode->ba_task);

	if (binode_ret)
		*binode_ret = binode;

	rb_link_node(&binode->ba_node, parent, p);
	rb_insert_color(&binode->ba_node, &ctxt->b_inodes);

	return 0;
}

static int ocfs2_fill_ctxt_from_buf(struct super_block *sb,
				    struct inode *target_inode,
				    char __user *buf,
				    size_t size,
				    struct ocfs2_buffer_lock_ctxt *ctxt)
{
	int status;
	unsigned long start = (unsigned long)buf;
	unsigned long end = start + size;
	struct inode *inode;
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;

	for (vma = find_vma(mm, start); vma; vma = vma->vm_next) {
		if (end <= vma->vm_start)
			break;
		if (vma->vm_ops == &ocfs2_file_vm_ops) {
			if (!vma->vm_file)
				continue;
			inode = vma->vm_file->f_dentry->d_inode;
			if (inode->i_sb == sb &&
			    inode != target_inode) {
				status = ocfs2_buffer_lock_ctxt_insert(ctxt,
								       inode,
								       NULL);
				if (status < 0)
					goto bail;
			}
		}
	}
	status = 0;
bail:
	return status;
}

int ocfs2_setup_io_locks(struct super_block *sb,
			 struct inode *target_inode,
			 char __user *buf,
			 size_t size,
			 struct ocfs2_buffer_lock_ctxt *ctxt,
			 struct ocfs2_backing_inode **target_binode)
{
	struct mm_struct *mm = current->mm;
	int skip_sem = (current->flags & PF_DUMPCORE) || !mm;
	int status;

	if (!skip_sem)
		down_read(&mm->mmap_sem);

	BUG_ON(__ocfs2_buffer_lock_ctxt_root(ctxt));

	/* We always insert target because it might not be backing part of the
	 * buffer - but it needs to be in there so that it's lock gets ordered
	 * with everything else */
	status = ocfs2_buffer_lock_ctxt_insert(ctxt, target_inode,
					       target_binode);

	/* knfsd, which lacks an mm, may call us to do I/O. Since the buffer
	 * is private to the kernel, there isn't any need to insert any other
	 * locks, so we can skip it.
	 *
	 * The pile of duct tape and mixed nuts that is NFS 1, universe 0
	 */
	if (!status && mm) {
		/* Now fill the tree with any inodes that back this
		 * buffer. If target inode is in there, it will be
		 * skipped over. */
		status = ocfs2_fill_ctxt_from_buf(sb, target_inode, buf, size,
						  ctxt);
	}

	if (!skip_sem)
		up_read(&mm->mmap_sem);

	if (status < 0) {
		mlog_errno(status);
		ocfs2_unlock_buffer_inodes(ctxt);
		goto bail;
	}

	status = 0;
bail:
	return status;
}

/* starting from pos, which can be null for the first call, give the
 * next buffer that needs unlocking.  we return null when there are none
 * left or we see last_inode */
static struct ocfs2_backing_inode *
ocfs2_next_unlocked(struct ocfs2_buffer_lock_ctxt *ctxt,
		    struct inode *last_inode,
		    struct ocfs2_backing_inode *pos)
{
	struct ocfs2_backing_inode *binode = NULL;
	struct rb_node *node = NULL;

	if (pos == NULL) {
		if (ctxt->b_next_unlocked)
			binode = ctxt->b_next_unlocked;
		else
			node = rb_first(&ctxt->b_inodes);
	} else
		node = rb_next(&pos->ba_node);

	if (node)
		binode = rb_entry(node, struct ocfs2_backing_inode, ba_node);

	if (binode && last_inode && binode->ba_inode == last_inode)
		binode = NULL;

	/* this is just an optimization to skip nodes in the tree
	 * that we've already seen.  If we're moving from one we've locked
	 * to one we haven't then we mark this node in the ctxt so that
	 * we'll return to it in a future after, say, hitting last_inode
	 * or EIOCBRETRY in lock_buffer_inodes */
	if (pos && pos->ba_locked && binode)
		ctxt->b_next_unlocked = binode;

	return binode;
}

/* Will take locks on all inodes in the ctxt up until 'last_inode'. If
 * last_inode is NULL, then we take locks on everything. We mark lock
 * status on the context so we skip any that have already been
 * locked. On error we will completely abort the context. */
/* WARNING: If you get a failure case here, you *must* call
 * "ocfs2_unlock_buffer_inodes" as we may have left a few inodes under
 * cluster lock. */
int ocfs2_lock_buffer_inodes(struct ocfs2_buffer_lock_ctxt *ctxt,
			     struct inode *last_inode)
{
	int status, data_level;
	struct ocfs2_backing_inode *binode = NULL;
	struct inode *inode;

	while((binode = ocfs2_next_unlocked(ctxt, last_inode, binode))) {
		/* the tricksy caller might have locked inodes themselves
		 * between calls. */
		if (binode->ba_locked)
			continue;
		inode = binode->ba_inode;

		if (!binode->ba_meta_locked) {
			status = ocfs2_meta_lock_full(inode, NULL, NULL,
						      binode->ba_lock_meta_level,
						      0, ctxt->b_cb,
						      ctxt->b_cb_data);

			if (status < 0) {
				if (status != -EIOCBRETRY)
					mlog_errno(status);
				goto bail;
			}

			binode->ba_meta_locked = 1;
		}

		/* ba_lock_data isn't set for direct io */
		if (binode->ba_lock_data) {
			data_level = binode->ba_lock_data_level;
			status = ocfs2_data_lock(inode, data_level);
			if (status < 0) {
				if (status == -EIOCBRETRY)
					goto bail;

				/* clean up the metadata lock that we took
				 * above
				 */
				ocfs2_meta_unlock(inode,
						  binode->ba_lock_meta_level);
				binode->ba_meta_locked = 0;

				mlog_errno(status);
				goto bail;
			}
		}
		ocfs2_add_io_marker(inode, &binode->ba_task);
		binode->ba_locked = 1;
	}

	status = 0;
bail:
	return status;
}

void ocfs2_unlock_buffer_inodes(struct ocfs2_buffer_lock_ctxt *ctxt)
{
	struct ocfs2_backing_inode *binode;
	struct rb_node *node;

	/* dlm locks don't mask ints.. this should be lower down */
	BUG_ON(in_interrupt());

	/* unlock in reverse order to minimize waking forward lockers */
	while ((node = rb_last(&ctxt->b_inodes)) != NULL) {
		binode = rb_entry(node, struct ocfs2_backing_inode, ba_node);

		ocfs2_del_io_marker(binode->ba_inode, &binode->ba_task);

		if (binode->ba_locked && binode->ba_lock_data)
			ocfs2_data_unlock(binode->ba_inode,
					  binode->ba_lock_data_level);

		if (binode->ba_locked || binode->ba_meta_locked)
			ocfs2_meta_unlock(binode->ba_inode,
					  binode->ba_lock_meta_level);

		rb_erase(node, &ctxt->b_inodes);
		kfree(binode);
	}

	ctxt->b_next_unlocked = NULL;
}

/*
 * This builds up the locking state that will be used by a write.  both normal
 * file writes and AIO writes come in through here.  This function does no
 * teardown on its own.  The caller must examine the info struct to see if it
 * needs to release locks or i_sem, etc.  This function is also restartable in
 * that it can return EIOCBRETRY if it would have blocked in the dlm.  It
 * stores its partial progress in the info struct so the caller can call back
 * in when it thinks the dlm won't block any more.  Thus, the caller must zero
 * the info struct before calling in the first time.
 */
ssize_t ocfs2_write_lock_maybe_extend(struct file *filp,
				      const char __user *buf,
				      size_t count,
				      loff_t *ppos,
				      struct ocfs2_write_lock_info *info,
				      struct ocfs2_buffer_lock_ctxt *ctxt)
{
	int ret = 0;
	ocfs2_super *osb = NULL;
	struct dentry *dentry = filp->f_dentry;
	struct inode *inode = dentry->d_inode;
	int status;
	int level = filp->f_flags & O_APPEND;
	loff_t saved_ppos;
	u64 bytes_added = 0;

	osb = OCFS2_SB(inode->i_sb);

	/* the target inode is different from the other inodes.  in o_direct it
	 * doesn't get a data lock and when appending it gets a level 1 meta
	 * lock.  we use target_binode to set its flags accordingly */
	if (info->wl_target_binode == NULL) {
		ret = ocfs2_setup_io_locks(inode->i_sb, inode,
					   (char __user *) buf,
					   count, ctxt,
					   &info->wl_target_binode);
		if (ret < 0) {
			BUG_ON(ret == -EIOCBRETRY);
			mlog_errno(ret);
			goto bail;
		}
	}

	/* This will lock everyone in the context who's order puts
	 * them before us. */
	if (!info->wl_have_before) {
		info->wl_unlock_ctxt = 1;
		ret = ocfs2_lock_buffer_inodes(ctxt, inode);
		if (ret < 0) {
			if (ret != -EIOCBRETRY)
				mlog_errno(ret);
			goto bail;
		}
		info->wl_have_before = 1;
		/* we're writing so get an ex data cluster lock */
		info->wl_target_binode->ba_lock_data_level = 1;
	}

	if (!info->wl_have_i_sem) {
		down(&inode->i_sem);
		info->wl_have_i_sem = 1;
	}

lock:
	if (!info->wl_have_target_meta) {
		status = ocfs2_meta_lock(inode, NULL, NULL, level);
		if (status < 0) {
			mlog_errno(status);
			ret = status;
			goto bail;
		}
		info->wl_have_target_meta = 1;
	}
	/* to handle extending writes, we do a bit of our own locking
	 * here, but we setup the ctxt do unlock for us (as well as
	 * handle locking everything else. */
	if (level)
		info->wl_target_binode->ba_lock_meta_level = 1;

	/* work on a copy of ppos until we're sure that we won't have
	 * to recalculate it due to relocking. */
	saved_ppos = *ppos;

	if (filp->f_flags & O_APPEND) {
		saved_ppos = i_size_read(inode);
		mlog(0, "O_APPEND: inode->i_size=%llu\n", saved_ppos);

#ifdef OCFS2_ORACORE_WORKAROUNDS
		if (osb->s_mount_opt & OCFS2_MOUNT_COMPAT_OCFS) {
			/* ugh, work around some applications which open
			 * everything O_DIRECT + O_APPEND and really don't
			 * mean to use O_DIRECT. */
			filp->f_flags &= ~O_DIRECT;
		}
#endif
	}

	if (filp->f_flags & O_DIRECT) {
#ifdef OCFS2_ORACORE_WORKAROUNDS
		if (osb->s_mount_opt & OCFS2_MOUNT_COMPAT_OCFS) {
			int sector_size = 1 << osb->s_sectsize_bits;

			if ((saved_ppos & (sector_size - 1)) ||
			    (count & (sector_size - 1)) ||
			    ((unsigned long)buf & (sector_size - 1))) {
				info->wl_do_direct_io = 0;
				filp->f_flags |= O_SYNC;
			} else {
				info->wl_do_direct_io = 1;
			}
		} else
#endif
			info->wl_do_direct_io = 1;

		mlog(0, "O_DIRECT\n");
	}

	info->wl_target_binode->ba_lock_data = info->wl_do_direct_io ? 0 : 1;

	info->wl_newsize = count + saved_ppos;
	if (filp->f_flags & O_APPEND)
		info->wl_newsize = count + i_size_read(inode);

	mlog(0, "ppos=%lld newsize=%"MLFu64" cursize=%lld\n", saved_ppos,
	     info->wl_newsize, i_size_read(inode));

	if (info->wl_newsize > i_size_read(inode)) {
		if (!level) {
			/* we want an extend, but need a higher
			 * level cluster lock. */
			mlog(0, "inode %"MLFu64", had a PR, looping back "
			     "for EX\n", OCFS2_I(inode)->ip_blkno);
			ocfs2_meta_unlock(inode, level);
			info->wl_have_target_meta = 0;
			level = 1;
			goto lock;
		}

		mlog(0, "Writing at EOF, will need more allocation: "
		     "i_size=%lld, need=%"MLFu64"\n", i_size_read(inode),
		     info->wl_newsize);

		/* If we extend AT ALL here then we update our state
		 * and continue the write call, regardless of error --
		 * this is basically a short write. */
		status = ocfs2_extend_file(osb, inode, info->wl_newsize,
					   &bytes_added);
		if (status < 0 && (!bytes_added)) {
			if (status != -ERESTARTSYS
			    && status != -EINTR
			    && status != -ENOSPC) {
				mlog_errno(status);
				mlog(ML_ERROR, "Failed to extend inode %"MLFu64
				     " from %lld to %"MLFu64,
				     OCFS2_I(inode)->ip_blkno,
				     *ppos, info->wl_newsize);
			}
			ret = status;

			info->wl_have_target_meta = 0;
			ocfs2_meta_unlock(inode, level);
			goto bail;
		}

		info->wl_extended = 1;

		/* We need to recalulate newsize and count according
		 * to what extend could give us. If we got the whole
		 * extend then this doesn't wind up changing the
		 * values. */
		info->wl_newsize = i_size_read(inode) + bytes_added;
		count = info->wl_newsize - saved_ppos;

		if (status < 0
		    && status != -ENOSPC
		    && status != -EINTR
		    && status != -ERESTARTSYS)
			mlog(ML_ERROR, "status return of %d extending inode "
			     "%"MLFu64"\n", status,
			     OCFS2_I(inode)->ip_blkno);
		status = 0;
	}

	/* we've got whatever cluster lock is appropriate now, so we
	 * can stuff *ppos back. */
	*ppos = saved_ppos;

	if (!info->wl_do_direct_io && !info->wl_have_data_lock) {
		status = ocfs2_data_lock(inode, 1);
		if (status < 0) {
			mlog_errno(status);
			ret = status;

			info->wl_have_target_meta = 0;
			ocfs2_meta_unlock(inode, level);
			goto bail;
		}
		info->wl_have_data_lock = 1;
	}

	/* Alright, fool the io locking stuff into thinking it's
	 * handled our inode for us. We can now count on it to do the
	 * unlock for us. */
	info->wl_target_binode->ba_locked = 1;

	/* This will lock everyone who's order puts them *after* our inode. */
	ret = ocfs2_lock_buffer_inodes(ctxt, NULL);
	if (ret < 0) {
		if (ret != -EIOCBRETRY)
			mlog_errno(ret);
		goto bail;
	}

bail:
	mlog_exit(ret);
	return ret;
}

#if 0
static void ocfs2_buffer_ctxt_debug(struct ocfs2_buffer_lock_ctxt *ctxt)
{
	struct ocfs2_backing_inode *binode;
	struct inode *inode;
	struct rb_node *node;

	printk("(%u) ocfs2: buffer lock ctxt: direct io = %d\n",
	       current->pid, ctxt->b_lock_direct);

	node = rb_first(&ctxt->b_inodes);
	while (node) {
		binode = rb_entry(node, struct ocfs2_backing_inode, ba_node);
		inode = binode->ba_inode;

		printk("(%u) ocfs2: inode %llu, locked %d, is target? %s\n",
		       current->pid, OCFS2_I(inode)->ip_blkno,
		       binode->ba_locked,
		       ocfs2_buffer_lock_is_target(ctxt, inode) ? "yes" :
		       "no");

		node = rb_next(node);
	}
}
#endif
