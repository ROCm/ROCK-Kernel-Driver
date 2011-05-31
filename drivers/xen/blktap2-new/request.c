#include <linux/mempool.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/device.h>

#include "blktap.h"

/* max pages per shared pool. just to prevent accidental dos. */
#define POOL_MAX_PAGES           (256*BLKIF_MAX_SEGMENTS_PER_REQUEST)

/* default page pool size. when considering to shrink a shared pool,
 * note that paused tapdisks may grab a whole lot of pages for a long
 * time. */
#define POOL_DEFAULT_PAGES       (2 * MMAP_PAGES)

/* max number of pages allocatable per request. */
#define POOL_MAX_REQUEST_PAGES   BLKIF_MAX_SEGMENTS_PER_REQUEST

/* min request structs per pool. These grow dynamically. */
#define POOL_MIN_REQS            BLK_RING_SIZE

static struct kset *pool_set;

#define kobj_to_pool(_kobj) \
	container_of(_kobj, struct blktap_page_pool, kobj)

static struct kmem_cache *request_cache;
static mempool_t *request_pool;

static void
__page_pool_wake(struct blktap_page_pool *pool)
{
	mempool_t *mem = pool->bufs;

	/*
	  NB. slightly wasteful to always wait for a full segment
	  set. but this ensures the next disk makes
	  progress. presently, the repeated request struct
	  alloc/release cycles would otherwise keep everyone spinning.
	*/

	if (mem->curr_nr >= POOL_MAX_REQUEST_PAGES)
		wake_up(&pool->wait);
}

int
blktap_request_get_pages(struct blktap *tap,
			 struct blktap_request *request, int nr_pages)
{
	struct blktap_page_pool *pool = tap->pool;
	mempool_t *mem = pool->bufs;
	struct page *page;

	BUG_ON(request->nr_pages != 0);
	BUG_ON(nr_pages > POOL_MAX_REQUEST_PAGES);

	if (mem->curr_nr < nr_pages)
		return -ENOMEM;

	/* NB. avoid thundering herds of tapdisks colliding. */
	spin_lock(&pool->lock);

	if (mem->curr_nr < nr_pages) {
		spin_unlock(&pool->lock);
		return -ENOMEM;
	}

	while (request->nr_pages < nr_pages) {
		page = mempool_alloc(mem, GFP_NOWAIT);
		BUG_ON(!page);
		request->pages[request->nr_pages++] = page;
	}

	spin_unlock(&pool->lock);

	return 0;
}

static void
blktap_request_put_pages(struct blktap *tap,
			 struct blktap_request *request)
{
	struct blktap_page_pool *pool = tap->pool;
	struct page *page;

	while (request->nr_pages) {
		page = request->pages[--request->nr_pages];
		mempool_free(page, pool->bufs);
	}
}

size_t
blktap_request_debug(struct blktap *tap, char *buf, size_t size)
{
	struct blktap_page_pool *pool = tap->pool;
	mempool_t *mem = pool->bufs;
	char *s = buf, *end = buf + size;

	s += snprintf(buf, end - s,
		      "pool:%s pages:%d free:%d\n",
		      kobject_name(&pool->kobj),
		      mem->min_nr, mem->curr_nr);

	return s - buf;
}

struct blktap_request*
blktap_request_alloc(struct blktap *tap)
{
	struct blktap_request *request;

	request = mempool_alloc(request_pool, GFP_NOWAIT);
	if (request)
		request->tap = tap;

	return request;
}

void
blktap_request_free(struct blktap *tap,
		    struct blktap_request *request)
{
	blktap_request_put_pages(tap, request);

	mempool_free(request, request_pool);

	__page_pool_wake(tap->pool);
}

void
blktap_request_bounce(struct blktap *tap,
		      struct blktap_request *request,
		      int seg, int write)
{
	struct scatterlist *sg = &request->sg_table[seg];
	void *s, *p;

	BUG_ON(seg >= request->nr_pages);

	s = sg_virt(sg);
	p = page_address(request->pages[seg]) + sg->offset;

	if (write)
		memcpy(p, s, sg->length);
	else
		memcpy(s, p, sg->length);
}

static void
blktap_request_ctor(void *obj)
{
	struct blktap_request *request = obj;

	memset(request, 0, sizeof(*request));
	sg_init_table(request->sg_table, ARRAY_SIZE(request->sg_table));
}

static int
blktap_page_pool_resize(struct blktap_page_pool *pool, int target)
{
	mempool_t *bufs = pool->bufs;
	int err;

	/* NB. mempool asserts min_nr >= 1 */
	target = max(1, target);

	err = mempool_resize(bufs, target, GFP_KERNEL);
	if (err)
		return err;

	__page_pool_wake(pool);

	return 0;
}

struct pool_attribute {
	struct attribute attr;

	ssize_t (*show)(struct blktap_page_pool *pool,
			char *buf);

	ssize_t (*store)(struct blktap_page_pool *pool,
			 const char *buf, size_t count);
};

#define kattr_to_pool_attr(_kattr) \
	container_of(_kattr, struct pool_attribute, attr)

static ssize_t
blktap_page_pool_show_size(struct blktap_page_pool *pool,
			   char *buf)
{
	mempool_t *mem = pool->bufs;
	return sprintf(buf, "%d", mem->min_nr);
}

static ssize_t
blktap_page_pool_store_size(struct blktap_page_pool *pool,
			    const char *buf, size_t size)
{
	int target;

	/*
	 * NB. target fixup to avoid undesired results. less than a
	 * full segment set can wedge the disk. much more than a
	 * couple times the physical queue depth is rarely useful.
	 */

	target = simple_strtoul(buf, NULL, 0);
	target = max(POOL_MAX_REQUEST_PAGES, target);
	target = min(target, POOL_MAX_PAGES);

	return blktap_page_pool_resize(pool, target) ? : size;
}

