/* $Id: bitops.h,v 1.61 2000/09/23 02:11:22 davem Exp $
 * bitops.h: Bit string operations on the Sparc.
 *
 * Copyright 1995 David S. Miller (davem@caip.rutgers.edu)
 * Copyright 1996 Eddie C. Dost   (ecd@skynet.be)
 */

#ifndef _SPARC_BITOPS_H
#define _SPARC_BITOPS_H

#include <linux/kernel.h>
#include <asm/byteorder.h>

#ifndef __KERNEL__

/* User mode bitops, defined here for convenience. Note: these are not
 * atomic, so packages like nthreads should do some locking around these
 * themself.
 */

extern __inline__ unsigned long set_bit(unsigned long nr, void *addr)
{
	int mask;
	unsigned long *ADDR = (unsigned long *) addr;

	ADDR += nr >> 5;
	mask = 1 << (nr & 31);
	__asm__ __volatile__("
	ld	[%0], %%g3
	or	%%g3, %2, %%g2
	st	%%g2, [%0]
	and	%%g3, %2, %0
	"
	: "=&r" (ADDR)
	: "0" (ADDR), "r" (mask)
	: "g2", "g3");

	return (unsigned long) ADDR;
}

extern __inline__ unsigned long clear_bit(unsigned long nr, void *addr)
{
	int mask;
	unsigned long *ADDR = (unsigned long *) addr;

	ADDR += nr >> 5;
	mask = 1 << (nr & 31);
	__asm__ __volatile__("
	ld	[%0], %%g3
	andn	%%g3, %2, %%g2
	st	%%g2, [%0]
	and	%%g3, %2, %0
	"
	: "=&r" (ADDR)
	: "0" (ADDR), "r" (mask)
	: "g2", "g3");

	return (unsigned long) ADDR;
}

extern __inline__ void change_bit(unsigned long nr, void *addr)
{
	int mask;
	unsigned long *ADDR = (unsigned long *) addr;

	ADDR += nr >> 5;
	mask = 1 << (nr & 31);
	__asm__ __volatile__("
	ld	[%0], %%g3
	xor	%%g3, %2, %%g2
	st	%%g2, [%0]
	and	%%g3, %2, %0
	"
	: "=&r" (ADDR)
	: "0" (ADDR), "r" (mask)
	: "g2", "g3");
}

#else /* __KERNEL__ */

#include <asm/system.h>

/* Set bit 'nr' in 32-bit quantity at address 'addr' where bit '0'
 * is in the highest of the four bytes and bit '31' is the high bit
 * within the first byte. Sparc is BIG-Endian. Unless noted otherwise
 * all bit-ops return 0 if bit was previously clear and != 0 otherwise.
 */

extern __inline__ int test_and_set_bit(unsigned long nr, volatile void *addr)
{
	register unsigned long mask asm("g2");
	register unsigned long *ADDR asm("g1");
	ADDR = ((unsigned long *) addr) + (nr >> 5);
	mask = 1 << (nr & 31);
	__asm__ __volatile__("
	mov	%%o7, %%g4
	call	___set_bit
	 add	%%o7, 8, %%o7
"	: "=&r" (mask)
	: "0" (mask), "r" (ADDR)
	: "g3", "g4", "g5", "g7", "cc");

	return mask != 0;
}

extern __inline__ void set_bit(unsigned long nr, volatile void *addr)
{
	(void) test_and_set_bit(nr, addr);
}

extern __inline__ int test_and_clear_bit(unsigned long nr, volatile void *addr)
{
	register unsigned long mask asm("g2");
	register unsigned long *ADDR asm("g1");

	ADDR = ((unsigned long *) addr) + (nr >> 5);
	mask = 1 << (nr & 31);
	__asm__ __volatile__("
	mov	%%o7, %%g4
	call	___clear_bit
	 add	%%o7, 8, %%o7
"	: "=&r" (mask)
	: "0" (mask), "r" (ADDR)
	: "g3", "g4", "g5", "g7", "cc");

	return mask != 0;
}

extern __inline__ void clear_bit(unsigned long nr, volatile void *addr)
{
	(void) test_and_clear_bit(nr, addr);
}

