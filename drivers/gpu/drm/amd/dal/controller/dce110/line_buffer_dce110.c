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

#include "dce/dce_11_0_d.h"
#include "dce/dce_11_0_sh_mask.h"

#include "include/logger_interface.h"
#include "include/adapter_service_interface.h"
#include "include/asic_capability_types.h"
#include "include/fixed32_32.h"

#include "line_buffer_dce110.h"

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
	set_reg_field_value(value, 0, LB_MEMORY_CTRL, LB_MEMORY_CONFIG);
	/*hard coded number DCE11 1712(0x6B0) Partitions: 720/960/1712*/
	set_reg_field_value(value, LB_ENTRIES_TOTAL_NUMBER, LB_MEMORY_CTRL,
			LB_MEMORY_SIZE);

	dal_write_reg(base->dal_context, lb->lbx_memory_ctrl, value);
}

/*
 * reset_lb_on_vblank
 *
 * @brief
 * Resets LB on first VBlank
 */
static void reset_lb_on_vblank(
	struct line_buffer *lb,
	enum controller_id idx)
{
	uint32_t addr = 0;
	uint32_t value = 0;
	struct line_buffer_dce110 *lb_dce110 = LB110_FROM_BASE(lb);

	addr = lb_dce110->crtcx_crtc_status_position;
	value = dal_read_reg(lb->dal_context, addr);

	/* Wait for one frame if CRTC is moving */
	if (value != dal_read_reg(lb->dal_context, addr)) {
		uint32_t retry_count = 0;

		addr = lb_dce110->lbx_lb_sync_reset_sel;
		value = dal_read_reg(lb->dal_context, addr);
		set_reg_field_value(
			value, 3, LB_SYNC_RESET_SEL, LB_SYNC_RESET_SEL);
		set_reg_field_value(
			value, 1, LB_SYNC_RESET_SEL, LB_SYNC_RESET_SEL2);
		dal_write_reg(lb->dal_context, addr, value);

		/* mmCRTCx_CRTC_STATUS_FRAME_COUNT */
		addr = lb_dce110->crtcx_crtc_status_frame_count;
		value = dal_read_reg(lb->dal_context, addr);
		for (retry_count = 100; retry_count > 0; retry_count--) {
			if (value !=
				dal_read_reg(lb->dal_context, addr))
				break;
			dal_sleep_in_milliseconds(1);
		}
	}

	addr = lb_dce110->lbx_lb_sync_reset_sel;
	value = dal_read_reg(lb->dal_context, addr);
	set_reg_field_value(value, 2, LB_SYNC_RESET_SEL, LB_SYNC_RESET_SEL);
	set_reg_field_value(value, 0, LB_SYNC_RESET_SEL, LB_SYNC_RESET_SEL2);
	dal_write_reg(lb->dal_context, addr, value);
}

static void set_vblank_irq(
		struct line_buffer *lb,
		bool enable)
{
	struct line_buffer_dce110 *lb_dce110 = LB110_FROM_BASE(lb);
	uint32_t addr = lb_dce110->lbx_interrupt_mask;
	uint32_t value = dal_read_reg(lb->dal_context, addr);

	set_reg_field_value(value, enable?1:0, LB_INTERRUPT_MASK,
			VBLANK_INTERRUPT_MASK);
	dal_write_reg(lb->dal_context, addr, value);
}

bool dal_line_buffer_dce110_get_pixel_storage_depth(
	struct line_buffer *base,
	uint32_t display_bpp,
	enum lb_pixel_depth *depth)
{
	struct line_buffer_dce110 *lb = LB110_FROM_BASE(base);
	enum lb_pixel_depth display_depth;

	*depth = lb->default_pixel_depth;

	/* default value */
	display_depth = dal_line_buffer_display_bpp_to_lb_depth(display_bpp);

	if (lb->caps & display_depth) {
		/*we got the match lb and display bpp*/
		*depth = display_depth;
	} else {
		/*we have to go the higher lb if it is possible*/
		uint32_t i;
		uint32_t max_depth = LB_PIXEL_DEPTH_36BPP;

		for (i = display_depth; i <= max_depth; i <<= 1) {
			if (i & lb->caps) {
				*depth = i;
				break;
			}
		}
	}

	return true;
}

static uint32_t calculate_pitch(uint32_t depth, uint32_t width)
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

bool dal_line_buffer_dce110_get_max_num_of_supported_lines(
	struct line_buffer *lb,
	enum lb_pixel_depth depth,
	uint32_t pixel_width,
	uint32_t *lines)
{
	uint32_t pitch;

	if (pixel_width == 0)
		return false;

	pitch = calculate_pitch(depth, pixel_width);
	if (pitch == 0 || pitch > LB_ENTRIES_TOTAL_NUMBER)
		return false;

	*lines = LB_ENTRIES_TOTAL_NUMBER / pitch;
	return true;
}

