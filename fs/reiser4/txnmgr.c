/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by
 * reiser4/README */

/* Joshua MacDonald wrote the first draft of this code. */

/* ZAM-LONGTERM-FIXME-HANS: The locking in this file is badly designed, and a
filesystem scales only as well as its worst locking design.  You need to
substantially restructure this code. Josh was not as experienced a programmer
as you.  Particularly review how the locking style differs from what you did
for znodes usingt hi-lo priority locking, and present to me an opinion on
whether the differences are well founded.  */

/* I cannot help but to disagree with the sentiment above. Locking of
 * transaction manager is _not_ badly designed, and, at the very least, is not
 * the scaling bottleneck. Scaling bottleneck is _exactly_ hi-lo priority
 * locking on znodes, especially on the root node of the tree. --nikita,
 * 2003.10.13 */

/* The txnmgr is a set of interfaces that keep track of atoms and transcrash handles.  The
   txnmgr processes capture_block requests and manages the relationship between jnodes and
   atoms through the various stages of a transcrash, and it also oversees the fusion and
   capture-on-copy processes.  The main difficulty with this task is maintaining a
   deadlock-free lock ordering between atoms and jnodes/handles.  The reason for the
   difficulty is that jnodes, handles, and atoms contain pointer circles, and the cycle
   must be broken.  The main requirement is that atom-fusion be deadlock free, so once you
   hold the atom_lock you may then wait to acquire any jnode or handle lock.  This implies
   that any time you check the atom-pointer of a jnode or handle and then try to lock that
   atom, you must use trylock() and possibly reverse the order.

   This code implements the design documented at:

     http://namesys.com/txn-doc.html

ZAM-FIXME-HANS: update v4.html to contain all of the information present in the above (but updated), and then remove the
above document and reference the new.  Be sure to provide some credit to Josh.  I already have some writings on this
topic in v4.html, but they are lacking in details present in the above.  Cure that.  Remember to write for the bright 12
year old --- define all technical terms used.

*/

/* Thoughts on the external transaction interface:

   In the current code, a TRANSCRASH handle is created implicitly by init_context() (which
   creates state that lasts for the duration of a system call and is called at the start
   of ReiserFS methods implementing VFS operations), and closed by reiser4_exit_context(),
   occupying the scope of a single system call.  We wish to give certain applications an
   interface to begin and close (commit) transactions.  Since our implementation of
   transactions does not yet support isolation, allowing an application to open a
   transaction implies trusting it to later close the transaction.  Part of the
   transaction interface will be aimed at enabling that trust, but the interface for
   actually using transactions is fairly narrow.

   BEGIN_TRANSCRASH: Returns a transcrash identifier.  It should be possible to translate
   this identifier into a string that a shell-script could use, allowing you to start a
   transaction by issuing a command.  Once open, the transcrash should be set in the task
   structure, and there should be options (I suppose) to allow it to be carried across
   fork/exec.  A transcrash has several options:

     - READ_FUSING or WRITE_FUSING: The default policy is for txn-capture to capture only
     on writes (WRITE_FUSING) and allow "dirty reads".  If the application wishes to
     capture on reads as well, it should set READ_FUSING.

     - TIMEOUT: Since a non-isolated transcrash cannot be undone, every transcrash must
     eventually close (or else the machine must crash).  If the application dies an
     unexpected death with an open transcrash, for example, or if it hangs for a long
     duration, one solution (to avoid crashing the machine) is to simply close it anyway.
     This is a dangerous option, but it is one way to solve the problem until isolated
     transcrashes are available for untrusted applications.

     It seems to be what databases do, though it is unclear how one avoids a DoS attack
     creating a vulnerability based on resource starvation.  Guaranteeing that some
     minimum amount of computational resources are made available would seem more correct
     than guaranteeing some amount of time.  When we again have someone to code the work,
     this issue should be considered carefully.  -Hans

   RESERVE_BLOCKS: A running transcrash should indicate to the transaction manager how
   many dirty blocks it expects.  The reserve_blocks interface should be called at a point
   where it is safe for the application to fail, because the system may not be able to
   grant the allocation and the application must be able to back-out.  For this reason,
   the number of reserve-blocks can also be passed as an argument to BEGIN_TRANSCRASH, but
   the application may also wish to extend the allocation after beginning its transcrash.

   CLOSE_TRANSCRASH: The application closes the transcrash when it is finished making
   modifications that require transaction protection.  When isolated transactions are
   supported the CLOSE operation is replaced by either COMMIT or ABORT.  For example, if a
   RESERVE_BLOCKS call fails for the application, it should "abort" by calling
   CLOSE_TRANSCRASH, even though it really commits any changes that were made (which is
   why, for safety, the application should call RESERVE_BLOCKS before making any changes).

   For actually implementing these out-of-system-call-scopped transcrashes, the
   reiser4_context has a "txn_handle *trans" pointer that may be set to an open
   transcrash.  Currently there are no dynamically-allocated transcrashes, but there is a
   "kmem_cache_t *_txnh_slab" created for that purpose in this file.
*/

/* Extending the other system call interfaces for future transaction features:

   Specialized applications may benefit from passing flags to the ordinary system call
   interface such as read(), write(), or stat().  For example, the application specifies
   WRITE_FUSING by default but wishes to add that a certain read() command should be
   treated as READ_FUSING.  But which read?  Is it the directory-entry read, the stat-data
   read, or the file-data read?  These issues are straight-forward, but there are a lot of
   them and adding the necessary flags-passing code will be tedious.

   When supporting isolated transactions, there is a corresponding READ_MODIFY_WRITE (RMW)
   flag, which specifies that although it is a read operation being requested, a
   write-lock should be taken.  The reason is that read-locks are shared while write-locks
   are exclusive, so taking a read-lock when a later-write is known in advance will often
   leads to deadlock.  If a reader knows it will write later, it should issue read
   requests with the RMW flag set.
*/

/*
   The znode/atom deadlock avoidance.

   FIXME(Zam): writing of this comment is in progress.

   The atom's special stage ASTAGE_CAPTURE_WAIT introduces a kind of atom's
   long-term locking, which makes reiser4 locking scheme more complex.  It had
   deadlocks until we implement deadlock avoidance algorithms.  That deadlocks
   looked as the following: one stopped thread waits for a long-term lock on
   znode, the thread who owns that lock waits when fusion with another atom will
   be allowed.

   The source of the deadlocks is an optimization of not capturing index nodes
   for read.  Let's prove it.  Suppose we have dumb node capturing scheme which
   unconditionally captures each block before locking it.

   That scheme has no deadlocks.  Let's begin with the thread which stage is
   ASTAGE_CAPTURE_WAIT and it waits for a znode lock.  The thread can't wait for
   a capture because it's stage allows fusion with any atom except which are
   being committed currently. A process of atom commit can't deadlock because
   atom commit procedure does not acquire locks and does not fuse with other
   atoms.  Reiser4 does capturing right before going to sleep inside the
   longtertm_lock_znode() function, it means the znode which we want to lock is
   already captured and its atom is in ASTAGE_CAPTURE_WAIT stage.  If we
   continue the analysis we understand that no one process in the sequence may
   waits atom fusion.  Thereby there are no deadlocks of described kind.

   The capturing optimization makes the deadlocks possible.  A thread can wait a
   lock which owner did not captured that node.  The lock owner's current atom
   is not fused with the first atom and it does not get a ASTAGE_CAPTURE_WAIT
   state. A deadlock is possible when that atom meets another one which is in
   ASTAGE_CAPTURE_WAIT already.

   The deadlock avoidance scheme includes two algorithms:

   First algorithm is used when a thread captures a node which is locked but not
   captured by another thread.  Those nodes are marked MISSED_IN_CAPTURE at the
   moment we skip their capturing.  If such a node (marked MISSED_IN_CAPTURE) is
   being captured by a thread with current atom is in ASTAGE_CAPTURE_WAIT, the
   routine which forces all lock owners to join with current atom is executed.

   Second algorithm does not allow to skip capturing of already captured nodes.

   Both algorithms together prevent waiting a longterm lock without atom fusion
   with atoms of all lock owners, which is a key thing for getting atom/znode
   locking deadlocks.
*/

/*
 * Transactions and mmap(2).
 *
 *     1. Transactions are not supported for accesses through mmap(2), because
 *     this would effectively amount to user-level transactions whose duration
 *     is beyond control of the kernel.
 *
 *     2. That said, we still want to preserve some decency with regard to
 *     mmap(2). During normal write(2) call, following sequence of events
 *     happens:
 *
 *         1. page is created;
 *
 *         2. jnode is created, dirtied and captured into current atom.
 *
 *         3. extent is inserted and modified.
 *
 *     Steps (2) and (3) take place under long term lock on the twig node.
 *
 *     When file is accessed through mmap(2) page is always created during
 *     page fault. After this (in reiser4_readpage()->readpage_extent()):
 *
 *         1. if access is made to non-hole page new jnode is created, (if
 *         necessary)
 *
 *         2. if access is made to the hole page, jnode is not created (XXX
 *         not clear why).
 *
 *     Also, even if page is created by write page fault it is not marked
 *     dirty immediately by handle_mm_fault(). Probably this is to avoid races
 *     with page write-out.
 *
 *     Dirty bit installed by hardware is only transferred to the struct page
 *     later, when page is unmapped (in zap_pte_range(), or
 *     try_to_unmap_one()).
 *
 *     So, with mmap(2) we have to handle following irksome situations:
 *
 *         1. there exists modified page (clean or dirty) without jnode
 *
 *         2. there exists modified page (clean or dirty) with clean jnode
 *
 *         3. clean page which is a part of atom can be transparently modified
 *         at any moment through mapping without becoming dirty.
 *
 *     (1) and (2) can lead to the out-of-memory situation: ->writepage()
 *     doesn't know what to do with such pages and ->sync_sb()/->writepages()
 *     don't see them, because these methods operate on atoms.
 *
 *     (3) can lead to the loss of data: suppose we have dirty page with dirty
 *     captured jnode captured by some atom. As part of early flush (for
 *     example) page was written out. Dirty bit was cleared on both page and
 *     jnode. After this page is modified through mapping, but kernel doesn't
 *     notice and just discards page and jnode as part of commit. (XXX
 *     actually it doesn't, because to reclaim page ->releasepage() has to be
 *     called and before this dirty bit will be transferred to the struct
 *     page).
 *
 */

#include "debug.h"
#include "type_safe_list.h"
#include "txnmgr.h"
#include "jnode.h"
#include "znode.h"
#include "block_alloc.h"
#include "tree.h"
#include "wander.h"
#include "ktxnmgrd.h"
#include "super.h"
#include "page_cache.h"
#include "reiser4.h"
#include "vfs_ops.h"
#include "inode.h"
#include "prof.h"
#include "flush.h"

#include <asm/atomic.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/writeback.h>
#include <linux/swap.h>        /* for totalram_pages */

static void atom_free(txn_atom * atom);

static long commit_txnh(txn_handle * txnh);

static void wakeup_atom_waitfor_list(txn_atom * atom);
static void wakeup_atom_waiting_list(txn_atom * atom);

static void capture_assign_txnh_nolock(txn_atom * atom, txn_handle * txnh);

static void capture_assign_block_nolock(txn_atom * atom, jnode * node);

static int capture_assign_block(txn_handle * txnh, jnode * node);

static int capture_assign_txnh(jnode * node, txn_handle * txnh, txn_capture mode, int can_coc);

static int fuse_not_fused_lock_owners(txn_handle * txnh, znode * node);

static int capture_init_fusion(jnode * node, txn_handle * txnh, txn_capture mode, int can_coc);

static int capture_fuse_wait(jnode * node, txn_handle * txnh, txn_atom * atomf, txn_atom * atomh, txn_capture mode);

static void capture_fuse_into(txn_atom * small, txn_atom * large);

static int capture_copy(jnode * node, txn_handle * txnh, txn_atom * atomf, txn_atom * atomh, txn_capture mode, int can_coc);

void invalidate_list(capture_list_head *);

/* GENERIC STRUCTURES */

typedef struct _txn_wait_links txn_wait_links;

struct _txn_wait_links {
	lock_stack *_lock_stack;
	fwaitfor_list_link _fwaitfor_link;
	fwaiting_list_link _fwaiting_link;
	int (*waitfor_cb)(txn_atom *atom, struct _txn_wait_links *wlinks);
	int (*waiting_cb)(txn_atom *atom, struct _txn_wait_links *wlinks);
};

TYPE_SAFE_LIST_DEFINE(txnh, txn_handle, txnh_link);

TYPE_SAFE_LIST_DEFINE(fwaitfor, txn_wait_links, _fwaitfor_link);
TYPE_SAFE_LIST_DEFINE(fwaiting, txn_wait_links, _fwaiting_link);

/* FIXME: In theory, we should be using the slab cache init & destructor
   methods instead of, e.g., jnode_init, etc. */
static kmem_cache_t *_atom_slab = NULL;
/* this is for user-visible, cross system-call transactions. */
static kmem_cache_t *_txnh_slab = NULL;

ON_DEBUG(extern atomic_t flush_cnt;)

/* TXN_INIT */
/* Initialize static variables in this file. */
reiser4_internal int
txnmgr_init_static(void)
{
	assert("jmacd-600", _atom_slab == NULL);
	assert("jmacd-601", _txnh_slab == NULL);

	ON_DEBUG(atomic_set(&flush_cnt, 0));

	_atom_slab = kmem_cache_create("txn_atom", sizeof (txn_atom), 0,
				       SLAB_HWCACHE_ALIGN|SLAB_RECLAIM_ACCOUNT,
				       NULL, NULL);

	if (_atom_slab == NULL) {
		goto error;
	}

	_txnh_slab = kmem_cache_create("txn_handle", sizeof (txn_handle), 0, SLAB_HWCACHE_ALIGN, NULL, NULL);

	if (_txnh_slab == NULL) {
		goto error;
	}

	return 0;

error:

	if (_atom_slab != NULL) {
		kmem_cache_destroy(_atom_slab);
	}
	if (_txnh_slab != NULL) {
		kmem_cache_destroy(_txnh_slab);
	}
	return RETERR(-ENOMEM);
}

/* Un-initialize static variables in this file. */
reiser4_internal int
txnmgr_done_static(void)
{
	int ret1, ret2, ret3;

	ret1 = ret2 = ret3 = 0;

	if (_atom_slab != NULL) {
		ret1 = kmem_cache_destroy(_atom_slab);
		_atom_slab = NULL;
	}

	if (_txnh_slab != NULL) {
		ret2 = kmem_cache_destroy(_txnh_slab);
		_txnh_slab = NULL;
	}

	return ret1 ? : ret2;
}

/* Initialize a new transaction manager.  Called when the super_block is initialized. */
reiser4_internal void
txnmgr_init(txn_mgr * mgr)
{
	assert("umka-169", mgr != NULL);

	mgr->atom_count = 0;
	mgr->id_count = 1;

	atom_list_init(&mgr->atoms_list);
	spin_txnmgr_init(mgr);

	sema_init(&mgr->commit_semaphore, 1);
}

/* Free transaction manager. */
reiser4_internal int
txnmgr_done(txn_mgr * mgr UNUSED_ARG)
{
	assert("umka-170", mgr != NULL);

	return 0;
}

/* Initialize a transaction handle. */
/* Audited by: umka (2002.06.13) */
static void
txnh_init(txn_handle * txnh, txn_mode mode)
{
	assert("umka-171", txnh != NULL);

	txnh->mode = mode;
	txnh->atom = NULL;
	txnh->flags = 0;

	spin_txnh_init(txnh);

	txnh_list_clean(txnh);
}

#if REISER4_DEBUG
/* Check if a transaction handle is clean. */
static int
txnh_isclean(txn_handle * txnh)
{
	assert("umka-172", txnh != NULL);
	return txnh->atom == NULL && spin_txnh_is_not_locked(txnh);
}
#endif

/* Initialize an atom. */
static void
atom_init(txn_atom * atom)
{
	int level;

	assert("umka-173", atom != NULL);

	xmemset(atom, 0, sizeof (txn_atom));

	atom->stage = ASTAGE_FREE;
	atom->start_time = jiffies;

	for (level = 0; level < REAL_MAX_ZTREE_HEIGHT + 1; level += 1)
		capture_list_init(ATOM_DIRTY_LIST(atom, level));

	capture_list_init(ATOM_CLEAN_LIST(atom));
	capture_list_init(ATOM_OVRWR_LIST(atom));
	capture_list_init(ATOM_WB_LIST(atom));
	capture_list_init(&atom->inodes);
	spin_atom_init(atom);
	txnh_list_init(&atom->txnh_list);
	atom_list_clean(atom);
	fwaitfor_list_init(&atom->fwaitfor_list);
	fwaiting_list_init(&atom->fwaiting_list);
	prot_list_init(&atom->protected);
	blocknr_set_init(&atom->delete_set);
	blocknr_set_init(&atom->wandered_map);

	init_atom_fq_parts(atom);
}

#if REISER4_DEBUG
/* Check if an atom is clean. */
static int
atom_isclean(txn_atom * atom)
{
	int level;

	assert("umka-174", atom != NULL);

	for (level = 0; level < REAL_MAX_ZTREE_HEIGHT + 1; level += 1) {
		if (!capture_list_empty(ATOM_DIRTY_LIST(atom, level))) {
			return 0;
		}
	}

	return
		atom->stage == ASTAGE_FREE &&
		atom->txnh_count == 0 &&
		atom->capture_count == 0 &&
		atomic_read(&atom->refcount) == 0 &&
		atom_list_is_clean(atom) &&
		txnh_list_empty(&atom->txnh_list) &&
		capture_list_empty(ATOM_CLEAN_LIST(atom)) &&
		capture_list_empty(ATOM_OVRWR_LIST(atom)) &&
		capture_list_empty(ATOM_WB_LIST(atom)) &&
		fwaitfor_list_empty(&atom->fwaitfor_list) &&
		fwaiting_list_empty(&atom->fwaiting_list) &&
		prot_list_empty(&atom->protected) &&
		atom_fq_parts_are_clean(atom);
}
#endif

/* Begin a transaction in this context.  Currently this uses the reiser4_context's
   trans_in_ctx, which means that transaction handles are stack-allocated.  Eventually
   this will be extended to allow transaction handles to span several contexts. */
/* Audited by: umka (2002.06.13) */
reiser4_internal void
txn_begin(reiser4_context * context)
{
	assert("jmacd-544", context->trans == NULL);

	context->trans = &context->trans_in_ctx;

	/* FIXME_LATER_JMACD Currently there's no way to begin a TXN_READ_FUSING
	   transcrash.  Default should be TXN_WRITE_FUSING.  Also, the _trans variable is
	   stack allocated right now, but we would like to allow for dynamically allocated
	   transcrashes that span multiple system calls.
	*/
	txnh_init(context->trans, TXN_WRITE_FUSING);
}

