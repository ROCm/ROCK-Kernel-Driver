#ifndef _H8300_BITOPS_H
#define _H8300_BITOPS_H

/*
 * Copyright 1992, Linus Torvalds.
 * Copyright 2002, Yoshinori Sato
 */

#include <linux/kernel.h>
#include <linux/config.h>
#include <linux/compiler.h>
#include <asm/byteorder.h>	/* swab32 */

#ifdef __KERNEL__
/*
 * Function prototypes to keep gcc -Wall happy
 */

/*
 * The __ functions are not atomic
 */

extern void set_bit(int nr, volatile unsigned long* addr);
extern void clear_bit(int nr, volatile unsigned long* addr);
extern void change_bit(int nr, volatile unsigned long* addr);
extern int test_and_set_bit(int nr, volatile unsigned long* addr);
extern int __test_and_set_bit(int nr, volatile unsigned long* addr);
extern int test_and_clear_bit(int nr, volatile unsigned long* addr);
extern int __test_and_clear_bit(int nr, volatile unsigned long* addr);
extern int test_and_change_bit(int nr, volatile unsigned long* addr);
extern int __test_and_change_bit(int nr, volatile unsigned long* addr);
extern int __constant_test_bit(int nr, const volatile unsigned long* addr);
extern int __test_bit(int nr, volatile unsigned long* addr);
extern int find_first_zero_bit(void * addr, unsigned size);
extern int find_next_zero_bit (void * addr, int size, int offset);

/*
 * ffz = Find First Zero in word. Undefined if no zero exists,
 * so code should check against ~0UL first..
 */
extern __inline__ unsigned long ffz(unsigned long word)
{
	unsigned long result;

	__asm__("sub.l %0,%0\n\t"
		"dec.l #1,%0\n"
		"1:\n\t"
		"shlr.l %1\n\t"
		"adds #1,%0\n\t"
		"bcs 1b"
		: "=r" (result) : "r" (word));
	return result;
}

extern __inline__ void set_bit(int nr, volatile unsigned long* addr)
{
	unsigned char *a = (unsigned char *) addr;
	a += ((nr >> 3) & ~3) + (3 - ((nr >> 3) & 3));
	__asm__("mov.l %0,er0\n\t"
		"bset r0l,@%1"
		::"r"(nr & 7),"r"(a):"er0","er1");
}
/* Bigendian is complexed... */

#define __set_bit(nr, addr) set_bit(nr, addr)

/*
 * clear_bit() doesn't provide any barrier for the compiler.
 */
#define smp_mb__before_clear_bit()	barrier()
#define smp_mb__after_clear_bit()	barrier()

extern __inline__ void clear_bit(int nr, volatile unsigned long* addr)
{
	unsigned char *a = (unsigned char *) addr;
	a += ((nr >> 3) & ~3) + (3 - ((nr >> 3) & 3));
	__asm__("mov.l %0,er0\n\t"
		"bclr r0l,@%1"
		::"r"(nr & 7),"r"(a):"er0");
}

#define __clear_bit(nr, addr) clear_bit(nr, addr)

extern __inline__ void change_bit(int nr, volatile unsigned long* addr)
{
	unsigned char *a = (unsigned char *) addr;
	a += ((nr >> 3) & ~3) + (3 - ((nr >> 3) & 3));
	__asm__("mov.l %0,er0\n\t"
		"bnot r0l,@%1"
		::"r"(nr & 7),"r"(a):"er0");
}

#define __change_bit(nr, addr) change_bit(nr, addr)

extern __inline__ int test_and_set_bit(int nr, volatile unsigned long* addr)
{
	int retval;
	unsigned char *a;
	a = (unsigned char *) addr;

	a += ((nr >> 3) & ~3) + (3 - ((nr >> 3) & 3));
	__asm__("mov.l %1,er0\n\t"
		"stc ccr,r0h\n\t"
		"orc #0x80,ccr\n\t"
		"btst r0l,@%2\n\t"
		"bset r0l,@%2\n\t"
		"stc ccr,r0l\n\t"
		"ldc r0h,ccr\n\t"
		"btst #2,r0l\n\t"
		"bne 1f\n\t"
		"sub.l %0,%0\n\t"
		"inc.l #1,%0\n"
		"bra 2f\n"
		"1:\n\t"
		"sub.l %0,%0\n"
		"2:"
		: "=r"(retval) :"r"(nr & 7),"r"(a):"er0");
	return retval;
}

