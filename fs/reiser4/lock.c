/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by
 * reiser4/README */

/* Traditional deadlock avoidance is achieved by acquiring all locks in a single
   order.  V4 balances the tree from the bottom up, and searches the tree from
   the top down, and that is really the way we want it, so tradition won't work
   for us.

   Instead we have two lock orderings, a high priority lock ordering, and a low
   priority lock ordering.  Each node in the tree has a lock in its znode.

   Suppose we have a set of processes which lock (R/W) tree nodes. Each process
   has a set (maybe empty) of already locked nodes ("process locked set"). Each
   process may have a pending lock request to a node locked by another process.
   Note: we lock and unlock, but do not transfer locks: it is possible
   transferring locks instead would save some bus locking....

   Deadlock occurs when we have a loop constructed from process locked sets and
   lock request vectors.


   NOTE: The reiser4 "tree" is a tree on disk, but its cached representation in
   memory is extended with "znodes" with which we connect nodes with their left
   and right neighbors using sibling pointers stored in the znodes.  When we
   perform balancing operations we often go from left to right and from right to
   left.


   +-P1-+          +-P3-+
   |+--+|   V1     |+--+|
   ||N1|| -------> ||N3||
   |+--+|          |+--+|
   +----+          +----+
     ^               |
     |V2             |V3
     |               v
   +---------P2---------+
   |+--+            +--+|
   ||N2|  --------  |N4||
   |+--+            +--+|
   +--------------------+

   We solve this by ensuring that only low priority processes lock in top to
   bottom order and from right to left, and high priority processes lock from
   bottom to top and left to right.

   ZAM-FIXME-HANS: order not just node locks in this way, order atom locks, and
   kill those damn busy loops.
   ANSWER(ZAM): atom locks (which are introduced by ASTAGE_CAPTURE_WAIT atom
   stage) cannot be ordered that way. There are no rules what nodes can belong
   to the atom and what nodes cannot.  We cannot define what is right or left
   direction, what is top or bottom.  We can take immediate parent or side
   neighbor of one node, but nobody guarantees that, say, left neighbor node is
   not a far right neighbor for other nodes from the same atom.  It breaks
   deadlock avoidance rules and hi-low priority locking cannot be applied for
   atom locks.

   How does it help to avoid deadlocks ?

   Suppose we have a deadlock with n processes. Processes from one priority
   class never deadlock because they take locks in one consistent
   order.

   So, any possible deadlock loop must have low priority as well as high
   priority processes.  There are no other lock priority levels except low and
   high. We know that any deadlock loop contains at least one node locked by a
   low priority process and requested by a high priority process. If this
   situation is caught and resolved it is sufficient to avoid deadlocks.

   V4 DEADLOCK PREVENTION ALGORITHM IMPLEMENTATION.

   The deadlock prevention algorithm is based on comparing
   priorities of node owners (processes which keep znode locked) and
   requesters (processes which want to acquire a lock on znode).  We
   implement a scheme where low-priority owners yield locks to
   high-priority requesters. We created a signal passing system that
   is used to ask low-priority processes to yield one or more locked
   znodes.

   The condition when a znode needs to change its owners is described by the
   following formula:

   #############################################
   #                                           #
   # (number of high-priority requesters) >  0 #
   #                AND                        #
   # (numbers of high-priority owners)    == 0 #
   #                                           #
   #############################################

     Note that a low-priority process
     delays node releasing if another high-priority process owns this node.  So, slightly more strictly speaking, to have a deadlock capable cycle you must have a loop in which a high priority process is waiting on a low priority process to yield a node, which is slightly different from saying a high priority process is waiting on a node owned by a low priority process.

   It is enough to avoid deadlocks if we prevent any low-priority process from
   falling asleep if its locked set contains a node which satisfies the
   deadlock condition.

   That condition is implicitly or explicitly checked in all places where new
   high-priority requests may be added or removed from node request queue or
   high-priority process takes or releases a lock on node. The main
   goal of these checks is to never lose the moment when node becomes "has
   wrong owners" and send "must-yield-this-lock" signals to its low-pri owners
   at that time.

   The information about received signals is stored in the per-process
   structure (lock stack) and analyzed before a low-priority process goes to
   sleep but after a "fast" attempt to lock a node fails. Any signal wakes
   sleeping process up and forces him to re-check lock status and received
   signal info. If "must-yield-this-lock" signals were received the locking
   primitive (longterm_lock_znode()) fails with -E_DEADLOCK error code.

   V4 LOCKING DRAWBACKS

   If we have already balanced on one level, and we are propagating our changes upward to a higher level, it could be
   very messy to surrender all locks on the lower level because we put so much computational work into it, and reverting
   them to their state before they were locked might be very complex.  We also don't want to acquire all locks before
   performing balancing because that would either be almost as much work as the balancing, or it would be too
   conservative and lock too much.  We want balancing to be done only at high priority.  Yet, we might want to go to the
   left one node and use some of its empty space... So we make one attempt at getting the node to the left using
   try_lock, and if it fails we do without it, because we didn't really need it, it was only a nice to have.

   LOCK STRUCTURES DESCRIPTION

   The following data structures are used in the reiser4 locking
   implementation:

   All fields related to long-term locking are stored in znode->lock.

   The lock stack is a per thread object.  It owns all znodes locked by the
   thread. One znode may be locked by several threads in case of read lock or
   one znode may be write locked by one thread several times. The special link
   objects (lock handles) support n<->m relation between znodes and lock
   owners.

   <Thread 1>                       <Thread 2>

   +---------+                     +---------+
   |  LS1    |		           |  LS2    |
   +---------+			   +---------+
       ^                                ^
       |---------------+                +----------+
       v               v                v          v
   +---------+      +---------+    +---------+   +---------+
   |  LH1    |      |   LH2   |	   |  LH3    |   |   LH4   |
   +---------+	    +---------+	   +---------+   +---------+
       ^                   ^            ^           ^
       |                   +------------+           |
       v                   v                        v
   +---------+      +---------+                  +---------+
   |  Z1     |	    |	Z2    |                  |  Z3     |
   +---------+	    +---------+                  +---------+

   Thread 1 locked znodes Z1 and Z2, thread 2 locked znodes Z2 and Z3. The picture above shows that lock stack LS1 has a
   list of 2 lock handles LH1 and LH2, lock stack LS2 has a list with lock handles LH3 and LH4 on it.  Znode Z1 is
   locked by only one thread, znode has only one lock handle LH1 on its list, similar situation is for Z3 which is
   locked by the thread 2 only. Z2 is locked (for read) twice by different threads and two lock handles are on its
   list. Each lock handle represents a single relation of a locking of a znode by a thread. Locking of a znode is an
   establishing of a locking relation between the lock stack and the znode by adding of a new lock handle to a list of
   lock handles, the lock stack.  The lock stack links all lock handles for all znodes locked by the lock stack.  The znode
   list groups all lock handles for all locks stacks which locked the znode.

   Yet another relation may exist between znode and lock owners.  If lock
   procedure cannot immediately take lock on an object it adds the lock owner
   on special `requestors' list belongs to znode.  That list represents a
   queue of pending lock requests.  Because one lock owner may request only
   only one lock object at a time, it is a 1->n relation between lock objects
   and a lock owner implemented as it is described above. Full information
   (priority, pointers to lock and link objects) about each lock request is
   stored in lock owner structure in `request' field.

   SHORT_TERM LOCKING

   This is a list of primitive operations over lock stacks / lock handles /
   znodes and locking descriptions for them.

   1. locking / unlocking which is done by two list insertion/deletion, one
      to/from znode's list of lock handles, another one is to/from lock stack's
      list of lock handles.  The first insertion is protected by
      znode->lock.guard spinlock.  The list owned by the lock stack can be
      modified only by thread who owns the lock stack and nobody else can
      modify/read it. There is nothing to be protected by a spinlock or
      something else.

   2. adding/removing a lock request to/from znode requesters list. The rule is
      that znode->lock.guard spinlock should be taken for this.

   3. we can traverse list of lock handles and use references to lock stacks who
      locked given znode if znode->lock.guard spinlock is taken.

   4. If a lock stack is associated with a znode as a lock requestor or lock
      owner its existence is guaranteed by znode->lock.guard spinlock.  Some its
      (lock stack's) fields should be protected from being accessed in parallel
      by two or more threads. Please look at  lock_stack structure definition
      for the info how those fields are protected. */

