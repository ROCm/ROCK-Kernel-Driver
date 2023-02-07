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

#endif