extern __inline__ int __test_and_set_bit(int nr, volatile unsigned long* addr)
{
	int retval;
	unsigned char *a = (unsigned char *) addr;

	a += ((nr >> 3) & ~3) + (3 - ((nr >> 3) & 3));
	__asm__("mov.l %1,er0\n\t"
		"btst r0l,@%2\n\t"
		"bset r0l,@%2\n\t"
		"beq 1f\n\t"
		"sub.l %0,%0\n\t"
		"inc.l #1,%0\n"
		"bra 2f\n"
		"1:\n\t"
		"sub.l %0,%0\n"
		"2:"
		: "=r"(retval) :"r"(nr & 7),"r"(a):"er0");
	return retval;
}

extern __inline__ int test_and_clear_bit(int nr, volatile unsigned long* addr)
{
	int retval;
	unsigned char *a = (unsigned char *) addr;

	a += ((nr >> 3) & ~3) + (3 - ((nr >> 3) & 3));
	__asm__("mov.l %1,er0\n\t"
		"stc ccr,r0h\n\t"
		"orc #0x80,ccr\n\t"
		"btst r0l,@%2\n\t"
		"bclr r0l,@%2\n\t"
		"stc ccr,r0l\n\t"
		"ldc r0h,ccr\n\t"
		"btst #2,r0l\n\t"
		"bne 1f\n\t"
		"sub.l %0,%0\n\t"
		"inc.l #1,%0\n"
		"bra 2f\n"
		"1:\n\t"
		"sub.l %0,%0\n"
		"2:"
		: "=r"(retval) :"r"(nr & 7),"r"(a):"er0");
	return retval;
}

extern __inline__ int __test_and_clear_bit(int nr, volatile unsigned long* addr)
{
	int retval;
	unsigned char *a = (unsigned char *) addr;

	a += ((nr >> 3) & ~3) + (3 - ((nr >> 3) & 3));
	__asm__("mov.l %1,er0\n\t"
		"btst r0l,@%2\n\t"
		"bclr r0l,@%2\n\t"
		"beq 1f\n\t"
		"sub.l %0,%0\n\t"
		"inc.l #1,%0\n"
		"bra 2f\n"
		"1:\n\t"
		"sub.l %0,%0\n"
		"2:"
		: "=r"(retval) :"r"(nr & 7),"r"(a):"er0");
	return retval;
}

extern __inline__ int test_and_change_bit(int nr, volatile unsigned long* addr)
{
	int retval;
	unsigned char *a = (unsigned char *) addr;

	a += ((nr >> 3) & ~3) + (3 - ((nr >> 3) & 3));
	__asm__("mov.l %1,er0\n\t"
		"stc ccr,r0h\n\t"
		"orc #0x80,ccr\n\t"
		"btst r0l,@%2\n\t"
		"bnot r0l,@%2\n\t"
		"stc ccr,r0l\n\t"
		"ldc r0h,ccr\n\t"
		"btst #2,r0l\n\t"
		"bne 1f\n\t"
		"sub.l %0,%0\n\t"
		"inc.l #1,%0\n"
		"bra 2f\n"
		"1:\n\t"
		"sub.l %0,%0\n"
		"2:"
		: "=r"(retval) :"r"(nr & 7),"r"(a):"er0");
	return retval;
}

extern __inline__ int __test_and_change_bit(int nr, volatile unsigned long* addr)
{
	int retval;
	unsigned char *a = (unsigned char *) addr;

	a += ((nr >> 3) & ~3) + (3 - ((nr >> 3) & 3));
	__asm__("mov.l %1,er0\n\t"
		"btst r0l,@%2\n\t"
		"bnot r0l,@%2\n\t"
		"beq 1f\n\t"
		"sub.l %0,%0\n\t"
		"inc.l #1,%0\n"
		"bra 2f\n"
		"1:\n\t"
		"sub.l %0,%0\n"
		"2:"
		: "=r"(retval) :"r"(nr & 7),"r"(a):"er0");
	return retval;
}

/*
 * This routine doesn't need to be atomic.
 */
extern __inline__ int __constant_test_bit(int nr, const volatile unsigned long* addr)
{
	return ((1UL << (nr & 7)) & 
               (((const volatile unsigned char *) addr)
               [((nr >> 3) & ~3) + 3 - ((nr >> 3) & 3)])) != 0;
}

extern __inline__ int __test_bit(int nr, volatile unsigned long* addr)
{
	int retval;
	unsigned char *a = (unsigned char *) addr;

	a += ((nr >> 3) & ~3) + (3 - ((nr >> 3) & 3));
	__asm__("mov.l %1,er0\n\t"
		"btst r0l,@%2\n\t"
		"beq 1f\n\t"
		"sub.l %0,%0\n\t"
		"inc.l #1,%0\n"
		"bra 2f\n"
		"1:\n\t"
		"sub.l %0,%0\n"
		"2:"
		: "=r"(retval) :"r"(nr & 7),"r"(a):"er0");
	return retval;
}

