#ifndef __LINUX_SMPLOCK_H
#define __LINUX_SMPLOCK_H

#include <linux/config.h>

#if !defined(CONFIG_SMP) && !defined(CONFIG_PREEMPT)

#define lock_kernel()				do { } while(0)
#define unlock_kernel()				do { } while(0)
#define release_kernel_lock(task)		do { } while(0)
#define reacquire_kernel_lock(task)		do { } while(0)
#define kernel_locked() 1

#else

#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <asm/current.h>

extern spinlock_t kernel_flag;

#define kernel_locked()		(current->lock_depth >= 0)

#define get_kernel_lock()	spin_lock(&kernel_flag)
#define put_kernel_lock()	spin_unlock(&kernel_flag)

/*
 * Release global kernel lock and global interrupt lock
 */
#define release_kernel_lock(task)		\
do {						\
	if (unlikely(task->lock_depth >= 0))	\
		put_kernel_lock();		\
} while (0)

/*
 * Re-acquire the kernel lock
 */
#define reacquire_kernel_lock(task)		\
do {						\
	if (unlikely(task->lock_depth >= 0))	\
		get_kernel_lock();		\
} while (0)


/*
 * Getting the big kernel lock.
 *
 * This cannot happen asynchronously,
 * so we only need to worry about other
 * CPU's.
 */
static __inline__ void lock_kernel(void)
{
	int depth = current->lock_depth+1;
	if (!depth)
		get_kernel_lock();
	current->lock_depth = depth;
}

static __inline__ void unlock_kernel(void)
{
	if (current->lock_depth < 0)
		BUG();
	if (--current->lock_depth < 0)
		put_kernel_lock();
}

#endif /* CONFIG_SMP */

#endif