/* Finish a transaction handle context. */
reiser4_internal long
txn_end(reiser4_context * context)
{
	long ret = 0;
	txn_handle *txnh;

	assert("umka-283", context != NULL);
	assert("nikita-3012", schedulable());

	/* closing non top-level context---nothing to do */
	if (context != context->parent)
		return 0;

	assert("nikita-2967", lock_stack_isclean(get_current_lock_stack()));

	txnh = context->trans;

	if (txnh != NULL) {
		/* The txnh's field "atom" can be checked for NULL w/o holding a
		   lock because txnh->atom could be set by this thread's call to
		   try_capture or the deadlock prevention code in
		   fuse_not_fused_lock_owners().  But that code may assign an
		   atom to this transaction handle only if there are locked and
		   not yet fused nodes.  It cannot happen because lock stack
		   should be clean at this moment. */
		if (txnh->atom != NULL)
			ret = commit_txnh(txnh);

		assert("jmacd-633", txnh_isclean(txnh));

		context->trans = NULL;
	}

	return ret;
}

reiser4_internal void
txn_restart(reiser4_context * context)
{
	txn_end(context);
	preempt_point();
	txn_begin(context);
}

reiser4_internal void
txn_restart_current(void)
{
	txn_restart(get_current_context());
}

/* TXN_ATOM */

/* Get the atom belonging to a txnh, which is not locked.  Return txnh locked. Locks atom, if atom
   is not NULL.  This performs the necessary spin_trylock to break the lock-ordering cycle.  May
   return NULL. */
reiser4_internal txn_atom *
txnh_get_atom(txn_handle * txnh)
{
	txn_atom *atom;

	assert("umka-180", txnh != NULL);
	assert("jmacd-5108", spin_txnh_is_not_locked(txnh));

	while (1) {
		LOCK_TXNH(txnh);
		atom = txnh->atom;

		if (atom == NULL)
			break;

		if (spin_trylock_atom(atom))
			break;

		atomic_inc(&atom->refcount);

		UNLOCK_TXNH(txnh);
		LOCK_ATOM(atom);
		LOCK_TXNH(txnh);

		if (txnh->atom == atom) {
			atomic_dec(&atom->refcount);
			break;
		}

		UNLOCK_TXNH(txnh);
		atom_dec_and_unlock(atom);
	}

	return atom;
}

/* Get the current atom and spinlock it if current atom present. May return NULL  */
reiser4_internal txn_atom *
get_current_atom_locked_nocheck(void)
{
	reiser4_context *cx;
	txn_atom *atom;
	txn_handle *txnh;

	cx = get_current_context();
	assert("zam-437", cx != NULL);

	txnh = cx->trans;
	assert("zam-435", txnh != NULL);

	atom = txnh_get_atom(txnh);

	UNLOCK_TXNH(txnh);
	return atom;
}

/* Get the atom belonging to a jnode, which is initially locked.  Return with
   both jnode and atom locked.  This performs the necessary spin_trylock to
   break the lock-ordering cycle.  Assumes the jnode is already locked, and
   returns NULL if atom is not set. */
reiser4_internal txn_atom *
jnode_get_atom(jnode * node)
{
	txn_atom *atom;

	assert("umka-181", node != NULL);

	while (1) {
		assert("jmacd-5108", spin_jnode_is_locked(node));

		atom = node->atom;
		/* node is not in any atom */
		if (atom == NULL)
			break;

		/* If atom is not locked, grab the lock and return */
		if (spin_trylock_atom(atom))
			break;

		/* At least one jnode belongs to this atom it guarantees that
		 * atom->refcount > 0, we can safely increment refcount. */
		atomic_inc(&atom->refcount);
		UNLOCK_JNODE(node);

		/* re-acquire spin locks in the right order */
		LOCK_ATOM(atom);
		LOCK_JNODE(node);

		/* check if node still points to the same atom. */
		if (node->atom == atom) {
			atomic_dec(&atom->refcount);
			break;
		}

		/* releasing of atom lock and reference requires not holding
		 * locks on jnodes.  */
		UNLOCK_JNODE(node);

		/* We do not sure that this atom has extra references except our
		 * one, so we should call proper function which may free atom if
		 * last reference is released. */
		atom_dec_and_unlock(atom);

		/* lock jnode again for getting valid node->atom pointer
		 * value. */
		LOCK_JNODE(node);
	}

	return atom;
}

/* Returns true if @node is dirty and part of the same atom as one of its neighbors.  Used
   by flush code to indicate whether the next node (in some direction) is suitable for
   flushing. */
reiser4_internal int
same_slum_check(jnode * node, jnode * check, int alloc_check, int alloc_value)
{
	int compat;
	txn_atom *atom;

	assert("umka-182", node != NULL);
	assert("umka-183", check != NULL);

	/* Not sure what this function is supposed to do if supplied with @check that is
	   neither formatted nor unformatted (bitmap or so). */
	assert("nikita-2373", jnode_is_znode(check) || jnode_is_unformatted(check));

	/* Need a lock on CHECK to get its atom and to check various state bits.
	   Don't need a lock on NODE once we get the atom lock. */
	/* It is not enough to lock two nodes and check (node->atom ==
	   check->atom) because atom could be locked and being fused at that
	   moment, jnodes of the atom of that state (being fused) can point to
	   different objects, but the atom is the same.*/
	LOCK_JNODE(check);

	atom = jnode_get_atom(check);

	if (atom == NULL) {
		compat = 0;
	} else {
		compat = (node->atom == atom && jnode_is_dirty(check));

		if (compat && jnode_is_znode(check)) {
			compat &= znode_is_connected(JZNODE(check));
		}

		if (compat && alloc_check) {
			compat &= (alloc_value == jnode_is_flushprepped(check));
		}

		UNLOCK_ATOM(atom);
	}

	UNLOCK_JNODE(check);

	return compat;
}

/* Decrement the atom's reference count and if it falls to zero, free it. */
reiser4_internal void
atom_dec_and_unlock(txn_atom * atom)
{
	txn_mgr *mgr = &get_super_private(reiser4_get_current_sb())->tmgr;

	assert("umka-186", atom != NULL);
	assert("jmacd-1071", spin_atom_is_locked(atom));
	assert("zam-1039", atomic_read(&atom->refcount) > 0);

	if (atomic_dec_and_test(&atom->refcount)) {
		/* take txnmgr lock and atom lock in proper order. */
		if (!spin_trylock_txnmgr(mgr)) {
			/* This atom should exist after we re-acquire its
			 * spinlock, so we increment its reference counter. */
			atomic_inc(&atom->refcount);
			UNLOCK_ATOM(atom);
			spin_lock_txnmgr(mgr);
			LOCK_ATOM(atom);

			if (!atomic_dec_and_test(&atom->refcount)) {
				UNLOCK_ATOM(atom);
				spin_unlock_txnmgr(mgr);
				return;
			}
		}
		assert("nikita-2656", spin_txnmgr_is_locked(mgr));
		atom_free(atom);
		spin_unlock_txnmgr(mgr);
	} else
		UNLOCK_ATOM(atom);
}

/* Return a new atom, locked.  This adds the atom to the transaction manager's list and
   sets its reference count to 1, an artificial reference which is kept until it
   commits.  We play strange games to avoid allocation under jnode & txnh spinlocks.*/

/* ZAM-FIXME-HANS: should we set node->atom and txnh->atom here also? */
/* ANSWER(ZAM): there are special functions, capture_assign_txnh_nolock() and
   capture_assign_block_nolock(), they are called right after calling
   atom_begin_and_lock().  It could be done here, but, for understandability, it
   is better to keep those calls inside try_capture_block main routine where all
   assignments are made. */
static txn_atom *
atom_begin_andlock(txn_atom ** atom_alloc, jnode * node, txn_handle * txnh)
{
	txn_atom *atom;
	txn_mgr *mgr;

	assert("jmacd-43228", spin_jnode_is_locked(node));
	assert("jmacd-43227", spin_txnh_is_locked(txnh));
	assert("jmacd-43226", node->atom == NULL);
	assert("jmacd-43225", txnh->atom == NULL);

	if (REISER4_DEBUG && rofs_jnode(node)) {
		warning("nikita-3366", "Creating atom on rofs");
		dump_stack();
	}

	/* A memory allocation may schedule we have to release those spinlocks
	 * before kmem_cache_alloc() call. */
	UNLOCK_JNODE(node);
	UNLOCK_TXNH(txnh);

	if (*atom_alloc == NULL) {
		(*atom_alloc) = kmem_cache_alloc(_atom_slab, GFP_KERNEL);

		if (*atom_alloc == NULL)
			return ERR_PTR(RETERR(-ENOMEM));
	}

	/* and, also, txnmgr spin lock should be taken before jnode and txnh
	   locks. */
	mgr = &get_super_private(reiser4_get_current_sb())->tmgr;
	spin_lock_txnmgr(mgr);

	LOCK_JNODE(node);
	LOCK_TXNH(txnh);

	/* Check if both atom pointers are still NULL... */
	if (node->atom != NULL || txnh->atom != NULL) {
		ON_TRACE(TRACE_TXN, "alloc atom race\n");
		/* NOTE-NIKITA probably it is rather better to free
		 * atom_alloc here than thread it up to try_capture(). */

		UNLOCK_TXNH(txnh);
		UNLOCK_JNODE(node);
		spin_unlock_txnmgr(mgr);

		reiser4_stat_inc(txnmgr.restart.atom_begin);
		return ERR_PTR(-E_REPEAT);
	}

	atom = *atom_alloc;
	*atom_alloc = NULL;

	atom_init(atom);

	assert("jmacd-17", atom_isclean(atom));

	/* Take the atom and txnmgr lock. No checks for lock ordering, because
	   @atom is new and inaccessible for others. */
	spin_lock_atom_no_ord(atom, 0, 0);

	atom_list_push_back(&mgr->atoms_list, atom);
	atom->atom_id = mgr->id_count++;
	mgr->atom_count += 1;

	/* Release txnmgr lock */
	spin_unlock_txnmgr(mgr);

	/* One reference until it commits. */
	atomic_inc(&atom->refcount);

	atom->stage = ASTAGE_CAPTURE_FUSE;

	ON_TRACE(TRACE_TXN, "begin atom %u\n", atom->atom_id);

	return atom;
}

#if REISER4_DEBUG
/* Return true if an atom is currently "open". */
static int atom_isopen(const txn_atom * atom)
{
	assert("umka-185", atom != NULL);

	return atom->stage > 0 && atom->stage < ASTAGE_PRE_COMMIT;
}
#endif

/* Return the number of pointers to this atom that must be updated during fusion.  This
   approximates the amount of work to be done.  Fusion chooses the atom with fewer
   pointers to fuse into the atom with more pointers. */
static int
atom_pointer_count(const txn_atom * atom)
{
	assert("umka-187", atom != NULL);

	/* This is a measure of the amount of work needed to fuse this atom
	 * into another. */
	return atom->txnh_count + atom->capture_count;
}

/* Called holding the atom lock, this removes the atom from the transaction manager list
   and frees it. */
static void
atom_free(txn_atom * atom)
{
	txn_mgr *mgr = &get_super_private(reiser4_get_current_sb())->tmgr;

	assert("umka-188", atom != NULL);

	ON_TRACE(TRACE_TXN, "free atom %u\n", atom->atom_id);

	assert("jmacd-18", spin_atom_is_locked(atom));

	/* Remove from the txn_mgr's atom list */
	assert("nikita-2657", spin_txnmgr_is_locked(mgr));
	mgr->atom_count -= 1;
	atom_list_remove_clean(atom);

	/* Clean the atom */
	assert("jmacd-16", (atom->stage == ASTAGE_INVALID || atom->stage == ASTAGE_DONE));
	atom->stage = ASTAGE_FREE;

	blocknr_set_destroy(&atom->delete_set);
	blocknr_set_destroy(&atom->wandered_map);

	assert("jmacd-16", atom_isclean(atom));

	UNLOCK_ATOM(atom);

	kmem_cache_free(_atom_slab, atom);
}

static int
atom_is_dotard(const txn_atom * atom)
{
	return time_after(jiffies, atom->start_time +
			  get_current_super_private()->tmgr.atom_max_age);
}

static int atom_can_be_committed (txn_atom * atom)
{
	assert ("zam-884", spin_atom_is_locked(atom));
	assert ("zam-885", atom->txnh_count > atom->nr_waiters);
	return atom->txnh_count == atom->nr_waiters + 1;
}

/* Return true if an atom should commit now.  This is determined by aging, atom
   size or atom flags. */
static int
atom_should_commit(const txn_atom * atom)
{
	assert("umka-189", atom != NULL);
	return
		(atom->flags & ATOM_FORCE_COMMIT) ||
		((unsigned) atom_pointer_count(atom) > get_current_super_private()->tmgr.atom_max_size) ||
		atom_is_dotard(atom);
}

/* return 1 if current atom exists and requires commit. */
reiser4_internal int current_atom_should_commit(void)
{
	txn_atom * atom;
	int result = 0;

	atom = get_current_atom_locked_nocheck();
	if (atom) {
		result = atom_should_commit(atom);
		UNLOCK_ATOM(atom);
	}
	return result;
}

static int
atom_should_commit_asap(const txn_atom * atom)
{
	unsigned int captured;
	unsigned int pinnedpages;

	assert("nikita-3309", atom != NULL);

	captured = (unsigned) atom->capture_count;
	pinnedpages = (captured >> PAGE_CACHE_SHIFT) * sizeof(znode);

	return
		(pinnedpages > (totalram_pages >> 3)) ||
		(atom->flushed > 100);
}

static jnode * find_first_dirty_in_list (capture_list_head * head, int flags)
{
	jnode * first_dirty;

	for_all_type_safe_list(capture, head, first_dirty) {
		if (!(flags & JNODE_FLUSH_COMMIT)) {
			if (
				/* skip jnodes which have "heard banshee" */
				JF_ISSET(first_dirty, JNODE_HEARD_BANSHEE) ||
				/* and with active I/O */
				JF_ISSET(first_dirty, JNODE_WRITEBACK))
				continue;
		}
		return first_dirty;
	}
	return NULL;
}

/* Get first dirty node from the atom's dirty_nodes[n] lists; return NULL if atom has no dirty
   nodes on atom's lists */
reiser4_internal jnode * find_first_dirty_jnode (txn_atom * atom, int flags)
{
	jnode *first_dirty;
	tree_level level;

	assert("zam-753", spin_atom_is_locked(atom));

	/* The flush starts from LEAF_LEVEL (=1). */
	for (level = 1; level < REAL_MAX_ZTREE_HEIGHT + 1; level += 1) {
		if (capture_list_empty(ATOM_DIRTY_LIST(atom, level)))
			continue;

		first_dirty = find_first_dirty_in_list(ATOM_DIRTY_LIST(atom, level), flags);
		if (first_dirty)
			return first_dirty;
	}

	/* znode-above-root is on the list #0. */
	return find_first_dirty_in_list(ATOM_DIRTY_LIST(atom, 0), flags);
}

#if REISER4_COPY_ON_CAPTURE

/* this spin lock is used to prevent races during steal on capture.
   FIXME: should be per filesystem or even per atom */
spinlock_t scan_lock = SPIN_LOCK_UNLOCKED;

/* Scan atom->writeback_nodes list and dispatch jnodes according to their state:
 * move dirty and !writeback jnodes to @fq, clean jnodes to atom's clean
 * list. */
/* NOTE: doing that in end IO handler requires using of special spinlocks which
 * disables interrupts in all places except IO handler. That is expensive. */
static void dispatch_wb_list (txn_atom * atom, flush_queue_t * fq)
{
	jnode * cur;
	int total, moved;

	assert("zam-905", spin_atom_is_locked(atom));

	total = 0;
	moved = 0;

	spin_lock(&scan_lock);
	cur = capture_list_front(ATOM_WB_LIST(atom));
	while (!capture_list_end(ATOM_WB_LIST(atom), cur)) {
		jnode * next;

		total ++;
		JF_SET(cur, JNODE_SCANNED);
		next = capture_list_next(cur);
		if (!capture_list_end(ATOM_WB_LIST(atom), next))
			JF_SET(next, JNODE_SCANNED);

		spin_unlock(&scan_lock);

		LOCK_JNODE(cur);
		assert("vs-1441", NODE_LIST(cur) == WB_LIST);
		if (!JF_ISSET(cur, JNODE_WRITEBACK)) {
			moved ++;
			if (JF_ISSET(cur, JNODE_DIRTY)) {
				queue_jnode(fq, cur);
			} else {
				/* move from writeback list to clean list */
				capture_list_remove(cur);
				capture_list_push_back(ATOM_CLEAN_LIST(atom), cur);
				ON_DEBUG(count_jnode(atom, cur, WB_LIST, CLEAN_LIST, 1));
			}
		}
		UNLOCK_JNODE(cur);

		spin_lock(&scan_lock);
		JF_CLR(cur, JNODE_SCANNED);
		cur = next;
		assert("vs-1450", ergo(!capture_list_end(ATOM_WB_LIST(atom), cur),
				       JF_ISSET(cur, JNODE_SCANNED) && NODE_LIST(cur) == WB_LIST));
	}
	spin_unlock(&scan_lock);
}

#else

static void dispatch_wb_list (txn_atom * atom, flush_queue_t * fq)
{
        jnode * cur;

        assert("zam-905", atom_is_protected(atom));

        cur = capture_list_front(ATOM_WB_LIST(atom));
        while (!capture_list_end(ATOM_WB_LIST(atom), cur)) {
                jnode * next = capture_list_next(cur);

                LOCK_JNODE(cur);
                if (!JF_ISSET(cur, JNODE_WRITEBACK)) {
                        if (JF_ISSET(cur, JNODE_DIRTY)) {
                                queue_jnode(fq, cur);
                        } else {
                                capture_list_remove(cur);
                                capture_list_push_back(ATOM_CLEAN_LIST(atom), cur);
                        }
                }
                UNLOCK_JNODE(cur);

                cur = next;
        }
}

#endif

/* Scan current atom->writeback_nodes list, re-submit dirty and !writeback
 * jnodes to disk. */
static int submit_wb_list (void)
{
	int ret;
	flush_queue_t * fq;

	fq = get_fq_for_current_atom();
	if (IS_ERR(fq))
		return PTR_ERR(fq);

	dispatch_wb_list(fq->atom, fq);
	UNLOCK_ATOM(fq->atom);

	/* trace_mark(flush); */
	write_current_logf(WRITE_IO_LOG, "mark=flush");

	ret = write_fq(fq, NULL, 1);
	fq_put(fq);

	return ret;
}

#if 0

/* when during system call inode is "captured" (by reiser4_mark_inode_dirty) - blocks grabbed for stat data update are
   moved to atom's flush_reserved bucket. On commit time (right before updating stat datas of all captured inodes) those
   blocks are moved to grabbed. This function is used to calculate number of blocks reserved for stat data update when
   those blocks get moved back and forwward between buckets of grabbed and flush_reserved blocks */
