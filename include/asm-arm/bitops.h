/*
 * Copyright 1995, Russell King.
 * Various bits and pieces copyrights include:
 *  Linus Torvalds (test_bit).
 * Big endian support: Copyright 2001, Nicolas Pitre
 *  reworked by rmk.
 *
 * bit 0 is the LSB of addr; bit 32 is the LSB of (addr+1).
 *
 * Please note that the code in this file should never be included
 * from user space.  Many of these are not implemented in assembler
 * since they would be too costly.  Also, they require priviledged
 * instructions (which are not available from user mode) to ensure
 * that they are atomic.
 */

#ifndef __ASM_ARM_BITOPS_H
#define __ASM_ARM_BITOPS_H

#ifdef __KERNEL__

#include <asm/system.h>

#define smp_mb__before_clear_bit()	do { } while (0)
#define smp_mb__after_clear_bit()	do { } while (0)

/*
 * These functions are the basis of our bit ops.
 * First, the atomic bitops.
 *
 * The endian issue for these functions is handled by the macros below.
 */
static inline void
____atomic_set_bit_mask(unsigned int mask, volatile unsigned char *p)
{
	unsigned long flags;

	local_irq_save(flags);
	*p |= mask;
	local_irq_restore(flags);
}

static inline void
____atomic_clear_bit_mask(unsigned int mask, volatile unsigned char *p)
{
	unsigned long flags;

	local_irq_save(flags);
	*p &= ~mask;
	local_irq_restore(flags);
}

static inline void
____atomic_change_bit_mask(unsigned int mask, volatile unsigned char *p)
{
	unsigned long flags;

	local_irq_save(flags);
	*p ^= mask;
	local_irq_restore(flags);
}

static inline int
____atomic_test_and_set_bit_mask(unsigned int mask, volatile unsigned char *p)
{
	unsigned long flags;
	unsigned int res;

	local_irq_save(flags);
	res = *p;
	*p = res | mask;
	local_irq_restore(flags);

	return res & mask;
}

static inline int
____atomic_test_and_clear_bit_mask(unsigned int mask, volatile unsigned char *p)
{
	unsigned long flags;
	unsigned int res;

	local_irq_save(flags);
	res = *p;
	*p = res & ~mask;
	local_irq_restore(flags);

	return res & mask;
}

static inline int
____atomic_test_and_change_bit_mask(unsigned int mask, volatile unsigned char *p)
{
	unsigned long flags;
	unsigned int res;

	local_irq_save(flags);
	res = *p;
	*p = res ^ mask;
	local_irq_restore(flags);

	return res & mask;
}

/*
 * Now the non-atomic variants.  We let the compiler handle all optimisations
 * for these.
 */
static inline void ____nonatomic_set_bit(int nr, volatile void *p)
{
	((unsigned char *) p)[nr >> 3] |= (1U << (nr & 7));
}

static inline void ____nonatomic_clear_bit(int nr, volatile void *p)
{
	((unsigned char *) p)[nr >> 3] &= ~(1U << (nr & 7));
}

static inline void ____nonatomic_change_bit(int nr, volatile void *p)
{
	((unsigned char *) p)[nr >> 3] ^= (1U << (nr & 7));
}

static inline int ____nonatomic_test_and_set_bit(int nr, volatile void *p)
{
	unsigned int mask = 1 << (nr & 7);
	unsigned int oldval;

	oldval = ((unsigned char *) p)[nr >> 3];
	((unsigned char *) p)[nr >> 3] = oldval | mask;
	return oldval & mask;
}

static inline int ____nonatomic_test_and_clear_bit(int nr, volatile void *p)
{
	unsigned int mask = 1 << (nr & 7);
	unsigned int oldval;

	oldval = ((unsigned char *) p)[nr >> 3];
	((unsigned char *) p)[nr >> 3] = oldval & ~mask;
	return oldval & mask;
}

static inline int ____nonatomic_test_and_change_bit(int nr, volatile void *p)
{
	unsigned int mask = 1 << (nr & 7);
	unsigned int oldval;

	oldval = ((unsigned char *) p)[nr >> 3];
	((unsigned char *) p)[nr >> 3] = oldval ^ mask;
	return oldval & mask;
}

/*
 * This routine doesn't need to be atomic.
 */
static inline int ____test_bit(int nr, const void * p)
{
    return ((volatile unsigned char *) p)[nr >> 3] & (1U << (nr & 7));
}	

/*
 *  A note about Endian-ness.
 *  -------------------------
 *
 * When the ARM is put into big endian mode via CR15, the processor
 * merely swaps the order of bytes within words, thus:
 *
 *          ------------ physical data bus bits -----------
 *          D31 ... D24  D23 ... D16  D15 ... D8  D7 ... D0
 * little     byte 3       byte 2       byte 1      byte 0
 * big        byte 0       byte 1       byte 2      byte 3
 *
 * This means that reading a 32-bit word at address 0 returns the same
 * value irrespective of the endian mode bit.
 *
 * Peripheral devices should be connected with the data bus reversed in
 * "Big Endian" mode.  ARM Application Note 61 is applicable, and is
 * available from http://www.arm.com/.
 *
 * The following assumes that the data bus connectivity for big endian
 * mode has been followed.
 *
 * Note that bit 0 is defined to be 32-bit word bit 0, not byte 0 bit 0.
 */