/* Znode lock and capturing intertwining. */
/* In current implementation we capture formatted nodes before locking
   them. Take a look on longterm lock znode, try_capture() request precedes
   locking requests.  The longterm_lock_znode function unconditionally captures
   znode before even checking of locking conditions.

   Another variant is to capture znode after locking it.  It was not tested, but
   at least one deadlock condition is supposed to be there.  One thread has
   locked a znode (Node-1) and calls try_capture() for it.  Try_capture() sleeps
   because znode's atom has CAPTURE_WAIT state.  Second thread is a flushing
   thread, its current atom is the atom Node-1 belongs to. Second thread wants
   to lock Node-1 and sleeps because Node-1 is locked by the first thread.  The
   described situation is a deadlock. */

#include "debug.h"
#include "txnmgr.h"
#include "znode.h"
#include "jnode.h"
#include "tree.h"
#include "plugin/node/node.h"
#include "super.h"

#include <linux/spinlock.h>

#if REISER4_DEBUG
static int request_is_deadlock_safe(znode *, znode_lock_mode,
				    znode_lock_request);
#endif

#define ADDSTAT(node, counter) 						\
	reiser4_stat_inc_at_level(znode_get_level(node), znode.counter)

/* Returns a lock owner associated with current thread */
reiser4_internal lock_stack *
get_current_lock_stack(void)
{
	return &get_current_context()->stack;
}

/* Wakes up all low priority owners informing them about possible deadlock */
static void
wake_up_all_lopri_owners(znode * node)
{
	lock_handle *handle;

	assert("nikita-1824", rw_zlock_is_locked(&node->lock));
	for_all_type_safe_list(owners, &node->lock.owners, handle) {
		spin_lock_stack(handle->owner);

		assert("nikita-1832", handle->node == node);
		/* count this signal in owner->nr_signaled */
		if (!handle->signaled) {
			handle->signaled = 1;
			atomic_inc(&handle->owner->nr_signaled);
		}
		/* Wake up a single process */
		__reiser4_wake_up(handle->owner);

		spin_unlock_stack(handle->owner);
	}
}

/* Adds a lock to a lock owner, which means creating a link to the lock and
   putting the link into the two lists all links are on (the doubly linked list
   that forms the lock_stack, and the doubly linked list of links attached
   to a lock.
*/
static inline void
link_object(lock_handle * handle, lock_stack * owner, znode * node)
{
	assert("jmacd-810", handle->owner == NULL);
	assert("nikita-1828", owner == get_current_lock_stack());
	assert("nikita-1830", rw_zlock_is_locked(&node->lock));

	handle->owner = owner;
	handle->node = node;

	assert("reiser4-4", ergo(locks_list_empty(&owner->locks), owner->nr_locks == 0));
	locks_list_push_back(&owner->locks, handle);
	owner->nr_locks ++;

	owners_list_push_front(&node->lock.owners, handle);
	handle->signaled = 0;
}

/* Breaks a relation between a lock and its owner */
static inline void
unlink_object(lock_handle * handle)
{
	assert("zam-354", handle->owner != NULL);
	assert("nikita-1608", handle->node != NULL);
	assert("nikita-1633", rw_zlock_is_locked(&handle->node->lock));
	assert("nikita-1829", handle->owner == get_current_lock_stack());

	assert("reiser4-5", handle->owner->nr_locks > 0);
	locks_list_remove_clean(handle);
	handle->owner->nr_locks --;
	assert("reiser4-6", ergo(locks_list_empty(&handle->owner->locks), handle->owner->nr_locks == 0));

	owners_list_remove_clean(handle);

	/* indicates that lock handle is free now */
	handle->owner = NULL;
}

