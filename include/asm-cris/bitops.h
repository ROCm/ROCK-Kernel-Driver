/* $Id: bitops.h,v 1.3 2000/10/17 14:56:27 bjornw Exp $ */
/* all of these should probably be rewritten in assembler for speed. */

#ifndef _CRIS_BITOPS_H
#define _CRIS_BITOPS_H

#include <asm/system.h>

/*
 * These have to be done with inline assembly: that way the bit-setting
 * is guaranteed to be atomic. All bit operations return 0 if the bit
 * was cleared before the operation and != 0 if it was not.
 *
 * bit 0 is the LSB of addr; bit 32 is the LSB of (addr+1).
 */

/*
 * Some hacks to defeat gcc over-optimizations..
 */
struct __dummy { unsigned long a[100]; };
#define ADDR (*(struct __dummy *) addr)
#define CONST_ADDR (*(const struct __dummy *) addr)

#define set_bit(nr, addr)    (void)test_and_set_bit(nr, addr)
#define clear_bit(nr, addr)  (void)test_and_clear_bit(nr, addr)
#define change_bit(nr, addr) (void)test_and_change_bit(nr, addr)

extern __inline__ int test_and_set_bit(int nr, void *addr)
{
	unsigned int mask, retval;
	unsigned long flags;
	unsigned int *adr = (unsigned int *)addr;
	
	adr += nr >> 5;
	mask = 1 << (nr & 0x1f);
	save_flags(flags);
	cli();
	retval = (mask & *adr) != 0;
	*adr |= mask;
	restore_flags(flags);
	return retval;
}

/*
 * clear_bit() doesn't provide any barrier for the compiler.
 */
#define smp_mb__before_clear_bit()      barrier()
#define smp_mb__after_clear_bit()       barrier()

extern __inline__ int test_and_clear_bit(int nr, void *addr)
{
	unsigned int mask, retval;
	unsigned long flags;
	unsigned int *adr = (unsigned int *)addr;
	
	adr += nr >> 5;
	mask = 1 << (nr & 0x1f);
	save_flags(flags);
	cli();
	retval = (mask & *adr) != 0;
	*adr &= ~mask;
	restore_flags(flags);
	return retval;
}

extern __inline__ int test_and_change_bit(int nr, void *addr)
{
	unsigned int mask, retval;
	unsigned long flags;
	unsigned int *adr = (unsigned int *)addr;
	adr += nr >> 5;
	mask = 1 << (nr & 0x1f);
	save_flags(flags);
	cli();
	retval = (mask & *adr) != 0;
	*adr ^= mask;
	restore_flags(flags);
	return retval;
}

/*
 * This routine doesn't need to be atomic.
 */
extern __inline__ int test_bit(int nr, const void *addr)
{
	unsigned int mask;
	unsigned int *adr = (unsigned int *)addr;
	
	adr += nr >> 5;
	mask = 1 << (nr & 0x1f);
	return ((mask & *adr) != 0);
}

/*
 * Find-bit routines..
 */

/*
 * ffz = Find First Zero in word. Undefined if no zero exists,
 * so code should check against ~0UL first..
 */
extern __inline__ unsigned long ffz(unsigned long word)
{
	unsigned long result = 0;
	
	while(word & 1) {
		result++;
		word >>= 1;
	}
	return result;
}

/*
 * Find first one in word. Undefined if no one exists,
 * so code should check against 0UL first..
 */
extern __inline__ unsigned long find_first_one(unsigned long word)
{
	unsigned long result = 0;
	
	while(!(word & 1)) {
		result++;
		word >>= 1;
	}
	return result;
}

extern __inline__ int find_next_zero_bit (void * addr, int size, int offset)
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
	tmp |= ~0UL >> size;
 found_middle:
	return result + ffz(tmp);
}

#define find_first_zero_bit(addr, size) \
        find_next_zero_bit((addr), (size), 0)

#ifdef __KERNEL__ 

#define ext2_set_bit                 test_and_set_bit
#define ext2_clear_bit               test_and_clear_bit
#define ext2_test_bit                test_bit
#define ext2_find_first_zero_bit     find_first_zero_bit
#define ext2_find_next_zero_bit      find_next_zero_bit

/* Bitmap functions for the minix filesystem.  */
#define minix_set_bit(nr,addr) test_and_set_bit(nr,addr)
#define minix_clear_bit(nr,addr) test_and_clear_bit(nr,addr)
#define minix_test_bit(nr,addr) test_bit(nr,addr)
#define minix_find_first_zero_bit(addr,size) find_first_zero_bit(addr,size)

#endif /* __KERNEL__ */


#endif /* _CRIS_BITOPS_H */
