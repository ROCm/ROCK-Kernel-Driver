/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

#include "debug.h"
#include "dformat.h"
#include "plugin/plugin.h"
#include "txnmgr.h"
#include "znode.h"
#include "block_alloc.h"
#include "tree.h"
#include "super.h"
#include "lib.h"

#include <linux/types.h>	/* for __u??  */
#include <linux/fs.h>		/* for struct super_block  */
#include <linux/spinlock.h>

/* THE REISER4 DISK SPACE RESERVATION SCHEME. */

/* We need to be able to reserve enough disk space to ensure that an atomic
   operation will have enough disk space to flush (see flush.c and
   http://namesys.com/v4/v4.html) and commit it once it is started.

   In our design a call for reserving disk space may fail but not an actual
   block allocation.

   All free blocks, already allocated blocks, and all kinds of reserved blocks
   are counted in different per-fs block counters.

   A reiser4 super block's set of block counters currently is:

   free -- free blocks,
   used -- already allocated blocks,

   grabbed -- initially reserved for performing an fs operation, those blocks
          are taken from free blocks, then grabbed disk space leaks from grabbed
          blocks counter to other counters like "fake allocated", "flush
          reserved", "used", the rest of not used grabbed space is returned to
          free space at the end of fs operation;

   fake allocated -- counts all nodes without real disk block numbers assigned,
                     we have separate accounting for formatted and unformatted
                     nodes (for easier debugging);

   flush reserved -- disk space needed for flushing and committing an atom.
                     Each dirty already allocated block could be written as a
                     part of atom's overwrite set or as a part of atom's
                     relocate set.  In both case one additional block is needed,
                     it is used as a wandered block if we do overwrite or as a
		     new location for a relocated block.

   In addition, blocks in some states are counted on per-thread and per-atom
   basis.  A reiser4 context has a counter of blocks grabbed by this transaction
   and the sb's grabbed blocks counter is a sum of grabbed blocks counter values
   of each reiser4 context.  Each reiser4 atom has a counter of "flush reserved"
   blocks, which are reserved for flush processing and atom commit. */

/* AN EXAMPLE: suppose we insert new item to the reiser4 tree.  We estimate
   number of blocks to grab for most expensive case of balancing when the leaf
   node we insert new item to gets split and new leaf node is allocated.

   So, we need to grab blocks for

   1) one block for possible dirtying the node we insert an item to. That block
      would be used for node relocation at flush time or for allocating of a
      wandered one, it depends what will be a result (what set, relocate or
      overwrite the node gets assigned to) of the node processing by the flush
      algorithm.

   2) one block for either allocating a new node, or dirtying of right or left
      clean neighbor, only one case may happen.

   VS-FIXME-HANS: why can only one case happen? I would expect to see dirtying of left neighbor, right neighbor, current
   node, and creation of new node.  have I forgotten something?  email me.

   These grabbed blocks are counted in both reiser4 context "grabbed blocks"
   counter and in the fs-wide one (both ctx->grabbed_blocks and
   sbinfo->blocks_grabbed get incremented by 2), sb's free blocks counter is
   decremented by 2.

   Suppose both two blocks were spent for dirtying of an already allocated clean
   node (one block went from "grabbed" to "flush reserved") and for new block
   allocating (one block went from "grabbed" to "fake allocated formatted").

   Inserting of a child pointer to the parent node caused parent node to be
   split, the balancing code takes care about this grabbing necessary space
   immediately by calling reiser4_grab with BA_RESERVED flag set which means
   "can use the 5% reserved disk space".

   At this moment insertion completes and grabbed blocks (if they were not used)
   should be returned to the free space counter.

   However the atom life-cycle is not completed.  The atom had one "flush
   reserved" block added by our insertion and the new fake allocated node is
   counted as a "fake allocated formatted" one.  The atom has to be fully
   processed by flush before commit.  Suppose that the flush moved the first,
   already allocated node to the atom's overwrite list, the new fake allocated
   node, obviously, went into the atom relocate set.  The reiser4 flush
   allocates the new node using one unit from "fake allocated formatted"
   counter, the log writer uses one from "flush reserved" for wandered block
   allocation.

   And, it is not the end.  When the wandered block is deallocated after the
   atom gets fully played (see wander.c for term description), the disk space
   occupied for it is returned to free blocks. */

/* BLOCK NUMBERS */

/* Any reiser4 node has a block number assigned to it.  We use these numbers for
   indexing in hash tables, so if a block has not yet been assigned a location
   on disk we need to give it a temporary fake block number.

   Current implementation of reiser4 uses 64-bit integers for block numbers. We
   use highest bit in 64-bit block number to distinguish fake and real block
   numbers. So, only 63 bits may be used to addressing of real device
   blocks. That "fake" block numbers space is divided into subspaces of fake
   block numbers for data blocks and for shadow (working) bitmap blocks.

   Fake block numbers for data blocks are generated by a cyclic counter, which
   gets incremented after each real block allocation. We assume that it is
   impossible to overload this counter during one transaction life. */

/* Initialize a blocknr hint. */
reiser4_internal void
blocknr_hint_init(reiser4_blocknr_hint * hint)
{
	xmemset(hint, 0, sizeof (reiser4_blocknr_hint));
}

