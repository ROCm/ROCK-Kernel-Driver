/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

/* This file contains code for various block number sets used by the atom to
   track the deleted set and wandered block mappings. */

#include "debug.h"
#include "dformat.h"
#include "type_safe_list.h"
#include "txnmgr.h"

#include <linux/slab.h>

/* The proposed data structure for storing unordered block number sets is a
   list of elements, each of which contains an array of block number or/and
   array of block number pairs. That element called blocknr_set_entry is used
   to store block numbers from the beginning and for extents from the end of
   the data field (char data[...]). The ->nr_blocks and ->nr_pairs fields
   count numbers of blocks and extents.

   +------------------- blocknr_set_entry->data ------------------+
   |block1|block2| ... <free space> ... |pair3|pair2|pair1|
   +------------------------------------------------------------+

   When current blocknr_set_entry is full, allocate a new one. */

/* Usage examples: blocknr sets are used in reiser4 for storing atom's delete
 * set (single blocks and block extents), in that case blocknr pair represent an
 * extent; atom's wandered map is also stored as a blocknr set, blocknr pairs
 * there represent a (real block) -> (wandered block) mapping. */

typedef struct blocknr_pair blocknr_pair;

/* The total size of a blocknr_set_entry. */
#define BLOCKNR_SET_ENTRY_SIZE 128

/* The number of blocks that can fit the blocknr data area. */
#define BLOCKNR_SET_ENTRIES_NUMBER               \
       ((BLOCKNR_SET_ENTRY_SIZE -           \
         2 * sizeof (unsigned) -            \
         sizeof (blocknr_set_list_link)) /  \
        sizeof (reiser4_block_nr))

/* An entry of the blocknr_set */
struct blocknr_set_entry {
	unsigned nr_singles;
	unsigned nr_pairs;
	blocknr_set_list_link link;
	reiser4_block_nr entries[BLOCKNR_SET_ENTRIES_NUMBER];
};

/* A pair of blocks as recorded in the blocknr_set_entry data. */
struct blocknr_pair {
	reiser4_block_nr a;
	reiser4_block_nr b;
};

/* The list definition. */
TYPE_SAFE_LIST_DEFINE(blocknr_set, blocknr_set_entry, link);

/* Return the number of blocknr slots available in a blocknr_set_entry. */
/* Audited by: green(2002.06.11) */
static unsigned
bse_avail(blocknr_set_entry * bse)
{
	unsigned used = bse->nr_singles + 2 * bse->nr_pairs;

	assert("jmacd-5088", BLOCKNR_SET_ENTRIES_NUMBER >= used);
	cassert(sizeof (blocknr_set_entry) == BLOCKNR_SET_ENTRY_SIZE);

	return BLOCKNR_SET_ENTRIES_NUMBER - used;
}

/* Initialize a blocknr_set_entry. */
/* Audited by: green(2002.06.11) */
static void
bse_init(blocknr_set_entry * bse)
{
	bse->nr_singles = 0;
	bse->nr_pairs = 0;
	blocknr_set_list_clean(bse);
}

/* Allocate and initialize a blocknr_set_entry. */
/* Audited by: green(2002.06.11) */
static blocknr_set_entry *
bse_alloc(void)
{
	blocknr_set_entry *e;

	if ((e = (blocknr_set_entry *) kmalloc(sizeof (blocknr_set_entry), GFP_KERNEL)) == NULL) {
		return NULL;
	}

	bse_init(e);

	return e;
}

/* Free a blocknr_set_entry. */
/* Audited by: green(2002.06.11) */
static void
bse_free(blocknr_set_entry * bse)
{
	kfree(bse);
}

/* Add a block number to a blocknr_set_entry */
/* Audited by: green(2002.06.11) */
static void
bse_put_single(blocknr_set_entry * bse, const reiser4_block_nr * block)
{
	assert("jmacd-5099", bse_avail(bse) >= 1);

	bse->entries[bse->nr_singles++] = *block;
}

/* Get a pair of block numbers */
/* Audited by: green(2002.06.11) */
static inline blocknr_pair *
bse_get_pair(blocknr_set_entry * bse, unsigned pno)
{
	assert("green-1", BLOCKNR_SET_ENTRIES_NUMBER >= 2 * (pno + 1));

	return (blocknr_pair *) (bse->entries + BLOCKNR_SET_ENTRIES_NUMBER - 2 * (pno + 1));
}