static bool get_current_pixel_storage_depth(
	struct line_buffer *base,
	enum lb_pixel_depth *depth)
{
	struct line_buffer_dce110 *lb = LB110_FROM_BASE(base);
	uint32_t value = 0;

	if (depth == NULL)
		return false;

	value = dal_read_reg(
			base->dal_context,
			lb->lbx_data_format);

	switch (get_reg_field_value(value, LB_DATA_FORMAT, PIXEL_DEPTH)) {
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

	value = dal_read_reg(
			base->dal_context,
			lb->lbx_data_format);
	switch (depth) {
	case LB_PIXEL_DEPTH_18BPP:
		set_reg_field_value(value, 2, LB_DATA_FORMAT, PIXEL_DEPTH);
		set_reg_field_value(value, 1, LB_DATA_FORMAT, PIXEL_EXPAN_MODE);
		break;
	case LB_PIXEL_DEPTH_24BPP:
		set_reg_field_value(value, 1, LB_DATA_FORMAT, PIXEL_DEPTH);
		set_reg_field_value(value, 1, LB_DATA_FORMAT, PIXEL_EXPAN_MODE);
		break;
	case LB_PIXEL_DEPTH_30BPP:
		set_reg_field_value(value, 0, LB_DATA_FORMAT, PIXEL_DEPTH);
		set_reg_field_value(value, 1, LB_DATA_FORMAT, PIXEL_EXPAN_MODE);
		break;
	case LB_PIXEL_DEPTH_36BPP:
		set_reg_field_value(value, 3, LB_DATA_FORMAT, PIXEL_DEPTH);
		set_reg_field_value(value, 0, LB_DATA_FORMAT, PIXEL_EXPAN_MODE);
		break;
	default:
		ret = false;
		break;
	}

	if (ret == true) {

		set_reg_field_value(value, 0, LB_DATA_FORMAT, ALPHA_EN);
		dal_write_reg(
				base->dal_context, lb->lbx_data_format, value);
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
	/* do not power gate */
	return true;
}

static void enable_alpha(
	struct line_buffer *base,
	bool enable)
{
	struct line_buffer_dce110 *lb = LB110_FROM_BASE(base);
	struct dal_context *dal_ctx = base->dal_context;
	uint32_t value;
	uint32_t addr = lb->lbx_data_format;

	value = dal_read_reg(dal_ctx, addr);

	if (enable == 1)
		set_reg_field_value(
				value,
				1,
				LB_DATA_FORMAT,
				ALPHA_EN);
	else
		set_reg_field_value(
				value,
				0,
				LB_DATA_FORMAT,
				ALPHA_EN);

	dal_write_reg(dal_ctx, addr, value);
}

static enum lb_pixel_depth translate_display_bpp_to_lb_depth(
	uint32_t display_bpp)
{
	switch (display_bpp) {
	case 18:
		return LB_PIXEL_DEPTH_18BPP;
	case 24:
		return LB_PIXEL_DEPTH_24BPP;
	case 36:
	case 42:
	case 48:
		return LB_PIXEL_DEPTH_36BPP;
	case 30:
	default:
		return LB_PIXEL_DEPTH_30BPP;
	}
}

static bool get_next_lower_pixel_storage_depth(
	struct line_buffer *base,
	uint32_t display_bpp,
	enum lb_pixel_depth depth,
	enum lb_pixel_depth *lower_depth)
{
	struct line_buffer_dce110 *lb = LB110_FROM_BASE(base);
	enum lb_pixel_depth depth_req_by_display =
		translate_display_bpp_to_lb_depth(display_bpp);
	uint32_t current_required_depth = depth_req_by_display;
	uint32_t current_depth = depth;

	/* if required display depth < current we could go down, for example
	 * from LB_PIXEL_DEPTH_30BPP to LB_PIXEL_DEPTH_24BPP
	 */
	if (current_required_depth < current_depth) {
		current_depth = current_depth >> 1;
		if (lb->caps & current_depth) {
			*lower_depth = current_depth;
			return true;
		}
	}
	return false;
}

static bool is_prefetch_supported(
	struct line_buffer *base,
	struct lb_config_data *lb_config)
{
	struct line_buffer_dce110 *lb = LB110_FROM_BASE(base);
	uint32_t value = dal_read_reg(base->dal_context, lb->lbx_data_format);

	if (get_reg_field_value(value, LB_DATA_FORMAT, PREFETCH) == 1)
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
	.get_next_lower_pixel_storage_depth =
		get_next_lower_pixel_storage_depth,
	.get_max_num_of_supported_lines =
		dal_line_buffer_dce110_get_max_num_of_supported_lines,
	.reset_lb_on_vblank = reset_lb_on_vblank,
	.set_vblank_irq = set_vblank_irq,
	.enable_alpha = enable_alpha,
	.is_prefetch_supported = is_prefetch_supported
};

bool dal_line_buffer_dce110_construct(
	struct line_buffer_dce110 *lb,
	struct line_buffer_init_data *init_data)
{
	bool ret = true;
	struct line_buffer *base;

	base = &lb->base;
	/*init_data->lb_split = true;*/

	if (!dal_line_buffer_construct_base(&lb->base, init_data))
		return false;

	/*funcs init*/
	base->funcs = &funcs;

	/* data members init */
	lb->caps = LB_PIXEL_DEPTH_30BPP;
	lb->default_pixel_depth = LB_PIXEL_DEPTH_30BPP;
	lb->controller_id = init_data->id;

	if (init_data->as != NULL) {
		struct asic_feature_flags flags;

		flags = dal_adapter_service_get_feature_flags(init_data->as);

		/*we may change lb capability here*/
		if (flags.bits.WORKSTATION) {
			lb->caps |= LB_PIXEL_DEPTH_18BPP;
			lb->caps |= LB_PIXEL_DEPTH_24BPP;
			/* 04/13: HW removed 12bpc LB. */

		} else if (dal_adapter_service_is_feature_supported(
			FEATURE_LINE_BUFFER_ENHANCED_PIXEL_DEPTH)) {
			lb->caps |= LB_PIXEL_DEPTH_18BPP;
			lb->caps |= LB_PIXEL_DEPTH_24BPP;
		}

		/* TODO: read FEATURE_POWER_GATING_LINE_BUFFER_PORTION when
		 * LB PG feature is implemented for DCE11. */
	}

	switch (lb->controller_id) {
	case CONTROLLER_ID_D0:
		lb->lbx_memory_ctrl = mmLB0_LB_MEMORY_CTRL;
		lb->lbx_data_format = mmLB0_LB_DATA_FORMAT;
		lb->lbx_interrupt_mask = mmLB0_LB_INTERRUPT_MASK;
		lb->lbx_lb_sync_reset_sel = mmLB0_LB_SYNC_RESET_SEL;
		lb->crtcx_crtc_status_position = mmCRTC0_CRTC_STATUS_POSITION;
		lb->crtcx_crtc_status_frame_count =
				mmCRTC0_CRTC_STATUS_FRAME_COUNT;
		break;
	case CONTROLLER_ID_D1:
		lb->lbx_memory_ctrl = mmLB1_LB_MEMORY_CTRL;
		lb->lbx_data_format = mmLB1_LB_DATA_FORMAT;
		lb->lbx_interrupt_mask = mmLB1_LB_INTERRUPT_MASK;
		lb->lbx_lb_sync_reset_sel = mmLB1_LB_SYNC_RESET_SEL;
		lb->crtcx_crtc_status_position = mmCRTC1_CRTC_STATUS_POSITION;
		lb->crtcx_crtc_status_frame_count =
				mmCRTC1_CRTC_STATUS_FRAME_COUNT;
		break;
	case CONTROLLER_ID_D2:
		lb->lbx_memory_ctrl = mmLB2_LB_MEMORY_CTRL;
		lb->lbx_data_format = mmLB2_LB_DATA_FORMAT;
		lb->lbx_interrupt_mask = mmLB2_LB_INTERRUPT_MASK;
		lb->lbx_lb_sync_reset_sel = mmLB2_LB_SYNC_RESET_SEL;
		lb->crtcx_crtc_status_position = mmCRTC2_CRTC_STATUS_POSITION;
		lb->crtcx_crtc_status_frame_count =
				mmCRTC2_CRTC_STATUS_FRAME_COUNT;
		break;
	case CONTROLLER_ID_UNDERLAY0:
		break;
	default:
		ret = false;
		break;
	}

	if (false == ret)
		dal_line_buffer_destruct_base(base);

	return ret;
}


/******************************************************************************
 * non-static functions
 *****************************************************************************/

struct line_buffer *dal_line_buffer_dce110_create(
		struct line_buffer_init_data *init_data)
{
	struct line_buffer_dce110 *lb = NULL;

	lb = dal_alloc(sizeof(struct line_buffer_dce110));
	if (lb == NULL)
		return NULL;

	if (dal_line_buffer_dce110_construct(lb, init_data))
		return &lb->base;

	/* fail to construct */
	dal_free(lb);
	return NULL;
}