static reiser4_block_nr reserved_for_sd_update(struct inode *inode)
{
	return inode_file_plugin(inode)->estimate.update(inode);
}

static void atom_update_stat_data(txn_atom **atom)
{
	jnode *j;

	assert("vs-1241", spin_atom_is_locked(*atom));
	assert("vs-1616", capture_list_empty(&(*atom)->inodes));

	while (!capture_list_empty(&(*atom)->inodes)) {
		struct inode *inode;

		j = capture_list_front(&((*atom)->inodes));

		inode = inode_by_reiser4_inode(container_of(j, reiser4_inode, inode_jnode));

		/* move blocks grabbed for stat data update back from atom's
		 * flush_reserved to grabbed */
		flush_reserved2grabbed(*atom, reserved_for_sd_update(inode));

		capture_list_remove_clean(j);
		capture_list_push_back(ATOM_CLEAN_LIST(*atom), j);
		UNLOCK_ATOM(*atom);

		/* FIXME: it is not clear what to do if update sd fails. A warning will be issued (nikita-2221) */
		reiser4_update_sd(inode);
		*atom = get_current_atom_locked();
	}

	assert("vs-1231", capture_list_empty(&((*atom)->inodes)));
}
#endif

/* Wait completion of all writes, re-submit atom writeback list if needed. */
static int current_atom_complete_writes (void)
{
	int ret;

	/* Each jnode from that list was modified and dirtied when it had i/o
	 * request running already. After i/o completion we have to resubmit
	 * them to disk again.*/
	ret = submit_wb_list();
	if (ret < 0)
		return ret;

	/* Wait all i/o completion */
	ret = current_atom_finish_all_fq();
	if (ret)
		return ret;

	/* Scan wb list again; all i/o should be completed, we re-submit dirty
	 * nodes to disk */
	ret = submit_wb_list();
	if(ret < 0)
		return ret;

	/* Wait all nodes we just submitted */
	return current_atom_finish_all_fq();
}

#define TOOMANYFLUSHES (1 << 13)

/* Called with the atom locked and no open "active" transaction handlers except
   ours, this function calls flush_current_atom() until all dirty nodes are
   processed.  Then it initiates commit processing.

   Called by the single remaining open "active" txnh, which is closing. Other
   open txnhs belong to processes which wait atom commit in commit_txnh()
   routine. They are counted as "waiters" in atom->nr_waiters.  Therefore as
   long as we hold the atom lock none of the jnodes can be captured and/or
   locked.

   Return value is an error code if commit fails.
*/
static int commit_current_atom (long *nr_submitted, txn_atom ** atom)
{
	reiser4_super_info_data * sbinfo = get_current_super_private ();
	long ret;
	/* how many times jnode_flush() was called as a part of attempt to
	 * commit this atom. */
	int  flushiters;

	assert ("zam-888", atom != NULL && *atom != NULL);
	assert ("zam-886", spin_atom_is_locked(*atom));
	assert ("zam-887", get_current_context()->trans->atom == *atom);
	assert("jmacd-151", atom_isopen(*atom));

	/* lock ordering: delete_sema and commit_sema are unordered */
	assert("nikita-3184",
	       get_current_super_private()->delete_sema_owner != current);

	ON_TRACE(TRACE_TXN, "atom %u trying to commit %u: CAPTURE_WAIT\n",
		 (*atom)->atom_id, current->pid);

	/* call reiser4_update_sd for all atom's inodes */
	/*atom_update_stat_data(atom);*/

	for (flushiters = 0 ;; ++ flushiters) {
		ret = flush_current_atom(JNODE_FLUSH_WRITE_BLOCKS | JNODE_FLUSH_COMMIT, nr_submitted, atom);
		if (ret != -E_REPEAT)
			break;

		/* if atom's dirty list contains one znode which is
		   HEARD_BANSHEE and is locked we have to allow lock owner to
		   continue and uncapture that znode */
		preempt_point();

		*atom = get_current_atom_locked();
		if (flushiters > TOOMANYFLUSHES && IS_POW(flushiters)) {
			warning("nikita-3176",
				"Flushing like mad: %i", flushiters);
			info_atom("atom", *atom);
			DEBUGON(flushiters > (1 << 20));
		}
	}

	if (ret)
		return ret;

	assert ("zam-882", spin_atom_is_locked(*atom));

	if (!atom_can_be_committed(*atom)) {
		UNLOCK_ATOM(*atom);
		reiser4_stat_inc(txnmgr.restart.cannot_commit);
		return RETERR(-E_REPEAT);
	}

	/* Up to this point we have been flushing and after flush is called we
	   return -E_REPEAT.  Now we can commit.  We cannot return -E_REPEAT
	   at this point, commit should be successful. */
	atom_set_stage(*atom, ASTAGE_PRE_COMMIT);
	ON_DEBUG(((*atom)->committer = current));

	ON_TRACE(TRACE_TXN, "commit atom %u: PRE_COMMIT\n", (*atom)->atom_id);
	ON_TRACE(TRACE_FLUSH, "everything flushed atom %u: PRE_COMMIT\n", (*atom)->atom_id);

	UNLOCK_ATOM(*atom);

	ret = current_atom_complete_writes();
	if (ret)
		return ret;

	assert ("zam-906", capture_list_empty(ATOM_WB_LIST(*atom)));

	ON_TRACE(TRACE_FLUSH, "everything written back atom %u\n",
		 (*atom)->atom_id);

	/* isolate critical code path which should be executed by only one
	 * thread using tmgr semaphore */
	down(&sbinfo->tmgr.commit_semaphore);

	ret = reiser4_write_logs(nr_submitted);
	if (ret < 0)
		reiser4_panic("zam-597", "write log failed (%ld)\n", ret);

	/* The atom->ovrwr_nodes list is processed under commit semaphore held
	   because of bitmap nodes which are captured by special way in
	   bitmap_pre_commit_hook(), that way does not include
	   capture_fuse_wait() as a capturing of other nodes does -- the commit
	   semaphore is used for transaction isolation instead. */
	invalidate_list(ATOM_OVRWR_LIST(*atom));
	up(&sbinfo->tmgr.commit_semaphore);

	invalidate_list(ATOM_CLEAN_LIST(*atom));
	invalidate_list(ATOM_WB_LIST(*atom));
	assert("zam-927", capture_list_empty(&(*atom)->inodes));

	LOCK_ATOM(*atom);
	atom_set_stage(*atom, ASTAGE_DONE);
	ON_DEBUG((*atom)->committer = 0);

	/* Atom's state changes, so wake up everybody waiting for this
	   event. */
	wakeup_atom_waiting_list(*atom);

	/* Decrement the "until commit" reference, at least one txnh (the caller) is
	   still open. */
	atomic_dec(&(*atom)->refcount);

	assert("jmacd-1070", atomic_read(&(*atom)->refcount) > 0);
	assert("jmacd-1062", (*atom)->capture_count == 0);
	BUG_ON((*atom)->capture_count != 0);
	assert("jmacd-1071", spin_atom_is_locked(*atom));

	ON_TRACE(TRACE_TXN, "commit atom finished %u refcount %d\n",
		 (*atom)->atom_id, atomic_read(&(*atom)->refcount));
	return ret;
}

/* TXN_TXNH */

/* commit current atom and wait commit completion; atom and txn_handle should be
 * locked before call, this function unlocks them on exit. */
static int force_commit_atom_nolock (txn_handle * txnh)
{
	txn_atom * atom;

	assert ("zam-837", txnh != NULL);
	assert ("zam-835", spin_txnh_is_locked(txnh));
	assert ("nikita-2966", lock_stack_isclean(get_current_lock_stack()));

	atom = txnh->atom;

	assert ("zam-834", atom != NULL);
	assert ("zam-836", spin_atom_is_locked(atom));

	/* Set flags for atom and txnh: forcing atom commit and waiting for
	 * commit completion */
	txnh->flags |= TXNH_WAIT_COMMIT;
	atom->flags |= ATOM_FORCE_COMMIT;

	UNLOCK_TXNH(txnh);
	UNLOCK_ATOM(atom);

	txn_restart_current();
	return 0;
}

/* externally visible function which takes all necessary locks and commits
 * current atom */
reiser4_internal int txnmgr_force_commit_current_atom (void)
{
	txn_handle * txnh = get_current_context()->trans;
	txn_atom * atom;

	atom = txnh_get_atom(txnh);

	if (atom == NULL) {
		UNLOCK_TXNH(txnh);
		return 0;
	}

	return force_commit_atom_nolock(txnh);
}

/* Called to force commit of any outstanding atoms.  @commit_all_atoms controls
 * should we commit all atoms including new ones which are created after this
 * functions is called. */
reiser4_internal int
txnmgr_force_commit_all (struct super_block *super, int commit_all_atoms)
{
	int ret;
	txn_atom *atom;
	txn_mgr *mgr;
	txn_handle *txnh;
	unsigned long start_time = jiffies;
	reiser4_context * ctx = get_current_context();

	assert("nikita-2965", lock_stack_isclean(get_current_lock_stack()));
	assert("nikita-3058", commit_check_locks());

	txn_restart(ctx);

	mgr = &get_super_private(super)->tmgr;

	txnh = ctx->trans;

again:

	spin_lock_txnmgr(mgr);

	for_all_type_safe_list(atom, &mgr->atoms_list, atom) {
		LOCK_ATOM(atom);

		/* Commit any atom which can be committed.  If @commit_new_atoms
		 * is not set we commit only atoms which were created before
		 * this call is started. */
		if (commit_all_atoms || time_before_eq(atom->start_time, start_time)) {
			if (atom->stage <= ASTAGE_POST_COMMIT) {
				spin_unlock_txnmgr(mgr);

				if (atom->stage < ASTAGE_PRE_COMMIT) {
					LOCK_TXNH(txnh);
					/* Add force-context txnh */
					capture_assign_txnh_nolock(atom, txnh);
					ret = force_commit_atom_nolock(txnh);
					if(ret)
						return ret;
				} else
					/* wait atom commit */
					atom_wait_event(atom);

				goto again;
			}
		}

		UNLOCK_ATOM(atom);
	}

#if REISER4_DEBUG
	if (commit_all_atoms) {
		reiser4_super_info_data * sbinfo = get_super_private(super);
		reiser4_spin_lock_sb(sbinfo);
		assert("zam-813", sbinfo->blocks_fake_allocated_unformatted == 0);
		assert("zam-812", sbinfo->blocks_fake_allocated == 0);
		reiser4_spin_unlock_sb(sbinfo);
	}
#endif

	spin_unlock_txnmgr(mgr);

	return 0;
}

/* check whether commit_some_atoms() can commit @atom. Locking is up to the
 * caller */
static int atom_is_committable(txn_atom *atom)
{
	return
		atom->stage < ASTAGE_PRE_COMMIT &&
		atom->txnh_count == atom->nr_waiters &&
		atom_should_commit(atom);
}

/* called periodically from ktxnmgrd to commit old atoms. Releases ktxnmgrd spin
 * lock at exit */
reiser4_internal int
commit_some_atoms(txn_mgr * mgr)
{
	int ret = 0;
	txn_atom *atom;
	txn_atom *next_atom;
	txn_handle *txnh;
	reiser4_context *ctx;

	ctx = get_current_context();
	assert("nikita-2444", ctx != NULL);

	txnh = ctx->trans;
	spin_lock_txnmgr(mgr);

	/* look for atom to commit */
	for_all_type_safe_list_safe(atom, &mgr->atoms_list, atom, next_atom) {
		/* first test without taking atom spin lock, whether it is
		 * eligible for committing at all */
		if (atom_is_committable(atom)) {
			/* now, take spin lock and re-check */
			LOCK_ATOM(atom);
			if (atom_is_committable(atom))
				break;
			UNLOCK_ATOM(atom);
		}
	}

	ret = atom_list_end(&mgr->atoms_list, atom);
	spin_unlock_txnmgr(mgr);

	if (ret) {
		/* nothing found */
		spin_unlock(&mgr->daemon->guard);
		return 0;
	}

	LOCK_TXNH(txnh);

	/* Set the atom to force committing */
	atom->flags |= ATOM_FORCE_COMMIT;

	/* Add force-context txnh */
	capture_assign_txnh_nolock(atom, txnh);

	UNLOCK_TXNH(txnh);
	UNLOCK_ATOM(atom);

	/* we are about to release daemon spin lock, notify daemon it
	   has to rescan atoms */
	mgr->daemon->rescan = 1;
	spin_unlock(&mgr->daemon->guard);
	txn_restart(ctx);
	return 0;
}

/* Calls jnode_flush for current atom if it exists; if not, just take another
   atom and call jnode_flush() for him.  If current transaction handle has
   already assigned atom (current atom) we have to close current transaction
   prior to switch to another atom or do something with current atom. This
   code tries to flush current atom.

   flush_some_atom() is called as part of memory clearing process. It is
   invoked from balance_dirty_pages(), pdflushd, and entd.

   If we can flush no nodes, atom is committed, because this frees memory.

   If atom is too large or too old it is committed also.
*/
reiser4_internal int
flush_some_atom(long *nr_submitted, const struct writeback_control *wbc, int flags)
{
	reiser4_context *ctx = get_current_context();
	txn_handle *txnh = ctx->trans;
	txn_atom *atom;
	int ret;
	int ret1;

	assert("zam-1042", txnh != NULL);
 repeat:
	if (txnh->atom == NULL) {
		/* current atom is available, take first from txnmgr */
		txn_mgr *tmgr = &get_super_private(ctx->super)->tmgr;

		spin_lock_txnmgr(tmgr);

		/* traverse the list of all atoms */
		for_all_type_safe_list(atom, &tmgr->atoms_list, atom) {
			/* lock atom before checking its state */
			LOCK_ATOM(atom);

			/* we need an atom which is not being committed and which has no
			 * flushers (jnode_flush() add one flusher at the beginning and
			 * subtract one at the end). */
			if (atom->stage < ASTAGE_PRE_COMMIT && atom->nr_flushers == 0) {
				LOCK_TXNH(txnh);
				capture_assign_txnh_nolock(atom, txnh);
				UNLOCK_TXNH(txnh);

				goto found;
			}

			UNLOCK_ATOM(atom);
		}

		/* Write throttling is case of no one atom can be
		 * flushed/committed.  */
		if (!current_is_pdflush() && !wbc->nonblocking) {
			for_all_type_safe_list(atom, &tmgr->atoms_list, atom) {
				LOCK_ATOM(atom);
				/* Repeat the check from the above. */
				if (atom->stage < ASTAGE_PRE_COMMIT && atom->nr_flushers == 0) {
					LOCK_TXNH(txnh);
					capture_assign_txnh_nolock(atom, txnh);
					UNLOCK_TXNH(txnh);

					goto found;
				}
				if (atom->stage <= ASTAGE_POST_COMMIT) {
					spin_unlock_txnmgr(tmgr);
					/* we just wait until atom's flusher
					   makes a progress in flushing or
					   committing the atom */
					atom_wait_event(atom);
					goto repeat;
				}
				UNLOCK_ATOM(atom);
			}
		}
		spin_unlock_txnmgr(tmgr);
		return 0;
	found:
		spin_unlock_txnmgr(tmgr);
	} else
		atom = get_current_atom_locked();

	ret = flush_current_atom(flags, nr_submitted, &atom);
	if (ret == 0) {
		if (*nr_submitted == 0 || atom_should_commit_asap(atom)) {
			/* if early flushing could not make more nodes clean,
			 * or atom is too old/large,
			 * we force current atom to commit */
			/* wait for commit completion but only if this
			 * wouldn't stall pdflushd and ent thread. */
			if (!wbc->nonblocking && !ctx->entd)
				txnh->flags |= TXNH_WAIT_COMMIT;
			atom->flags |= ATOM_FORCE_COMMIT;
		}
		UNLOCK_ATOM(atom);
	} else if (ret == -E_REPEAT) {
		if (*nr_submitted == 0)
			goto repeat;
		ret = 0;
	}

	ret1 = txn_end(ctx);
	assert("vs-1692", ret1 == 0);
	if (ret1 > 0)
		*nr_submitted += ret1;
	txn_begin(ctx);

	return ret;
}

#if REISER4_COPY_ON_CAPTURE

/* Remove processed nodes from atom's clean list (thereby remove them from transaction). */
void
invalidate_list(capture_list_head * head)
{
	txn_atom *atom;

	spin_lock(&scan_lock);
	while (!capture_list_empty(head)) {
		jnode *node;

		node = capture_list_front(head);
		JF_SET(node, JNODE_SCANNED);
		spin_unlock(&scan_lock);

		atom = node->atom;
		LOCK_ATOM(atom);
		LOCK_JNODE(node);
		if (JF_ISSET(node, JNODE_CC) && node->pg) {
			/* corresponding page_cache_get is in swap_jnode_pages */
			assert("vs-1448", test_and_clear_bit(PG_arch_1, &node->pg->flags));
			page_cache_release(node->pg);
		}
		uncapture_block(node);
		UNLOCK_ATOM(atom);
		JF_CLR(node, JNODE_SCANNED);
		jput(node);

		spin_lock(&scan_lock);
	}
	spin_unlock(&scan_lock);
}

#else

/* Remove processed nodes from atom's clean list (thereby remove them from transaction). */
void
invalidate_list(capture_list_head * head)
{
	while (!capture_list_empty(head)) {
		jnode *node;

		node = capture_list_front(head);
		LOCK_JNODE(node);
		uncapture_block(node);
		jput(node);
	}
}

#endif

static void
init_wlinks(txn_wait_links * wlinks)
{
	wlinks->_lock_stack = get_current_lock_stack();
	fwaitfor_list_clean(wlinks);
	fwaiting_list_clean(wlinks);
	wlinks->waitfor_cb = NULL;
	wlinks->waiting_cb = NULL;
}

/* Add atom to the atom's waitfor list and wait for somebody to wake us up; */
reiser4_internal void atom_wait_event(txn_atom * atom)
{
	txn_wait_links _wlinks;

	assert("zam-744", spin_atom_is_locked(atom));
	assert("nikita-3156",
	       lock_stack_isclean(get_current_lock_stack()) ||
	       atom->nr_running_queues > 0);

	init_wlinks(&_wlinks);
	fwaitfor_list_push_back(&atom->fwaitfor_list, &_wlinks);
	atomic_inc(&atom->refcount);
	UNLOCK_ATOM(atom);

	/* assert("nikita-3056", commit_check_locks()); */
	prepare_to_sleep(_wlinks._lock_stack);
	go_to_sleep(_wlinks._lock_stack, ADD_TO_SLEPT_IN_WAIT_EVENT);

	LOCK_ATOM (atom);
	fwaitfor_list_remove(&_wlinks);
	atom_dec_and_unlock (atom);
}

reiser4_internal void
atom_set_stage(txn_atom *atom, txn_stage stage)
{
	assert("nikita-3535", atom != NULL);
	assert("nikita-3538", spin_atom_is_locked(atom));
	assert("nikita-3536", ASTAGE_FREE <= stage && stage <= ASTAGE_INVALID);
	/* Excelsior! */
	assert("nikita-3537", stage >= atom->stage);
	if (atom->stage != stage) {
		atom->stage = stage;
		atom_send_event(atom);
	}
}