/* Release any resources of a blocknr hint. */
reiser4_internal void
blocknr_hint_done(reiser4_blocknr_hint * hint UNUSED_ARG)
{
	/* No resources should be freed in current blocknr_hint implementation.*/
}

/* see above for explanation of fake block number.  */
/* Audited by: green(2002.06.11) */
reiser4_internal int
blocknr_is_fake(const reiser4_block_nr * da)
{
	/* The reason for not simply returning result of '&' operation is that
	   while return value is (possibly 32bit) int,  the reiser4_block_nr is
	   at least 64 bits long, and high bit (which is the only possible
	   non zero bit after the masking) would be stripped off */
	return (*da & REISER4_FAKE_BLOCKNR_BIT_MASK) ? 1 : 0;
}

/* Static functions for <reiser4 super block>/<reiser4 context> block counters
   arithmetic. Mostly, they are isolated to not to code same assertions in
   several places. */
static void
sub_from_ctx_grabbed(reiser4_context *ctx, __u64 count)
{
	if (ctx->grabbed_blocks < count)
		print_clog();
	BUG_ON(ctx->grabbed_blocks < count);
	assert("zam-527", ctx->grabbed_blocks >= count);
	ctx->grabbed_blocks -= count;
}


static void
sub_from_sb_grabbed(reiser4_super_info_data *sbinfo, __u64 count)
{
	assert("zam-525", sbinfo->blocks_grabbed >= count);
	sbinfo->blocks_grabbed -= count;
}

/* Decrease the counter of block reserved for flush in super block. */
static void
sub_from_sb_flush_reserved (reiser4_super_info_data *sbinfo, __u64 count)
{
	assert ("vpf-291", sbinfo->blocks_flush_reserved >= count);
	sbinfo->blocks_flush_reserved -= count;
}

static void
sub_from_sb_fake_allocated(reiser4_super_info_data *sbinfo, __u64 count, reiser4_ba_flags_t flags)
{
	if (flags & BA_FORMATTED) {
		assert("zam-806", sbinfo->blocks_fake_allocated >= count);
		sbinfo->blocks_fake_allocated -= count;
	} else {
		assert("zam-528", sbinfo->blocks_fake_allocated_unformatted >= count);
		sbinfo->blocks_fake_allocated_unformatted -= count;
	}
}

static void
sub_from_sb_used(reiser4_super_info_data *sbinfo, __u64 count)
{
	assert("zam-530", sbinfo->blocks_used >= count + sbinfo->min_blocks_used);
	sbinfo->blocks_used -= count;
}

static void
sub_from_cluster_reserved(reiser4_super_info_data *sbinfo, __u64 count)
{
	assert("edward-501", sbinfo->blocks_clustered >= count);
	sbinfo->blocks_clustered -= count;
}

/* Increase the counter of block reserved for flush in atom. */
static void
add_to_atom_flush_reserved_nolock (txn_atom * atom, __u32 count)
{
	assert ("zam-772", atom != NULL);
	assert ("zam-773", spin_atom_is_locked (atom));
	atom->flush_reserved += count;
}

/* Decrease the counter of block reserved for flush in atom. */
static void
sub_from_atom_flush_reserved_nolock (txn_atom * atom, __u32 count)
{
	assert ("zam-774", atom != NULL);
	assert ("zam-775", spin_atom_is_locked (atom));
	assert ("nikita-2790", atom->flush_reserved >= count);
	atom->flush_reserved -= count;
}

/* super block has 6 counters: free, used, grabbed, fake allocated
   (formatted and unformatted) and flush reserved. Their sum must be
   number of blocks on a device. This function checks this */
reiser4_internal int
check_block_counters(const struct super_block *super)
{
	__u64 sum;

	sum = reiser4_grabbed_blocks(super) + reiser4_free_blocks(super) +
	    	reiser4_data_blocks(super) + reiser4_fake_allocated(super) +
		reiser4_fake_allocated_unformatted(super) + flush_reserved(super) +
		reiser4_clustered_blocks(super);
	if (reiser4_block_count(super) != sum) {
		printk("super block counters: "
		       "used %llu, free %llu, "
		       "grabbed %llu, fake allocated (formatetd %llu, unformatted %llu), "
		       "reserved %llu, clustered %llu, sum %llu, must be (block count) %llu\n",
		       (unsigned long long)reiser4_data_blocks(super),
		       (unsigned long long)reiser4_free_blocks(super),
		       (unsigned long long)reiser4_grabbed_blocks(super),
		       (unsigned long long)reiser4_fake_allocated(super),
		       (unsigned long long)reiser4_fake_allocated_unformatted(super),
		       (unsigned long long)flush_reserved(super),
		       (unsigned long long)reiser4_clustered_blocks(super),
		       (unsigned long long)sum,
		       (unsigned long long)reiser4_block_count(super));
		return 0;
	}
	return 1;
}

