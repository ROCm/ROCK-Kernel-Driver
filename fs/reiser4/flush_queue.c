/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

#include "debug.h"
#include "type_safe_list.h"
#include "super.h"
#include "txnmgr.h"
#include "jnode.h"
#include "znode.h"
#include "page_cache.h"
#include "wander.h"
#include "vfs_ops.h"
#include "writeout.h"

#include <linux/bio.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/blkdev.h>
#include <linux/writeback.h>

/* A flush queue object is an accumulator for keeping jnodes prepared
   by the jnode_flush() function for writing to disk. Those "queued" jnodes are
   kept on the flush queue until memory pressure or atom commit asks
   flush queues to write some or all from their jnodes. */

TYPE_SAFE_LIST_DEFINE(fq, flush_queue_t, alink);

#if REISER4_DEBUG
#   define spin_ordering_pred_fq(fq)  (1)
#endif

SPIN_LOCK_FUNCTIONS(fq, flush_queue_t, guard);

/*
   LOCKING:

   fq->guard spin lock protects fq->atom pointer and nothing else.  fq->prepped
   list protected by atom spin lock.  fq->prepped list uses the following
   locking:

   two ways to protect fq->prepped list for read-only list traversal:

   1. atom spin-lock atom.
   2. fq is IN_USE, atom->nr_running_queues increased.

   and one for list modification:

   1. atom is spin-locked and one condition is true: fq is IN_USE or
      atom->nr_running_queues == 0.

   The deadlock-safe order for flush queues and atoms is: first lock atom, then
   lock flush queue, then lock jnode.
*/

#define fq_in_use(fq)          ((fq)->state & FQ_IN_USE)
#define fq_ready(fq)           (!fq_in_use(fq))

#define mark_fq_in_use(fq)     do { (fq)->state |= FQ_IN_USE;    } while (0)
#define mark_fq_ready(fq)      do { (fq)->state &= ~FQ_IN_USE;   } while (0)

/* get lock on atom from locked flush queue object */
reiser4_internal txn_atom *
atom_get_locked_by_fq(flush_queue_t * fq)
{
	/* This code is similar to jnode_get_atom(), look at it for the
	 * explanation. */
	txn_atom *atom;

	assert("zam-729", spin_fq_is_locked(fq));

	while(1) {
		atom = fq->atom;
		if (atom == NULL)
			break;

		if (spin_trylock_atom(atom))
			break;

		atomic_inc(&atom->refcount);
		spin_unlock_fq(fq);
		LOCK_ATOM(atom);
		spin_lock_fq(fq);

		if (fq->atom == atom) {
			atomic_dec(&atom->refcount);
			break;
		}

		spin_unlock_fq(fq);
		atom_dec_and_unlock(atom);
		spin_lock_fq(fq);
	}

	return atom;
}

reiser4_internal txn_atom *
atom_locked_by_fq(flush_queue_t * fq)
{
	return UNDER_SPIN(fq, fq, atom_get_locked_by_fq(fq));
}

static void
init_fq(flush_queue_t * fq)
{
	xmemset(fq, 0, sizeof *fq);

	atomic_set(&fq->nr_submitted, 0);

	capture_list_init(ATOM_FQ_LIST(fq));

	sema_init(&fq->io_sem, 0);
	spin_fq_init(fq);
}

/* slab for flush queues */
static kmem_cache_t *fq_slab;

reiser4_internal int init_fqs(void)
{
	fq_slab = kmem_cache_create("fq",
				    sizeof (flush_queue_t),
				    0,
				    SLAB_HWCACHE_ALIGN,
				    NULL,
				    NULL);
	return (fq_slab == NULL) ? RETERR(-ENOMEM) : 0;
}

reiser4_internal void done_fqs(void)
{
	kmem_cache_destroy(fq_slab);
}

/* create new flush queue object */
static flush_queue_t *
create_fq(int gfp)
{
	flush_queue_t *fq;

	fq = kmem_cache_alloc(fq_slab, gfp);
	if (fq)
		init_fq(fq);

	return fq;
}

/* adjust atom's and flush queue's counters of queued nodes */
static void
count_enqueued_node(flush_queue_t * fq)
{
	ON_DEBUG(fq->atom->num_queued++);
}

static void
count_dequeued_node(flush_queue_t * fq)
{
	assert("zam-993", fq->atom->num_queued > 0);
	ON_DEBUG(fq->atom->num_queued--);
}

