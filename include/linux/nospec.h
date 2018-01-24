// SPDX-License-Identifier: GPL-2.0
// Copyright(c) 2018 Intel Corporation. All rights reserved.

#ifndef __NOSPEC_H__
#define __NOSPEC_H__

#include <linux/jump_label.h>
#include <asm/barrier.h>

/*
 * If idx is negative or if idx > size then bit 63 is set in the mask,
 * and the value of ~(-1L) is zero. When the mask is zero, bounds check
 * failed, array_ptr will return NULL.
 */
#ifndef array_ptr_mask
static inline unsigned long array_ptr_mask(unsigned long idx, unsigned long sz)
{
	return ~(long)(idx | (sz - 1 - idx)) >> (BITS_PER_LONG - 1);
}
#endif

/**
 * array_ptr - Generate a pointer to an array element, ensuring
 * the pointer is bounded under speculation to NULL.
 *
 * @base: the base of the array
 * @idx: the index of the element, must be less than LONG_MAX
 * @sz: the number of elements in the array, must be less than LONG_MAX
 *
 * If @idx falls in the interval [0, @sz), returns the pointer to
 * @arr[@idx], otherwise returns NULL.
 */
#define array_ptr(base, idx, sz)					\
({									\
	union { typeof(*(base)) *_ptr; unsigned long _bit; } __u;	\
	typeof(*(base)) *_arr = (base);					\
	unsigned long _i = (idx);					\
	unsigned long _mask = array_ptr_mask(_i, (sz));			\
									\
	__u._ptr = _arr + _i;						\
	__u._bit &= _mask;						\
	__u._ptr;							\
})

/**
 * array_idx - Generate a pointer to an array index, ensuring the
 * pointer is bounded under speculation to NULL.
 *
 * @idx: the index of the element, must be less than LONG_MAX
 * @sz: the number of elements in the array, must be less than LONG_MAX
 *
 * If @idx falls in the interval [0, @sz), returns &@idx otherwise
 * returns NULL.
 */
#define array_idx(idx, sz)						\
({									\
	union { typeof((idx)) *_ptr; unsigned long _bit; } __u;		\
	typeof(idx) *_i = &(idx);					\
	unsigned long _mask = array_ptr_mask(*_i, (sz));		\
									\
	__u._ptr = _i;							\
	__u._bit &= _mask;						\
	__u._ptr;							\
})
#endif /* __NOSPEC_H__ */