static struct pool_attribute blktap_page_pool_attr_size =
	__ATTR(size, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH,
	       blktap_page_pool_show_size,
	       blktap_page_pool_store_size);

static ssize_t
blktap_page_pool_show_free(struct blktap_page_pool *pool,
			   char *buf)
{
	mempool_t *mem = pool->bufs;
	return sprintf(buf, "%d", mem->curr_nr);
}

static struct pool_attribute blktap_page_pool_attr_free =
	__ATTR(free, S_IRUSR|S_IRGRP|S_IROTH,
	       blktap_page_pool_show_free,
	       NULL);

static struct attribute *blktap_page_pool_attrs[] = {
	&blktap_page_pool_attr_size.attr,
	&blktap_page_pool_attr_free.attr,
	NULL,
};

static inline struct kobject*
__blktap_kset_find_obj(struct kset *kset, const char *name)
{
	struct kobject *k;
	struct kobject *ret = NULL;

	spin_lock(&kset->list_lock);
	list_for_each_entry(k, &kset->list, entry) {
		if (kobject_name(k) && !strcmp(kobject_name(k), name)) {
			ret = kobject_get(k);
			break;
		}
	}
	spin_unlock(&kset->list_lock);
	return ret;
}

static ssize_t
blktap_page_pool_show_attr(struct kobject *kobj, struct attribute *kattr,
			   char *buf)
{
	struct blktap_page_pool *pool = kobj_to_pool(kobj);
	struct pool_attribute *attr = kattr_to_pool_attr(kattr);

	if (attr->show)
		return attr->show(pool, buf);

	return -EIO;
}

static ssize_t
blktap_page_pool_store_attr(struct kobject *kobj, struct attribute *kattr,
			    const char *buf, size_t size)
{
	struct blktap_page_pool *pool = kobj_to_pool(kobj);
	struct pool_attribute *attr = kattr_to_pool_attr(kattr);

	if (attr->show)
		return attr->store(pool, buf, size);

	return -EIO;
}

static struct sysfs_ops blktap_page_pool_sysfs_ops = {
	.show		= blktap_page_pool_show_attr,
	.store		= blktap_page_pool_store_attr,
};

static void
blktap_page_pool_release(struct kobject *kobj)
{
	struct blktap_page_pool *pool = kobj_to_pool(kobj);
	mempool_destroy(pool->bufs);
	kfree(pool);
}

struct kobj_type blktap_page_pool_ktype = {
	.release       = blktap_page_pool_release,
	.sysfs_ops     = &blktap_page_pool_sysfs_ops,
	.default_attrs = blktap_page_pool_attrs,
};

static void*
__mempool_page_alloc(gfp_t gfp_mask, void *pool_data)
{
	struct page *page;

	if (!(gfp_mask & __GFP_WAIT))
		return NULL;

	page = alloc_page(gfp_mask);
	if (page)
		SetPageReserved(page);

	return page;
}

static void
__mempool_page_free(void *element, void *pool_data)
{
	struct page *page = element;

	ClearPageReserved(page);
	put_page(page);
}

static struct kobject*
blktap_page_pool_create(const char *name, int nr_pages)
{
	struct blktap_page_pool *pool;
	int err;

	pool = kzalloc(sizeof(*pool), GFP_KERNEL);
	if (!pool)
		goto fail;

	spin_lock_init(&pool->lock);
	init_waitqueue_head(&pool->wait);

	pool->bufs = mempool_create(nr_pages,
				    __mempool_page_alloc, __mempool_page_free,
				    pool);
	if (!pool->bufs)
		goto fail_pool;

	kobject_init(&pool->kobj, &blktap_page_pool_ktype);
	pool->kobj.kset = pool_set;
	err = kobject_add(&pool->kobj, &pool_set->kobj, "%s", name);
	if (err)
		goto fail_bufs;

	return &pool->kobj;

	kobject_del(&pool->kobj);
fail_bufs:
	mempool_destroy(pool->bufs);
fail_pool:
	kfree(pool);
fail:
	return NULL;
}

struct blktap_page_pool*
blktap_page_pool_get(const char *name)
{
	struct kobject *kobj;

	kobj = __blktap_kset_find_obj(pool_set, name);
	if (!kobj)
		kobj = blktap_page_pool_create(name,
					       POOL_DEFAULT_PAGES);
	if (!kobj)
		return ERR_PTR(-ENOMEM);

	return kobj_to_pool(kobj);
}

int __init
blktap_page_pool_init(struct kobject *parent)
{
	request_cache =
		kmem_cache_create("blktap-request",
				  sizeof(struct blktap_request), 0,
				  0, blktap_request_ctor);
	if (!request_cache)
		return -ENOMEM;

	request_pool =
		mempool_create_slab_pool(POOL_MIN_REQS, request_cache);
	if (!request_pool)
		return -ENOMEM;

	pool_set = kset_create_and_add("pools", NULL, parent);
	if (!pool_set)
		return -ENOMEM;

	return 0;
}

void
blktap_page_pool_exit(void)
{
	if (pool_set) {
		BUG_ON(!list_empty(&pool_set->list));
		kset_unregister(pool_set);
		pool_set = NULL;
	}

	if (request_pool) {
		mempool_destroy(request_pool);
		request_pool = NULL;
	}

	if (request_cache) {
		kmem_cache_destroy(request_cache);
		request_cache = NULL;
	}
}
