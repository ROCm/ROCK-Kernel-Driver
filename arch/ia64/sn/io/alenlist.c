/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */

/* Implementation of Address/Length Lists. */


#include <linux/types.h>
#include <linux/slab.h>
#include <asm/sn/sgi.h>
#include <asm/sn/alenlist.h>
#include <asm/sn/mmzone_sn1.h>

/*
 * Logically, an Address/Length List is a list of Pairs, where each pair
 * holds an Address and a Length, all in some Address Space.  In this
 * context, "Address Space" is a particular Crosstalk Widget address
 * space, a PCI device address space, a VME bus address space, a
 * physical memory address space, etc.
 *
 * The main use for these Lists is to provide a single mechanism that
 * describes where in an address space a DMA occurs.  This allows the
 * various I/O Bus support layers to provide a single interface for
 * DMA mapping and DMA translation without regard to how the DMA target
 * was specified by upper layers.  The upper layers commonly specify a 
 * DMA target via a buf structure page list, a kernel virtual address,
 * a user virtual address, a vector of addresses (a la uio and iov), 
 * or possibly a pfn list.
 *
 * Address/Length Lists also enable drivers to take advantage of their
 * inate scatter/gather capabilities in systems where some address
 * translation may be required between bus adapters.  The driver forms
 * a List that represents physical memory targets.  This list is passed
 * to the various adapters, which apply various translations.  The final
 * list that's returned to the driver is in terms of its local address
 * address space -- addresses which can be passed off to a scatter/gather
 * capable DMA controller.
 *
 * The current implementation is intended to be useful both in kernels
 * that support interrupt threads (INTR_KTHREAD) and in systems that do
 * not support interrupt threads.  Of course, in the latter case, some
 * interfaces can be called only within a suspendable context.
 *
 * Basic operations on Address/Length Lists include:
 *	alenlist_create		Create a list
 *	alenlist_clear		Clear a list
 *	alenlist_destroy	Destroy a list
 *	alenlist_append		Append a Pair to the end of a list
 *	alenlist_replace	Replace a Pair in the middle of a list
 *	alenlist_get		Get an Address/Length Pair from a list
 *	alenlist_size		Return the number of Pairs in a list
 *	alenlist_concat		Append one list to the end of another
 *	alenlist_clone		Create a new copy of a list
 *
 * Operations that convert from upper-level specifications to Address/
 * Length Lists currently include:
 *	kvaddr_to_alenlist	Convert from a kernel virtual address
 *	uvaddr_to_alenlist	Convert from a user virtual address
 *	buf_to_alenlist		Convert from a buf structure
 *	alenlist_done		Tell system that we're done with an alenlist
 *				obtained from a conversion.
 * Additional convenience operations:
 *	alenpair_init		Create a list and initialize it with a Pair
 *	alenpair_get		Peek at the first pair on a List
 *
 * A supporting type for Address/Length Lists is an alenlist_cursor_t.  A
 * cursor marks a position in a List, and determines which Pair is fetched
 * by alenlist_get.
 *	alenlist_cursor_create	Allocate and initialize a cursor
 *	alenlist_cursor_destroy	Free space consumed by a cursor
 *	alenlist_cursor_init	(Re-)Initialize a cursor to point 
 *				to the start of a list
 *	alenlist_cursor_clone	Clone a cursor (at the current offset)
 *	alenlist_cursor_offset	Return the number of bytes into
 *				a list that this cursor marks
 * Multiple cursors can point at various points into a List.  Also, each
 * list maintains one "internal cursor" which may be updated by alenlist_clear
 * and alenlist_get.  If calling code simply wishes to scan sequentially
 * through a list starting at the beginning, and if it is the only user of
 * a list, it can rely on this internal cursor rather than managing a 
 * separate explicit cursor.
 *
 * The current implementation allows callers to allocate both cursors and
 * the lists as local stack (structure) variables.  This allows for some
 * extra efficiency at the expense of forward binary compatibility.  It 
 * is recommended that customer drivers refrain from local allocation.
 * In fact, we likely will choose to move the structures out of the public 
 * header file into a private place in order to discourage this usage.
 *
 * Currently, no locking is provided by the alenlist implementation.
 *
 * Implementation notes:
 * For efficiency, Pairs are grouped into "chunks" of, say, 32 Pairs
 * and a List consists of some number of these chunks.  Chunks are completely
 * invisible to calling code.  Chunks should be large enough to hold most
 * standard-sized DMA's, but not so large that they consume excessive space.
 *
 * It is generally expected that Lists will be constructed at one time and
 * scanned at a later time.  It is NOT expected that drivers will scan
 * a List while the List is simultaneously extended, although this is
 * theoretically possible with sufficient upper-level locking.
 *
 * In order to support demands of Real-Time drivers and in order to support
 * swapping under low-memory conditions, we support the concept of a
 * "pre-allocated fixed-sized List".  After creating a List with 
 * alenlist_create, a driver may explicitly grow the list (via "alenlist_grow")
 * to a specific number of Address/Length pairs.  It is guaranteed that future 
 * operations involving this list will never automatically grow the list 
 * (i.e. if growth is ever required, the operation will fail).  Additionally, 
 * operations that use alenlist's (e.g. DMA operations) accept a flag which 
 * causes processing to take place "in-situ"; that is, the input alenlist 
 * entries are replaced with output alenlist entries.  The combination of 
 * pre-allocated Lists and in-situ processing allows us to avoid the 
 * potential deadlock scenario where we sleep (waiting for memory) in the 
 * swap out path.
 *
 * For debugging, we track the number of allocated Lists in alenlist_count
 * the number of allocated chunks in alenlist_chunk_count, and the number
 * of allocate cursors in alenlist_cursor_count.  We also provide a debug 
 * routine, alenlist_show, which dumps the contents of an Address/Length List.
 *
 * Currently, Lists are formed by drivers on-demand.  Eventually, we may
 * associate an alenlist with a buf structure and keep it up to date as
 * we go along.  In that case, buf_to_alenlist simply returns a pointer
 * to the existing List, and increments the Lists's reference count.
 * alenlist_done would decrement the reference count and destroys the List
 * if it was the last reference.
 *
 * Eventually alenlist's may allow better support for user-level scatter/
 * gather operations (e.g. via readv/writev):  With proper support, we
 * could potentially handle a vector of reads with a single scatter/gather
 * DMA operation.  This could be especially useful on NUMA systems where
 * there's more of a reason for users to use vector I/O operations.
 *
 * Eventually, alenlist's may replace kaio lists, vhand page lists,
 * buffer cache pfdat lists, DMA page lists, etc.
 */

