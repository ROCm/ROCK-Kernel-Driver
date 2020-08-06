/* SPDX-License-Identifier: MIT */
#ifndef AMDKCL_RESERVATION_BACKPORT_H
#define AMDKCL_RESERVATION_BACKPORT_H

#include <asm/barrier.h>
#include <linux/ww_mutex.h>
#include <kcl/backport/kcl_fence_backport.h>
#include <linux/seqlock.h>
#include <linux/rcupdate.h>
#include <asm/barrier.h>

extern struct ww_class *_kcl_reservation_ww_class;
extern struct lock_class_key *_kcl_reservation_seqcount_class;
extern const char *_kcl_reservation_seqcount_string;

#define reservation_ww_class (*_kcl_reservation_ww_class)
#define reservation_seqcount_class (*_kcl_reservation_seqcount_class)
#define reservation_seqcount_string (_kcl_reservation_seqcount_string)

struct dma_resv_list;

#if defined(HAVE_RESERVATION_OBJECT_STAGED)
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
