/* Copyright 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

/* This file exists only until VM gets fixed to reserve pages properly, which
 * might or might not be very political. */

/* Implementation of emergency flush. */

/* OVERVIEW:

     Before writing a node to the disk, some complex process (flush.[ch]) is
     to be performed. Flush is the main necessary preliminary step before
     writing pages back to the disk, but it has some characteristics that make
     it completely different from traditional ->writepage():

        1 It operates on a large number of nodes, possibly far away from the
        starting node, both in tree and disk order.

        2 it can involve reading of nodes from the disk (during extent
        allocation, for example).

        3 it can allocate memory (during insertion of allocated extents).

        4 it participates in the locking protocol which reiser4 uses to
        implement concurrent tree modifications.

        5 it is CPU consuming and long

     As a result, flush reorganizes some part of reiser4 tree and produces
     large queue of nodes ready to be submitted for io.

     Items (3) and (4) alone make flush unsuitable for being called directly
     from reiser4 ->writepage() callback, because of OOM and deadlocks
     against threads waiting for memory.

     So, flush is performed from within balance_dirty_page() path when dirty
     pages are generated. If balance_dirty_page() fails to throttle writers
     and page replacement finds dirty page on the inactive list, we resort to
     "emergency flush" in our ->vm_writeback().

     Emergency flush is relatively dumb algorithm, implemented in this file,
     that tries to write tree nodes to the disk without taking locks and without
     thoroughly optimizing tree layout. We only want to call emergency flush in
     desperate situations, because it is going to produce sub-optimal disk
     layouts.

  DETAILED DESCRIPTION

     Emergency flush (eflush) is designed to work as low level mechanism with
     no or little impact on the rest of (already too complex) code.

     eflush is initiated from ->writepage() method called by VM on memory
     pressure. It is supposed that ->writepage() is rare call path, because
     balance_dirty_pages() throttles writes and tries to keep memory in
     balance.

     eflush main entry point (emergency_flush()) checks whether jnode is
     eligible for emergency flushing. Check is performed by flushable()
     function which see for details. After successful check, new block number
     ("emergency block") is allocated and io is initiated to write jnode
     content to that block.

     After io is finished, jnode will be cleaned and VM will be able to free
     page through call to ->releasepage().

     emergency_flush() also contains special case invoked when it is possible
     to avoid allocation of new node.

     Node selected for eflush is marked (by JNODE_EFLUSH bit in ->flags field)
     and added to the special hash table of all eflushed nodes. This table
     doesn't have linkage within each jnode, as this would waste memory in
     assumption that eflush is rare. In stead new small memory object
     (eflush_node_t) is allocated that contains pointer to jnode, emergency
     block number, and is inserted into hash table. Per super block counter of
     eflushed nodes is incremented. See section [INODE HANDLING] below for
     more on this.

     It should be noted that emergency flush may allocate memory and wait for
     io completion (bitmap read).

     Basically eflushed node has following distinctive characteristics:

          (1) JNODE_EFLUSH bit is set

          (2) no page

          (3) there is an element in hash table, for this node

          (4) node content is stored on disk in block whose number is stored
          in the hash table element

  UNFLUSH

      Unflush is reverse of eflush, that is process bringing page of eflushed
      inode back into memory.

      In accordance with the policy that eflush is low level and low impact
      mechanism, transparent to the rest of the code, unflushing is performed
      deeply within jload_gfp() which is main function used to load and pin
      jnode page into memory.

      Specifically, if jload_gfp() determines that it is called on eflushed
      node it gets emergency block number to start io against from the hash
      table rather than from jnode itself. This is done in
      jnode_get_io_block() function. After io completes, hash table element
      for this node is removed and JNODE_EFLUSH bit is cleared.

  LOCKING

      The page lock is used to avoid eflush/e-unflush/jnode_get_io_block races.
      emergency_flush() and jnode_get_io_block are called under the page lock.
      The eflush_del() function (emergency unflush) may be called for a node w/o
      page attached.  In that case eflush_del() allocates a page and locks it.

  PROBLEMS

  1. INODE HANDLING

      Usually (i.e., without eflush), jnode has a page attached to it. This
      page pins corresponding struct address_space, and, hence, inode in
      memory. Once inode has been eflushed, its page is gone and inode can be
      wiped out of memory by the memory pressure (prune_icache()). This leads
      to the number of complications:

           (1) jload_gfp() has to attach jnode tho the address space's radix
           tree. This requires existence if inode.

           (2) normal flush needs jnode's inode to start slum collection from
           unformatted jnode.

      (1) is really a problem, because it is too late to load inode (which
      would lead to loading of stat data, etc.) within jload_gfp().

      We, therefore, need some way to protect inode from being recycled while
      having accessible eflushed nodes.

      I'll describe old solution here so it can be compared with new one.

      Original solution pinned inode by __iget() when first its node was
      eflushed and released (through iput()) when last was unflushed. This
      required maintenance of inode->eflushed counter in inode.

      Problem arise if last name of inode is unlinked when it has eflushed
      nodes. In this case, last iput() that leads to the removal of file is
      iput() made by unflushing from within jload_gfp(). Obviously, calling
      truncate, and tree traversals from jload_gfp() is not a good idea.

      New solution is to pin inode in memory by adding I_EFLUSH bit to its
      ->i_state field. This protects inode from being evicted by
      prune_icache().

  DISK SPACE ALLOCATION

      This section will describe how emergency block is allocated and how
      block counters (allocated, grabbed, etc.) are manipulated. To be done.

   *****HISTORICAL SECTION****************************************************

   DELAYED PARENT UPDATE

     Important point of emergency flush is that update of parent is sometimes
     delayed: we don't update parent immediately if:

      1 Child was just allocated, but parent is locked. Waiting for parent
      lock in emergency flush is impossible (deadlockable).

      2 Part of extent was allocated, but parent has not enough space to
      insert allocated extent unit. Balancing in emergency flush is
      impossible, because it will possibly wait on locks.

     When we delay update of parent node, we mark it as such (and possibly
     also mark children to simplify delayed update later). Question: when
     parent should be really updated?

   WHERE TO WRITE PAGE INTO?


     So, it was decided that flush has to be performed from a separate
     thread. Reiser4 has a thread used to periodically commit old transactions,
     and this thread can be used for the flushing. That is, flushing thread
     does flush and accumulates nodes prepared for the IO on the special
     queue. reiser4_vm_writeback() submits nodes from this queue, if queue is
     empty, it only wakes up flushing thread and immediately returns.

     Still there are some problems with integrating this stuff into VM
     scanning:

        1 As ->vm_writeback() returns immediately without actually submitting
        pages for IO, throttling on PG_writeback in shrink_list() will not
        work. This opens a possibility (on a fast CPU), of try_to_free_pages()
        completing scanning and calling out_of_memory() before flushing thread
        managed to add anything to the queue.

        2 It is possible, however unlikely, that flushing thread will be
        unable to flush anything, because there is not enough memory. In this
        case reiser4 resorts to the "emergency flush": some dumb algorithm,
        implemented in this file, that tries to write tree nodes to the disk
        without taking locks and without thoroughly optimizing tree layout. We
        only want to call emergency flush in desperate situations, because it
        is going to produce sub-optimal disk layouts.

        3 Nodes prepared for IO can be from the active list, this means that
        they will not be met/freed by shrink_list() after IO completion. New
        blk_congestion_wait() should help with throttling but not
        freeing. This is not fatal though, because inactive list refilling
        will ultimately get to these pages and reclaim them.

   REQUIREMENTS

     To make this work we need at least some hook inside VM scanning which
     gets triggered after scanning (or scanning with particular priority)
     failed to free pages. This is already present in the
     mm/vmscan.c:set_shrinker() interface.

     Another useful thing that we would like to have is passing scanning
     priority down to the ->vm_writeback() that will allow file system to
     switch to the emergency flush more gracefully.

   POSSIBLE ALGORITHMS

     1 Start emergency flush from ->vm_writeback after reaching some priority.
     This allows to implement simple page based algorithm: look at the page VM
     supplied us with and decide what to do.

     2 Start emergency flush from shrinker after reaching some priority.
     This delays emergency flush as far as possible.

   *****END OF HISTORICAL SECTION**********************************************

*/