/* Opaque data types */

/* An Address/Length pair.  */
typedef struct alen_s {
	alenaddr_t	al_addr;
	size_t		al_length;
} alen_t;

/* 
 * Number of elements in one chunk of an Address/Length List.
 *
 * This size should be sufficient to hold at least an "average" size
 * DMA request.  Must be at least 1, and should be a power of 2,
 * for efficiency.
 */
#define ALEN_CHUNK_SZ ((512*1024)/NBPP)

/*
 * A fixed-size set of Address/Length Pairs.  Chunks of Pairs are strung together 
 * to form a complete Address/Length List.  Chunking is entirely hidden within the 
 * alenlist implementation, and it simply makes allocation and growth of lists more 
 * efficient.
 */
typedef struct alenlist_chunk_s {
	alen_t			alc_pair[ALEN_CHUNK_SZ];/* list of addr/len pairs */
	struct alenlist_chunk_s *alc_next;		/* point to next chunk of pairs */
} *alenlist_chunk_t;

/* 
 * An Address/Length List.  An Address/Length List is allocated with alenlist_create.  
 * Alternatively, a list can be allocated on the stack (local variable of type 
 * alenlist_t) and initialized with alenpair_init or with a combination of 
 * alenlist_clear and alenlist_append, etc.  Code which statically allocates these
 * structures loses forward binary compatibility!
 *
 * A statically allocated List is sufficiently large to hold ALEN_CHUNK_SZ pairs.
 */
