#include <linux/spinlock.h>
#include <xen/balloon.h>

#include "blktap.h"

#define MAX_BUCKETS                      8
#define BUCKET_SIZE                      MAX_PENDING_REQS

#define BLKTAP_POOL_CLOSING              1

struct blktap_request_bucket;

struct blktap_request_handle {
	int                              slot;
	uint8_t                          inuse;
	struct blktap_request            request;
	struct blktap_request_bucket    *bucket;
};

struct blktap_request_bucket {
	atomic_t                         reqs_in_use;
	struct blktap_request_handle     handles[BUCKET_SIZE];
	struct page                    **foreign_pages;
};

struct blktap_request_pool {
	spinlock_t                       lock;
	uint8_t                          status;
	struct list_head                 free_list;
	atomic_t                         reqs_in_use;
	wait_queue_head_t                wait_queue;
	struct blktap_request_bucket    *buckets[MAX_BUCKETS];
};

static struct blktap_request_pool pool;

static inline struct blktap_request_handle *
blktap_request_to_handle(struct blktap_request *req)
{
	return container_of(req, struct blktap_request_handle, request);
}

static void
blktap_request_pool_init_request(struct blktap_request *request)
{
	int i;

	request->usr_idx  = -1;
	request->nr_pages = 0;
	request->status   = BLKTAP_REQUEST_FREE;
	INIT_LIST_HEAD(&request->free_list);
	for (i = 0; i < ARRAY_SIZE(request->handles); i++) {
		request->handles[i].user   = INVALID_GRANT_HANDLE;
		request->handles[i].kernel = INVALID_GRANT_HANDLE;
	}
}

static int
blktap_request_pool_allocate_bucket(void)
{
	int i, idx;
	unsigned long flags;
	struct blktap_request *request;
	struct blktap_request_handle *handle;
	struct blktap_request_bucket *bucket;

	bucket = kzalloc(sizeof(struct blktap_request_bucket), GFP_KERNEL);
	if (!bucket)
		goto fail;

	bucket->foreign_pages = alloc_empty_pages_and_pagevec(MMAP_PAGES);
	if (!bucket->foreign_pages)
		goto fail;

	spin_lock_irqsave(&pool.lock, flags);

	idx = -1;
	for (i = 0; i < MAX_BUCKETS; i++) {
		if (!pool.buckets[i]) {
			idx = i;
			pool.buckets[idx] = bucket;
			break;
		}
	}

	if (idx == -1) {
		spin_unlock_irqrestore(&pool.lock, flags);
		goto fail;
	}

	for (i = 0; i < BUCKET_SIZE; i++) {
		handle  = bucket->handles + i;
		request = &handle->request;

		handle->slot   = i;
		handle->inuse  = 0;
		handle->bucket = bucket;

		blktap_request_pool_init_request(request);
		list_add_tail(&request->free_list, &pool.free_list);
	}

	spin_unlock_irqrestore(&pool.lock, flags);

	return 0;

fail:
	if (bucket && bucket->foreign_pages)
		free_empty_pages_and_pagevec(bucket->foreign_pages, MMAP_PAGES);
	kfree(bucket);
	return -ENOMEM;
}

static void
blktap_request_pool_free_bucket(struct blktap_request_bucket *bucket)
{
	if (!bucket)
		return;

	BTDBG("freeing bucket %p\n", bucket);

	free_empty_pages_and_pagevec(bucket->foreign_pages, MMAP_PAGES);
	kfree(bucket);
}

unsigned long
request_to_kaddr(struct blktap_request *req, int seg)
{
	struct blktap_request_handle *handle = blktap_request_to_handle(req);
	int idx = handle->slot * BLKIF_MAX_SEGMENTS_PER_REQUEST + seg;
	unsigned long pfn = page_to_pfn(handle->bucket->foreign_pages[idx]);
	return (unsigned long)pfn_to_kaddr(pfn);
}

