#ifndef __LINUX_BITMAP_H
#define __LINUX_BITMAP_H

#ifndef __ASSEMBLY__

#include <linux/config.h>
#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/string.h>

int bitmap_empty(const unsigned long *bitmap, int bits);
int bitmap_full(const unsigned long *bitmap, int bits);
int bitmap_equal(const unsigned long *bitmap1,
			unsigned long *bitmap2, int bits);
void bitmap_complement(unsigned long *bitmap, int bits);

static inline void bitmap_zero(unsigned long *bitmap, int bits)
{
	memset(bitmap, 0, BITS_TO_LONGS(bits)*sizeof(unsigned long));
}

static inline void bitmap_fill(unsigned long *bitmap, int bits)
{
	memset(bitmap, 0xff, BITS_TO_LONGS(bits)*sizeof(unsigned long));
}

static inline void bitmap_copy(unsigned long *dst,
			const unsigned long *src, int bits)
{
	int len = BITS_TO_LONGS(bits)*sizeof(unsigned long);
	memcpy(dst, src, len);
}

void bitmap_shift_right(unsigned long *dst,
			const unsigned long *src, int shift, int bits);
void bitmap_shift_left(unsigned long *dst,
			const unsigned long *src, int shift, int bits);
void bitmap_and(unsigned long *dst, const unsigned long *bitmap1,
			const unsigned long *bitmap2, int bits);
void bitmap_or(unsigned long *dst, const unsigned long *bitmap1,
			const unsigned long *bitmap2, int bits);
int bitmap_weight(const unsigned long *bitmap, int bits);
int bitmap_scnprintf(char *buf, unsigned int buflen,
			const unsigned long *maskp, int bits);
int bitmap_parse(const char __user *ubuf, unsigned int ubuflen,
			unsigned long *maskp, int bits);

#endif /* __ASSEMBLY__ */

#endif /* __LINUX_BITMAP_H */
