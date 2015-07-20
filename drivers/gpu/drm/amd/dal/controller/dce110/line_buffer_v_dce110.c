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

#include "dce/dce_11_0_d.h"
#include "dce/dce_11_0_sh_mask.h"

#include "include/logger_interface.h"
#include "include/adapter_service_interface.h"
#include "include/asic_capability_types.h"
#include "include/fixed32_32.h"

#include "line_buffer_v_dce110.h"

/******************************************************************************
 * static functions
 *****************************************************************************/

static void destruct(struct line_buffer_dce110 *lb)
{
	dal_line_buffer_destruct_base(&lb->base);
}

static void destroy(struct line_buffer **base)
{
	struct line_buffer_dce110 *lb;

	lb = LB110_FROM_BASE(*base);

	destruct(lb);

	dal_free(lb);

	*base = NULL;
}

/* LB_MEMORY_CONFIG
 *  00 - Use all three pieces of memory
 *  01 - Use only one piece of memory of total 720x144 bits
 *  10 - Use two pieces of memory of total 960x144 bits
 *  11 - reserved
 *
 * LB_MEMORY_SIZE
 *  Total entries of LB memory.
 *  This number should be larger than 960. The default value is 1712(0x6B0) */
static void power_up(struct line_buffer *base)
{
	struct line_buffer_dce110 *lb = LB110_FROM_BASE(base);
	uint32_t value;

	value = dal_read_reg(base->dal_context, lb->lbx_memory_ctrl);

	/*Use all three pieces of memory always*/
	set_reg_field_value(value, 0, LBV_MEMORY_CTRL, LB_MEMORY_CONFIG);
	/*hard coded number DCE11 1712(0x6B0) Partitions: 720/960/1712*/
	set_reg_field_value(
		value,
		LB_ENTRIES_TOTAL_NUMBER,
		LBV_MEMORY_CTRL,
		LB_MEMORY_SIZE);

	dal_write_reg(base->dal_context, lb->lbx_memory_ctrl, value);
}

static bool get_current_pixel_storage_depth(
	struct line_buffer *base,
	enum lb_pixel_depth *depth)
{
	struct line_buffer_dce110 *lb = LB110_FROM_BASE(base);
	uint32_t value = 0;

	if (depth == NULL)
		return false;

	value = dal_read_reg(base->dal_context, lb->lbx_data_format);

	switch (get_reg_field_value(value, LBV_DATA_FORMAT, PIXEL_DEPTH)) {
	case 0:
		*depth = LB_PIXEL_DEPTH_30BPP;
		break;
	case 1:
		*depth = LB_PIXEL_DEPTH_24BPP;
		break;
	case 2:
		*depth = LB_PIXEL_DEPTH_18BPP;
		break;
	case 3:
		*depth = LB_PIXEL_DEPTH_36BPP;
		break;
	default:
		dal_logger_write(base->dal_context->logger,
			LOG_MAJOR_WARNING,
			LOG_MINOR_COMPONENT_GPU,
			"%s: Invalid LB pixel depth",
			__func__);
		*depth = LB_PIXEL_DEPTH_30BPP;
		break;
	}
	return true;

}

static bool set_pixel_storage_depth(
	struct line_buffer *base,
	enum lb_pixel_depth depth)
{
	bool ret = true;
	struct line_buffer_dce110 *lb = LB110_FROM_BASE(base);
	uint32_t value;

	value = dal_read_reg(base->dal_context, lb->lbx_data_format);

	switch (depth) {
	case LB_PIXEL_DEPTH_18BPP:
		set_reg_field_value(value, 2, LBV_DATA_FORMAT, PIXEL_DEPTH);
		set_reg_field_value(
			value,
			1,
			LBV_DATA_FORMAT,
			PIXEL_EXPAN_MODE);
		set_reg_field_value(
			value,
			1,
			LBV_DATA_FORMAT,
			PIXEL_REDUCE_MODE);
		set_reg_field_value(value, 1, LBV_DATA_FORMAT, DITHER_EN);
		break;
	case LB_PIXEL_DEPTH_24BPP:
		set_reg_field_value(value, 1, LBV_DATA_FORMAT, PIXEL_DEPTH);
		set_reg_field_value(
			value,
			1,
			LBV_DATA_FORMAT,
			PIXEL_EXPAN_MODE);
		set_reg_field_value(
			value,
			1,
			LBV_DATA_FORMAT,
			PIXEL_REDUCE_MODE);
		set_reg_field_value(value, 0, LBV_DATA_FORMAT, DITHER_EN);
		break;
	case LB_PIXEL_DEPTH_30BPP:
		set_reg_field_value(value, 0, LBV_DATA_FORMAT, PIXEL_DEPTH);
		set_reg_field_value(
			value,
			1,
			LBV_DATA_FORMAT,
			PIXEL_EXPAN_MODE);
		set_reg_field_value(
			value,
			1,
			LBV_DATA_FORMAT,
			PIXEL_REDUCE_MODE);
		set_reg_field_value(value, 0, LBV_DATA_FORMAT, DITHER_EN);
		break;
	case LB_PIXEL_DEPTH_36BPP:
		set_reg_field_value(value, 3, LB_DATA_FORMAT, PIXEL_DEPTH);
		set_reg_field_value(
			value,
			0,
			LBV_DATA_FORMAT,
			PIXEL_EXPAN_MODE);
		set_reg_field_value(
			value,
			0,
			LBV_DATA_FORMAT,
			PIXEL_REDUCE_MODE);
		set_reg_field_value(value, 0, LBV_DATA_FORMAT, DITHER_EN);
		break;
	default:
		ret = false;
		break;
	}

