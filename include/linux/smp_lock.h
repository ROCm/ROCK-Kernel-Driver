#ifndef __LINUX_SMPLOCK_H
#define __LINUX_SMPLOCK_H

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/spinlock.h>

#ifdef CONFIG_LOCK_KERNEL

#define kernel_locked()		(current->lock_depth >= 0)

extern int __lockfunc get_kernel_lock(void);
extern void __lockfunc put_kernel_lock(void);

/*
 * Release/re-acquire global kernel lock for the scheduler
 */
#define release_kernel_lock(tsk) do { 		\
	if (unlikely((tsk)->lock_depth >= 0))	\
		put_kernel_lock();		\
} while (0)

/*
 * Non-SMP kernels will never block on the kernel lock,
 * so we are better off returning a constant zero from
 * reacquire_kernel_lock() so that the compiler can see
 * it at compile-time.
 */
#ifdef CONFIG_SMP
#define return_value_on_smp return
#else
#define return_value_on_smp
#endif

static inline int reacquire_kernel_lock(struct task_struct *task)
{
	if (unlikely(task->lock_depth >= 0))
		return_value_on_smp get_kernel_lock();
	return 0;
}

extern void __lockfunc lock_kernel(void)	__acquires(kernel_lock);
extern void __lockfunc unlock_kernel(void)	__releases(kernel_lock);

#else

#define lock_kernel()				do { } while(0)
#define unlock_kernel()				do { } while(0)
#define release_kernel_lock(task)		do { } while(0)
#define reacquire_kernel_lock(task)		0
#define kernel_locked()				1

#endif /* CONFIG_LOCK_KERNEL */
#endif /* __LINUX_SMPLOCK_H */