/* attach flush queue object to the atom */
static void
attach_fq(txn_atom * atom, flush_queue_t * fq)
{
	assert("zam-718", spin_atom_is_locked(atom));
	fq_list_push_front(&atom->flush_queues, fq);
	fq->atom = atom;
	ON_DEBUG(atom->nr_flush_queues++);
}

static void
detach_fq(flush_queue_t * fq)
{
	assert("zam-731", spin_atom_is_locked(fq->atom));

	spin_lock_fq(fq);
	fq_list_remove_clean(fq);
	assert("vs-1456", fq->atom->nr_flush_queues > 0);
	ON_DEBUG(fq->atom->nr_flush_queues--);
	fq->atom = NULL;
	spin_unlock_fq(fq);
}

/* destroy flush queue object */
reiser4_internal void
done_fq(flush_queue_t * fq)
{
	assert("zam-763", capture_list_empty(ATOM_FQ_LIST(fq)));
	assert("zam-766", atomic_read(&fq->nr_submitted) == 0);

	kmem_cache_free(fq_slab, fq);
}

/* */
reiser4_internal void
mark_jnode_queued(flush_queue_t *fq, jnode *node)
{
	JF_SET(node, JNODE_FLUSH_QUEUED);
	count_enqueued_node(fq);
}

/* Putting jnode into the flush queue. Both atom and jnode should be
   spin-locked. */
reiser4_internal void
queue_jnode(flush_queue_t * fq, jnode * node)
{
	assert("zam-711", spin_jnode_is_locked(node));
	assert("zam-713", node->atom != NULL);
	assert("zam-712", spin_atom_is_locked(node->atom));
	assert("zam-714", jnode_is_dirty(node));
	assert("zam-716", fq->atom != NULL);
	assert("zam-717", fq->atom == node->atom);
	assert("zam-907", fq_in_use(fq));

	assert("zam-826", JF_ISSET(node, JNODE_RELOC));
	assert("vs-1481", !JF_ISSET(node, JNODE_FLUSH_QUEUED));
	assert("vs-1481", NODE_LIST(node) != FQ_LIST);

	mark_jnode_queued(fq, node);
	capture_list_remove_clean(node);
	capture_list_push_back(ATOM_FQ_LIST(fq), node);
	/*XXXX*/ON_DEBUG(count_jnode(node->atom, node, NODE_LIST(node), FQ_LIST, 1));
}

/* repeatable process for waiting io completion on a flush queue object */
static int
wait_io(flush_queue_t * fq, int *nr_io_errors)
{
	assert("zam-738", fq->atom != NULL);
	assert("zam-739", spin_atom_is_locked(fq->atom));
	assert("zam-736", fq_in_use(fq));
	assert("zam-911", capture_list_empty(ATOM_FQ_LIST(fq)));

	if (atomic_read(&fq->nr_submitted) != 0) {
		struct super_block *super;

		UNLOCK_ATOM(fq->atom);

		assert("nikita-3013", schedulable());

		super = reiser4_get_current_sb();

		/* FIXME: this is instead of blk_run_queues() */
		blk_run_address_space(get_super_fake(super)->i_mapping);

		if ( !(super->s_flags & MS_RDONLY) )
			down(&fq->io_sem);

		/* Ask the caller to re-acquire the locks and call this
		   function again. Note: this technique is commonly used in
		   the txnmgr code. */
		return -E_REPEAT;
	}

	*nr_io_errors += atomic_read(&fq->nr_errors);
	return 0;
}

/* wait on I/O completion, re-submit dirty nodes to write */
static int
finish_fq(flush_queue_t * fq, int *nr_io_errors)
{
	int ret;
	txn_atom * atom = fq->atom;

	assert("zam-801", atom != NULL);
	assert("zam-744", spin_atom_is_locked(atom));
	assert("zam-762", fq_in_use(fq));

	ret = wait_io(fq, nr_io_errors);
	if (ret)
		return ret;

	detach_fq(fq);
	done_fq(fq);

	atom_send_event(atom);

	return 0;
}

/* wait for all i/o for given atom to be completed, actually do one iteration
   on that and return -E_REPEAT if there more iterations needed */
