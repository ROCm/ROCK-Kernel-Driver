/* $Id: bitops.h,v 1.7 1999/08/19 22:56:33 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 1994 - 1997, 1999  Ralf Baechle (ralf@gnu.org)
 */
#ifndef _ASM_BITOPS_H
#define _ASM_BITOPS_H

#include <linux/types.h>
#include <asm/byteorder.h>		/* sigh ... */

#ifdef __KERNEL__

#include <asm/sgidefs.h>
#include <asm/system.h>
#include <linux/config.h>

/*
 * Only disable interrupt for kernel mode stuff to keep usermode stuff
 * that dares to use kernel include files alive.
 */
#define __bi_flags unsigned long flags
#define __bi_cli() __cli()
#define __bi_save_flags(x) __save_flags(x)
#define __bi_save_and_cli(x) __save_and_cli(x)
#define __bi_restore_flags(x) __restore_flags(x)
#else
#define __bi_flags
#define __bi_cli()
#define __bi_save_flags(x)
#define __bi_save_and_cli(x)
#define __bi_restore_flags(x)
#endif /* __KERNEL__ */

/*
 * Note that the bit operations are defined on arrays of 32 bit sized
 * elements.  With respect to a future 64 bit implementation it is
 * wrong to use long *.  Use u32 * or int *.
 */
extern __inline__ void set_bit(int nr, void *addr);
extern __inline__ void clear_bit(int nr, void *addr);
extern __inline__ void change_bit(int nr, void *addr);
extern __inline__ int test_and_set_bit(int nr, void *addr);
extern __inline__ int test_and_clear_bit(int nr, void *addr);
extern __inline__ int test_and_change_bit(int nr, void *addr);

extern __inline__ int test_bit(int nr, const void *addr);
#ifndef __MIPSEB__
extern __inline__ int find_first_zero_bit (void *addr, unsigned size);
#endif
extern __inline__ int find_next_zero_bit (void * addr, int size, int offset);
extern __inline__ unsigned long ffz(unsigned long word);

#if defined(CONFIG_CPU_HAS_LLSC)

#include <asm/mipsregs.h>

/*
 * These functions for MIPS ISA > 1 are interrupt and SMP proof and
 * interrupt friendly
 */

/*
 * The following functions will only work for the R4000!
 */

extern __inline__ void set_bit(int nr, void *addr)
{
	int	mask, mw;

	addr += ((nr >> 3) & ~3);
	mask = 1 << (nr & 0x1f);
	do {
		mw = load_linked(addr);
	} while (!store_conditional(addr, mw|mask));
}

extern __inline__ void clear_bit(int nr, void *addr)
{
	int	mask, mw;

	addr += ((nr >> 3) & ~3);
	mask = 1 << (nr & 0x1f);
	do {
		mw = load_linked(addr);
		}
	while (!store_conditional(addr, mw & ~mask));
}

extern __inline__ void change_bit(int nr, void *addr)
{
	int	mask, mw;

	addr += ((nr >> 3) & ~3);
	mask = 1 << (nr & 0x1f);
	do {
		mw = load_linked(addr);
	} while (!store_conditional(addr, mw ^ mask));
}

extern __inline__ int test_and_set_bit(int nr, void *addr)
{
	int	mask, retval, mw;

	addr += ((nr >> 3) & ~3);
	mask = 1 << (nr & 0x1f);
	do {
		mw = load_linked(addr);
		retval = (mask & mw) != 0;
	} while (!store_conditional(addr, mw|mask));

	return retval;
}

extern __inline__ int test_and_clear_bit(int nr, void *addr)
{
	int	mask, retval, mw;

	addr += ((nr >> 3) & ~3);
	mask = 1 << (nr & 0x1f);
	do {
		mw = load_linked(addr);
		retval = (mask & mw) != 0;
		}
	while (!store_conditional(addr, mw & ~mask));

	return retval;
}

extern __inline__ int test_and_change_bit(int nr, void *addr)
{
	int	mask, retval, mw;

	addr += ((nr >> 3) & ~3);
	mask = 1 << (nr & 0x1f);
	do {
		mw = load_linked(addr);
		retval = (mask & mw) != 0;
	} while (!store_conditional(addr, mw ^ mask));

	return retval;
}

#else /* MIPS I */

extern __inline__ void set_bit(int nr, void * addr)
{
	int	mask;
	int	*a = addr;
	__bi_flags;

	a += nr >> 5;
	mask = 1 << (nr & 0x1f);
	__bi_save_and_cli(flags);
	*a |= mask;
	__bi_restore_flags(flags);
}

extern __inline__ void clear_bit(int nr, void * addr)
{
	int	mask;
	int	*a = addr;
	__bi_flags;

	a += nr >> 5;
	mask = 1 << (nr & 0x1f);
	__bi_save_and_cli(flags);
	*a &= ~mask;
	__bi_restore_flags(flags);
}

