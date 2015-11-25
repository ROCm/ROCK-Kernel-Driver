/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#ifndef __DAL_BIT_SET_H__
#define __DAL_BIT_SET_H__

struct bit_set_iterator_32 {
	uint32_t value;
};

static inline uint32_t least_significant_bit(uint32_t bs32_container)
{
	return bs32_container & (0 - bs32_container);
}
/* iterates over bit_set_iterator by means of least significant bit purge*/
static inline uint32_t get_next_significant_bit(
		struct bit_set_iterator_32 *bs32)
{
	uint32_t lsb = least_significant_bit(bs32->value);

	bs32->value &= ~lsb;
	return lsb;
}

static inline void bit_set_iterator_reset_to_mask(
		struct bit_set_iterator_32 *bs32,
		uint32_t mask)
{
	bs32->value = mask;
}

static inline void bit_set_iterator_construct(
		struct bit_set_iterator_32 *bs32,
		uint32_t mask)
{
	bit_set_iterator_reset_to_mask(bs32, mask);
}

#endif /* __DAL_BIT_SET_H__ */