#if REISER4_DEBUG_OUTPUT
reiser4_internal void
print_block_counters(const char *prefix,
		     const struct super_block *super, txn_atom *atom)
{
	if (super == NULL)
		super = reiser4_get_current_sb();
	printk("%s:\tsuper: G: %llu, F: %llu, D: %llu, U: %llu + %llu, R: %llu, C: %llu, T: %llu\n",
	       prefix,
	       reiser4_grabbed_blocks(super),
	       reiser4_free_blocks(super),
	       reiser4_data_blocks(super),
	       reiser4_fake_allocated(super),
	       reiser4_fake_allocated_unformatted(super),
	       flush_reserved(super),
	       reiser4_clustered_blocks(super),
	       reiser4_block_count(super));
	printk("\tcontext: G: %llu",
	       get_current_context()->grabbed_blocks);
	if (atom == NULL)
		atom = get_current_atom_locked_nocheck();
	if (atom != NULL) {
		printk("\tatom: R: %llu", atom->flush_reserved);
		UNLOCK_ATOM(atom);
	}
	printk("\n");
}
#endif

/* Adjust "working" free blocks counter for number of blocks we are going to
   allocate.  Record number of grabbed blocks in fs-wide and per-thread
   counters.  This function should be called before bitmap scanning or
   allocating fake block numbers

   @super           -- pointer to reiser4 super block;
   @count           -- number of blocks we reserve;

   @return          -- 0 if success,  -ENOSPC, if all
                       free blocks are preserved or already allocated.
*/

static int
reiser4_grab(reiser4_context *ctx, __u64 count, reiser4_ba_flags_t flags)
{
	__u64 free_blocks;
	int ret = 0, use_reserved = flags & BA_RESERVED;
	reiser4_super_info_data *sbinfo;

	assert("vs-1276", ctx == get_current_context());

	sbinfo = get_super_private(ctx->super);

	reiser4_spin_lock_sb(sbinfo);

	free_blocks = sbinfo->blocks_free;

	ON_TRACE(TRACE_ALLOC, "reiser4_grab: free_blocks %llu\n", free_blocks);

	if ((use_reserved && free_blocks < count) ||
	    (!use_reserved && free_blocks < count + sbinfo->blocks_reserved)) {
		ret = RETERR(-ENOSPC);

		ON_TRACE(TRACE_ALLOC, "reiser4_grab: ENOSPC: count %llu\n", count);

		goto unlock_and_ret;
	}

	ctx->grabbed_blocks += count;

	sbinfo->blocks_grabbed += count;
	sbinfo->blocks_free -= count;

#if REISER4_DEBUG
	ctx->grabbed_initially = count;
	fill_backtrace(&ctx->grabbed_at, REISER4_BACKTRACE_DEPTH, 0);
#endif

	assert("nikita-2986", check_block_counters(ctx->super));

	ON_TRACE(TRACE_ALLOC, "%s: grabbed %llu, free blocks left %llu\n",
		 __FUNCTION__, count, reiser4_free_blocks (ctx->super));

	/* disable grab space in current context */
	ctx->grab_enabled = 0;

unlock_and_ret:
	reiser4_spin_unlock_sb(sbinfo);

	return ret;
}

reiser4_internal int
reiser4_grab_space(__u64 count, reiser4_ba_flags_t flags)
{
	int ret;
	reiser4_context *ctx;

	assert("nikita-2964", ergo(flags & BA_CAN_COMMIT,
				   lock_stack_isclean(get_current_lock_stack())));
	ON_TRACE(TRACE_RESERVE, "grab_space: %llu block(s).", count);

	ctx = get_current_context();
	if (!(flags & BA_FORCE) && !is_grab_enabled(ctx)) {
		ON_TRACE(TRACE_RESERVE, "grab disabled and not forced!\n");
		return 0;
	}

	ret = reiser4_grab(ctx, count, flags);
	if (ret == -ENOSPC) {

		/* Trying to commit the all transactions if BA_CAN_COMMIT flag present */
		if (flags & BA_CAN_COMMIT) {

			ON_TRACE(TRACE_RESERVE, "force commit!..");

			txnmgr_force_commit_all(ctx->super, 0);

			ctx->grab_enabled = 1;
			ret = reiser4_grab(ctx, count, flags);
		}
	}
	ON_TRACE(TRACE_RESERVE, "%s(%d)\n", (ret == 0) ? "ok" : "failed", ret);
	/*
	 * allocation from reserved pool cannot fail. This is severe error.
	 */
	assert("nikita-3005", ergo(flags & BA_RESERVED, ret == 0));
	return ret;
}

/*
 * SPACE RESERVED FOR UNLINK/TRUNCATE
 *
 * Unlink and truncate require space in transaction (to update stat data, at
 * least). But we don't want rm(1) to fail with "No space on device" error.
 *
 * Solution is to reserve 5% of disk space for truncates and
 * unlinks. Specifically, normal space grabbing requests don't grab space from
 * reserved area. Only requests with BA_RESERVED bit in flags are allowed to
 * drain it. Per super block delete_sema semaphore is used to allow only one
 * thread at a time to grab from reserved area.
 *
 * Grabbing from reserved area should always be performed with BA_CAN_COMMIT
 * flag.
 *
 */

