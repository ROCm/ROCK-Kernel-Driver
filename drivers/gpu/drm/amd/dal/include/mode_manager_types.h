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

#ifndef __DAL_MODE_MANAGER_TYPES_H__
#define __DAL_MODE_MANAGER_TYPES_H__

#include "timing_service_types.h"
#include "set_mode_interface.h"
#include "bit_set.h"

struct pixel_format_support {
	bool INDEX8:1;
	bool RGB565:1;
	bool ARGB8888:1;
	bool ARGB2101010:1;
	bool ARGB2101010_XRBIAS:1;
	bool FP16:1;
};

struct render_mode {
	struct view view;
	enum pixel_format pixel_format;
};

struct refresh_rate {
	uint32_t field_rate;
	bool INTERLACED:1;
	bool VIDEO_OPTIMIZED_RATE:1;
};

struct stereo_3d_view {
	enum view_3d_format view_3d_format;
	union {
		uint32_t raw;
		struct /*stereo_3d_view_flags*/
		{
			bool SINGLE_FRAME_SW_PACKED:1;
			bool EXCLUSIVE_3D:1;
		} bits;
	} flags;
};

enum solution_importance {
	SOLUTION_IMPORTANCE_PREFERRED = 1,
	/* Means we want to use this solution
	 * even in wide topology configurations*/
	SOLUTION_IMPORTANCE_SAFE,
	SOLUTION_IMPORTANCE_UNSAFE,
	SOLUTION_IMPORTANCE_DEFAULT
	/* Temporary state , means Solution object
	 * should define importance by itself
	 */
};

struct solution {
	const struct mode_timing *mode_timing;
	enum solution_importance importance;
	bool is_custom_mode;
	uint32_t scl_support[NUM_PIXEL_FORMATS];
	/* bit vector of the scaling that can be supported on the timing */
	uint32_t scl_support_guaranteed[NUM_PIXEL_FORMATS];
	/* subset of m_sclSupport that can be guaranteed supported */
};

static inline void stereo_3d_view_reset(struct stereo_3d_view *stereo_3d_view)
{
	stereo_3d_view->view_3d_format = VIEW_3D_FORMAT_NONE;
	stereo_3d_view->flags.raw = 0;
}

bool dal_refresh_rate_is_equal(
		const struct refresh_rate *lhs,
		const struct refresh_rate *rhs);

bool dal_refresh_rate_less_than(
		const struct refresh_rate *lhs,
		const struct refresh_rate *rhs);

void refresh_rate_from_mode_info(
		struct refresh_rate *,
		const struct mode_info *);
bool dal_solution_less_than(const void *lhs, const void *rhs);
bool dal_view_is_equal(const struct view *lhs, const struct view *rhs);

struct pixel_format_list {
	uint32_t set;
	struct bit_set_iterator_32 iter;
};

void dal_pixel_format_list_reset_iterator(struct pixel_format_list *pfl);
void dal_pixel_format_list_zero_iterator(struct pixel_format_list *pfl);

void dal_pixel_format_list_construct(
		struct pixel_format_list *pfl,
		uint32_t mask);

uint32_t dal_pixel_format_list_next(struct pixel_format_list *pfl);

uint32_t dal_pixel_format_list_get_count(
		const struct pixel_format_list *pfl);
enum pixel_format dal_pixel_format_list_get_pixel_format(
		const struct pixel_format_list *pfl);

enum timing_select {
	TIMING_SELECT_DEFAULT,
	TIMING_SELECT_NATIVE_ONLY,
	TIMING_SELECT_PRESERVE_ASPECT
};

struct bestview_options {
	enum timing_select base_timing_select;
	enum display_color_depth prefered_color_depth;
	enum pixel_encoding prefered_pixel_encoding;
	bool RESTRICT_HD_TIMING:1;
	bool DISALLOW_CUSTOM_MODES_AS_BASE:1;
	bool MAINTAIN_ASPECT_RATIO:1;
	bool ENABLE_SCALING:1;
};

#endif /* __DAL_MODE_MANAGER_TYPES_H__ */
