/*
 * Copyright (c) 2000-2002 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */

/*
 *	page_buf_locking.c
 *
 *	The page_buf module provides an abstract buffer cache model on top of
 *	the Linux page cache.  Cached metadata blocks for a file system are
 *	hashed to the inode for the block device.  The page_buf module
 *	assembles buffer (page_buf_t) objects on demand to aggregate such
 *	cached pages for I/O.  The page_buf_locking module adds support for
 *	locking such page buffers.
 *
 *      Written by Steve Lord at SGI
 *
 */

#include <linux/stddef.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/bitops.h>
#include <linux/string.h>
#include <linux/pagemap.h>
#include <linux/init.h>
#include <linux/major.h>

#include <support/debug.h>

#include "page_buf_internal.h"

/*
 *	pagebuf_cond_lock
 *
 *	pagebuf_cond_lock locks a buffer object, if it is not already locked.
 *	Note that this in no way
 *	locks the underlying pages, so it is only useful for synchronizing
 *	concurrent use of page buffer objects, not for synchronizing independent
 *	access to the underlying pages.
 */
int
pagebuf_cond_lock(			/* lock buffer, if not locked	*/
					/* returns -EBUSY if locked)	*/
	page_buf_t		*pb)
{
	int			locked;

	ASSERT(pb->pb_flags & _PBF_LOCKABLE);

	locked = down_trylock(&pb->pb_sema) == 0;
	if (locked) {
		PB_SET_OWNER(pb);
	}

	PB_TRACE(pb, PB_TRACE_REC(condlck), locked);

	return(locked ? 0 : -EBUSY);
}

/*
 *	pagebuf_lock_value
 *
 *	Return lock value for a pagebuf
 */
int
pagebuf_lock_value(
	page_buf_t		*pb)
{
	ASSERT(pb->pb_flags & _PBF_LOCKABLE);
	return(atomic_read(&pb->pb_sema.count));
}

/*
 *	pagebuf_lock
 *
 *	pagebuf_lock locks a buffer object.  Note that this in no way
 *	locks the underlying pages, so it is only useful for synchronizing
 *	concurrent use of page buffer objects, not for synchronizing independent
 *	access to the underlying pages.
 */
int
pagebuf_lock(
	page_buf_t		*pb)
{
	ASSERT(pb->pb_flags & _PBF_LOCKABLE);

	PB_TRACE(pb, PB_TRACE_REC(lock), 0);
	if (atomic_read(&pb->pb_io_remaining))
		blk_run_queues();
	down(&pb->pb_sema);
	PB_SET_OWNER(pb);
	PB_TRACE(pb, PB_TRACE_REC(locked), 0);
	return 0;
}

/*
 *	pagebuf_unlock
 *
 *	pagebuf_unlock releases the lock on the buffer object created by
 *	pagebuf_lock or pagebuf_cond_lock (not any
 *	pinning of underlying pages created by pagebuf_pin).
 */
void
pagebuf_unlock(				/* unlock buffer		*/
	page_buf_t		*pb)	/* buffer to unlock		*/
{
	ASSERT(pb->pb_flags & _PBF_LOCKABLE);
	PB_CLEAR_OWNER(pb);
	up(&pb->pb_sema);
	PB_TRACE(pb, PB_TRACE_REC(unlock), 0);
}