struct alenlist_s {
	unsigned short		al_flags;
	unsigned short		al_logical_size;	/* logical size of list, in pairs */
	unsigned short		al_actual_size;		/* actual size of list, in pairs */
	struct alenlist_chunk_s	*al_last_chunk;		/* pointer to last logical chunk */
	struct alenlist_cursor_s al_cursor;		/* internal cursor */
	struct alenlist_chunk_s	al_chunk;		/* initial set of pairs */
	alenaddr_t		al_compaction_address;	/* used to compact pairs */
};

/* al_flags field */
#define AL_FIXED_SIZE	0x1	/* List is pre-allocated, and of fixed size */


zone_t *alenlist_zone = NULL;
zone_t *alenlist_chunk_zone = NULL;
zone_t *alenlist_cursor_zone = NULL;

#if DEBUG
int alenlist_count=0;		/* Currently allocated Lists */
int alenlist_chunk_count = 0;	/* Currently allocated chunks */
int alenlist_cursor_count = 0;	/* Currently allocate cursors */
#define INCR_COUNT(ptr) atomicAddInt((ptr), 1);
#define DECR_COUNT(ptr) atomicAddInt((ptr), -1);
#else
#define INCR_COUNT(ptr)
#define DECR_COUNT(ptr)
#endif /* DEBUG */

#if DEBUG
static void alenlist_show(alenlist_t);
#endif /* DEBUG */

/*
 * Initialize Address/Length List management.  One time initialization.
 */
void
alenlist_init(void)
{
	alenlist_zone = kmem_zone_init(sizeof(struct alenlist_s), "alenlist");
	alenlist_chunk_zone = kmem_zone_init(sizeof(struct alenlist_chunk_s), "alchunk");
	alenlist_cursor_zone = kmem_zone_init(sizeof(struct alenlist_cursor_s), "alcursor");
#if DEBUG
	idbg_addfunc("alenshow", alenlist_show);
#endif /* DEBUG */
}


/*
 * Initialize an Address/Length List cursor.
 */
static void
do_cursor_init(alenlist_t alenlist, alenlist_cursor_t cursorp)
{
	cursorp->al_alenlist = alenlist;
	cursorp->al_offset = 0;
	cursorp->al_chunk = &alenlist->al_chunk;
	cursorp->al_index = 0;
	cursorp->al_bcount = 0;
}


/*
 * Create an Address/Length List, and clear it.
 * Set the cursor to the beginning.
 */
alenlist_t 
alenlist_create(unsigned flags)
{
	alenlist_t alenlist;

	alenlist = kmem_zone_alloc(alenlist_zone, flags & AL_NOSLEEP ? VM_NOSLEEP : 0);
	if (alenlist) {
		INCR_COUNT(&alenlist_count);

		alenlist->al_flags = 0;
		alenlist->al_logical_size = 0;
		alenlist->al_actual_size = ALEN_CHUNK_SZ;
		alenlist->al_last_chunk = &alenlist->al_chunk;
		alenlist->al_chunk.alc_next = NULL;
		do_cursor_init(alenlist, &alenlist->al_cursor);
	}

	return(alenlist);
}


/*
 * Grow an Address/Length List so that all resources needed to contain
 * the specified number of Pairs are pre-allocated.  An Address/Length
 * List that has been explicitly "grown" will never *automatically*
 * grow, shrink, or be destroyed.
 *
 * Pre-allocation is useful for Real-Time drivers and for drivers that
 * may be used along the swap-out path and therefore cannot afford to 
 * sleep until memory is freed.
 * 
 * The cursor is set to the beginning of the list.
 */
