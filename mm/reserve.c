/*
 * Memory reserve management.
 *
 *  Copyright (C) 2007-2008, Red Hat, Inc.,
 *  			     Peter Zijlstra <pzijlstr@redhat.com>
 *
 * Description:
 *
 * Manage a set of memory reserves.
 *
 * A memory reserve is a reserve for a specified number of object of specified
 * size. Since memory is managed in pages, this reserve demand is then
 * translated into a page unit.
 *
 * So each reserve has a specified object limit, an object usage count and a
 * number of pages required to back these objects.
 *
 * Usage is charged against a reserve, if the charge fails, the resource must
 * not be allocated/used.
 *
 * The reserves are managed in a tree, and the resource demands (pages and
 * limit) are propagated up the tree. Obviously the object limit will be
 * meaningless as soon as the unit starts mixing, but the required page reserve
 * (being of one unit) is still valid at the root.
 *
 * It is the page demand of the root node that is used to set the global
 * reserve (adjust_memalloc_reserve() which sets zone->pages_emerg).
 *
 * As long as a subtree has the same usage unit, an aggregate node can be used
 * to charge against, instead of the leaf nodes. However, do be consistent with
 * who is charged, resource usage is not propagated up the tree (for
 * performance reasons).
 */

#include <linux/reserve.h>
#include <linux/mutex.h>
#include <linux/mmzone.h>
#include <linux/log2.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include "internal.h"

static DEFINE_MUTEX(mem_reserve_mutex);

/**
 * @mem_reserve_root - the global reserve root
 *
 * The global reserve is empty, and has no limit unit, it merely
 * acts as an aggregation point for reserves and an interface to
 * adjust_memalloc_reserve().
 */
struct mem_reserve mem_reserve_root = {
	.children = LIST_HEAD_INIT(mem_reserve_root.children),
	.siblings = LIST_HEAD_INIT(mem_reserve_root.siblings),
	.name = "total reserve",
	.lock = __SPIN_LOCK_UNLOCKED(mem_reserve_root.lock),
	.waitqueue = __WAIT_QUEUE_HEAD_INITIALIZER(mem_reserve_root.waitqueue),
};
EXPORT_SYMBOL_GPL(mem_reserve_root);

/**
 * mem_reserve_init() - initialize a memory reserve object
 * @res - the new reserve object
 * @name - a name for this reserve
 * @parent - when non NULL, the parent to connect to.
 */
void mem_reserve_init(struct mem_reserve *res, const char *name,
		      struct mem_reserve *parent)
{
	memset(res, 0, sizeof(*res));
	INIT_LIST_HEAD(&res->children);
	INIT_LIST_HEAD(&res->siblings);
	res->name = name;
	spin_lock_init(&res->lock);
	init_waitqueue_head(&res->waitqueue);

	if (parent)
		mem_reserve_connect(res, parent);
}
EXPORT_SYMBOL_GPL(mem_reserve_init);

/*
 * propagate the pages and limit changes up the (sub)tree.
 */
static void __calc_reserve(struct mem_reserve *res, long pages, long limit)
{
	unsigned long flags;

	for ( ; res; res = res->parent) {
		res->pages += pages;

		if (limit) {
			spin_lock_irqsave(&res->lock, flags);
			res->limit += limit;
			spin_unlock_irqrestore(&res->lock, flags);
		}
	}
}

/**
 * __mem_reserve_add() - primitive to change the size of a reserve
 * @res - reserve to change
 * @pages - page delta
 * @limit - usage limit delta
 *
 * Returns -ENOMEM when a size increase is not possible atm.
 */
static int __mem_reserve_add(struct mem_reserve *res, long pages, long limit)
{
	int ret = 0;
	long reserve;

	/*
	 * This looks more complex than need be, that is because we handle
	 * the case where @res isn't actually connected to mem_reserve_root.
	 *
	 * So, by propagating the new pages up the (sub)tree and computing
	 * the difference in mem_reserve_root.pages we find if this action
	 * affects the actual reserve.
	 *
	 * The (partial) propagation also makes that mem_reserve_connect()
	 * needs only look at the direct child, since each disconnected
	 * sub-tree is fully up-to-date.
	 */
	reserve = mem_reserve_root.pages;
	__calc_reserve(res, pages, 0);
	reserve = mem_reserve_root.pages - reserve;

	if (reserve) {
		ret = adjust_memalloc_reserve(reserve);
		if (ret)
			__calc_reserve(res, -pages, 0);
	}

	/*
	 * Delay updating the limits until we've acquired the resources to
	 * back it.
	 */
	if (!ret)
		__calc_reserve(res, 0, limit);

	return ret;
}

