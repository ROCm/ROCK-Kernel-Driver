#ifndef __LINUX_SMPLOCK_H
#define __LINUX_SMPLOCK_H

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/spinlock.h>

#if defined(CONFIG_SMP) || defined(CONFIG_PREEMPT)

extern spinlock_t kernel_flag;

#define kernel_locked()		(current->lock_depth >= 0)

#define get_kernel_lock()	spin_lock(&kernel_flag)
#define put_kernel_lock()	spin_unlock(&kernel_flag)

/*
 * Release global kernel lock.
 */
static inline void release_kernel_lock(struct task_struct *task)
{
	if (unlikely(task->lock_depth >= 0))
		put_kernel_lock();
}

/*
 * Re-acquire the kernel lock
 */
static inline void reacquire_kernel_lock(struct task_struct *task)
{
	if (unlikely(task->lock_depth >= 0))
		get_kernel_lock();
}

/*
 * Getting the big kernel lock.
 *
 * This cannot happen asynchronously,
 * so we only need to worry about other
 * CPU's.
 */
static inline void lock_kernel(void)
{
	int depth = current->lock_depth+1;
	if (likely(!depth))
		get_kernel_lock();
	current->lock_depth = depth;
}

static inline void unlock_kernel(void)
{
	if (unlikely(current->lock_depth < 0))
		BUG();
	if (likely(--current->lock_depth < 0))
		put_kernel_lock();
}

#else

#define lock_kernel()				do { } while(0)
#define unlock_kernel()				do { } while(0)
#define release_kernel_lock(task)		do { } while(0)
#define reacquire_kernel_lock(task)		do { } while(0)
#define kernel_locked()				1

#endif /* CONFIG_SMP || CONFIG_PREEMPT */
#endif /* __LINUX_SMPLOCK_H */
