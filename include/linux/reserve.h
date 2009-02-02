/*
 * Memory reserve management.
 *
 *  Copyright (C) 2007-2008 Red Hat, Inc.,
 *  			    Peter Zijlstra <pzijlstr@redhat.com>
 *
 * This file contains the public data structure and API definitions.
 */

#ifndef _LINUX_RESERVE_H
#define _LINUX_RESERVE_H

#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/slab.h>

struct mem_reserve {
	struct mem_reserve *parent;
	struct list_head children;
	struct list_head siblings;

	const char *name;

	long pages;
	long limit;
	long usage;
	spinlock_t lock;	/* protects limit and usage */

	wait_queue_head_t waitqueue;
};

extern struct mem_reserve mem_reserve_root;

void mem_reserve_init(struct mem_reserve *res, const char *name,
		      struct mem_reserve *parent);
int mem_reserve_connect(struct mem_reserve *new_child,
			struct mem_reserve *node);
void mem_reserve_disconnect(struct mem_reserve *node);

int mem_reserve_pages_set(struct mem_reserve *res, long pages);
int mem_reserve_pages_add(struct mem_reserve *res, long pages);
int mem_reserve_pages_charge(struct mem_reserve *res, long pages);

int mem_reserve_kmalloc_set(struct mem_reserve *res, long bytes);
int mem_reserve_kmalloc_charge(struct mem_reserve *res, long bytes);

struct kmem_cache;

int mem_reserve_kmem_cache_set(struct mem_reserve *res,
			       struct kmem_cache *s,
			       int objects);
int mem_reserve_kmem_cache_charge(struct mem_reserve *res,
				  struct kmem_cache *s, long objs);

void *___kmalloc_reserve(size_t size, gfp_t flags, int node, unsigned long ip,
			 struct mem_reserve *res, int *emerg);

static inline
void *__kmalloc_reserve(size_t size, gfp_t flags, int node, unsigned long ip,
			struct mem_reserve *res, int *emerg)
{
	void *obj;

	obj = __kmalloc_node_track_caller(size,
			flags | __GFP_NOMEMALLOC | __GFP_NOWARN, node, ip);
	if (!obj)
		obj = ___kmalloc_reserve(size, flags, node, ip, res, emerg);

	return obj;
}

/**
 * kmalloc_reserve() - kmalloc() and charge against @res for @emerg allocations
 * @size - size of the requested memory region
 * @gfp - allocation flags to use for this allocation
 * @node - preferred memory node for this allocation
 * @res - reserve to charge emergency allocations against
 * @emerg - bit 0 is set when the allocation was an emergency allocation
 *
 * Returns NULL on failure
 */
#define kmalloc_reserve(size, gfp, node, res, emerg) 			\
	__kmalloc_reserve(size, gfp, node, 				\
			  _RET_IP_, res, emerg)

void __kfree_reserve(void *obj, struct mem_reserve *res, int emerg);

/**
 * kfree_reserve() - kfree() and uncharge against @res for @emerg allocations
 * @obj - memory to free
 * @res - reserve to uncharge emergency allocations from
 * @emerg - was this an emergency allocation
 */
static inline
void kfree_reserve(void *obj, struct mem_reserve *res, int emerg)
{
	if (unlikely(obj && res && emerg))
		__kfree_reserve(obj, res, emerg);
	else
		kfree(obj);
}

void *__kmem_cache_alloc_reserve(struct kmem_cache *s, gfp_t flags, int node,
				 struct mem_reserve *res, int *emerg);

/**
 * kmem_cache_alloc_reserve() - kmem_cache_alloc() and charge against @res
 * @s - kmem_cache to allocate from
 * @gfp - allocation flags to use for this allocation
 * @node - preferred memory node for this allocation
 * @res - reserve to charge emergency allocations against
 * @emerg - bit 0 is set when the allocation was an emergency allocation
 *
 * Returns NULL on failure
 */
static inline
void *kmem_cache_alloc_reserve(struct kmem_cache *s, gfp_t flags, int node,
			       struct mem_reserve *res, int *emerg)
{
	void *obj;

	obj = kmem_cache_alloc_node(s,
			flags | __GFP_NOMEMALLOC | __GFP_NOWARN, node);
	if (!obj)
		obj = __kmem_cache_alloc_reserve(s, flags, node, res, emerg);

	return obj;
}

void __kmem_cache_free_reserve(struct kmem_cache *s, void *obj,
			       struct mem_reserve *res, int emerg);

/**
 * kmem_cache_free_reserve() - kmem_cache_free() and uncharge against @res
 * @s - kmem_cache to free to
 * @obj - memory to free
 * @res - reserve to uncharge emergency allocations from
 * @emerg - was this an emergency allocation
 */
static inline
void kmem_cache_free_reserve(struct kmem_cache *s, void *obj,
			     struct mem_reserve *res, int emerg)
{
	if (unlikely(obj && res && emerg))
		__kmem_cache_free_reserve(s, obj, res, emerg);
	else
		kmem_cache_free(s, obj);
}

struct page *__alloc_pages_reserve(int node, gfp_t flags, int order,
				  struct mem_reserve *res, int *emerg);

/**
 * alloc_pages_reserve() - alloc_pages() and charge against @res
 * @node - preferred memory node for this allocation
 * @gfp - allocation flags to use for this allocation
 * @order - page order
 * @res - reserve to charge emergency allocations against
 * @emerg - bit 0 is set when the allocation was an emergency allocation
 *
 * Returns NULL on failure
 */
static inline
struct page *alloc_pages_reserve(int node, gfp_t flags, int order,
				 struct mem_reserve *res, int *emerg)
{
	struct page *page;

	page = alloc_pages_node(node,
			flags | __GFP_NOMEMALLOC | __GFP_NOWARN, order);
	if (!page)
		page = __alloc_pages_reserve(node, flags, order, res, emerg);

	return page;
}

void __free_pages_reserve(struct page *page, int order,
			  struct mem_reserve *res, int emerg);

/**
 * free_pages_reserve() - __free_pages() and uncharge against @res
 * @page - page to free
 * @order - page order
 * @res - reserve to uncharge emergency allocations from
 * @emerg - was this an emergency allocation
 */
static inline
void free_pages_reserve(struct page *page, int order,
			struct mem_reserve *res, int emerg)
{
	if (unlikely(page && res && emerg))
		__free_pages_reserve(page, order, res, emerg);
	else
		__free_pages(page, order);
}

#endif /* _LINUX_RESERVE_H */