static int
finish_all_fq(txn_atom * atom, int *nr_io_errors)
{
	flush_queue_t *fq;

	assert("zam-730", spin_atom_is_locked(atom));

	if (fq_list_empty(&atom->flush_queues))
		return 0;

	for_all_type_safe_list(fq, &atom->flush_queues, fq) {
		if (fq_ready(fq)) {
			int ret;

			mark_fq_in_use(fq);
			assert("vs-1247", fq->owner == NULL);
			ON_DEBUG(fq->owner = current);
			ret = finish_fq(fq, nr_io_errors);

			if ( *nr_io_errors )
				reiser4_handle_error();

			if (ret) {
				fq_put(fq);
				return ret;
			}

			UNLOCK_ATOM(atom);

			return -E_REPEAT;
		}
	}

	/* All flush queues are in use; atom remains locked */
	return -EBUSY;
}

/* wait all i/o for current atom */
reiser4_internal int
current_atom_finish_all_fq(void)
{
	txn_atom *atom;
	int nr_io_errors = 0;
	int ret = 0;

	do {
		while (1) {
			atom = get_current_atom_locked();
			ret = finish_all_fq(atom, &nr_io_errors);
			if (ret != -EBUSY)
				break;
			atom_wait_event(atom);
		}
	} while (ret == -E_REPEAT);

	/* we do not need locked atom after this function finishes, SUCCESS or
	   -EBUSY are two return codes when atom remains locked after
	   finish_all_fq */
	if (!ret)
		UNLOCK_ATOM(atom);

	assert("nikita-2696", spin_atom_is_not_locked(atom));

	if (ret)
		return ret;

	if (nr_io_errors)
		return RETERR(-EIO);

	return 0;
}

/* change node->atom field for all jnode from given list */
static void
scan_fq_and_update_atom_ref(capture_list_head * list, txn_atom * atom)
{
	jnode *cur;

	for_all_type_safe_list(capture, list, cur) {
		LOCK_JNODE(cur);
		cur->atom = atom;
		UNLOCK_JNODE(cur);
	}
}

/* support for atom fusion operation */
reiser4_internal void
fuse_fq(txn_atom * to, txn_atom * from)
{
	flush_queue_t *fq;

	assert("zam-720", spin_atom_is_locked(to));
	assert("zam-721", spin_atom_is_locked(from));


	for_all_type_safe_list(fq, &from->flush_queues, fq) {
		scan_fq_and_update_atom_ref(ATOM_FQ_LIST(fq), to);
		spin_lock_fq(fq);
		fq->atom = to;
		spin_unlock_fq(fq);
	}

	fq_list_splice(&to->flush_queues, &from->flush_queues);

#if REISER4_DEBUG
	to->num_queued += from->num_queued;
	to->nr_flush_queues += from->nr_flush_queues;
	from->nr_flush_queues = 0;
#endif
}

#if REISER4_DEBUG
int atom_fq_parts_are_clean (txn_atom * atom)
{
	assert("zam-915", atom != NULL);
	return fq_list_empty(&atom->flush_queues);
}
#endif
/* Bio i/o completion routine for reiser4 write operations. */
static int
end_io_handler(struct bio *bio, unsigned int bytes_done UNUSED_ARG, int err UNUSED_ARG)
{
	int i;
	int nr_errors = 0;
	flush_queue_t *fq;

	assert ("zam-958", bio->bi_rw & WRITE);

	/* i/o op. is not fully completed */
	if (bio->bi_size != 0)
		return 1;

	/* we expect that bio->private is set to NULL or fq object which is used
	 * for synchronization and error counting. */
	fq = bio->bi_private;
	/* Check all elements of io_vec for correct write completion. */
	for (i = 0; i < bio->bi_vcnt; i += 1) {
		struct page *pg = bio->bi_io_vec[i].bv_page;

		if (!test_bit(BIO_UPTODATE, &bio->bi_flags)) {
			SetPageError(pg);
			nr_errors++;
		}

		{
			/* jnode WRITEBACK ("write is in progress bit") is
			 * atomically cleared here. */
			jnode *node;

			assert("zam-736", pg != NULL);
			assert("zam-736", PagePrivate(pg));
			node = (jnode *) (pg->private);

			JF_CLR(node, JNODE_WRITEBACK);
		}

		end_page_writeback(pg);
		page_cache_release(pg);
	}

	if (fq) {
		/* count i/o error in fq object */
		atomic_add(nr_errors, &fq->nr_errors);

		/* If all write requests registered in this "fq" are done we up
		 * the semaphore. */
		if (atomic_sub_and_test(bio->bi_vcnt, &fq->nr_submitted))
			up(&fq->io_sem);
	}

	bio_put(bio);
	return 0;
}