reiser4_internal int reiser4_grab_reserved(struct super_block *super,
					   __u64 count, reiser4_ba_flags_t flags)
{
	reiser4_super_info_data *sbinfo = get_super_private(super);

	assert("nikita-3175", flags & BA_CAN_COMMIT);

	/* Check the delete semaphore already taken by us, we assume that
	 * reading of machine word is atomic. */
	if (sbinfo->delete_sema_owner == current) {
		if (reiser4_grab_space(count, (flags | BA_RESERVED) & ~BA_CAN_COMMIT)) {
			warning("zam-1003", "nested call of grab_reserved fails count=(%llu)",
				(unsigned long long)count);
			reiser4_release_reserved(super);
			return RETERR(-ENOSPC);
		}
		return 0;
	}

	if (reiser4_grab_space(count, flags)) {
		down(&sbinfo->delete_sema);
		assert("nikita-2929", sbinfo->delete_sema_owner == NULL);
		sbinfo->delete_sema_owner = current;

		if (reiser4_grab_space(count, flags | BA_RESERVED)) {
			warning("zam-833",
				"reserved space is not enough (%llu)", (unsigned long long)count);
			reiser4_release_reserved(super);
			return RETERR(-ENOSPC);
		}
	}
	return 0;
}

reiser4_internal void
reiser4_release_reserved(struct super_block *super)
{
	reiser4_super_info_data *info;

	info = get_super_private(super);
	if (info->delete_sema_owner == current) {
		info->delete_sema_owner = NULL;
		up(&info->delete_sema);
	}
}

static reiser4_super_info_data *
grabbed2fake_allocated_head(void)
{
	reiser4_context *ctx;
	reiser4_super_info_data *sbinfo;

	ctx = get_current_context();
	sub_from_ctx_grabbed(ctx, 1);

	sbinfo = get_super_private(ctx->super);
	reiser4_spin_lock_sb(sbinfo);

	sub_from_sb_grabbed(sbinfo, 1);
	/* return sbinfo locked */
	return sbinfo;
}

/* is called after @count fake block numbers are allocated and pointer to
   those blocks are inserted into tree. */
static void
grabbed2fake_allocated_formatted(void)
{
	reiser4_super_info_data *sbinfo;

	sbinfo = grabbed2fake_allocated_head();
	sbinfo->blocks_fake_allocated ++;

	assert("vs-922", check_block_counters(reiser4_get_current_sb()));

	reiser4_spin_unlock_sb(sbinfo);
}

static void
grabbed2fake_allocated_unformatted(void)
{
	reiser4_super_info_data *sbinfo;

	sbinfo = grabbed2fake_allocated_head();
	sbinfo->blocks_fake_allocated_unformatted ++;

	assert("vs-9221", check_block_counters(reiser4_get_current_sb()));

	reiser4_spin_unlock_sb(sbinfo);
}

reiser4_internal void
grabbed2cluster_reserved(int count)
{
	reiser4_context *ctx;
	reiser4_super_info_data *sbinfo;

	ctx = get_current_context();
	sub_from_ctx_grabbed(ctx, count);

	sbinfo = get_super_private(ctx->super);
	reiser4_spin_lock_sb(sbinfo);

	sub_from_sb_grabbed(sbinfo, count);
	sbinfo->blocks_clustered += count;

	assert("edward-504", check_block_counters(ctx->super));

	reiser4_spin_unlock_sb(sbinfo);
}

reiser4_internal void
cluster_reserved2grabbed(int count)
{
	reiser4_context *ctx;
	reiser4_super_info_data *sbinfo;

	ctx = get_current_context();

	sbinfo = get_super_private(ctx->super);
	reiser4_spin_lock_sb(sbinfo);

	sub_from_cluster_reserved(sbinfo, count);
	sbinfo->blocks_grabbed += count;

	assert("edward-505", check_block_counters(ctx->super));

	reiser4_spin_unlock_sb(sbinfo);
	ctx->grabbed_blocks += count;
}

reiser4_internal void
cluster_reserved2free(int count)
{
	reiser4_context *ctx;
	reiser4_super_info_data *sbinfo;

	assert("edward-503", get_current_context()->grabbed_blocks == 0);

	ctx = get_current_context();
	sbinfo = get_super_private(ctx->super);
	reiser4_spin_lock_sb(sbinfo);

	sub_from_cluster_reserved(sbinfo, count);
	sbinfo->blocks_free += count;

	assert("edward-502", check_block_counters(ctx->super));

	reiser4_spin_unlock_sb(sbinfo);
}

static spinlock_t fake_lock = SPIN_LOCK_UNLOCKED;
static reiser4_block_nr fake_gen = 0;

/* obtain a block number for new formatted node which will be used to refer
   to this newly allocated node until real allocation is done */
static inline void assign_fake_blocknr(reiser4_block_nr *blocknr)
{
	spin_lock(&fake_lock);
	*blocknr = fake_gen++;
	spin_unlock(&fake_lock);

	*blocknr &= ~REISER4_BLOCKNR_STATUS_BIT_MASK;
	*blocknr |= REISER4_UNALLOCATED_STATUS_VALUE;
	assert("zam-394", zlook(current_tree, blocknr) == NULL);
}

reiser4_internal int
assign_fake_blocknr_formatted(reiser4_block_nr *blocknr)
{
	ON_TRACE(TRACE_RESERVE, "assign_fake_blocknr_formatted: moving 1 grabbed block to fake allocated formatted\n");

	assign_fake_blocknr(blocknr);
	grabbed2fake_allocated_formatted();

	return 0;
}