int
alenlist_grow(alenlist_t alenlist, size_t npairs)
{
	/* 
	 * This interface should be used relatively rarely, so
	 * the implementation is kept simple: We clear the List,
	 * then append npairs bogus entries.  Finally, we mark
	 * the list as FIXED_SIZE and re-initialize the internal
	 * cursor.
	 */

	/* 
	 * Temporarily mark as non-fixed size, since we're about
	 * to shrink and expand it.
	 */
	alenlist->al_flags &= ~AL_FIXED_SIZE;

	/* Free whatever was in the alenlist. */
	alenlist_clear(alenlist);

	/* Allocate everything that we need via automatic expansion. */
	while (npairs--)
		if (alenlist_append(alenlist, 0, 0, AL_NOCOMPACT) == ALENLIST_FAILURE)
			return(ALENLIST_FAILURE);

	/* Now, mark as FIXED_SIZE */
	alenlist->al_flags |= AL_FIXED_SIZE;

	/* Clear out bogus entries */
	alenlist_clear(alenlist);

	/* Initialize internal cursor to the beginning */
	do_cursor_init(alenlist, &alenlist->al_cursor);

	return(ALENLIST_SUCCESS);
}


/*
 * Clear an Address/Length List so that it holds no pairs.
 */
void
alenlist_clear(alenlist_t alenlist)
{
	alenlist_chunk_t chunk, freechunk;

	/*
	 * If this List is not FIXED_SIZE, free all the
	 * extra chunks.
	 */
	if (!(alenlist->al_flags & AL_FIXED_SIZE)) {
		/* First, free any extension alenlist chunks */
		chunk = alenlist->al_chunk.alc_next;
		while (chunk) {
			freechunk = chunk;
			chunk = chunk->alc_next;
			kmem_zone_free(alenlist_chunk_zone, freechunk);
			DECR_COUNT(&alenlist_chunk_count);
		}
		alenlist->al_actual_size = ALEN_CHUNK_SZ;
		alenlist->al_chunk.alc_next = NULL;
	}

	alenlist->al_logical_size = 0;
	alenlist->al_last_chunk = &alenlist->al_chunk;
	do_cursor_init(alenlist, &alenlist->al_cursor);
}


/*
 * Create and initialize an Address/Length Pair.
 * This is intended for degenerate lists, consisting of a single 
 * address/length pair.
 */
alenlist_t
alenpair_init(	alenaddr_t address, 
		size_t length)
{
	alenlist_t alenlist;

	alenlist = alenlist_create(0);

	alenlist->al_logical_size = 1;
	ASSERT(alenlist->al_last_chunk == &alenlist->al_chunk);
	alenlist->al_chunk.alc_pair[0].al_length = length;
	alenlist->al_chunk.alc_pair[0].al_addr = address;

	return(alenlist);
}

/*
 * Return address/length from a degenerate (1-pair) List, or
 * first pair from a larger list.  Does NOT update the internal cursor,
 * so this is an easy way to peek at a start address.
 */
int
alenpair_get(	alenlist_t alenlist,
		alenaddr_t *address,
		size_t *length)
{
	if (alenlist->al_logical_size == 0)
		return(ALENLIST_FAILURE);

	*length = alenlist->al_chunk.alc_pair[0].al_length;
	*address = alenlist->al_chunk.alc_pair[0].al_addr;
	return(ALENLIST_SUCCESS);
}


/*
 * Destroy an Address/Length List.
 */
void 
alenlist_destroy(alenlist_t alenlist)
{
	if (alenlist == NULL)
		return;

	/* 
	 * Turn off FIXED_SIZE so this List can be 
	 * automatically shrunk.
	 */
	alenlist->al_flags &= ~AL_FIXED_SIZE;

	/* Free extension chunks first */
	if (alenlist->al_chunk.alc_next)
		alenlist_clear(alenlist);

	/* Now, free the alenlist itself */
	kmem_zone_free(alenlist_zone, alenlist);
	DECR_COUNT(&alenlist_count);
}

