/*
 *  Generic semaphore code. Buyer beware. Do your own
 * specific changes in <asm/semaphore-helper.h>
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <asm/semaphore-helper.h>

#ifndef CONFIG_RMW_INSNS
spinlock_t semaphore_wake_lock;
#endif

/*
 * Semaphores are implemented using a two-way counter:
 * The "count" variable is decremented for each process
 * that tries to sleep, while the "waking" variable is
 * incremented when the "up()" code goes to wake up waiting
 * processes.
 *
 * Notably, the inline "up()" and "down()" functions can
 * efficiently test if they need to do any extra work (up
 * needs to do something only if count was negative before
 * the increment operation.
 *
 * waking_non_zero() (from asm/semaphore.h) must execute
 * atomically.
 *
 * When __up() is called, the count was negative before
 * incrementing it, and we need to wake up somebody.
 *
 * This routine adds one to the count of processes that need to
 * wake up and exit.  ALL waiting processes actually wake up but
 * only the one that gets to the "waking" field first will gate
 * through and acquire the semaphore.  The others will go back
 * to sleep.
 *
 * Note that these functions are only called when there is
 * contention on the lock, and as such all this is the
 * "non-critical" part of the whole semaphore business. The
 * critical part is the inline stuff in <asm/semaphore.h>
 * where we want to avoid any extra jumps and calls.
 */
void __up(struct semaphore *sem)
{
	wake_one_more(sem);
	wake_up(&sem->wait);
}

/*
 * Perform the "down" function.  Return zero for semaphore acquired,
 * return negative for signalled out of the function.
 *
 * If called from __down, the return is ignored and the wait loop is
 * not interruptible.  This means that a task waiting on a semaphore
 * using "down()" cannot be killed until someone does an "up()" on
 * the semaphore.
 *
 * If called from __down_interruptible, the return value gets checked
 * upon return.  If the return value is negative then the task continues
 * with the negative value in the return register (it can be tested by
 * the caller).
 *
 * Either form may be used in conjunction with "up()".
 *
 */


#define DOWN_HEAD(task_state)						\
									\
									\
	current->state = (task_state);					\
	add_wait_queue(&sem->wait, &wait);				\
									\
	/*								\
	 * Ok, we're set up.  sem->count is known to be less than zero	\
	 * so we must wait.						\
	 *								\
	 * We can let go the lock for purposes of waiting.		\
	 * We re-acquire it after awaking so as to protect		\
	 * all semaphore operations.					\
	 *								\
	 * If "up()" is called before we call waking_non_zero() then	\
	 * we will catch it right away.  If it is called later then	\
	 * we will have to go through a wakeup cycle to catch it.	\
	 *								\
	 * Multiple waiters contend for the semaphore lock to see	\
	 * who gets to gate through and who has to wait some more.	\
	 */								\
	for (;;) {

#define DOWN_TAIL(task_state)			\
		current->state = (task_state);	\
	}					\
	current->state = TASK_RUNNING;		\
	remove_wait_queue(&sem->wait, &wait);

void __down(struct semaphore * sem)
{
	DECLARE_WAITQUEUE(wait, current);

	DOWN_HEAD(TASK_UNINTERRUPTIBLE)
	if (waking_non_zero(sem))
		break;
	schedule();
	DOWN_TAIL(TASK_UNINTERRUPTIBLE)
}

int __down_interruptible(struct semaphore * sem)
{
	DECLARE_WAITQUEUE(wait, current);
	int ret = 0;

	DOWN_HEAD(TASK_INTERRUPTIBLE)

	ret = waking_non_zero_interruptible(sem, current);
	if (ret)
	{
		if (ret == 1)
			/* ret != 0 only if we get interrupted -arca */
			ret = 0;
		break;
	}
	schedule();
	DOWN_TAIL(TASK_INTERRUPTIBLE)
	return ret;
}

int __down_trylock(struct semaphore * sem)
{
	return waking_non_zero_trylock(sem);
}


/* Wait for the lock to become unbiased.  Readers
 * are non-exclusive. =)
 */
void down_read_failed(struct rw_semaphore *sem)
{
	DECLARE_WAITQUEUE(wait, current);

	__up_read(sem);	/* this takes care of granting the lock */

	add_wait_queue(&sem->wait, &wait);

	while (atomic_read(&sem->count) < 0) {
		set_task_state(current, TASK_UNINTERRUPTIBLE);
		if (atomic_read(&sem->count) >= 0)
			break;
		schedule();
	}

	remove_wait_queue(&sem->wait, &wait);
	current->state = TASK_RUNNING;
}

void down_read_failed_biased(struct rw_semaphore *sem)
{
	DECLARE_WAITQUEUE(wait, current);

	add_wait_queue(&sem->wait, &wait);	/* put ourselves at the head of the list */

	for (;;) {
		if (sem->read_bias_granted && xchg(&sem->read_bias_granted, 0))
			break;
		set_task_state(current, TASK_UNINTERRUPTIBLE);
                if (!sem->read_bias_granted)
			schedule();
	}

	remove_wait_queue(&sem->wait, &wait);
	current->state = TASK_RUNNING;
}


/* Wait for the lock to become unbiased. Since we're
 * a writer, we'll make ourselves exclusive.
 */
void down_write_failed(struct rw_semaphore *sem)
{
	DECLARE_WAITQUEUE(wait, current);

	__up_write(sem);	/* this takes care of granting the lock */

	add_wait_queue_exclusive(&sem->wait, &wait);

	while (atomic_read(&sem->count) < 0) {
		set_task_state(current, TASK_UNINTERRUPTIBLE);
		if (atomic_read(&sem->count) >= 0)
			break;	/* we must attempt to acquire or bias the lock */
		schedule();
	}

	remove_wait_queue(&sem->wait, &wait);
	current->state = TASK_RUNNING;
}

void down_write_failed_biased(struct rw_semaphore *sem)
{
	DECLARE_WAITQUEUE(wait, current);

	add_wait_queue_exclusive(&sem->write_bias_wait, &wait);	/* put ourselves at the end of the list */

	for (;;) {
		if (sem->write_bias_granted && xchg(&sem->write_bias_granted, 0))
			break;
		set_task_state(current, TASK_UNINTERRUPTIBLE);
		if (!sem->write_bias_granted)
			schedule();
	}

	remove_wait_queue(&sem->write_bias_wait, &wait);
	current->state = TASK_RUNNING;

	/* if the lock is currently unbiased, awaken the sleepers
	 * FIXME: this wakes up the readers early in a bit of a
	 * stampede -> bad!
	 */
	if (atomic_read(&sem->count) >= 0)
		wake_up(&sem->wait);
}


/* Called when someone has done an up that transitioned from
 * negative to non-negative, meaning that the lock has been
 * granted to whomever owned the bias.
 */
void rwsem_wake_readers(struct rw_semaphore *sem)
{
	if (xchg(&sem->read_bias_granted, 1))
		BUG();
	wake_up(&sem->wait);
}

void rwsem_wake_writer(struct rw_semaphore *sem)
{
	if (xchg(&sem->write_bias_granted, 1))
		BUG();
	wake_up(&sem->write_bias_wait);
}