/* Add a pair of block numbers to a blocknr_set_entry */
/* Audited by: green(2002.06.11) */
static void
bse_put_pair(blocknr_set_entry * bse, const reiser4_block_nr * a, const reiser4_block_nr * b)
{
	blocknr_pair *pair;

	assert("jmacd-5100", bse_avail(bse) >= 2 && a != NULL && b != NULL);

	pair = bse_get_pair(bse, bse->nr_pairs++);

	pair->a = *a;
	pair->b = *b;
}

/* Add either a block or pair of blocks to the block number set.  The first
   blocknr (@a) must be non-NULL.  If @b is NULL a single blocknr is added, if
   @b is non-NULL a pair is added.  The block number set belongs to atom, and
   the call is made with the atom lock held.  There may not be enough space in
   the current blocknr_set_entry.  If new_bsep points to a non-NULL
   blocknr_set_entry then it will be added to the blocknr_set and new_bsep
   will be set to NULL.  If new_bsep contains NULL then the atom lock will be
   released and a new bse will be allocated in new_bsep.  E_REPEAT will be
   returned with the atom unlocked for the operation to be tried again.  If
   the operation succeeds, 0 is returned.  If new_bsep is non-NULL and not
   used during the call, it will be freed automatically. */
/* Audited by: green(2002.06.11) */
static int
blocknr_set_add(txn_atom * atom,
		blocknr_set * bset,
		blocknr_set_entry ** new_bsep, const reiser4_block_nr * a, const reiser4_block_nr * b)
{
	blocknr_set_entry *bse;
	unsigned entries_needed;

	assert("jmacd-5101", a != NULL);

	entries_needed = (b == NULL) ? 1 : 2;
	if (blocknr_set_list_empty(&bset->entries) || bse_avail(blocknr_set_list_front(&bset->entries))
	    < entries_needed) {
		/* See if a bse was previously allocated. */
		if (*new_bsep == NULL) {
			UNLOCK_ATOM(atom);
			*new_bsep = bse_alloc();
			return (*new_bsep != NULL) ? -E_REPEAT : RETERR(-ENOMEM);
		}

		/* Put it on the head of the list. */
		blocknr_set_list_push_front(&bset->entries, *new_bsep);

		*new_bsep = NULL;
	}

	/* Add the single or pair. */
	bse = blocknr_set_list_front(&bset->entries);
	if (b == NULL) {
		bse_put_single(bse, a);
	} else {
		bse_put_pair(bse, a, b);
	}

	/* If new_bsep is non-NULL then there was an allocation race, free this copy. */
	if (*new_bsep != NULL) {
		bse_free(*new_bsep);
		*new_bsep = NULL;
	}

	return 0;
}

/* Add an extent to the block set.  If the length is 1, it is treated as a
   single block (e.g., reiser4_set_add_block). */
/* Audited by: green(2002.06.11) */
/* Auditor note: Entire call chain cannot hold any spinlocks, because
   kmalloc might schedule. The only exception is atom spinlock, which is
   properly freed. */
reiser4_internal int
blocknr_set_add_extent(txn_atom * atom,
		       blocknr_set * bset,
		       blocknr_set_entry ** new_bsep, const reiser4_block_nr * start, const reiser4_block_nr * len)
{
	assert("jmacd-5102", start != NULL && len != NULL && *len > 0);
	return blocknr_set_add(atom, bset, new_bsep, start, *len == 1 ? NULL : len);
}

/* Add a block pair to the block set. It adds exactly a pair, which is checked
 * by an assertion that both arguments are not null.*/
/* Audited by: green(2002.06.11) */
/* Auditor note: Entire call chain cannot hold any spinlocks, because
   kmalloc might schedule. The only exception is atom spinlock, which is
   properly freed. */
reiser4_internal int
blocknr_set_add_pair(txn_atom * atom,
		     blocknr_set * bset,
		     blocknr_set_entry ** new_bsep, const reiser4_block_nr * a, const reiser4_block_nr * b)
{
	assert("jmacd-5103", a != NULL && b != NULL);
	return blocknr_set_add(atom, bset, new_bsep, a, b);
}

/* Initialize a blocknr_set. */
/* Audited by: green(2002.06.11) */
reiser4_internal void
blocknr_set_init(blocknr_set * bset)
{
	blocknr_set_list_init(&bset->entries);
}