/*
 * Release an Address/Length List.
 * This is in preparation for a day when alenlist's may be longer-lived, and
 * perhaps associated with a buf structure.  We'd add a reference count, and
 * this routine would decrement the count.  For now, we create alenlist's on
 * on demand and free them when done.  If the driver is not explicitly managing
 * a List for its own use, it should call alenlist_done rather than alenlist_destroy.
 */
void
alenlist_done(alenlist_t alenlist)
{
	alenlist_destroy(alenlist);
}


/*
 * Append another address/length to the end of an Address/Length List,
 * growing the list if permitted and necessary.
 *
 * Returns: SUCCESS/FAILURE
 */
int 
alenlist_append(	alenlist_t alenlist, 		/* append to this list */
			alenaddr_t address, 		/* address to append */
			size_t length,			/* length to append */
			unsigned flags)
{
	alen_t *alenp;
	int index, last_index;

	index = alenlist->al_logical_size % ALEN_CHUNK_SZ;

	if ((alenlist->al_logical_size > 0)) {
		/*
		 * See if we can compact this new pair in with the previous entry.
		 * al_compaction_address holds that value that we'd need to see
		 * in order to compact.
		 */
		if (!(flags & AL_NOCOMPACT) &&
		    (alenlist->al_compaction_address == address)) {
			last_index = (alenlist->al_logical_size-1) % ALEN_CHUNK_SZ;
			alenp = &(alenlist->al_last_chunk->alc_pair[last_index]);
			alenp->al_length += length;
			alenlist->al_compaction_address += length;
			return(ALENLIST_SUCCESS);
		}

		/*
		 * If we're out of room in this chunk, move to a new chunk.
	 	 */
		if (index == 0) {
			if (alenlist->al_flags & AL_FIXED_SIZE) {
				alenlist->al_last_chunk = alenlist->al_last_chunk->alc_next;

				/* If we're out of space in a FIXED_SIZE List, quit. */
				if (alenlist->al_last_chunk == NULL) {
					ASSERT(alenlist->al_logical_size == alenlist->al_actual_size);
					return(ALENLIST_FAILURE);
				}
			} else {
				alenlist_chunk_t new_chunk;

				new_chunk = kmem_zone_alloc(alenlist_chunk_zone, 
							flags & AL_NOSLEEP ? VM_NOSLEEP : 0);

				if (new_chunk == NULL)
					return(ALENLIST_FAILURE);

				alenlist->al_last_chunk->alc_next = new_chunk;
				new_chunk->alc_next = NULL;
				alenlist->al_last_chunk = new_chunk;
				alenlist->al_actual_size += ALEN_CHUNK_SZ;
				INCR_COUNT(&alenlist_chunk_count);
			}
		}
	}

	alenp = &(alenlist->al_last_chunk->alc_pair[index]);
	alenp->al_addr = address;
	alenp->al_length = length;
	
	alenlist->al_logical_size++;
	alenlist->al_compaction_address = address + length;

	return(ALENLIST_SUCCESS);
}


/*
 * Replace an item in an Address/Length List.  Cursor is updated so
 * that alenlist_get will get the next item in the list.  This interface 
 * is not very useful for drivers; but it is useful to bus providers 
 * that need to translate between address spaced in situ.  The old Address
 * and Length are returned.
 */
