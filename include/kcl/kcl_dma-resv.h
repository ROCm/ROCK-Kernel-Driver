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

struct dma_resv_list;

struct dma_resv {
	struct ww_mutex lock;
	seqcount_t seq;

	struct dma_fence __rcu *fence_excl;
	struct dma_resv_list __rcu *fence;
};

#if !defined(smp_store_mb)
#define smp_store_mb set_mb
#endif
#endif