/* wake all threads which wait for an event */
reiser4_internal void
atom_send_event(txn_atom * atom)
{
	assert("zam-745", spin_atom_is_locked(atom));
	wakeup_atom_waitfor_list(atom);
}

/* Informs txn manager code that owner of this txn_handle should wait atom commit completion (for
   example, because it does fsync(2)) */
static int
should_wait_commit(txn_handle * h)
{
	return h->flags & TXNH_WAIT_COMMIT;
}

typedef struct commit_data {
	txn_atom    *atom;
	txn_handle  *txnh;
	long         nr_written;
	/* as an optimization we start committing atom by first trying to
	 * flush it few times without switching into ASTAGE_CAPTURE_WAIT. This
	 * allows to reduce stalls due to other threads waiting for atom in
	 * ASTAGE_CAPTURE_WAIT stage. ->preflush is counter of these
	 * preliminary flushes. */
	int          preflush;
	/* have we waited on atom. */
	int          wait;
	int          failed;
	int          wake_ktxnmgrd_up;
} commit_data;

/*
 * Called from commit_txnh() repeatedly, until either error happens, or atom
 * commits successfully.
 */
static int
try_commit_txnh(commit_data *cd)
{
	int result;

	assert("nikita-2968", lock_stack_isclean(get_current_lock_stack()));

	/* Get the atom and txnh locked. */
	cd->atom = txnh_get_atom(cd->txnh);
	assert("jmacd-309", cd->atom != NULL);
	UNLOCK_TXNH(cd->txnh);

	if (cd->wait) {
		cd->atom->nr_waiters --;
		cd->wait = 0;
	}

	if (cd->atom->stage == ASTAGE_DONE)
		return 0;

	ON_TRACE(TRACE_TXN,
		 "commit_txnh: atom %u failed %u; txnh_count %u; should_commit %u\n",
		 cd->atom->atom_id, cd->failed, cd->atom->txnh_count,
		 atom_should_commit(cd->atom));

	if (cd->failed)
		return 0;

	if (atom_should_commit(cd->atom)) {
		/* if atom is  _very_ large schedule it for  common as soon as
		 * possible. */
		if (atom_should_commit_asap(cd->atom)) {
			/*
			 * When atom is in PRE_COMMIT or later stage following
			 * invariant (encoded   in    atom_can_be_committed())
			 * holds:  there is exactly one non-waiter transaction
			 * handle opened  on this atom.  When  thread wants to
			 * wait  until atom  commits (for  example  sync()) it
			 * waits    on    atom  event     after     increasing
			 * atom->nr_waiters (see blow  in  this  function). It
			 * cannot be guaranteed that atom is already committed
			 * after    receiving event,  so     loop has   to  be
			 * re-started. But  if  atom switched into  PRE_COMMIT
			 * stage and became  too  large, we cannot  change its
			 * state back   to CAPTURE_WAIT (atom  stage can  only
			 * increase monotonically), hence this check.
			 */
			if (cd->atom->stage < ASTAGE_CAPTURE_WAIT)
				atom_set_stage(cd->atom, ASTAGE_CAPTURE_WAIT);
			cd->atom->flags |= ATOM_FORCE_COMMIT;
		}
		if (cd->txnh->flags & TXNH_DONT_COMMIT) {
			/*
			 * this  thread (transaction  handle  that is) doesn't
			 * want to commit  atom. Notify waiters that handle is
			 * closed. This can happen, for  example, when we  are
			 * under  VFS directory lock  and don't want to commit
			 * atom  right   now to  avoid  stalling other threads
			 * working in the same directory.
			 */

			/* Wake  the ktxnmgrd up if  the ktxnmgrd is needed to
			 * commit this  atom: no  atom  waiters  and only  one
			 * (our) open transaction handle. */
			cd->wake_ktxnmgrd_up =
				cd->atom->txnh_count == 1 &&
				cd->atom->nr_waiters == 0;
			atom_send_event(cd->atom);
			result = 0;
		} else if (!atom_can_be_committed(cd->atom)) {
			if (should_wait_commit(cd->txnh)) {
				/* sync(): wait for commit */
				cd->atom->nr_waiters++;
				cd->wait = 1;
				atom_wait_event(cd->atom);
				reiser4_stat_inc(txnmgr.restart.should_wait);
				result = RETERR(-E_REPEAT);
			} else {
				result = 0;
			}
		} else if (cd->preflush > 0 && !is_current_ktxnmgrd()) {
			/*
			 * optimization: flush  atom without switching it into
			 * ASTAGE_CAPTURE_WAIT.
			 *
			 * But don't  do this for  ktxnmgrd, because  ktxnmgrd
			 * should never block on atom fusion.
			 */
			result = flush_current_atom(JNODE_FLUSH_WRITE_BLOCKS,
						    &cd->nr_written, &cd->atom);
			if (result == 0) {
				UNLOCK_ATOM(cd->atom);
				cd->preflush = 0;
				reiser4_stat_inc(txnmgr.restart.flush);
				result = RETERR(-E_REPEAT);
			} else	/* Atoms wasn't flushed
				 * completely. Rinse. Repeat. */
				-- cd->preflush;
		} else {
			/* We change   atom state  to   ASTAGE_CAPTURE_WAIT to
			   prevent atom fusion and count  ourself as an active
			   flusher */
			atom_set_stage(cd->atom, ASTAGE_CAPTURE_WAIT);
			cd->atom->flags |= ATOM_FORCE_COMMIT;

			result = commit_current_atom(&cd->nr_written, &cd->atom);
			if (result != 0 && result != -E_REPEAT)
				cd->failed = 1;
		}
	} else
		result = 0;

	assert("jmacd-1027", ergo(result == 0, spin_atom_is_locked(cd->atom)));
	/* perfectly valid assertion, except that when atom/txnh is not locked
	 * fusion can take place, and cd->atom points nowhere. */
	/*
	  assert("jmacd-1028", ergo(result != 0, spin_atom_is_not_locked(cd->atom)));
	*/
	return result;
}

/* Called to commit a transaction handle.  This decrements the atom's number of open
   handles and if it is the last handle to commit and the atom should commit, initiates
   atom commit. if commit does not fail, return number of written blocks */
static long
commit_txnh(txn_handle * txnh)
{
	commit_data cd;
	assert("umka-192", txnh != NULL);

	xmemset(&cd, 0, sizeof cd);
	cd.txnh = txnh;
	cd.preflush = 10;

	/* calls try_commit_txnh() until either atom commits, or error
	 * happens */
	while (try_commit_txnh(&cd) != 0)
		preempt_point();

	assert("nikita-3171", spin_txnh_is_not_locked(txnh));
	LOCK_TXNH(txnh);

	cd.atom->txnh_count -= 1;
	txnh->atom = NULL;

	txnh_list_remove(txnh);

	ON_TRACE(TRACE_TXN, "close txnh atom %u refcount %d\n",
		 cd.atom->atom_id, atomic_read(&cd.atom->refcount));

	UNLOCK_TXNH(txnh);
	atom_dec_and_unlock(cd.atom);
	/* if we don't want to do a commit (TXNH_DONT_COMMIT is set, probably
	 * because it takes time) by current thread, we do that work
	 * asynchronously by ktxnmgrd daemon. */
	if (cd.wake_ktxnmgrd_up)
		ktxnmgrd_kick(&get_current_super_private()->tmgr);

	return 0;
}

/* TRY_CAPTURE */

/* This routine attempts a single block-capture request.  It may return -E_REPEAT if some
   condition indicates that the request should be retried, and it may block if the
   txn_capture mode does not include the TXN_CAPTURE_NONBLOCKING request flag.

   This routine encodes the basic logic of block capturing described by:

     http://namesys.com/v4/v4.html

   Our goal here is to ensure that any two blocks that contain dependent modifications
   should commit at the same time.  This function enforces this discipline by initiating
   fusion whenever a transaction handle belonging to one atom requests to read or write a
   block belonging to another atom (TXN_CAPTURE_WRITE or TXN_CAPTURE_READ_ATOMIC).

   In addition, this routine handles the initial assignment of atoms to blocks and
   transaction handles.  These are possible outcomes of this function:

   1. The block and handle are already part of the same atom: return immediate success

   2. The block is assigned but the handle is not: call capture_assign_txnh to assign
      the handle to the block's atom.

   3. The handle is assigned but the block is not: call capture_assign_block to assign
      the block to the handle's atom.

   4. Both handle and block are assigned, but to different atoms: call capture_init_fusion
      to fuse atoms.

   5. Neither block nor handle are assigned: create a new atom and assign them both.

   6. A read request for a non-captured block: return immediate success.

   This function acquires and releases the handle's spinlock.  This function is called
   under the jnode lock and if the return value is 0, it returns with the jnode lock still
   held.  If the return is -E_REPEAT or some other error condition, the jnode lock is
   released.  The external interface (try_capture) manages re-aquiring the jnode lock
   in the failure case.
*/

static int
try_capture_block(txn_handle * txnh, jnode * node, txn_capture mode, txn_atom ** atom_alloc, int can_coc)
{
	int ret;
	txn_atom *block_atom;
	txn_atom *txnh_atom;

	/* Should not call capture for READ_NONCOM requests, handled in try_capture. */
	assert("jmacd-567", CAPTURE_TYPE(mode) != TXN_CAPTURE_READ_NONCOM);

	/* FIXME-ZAM-HANS: FIXME_LATER_JMACD Should assert that atom->tree == node->tree somewhere. */

	assert("umka-194", txnh != NULL);
	assert("umka-195", node != NULL);

	/* The jnode is already locked!  Being called from try_capture(). */
	assert("jmacd-567", spin_jnode_is_locked(node));

	block_atom = node->atom;

	/* Get txnh spinlock, this allows us to compare txn_atom pointers but it doesn't
	   let us touch the atoms themselves. */
	LOCK_TXNH(txnh);

	txnh_atom = txnh->atom;

	if (REISER4_STATS) {
		if (block_atom != NULL && txnh_atom != NULL)
			if (block_atom == txnh_atom)
				reiser4_stat_inc(txnmgr.capture_equal);
			else
				reiser4_stat_inc(txnmgr.capture_both);
		else if (block_atom != NULL && txnh_atom == NULL)
			reiser4_stat_inc(txnmgr.capture_block);
		else if (block_atom == NULL && txnh_atom != NULL)
			reiser4_stat_inc(txnmgr.capture_txnh);
		else
			reiser4_stat_inc(txnmgr.capture_none);
	}

	if (txnh_atom != NULL && block_atom == txnh_atom) {
		UNLOCK_TXNH(txnh);
		return 0;
	}
	/* NIKITA-HANS: nothing */
	if (txnh_atom != NULL) {
		/* It is time to perform deadlock prevention check over the
		   node we want to capture.  It is possible this node was
		   locked for read without capturing it. The optimization
		   which allows to do it helps us in keeping atoms independent
		   as long as possible but it may cause lock/fuse deadlock
		   problems.

		   A number of similar deadlock situations with locked but not
		   captured nodes were found.  In each situation there are two
		   or more threads: one of them does flushing while another
		   one does routine balancing or tree lookup.  The flushing
		   thread (F) sleeps in long term locking request for node
		   (N), another thread (A) sleeps in trying to capture some
		   node already belonging the atom F, F has a state which
		   prevents immediately fusion .

		   Deadlocks of this kind cannot happen if node N was properly
		   captured by thread A. The F thread fuse atoms before
		   locking therefore current atom of thread F and current atom
		   of thread A became the same atom and thread A may proceed.
		   This does not work if node N was not captured because the
		   fusion of atom does not happens.

		   The following scheme solves the deadlock: If
		   longterm_lock_znode locks and does not capture a znode,
		   that znode is marked as MISSED_IN_CAPTURE.  A node marked
		   this way is processed by the code below which restores the
		   missed capture and fuses current atoms of all the node lock
		   owners by calling the fuse_not_fused_lock_owners()
		   function.
		*/

		if (		// txnh_atom->stage >= ASTAGE_CAPTURE_WAIT &&
			   jnode_is_znode(node) && znode_is_locked(JZNODE(node))
			   && JF_ISSET(node, JNODE_MISSED_IN_CAPTURE)) {
			JF_CLR(node, JNODE_MISSED_IN_CAPTURE);

			ret = fuse_not_fused_lock_owners(txnh, JZNODE(node));

			if (ret) {
				JF_SET(node, JNODE_MISSED_IN_CAPTURE);

				assert("zam-687", spin_txnh_is_not_locked(txnh));
				assert("zam-688", spin_jnode_is_not_locked(node));

				return ret;
			}

			assert("zam-701", spin_txnh_is_locked(txnh));
			assert("zam-702", spin_jnode_is_locked(node));
		}
	}

	if (block_atom != NULL) {
		/* The block has already been assigned to an atom. */

		/* case (block_atom == txnh_atom) is already handled above */
		if (txnh_atom == NULL) {

			/* The txnh is unassigned, try to assign it. */
			ret = capture_assign_txnh(node, txnh, mode, can_coc);
			if (ret != 0) {
				/* E_REPEAT or otherwise */
				assert("jmacd-6129", spin_txnh_is_not_locked(txnh));
				assert("jmacd-6130", spin_jnode_is_not_locked(node));
				return ret;
			}

			/* Either the txnh is now assigned to the block's atom or the read-request was
			   granted because the block is committing.  Locks still held. */
		} else {
			if (mode & TXN_CAPTURE_DONT_FUSE) {
				UNLOCK_TXNH(txnh);
				UNLOCK_JNODE(node);
				/* we are in a "no-fusion" mode and @node is
				 * already part of transaction. */
				return RETERR(-E_NO_NEIGHBOR);
			}
			/* In this case, both txnh and node belong to different atoms.  This function
			   returns -E_REPEAT on successful fusion, 0 on the fall-through case. */
			ret = capture_init_fusion(node, txnh, mode, can_coc);
			if (ret != 0) {
				assert("jmacd-6131", spin_txnh_is_not_locked(txnh));
				assert("jmacd-6132", spin_jnode_is_not_locked(node));
				return ret;
			}

			/* The fall-through case is read request for committing block.  Locks still
			   held. */
		}

	} else if ((mode & TXN_CAPTURE_WTYPES) != 0) {

		/* In this case, the page is unlocked and the txnh wishes exclusive access. */

		if (txnh_atom != NULL) {
			/* The txnh is already assigned: add the page to its atom. */
			ret = capture_assign_block(txnh, node);
			if (ret != 0) {
				/* E_REPEAT or otherwise */
				assert("jmacd-6133", spin_txnh_is_not_locked(txnh));
				assert("jmacd-6134", spin_jnode_is_not_locked(node));
				return ret;
			}

			/* Success: Locks are still held. */

		} else {

			/* In this case, neither txnh nor page are assigned to an atom. */
			block_atom = atom_begin_andlock(atom_alloc, node, txnh);

			if (!IS_ERR(block_atom)) {
				/* Assign both, release atom lock. */
				assert("jmacd-125", block_atom->stage == ASTAGE_CAPTURE_FUSE);

				capture_assign_txnh_nolock(block_atom, txnh);
				capture_assign_block_nolock(block_atom, node);

				UNLOCK_ATOM(block_atom);
			} else {
				/* all locks are released already */
				return PTR_ERR(block_atom);
			}

			/* Success: Locks are still held. */
		}

	} else {
		/* The jnode is uncaptured and its a read request -- fine. */
		assert("jmacd-411", CAPTURE_TYPE(mode) == TXN_CAPTURE_READ_ATOMIC);
	}

	/* Successful case: both jnode and txnh are still locked. */
	assert("jmacd-740", spin_txnh_is_locked(txnh));
	assert("jmacd-741", spin_jnode_is_locked(node));

	/* Release txnh lock, return with the jnode still locked. */
	UNLOCK_TXNH(txnh);

	return 0;
}

reiser4_internal txn_capture
build_capture_mode(jnode * node, znode_lock_mode lock_mode, txn_capture flags)
{
	txn_capture cap_mode;

	assert("nikita-3187", spin_jnode_is_locked(node));

	/* FIXME_JMACD No way to set TXN_CAPTURE_READ_MODIFY yet. */

	if (lock_mode == ZNODE_WRITE_LOCK) {
		cap_mode = TXN_CAPTURE_WRITE;
	} else if (node->atom != NULL) {
		cap_mode = TXN_CAPTURE_WRITE;
	} else if (0 && /* txnh->mode == TXN_READ_FUSING && */
		   jnode_get_level(node) == LEAF_LEVEL) {
		/* NOTE-NIKITA TXN_READ_FUSING is not currently used */
		/* We only need a READ_FUSING capture at the leaf level.  This
		   is because the internal levels of the tree (twigs included)
		   are redundant from the point of the user that asked for a
		   read-fusing transcrash.  The user only wants to read-fuse
		   atoms due to reading uncommitted data that another user has
		   written.  It is the file system that reads/writes the
		   internal tree levels, the user only reads/writes leaves. */
		cap_mode = TXN_CAPTURE_READ_ATOMIC;
	} else {
		/* In this case (read lock at a non-leaf) there's no reason to
		 * capture. */
		/* cap_mode = TXN_CAPTURE_READ_NONCOM; */

		/* Mark this node as "MISSED".  It helps in further deadlock
		 * analysis */
		JF_SET(node, JNODE_MISSED_IN_CAPTURE);
		return 0;
	}

	cap_mode |= (flags & (TXN_CAPTURE_NONBLOCKING |
			      TXN_CAPTURE_DONT_FUSE));
	assert("nikita-3186", cap_mode != 0);
	return cap_mode;
}