/**
 * __mem_reserve_charge() - primitive to charge object usage of a reserve
 * @res - reserve to charge
 * @charge - size of the charge
 *
 * Returns non-zero on success, zero on failure.
 */
static
int __mem_reserve_charge(struct mem_reserve *res, long charge)
{
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&res->lock, flags);
	if (charge < 0 || res->usage + charge < res->limit) {
		res->usage += charge;
		if (unlikely(res->usage < 0))
			res->usage = 0;
		ret = 1;
	}
	if (charge < 0)
		wake_up_all(&res->waitqueue);
	spin_unlock_irqrestore(&res->lock, flags);

	return ret;
}

/**
 * mem_reserve_connect() - connect a reserve to another in a child-parent relation
 * @new_child - the reserve node to connect (child)
 * @node - the reserve node to connect to (parent)
 *
 * Connecting a node results in an increase of the reserve by the amount of
 * pages in @new_child->pages if @node has a connection to mem_reserve_root.
 *
 * Returns -ENOMEM when the new connection would increase the reserve (parent
 * is connected to mem_reserve_root) and there is no memory to do so.
 *
 * On error, the child is _NOT_ connected.
 */
int mem_reserve_connect(struct mem_reserve *new_child, struct mem_reserve *node)
{
	int ret;

	WARN_ON(!new_child->name);

	mutex_lock(&mem_reserve_mutex);
	if (new_child->parent) {
		ret = -EEXIST;
		goto unlock;
	}
	new_child->parent = node;
	list_add(&new_child->siblings, &node->children);
	ret = __mem_reserve_add(node, new_child->pages, new_child->limit);
	if (ret) {
		new_child->parent = NULL;
		list_del_init(&new_child->siblings);
	}
unlock:
	mutex_unlock(&mem_reserve_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(mem_reserve_connect);

/**
 * mem_reserve_disconnect() - sever a nodes connection to the reserve tree
 * @node - the node to disconnect
 *
 * Disconnecting a node results in a reduction of the reserve by @node->pages
 * if node had a connection to mem_reserve_root.
 */
void mem_reserve_disconnect(struct mem_reserve *node)
{
	int ret;

	BUG_ON(!node->parent);

	mutex_lock(&mem_reserve_mutex);
	if (!node->parent) {
		ret = -ENOENT;
		goto unlock;
	}
	ret = __mem_reserve_add(node->parent, -node->pages, -node->limit);
	if (!ret) {
		node->parent = NULL;
		list_del_init(&node->siblings);
	}
unlock:
	mutex_unlock(&mem_reserve_mutex);

	/*
	 * We cannot fail to shrink the reserves, can we?
	 */
	WARN_ON(ret);
}
EXPORT_SYMBOL_GPL(mem_reserve_disconnect);

#ifdef CONFIG_PROC_FS

/*
 * Simple output of the reserve tree in: /proc/reserve_info
 * Example:
 *
 * localhost ~ # cat /proc/reserve_info
 * 1:0 "total reserve" 6232K 0/278581
 * 2:1 "total network reserve" 6232K 0/278581
 * 3:2 "network TX reserve" 212K 0/53
 * 4:3 "protocol TX pages" 212K 0/53
 * 5:2 "network RX reserve" 6020K 0/278528
 * 6:5 "IPv4 route cache" 5508K 0/16384
 * 7:5 "SKB data reserve" 512K 0/262144
 * 8:7 "IPv4 fragment cache" 512K 0/262144
 */

static void mem_reserve_show_item(struct seq_file *m, struct mem_reserve *res,
				  unsigned int parent, unsigned int *id)
{
	struct mem_reserve *child;
	unsigned int my_id = ++*id;

	seq_printf(m, "%d:%d \"%s\" %ldK %ld/%ld\n",
			my_id, parent, res->name,
			res->pages << (PAGE_SHIFT - 10),
			res->usage, res->limit);

	list_for_each_entry(child, &res->children, siblings)
		mem_reserve_show_item(m, child, my_id, id);
}

static int mem_reserve_show(struct seq_file *m, void *v)
{
	unsigned int ident = 0;

	mutex_lock(&mem_reserve_mutex);
	mem_reserve_show_item(m, &mem_reserve_root, ident, &ident);
	mutex_unlock(&mem_reserve_mutex);

	return 0;
}

static int mem_reserve_open(struct inode *inode, struct file *file)
{
	return single_open(file, mem_reserve_show, NULL);
}

static const struct file_operations mem_reserve_opterations = {
	.open = mem_reserve_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static __init int mem_reserve_proc_init(void)
{
	proc_create("reserve_info", S_IRUSR, NULL, &mem_reserve_opterations);
	return 0;
}

module_init(mem_reserve_proc_init);

#endif

/*
 * alloc_page helpers
 */

/**
 * mem_reserve_pages_set() - set reserves size in pages
 * @res - reserve to set
 * @pages - size in pages to set it to
 *
 * Returns -ENOMEM when it fails to set the reserve. On failure the old size
 * is preserved.
 */
int mem_reserve_pages_set(struct mem_reserve *res, long pages)
{
	int ret;

	mutex_lock(&mem_reserve_mutex);
	pages -= res->pages;
	ret = __mem_reserve_add(res, pages, pages * PAGE_SIZE);
	mutex_unlock(&mem_reserve_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(mem_reserve_pages_set);

/**
 * mem_reserve_pages_add() - change the size in a relative way
 * @res - reserve to change
 * @pages - number of pages to add (or subtract when negative)
 *
 * Similar to mem_reserve_pages_set, except that the argument is relative
 * instead of absolute.
 *
 * Returns -ENOMEM when it fails to increase.
 */
int mem_reserve_pages_add(struct mem_reserve *res, long pages)
{
	int ret;

	mutex_lock(&mem_reserve_mutex);
	ret = __mem_reserve_add(res, pages, pages * PAGE_SIZE);
	mutex_unlock(&mem_reserve_mutex);

	return ret;
}

/**
 * mem_reserve_pages_charge() - charge page usage to a reserve
 * @res - reserve to charge
 * @pages - size to charge
 *
 * Returns non-zero on success.
 */
int mem_reserve_pages_charge(struct mem_reserve *res, long pages)
{
	return __mem_reserve_charge(res, pages * PAGE_SIZE);
}
EXPORT_SYMBOL_GPL(mem_reserve_pages_charge);

/*
 * kmalloc helpers
 */

/**
 * mem_reserve_kmalloc_set() - set this reserve to bytes worth of kmalloc
 * @res - reserve to change
 * @bytes - size in bytes to reserve
 *
 * Returns -ENOMEM on failure.
 */
int mem_reserve_kmalloc_set(struct mem_reserve *res, long bytes)
{
	int ret;
	long pages;

	mutex_lock(&mem_reserve_mutex);
	pages = kmalloc_estimate_bytes(GFP_ATOMIC, bytes);
	pages -= res->pages;
	bytes -= res->limit;
	ret = __mem_reserve_add(res, pages, bytes);
	mutex_unlock(&mem_reserve_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(mem_reserve_kmalloc_set);

/**
 * mem_reserve_kmalloc_charge() - charge bytes to a reserve
 * @res - reserve to charge
 * @bytes - bytes to charge
 *
 * Returns non-zero on success.
 */
int mem_reserve_kmalloc_charge(struct mem_reserve *res, long bytes)
{
	if (bytes < 0)
		bytes = -roundup_pow_of_two(-bytes);
	else
		bytes = roundup_pow_of_two(bytes);

	return __mem_reserve_charge(res, bytes);
}
EXPORT_SYMBOL_GPL(mem_reserve_kmalloc_charge);

/*
 * kmem_cache helpers
 */

/**
 * mem_reserve_kmem_cache_set() - set reserve to @objects worth of kmem_cache_alloc of @s
 * @res - reserve to set
 * @s - kmem_cache to reserve from
 * @objects - number of objects to reserve
 *
 * Returns -ENOMEM on failure.
 */
int mem_reserve_kmem_cache_set(struct mem_reserve *res, struct kmem_cache *s,
			       int objects)
{
	int ret;
	long pages, bytes;

	mutex_lock(&mem_reserve_mutex);
	pages = kmem_alloc_estimate(s, GFP_ATOMIC, objects);
	pages -= res->pages;
	bytes = objects * kmem_cache_size(s) - res->limit;
	ret = __mem_reserve_add(res, pages, bytes);
	mutex_unlock(&mem_reserve_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(mem_reserve_kmem_cache_set);

/**
 * mem_reserve_kmem_cache_charge() - charge (or uncharge) usage of objs
 * @res - reserve to charge
 * @objs - objects to charge for
 *
 * Returns non-zero on success.
 */
int mem_reserve_kmem_cache_charge(struct mem_reserve *res, struct kmem_cache *s,
				  long objs)
{
	return __mem_reserve_charge(res, objs * kmem_cache_size(s));
}
EXPORT_SYMBOL_GPL(mem_reserve_kmem_cache_charge);

/*
 * Alloc wrappers.
 *
 * Actual usage is commented in linux/reserve.h where the interface functions
 * live. Furthermore, the code is 3 instances of the same paradigm, hence only
 * the first contains extensive comments.
 */

/*
 * kmalloc/kfree
 */

void *___kmalloc_reserve(size_t size, gfp_t flags, int node, unsigned long ip,
			 struct mem_reserve *res, int *emerg)
{
	void *obj;
	gfp_t gfp;

	/*
	 * Try a regular allocation, when that fails and we're not entitled
	 * to the reserves, fail.
	 */
	gfp = flags | __GFP_NOMEMALLOC | __GFP_NOWARN;
	obj = __kmalloc_node_track_caller(size, gfp, node, ip);

	if (obj || !(gfp_to_alloc_flags(flags) & ALLOC_NO_WATERMARKS))
		goto out;

	/*
	 * If we were given a reserve to charge against, try that.
	 */
	if (res && !mem_reserve_kmalloc_charge(res, size)) {
		/*
		 * If we failed to charge and we're not allowed to wait for
		 * it to succeed, bail.
		 */
		if (!(flags & __GFP_WAIT))
			goto out;

		/*
		 * Wait for a successfull charge against the reserve. All
		 * uncharge operations against this reserve will wake us up.
		 */
		wait_event(res->waitqueue,
				mem_reserve_kmalloc_charge(res, size));

		/*
		 * After waiting for it, again try a regular allocation.
		 * Pressure could have lifted during our sleep. If this
		 * succeeds, uncharge the reserve.
		 */
		obj = __kmalloc_node_track_caller(size, gfp, node, ip);
		if (obj) {
			mem_reserve_kmalloc_charge(res, -size);
			goto out;
		}
	}

	/*
	 * Regular allocation failed, and we've successfully charged our
	 * requested usage against the reserve. Do the emergency allocation.
	 */
	obj = __kmalloc_node_track_caller(size, flags, node, ip);
	WARN_ON(!obj);
	if (emerg)
		*emerg = 1;

out:
	return obj;
}

void __kfree_reserve(void *obj, struct mem_reserve *res, int emerg)
{
	/*
	 * ksize gives the full allocated size vs the requested size we used to
	 * charge; however since we round up to the nearest power of two, this
	 * should all work nicely.
	 */
	size_t size = ksize(obj);

	kfree(obj);
	/*
	 * Free before uncharge, this ensures memory is actually present when
	 * a subsequent charge succeeds.
	 */
	mem_reserve_kmalloc_charge(res, -size);
}

/*
 * kmem_cache_alloc/kmem_cache_free
 */

void *__kmem_cache_alloc_reserve(struct kmem_cache *s, gfp_t flags, int node,
				 struct mem_reserve *res, int *emerg)
{
	void *obj;
	gfp_t gfp;

	gfp = flags | __GFP_NOMEMALLOC | __GFP_NOWARN;
	obj = kmem_cache_alloc_node(s, gfp, node);

	if (obj || !(gfp_to_alloc_flags(flags) & ALLOC_NO_WATERMARKS))
		goto out;

	if (res && !mem_reserve_kmem_cache_charge(res, s, 1)) {
		if (!(flags & __GFP_WAIT))
			goto out;

		wait_event(res->waitqueue,
				mem_reserve_kmem_cache_charge(res, s, 1));

		obj = kmem_cache_alloc_node(s, gfp, node);
		if (obj) {
			mem_reserve_kmem_cache_charge(res, s, -1);
			goto out;
		}
	}

	obj = kmem_cache_alloc_node(s, flags, node);
	WARN_ON(!obj);
	if (emerg)
		*emerg = 1;

out:
	return obj;
}

void __kmem_cache_free_reserve(struct kmem_cache *s, void *obj,
			       struct mem_reserve *res, int emerg)
{
	kmem_cache_free(s, obj);
	mem_reserve_kmem_cache_charge(res, s, -1);
}

/*
 * alloc_pages/free_pages
 */

struct page *__alloc_pages_reserve(int node, gfp_t flags, int order,
				   struct mem_reserve *res, int *emerg)
{
	struct page *page;
	gfp_t gfp;
	long pages = 1 << order;

	gfp = flags | __GFP_NOMEMALLOC | __GFP_NOWARN;
	page = alloc_pages_node(node, gfp, order);

	if (page || !(gfp_to_alloc_flags(flags) & ALLOC_NO_WATERMARKS))
		goto out;

	if (res && !mem_reserve_pages_charge(res, pages)) {
		if (!(flags & __GFP_WAIT))
			goto out;

		wait_event(res->waitqueue,
				mem_reserve_pages_charge(res, pages));

		page = alloc_pages_node(node, gfp, order);
		if (page) {
			mem_reserve_pages_charge(res, -pages);
			goto out;
		}
	}

	page = alloc_pages_node(node, flags, order);
	WARN_ON(!page);
	if (emerg)
		*emerg = 1;

out:
	return page;
}

void __free_pages_reserve(struct page *page, int order,
			  struct mem_reserve *res, int emerg)
{
	__free_pages(page, order);
	mem_reserve_pages_charge(res, -(1 << order));
}
