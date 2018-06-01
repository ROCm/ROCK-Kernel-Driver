#ifndef AMDKCL_BITMAP_H
#define AMDKCL_BITMAP_H

#include <linux/version.h>

#if !defined(HAVE_BITMAP_FIND_NEXT_ZERO_AREA_OFF)
unsigned long bitmap_find_next_zero_area_off(unsigned long *map,
					     unsigned long size,
					     unsigned long start,
					     unsigned int nr,
					     unsigned long align_mask,
					     unsigned long align_offset);
#endif

#endif /* AMDKCL_BITMAP_H */