/* Actually locks an object knowing that we are able to do this */
static void
lock_object(lock_stack * owner)
{
	lock_request *request;
	znode        *node;
	assert("nikita-1839", owner == get_current_lock_stack());

	request = &owner->request;
	node    = request->node;
	assert("nikita-1834", rw_zlock_is_locked(&node->lock));
	if (request->mode == ZNODE_READ_LOCK) {
		node->lock.nr_readers++;
	} else {
		/* check that we don't switched from read to write lock */
		assert("nikita-1840", node->lock.nr_readers <= 0);
		/* We allow recursive locking; a node can be locked several
		   times for write by same process */
		node->lock.nr_readers--;
	}

	link_object(request->handle, owner, node);

	if (owner->curpri) {
		node->lock.nr_hipri_owners++;
	}
	ON_TRACE(TRACE_LOCKS,
		 "%spri lock: %p node: %p: hipri_owners: %u: nr_readers: %d\n",
		 owner->curpri ? "hi" : "lo", owner, node, node->lock.nr_hipri_owners, node->lock.nr_readers);
}

/* Check for recursive write locking */
static int
recursive(lock_stack * owner)
{
	int ret;
	znode *node;

	node = owner->request.node;

	/* Owners list is not empty for a locked node */
	assert("zam-314", !owners_list_empty(&node->lock.owners));
	assert("nikita-1841", owner == get_current_lock_stack());
	assert("nikita-1848", rw_zlock_is_locked(&node->lock));

	ret = (owners_list_front(&node->lock.owners)->owner == owner);

	/* Recursive read locking should be done usual way */
	assert("zam-315", !ret || owner->request.mode == ZNODE_WRITE_LOCK);
	/* mixing of read/write locks is not allowed */
	assert("zam-341", !ret || znode_is_wlocked(node));

	return ret;
}

#if REISER4_DEBUG
/* Returns true if the lock is held by the calling thread. */
int
znode_is_any_locked(const znode * node)
{
	lock_handle *handle;
	lock_stack *stack;
	int ret;

	if (!znode_is_locked(node)) {
		return 0;
	}

	stack = get_current_lock_stack();

	spin_lock_stack(stack);

	ret = 0;

	for_all_type_safe_list(locks, &stack->locks, handle) {
		if (handle->node == node) {
			ret = 1;
			break;
		}
	}

	spin_unlock_stack(stack);

	return ret;
}

#endif

/* Returns true if a write lock is held by the calling thread. */
reiser4_internal int
znode_is_write_locked(const znode * node)
{
	lock_stack *stack;
	lock_handle *handle;

	assert("jmacd-8765", node != NULL);

	if (!znode_is_wlocked(node)) {
		return 0;
	}

	stack = get_current_lock_stack();

	/* If it is write locked, then all owner handles must equal the current stack. */
	handle = owners_list_front(&node->lock.owners);

	return (handle->owner == stack);
}

/* This "deadlock" condition is the essential part of reiser4 locking
   implementation. This condition is checked explicitly by calling
   check_deadlock_condition() or implicitly in all places where znode lock
   state (set of owners and request queue) is changed. Locking code is
   designed to use this condition to trigger procedure of passing object from
   low priority owner(s) to high priority one(s).

   The procedure results in passing an event (setting lock_handle->signaled
   flag) and counting this event in nr_signaled field of owner's lock stack
   object and wakeup owner's process.
*/
static inline int
check_deadlock_condition(znode * node)
{
	assert("nikita-1833", rw_zlock_is_locked(&node->lock));
	return node->lock.nr_hipri_requests > 0 && node->lock.nr_hipri_owners == 0;
}

/* checks lock/request compatibility */
static int
check_lock_object(lock_stack * owner)
{
	znode *node = owner->request.node;

	assert("nikita-1842", owner == get_current_lock_stack());
	assert("nikita-1843", rw_zlock_is_locked(&node->lock));

	/* See if the node is disconnected. */
	if (unlikely(ZF_ISSET(node, JNODE_IS_DYING))) {
		ON_TRACE(TRACE_LOCKS, "attempt to lock dying znode: %p", node);
		return RETERR(-EINVAL);
	}

	/* Do not ever try to take a lock if we are going in low priority
	   direction and a node have a high priority request without high
	   priority owners. */
	if (unlikely(!owner->curpri && check_deadlock_condition(node))) {
		return RETERR(-E_REPEAT);
	}

	if (unlikely(!is_lock_compatible(node, owner->request.mode))) {
		return RETERR(-E_REPEAT);
	}

	return 0;
}

/* check for lock/request compatibility and update tree statistics */
static int
can_lock_object(lock_stack * owner)
{
	int result;
	znode *node = owner->request.node;

	result = check_lock_object(owner);
	if (REISER4_STATS && znode_get_level(node) > 0) {
		if (result != 0)
			ADDSTAT(node, lock_contented);
		else
			ADDSTAT(node, lock_uncontented);
	}
	return result;
}

/* Setting of a high priority to the process. It clears "signaled" flags
   because znode locked by high-priority process can't satisfy our "deadlock
   condition". */
static void
set_high_priority(lock_stack * owner)
{
	assert("nikita-1846", owner == get_current_lock_stack());
	/* Do nothing if current priority is already high */
	if (!owner->curpri) {
		/* We don't need locking for owner->locks list, because, this
		 * function is only called with the lock stack of the current
		 * thread, and no other thread can play with owner->locks list
		 * and/or change ->node pointers of lock handles in this list.
		 *
		 * (Interrupts also are not involved.)
		 */
		lock_handle *item = locks_list_front(&owner->locks);
		while (!locks_list_end(&owner->locks, item)) {
			znode *node = item->node;

			WLOCK_ZLOCK(&node->lock);

			node->lock.nr_hipri_owners++;

			ON_TRACE(TRACE_LOCKS,
				 "set_hipri lock: %p node: %p: hipri_owners after: %u nr_readers: %d\n",
				 item, node, node->lock.nr_hipri_owners, node->lock.nr_readers);

			/* we can safely set signaled to zero, because
			   previous statement (nr_hipri_owners ++) guarantees
			   that signaled will be never set again. */
			item->signaled = 0;
			WUNLOCK_ZLOCK(&node->lock);

			item = locks_list_next(item);
		}
		owner->curpri = 1;
		atomic_set(&owner->nr_signaled, 0);
	}
}

