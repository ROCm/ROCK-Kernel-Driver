/*
  This software is available to you under a choice of one of two
  licenses.  You may choose to be licensed under the terms of the GNU
  General Public License (GPL) Version 2, available at
  <http://www.fsf.org/copyleft/gpl.html>, or the OpenIB.org BSD
  license, available in the LICENSE.TXT file accompanying this
  software.  These details are also available at
  <http://openib.org/license.html>.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
  ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

  Copyright (c) 2004 Topspin Communications.  All rights reserved.

  $Id: core_fmr.c 32 2004-04-09 03:57:42Z roland $
*/

#include "core_priv.h"

#include "ts_kernel_trace.h"
#include "ts_kernel_services.h"
#include "ts_kernel_thread.h"
#include "ts_kernel_hash.h"

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>

#include <linux/errno.h>
#include <linux/spinlock.h>
#include <linux/slab.h>

#if defined(IB_FMR_NODEBUG)
#  define IB_COMPILE_FMR_DEBUGGING_CODE 0
#else
#  define IB_COMPILE_FMR_DEBUGGING_CODE 1
#endif

/* XXX Move this compatibility code to a better place */
#ifdef W2K_OS
#  define wait_queue_head_t KEVENT
#  define TS_WINDOWS_SPINLOCK_FLAGS unsigned long flags;
#  define spin_lock(s)       spin_lock_irqsave((s), flags)
#  define spin_unlock(s)     spin_unlock_irqrestore((s), flags)
#  define spin_lock_irq(s)   spin_lock_irqsave((s), flags)
#  define spin_unlock_irq(s) spin_unlock_irqrestore((s), flags)
#else
#  define TS_WINDOWS_SPINLOCK_FLAGS
#endif

enum {
	TS_IB_FMR_MAX_REMAPS = 32,

	TS_IB_FMR_HASH_BITS  = 8,
	TS_IB_FMR_HASH_SIZE  = 1 << TS_IB_FMR_HASH_BITS,
	TS_IB_FMR_HASH_MASK  = TS_IB_FMR_HASH_SIZE - 1
};

/*
  If an FMR is not in use, then the list member will point to either
  its pool's free_list (if the FMR can be mapped again; that is,
  remap_count < TS_IB_FMR_MAX_REMAPS) or its pool's dirty_list (if the
  FMR needs to be unmapped before being remapped).  In either of these
  cases it is a bug if the ref_count is not 0.  In other words, if
  ref_count is > 0, then the list member must not be linked into
  either free_list or dirty_list.

  The cache_node member is used to link the FMR into a cache bucket
  (if caching is enabled).  This is independent of the reference count
  of the FMR.  When a valid FMR is released, its ref_count is
  decremented, and if ref_count reaches 0, the FMR is placed in either
  free_list or dirty_list as appropriate.  However, it is not removed
  from the cache and may be "revived" if a call to
  ib_fmr_register_physical() occurs before the FMR is remapped.  In
  this case we just increment the ref_count and remove the FMR from
  free_list/dirty_list.

  Before we remap an FMR from free_list, we remove it from the cache
  (to prevent another user from obtaining a stale FMR).  When an FMR
  is released, we add it to the tail of the free list, so that our
  cache eviction policy is "least recently used."

  All manipulation of ref_count, list and cache_node is protected by
  pool_lock to maintain consistency.
*/

struct ib_fmr_pool {
	TS_IB_DECLARE_MAGIC
	struct ib_device         *device;

	spinlock_t                pool_lock;

	int                       pool_size;
	int                       max_pages;
	int                       dirty_watermark;
	int                       dirty_len;
	struct list_head          free_list;
	struct list_head          dirty_list;
	tTS_HASH_HEAD             cache_bucket;

	tTS_KERNEL_THREAD         thread;

	tTS_IB_FMR_FLUSH_FUNCTION flush_function;
	void                     *flush_arg;

	wait_queue_head_t         wait;
};

static inline uint32_t ib_fmr_hash(uint64_t first_page)
{
	return tsKernelHashFunction((uint32_t) (first_page >> PAGE_SHIFT),
				    TS_IB_FMR_HASH_MASK);
}