#include "forward.h"
#include "debug.h"
#include "page_cache.h"
#include "tree.h"
#include "jnode.h"
#include "znode.h"
#include "inode.h"
#include "super.h"
#include "block_alloc.h"
#include "emergency_flush.h"

#include <linux/mm.h>
#include <linux/writeback.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/swap.h>

#if REISER4_USE_EFLUSH

static int flushable(const jnode * node, struct page *page, int);
static int needs_allocation(const jnode * node);
static eflush_node_t *ef_alloc(int flags);
static reiser4_ba_flags_t ef_block_flags(const jnode *node);
static int ef_free_block(jnode *node, const reiser4_block_nr *blk, block_stage_t stage, eflush_node_t *ef);
static int ef_prepare(jnode *node, reiser4_block_nr *blk, eflush_node_t **enode, reiser4_blocknr_hint *hint);
static int eflush_add(jnode *node, reiser4_block_nr *blocknr, eflush_node_t *ef);

/* slab for eflush_node_t's */
static kmem_cache_t *eflush_slab;

#define EFLUSH_START_BLOCK ((reiser4_block_nr)0)

#define INC_STAT(node, counter)						\
	reiser4_stat_inc_at_level(jnode_get_level(node), counter);

/* this function exists only until VM gets fixed to reserve pages properly,
 * which might or might not be very political. */
