#ifndef AMDKCL_BITMAP_H
#define AMDKCL_BITMAP_H

#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 19, 0)
unsigned long bitmap_find_next_zero_area_off(unsigned long *map,
					     unsigned long size,
					     unsigned long start,
					     unsigned int nr,
					     unsigned long align_mask,
					     unsigned long align_offset);
#endif

#endif /* AMDKCL_BITMAP_H */
