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

#ifndef __DAL_LINE_BUFFER_DCE110_H__
#define __DAL_LINE_BUFFER_DCE110_H__

#include "../line_buffer.h"

#define LB_ENTRIES_NUMBER_PART_1 720
#define LB_ENTRIES_NUMBER_PART_2 960
#define LB_ENTRIES_TOTAL_NUMBER 1712

struct line_buffer_dce110 {
	struct line_buffer base;
	enum controller_id controller_id;

	enum lb_pixel_depth default_pixel_depth;

	uint32_t caps;
	uint32_t index;
	uint32_t power_gating;

	uint32_t lbx_memory_ctrl;
	uint32_t lbx_data_format;
	uint32_t lbx_interrupt_mask;

	uint32_t lbx_lb_sync_reset_sel;
	uint32_t crtcx_crtc_status_position;
	uint32_t crtcx_crtc_status_frame_count;
};

#define LB110_FROM_BASE(lb_base) \
	container_of(lb_base, struct line_buffer_dce110, base)

struct line_buffer *dal_line_buffer_dce110_create(
		struct line_buffer_init_data *init_data);

bool dal_line_buffer_dce110_construct(
	struct line_buffer_dce110 *lb,
	struct line_buffer_init_data *init_data);

bool dal_line_buffer_dce110_get_max_num_of_supported_lines(
	struct line_buffer *lb,
	enum lb_pixel_depth depth,
	uint32_t pixel_width,
	uint32_t *lines);

bool dal_line_buffer_dce110_get_pixel_storage_depth(
	struct line_buffer *base,
	uint32_t display_bpp,
	enum lb_pixel_depth *depth);

#endif /* __DAL_LINE_BUFFER_DCE110_H__ */
