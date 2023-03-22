/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Written by Mark Hemment, 1996 (markhe@nextd.demon.co.uk).
 *
 * (C) SGI 2006, Christoph Lameter
 *      Cleaned up and restructured to ease the addition of alternative
 *      implementations of SLAB allocators.
 * (C) Linux Foundation 2008-2013
 *      Unified interface for all slab allocators
 */
#ifndef AMDKCL_SLAB_H
#define AMDKCL_SLAB_H

#include <linux/gfp.h>
#include <linux/slab.h>

#ifndef HAVE_KREALLOC_ARRAY
/**
 * krealloc_array - reallocate memory for an array.
 * @p: pointer to the memory chunk to reallocate
 * @new_n: new number of elements to alloc
 * @new_size: new size of a single member of the array
 * @flags: the type of memory to allocate (see kmalloc)
 */
static __must_check inline void *
krealloc_array(void *p, size_t new_n, size_t new_size, gfp_t flags)
{
        size_t bytes;

        if (unlikely(check_mul_overflow(new_n, new_size, &bytes)))
                return NULL;

        return krealloc(p, bytes, flags);
}
#endif

#endif
