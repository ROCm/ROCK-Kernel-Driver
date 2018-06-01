/*
* lib/bitmap.c
* Helper functions for bitmap.h.
*
* This source code is licensed under the GNU General Public License,
* Version 2. See the file COPYING for more details.
*/

#include <kcl/kcl_bitmap.h>

#if !defined(HAVE_BITMAP_FIND_NEXT_ZERO_AREA_OFF)
unsigned long bitmap_find_next_zero_area_off(unsigned long *map,
					     unsigned long size,
					     unsigned long start,
					     unsigned int nr,
					     unsigned long align_mask,
					     unsigned long align_offset)
{
	unsigned long index, end, i;
again:
	index = find_next_zero_bit(map, size, start);

	/* Align allocation */
	index = __ALIGN_MASK(index + align_offset, align_mask) - align_offset;

	end = index + nr;
	if (end > size)
		return end;
	i = find_next_bit(map, end, index);
	if (i < end) {
		start = i + 1;
		goto again;
	}
	return index;
}
EXPORT_SYMBOL(bitmap_find_next_zero_area_off);
#endif