/* return fake blocknr which will be used for unformatted nodes */
reiser4_internal reiser4_block_nr
fake_blocknr_unformatted(void)
{
	reiser4_block_nr blocknr;

	ON_TRACE(TRACE_RESERVE, "fake_blocknr_unformatted: moving 1 grabbed block to fake allocated unformatted\n");

	assign_fake_blocknr(&blocknr);
	grabbed2fake_allocated_unformatted();

	/*XXXXX*/inc_unalloc_unfm_ptr();
	return blocknr;
}


/* adjust sb block counters, if real (on-disk) block allocation immediately
   follows grabbing of free disk space. */
static void
grabbed2used(reiser4_context *ctx, reiser4_super_info_data *sbinfo, __u64 count)
{
	sub_from_ctx_grabbed(ctx, count);

	reiser4_spin_lock_sb(sbinfo);

	sub_from_sb_grabbed(sbinfo, count);
	sbinfo->blocks_used += count;

	assert("nikita-2679", check_block_counters(ctx->super));

	reiser4_spin_unlock_sb(sbinfo);
}

/* adjust sb block counters when @count unallocated blocks get mapped to disk */
static void
fake_allocated2used(reiser4_super_info_data *sbinfo, __u64 count, reiser4_ba_flags_t flags)
{
	reiser4_spin_lock_sb(sbinfo);

	sub_from_sb_fake_allocated(sbinfo, count, flags);
	sbinfo->blocks_used += count;

	assert("nikita-2680", check_block_counters(reiser4_get_current_sb()));

	reiser4_spin_unlock_sb(sbinfo);
}

static void
flush_reserved2used(txn_atom * atom, __u64 count)
{
	reiser4_super_info_data *sbinfo;

	assert("zam-787", atom != NULL);
	assert("zam-788", spin_atom_is_locked(atom));

	sub_from_atom_flush_reserved_nolock(atom, (__u32)count);

	sbinfo = get_current_super_private();
	reiser4_spin_lock_sb(sbinfo);

	sub_from_sb_flush_reserved(sbinfo, count);
	sbinfo->blocks_used += count;

	assert ("zam-789", check_block_counters(reiser4_get_current_sb()));

	reiser4_spin_unlock_sb(sbinfo);
}

/* update the per fs  blocknr hint default value. */
reiser4_internal void
update_blocknr_hint_default (const struct super_block *s, const reiser4_block_nr * block)
{
	reiser4_super_info_data *sbinfo = get_super_private(s);

	assert("nikita-3342", !blocknr_is_fake(block));

	reiser4_spin_lock_sb(sbinfo);
	if (*block < sbinfo->block_count) {
		sbinfo->blocknr_hint_default = *block;
	} else {
		warning("zam-676",
			"block number %llu is too large to be used in a blocknr hint\n", (unsigned long long) *block);
		dump_stack();
		DEBUGON(1);
	}
	reiser4_spin_unlock_sb(sbinfo);
}

/* get current value of the default blocknr hint. */
reiser4_internal void get_blocknr_hint_default(reiser4_block_nr * result)
{
	reiser4_super_info_data * sbinfo = get_current_super_private();

	reiser4_spin_lock_sb(sbinfo);
	*result = sbinfo->blocknr_hint_default;
	assert("zam-677", *result < sbinfo->block_count);
	reiser4_spin_unlock_sb(sbinfo);
}

/* Allocate "real" disk blocks by calling a proper space allocation plugin
 * method. Blocks are allocated in one contiguous disk region. The plugin
 * independent part accounts blocks by subtracting allocated amount from grabbed
 * or fake block counter and add the same amount to the counter of allocated
 * blocks.
 *
 * @hint -- a reiser4 blocknr hint object which contains further block
 *          allocation hints and parameters (search start, a stage of block
 *          which will be mapped to disk, etc.),
 * @blk  -- an out parameter for the beginning of the allocated region,
 * @len  -- in/out parameter, it should contain the maximum number of allocated
 *          blocks, after block allocation completes, it contains the length of
 *          allocated disk region.
 * @flags -- see reiser4_ba_flags_t description.
 *
 * @return -- 0 if success, error code otherwise.
 */