/* Count I/O requests which will be submitted by @bio in given flush queues
   @fq */
reiser4_internal void
add_fq_to_bio(flush_queue_t * fq, struct bio *bio)
{
	bio->bi_private = fq;
	bio->bi_end_io = end_io_handler;

	if (fq)
		atomic_add(bio->bi_vcnt, &fq->nr_submitted);
}

/* Move all queued nodes out from @fq->prepped list. */
static void release_prepped_list(flush_queue_t * fq)
{
	txn_atom * atom;

	assert ("zam-904", fq_in_use(fq));
	atom = UNDER_SPIN(fq, fq, atom_get_locked_by_fq(fq));

	while(!capture_list_empty(ATOM_FQ_LIST(fq))) {
		jnode * cur;

		cur = capture_list_front(ATOM_FQ_LIST(fq));
		capture_list_remove_clean(cur);

		count_dequeued_node(fq);
		LOCK_JNODE(cur);
		assert("nikita-3154", !JF_ISSET(cur, JNODE_OVRWR));
		assert("nikita-3154", JF_ISSET(cur, JNODE_RELOC));
		assert("nikita-3154", JF_ISSET(cur, JNODE_FLUSH_QUEUED));
		JF_CLR(cur, JNODE_FLUSH_QUEUED);

		if (JF_ISSET(cur, JNODE_DIRTY)) {
			capture_list_push_back(ATOM_DIRTY_LIST(atom, jnode_get_level(cur)), cur);
			ON_DEBUG(count_jnode(atom, cur, FQ_LIST, DIRTY_LIST, 1));
		} else {
			capture_list_push_back(ATOM_CLEAN_LIST(atom), cur);
			ON_DEBUG(count_jnode(atom, cur, FQ_LIST, CLEAN_LIST, 1));
		}

		UNLOCK_JNODE(cur);
	}

	if (-- atom->nr_running_queues == 0)
		atom_send_event(atom);

	UNLOCK_ATOM(atom);
}

/* Submit write requests for nodes on the already filled flush queue @fq.

   @fq: flush queue object which contains jnodes we can (and will) write.
   @return: number of submitted blocks (>=0) if success, otherwise -- an error
            code (<0). */
reiser4_internal int
write_fq(flush_queue_t * fq, long * nr_submitted, int flags)
{
	int ret;
	txn_atom * atom;

	while (1) {
		atom = UNDER_SPIN(fq, fq, atom_get_locked_by_fq(fq));
		assert ("zam-924", atom);
		/* do not write fq in parallel. */
		if (atom->nr_running_queues == 0 || !(flags & WRITEOUT_SINGLE_STREAM))
			break;
		atom_wait_event(atom);
	}

	atom->nr_running_queues ++;
	UNLOCK_ATOM(atom);

	ret = write_jnode_list(ATOM_FQ_LIST(fq), fq, nr_submitted, flags);
	release_prepped_list(fq);

	return ret;
}

/* Getting flush queue object for exclusive use by one thread. May require
   several iterations which is indicated by -E_REPEAT return code.

   This function does not contain code for obtaining an atom lock because an
   atom lock is obtained by different ways in different parts of reiser4,
   usually it is current atom, but we need a possibility for getting fq for the
   atom of given jnode. */
reiser4_internal int
fq_by_atom_gfp(txn_atom * atom, flush_queue_t ** new_fq, int gfp)
{
	flush_queue_t *fq;

	assert("zam-745", spin_atom_is_locked(atom));

	fq = fq_list_front(&atom->flush_queues);
	while (!fq_list_end(&atom->flush_queues, fq)) {
		spin_lock_fq(fq);

		if (fq_ready(fq)) {
			mark_fq_in_use(fq);
			assert("vs-1246", fq->owner == NULL);
			ON_DEBUG(fq->owner = current);
			spin_unlock_fq(fq);

			if (*new_fq)
				done_fq(*new_fq);

			*new_fq = fq;

			return 0;
		}

		spin_unlock_fq(fq);

		fq = fq_list_next(fq);
	}

	/* Use previously allocated fq object */
	if (*new_fq) {
		mark_fq_in_use(*new_fq);
		assert("vs-1248", (*new_fq)->owner == 0);
		ON_DEBUG((*new_fq)->owner = current);
		attach_fq(atom, *new_fq);

		return 0;
	}

	UNLOCK_ATOM(atom);

	*new_fq = create_fq(gfp);

	if (*new_fq == NULL)
		return RETERR(-ENOMEM);

	return RETERR(-E_REPEAT);
}

