/*
 * Copyright (c) 2000-2004 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.	 Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */
#ifndef _DMAPI_PORT_H
#define _DMAPI_PORT_H

#include <asm/div64.h>
#include "sv.h"

#include <linux/sched.h>	/* preempt needs this */
#include <linux/spinlock.h>

typedef spinlock_t lock_t;

#define spinlock_init(lock, name)	spin_lock_init(lock)
#define	spinlock_destroy(lock)

#define mutex_spinlock(lock)		({ spin_lock(lock); 0; })
#define mutex_spinunlock(lock, s)	spin_unlock(lock)
#define nested_spinlock(lock)		spin_lock(lock)
#define nested_spinunlock(lock)		spin_unlock(lock)

typedef signed int		__int32_t;
typedef unsigned int		__uint32_t;
typedef signed long long int	__int64_t;
typedef unsigned long long int	__uint64_t;


/* __psint_t is the same size as a pointer */
#if (BITS_PER_LONG == 32)
typedef __int32_t __psint_t;
typedef __uint32_t __psunsigned_t;
#elif (BITS_PER_LONG == 64)
typedef __int64_t __psint_t;
typedef __uint64_t __psunsigned_t;
#else
#error BITS_PER_LONG must be 32 or 64
#endif

static inline void
assfail(char *a, char *f, int l)
{
    printk("DMAPI assertion failed: %s, file: %s, line: %d\n", a, f, l);
    BUG();
}

#ifdef DEBUG
#define doass 1
# ifdef lint
#  define ASSERT(EX)	((void)0) /* avoid "constant in conditional" babble */
# else
#  define ASSERT(EX) ((!doass||(EX))?((void)0):assfail(#EX, __FILE__, __LINE__))
# endif	/* lint */
#else
# define ASSERT(x)	((void)0)
#endif /* DEBUG */

#define ASSERT_ALWAYS(EX)  ((EX)?((void)0):assfail(#EX, __FILE__, __LINE__))


#if defined __i386__

/* Side effect free 64 bit mod operation */
static inline __u32 dmapi_do_mod(void *a, __u32 b, int n)
{
	switch (n) {
		case 4:
			return *(__u32 *)a % b;
		case 8:
			{
			unsigned long __upper, __low, __high, __mod;
			__u64	c = *(__u64 *)a;
			__upper = __high = c >> 32;
			__low = c;
			if (__high) {
				__upper = __high % (b);
				__high = __high / (b);
			}
			asm("divl %2":"=a" (__low), "=d" (__mod):"rm" (b), "0" (__low), "1" (__upper));
			asm("":"=A" (c):"a" (__low),"d" (__high));
			return __mod;
			}
	}

	/* NOTREACHED */
	return 0;
}
#else

/* Side effect free 64 bit mod operation */
static inline __u32 dmapi_do_mod(void *a, __u32 b, int n)
{
	switch (n) {
		case 4:
			return *(__u32 *)a % b;
		case 8:
			{
			__u64	c = *(__u64 *)a;
			return do_div(c, b);
			}
	}

	/* NOTREACHED */
	return 0;
}
#endif

#define do_mod(a, b)	dmapi_do_mod(&(a), (b), sizeof(a))

#endif /* _DMAPI_PORT_H */
