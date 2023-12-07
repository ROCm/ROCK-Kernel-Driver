/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Fence mechanism for dma-buf to allow for asynchronous dma access
 *
 * Copyright (C) 2012 Canonical Ltd
 * Copyright (C) 2012 Texas Instruments
 *
 * Authors:
 * Rob Clark <robdclark@gmail.com>
 * Maarten Lankhorst <maarten.lankhorst@canonical.com>
 */

#ifndef AMDKCL_DMA_FENCE_H
#define AMDKCL_DMA_FENCE_H

#ifndef HAVE_DMA_FENCE_IS_CONTAINER
#include <kcl/kcl_dma_fence_chain.h>

#if !defined(HAVE_LINUX_FENCE_ARRAY_H)
#include <linux/dma-fence-array.h>
#endif
/**
 * dma_fence_is_chain - check if a fence is from the chain subclass
 * @fence: the fence to test
 *
 * Return true if it is a dma_fence_chain and false otherwise.
 */
static inline bool dma_fence_is_chain(struct dma_fence *fence)
{
        return fence->ops == &dma_fence_chain_ops;
}

/**
 * dma_fence_is_container - check if a fence is a container for other fences
 * @fence: the fence to test
 *
 * Return true if this fence is a container for other fences, false otherwise.
 * This is important since we can't build up large fence structure or otherwise
 * we run into recursion during operation on those fences.
 */
static inline bool dma_fence_is_container(struct dma_fence *fence)
{
        return dma_fence_is_array(fence) || dma_fence_is_chain(fence);
}

#endif /* HAVE_DMA_FENCE_IS_CONTAINER */

#ifndef HAVE_DMA_FENCE_TIMESTAMP
/**
 * dma_fence_timestamp - helper to get the completion timestamp of a fence
 * @fence: fence to get the timestamp from.
 *
 * After a fence is signaled the timestamp is updated with the signaling time,
 * but setting the timestamp can race with tasks waiting for the signaling. This
 * helper busy waits for the correct timestamp to appear.
 */
static inline ktime_t dma_fence_timestamp(struct dma_fence *fence)
{
        if (WARN_ON(!test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags)))
                return ktime_get();

        while (!test_bit(DMA_FENCE_FLAG_TIMESTAMP_BIT, &fence->flags))
                cpu_relax();

        return fence->timestamp;
}
#endif

/* copy from include/linux/dma-fence.h*/
#ifndef HAVE_DMA_FENCE_IS_LATER_OR_SAME
static inline bool dma_fence_is_later_or_same(struct dma_fence *f1,
                                              struct dma_fence *f2)
{
        return f1 == f2 || dma_fence_is_later(f1, f2);
}
#endif /*HAVE_DMA_FENCE_IS_LATER_OR_SAME*/
#endif