/* This is an external interface to try_capture_block(), it calls
   try_capture_block() repeatedly as long as -E_REPEAT is returned.

   @node:         node to capture,
   @lock_mode:    read or write lock is used in capture mode calculation,
   @flags:        see txn_capture flags enumeration,
   @can_coc     : can copy-on-capture

   @return: 0 - node was successfully captured, -E_REPEAT - capture request
            cannot be processed immediately as it was requested in flags,
	    < 0 - other errors.
*/
reiser4_internal int
try_capture(jnode * node,  znode_lock_mode lock_mode,
	    txn_capture flags, int can_coc)
{
	txn_atom    *atom_alloc = NULL;
	txn_capture cap_mode;
	txn_handle * txnh = get_current_context()->trans;
#if REISER4_COPY_ON_CAPTURE
	int coc_enabled = 1;
#endif
	int ret;

	assert("jmacd-604", spin_jnode_is_locked(node));

repeat:
	cap_mode = build_capture_mode(node, lock_mode, flags);
	if (cap_mode == 0)
		return 0;

	/* Repeat try_capture as long as -E_REPEAT is returned. */
#if REISER4_COPY_ON_CAPTURE
	ret = try_capture_block(txnh, node, cap_mode, &atom_alloc, can_coc && coc_enabled);
	coc_enabled = 1;
#else
	ret = try_capture_block(txnh, node, cap_mode, &atom_alloc, can_coc);
#endif
	/* Regardless of non_blocking:

	   If ret == 0 then jnode is still locked.
	   If ret != 0 then jnode is unlocked.
	*/
	assert("nikita-2674", ergo(ret == 0, spin_jnode_is_locked(node)));
	assert("nikita-2675", ergo(ret != 0, spin_jnode_is_not_locked(node)));

	assert("nikita-2974", spin_txnh_is_not_locked(txnh));

	if (ret == -E_REPEAT) {
		/* E_REPEAT implies all locks were released, therefore we need
		   to take the jnode's lock again. */
		LOCK_JNODE(node);

		/* Although this may appear to be a busy loop, it is not.
		   There are several conditions that cause E_REPEAT to be
		   returned by the call to try_capture_block, all cases
		   indicating some kind of state change that means you should
		   retry the request and will get a different result.  In some
		   cases this could be avoided with some extra code, but
		   generally it is done because the necessary locks were
		   released as a result of the operation and repeating is the
		   simplest thing to do (less bug potential).  The cases are:
		   atom fusion returns E_REPEAT after it completes (jnode and
		   txnh were unlocked); race conditions in assign_block,
		   assign_txnh, and init_fusion return E_REPEAT (trylock
		   failure); after going to sleep in capture_fuse_wait
		   (request was blocked but may now succeed).  I'm not quite
		   sure how capture_copy works yet, but it may also return
		   E_REPEAT.  When the request is legitimately blocked, the
		   requestor goes to sleep in fuse_wait, so this is not a busy
		   loop. */
		/* NOTE-NIKITA: still don't understand:

		   try_capture_block->capture_assign_txnh->spin_trylock_atom->E_REPEAT

		   looks like busy loop?
		*/
		goto repeat;
	}

#if REISER4_COPY_ON_CAPTURE
	if (ret == -E_WAIT) {
		reiser4_stat_inc(coc.coc_wait);
		/* disable COC for the next loop iteration */
		coc_enabled = 0;
		LOCK_JNODE(node);
		goto repeat;
	}
#endif

	/* free extra atom object that was possibly allocated by
	   try_capture_block().

	   Do this before acquiring jnode spin lock to
	   minimize time spent under lock. --nikita */
	if (atom_alloc != NULL) {
		kmem_cache_free(_atom_slab, atom_alloc);
	}

	if (ret != 0) {
		if (ret == -E_BLOCK) {
			assert("nikita-3360", cap_mode & TXN_CAPTURE_NONBLOCKING);
			ret = -E_REPEAT;
		}

		/* Failure means jnode is not locked.  FIXME_LATER_JMACD May
		   want to fix the above code to avoid releasing the lock and
		   re-acquiring it, but there are cases were failure occurs
		   when the lock is not held, and those cases would need to be
		   modified to re-take the lock. */
		LOCK_JNODE(node);
	}

	/* Jnode is still locked. */
	assert("jmacd-760", spin_jnode_is_locked(node));
	return ret;
}

/* This function sets up a call to try_capture_block and repeats as long as -E_REPEAT is
   returned by that routine.  The txn_capture request mode is computed here depending on
   the transaction handle's type and the lock request.  This is called from the depths of
   the lock manager with the jnode lock held and it always returns with the jnode lock
   held.
*/

/* fuse all 'active' atoms of lock owners of given node. */
static int
fuse_not_fused_lock_owners(txn_handle * txnh, znode * node)
{
	lock_handle *lh;
	int repeat = 0;
	txn_atom *atomh = txnh->atom;

/*	assert ("zam-689", znode_is_rlocked (node));*/
	assert("zam-690", spin_znode_is_locked(node));
	assert("zam-691", spin_txnh_is_locked(txnh));
	assert("zam-692", atomh != NULL);

	RLOCK_ZLOCK(&node->lock);

	if (!spin_trylock_atom(atomh)) {
		repeat = 1;
		goto fail;
	}

	/* inspect list of lock owners */
	for_all_type_safe_list(owners, &node->lock.owners, lh) {
		reiser4_context *ctx;
		txn_atom *atomf;

		ctx = get_context_by_lock_stack(lh->owner);

		if (ctx == get_current_context())
			continue;

		if (!spin_trylock_txnh(ctx->trans)) {
			repeat = 1;
			continue;
		}

		atomf = ctx->trans->atom;

		if (atomf == NULL) {
			capture_assign_txnh_nolock(atomh, ctx->trans);
			UNLOCK_TXNH(ctx->trans);

			reiser4_wake_up(lh->owner);
			continue;
		}

		if (atomf == atomh) {
			UNLOCK_TXNH(ctx->trans);
			continue;
		}

		if (!spin_trylock_atom(atomf)) {
			UNLOCK_TXNH(ctx->trans);
			repeat = 1;
			continue;
		}

		UNLOCK_TXNH(ctx->trans);

		if (atomf == atomh || atomf->stage > ASTAGE_CAPTURE_WAIT) {
			UNLOCK_ATOM(atomf);
			continue;
		}
		// repeat = 1;

		reiser4_wake_up(lh->owner);

		UNLOCK_TXNH(txnh);
		RUNLOCK_ZLOCK(&node->lock);
		spin_unlock_znode(node);

		/* @atomf is "small" and @atomh is "large", by
		   definition. Small atom is destroyed and large is unlocked
		   inside capture_fuse_into()
		*/
		capture_fuse_into(atomf, atomh);

		reiser4_stat_inc(txnmgr.restart.fuse_lock_owners_fused);
		return RETERR(-E_REPEAT);
	}

	UNLOCK_ATOM(atomh);

	if (repeat) {
fail:
		UNLOCK_TXNH(txnh);
		RUNLOCK_ZLOCK(&node->lock);
		spin_unlock_znode(node);
		reiser4_stat_inc(txnmgr.restart.fuse_lock_owners);
		return RETERR(-E_REPEAT);
	}

	RUNLOCK_ZLOCK(&node->lock);
	return 0;
}

/* This is the interface to capture unformatted nodes via their struct page
   reference. Currently it is only used in reiser4_invalidatepage */
reiser4_internal int
try_capture_page_to_invalidate(struct page *pg)
{
	int ret;
	jnode *node;

	assert("umka-292", pg != NULL);
	assert("nikita-2597", PageLocked(pg));

	if (IS_ERR(node = jnode_of_page(pg))) {
		return PTR_ERR(node);
	}

	LOCK_JNODE(node);
	unlock_page(pg);

	ret = try_capture(node, ZNODE_WRITE_LOCK, 0, 0/* no copy on capture */);
	UNLOCK_JNODE(node);
	jput(node);
	lock_page(pg);
	return ret;
}

/* This informs the transaction manager when a node is deleted.  Add the block to the
   atom's delete set and uncapture the block.

VS-FIXME-HANS: this E_REPEAT paradigm clutters the code and creates a need for
explanations.  find all the functions that use it, and unless there is some very
good reason to use it (I have not noticed one so far and I doubt it exists, but maybe somewhere somehow....),
move the loop to inside the function.

VS-FIXME-HANS: can this code be at all streamlined?  In particular, can you lock and unlock the jnode fewer times?
  */
reiser4_internal void
uncapture_page(struct page *pg)
{
	jnode *node;
	txn_atom *atom;

	assert("umka-199", pg != NULL);
	assert("nikita-3155", PageLocked(pg));

	reiser4_clear_page_dirty(pg);

	reiser4_wait_page_writeback(pg);

	node = (jnode *) (pg->private);
	if (node == NULL)
		return;

	LOCK_JNODE(node);

	eflush_del(node, 1/* page is locked */);
	/*assert ("zam-815", !JF_ISSET(node, JNODE_EFLUSH));*/

	atom = jnode_get_atom(node);
	if (atom == NULL) {
		assert("jmacd-7111", !jnode_is_dirty(node));
		UNLOCK_JNODE (node);
		return;
	}

	/* We can remove jnode from transaction even if it is on flush queue
	 * prepped list, we only need to be sure that flush queue is not being
	 * written by write_fq().  write_fq() does not use atom spin lock for
	 * protection of the prepped nodes list, instead write_fq() increments
	 * atom's nr_running_queues counters for the time when prepped list is
	 * not protected by spin lock.  Here we check this counter if we want
	 * to remove jnode from flush queue and, if the counter is not zero,
	 * wait all write_fq() for this atom to complete. This is not
	 * significant overhead. */
	while (JF_ISSET(node, JNODE_FLUSH_QUEUED) && atom->nr_running_queues) {
		UNLOCK_JNODE(node);
		/*
		 * at this moment we want to wait for "atom event", viz. wait
		 * until @node can be removed from flush queue. But
		 * atom_wait_event() cannot be called with page locked, because
		 * it deadlocks with jnode_extent_write(). Unlock page, after
		 * making sure (through page_cache_get()) that it cannot be
		 * released from memory.
		 */
		page_cache_get(pg);
		unlock_page(pg);
		atom_wait_event(atom);
		lock_page(pg);
		/*
		 * page may has been detached by ->writepage()->releasepage().
		 */
		reiser4_wait_page_writeback(pg);
		LOCK_JNODE(node);
		eflush_del(node, 1);
		page_cache_release(pg);
		atom = jnode_get_atom(node);
/* VS-FIXME-HANS: improve the commenting in this function */
		if (atom == NULL) {
			UNLOCK_JNODE(node);
			return;
		}
	}
	uncapture_block(node);
	UNLOCK_ATOM(atom);
	jput(node);
}

/* this is used in extent's kill hook to uncapture and unhash jnodes attached to inode's tree of jnodes */
reiser4_internal void
uncapture_jnode(jnode *node)
{
	txn_atom *atom;

	assert("vs-1462", spin_jnode_is_locked(node));
	assert("", node->pg == 0);

	eflush_del(node, 0);
	/*jnode_make_clean(node);*/
	atom = jnode_get_atom(node);
	if (atom == NULL) {
		assert("jmacd-7111", !jnode_is_dirty(node));
		UNLOCK_JNODE (node);
		return;
	}

	uncapture_block(node);
	UNLOCK_ATOM(atom);
	jput(node);
}

/* No-locking version of assign_txnh.  Sets the transaction handle's atom pointer,
   increases atom refcount and txnh_count, adds to txnh_list. */
static void
capture_assign_txnh_nolock(txn_atom * atom, txn_handle * txnh)
{
	assert("umka-200", atom != NULL);
	assert("umka-201", txnh != NULL);

	assert("jmacd-822", spin_txnh_is_locked(txnh));
	assert("jmacd-823", spin_atom_is_locked(atom));
	assert("jmacd-824", txnh->atom == NULL);
	assert("nikita-3540", atom_isopen(atom));

	atomic_inc(&atom->refcount);

	ON_TRACE(TRACE_TXN, "assign txnh atom %u refcount %d\n", atom->atom_id, atomic_read(&atom->refcount));

	txnh->atom = atom;
	txnh_list_push_back(&atom->txnh_list, txnh);
	atom->txnh_count += 1;
}

/* No-locking version of assign_block.  Sets the block's atom pointer, references the
   block, adds it to the clean or dirty capture_jnode list, increments capture_count. */
static void
capture_assign_block_nolock(txn_atom * atom, jnode * node)
{
	assert("umka-202", atom != NULL);
	assert("umka-203", node != NULL);
	assert("jmacd-321", spin_jnode_is_locked(node));
	assert("umka-295", spin_atom_is_locked(atom));
	assert("jmacd-323", node->atom == NULL);
	BUG_ON(!capture_list_is_clean(node));
	assert("nikita-3470", !jnode_is_dirty(node));

	/* Pointer from jnode to atom is not counted in atom->refcount. */
	node->atom = atom;

	capture_list_push_back(ATOM_CLEAN_LIST(atom), node);
	atom->capture_count += 1;
	/* reference to jnode is acquired by atom. */
	jref(node);

	ON_DEBUG(count_jnode(atom, node, NOT_CAPTURED, CLEAN_LIST, 1));

	LOCK_CNT_INC(t_refs);

	ON_TRACE(TRACE_TXN, "capture %p for atom %u (captured %u)\n", node, atom->atom_id, atom->capture_count);
}

#if REISER4_COPY_ON_CAPTURE
static void
set_cced_bit(jnode *node)
{
	BUG_ON(JF_ISSET(node, JNODE_CCED));
	JF_SET(node, JNODE_CCED);
}
#endif

static void
clear_cced_bits(jnode *node)
{
	JF_CLR(node, JNODE_CCED);
}

int
is_cced(const jnode *node)
{
	return JF_ISSET(node, JNODE_CCED);
}

/* common code for dirtying both unformatted jnodes and formatted znodes. */
static void
do_jnode_make_dirty(jnode * node, txn_atom * atom)
{
	assert("zam-748", spin_jnode_is_locked(node));
	assert("zam-750", spin_atom_is_locked(atom));
	assert("jmacd-3981", !jnode_is_dirty(node));

	JF_SET(node, JNODE_DIRTY);

	get_current_context()->nr_marked_dirty ++;

	/* We grab2flush_reserve one additional block only if node was
	   not CREATED and jnode_flush did not sort it into neither
	   relocate set nor overwrite one. If node is in overwrite or
	   relocate set we assume that atom's flush reserved counter was
	   already adjusted. */
	if (!JF_ISSET(node, JNODE_CREATED) && !JF_ISSET(node, JNODE_RELOC)
	    && !JF_ISSET(node, JNODE_OVRWR) && jnode_is_leaf(node)
	    && !jnode_is_cluster_page(node)) {
		assert("vs-1093", !blocknr_is_fake(&node->blocknr));
		assert("vs-1506", *jnode_get_block(node) != 0);
		grabbed2flush_reserved_nolock(atom, (__u64)1);
		JF_SET(node, JNODE_FLUSH_RESERVED);
	}

	if (!JF_ISSET(node, JNODE_FLUSH_QUEUED)) {
		/* If the atom is not set yet, it will be added to the appropriate list in
		   capture_assign_block_nolock. */
		/* Sometimes a node is set dirty before being captured -- the case for new
		   jnodes.  In that case the jnode will be added to the appropriate list
		   in capture_assign_block_nolock. Another reason not to re-link jnode is
		   that jnode is on a flush queue (see flush.c for details) */

		int level = jnode_get_level(node);

		assert("nikita-3152", !JF_ISSET(node, JNODE_OVRWR));
		assert("zam-654", atom->stage < ASTAGE_PRE_COMMIT);
		assert("nikita-2607", 0 <= level);
		assert("nikita-2606", level <= REAL_MAX_ZTREE_HEIGHT);

		capture_list_remove(node);
		capture_list_push_back(ATOM_DIRTY_LIST(atom, level), node);
		ON_DEBUG(count_jnode(atom, node, NODE_LIST(node), DIRTY_LIST, 1));

		/*
		 * JNODE_CCED bit protects clean copy (page created by
		 * copy-on-capture) from being evicted from the memory. This
		 * is necessary, because otherwise jload() would load obsolete
		 * disk block (up-to-date original is still in memory). But
		 * once jnode is dirtied, it cannot be released without
		 * storing its content on the disk, so protection is no longer
		 * necessary.
		 */
		clear_cced_bits(node);
	}
}

/* Set the dirty status for this (spin locked) jnode. */
reiser4_internal void
jnode_make_dirty_locked(jnode * node)
{
	assert("umka-204", node != NULL);
	assert("zam-7481", spin_jnode_is_locked(node));

	if (REISER4_DEBUG && rofs_jnode(node)) {
		warning("nikita-3365", "Dirtying jnode on rofs");
		dump_stack();
	}

	/* Fast check for already dirty node */
	if (!jnode_is_dirty(node)) {
		txn_atom * atom;

		atom = jnode_get_atom (node);
		assert("vs-1094", atom);
		/* Check jnode dirty status again because node spin lock might
		 * be released inside jnode_get_atom(). */
		if (likely(!jnode_is_dirty(node)))
			do_jnode_make_dirty(node, atom);
		UNLOCK_ATOM (atom);
	}
}

/* Set the dirty status for this znode. */
reiser4_internal void
znode_make_dirty(znode * z)
{
	jnode *node;
	struct page *page;

	assert("umka-204", z != NULL);
	assert("nikita-3290", znode_above_root(z) || znode_is_loaded(z));
	assert("nikita-3291", !ZF_ISSET(z, JNODE_EFLUSH));
	assert("nikita-3560", znode_is_write_locked(z));

	node = ZJNODE(z);

	LOCK_JNODE(node);
	jnode_make_dirty_locked(node);
	page = jnode_page(node);
	if (page != NULL) {
		/* this is useful assertion (allows one to check that no
		 * modifications are lost due to update of in-flight page),
		 * but it requires locking on page to check PG_writeback
		 * bit. */
		/* assert("nikita-3292",
		       !PageWriteback(page) || ZF_ISSET(z, JNODE_WRITEBACK)); */
		page_cache_get(page);
		ON_DEBUG_MODIFY(znode_set_checksum(ZJNODE(z), 1));
		/* jnode lock is not needed for the rest of
		 * znode_set_dirty(). */
		UNLOCK_JNODE(node);
		/* reiser4 file write code calls set_page_dirty for
		 * unformatted nodes, for formatted nodes we do it here. */
		set_page_dirty_internal(page, 0);
		page_cache_release(page);
		/* bump version counter in znode */
		z->version = znode_build_version(jnode_get_tree(node));
	} else {
		assert("zam-596", znode_above_root(JZNODE(node)));
		UNLOCK_JNODE(node);
	}

	assert("nikita-1900", znode_is_write_locked(z));
	assert("jmacd-9777", node->atom != NULL);
}

reiser4_internal int
sync_atom(txn_atom *atom)
{
	int result;
	txn_handle *txnh;

	txnh = get_current_context()->trans;

	result = 0;
	if (atom != NULL) {
		if (atom->stage < ASTAGE_PRE_COMMIT) {
			LOCK_TXNH(txnh);
			capture_assign_txnh_nolock(atom, txnh);
			result = force_commit_atom_nolock(txnh);
		} else if (atom->stage < ASTAGE_POST_COMMIT) {
			/* wait atom commit */
			atom_wait_event(atom);
			/* try once more */
			result = RETERR(-E_REPEAT);
		} else
			UNLOCK_ATOM(atom);
	}
	return result;
}

#if REISER4_DEBUG

void check_fq(const txn_atom *atom);

/* move jnode form one list to another
   call this after atom->capture_count is updated */