/* try to flush @page to the disk
 *
 * Return 0 if page was successfully paged out. 1 if it is busy, error
 * otherwise.
 */
reiser4_internal int
emergency_flush(struct page *page)
{
	struct super_block *sb;
	jnode *node;
	int result;
	assert("nikita-2721", page != NULL);
	assert("nikita-2724", PageLocked(page));

	// warning("nikita-3112", "Emergency flush. Notify Reiser@Namesys.COM");

	/*
	 * Page is locked, hence page<->jnode mapping cannot change.
	 */

	sb = page->mapping->host->i_sb;
	node = jprivate(page);

	assert("vs-1452", node != NULL);

	jref(node);
	INC_STAT(node, vm.eflush.called);

	result = 0;
	LOCK_JNODE(node);
	/*
	 * page was dirty and under eflush. This is (only?) possible if page
	 * was re-dirtied through mmap(2) after eflush IO was submitted, but
	 * before ->releasepage() freed page.
	 */
	eflush_del(node, 1);

	LOCK_JLOAD(node);
	if (flushable(node, page, 1)) {
		if (needs_allocation(node)) {
			reiser4_block_nr blk;
			eflush_node_t *efnode;
			reiser4_blocknr_hint hint;

			blk = 0ull;
			efnode = NULL;

			/* Set JNODE_EFLUSH bit _before_ allocating a block,
			 * that prevents flush reserved block from using here
			 * and by a reiser4 flush process  */
			JF_SET(node, JNODE_EFLUSH);

			blocknr_hint_init(&hint);

			INC_STAT(node, vm.eflush.needs_block);
			result = ef_prepare(node, &blk, &efnode, &hint);
			if (flushable(node, page, 0) && result == 0) {
				assert("nikita-2759", efnode != NULL);
				eflush_add(node, &blk, efnode);

				result = page_io(page, node, WRITE,
						 GFP_NOFS | __GFP_HIGH);
				INC_STAT(node, vm.eflush.ok);
			} else {
				JF_CLR(node, JNODE_EFLUSH);
				UNLOCK_JLOAD(node);
				UNLOCK_JNODE(node);
				if (blk != 0ull) {
					ef_free_block(node, &blk,
						      hint.block_stage, efnode);
					kmem_cache_free(eflush_slab, efnode);
				}
				ON_TRACE(TRACE_EFLUSH, "failure-2\n");
				result = 1;
				INC_STAT(node, vm.eflush.nolonger);
			}

			blocknr_hint_done(&hint);
		} else {
			txn_atom *atom;
			flush_queue_t *fq;

			/* eflush without allocation temporary location for a node */
			ON_TRACE(TRACE_EFLUSH, "flushing to relocate place: %llu..", *jnode_get_block(node));

			/* get flush queue for this node */
			result = fq_by_jnode_gfp(node, &fq, GFP_ATOMIC);

			if (result)
				return result;

			atom = node->atom;

			if (!flushable(node, page, 1) || needs_allocation(node) || !jnode_is_dirty(node)) {
				ON_TRACE(TRACE_EFLUSH, "failure-3\n");
				UNLOCK_JLOAD(node);
				UNLOCK_JNODE(node);
				UNLOCK_ATOM(atom);
				fq_put(fq);
				return 1;
			}

			/* ok, now we can flush it */
			unlock_page(page);

			queue_jnode(fq, node);

			UNLOCK_JLOAD(node);
			UNLOCK_JNODE(node);
			UNLOCK_ATOM(atom);

			result = write_fq(fq, NULL, 0);
			if (result != 0)
				lock_page(page);

			ON_TRACE(TRACE_EFLUSH, "flushed %d blocks\n", result);
			/* Even if we wrote nothing, We unlocked the page, so let know to the caller that page should
			   not be unlocked again */
			fq_put(fq);
		}

	} else {
		UNLOCK_JLOAD(node);
		UNLOCK_JNODE(node);
		ON_TRACE(TRACE_EFLUSH, "failure-1\n");
		result = 1;
	}

	jput(node);
	return result;
}