/* Sets a low priority to the process. */
static void
set_low_priority(lock_stack * owner)
{
	assert("nikita-3075", owner == get_current_lock_stack());
	/* Do nothing if current priority is already low */
	if (owner->curpri) {
		/* scan all locks (lock handles) held by @owner, which is
		   actually current thread, and check whether we are reaching
		   deadlock possibility anywhere.
		*/
		lock_handle *handle = locks_list_front(&owner->locks);
		while (!locks_list_end(&owner->locks, handle)) {
			znode *node = handle->node;
			WLOCK_ZLOCK(&node->lock);
			/* this thread just was hipri owner of @node, so
			   nr_hipri_owners has to be greater than zero. */
			ON_TRACE(TRACE_LOCKS,
				 "set_lopri lock: %p node: %p: hipri_owners before: %u nr_readers: %d\n",
				 handle, node, node->lock.nr_hipri_owners, node->lock.nr_readers);
			assert("nikita-1835", node->lock.nr_hipri_owners > 0);
			node->lock.nr_hipri_owners--;
			/* If we have deadlock condition, adjust a nr_signaled
			   field. It is enough to set "signaled" flag only for
			   current process, other low-pri owners will be
			   signaled and waken up after current process unlocks
			   this object and any high-priority requestor takes
			   control. */
			if (check_deadlock_condition(node)
			    && !handle->signaled) {
				handle->signaled = 1;
				atomic_inc(&owner->nr_signaled);
			}
			WUNLOCK_ZLOCK(&node->lock);
			handle = locks_list_next(handle);
		}
		owner->curpri = 0;
	}
}

#define MAX_CONVOY_SIZE ((NR_CPUS - 1))

/* helper function used by longterm_unlock_znode() to wake up requestor(s). */
/*
 * In certain multi threaded work loads jnode spin lock is the most
 * contented one. Wake up of threads waiting for znode is, thus,
 * important to do right. There are three well known strategies:
 *
 *  (1) direct hand-off. Hasn't been tried.
 *
 *  (2) wake all (thundering herd). This degrades performance in our
 *      case.
 *
 *  (3) wake one. Simplest solution where requestor in the front of
 *      requestors list is awaken under znode spin lock is not very
 *      good on the SMP, because first thing requestor will try to do
 *      after waking up on another CPU is to acquire znode spin lock
 *      that is still held by this thread. As an optimization we grab
 *      lock stack spin lock, release znode spin lock and wake
 *      requestor. done_context() synchronize against stack spin lock
 *      to avoid (impossible) case where requestor has been waked by
 *      some other thread (wake_up_all_lopri_owners(), or something
 *      similar) and managed to exit before we waked it up.
 *
 *      Effect of this optimization wasn't big, after all.
 *
 */
static void
wake_up_requestor(znode *node)
{
#if NR_CPUS > 2
	requestors_list_head *creditors;
	lock_stack           *convoy[MAX_CONVOY_SIZE];
	int                   convoyused;
	int                   convoylimit;

	assert("nikita-3180", node != NULL);
	assert("nikita-3181", rw_zlock_is_locked(&node->lock));

	ADDSTAT(node, wakeup);

	convoyused = 0;
	convoylimit = min(num_online_cpus() - 1, MAX_CONVOY_SIZE);
	creditors = &node->lock.requestors;
	if (!requestors_list_empty(creditors)) {
		convoy[0] = requestors_list_front(creditors);
		convoyused = 1;
		ADDSTAT(node, wakeup_found);
		/*
		 * it has been verified experimentally, that there are no
		 * convoys on the leaf level.
		 */
		if (znode_get_level(node) != LEAF_LEVEL &&
		    convoy[0]->request.mode == ZNODE_READ_LOCK &&
		    convoylimit > 1) {
			lock_stack *item;

			ADDSTAT(node, wakeup_found_read);
			for (item = requestors_list_next(convoy[0]);
			          ! requestors_list_end(creditors, item);
			     item = requestors_list_next(item)) {
				ADDSTAT(node, wakeup_scan);
				if (item->request.mode == ZNODE_READ_LOCK) {
					ADDSTAT(node, wakeup_convoy);
					convoy[convoyused] = item;
					++ convoyused;
					/*
					 * it is safe to spin lock multiple
					 * lock stacks here, because lock
					 * stack cannot sleep on more than one
					 * requestors queue.
					 */
					/*
					 * use raw spin_lock in stead of macro
					 * wrappers, because spin lock
					 * profiling code cannot cope with so
					 * many locks held at the same time.
					 */
					spin_lock(&item->sguard.lock);
					if (convoyused == convoylimit)
						break;
				}
			}
		}
		spin_lock(&convoy[0]->sguard.lock);
	}

	WUNLOCK_ZLOCK(&node->lock);

	while (convoyused > 0) {
		-- convoyused;
		__reiser4_wake_up(convoy[convoyused]);
		spin_unlock(&convoy[convoyused]->sguard.lock);
	}
#else
	/* uniprocessor case: keep it simple */
	if (!requestors_list_empty(&node->lock.requestors)) {
		lock_stack *requestor;

		requestor = requestors_list_front(&node->lock.requestors);
		reiser4_wake_up(requestor);
	}

	WUNLOCK_ZLOCK(&node->lock);
#endif
}

#undef MAX_CONVOY_SIZE