void
count_jnode(txn_atom *atom, jnode *node, atom_list old_list, atom_list new_list, int check_lists)
{
#if REISER4_COPY_ON_CAPTURE
	assert("", spin_atom_is_locked(atom));
#else
	assert("zam-1018", atom_is_protected(atom));
#endif
	assert("", spin_jnode_is_locked(node));
	assert("", NODE_LIST(node) == old_list);

	switch(NODE_LIST(node)) {
	case NOT_CAPTURED:
		break;
	case DIRTY_LIST:
		assert("", atom->dirty > 0);
		atom->dirty --;
		break;
	case CLEAN_LIST:
		assert("", atom->clean > 0);
		atom->clean --;
		break;
	case FQ_LIST:
		assert("", atom->fq > 0);
		atom->fq --;
		break;
	case WB_LIST:
		assert("", atom->wb > 0);
		atom->wb --;
		break;
	case OVRWR_LIST:
		assert("", atom->ovrwr > 0);
		atom->ovrwr --;
		break;
	case PROTECT_LIST:
		/* protect list is an intermediate atom's list to which jnodes
		   get put from dirty list before disk space is allocated for
		   them. From this list jnodes can either go to flush queue list
		   or back to dirty list */
		assert("", atom->protect > 0);
		assert("", new_list == FQ_LIST || new_list == DIRTY_LIST);
		atom->protect --;
		break;
	default:
		impossible("", "");
	}

	switch(new_list) {
	case NOT_CAPTURED:
		break;
	case DIRTY_LIST:
		atom->dirty ++;
		break;
	case CLEAN_LIST:
		atom->clean ++;
		break;
	case FQ_LIST:
		atom->fq ++;
		break;
	case WB_LIST:
		atom->wb ++;
		break;
	case OVRWR_LIST:
		atom->ovrwr ++;
		break;
	case PROTECT_LIST:
		assert("", old_list == DIRTY_LIST);
		atom->protect ++;
		break;
	default:
		impossible("", "");
	}
	ASSIGN_NODE_LIST(node, new_list);
	if (0 && check_lists) {
		int count;
		tree_level level;
		jnode *node;

		count = 0;

		/* flush queue list */
		/*check_fq(atom);*/

		/* dirty list */
		count = 0;
		for (level = 0; level < REAL_MAX_ZTREE_HEIGHT + 1; level += 1) {
			for_all_type_safe_list(capture, ATOM_DIRTY_LIST(atom, level), node)
				count ++;
		}
		if (count != atom->dirty)
			warning("", "dirty counter %d, real %d\n", atom->dirty, count);

		/* clean list */
		count = 0;
		for_all_type_safe_list(capture, ATOM_CLEAN_LIST(atom), node)
			count ++;
		if (count != atom->clean)
			warning("", "clean counter %d, real %d\n", atom->clean, count);

		/* wb list */
		count = 0;
		for_all_type_safe_list(capture, ATOM_WB_LIST(atom), node)
			count ++;
		if (count != atom->wb)
			warning("", "wb counter %d, real %d\n", atom->wb, count);

		/* overwrite list */
		count = 0;
		for_all_type_safe_list(capture, ATOM_OVRWR_LIST(atom), node)
			count ++;

		if (count != atom->ovrwr)
			warning("", "ovrwr counter %d, real %d\n", atom->ovrwr, count);
	}
	assert("vs-1624", atom->num_queued == atom->fq);
	if (atom->capture_count != atom->dirty + atom->clean + atom->ovrwr + atom->wb + atom->fq + atom->protect) {
		printk("count %d, dirty %d clean %d ovrwr %d wb %d fq %d protect %d\n", atom->capture_count, atom->dirty, atom->clean, atom->ovrwr, atom->wb, atom->fq, atom->protect);
		assert("vs-1622",
		       atom->capture_count == atom->dirty + atom->clean + atom->ovrwr + atom->wb + atom->fq + atom->protect);
	}
}

#endif

/* Make node OVRWR and put it on atom->overwrite_nodes list, atom lock and jnode
 * lock should be taken before calling this function. */
reiser4_internal void jnode_make_wander_nolock (jnode * node)
{
	txn_atom * atom;

	assert("nikita-2431", node != NULL);
	assert("nikita-2432", !JF_ISSET(node, JNODE_RELOC));
	assert("nikita-3153", jnode_is_dirty(node));
	assert("zam-897", !JF_ISSET(node, JNODE_FLUSH_QUEUED));
	assert("nikita-3367", !blocknr_is_fake(jnode_get_block(node)));

	atom = node->atom;

	assert("zam-895", atom != NULL);
	assert("zam-894", atom_is_protected(atom));

	JF_SET(node, JNODE_OVRWR);
	capture_list_remove_clean(node);
	capture_list_push_back(ATOM_OVRWR_LIST(atom), node);
	/*XXXX*/ON_DEBUG(count_jnode(atom, node, DIRTY_LIST, OVRWR_LIST, 1));
}

/* Same as jnode_make_wander_nolock, but all necessary locks are taken inside
 * this function. */
reiser4_internal void jnode_make_wander (jnode * node)
{
	txn_atom * atom;

	LOCK_JNODE(node);
	atom = jnode_get_atom(node);
	assert ("zam-913", atom != NULL);
	assert ("zam-914", !JF_ISSET(node, JNODE_RELOC));

	jnode_make_wander_nolock(node);
	UNLOCK_ATOM(atom);
	UNLOCK_JNODE(node);
}

/* this just sets RELOC bit  */
static void
jnode_make_reloc_nolock(flush_queue_t *fq, jnode *node)
{
	assert("vs-1480", spin_jnode_is_locked(node));
	assert ("zam-916", jnode_is_dirty(node));
	assert ("zam-917", !JF_ISSET(node, JNODE_RELOC));
	assert ("zam-918", !JF_ISSET(node, JNODE_OVRWR));
	assert ("zam-920", !JF_ISSET(node, JNODE_FLUSH_QUEUED));
	assert ("nikita-3367", !blocknr_is_fake(jnode_get_block(node)));

	jnode_set_reloc(node);
}

/* Make znode RELOC and put it on flush queue */
reiser4_internal void znode_make_reloc (znode *z, flush_queue_t * fq)
{
	jnode *node;
	txn_atom * atom;

	node = ZJNODE(z);
	LOCK_JNODE(node);

	atom = jnode_get_atom(node);
	assert ("zam-919", atom != NULL);

	jnode_make_reloc_nolock(fq, node);
	queue_jnode(fq, node);

	UNLOCK_ATOM(atom);
	UNLOCK_JNODE(node);

}

/* Make unformatted node RELOC and put it on flush queue */
reiser4_internal void
unformatted_make_reloc(jnode *node, flush_queue_t * fq)
{
	assert("vs-1479", jnode_is_unformatted(node));

	jnode_make_reloc_nolock(fq, node);
	mark_jnode_queued(fq, node);
}

static int
trylock_wait(txn_atom *atom, txn_handle * txnh, jnode * node)
{
	if (unlikely(!spin_trylock_atom(atom))) {
		atomic_inc(&atom->refcount);

		UNLOCK_JNODE(node);
		UNLOCK_TXNH(txnh);

		LOCK_ATOM(atom);
		/* caller should eliminate extra reference by calling
		 * atom_dec_and_unlock() for this atom. */
		return 1;
	} else
		return 0;
}

/*
 * in transaction manager jnode spin lock and transaction handle spin lock
 * nest within atom spin lock. During capturing we are in a situation when
 * jnode and transaction handle spin locks are held and we want to manipulate
 * atom's data (capture lists, and txnh list) to add node and/or handle to the
 * atom. Releasing jnode (or txnh) spin lock at this point is unsafe, because
 * concurrent fusion can render assumption made by capture so far (about
 * ->atom pointers in jnode and txnh) invalid. Initial code used try-lock and
 * if atom was busy returned -E_REPEAT to the top level. This can lead to the
 * busy loop if atom is locked for long enough time. Function below tries to
 * throttle this loop.
 *
 */
/* ZAM-FIXME-HANS: how feasible would it be to use our hi-lo priority locking
   mechanisms/code for this as well? Does that make any sense? */
/* ANSWER(Zam): I am not sure that I understand you proposal right, but the idea
   might be in inventing spin_lock_lopri() which should be a complex loop with
   "release lock" messages check like we have in the znode locking.  I think we
   should not substitute spin locks by more complex busy loops.  Once it was
   done that way in try_capture_block() where spin lock waiting was spread in a
   busy loop  through several functions.  The proper solution should be in
   making spin lock contention rare. */
static int
trylock_throttle(txn_atom *atom, txn_handle * txnh, jnode * node)
{
	assert("nikita-3224", atom != NULL);
	assert("nikita-3225", txnh != NULL);
	assert("nikita-3226", node != NULL);

	assert("nikita-3227", spin_txnh_is_locked(txnh));
	assert("nikita-3229", spin_jnode_is_locked(node));

	if (unlikely(trylock_wait(atom, txnh, node) != 0)) {
		atom_dec_and_unlock(atom);
		reiser4_stat_inc(txnmgr.restart.trylock_throttle);
		return RETERR(-E_REPEAT);
	} else
		return 0;
}

/* This function assigns a block to an atom, but first it must obtain the atom lock.  If
   the atom lock is busy, it returns -E_REPEAT to avoid deadlock with a fusing atom.  Since
   the transaction handle is currently open, we know the atom must also be open. */
static int
capture_assign_block(txn_handle * txnh, jnode * node)
{
	txn_atom *atom;
	int       result;

	assert("umka-206", txnh != NULL);
	assert("umka-207", node != NULL);

	atom = txnh->atom;

	assert("umka-297", atom != NULL);

	result = trylock_throttle(atom, txnh, node);
	if (result != 0) {
		/* this avoid busy loop, but we return -E_REPEAT anyway to
		 * simplify things. */
		reiser4_stat_inc(txnmgr.restart.assign_block);
		return result;
	} else {
		assert("jmacd-19", atom_isopen(atom));

		/* Add page to capture list. */
		capture_assign_block_nolock(atom, node);

		/* Success holds onto jnode & txnh locks.  Unlock atom. */
		UNLOCK_ATOM(atom);
		return 0;
	}
}

/* This function assigns a handle to an atom, but first it must obtain the atom lock.  If
   the atom is busy, it returns -E_REPEAT to avoid deadlock with a fusing atom.  Unlike
   capture_assign_block, the atom may be closed but we cannot know this until the atom is
   locked.  If the atom is closed and the request is to read, it is as if the block is
   unmodified and the request is satisified without actually assigning the transaction
   handle.  If the atom is closed and the handle requests to write the block, then
   initiate copy-on-capture.
*/
static int
capture_assign_txnh(jnode * node, txn_handle * txnh, txn_capture mode, int can_coc)
{
	txn_atom *atom;

	assert("umka-208", node != NULL);
	assert("umka-209", txnh != NULL);

	atom = node->atom;

	assert("umka-298", atom != NULL);

	/*
	 * optimization: this code went through three evolution stages. Main
	 * driving force of evolution here is lock ordering:
	 *
	 * at the entry to this function following pre-conditions are met:
	 *
	 *     1. txnh and node are both spin locked,
	 *
	 *     2. node belongs to atom, and
	 *
	 *     3. txnh don't.
	 *
	 * What we want to do here is to acquire spin lock on node's atom and
	 * modify it somehow depending on its ->stage. In the simplest case,
	 * where ->stage is ASTAGE_CAPTURE_FUSE, txnh should be added to
	 * atom's list. Problem is that atom spin lock nests outside of jnode
	 * and transaction handle ones. So, we cannot just LOCK_ATOM here.
	 *
	 * Solutions tried here:
	 *
	 *     1. spin_trylock(atom), return -E_REPEAT on failure.
	 *
	 *     2. spin_trylock(atom). On failure to acquire lock, increment
	 *     atom->refcount, release all locks, and spin on atom lock. Then
	 *     decrement ->refcount, unlock atom and return -E_REPEAT.
	 *
	 *     3. like previous one, but before unlocking atom, re-acquire
	 *     spin locks on node and txnh and re-check whether function
	 *     pre-condition are still met. Continue boldly if they are.
	 *
	 */
	if (trylock_wait(atom, txnh, node) != 0) {
		LOCK_JNODE(node);
		LOCK_TXNH(txnh);
		/* NOTE-NIKITA is it at all possible that current txnh
		 * spontaneously changes ->atom from NULL to non-NULL? */
		if (node->atom == NULL ||
		    txnh->atom != NULL || atom != node->atom) {
			/* something changed. Caller have to re-decide */
			UNLOCK_TXNH(txnh);
			UNLOCK_JNODE(node);
			atom_dec_and_unlock(atom);
			reiser4_stat_inc(txnmgr.restart.assign_txnh);
			return RETERR(-E_REPEAT);
		} else {
			/* atom still has a jnode on its list (node->atom ==
			 * atom), it means atom is not fused or finished
			 * (committed), we can safely decrement its refcount
			 * because it is not a last reference. */
			atomic_dec(&atom->refcount);
			assert("zam-990", atomic_read(&atom->refcount) > 0);
		}
	}

	if (atom->stage == ASTAGE_CAPTURE_WAIT &&
	    (atom->txnh_count != 0 ||
	     atom_should_commit(atom) || atom_should_commit_asap(atom))) {
		/* We don't fuse with the atom in ASTAGE_CAPTURE_WAIT only if
		 * there is open transaction handler.  It makes sense: those
		 * atoms should not wait ktxnmgrd to flush and commit them.
		 * And, it solves deadlocks with loop back devices (reiser4 over
		 * loopback over reiser4), when ktxnmrgd is busy committing one
		 * atom (above the loop back device) and can't flush an atom
		 * below the loopback. */

		/* The atom could be blocking requests--this is the first chance we've had
		   to test it.  Since this txnh is not yet assigned, the fuse_wait logic
		   is not to avoid deadlock, its just waiting.  Releases all three locks
		   and returns E_REPEAT. */

		return capture_fuse_wait(node, txnh, atom, NULL, mode);

	} else if (atom->stage > ASTAGE_CAPTURE_WAIT) {

		/* The block is involved with a committing atom. */
		if (CAPTURE_TYPE(mode) == TXN_CAPTURE_READ_ATOMIC) {

			/* A read request for a committing block can be satisfied w/o
			   COPY-ON-CAPTURE. */

			/* Success holds onto the jnode & txnh lock.  Continue to unlock
			   atom below. */

		} else {

			/* Perform COPY-ON-CAPTURE.  Copy and try again.  This function
			   releases all three locks. */
			return capture_copy(node, txnh, atom, NULL, mode, can_coc);
		}

	} else {

		assert("jmacd-160", atom->stage == ASTAGE_CAPTURE_FUSE ||
		       (atom->stage == ASTAGE_CAPTURE_WAIT && atom->txnh_count == 0));

		/* Add txnh to active list. */
		capture_assign_txnh_nolock(atom, txnh);

		/* Success holds onto the jnode & txnh lock.  Continue to unlock atom
		   below. */
	}

	/* Unlock the atom */
	UNLOCK_ATOM(atom);
	return 0;
}

reiser4_internal int
capture_super_block(struct super_block *s)
{
	int result;
	znode *uber;
	lock_handle lh;

	init_lh(&lh);
	result = get_uber_znode(get_tree(s),
				ZNODE_WRITE_LOCK, ZNODE_LOCK_LOPRI, &lh);
	if (result)
		return result;

	uber = lh.node;
	/* Grabbing one block for superblock */
	result = reiser4_grab_space_force((__u64)1, BA_RESERVED);
	if (result != 0)
		return result;

	znode_make_dirty(uber);

	done_lh(&lh);
	return 0;
}

/* Wakeup every handle on the atom's WAITFOR list */
static void
wakeup_atom_waitfor_list(txn_atom * atom)
{
	txn_wait_links *wlinks;

	assert("umka-210", atom != NULL);

	/* atom is locked */
	for_all_type_safe_list(fwaitfor, &atom->fwaitfor_list, wlinks) {
		if (wlinks->waitfor_cb == NULL ||
		    wlinks->waitfor_cb(atom, wlinks))
			/* Wake up. */
			reiser4_wake_up(wlinks->_lock_stack);
	}
}

/* Wakeup every handle on the atom's WAITING list */
static void
wakeup_atom_waiting_list(txn_atom * atom)
{
	txn_wait_links *wlinks;

	assert("umka-211", atom != NULL);

	/* atom is locked */
	for_all_type_safe_list(fwaiting, &atom->fwaiting_list, wlinks) {
		if (wlinks->waiting_cb == NULL ||
		    wlinks->waiting_cb(atom, wlinks))
			/* Wake up. */
			reiser4_wake_up(wlinks->_lock_stack);
	}
}

/* helper function used by capture_fuse_wait() to avoid "spurious wake-ups" */
static int wait_for_fusion(txn_atom * atom, txn_wait_links * wlinks)
{
	assert("nikita-3330", atom != NULL);
	assert("nikita-3331", spin_atom_is_locked(atom));


	/* atom->txnh_count == 1 is for waking waiters up if we are releasing
	 * last transaction handle. */
	return atom->stage != ASTAGE_CAPTURE_WAIT || atom->txnh_count == 1;
}

/* The general purpose of this function is to wait on the first of two possible events.
   The situation is that a handle (and its atom atomh) is blocked trying to capture a
   block (i.e., node) but the node's atom (atomf) is in the CAPTURE_WAIT state.  The
   handle's atom (atomh) is not in the CAPTURE_WAIT state.  However, atomh could fuse with
   another atom or, due to age, enter the CAPTURE_WAIT state itself, at which point it
   needs to unblock the handle to avoid deadlock.  When the txnh is unblocked it will
   proceed and fuse the two atoms in the CAPTURE_WAIT state.

   In other words, if either atomh or atomf change state, the handle will be awakened,
   thus there are two lists per atom: WAITING and WAITFOR.

   This is also called by capture_assign_txnh with (atomh == NULL) to wait for atomf to
   close but it is not assigned to an atom of its own.

   Lock ordering in this method: all four locks are held: JNODE_LOCK, TXNH_LOCK,
   BOTH_ATOM_LOCKS.  Result: all four locks are released.
*/
static int
capture_fuse_wait(jnode * node, txn_handle * txnh, txn_atom * atomf, txn_atom * atomh, txn_capture mode)
{
	int ret;

	/* Initialize the waiting list links. */
	txn_wait_links wlinks;

	assert("umka-212", node != NULL);
	assert("umka-213", txnh != NULL);
	assert("umka-214", atomf != NULL);

	/* We do not need the node lock. */
	UNLOCK_JNODE(node);

	if ((mode & TXN_CAPTURE_NONBLOCKING) != 0) {
		UNLOCK_TXNH(txnh);
		UNLOCK_ATOM(atomf);

		if (atomh) {
			UNLOCK_ATOM(atomh);
		}

		ON_TRACE(TRACE_TXN, "thread %u nonblocking on atom %u\n", current->pid, atomf->atom_id);

		reiser4_stat_inc(txnmgr.restart.fuse_wait_nonblock);
		return RETERR(-E_BLOCK);
	}

	init_wlinks(&wlinks);

	/* Add txnh to atomf's waitfor list, unlock atomf. */
	fwaitfor_list_push_back(&atomf->fwaitfor_list, &wlinks);
	wlinks.waitfor_cb = wait_for_fusion;
	atomic_inc(&atomf->refcount);
	UNLOCK_ATOM(atomf);

	if (atomh) {
		/* Add txnh to atomh's waiting list, unlock atomh. */
		fwaiting_list_push_back(&atomh->fwaiting_list, &wlinks);
		atomic_inc(&atomh->refcount);
		UNLOCK_ATOM(atomh);
	}

	ON_TRACE(TRACE_TXN, "thread %u waitfor %u waiting %u\n", current->pid,
		 atomf->atom_id, atomh ? atomh->atom_id : 0);

	/* Go to sleep. */
	UNLOCK_TXNH(txnh);

	ret = prepare_to_sleep(wlinks._lock_stack);
	if (ret != 0) {
		ON_TRACE(TRACE_TXN, "thread %u deadlock blocking on atom %u\n", current->pid, atomf->atom_id);
	} else {
		go_to_sleep(wlinks._lock_stack, ADD_TO_SLEPT_IN_WAIT_ATOM);

		reiser4_stat_inc(txnmgr.restart.fuse_wait_slept);
		ret = RETERR(-E_REPEAT);
		ON_TRACE(TRACE_TXN, "thread %u wakeup %u waiting %u\n",
			 current->pid, atomf->atom_id, atomh ? atomh->atom_id : 0);
	}

	/* Remove from the waitfor list. */
	LOCK_ATOM(atomf);
	fwaitfor_list_remove(&wlinks);
	atom_dec_and_unlock(atomf);

	if (atomh) {
		/* Remove from the waiting list. */
		LOCK_ATOM(atomh);
		fwaiting_list_remove(&wlinks);
		atom_dec_and_unlock(atomh);
	}

	assert("nikita-2186", ergo(ret, spin_jnode_is_not_locked(node)));
	return ret;
}