reiser4_internal int
reiser4_alloc_blocks(reiser4_blocknr_hint * hint, reiser4_block_nr * blk,
		     reiser4_block_nr * len, reiser4_ba_flags_t flags)
{
	__u64 needed = *len;
	reiser4_context *ctx;
	reiser4_super_info_data *sbinfo;
	int ret;

	assert ("zam-986", hint != NULL);

	ctx = get_current_context();
	sbinfo = get_super_private(ctx->super);

	ON_TRACE(TRACE_RESERVE, "reiser4_alloc_blocks: needed %llu..", needed);

	assert("vpf-339", hint != NULL);

	ON_TRACE(TRACE_ALLOC,
		 "alloc_blocks: requested %llu, search from %llu\n",
		 (unsigned long long) *len, (unsigned long long) (hint ? hint->blk : ~0ull));

	/* For write-optimized data we use default search start value, which is
	 * close to last write location. */
	if (flags & BA_USE_DEFAULT_SEARCH_START) {
		reiser4_stat_inc(block_alloc.nohint);
		get_blocknr_hint_default(&hint->blk);
	}

	/* VITALY: allocator should grab this for internal/tx-lists/similar only. */
/* VS-FIXME-HANS: why is this comment above addressed to vitaly (from vitaly)? */
	if (hint->block_stage == BLOCK_NOT_COUNTED) {
		ret = reiser4_grab_space_force(*len, flags);
		if (ret != 0)
			return ret;
	}

	ret = sa_alloc_blocks(get_space_allocator(ctx->super), hint, (int) needed, blk, len);

	if (!ret) {
		assert("zam-680", *blk < reiser4_block_count(ctx->super));
		assert("zam-681", *blk + *len <= reiser4_block_count(ctx->super));

		if (flags & BA_PERMANENT) {
			/* we assume that current atom exists at this moment */
			txn_atom * atom = get_current_atom_locked ();
			atom -> nr_blocks_allocated += *len;
			UNLOCK_ATOM (atom);
		}

		switch (hint->block_stage) {
		case BLOCK_NOT_COUNTED:
		case BLOCK_GRABBED:
			ON_TRACE(TRACE_RESERVE, "ok. %llu blocks grabbed to used.\n", *len);
			grabbed2used(ctx, sbinfo, *len);
			break;
		case BLOCK_UNALLOCATED:
			ON_TRACE(TRACE_RESERVE, "ok. %llu blocks fake allocated to used.\n", *len);
			fake_allocated2used(sbinfo, *len, flags);
			break;
		case BLOCK_FLUSH_RESERVED:
			ON_TRACE(TRACE_RESERVE, "ok. %llu flush reserved to used (get wandered?)\n", *len);
			{
				txn_atom * atom = get_current_atom_locked ();
				flush_reserved2used(atom, *len);
				UNLOCK_ATOM (atom);
			}
			break;
		default:
			impossible("zam-531", "wrong block stage");
		}
	} else {
		assert ("zam-821", ergo(hint->max_dist == 0 && !hint->backward, ret != -ENOSPC));
		if (hint->block_stage == BLOCK_NOT_COUNTED)
			grabbed2free(ctx, sbinfo, needed);
	}

	return ret;
}

/* used -> fake_allocated -> grabbed -> free */

/* adjust sb block counters when @count unallocated blocks get unmapped from
   disk */
static void
used2fake_allocated(reiser4_super_info_data *sbinfo, __u64 count, int formatted)
{
	reiser4_spin_lock_sb(sbinfo);

	if (formatted)
		sbinfo->blocks_fake_allocated += count;
	else
		sbinfo->blocks_fake_allocated_unformatted += count;

	sub_from_sb_used(sbinfo, count);

	assert("nikita-2681", check_block_counters(reiser4_get_current_sb()));

	reiser4_spin_unlock_sb(sbinfo);
}

static void
used2flush_reserved(reiser4_super_info_data *sbinfo, txn_atom * atom, __u64 count,
		    reiser4_ba_flags_t flags UNUSED_ARG)
{
	assert("nikita-2791", atom != NULL);
	assert("nikita-2792", spin_atom_is_locked(atom));

	add_to_atom_flush_reserved_nolock(atom, (__u32)count);

	reiser4_spin_lock_sb(sbinfo);

	sbinfo->blocks_flush_reserved += count;
	/*add_to_sb_flush_reserved(sbinfo, count);*/
	sub_from_sb_used(sbinfo, count);

	assert("nikita-2681", check_block_counters(reiser4_get_current_sb()));

	reiser4_spin_unlock_sb(sbinfo);
}

/* disk space, virtually used by fake block numbers is counted as "grabbed" again. */
static void
fake_allocated2grabbed(reiser4_context *ctx, reiser4_super_info_data *sbinfo, __u64 count, reiser4_ba_flags_t flags)
{
	ctx->grabbed_blocks += count;

	reiser4_spin_lock_sb(sbinfo);

	assert("nikita-2682", check_block_counters(ctx->super));

	sbinfo->blocks_grabbed += count;
	sub_from_sb_fake_allocated(sbinfo, count, flags & BA_FORMATTED);

	assert("nikita-2683", check_block_counters(ctx->super));

	reiser4_spin_unlock_sb(sbinfo);
}

reiser4_internal void
fake_allocated2free(__u64 count, reiser4_ba_flags_t flags)
{
	reiser4_context *ctx;
	reiser4_super_info_data *sbinfo;

	ctx = get_current_context();
	sbinfo = get_super_private(ctx->super);

	ON_TRACE(TRACE_RESERVE, "fake_allocated2free %llu blocks\n", count);

	fake_allocated2grabbed(ctx, sbinfo, count, flags);
	grabbed2free(ctx, sbinfo, count);
}

reiser4_internal void
grabbed2free_mark(__u64 mark)
{
	reiser4_context *ctx;
	reiser4_super_info_data *sbinfo;

	ctx = get_current_context();
	sbinfo = get_super_private(ctx->super);

	assert("nikita-3007", (__s64)mark >= 0);
	assert("nikita-3006",
	       ctx->grabbed_blocks >= mark);
	grabbed2free(ctx, sbinfo, ctx->grabbed_blocks - mark);
}

