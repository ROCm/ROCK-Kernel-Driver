/* rwsem.c: R/W semaphores: contention handling functions
 *
 * Written by David Howells (dhowells@redhat.com).
 * Derived from arch/i386/kernel/semaphore.c
 */
#include <linux/rwsem.h>
#include <linux/sched.h>
#include <linux/module.h>

/*
 * wait for the read lock to be granted
 * - need to repeal the increment made inline by the caller
 * - need to throw a write-lock style spanner into the works (sub 0x00010000 from count)
 */
struct rw_semaphore *rwsem_down_read_failed(struct rw_semaphore *sem)
{
	struct task_struct *tsk = current;
	DECLARE_WAITQUEUE(wait,tsk);
	signed long count;

	rwsemdebug("[%d] Entering rwsem_down_read_failed(%08lx)\n",current->pid,sem->count);

	/* this waitqueue context flag will be cleared when we are granted the lock */
	__set_bit(RWSEM_WAITING_FOR_READ,&wait.flags);
	set_task_state(tsk, TASK_UNINTERRUPTIBLE);

	add_wait_queue_exclusive(&sem->wait, &wait); /* FIFO */

	/* note that we're now waiting on the lock, but no longer actively read-locking */
	count = rwsem_atomic_update(RWSEM_WAITING_BIAS-RWSEM_ACTIVE_BIAS,sem);
	rwsemdebug("X(%08lx)\n",count);

	/* if there are no longer active locks, wake the front queued process(es) up
	 * - it might even be this process, since the waker takes a more active part
	 */
	if (!(count & RWSEM_ACTIVE_MASK))
		rwsem_wake(sem);

	/* wait to be given the lock */
	for (;;) {
		if (!test_bit(RWSEM_WAITING_FOR_READ,&wait.flags))
			break;
		schedule();
		set_task_state(tsk, TASK_UNINTERRUPTIBLE);
	}

	remove_wait_queue(&sem->wait,&wait);
	tsk->state = TASK_RUNNING;

	rwsemdebug("[%d] Leaving rwsem_down_read_failed(%08lx)\n",current->pid,sem->count);

	return sem;
}

/*
 * wait for the write lock to be granted
 */
struct rw_semaphore *rwsem_down_write_failed(struct rw_semaphore *sem)
{
	struct task_struct *tsk = current;
	DECLARE_WAITQUEUE(wait,tsk);
	signed long count;

	rwsemdebug("[%d] Entering rwsem_down_write_failed(%08lx)\n",current->pid,sem->count);

	/* this waitqueue context flag will be cleared when we are granted the lock */
	__set_bit(RWSEM_WAITING_FOR_WRITE,&wait.flags);
	set_task_state(tsk, TASK_UNINTERRUPTIBLE);

	add_wait_queue_exclusive(&sem->wait, &wait); /* FIFO */

	/* note that we're waiting on the lock, but no longer actively locking */
	count = rwsem_atomic_update(-RWSEM_ACTIVE_BIAS,sem);
	rwsemdebug("[%d] updated(%08lx)\n",current->pid,count);

	/* if there are no longer active locks, wake the front queued process(es) up
	 * - it might even be this process, since the waker takes a more active part
	 */
	if (!(count & RWSEM_ACTIVE_MASK))
		rwsem_wake(sem);

	/* wait to be given the lock */
	for (;;) {
		if (!test_bit(RWSEM_WAITING_FOR_WRITE,&wait.flags))
			break;
		schedule();
		set_task_state(tsk, TASK_UNINTERRUPTIBLE);
	}

	remove_wait_queue(&sem->wait,&wait);
	tsk->state = TASK_RUNNING;

	rwsemdebug("[%d] Leaving rwsem_down_write_failed(%08lx)\n",current->pid,sem->count);

	return sem;
}

/*
 * handle the lock being released whilst there are processes blocked on it that can now run
 * - if we come here, then:
 *   - the 'active part' of the count (&0x0000ffff) reached zero (but may no longer be zero)
 *   - the 'waiting part' of the count (&0xffff0000) is negative (and will still be so)
 */
struct rw_semaphore *rwsem_wake(struct rw_semaphore *sem)
{
	signed long count;
	int woken;

	rwsemdebug("[%d] Entering rwsem_wake(%08lx)\n",current->pid,sem->count);

 try_again:
	/* try to grab an 'activity' marker
	 * - need to make sure two copies of rwsem_wake() don't do this for two separate processes
	 *   simultaneously
	 * - be horribly naughty, and only deal with the LSW of the atomic counter
	 */
	if (rwsem_cmpxchgw(sem,0,RWSEM_ACTIVE_BIAS)!=0) {
		rwsemdebug("[%d] rwsem_wake: abort wakeup due to renewed activity\n",current->pid);
		goto out;
	}

	/* try to grant a single write lock if there's a writer at the front of the queue
	 * - note we leave the 'active part' of the count incremented by 1 and the waiting part
	 *   incremented by 0x00010000
	 */
	if (wake_up_ctx(&sem->wait,1,-RWSEM_WAITING_FOR_WRITE)==1)
		goto out;

	/* grant an infinite number of read locks to the readers at the front of the queue
	 * - note we increment the 'active part' of the count by the number of readers just woken,
	 *   less one for the activity decrement we've already done
	 */
	woken = wake_up_ctx(&sem->wait,65535,-RWSEM_WAITING_FOR_READ);
	if (woken<=0)
		goto counter_correction;

	woken *= RWSEM_ACTIVE_BIAS-RWSEM_WAITING_BIAS;
	woken -= RWSEM_ACTIVE_BIAS;
	rwsem_atomic_update(woken,sem);

 out:
	rwsemdebug("[%d] Leaving rwsem_wake(%08lx)\n",current->pid,sem->count);
	return sem;

	/* come here if we need to correct the counter for odd SMP-isms */
 counter_correction:
	count = rwsem_atomic_update(-RWSEM_ACTIVE_BIAS,sem);
	rwsemdebug("[%d] corrected(%08lx)\n",current->pid,count);
	if (!(count & RWSEM_ACTIVE_MASK))
		goto try_again;
	goto out;
}

EXPORT_SYMBOL(rwsem_down_read_failed);
EXPORT_SYMBOL(rwsem_down_write_failed);
EXPORT_SYMBOL(rwsem_wake);