static inline int
capture_init_fusion_locked(jnode * node, txn_handle * txnh, txn_capture mode, int can_coc)
{
	txn_atom *atomf;
	txn_atom *atomh;

	assert("umka-216", txnh != NULL);
	assert("umka-217", node != NULL);

	atomh = txnh->atom;
	atomf = node->atom;

	/* The txnh atom must still be open (since the txnh is active)...  the node atom may
	   be in some later stage (checked next). */
	assert("jmacd-20", atom_isopen(atomh));

	/* If the node atom is in the FUSE_WAIT state then we should wait, except to
	   avoid deadlock we still must fuse if the txnh atom is also in FUSE_WAIT. */
	if (atomf->stage == ASTAGE_CAPTURE_WAIT &&
	    atomh->stage != ASTAGE_CAPTURE_WAIT &&
	    (atomf->txnh_count != 0 ||
	     atom_should_commit(atomf) || atom_should_commit_asap(atomf))) {
		/* see comment in capture_assign_txnh() about the
		 * "atomf->txnh_count != 0" condition. */
		/* This unlocks all four locks and returns E_REPEAT. */
		return capture_fuse_wait(node, txnh, atomf, atomh, mode);

	} else if (atomf->stage > ASTAGE_CAPTURE_WAIT) {

		/* The block is involved with a comitting atom. */
		if (CAPTURE_TYPE(mode) == TXN_CAPTURE_READ_ATOMIC) {
			/* A read request for a committing block can be satisfied w/o
			   COPY-ON-CAPTURE.  Success holds onto the jnode & txnh
			   locks. */
			UNLOCK_ATOM(atomf);
			UNLOCK_ATOM(atomh);
			return 0;
		} else {
			/* Perform COPY-ON-CAPTURE.  Copy and try again.  This function
			   releases all four locks. */
			return capture_copy(node, txnh, atomf, atomh, mode, can_coc);
		}
	}

	/* Because atomf's stage <= CAPTURE_WAIT */
	assert("jmacd-175", atom_isopen(atomf));

	/* If we got here its either because the atomh is in CAPTURE_WAIT or because the
	   atomf is not in CAPTURE_WAIT. */
	assert("jmacd-176", (atomh->stage == ASTAGE_CAPTURE_WAIT || atomf->stage != ASTAGE_CAPTURE_WAIT) || atomf->txnh_count == 0);

	/* Now release the txnh lock: only holding the atoms at this point. */
	UNLOCK_TXNH(txnh);
	UNLOCK_JNODE(node);

	/* Decide which should be kept and which should be merged. */
	if (atom_pointer_count(atomf) < atom_pointer_count(atomh)) {
		capture_fuse_into(atomf, atomh);
	} else {
		capture_fuse_into(atomh, atomf);
	}

	/* Atoms are unlocked in capture_fuse_into.  No locks held. */
	reiser4_stat_inc(txnmgr.restart.init_fusion_fused);
	return RETERR(-E_REPEAT);
}

/* Perform the necessary work to prepare for fusing two atoms, which involves
 * acquiring two atom locks in the proper order.  If one of the node's atom is
 * blocking fusion (i.e., it is in the CAPTURE_WAIT stage) and the handle's
 * atom is not then the handle's request is put to sleep.  If the node's atom
 * is committing, then the node can be copy-on-captured.  Otherwise, pick the
 * atom with fewer pointers to be fused into the atom with more pointer and
 * call capture_fuse_into.
 */
static int
capture_init_fusion(jnode * node, txn_handle * txnh, txn_capture mode, int can_coc)
{
	/* Have to perform two trylocks here. */
	if (likely(spin_trylock_atom(node->atom)))
		if (likely(spin_trylock_atom(txnh->atom)))
			return capture_init_fusion_locked(node, txnh, mode, can_coc);
		else {
			UNLOCK_ATOM(node->atom);
			reiser4_stat_inc(txnmgr.restart.init_fusion_atomh);
		}
	else {
		reiser4_stat_inc(txnmgr.restart.init_fusion_atomf);
	}

	UNLOCK_JNODE(node);
	UNLOCK_TXNH(txnh);
	return RETERR(-E_REPEAT);
}
/* This function splices together two jnode lists (small and large) and sets all jnodes in
   the small list to point to the large atom.  Returns the length of the list. */
static int
capture_fuse_jnode_lists(txn_atom * large, capture_list_head * large_head, capture_list_head * small_head)
{
	int count = 0;
	jnode *node;

	assert("umka-218", large != NULL);
	assert("umka-219", large_head != NULL);
	assert("umka-220", small_head != NULL);
	/* small atom should be locked also. */
	assert("zam-968", spin_atom_is_locked(large));

	/* For every jnode on small's capture list... */
	for_all_type_safe_list(capture, small_head, node) {
		count += 1;

		/* With the jnode lock held, update atom pointer. */
		UNDER_SPIN_VOID(jnode, node, node->atom = large);
	}

	/* Splice the lists. */
	capture_list_splice(large_head, small_head);

	return count;
}

/* This function splices together two txnh lists (small and large) and sets all txn handles in
   the small list to point to the large atom.  Returns the length of the list. */
/* Audited by: umka (2002.06.13) */
static int
capture_fuse_txnh_lists(txn_atom * large, txnh_list_head * large_head, txnh_list_head * small_head)
{
	int count = 0;
	txn_handle *txnh;

	assert("umka-221", large != NULL);
	assert("umka-222", large_head != NULL);
	assert("umka-223", small_head != NULL);

	/* Adjust every txnh to the new atom. */
	for_all_type_safe_list(txnh, small_head, txnh) {
		count += 1;

		/* With the txnh lock held, update atom pointer. */
		UNDER_SPIN_VOID(txnh, txnh, txnh->atom = large);
	}

	/* Splice the txn_handle list. */
	txnh_list_splice(large_head, small_head);

	return count;
}

/* This function fuses two atoms.  The captured nodes and handles belonging to SMALL are
   added to LARGE and their ->atom pointers are all updated.  The associated counts are
   updated as well, and any waiting handles belonging to either are awakened.  Finally the
   smaller atom's refcount is decremented.
*/
static void
capture_fuse_into(txn_atom * small, txn_atom * large)
{
	int level;
	unsigned zcount = 0;
	unsigned tcount = 0;
	protected_jnodes *prot_list;

	assert("umka-224", small != NULL);
	assert("umka-225", small != NULL);

	assert("umka-299", spin_atom_is_locked(large));
	assert("umka-300", spin_atom_is_locked(small));

	assert("jmacd-201", atom_isopen(small));
	assert("jmacd-202", atom_isopen(large));

	ON_TRACE(TRACE_TXN, "fuse atom %u into %u\n", small->atom_id, large->atom_id);

	/* Splice and update the per-level dirty jnode lists */
	for (level = 0; level < REAL_MAX_ZTREE_HEIGHT + 1; level += 1) {
		zcount += capture_fuse_jnode_lists(large, ATOM_DIRTY_LIST(large, level), ATOM_DIRTY_LIST(small, level));
	}

	/* Splice and update the [clean,dirty] jnode and txnh lists */
	zcount += capture_fuse_jnode_lists(large, ATOM_CLEAN_LIST(large), ATOM_CLEAN_LIST(small));
	zcount += capture_fuse_jnode_lists(large, ATOM_OVRWR_LIST(large), ATOM_OVRWR_LIST(small));
	zcount += capture_fuse_jnode_lists(large, ATOM_WB_LIST(large), ATOM_WB_LIST(small));
	zcount += capture_fuse_jnode_lists(large, &large->inodes, &small->inodes);
	tcount += capture_fuse_txnh_lists(large, &large->txnh_list, &small->txnh_list);

	for_all_type_safe_list(prot, &small->protected, prot_list) {
		jnode *node;

		for_all_type_safe_list(capture, &prot_list->nodes, node) {
			zcount += 1;

			LOCK_JNODE(node);
			assert("nikita-3375", node->atom == small);
			/* With the jnode lock held, update atom pointer. */
			node->atom = large;
			UNLOCK_JNODE(node);
		}
	}
	/* Splice the lists of lists. */
	prot_list_splice(&large->protected, &small->protected);

	/* Check our accounting. */
	assert("jmacd-1063", zcount + small->num_queued == small->capture_count);
	assert("jmacd-1065", tcount == small->txnh_count);

	/* sum numbers of waiters threads */
	large->nr_waiters += small->nr_waiters;
	small->nr_waiters = 0;

	/* splice flush queues */
	fuse_fq(large, small);

	/* update counter of jnode on every atom' list */
	ON_DEBUG(large->dirty += small->dirty;
		 small->dirty = 0;
		 large->clean += small->clean;
		 small->clean = 0;
		 large->ovrwr += small->ovrwr;
		 small->ovrwr = 0;
		 large->wb += small->wb;
		 small->wb = 0;
		 large->fq += small->fq;
		 small->fq = 0;
		 large->protect += small->protect;
		 small->protect = 0;
		);

	/* count flushers in result atom */
	large->nr_flushers += small->nr_flushers;
	small->nr_flushers = 0;

	/* update counts of flushed nodes */
	large->flushed += small->flushed;
	small->flushed = 0;

	/* Transfer list counts to large. */
	large->txnh_count += small->txnh_count;
	large->capture_count += small->capture_count;

	/* Add all txnh references to large. */
	atomic_add(small->txnh_count, &large->refcount);
	atomic_sub(small->txnh_count, &small->refcount);

	/* Reset small counts */
	small->txnh_count = 0;
	small->capture_count = 0;

	/* Assign the oldest start_time, merge flags. */
	large->start_time = min(large->start_time, small->start_time);
	large->flags |= small->flags;

	/* Merge blocknr sets. */
	blocknr_set_merge(&small->delete_set, &large->delete_set);
	blocknr_set_merge(&small->wandered_map, &large->wandered_map);

	/* Merge allocated/deleted file counts */
	large->nr_objects_deleted += small->nr_objects_deleted;
	large->nr_objects_created += small->nr_objects_created;

	small->nr_objects_deleted = 0;
	small->nr_objects_created = 0;

	/* Merge allocated blocks counts */
	large->nr_blocks_allocated += small->nr_blocks_allocated;

	large->nr_running_queues += small->nr_running_queues;
	small->nr_running_queues = 0;

	/* Merge blocks reserved for overwrite set. */
	large->flush_reserved += small->flush_reserved;
	small->flush_reserved = 0;

	if (large->stage < small->stage) {
		/* Large only needs to notify if it has changed state. */
		atom_set_stage(large, small->stage);
		wakeup_atom_waiting_list(large);
	}

	atom_set_stage(small, ASTAGE_INVALID);

	/* Notify any waiters--small needs to unload its wait lists.  Waiters
	   actually remove themselves from the list before returning from the
	   fuse_wait function. */
	wakeup_atom_waiting_list(small);

	/* Unlock atoms */
	UNLOCK_ATOM(large);
	atom_dec_and_unlock(small);
}

reiser4_internal void
protected_jnodes_init(protected_jnodes *list)
{
	txn_atom *atom;

	assert("nikita-3376", list != NULL);

	atom = get_current_atom_locked();
	prot_list_push_front(&atom->protected, list);
	capture_list_init(&list->nodes);
	UNLOCK_ATOM(atom);
}

reiser4_internal void
protected_jnodes_done(protected_jnodes *list)
{
	txn_atom *atom;

	assert("nikita-3379", capture_list_empty(&list->nodes));

	atom = get_current_atom_locked();
	prot_list_remove(list);
	UNLOCK_ATOM(atom);
}

/* TXNMGR STUFF */

#if REISER4_COPY_ON_CAPTURE

/* copy on capture steals jnode (J) from capture list. It may replace (J) with
   special newly created jnode (CCJ) to which J's page gets attached. J in its
   turn gets newly created copy of page.
   Or, it may merely take J from capture list if J was never dirtied

   The problem with this replacement is that capture lists are being contiguously
   scanned.
   Race between replacement and scanning are avoided with one global spin lock
   (scan_lock) and JNODE_SCANNED state of jnode. Replacement (in capture copy)
   goes under scan_lock locked only if jnode is not in JNODE_SCANNED state. This
   state gets set under scan_lock locked whenever scanning is working with that
   jnode.
*/

/* remove jnode page from mapping's tree and insert new page with the same index */
static void
replace_page_in_mapping(jnode *node, struct page *new_page)
{
	struct address_space *mapping;
	unsigned long index;

	mapping = jnode_get_mapping(node);
	index = jnode_get_index(node);

	spin_lock(&mapping->page_lock);

	/* delete old page from. This resembles __remove_from_page_cache */
	assert("vs-1416", radix_tree_lookup(&mapping->page_tree, index) == node->pg);
	assert("vs-1428", node->pg->mapping == mapping);
	__remove_from_page_cache(node->pg);

	/* insert new page into mapping */
	check_me("vs-1411",
		 radix_tree_insert(&mapping->page_tree, index, new_page) == 0);

	/* this resembles add_to_page_cache */
	page_cache_get(new_page);
	___add_to_page_cache(new_page, mapping, index);

	spin_unlock(&mapping->page_lock);
	lru_cache_add(new_page);
}

/* attach page of @node to @copy, @new_page to @node */
static void
swap_jnode_pages(jnode *node, jnode *copy, struct page *new_page)
{
	/* attach old page to new jnode */
	assert("vs-1414", jnode_by_page(node->pg) == node);
	copy->pg = node->pg;
	copy->data = page_address(copy->pg);
	jnode_set_block(copy, jnode_get_block(node));
	copy->pg->private = (unsigned long)copy;

	/* attach new page to jnode */
	assert("vs-1412", !PagePrivate(new_page));
	page_cache_get(new_page);
	node->pg = new_page;
	node->data = page_address(new_page);
	new_page->private = (unsigned long)node;
	SetPagePrivate(new_page);

	{
		/* insert old page to new mapping */
		struct address_space *mapping;
		unsigned long index;

		mapping = get_current_super_private()->cc->i_mapping;
		index = (unsigned long)copy;
		spin_lock(&mapping->page_lock);

		/* insert old page into new (fake) mapping. No page_cache_get
		   because page reference counter was not decreased on removing
		   it from old mapping */
		assert("vs-1416", radix_tree_lookup(&mapping->page_tree, index) == NULL);
		check_me("vs-1418", radix_tree_insert(&mapping->page_tree, index, copy->pg) == 0);
		___add_to_page_cache(copy->pg, mapping, index);
		ON_DEBUG(set_bit(PG_arch_1, &(copy->pg)->flags));

		/* corresponding page_cache_release is in invalidate_list */
		page_cache_get(copy->pg);
		spin_unlock(&mapping->page_lock);
	}
}

/* this is to make capture copied jnode looking like if there were jload called for it */
static void
fake_jload(jnode *node)
{
	jref(node);
	atomic_inc(&node->d_count);
	JF_SET(node, JNODE_PARSED);
}

/* for now - refuse to copy-on-capture any suspicious nodes (WRITEBACK, DIRTY, FLUSH_QUEUED) */
static int
check_capturable(const jnode *node, const txn_atom *atom)
{
	assert("vs-1429", spin_jnode_is_locked(node));
	assert("vs-1487", check_spin_is_locked(&scan_lock));

	if (JF_ISSET(node, JNODE_WRITEBACK)) {
		reiser4_stat_inc(coc.writeback);
		return RETERR(-E_WAIT);
	}
	if (JF_ISSET(node, JNODE_FLUSH_QUEUED)) {
		reiser4_stat_inc(coc.flush_queued);
		return RETERR(-E_WAIT);
	}
	if (JF_ISSET(node, JNODE_DIRTY)) {
		reiser4_stat_inc(coc.dirty);
		return RETERR(-E_WAIT);
	}
	if (JF_ISSET(node, JNODE_SCANNED)) {
		reiser4_stat_inc(coc.scan_race);
		return RETERR(-E_REPEAT);
	}
	if (node->atom != atom) {
		reiser4_stat_inc(coc.atom_changed);
		return RETERR(-E_WAIT);
	}
	return 0; /* OK */
}

static void
remove_from_capture_list(jnode *node)
{
	ON_DEBUG_MODIFY(znode_set_checksum(node, 1));
	JF_CLR(node, JNODE_DIRTY);
	JF_CLR(node, JNODE_RELOC);
	JF_CLR(node, JNODE_OVRWR);
	JF_CLR(node, JNODE_CREATED);
	JF_CLR(node, JNODE_WRITEBACK);
	JF_CLR(node, JNODE_REPACK);

	capture_list_remove_clean(node);
	node->atom->capture_count --;
	atomic_dec(&node->x_count);
	/*XXXX*/ON_DEBUG(count_jnode(node->atom, node, NODE_LIST(node), NOT_CAPTURED, 1));
	node->atom = 0;
}

/* insert new jnode (copy) to capture list instead of old one */
static void
replace_on_capture_list(jnode *node, jnode *copy)
{
	assert("vs-1415", node->atom);
	assert("vs-1489", !capture_list_is_clean(node));
	assert("vs-1493", JF_ISSET(copy, JNODE_CC) && JF_ISSET(copy, JNODE_HEARD_BANSHEE));

	copy->state |= node->state;

	/* insert cc-jnode @copy into capture list before old jnode @node */
	capture_list_insert_before(node, copy);
	jref(copy);
	copy->atom = node->atom;
	node->atom->capture_count ++;
	/*XXXX*/ON_DEBUG(count_jnode(node->atom, copy, NODE_LIST(copy), NODE_LIST(node), 1));

	/* remove old jnode from capture list */
	remove_from_capture_list(node);
}