extern __inline__ void change_bit(int nr, void * addr)
{
	int	mask;
	int	*a = addr;
	__bi_flags;

	a += nr >> 5;
	mask = 1 << (nr & 0x1f);
	__bi_save_and_cli(flags);
	*a ^= mask;
	__bi_restore_flags(flags);
}

extern __inline__ int test_and_set_bit(int nr, void * addr)
{
	int	mask, retval;
	int	*a = addr;
	__bi_flags;

	a += nr >> 5;
	mask = 1 << (nr & 0x1f);
	__bi_save_and_cli(flags);
	retval = (mask & *a) != 0;
	*a |= mask;
	__bi_restore_flags(flags);

	return retval;
}

extern __inline__ int test_and_clear_bit(int nr, void * addr)
{
	int	mask, retval;
	int	*a = addr;
	__bi_flags;

	a += nr >> 5;
	mask = 1 << (nr & 0x1f);
	__bi_save_and_cli(flags);
	retval = (mask & *a) != 0;
	*a &= ~mask;
	__bi_restore_flags(flags);

	return retval;
}

extern __inline__ int test_and_change_bit(int nr, void * addr)
{
	int	mask, retval;
	int	*a = addr;
	__bi_flags;

	a += nr >> 5;
	mask = 1 << (nr & 0x1f);
	__bi_save_and_cli(flags);
	retval = (mask & *a) != 0;
	*a ^= mask;
	__bi_restore_flags(flags);

	return retval;
}

#undef __bi_flags
#undef __bi_cli()
#undef __bi_save_flags(x)
#undef __bi_restore_flags(x)

#endif /* MIPS I */

extern __inline__ int test_bit(int nr, const void *addr)
{
	return ((1UL << (nr & 31)) & (((const unsigned int *) addr)[nr >> 5])) != 0;
}

#ifndef __MIPSEB__

/* Little endian versions. */

extern __inline__ int find_first_zero_bit (void *addr, unsigned size)
{
	unsigned long dummy;
	int res;

	if (!size)
		return 0;

	__asm__ (".set\tnoreorder\n\t"
		".set\tnoat\n"
		"1:\tsubu\t$1,%6,%0\n\t"
		"blez\t$1,2f\n\t"
		"lw\t$1,(%5)\n\t"
		"addiu\t%5,4\n\t"
#if (_MIPS_ISA == _MIPS_ISA_MIPS2) || (_MIPS_ISA == _MIPS_ISA_MIPS3) || \
    (_MIPS_ISA == _MIPS_ISA_MIPS4) || (_MIPS_ISA == _MIPS_ISA_MIPS5)
		"beql\t%1,$1,1b\n\t"
		"addiu\t%0,32\n\t"
#else
		"addiu\t%0,32\n\t"
		"beq\t%1,$1,1b\n\t"
		"nop\n\t"
		"subu\t%0,32\n\t"
#endif
#ifdef __MIPSEB__
#error "Fix this for big endian"
#endif /* __MIPSEB__ */
		"li\t%1,1\n"
		"1:\tand\t%2,$1,%1\n\t"
		"beqz\t%2,2f\n\t"
		"sll\t%1,%1,1\n\t"
		"bnez\t%1,1b\n\t"
		"add\t%0,%0,1\n\t"
		".set\tat\n\t"
		".set\treorder\n"
		"2:"
		: "=r" (res),
		  "=r" (dummy),
		  "=r" (addr)
		: "0" ((signed int) 0),
		  "1" ((unsigned int) 0xffffffff),
		  "2" (addr),
		  "r" (size)
		: "$1");

	return res;
}

extern __inline__ int find_next_zero_bit (void * addr, int size, int offset)
{
	unsigned int *p = ((unsigned int *) addr) + (offset >> 5);
	int set = 0, bit = offset & 31, res;
	unsigned long dummy;
	
	if (bit) {
		/*
		 * Look for zero in first byte
		 */
#ifdef __MIPSEB__
#error "Fix this for big endian byte order"
#endif
		__asm__(".set\tnoreorder\n\t"
			".set\tnoat\n"
			"1:\tand\t$1,%4,%1\n\t"
			"beqz\t$1,1f\n\t"
			"sll\t%1,%1,1\n\t"
			"bnez\t%1,1b\n\t"
			"addiu\t%0,1\n\t"
			".set\tat\n\t"
			".set\treorder\n"
			"1:"
			: "=r" (set),
			  "=r" (dummy)
			: "0" (0),
			  "1" (1 << bit),
			  "r" (*p)
			: "$1");
		if (set < (32 - bit))
			return set + offset;
		set = 32 - bit;
		p++;
	}
	/*
	 * No zero yet, search remaining full bytes for a zero
	 */
	res = find_first_zero_bit(p, size - 32 * (p - (unsigned int *) addr));
	return offset + set + res;
}

#endif /* !(__MIPSEB__) */

/*
 * ffz = Find First Zero in word. Undefined if no zero exists,
 * so code should check against ~0UL first..
 */
