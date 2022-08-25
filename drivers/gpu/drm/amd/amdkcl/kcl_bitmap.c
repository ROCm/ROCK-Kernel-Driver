/*
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <kcl/kcl_bitmap.h>

#ifndef HAVE_BITMAP_FUNCS

#include <linux/bitops.h>
#include <linux/mm.h>
#include <linux/slab.h>

unsigned long *kcl_bitmap_alloc(unsigned int nbits, gfp_t flags)
{
        return kmalloc_array(BITS_TO_LONGS(nbits), sizeof(unsigned long),
                             flags);
}
EXPORT_SYMBOL(kcl_bitmap_alloc);

unsigned long *kcl_bitmap_zalloc(unsigned int nbits, gfp_t flags)
{
        return kcl_bitmap_alloc(nbits, flags | __GFP_ZERO);
}
EXPORT_SYMBOL(kcl_bitmap_zalloc);

void kcl_bitmap_free(const unsigned long *bitmap)
{
        kfree(bitmap);
}
EXPORT_SYMBOL(kcl_bitmap_free);
#endif /* HAVE_BITMAP_FUNCS */

#ifndef HAVE_BITMAP_TO_ARR32
#if BITS_PER_LONG == 64
/**
 * kcl_bitmap_to_arr32 - copy the contents of bitmap to a u32 array of bits
 *	@buf: array of u32 (in host byte order), the dest bitmap
 *	@bitmap: array of unsigned longs, the source bitmap
 *	@nbits: number of bits in @bitmap
 */
void kcl_bitmap_to_arr32(u32 *buf, const unsigned long *bitmap, unsigned int nbits)
{
	unsigned int i, halfwords;

	halfwords = DIV_ROUND_UP(nbits, 32);
	for (i = 0; i < halfwords; i++) {
		buf[i] = (u32) (bitmap[i/2] & UINT_MAX);
		if (++i < halfwords)
			buf[i] = (u32) (bitmap[i/2] >> 32);
	}

	/* Clear tail bits in last element of array beyond nbits. */
	if (nbits % BITS_PER_LONG)
		buf[halfwords - 1] &= (u32) (UINT_MAX >> ((-nbits) & 31));
}
EXPORT_SYMBOL(kcl_bitmap_to_arr32);
#endif
#endif