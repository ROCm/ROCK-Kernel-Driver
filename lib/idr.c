/*
 * linux/kernel/id.c
 *
 * 2002-10-18  written by Jim Houston jim.houston@ccur.com
 *	Copyright (C) 2002 by Concurrent Computer Corporation
 *	Distributed under the GNU GPL license version 2.
 *
 * Small id to pointer translation service.  
 *
 * It uses a radix tree like structure as a sparse array indexed 
 * by the id to obtain the pointer.  The bitmap makes allocating
 * a new id quick.  

 * Modified by George Anzinger to reuse immediately and to use
 * find bit instructions.  Also removed _irq on spinlocks.

 * So here is what this bit of code does:

 * You call it to allocate an id (an int) an associate with that id a
 * pointer or what ever, we treat it as a (void *).  You can pass this
 * id to a user for him to pass back at a later time.  You then pass
 * that id to this code and it returns your pointer.

 * You can release ids at any time. When all ids are released, most of 
 * the memory is returned (we keep IDR_FREE_MAX) in a local pool so we
 * don't need to go to the memory "store" during an id allocate, just 
 * so you don't need to be too concerned about locking and conflicts
 * with the slab allocator.

 * A word on reuse.  We reuse empty id slots as soon as we can, always
 * using the lowest one available.  But we also merge a counter in the
 * high bits of the id.  The counter is RESERVED_ID_BITS (8 at this time)
 * long.  This means that if you allocate and release the same id in a 
 * loop we will reuse an id after about 256 times around the loop.  The
 * word about is used here as we will NOT return a valid id of -1 so if
 * you loop on the largest possible id (and that is 24 bits, wow!) we
 * will kick the counter to avoid -1.  (Paranoid?  You bet!)
 *
 * What you need to do is, since we don't keep the counter as part of
 * id / ptr pair, to keep a copy of it in the pointed to structure
 * (or else where) so that when you ask for a ptr you can varify that
 * the returned ptr is correct by comparing the id it contains with the one
 * you asked for.  In other words, we only did half the reuse protection.
 * Since the code depends on your code doing this check, we ignore high
 * order bits in the id, not just the count, but bits that would, if used,
 * index outside of the allocated ids.  In other words, if the largest id
 * currently allocated is 32 a look up will only look at the low 5 bits of
 * the id.  Since you will want to keep this id in the structure anyway
 * (if for no other reason than to be able to eliminate the id when the
 * structure is found in some other way) this seems reasonable.  If you
 * really think otherwise, the code to check these bits here, it is just
 * disabled with a #if 0.


 * So here are the complete details:

 *  include <linux/idr.h>

 * void idr_init(struct idr *idp)

 *   This function is use to set up the handle (idp) that you will pass
 *   to the rest of the functions.  The structure is defined in the
 *   header.

 * int idr_pre_get(struct idr *idp)

 *   This function should be called prior to locking and calling the
 *   following function.  It pre allocates enough memory to satisfy the
 *   worst possible allocation.  It can sleep, so must not be called
 *   with any spinlocks held.  If the system is REALLY out of memory
 *   this function returns 0, other wise 1.

 * int idr_get_new(struct idr *idp, void *ptr);
 
 *   This is the allocate id function.  It should be called with any
 *   required locks.  In fact, in the SMP case, you MUST lock prior to
 *   calling this function to avoid possible out of memory problems.  If
 *   memory is required, it will return a -1, in which case you should
 *   unlock and go back to the idr_pre_get() call.  ptr is the pointer
 *   you want associated with the id.  In other words:

 * void *idr_find(struct idr *idp, int id);
 
 *   returns the "ptr", given the id.  A NULL return indicates that the
 *   id is not valid (or you passed NULL in the idr_get_new(), shame on
 *   you).  This function must be called with a spinlock that prevents
 *   calling either idr_get_new() or idr_remove() or idr_find() while it
 *   is working.

 * void idr_remove(struct idr *idp, int id);

 *   removes the given id, freeing that slot and any memory that may
 *   now be unused.  See idr_find() for locking restrictions.

 */



#ifndef TEST                        // to test in user space...
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/module.h>
#endif
#include <linux/string.h>
#include <linux/idr.h>


static kmem_cache_t *idr_layer_cache;



static inline struct idr_layer *alloc_layer(struct idr *idp)
{
	struct idr_layer *p;

	spin_lock(&idp->lock);
	if (!(p = idp->id_free))
		BUG();
	idp->id_free = p->ary[0];
	idp->id_free_cnt--;
	p->ary[0] = 0;
	spin_unlock(&idp->lock);
	return(p);
}

static inline void free_layer(struct idr *idp, struct idr_layer *p)
{
	/*
	 * Depends on the return element being zeroed.
	 */
	spin_lock(&idp->lock);
	p->ary[0] = idp->id_free;
	idp->id_free = p;
	idp->id_free_cnt++;
	spin_unlock(&idp->lock);
}

int idr_pre_get(struct idr *idp)
{
	while (idp->id_free_cnt < idp->layers + 1) {
		struct idr_layer *new;
		new = kmem_cache_alloc(idr_layer_cache, GFP_KERNEL);
		if(new == NULL)
			return (0);
		free_layer(idp, new);
	}
	return 1;
}
EXPORT_SYMBOL(idr_pre_get);

