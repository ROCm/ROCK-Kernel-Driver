/* $Id: semaphore.c,v 1.5 2000/12/29 10:35:05 anton Exp $ */

/* sparc32 semaphore implementation, based on i386 version */

#include <linux/sched.h>

#include <asm/semaphore.h>

/*
 * Semaphores are implemented using a two-way counter:
 * The "count" variable is decremented for each process
 * that tries to acquire the semaphore, while the "sleeping"
 * variable is a count of such acquires.
 *
 * Notably, the inline "up()" and "down()" functions can
 * efficiently test if they need to do any extra work (up
 * needs to do something only if count was negative before
 * the increment operation.
 *
 * "sleeping" and the contention routine ordering is
 * protected by the semaphore spinlock.
 *
 * Note that these functions are only called when there is
 * contention on the lock, and as such all this is the
 * "non-critical" part of the whole semaphore business. The
 * critical part is the inline stuff in <asm/semaphore.h>
 * where we want to avoid any extra jumps and calls.
 */

/*
 * Logic:
 *  - only on a boundary condition do we need to care. When we go
 *    from a negative count to a non-negative, we wake people up.
 *  - when we go from a non-negative count to a negative do we
 *    (a) synchronize with the "sleeper" count and (b) make sure
 *    that we're on the wakeup list before we synchronize so that
 *    we cannot lose wakeup events.
 */

void __up(struct semaphore *sem)
{
	wake_up(&sem->wait);
}

static spinlock_t semaphore_lock = SPIN_LOCK_UNLOCKED;

void __down(struct semaphore * sem)
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

int __down_interruptible(struct semaphore * sem)
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
 * Trylock failed - make sure we correct for
 * having decremented the count.
 */
int __down_trylock(struct semaphore * sem)
{
	int sleepers;
	unsigned long flags;

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

/* rw mutexes
 * Implemented by Jakub Jelinek (jakub@redhat.com) based on
 * i386 implementation by Ben LaHaise (bcrl@redhat.com).
 */

extern inline int ldstub(unsigned char *p)
{
	int ret;
	asm volatile("ldstub %1, %0" : "=r" (ret) : "m" (*p) : "memory");
	return ret;
}

#define DOWN_VAR				\
	struct task_struct *tsk = current;	\
	DECLARE_WAITQUEUE(wait, tsk);

void down_read_failed_biased(struct rw_semaphore *sem)
{
	DOWN_VAR

	add_wait_queue(&sem->wait, &wait);	/* put ourselves at the head of the list */

	for (;;) {
		if (!ldstub(&sem->read_not_granted))
			break;
		set_task_state(tsk, TASK_UNINTERRUPTIBLE);
		if (sem->read_not_granted)
			schedule();
	}

	remove_wait_queue(&sem->wait, &wait);
	tsk->state = TASK_RUNNING;
}

void down_write_failed_biased(struct rw_semaphore *sem)
{
	DOWN_VAR

	add_wait_queue_exclusive(&sem->write_bias_wait, &wait); /* put ourselves at the end of the list */

	for (;;) {
		if (!ldstub(&sem->write_not_granted))
			break;
		set_task_state(tsk, TASK_UNINTERRUPTIBLE);
		if (sem->write_not_granted)
			schedule();
	}

	remove_wait_queue(&sem->write_bias_wait, &wait);
	tsk->state = TASK_RUNNING;

	/* if the lock is currently unbiased, awaken the sleepers
	 * FIXME: this wakes up the readers early in a bit of a
	 * stampede -> bad!
	 */
	if (sem->count >= 0)
		wake_up(&sem->wait);
}

/* Wait for the lock to become unbiased.  Readers
 * are non-exclusive. =)
 */
void down_read_failed(struct rw_semaphore *sem)
{
	DOWN_VAR

	__up_read(sem); /* this takes care of granting the lock */

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

/* Wait for the lock to become unbiased. Since we're
 * a writer, we'll make ourselves exclusive.
 */
void down_write_failed(struct rw_semaphore *sem)
{
	DOWN_VAR

	__up_write(sem);	/* this takes care of granting the lock */

	add_wait_queue_exclusive(&sem->wait, &wait);

	while (sem->count < 0) {
		set_task_state(tsk, TASK_UNINTERRUPTIBLE);
		if (sem->count >= 0)
			break;  /* we must attempt to acquire or bias the lock */
		schedule();
	}

	remove_wait_queue(&sem->wait, &wait);
	tsk->state = TASK_RUNNING;
}

void __rwsem_wake(struct rw_semaphore *sem, unsigned long readers)
{
	if (readers) {
		/* Due to lame ldstub we don't do here
		   a BUG() consistency check */
		sem->read_not_granted = 0;
		wake_up(&sem->wait);
	} else {
		sem->write_not_granted = 0;
		wake_up(&sem->write_bias_wait);
	}
}
