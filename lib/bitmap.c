/*
 * lib/bitmap.c
 * Helper functions for bitmap.h.
 *
 * This source code is licensed under the GNU General Public License,
 * Version 2.  See the file COPYING for more details.
 */
#include <linux/module.h>
#include <linux/ctype.h>
#include <linux/errno.h>
#include <linux/bitmap.h>
#include <asm/bitops.h>
#include <asm/uaccess.h>

#define MAX_BITMAP_BITS	512U	/* for ia64 NR_CPUS maximum */

int bitmap_empty(const unsigned long *bitmap, int bits)
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
EXPORT_SYMBOL(bitmap_empty);

int bitmap_full(const unsigned long *bitmap, int bits)
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
EXPORT_SYMBOL(bitmap_full);

int bitmap_equal(const unsigned long *bitmap1,
		unsigned long *bitmap2, int bits)
{
	int k, lim = bits/BITS_PER_LONG;
	for (k = 0; k < lim; ++k)
		if (bitmap1[k] != bitmap2[k])
			return 0;

	if (bits % BITS_PER_LONG)
		if ((bitmap1[k] ^ bitmap2[k]) &
				((1UL << (bits % BITS_PER_LONG)) - 1))
			return 0;

	return 1;
}
EXPORT_SYMBOL(bitmap_equal);

void bitmap_complement(unsigned long *bitmap, int bits)
{
	int k;
	int nr = BITS_TO_LONGS(bits);

	for (k = 0; k < nr; ++k)
		bitmap[k] = ~bitmap[k];
}
EXPORT_SYMBOL(bitmap_complement);

/*
 * bitmap_shift_write - logical right shift of the bits in a bitmap
 *   @dst - destination bitmap
 *   @src - source bitmap
 *   @nbits - shift by this many bits
 *   @bits - bitmap size, in bits
 *
 * Shifting right (dividing) means moving bits in the MS -> LS bit
 * direction.  Zeros are fed into the vacated MS positions and the
 * LS bits shifted off the bottom are lost.
 */
void bitmap_shift_right(unsigned long *dst,
			const unsigned long *src, int shift, int bits)
{
	int k;
	DECLARE_BITMAP(__shr_tmp, MAX_BITMAP_BITS);

	BUG_ON(bits > MAX_BITMAP_BITS);
	bitmap_clear(__shr_tmp, bits);
	for (k = 0; k < bits - shift; ++k)
		if (test_bit(k + shift, src))
			set_bit(k, __shr_tmp);
	bitmap_copy(dst, __shr_tmp, bits);
}
EXPORT_SYMBOL(bitmap_shift_right);

/*
 * bitmap_shift_left - logical left shift of the bits in a bitmap
 *   @dst - destination bitmap
 *   @src - source bitmap
 *   @nbits - shift by this many bits
 *   @bits - bitmap size, in bits
 *
 * Shifting left (multiplying) means moving bits in the LS -> MS
 * direction.  Zeros are fed into the vacated LS bit positions
 * and those MS bits shifted off the top are lost.
 */
void bitmap_shift_left(unsigned long *dst,
			const unsigned long *src, int shift, int bits)
{
	int k;
	DECLARE_BITMAP(__shl_tmp, MAX_BITMAP_BITS);

	BUG_ON(bits > MAX_BITMAP_BITS);
	bitmap_clear(__shl_tmp, bits);
	for (k = bits; k >= shift; --k)
		if (test_bit(k - shift, src))
			set_bit(k, __shl_tmp);
	bitmap_copy(dst, __shl_tmp, bits);
}
EXPORT_SYMBOL(bitmap_shift_left);

void bitmap_and(unsigned long *dst, const unsigned long *bitmap1,
				const unsigned long *bitmap2, int bits)
{
	int k;
	int nr = BITS_TO_LONGS(bits);

	for (k = 0; k < nr; k++)
		dst[k] = bitmap1[k] & bitmap2[k];
}
EXPORT_SYMBOL(bitmap_and);

void bitmap_or(unsigned long *dst, const unsigned long *bitmap1,
				const unsigned long *bitmap2, int bits)
{
	int k;
	int nr = BITS_TO_LONGS(bits);

	for (k = 0; k < nr; k++)
		dst[k] = bitmap1[k] | bitmap2[k];
}
EXPORT_SYMBOL(bitmap_or);

#if BITS_PER_LONG == 32
int bitmap_weight(const unsigned long *bitmap, int bits)
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
int bitmap_weight(const unsigned long *bitmap, int bits)
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
EXPORT_SYMBOL(bitmap_weight);