static int
flushable(const jnode * node, struct page *page, int check_eflush)
{
	assert("nikita-2725", node != NULL);
	assert("nikita-2726", spin_jnode_is_locked(node));
	assert("nikita-3388", spin_jload_is_locked(node));

	if (jnode_is_loaded(node)) {             /* loaded */
		INC_STAT(node, vm.eflush.loaded);
		return 0;
	}
	if (JF_ISSET(node, JNODE_FLUSH_QUEUED)) { /* already pending io */
		INC_STAT(node, vm.eflush.queued);
		return 0;
	}
	if (JF_ISSET(node, JNODE_EPROTECTED)) {  /* protected from e-flush */
		INC_STAT(node, vm.eflush.protected);
		return 0;
	}
	if (JF_ISSET(node, JNODE_HEARD_BANSHEE)) {
		INC_STAT(node, vm.eflush.heard_banshee);
		return 0;
	}
	if (page == NULL) {           		/* nothing to flush */
		INC_STAT(node, vm.eflush.nopage);
		return 0;
	}
	if (PageWriteback(page)) {               /* already under io */
		INC_STAT(node, vm.eflush.writeback);
		return 0;
	}
	/* don't flush bitmaps or journal records */
	if (!jnode_is_znode(node) && !jnode_is_unformatted(node)) {
		INC_STAT(node, vm.eflush.bitmap);
		return 0;
	}
	/* don't flush cluster pages */
	if (jnode_is_cluster_page(node)) {
		INC_STAT(node, vm.eflush.clustered);
		return 0;
	}
	if (check_eflush && JF_ISSET(node, JNODE_EFLUSH)) {      /* already flushed */
		INC_STAT(node, vm.eflush.eflushed);
		return 0;
	}
	return 1;
}

#undef INC_STAT

/* does node need allocation for eflushing? */
static int
needs_allocation(const jnode * node)
{
	return !(JF_ISSET(node, JNODE_RELOC) && !blocknr_is_fake(jnode_get_block(node)));
}


static inline int
jnode_eq(jnode * const * j1, jnode * const * j2)
{
	assert("nikita-2733", j1 != NULL);
	assert("nikita-2734", j2 != NULL);

	return *j1 == *j2;
}

static ef_hash_table *
get_jnode_enhash(const jnode *node)
{
	struct super_block *super;

	assert("nikita-2739", node != NULL);

	super = jnode_get_tree(node)->super;
	return &get_super_private(super)->efhash_table;
}

static inline __u32
jnode_hfn(ef_hash_table *table, jnode * const * j)
{
	__u32 val;

	assert("nikita-2735", j != NULL);
	assert("nikita-3346", IS_POW(table->_buckets));

	val = (unsigned long)*j;
	val /= sizeof(**j);
	return val & (table->_buckets - 1);
}


/* The hash table definition */
#define KMALLOC(size) vmalloc(size)
#define KFREE(ptr, size) vfree(ptr)
TYPE_SAFE_HASH_DEFINE(ef, eflush_node_t, jnode *, node, linkage, jnode_hfn, jnode_eq);
#undef KFREE
#undef KMALLOC

reiser4_internal int
eflush_init(void)
{
	eflush_slab = kmem_cache_create("eflush", sizeof (eflush_node_t),
					0, SLAB_HWCACHE_ALIGN, NULL, NULL);
	if (eflush_slab == NULL)
		return RETERR(-ENOMEM);
	else
		return 0;
}