extern __inline__ int test_and_change_bit(unsigned long nr, volatile void *addr)
{
	register unsigned long mask asm("g2");
	register unsigned long *ADDR asm("g1");

	ADDR = ((unsigned long *) addr) + (nr >> 5);
	mask = 1 << (nr & 31);
	__asm__ __volatile__("
	mov	%%o7, %%g4
	call	___change_bit
	 add	%%o7, 8, %%o7
"	: "=&r" (mask)
	: "0" (mask), "r" (ADDR)
	: "g3", "g4", "g5", "g7", "cc");

	return mask != 0;
}

extern __inline__ void change_bit(unsigned long nr, volatile void *addr)
{
	(void) test_and_change_bit(nr, addr);
}

#endif /* __KERNEL__ */

#define smp_mb__before_clear_bit()	do { } while(0)
#define smp_mb__after_clear_bit()	do { } while(0)

/* The following routine need not be atomic. */
extern __inline__ int test_bit(int nr, __const__ void *addr)
{
	return (1 & (((__const__ unsigned int *) addr)[nr >> 5] >> (nr & 31))) != 0;
}

/* The easy/cheese version for now. */
extern __inline__ unsigned long ffz(unsigned long word)
{
	unsigned long result = 0;

	while(word & 1) {
		result++;
		word >>= 1;
	}
	return result;
}

#ifdef __KERNEL__

/*
 * ffs: find first bit set. This is defined the same way as
 * the libc and compiler builtin ffs routines, therefore
 * differs in spirit from the above ffz (man ffs).
 */

#define ffs(x) generic_ffs(x)

/*
 * hweightN: returns the hamming weight (i.e. the number
 * of bits set) of a N-bit word
 */

#define hweight32(x) generic_hweight32(x)
#define hweight16(x) generic_hweight16(x)
#define hweight8(x) generic_hweight8(x)

#endif /* __KERNEL__ */

/* find_next_zero_bit() finds the first zero bit in a bit string of length
 * 'size' bits, starting the search at bit 'offset'. This is largely based
 * on Linus's ALPHA routines, which are pretty portable BTW.
 */

extern __inline__ unsigned long find_next_zero_bit(void *addr, unsigned long size, unsigned long offset)
{
	unsigned long *p = ((unsigned long *) addr) + (offset >> 5);
	unsigned long result = offset & ~31UL;
	unsigned long tmp;

	if (offset >= size)
		return size;
	size -= result;
	offset &= 31UL;
	if (offset) {
		tmp = *(p++);
		tmp |= ~0UL >> (32-offset);
		if (size < 32)
			goto found_first;
		if (~tmp)
			goto found_middle;
		size -= 32;
		result += 32;
	}
	while (size & ~31UL) {
		if (~(tmp = *(p++)))
			goto found_middle;
		result += 32;
		size -= 32;
	}
	if (!size)
		return result;
	tmp = *p;

found_first:
	tmp |= ~0UL << size;
	if (tmp == ~0UL)        /* Are any bits zero? */
		return result + size; /* Nope. */
found_middle:
	return result + ffz(tmp);
}

/* Linus sez that gcc can optimize the following correctly, we'll see if this
 * holds on the Sparc as it does for the ALPHA.
 */

#define find_first_zero_bit(addr, size) \
        find_next_zero_bit((addr), (size), 0)

#ifndef __KERNEL__

extern __inline__ int set_le_bit(int nr, void *addr)
{
	int		mask;
	unsigned char	*ADDR = (unsigned char *) addr;

	ADDR += nr >> 3;
	mask = 1 << (nr & 0x07);
	__asm__ __volatile__("
	ldub	[%0], %%g3
	or	%%g3, %2, %%g2
	stb	%%g2, [%0]
	and	%%g3, %2, %0
	"
	: "=&r" (ADDR)
	: "0" (ADDR), "r" (mask)
	: "g2", "g3");

	return (int) ADDR;
}