/*
 * Bitmap printing & parsing functions: first version by Bill Irwin,
 * second version by Paul Jackson, third by Joe Korty.
 */

#define CHUNKSZ				32
#define nbits_to_hold_value(val)	fls(val)
#define roundup_power2(val,modulus)	(((val) + (modulus) - 1) & ~((modulus) - 1))
#define unhex(c)			(isdigit(c) ? (c - '0') : (toupper(c) - 'A' + 10))

/**
 * bitmap_scnprintf - convert bitmap to an ASCII hex string.
 * @buf: byte buffer into which string is placed
 * @buflen: reserved size of @buf, in bytes
 * @maskp: pointer to bitmap to convert
 * @nmaskbits: size of bitmap, in bits
 *
 * Exactly @nmaskbits bits are displayed.  Hex digits are grouped into
 * comma-separated sets of eight digits per set.
 */
int bitmap_scnprintf(char *buf, unsigned int buflen,
	const unsigned long *maskp, int nmaskbits)
{
	int i, word, bit, len = 0;
	unsigned long val;
	const char *sep = "";
	int chunksz;
	u32 chunkmask;

	chunksz = nmaskbits & (CHUNKSZ - 1);
	if (chunksz == 0)
		chunksz = CHUNKSZ;

	i = roundup_power2(nmaskbits, CHUNKSZ) - CHUNKSZ;
	for (; i >= 0; i -= CHUNKSZ) {
		chunkmask = ((1ULL << chunksz) - 1);
		word = i / BITS_PER_LONG;
		bit = i % BITS_PER_LONG;
		val = (maskp[word] >> bit) & chunkmask;
		len += scnprintf(buf+len, buflen-len, "%s%0*lx", sep,
			(chunksz+3)/4, val);
		chunksz = CHUNKSZ;
		sep = ",";
	}
	return len;
}
EXPORT_SYMBOL(bitmap_scnprintf);

/**
 * bitmap_parse - convert an ASCII hex string into a bitmap.
 * @buf: pointer to buffer in user space containing string.
 * @buflen: buffer size in bytes.  If string is smaller than this
 *    then it must be terminated with a \0.
 * @maskp: pointer to bitmap array that will contain result.
 * @nmaskbits: size of bitmap, in bits.
 *
 * Commas group hex digits into chunks.  Each chunk defines exactly 32
 * bits of the resultant bitmask.  No chunk may specify a value larger
 * than 32 bits (-EOVERFLOW), and if a chunk specifies a smaller value
 * then leading 0-bits are prepended.  -EINVAL is returned for illegal
 * characters and for grouping errors such as "1,,5", ",44", "," and "".
 * Leading and trailing whitespace accepted, but not embedded whitespace.
 */
int bitmap_parse(const char __user *ubuf, unsigned int ubuflen,
        unsigned long *maskp, int nmaskbits)
{
	int c, old_c, totaldigits, ndigits, nchunks, nbits;
	u32 chunk;

	bitmap_clear(maskp, nmaskbits);

	nchunks = nbits = totaldigits = c = 0;
	do {
		chunk = ndigits = 0;

		/* Get the next chunk of the bitmap */
		while (ubuflen) {
			old_c = c;
			if (get_user(c, ubuf++))
				return -EFAULT;
			ubuflen--;
			if (isspace(c))
				continue;

			/*
			 * If the last character was a space and the current
			 * character isn't '\0', we've got embedded whitespace.
			 * This is a no-no, so throw an error.
			 */
			if (totaldigits && c && isspace(old_c))
				return -EINVAL;

			/* A '\0' or a ',' signal the end of the chunk */
			if (c == '\0' || c == ',')
				break;

			if (!isxdigit(c))
				return -EINVAL;

			/*
			 * Make sure there are at least 4 free bits in 'chunk'.
			 * If not, this hexdigit will overflow 'chunk', so
			 * throw an error.
			 */
			if (chunk & ~((1UL << (CHUNKSZ - 4)) - 1))
				return -EOVERFLOW;

			chunk = (chunk << 4) | unhex(c);
			ndigits++; totaldigits++;
		}
		if (ndigits == 0)
			return -EINVAL;
		if (nchunks == 0 && chunk == 0)
			continue;

		bitmap_shift_left(maskp, maskp, CHUNKSZ, nmaskbits);
		*maskp |= chunk;
		nchunks++;
		nbits += (nchunks == 1) ? nbits_to_hold_value(chunk) : CHUNKSZ;
		if (nbits > nmaskbits)
			return -EOVERFLOW;
	} while (ubuflen && c == ',');

	return 0;
}
EXPORT_SYMBOL(bitmap_parse);
