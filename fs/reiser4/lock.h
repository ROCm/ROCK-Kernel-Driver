/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

/* Long term locking data structures. See lock.c for details. */

#ifndef __LOCK_H__
#define __LOCK_H__

#include "forward.h"
#include "debug.h"
#include "dformat.h"
#include "spin_macros.h"
#include "key.h"
#include "coord.h"
#include "type_safe_list.h"
#include "plugin/node/node.h"
#include "jnode.h"
#include "readahead.h"

#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/pagemap.h>	/* for PAGE_CACHE_SIZE */
#include <asm/atomic.h>
#include <asm/semaphore.h>

/* per-znode lock requests queue; list items are lock owner objects
   which want to lock given znode.

   Locking: protected by znode spin lock. */
TYPE_SAFE_LIST_DECLARE(requestors);
/* per-znode list of lock handles for this znode

   Locking: protected by znode spin lock. */
TYPE_SAFE_LIST_DECLARE(owners);
/* per-owner list of lock handles that point to locked znodes which
   belong to one lock owner

   Locking: this list is only accessed by the thread owning the lock stack this
   list is attached to. Hence, no locking is necessary.
*/
TYPE_SAFE_LIST_DECLARE(locks);

/* Per-znode lock object */
struct zlock {
	reiser4_rw_data guard;
	/* The number of readers if positive; the number of recursively taken
	   write locks if negative. Protected by zlock spin lock. */
	int nr_readers;
	/* A number of processes (lock_stacks) that have this object
	   locked with high priority */
	unsigned nr_hipri_owners;
	/* A number of attempts to lock znode in high priority direction */
	unsigned nr_hipri_requests;
	/* A linked list of lock_handle objects that contains pointers
	   for all lock_stacks which have this lock object locked */
	owners_list_head owners;
	/* A linked list of lock_stacks that wait for this lock */
	requestors_list_head requestors;
};

#define rw_ordering_pred_zlock(lock)			\
	  (lock_counters()->spin_locked_stack == 0)

/* Define spin_lock_zlock, spin_unlock_zlock, etc. */
RW_LOCK_FUNCTIONS(zlock, zlock, guard);

#define lock_is_locked(lock)          ((lock)->nr_readers != 0)
#define lock_is_rlocked(lock)         ((lock)->nr_readers > 0)
#define lock_is_wlocked(lock)         ((lock)->nr_readers < 0)
#define lock_is_wlocked_once(lock)    ((lock)->nr_readers == -1)
#define lock_can_be_rlocked(lock)     ((lock)->nr_readers >=0)
#define lock_mode_compatible(lock, mode) \
             (((mode) == ZNODE_WRITE_LOCK && !lock_is_locked(lock)) \
           || ((mode) == ZNODE_READ_LOCK && lock_can_be_rlocked(lock)))


/* Since we have R/W znode locks we need additional bidirectional `link'
   objects to implement n<->m relationship between lock owners and lock
   objects. We call them `lock handles'.

   Locking: see lock.c/"SHORT-TERM LOCKING"
*/
struct lock_handle {
	/* This flag indicates that a signal to yield a lock was passed to
	   lock owner and counted in owner->nr_signalled

	   Locking: this is accessed under spin lock on ->node.
	*/
	int signaled;
	/* A link to owner of a lock */
	lock_stack *owner;
	/* A link to znode locked */
	znode *node;
	/* A list of all locks for a process */
	locks_list_link locks_link;
	/* A list of all owners for a znode */
	owners_list_link owners_link;
};

typedef struct lock_request {
	/* A pointer to uninitialized link object */
	lock_handle *handle;
	/* A pointer to the object we want to lock */
	znode *node;
	/* Lock mode (ZNODE_READ_LOCK or ZNODE_WRITE_LOCK) */
	znode_lock_mode mode;
} lock_request;