#define test_bit(nr,addr) \
(__builtin_constant_p(nr) ? \
 __constant_test_bit((nr),(addr)) : \
 __test_bit((nr),(addr)))


#define find_first_zero_bit(addr, size) \
        find_next_zero_bit((addr), (size), 0)

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

extern __inline__ unsigned long ffs(unsigned long word)
{
	unsigned long result;

	__asm__("sub.l er0,er0\n\t"
		"dec.l #1,er0\n"
		"1:\n\t"
		"shlr.l %1\n\t"
		"adds #1,er0\n\t"
		"bcc 1b\n\t"
		"mov.l er0,%0"
		: "=r" (result) : "r"(word) : "er0");
	return result;
}

#define __ffs(x) ffs(x)

/*
 * fls: find last bit set.
 */
#define fls(x) generic_fls(x)

/*
 * Every architecture must define this function. It's the fastest
 * way of searching a 140-bit bitmap where the first 100 bits are
 * unlikely to be set. It's guaranteed that at least one of the 140
 * bits is cleared.
 */
static inline int sched_find_first_bit(unsigned long *b)
{
	if (unlikely(b[0]))
		return __ffs(b[0]);
	if (unlikely(b[1]))
		return __ffs(b[1]) + 32;
	if (unlikely(b[2]))
		return __ffs(b[2]) + 64;
	if (b[3])
		return __ffs(b[3]) + 96;
	return __ffs(b[4]) + 128;
}

/*
 * hweightN: returns the hamming weight (i.e. the number
 * of bits set) of a N-bit word
 */

#define hweight32(x) generic_hweight32(x)
#define hweight16(x) generic_hweight16(x)
#define hweight8(x) generic_hweight8(x)

extern __inline__ int ext2_set_bit(int nr, volatile void *addr)
{
	unsigned char *a = (unsigned char *) addr;
	register unsigned short r __asm__("er0");
	a += nr >> 3;
	__asm__("mov.l %1,er0\n\t"
		"sub.w e0,e0\n\t"
		"btst r0l,@%2\n\t"
		"bset r0l,@%2\n\t"
		"beq 1f\n\t"
		"inc.w #1,e0\n"
		"1:\n\t"
		"mov.w e0,r0\n\t"
		"sub.w e0,e0"
		:"=r"(r):"r"(nr & 7),"r"(a));
	return r;
}

extern __inline__ int ext2_clear_bit(int nr, volatile void *addr)
{
	unsigned char *a = (unsigned char *) addr;
	register unsigned short r __asm__("er0");
	a += nr >> 3;
	__asm__("mov.l %1,er0\n\t"
		"sub.w e0,e0\n\t"
		"btst r0l,@%2\n\t"
		"bclr r0l,@%2\n\t"
		"beq 1f\n\t"
		"inc.w #1,e0\n"
		"1:\n\t"
		"mov.w e0,r0\n\t"
		"sub.w e0,e0"
		:"=r"(r):"r"(nr & 7),"r"(a));
	return r;
}

extern __inline__ int ext2_test_bit(int nr, volatile void *addr)
{
	unsigned char *a = (unsigned char *) addr;
	int ret;
	a += nr >> 3;
	__asm__("mov.l %1,er0\n\t"
		"sub.l %0,%0\n\t"
		"btst r0l,@%2\n\t"
		"beq 1f\n\t"
		"inc.l #1,%0\n"
		"1:"
		: "=r"(ret) :"r"(nr & 7),"r"(a):"er0","er1");
	return ret;
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
		 * but this would decrease performance, so we change the
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

/* Bitmap functions for the minix filesystem.  */
#define minix_test_and_set_bit(nr,addr) test_and_set_bit(nr,addr)
#define minix_set_bit(nr,addr) set_bit(nr,addr)
#define minix_test_and_clear_bit(nr,addr) test_and_clear_bit(nr,addr)
#define minix_test_bit(nr,addr) test_bit(nr,addr)
#define minix_find_first_zero_bit(addr,size) find_first_zero_bit(addr,size)

/**
 * hweightN - returns the hamming weight of a N-bit word
 * @x: the word to weigh
 *
 * The Hamming Weight of a number is the total number of bits set in it.
 */

#define hweight32(x) generic_hweight32(x)
#define hweight16(x) generic_hweight16(x)
#define hweight8(x) generic_hweight8(x)

#endif /* __KERNEL__ */

#endif /* _H8300_BITOPS_H */
