/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * aio.c
 *
 * aio read and write
 *
 * Copyright (C) 2002, 2004, 2005 Oracle.  All rights reserved.
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

#define MLOG_MASK_PREFIX ML_FILE_IO|ML_AIO
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
#include "mmap.h"
#include "suballoc.h"


struct ocfs2_kiocb_private {
	struct ocfs2_kiocb_private	*kp_teardown_next;
	ocfs2_super			*kp_osb;
	unsigned			kp_have_alloc_sem:1,
					kp_have_write_locks:1;
	struct inode			*kp_inode;
	struct ocfs2_buffer_lock_ctxt	kp_ctxt;
	struct ocfs2_write_lock_info	kp_info;
};

static void okp_teardown(struct ocfs2_kiocb_private *okp)
{
	mlog(0, "okp %p\n", okp);

	BUG_ON(okp->kp_inode == NULL);

	if (okp->kp_info.wl_unlock_ctxt)
		ocfs2_unlock_buffer_inodes(&okp->kp_ctxt);
	if (okp->kp_have_alloc_sem)
		up_read(&OCFS2_I(okp->kp_inode)->ip_alloc_sem);

	iput(okp->kp_inode);
	kfree(okp);
}

void okp_teardown_from_list(void *data)
{
	ocfs2_super *osb = data;
	struct ocfs2_kiocb_private *okp, *next;

	for (okp = xchg(&osb->osb_okp_teardown_next, NULL); okp != NULL;
	     okp = next) {

		next = okp->kp_teardown_next;
		okp_teardown(okp);
	}
}

/*
 * This releases the dlm locks we held across an aio operation and frees the
 * space we were tracking them in.
 *
 * While aio operations are in flight they have a vfsmnt reference for the file
 * which prevents unmount.  This dtor gets called *after* that ref is dropped,
 * however, so we have to make sure to account for pending work we have here in
 * the unmount path.  The race starts when aio does its fputs, before it calls
 * dtor which queues work, so just synchronizing with the work queue could miss
 * that first phase.  So unmount first waits for the pending count to drop.
 * Then it has to wait for keventd to finish the work freeing the okps.
 *
 * _dtor can be called from just about any context and lock teardown is
 * anything but interrupt safe.  We used to hand the okps to
 * okp_teardown_from_list with a normal list_head and irq masking lock but we
 * want to avoid masking interrupts so it was shifted to the {cmp,}xchg() and
 * atomic_t.
 *
 * Adding to the singly linked ->next list is only a little tricky.  We have to
 * watch for races between sampling the head to assign ->next in the inserting
 * okp and a new head being written before we point the head to the inserting
 * okp.
 */
static void ocfs2_ki_dtor(struct kiocb *iocb)
{
	struct ocfs2_kiocb_private *next, *okp = iocb->private;
	ocfs2_super *osb = okp->kp_osb;

	mlog(0, "iocb %p okp %p\n", iocb, okp);

	/* okp_alloc only assigns the iocb->private and ->ki_dtor pointers if
	 * it was able to alloc the okp and get an inode reference */
	BUG_ON(okp == NULL);
	BUG_ON(okp->kp_inode == NULL);

	/* we had better not try to work with this iocb again */
	iocb->private = NULL;

	 /* once this cmpxchg succeeds the okp can be freed so we have to be
	  * careful not to deref it when testing success */
	do {
		next = osb->osb_okp_teardown_next;
		okp->kp_teardown_next = next;
	} while (cmpxchg(&osb->osb_okp_teardown_next, next, okp) != next);

	schedule_work(&osb->osb_okp_teardown_work);

	if (atomic_dec_and_test(&osb->osb_okp_pending))
		wake_up(&osb->osb_okp_pending_wq);
}

/* see ocfs2_ki_dtor() */
void ocfs2_wait_for_okp_destruction(ocfs2_super *osb)
{
	/* first wait for okps to enter the work queue */
	wait_event(osb->osb_okp_pending_wq,
		   atomic_read(&osb->osb_okp_pending) == 0);
	/*
	 * then wait for keventd to finish with all its work, including ours.
	 *
	 * XXX this makes me very nervous.  what if our work blocks keventd
	 * during an unlock and the unlock can only proceed if keventd
	 * can get to some more work that the dlm might have queued?
	 * do we push any dlm work to keventd?
	 */
	flush_scheduled_work();
}

