/*
 * lib/kernel_lock.c
 *
 * This is the traditional BKL - big kernel lock. Largely
 * relegated to obsolescense, but used by various less
 * important (or lazy) subsystems.
 */
#include <linux/smp_lock.h>
#include <linux/module.h>

/*
 * The 'big kernel lock'
 *
 * This spinlock is taken and released recursively by lock_kernel()
 * and unlock_kernel().  It is transparently dropped and reaquired
 * over schedule().  It is used to protect legacy code that hasn't
 * been migrated to a proper locking design yet.
 *
 * Don't use in new code.
 */
static spinlock_t kernel_flag __cacheline_aligned_in_smp = SPIN_LOCK_UNLOCKED;


/*
 * Acquire/release the underlying lock from the scheduler.
 *
 * The scheduler release and re-acquire currently always happen
 * with preemption disabled. Which is likely a bug in the acquire
 * case...
 *
 * Regardless, we try to be polite about preemption. If SMP is
 * not on (ie UP preemption), this all goes away because the
 * _raw_spin_trylock() will always succeed.
 */
#ifdef CONFIG_PREEMPT
inline void __lockfunc get_kernel_lock(void)
{
	preempt_disable();
	if (unlikely(!_raw_spin_trylock(&kernel_flag))) {
		/*
		 * If preemption was disabled even before this
		 * was called, there's nothing we can be polite
		 * about - just spin.
		 */
		if (preempt_count() > 1) {
			_raw_spin_lock(&kernel_flag);
			return;
		}

		/*
		 * Otherwise, let's wait for the kernel lock
		 * with preemption enabled..
		 */
		do {
			preempt_enable();
			while (spin_is_locked(&kernel_flag))
				cpu_relax();
			preempt_disable();
		} while (!_raw_spin_trylock(&kernel_flag));
	}
}

#else

/*
 * Non-preemption case - just get the spinlock
 */
inline void __lockfunc get_kernel_lock(void)
{
	_raw_spin_lock(&kernel_flag);
}
#endif

inline void __lockfunc put_kernel_lock(void)
{
	_raw_spin_unlock(&kernel_flag);
	preempt_enable();
}

/*
 * Getting the big kernel lock.
 *
 * This cannot happen asynchronously, so we only need to
 * worry about other CPU's.
 */
void __lockfunc lock_kernel(void)
{
	int depth = current->lock_depth+1;
	if (likely(!depth))
		get_kernel_lock();
	current->lock_depth = depth;
}

void __lockfunc unlock_kernel(void)
{
	BUG_ON(current->lock_depth < 0);
	if (likely(--current->lock_depth < 0))
		put_kernel_lock();
}

EXPORT_SYMBOL(lock_kernel);
EXPORT_SYMBOL(unlock_kernel);