reiser4_internal int
eflush_done(void)
{
	return kmem_cache_destroy(eflush_slab);
}

reiser4_internal int
eflush_init_at(struct super_block *super)
{
	return ef_hash_init(&get_super_private(super)->efhash_table,
			    8192,
			    reiser4_stat(super, hashes.eflush));
}

reiser4_internal void
eflush_done_at(struct super_block *super)
{
	ef_hash_done(&get_super_private(super)->efhash_table);
}

static eflush_node_t *
ef_alloc(int flags)
{
	return kmem_cache_alloc(eflush_slab, flags);
}

#define EFLUSH_MAGIC 4335203

static int
eflush_add(jnode *node, reiser4_block_nr *blocknr, eflush_node_t *ef)
{
	reiser4_tree  *tree;

	assert("nikita-2737", node != NULL);
	assert("nikita-2738", JF_ISSET(node, JNODE_EFLUSH));
	assert("nikita-3382", !JF_ISSET(node, JNODE_EPROTECTED));
	assert("nikita-2765", spin_jnode_is_locked(node));
	assert("nikita-3381", spin_jload_is_locked(node));

	tree = jnode_get_tree(node);

	ef->node = node;
	ef->blocknr = *blocknr;
	ef->hadatom = (node->atom != NULL);
	ef->incatom = 0;
	jref(node);
	spin_lock_eflush(tree->super);
	ef_hash_insert(get_jnode_enhash(node), ef);
	ON_DEBUG(++ get_super_private(tree->super)->eflushed);
	spin_unlock_eflush(tree->super);

	if (jnode_is_unformatted(node)) {
		struct inode  *inode;
		reiser4_inode *info;

		WLOCK_TREE(tree);

		inode = mapping_jnode(node)->host;
		info = reiser4_inode_data(inode);

		if (!ef->hadatom) {
			radix_tree_tag_set(jnode_tree_by_reiser4_inode(info),
					   index_jnode(node), EFLUSH_TAG_ANONYMOUS);
			ON_DEBUG(info->anonymous_eflushed ++);
		} else {
			radix_tree_tag_set(jnode_tree_by_reiser4_inode(info),
					   index_jnode(node), EFLUSH_TAG_CAPTURED);
			ON_DEBUG(info->captured_eflushed ++);
		}
		WUNLOCK_TREE(tree);
		/*XXXX*/
		inc_unfm_ef();
	}

	/* FIXME: do we need it here, if eflush add/del are protected by page lock? */
	UNLOCK_JLOAD(node);

	/*
	 * jnode_get_atom() can possible release jnode spin lock. This
	 * means it can only be called _after_ JNODE_EFLUSH is set, because
	 * otherwise we would have to re-check flushable() once more. No
	 * thanks.
	 */

	if (ef->hadatom) {
		txn_atom *atom;

		atom = jnode_get_atom(node);
		if (atom != NULL) {
			++ atom->flushed;
			ef->incatom = 1;
			UNLOCK_ATOM(atom);
		}
	}

	UNLOCK_JNODE(node);
	return 0;
}

/* Arrghh... cast to keep hash table code happy. */
#define C(node) ((jnode *const *)&(node))

reiser4_internal reiser4_block_nr *
eflush_get(const jnode *node)
{
	eflush_node_t *ef;
	reiser4_tree  *tree;

	assert("nikita-2740", node != NULL);
	assert("nikita-2741", JF_ISSET(node, JNODE_EFLUSH));
	assert("nikita-2767", spin_jnode_is_locked(node));


	tree = jnode_get_tree(node);
	spin_lock_eflush(tree->super);
	ef = ef_hash_find(get_jnode_enhash(node), C(node));
	spin_unlock_eflush(tree->super);

	assert("nikita-2742", ef != NULL);
	return &ef->blocknr;
}

