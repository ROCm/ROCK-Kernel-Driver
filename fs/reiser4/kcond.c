/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

/* Kernel condition variables implementation.

   This is simplistic (90 LOC mod comments) condition variable
   implementation. Condition variable is the most natural "synchronization
   object" in some circumstances.

   Each CS text-book on multi-threading should discuss condition
   variables. Also see man/info for:

                   pthread_cond_init(3),
                   pthread_cond_destroy(3),
                   pthread_cond_signal(3),
                   pthread_cond_broadcast(3),
                   pthread_cond_wait(3),
                   pthread_cond_timedwait(3).

   See comments in kcond_wait().

   TODO

    1. Add an option (to kcond_init?) to make conditional variable async-safe
    so that signals and broadcasts can be done from interrupt
    handlers. Requires using spin_lock_irq in kcond_*().

    2. "Predicated" sleeps: add predicate function to the qlink and only wake
    sleeper if predicate is true. Probably requires additional parameters to
    the kcond_{signal,broadcast}() to supply cookie to the predicate. Standard
    wait_queues already have this functionality. Idea is that if one has
    object behaving like finite state automaton it is possible to use single
    per-object condition variable to signal all state transitions. Predicates
    allow waiters to select only transitions they are interested in without
    going through context switch.

    3. It is relatively easy to add support for sleeping on the several
    condition variables at once. Does anybody need this?

*/

#include "debug.h"
#include "kcond.h"
#include "spin_macros.h"

#include <linux/timer.h>
#include <linux/spinlock.h>

static void kcond_timeout(unsigned long datum);
static void kcond_remove(kcond_t * cvar, kcond_queue_link_t * link);

/* initialize condition variable. Initializer for global condition variables
   is macro in kcond.h  */
reiser4_internal kcond_t *
kcond_init(kcond_t * cvar /* cvar to init */ )
{
	assert("nikita-1868", cvar != NULL);

	xmemset(cvar, 0, sizeof *cvar);
	spin_lock_init(&cvar->lock);
	cvar->queue = NULL;
	return cvar;
}

/* destroy condition variable. */
reiser4_internal int
kcond_destroy(kcond_t * cvar /* cvar to destroy */ )
{
	return kcond_are_waiters(cvar) ? -EBUSY : 0;
}

/* Wait until condition variable is signalled. Call this with @lock locked.
   If @signl is true, then sleep on condition variable will be interruptible
   by signals. -EINTR is returned if sleep were interrupted by signal and 0
   otherwise.

   kcond_t is just a queue protected by spinlock. Whenever thread is going to
   sleep on the kcond_t it does the following:

    (1) prepares "queue link" @qlink which is semaphore constructed locally on
    the stack of the thread going to sleep.

    (2) takes @cvar spinlock

    (3) adds @qlink to the @cvar queue of waiters

    (4) releases @cvar spinlock

    (5) sleeps on semaphore constructed at step (1)

   When @cvar will be signalled or broadcasted all semaphors enqueued to the
   @cvar queue will be upped and kcond_wait() will return.

   By use of local semaphore for each waiter we avoid races between going to
   sleep and waking up---endemic plague of condition variables.

   For example, should kcond_broadcast() come in between steps (4) and (5) it
   would call up() on semaphores already in a queue and hence, down() in the
   step (5) would return immediately.

*/
reiser4_internal int
kcond_wait(kcond_t * cvar /* cvar to wait for */ ,
	   spinlock_t * lock /* lock to use */ ,
	   int signl /* if 0, ignore signals during sleep */ )
{
	kcond_queue_link_t qlink;
	int result;

	assert("nikita-1869", cvar != NULL);
	assert("nikita-1870", lock != NULL);
	assert("nikita-1871", check_spin_is_locked(lock));

	spin_lock(&cvar->lock);
	qlink.next = cvar->queue;
	cvar->queue = &qlink;
	init_MUTEX_LOCKED(&qlink.wait);
	spin_unlock(&cvar->lock);
	spin_unlock(lock);

	result = 0;
	if (signl)
		result = down_interruptible(&qlink.wait);
	else
		down(&qlink.wait);
	spin_lock(&cvar->lock);
	if (result != 0) {
		/* if thread was woken up by signal, @qlink is probably still
		   in the queue, remove it. */
		kcond_remove(cvar, &qlink);
	}
	/* if it wasn't woken up by signal, spinlock here is still useful,
	   because we want to wait until kcond_{broadcast|signal}
	   finishes. Otherwise down() could interleave with up() in such a way
	   that, that kcond_wait() would exit and up() would see garbage in a
	   semaphore.
	*/
	spin_unlock(&cvar->lock);
	spin_lock(lock);
	return result;
}

