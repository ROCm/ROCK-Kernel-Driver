/*
 * IA-64 semaphore implementation (derived from x86 version).
 *
 * Copyright (C) 1999-2000 Hewlett-Packard Co
 * Copyright (C) 1999-2000 David Mosberger-Tang <davidm@hpl.hp.com>
 */

/*
 * Semaphores are implemented using a two-way counter: The "count"
 * variable is decremented for each process that tries to acquire the
 * semaphore, while the "sleepers" variable is a count of such
 * acquires.
 *
 * Notably, the inline "up()" and "down()" functions can efficiently
 * test if they need to do any extra work (up needs to do something
 * only if count was negative before the increment operation.
 *
 * "sleepers" and the contention routine ordering is protected by the
 * semaphore spinlock.
 *
 * Note that these functions are only called when there is contention
 * on the lock, and as such all this is the "non-critical" part of the
 * whole semaphore business. The critical part is the inline stuff in
 * <asm/semaphore.h> where we want to avoid any extra jumps and calls.
 */
#include <linux/sched.h>

#include <asm/semaphore.h>

/*
 * Logic:
 *  - Only on a boundary condition do we need to care. When we go
 *    from a negative count to a non-negative, we wake people up.
 *  - When we go from a non-negative count to a negative do we
 *    (a) synchronize with the "sleepers" count and (b) make sure
 *    that we're on the wakeup list before we synchronize so that
 *    we cannot lose wakeup events.
 */

void
__up (struct semaphore *sem)
{
	wake_up(&sem->wait);
}

static spinlock_t semaphore_lock = SPIN_LOCK_UNLOCKED;

void
__down (struct semaphore *sem)
{
	struct task_struct *tsk = current;
	DECLARE_WAITQUEUE(wait, tsk);
	tsk->state = TASK_UNINTERRUPTIBLE;
	add_wait_queue_exclusive(&sem->wait, &wait);

	spin_lock_irq(&semaphore_lock);
	sem->sleepers++;
	for (;;) {
		int sleepers = sem->sleepers;

		/*
		 * Add "everybody else" into it. They aren't
		 * playing, because we own the spinlock.
		 */
		if (!atomic_add_negative(sleepers - 1, &sem->count)) {
			sem->sleepers = 0;
			break;
		}
		sem->sleepers = 1;	/* us - see -1 above */
		spin_unlock_irq(&semaphore_lock);

		schedule();
		tsk->state = TASK_UNINTERRUPTIBLE;
		spin_lock_irq(&semaphore_lock);
	}
	spin_unlock_irq(&semaphore_lock);
	remove_wait_queue(&sem->wait, &wait);
	tsk->state = TASK_RUNNING;
	wake_up(&sem->wait);
}

int
__down_interruptible (struct semaphore * sem)
{
	int retval = 0;
	struct task_struct *tsk = current;
	DECLARE_WAITQUEUE(wait, tsk);
	tsk->state = TASK_INTERRUPTIBLE;
	add_wait_queue_exclusive(&sem->wait, &wait);

	spin_lock_irq(&semaphore_lock);
	sem->sleepers ++;
	for (;;) {
		int sleepers = sem->sleepers;

		/*
		 * With signals pending, this turns into
		 * the trylock failure case - we won't be
		 * sleeping, and we* can't get the lock as
		 * it has contention. Just correct the count
		 * and exit.
		 */
		if (signal_pending(current)) {
			retval = -EINTR;
			sem->sleepers = 0;
			atomic_add(sleepers, &sem->count);
			break;
		}

		/*
		 * Add "everybody else" into it. They aren't
		 * playing, because we own the spinlock. The
		 * "-1" is because we're still hoping to get
		 * the lock.
		 */
		if (!atomic_add_negative(sleepers - 1, &sem->count)) {
			sem->sleepers = 0;
			break;
		}
		sem->sleepers = 1;	/* us - see -1 above */
		spin_unlock_irq(&semaphore_lock);

		schedule();
		tsk->state = TASK_INTERRUPTIBLE;
		spin_lock_irq(&semaphore_lock);
	}
	spin_unlock_irq(&semaphore_lock);
	tsk->state = TASK_RUNNING;
	remove_wait_queue(&sem->wait, &wait);
	wake_up(&sem->wait);
	return retval;
}

/*
 * Trylock failed - make sure we correct for having decremented the
 * count.
 */
int
__down_trylock (struct semaphore *sem)
{
	unsigned long flags;
	int sleepers;

	spin_lock_irqsave(&semaphore_lock, flags);
	sleepers = sem->sleepers + 1;
	sem->sleepers = 0;

	/*
	 * Add "everybody else" and us into it. They aren't
	 * playing, because we own the spinlock.
	 */
	if (!atomic_add_negative(sleepers, &sem->count))
		wake_up(&sem->wait);

	spin_unlock_irqrestore(&semaphore_lock, flags);
	return 1;
}