reiser4_internal int
fq_by_atom(txn_atom * atom, flush_queue_t ** new_fq)
{
	return fq_by_atom_gfp(atom, new_fq, GFP_KERNEL);
}

/* A wrapper around fq_by_atom for getting a flush queue object for current
 * atom, if success fq->atom remains locked. */
reiser4_internal flush_queue_t *
get_fq_for_current_atom(void)
{
	flush_queue_t *fq = NULL;
	txn_atom *atom;
	int ret;

	do {
		atom = get_current_atom_locked();
		ret = fq_by_atom(atom, &fq);
	} while (ret == -E_REPEAT);

	if (ret)
		return ERR_PTR(ret);
	return fq;
}

/* Releasing flush queue object after exclusive use */
reiser4_internal void
fq_put_nolock(flush_queue_t * fq)
{
	assert("zam-747", fq->atom != NULL);
	assert("zam-902", capture_list_empty(ATOM_FQ_LIST(fq)));
	mark_fq_ready(fq);
	assert("vs-1245", fq->owner == current);
	ON_DEBUG(fq->owner = NULL);
}

reiser4_internal void
fq_put(flush_queue_t * fq)
{
	txn_atom *atom;

	spin_lock_fq(fq);
	atom = atom_get_locked_by_fq(fq);

	assert("zam-746", atom != NULL);

	fq_put_nolock(fq);
	atom_send_event(atom);

	spin_unlock_fq(fq);
	UNLOCK_ATOM(atom);
}

/* A part of atom object initialization related to the embedded flush queue
   list head */

reiser4_internal void
init_atom_fq_parts(txn_atom * atom)
{
	fq_list_init(&atom->flush_queues);
}

/* get a flush queue for an atom pointed by given jnode (spin-locked) ; returns
 * both atom and jnode locked and found and took exclusive access for flush
 * queue object.  */
reiser4_internal int fq_by_jnode_gfp(jnode * node, flush_queue_t ** fq, int gfp)
{
	txn_atom * atom;
	int ret;

	assert("zam-835", spin_jnode_is_locked(node));

	*fq = NULL;

	while (1) {
		/* begin with taking lock on atom */
		atom = jnode_get_atom(node);
		UNLOCK_JNODE(node);

		if (atom == NULL) {
			/* jnode does not point to the atom anymore, it is
			 * possible because jnode lock could be removed for a
			 * time in atom_get_locked_by_jnode() */
			if (*fq) {
				done_fq(*fq);
				*fq = NULL;
			}
			return 0;
		}

		/* atom lock is required for taking flush queue */
		ret = fq_by_atom_gfp(atom, fq, gfp);

		if (ret) {
			if (ret == -E_REPEAT)
				/* atom lock was released for doing memory
				 * allocation, start with locked jnode one more
				 * time */
				goto lock_again;
			return ret;
 		}

		/* It is correct to lock atom first, then lock a jnode */
		LOCK_JNODE(node);

		if (node->atom == atom)
			break;	/* Yes! it is our jnode. We got all of them:
				 * flush queue, and both locked atom and
				 * jnode */

		/* release all locks and allocated objects and restart from
		 * locked jnode. */
		UNLOCK_JNODE(node);

		fq_put(*fq);
		fq = NULL;

		UNLOCK_ATOM(atom);

	lock_again:
		LOCK_JNODE(node);
	}

	return 0;
}

reiser4_internal int fq_by_jnode(jnode * node, flush_queue_t ** fq)
{
        return fq_by_jnode_gfp(node, fq, GFP_KERNEL);
}


#if REISER4_DEBUG

void check_fq(const txn_atom *atom)
{
	/* check number of nodes on all atom's flush queues */
	flush_queue_t *fq;
	int count;
	jnode *node;

	count = 0;
	for_all_type_safe_list(fq, &atom->flush_queues, fq) {
		spin_lock_fq(fq);
		for_all_type_safe_list(capture, ATOM_FQ_LIST(fq), node)
			count ++;
		spin_unlock_fq(fq);
	}
	if (count != atom->fq)
		warning("", "fq counter %d, real %d\n", atom->fq, count);

}

#endif

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 80
   scroll-step: 1
   End:
*/
