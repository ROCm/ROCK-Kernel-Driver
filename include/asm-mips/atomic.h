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
 * Copyright (C) 1996, 1997 by Ralf Baechle
 *
 * $Id: atomic.h,v 1.6 1999/07/26 19:42:42 harald Exp $
 */
#ifndef __ASM_ATOMIC_H
#define __ASM_ATOMIC_H

#include <linux/config.h>

#ifdef CONFIG_SMP
typedef struct { volatile int counter; } atomic_t;
#else
typedef struct { int counter; } atomic_t;
#endif

#ifdef __KERNEL__
#define ATOMIC_INIT(i)    { (i) }

#define atomic_read(v)	((v)->counter)
#define atomic_set(v,i)	((v)->counter = (i))

#if !defined(CONFIG_CPU_HAS_LLSC)

#include <asm/system.h>

/*
 * The MIPS I implementation is only atomic with respect to
 * interrupts.  R3000 based multiprocessor machines are rare anyway ...
 */
extern __inline__ void atomic_add(int i, volatile atomic_t * v)
{
	int	flags;

	save_flags(flags);
	cli();
	v->counter += i;
	restore_flags(flags);
}

extern __inline__ void atomic_sub(int i, volatile atomic_t * v)
{
	int	flags;

	save_flags(flags);
	cli();
	v->counter -= i;
	restore_flags(flags);
}

extern __inline__ int atomic_add_return(int i, atomic_t * v)
{
	int	temp, flags;

	save_flags(flags);
	cli();
	temp = v->counter;
	temp += i;
	v->counter = temp;
	restore_flags(flags);

	return temp;
}

extern __inline__ int atomic_sub_return(int i, atomic_t * v)
{
	int	temp, flags;

	save_flags(flags);
	cli();
	temp = v->counter;
	temp -= i;
	v->counter = temp;
	restore_flags(flags);

	return temp;
}

extern __inline__ void atomic_clear_mask(unsigned long mask, unsigned long * v)
{
        unsigned long temp;
        int     flags;

        save_flags(flags);
        cli();
        temp = *v;
        temp &= ~mask;
        *v = temp;
        restore_flags(flags);

        return;
}

#else

/*
 * ... while for MIPS II and better we can use ll/sc instruction.  This
 * implementation is SMP safe ...
 */

/*
 * Make sure gcc doesn't try to be clever and move things around
 * on us. We need to use _exactly_ the address the user gave us,
 * not some alias that contains the same information.
 */
#define __atomic_fool_gcc(x) (*(volatile struct { int a[100]; } *)x)

extern __inline__ void atomic_add(int i, volatile atomic_t * v)
{
	unsigned long temp;

	__asm__ __volatile__(
		"1:\tll\t%0,%1\n\t"
		"addu\t%0,%2\n\t"
		"sc\t%0,%1\n\t"
		"beqz\t%0,1b"
		:"=&r" (temp),
		 "=m" (__atomic_fool_gcc(v))
		:"Ir" (i),
		 "m" (__atomic_fool_gcc(v)));
}

extern __inline__ void atomic_sub(int i, volatile atomic_t * v)
{
	unsigned long temp;

	__asm__ __volatile__(
		"1:\tll\t%0,%1\n\t"
		"subu\t%0,%2\n\t"
		"sc\t%0,%1\n\t"
		"beqz\t%0,1b"
		:"=&r" (temp),
		 "=m" (__atomic_fool_gcc(v))
		:"Ir" (i),
		 "m" (__atomic_fool_gcc(v)));
}

/*
 * Same as above, but return the result value
 */
extern __inline__ int atomic_add_return(int i, atomic_t * v)
{
	unsigned long temp, result;

	__asm__ __volatile__(
		".set\tnoreorder\n"
		"1:\tll\t%1,%2\n\t"
		"addu\t%0,%1,%3\n\t"
		"sc\t%0,%2\n\t"
		"beqz\t%0,1b\n\t"
		"addu\t%0,%1,%3\n\t"
		".set\treorder"
		:"=&r" (result),
		 "=&r" (temp),
		 "=m" (__atomic_fool_gcc(v))
		:"Ir" (i),
		 "m" (__atomic_fool_gcc(v)));

	return result;
}

extern __inline__ int atomic_sub_return(int i, atomic_t * v)
{
	unsigned long temp, result;

	__asm__ __volatile__(
		".set\tnoreorder\n"
		"1:\tll\t%1,%2\n\t"
		"subu\t%0,%1,%3\n\t"
		"sc\t%0,%2\n\t"
		"beqz\t%0,1b\n\t"
		"subu\t%0,%1,%3\n\t"
		".set\treorder"
		:"=&r" (result),
		 "=&r" (temp),
		 "=m" (__atomic_fool_gcc(v))
		:"Ir" (i),
		 "m" (__atomic_fool_gcc(v)));

	return result;
}
#endif

#define atomic_dec_return(v) atomic_sub_return(1,(v))
#define atomic_inc_return(v) atomic_add_return(1,(v))

#define atomic_sub_and_test(i,v) (atomic_sub_return((i), (v)) == 0)
#define atomic_dec_and_test(v) (atomic_sub_return(1, (v)) == 0)

#define atomic_inc(v) atomic_add(1,(v))
#define atomic_dec(v) atomic_sub(1,(v))
#endif /* defined(__KERNEL__) */

#endif /* __ASM_MIPS_ATOMIC_H */
