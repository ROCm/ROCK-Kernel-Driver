#ifndef __LINUX_SMPLOCK_H
#define __LINUX_SMPLOCK_H

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/spinlock.h>

#ifdef CONFIG_LOCK_KERNEL

#define kernel_locked()		(current->lock_depth >= 0)

extern void __lockfunc get_kernel_lock(void);
extern void __lockfunc put_kernel_lock(void);

/*
 * Release/re-acquire global kernel lock for the scheduler
 */
#define release_kernel_lock(tsk) do { 		\
	if (unlikely((tsk)->lock_depth >= 0))	\
		put_kernel_lock();		\
} while (0)

#define reacquire_kernel_lock(tsk) do {	\
	if (unlikely((tsk)->lock_depth >= 0))	\
		get_kernel_lock();		\
} while (0)

extern void __lockfunc lock_kernel(void)	__acquires(kernel_lock);
extern void __lockfunc unlock_kernel(void)	__releases(kernel_lock);

#else

#define lock_kernel()				do { } while(0)
#define unlock_kernel()				do { } while(0)
#define release_kernel_lock(task)		do { } while(0)
#define reacquire_kernel_lock(task)		do { } while(0)
#define kernel_locked()				1

#endif /* CONFIG_LOCK_KERNEL */
#endif /* __LINUX_SMPLOCK_H */
