/* rwsem.c: R/W semaphores: contention handling functions
 *
 * Written by David Howells (dhowells@redhat.com).
 * Derived from arch/i386/kernel/semaphore.c
 */
#include <linux/rwsem.h>
#include <linux/sched.h>
#include <linux/module.h>

struct rwsem_waiter {
	struct rwsem_waiter	*next;
	struct task_struct	*task;
	unsigned int		flags;
#define RWSEM_WAITING_FOR_READ	0x00000001
#define RWSEM_WAITING_FOR_WRITE	0x00000002
};
#define RWSEM_WAITER_MAGIC 0x52575345

static struct rw_semaphore *FASTCALL(__rwsem_do_wake(struct rw_semaphore *sem));

#if RWSEM_DEBUG
void rwsemtrace(struct rw_semaphore *sem, const char *str)
{
	if (sem->debug)
		printk("[%d] %s(count=%08lx)\n",current->pid,str,sem->count);
}
#endif

/*
 * handle the lock being released whilst there are processes blocked on it that can now run
 * - if we come here, then:
 *   - the 'active part' of the count (&0x0000ffff) reached zero (but may no longer be zero)
 *   - the 'waiting part' of the count (&0xffff0000) is negative (and will still be so)
 *   - the spinlock must be held before entry
 *   - woken process blocks are discarded from the list after having flags zeroised
 */
static struct rw_semaphore *__rwsem_do_wake(struct rw_semaphore *sem)
{
	struct rwsem_waiter *waiter, *next;
	int woken, loop;

	rwsemtrace(sem,"Entering __rwsem_do_wake");

	/* try to grab an 'activity' marker
	 * - need to make sure two copies of rwsem_wake() don't do this for two separate processes
	 *   simultaneously
	 * - be horribly naughty, and only deal with the LSW of the atomic counter
	 */
	if (rwsem_cmpxchgw(sem,0,RWSEM_ACTIVE_BIAS)!=0) {
		rwsemtrace(sem,"__rwsem_do_wake: abort wakeup due to renewed activity");
		goto out;
	}

	/* check the wait queue is populated */
	waiter = sem->wait_front;

	if (__builtin_expect(!waiter,0)) {
		printk("__rwsem_do_wake(): wait_list unexpectedly empty\n");
		BUG();
		goto out;
	}

	if (__builtin_expect(!waiter->flags,0)) {
		printk("__rwsem_do_wake(): wait_list front apparently not waiting\n");
		BUG();
		goto out;
	}

	next = NULL;

	/* try to grant a single write lock if there's a writer at the front of the queue
	 * - note we leave the 'active part' of the count incremented by 1 and the waiting part
	 *   incremented by 0x00010000
	 */
	if (waiter->flags & RWSEM_WAITING_FOR_WRITE) {
		next = waiter->next;
		waiter->flags = 0;
		wake_up_process(waiter->task);
		goto discard_woken_processes;
	}

	/* grant an infinite number of read locks to the readers at the front of the queue
	 * - note we increment the 'active part' of the count by the number of readers (less one
	 *   for the activity decrement we've already done) before waking any processes up
	 */
	woken = 0;
	do {
		woken++;
		waiter = waiter->next;
	} while (waiter && waiter->flags&RWSEM_WAITING_FOR_READ);

	loop = woken;
	woken *= RWSEM_ACTIVE_BIAS-RWSEM_WAITING_BIAS;
	woken -= RWSEM_ACTIVE_BIAS;
	rwsem_atomic_update(woken,sem);

	waiter = sem->wait_front;
	for (; loop>0; loop--) {
		next = waiter->next;
		waiter->flags = 0;
		wake_up_process(waiter->task);
		waiter = next;
	}

 discard_woken_processes:
	sem->wait_front = next;
	if (!next) sem->wait_back = &sem->wait_front;

 out:
	rwsemtrace(sem,"Leaving __rwsem_do_wake");
	return sem;
}

