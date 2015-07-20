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

#ifndef __DAL_LINE_BUFFER_H__
#define __DAL_LINE_BUFFER_H__

#include "include/line_buffer_interface.h"

struct line_buffer_funcs {
	void (*destroy)(struct line_buffer **lb);
	void (*power_up)(struct line_buffer *lb);
	bool (*enable_power_gating)(
		struct line_buffer *lb,
		enum controller_id idx,
		struct lb_config_data *lb_config);
	bool (*set_pixel_storage_depth)(
		struct line_buffer *lb,
		enum lb_pixel_depth depth);
	bool (*get_current_pixel_storage_depth)(
		struct line_buffer *lb,
		enum lb_pixel_depth *lower_depth);
	bool (*get_pixel_storage_depth)(
		struct line_buffer *lb,
		uint32_t display_bpp,
		enum lb_pixel_depth *depth);
	bool (*get_next_lower_pixel_storage_depth)(
		struct line_buffer *lb,
		uint32_t display_bpp,
		enum lb_pixel_depth depth,
		enum lb_pixel_depth *lower_depth);
	bool (*get_max_num_of_supported_lines)(
		struct line_buffer *lb,
		enum lb_pixel_depth depth,
		uint32_t pixel_width,
		uint32_t *lines);
	void (*reset_lb_on_vblank)(
		struct line_buffer *lb,
		enum controller_id idx);
	void (*set_vblank_irq)(
		struct line_buffer *lb,
		bool enable);
	void (*enable_alpha)(
		struct line_buffer *lb,
		bool enable);
	bool (*is_prefetch_supported)(
		struct line_buffer *lb,
		struct lb_config_data *lb_config);
};

struct line_buffer {
	const struct line_buffer_funcs *funcs;
	uint32_t size;
	struct dal_context *dal_context;
	bool power_gating;
};

struct line_buffer_init_data {
	struct dal_context *dal_context;
	struct adapter_service *as;
	enum controller_id id;
};

bool dal_line_buffer_construct_base(
	struct line_buffer *lb,
	struct line_buffer_init_data *init_data);
void dal_line_buffer_destruct_base(struct line_buffer *lb);
void dal_line_buffer_destroy(struct line_buffer **lb);

enum lb_pixel_depth dal_line_buffer_display_bpp_to_lb_depth(
	uint32_t disp_bpp);

bool dal_line_buffer_base_is_prefetch_supported(
	struct line_buffer *lb,
	struct lb_config_data *lb_config);

uint32_t dal_line_buffer_base_calculate_pitch(
	enum lb_pixel_depth depth,
	uint32_t width);

#endif /* __DAL_LINE_BUFFER_H__ */