/* Caller must hold pool_lock */
static inline struct ib_fmr *ib_fmr_cache_lookup(struct ib_fmr_pool *pool,
						 uint64_t           *page_list,
						 int                 page_list_len,
						 uint64_t            io_virtual_address,
						 uint64_t            iova_offset)
{
	tTS_HASH_HEAD  bucket;
	struct ib_fmr *fmr;
#ifdef W2K_OS
	tTS_HASH_NODE cache_node;
#endif

	if (!pool->cache_bucket) {
		return NULL;
	}

	bucket = &pool->cache_bucket[ib_fmr_hash(*page_list)];
	/* XXX this #if must be removed */
#ifndef W2K_OS
	TS_KERNEL_HASH_FOR_EACH_ENTRY(fmr, bucket, cache_node) {
		if (io_virtual_address == fmr->io_virtual_address &&
		    iova_offset        == fmr->iova_offset        &&
		    page_list_len      == fmr->page_list_len      &&
		    !memcmp(page_list, fmr->page_list, page_list_len * sizeof *page_list)) {
			return fmr;
		}
	}
#else
	cache_node = bucket->first;
	while (cache_node)
	{
		fmr = (struct ib_fmr *) ((char *)cache_node - offsetof(struct ib_fmr, cache_node));
		if (io_virtual_address == fmr->io_virtual_address &&
		    iova_offset        == fmr->iova_offset        &&
		    page_list_len      == fmr->page_list_len      &&
		    !memcmp(page_list, fmr->page_list, page_list_len * sizeof *page_list)) {
			return fmr;
		}
		cache_node = cache_node->next;
	}
#endif

	return NULL;
}

/* Caller must hold pool_lock */
static inline void ib_fmr_cache_store(struct ib_fmr_pool *pool,
                                      struct ib_fmr      *fmr)
{
	tsKernelHashNodeAdd(&fmr->cache_node,
			    &pool->cache_bucket[ib_fmr_hash(fmr->page_list[0])]);
}

/* Caller must hold pool_lock */
static inline void ib_fmr_cache_remove(struct ib_fmr *fmr)
{
	if (!tsKernelHashNodeUnhashed(&fmr->cache_node)) {
		tsKernelHashNodeRemove(&fmr->cache_node);
	}
}

static void ib_fmr_batch_release(struct ib_fmr_pool *pool)
{
	int               ret;
	struct list_head *ptr;
	struct ib_fmr    *fmr;
	LIST_HEAD(unmap_list);
	TS_WINDOWS_SPINLOCK_FLAGS

	spin_lock_irq(&pool->pool_lock);

	list_for_each(ptr, &pool->dirty_list) {
		fmr = list_entry(ptr, struct ib_fmr, list);

		ib_fmr_cache_remove(fmr);
		fmr->remap_count = 0;

		if (IB_COMPILE_FMR_DEBUGGING_CODE) {
			if (fmr->ref_count !=0) {
				TS_REPORT_WARN(MOD_KERNEL_IB,
					       "Unmapping FMR 0x%08x with ref count %d",
					       fmr, fmr->ref_count);
			}
		}
	}

	list_splice(&pool->dirty_list, &unmap_list);
	INIT_LIST_HEAD(&pool->dirty_list);
	pool->dirty_len = 0;

	spin_unlock_irq(&pool->pool_lock);

	if (list_empty(&unmap_list)) {
		return;
	}

	ret = pool->device->fmr_unmap(pool->device, &unmap_list);
	if (ret) {
		TS_REPORT_WARN(MOD_KERNEL_IB,
			       "fmr_unmap for %s returns %d",
			       pool->device->name, ret);
	}

	spin_lock_irq(&pool->pool_lock);
	list_splice(&unmap_list, &pool->free_list);
	spin_unlock_irq(&pool->pool_lock);
}

