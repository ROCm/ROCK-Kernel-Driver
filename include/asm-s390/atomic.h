#ifndef __ARCH_S390_ATOMIC__
#define __ARCH_S390_ATOMIC__

/*
 *  include/asm-s390/atomic.h
 *
 *  S390 version
 *    Copyright (C) 1999,2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 *               Denis Joseph Barrow
 *
 *  Derived from "include/asm-i386/bitops.h"
 *    Copyright (C) 1992, Linus Torvalds
 *
 */

/*
 * Atomic operations that C can't guarantee us.  Useful for
 * resource counting etc..
 * S390 uses 'Compare And Swap' for atomicity in SMP enviroment
 */

typedef struct { volatile int counter; } atomic_t __attribute__ ((aligned (4)));
#define ATOMIC_INIT(i)  { (i) }

#define atomic_eieio()          __asm__ __volatile__ ("BCR 15,0")

static __inline__ int atomic_read(atomic_t *v)
{
        int retval;
        __asm__ __volatile__("bcr      15,0\n\t"
                             "l        %0,%1"
                             : "=d" (retval) : "m" (*v) );
        return retval;
}

static __inline__ void atomic_set(atomic_t *v, int i)
{
        __asm__ __volatile__("st  %1,%0\n\t"
                             "bcr 15,0"
                             : : "m" (*v), "d" (i) : "memory");
}

static __inline__ void atomic_add(int i, atomic_t *v)
{
        __asm__ __volatile__("   l     0,%0\n"
                             "0: lr    1,0\n"
                             "   ar    1,%1\n"
                             "   cs    0,1,%0\n"
                             "   jl    0b"
                             : "+m" (*v) : "d" (i) : "0", "1" );
}

static __inline__ int atomic_add_return (int i, atomic_t *v)
{
	int newval;
        __asm__ __volatile__("   l     0,%0\n"
                             "0: lr    %1,0\n"
                             "   ar    %1,%2\n"
                             "   cs    0,%1,%0\n"
                             "   jl    0b"
                             : "+m" (*v), "=&d" (newval)
			     : "d" (i) : "0", "cc" );
	return newval;
}

static __inline__ int atomic_add_negative(int i, atomic_t *v)
{
	int newval;
        __asm__ __volatile__("   l     0,%0\n"
                             "0: lr    %1,0\n"
                             "   ar    %1,%2\n"
                             "   cs    0,%1,%0\n"
                             "   jl    0b\n"
                             : "+m" (*v), "=&d" (newval)
                             : "d" (i) : "0", "cc" );
        return newval < 0;
}

static __inline__ void atomic_sub(int i, atomic_t *v)
{
        __asm__ __volatile__("   l     0,%0\n"
                             "0: lr    1,0\n"
                             "   sr    1,%1\n"
                             "   cs    0,1,%0\n"
                             "   jl    0b"
                             : "+m" (*v) : "d" (i) : "0", "1" );
}

static __inline__ void atomic_inc(volatile atomic_t *v)
{
        __asm__ __volatile__("   l     0,%0\n"
                             "0: lr    1,0\n"
                             "   ahi   1,1\n"
                             "   cs    0,1,%0\n"
                             "   jl    0b"
                             : "+m" (*v) : : "0", "1" );
}

static __inline__ int atomic_inc_return(volatile atomic_t *v)
{
        int i;
        __asm__ __volatile__("   l     0,%0\n"
                             "0: lr    %1,0\n"
                             "   ahi   %1,1\n"
                             "   cs    0,%1,%0\n"
                             "   jl    0b"
                             : "+m" (*v), "=&d" (i) : : "0" );
        return i;
}

static __inline__ int atomic_inc_and_test(volatile atomic_t *v)
{
       int i;

        __asm__ __volatile__("   l     0,%0\n"
                             "0: lr    %1,0\n"
                             "   ahi   %1,1\n"
                             "   cs    0,%1,%0\n"
                             "   jl    0b"
                             : "+m" (*v), "=&d" (i) : : "0" );
       return i != 0;
}

static __inline__ void atomic_dec(volatile atomic_t *v)
{
        __asm__ __volatile__("   l     0,%0\n"
                             "0: lr    1,0\n"
                             "   ahi   1,-1\n"
                             "   cs    0,1,%0\n"
                             "   jl    0b"
                             : "+m" (*v) : : "0", "1" );
}

static __inline__ int atomic_dec_return(volatile atomic_t *v)
{
        int i;
        __asm__ __volatile__("   l     0,%0\n"
                             "0: lr    %1,0\n"
                             "   ahi   %1,-1\n"
                             "   cs    0,%1,%0\n"
                             "   jl    0b"
                             : "+m" (*v), "=&d" (i) : : "0" );
        return i;
}

static __inline__ int atomic_dec_and_test(volatile atomic_t *v)
{
        int i;
        __asm__ __volatile__("   l     0,%0\n"
                             "0: lr    %1,0\n"
                             "   ahi   %1,-1\n"
                             "   cs    0,%1,%0\n"
                             "   jl    0b"
                             : "+m" (*v), "=&d" (i) : : "0");
        return i == 0;
}

static __inline__ void atomic_clear_mask(unsigned long mask, atomic_t *v)
{
        __asm__ __volatile__("   l     0,%0\n"
                             "0: lr    1,0\n"
                             "   nr    1,%1\n"
                             "   cs    0,1,%0\n"
                             "   jl    0b"
                             : "+m" (*v) : "d" (~(mask)) : "0", "1" );
}

static __inline__ void atomic_set_mask(unsigned long mask, atomic_t *v)
{
        __asm__ __volatile__("   l     0,%0\n"
                             "0: lr    1,0\n"
                             "   or    1,%1\n"
                             "   cs    0,1,%0\n"
                             "   jl    0b"
                             : "+m" (*v) : "d" (mask) : "0", "1" );
}

/*
  returns 0  if expected_oldval==value in *v ( swap was successful )
  returns 1  if unsuccessful.
*/
static __inline__ int
atomic_compare_and_swap(int expected_oldval,int new_val,atomic_t *v)
{
        int retval;

        __asm__ __volatile__(
                "  cs   %2,%3,%1\n"
                "  ipm  %0\n"
                "  srl  %0,28\n"
                "0:"
                : "=&r" (retval), "+m" (*v)
                : "d" (expected_oldval) , "d" (new_val)
                : "memory", "cc");
        return retval;
}

/*
  Spin till *v = expected_oldval then swap with newval.
 */
static __inline__ void
atomic_compare_and_swap_spin(int expected_oldval,int new_val,atomic_t *v)
{
        __asm__ __volatile__(
                "0: lr  1,%1\n"
                "   cs  1,%2,%0\n"
                "   jl  0b\n"
                : "+m" (*v)
                : "d" (expected_oldval) , "d" (new_val)
                : "memory", "cc", "1");
}

#endif                                 /* __ARCH_S390_ATOMIC __            */

