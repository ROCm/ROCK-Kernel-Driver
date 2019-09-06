/*
 * Fence mechanism for dma-buf and to allow for asynchronous dma access
 *
 * Copyright (C) 2012 Canonical Ltd
 * Copyright (C) 2012 Texas Instruments
 *
 * Authors:
 * Rob Clark <robdclark@gmail.com>
 * Maarten Lankhorst <maarten.lankhorst@canonical.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 */

#include <linux/slab.h>
#include <kcl/kcl_fence.h>
#include "kcl_common.h"

#define CREATE_TRACE_POINTS
#include <kcl/kcl_trace.h>
#if !defined(HAVE_DMA_FENCE_DEFINED)
static atomic64_t fence_context_counter = ATOMIC64_INIT(0);
u64 _kcl_fence_context_alloc(unsigned num)
{
	BUG_ON(!num);
	return atomic64_add_return(num, &fence_context_counter) - num;
}
EXPORT_SYMBOL(_kcl_fence_context_alloc);

void
_kcl_fence_init(struct dma_fence *fence, const struct dma_fence_ops *ops,
	       spinlock_t *lock, u64 context, unsigned seqno)
{
	BUG_ON(!lock);
	BUG_ON(!ops || !ops->wait || !ops->enable_signaling ||
	       !ops->get_driver_name || !ops->get_timeline_name);

	kref_init(&fence->refcount);
	fence->ops = ops;
	INIT_LIST_HEAD(&fence->cb_list);
	fence->lock = lock;
	fence->context = context;
	fence->seqno = seqno;
	fence->flags = 0UL;
	fence->status = 0;

	/*
	 * Modifications [2017-03-29] (c) [2017]
	 * Advanced Micro Devices, Inc.
	 */
	trace_kcl_fence_init(fence);
}
EXPORT_SYMBOL(_kcl_fence_init);
#endif

static bool
dma_fence_test_signaled_any(struct dma_fence **fences, uint32_t count,
			    uint32_t *idx)
{
	int i;

	for (i = 0; i < count; ++i) {
		struct dma_fence *fence = fences[i];
		if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags)) {
			if (idx)
				*idx = i;
			return true;
		}
	}
	return false;
}

struct default_wait_cb {
	struct dma_fence_cb base;
	struct task_struct *task;
};

static void (*_kcl_fence_default_wait_cb)(struct dma_fence *fence, struct dma_fence_cb *cb);

#if DRM_VERSION_CODE < DRM_VERSION(4, 19, 0)
signed long
_kcl_fence_default_wait(struct dma_fence *fence, bool intr, signed long timeout)
{
	struct default_wait_cb cb;
	unsigned long flags;
	signed long ret = timeout ? timeout : 1;
	bool was_set;

	if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags))
		return ret;

	spin_lock_irqsave(fence->lock, flags);

	if (intr && signal_pending(current)) {
		ret = -ERESTARTSYS;
		goto out;
	}

	was_set = test_and_set_bit(DMA_FENCE_FLAG_ENABLE_SIGNAL_BIT,
				   &fence->flags);

	if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags))
		goto out;

	if (!was_set && fence->ops->enable_signaling) {
		/*
		 * Modifications [2017-03-29] (c) [2017]
		 * Advanced Micro Devices, Inc.
		 */
		trace_kcl_fence_enable_signal(fence);

		if (!fence->ops->enable_signaling(fence)) {
			dma_fence_signal_locked(fence);
			goto out;
		}
	}

	if (!timeout) {
		ret = 0;
		goto out;
	}

	cb.base.func = _kcl_fence_default_wait_cb;
	cb.task = current;
	list_add(&cb.base.node, &fence->cb_list);

	while (!test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags) && ret > 0) {
		if (intr)
			__set_current_state(TASK_INTERRUPTIBLE);
		else
			__set_current_state(TASK_UNINTERRUPTIBLE);
		spin_unlock_irqrestore(fence->lock, flags);

		ret = schedule_timeout(ret);

		spin_lock_irqsave(fence->lock, flags);
		if (ret > 0 && intr && signal_pending(current))
			ret = -ERESTARTSYS;
	}

	if (!list_empty(&cb.base.node))
		list_del(&cb.base.node);
	__set_current_state(TASK_RUNNING);

out:
	spin_unlock_irqrestore(fence->lock, flags);
	return ret;
}
EXPORT_SYMBOL(_kcl_fence_default_wait);
#endif

/*
 * Modifications [2017-09-19] (c) [2017]
 * Advanced Micro Devices, Inc.
 */
#if DRM_VERSION_CODE < DRM_VERSION(4, 19, 0)
signed long
_kcl_fence_wait_any_timeout(struct dma_fence **fences, uint32_t count,
			   bool intr, signed long timeout, uint32_t *idx)
{
	struct default_wait_cb *cb;
	signed long ret = timeout;
	unsigned i;

	if (WARN_ON(!fences || !count || timeout < 0))
		return -EINVAL;

	if (timeout == 0) {
		for (i = 0; i < count; ++i)
			if (dma_fence_is_signaled(fences[i])) {
				if (idx)
					*idx = i;
				return 1;
			}

		return 0;
	}

	cb = kcalloc(count, sizeof(struct default_wait_cb), GFP_KERNEL);
	if (cb == NULL) {
		ret = -ENOMEM;
		goto err_free_cb;
	}

	for (i = 0; i < count; ++i) {
		struct dma_fence *fence = fences[i];

		cb[i].task = current;
		if (dma_fence_add_callback(fence, &cb[i].base,
					   _kcl_fence_default_wait_cb)) {
			/* This fence is already signaled */
			if (idx)
				*idx = i;
			goto fence_rm_cb;
		}
	}

	while (ret > 0) {
		if (intr)
			set_current_state(TASK_INTERRUPTIBLE);
		else
			set_current_state(TASK_UNINTERRUPTIBLE);

		if (dma_fence_test_signaled_any(fences, count, idx))
			break;

		ret = schedule_timeout(ret);

		if (ret > 0 && intr && signal_pending(current))
			ret = -ERESTARTSYS;
	}

	__set_current_state(TASK_RUNNING);

fence_rm_cb:
	while (i-- > 0)
		dma_fence_remove_callback(fences[i], &cb[i].base);

err_free_cb:
	kfree(cb);

	return ret;
}
EXPORT_SYMBOL(_kcl_fence_wait_any_timeout);
#endif

#if !defined(HAVE_DMA_FENCE_DEFINED)
signed long
_kcl_fence_wait_timeout(struct dma_fence *fence, bool intr, signed long timeout)
{
	signed long ret;

	if (WARN_ON(timeout < 0))
		return -EINVAL;

	/*
	 * Modifications [2017-03-29] (c) [2017]
	 * Advanced Micro Devices, Inc.
	 */
	trace_kcl_fence_wait_start(fence);
	if (fence->ops->wait)
		ret = fence->ops->wait(fence, intr, timeout);
	else
		ret = kcl_fence_default_wait(fence, intr, timeout);
	trace_kcl_fence_wait_end(fence);
	return ret;
}
EXPORT_SYMBOL(_kcl_fence_wait_timeout);
#endif
/*
 * Modifications [2016-12-23] (c) [2016]
 * Advanced Micro Devices, Inc.
 */
void amdkcl_fence_init(void)
{
#if defined(HAVE_DMA_FENCE_DEFINED)
	_kcl_fence_default_wait_cb = amdkcl_fp_setup("dma_fence_default_wait_cb", NULL);
#else
	_kcl_fence_default_wait_cb = amdkcl_fp_setup("fence_default_wait_cb", NULL);
#endif
}
