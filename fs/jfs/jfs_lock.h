/*
 *   Copyright (c) International Business Machines Corp., 2000-2001
 *   Portions Copyright (c) Christoph Hellwig, 2001-2002
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or 
 *   (at your option) any later version.
 * 
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software 
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#ifndef _H_JFS_LOCK
#define _H_JFS_LOCK

#include <linux/spinlock.h>
#include <linux/sched.h>

/*
 *	jfs_lock.h
 *
 * JFS lock definition for globally referenced locks
 */

/* readers/writer lock: thread-thread */

/*
 * RW semaphores do not currently have a trylock function.  Since the
 * implementation varies by platform, I have implemented a platform-independent
 * wrapper around the rw_semaphore routines.  If this turns out to be the best
 * way of avoiding our locking problems, I will push to get a trylock
 * implemented in the kernel, but I'd rather find a way to avoid having to
 * use it.
 */
#define RDWRLOCK_T jfs_rwlock_t
static inline void RDWRLOCK_INIT(jfs_rwlock_t * Lock)
{
	init_rwsem(&Lock->rw_sem);
	atomic_set(&Lock->in_use, 0);
}
static inline void READ_LOCK(jfs_rwlock_t * Lock)
{
	atomic_inc(&Lock->in_use);
	down_read(&Lock->rw_sem);
}
static inline void READ_UNLOCK(jfs_rwlock_t * Lock)
{
	up_read(&Lock->rw_sem);
	atomic_dec(&Lock->in_use);
}
static inline void WRITE_LOCK(jfs_rwlock_t * Lock)
{
	atomic_inc(&Lock->in_use);
	down_write(&Lock->rw_sem);
}

static inline int WRITE_TRYLOCK(jfs_rwlock_t * Lock)
{
	if (atomic_read(&Lock->in_use))
		return 0;
	WRITE_LOCK(Lock);
	return 1;
}
static inline void WRITE_UNLOCK(jfs_rwlock_t * Lock)
{
	up_write(&Lock->rw_sem);
	atomic_dec(&Lock->in_use);
}

#define IREAD_LOCK(ip)		READ_LOCK(&JFS_IP(ip)->rdwrlock)
#define IREAD_UNLOCK(ip)	READ_UNLOCK(&JFS_IP(ip)->rdwrlock)
#define IWRITE_LOCK(ip)		WRITE_LOCK(&JFS_IP(ip)->rdwrlock)
#define IWRITE_TRYLOCK(ip)	WRITE_TRYLOCK(&JFS_IP(ip)->rdwrlock)
#define IWRITE_UNLOCK(ip)	WRITE_UNLOCK(&JFS_IP(ip)->rdwrlock)
#define IWRITE_LOCK_LIST	iwritelocklist

extern void iwritelocklist(int, ...);

/*
 * Conditional sleep where condition is protected by spinlock
 *
 * lock_cmd and unlock_cmd take and release the spinlock
 */
#define __SLEEP_COND(wq, cond, lock_cmd, unlock_cmd)	\
do {							\
	DECLARE_WAITQUEUE(__wait, current);		\
							\
	add_wait_queue(&wq, &__wait);			\
	for (;;) {					\
		set_current_state(TASK_UNINTERRUPTIBLE);\
		if (cond)				\
			break;				\
		unlock_cmd;				\
		schedule();				\
		lock_cmd;				\
	}						\
	current->state = TASK_RUNNING;			\
	remove_wait_queue(&wq, &__wait);		\
} while (0)

#endif				/* _H_JFS_LOCK */