extern __inline__ unsigned long ffz(unsigned long word)
{
	unsigned int	__res;
	unsigned int	mask = 1;

	__asm__ (
		".set\tnoreorder\n\t"
		".set\tnoat\n\t"
		"move\t%0,$0\n"
		"1:\tand\t$1,%2,%1\n\t"
		"beqz\t$1,2f\n\t"
		"sll\t%1,1\n\t"
		"bnez\t%1,1b\n\t"
		"addiu\t%0,1\n\t"
		".set\tat\n\t"
		".set\treorder\n"
		"2:\n\t"
		: "=&r" (__res), "=r" (mask)
		: "r" (word), "1" (mask)
		: "$1");

	return __res;
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

#ifdef __MIPSEB__
/* For now I steal the Sparc C versions, no need for speed, just need to
 * get it working.
 */
/* find_next_zero_bit() finds the first zero bit in a bit string of length
 * 'size' bits, starting the search at bit 'offset'. This is largely based
 * on Linus's ALPHA routines, which are pretty portable BTW.
 */

extern __inline__ int find_next_zero_bit(void *addr, int size, int offset)
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
found_middle:
	return result + ffz(tmp);
}

/* Linus sez that gcc can optimize the following correctly, we'll see if this
 * holds on the Sparc as it does for the ALPHA.
 */

#define find_first_zero_bit(addr, size) \
        find_next_zero_bit((addr), (size), 0)

#endif /* (__MIPSEB__) */

/* Now for the ext2 filesystem bit operations and helper routines. */

#ifdef __MIPSEB__
extern __inline__ int ext2_set_bit(int nr,void * addr)
{
	int		mask, retval, flags;
	unsigned char	*ADDR = (unsigned char *) addr;

	ADDR += nr >> 3;
	mask = 1 << (nr & 0x07);
	save_and_cli(flags);
	retval = (mask & *ADDR) != 0;
	*ADDR |= mask;
	restore_flags(flags);
	return retval;
}

extern __inline__ int ext2_clear_bit(int nr, void * addr)
{
	int		mask, retval, flags;
	unsigned char	*ADDR = (unsigned char *) addr;

	ADDR += nr >> 3;
	mask = 1 << (nr & 0x07);
	save_and_cli(flags);
	retval = (mask & *ADDR) != 0;
	*ADDR &= ~mask;
	restore_flags(flags);
	return retval;
}

extern __inline__ int ext2_test_bit(int nr, const void * addr)
{
	int			mask;
	const unsigned char	*ADDR = (const unsigned char *) addr;

	ADDR += nr >> 3;
	mask = 1 << (nr & 0x07);
	return ((mask & *ADDR) != 0);
}

#define ext2_find_first_zero_bit(addr, size) \
        ext2_find_next_zero_bit((addr), (size), 0)

extern __inline__ unsigned long ext2_find_next_zero_bit(void *addr, unsigned long size, unsigned long offset)
{
	unsigned long *p = ((unsigned long *) addr) + (offset >> 5);
	unsigned long result = offset & ~31UL;
	unsigned long tmp;

	if (offset >= size)
		return size;
	size -= result;
	offset &= 31UL;
	if(offset) {
		/* We hold the little endian value in tmp, but then the
		 * shift is illegal. So we could keep a big endian value
		 * in tmp, like this:
		 *
		 * tmp = __swab32(*(p++));
		 * tmp |= ~0UL >> (32-offset);
		 *
		 * but this would decrease preformance, so we change the
		 * shift:
		 */
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
	/* tmp is little endian, so we would have to swab the shift,
	 * see above. But then we have to swab tmp below for ffz, so
	 * we might as well do this here.
	 */
	return result + ffz(__swab32(tmp) | (~0UL << size));
found_middle:
	return result + ffz(__swab32(tmp));
}
#else /* !(__MIPSEB__) */

/* Native ext2 byte ordering, just collapse using defines. */
#define ext2_set_bit(nr, addr) test_and_set_bit((nr), (addr))
#define ext2_clear_bit(nr, addr) test_and_clear_bit((nr), (addr))
#define ext2_test_bit(nr, addr) test_bit((nr), (addr))
#define ext2_find_first_zero_bit(addr, size) find_first_zero_bit((addr), (size))
#define ext2_find_next_zero_bit(addr, size, offset) \
                find_next_zero_bit((addr), (size), (offset))
 
#endif /* !(__MIPSEB__) */

/*
 * Bitmap functions for the minix filesystem.
 * FIXME: These assume that Minix uses the native byte/bitorder.
 * This limits the Minix filesystem's value for data exchange very much.
 */
#define minix_test_and_set_bit(nr,addr) test_and_set_bit(nr,addr)
#define minix_set_bit(nr,addr) set_bit(nr,addr)
#define minix_test_and_clear_bit(nr,addr) test_and_clear_bit(nr,addr)
#define minix_test_bit(nr,addr) test_bit(nr,addr)
#define minix_find_first_zero_bit(addr,size) find_first_zero_bit(addr,size)

#endif /* _ASM_BITOPS_H */