static void ib_fmr_cleanup_thread(void *pool_ptr)
{
	struct ib_fmr_pool *pool = pool_ptr;
	int ret;

	/* XXX this #if must be removed */
#ifndef _WINNT
	while (!signal_pending(current)) {
		ret = wait_event_interruptible(pool->wait,
					       pool->dirty_len >= pool->dirty_watermark);
#else
	while (ret = wait_event_interruptible(&pool->wait,
					      pool->dirty_len >= pool->dirty_watermark)) {
#endif

		TS_TRACE(MOD_KERNEL_IB, T_VERY_VERBOSE, TRACE_KERNEL_IB_GEN,
			 "cleanup thread woken up, dirty len = %d",
			 pool->dirty_len);

		if (ret) {
			break;
		}

		ib_fmr_batch_release(pool);

		if (pool->flush_function) {
			pool->flush_function(pool, pool->flush_arg);
		}
	}

	TS_REPORT_CLEANUP(MOD_KERNEL_IB, "FMR cleanup thread exiting");
}

int ib_fmr_pool_create(tTS_IB_PD_HANDLE          pd_handle,
		       struct ib_fmr_pool_param *params,
		       tTS_IB_FMR_POOL_HANDLE   *pool_handle)
{
	struct ib_pd       *pd = pd_handle;
	struct ib_device   *device;
	struct ib_fmr_pool *pool;
	int i;
	int ret;

	TS_IB_CHECK_MAGIC(pd, PD);

	if (!params) {
		return -EINVAL;
	}

	device = pd->device;
	if (!device->fmr_create  ||
	    !device->fmr_destroy ||
	    !device->fmr_map     ||
	    !device->fmr_unmap) {
		TS_REPORT_WARN(MOD_KERNEL_IB,
			       "Device %s does not support fast memory regions",
			       device->name);
		return -ENOSYS;
	}

	pool = kmalloc(sizeof *pool, GFP_KERNEL);
	if (!pool) {
		TS_REPORT_WARN(MOD_KERNEL_IB,
			       "couldn't allocate pool struct");
		return -ENOMEM;
	}

	pool->cache_bucket   = NULL;

	pool->flush_function = params->flush_function;
	pool->flush_arg      = params->flush_arg;

	INIT_LIST_HEAD(&pool->free_list);
	INIT_LIST_HEAD(&pool->dirty_list);

	if (params->cache) {
		pool->cache_bucket =
			kmalloc(TS_IB_FMR_HASH_SIZE * sizeof *pool->cache_bucket, GFP_KERNEL);
		if (!pool->cache_bucket) {
			TS_REPORT_WARN(MOD_KERNEL_IB, "Failed to allocate cache in pool");
			ret = -ENOMEM;
			goto out_free_pool;
		}

		for (i = 0; i < TS_IB_FMR_HASH_SIZE; ++i) {
			tsKernelHashHeadInit(&pool->cache_bucket[i]);
		}
	}

	pool->device          = device;
	pool->pool_size       = 0;
	pool->max_pages       = params->max_pages_per_fmr;
	pool->dirty_watermark = params->dirty_watermark;
	pool->dirty_len       = 0;
	spin_lock_init(&pool->pool_lock);
	init_waitqueue_head(&pool->wait);

	ret = tsKernelThreadStart("ts_fmr",
				  ib_fmr_cleanup_thread,
				  pool,
				  &pool->thread);

	if (ret) {
		TS_REPORT_WARN(MOD_KERNEL_IB,
			       "couldn't start cleanup thread");
		goto out_free_pool;
	}

	{
		struct ib_fmr *fmr;

		for (i = 0; i < params->pool_size; ++i) {
			fmr = kmalloc(sizeof *fmr + params->max_pages_per_fmr * sizeof (uint64_t),
				      GFP_KERNEL);
			if (!fmr) {
				TS_REPORT_WARN(MOD_KERNEL_IB,
					       "failed to allocate fmr struct for FMR %d", i);
				goto out_fail;
			}

			fmr->device           = device;
			fmr->pool             = pool;
			fmr->remap_count      = 0;
			fmr->ref_count        = 0;
			fmr->cache_node.pprev = NULL;

			if (device->fmr_create(pd,
					       params->access,
					       params->max_pages_per_fmr,
					       TS_IB_FMR_MAX_REMAPS,
					       fmr)) {
				TS_REPORT_WARN(MOD_KERNEL_IB,
					       "fmr_create failed for FMR %d", i);
				kfree(fmr);
				goto out_fail;
			}

			TS_IB_SET_MAGIC(fmr, FMR);
			list_add_tail(&fmr->list, &pool->free_list);
			++pool->pool_size;
		}
	}

	TS_IB_SET_MAGIC(pool, FMR_POOL);
	*pool_handle = pool;
	return 0;

 out_free_pool:
	kfree(pool->cache_bucket);
	kfree(pool);

	return ret;

 out_fail:
	TS_IB_SET_MAGIC(pool, FMR_POOL);
	ib_fmr_pool_destroy(pool);
	*pool_handle = NULL;

	return -ENOMEM;
}

int ib_fmr_pool_destroy(tTS_IB_FMR_POOL_HANDLE pool_handle)
{
	struct ib_fmr_pool *pool = pool_handle;
	struct list_head   *ptr;
	struct list_head   *tmp;
	struct ib_fmr      *fmr;
	int                 i;

	TS_IB_CHECK_MAGIC(pool, FMR_POOL);

	tsKernelThreadStop(pool->thread);
	ib_fmr_batch_release(pool);

	i = 0;
	list_for_each_safe(ptr, tmp, &pool->free_list) {
		fmr = list_entry(ptr, struct ib_fmr, list);
		pool->device->fmr_destroy(fmr);

		list_del(ptr);
		kfree(fmr);
		++i;
	}

	if (i < pool->pool_size) {
		TS_REPORT_WARN(MOD_KERNEL_IB,
			       "pool still has %d regions registered",
			       pool->pool_size - i);
	}

	kfree(pool->cache_bucket);
	kfree(pool);

	return 0;
}

int ib_fmr_register_physical(tTS_IB_FMR_POOL_HANDLE  pool_handle,
			     uint64_t               *page_list,
			     int                     list_len,
			     uint64_t               *io_virtual_address,
			     uint64_t                iova_offset,
			     tTS_IB_FMR_HANDLE      *fmr_handle,
			     tTS_IB_LKEY            *lkey,
			     tTS_IB_RKEY            *rkey)
{
	struct ib_fmr_pool *pool = pool_handle;
	struct ib_fmr      *fmr;
	unsigned long       flags;
	int                 result;

	TS_IB_CHECK_MAGIC(pool, FMR_POOL);

	if (list_len < 1 || list_len > pool->max_pages) {
		return -EINVAL;
	}

	spin_lock_irqsave(&pool->pool_lock, flags);
	fmr = ib_fmr_cache_lookup(pool,
				  page_list,
				  list_len,
				  *io_virtual_address,
				  iova_offset);
	if (fmr) {
		/* found in cache */
		++fmr->ref_count;
		if (fmr->ref_count == 1) {
			list_del(&fmr->list);
		}

		spin_unlock_irqrestore(&pool->pool_lock, flags);

		*lkey       = fmr->lkey;
		*rkey       = fmr->rkey;
		*fmr_handle = fmr;

		return 0;
	}

	if (list_empty(&pool->free_list)) {
		spin_unlock_irqrestore(&pool->pool_lock, flags);
		return -EAGAIN;
	}

	fmr = list_entry(pool->free_list.next, struct ib_fmr, list);
	list_del(&fmr->list);
	ib_fmr_cache_remove(fmr);
	spin_unlock_irqrestore(&pool->pool_lock, flags);

	result = pool->device->fmr_map(fmr,
				       page_list,
				       list_len,
				       io_virtual_address,
				       iova_offset,
				       lkey,
				       rkey);

	if (result) {
		spin_lock_irqsave(&pool->pool_lock, flags);
		list_add(&fmr->list, &pool->free_list);
		spin_unlock_irqrestore(&pool->pool_lock, flags);

		TS_REPORT_WARN(MOD_KERNEL_IB,
			       "fmr_map returns %d",
			       result);
		*fmr_handle = TS_IB_HANDLE_INVALID;

		return -EINVAL;
	}

	++fmr->remap_count;
	fmr->ref_count = 1;

	*fmr_handle = fmr;

	if (pool->cache_bucket) {
		fmr->lkey               = *lkey;
		fmr->rkey               = *rkey;
		fmr->io_virtual_address = *io_virtual_address;
		fmr->iova_offset        = iova_offset;
		fmr->page_list_len      = list_len;
		memcpy(fmr->page_list, page_list, list_len * sizeof(*page_list));

		spin_lock_irqsave(&pool->pool_lock, flags);
		ib_fmr_cache_store(pool, fmr);
		spin_unlock_irqrestore(&pool->pool_lock, flags);
	}

	return 0;
}

int ib_fmr_deregister(tTS_IB_FMR_HANDLE fmr_handle)
{
	struct ib_fmr      *fmr = fmr_handle;
	struct ib_fmr_pool *pool;
	unsigned long flags;

	TS_IB_CHECK_MAGIC(fmr, FMR);

	pool = fmr->pool;

	spin_lock_irqsave(&pool->pool_lock, flags);

	--fmr->ref_count;
	if (!fmr->ref_count) {
		if (fmr->remap_count < TS_IB_FMR_MAX_REMAPS) {
			list_add_tail(&fmr->list, &pool->free_list);
		} else {
			list_add_tail(&fmr->list, &pool->dirty_list);
			++pool->dirty_len;
			wake_up_interruptible(&pool->wait);
		}
	}

	if (IB_COMPILE_FMR_DEBUGGING_CODE) {
		if (fmr->ref_count < 0) {
			TS_REPORT_WARN(MOD_KERNEL_IB,
				       "FMR %p has ref count %d < 0",
				       fmr, fmr->ref_count);
		}
	}

	spin_unlock_irqrestore(&pool->pool_lock, flags);

	return 0;
}

/*
  Local Variables:
  c-file-style: "linux"
  indent-tabs-mode: t
  End:
*/
