/*
 *  linux/mm/mempool.c
 *
 *  memory buffer pool support. Such pools are mostly used
 *  for guaranteed, deadlock-free memory allocations during
 *  extreme VM load.
 *
 *  started by Ingo Molnar, Copyright (C) 2001
 */

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/mempool.h>

/**
 * mempool_create - create a memory pool
 * @min_nr:    the minimum number of elements guaranteed to be
 *             allocated for this pool.
 * @alloc_fn:  user-defined element-allocation function.
 * @free_fn:   user-defined element-freeing function.
 * @pool_data: optional private data available to the user-defined functions.
 *
 * this function creates and allocates a guaranteed size, preallocated
 * memory pool. The pool can be used from the mempool_alloc and mempool_free
 * functions. This function might sleep. Both the alloc_fn() and the free_fn()
 * functions might sleep - as long as the mempool_alloc function is not called
 * from IRQ contexts. The element allocated by alloc_fn() must be able to
 * hold a struct list_head. (8 bytes on x86.)
 */
mempool_t * mempool_create(int min_nr, mempool_alloc_t *alloc_fn,
				mempool_free_t *free_fn, void *pool_data)
{
	mempool_t *pool;
	int i;

	pool = kmalloc(sizeof(*pool), GFP_KERNEL);
	if (!pool)
		return NULL;
	memset(pool, 0, sizeof(*pool));

	spin_lock_init(&pool->lock);
	pool->min_nr = min_nr;
	pool->pool_data = pool_data;
	INIT_LIST_HEAD(&pool->elements);
	init_waitqueue_head(&pool->wait);
	pool->alloc = alloc_fn;
	pool->free = free_fn;

	/*
	 * First pre-allocate the guaranteed number of buffers.
	 */
	for (i = 0; i < min_nr; i++) {
		void *element;
		struct list_head *tmp;
		element = pool->alloc(GFP_KERNEL, pool->pool_data);

		if (unlikely(!element)) {
			/*
			 * Not enough memory - free the allocated ones
			 * and return:
			 */
			list_for_each(tmp, &pool->elements) {
				element = tmp;
				pool->free(element, pool->pool_data);
			}
			kfree(pool);

			return NULL;
		}
		tmp = element;
		list_add(tmp, &pool->elements);
		pool->curr_nr++;
	}
	return pool;
}

/**
 * mempool_resize - resize an existing memory pool
 * @pool:       pointer to the memory pool which was allocated via
 *              mempool_create().
 * @new_min_nr: the new minimum number of elements guaranteed to be
 *              allocated for this pool.
 * @gfp_mask:   the usual allocation bitmask.
 *
 * This function shrinks/grows the pool. In the case of growing,
 * it cannot be guaranteed that the pool will be grown to the new
 * size immediately, but new mempool_free() calls will refill it.
 *
 * Note, the caller must guarantee that no mempool_destroy is called
 * while this function is running. mempool_alloc() & mempool_free()
 * might be called (eg. from IRQ contexts) while this function executes.
 */
void mempool_resize(mempool_t *pool, int new_min_nr, int gfp_mask)
{
	int delta;
	void *element;
	unsigned long flags;
	struct list_head *tmp;

	if (new_min_nr <= 0)
		BUG();

	spin_lock_irqsave(&pool->lock, flags);
	if (new_min_nr < pool->min_nr) {
		pool->min_nr = new_min_nr;
		/*
		 * Free possible excess elements.
		 */
		while (pool->curr_nr > pool->min_nr) {
			tmp = pool->elements.next;
			if (tmp == &pool->elements)
				BUG();
			list_del(tmp);
			element = tmp;
			pool->curr_nr--;
			spin_unlock_irqrestore(&pool->lock, flags);

			pool->free(element, pool->pool_data);

			spin_lock_irqsave(&pool->lock, flags);
		}
		spin_unlock_irqrestore(&pool->lock, flags);
		return;
	}
	delta = new_min_nr - pool->min_nr;
	pool->min_nr = new_min_nr;
	spin_unlock_irqrestore(&pool->lock, flags);

	/*
	 * We refill the pool up to the new treshold - but we dont
	 * (cannot) guarantee that the refill succeeds.
	 */
	while (delta) {
		element = pool->alloc(gfp_mask, pool->pool_data);
		if (!element)
			break;
		mempool_free(element, pool);
		delta--;
	}
}