/* just to stop sys_io_cancel() from spewing to the console when it sees an
 * iocb without ki_cancel */
static int ocfs2_ki_cancel(struct kiocb *iocb, struct io_event *ev)
{
	mlog(0, "iocb %p\n", iocb);
	aio_put_req(iocb);
	return -EAGAIN;
}

static struct ocfs2_kiocb_private *okp_alloc(struct kiocb *iocb)
{
	struct inode *inode = iocb->ki_filp->f_dentry->d_inode;
	struct ocfs2_kiocb_private *okp;
	ocfs2_super *osb;

	okp = kcalloc(1, sizeof(*okp), GFP_KERNEL);
	if (okp == NULL) {
		okp = ERR_PTR(-ENOMEM);
		goto out;
	}

	/* our dtor only gets registerd if we can guarantee that it holds
	 * a reference to the inode */
	okp->kp_inode = igrab(inode);
	if (okp->kp_inode == NULL) {
		kfree(okp);
		okp = ERR_PTR(-EINVAL);
		goto out;
	}
	/* unmount syncs with work using this ref before destroying the osb */
	osb = OCFS2_SB(inode->i_sb);
	okp->kp_osb = osb;

	iocb->private = okp;
	iocb->ki_dtor = ocfs2_ki_dtor;
	iocb->ki_cancel = ocfs2_ki_cancel;
	INIT_BUFFER_LOCK_CTXT(&okp->kp_ctxt);

	atomic_inc(&osb->osb_okp_pending);
out:
	mlog(0, "iocb %p returning %p\n", iocb, okp);
	return okp;
}

/* The DLM supports a minimal notion of AIO lock acquiry.  Instead of testing
 * the iocb or current-> like kernel fs/block paths tend to, it takes an
 * explicit callback which it calls when a lock state attempt makes forward
 * progress.  It would be better if it worked with the native
 * kernel AIO mechanics */
static void ocfs2_aio_kick(int status, unsigned long data)
{
	struct kiocb *iocb = (struct kiocb *)data;
	/* XXX worry about racing with ki_cancel once we set it */
	mlog(0, "iocb %p\n", iocb);
	kick_iocb(iocb);
}

/* this is called as iocb->ki_retry so it is careful to only repeat
 * what is needed */
ssize_t ocfs2_file_aio_read(struct kiocb *iocb, char __user *buf, size_t count,
			    loff_t pos)
{
	struct ocfs2_kiocb_private *okp = iocb->private;
	struct file *filp = iocb->ki_filp;
	struct inode *inode = filp->f_dentry->d_inode;
	struct ocfs2_backing_inode *target_binode;
	ssize_t ret, ret2;
	sigset_t blocked, oldset;

	/*
	 * The DLM doesn't block waiting for network traffic or anything, it
	 * modifies state and calls our callback when things have changed.
	 * However, it still likes to check signals and return ERESTARTSYS.
	 * The AIO core does not appreciate ERESTARTSYS as its semantics are
	 * not exactly clear for submission, etc.  So we block signals and
	 * ensure that the DLM won't notice them.  The caller, particularly
	 * sys_io_getevents(), will eventually check signals before sleeping
	 * and so things should still work as expected, if perhaps with
	 * slightly higher signal delivery latency.
	 */
	sigfillset(&blocked);
	ret = sigprocmask(SIG_BLOCK, &blocked, &oldset);
	if (ret < 0) {
		mlog_errno(ret);
		goto out;
	}

	mlog(0, "iocb %p okp %p\n", iocb, okp);

	if (okp == NULL) {
		okp = okp_alloc(iocb);
		if (IS_ERR(okp)) {
			ret = PTR_ERR(okp);
			mlog_errno(ret);
			goto setmask;
		}

		ret = ocfs2_setup_io_locks(inode->i_sb, inode, buf, count,
					   &okp->kp_ctxt, &target_binode);
		if (ret < 0) {
			mlog_errno(ret);
			goto setmask;
		}

		okp->kp_ctxt.b_cb = ocfs2_aio_kick;
		okp->kp_ctxt.b_cb_data = (unsigned long)iocb;
		target_binode->ba_lock_data = filp->f_flags & O_DIRECT ? 0 : 1;
	}

	/* this might return EIOCBRETRY and we'll come back again to
	 * continue the locking.  It's harmless to call it once it has
	 * returned success.. */
	okp->kp_info.wl_unlock_ctxt = 1; /* re-use the write info path */
	ret = ocfs2_lock_buffer_inodes(&okp->kp_ctxt, NULL);
	if (ret < 0) {
		if (ret != -EIOCBRETRY)
			mlog_errno(ret);
		goto setmask;
	}

	/* hold the ip_alloc_sem across the op */
	if (!okp->kp_have_alloc_sem) {
		down_read(&OCFS2_I(inode)->ip_alloc_sem);
		okp->kp_have_alloc_sem = 1;
	}

	ret = generic_file_aio_read(iocb, buf, count, pos);

setmask:
	ret2 = sigprocmask(SIG_SETMASK, &oldset, NULL);
	if (ret2 < 0) {
		mlog_errno(ret2);
		if (ret == 0)
			ret = ret2;
	}

out:
	/* ki_dtor will always be called eventually, no tear down here */
	mlog(0, "iocb %p returning %lld\n", iocb, (long long)ret);
	return ret;
}