/* ARGSUSED */
int
alenlist_replace(	alenlist_t alenlist, 		/* in: replace in this list */
			alenlist_cursor_t cursorp, 	/* inout: which item to replace */
			alenaddr_t *addrp, 		/* inout: address */
			size_t *lengthp,		/* inout: length */
			unsigned flags)
{
	alen_t *alenp;
	alenlist_chunk_t chunk;
	unsigned int index;
	size_t length;
	alenaddr_t addr;

	if ((addrp == NULL) || (lengthp == NULL))
		return(ALENLIST_FAILURE);

	if (alenlist->al_logical_size == 0)
		return(ALENLIST_FAILURE);

	addr = *addrp;
	length = *lengthp;

	/* 
	 * If no cursor explicitly specified, use the Address/Length List's 
	 * internal cursor.
	 */
	if (cursorp == NULL)
		cursorp = &alenlist->al_cursor;

	chunk = cursorp->al_chunk;
	index = cursorp->al_index;

	ASSERT(cursorp->al_alenlist == alenlist);
	if (cursorp->al_alenlist != alenlist)
		return(ALENLIST_FAILURE);

	alenp = &chunk->alc_pair[index];

	/* Return old values */
	*addrp = alenp->al_length;
	*lengthp = alenp->al_addr;

	/* Set up new values */
	alenp->al_length = length;
	alenp->al_addr = addr;

	/* Update cursor to point to next item */
	cursorp->al_bcount = length;

	return(ALENLIST_SUCCESS);
}


/*
 * Initialize a cursor in order to walk an alenlist.
 * An alenlist_cursor always points to the last thing that was obtained
 * from the list.  If al_chunk is NULL, then nothing has yet been obtained.
 *
 * Note: There is an "internal cursor" associated with every Address/Length List.
 * For users that scan sequentially through a List, it is more efficient to
 * simply use the internal cursor.  The caller must insure that no other users
 * will simultaneously scan the List.  The caller can reposition the internal
 * cursor by calling alenlist_cursor_init with a NULL cursorp.
 */
int
alenlist_cursor_init(alenlist_t alenlist, size_t offset, alenlist_cursor_t cursorp)
{
	size_t byte_count;

	if (cursorp == NULL)
		cursorp = &alenlist->al_cursor;

	/* Get internal cursor's byte count for use as a hint.
	 *
	 * If the internal cursor points passed the point that we're interested in,
	 * we need to seek forward from the beginning.  Otherwise, we can seek forward
	 * from the internal cursor.
	 */
	if ((offset > 0) &&
	   ((byte_count = alenlist_cursor_offset(alenlist, (alenlist_cursor_t)NULL)) <= offset)) {
		offset -= byte_count;
		alenlist_cursor_clone(alenlist, NULL, cursorp);
	} else
		do_cursor_init(alenlist, cursorp);

	/* We could easily speed this up, but it shouldn't be used very often. */
	while (offset != 0) {
		alenaddr_t addr;
		size_t length;

		if (alenlist_get(alenlist, cursorp, offset, &addr, &length, 0) != ALENLIST_SUCCESS)
			return(ALENLIST_FAILURE);
		offset -= length;
	}
	return(ALENLIST_SUCCESS);
}


/*
 * Copy a cursor.  The source cursor is either an internal alenlist cursor
 * or an explicit cursor.
 */
int
alenlist_cursor_clone(	alenlist_t alenlist, 
			alenlist_cursor_t cursorp_in, 
			alenlist_cursor_t cursorp_out)
{
	ASSERT(cursorp_out);

	if (alenlist && cursorp_in)
		if (alenlist != cursorp_in->al_alenlist)
			return(ALENLIST_FAILURE);

	if (alenlist)
		*cursorp_out = alenlist->al_cursor; /* small structure copy */
	else if (cursorp_in)
		*cursorp_out = *cursorp_in; /* small structure copy */
	else
		return(ALENLIST_FAILURE); /* no source */

	return(ALENLIST_SUCCESS);
}

/*
 * Return the number of bytes passed so far according to the specified cursor.
 * If cursorp is NULL, use the alenlist's internal cursor.
 */
size_t
alenlist_cursor_offset(alenlist_t alenlist, alenlist_cursor_t cursorp)
{
	ASSERT(!alenlist || !cursorp || (alenlist == cursorp->al_alenlist));

	if (cursorp == NULL) {
		ASSERT(alenlist);
		cursorp = &alenlist->al_cursor;
	}

	return(cursorp->al_offset);
}

/*
 * Allocate and initialize an Address/Length List cursor.
 */