/* free resources taken for emergency flushing of the node */
static void eflush_free (jnode * node)
{
	eflush_node_t *ef;
	ef_hash_table *table;
	reiser4_tree  *tree;
	txn_atom      *atom;
	struct inode  *inode = NULL;
	reiser4_block_nr blk;

	assert ("zam-1026", spin_jnode_is_locked(node));

	table = get_jnode_enhash(node);
	tree = jnode_get_tree(node);

	spin_lock_eflush(tree->super);
	ef = ef_hash_find(table, C(node));
	BUG_ON(ef == NULL);
	assert("nikita-2745", ef != NULL);
	blk = ef->blocknr;
	ef_hash_remove(table, ef);
	ON_DEBUG(-- get_super_private(tree->super)->eflushed);
	spin_unlock_eflush(tree->super);

	if (ef->incatom) {
		atom = jnode_get_atom(node);
		assert("nikita-3311", atom != NULL);
		-- atom->flushed;
		UNLOCK_ATOM(atom);
	}

	assert("vs-1215", JF_ISSET(node, JNODE_EFLUSH));

	if (jnode_is_unformatted(node)) {
		reiser4_inode *info;

		WLOCK_TREE(tree);

		inode = mapping_jnode(node)->host;
		info = reiser4_inode_data(inode);

		/* clear e-flush specific tags from node's radix tree slot */
		radix_tree_tag_clear(
			jnode_tree_by_reiser4_inode(info), index_jnode(node),
			ef->hadatom ? EFLUSH_TAG_CAPTURED : EFLUSH_TAG_ANONYMOUS);
		ON_DEBUG(ef->hadatom ? (info->captured_eflushed --) : (info->anonymous_eflushed --));

		assert("nikita-3355", ergo(jnode_tree_by_reiser4_inode(info)->rnode == NULL,
					   (info->captured_eflushed == 0 && info->anonymous_eflushed == 0)));

		WUNLOCK_TREE(tree);

		/*XXXX*/
		dec_unfm_ef();

	}
	UNLOCK_JNODE(node);

#if REISER4_DEBUG
	if (blocknr_is_fake(jnode_get_block(node)))
		assert ("zam-817", ef->initial_stage == BLOCK_UNALLOCATED);
	else
		assert ("zam-818", ef->initial_stage == BLOCK_GRABBED);
#endif

	jput(node);

	ef_free_block(node, &blk,
		      blocknr_is_fake(jnode_get_block(node)) ?
		      BLOCK_UNALLOCATED : BLOCK_GRABBED, ef);

	kmem_cache_free(eflush_slab, ef);

	LOCK_JNODE(node);
}

reiser4_internal void eflush_del (jnode * node, int page_locked)
{
        struct page * page;

        assert("nikita-2743", node != NULL);
        assert("nikita-2770", spin_jnode_is_locked(node));

        if (!JF_ISSET(node, JNODE_EFLUSH))
                return;

        if (page_locked) {
                page = jnode_page(node);
                assert("nikita-2806", page != NULL);
                assert("nikita-2807", PageLocked(page));
        } else {
                UNLOCK_JNODE(node);
                page = jnode_get_page_locked(node, GFP_NOFS);
                LOCK_JNODE(node);
                if (page == NULL) {
                        warning ("zam-1025", "eflush_del failed to get page back\n");
                        return;
                }
                if (unlikely(!JF_ISSET(node, JNODE_EFLUSH)))
                        /* race: some other thread unflushed jnode. */
                        goto out;
        }

        if (PageWriteback(page)) {
                UNLOCK_JNODE(node);
                page_cache_get(page);
                reiser4_wait_page_writeback(page);
                page_cache_release(page);
                LOCK_JNODE(node);
                if (unlikely(!JF_ISSET(node, JNODE_EFLUSH)))
                        /* race: some other thread unflushed jnode. */
                        goto out;
        }

	if (JF_ISSET(node, JNODE_KEEPME))
		set_page_dirty(page);
	else
		/*
		 * either jnode was dirty or page was dirtied through mmap. Page's dirty
		 * bit was cleared before io was submitted. If page is left clean, we
		 * would have dirty jnode with clean page. Neither ->writepage() nor
		 * ->releasepage() can free it. Re-dirty page, so ->writepage() will be
		 * called again if necessary.
		 */
		set_page_dirty_internal(page, 0);

        assert("nikita-2766", atomic_read(&node->x_count) > 1);
        /* release allocated disk block and in-memory structures  */
        eflush_free(node);
        JF_CLR(node, JNODE_EFLUSH);
 out:
        if (!page_locked)
                unlock_page(page);
}

