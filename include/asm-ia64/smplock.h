/*
 * <asm/smplock.h>
 *
 * Default SMP lock implementation
 */
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/sched.h>

#include <asm/current.h>
#include <asm/hardirq.h>

extern spinlock_t kernel_flag;

#ifdef CONFIG_SMP
# define kernel_locked()	spin_is_locked(&kernel_flag)
#else
# define kernel_locked()	(1)
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
static __inline__ void
lock_kernel(void)
{
	if (!++current->lock_depth)
		spin_lock(&kernel_flag);
}

static __inline__ void
unlock_kernel(void)
{
	if (--current->lock_depth < 0)
		spin_unlock(&kernel_flag);
}
