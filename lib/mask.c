/*
 * lib/mask.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003 Silicon Graphics, Inc.  All Rights Reserved.
 */

/*
 * Routines to manipulate multi-word bit masks, such as cpumasks.
 *
 * The ascii representation of multi-word bit masks displays each
 * 32bit word in hex (not zero filled), and for masks longer than
 * one word, uses a comma separator between words.  Words are
 * displayed in big-endian order most significant first.  And hex
 * digits within a word are also in big-endian order, of course.
 *
 * Examples:
 *   A mask with just bit 0 set displays as "1".
 *   A mask with just bit 127 set displays as "80000000,0,0,0".
 *   A mask with just bit 64 set displays as "1,0,0".
 *   A mask with bits 0, 1, 2, 4, 8, 16, 32 and 64 set displays
 *     as "1,1,10117".  The first "1" is for bit 64, the second
 *     for bit 32, the third for bit 16, and so forth, to the
 *     "7", which is for bits 2, 1 and 0.
 *   A mask with bits 32 through 39 set displays as "ff,0".
 *
 * The internal binary representation of masks is as one or
 * an array of unsigned longs, perhaps wrapped in a struct for
 * convenient use as an lvalue.  The following code doesn't know
 * about any such struct details, relying on inline macros in
 * files such as cpumask.h to pass in an unsigned long pointer
 * and a length (in bytes), describing the mask contents.
 * The 32bit words in the array are in little-endian order,
 * low order word first.  Beware that this is the reverse order
 * of the ascii representation.
 *
 * Even though the size of the input mask is provided in bytes,
 * the following code may assume that the mask is a multiple of
 * 32 or 64 bit words long, and ignore any fractional portion
 * of a word at the end.  The main reason the size is passed in
 * bytes is because it is so easy to write 'sizeof(somemask_t)'
 * in the macros.
 *
 * Masks are not a single,simple type, like classic 'C'
 * nul-term strings.  Rather they are a family of types, one
 * for each different length.  Inline macros are used to pick
 * up the actual length, where it is known to the compiler, and
 * pass it down to these routines, which work on any specified
 * length array of unsigned longs.  Poor man's templates.
 *
 * Many of the inline macros don't call into the following
 * routines.  Some of them call into other kernel routines,
 * such as memset(), set_bit() or ffs().  Some of them can
 * accomplish their task right inline, such as returning the
 * size or address of the unsigned long array, or optimized
 * versions of the macros for the most common case of an array
 * of a single unsigned long.
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/gfp.h>
#include <asm/uaccess.h>

#define MAX_HEX_PER_BYTE 4	/* dont need > 4 hex chars to encode byte */
#define BASE 16			/* masks are input in hex (base 16) */
#define NUL ((char)'\0')	/* nul-terminator */

/**
 * __mask_snprintf_len - represent multi-word bit mask as string.
 * @buf: The buffer to place the result into
 * @buflen: The size of the buffer, including the trailing null space
 * @maskp: Points to beginning of multi-word bit mask.
 * @maskbytes: Number of bytes in bit mask at maskp.
 *
 * This routine is expected to be called from a macro such as:
 *
 * #define cpumask_snprintf(buf, buflen, mask) \
 *   __mask_snprintf_len(buf, buflen, cpus_addr(mask), sizeof(mask))
 */

int __mask_snprintf_len(char *buf, unsigned int buflen,
	const unsigned long *maskp, unsigned int maskbytes)
{
	u32 *wordp = (u32 *)maskp;
	int i = maskbytes/sizeof(u32) - 1;
	int len = 0;
	char *sep = "";

	while (i >= 1 && wordp[i] == 0)
		i--;
	while (i >= 0) {
		len += snprintf(buf+len, buflen-len, "%s%x", sep, wordp[i]);
		sep = ",";
		i--;
	}
	return len;
}

/**
 * __mask_parse_len - parse user string into maskbytes mask at maskp
 * @ubuf: The user buffer from which to take the string
 * @ubuflen: The size of this buffer, including the terminating char
 * @maskp: Place resulting mask (array of unsigned longs) here
 * @masklen: Construct mask at @maskp to have exactly @masklen bytes
 *
 * @masklen is a multiple of sizeof(unsigned long).  A mask of
 * @masklen bytes is constructed starting at location @maskp.
 * The value of this mask is specified by the user provided
 * string starting at address @ubuf.  Only bytes in the range
 * [@ubuf, @ubuf+@ubuflen) can be read from user space, and
 * reading will stop after the first byte that is not a comma
 * or valid hex digit in the characters [,0-9a-fA-F], or at
 * the point @ubuf+@ubuflen, whichever comes first.
 *
 * Since the user only needs about 2.25 chars per byte to encode
 * a mask (one char per nibble plus one comma separator or nul
 * terminator per byte), we blow them off with -EINVAL if they
 * claim a @ubuflen more than 4 (MAX_HEX_PER_BYTE) times maskbytes.
 * An empty word (delimited by two consecutive commas, for example)
 * is taken as zero.  If @buflen is zero, the entire @maskp is set
 * to zero.
 *
 * If the user provides fewer comma-separated ascii words
 * than there are 32 bit words in maskbytes, we zero fill the
 * remaining high order words.  If they provide more, they fail
 * with -EINVAL.  Each comma-separate ascii word is taken as
 * a hex representation; leading zeros are ignored, and do not
 * imply octal.  '00e1', 'e1', '00E1', 'E1' are all the same.
 * If user passes a word that is larger than fits in a u32,
 * they fail with -EOVERFLOW.
 */

int __mask_parse_len(const char __user *ubuf, unsigned int ubuflen,
	unsigned long *maskp, unsigned int maskbytes)
{
	char buf[maskbytes * MAX_HEX_PER_BYTE + sizeof(NUL)];
	char *bp = buf;
	u32 *wordp = (u32 *)maskp;
	char *p;
	int i, j;

	if (ubuflen > maskbytes * MAX_HEX_PER_BYTE)
		return -EINVAL;
	if (copy_from_user(buf, ubuf, ubuflen))
		return -EFAULT;
	buf[ubuflen] = NUL;

	/*
	 * Put the words into wordp[] in big-endian order,
	 * then go back and reverse them.
	 */
	memset(wordp, 0, maskbytes);
	i = j = 0;
	while ((p = strsep(&bp, ",")) != NULL) {
		unsigned long long t;
		if (j == maskbytes/sizeof(u32))
			return -EINVAL;
		t = simple_strtoull(p, 0, BASE);
		if (t != (u32)t)
			return -EOVERFLOW;
		wordp[j++] = t;
	}
	--j;
	while (i < j) {
		u32 t = wordp[i];
		wordp[i] = wordp[j];
		wordp[j] = t;
		i++, --j;
	}
	return 0;
}