/* release long-term lock, acquired by longterm_lock_znode() */
reiser4_internal void
longterm_unlock_znode(lock_handle * handle)
{
	znode *node = handle->node;
	lock_stack *oldowner = handle->owner;
	int hipri;
	int readers;
	int rdelta;
	int youdie;

	/*
	 * this is time-critical and highly optimized code. Modify carefully.
	 */

	assert("jmacd-1021", handle != NULL);
	assert("jmacd-1022", handle->owner != NULL);
	assert("nikita-1392", LOCK_CNT_GTZ(long_term_locked_znode));

	assert("zam-130", oldowner == get_current_lock_stack());

	LOCK_CNT_DEC(long_term_locked_znode);

	ADDSTAT(node, unlock);

	/*
	 * to minimize amount of operations performed under lock, pre-compute
	 * all variables used within critical section. This makes code
	 * obscure.
	 */

	/* was this lock of hi or lo priority */
	hipri   = oldowner->curpri ? -1 : 0;
	/* number of readers */
	readers = node->lock.nr_readers;
	/* +1 if write lock, -1 if read lock */
	rdelta  = (readers > 0) ? -1 : +1;
	/* true if node is to die and write lock is released */
	youdie  = ZF_ISSET(node, JNODE_HEARD_BANSHEE) && (readers < 0);

	WLOCK_ZLOCK(&node->lock);

	assert("zam-101", znode_is_locked(node));

	/* Adjust a number of high priority owners of this lock */
	node->lock.nr_hipri_owners += hipri;
	assert("nikita-1836", node->lock.nr_hipri_owners >= 0);

	ON_TRACE(TRACE_LOCKS,
		 "%spri unlock: %p node: %p: hipri_owners: %u nr_readers %d\n",
		 oldowner->curpri ? "hi" : "lo",
		 handle,
		 node,
		 node->lock.nr_hipri_owners,
		 node->lock.nr_readers);

	/* Handle znode deallocation on last write-lock release. */
	if (znode_is_wlocked_once(node)) {
		if (youdie) {
			forget_znode(handle);
			assert("nikita-2191", znode_invariant(node));
			zput(node);
			return;
		}
		znode_post_write(node);
	}
	if (znode_is_rlocked(node))
		ON_STATS(znode_at_read(node));

	if (handle->signaled)
		atomic_dec(&oldowner->nr_signaled);

	/* Unlocking means owner<->object link deletion */
	unlink_object(handle);

	/* This is enough to be sure whether an object is completely
	   unlocked. */
	node->lock.nr_readers += rdelta;

	/* If the node is locked it must have an owners list.  Likewise, if
	   the node is unlocked it must have an empty owners list. */
	assert("zam-319", equi(znode_is_locked(node),
			       !owners_list_empty(&node->lock.owners)));

#if REISER4_DEBUG
	if (!znode_is_locked(node))
		++ node->times_locked;
#endif

	/* If there are pending lock requests we wake up a requestor */
	if (!znode_is_wlocked(node))
		wake_up_requestor(node);
	else
		WUNLOCK_ZLOCK(&node->lock);

	assert("nikita-3182", rw_zlock_is_not_locked(&node->lock));
	/* minus one reference from handle->node */
	handle->node = NULL;
	assert("nikita-2190", znode_invariant(node));
	ON_DEBUG(check_lock_data());
	ON_DEBUG(check_lock_node_data(node));
	zput(node);
}

/* final portion of longterm-unlock*/
static int
lock_tail(lock_stack *owner, int wake_up_next, int ok, znode_lock_mode mode)
{
	znode *node = owner->request.node;

	assert("jmacd-807", rw_zlock_is_locked(&node->lock));

	/* If we broke with (ok == 0) it means we can_lock, now do it. */
	if (ok == 0) {
		lock_object(owner);
		owner->request.mode = 0;
		if (mode == ZNODE_READ_LOCK)
			wake_up_next = 1;
		if (REISER4_DEBUG_MODIFY) {
			if (znode_is_wlocked_once(node))
				znode_post_write(node);
			else if (znode_is_rlocked(node))
				ON_STATS(znode_at_read(node));
		}
	}

	if (wake_up_next)
		wake_up_requestor(node);
	else
		WUNLOCK_ZLOCK(&node->lock);

	if (ok == 0) {
		/* count a reference from lockhandle->node

		   znode was already referenced at the entry to this function,
		   hence taking spin-lock here is not necessary (see comment
		   in the zref()).
		*/
		zref(node);

		LOCK_CNT_INC(long_term_locked_znode);
		if (REISER4_DEBUG_NODE && mode == ZNODE_WRITE_LOCK) {
			node_check(node, 0);
			ON_DEBUG_MODIFY(znode_pre_write(node));
		}
	}

	ON_DEBUG(check_lock_data());
	ON_DEBUG(check_lock_node_data(node));
	return ok;
}

/*
 * version of longterm_znode_lock() optimized for the most common case: read
 * lock without any special flags. This is the kind of lock that any tree
 * traversal takes on the root node of the tree, which is very frequent.
 */
static int
longterm_lock_tryfast(lock_stack * owner)
{
	int          result;
	int          wake_up_next      = 0;
	znode       *node;
	zlock       *lock;

	node = owner->request.node;
	lock = &node->lock;

	assert("nikita-3340", schedulable());
	assert("nikita-3341", request_is_deadlock_safe(node,
						       ZNODE_READ_LOCK,
						       ZNODE_LOCK_LOPRI));

	result = UNDER_RW(zlock, lock, read, can_lock_object(owner));

	if (likely(result != -EINVAL)) {
		spin_lock_znode(node);
		result = try_capture(
			ZJNODE(node), ZNODE_READ_LOCK, 0, 1/* can copy on capture */);
		spin_unlock_znode(node);
		WLOCK_ZLOCK(lock);
		if (unlikely(result != 0)) {
			owner->request.mode = 0;
			wake_up_next = 1;
		} else {
			result = can_lock_object(owner);
			if (unlikely(result == -E_REPEAT)) {
				/* fall back to longterm_lock_znode() */
				WUNLOCK_ZLOCK(lock);
				return 1;
			}
		}
		return lock_tail(owner, wake_up_next, result, ZNODE_READ_LOCK);
	} else
		return 1;
}