	if (ret == true) {
		set_reg_field_value(
			value,
			1,
			LBV_DATA_FORMAT,
			DOWNSCALE_PREFETCH_EN);

		set_reg_field_value(value, 0, LBV_DATA_FORMAT, ALPHA_EN);
		dal_write_reg(base->dal_context, lb->lbx_data_format, value);
		if (!(lb->caps & depth)) {
			/*we should use unsupported capabilities
			 *  unless it is required by w/a*/
			dal_logger_write(base->dal_context->logger,
				LOG_MAJOR_WARNING,
				LOG_MINOR_COMPONENT_GPU,
				"%s: Capability not supported",
				__func__);
		}

	}

	return ret;
}

static bool enable_power_gating(
	struct line_buffer *base,
	enum controller_id idx,
	struct lb_config_data *lb_config)
{
	return true;
}

static void enable_alpha(
	struct line_buffer *base,
	bool enable)
{
}

static bool is_prefetch_supported(
	struct line_buffer *base,
	struct lb_config_data *lb_config_data)
{
	uint32_t pitch_l =
		dal_line_buffer_base_calculate_pitch(
			lb_config_data->depth,
			lb_config_data->src_pixel_width);
	uint32_t pitch_c =
		dal_line_buffer_base_calculate_pitch(
			lb_config_data->depth,
			lb_config_data->src_pixel_width_c);

	/* Required number of lines from taps, minimum is 3 for prefetch */
	uint32_t num_lines_required_l = lb_config_data->taps.v_taps + 2;
	uint32_t num_lines_required_c = lb_config_data->taps.v_taps_c + 2;
	uint32_t min_req_lb_entries_l;
	uint32_t min_req_lb_entries_c;

	if (num_lines_required_l < 3)
		num_lines_required_l = 3;

	if (num_lines_required_c < 3)
		num_lines_required_c = 3;

	min_req_lb_entries_l = num_lines_required_l * pitch_l;
	min_req_lb_entries_c = num_lines_required_c * pitch_c;

	if (min_req_lb_entries_l < LB_ENTRIES_TOTAL_NUMBER &&
		min_req_lb_entries_c <= LB_ENTRIES_TOTAL_NUMBER)
		return true;

	return false;
}

static const struct line_buffer_funcs funcs = {
	.destroy = destroy,
	.power_up = power_up,
	.enable_power_gating = enable_power_gating,
	.set_pixel_storage_depth = set_pixel_storage_depth,
	.get_current_pixel_storage_depth = get_current_pixel_storage_depth,
	.get_pixel_storage_depth =
		dal_line_buffer_dce110_get_pixel_storage_depth,
	.get_next_lower_pixel_storage_depth = NULL,
	.get_max_num_of_supported_lines =
		dal_line_buffer_dce110_get_max_num_of_supported_lines,
	.reset_lb_on_vblank = NULL,
	.set_vblank_irq = NULL,
	.enable_alpha = enable_alpha,
	.is_prefetch_supported = is_prefetch_supported
};

static bool construct(
	struct line_buffer_dce110 *lb,
	struct line_buffer_init_data *init_data)
{
	bool ret = true;

	if (!dal_line_buffer_dce110_construct(lb, init_data))
		return false;

	lb->caps = LB_PIXEL_DEPTH_24BPP;
	lb->default_pixel_depth = LB_PIXEL_DEPTH_24BPP;

	switch (lb->controller_id) {
	case CONTROLLER_ID_UNDERLAY0:
		lb->lbx_memory_ctrl = mmLBV_MEMORY_CTRL;
		lb->lbx_data_format = mmLBV_DATA_FORMAT;
		lb->lbx_interrupt_mask = mmLBV_INTERRUPT_MASK;
		lb->lbx_lb_sync_reset_sel = mmLBV_SYNC_RESET_SEL;
		lb->crtcx_crtc_status_position = mmCRTCV_STATUS_POSITION;
		lb->crtcx_crtc_status_frame_count = mmCRTCV_STATUS_FRAME_COUNT;
		break;
	default:
		ret = false;
		break;
	}

	if (false == ret)
		dal_line_buffer_destruct_base(&lb->base);

	lb->base.funcs = &funcs;

	return ret;
}


/******************************************************************************
 * non-static functions
 *****************************************************************************/

struct line_buffer *dal_line_buffer_v_dce110_create(
		struct line_buffer_init_data *init_data)
{
	struct line_buffer_dce110 *lb = dal_alloc(sizeof(*lb));

	if (lb == NULL)
		return NULL;

	if (construct(lb, init_data))
		return &lb->base;

	/* fail to construct */
	dal_free(lb);
	return NULL;
}
