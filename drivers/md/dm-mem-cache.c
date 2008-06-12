/*
 * Copyright (C) 2006,2007 Red Hat GmbH
 *
 * Module Author: Heinz Mauelshagen <Mauelshagen@RedHat.de>
 *
 * Allocate/free total_pages to a per client page pool.
 * Allocate/free memory objects with chunks (1..n) of pages_per_chunk pages
 * hanging off.
 *
 * This file is released under the GPL.
 */

#define	DM_MEM_CACHE_VERSION	"0.2"

#include "dm.h"
#include <linux/dm-io.h>
#include "dm-mem-cache.h"

struct dm_mem_cache_client {
	spinlock_t lock;
	mempool_t *objs_pool;
	struct page_list *free_list;
	unsigned objects;
	unsigned chunks;
	unsigned free_pages;
	unsigned total_pages;
};

/*
 * Free pages and page_list elements of client.
 */
static void free_cache_pages(struct page_list *list)
{
	while (list) {
		struct page_list *pl = list;

		list = pl->next;
		BUG_ON(!pl->page);
		__free_page(pl->page);
		kfree(pl);
	}
}

/*
 * Alloc number of pages and page_list elements as required by client.
 */
static struct page_list *alloc_cache_pages(unsigned pages)
{
	struct page_list *pl, *ret = NULL;
	struct page *page;

	while (pages--) {
		page = alloc_page(GFP_NOIO);
		if (!page)
			goto err;

		pl = kmalloc(sizeof(*pl), GFP_NOIO);
		if (!pl) {
			__free_page(page);
			goto err;
		}

		pl->page = page;
		pl->next = ret;
		ret = pl;
	}

	return ret;

   err:
	free_cache_pages(ret);
	return NULL;
}

/*
 * Allocate page_list elements from the pool to chunks of the mem object
 */
static void alloc_chunks(struct dm_mem_cache_client *cl,
			 struct dm_mem_cache_object *obj,
			 unsigned pages_per_chunk)
{
	unsigned chunks = cl->chunks;
	unsigned long flags;

	local_irq_save(flags);
	local_irq_disable();
	while (chunks--) {
		unsigned p = pages_per_chunk;

		obj[chunks].pl = NULL;

		while (p--) {
			struct page_list *pl;

			/* Take next element from free list */
			spin_lock(&cl->lock);
			pl = cl->free_list;
			BUG_ON(!pl);
			cl->free_list = pl->next;
			spin_unlock(&cl->lock);

			pl->next = obj[chunks].pl;
			obj[chunks].pl = pl;
		}
	}

	local_irq_restore(flags);
}

/*
 * Free page_list elements putting them back onto free list
 */
static void free_chunks(struct dm_mem_cache_client *cl,
			struct dm_mem_cache_object *obj)
{
	unsigned chunks = cl->chunks;
	unsigned long flags;
	struct page_list *next, *pl;

	local_irq_save(flags);
	local_irq_disable();
	while (chunks--) {
		for (pl = obj[chunks].pl; pl; pl = next) {
			next = pl->next;

			spin_lock(&cl->lock);
			pl->next = cl->free_list;
			cl->free_list = pl;
			cl->free_pages++;
			spin_unlock(&cl->lock);
		}
	}

	local_irq_restore(flags);
}

/*
 * Create/destroy dm memory cache client resources.
 */
struct dm_mem_cache_client *
dm_mem_cache_client_create(unsigned total_pages, unsigned objects,
			   unsigned chunks)
{
	struct dm_mem_cache_client *client;

	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client)
		return ERR_PTR(-ENOMEM);

	client->objs_pool = mempool_create_kmalloc_pool(objects, chunks * sizeof(struct dm_mem_cache_object));
	if (!client->objs_pool)
		goto err;

	client->free_list = alloc_cache_pages(total_pages);
	if (!client->free_list)
		goto err1;

	spin_lock_init(&client->lock);
	client->objects = objects;
	client->chunks = chunks;
	client->free_pages = client->total_pages = total_pages;
	return client;

   err1:
	mempool_destroy(client->objs_pool);
   err:
	kfree(client);
	return ERR_PTR(-ENOMEM);
}