typedef struct {
	kcond_queue_link_t *link;
	int *woken_up;
} kcond_timer_arg;

/* like kcond_wait(), but with timeout */
reiser4_internal int
kcond_timedwait(kcond_t * cvar /* cvar to wait for */ ,
		spinlock_t * lock /* lock to use */ ,
		signed long timeout /* timeout in jiffies */ ,
		int signl /* if 0, ignore signals during sleep */ )
{
	struct timer_list timer;
	kcond_queue_link_t qlink;
	int result;
	int woken_up;
	kcond_timer_arg targ;

	assert("nikita-2437", cvar != NULL);
	assert("nikita-2438", lock != NULL);
	assert("nikita-2439", check_spin_is_locked(lock));

	spin_lock(&cvar->lock);
	qlink.next = cvar->queue;
	cvar->queue = &qlink;
	init_MUTEX_LOCKED(&qlink.wait);
	spin_unlock(&cvar->lock);
	spin_unlock(lock);

	assert("nikita-3011", schedulable());

	/* prepare timer */
	init_timer(&timer);
	timer.expires = jiffies + timeout;
	timer.data = (unsigned long) &targ;
	timer.function = kcond_timeout;

	woken_up = 0;

	targ.link = &qlink;
	targ.woken_up = &woken_up;

	/* ... and set it up */
	add_timer(&timer);

	result = 0;
	if (signl)
		result = down_interruptible(&qlink.wait);
	else
		down(&qlink.wait);

	/* cancel timer */
	del_timer_sync(&timer);

	if (woken_up)
		result = -ETIMEDOUT;

	spin_lock(&cvar->lock);
	if (result != 0) {
		/* if thread was woken up by signal, or due to time-out,
		   @qlink is probably still in the queue, remove it. */
		kcond_remove(cvar, &qlink);
	}
	spin_unlock(&cvar->lock);

	spin_lock(lock);
	return result;
}

/* Signal condition variable: wake up one waiter, if any. */
reiser4_internal int
kcond_signal(kcond_t * cvar /* cvar to signal */ )
{
	kcond_queue_link_t *queue_head;

	assert("nikita-1872", cvar != NULL);

	spin_lock(&cvar->lock);

	queue_head = cvar->queue;
	if (queue_head != NULL) {
		cvar->queue = queue_head->next;
		up(&queue_head->wait);
	}
	spin_unlock(&cvar->lock);
	return 1;
}

/* Broadcast condition variable: wake up all waiters. */
reiser4_internal int
kcond_broadcast(kcond_t * cvar /* cvar to broadcast */ )
{
	kcond_queue_link_t *queue_head;

	assert("nikita-1875", cvar != NULL);

	spin_lock(&cvar->lock);

	for (queue_head = cvar->queue; queue_head != NULL; queue_head = queue_head->next)
		up(&queue_head->wait);

	cvar->queue = NULL;
	spin_unlock(&cvar->lock);
	return 1;
}

/* true if there are threads sleeping on @cvar */
reiser4_internal int
kcond_are_waiters(kcond_t * cvar /* cvar to query */ )
{
	assert("nikita-1877", cvar != NULL);
	return cvar->queue != NULL;
}

/* timer expiration function used by kcond_timedwait */
static void
kcond_timeout(unsigned long datum)
{
	kcond_timer_arg *arg;

	arg = (kcond_timer_arg *) datum;
	*arg->woken_up = 1;
	up(&arg->link->wait);
}

/* helper function to remove @link from @cvar queue */
static void
kcond_remove(kcond_t * cvar /* cvar to operate on */ ,
	     kcond_queue_link_t * link /* link to remove */ )
{
	kcond_queue_link_t *scan;
	kcond_queue_link_t *prev;

	assert("nikita-2440", cvar != NULL);
	assert("nikita-2441", check_spin_is_locked(&cvar->lock));

	for (scan = cvar->queue, prev = NULL; scan != NULL; prev = scan, scan = scan->next) {
		if (scan == link) {
			if (prev == NULL)
				cvar->queue = scan->next;
			else
				prev->next = scan->next;
			break;
		}
	}
}

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   scroll-step: 1
   End:
*/