/*
 * wait for the read lock to be granted
 * - need to repeal the increment made inline by the caller
 * - need to throw a write-lock style spanner into the works (sub 0x00010000 from count)
 */
struct rw_semaphore *rwsem_down_read_failed(struct rw_semaphore *sem)
{
	struct rwsem_waiter waiter;
	struct task_struct *tsk = current;
	signed long count;

	rwsemtrace(sem,"Entering rwsem_down_read_failed");
	
	set_task_state(tsk,TASK_UNINTERRUPTIBLE);

	/* set up my own style of waitqueue */
	waiter.next = NULL;
	waiter.task = tsk;
	waiter.flags = RWSEM_WAITING_FOR_READ;

	spin_lock(&sem->wait_lock);

	*sem->wait_back = &waiter; /* add to back of queue */
	sem->wait_back = &waiter.next;

	/* note that we're now waiting on the lock, but no longer actively read-locking */
	count = rwsem_atomic_update(RWSEM_WAITING_BIAS-RWSEM_ACTIVE_BIAS,sem);

	/* if there are no longer active locks, wake the front queued process(es) up
	 * - it might even be this process, since the waker takes a more active part
	 */
	if (!(count & RWSEM_ACTIVE_MASK))
		__rwsem_do_wake(sem);

	spin_unlock(&sem->wait_lock);

	/* wait to be given the lock */
	for (;;) {
		if (!waiter.flags)
			break;
		schedule();
		set_task_state(tsk, TASK_UNINTERRUPTIBLE);
	}

	tsk->state = TASK_RUNNING;

	rwsemtrace(sem,"Leaving rwsem_down_read_failed");
	return sem;
}

/*
 * wait for the write lock to be granted
 */
struct rw_semaphore *rwsem_down_write_failed(struct rw_semaphore *sem)
{
	struct rwsem_waiter waiter;
	struct task_struct *tsk = current;
	signed long count;

	rwsemtrace(sem,"Entering rwsem_down_write_failed");

	set_task_state(tsk, TASK_UNINTERRUPTIBLE);

	/* set up my own style of waitqueue */
	waiter.next = NULL;
	waiter.task = tsk;
	waiter.flags = RWSEM_WAITING_FOR_WRITE;

	spin_lock(&sem->wait_lock);

	*sem->wait_back = &waiter; /* add to back of queue */
	sem->wait_back = &waiter.next;

	/* note that we're waiting on the lock, but no longer actively locking */
	count = rwsem_atomic_update(-RWSEM_ACTIVE_BIAS,sem);

	/* if there are no longer active locks, wake the front queued process(es) up
	 * - it might even be this process, since the waker takes a more active part
	 */
	if (!(count & RWSEM_ACTIVE_MASK))
		__rwsem_do_wake(sem);

	spin_unlock(&sem->wait_lock);

	/* wait to be given the lock */
	for (;;) {
		if (!waiter.flags)
			break;
		schedule();
		set_task_state(tsk, TASK_UNINTERRUPTIBLE);
	}

	tsk->state = TASK_RUNNING;

	rwsemtrace(sem,"Leaving rwsem_down_write_failed");
	return sem;
}

/*
 * spinlock grabbing wrapper for __rwsem_do_wake()
 */
struct rw_semaphore *rwsem_wake(struct rw_semaphore *sem)
{
	rwsemtrace(sem,"Entering rwsem_wake");

	spin_lock(&sem->wait_lock);

	sem = __rwsem_do_wake(sem);

	spin_unlock(&sem->wait_lock);

	rwsemtrace(sem,"Leaving rwsem_wake");
	return sem;
}

EXPORT_SYMBOL(rwsem_down_read_failed);
EXPORT_SYMBOL(rwsem_down_write_failed);
EXPORT_SYMBOL(rwsem_wake);
#if RWSEM_DEBUG
EXPORT_SYMBOL(rwsemtrace);
#endif