/* Adjust free blocks count for blocks which were reserved but were not used. */
reiser4_internal void
grabbed2free(reiser4_context *ctx, reiser4_super_info_data *sbinfo,
	       __u64 count)
{
	ON_TRACE(TRACE_RESERVE, "grabbed2free: %llu\n", count);

	sub_from_ctx_grabbed(ctx, count);


	reiser4_spin_lock_sb(sbinfo);

	sub_from_sb_grabbed(sbinfo, count);
	sbinfo->blocks_free += count;
	assert("nikita-2684", check_block_counters(ctx->super));

	reiser4_spin_unlock_sb(sbinfo);
}

reiser4_internal void
grabbed2flush_reserved_nolock(txn_atom * atom, __u64 count)
{
	reiser4_context *ctx;
	reiser4_super_info_data *sbinfo;

	assert("vs-1095", atom);

	ctx = get_current_context();
	sbinfo = get_super_private(ctx->super);

	sub_from_ctx_grabbed(ctx, count);

	add_to_atom_flush_reserved_nolock(atom, count);

	reiser4_spin_lock_sb(sbinfo);

	sbinfo->blocks_flush_reserved += count;
	sub_from_sb_grabbed(sbinfo, count);

	assert ("vpf-292", check_block_counters(ctx->super));

	ON_TRACE(TRACE_RESERVE, "__grabbed2flush_reserved_nolock %llu blocks: atom %u has %llu flush reserved blocks\n",
		 count, atom->atom_id, atom->flush_reserved);

	reiser4_spin_unlock_sb(sbinfo);
}

reiser4_internal void
grabbed2flush_reserved(__u64 count)
{
	txn_atom * atom = get_current_atom_locked ();

	ON_TRACE(TRACE_RESERVE, "__grabbed2flush_reserved\n");

	grabbed2flush_reserved_nolock (atom, count);

	UNLOCK_ATOM (atom);
}

reiser4_internal void flush_reserved2grabbed(txn_atom * atom, __u64 count)
{
	reiser4_context *ctx;
	reiser4_super_info_data *sbinfo;

	assert("nikita-2788", atom != NULL);
	assert("nikita-2789", spin_atom_is_locked(atom));

	ctx = get_current_context();
	sbinfo = get_super_private(ctx->super);

	ctx->grabbed_blocks += count;

	sub_from_atom_flush_reserved_nolock(atom, (__u32)count);

	reiser4_spin_lock_sb(sbinfo);

	sbinfo->blocks_grabbed += count;
	sub_from_sb_flush_reserved(sbinfo, count);

	assert ("vpf-292", check_block_counters (ctx->super));

	reiser4_spin_unlock_sb (sbinfo);
}

/* release all blocks grabbed in context which where not used. */
reiser4_internal void
all_grabbed2free(void)
{
	reiser4_context *ctx = get_current_context();

	grabbed2free(ctx, get_super_private(ctx->super), ctx->grabbed_blocks);
}

/* adjust sb block counters if real (on-disk) blocks do not become unallocated
   after freeing, @count blocks become "grabbed". */
static void
used2grabbed(reiser4_context *ctx, reiser4_super_info_data *sbinfo, __u64 count)
{
	ctx->grabbed_blocks += count;

	reiser4_spin_lock_sb(sbinfo);

	sbinfo->blocks_grabbed += count;
	sub_from_sb_used(sbinfo, count);

	assert("nikita-2685", check_block_counters(ctx->super));

	reiser4_spin_unlock_sb(sbinfo);
}

/* this used to be done through used2grabbed and grabbed2free*/
static void
used2free(reiser4_super_info_data *sbinfo, __u64 count)
{
	reiser4_spin_lock_sb(sbinfo);

	sbinfo->blocks_free += count;
	sub_from_sb_used(sbinfo, count);

	assert("nikita-2685", check_block_counters(reiser4_get_current_sb()));

	reiser4_spin_unlock_sb(sbinfo);
}

#if REISER4_DEBUG

/* check "allocated" state of given block range */
void
reiser4_check_blocks(const reiser4_block_nr * start, const reiser4_block_nr * len, int desired)
{
	sa_check_blocks(start, len, desired);
}

/* check "allocated" state of given block */
void
reiser4_check_block(const reiser4_block_nr * block, int desired)
{
	const reiser4_block_nr one = 1;

	reiser4_check_blocks(block, &one, desired);
}

#endif

/* Blocks deallocation function may do an actual deallocation through space
   plugin allocation or store deleted block numbers in atom's delete_set data
   structure depend on @defer parameter. */

/* if BA_DEFER bit is not turned on, @target_stage means the stage of blocks which
   will be deleted from WORKING bitmap. They might be just unmapped from disk, or
   freed but disk space is still grabbed by current thread, or these blocks must
   not be counted in any reiser4 sb block counters, see block_stage_t comment */

/* BA_FORMATTED bit is only used when BA_DEFER in not present: it is used to
   distinguish blocks allocated for unformatted and formatted nodes */

