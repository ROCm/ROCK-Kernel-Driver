/*
 * <asm/smplock.h>
 *
 * i386 SMP lock implementation
 */
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <asm/current.h>

extern spinlock_t kernel_flag;

#ifdef CONFIG_SMP
#define kernel_locked()		spin_is_locked(&kernel_flag)
#else
#ifdef CONFIG_PREEMPT
#define kernel_locked()		preempt_count()
#else
#define kernel_locked()		1
#endif
#endif

/*
 * Release global kernel lock and global interrupt lock
 */
#define release_kernel_lock(task)		\
do {						\
	if (unlikely(task->lock_depth >= 0))	\
		spin_unlock(&kernel_flag);	\
} while (0)

/*
 * Re-acquire the kernel lock
 */
#define reacquire_kernel_lock(task)		\
do {						\
	if (unlikely(task->lock_depth >= 0))	\
		spin_lock(&kernel_flag);	\
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
#ifdef CONFIG_PREEMPT
	if (current->lock_depth == -1)
		spin_lock(&kernel_flag);
	++current->lock_depth;
#else
#if 1
	if (!++current->lock_depth)
		spin_lock(&kernel_flag);
#else
	__asm__ __volatile__(
		"incl %1\n\t"
		"jne 9f"
		spin_lock_string
		"\n9:"
		:"=m" (__dummy_lock(&kernel_flag)),
		 "=m" (current->lock_depth));
#endif
#endif
}

static __inline__ void unlock_kernel(void)
{
	if (current->lock_depth < 0)
		BUG();
#if 1
	if (--current->lock_depth < 0)
		spin_unlock(&kernel_flag);
#else
	__asm__ __volatile__(
		"decl %1\n\t"
		"jns 9f\n\t"
		spin_unlock_string
		"\n9:"
		:"=m" (__dummy_lock(&kernel_flag)),
		 "=m" (current->lock_depth));
#endif
}