static inline int sub_alloc(struct idr *idp, int shift, void *ptr)
{
	int n, v = 0;
	struct idr_layer *p;
	struct idr_layer **pa[MAX_LEVEL];
	struct idr_layer ***paa = &pa[0];
	
	*paa = NULL;
	*++paa = &idp->top;

	/*
	 * By keeping each pointer in an array we can do the 
	 * "after" recursion processing.  In this case, that means
	 * we can update the upper level bit map.
	 */
	
	while (1){
		p = **paa;
		n = ffz(p->bitmap);
		if (shift){
			/*
			 * We run around this while until we
			 * reach the leaf node...
			 */
			if (!p->ary[n]){
				/*
				 * If no node, allocate one, AFTER
				 * we insure that we will not
				 * intrude on the reserved bit field.
				 */
				if ((n << shift) >= MAX_ID_BIT)
					return -1;
				p->ary[n] = alloc_layer(idp);
				p->count++;
			}
			*++paa = &p->ary[n];
			v += (n << shift);
			shift -= IDR_BITS;
		} else {
			/*
			 * We have reached the leaf node, plant the
			 * users pointer and return the raw id.
			 */
			p->ary[n] = (struct idr_layer *)ptr;
			__set_bit(n, &p->bitmap);
			v += n;
			p->count++;
			/*
			 * This is the post recursion processing.  Once
			 * we find a bitmap that is not full we are
			 * done
			 */
			while (*(paa-1) && (**paa)->bitmap == IDR_FULL){
				n = *paa - &(**(paa-1))->ary[0];
				__set_bit(n, &(**--paa)->bitmap);
			}
			return(v);
		}
	}
}

int idr_get_new(struct idr *idp, void *ptr)
{
	int v;
	
	if (idp->id_free_cnt < idp->layers + 1) 
		return (-1);
	/*
	 * Add a new layer if the array is full 
	 */
	if (unlikely(!idp->top || idp->top->bitmap == IDR_FULL)){
		/*
		 * This is a bit different than the lower layers because
		 * we have one branch already allocated and full.
		 */
		struct idr_layer *new = alloc_layer(idp);
		new->ary[0] = idp->top;
		if ( idp->top)
			++new->count;
		idp->top = new;
		if ( idp->layers++ )
			__set_bit(0, &new->bitmap);
	}
	v = sub_alloc(idp,  (idp->layers - 1) * IDR_BITS, ptr);
	if ( likely(v >= 0 )){
		idp->count++;
		v += (idp->count << MAX_ID_SHIFT);
		if ( unlikely( v == -1 ))
		     v += (1L << MAX_ID_SHIFT);
	}
	return(v);
}
EXPORT_SYMBOL(idr_get_new);


static inline void sub_remove(struct idr *idp, int shift, int id)
{
	struct idr_layer *p = idp->top;
	struct idr_layer **pa[MAX_LEVEL];
	struct idr_layer ***paa = &pa[0];

	*paa = NULL;
	*++paa = &idp->top;

	while ((shift > 0) && p) {
		int n = (id >> shift) & IDR_MASK;
		__clear_bit(n, &p->bitmap);
		*++paa = &p->ary[n];
		p = p->ary[n];
		shift -= IDR_BITS;
	}
	if (likely(p != NULL)){
		int n = id & IDR_MASK;
		__clear_bit(n, &p->bitmap);
		p->ary[n] = NULL;
		while(*paa && ! --((**paa)->count)){
			free_layer(idp, **paa);
			**paa-- = NULL;
		}
		if ( ! *paa )
			idp->layers = 0;
	}
}
void idr_remove(struct idr *idp, int id)
{
	struct idr_layer *p;

	sub_remove(idp, (idp->layers - 1) * IDR_BITS, id);
	if ( idp->top && idp->top->count == 1 && 
	     (idp->layers > 1) &&
	     idp->top->ary[0]){  // We can drop a layer

		p = idp->top->ary[0];
		idp->top->bitmap = idp->top->count = 0;
		free_layer(idp, idp->top);
		idp->top = p;
		--idp->layers;
	}
	while (idp->id_free_cnt >= IDR_FREE_MAX) {
		
		p = alloc_layer(idp);
		kmem_cache_free(idr_layer_cache, p);
		return;
	}
}
EXPORT_SYMBOL(idr_remove);

void *idr_find(struct idr *idp, int id)
{
	int n;
	struct idr_layer *p;

	n = idp->layers * IDR_BITS;
	p = idp->top;
#if 0
	/*
	 * This tests to see if bits outside the current tree are
	 * present.  If so, tain't one of ours!
	 */
	if ( unlikely( (id & ~(~0 << MAX_ID_SHIFT)) >> (n + IDR_BITS)))
	     return NULL;
#endif
	while (n > 0 && p) {
		n -= IDR_BITS;
		p = p->ary[(id >> n) & IDR_MASK];
	}
	return((void *)p);
}
EXPORT_SYMBOL(idr_find);

static void idr_cache_ctor(void * idr_layer, 
			   kmem_cache_t *idr_layer_cache, unsigned long flags)
{
	memset(idr_layer, 0, sizeof(struct idr_layer));
}

static  int init_id_cache(void)
{
	if (!idr_layer_cache)
		idr_layer_cache = kmem_cache_create("idr_layer_cache", 
			sizeof(struct idr_layer), 0, 0, idr_cache_ctor, 0);
	return 0;
}

void idr_init(struct idr *idp)
{
	init_id_cache();
	memset(idp, 0, sizeof(struct idr));
	spin_lock_init(&idp->lock);
}
EXPORT_SYMBOL(idr_init);

