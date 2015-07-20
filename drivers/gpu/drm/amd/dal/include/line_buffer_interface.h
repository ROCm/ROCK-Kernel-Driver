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

#ifndef __LINE_BUFFER_INTERFACE_H__
#define __LINE_BUFFER_INTERFACE_H__

#include "grph_object_id.h"
#include "scaler_types.h"

enum lb_pixel_depth {
	/* do not change the values because it is used as bit vector */
	LB_PIXEL_DEPTH_18BPP = 1,
	LB_PIXEL_DEPTH_24BPP = 2,
	LB_PIXEL_DEPTH_30BPP = 4,
	LB_PIXEL_DEPTH_36BPP = 8
};

struct lb_config_data {
	uint32_t src_pixel_width;
	uint32_t src_pixel_width_c;
	uint32_t dst_pixel_width;
	struct scaling_tap_info taps;
	enum lb_pixel_depth depth; /* for dce8 and up use this */
	uint32_t src_height; /* dce8 vsr calculation on lb power gating */
	uint32_t dst_height; /* dce8 vsr calculation on lb power gating */
	bool interlaced; /*dce8 vsr calculation on lb power gating*/
};

struct lb_params_data {
	uint32_t id;
	enum lb_pixel_depth depth;
};

/* forward declaration */
struct line_buffer;

void dal_line_buffer_power_up(struct line_buffer *lb);
uint32_t dal_line_buffer_get_size(struct line_buffer *lb);
void dal_line_buffer_program_interleave_mode(
	struct line_buffer *lb,
	enum controller_id idx,
	bool interleave);
void dal_line_buffer_reset_on_vblank(
	struct line_buffer *lb,
	enum controller_id idx);
bool dal_line_buffer_enable_power_gating(
	struct line_buffer *lb,
	enum controller_id idx,
	struct lb_config_data *lb_config);
bool dal_line_buffer_set_pixel_storage_depth(
	struct line_buffer *lb,
	enum lb_pixel_depth depth);
bool dal_line_buffer_get_current_pixel_storage_depth(
	struct line_buffer *lb,
	enum lb_pixel_depth *lower_depth);
bool dal_line_buffer_get_pixel_storage_depth(
	struct line_buffer *lb,
	uint32_t display_bpp,
	enum lb_pixel_depth *depth);
bool dal_line_buffer_get_next_lower_pixel_storage_depth(
	struct line_buffer *lb,
	uint32_t display_bpp,
	enum lb_pixel_depth depth,
	enum lb_pixel_depth *lower_depth);
bool dal_line_buffer_get_max_num_of_supported_lines(
	struct line_buffer *lb,
	enum lb_pixel_depth depth,
	uint32_t pixel_width,
	uint32_t *lines);
void dal_line_buffer_set_vblank_irq(
	struct line_buffer *lb,
	bool enable);
void dal_line_buffer_enable_alpha(
	struct line_buffer *lb,
	bool enable);
bool dal_line_buffer_is_prefetch_supported(
	struct line_buffer *lb,
	struct lb_config_data *lb_config);

#endif /* __LINE_BUFFER_INTERFACE_H__ */