reiser4_internal int
emergency_unflush(jnode *node)
{
	int result;

	assert("nikita-2778", node != NULL);
	assert("nikita-3046", schedulable());

	if (JF_ISSET(node, JNODE_EFLUSH)) {
		result = jload(node);
		if (result == 0) {
			struct page *page;

			assert("nikita-2777", !JF_ISSET(node, JNODE_EFLUSH));
			page = jnode_page(node);
			assert("nikita-2779", page != NULL);
			wait_on_page_writeback(page);

			jrelse(node);
		}
	} else
		result = 0;
	return result;
}

static reiser4_ba_flags_t
ef_block_flags(const jnode *node)
{
	return jnode_is_znode(node) ? BA_FORMATTED : 0;
}

static int ef_free_block(jnode *node,
			 const reiser4_block_nr *blk,
			 block_stage_t stage, eflush_node_t *ef)
{
	int result = 0;

	/* We cannot just ask block allocator to return block into flush
	 * reserved space, because there is no current atom at this point. */
	result = reiser4_dealloc_block(blk, stage, ef_block_flags(node));
	if (result == 0 && stage == BLOCK_GRABBED) {
		txn_atom *atom;

		if (ef->reserve) {
			/* further, transfer block from grabbed into flush
			 * reserved space. */
			LOCK_JNODE(node);
			atom = jnode_get_atom(node);
			assert("nikita-2785", atom != NULL);
			grabbed2flush_reserved_nolock(atom, 1);
			UNLOCK_ATOM(atom);
			JF_SET(node, JNODE_FLUSH_RESERVED);
			UNLOCK_JNODE(node);
		} else {
			reiser4_context * ctx = get_current_context();
			grabbed2free(ctx, get_super_private(ctx->super),
				     (__u64)1);
		}
	}
	return result;
}

static int
ef_prepare(jnode *node, reiser4_block_nr *blk, eflush_node_t **efnode, reiser4_blocknr_hint * hint)
{
	int result;
	int usedreserve;

	assert("nikita-2760", node != NULL);
	assert("nikita-2761", blk != NULL);
	assert("nikita-2762", efnode != NULL);
	assert("nikita-2763", spin_jnode_is_locked(node));
	assert("nikita-3387", spin_jload_is_locked(node));

	hint->blk         = EFLUSH_START_BLOCK;
	hint->max_dist    = 0;
	hint->level       = jnode_get_level(node);
	usedreserve = 0;
	if (blocknr_is_fake(jnode_get_block(node)))
		hint->block_stage = BLOCK_UNALLOCATED;
	else {
		txn_atom *atom;
		switch (jnode_is_leaf(node)) {
		default:
			/* We cannot just ask block allocator to take block from
			 * flush reserved space, because there is no current
			 * atom at this point. */
			atom = jnode_get_atom(node);
			if (atom != NULL) {
				if (JF_ISSET(node, JNODE_FLUSH_RESERVED)) {
					usedreserve = 1;
					flush_reserved2grabbed(atom, 1);
					JF_CLR(node, JNODE_FLUSH_RESERVED);
					UNLOCK_ATOM(atom);
					break;
				} else
					UNLOCK_ATOM(atom);
			}
			/* fall through */
			/* node->atom == NULL if page was dirtied through
			 * mmap */
		case 0:
			result = reiser4_grab_space_force((__u64)1, BA_RESERVED);
			grab_space_enable();
			if (result) {
				warning("nikita-3323",
					"Cannot allocate eflush block");
				return result;
			}
		}

		hint->block_stage = BLOCK_GRABBED;
	}

	/* XXX protect @node from being concurrently eflushed. Otherwise,
	 * there is a danger of underflowing block space */
	UNLOCK_JLOAD(node);
	UNLOCK_JNODE(node);

	*efnode = ef_alloc(GFP_NOFS | __GFP_HIGH);
	if (*efnode == NULL) {
		result = RETERR(-ENOMEM);
		goto out;
	}

#if REISER4_DEBUG
	(*efnode)->initial_stage = hint->block_stage;
#endif
	(*efnode)->reserve = usedreserve;

	result = reiser4_alloc_block(hint, blk, ef_block_flags(node));
	if (result)
		kmem_cache_free(eflush_slab, *efnode);
 out:
	LOCK_JNODE(node);
	LOCK_JLOAD(node);
	return result;
}

#endif /* REISER4_USE_EFLUSH */

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 80
   LocalWords: " unflush eflushed LocalWords eflush writepage VM releasepage unflushing io "
   End:
*/