/* locks given lock object */
reiser4_internal int
longterm_lock_znode(
	/* local link object (allocated by lock owner thread, usually on its own
	 * stack) */
	lock_handle * handle,
	/* znode we want to lock. */
	znode * node,
	/* {ZNODE_READ_LOCK, ZNODE_WRITE_LOCK}; */
	znode_lock_mode mode,
	/* {0, -EINVAL, -E_DEADLOCK}, see return codes description. */
	znode_lock_request request)
{
	int          ret;
	int          hipri             = (request & ZNODE_LOCK_HIPRI) != 0;
	int          wake_up_next      = 0;
	int          non_blocking      = 0;
	int          has_atom;
	txn_capture  cap_flags;
	zlock       *lock;
	txn_handle  *txnh;
	tree_level   level;

	/* Get current process context */
	lock_stack *owner = get_current_lock_stack();

	/* Check that the lock handle is initialized and isn't already being
	 * used. */
	assert("jmacd-808", handle->owner == NULL);
	assert("nikita-3026", schedulable());
	assert("nikita-3219", request_is_deadlock_safe(node, mode, request));
	/* long term locks are not allowed in the VM contexts (->writepage(),
	 * prune_{d,i}cache()).
	 *
	 * FIXME this doesn't work due to unused-dentry-with-unlinked-inode
	 * bug caused by d_splice_alias() only working for directories.
	 */
	assert("nikita-3547", 1 || ((current->flags & PF_MEMALLOC) == 0));

	cap_flags = 0;
	if (request & ZNODE_LOCK_NONBLOCK) {
		cap_flags |= TXN_CAPTURE_NONBLOCKING;
		non_blocking = 1;
	}

	if (request & ZNODE_LOCK_DONT_FUSE)
		cap_flags |= TXN_CAPTURE_DONT_FUSE;

	/* If we are changing our process priority we must adjust a number
	   of high priority owners for each znode that we already lock */
	if (hipri) {
		set_high_priority(owner);
	} else {
		set_low_priority(owner);
	}

	level = znode_get_level(node);
	ADDSTAT(node, lock);

	/* Fill request structure with our values. */
	owner->request.mode = mode;
	owner->request.handle = handle;
	owner->request.node = node;

	txnh = get_current_context()->trans;
	lock = &node->lock;

	if (mode == ZNODE_READ_LOCK && request == 0) {
		ret = longterm_lock_tryfast(owner);
		if (ret <= 0)
			return ret;
	}

	has_atom = (txnh->atom != NULL);

	/* update statistics */
	if (REISER4_STATS) {
		if (mode == ZNODE_READ_LOCK)
			ADDSTAT(node, lock_read);
		else
			ADDSTAT(node, lock_write);

		if (hipri)
			ADDSTAT(node, lock_hipri);
		else
			ADDSTAT(node, lock_lopri);
	}

	/* Synchronize on node's zlock guard lock. */
	WLOCK_ZLOCK(lock);

	if (znode_is_locked(node) &&
	    mode == ZNODE_WRITE_LOCK && recursive(owner))
		return lock_tail(owner, 0, 0, mode);

	for (;;) {
		ADDSTAT(node, lock_iteration);

		/* Check the lock's availability: if it is unavaiable we get
		   E_REPEAT, 0 indicates "can_lock", otherwise the node is
		   invalid.  */
		ret = can_lock_object(owner);

		if (unlikely(ret == -EINVAL)) {
			/* @node is dying. Leave it alone. */
			/* wakeup next requestor to support lock invalidating */
			wake_up_next = 1;
			ADDSTAT(node, lock_dying);
			break;
		}

		if (unlikely(ret == -E_REPEAT && non_blocking)) {
			/* either locking of @node by the current thread will
			 * lead to the deadlock, or lock modes are
			 * incompatible. */
			ADDSTAT(node, lock_cannot_lock);
			break;
		}

		assert("nikita-1844", (ret == 0) || ((ret == -E_REPEAT) && !non_blocking));
		/* If we can get the lock... Try to capture first before
		   taking the lock.*/

		/* first handle commonest case where node and txnh are already
		 * in the same atom. */
		/* safe to do without taking locks, because:
		 *
		 * 1. read of aligned word is atomic with respect to writes to
		 * this word
		 *
		 * 2. false negatives are handled in try_capture().
		 *
		 * 3. false positives are impossible.
		 *
		 * PROOF: left as an exercise to the curious reader.
		 *
		 * Just kidding. Here is one:
		 *
		 * At the time T0 txnh->atom is stored in txnh_atom.
		 *
		 * At the time T1 node->atom is stored in node_atom.
		 *
		 * At the time T2 we observe that
		 *
		 *     txnh_atom != NULL && node_atom == txnh_atom.
		 *
		 * Imagine that at this moment we acquire node and txnh spin
		 * lock in this order. Suppose that under spin lock we have
		 *
		 *     node->atom != txnh->atom,                       (S1)
		 *
		 * at the time T3.
		 *
		 * txnh->atom != NULL still, because txnh is open by the
		 * current thread.
		 *
		 * Suppose node->atom == NULL, that is, node was un-captured
		 * between T1, and T3. But un-capturing of formatted node is
		 * always preceded by the call to invalidate_lock(), which
		 * marks znode as JNODE_IS_DYING under zlock spin
		 * lock. Contradiction, because can_lock_object() above checks
		 * for JNODE_IS_DYING. Hence, node->atom != NULL at T3.
		 *
		 * Suppose that node->atom != node_atom, that is, atom, node
		 * belongs to was fused into another atom: node_atom was fused
		 * into node->atom. Atom of txnh was equal to node_atom at T2,
		 * which means that under spin lock, txnh->atom == node->atom,
		 * because txnh->atom can only follow fusion
		 * chain. Contradicts S1.
		 *
		 * The same for hypothesis txnh->atom != txnh_atom. Hence,
		 * node->atom == node_atom == txnh_atom == txnh->atom. Again
		 * contradicts S1. Hence S1 is false. QED.
		 *
		 */

		if (likely(has_atom && ZJNODE(node)->atom == txnh->atom)) {
			ADDSTAT(node, lock_no_capture);
		} else {
			/*
			 * unlock zlock spin lock here. It is possible for
			 * longterm_unlock_znode() to sneak in here, but there
			 * is no harm: invalidate_lock() will mark znode as
			 * JNODE_IS_DYING and this will be noted by
			 * can_lock_object() below.
			 */
			WUNLOCK_ZLOCK(lock);
			spin_lock_znode(node);
			ret = try_capture(
				ZJNODE(node), mode, cap_flags, 1/* can copy on capture*/);
			spin_unlock_znode(node);
			WLOCK_ZLOCK(lock);
			if (unlikely(ret != 0)) {
				/* In the failure case, the txnmgr releases
				   the znode's lock (or in some cases, it was
				   released a while ago).  There's no need to
				   reacquire it so we should return here,
				   avoid releasing the lock. */
				owner->request.mode = 0;
				/* next requestor may not fail */
				wake_up_next = 1;
				break;
			}

			/* Check the lock's availability again -- this is
			   because under some circumstances the capture code
			   has to release and reacquire the znode spinlock. */
			ret = can_lock_object(owner);
		}

		/* This time, a return of (ret == 0) means we can lock, so we
		   should break out of the loop. */
		if (likely(ret != -E_REPEAT || non_blocking)) {
			ADDSTAT(node, lock_can_lock);
			break;
		}

		/* Lock is unavailable, we have to wait. */

		/* By having semaphore initialization here we cannot lose
		   wakeup signal even if it comes after `nr_signaled' field
		   check. */
		ret = prepare_to_sleep(owner);
		if (unlikely(ret != 0)) {
			break;
		}

		assert("nikita-1837", rw_zlock_is_locked(&node->lock));
		if (hipri) {
			/* If we are going in high priority direction then
			   increase high priority requests counter for the
			   node */
			lock->nr_hipri_requests++;
			/* If there are no high priority owners for a node,
			   then immediately wake up low priority owners, so
			   they can detect possible deadlock */
			if (lock->nr_hipri_owners == 0)
				wake_up_all_lopri_owners(node);
			/* And prepare a lock request */
			requestors_list_push_front(&lock->requestors, owner);
		} else {
			/* If we are going in low priority direction then we
			   set low priority to our process. This is the only
			   case  when a process may become low priority */
			/* And finally prepare a lock request */
			requestors_list_push_back(&lock->requestors, owner);
		}

		/* Ok, here we have prepared a lock request, so unlock
		   a znode ...*/
		WUNLOCK_ZLOCK(lock);
		/* ... and sleep */
		go_to_sleep(owner, level);

		WLOCK_ZLOCK(lock);

		if (hipri) {
			assert("nikita-1838", lock->nr_hipri_requests > 0);
			lock->nr_hipri_requests--;
		}

		requestors_list_remove(owner);
	}

	assert("jmacd-807/a", rw_zlock_is_locked(&node->lock));
	return lock_tail(owner, wake_up_next, ret, mode);
}