alenlist_cursor_t
alenlist_cursor_create(alenlist_t alenlist, unsigned flags)
{
	alenlist_cursor_t cursorp;

	ASSERT(alenlist != NULL);
	cursorp = kmem_zone_alloc(alenlist_cursor_zone, flags & AL_NOSLEEP ? VM_NOSLEEP : 0);
	if (cursorp) {
		INCR_COUNT(&alenlist_cursor_count);
		alenlist_cursor_init(alenlist, 0, cursorp);
	}
	return(cursorp);
}

/*
 * Free an Address/Length List cursor.
 */
void
alenlist_cursor_destroy(alenlist_cursor_t cursorp)
{
	DECR_COUNT(&alenlist_cursor_count);
	kmem_zone_free(alenlist_cursor_zone, cursorp);
}


/*
 * Fetch an address/length pair from an Address/Length List.  Update
 * the "cursor" so that next time this routine is called, we'll get
 * the next address range.  Never return a length that exceeds maxlength
 * (if non-zero).  If maxlength is a power of 2, never return a length 
 * that crosses a maxlength boundary.  [This may seem strange at first,
 * but it's what many drivers want.]
 *
 * Returns: SUCCESS/FAILURE
 */
int
alenlist_get(	alenlist_t alenlist, 		/* in: get from this list */
		alenlist_cursor_t cursorp, 	/* inout: which item to get */
		size_t	maxlength,		/* in: at most this length */
		alenaddr_t *addrp, 		/* out: address */
		size_t *lengthp,		/* out: length */
		unsigned flags)
{
	alen_t *alenp;
	alenlist_chunk_t chunk;
	unsigned int index;
	size_t bcount;
	size_t length;

	/* 
	 * If no cursor explicitly specified, use the Address/Length List's 
	 * internal cursor.
	 */
	if (cursorp == NULL) {
		if (alenlist->al_logical_size == 0)
			return(ALENLIST_FAILURE);
		cursorp = &alenlist->al_cursor;
	}

	chunk = cursorp->al_chunk;
	index = cursorp->al_index;
	bcount = cursorp->al_bcount;

	ASSERT(cursorp->al_alenlist == alenlist);
	if (cursorp->al_alenlist != alenlist)
		return(ALENLIST_FAILURE);

	alenp = &chunk->alc_pair[index];
	length = alenp->al_length - bcount;

	/* Bump up to next pair, if we're done with this pair. */
	if (length == 0) {
		cursorp->al_bcount = bcount = 0;
		cursorp->al_index = index = (index + 1) % ALEN_CHUNK_SZ;

		/* Bump up to next chunk, if we're done with this chunk. */
		if (index == 0) {
			if (cursorp->al_chunk == alenlist->al_last_chunk)
				return(ALENLIST_FAILURE);
			chunk = chunk->alc_next;
			ASSERT(chunk != NULL);
		} else {
			/* If in last chunk, don't go beyond end. */
			if (cursorp->al_chunk == alenlist->al_last_chunk) {
				int last_size = alenlist->al_logical_size % ALEN_CHUNK_SZ;
				if (last_size && (index >= last_size))
					return(ALENLIST_FAILURE);
			}
		}

		alenp = &chunk->alc_pair[index];
		length = alenp->al_length;
	}

	/* Constrain what we return according to maxlength */
	if (maxlength) {
		size_t maxlen1 = maxlength - 1;

		if ((maxlength & maxlen1) == 0) /* power of 2 */
			maxlength -= 
			   ((alenp->al_addr + cursorp->al_bcount) & maxlen1);

		length = MIN(maxlength, length);
	}

	/* Update the cursor, if desired. */
	if (!(flags & AL_LEAVE_CURSOR)) {
		cursorp->al_bcount += length;
		cursorp->al_chunk = chunk;
	}

	*lengthp = length;
	*addrp = alenp->al_addr + bcount;

	return(ALENLIST_SUCCESS);
}