reiser4_internal int
reiser4_dealloc_blocks(const reiser4_block_nr * start,
		       const reiser4_block_nr * len,
		       block_stage_t target_stage, reiser4_ba_flags_t flags)
{
	txn_atom *atom = NULL;
	int ret;
	reiser4_context *ctx;
	reiser4_super_info_data *sbinfo;

	ON_TRACE(TRACE_RESERVE, "reiser4_dealloc_blocks: %llu blocks", *len);

	ctx = get_current_context();
	sbinfo = get_super_private(ctx->super);

	if (REISER4_DEBUG) {
		assert("zam-431", *len != 0);
		assert("zam-432", *start != 0);
		assert("zam-558", !blocknr_is_fake(start));

		reiser4_spin_lock_sb(sbinfo);
		assert("zam-562", *start < sbinfo->block_count);
		reiser4_spin_unlock_sb(sbinfo);
	}

	if (flags & BA_DEFER) {
		blocknr_set_entry *bsep = NULL;

		ON_TRACE(TRACE_RESERVE, "put on delete set\n");

		/* storing deleted block numbers in a blocknr set
		   datastructure for further actual deletion */
		do {
			atom = get_current_atom_locked();
			assert("zam-430", atom != NULL);

			ret = blocknr_set_add_extent(atom, &atom->delete_set, &bsep, start, len);

			if (ret == -ENOMEM)
				return ret;

			/* This loop might spin at most two times */
		} while (ret == -E_REPEAT);

		assert("zam-477", ret == 0);
		assert("zam-433", atom != NULL);

		UNLOCK_ATOM(atom);

	} else {
		assert("zam-425", get_current_super_private() != NULL);
		sa_dealloc_blocks(get_space_allocator(ctx->super), *start, *len);

		if (flags & BA_PERMANENT) {
			/* These blocks were counted as allocated, we have to revert it
			 * back if allocation is discarded. */
			txn_atom * atom = get_current_atom_locked ();
			atom->nr_blocks_allocated -= *len;
			UNLOCK_ATOM (atom);
		}

		switch (target_stage) {
		case BLOCK_NOT_COUNTED:
			assert("vs-960", flags & BA_FORMATTED);

			ON_TRACE(TRACE_RESERVE, "moved from used to free\n");

			/* VITALY: This is what was grabbed for internal/tx-lists/similar only */
			used2free(sbinfo, *len);
			break;

		case BLOCK_GRABBED:

			ON_TRACE(TRACE_RESERVE, "moved from used to grabbed\n");

			used2grabbed(ctx, sbinfo, *len);
			break;

		case BLOCK_UNALLOCATED:

			ON_TRACE(TRACE_RESERVE, "moved from used to fake allocated\n");

			used2fake_allocated(sbinfo, *len, flags & BA_FORMATTED);
			break;

		case BLOCK_FLUSH_RESERVED: {
			txn_atom *atom;

			ON_TRACE(TRACE_RESERVE, "moved from used to flush reserved\n");

			atom = get_current_atom_locked();
			used2flush_reserved(sbinfo, atom, *len, flags & BA_FORMATTED);
			UNLOCK_ATOM(atom);
			break;
		}
		default:
			impossible("zam-532", "wrong block stage");
		}
	}

	return 0;
}

/* wrappers for block allocator plugin methods */
reiser4_internal int
pre_commit_hook(void)
{
	assert("zam-502", get_current_super_private() != NULL);
	sa_pre_commit_hook();
	return 0;
}

/* an actor which applies delete set to block allocator data */
static int
apply_dset(txn_atom * atom UNUSED_ARG, const reiser4_block_nr * a, const reiser4_block_nr * b, void *data UNUSED_ARG)
{
	reiser4_context *ctx;
	reiser4_super_info_data *sbinfo;

	__u64 len = 1;

	ctx = get_current_context();
	sbinfo = get_super_private(ctx->super);

	assert("zam-877", atom->stage >= ASTAGE_PRE_COMMIT);
	assert("zam-552", sbinfo != NULL);

	if (b != NULL)
		len = *b;

	if (REISER4_DEBUG) {
		reiser4_spin_lock_sb(sbinfo);

		assert("zam-554", *a < reiser4_block_count(ctx->super));
		assert("zam-555", *a + len <= reiser4_block_count(ctx->super));

		reiser4_spin_unlock_sb(sbinfo);
	}

	sa_dealloc_blocks(&sbinfo->space_allocator, *a, len);
	/* adjust sb block counters */
	used2free(sbinfo, len);
	return 0;
}

reiser4_internal void
post_commit_hook(void)
{
	txn_atom *atom;

	atom = get_current_atom_locked();
	assert("zam-452", atom->stage == ASTAGE_POST_COMMIT);
	UNLOCK_ATOM(atom);

	/* do the block deallocation which was deferred
	   until commit is done */
	blocknr_set_iterator(atom, &atom->delete_set, apply_dset, NULL, 1);

	assert("zam-504", get_current_super_private() != NULL);
	sa_post_commit_hook();
}

reiser4_internal void
post_write_back_hook(void)
{
	assert("zam-504", get_current_super_private() != NULL);

	sa_post_commit_hook();
}

/*
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   scroll-step: 1
   End:
*/
