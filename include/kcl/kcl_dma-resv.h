/* SPDX-License-Identifier: MIT */

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
#include <kcl/kcl_seqlock.h>

struct dma_resv_list;

#if defined(HAVE_DMA_RESV_SEQCOUNT_WW_MUTEX_T)
struct dma_resv {
	struct ww_mutex lock;
	seqcount_ww_mutex_t seq;

	struct dma_fence __rcu *fence_excl;
	struct dma_resv_list __rcu *fence;
};
#elif defined(HAVE_RESERVATION_OBJECT_STAGED)
struct dma_resv {
	struct ww_mutex lock;
	seqcount_t seq;

	struct dma_fence __rcu *fence_excl;
	struct dma_resv_list __rcu *fence;
	struct dma_resv_list *staged;
};
#else
struct dma_resv {
	struct ww_mutex lock;
	seqcount_t seq;

	struct dma_fence __rcu *fence_excl;
	struct dma_resv_list __rcu *fence;
};
#endif

#if !defined(smp_store_mb)
#define smp_store_mb set_mb
#endif
#endif
