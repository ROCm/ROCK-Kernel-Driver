/* Copyright 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

/* Declaration of kernel condition variables and API. See kcond.c for more
   info. */

#ifndef __KCOND_H__
#define __KCOND_H__

#include <linux/spinlock.h>
#include <asm/semaphore.h>

typedef struct kcond_queue_link_s kcond_queue_link_t;

/* condition variable */
typedef struct kcond_s {
	/* lock protecting integrity of @queue */
	spinlock_t lock;
	/* queue of waiters */
	kcond_queue_link_t *queue;
} kcond_t;

/* queue link added to the kcond->queue by each waiter */
struct kcond_queue_link_s {
	/* next link in the queue */
	kcond_queue_link_t *next;
	/* semaphore to signal on wake up */
	struct semaphore wait;
};

extern kcond_t *kcond_init(kcond_t * cvar);
extern int kcond_destroy(kcond_t * cvar);

extern int kcond_wait(kcond_t * cvar, spinlock_t * lock, int signl);
extern int kcond_timedwait(kcond_t * cvar, spinlock_t * lock, signed long timeout, int signl);
extern int kcond_signal(kcond_t * cvar);
extern int kcond_broadcast(kcond_t * cvar);

extern int kcond_are_waiters(kcond_t * cvar);

extern void kcond_print(kcond_t * cvar);

#define KCOND_STATIC_INIT			\
	{					\
		.lock = SPIN_LOCK_UNLOCKED,	\
		.queue = NULL			\
	}

/* __KCOND_H__ */
#endif

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