void dm_mem_cache_client_destroy(struct dm_mem_cache_client *cl)
{
	BUG_ON(cl->free_pages != cl->total_pages);
	free_cache_pages(cl->free_list);
	mempool_destroy(cl->objs_pool);
	kfree(cl);
}

/*
 * Grow a clients cache by an amount of pages.
 *
 * Don't call from interrupt context!
 */
int dm_mem_cache_grow(struct dm_mem_cache_client *cl,
		      unsigned pages_per_chunk)
{
	unsigned pages = cl->chunks * pages_per_chunk;
	struct page_list *pl = alloc_cache_pages(pages), *last = pl;

	if (!pl)
		return -ENOMEM;

	while (last->next)
		last = last->next;

	spin_lock_irq(&cl->lock);
	last->next = cl->free_list;
	cl->free_list = pl;
	cl->free_pages += pages;
	cl->total_pages += pages;
	cl->objects++;
	spin_unlock_irq(&cl->lock);

	mempool_resize(cl->objs_pool, cl->objects, GFP_NOIO);
	return 0;
}

/* Shrink a clients cache by an amount of pages */
int dm_mem_cache_shrink(struct dm_mem_cache_client *cl,
			unsigned pages_per_chunk)
{
	int r = 0;
	unsigned pages = cl->chunks * pages_per_chunk, p = pages;
	unsigned long flags;
	struct page_list *last = NULL, *pl, *pos;

	spin_lock_irqsave(&cl->lock, flags);
	pl = pos = cl->free_list;
	while (p-- && pos->next) {
		last = pos;
		pos = pos->next;
	}

	if (++p)
		r = -ENOMEM;
	else {
		cl->free_list = pos;
		cl->free_pages -= pages;
		cl->total_pages -= pages;
		cl->objects--;
		last->next = NULL;
	}
	spin_unlock_irqrestore(&cl->lock, flags);

	if (!r) {
		free_cache_pages(pl);
		mempool_resize(cl->objs_pool, cl->objects, GFP_NOIO);
	}

	return r;
}

/*
 * Allocate/free a memory object
 *
 * Can be called from interrupt context
 */
struct dm_mem_cache_object *dm_mem_cache_alloc(struct dm_mem_cache_client *cl,
					       unsigned pages_per_chunk)
{
	int r = 0;
	unsigned pages = cl->chunks * pages_per_chunk;
	unsigned long flags;
	struct dm_mem_cache_object *obj;

	obj = mempool_alloc(cl->objs_pool, GFP_NOIO);
	if (!obj)
		return ERR_PTR(-ENOMEM);

	spin_lock_irqsave(&cl->lock, flags);
	if (pages > cl->free_pages)
		r = -ENOMEM;
	else
		cl->free_pages -= pages;
	spin_unlock_irqrestore(&cl->lock, flags);

	if (r) {
		mempool_free(obj, cl->objs_pool);
		return ERR_PTR(r);
	}

	alloc_chunks(cl, obj, pages_per_chunk);
	return obj;
}

void dm_mem_cache_free(struct dm_mem_cache_client *cl,
		       struct dm_mem_cache_object *obj)
{
	free_chunks(cl, obj);
	mempool_free(obj, cl->objs_pool);
}

EXPORT_SYMBOL(dm_mem_cache_client_create);
EXPORT_SYMBOL(dm_mem_cache_client_destroy);
EXPORT_SYMBOL(dm_mem_cache_alloc);
EXPORT_SYMBOL(dm_mem_cache_free);
EXPORT_SYMBOL(dm_mem_cache_grow);
EXPORT_SYMBOL(dm_mem_cache_shrink);

MODULE_DESCRIPTION(DM_NAME " dm memory cache");
MODULE_AUTHOR("Heinz Mauelshagen <hjm@redhat.de>");
MODULE_LICENSE("GPL");