/**
 * mempool_destroy - deallocate a memory pool
 * @pool:      pointer to the memory pool which was allocated via
 *             mempool_create().
 *
 * this function only sleeps if the free_fn() function sleeps. The caller
 * has to guarantee that no mempool_alloc() nor mempool_free() happens in
 * this pool when calling this function.
 */
void mempool_destroy(mempool_t *pool)
{
	void *element;
	struct list_head *head, *tmp;

	if (!pool)
		return;

	head = &pool->elements;
	for (tmp = head->next; tmp != head; ) {
		element = tmp;
		tmp = tmp->next;
		pool->free(element, pool->pool_data);
		pool->curr_nr--;
	}
	if (pool->curr_nr)
		BUG();
	kfree(pool);
}

/**
 * mempool_alloc - allocate an element from a specific memory pool
 * @pool:      pointer to the memory pool which was allocated via
 *             mempool_create().
 * @gfp_mask:  the usual allocation bitmask.
 *
 * this function only sleeps if the alloc_fn function sleeps or
 * returns NULL. Note that due to preallocation, this function
 * *never* fails when called from process contexts. (it might
 * fail if called from an IRQ context.)
 */
void * mempool_alloc(mempool_t *pool, int gfp_mask)
{
	void *element;
	unsigned long flags;
	struct list_head *tmp;
	int curr_nr;
	DECLARE_WAITQUEUE(wait, current);
	int gfp_nowait = gfp_mask & ~(__GFP_WAIT | __GFP_IO);

repeat_alloc:
	element = pool->alloc(gfp_nowait, pool->pool_data);
	if (likely(element != NULL))
		return element;

	/*
	 * If the pool is less than 50% full then try harder
	 * to allocate an element:
	 */
	if ((gfp_mask != gfp_nowait) && (pool->curr_nr <= pool->min_nr/2)) {
		element = pool->alloc(gfp_mask, pool->pool_data);
		if (likely(element != NULL))
			return element;
	}

	/*
	 * Kick the VM at this point.
	 */
	wakeup_bdflush();

	spin_lock_irqsave(&pool->lock, flags);
	if (likely(pool->curr_nr)) {
		tmp = pool->elements.next;
		list_del(tmp);
		element = tmp;
		pool->curr_nr--;
		spin_unlock_irqrestore(&pool->lock, flags);
		return element;
	}
	spin_unlock_irqrestore(&pool->lock, flags);

	/* We must not sleep in the GFP_ATOMIC case */
	if (gfp_mask == gfp_nowait)
		return NULL;

	run_task_queue(&tq_disk);

	add_wait_queue_exclusive(&pool->wait, &wait);
	set_task_state(current, TASK_UNINTERRUPTIBLE);

	spin_lock_irqsave(&pool->lock, flags);
	curr_nr = pool->curr_nr;
	spin_unlock_irqrestore(&pool->lock, flags);

	if (!curr_nr)
		schedule();

	current->state = TASK_RUNNING;
	remove_wait_queue(&pool->wait, &wait);

	goto repeat_alloc;
}

/**
 * mempool_free - return an element to the pool.
 * @element:   pool element pointer.
 * @pool:      pointer to the memory pool which was allocated via
 *             mempool_create().
 *
 * this function only sleeps if the free_fn() function sleeps.
 */
void mempool_free(void *element, mempool_t *pool)
{
	unsigned long flags;

	if (pool->curr_nr < pool->min_nr) {
		spin_lock_irqsave(&pool->lock, flags);
		if (pool->curr_nr < pool->min_nr) {
			list_add(element, &pool->elements);
			pool->curr_nr++;
			spin_unlock_irqrestore(&pool->lock, flags);
			wake_up(&pool->wait);
			return;
		}
		spin_unlock_irqrestore(&pool->lock, flags);
	}
	pool->free(element, pool->pool_data);
}

EXPORT_SYMBOL(mempool_create);
EXPORT_SYMBOL(mempool_resize);
EXPORT_SYMBOL(mempool_destroy);
EXPORT_SYMBOL(mempool_alloc);
EXPORT_SYMBOL(mempool_free);

