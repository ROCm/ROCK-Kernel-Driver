#ifndef __LINUX_BITMAP_H
#define __LINUX_BITMAP_H

#ifndef __ASSEMBLY__

#include <linux/config.h>
#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/string.h>

static inline int bitmap_empty(const unsigned long *bitmap, int bits)
{
	int k, lim = bits/BITS_PER_LONG;
	for (k = 0; k < lim; ++k)
		if (bitmap[k])
			return 0;

	if (bits % BITS_PER_LONG)
		if (bitmap[k] & ((1UL << (bits % BITS_PER_LONG)) - 1))
			return 0;

	return 1;
}

static inline int bitmap_full(const unsigned long *bitmap, int bits)
{
	int k, lim = bits/BITS_PER_LONG;
	for (k = 0; k < lim; ++k)
		if (~bitmap[k])
			return 0;

	if (bits % BITS_PER_LONG)
		if (~bitmap[k] & ((1UL << (bits % BITS_PER_LONG)) - 1))
			return 0;

	return 1;
}

static inline int bitmap_equal(const unsigned long *bitmap1,
				unsigned long *bitmap2, int bits)
{
	int k, lim = bits/BITS_PER_LONG;;
	for (k = 0; k < lim; ++k)
		if (bitmap1[k] != bitmap2[k])
			return 0;

	if (bits % BITS_PER_LONG)
		if ((bitmap1[k] ^ bitmap2[k]) &
				((1UL << (bits % BITS_PER_LONG)) - 1))
			return 0;

	return 1;
}

static inline void bitmap_complement(unsigned long *bitmap, int bits)
{
	int k;

	for (k = 0; k < BITS_TO_LONGS(bits); ++k)
		bitmap[k] = ~bitmap[k];
}

static inline void bitmap_clear(unsigned long *bitmap, int bits)
{
	CLEAR_BITMAP((unsigned long *)bitmap, bits);
}

static inline void bitmap_fill(unsigned long *bitmap, int bits)
{
	memset(bitmap, 0xff, BITS_TO_LONGS(bits)*sizeof(unsigned long));
}

static inline void bitmap_copy(unsigned long *dst,
			const unsigned long *src, int bits)
{
	memcpy(dst, src, BITS_TO_LONGS(bits)*sizeof(unsigned long));
}

static inline void bitmap_shift_right(unsigned long *dst,
				const unsigned long *src, int shift, int bits)
{
	int k;
	DECLARE_BITMAP(__shr_tmp, bits);

	bitmap_clear(__shr_tmp, bits);
	for (k = 0; k < bits - shift; ++k)
		if (test_bit(k + shift, src))
			set_bit(k, __shr_tmp);
	bitmap_copy(dst, __shr_tmp, bits);
}

static inline void bitmap_shift_left(unsigned long *dst,
				const unsigned long *src, int shift, int bits)
{
	int k;
	DECLARE_BITMAP(__shl_tmp, bits);

	bitmap_clear(__shl_tmp, bits);
	for (k = bits; k >= shift; --k)
		if (test_bit(k - shift, src))
			set_bit(k, __shl_tmp);
	bitmap_copy(dst, __shl_tmp, bits);
}

static inline void bitmap_and(unsigned long *dst, const unsigned long *bitmap1,
				const unsigned long *bitmap2, int bits)
{
	int k;
	int nr = BITS_TO_LONGS(bits);

	for (k = 0; k < nr; k++)
		dst[k] = bitmap1[k] & bitmap2[k];
}

static inline void bitmap_or(unsigned long *dst, const unsigned long *bitmap1,
				const unsigned long *bitmap2, int bits)
{
	int k;
	int nr = BITS_TO_LONGS(bits);

	for (k = 0; k < nr; k++)
		dst[k] = bitmap1[k] | bitmap2[k];
}

#if BITS_PER_LONG == 32
static inline int bitmap_weight(const unsigned long *bitmap, int bits)
{
	int k, w = 0, lim = bits/BITS_PER_LONG;

	for (k = 0; k < lim; k++)
		w += hweight32(bitmap[k]);

	if (bits % BITS_PER_LONG)
		w += hweight32(bitmap[k] &
				((1UL << (bits % BITS_PER_LONG)) - 1));

	return w;
}
#else
static inline int bitmap_weight(const unsigned long *bitmap, int bits)
{
	int k, w = 0, lim = bits/BITS_PER_LONG;

	for (k = 0; k < lim; k++)
		w += hweight64(bitmap[k]);

	if (bits % BITS_PER_LONG)
		w += hweight64(bitmap[k] &
				((1UL << (bits % BITS_PER_LONG)) - 1));

	return w;
}
#endif

#endif /* __ASSEMBLY__ */

#endif /* __LINUX_BITMAP_H */
