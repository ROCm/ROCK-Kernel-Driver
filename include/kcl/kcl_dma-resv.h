/*
 * Header file for reservations for dma-buf and ttm
 *
 * Copyright(C) 2011 Linaro Limited. All rights reserved.
 * Copyright (C) 2012-2013 Canonical Ltd
 * Copyright (C) 2012 Texas Instruments
 *
 * Authors:
 * Rob Clark <robdclark@gmail.com>
 * Maarten Lankhorst <maarten.lankhorst@canonical.com>
 * Thomas Hellstrom <thellstrom-at-vmware-dot-com>
 *
 * Based on bo.c which bears the following copyright notice,
 * but is dual licensed:
 *
 * Copyright (c) 2006-2009 VMware, Inc., Palo Alto, CA., USA
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * NOTICE:
 * THIS HEADER IS FOR DMA-RESV.H ONLY
 * DO NOT INCLUDE THIS HEADER ANY OTHER PLACE
 * INCLUDE LINUX/DMA-RESV.H OR LINUX/RESERVATION.H INSTEAD
 */
#ifndef KCL_KCL_DMA_RESV_H
#define KCL_KCL_DMA_RESV_H

#include <asm/barrier.h>
#include <kcl/backport/kcl_fence_backport.h>
#include <kcl/backport/kcl_ww_mutex.h>
#include <kcl/kcl_seqlock.h>
#include <kcl/kcl_overflow.h>
#include <kcl/kcl_dma_fence.h>

struct dma_resv_list;

enum dma_resv_usage {
        /**
         * @DMA_RESV_USAGE_KERNEL: For in kernel memory management only.
         *
         * This should only be used for things like copying or clearing memory
         * with a DMA hardware engine for the purpose of kernel memory
         * management.
         *
         * Drivers *always* must wait for those fences before accessing the
         * resource protected by the dma_resv object. The only exception for
         * that is when the resource is known to be locked down in place by
         * pinning it previously.
         */
        DMA_RESV_USAGE_KERNEL,

        /**
         * @DMA_RESV_USAGE_WRITE: Implicit write synchronization.
         *
         * This should only be used for userspace command submissions which add
         * an implicit write dependency.
         */
        DMA_RESV_USAGE_WRITE,

        /**
         * @DMA_RESV_USAGE_READ: Implicit read synchronization.
         *
         * This should only be used for userspace command submissions which add
         * an implicit read dependency.
         */
        DMA_RESV_USAGE_READ,

        /**
         * @DMA_RESV_USAGE_BOOKKEEP: No implicit sync.
         *
         * This should be used by submissions which don't want to participate in
         * implicit synchronization.
         *
         * The most common case are preemption fences as well as page table
         * updates and their TLB flushes.
         */
        DMA_RESV_USAGE_BOOKKEEP
};

#if defined(HAVE_DMA_RESV_FENCES)
struct dma_resv {
	struct ww_mutex lock;
	struct dma_resv_list __rcu *fences;
};

struct dma_resv_iter {
        /** @obj: The dma_resv object we iterate over */
        struct dma_resv *obj;

        /** @usage: Return fences with this usage or lower. */
        enum dma_resv_usage usage;

        /** @fence: the currently handled fence */
        struct dma_fence *fence;

        /** @fence_usage: the usage of the current fence */
        enum dma_resv_usage fence_usage;

        /** @index: index into the shared fences */
        unsigned int index;

        /** @fences: the shared fences; private, *MUST* not dereference  */
        struct dma_resv_list *fences;

        /** @num_fences: number of fences */
        unsigned int num_fences;

        /** @is_restarted: true if this is the first returned fence */
        bool is_restarted;
};

#else

/**
 * struct dma_resv_list - a list of shared fences
 * @rcu: for internal use
 * @shared_count: table of shared fences
 * @shared_max: for growing shared fence table
 * @shared: shared fence table
 */
struct dma_resv_list {
        struct rcu_head rcu;
        u32 shared_count, shared_max;
        struct dma_fence __rcu *shared[];
};

struct dma_resv_iter {
        /** @obj: The dma_resv object we iterate over */
        struct dma_resv *obj;

        /** @usage: Return fences with this usage or lower. */
        enum dma_resv_usage usage;

        /** @fence: the currently handled fence */
        struct dma_fence *fence;

        /** @fence_usage: the usage of the current fence */
        enum dma_resv_usage fence_usage;

        /** @seq: sequence number to check for modifications */
        unsigned int seq;

        /** @index: index into the shared fences */
        unsigned int index;

        /** @fences: the shared fences; private, *MUST* not dereference  */
        struct dma_resv_list *fences;

        /** @shared_count: number of shared fences */
        unsigned int shared_count;

        /** @is_restarted: true if this is the first returned fence */
        bool is_restarted;

	/** @excl_fence: keep a reference to excl_fence when begin iterating kernel fences */
	struct dma_fence *excl_fence;

	/** @kernel_iter: next kernel fence pointer when iterating kernel fences */
	struct dma_fence *kernel_iter;
};

#if defined(HAVE_DMA_RESV_SEQCOUNT_WW_MUTEX_T)
struct dma_resv {
	struct ww_mutex lock;
	seqcount_ww_mutex_t seq;

	struct dma_fence __rcu *fence_excl;
	struct dma_resv_list __rcu *fence;
};
#else
struct dma_resv {
	struct ww_mutex lock;
	seqcount_t seq;

	struct dma_fence __rcu *fence_excl;
	struct dma_resv_list __rcu *fence;
};
#endif

/**
 * dma_resv_excl_fence - return the object's exclusive fence
 * @obj: the reservation object
 *
 * Returns the exclusive fence (if any). Caller must either hold the objects
 * through dma_resv_lock() or the RCU read side lock through rcu_read_lock(),
 * or one of the variants of each
 *
 * RETURNS
 * The exclusive fence or NULL
 */
static inline struct dma_fence *
dma_resv_excl_fence(struct dma_resv *obj)
{
        return rcu_dereference_check(obj->fence_excl, lockdep_is_held(&(obj)->lock.base));
}

/**
 * dma_resv_shared_list - get the reservation object's shared fence list
 * @obj: the reservation object
 *
 * Returns the shared fence list. Caller must either hold the objects
 * through dma_resv_lock() or the RCU read side lock through rcu_read_lock(),
 * or one of the variants of each
 */
static inline struct dma_resv_list *dma_resv_shared_list(struct dma_resv *obj)
{
        return rcu_dereference_check(obj->fence, lockdep_is_held(&(obj)->lock.base));
}

/**
 * dma_resv_iter_is_exclusive - test if the current fence is the exclusive one
 * @cursor: the cursor of the current position
 *
 * Returns true if the currently returned fence is the exclusive one.
 */
static inline bool dma_resv_iter_is_exclusive(struct dma_resv_iter *cursor)
{
	return cursor->index == 0;
}

#endif /* !defined(HAVE_DMA_RESV_FENCES) */

#if !defined(smp_store_mb)
#define smp_store_mb set_mb
#endif
#endif