extern __inline__ int clear_le_bit(int nr, void *addr)
{
	int		mask;
	unsigned char	*ADDR = (unsigned char *) addr;

	ADDR += nr >> 3;
	mask = 1 << (nr & 0x07);
	__asm__ __volatile__("
	ldub	[%0], %%g3
	andn	%%g3, %2, %%g2
	stb	%%g2, [%0]
	and	%%g3, %2, %0
	"
	: "=&r" (ADDR)
	: "0" (ADDR), "r" (mask)
	: "g2", "g3");

	return (int) ADDR;
}

#else /* __KERNEL__ */

/* Now for the ext2 filesystem bit operations and helper routines. */

extern __inline__ int set_le_bit(int nr, volatile void * addr)
{
	register int mask asm("g2");
	register unsigned char *ADDR asm("g1");

	ADDR = ((unsigned char *) addr) + (nr >> 3);
	mask = 1 << (nr & 0x07);
	__asm__ __volatile__("
	mov	%%o7, %%g4
	call	___set_le_bit
	 add	%%o7, 8, %%o7
"	: "=&r" (mask)
	: "0" (mask), "r" (ADDR)
	: "g3", "g4", "g5", "g7", "cc");

	return mask;
}

extern __inline__ int clear_le_bit(int nr, volatile void * addr)
{
	register int mask asm("g2");
	register unsigned char *ADDR asm("g1");

	ADDR = ((unsigned char *) addr) + (nr >> 3);
	mask = 1 << (nr & 0x07);
	__asm__ __volatile__("
	mov	%%o7, %%g4
	call	___clear_le_bit
	 add	%%o7, 8, %%o7
"	: "=&r" (mask)
	: "0" (mask), "r" (ADDR)
	: "g3", "g4", "g5", "g7", "cc");

	return mask;
}

#endif /* __KERNEL__ */

extern __inline__ int test_le_bit(int nr, __const__ void * addr)
{
	int			mask;
	__const__ unsigned char	*ADDR = (__const__ unsigned char *) addr;

	ADDR += nr >> 3;
	mask = 1 << (nr & 0x07);
	return ((mask & *ADDR) != 0);
}

#ifdef __KERNEL__

#define ext2_set_bit   set_le_bit
#define ext2_clear_bit clear_le_bit
#define ext2_test_bit  test_le_bit

#endif /* __KERNEL__ */

#define find_first_zero_le_bit(addr, size) \
        find_next_zero_le_bit((addr), (size), 0)

extern __inline__ unsigned long find_next_zero_le_bit(void *addr, unsigned long size, unsigned long offset)
{
	unsigned long *p = ((unsigned long *) addr) + (offset >> 5);
	unsigned long result = offset & ~31UL;
	unsigned long tmp;

	if (offset >= size)
		return size;
	size -= result;
	offset &= 31UL;
	if(offset) {
		tmp = *(p++);
		tmp |= __swab32(~0UL >> (32-offset));
		if(size < 32)
			goto found_first;
		if(~tmp)
			goto found_middle;
		size -= 32;
		result += 32;
	}
	while(size & ~31UL) {
		if(~(tmp = *(p++)))
			goto found_middle;
		result += 32;
		size -= 32;
	}
	if(!size)
		return result;
	tmp = *p;

found_first:
	tmp = __swab32(tmp) | (~0UL << size);
	if (tmp == ~0UL)        /* Are any bits zero? */
		return result + size; /* Nope. */
	return result + ffz(tmp);

found_middle:
	return result + ffz(__swab32(tmp));
}

#ifdef __KERNEL__

#define ext2_find_first_zero_bit     find_first_zero_le_bit
#define ext2_find_next_zero_bit      find_next_zero_le_bit

/* Bitmap functions for the minix filesystem.  */
#define minix_test_and_set_bit(nr,addr) test_and_set_bit(nr,addr)
#define minix_set_bit(nr,addr) set_bit(nr,addr)
#define minix_test_and_clear_bit(nr,addr) test_and_clear_bit(nr,addr)
#define minix_test_bit(nr,addr) test_bit(nr,addr)
#define minix_find_first_zero_bit(addr,size) find_first_zero_bit(addr,size)

#endif /* __KERNEL__ */

#endif /* defined(_SPARC_BITOPS_H) */