/* lock object invalidation means changing of lock object state to `INVALID'
   and waiting for all other processes to cancel theirs lock requests. */
reiser4_internal void
invalidate_lock(lock_handle * handle	/* path to lock
					   * owner and lock
					   * object is being
					   * invalidated. */ )
{
	znode *node = handle->node;
	lock_stack *owner = handle->owner;
	lock_stack *rq;

	assert("zam-325", owner == get_current_lock_stack());
	assert("zam-103", znode_is_write_locked(node));
	assert("nikita-1393", !ZF_ISSET(node, JNODE_LEFT_CONNECTED));
	assert("nikita-1793", !ZF_ISSET(node, JNODE_RIGHT_CONNECTED));
	assert("nikita-1394", ZF_ISSET(node, JNODE_HEARD_BANSHEE));
	assert("nikita-3097", znode_is_wlocked_once(node));
	assert("nikita-3338", rw_zlock_is_locked(&node->lock));

	if (handle->signaled)
		atomic_dec(&owner->nr_signaled);

	ZF_SET(node, JNODE_IS_DYING);
	unlink_object(handle);
	node->lock.nr_readers = 0;

	/* all requestors will be informed that lock is invalidated. */
	for_all_type_safe_list(requestors, &node->lock.requestors, rq) {
		reiser4_wake_up(rq);
	}

	/* We use that each unlock() will wakeup first item from requestors
	   list; our lock stack is the last one. */
	while (!requestors_list_empty(&node->lock.requestors)) {
		requestors_list_push_back(&node->lock.requestors, owner);

		prepare_to_sleep(owner);

		WUNLOCK_ZLOCK(&node->lock);
		go_to_sleep(owner, znode_get_level(node));
		WLOCK_ZLOCK(&node->lock);

		requestors_list_remove(owner);
	}

	WUNLOCK_ZLOCK(&node->lock);
}

/* Initializes lock_stack. */
reiser4_internal void
init_lock_stack(lock_stack * owner	/* pointer to
					   * allocated
					   * structure. */ )
{
	/* xmemset(,0,) is done already as a part of reiser4 context
	 * initialization */
	/* xmemset(owner, 0, sizeof (lock_stack)); */
	locks_list_init(&owner->locks);
	requestors_list_clean(owner);
	spin_stack_init(owner);
	owner->curpri = 1;
	sema_init(&owner->sema, 0);
}

/* Initializes lock object. */
reiser4_internal void
reiser4_init_lock(zlock * lock	/* pointer on allocated
				   * uninitialized lock object
				   * structure. */ )
{
	xmemset(lock, 0, sizeof (zlock));
	rw_zlock_init(lock);
	requestors_list_init(&lock->requestors);
	owners_list_init(&lock->owners);
}

/* lock handle initialization */
reiser4_internal void
init_lh(lock_handle * handle)
{
	xmemset(handle, 0, sizeof *handle);
	locks_list_clean(handle);
	owners_list_clean(handle);
}

/* freeing of lock handle resources */
reiser4_internal void
done_lh(lock_handle * handle)
{
	assert("zam-342", handle != NULL);
	if (handle->owner != NULL)
		longterm_unlock_znode(handle);
}

/* What kind of lock? */
reiser4_internal znode_lock_mode lock_mode(lock_handle * handle)
{
	if (handle->owner == NULL) {
		return ZNODE_NO_LOCK;
	} else if (znode_is_rlocked(handle->node)) {
		return ZNODE_READ_LOCK;
	} else {
		return ZNODE_WRITE_LOCK;
	}
}

/* Transfer a lock handle (presumably so that variables can be moved between stack and
   heap locations). */
static void
move_lh_internal(lock_handle * new, lock_handle * old, int unlink_old)
{
	znode *node = old->node;
	lock_stack *owner = old->owner;
	int signaled;

	/* locks_list, modified by link_object() is not protected by
	   anything. This is valid because only current thread ever modifies
	   locks_list of its lock_stack.
	*/
	assert("nikita-1827", owner == get_current_lock_stack());
	assert("nikita-1831", new->owner == NULL);

	WLOCK_ZLOCK(&node->lock);

	signaled = old->signaled;
	if (unlink_old) {
		unlink_object(old);
	} else {
		if (node->lock.nr_readers > 0) {
			node->lock.nr_readers += 1;
		} else {
			node->lock.nr_readers -= 1;
		}
		if (signaled) {
			atomic_inc(&owner->nr_signaled);
		}
		if (owner->curpri) {
			node->lock.nr_hipri_owners += 1;
		}
		LOCK_CNT_INC(long_term_locked_znode);

		zref(node);
	}
	link_object(new, owner, node);
	new->signaled = signaled;

	WUNLOCK_ZLOCK(&node->lock);
}

reiser4_internal void
move_lh(lock_handle * new, lock_handle * old)
{
	move_lh_internal(new, old, /*unlink_old */ 1);
}

