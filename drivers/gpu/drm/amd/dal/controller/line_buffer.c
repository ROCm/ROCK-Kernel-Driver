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
#include "dal_services.h"
#include "include/logger_interface.h"
#include "include/line_buffer_interface.h"
#include "include/adapter_service_interface.h"
#include "line_buffer.h"

void dal_line_buffer_destroy(struct line_buffer **lb)
{
	if (lb == NULL || *lb == NULL) {
		BREAK_TO_DEBUGGER();
		return;
	}

	(*lb)->funcs->destroy(lb);

	*lb = NULL;
}

void dal_line_buffer_destruct_base(struct line_buffer *lb)
{

}

bool dal_line_buffer_construct_base(
	struct line_buffer *lb,
	struct line_buffer_init_data *init_data
	)
{
	struct dal_context *dal_context = init_data->dal_context;

	if (init_data == NULL || init_data->as == NULL)
		return false;
	lb->dal_context = init_data->dal_context;
	lb->size = dal_adapter_service_get_line_buffer_size(init_data->as);

	lb->power_gating = dal_adapter_service_is_feature_supported(
			FEATURE_POWER_GATING_LB_PORTION);
	dal_logger_write(dal_context->logger,
		LOG_MAJOR_LINE_BUFFER,
		LOG_MINOR_LINE_BUFFER_POWERGATING,
		"LB Partial Power Gating option: %s\n",
		(lb->power_gating == true ? "Enabled" : "Disabled"));

	return true;
}

enum lb_pixel_depth dal_line_buffer_display_bpp_to_lb_depth(
		uint32_t disp_bpp)
{
	switch (disp_bpp) {
	case 18:
		return LB_PIXEL_DEPTH_18BPP;
	case 24:
		return LB_PIXEL_DEPTH_24BPP;
	case 36:
	case 42:
	case 48:
		return LB_PIXEL_DEPTH_36BPP;
	case 30:
		return LB_PIXEL_DEPTH_30BPP;
	default:
		break;

	}
	return LB_PIXEL_DEPTH_30BPP;
}

void dal_line_buffer_power_up(struct line_buffer *lb)
{
	lb->funcs->power_up(lb);
}

uint32_t dal_line_buffer_get_size(struct line_buffer *lb)
{
	return lb->size;
}

void dal_line_buffer_program_interleave_mode(
	struct line_buffer *lb,
	enum controller_id idx,
	bool interleave)
{
	/* TODO: never called */
}

void dal_line_buffer_reset_on_vblank(
	struct line_buffer *lb,
	enum controller_id idx)
{
	lb->funcs->reset_lb_on_vblank(lb, idx);
}

bool dal_line_buffer_enable_power_gating(
	struct line_buffer *lb,
	enum controller_id idx,
	struct lb_config_data *lb_config)
{
	/* TODO:check if need here controller_id*/
	return lb->funcs->enable_power_gating(lb, idx, lb_config);
}

bool dal_line_buffer_set_pixel_storage_depth(
	struct line_buffer *lb,
	enum lb_pixel_depth depth)
{
	return lb->funcs->set_pixel_storage_depth(lb, depth);
}

bool dal_line_buffer_get_current_pixel_storage_depth(
	struct line_buffer *lb,
	enum lb_pixel_depth *lower_depth)
{
	return lb->funcs->get_current_pixel_storage_depth(lb, lower_depth);
}

bool dal_line_buffer_get_pixel_storage_depth(
	struct line_buffer *lb,
	uint32_t display_bpp,
	enum lb_pixel_depth *depth)
{
	return lb->funcs->get_pixel_storage_depth(lb, display_bpp, depth);
}

bool dal_line_buffer_get_next_lower_pixel_storage_depth(
	struct line_buffer *lb,
	uint32_t display_bpp,
	enum lb_pixel_depth depth,
	enum lb_pixel_depth *lower_depth)
{
	return lb->funcs->get_next_lower_pixel_storage_depth(
		lb, display_bpp, depth, lower_depth);
}

bool dal_line_buffer_get_max_num_of_supported_lines(
	struct line_buffer *lb,
	enum lb_pixel_depth depth,
	uint32_t pixel_width,
	uint32_t *lines)
{
	return lb->funcs->get_max_num_of_supported_lines(
		lb, depth, pixel_width, lines);
}

void dal_line_buffer_set_vblank_irq(
	struct line_buffer *lb,
	bool enable)
{
	lb->funcs->set_vblank_irq(lb, enable);
}

void dal_line_buffer_enable_alpha(
	struct line_buffer *lb,
	bool enable)
{
	lb->funcs->enable_alpha(lb, enable);
}

bool dal_line_buffer_is_prefetch_supported(
	struct line_buffer *lb,
	struct lb_config_data *lb_config)
{
	return lb->funcs->is_prefetch_supported(lb, lb_config);
}

bool dal_line_buffer_base_is_prefetch_supported(
	struct line_buffer *lb,
	struct lb_config_data *lb_config)
{
	return false;
}

uint32_t dal_line_buffer_base_calculate_pitch(
	enum lb_pixel_depth depth,
	uint32_t width)
{
	uint32_t pitch = 0;

	switch (depth) {
	case LB_PIXEL_DEPTH_18BPP:
		pitch = (width + 7) >> 3;
		break;

	case LB_PIXEL_DEPTH_24BPP:
		pitch = ((width + 7) / 8) * 683;
		pitch = (pitch + 511) >> 9;
		break;

	case LB_PIXEL_DEPTH_30BPP:
		pitch = ((width + 7) / 8) * 854;
		pitch = (pitch + 511) >> 9;
		break;

	case LB_PIXEL_DEPTH_36BPP:
		pitch = (width + 3) >> 2;
		break;
	}
	return pitch;
}
