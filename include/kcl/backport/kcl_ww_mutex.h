/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Wound/Wait Mutexes: blocking mutual exclusion locks with deadlock avoidance
 *
 * Original mutex implementation started by Ingo Molnar:
 *
 *  Copyright (C) 2004, 2005, 2006 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 *
 * Wait/Die implementation:
 *  Copyright (C) 2013 Canonical Ltd.
 * Choice of algorithm:
 *  Copyright (C) 2018 WMWare Inc.
 *
 * This file contains the main data structure and API definitions.
 */
#ifndef __KCL_BACKPORT_KCL_WW_MUTEX_H__
#define	__KCL_BACKPORT_KCL_WW_MUTEX_H__

#include <linux/ww_mutex.h>

#ifndef HAVE_WW_MUTEX_TRYLOCK_CONTEXT_ARG
static inline int _kcl_ww_mutex_trylock(struct ww_mutex *lock)
{
	return ww_mutex_trylock(lock);
}
#define ww_mutex_trylock(MUTEX, CTX)  _kcl_ww_mutex_trylock(MUTEX)
#endif

#endif
