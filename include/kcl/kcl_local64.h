/* SPDX-License-Identifier: GPL-2.0 */
#ifndef AMDKCL_ASM_GENERIC_LOCAL64_H
#define AMDKCL_ASM_GENERIC_LOCAL64_H

#include <linux/percpu.h>
#include <asm/types.h>
#include <asm/local.h>

/*
 * A signed long type for operations which are atomic for a single CPU.
 * Usually used in combination with per-cpu variables.
 *
 * This is the default implementation, which uses atomic64_t.  Which is
 * rather pointless.  The whole point behind local64_t is that some processors
 * can perform atomic adds and subtracts in a manner which is atomic wrt IRQs
 * running on this CPU.  local64_t allows exploitation of such capabilities.
 */

/* Implement in terms of atomics. */

#if !defined HAVE_LINUX_LOCAL_TRY_CMPXCHG && defined HAVE_LINUX_ATOMIC_LONG_TRY_CMPXCHG
#define local_try_cmpxchg(l, po, n) atomic_long_try_cmpxchg((&(l)->a), (po), (n))
#if BITS_PER_LONG == 64

static inline bool local64_try_cmpxchg(local64_t *l, s64 *old, s64 new)
{
        return local_try_cmpxchg(&l->a, (long *)old, new);
}
#else
#define local64_try_cmpxchg(l, po, n) atomic64_try_cmpxchg((&(l)->a), (po), (n))
#endif
#endif
#endif