/* when capture request is made for a node which is captured but was never
   dirtied copy on capture will merely uncapture it */
static int
copy_on_capture_clean(jnode *node, txn_atom *atom)
{
	int result;

	assert("vs-1625", spin_atom_is_locked(atom));
	assert("vs-1432", spin_jnode_is_locked(node));
	assert("vs-1627", !JF_ISSET(node, JNODE_WRITEBACK));

	spin_lock(&scan_lock);
	result = check_capturable(node, atom);
	if (result == 0) {
		/* remove jnode from capture list */
		remove_from_capture_list(node);
		reiser4_stat_inc(coc.ok_clean);
	}
	spin_unlock(&scan_lock);
	UNLOCK_JNODE(node);
	UNLOCK_ATOM(atom);

	return result;
}

static void
lock_two_nodes(jnode *node1, jnode *node2)
{
	if (node1 > node2) {
		LOCK_JNODE(node2);
		LOCK_JNODE(node1);
	} else {
		LOCK_JNODE(node1);
		LOCK_JNODE(node2);
	}
}

/* capture request is made for node which does not have page. In most cases this
   is "uber" znode */
static int
copy_on_capture_nopage(jnode *node, txn_atom *atom)
{
	int result;
	jnode *copy;

	assert("vs-1432", spin_atom_is_locked(atom));
	assert("vs-1432", spin_jnode_is_locked(node));

	jref(node);
	UNLOCK_JNODE(node);
	UNLOCK_ATOM(atom);
	assert("nikita-3475", schedulable());
	copy = jclone(node);
	if (IS_ERR(copy)) {
		jput(node);
		return PTR_ERR(copy);
	}

	LOCK_ATOM(atom);
	lock_two_nodes(node, copy);
	spin_lock(&scan_lock);

	result = check_capturable(node, atom);
	if (result == 0) {
		if (jnode_page(node) == NULL) {
			replace_on_capture_list(node, copy);
#if REISER4_STATS
			if (znode_above_root(JZNODE(node)))
				reiser4_stat_inc(coc.ok_uber);
			else
				reiser4_stat_inc(coc.ok_nopage);
#endif
		} else
			result = RETERR(-E_REPEAT);
	}

	spin_unlock(&scan_lock);
	UNLOCK_JNODE(node);
	UNLOCK_JNODE(copy);
	UNLOCK_ATOM(atom);
	assert("nikita-3476", schedulable());
	jput(copy);
	assert("nikita-3477", schedulable());
	jput(node);
	assert("nikita-3478", schedulable());
	ON_TRACE(TRACE_CAPTURE_COPY, "nopage\n");
	return result;
}

static int
handle_coc(jnode *node, jnode *copy, struct page *page, struct page *new_page,
	   txn_atom *atom)
{
	char *to;
	char *from;
	int   result;

	to = kmap(new_page);
	lock_page(page);
	from = kmap(page);
	/*
	 * FIXME(zam): one preloaded radix tree node may be not enough for two
	 * insertions, one insertion is in replace_page_in_mapping(), another
	 * one is in swap_jnode_pages(). The radix_tree_delete() call might
	 * not help, because an empty radix tree node is freed and the node's
	 * free space may not be re-used in insertion.
	 */
	radix_tree_preload(GFP_KERNEL);
	LOCK_ATOM(atom);
	lock_two_nodes(node, copy);
	spin_lock(&scan_lock);

	result = check_capturable(node, atom);
	if (result == 0) {
		/* if node was jloaded by get_overwrite_set, we have to jrelse
		   it here, because we remove jnode from atom's capture list -
		   put_overwrite_set will not jrelse it */
		int was_jloaded;

		was_jloaded = JF_ISSET(node, JNODE_JLOADED_BY_GET_OVERWRITE_SET);

		replace_page_in_mapping(node, new_page);
		swap_jnode_pages(node, copy, new_page);
		replace_on_capture_list(node, copy);
		/* statistics */
		if (JF_ISSET(copy, JNODE_RELOC)) {
			reiser4_stat_inc(coc.ok_reloc);
		} else if (JF_ISSET(copy, JNODE_OVRWR)) {
			reiser4_stat_inc(coc.ok_ovrwr);
		} else
			impossible("", "");

		memcpy(to, from, PAGE_CACHE_SIZE);
		SetPageUptodate(new_page);
		if (was_jloaded)
			fake_jload(copy);
		else
			kunmap(page);

		assert("vs-1419", page_count(new_page) >= 3);
		spin_unlock(&scan_lock);
		UNLOCK_JNODE(node);
		UNLOCK_JNODE(copy);
		UNLOCK_ATOM(atom);
		radix_tree_preload_end();
		unlock_page(page);

		if (was_jloaded) {
			jrelse_tail(node);
			assert("vs-1494", JF_ISSET(node, JNODE_JLOADED_BY_GET_OVERWRITE_SET));
			JF_CLR(node, JNODE_JLOADED_BY_GET_OVERWRITE_SET);
		} else
			kunmap(new_page);

		jput(copy);
		jrelse(node);
		jput(node);
		page_cache_release(page);
		page_cache_release(new_page);
		ON_TRACE(TRACE_CAPTURE_COPY, "copy on capture done\n");
	} else {
		spin_unlock(&scan_lock);
		UNLOCK_JNODE(node);
		UNLOCK_JNODE(copy);
		UNLOCK_ATOM(atom);
		radix_tree_preload_end();
		kunmap(page);
		unlock_page(page);
		kunmap(new_page);
		page_cache_release(new_page);
	}
	return result;
}

static int
real_copy_on_capture(jnode *node, txn_atom *atom)
{
	int result;
	jnode *copy;
	struct page *page;
	struct page *new_page;

	assert("vs-1432", spin_jnode_is_locked(node));
	assert("vs-1490", !JF_ISSET(node, JNODE_EFLUSH));
	assert("vs-1491", node->pg);
	assert("vs-1492", jprivate(node->pg) == node);

	page = node->pg;
	page_cache_get(page);
	jref(node);
	UNLOCK_JNODE(node);
	UNLOCK_ATOM(atom);

	/* prevent node from eflushing */
	result = jload(node);
	if (!result) {
		copy = jclone(node);
		if (likely(!IS_ERR(copy))) {
			new_page = alloc_page(GFP_KERNEL);
			if (new_page) {
				result = handle_coc(node,
						    copy, page, new_page, atom);
				if (result == 0)
					return 0;
			} else
				result = RETERR(-ENOMEM);
			jput(copy);
		}
		jrelse(node);
	}

	jput(node);
	page_cache_release(page);
	return result;
}

/* create new jnode, create new page, jload old jnode, copy data, detach old
   page from old jnode, attach new page to old jnode, attach old page to new
   jnode this returns 0 if copy on capture succeeded, E_REPEAT to have
   capture_fuse_wait to be called */
static int
create_copy_and_replace(jnode *node, txn_atom *atom)
{
	int result;
	struct inode *inode; /* inode for which filemap_nopage is blocked */

	assert("jmacd-321", spin_jnode_is_locked(node));
	assert("umka-295", spin_atom_is_locked(atom));
	assert("vs-1381", node->atom == atom);
	assert("vs-1409", atom->stage > ASTAGE_CAPTURE_WAIT && atom->stage < ASTAGE_DONE);
	assert("vs-1410", jnode_is_znode(node) || jnode_is_unformatted(node));


	if (JF_ISSET(node, JNODE_CCED)) {
		/* node is under copy on capture already */
		reiser4_stat_inc(coc.coc_race);
		UNLOCK_JNODE(node);
		UNLOCK_ATOM(atom);
		return RETERR(-E_WAIT);
	}

	/* measure how often suspicious (WRITEBACK, DIRTY, FLUSH_QUEUED) appear
	   here. For most often case we can return EAGAIN right here and avoid
	   all the preparations made for copy on capture */
	ON_TRACE(TRACE_CAPTURE_COPY, "copy_on_capture: node %p, atom %p..", node, atom);
	if (JF_ISSET(node, JNODE_EFLUSH)) {
		UNLOCK_JNODE(node);
		UNLOCK_ATOM(atom);

		reiser4_stat_inc(coc.eflush);
		ON_TRACE(TRACE_CAPTURE_COPY, "eflushed\n");
		result = jload(node);
		if (result)
			return RETERR(result);
		jrelse(node);
		return RETERR(-E_REPEAT);
	}

	set_cced_bit(node);

	if (jnode_is_unformatted(node)) {
		/* to capture_copy unformatted node we have to take care of its
		   page mappings. Page gets unmapped here and concurrent
		   mappings are blocked on reiser4 inodes's coc_sem in reiser4's
		   filemap_nopage */
		struct page *page;

		inode = mapping_jnode(node)->host;
		page = jnode_page(node);
		assert("vs-1640", inode != NULL);
		assert("vs-1641", page != NULL);
		assert("vs-1642", page->mapping != NULL);
		UNLOCK_JNODE(node);
		UNLOCK_ATOM(atom);

		down_write(&reiser4_inode_data(inode)->coc_sem);
		lock_page(page);
		pte_chain_lock(page);

		if (page_mapped(page)) {
			result = try_to_unmap(page);
			if (result == SWAP_AGAIN) {
				result = RETERR(-E_REPEAT);

			} else if (result == SWAP_FAIL)
				result = RETERR(-E_WAIT);
			else {
				assert("vs-1643", result == SWAP_SUCCESS);
				result = 0;
			}
			if (result != 0) {
				unlock_page(page);
				pte_chain_unlock(page);
				up_write(&reiser4_inode_data(inode)->coc_sem);
				return result;
			}
		}
		pte_chain_unlock(page);
		unlock_page(page);
		LOCK_ATOM(atom);
		LOCK_JNODE(node);
	} else
		inode = NULL;

	if (!JF_ISSET(node, JNODE_OVRWR) && !JF_ISSET(node, JNODE_RELOC)) {
		/* clean node can be made available for capturing. Just take
		   care to preserve atom list during uncapturing */
		ON_TRACE(TRACE_CAPTURE_COPY, "clean\n");
		result = copy_on_capture_clean(node, atom);
	} else if (!node->pg) {
		ON_TRACE(TRACE_CAPTURE_COPY, "uber\n");
		result = copy_on_capture_nopage(node, atom);
	} else
		result = real_copy_on_capture(node, atom);
	if (result != 0)
		clear_cced_bits(node);
	assert("vs-1626", spin_atom_is_not_locked(atom));

	if (inode != NULL)
		up_write(&reiser4_inode_data(inode)->coc_sem);

	return result;
}
#endif /* REISER4_COPY_ON_CAPTURE */

/* Perform copy-on-capture of a block. */
static int
capture_copy(jnode * node, txn_handle * txnh, txn_atom * atomf, txn_atom * atomh, txn_capture mode, int can_coc)
{
#if REISER4_COPY_ON_CAPTURE
	reiser4_stat_inc(coc.calls);

	/* do not copy on capture in ent thread to avoid deadlock on coc semaphore */
	if (can_coc && get_current_context()->entd == 0) {
		int result;

		ON_TRACE(TRACE_TXN, "capture_copy\n");

		/* The txnh and its (possibly NULL) atom's locks are not needed
		   at this point. */
		UNLOCK_TXNH(txnh);
		if (atomh != NULL)
			UNLOCK_ATOM(atomh);

		/* create a copy of node, detach node from atom and attach its copy
		   instead */
		atomic_inc(&atomf->refcount);
		result = create_copy_and_replace(node, atomf);
		assert("nikita-3474", schedulable());
		preempt_point();
		LOCK_ATOM(atomf);
		atom_dec_and_unlock(atomf);
		preempt_point();

		if (result == 0) {
			if (jnode_is_znode(node)) {
				znode *z;

				z = JZNODE(node);
				z->version = znode_build_version(jnode_get_tree(node));
			}
			result = RETERR(-E_REPEAT);
		}
		return result;
	}

	reiser4_stat_inc(coc.forbidden);
	return capture_fuse_wait(node, txnh, atomf, atomh, mode);
#else
	ON_TRACE(TRACE_TXN, "capture_copy: fuse wait\n");

	return capture_fuse_wait(node, txnh, atomf, atomh, mode);

#endif
}

/* Release a block from the atom, reversing the effects of being captured,
   do not release atom's reference to jnode due to holding spin-locks.
   Currently this is only called when the atom commits.

   NOTE: this function does not release a (journal) reference to jnode
   due to locking optimizations, you should call jput() somewhere after
   calling uncapture_block(). */
reiser4_internal void uncapture_block(jnode * node)
{
	txn_atom * atom;

	assert("umka-226", node != NULL);
	atom = node->atom;
	assert("umka-228", atom != NULL);

	assert("jmacd-1021", node->atom == atom);
	assert("jmacd-1022", spin_jnode_is_locked(node));
#if REISER4_COPY_ON_CAPTURE
	assert("jmacd-1023", spin_atom_is_locked(atom));
#else
	assert("jmacd-1023", atom_is_protected(atom));
#endif

	/*ON_TRACE (TRACE_TXN, "un-capture %p from atom %u (captured %u)\n",
	 * node, atom->atom_id, atom->capture_count); */

 	ON_DEBUG_MODIFY(znode_set_checksum(node, 1));
	JF_CLR(node, JNODE_DIRTY);
	JF_CLR(node, JNODE_RELOC);
	JF_CLR(node, JNODE_OVRWR);
	JF_CLR(node, JNODE_CREATED);
	JF_CLR(node, JNODE_WRITEBACK);
	JF_CLR(node, JNODE_REPACK);
	clear_cced_bits(node);
#if REISER4_DEBUG
	node->written = 0;
#endif

	capture_list_remove_clean(node);
	if (JF_ISSET(node, JNODE_FLUSH_QUEUED)) {
		assert("zam-925", atom_isopen(atom));
		assert("vs-1623", NODE_LIST(node) == FQ_LIST);
		ON_DEBUG(atom->num_queued --);
		JF_CLR(node, JNODE_FLUSH_QUEUED);
	}
	atom->capture_count -= 1;
	ON_DEBUG(count_jnode(atom, node, NODE_LIST(node), NOT_CAPTURED, 1));
	node->atom = NULL;

	UNLOCK_JNODE(node);
	LOCK_CNT_DEC(t_refs);
}

/* Unconditional insert of jnode into atom's overwrite list. Currently used in
   bitmap-based allocator code for adding modified bitmap blocks the
   transaction. @atom and @node are spin locked */
reiser4_internal void
insert_into_atom_ovrwr_list(txn_atom * atom, jnode * node)
{
	assert("zam-538", spin_atom_is_locked(atom) || atom->stage >= ASTAGE_PRE_COMMIT);
	assert("zam-539", spin_jnode_is_locked(node));
	assert("zam-899", JF_ISSET(node, JNODE_OVRWR));
	assert("zam-543", node->atom == NULL);
	assert("vs-1433", !jnode_is_unformatted(node) && !jnode_is_znode(node));

	capture_list_push_front(ATOM_OVRWR_LIST(atom), node);
	jref(node);
	node->atom = atom;
	atom->capture_count++;
	ON_DEBUG(count_jnode(atom, node, NODE_LIST(node), OVRWR_LIST, 1));
}

/* return 1 if two dirty jnodes belong to one atom, 0 - otherwise */
reiser4_internal int
jnodes_of_one_atom(jnode * j1, jnode * j2)
{
	int ret = 0;
	int finish = 0;

	assert("zam-9003", j1 != j2);
	/*assert ("zam-9004", jnode_check_dirty (j1)); */
	assert("zam-9005", jnode_check_dirty(j2));

	do {
		LOCK_JNODE(j1);
		assert("zam-9001", j1->atom != NULL);
		if (spin_trylock_jnode(j2)) {
			assert("zam-9002", j2->atom != NULL);
			ret = (j2->atom == j1->atom);
			finish = 1;

			UNLOCK_JNODE(j2);
		}
		UNLOCK_JNODE(j1);
	} while (!finish);

	return ret;
}

/* when atom becomes that big, commit it as soon as possible. This was found
 * to be most effective by testing. */
reiser4_internal unsigned int
txnmgr_get_max_atom_size(struct super_block *super UNUSED_ARG)
{
	return totalram_pages / 4;
}


#if REISER4_DEBUG_OUTPUT

reiser4_internal void
info_atom(const char *prefix, const txn_atom * atom)
{
	if (atom == NULL) {
		printk("%s: no atom\n", prefix);
		return;
	}

	printk("%s: refcount: %i id: %i flags: %x txnh_count: %i"
	       " capture_count: %i stage: %x start: %lu, flushed: %i\n", prefix,
	       atomic_read(&atom->refcount), atom->atom_id, atom->flags, atom->txnh_count,
	       atom->capture_count, atom->stage, atom->start_time, atom->flushed);
}


reiser4_internal void
print_atom(const char *prefix, txn_atom * atom)
{
	jnode *pos_in_atom;
	char list[32];
	int level;

	assert("umka-229", atom != NULL);

	info_atom(prefix, atom);

	for (level = 0; level < REAL_MAX_ZTREE_HEIGHT + 1; level += 1) {

		sprintf(list, "capture level %d", level);

		for (pos_in_atom = capture_list_front(ATOM_DIRTY_LIST(atom, level));
		     !capture_list_end(ATOM_DIRTY_LIST(atom, level), pos_in_atom);
		     pos_in_atom = capture_list_next(pos_in_atom)) {

			info_jnode(list, pos_in_atom);
			printk("\n");
		}
	}

	for_all_type_safe_list(capture, ATOM_CLEAN_LIST(atom), pos_in_atom) {
		info_jnode("clean", pos_in_atom);
		printk("\n");
	}
}
#endif

static int count_deleted_blocks_actor (
	txn_atom *atom, const reiser4_block_nr * a, const reiser4_block_nr *b, void * data)
{
	reiser4_block_nr *counter = data;

	assert ("zam-995", data != NULL);
	assert ("zam-996", a != NULL);
	if (b == NULL)
		*counter += 1;
	else
		*counter += *b;
	return 0;
}
reiser4_internal reiser4_block_nr txnmgr_count_deleted_blocks (void)
{
	reiser4_block_nr result;
	txn_mgr *tmgr = &get_super_private(reiser4_get_current_sb())->tmgr;
	txn_atom * atom;

	result = 0;

	spin_lock_txnmgr(tmgr);
	for_all_type_safe_list(atom, &tmgr->atoms_list, atom) {
		LOCK_ATOM(atom);
		blocknr_set_iterator(atom, &atom->delete_set,
				     count_deleted_blocks_actor, &result, 0);
		UNLOCK_ATOM(atom);
	}
	spin_unlock_txnmgr(tmgr);

	return result;
}

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 80
   End:
*/