/*
 * Helper routines for rw semaphores.  These could be optimized some
 * more, but since they're off the critical path, I prefer clarity for
 * now...
 */

/*
 * This gets called if we failed to acquire the lock, but we're biased
 * to acquire the lock by virtue of causing the count to change from 0
 * to -1.  Being biased, we sleep and attempt to grab the lock until
 * we succeed.  When this function returns, we own the lock.
 */
static inline void
down_read_failed_biased (struct rw_semaphore *sem)
{
	struct task_struct *tsk = current;
	DECLARE_WAITQUEUE(wait, tsk);

	add_wait_queue(&sem->wait, &wait);	/* put ourselves at the head of the list */

	for (;;) {
		if (sem->read_bias_granted && xchg(&sem->read_bias_granted, 0))
			break;
		set_task_state(tsk, TASK_UNINTERRUPTIBLE);
		if (!sem->read_bias_granted)
			schedule();
	}
	remove_wait_queue(&sem->wait, &wait);
	tsk->state = TASK_RUNNING;
}

/*
 * This gets called if we failed to acquire the lock and we are not
 * biased to acquire the lock.  We undo the decrement that was
 * done earlier, go to sleep, and then attempt to re-acquire the
 * lock afterwards.
 */
static inline void
down_read_failed (struct rw_semaphore *sem)
{
	struct task_struct *tsk = current;
	DECLARE_WAITQUEUE(wait, tsk);

	/*
	 * Undo the decrement we did in down_read() and check if we
	 * need to wake up someone.
	 */
	__up_read(sem);

	add_wait_queue(&sem->wait, &wait);
	while (sem->count < 0) {
		set_task_state(tsk, TASK_UNINTERRUPTIBLE);
		if (sem->count >= 0)
			break;
		schedule();
	}
	remove_wait_queue(&sem->wait, &wait);
	tsk->state = TASK_RUNNING;
}

/*
 * Wait for the lock to become unbiased.  Readers are non-exclusive.
 */
void
__down_read_failed (struct rw_semaphore *sem, long count)
{
	while (1) {
		if (count == -1) {
			down_read_failed_biased(sem);
			return;
		}
		/* unbiased */
		down_read_failed(sem);

		count = ia64_fetch_and_add(-1, &sem->count);
		if (count >= 0)
			return;
	}
}

static inline void
down_write_failed_biased (struct rw_semaphore *sem)
{
	struct task_struct *tsk = current;
	DECLARE_WAITQUEUE(wait, tsk);

	/* put ourselves at the end of the list */
	add_wait_queue_exclusive(&sem->write_bias_wait, &wait);

	for (;;) {
		if (sem->write_bias_granted && xchg(&sem->write_bias_granted, 0))
			break;
		set_task_state(tsk, TASK_UNINTERRUPTIBLE);
		if (!sem->write_bias_granted)
			schedule();
	}

	remove_wait_queue(&sem->write_bias_wait, &wait);
	tsk->state = TASK_RUNNING;

	/*
	 * If the lock is currently unbiased, awaken the sleepers
	 * FIXME: this wakes up the readers early in a bit of a
	 * stampede -> bad!
	 */
	if (sem->count >= 0)
		wake_up(&sem->wait);
}


static inline void
down_write_failed (struct rw_semaphore *sem)
{
	struct task_struct *tsk = current;
	DECLARE_WAITQUEUE(wait, tsk);

	__up_write(sem);	/* this takes care of granting the lock */

	add_wait_queue_exclusive(&sem->wait, &wait);

	while (sem->count < 0) {
		set_task_state(tsk, TASK_UNINTERRUPTIBLE);
		if (sem->count >= 0)
			break;	/* we must attempt to acquire or bias the lock */
		schedule();
	}

	remove_wait_queue(&sem->wait, &wait);
	tsk->state = TASK_RUNNING;
}


/*
 * Wait for the lock to become unbiased.  Since we're a writer, we'll
 * make ourselves exclusive.
 */
void
__down_write_failed (struct rw_semaphore *sem, long count)
{
	long old_count;

	while (1) {
		if (count == -RW_LOCK_BIAS) {
			down_write_failed_biased(sem);
			return;
		}
		down_write_failed(sem);

		do {
			old_count = sem->count;
			count = old_count - RW_LOCK_BIAS;
		} while (cmpxchg_acq(&sem->count, old_count, count) != old_count);

		if (count == 0)
			return;
	}
}

void
__rwsem_wake (struct rw_semaphore *sem, long count)
{
	wait_queue_head_t *wq;

	if (count == 0) {
		/* wake a writer */
		if (xchg(&sem->write_bias_granted, 1))
			BUG();
		wq = &sem->write_bias_wait;
	} else {
		/* wake reader(s) */
		if (xchg(&sem->read_bias_granted, 1))
			BUG();
		wq = &sem->wait;
	}
	wake_up(wq);	/* wake up everyone on the wait queue */
}
