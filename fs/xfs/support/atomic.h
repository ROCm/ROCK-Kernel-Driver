/*
 * Copyright (c) 2000-2003 Silicon Graphics, Inc.  All Rights Reserved.
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
 * or the like.	 Any license provided herein, whether implied or
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
#ifndef __XFS_SUPPORT_ATOMIC_H__
#define __XFS_SUPPORT_ATOMIC_H__

#include <linux/version.h>
#include <linux/time.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <asm/atomic.h>
#include <asm/semaphore.h>

/*
 * This is used for two variables in XFS, one of which is a debug trace
 * buffer index. They are not accessed via any other atomic operations
 * so this is safe. All other atomic increments and decrements in XFS
 * now use the Linux built-in functions.
 */

extern spinlock_t xfs_atomic_spin;

static __inline__ int atomicIncWithWrap(int *ip, int val)
{
	unsigned long flags;
	int ret;
	spin_lock_irqsave(&xfs_atomic_spin, flags);
	ret = *ip;
	(*ip)++;
	if (*ip == val) *ip = 0;
	spin_unlock_irqrestore(&xfs_atomic_spin, flags);
	return ret;
}

#endif /* __XFS_SUPPORT_ATOMIC_H__ */
