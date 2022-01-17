// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *
 *  Swap reorganised 29.12.95, Stephen Tweedie.
 *  kswapd added: 7.1.96  sct
 *  Removed kswapd_ctl limits, and swap out as many pages as needed
 *  to bring the system back to freepages.high: 2.4.97, Rik van Riel.
 *  Zone aware kswapd started 02/00, Kanoj Sarcar (kanoj@sgi.com).
 *  Multiqueue VM started 5.8.00, Rik van Riel.
 */
#include <linux/rwsem.h>

#ifndef HAVE_SYNCHRONIZE_SHRINKERS
static DECLARE_RWSEM(shrinker_rwsem);

/**
 * synchronize_shrinkers - Wait for all running shrinkers to complete.
 *
 * This is equivalent to calling unregister_shrink() and register_shrinker(),
 * but atomically and with less overhead. This is useful to guarantee that all
 * shrinker invocations have seen an update, before freeing memory, similar to
 * rcu.
 */
void synchronize_shrinkers(void)
{
        down_write(&shrinker_rwsem);
        up_write(&shrinker_rwsem);
}
EXPORT_SYMBOL(synchronize_shrinkers);
#endif
