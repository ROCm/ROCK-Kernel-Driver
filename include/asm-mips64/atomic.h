/*
 * Atomic operations that C can't guarantee us.  Useful for
 * resource counting etc..
 *
 * But use these as seldom as possible since they are much more slower
 * than regular operations.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 1997, 1999, 2000 by Ralf Baechle
 */
#ifndef _ASM_ATOMIC_H
#define _ASM_ATOMIC_H

#include <asm/sgidefs.h>

typedef struct { volatile int counter; } atomic_t;

#ifdef __KERNEL__
#define ATOMIC_INIT(i)    { (i) }

#define atomic_read(v)	((v)->counter)
#define atomic_set(v,i)	((v)->counter = (i))

extern __inline__ void atomic_add(int i, volatile atomic_t * v)
{
	unsigned long temp;

	__asm__ __volatile__(
		"1:\tll\t%0,%1\t\t\t# atomic_add\n\t"
		"addu\t%0,%2\n\t"
		"sc\t%0,%1\n\t"
		"beqz\t%0,1b"
		: "=&r" (temp), "=m" (v->counter)
		: "Ir" (i), "m" (v->counter));
}

extern __inline__ void atomic_sub(int i, volatile atomic_t * v)
{
	unsigned long temp;

	__asm__ __volatile__(
		"1:\tll\t%0,%1\t\t\t# atomic_sub\n\t"
		"subu\t%0,%2\n\t"
		"sc\t%0,%1\n\t"
		"beqz\t%0,1b"
		: "=&r" (temp), "=m" (v->counter)
		: "Ir" (i), "m" (v->counter));
}

/*
 * Same as above, but return the result value
 */
extern __inline__ int atomic_add_return(int i, atomic_t * v)
{
	unsigned long temp, result;

	__asm__ __volatile__(
		".set\tnoreorder\t\t\t# atomic_add_return\n"
		"1:\tll\t%1,%2\n\t"
		"addu\t%0,%1,%3\n\t"
		"sc\t%0,%2\n\t"
		"beqz\t%0,1b\n\t"
		"addu\t%0,%1,%3\n\t"
		".set\treorder"
		: "=&r" (result), "=&r" (temp), "=m" (v->counter)
		: "Ir" (i), "m" (v->counter)
		: "memory");

	return result;
}

extern __inline__ int atomic_sub_return(int i, atomic_t * v)
{
	unsigned long temp, result;

	__asm__ __volatile__(
		".set\tnoreorder\t\t\t# atomic_sub_return\n"
		"1:\tll\t%1,%2\n\t"
		"subu\t%0,%1,%3\n\t"
		"sc\t%0,%2\n\t"
		"beqz\t%0,1b\n\t"
		"subu\t%0,%1,%3\n\t"
		".set\treorder"
		: "=&r" (result), "=&r" (temp), "=m" (v->counter)
		: "Ir" (i), "m" (v->counter)
		: "memory");

	return result;
}

#define atomic_dec_return(v) atomic_sub_return(1,(v))
#define atomic_inc_return(v) atomic_add_return(1,(v))

#define atomic_sub_and_test(i,v) (atomic_sub_return((i), (v)) == 0)
#define atomic_dec_and_test(v) (atomic_sub_return(1, (v)) == 0)

#define atomic_inc(v) atomic_add(1,(v))
#define atomic_dec(v) atomic_sub(1,(v))
#endif /* defined(__KERNEL__) */

#endif /* _ASM_ATOMIC_H */