/* A lock stack structure for accumulating locks owned by a process */
struct lock_stack {
	/* A guard lock protecting a lock stack */
	reiser4_spin_data sguard;
	/* number of znodes which were requested by high priority processes */
	atomic_t nr_signaled;
	/* Current priority of a process

	   This is only accessed by the current thread and thus requires no
	   locking.
	*/
	int curpri;
	/* A list of all locks owned by this process. Elements can be added to
	 * this list only by the current thread. ->node pointers in this list
	 * can be only changed by the current thread. */
	locks_list_head locks;
	int nr_locks; /* number of lock handles in the above list */
	/* When lock_stack waits for the lock, it puts itself on double-linked
	   requestors list of that lock */
	requestors_list_link requestors_link;
	/* Current lock request info.

	   This is only accessed by the current thread and thus requires no
	   locking.
	*/
	lock_request request;
	/* It is a lock_stack's synchronization object for when process sleeps
	   when requested lock not on this lock_stack but which it wishes to
	   add to this lock_stack is not immediately available. It is used
	   instead of wait_queue_t object due to locking problems (lost wake
	   up). "lost wakeup" occurs when process is waken up before he actually
	   becomes 'sleepy' (through sleep_on()). Using of semaphore object is
	   simplest way to avoid that problem.

	   A semaphore is used in the following way: only the process that is
	   the owner of the lock_stack initializes it (to zero) and calls
	   down(sema) on it. Usually this causes the process to sleep on the
	   semaphore. Other processes may wake him up by calling up(sema). The
	   advantage to a semaphore is that up() and down() calls are not
	   required to preserve order. Unlike wait_queue it works when process
	   is woken up before getting to sleep.

	   NOTE-NIKITA: Transaction manager is going to have condition variables
	   (&kcondvar_t) anyway, so this probably will be replaced with
	   one in the future.

	   After further discussion, Nikita has shown me that Zam's implementation is
	   exactly a condition variable.  The znode's {zguard,requestors_list} represents
	   condition variable and the lock_stack's {sguard,semaphore} guards entry and
	   exit from the condition variable's wait queue.  But the existing code can't
	   just be replaced with a more general abstraction, and I think its fine the way
	   it is. */
	struct semaphore sema;
};

/* defining of list manipulation functions for lists above */
TYPE_SAFE_LIST_DEFINE(requestors, lock_stack, requestors_link);
TYPE_SAFE_LIST_DEFINE(owners, lock_handle, owners_link);
TYPE_SAFE_LIST_DEFINE(locks, lock_handle, locks_link);

/*
  User-visible znode locking functions
*/

extern int longterm_lock_znode   (lock_handle * handle,
				  znode * node,
				  znode_lock_mode mode,
				  znode_lock_request request);

extern void longterm_unlock_znode(lock_handle * handle);

extern int check_deadlock(void);

extern lock_stack *get_current_lock_stack(void);

extern void init_lock_stack(lock_stack * owner);
extern void reiser4_init_lock(zlock * lock);

extern void init_lh(lock_handle *);
extern void move_lh(lock_handle * new, lock_handle * old);
extern void copy_lh(lock_handle * new, lock_handle * old);
extern void done_lh(lock_handle *);
extern znode_lock_mode lock_mode(lock_handle *);

extern int prepare_to_sleep(lock_stack * owner);

#if REISER4_STATS

#define ADD_TO_SLEPT_IN_WAIT_EVENT (-1)
#define ADD_TO_SLEPT_IN_WAIT_ATOM  (-2)

/* if REISER4_STATS __go_to_sleep() accepts additional parameter @level for
 * gathering per-level sleep statistics. The go_to_sleep wrapper hides the
 * __go_to_sleep() function prototypes difference. */
void __go_to_sleep(lock_stack*, int);
#define go_to_sleep(owner, level) __go_to_sleep(owner, level);

#else

void __go_to_sleep(lock_stack*);
#define go_to_sleep(owner, level) __go_to_sleep(owner)

#endif

extern void __reiser4_wake_up(lock_stack * owner);

extern int lock_stack_isclean(lock_stack * owner);

/* zlock object state check macros: only used in assertions.  Both forms imply that the
   lock is held by the current thread. */
extern int znode_is_write_locked(const znode * node);

#if REISER4_DEBUG
#define spin_ordering_pred_stack_addendum (1)
#else
#define spin_ordering_pred_stack_addendum		\
	 ((lock_counters()->rw_locked_dk == 0) &&	\
	  (lock_counters()->rw_locked_tree == 0))
#endif
/* lock ordering is: first take zlock spin lock, then lock stack spin lock */
#define spin_ordering_pred_stack(stack)				\
	((lock_counters()->spin_locked_stack == 0) &&		\
	 (lock_counters()->spin_locked_txnmgr == 0) &&		\
	 (lock_counters()->spin_locked_super == 0) &&		\
	 (lock_counters()->spin_locked_inode_object == 0) &&	\
	 (lock_counters()->rw_locked_cbk_cache == 0) &&	\
	 (lock_counters()->spin_locked_epoch == 0) &&		\
	 (lock_counters()->spin_locked_super_eflush == 0) &&	\
	 spin_ordering_pred_stack_addendum)

/* Same for lock_stack */
SPIN_LOCK_FUNCTIONS(stack, lock_stack, sguard);

static inline void
reiser4_wake_up(lock_stack * owner)
{
	spin_lock_stack(owner);
	__reiser4_wake_up(owner);
	spin_unlock_stack(owner);
}

const char *lock_mode_name(znode_lock_mode lock);

#if REISER4_DEBUG
extern void check_lock_data(void);
extern void check_lock_node_data(znode * node);
#else
#define check_lock_data() noop
#define check_lock_node_data() noop
#endif

/* __LOCK_H__ */
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