/*
 * Little endian assembly bitops.  nr = 0 -> byte 0 bit 0.
 */
extern void _set_bit_le(int nr, volatile void * p);
extern void _clear_bit_le(int nr, volatile void * p);
extern void _change_bit_le(int nr, volatile void * p);
extern int _test_and_set_bit_le(int nr, volatile void * p);
extern int _test_and_clear_bit_le(int nr, volatile void * p);
extern int _test_and_change_bit_le(int nr, volatile void * p);
extern int _find_first_zero_bit_le(void * p, unsigned size);
extern int _find_next_zero_bit_le(void * p, int size, int offset);

/*
 * Big endian assembly bitops.  nr = 0 -> byte 3 bit 0.
 */
extern void _set_bit_be(int nr, volatile void * p);
extern void _clear_bit_be(int nr, volatile void * p);
extern void _change_bit_be(int nr, volatile void * p);
extern int _test_and_set_bit_be(int nr, volatile void * p);
extern int _test_and_clear_bit_be(int nr, volatile void * p);
extern int _test_and_change_bit_be(int nr, volatile void * p);
extern int _find_first_zero_bit_be(void * p, unsigned size);
extern int _find_next_zero_bit_be(void * p, int size, int offset);


/*
 * The __* form of bitops are non-atomic and may be reordered.
 */
#define	ATOMIC_BITOP_LE(name,nr,p)		\
	(__builtin_constant_p(nr) ?		\
	 ____atomic_##name##_mask(1 << ((nr) & 7), \
			((unsigned char *)(p)) + ((nr) >> 3)) : \
	 _##name##_le(nr,p))

#define	ATOMIC_BITOP_BE(name,nr,p)		\
	(__builtin_constant_p(nr) ?		\
	 ____atomic_##name##_mask(1 << ((nr) & 7), \
			((unsigned char *)(p)) + (((nr) >> 3) ^ 3)) : \
	 _##name##_be(nr,p))