int
blktap_request_pool_shrink(void)
{
	int i, err;
	unsigned long flags;
	struct blktap_request_bucket *bucket;

	err = -EAGAIN;

	spin_lock_irqsave(&pool.lock, flags);

	/* always keep at least one bucket */
	for (i = 1; i < MAX_BUCKETS; i++) {
		bucket = pool.buckets[i];
		if (!bucket)
			continue;

		if (atomic_read(&bucket->reqs_in_use))
			continue;

		blktap_request_pool_free_bucket(bucket);
		pool.buckets[i] = NULL;
		err = 0;
		break;
	}

	spin_unlock_irqrestore(&pool.lock, flags);

	return err;
}

int
blktap_request_pool_grow(void)
{
	return blktap_request_pool_allocate_bucket();
}

struct blktap_request *
blktap_request_allocate(struct blktap *tap)
{
	int i;
	uint16_t usr_idx;
	unsigned long flags;
	struct blktap_request *request;

	usr_idx = -1;
	request = NULL;

	spin_lock_irqsave(&pool.lock, flags);

	if (pool.status == BLKTAP_POOL_CLOSING)
		goto out;

	for (i = 0; i < ARRAY_SIZE(tap->pending_requests); i++)
		if (!tap->pending_requests[i]) {
			usr_idx = i;
			break;
		}

	if (usr_idx == (uint16_t)-1)
		goto out;

	if (!list_empty(&pool.free_list)) {
		request = list_entry(pool.free_list.next,
				     struct blktap_request, free_list);
		list_del(&request->free_list);
	}

	if (request) {
		struct blktap_request_handle *handle;

		atomic_inc(&pool.reqs_in_use);

		handle = blktap_request_to_handle(request);
		atomic_inc(&handle->bucket->reqs_in_use);
		handle->inuse = 1;

		request->usr_idx = usr_idx;

		tap->pending_requests[usr_idx] = request;
		tap->pending_cnt++;
	}

out:
	spin_unlock_irqrestore(&pool.lock, flags);
	return request;
}

void
blktap_request_free(struct blktap *tap, struct blktap_request *request)
{
	int free;
	unsigned long flags;
	struct blktap_request_handle *handle;

	BUG_ON(request->usr_idx >= ARRAY_SIZE(tap->pending_requests));
	handle = blktap_request_to_handle(request);

	spin_lock_irqsave(&pool.lock, flags);

	handle->inuse = 0;
	tap->pending_requests[request->usr_idx] = NULL;
	blktap_request_pool_init_request(request);
	list_add(&request->free_list, &pool.free_list);
	atomic_dec(&handle->bucket->reqs_in_use);
	free = atomic_dec_and_test(&pool.reqs_in_use);

	spin_unlock_irqrestore(&pool.lock, flags);

	if (--tap->pending_cnt == 0)
		wake_up_interruptible(&tap->wq);

	if (free)
		wake_up(&pool.wait_queue);
}

void
blktap_request_pool_free(void)
{
	int i;
	unsigned long flags;

	spin_lock_irqsave(&pool.lock, flags);

	pool.status = BLKTAP_POOL_CLOSING;
	while (atomic_read(&pool.reqs_in_use)) {
		spin_unlock_irqrestore(&pool.lock, flags);
		wait_event(pool.wait_queue, !atomic_read(&pool.reqs_in_use));
		spin_lock_irqsave(&pool.lock, flags);
	}

	for (i = 0; i < MAX_BUCKETS; i++) {
		blktap_request_pool_free_bucket(pool.buckets[i]);
		pool.buckets[i] = NULL;
	}

	spin_unlock_irqrestore(&pool.lock, flags);
}

int
blktap_request_pool_init(void)
{
	int i, err;

	memset(&pool, 0, sizeof(pool));

	spin_lock_init(&pool.lock);
	INIT_LIST_HEAD(&pool.free_list);
	atomic_set(&pool.reqs_in_use, 0);
	init_waitqueue_head(&pool.wait_queue);

	for (i = 0; i < 2; i++) {
		err = blktap_request_pool_allocate_bucket();
		if (err)
			goto fail;
	}

	return 0;

fail:
	blktap_request_pool_free();
	return err;
}
