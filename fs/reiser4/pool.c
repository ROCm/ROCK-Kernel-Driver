/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by
 * reiser4/README */

/* Fast pool allocation.

   There are situations when some sub-system normally asks memory allocator
   for only few objects, but under some circumstances could require much
   more. Typical and actually motivating example is tree balancing. It needs
   to keep track of nodes that were involved into it, and it is well-known
   that in reasonable packed balanced tree most (92.938121%) percent of all
   balancings end up after working with only few nodes (3.141592 on
   average). But in rare cases balancing can involve much more nodes
   (3*tree_height+1 in extremal situation).

   On the one hand, we don't want to resort to dynamic allocation (slab,
    malloc(), etc.) to allocate data structures required to keep track of
   nodes during balancing. On the other hand, we cannot statically allocate
   required amount of space on the stack, because first: it is useless wastage
   of precious resource, and second: this amount is unknown in advance (tree
   height can change).

   Pools, implemented in this file are solution for this problem:

    - some configurable amount of objects is statically preallocated on the
    stack

    - if this preallocated pool is exhausted and more objects is requested
    they are allocated dynamically.

   Pools encapsulate distinction between statically and dynamically allocated
   objects. Both allocation and recycling look exactly the same.

   To keep track of dynamically allocated objects, pool adds its own linkage
   to each object.

   NOTE-NIKITA This linkage also contains some balancing-specific data. This
   is not perfect. On the other hand, balancing is currently the only client
   of pool code.

   NOTE-NIKITA Another desirable feature is to rewrite all pool manipulation
   functions in the style of tslist/tshash, i.e., make them unreadable, but
   type-safe.


*/

#include "debug.h"
#include "pool.h"
#include "super.h"

#include <linux/types.h>
#include <linux/err.h>

/* initialise new pool object */
static void
reiser4_init_pool_obj(reiser4_pool_header * h	/* pool object to
						 * initialise */ )
{
	pool_usage_list_clean(h);
	pool_level_list_clean(h);
	pool_extra_list_clean(h);
}

/* initialise new pool */
reiser4_internal void
reiser4_init_pool(reiser4_pool * pool /* pool to initialise */ ,
		  size_t obj_size /* size of objects in @pool */ ,
		  int num_of_objs /* number of preallocated objects */ ,
		  char *data /* area for preallocated objects */ )
{
	reiser4_pool_header *h;
	int i;

	assert("nikita-955", pool != NULL);
	assert("nikita-1044", obj_size > 0);
	assert("nikita-956", num_of_objs >= 0);
	assert("nikita-957", data != NULL);

	xmemset(pool, 0, sizeof *pool);
	pool->obj_size = obj_size;
	pool->data = data;
	pool_usage_list_init(&pool->free);
	pool_usage_list_init(&pool->used);
	pool_extra_list_init(&pool->extra);
	xmemset(data, 0, obj_size * num_of_objs);
	for (i = 0; i < num_of_objs; ++i) {
		h = (reiser4_pool_header *) (data + i * obj_size);
		reiser4_init_pool_obj(h);
		pool_usage_list_push_back(&pool->free, h);
	}
}

/* release pool resources

   Release all resources acquired by this pool, specifically, dynamically
   allocated objects.

*/
reiser4_internal void
reiser4_done_pool(reiser4_pool * pool UNUSED_ARG /* pool to destroy */ )
{
}

/* allocate carry object from pool

   First, try to get preallocated object. If this fails, resort to dynamic
   allocation.

*/
reiser4_internal void *
reiser4_pool_alloc(reiser4_pool * pool	/* pool to allocate object
					 * from */ )
{
	reiser4_pool_header *result;

	assert("nikita-959", pool != NULL);
	trace_stamp(TRACE_CARRY);
	reiser4_stat_inc(pool.alloc);

	if (!pool_usage_list_empty(&pool->free)) {
		result = pool_usage_list_pop_front(&pool->free);
		pool_usage_list_clean(result);
		assert("nikita-965", pool_extra_list_is_clean(result));
	} else {
		reiser4_stat_inc(pool.kmalloc);
		/* pool is empty. Extra allocations don't deserve dedicated
		   slab to be served from, as they are expected to be rare. */
		result = reiser4_kmalloc(pool->obj_size, GFP_KERNEL);
		if (result != 0) {
			reiser4_init_pool_obj(result);
			pool_extra_list_push_front(&pool->extra, result);
		} else
			return ERR_PTR(RETERR(-ENOMEM));
	}
	++pool->objs;
	pool_level_list_clean(result);
	pool_usage_list_push_front(&pool->used, result);
	xmemset(result + 1, 0, pool->obj_size - sizeof *result);
	return result;
}

/* return object back to the pool */
reiser4_internal void
reiser4_pool_free(reiser4_pool * pool,
		  reiser4_pool_header * h	/* pool to return object back
						 * into */ )
{
	assert("nikita-961", h != NULL);
	assert("nikita-962", pool != NULL);
	trace_stamp(TRACE_CARRY);

	-- pool->objs;
	assert("nikita-963", pool->objs >= 0);

	pool_usage_list_remove_clean(h);
	pool_level_list_remove_clean(h);
	if (pool_extra_list_is_clean(h))
		pool_usage_list_push_front(&pool->free, h);
	else {
		pool_extra_list_remove_clean(h);
		reiser4_kfree(h);
	}
}

/* add new object to the carry level list

   Carry level is FIFO most of the time, but not always. Complications arise
   when make_space() function tries to go to the left neighbor and thus adds
   carry node before existing nodes, and also, when updating delimiting keys
   after moving data between two nodes, we want left node to be locked before
   right node.

   Latter case is confusing at the first glance. Problem is that COP_UPDATE
   opration that updates delimiting keys is sometimes called with two nodes
   (when data are moved between two nodes) and sometimes with only one node
   (when leftmost item is deleted in a node). In any case operation is
   supplied with at least node whose left delimiting key is to be updated
   (that is "right" node).

*/
reiser4_internal reiser4_pool_header *
add_obj(reiser4_pool * pool	/* pool from which to
				 * allocate new object */ ,
	pool_level_list_head * list	/* list where to add
					 * object */ ,
	pool_ordering order /* where to add */ ,
	reiser4_pool_header * reference	/* after (or
					 * before) which
					 * existing
					 * object to
					 * add */ )
{
	reiser4_pool_header *result;

	assert("nikita-972", pool != NULL);

	trace_stamp(TRACE_CARRY);

	result = reiser4_pool_alloc(pool);
	if (IS_ERR(result))
		return result;

	assert("nikita-973", result != NULL);

	switch (order) {
	case POOLO_BEFORE:
		pool_level_list_insert_before(reference, result);
		break;
	case POOLO_AFTER:
		pool_level_list_insert_after(reference, result);
		break;
	case POOLO_LAST:
		pool_level_list_push_back(list, result);
		break;
	case POOLO_FIRST:
		pool_level_list_push_front(list, result);
		break;
	default:
		wrong_return_value("nikita-927", "order");
	}
	return result;
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