#define NONATOMIC_BITOP_LE(name,nr,p)	\
	(____nonatomic_##name(nr, p))

#define NONATOMIC_BITOP_BE(name,nr,p)	\
	(____nonatomic_##name(nr ^ 0x18, p))

#ifndef __ARMEB__
/*
 * These are the little endian, atomic definitions.
 */
#define set_bit(nr,p)			ATOMIC_BITOP_LE(set_bit,nr,p)
#define clear_bit(nr,p)			ATOMIC_BITOP_LE(clear_bit,nr,p)
#define change_bit(nr,p)		ATOMIC_BITOP_LE(change_bit,nr,p)
#define test_and_set_bit(nr,p)		ATOMIC_BITOP_LE(test_and_set_bit,nr,p)
#define test_and_clear_bit(nr,p)	ATOMIC_BITOP_LE(test_and_clear_bit,nr,p)
#define test_and_change_bit(nr,p)	ATOMIC_BITOP_LE(test_and_change_bit,nr,p)
#define test_bit(nr,p)			____test_bit(nr,p)
#define find_first_zero_bit(p,sz)	_find_first_zero_bit_le(p,sz)
#define find_next_zero_bit(p,sz,off)	_find_next_zero_bit_le(p,sz,off)

/*
 * These are the little endian, non-atomic definitions.
 */
#define __set_bit(nr,p)			NONATOMIC_BITOP_LE(set_bit,nr,p)
#define __clear_bit(nr,p)		NONATOMIC_BITOP_LE(clear_bit,nr,p)
#define __change_bit(nr,p)		NONATOMIC_BITOP_LE(change_bit,nr,p)
#define __test_and_set_bit(nr,p)	NONATOMIC_BITOP_LE(test_and_set_bit,nr,p)
#define __test_and_clear_bit(nr,p)	NONATOMIC_BITOP_LE(test_and_clear_bit,nr,p)
#define __test_and_change_bit(nr,p)	NONATOMIC_BITOP_LE(test_and_change_bit,nr,p)
#define __test_bit(nr,p)		____test_bit(nr,p)

#else

/*
 * These are the big endian, atomic definitions.
 */
#define set_bit(nr,p)			ATOMIC_BITOP_BE(set_bit,nr,p)
#define clear_bit(nr,p)			ATOMIC_BITOP_BE(clear_bit,nr,p)
#define change_bit(nr,p)		ATOMIC_BITOP_BE(change_bit,nr,p)
#define test_and_set_bit(nr,p)		ATOMIC_BITOP_BE(test_and_set_bit,nr,p)
#define test_and_clear_bit(nr,p)	ATOMIC_BITOP_BE(test_and_clear_bit,nr,p)
#define test_and_change_bit(nr,p)	ATOMIC_BITOP_BE(test_and_change_bit,nr,p)
#define test_bit(nr,p)			____test_bit((nr) ^ 0x18, p)
#define find_first_zero_bit(p,sz)	_find_first_zero_bit_be(p,sz)
#define find_next_zero_bit(p,sz,off)	_find_next_zero_bit_be(p,sz,off)

/*
 * These are the big endian, non-atomic definitions.
 */
#define __set_bit(nr,p)			NONATOMIC_BITOP_BE(set_bit,nr,p)
#define __clear_bit(nr,p)		NONATOMIC_BITOP_BE(clear_bit,nr,p)
#define __change_bit(nr,p)		NONATOMIC_BITOP_BE(change_bit,nr,p)
#define __test_and_set_bit(nr,p)	NONATOMIC_BITOP_BE(test_and_set_bit,nr,p)
#define __test_and_clear_bit(nr,p)	NONATOMIC_BITOP_BE(test_and_clear_bit,nr,p)
#define __test_and_change_bit(nr,p)	NONATOMIC_BITOP_BE(test_and_change_bit,nr,p)
#define __test_bit(nr,p)		____test_bit((nr) ^ 0x18, p)

#endif

/*
 * ffz = Find First Zero in word. Undefined if no zero exists,
 * so code should check against ~0UL first..
 */
static inline unsigned long ffz(unsigned long word)
{
	int k;

	word = ~word;
	k = 31;
	if (word & 0x0000ffff) { k -= 16; word <<= 16; }
	if (word & 0x00ff0000) { k -= 8;  word <<= 8;  }
	if (word & 0x0f000000) { k -= 4;  word <<= 4;  }
	if (word & 0x30000000) { k -= 2;  word <<= 2;  }
	if (word & 0x40000000) { k -= 1; }
        return k;
}

/*
 * ffz = Find First Zero in word. Undefined if no zero exists,
 * so code should check against ~0UL first..
 */
static inline unsigned long __ffs(unsigned long word)
{
	int k;

	k = 31;
	if (word & 0x0000ffff) { k -= 16; word <<= 16; }
	if (word & 0x00ff0000) { k -= 8;  word <<= 8;  }
	if (word & 0x0f000000) { k -= 4;  word <<= 4;  }
	if (word & 0x30000000) { k -= 2;  word <<= 2;  }
	if (word & 0x40000000) { k -= 1; }
        return k;
}

/*
 * fls: find last bit set.
 */

#define fls(x) generic_fls(x)

/*
 * ffs: find first bit set. This is defined the same way as
 * the libc and compiler builtin ffs routines, therefore
 * differs in spirit from the above ffz (man ffs).
 */

#define ffs(x) generic_ffs(x)

/*
 * Find first bit set in a 168-bit bitmap, where the first
 * 128 bits are unlikely to be set.
 */
static inline int sched_find_first_bit(unsigned long *b)
{
	unsigned long v;
	unsigned int off;

	for (off = 0; v = b[off], off < 4; off++) {
		if (unlikely(v))
			break;
	}
	return __ffs(v) + off * 32;
}

/*
 * hweightN: returns the hamming weight (i.e. the number
 * of bits set) of a N-bit word
 */

#define hweight32(x) generic_hweight32(x)
#define hweight16(x) generic_hweight16(x)
#define hweight8(x) generic_hweight8(x)

/*
 * Ext2 is defined to use little-endian byte ordering.
 * These do not need to be atomic.
 */
#define ext2_set_bit(nr,p)			NONATOMIC_BITOP_LE(test_and_set_bit,nr,p)
#define ext2_clear_bit(nr,p)			NONATOMIC_BITOP_LE(test_and_clear_bit,nr,p)
#define ext2_test_bit(nr,p)			__test_bit(nr,p)
#define ext2_find_first_zero_bit(p,sz)		_find_first_zero_bit_le(p,sz)
#define ext2_find_next_zero_bit(p,sz,off)	_find_next_zero_bit_le(p,sz,off)

/*
 * Minix is defined to use little-endian byte ordering.
 * These do not need to be atomic.
 */
#define minix_set_bit(nr,p)			NONATOMIC_BITOP_LE(set_bit,nr,p)
#define minix_test_bit(nr,p)			__test_bit(nr,p)
#define minix_test_and_set_bit(nr,p)		NONATOMIC_BITOP_LE(test_and_set_bit,nr,p)
#define minix_test_and_clear_bit(nr,p)		NONATOMIC_BITOP_LE(test_and_clear_bit,nr,p)
#define minix_find_first_zero_bit(p,sz)		_find_first_zero_bit_le(p,sz)

#endif /* __KERNEL__ */

#endif /* _ARM_BITOPS_H */