/* this is called as iocb->ki_retry so it is careful to only repeat
 * what is needed */
ssize_t ocfs2_file_aio_write(struct kiocb *iocb, const char __user *buf,
			     size_t count, loff_t pos)
{
	struct ocfs2_kiocb_private *okp = iocb->private;
	struct file *filp = iocb->ki_filp;
	struct inode *inode = filp->f_dentry->d_inode;
	ssize_t ret = 0, ret2;
	sigset_t blocked, oldset;
	struct iovec local_iov = { .iov_base = (void __user *)buf,
				   .iov_len = count };

	/* explained up in ocfs2_file_aio_read() */
	sigfillset(&blocked);
	ret = sigprocmask(SIG_BLOCK, &blocked, &oldset);
	if (ret < 0) {
		mlog_errno(ret);
		goto out;
	}

	mlog(0, "iocb %p okp %p\n", iocb, okp);

	if (okp == NULL) {
		okp = okp_alloc(iocb);
		if (IS_ERR(okp)) {
			ret = PTR_ERR(okp);
			mlog_errno(ret);
			goto up_io;
		}

		okp->kp_ctxt.b_cb = ocfs2_aio_kick;
		okp->kp_ctxt.b_cb_data = (unsigned long)iocb;
	}

	if (!okp->kp_have_write_locks) {
		ret = ocfs2_write_lock_maybe_extend(filp, buf, count,
						    &iocb->ki_pos,
						    &okp->kp_info,
						    &okp->kp_ctxt);
		okp->kp_have_write_locks = 1;
		if (okp->kp_info.wl_extended) {
			/*
			 * this is not a particularly nice place to do this but
			 * extending aio in ocfs2 is not yet a priority.  it
			 * means that we'll write zeros in the buffered case
			 * before then over-writing them with the real op.  It
			 * also sleeps in the aio submission context.
			 */
			ocfs2_file_finish_extension(inode,
						    !okp->kp_info.wl_newsize,
						    okp->kp_info.wl_do_direct_io);
			okp->kp_info.wl_extended = 0;
		}
		if (ret) {
			mlog_errno(ret);
			goto up_io;
		}
	}

	/* hold the ip_alloc_sem across the op */
	if (!okp->kp_have_alloc_sem) {
		down_read(&OCFS2_I(inode)->ip_alloc_sem);
		okp->kp_have_alloc_sem = 1;
	}

up_io:
	/*
	 * never hold i_sem when we leave this function, nor when we call
	 * g_f_a_w().  we've done all extending and inode field updating under
	 * the i_sem and we hold the ip_alloc_sem for reading across the ops.
	 * ocfs2_direct_IO calls blockdev_direct_IO with NO_LOCKING.
	 */
	if (okp->kp_info.wl_have_i_sem) {
		up(&inode->i_sem);
		okp->kp_info.wl_have_i_sem = 0;
	}
	if (ret == 0)
		ret = generic_file_aio_write_nolock(iocb, &local_iov, 1,
						    &iocb->ki_pos);

	ret2 = sigprocmask(SIG_SETMASK, &oldset, NULL);
	if (ret2 < 0) {
		mlog_errno(ret2);
		if (ret == 0)
			ret = ret2;
	}
out:
	/* ki_dtor will always be called eventually, no tear down here */
	mlog(0, "iocb %p returning %lld\n", iocb, (long long)ret);
	return ret;
}