/* Release the entries of a blocknr_set. */
/* Audited by: green(2002.06.11) */
reiser4_internal void
blocknr_set_destroy(blocknr_set * bset)
{
	while (!blocknr_set_list_empty(&bset->entries)) {
		bse_free(blocknr_set_list_pop_front(&bset->entries));
	}
}

/* Merge blocknr_set entries out of @from into @into. */
/* Audited by: green(2002.06.11) */
/* Auditor comments: This merge does not know if merged sets contain
   blocks pairs (As for wandered sets) or extents, so it cannot really merge
   overlapping ranges if there is some. So I believe it may lead to
   some blocks being presented several times in one blocknr_set. To help
   debugging such problems it might help to check for duplicate entries on
   actual processing of this set. Testing this kind of stuff right here is
   also complicated by the fact that these sets are not sorted and going
   through whole set on each element addition is going to be CPU-heavy task */
reiser4_internal void
blocknr_set_merge(blocknr_set * from, blocknr_set * into)
{
	blocknr_set_entry *bse_into = NULL;

	/* If @from is empty, no work to perform. */
	if (blocknr_set_list_empty(&from->entries)) {
		return;
	}

	/* If @into is not empty, try merging partial-entries. */
	if (!blocknr_set_list_empty(&into->entries)) {

		/* Neither set is empty, pop the front to members and try to combine them. */
		blocknr_set_entry *bse_from;
		unsigned into_avail;

		bse_into = blocknr_set_list_pop_front(&into->entries);
		bse_from = blocknr_set_list_pop_front(&from->entries);

		/* Combine singles. */
		for (into_avail = bse_avail(bse_into); into_avail != 0 && bse_from->nr_singles != 0; into_avail -= 1) {
			bse_put_single(bse_into, &bse_from->entries[--bse_from->nr_singles]);
		}

		/* Combine pairs. */
		for (; into_avail > 1 && bse_from->nr_pairs != 0; into_avail -= 2) {
			blocknr_pair *pair = bse_get_pair(bse_from, --bse_from->nr_pairs);
			bse_put_pair(bse_into, &pair->a, &pair->b);
		}

		/* If bse_from is empty, delete it now. */
		if (bse_avail(bse_from) == BLOCKNR_SET_ENTRIES_NUMBER) {
			bse_free(bse_from);
		} else {
			/* Otherwise, bse_into is full or nearly full (e.g.,
			   it could have one slot avail and bse_from has one
			   pair left).  Push it back onto the list.  bse_from
			   becomes bse_into, which will be the new partial. */
			blocknr_set_list_push_front(&into->entries, bse_into);
			bse_into = bse_from;
		}
	}

	/* Splice lists together. */
	blocknr_set_list_splice(&into->entries, &from->entries);

	/* Add the partial entry back to the head of the list. */
	if (bse_into != NULL) {
		blocknr_set_list_push_front(&into->entries, bse_into);
	}
}

/* Iterate over all blocknr set elements. */
reiser4_internal int
blocknr_set_iterator(txn_atom * atom, blocknr_set * bset, blocknr_set_actor_f actor, void *data, int delete)
{

	blocknr_set_entry *entry;

	assert("zam-429", atom != NULL);
	assert("zam-430", atom_is_protected(atom));
	assert("zam-431", bset != 0);
	assert("zam-432", actor != NULL);

	entry = blocknr_set_list_front(&bset->entries);
	while (!blocknr_set_list_end(&bset->entries, entry)) {
		blocknr_set_entry *tmp = blocknr_set_list_next(entry);
		unsigned int i;
		int ret;

		for (i = 0; i < entry->nr_singles; i++) {
			ret = actor(atom, &entry->entries[i], NULL, data);

			/* We can't break a loop if delete flag is set. */
			if (ret != 0 && !delete)
				return ret;
		}

		for (i = 0; i < entry->nr_pairs; i++) {
			struct blocknr_pair *ab;

			ab = bse_get_pair(entry, i);

			ret = actor(atom, &ab->a, &ab->b, data);

			if (ret != 0 && !delete)
				return ret;
		}

		if (delete) {
			blocknr_set_list_remove(entry);
			bse_free(entry);
		}

		entry = tmp;
	}

	return 0;
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
