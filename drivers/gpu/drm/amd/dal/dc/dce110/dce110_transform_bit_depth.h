/* Copyright 2012-15 Advanced Micro Devices, Inc.
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

#ifndef __DC_TRANSFORM_BIT_DEPTH_DCE110_H__
#define __DC_TRANSFORM_BIT_DEPTH_DCE110_H__

#include "dce110_transform.h"

bool dce110_transform_power_up_line_buffer(struct transform *xfm);

bool dce110_transform_get_max_num_of_supported_lines(
	struct dce110_transform *xfm110,
	enum lb_pixel_depth depth,
	uint32_t pixel_width,
	uint32_t *lines);

bool dce110_transform_get_next_lower_pixel_storage_depth(
	struct dce110_transform *xfm110,
	uint32_t display_bpp,
	enum lb_pixel_depth depth,
	enum lb_pixel_depth *lower_depth);

bool dce110_transform_is_prefetch_enabled(
	struct dce110_transform *xfm110);

#endif
