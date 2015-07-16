/*
 * Copyright 2012-14 Advanced Micro Devices, Inc.
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

#include "dal_services.h"
#include "include/mode_manager_types.h"

bool dal_refresh_rate_is_equal(
		const struct refresh_rate *lhs,
		const struct refresh_rate *rhs)
{
	return lhs->field_rate == rhs->field_rate &&
		lhs->INTERLACED == rhs->INTERLACED &&
		lhs->VIDEO_OPTIMIZED_RATE == rhs->VIDEO_OPTIMIZED_RATE;
}

void refresh_rate_from_mode_info(
		struct refresh_rate *rf,
		const struct mode_info *mode_info)
{
	rf->field_rate = mode_info->field_rate;
	rf->INTERLACED = mode_info->flags.INTERLACE;
	rf->VIDEO_OPTIMIZED_RATE = mode_info->flags.VIDEO_OPTIMIZED_RATE;
}

bool dal_refresh_rate_less_than(
		const struct refresh_rate *lhs,
		const struct refresh_rate *rhs)
{
	unsigned int lhsRefreshRate;
	unsigned int rhsRefreshRate;

	lhsRefreshRate = lhs->INTERLACED ?
			(lhs->field_rate / 2) : lhs->field_rate;
	rhsRefreshRate = rhs->INTERLACED ?
			(rhs->field_rate / 2) : rhs->field_rate;

	if (lhsRefreshRate != rhsRefreshRate)
		return lhsRefreshRate < rhsRefreshRate;

	/* interlaced < progressive */
	if (lhs->INTERLACED != rhs->INTERLACED)
		return lhs->INTERLACED > rhs->INTERLACED;

	/* videoOptimizedRate (refreshrate/1.001)
	 * < non videoOptimizedRate (refreshrate/1.000) */
	return lhs->VIDEO_OPTIMIZED_RATE > rhs->VIDEO_OPTIMIZED_RATE;
}

bool dal_solution_less_than(const void *lhs, const void *rhs)
{
	const struct solution *lhs_solution = lhs;
	const struct solution *rhs_solution = rhs;
	struct refresh_rate l_refresh_rate, r_refresh_rate;
	enum timing_3d_format l_timing_3d_format, r_timing_3d_format;

	refresh_rate_from_mode_info(
				&l_refresh_rate,
				&lhs_solution->mode_timing->mode_info);
	refresh_rate_from_mode_info(
				&r_refresh_rate,
				&rhs_solution->mode_timing->mode_info);
	if (dal_refresh_rate_less_than(&l_refresh_rate, &r_refresh_rate))
		return true;
	else if (dal_refresh_rate_less_than(&r_refresh_rate, &l_refresh_rate))
		return false;

	l_timing_3d_format = lhs_solution->mode_timing->
					crtc_timing.timing_3d_format;
	r_timing_3d_format = rhs_solution->mode_timing->
					crtc_timing.timing_3d_format;
	if (l_timing_3d_format != r_timing_3d_format)
			return l_timing_3d_format < r_timing_3d_format;

	return lhs_solution->mode_timing->mode_info.timing_source <
			rhs_solution->mode_timing->mode_info.timing_source;
}

bool dal_view_is_equal(const struct view *lhs, const struct view *rhs)
{
	return lhs->height == rhs->height && lhs->width == rhs->width;
}

uint32_t dal_pixel_format_list_get_count(
		const struct pixel_format_list *pfl)
{
	uint32_t i, count = 0;

	for (i = pfl->set; i > 0; i >>= 1)
		if ((i & 1) != 0)
			count += 1;
	return count;
}

enum pixel_format dal_pixel_format_list_get_pixel_format(
		const struct pixel_format_list *pfl)
{
	return least_significant_bit(pfl->iter.value);
}

void dal_pixel_format_list_reset_iterator(struct pixel_format_list *pfl)
{
	bit_set_iterator_reset_to_mask(&pfl->iter, pfl->set);
}

void dal_pixel_format_list_zero_iterator(struct pixel_format_list *pfl)
{
	bit_set_iterator_reset_to_mask(&pfl->iter, 0);
}

enum pixel_format dal_pixel_format_list_next(struct pixel_format_list *pfl)
{
	return get_next_significant_bit(&pfl->iter);
}

void dal_pixel_format_list_construct(
		struct pixel_format_list *pfl,
		uint32_t mask)
{
	bit_set_iterator_construct(&pfl->iter, mask);
	pfl->set = mask;
}
