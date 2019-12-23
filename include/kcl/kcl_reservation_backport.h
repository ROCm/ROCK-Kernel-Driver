#ifndef AMDKCL_RESERVATION_BACKPORT_H
#define AMDKCL_RESERVATION_BACKPORT_H

#include <kcl/kcl_reservation.h>
#include <asm/barrier.h>
#include <linux/ww_mutex.h>
#include <kcl/kcl_fence_backport.h>
#include <linux/seqlock.h>
#include <linux/rcupdate.h>
#include <asm/barrier.h>

#define reservation_ww_class (*_kcl_reservation_ww_class)
#define reservation_seqcount_class (*_kcl_reservation_seqcount_class)
#define reservation_seqcount_string (_kcl_reservation_seqcount_string)

#if defined(HAVE_DMA_RESV_H)
#if defined(HAVE_DMA_RESV_SEQ)
struct dma_resv {
	struct ww_mutex lock;
	seqcount_t seq;

	struct dma_fence __rcu *fence_excl;
	struct dma_resv_list __rcu *fence;
};
#else
struct dma_resv {
	struct ww_mutex lock;

	struct dma_fence __rcu *fence_excl;
	struct dma_resv_list __rcu *fence;
	seqcount_t seq;
};
#endif
#else
#if defined(HAVE_RESERVATION_OBJECT_DROP_SEQ)
struct dma_resv {
	struct ww_mutex lock;

	struct dma_fence __rcu *fence_excl;
	struct dma_resv_list __rcu *fence;
	seqcount_t seq;
};
#elif defined(HAVE_RESERVATION_OBJECT_DROP_STAGED)
struct dma_resv {
	struct ww_mutex lock;
	seqcount_t seq;

	struct dma_fence __rcu *fence_excl;
	struct dma_resv_list __rcu *fence;
};
#else
struct dma_resv {
	struct ww_mutex lock;
	seqcount_t seq;

	struct dma_fence __rcu *fence_excl;
	struct dma_resv_list __rcu *fence;
	struct dma_resv_list *staged;
};
#endif
#endif

#if !defined(smp_store_mb)
#define smp_store_mb set_mb
#endif
#endif