reiser4_internal void
copy_lh(lock_handle * new, lock_handle * old)
{
	move_lh_internal(new, old, /*unlink_old */ 0);
}

/* after getting -E_DEADLOCK we unlock znodes until this function returns false */
reiser4_internal int
check_deadlock(void)
{
	lock_stack *owner = get_current_lock_stack();
	return atomic_read(&owner->nr_signaled) != 0;
}

/* Before going to sleep we re-check "release lock" requests which might come from threads with hi-pri lock
   priorities. */
reiser4_internal int
prepare_to_sleep(lock_stack * owner)
{
	assert("nikita-1847", owner == get_current_lock_stack());
	/* NOTE(Zam): We cannot reset the lock semaphore here because it may
	   clear wake-up signal. The initial design was to re-check all
	   conditions under which we continue locking, release locks or sleep
	   until conditions are changed. However, even lock.c does not follow
	   that design.  So, wake-up signal which is stored in semaphore state
	   could we loosen by semaphore reset.  The less complex scheme without
	   resetting the semaphore is enough to not to loose wake-ups.

	if (0) {

	           NOTE-NIKITA: I commented call to sema_init() out hoping
		   that it is the reason or thread sleeping in
		   down(&owner->sema) without any other thread running.

		   Anyway, it is just an optimization: is semaphore is not
		   reinitialised at this point, in the worst case
		   longterm_lock_znode() would have to iterate its loop once
		   more.
		spin_lock_stack(owner);
		sema_init(&owner->sema, 0);
		spin_unlock_stack(owner);
	}
	*/

	/* We return -E_DEADLOCK if one or more "give me the lock" messages are
	 * counted in nr_signaled */
	if (unlikely(atomic_read(&owner->nr_signaled) != 0)) {
		assert("zam-959", !owner->curpri);
		return RETERR(-E_DEADLOCK);
	}
	return 0;
}

/* Wakes up a single thread */
reiser4_internal void
__reiser4_wake_up(lock_stack * owner)
{
	up(&owner->sema);
}

/* Puts a thread to sleep */
reiser4_internal void
__go_to_sleep(lock_stack * owner
#if REISER4_STATS
	    , int node_level
#endif
)
{
#if REISER4_STATS
	unsigned long sleep_start = jiffies;
#endif
	/* Well, we might sleep here, so holding of any spinlocks is no-no */
	assert("nikita-3027", schedulable());
	/* return down_interruptible(&owner->sema); */
	down(&owner->sema);
#if REISER4_STATS
	switch (node_level) {
	    case ADD_TO_SLEPT_IN_WAIT_EVENT:
		    reiser4_stat_add(txnmgr.slept_in_wait_event, jiffies - sleep_start);
		    break;
	    case ADD_TO_SLEPT_IN_WAIT_ATOM:
		    reiser4_stat_add(txnmgr.slept_in_wait_atom, jiffies - sleep_start);
		    break;
	    default:
		    reiser4_stat_add_at_level(node_level, time_slept,
					      jiffies - sleep_start);
	}
#endif
}

reiser4_internal int
lock_stack_isclean(lock_stack * owner)
{
	if (locks_list_empty(&owner->locks)) {
		assert("zam-353", atomic_read(&owner->nr_signaled) == 0);
		return 1;
	}

	return 0;
}

#if REISER4_DEBUG_OUTPUT
/* Debugging help */
reiser4_internal void
print_lock_stack(const char *prefix, lock_stack * owner)
{
	lock_handle *handle;

	spin_lock_stack(owner);

	printk("%s:\n", prefix);
	printk(".... nr_signaled %d\n", atomic_read(&owner->nr_signaled));
	printk(".... curpri %s\n", owner->curpri ? "high" : "low");

	if (owner->request.mode != 0) {
		printk(".... current request: %s", owner->request.mode == ZNODE_WRITE_LOCK ? "write" : "read");
		print_address("", znode_get_block(owner->request.node));
	}

	printk(".... current locks:\n");

	for_all_type_safe_list(locks, &owner->locks, handle) {
		if (handle->node != NULL)
			print_address(znode_is_rlocked(handle->node) ?
				      "......  read" : "...... write", znode_get_block(handle->node));
	}

	spin_unlock_stack(owner);
}
#endif

#if REISER4_DEBUG

/*
 * debugging functions
 */

/* check consistency of locking data-structures hanging of the @stack */
void
check_lock_stack(lock_stack * stack)
{
	spin_lock_stack(stack);
	/* check that stack->locks is not corrupted */
	locks_list_check(&stack->locks);
	spin_unlock_stack(stack);
}

/* check consistency of locking data structures */
void
check_lock_data(void)
{
	check_lock_stack(&get_current_context()->stack);
}

/* check consistency of locking data structures for @node */
void
check_lock_node_data(znode * node)
{
	RLOCK_ZLOCK(&node->lock);
	owners_list_check(&node->lock.owners);
	requestors_list_check(&node->lock.requestors);
	RUNLOCK_ZLOCK(&node->lock);
}

/* check that given lock request is dead lock safe. This check is, of course,
 * not exhaustive. */
static int
request_is_deadlock_safe(znode * node, znode_lock_mode mode,
			 znode_lock_request request)
{
	lock_stack *owner;

	owner = get_current_lock_stack();
	/*
	 * check that hipri lock request is not issued when there are locked
	 * nodes at the higher levels.
	 */
	if (request & ZNODE_LOCK_HIPRI && !(request & ZNODE_LOCK_NONBLOCK) &&
	    znode_get_level(node) != 0) {
		lock_handle *item;

		for_all_type_safe_list(locks, &owner->locks, item) {
			znode *other = item->node;

			if (znode_get_level(other) == 0)
				continue;
			if (znode_get_level(other) > znode_get_level(node))
				return 0;
		}
	}
	return 1;
}

#endif

/* return pointer to static storage with name of lock_mode. For
    debugging */
reiser4_internal const char *
lock_mode_name(znode_lock_mode lock /* lock mode to get name of */ )
{
	if (lock == ZNODE_READ_LOCK)
		return "read";
	else if (lock == ZNODE_WRITE_LOCK)
		return "write";
	else {
		static char buf[30];

		sprintf(buf, "unknown: %i", lock);
		return buf;
	}
}

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