/*
 * Return the number of pairs in the specified Address/Length List.
 * (For FIXED_SIZE Lists, this returns the logical size of the List, 
 * not the actual capacity of the List.)
 */
int
alenlist_size(alenlist_t alenlist)
{
	return(alenlist->al_logical_size);
}


/*
 * Concatenate two Address/Length Lists.
 */
void
alenlist_concat(alenlist_t from,
		alenlist_t to)
{
	struct alenlist_cursor_s cursor;
	alenaddr_t addr;
	size_t length;

	alenlist_cursor_init(from, 0, &cursor);

	while(alenlist_get(from, &cursor, (size_t)0, &addr, &length, 0) == ALENLIST_SUCCESS)
		alenlist_append(to, addr, length, 0);
}

/*
 * Create a copy of a list.
 * (Not all attributes of the old list are cloned.  For instance, if
 * a FIXED_SIZE list is cloned, the resulting list is NOT FIXED_SIZE.)
 */
alenlist_t
alenlist_clone(alenlist_t old_list, unsigned flags)
{
	alenlist_t new_list;

	new_list = alenlist_create(flags);
	if (new_list != NULL)
		alenlist_concat(old_list, new_list);

	return(new_list);
}


/* 
 * Convert a kernel virtual address to a Physical Address/Length List.
 */
alenlist_t
kvaddr_to_alenlist(alenlist_t alenlist, caddr_t kvaddr, size_t length, unsigned flags)
{
	alenaddr_t paddr;
	long offset;
	size_t piece_length;
	int created_alenlist;

	if (length <=0)
		return(NULL);

	/* If caller supplied a List, use it.  Otherwise, allocate one. */
	if (alenlist == NULL) {
		alenlist = alenlist_create(0);
		created_alenlist = 1;
	} else {
		alenlist_clear(alenlist);
		created_alenlist = 0;
	}

	paddr = kvtophys(kvaddr);
	offset = poff(kvaddr);

	/* Handle first page */
	piece_length = MIN(NBPP - offset, length);
	if (alenlist_append(alenlist, paddr, piece_length, flags) == ALENLIST_FAILURE)
		goto failure;
	length -= piece_length;
	kvaddr += piece_length;

	/* Handle middle pages */
	while (length >= NBPP) {
		paddr = kvtophys(kvaddr);
		if (alenlist_append(alenlist, paddr, NBPP, flags) == ALENLIST_FAILURE)
			goto failure;
		length -= NBPP;
		kvaddr += NBPP;
	}

	/* Handle last page */
	if (length) {
		ASSERT(length < NBPP);
		paddr = kvtophys(kvaddr);
		if (alenlist_append(alenlist, paddr, length, flags) == ALENLIST_FAILURE)
			goto failure;
	}

	alenlist_cursor_init(alenlist, 0, NULL);
	return(alenlist);

failure:
	if (created_alenlist)
		alenlist_destroy(alenlist);
	return(NULL);
}


#if DEBUG
static void
alenlist_show(alenlist_t alenlist)
{
	struct alenlist_cursor_s cursor;
	alenaddr_t addr;
	size_t length;
	int i = 0;

	alenlist_cursor_init(alenlist, 0, &cursor);

	qprintf("Address/Length List@0x%x:\n", alenlist);
	qprintf("logical size=0x%x actual size=0x%x last_chunk at 0x%x\n", 
		alenlist->al_logical_size, alenlist->al_actual_size, 
		alenlist->al_last_chunk);
	qprintf("cursor: chunk=0x%x index=%d offset=0x%x\n",
		alenlist->al_cursor.al_chunk, 
		alenlist->al_cursor.al_index,
		alenlist->al_cursor.al_bcount);
	while(alenlist_get(alenlist, &cursor, (size_t)0, &addr, &length, 0) == ALENLIST_SUCCESS)
		qprintf("%d:\t0x%lx 0x%lx\n", ++i, addr, length);
}
#endif /* DEBUG */
