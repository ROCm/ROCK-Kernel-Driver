/*
 * Copyright (C) 1996 Paul Mackerras.
 */

#include <linux/kernel.h>
#include <asm/bitops.h>

/*
 * If the bitops are not inlined in bitops.h, they are defined here.
 *  -- paulus
 */
#if !__INLINE_BITOPS
void set_bit(int nr, volatile void * addr)
{
	unsigned long old;
	unsigned long mask = 1 << (nr & 0x1f);
	unsigned long *p = ((unsigned long *)addr) + (nr >> 5);
	
	__asm__ __volatile__(SMP_WMB "\
1:	lwarx	%0,0,%3
	or	%0,%0,%2
	stwcx.	%0,0,%3
	bne	1b"
	SMP_MB
	: "=&r" (old), "=m" (*p)
	: "r" (mask), "r" (p), "m" (*p)
	: "cc" );
}

void clear_bit(int nr, volatile void *addr)
{
	unsigned long old;
	unsigned long mask = 1 << (nr & 0x1f);
	unsigned long *p = ((unsigned long *)addr) + (nr >> 5);

	__asm__ __volatile__(SMP_WMB "\
1:	lwarx	%0,0,%3
	andc	%0,%0,%2
	stwcx.	%0,0,%3
	bne	1b"
	SMP_MB
	: "=&r" (old), "=m" (*p)
	: "r" (mask), "r" (p), "m" (*p)
	: "cc");
}

void change_bit(int nr, volatile void *addr)
{
	unsigned long old;
	unsigned long mask = 1 << (nr & 0x1f);
	unsigned long *p = ((unsigned long *)addr) + (nr >> 5);

	__asm__ __volatile__(SMP_WMB "\
1:	lwarx	%0,0,%3
	xor	%0,%0,%2
	stwcx.	%0,0,%3
	bne	1b"
	SMP_MB
	: "=&r" (old), "=m" (*p)
	: "r" (mask), "r" (p), "m" (*p)
	: "cc");
}

int test_and_set_bit(int nr, volatile void *addr)
{
	unsigned int old, t;
	unsigned int mask = 1 << (nr & 0x1f);
	volatile unsigned int *p = ((volatile unsigned int *)addr) + (nr >> 5);

	__asm__ __volatile__(SMP_WMB "\
1:	lwarx	%0,0,%4
	or	%1,%0,%3
	stwcx.	%1,0,%4
	bne	1b"
	SMP_MB
	: "=&r" (old), "=&r" (t), "=m" (*p)
	: "r" (mask), "r" (p), "m" (*p)
	: "cc");

	return (old & mask) != 0;
}

int test_and_clear_bit(int nr, volatile void *addr)
{
	unsigned int old, t;
	unsigned int mask = 1 << (nr & 0x1f);
	volatile unsigned int *p = ((volatile unsigned int *)addr) + (nr >> 5);

	__asm__ __volatile__(SMP_WMB "\
1:	lwarx	%0,0,%4
	andc	%1,%0,%3
	stwcx.	%1,0,%4
	bne	1b"
	SMP_MB
	: "=&r" (old), "=&r" (t), "=m" (*p)
	: "r" (mask), "r" (p), "m" (*p)
	: "cc");

	return (old & mask) != 0;
}

int test_and_change_bit(int nr, volatile void *addr)
{
	unsigned int old, t;
	unsigned int mask = 1 << (nr & 0x1f);
	volatile unsigned int *p = ((volatile unsigned int *)addr) + (nr >> 5);

	__asm__ __volatile__(SMP_WMB "\
1:	lwarx	%0,0,%4
	xor	%1,%0,%3
	stwcx.	%1,0,%4
	bne	1b"
	SMP_MB
	: "=&r" (old), "=&r" (t), "=m" (*p)
	: "r" (mask), "r" (p), "m" (*p)
	: "cc");

	return (old & mask) != 0;
}
#endif /* !__INLINE_BITOPS */
