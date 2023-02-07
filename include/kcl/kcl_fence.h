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
#ifndef AMDKCL_FENCE_H
#define AMDKCL_FENCE_H

#include <linux/version.h>
#include <kcl/kcl_rcupdate.h>
#include <linux/dma-fence.h>

#if !defined(HAVE__DMA_FENCE_IS_LATER_2ARGS)

#if !defined(HAVE_DMA_FENCE_OPS_USE_64BIT_SEQNO)
static inline bool __dma_fence_is_later(u64 f1, u64 f2)
{
	
	/* This is for backward compatibility with drivers which can only handle
	 * 32bit sequence numbers. Use a 64bit compare when any of the higher
	 * bits are none zero, otherwise use a 32bit compare with wrap around
	 * handling.
	 */
	if (upper_32_bits(f1) || upper_32_bits(f2))
		return f1 > f2;

	return (int)(lower_32_bits(f1) - lower_32_bits(f2)) > 0;
}

#elif !defined(HAVE__DMA_FENCE_IS_LATER_WITH_OPS_ARG) && \
	defined(HAVE_DMA_FENCE_OPS_USE_64BIT_SEQNO)
static inline bool __dma_fence_is_later(u64 f1, u64 f2,
                                        const struct dma_fence_ops *ops)
{
        /* This is for backward compatibility with drivers which can only handle
         * 32bit sequence numbers. Use a 64bit compare when the driver says to
         * do so.
         */
        if (ops->use_64bit_seqno)
                return f1 > f2;

        return (int)(lower_32_bits(f1) - lower_32_bits(f2)) > 0;
}

#endif
#endif /* HAVE__DMA_FENCE_IS_LATER_2ARGS */

/*
 * commit v4.18-rc2-533-g418cc6ca0607
 * dma-fence: Allow wait_any_timeout for all fences)
 */
#if DRM_VERSION_CODE < DRM_VERSION(4, 19, 0)
#define AMDKCL_FENCE_WAIT_ANY_TIMEOUT
signed long
_kcl_fence_wait_any_timeout(struct dma_fence **fences, uint32_t count,
			   bool intr, signed long timeout, uint32_t *idx);
#endif

/*
 * commit  v4.9-rc2-472-gbcc004b629d2
 * dma-buf/fence: make timeout handling in fence_default_wait consistent (v2))
 *
 * commit v4.9-rc2-473-g698c0f7ff216
 * dma-buf/fence: revert "don't wait when specified timeout is zero" (v2)
 */
#if DRM_VERSION_CODE < DRM_VERSION(4, 10, 0)
#define AMDKCL_FENCE_DEFAULT_WAIT_TIMEOUT
signed long
_kcl_fence_default_wait(struct dma_fence *fence, bool intr, signed long timeout);
extern signed long _kcl_fence_wait_timeout(struct fence *fence, bool intr,
				signed long timeout);
#endif

/*
 * commit v4.14-rc3-601-g5f72db59160c
 * dma-buf/fence: Sparse wants __rcu on the object itself
 */
#if DRM_VERSION_CODE < DRM_VERSION(4, 15, 0)
#define AMDKCL_FENCE_GET_RCU_SAFE
static inline struct dma_fence *
_kcl_fence_get_rcu_safe(struct dma_fence __rcu **fencep)
{
	do {
		struct dma_fence *fence;

		fence = rcu_dereference(*fencep);
		if (!fence)
			return NULL;

		if (!dma_fence_get_rcu(fence))
			continue;

		/* The atomic_inc_not_zero() inside dma_fence_get_rcu()
		 * provides a full memory barrier upon success (such as now).
		 * This is paired with the write barrier from assigning
		 * to the __rcu protected fence pointer so that if that
		 * pointer still matches the current fence, we know we
		 * have successfully acquire a reference to it. If it no
		 * longer matches, we are holding a reference to some other
		 * reallocated pointer. This is possible if the allocator
		 * is using a freelist like SLAB_TYPESAFE_BY_RCU where the
		 * fence remains valid for the RCU grace period, but it
		 * may be reallocated. When using such allocators, we are
		 * responsible for ensuring the reference we get is to
		 * the right fence, as below.
		 */
		if (fence == rcu_access_pointer(*fencep))
			return rcu_pointer_handoff(fence);

		dma_fence_put(fence);
	} while (1);
}
#endif

/*
 * commit v4.18-rc2-519-gc701317a3eb8
 * dma-fence: Make ->enable_signaling optional
 */
#if DRM_VERSION_CODE < DRM_VERSION(4, 19, 0)
#define AMDKCL_DMA_FENCE_OPS_ENABLE_SIGNALING
bool _kcl_fence_enable_signaling(struct dma_fence *f);
#define AMDKCL_DMA_FENCE_OPS_ENABLE_SIGNALING_OPTIONAL \
	.enable_signaling = _kcl_fence_enable_signaling,
#else
#define AMDKCL_DMA_FENCE_OPS_ENABLE_SIGNALING_OPTIONAL
#endif

/*
 * commit v4.18-rc2-533-g418cc6ca0607
 * dma-fence: Make ->wait callback optional
 */
#if DRM_VERSION_CODE < DRM_VERSION(4, 19, 0)
#define AMDKCL_DMA_FENCE_OPS_WAIT_OPTIONAL \
	.wait = dma_fence_default_wait,
#else
#define AMDKCL_DMA_FENCE_OPS_WAIT_OPTIONAL
#endif

#if !defined(HAVE_DMA_FENCE_DESCRIBE)
void dma_fence_describe(struct dma_fence *fence, struct seq_file *seq);
#endif

#endif /* AMDKCL_FENCE_H */
