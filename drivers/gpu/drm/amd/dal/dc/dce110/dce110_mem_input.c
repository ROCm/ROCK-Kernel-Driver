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
#include "dm_services.h"

#include "dce/dce_11_0_d.h"
#include "dce/dce_11_0_sh_mask.h"
/* TODO: this needs to be looked at, used by Stella's workaround*/
#include "gmc/gmc_8_2_d.h"
#include "gmc/gmc_8_2_sh_mask.h"

#include "include/logger_interface.h"
#include "adapter_service_interface.h"

#include "dce110_mem_input.h"

#define DCP_REG(reg) (reg + mem_input110->offsets.dcp)
#define DMIF_REG(reg) (reg + mem_input110->offsets.dmif)
#define PIPE_REG(reg) (reg + mem_input110->offsets.pipe)

static void program_sec_addr(
	struct dce110_mem_input *mem_input110,
	PHYSICAL_ADDRESS_LOC address)
{
	uint32_t value = 0;
	uint32_t temp;

	/*high register MUST be programmed first*/
	temp = address.high_part &
		GRPH_SECONDARY_SURFACE_ADDRESS_HIGH__GRPH_SECONDARY_SURFACE_ADDRESS_HIGH_MASK;
	set_reg_field_value(value, temp,
		GRPH_SECONDARY_SURFACE_ADDRESS_HIGH,
		GRPH_SECONDARY_SURFACE_ADDRESS_HIGH);
	dm_write_reg(mem_input110->base.ctx,
				 DCP_REG(mmGRPH_SECONDARY_SURFACE_ADDRESS_HIGH), value);

	value = 0;
	temp = address.low_part >>
		GRPH_SECONDARY_SURFACE_ADDRESS__GRPH_SECONDARY_SURFACE_ADDRESS__SHIFT;
	set_reg_field_value(value, temp,
		GRPH_SECONDARY_SURFACE_ADDRESS,
		GRPH_SECONDARY_SURFACE_ADDRESS);
	dm_write_reg(mem_input110->base.ctx,
				 DCP_REG(mmGRPH_SECONDARY_SURFACE_ADDRESS), value);
}

static void program_pri_addr(
	struct dce110_mem_input *mem_input110,
	PHYSICAL_ADDRESS_LOC address)
{
	uint32_t value = 0;
	uint32_t temp;

	/*high register MUST be programmed first*/
	temp = address.high_part &
		GRPH_PRIMARY_SURFACE_ADDRESS_HIGH__GRPH_PRIMARY_SURFACE_ADDRESS_HIGH_MASK;
	set_reg_field_value(value, temp,
		GRPH_PRIMARY_SURFACE_ADDRESS_HIGH,
		GRPH_PRIMARY_SURFACE_ADDRESS_HIGH);
	dm_write_reg(mem_input110->base.ctx,
				 DCP_REG(mmGRPH_PRIMARY_SURFACE_ADDRESS_HIGH), value);

	value = 0;
	temp = address.low_part >>
		GRPH_PRIMARY_SURFACE_ADDRESS__GRPH_PRIMARY_SURFACE_ADDRESS__SHIFT;
	set_reg_field_value(value, temp,
		GRPH_PRIMARY_SURFACE_ADDRESS,
		GRPH_PRIMARY_SURFACE_ADDRESS);
	dm_write_reg(mem_input110->base.ctx,
				 DCP_REG(mmGRPH_PRIMARY_SURFACE_ADDRESS), value);
}

static void enable(struct dce110_mem_input *mem_input110)
{
	uint32_t value = 0;

	value = dm_read_reg(mem_input110->base.ctx, DCP_REG(mmGRPH_ENABLE));
	set_reg_field_value(value, 1, GRPH_ENABLE, GRPH_ENABLE);
	dm_write_reg(mem_input110->base.ctx,
		DCP_REG(mmGRPH_ENABLE),
		value);
}

static void program_tiling(
	struct dce110_mem_input *mem_input110,
	const struct dc_tiling_info *info,
	const enum surface_pixel_format pixel_format)
{
	uint32_t value = 0;

	value = dm_read_reg(
			mem_input110->base.ctx,
			DCP_REG(mmGRPH_CONTROL));

	set_reg_field_value(value, info->num_banks,
		GRPH_CONTROL, GRPH_NUM_BANKS);

	set_reg_field_value(value, info->bank_width,
		GRPH_CONTROL, GRPH_BANK_WIDTH);

	set_reg_field_value(value, info->bank_height,
		GRPH_CONTROL, GRPH_BANK_HEIGHT);

	set_reg_field_value(value, info->tile_aspect,
		GRPH_CONTROL, GRPH_MACRO_TILE_ASPECT);

	set_reg_field_value(value, info->tile_split,
		GRPH_CONTROL, GRPH_TILE_SPLIT);

	set_reg_field_value(value, info->tile_mode,
		GRPH_CONTROL, GRPH_MICRO_TILE_MODE);

	set_reg_field_value(value, info->pipe_config,
		GRPH_CONTROL, GRPH_PIPE_CONFIG);

	set_reg_field_value(value, info->array_mode,
		GRPH_CONTROL, GRPH_ARRAY_MODE);

	set_reg_field_value(value, 1,
		GRPH_CONTROL, GRPH_COLOR_EXPANSION_MODE);

	set_reg_field_value(value, 0,
		GRPH_CONTROL, GRPH_Z);

	dm_write_reg(
		mem_input110->base.ctx,
		DCP_REG(mmGRPH_CONTROL),
		value);
}

static void program_size_and_rotation(
	struct dce110_mem_input *mem_input110,
	enum dc_rotation_angle rotation,
	const union plane_size *plane_size)
{
	uint32_t value = 0;
	union plane_size local_size = *plane_size;

	if (rotation == ROTATION_ANGLE_90 ||
		rotation == ROTATION_ANGLE_270) {

		uint32_t swap;

		swap = local_size.grph.surface_size.x;
		local_size.grph.surface_size.x =
			local_size.grph.surface_size.y;
		local_size.grph.surface_size.y  = swap;

		swap = local_size.grph.surface_size.width;
		local_size.grph.surface_size.width =
			local_size.grph.surface_size.height;
		local_size.grph.surface_size.height = swap;
	}

	set_reg_field_value(value, local_size.grph.surface_size.x,
			GRPH_X_START, GRPH_X_START);
	dm_write_reg(
		mem_input110->base.ctx,
		DCP_REG(mmGRPH_X_START),
		value);

	value = 0;
	set_reg_field_value(value, local_size.grph.surface_size.y,
			GRPH_Y_START, GRPH_Y_START);
	dm_write_reg(
		mem_input110->base.ctx,
		DCP_REG(mmGRPH_Y_START),
		value);

	value = 0;
	set_reg_field_value(value, local_size.grph.surface_size.width,
			GRPH_X_END, GRPH_X_END);
	dm_write_reg(
		mem_input110->base.ctx,
		DCP_REG(mmGRPH_X_END),
		value);

	value = 0;
	set_reg_field_value(value, local_size.grph.surface_size.height,
			GRPH_Y_END, GRPH_Y_END);
	dm_write_reg(
		mem_input110->base.ctx,
		DCP_REG(mmGRPH_Y_END),
		value);

	value = 0;
	set_reg_field_value(value, local_size.grph.surface_pitch,
			GRPH_PITCH, GRPH_PITCH);
	dm_write_reg(
		mem_input110->base.ctx,
		DCP_REG(mmGRPH_PITCH),
		value);

	value = 0;
	switch (rotation) {
	case ROTATION_ANGLE_90:
		set_reg_field_value(value, 3,
			HW_ROTATION, GRPH_ROTATION_ANGLE);
		break;
	case ROTATION_ANGLE_180:
		set_reg_field_value(value, 2,
			HW_ROTATION, GRPH_ROTATION_ANGLE);
		break;
	case ROTATION_ANGLE_270:
		set_reg_field_value(value, 1,
			HW_ROTATION, GRPH_ROTATION_ANGLE);
		break;
	default:
		set_reg_field_value(value, 0,
			HW_ROTATION, GRPH_ROTATION_ANGLE);
		break;
	}
	dm_write_reg(
		mem_input110->base.ctx,
		DCP_REG(mmHW_ROTATION),
		value);
}

static void program_pixel_format(
	struct dce110_mem_input *mem_input110,
	enum surface_pixel_format format)
{
	if (format >= SURFACE_PIXEL_FORMAT_GRPH_BEGIN &&
		format < SURFACE_PIXEL_FORMAT_VIDEO_BEGIN) {
		uint32_t value = 0;

		/* handle colour twizzle formats, swapping R and B */
		if (format == SURFACE_PIXEL_FORMAT_GRPH_BGRA8888 ||
			format == SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010 ||
			format ==
			SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010_XR_BIAS ||
			format == SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616F) {
			set_reg_field_value(
				value, 2, GRPH_SWAP_CNTL, GRPH_RED_CROSSBAR);
			set_reg_field_value(
				value, 2, GRPH_SWAP_CNTL, GRPH_BLUE_CROSSBAR);
		}

		dm_write_reg(
			mem_input110->base.ctx,
			DCP_REG(mmGRPH_SWAP_CNTL),
			value);

		value =	dm_read_reg(
				mem_input110->base.ctx,
				DCP_REG(mmGRPH_CONTROL));

		switch (format) {
		case SURFACE_PIXEL_FORMAT_GRPH_PALETA_256_COLORS:
			set_reg_field_value(
				value, 0, GRPH_CONTROL, GRPH_DEPTH);
			set_reg_field_value(
				value, 0, GRPH_CONTROL, GRPH_FORMAT);
			break;
		case SURFACE_PIXEL_FORMAT_GRPH_RGB565:
			set_reg_field_value(
				value, 1, GRPH_CONTROL, GRPH_DEPTH);
			set_reg_field_value(
				value, 1, GRPH_CONTROL, GRPH_FORMAT);
			break;
		case SURFACE_PIXEL_FORMAT_GRPH_ARGB8888:
		case SURFACE_PIXEL_FORMAT_GRPH_BGRA8888:
			set_reg_field_value(
				value, 2, GRPH_CONTROL, GRPH_DEPTH);
			set_reg_field_value(
				value, 0, GRPH_CONTROL, GRPH_FORMAT);
			break;
		case SURFACE_PIXEL_FORMAT_GRPH_ARGB2101010:
		case SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010:
		case SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010_XR_BIAS:
			set_reg_field_value(
				value, 2, GRPH_CONTROL, GRPH_DEPTH);
			set_reg_field_value(
				value, 1, GRPH_CONTROL, GRPH_FORMAT);
			break;
		case SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616:
		case SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616F:
			set_reg_field_value(
				value, 3, GRPH_CONTROL, GRPH_DEPTH);
			set_reg_field_value(
				value, 0, GRPH_CONTROL, GRPH_FORMAT);
			break;
		default:
			break;
		}
		dm_write_reg(
			mem_input110->base.ctx,
			DCP_REG(mmGRPH_CONTROL),
			value);

		/*TODO [hwentlan] MOVE THIS TO CONTROLLER GAMMA!!!!!*/
		value = dm_read_reg(
			mem_input110->base.ctx,
			DCP_REG(mmPRESCALE_GRPH_CONTROL));

		if (format == SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616F) {
			set_reg_field_value(
				value, 1, PRESCALE_GRPH_CONTROL,
				GRPH_PRESCALE_SELECT);
			set_reg_field_value(
				value, 1, PRESCALE_GRPH_CONTROL,
				GRPH_PRESCALE_R_SIGN);
			set_reg_field_value(
				value, 1, PRESCALE_GRPH_CONTROL,
				GRPH_PRESCALE_G_SIGN);
			set_reg_field_value(
				value, 1, PRESCALE_GRPH_CONTROL,
				GRPH_PRESCALE_B_SIGN);
		} else {
			set_reg_field_value(
				value, 0, PRESCALE_GRPH_CONTROL,
				GRPH_PRESCALE_SELECT);
			set_reg_field_value(
				value, 0, PRESCALE_GRPH_CONTROL,
				GRPH_PRESCALE_R_SIGN);
			set_reg_field_value(
				value, 0, PRESCALE_GRPH_CONTROL,
				GRPH_PRESCALE_G_SIGN);
			set_reg_field_value(
				value, 0, PRESCALE_GRPH_CONTROL,
				GRPH_PRESCALE_B_SIGN);
		}
		dm_write_reg(
			mem_input110->base.ctx,
			DCP_REG(mmPRESCALE_GRPH_CONTROL),
			value);
	}
}

void dce110_mem_input_wait_for_no_surface_update_pending(struct mem_input *mem_input)
{
	struct dce110_mem_input *mem_input110 = TO_DCE110_MEM_INPUT(mem_input);
	uint32_t value;

	do {
		value = dm_read_reg(mem_input110->base.ctx, DCP_REG(mmGRPH_UPDATE));
	} while (get_reg_field_value(value, GRPH_UPDATE, GRPH_SURFACE_UPDATE_PENDING));
}

bool dce110_mem_input_program_surface_flip_and_addr(
	struct mem_input *mem_input,
	const struct dc_plane_address *address,
	bool flip_immediate)
{
	struct dce110_mem_input *mem_input110 = TO_DCE110_MEM_INPUT(mem_input);

	uint32_t value = 0;

	value = dm_read_reg(mem_input110->base.ctx, DCP_REG(mmGRPH_FLIP_CONTROL));
	if (flip_immediate) {
		set_reg_field_value(value, 1, GRPH_FLIP_CONTROL, GRPH_SURFACE_UPDATE_IMMEDIATE_EN);
		set_reg_field_value(value, 1, GRPH_FLIP_CONTROL, GRPH_SURFACE_UPDATE_H_RETRACE_EN);
	} else {
		set_reg_field_value(value, 0, GRPH_FLIP_CONTROL, GRPH_SURFACE_UPDATE_IMMEDIATE_EN);
		set_reg_field_value(value, 0, GRPH_FLIP_CONTROL, GRPH_SURFACE_UPDATE_H_RETRACE_EN);
	}
	dm_write_reg(mem_input110->base.ctx, DCP_REG(mmGRPH_FLIP_CONTROL), value);

	switch (address->type) {
	case PLN_ADDR_TYPE_GRAPHICS:
		if (address->grph.addr.quad_part == 0)
			break;
		program_pri_addr(mem_input110, address->grph.addr);
		break;
	case PLN_ADDR_TYPE_GRPH_STEREO:
		if (address->grph_stereo.left_addr.quad_part == 0
			|| address->grph_stereo.right_addr.quad_part == 0)
			break;
		program_pri_addr(mem_input110, address->grph_stereo.left_addr);
		program_sec_addr(mem_input110, address->grph_stereo.right_addr);
		break;
	default:
		/* not supported */
		BREAK_TO_DEBUGGER();
		break;
	}

	return true;
}

bool dce110_mem_input_program_surface_config(
	struct mem_input *mem_input,
	enum surface_pixel_format format,
	struct dc_tiling_info *tiling_info,
	union plane_size *plane_size,
	enum dc_rotation_angle rotation)
{
	struct dce110_mem_input *mem_input110 = TO_DCE110_MEM_INPUT(mem_input);

	enable(mem_input110);
	program_tiling(mem_input110, tiling_info, format);
	program_size_and_rotation(mem_input110, rotation, plane_size);
	program_pixel_format(mem_input110, format);

	return true;
}

static void program_urgency_watermark(
	const struct dc_context *ctx,
	const uint32_t offset,
	struct bw_watermarks marks_low,
	uint32_t total_dest_line_time_ns)
{
	/* register value */
	uint32_t urgency_cntl = 0;
	uint32_t wm_mask_cntl = 0;

	uint32_t urgency_addr = offset + mmDPG_PIPE_URGENCY_CONTROL;
	uint32_t wm_addr = offset + mmDPG_WATERMARK_MASK_CONTROL;

	/*Write mask to enable reading/writing of watermark set A*/
	wm_mask_cntl = dm_read_reg(ctx, wm_addr);
	set_reg_field_value(wm_mask_cntl,
			1,
			DPG_WATERMARK_MASK_CONTROL,
			URGENCY_WATERMARK_MASK);
	dm_write_reg(ctx, wm_addr, wm_mask_cntl);

	urgency_cntl = dm_read_reg(ctx, urgency_addr);

	set_reg_field_value(
		urgency_cntl,
		marks_low.d_mark,
		DPG_PIPE_URGENCY_CONTROL,
		URGENCY_LOW_WATERMARK);

	set_reg_field_value(
		urgency_cntl,
		total_dest_line_time_ns,
		DPG_PIPE_URGENCY_CONTROL,
		URGENCY_HIGH_WATERMARK);
	dm_write_reg(ctx, urgency_addr, urgency_cntl);

	/*Write mask to enable reading/writing of watermark set B*/
	wm_mask_cntl = dm_read_reg(ctx, wm_addr);
	set_reg_field_value(wm_mask_cntl,
			2,
			DPG_WATERMARK_MASK_CONTROL,
			URGENCY_WATERMARK_MASK);
	dm_write_reg(ctx, wm_addr, wm_mask_cntl);

	urgency_cntl = dm_read_reg(ctx, urgency_addr);

	set_reg_field_value(urgency_cntl,
		marks_low.a_mark,
		DPG_PIPE_URGENCY_CONTROL,
		URGENCY_LOW_WATERMARK);

	set_reg_field_value(urgency_cntl,
		total_dest_line_time_ns,
		DPG_PIPE_URGENCY_CONTROL,
		URGENCY_HIGH_WATERMARK);
	dm_write_reg(ctx, urgency_addr, urgency_cntl);
}

static void program_stutter_watermark(
	const struct dc_context *ctx,
	const uint32_t offset,
	struct bw_watermarks marks)
{
	/* register value */
	uint32_t stutter_cntl = 0;
	uint32_t wm_mask_cntl = 0;

	uint32_t stutter_addr = offset + mmDPG_PIPE_STUTTER_CONTROL;
	uint32_t wm_addr = offset + mmDPG_WATERMARK_MASK_CONTROL;

	/*Write mask to enable reading/writing of watermark set A*/

	wm_mask_cntl = dm_read_reg(ctx, wm_addr);
	set_reg_field_value(wm_mask_cntl,
		1,
		DPG_WATERMARK_MASK_CONTROL,
		STUTTER_EXIT_SELF_REFRESH_WATERMARK_MASK);
	dm_write_reg(ctx, wm_addr, wm_mask_cntl);

	stutter_cntl = dm_read_reg(ctx, stutter_addr);

	set_reg_field_value(stutter_cntl,
		1,
		DPG_PIPE_STUTTER_CONTROL,
		STUTTER_ENABLE);
	set_reg_field_value(stutter_cntl,
		1,
		DPG_PIPE_STUTTER_CONTROL,
		STUTTER_IGNORE_FBC);

	/*Write watermark set A*/
	set_reg_field_value(stutter_cntl,
		marks.d_mark,
		DPG_PIPE_STUTTER_CONTROL,
		STUTTER_EXIT_SELF_REFRESH_WATERMARK);
	dm_write_reg(ctx, stutter_addr, stutter_cntl);

	/*Write mask to enable reading/writing of watermark set B*/
	wm_mask_cntl = dm_read_reg(ctx, wm_addr);
	set_reg_field_value(wm_mask_cntl,
		2,
		DPG_WATERMARK_MASK_CONTROL,
		STUTTER_EXIT_SELF_REFRESH_WATERMARK_MASK);
	dm_write_reg(ctx, wm_addr, wm_mask_cntl);

	stutter_cntl = dm_read_reg(ctx, stutter_addr);
	set_reg_field_value(stutter_cntl,
		1,
		DPG_PIPE_STUTTER_CONTROL,
		STUTTER_ENABLE);
	set_reg_field_value(stutter_cntl,
		1,
		DPG_PIPE_STUTTER_CONTROL,
		STUTTER_IGNORE_FBC);

	/*Write watermark set B*/
	set_reg_field_value(stutter_cntl,
		marks.a_mark,
		DPG_PIPE_STUTTER_CONTROL,
		STUTTER_EXIT_SELF_REFRESH_WATERMARK);
	dm_write_reg(ctx, stutter_addr, stutter_cntl);
}

static void program_nbp_watermark(
	const struct dc_context *ctx,
	const uint32_t offset,
	struct bw_watermarks marks)
{
	uint32_t value;
	uint32_t addr;
	/* Write mask to enable reading/writing of watermark set A */
	addr = offset + mmDPG_WATERMARK_MASK_CONTROL;
	value = dm_read_reg(ctx, addr);
	set_reg_field_value(
		value,
		1,
		DPG_WATERMARK_MASK_CONTROL,
		NB_PSTATE_CHANGE_WATERMARK_MASK);
	dm_write_reg(ctx, addr, value);

	addr = offset + mmDPG_PIPE_NB_PSTATE_CHANGE_CONTROL;
	value = dm_read_reg(ctx, addr);
	set_reg_field_value(
		value,
		1,
		DPG_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_ENABLE);
	set_reg_field_value(
		value,
		1,
		DPG_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_URGENT_DURING_REQUEST);
	set_reg_field_value(
		value,
		1,
		DPG_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_NOT_SELF_REFRESH_DURING_REQUEST);
	dm_write_reg(ctx, addr, value);

	/* Write watermark set A */
	value = dm_read_reg(ctx, addr);
	set_reg_field_value(
		value,
		marks.d_mark,
		DPG_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_WATERMARK);
	dm_write_reg(ctx, addr, value);

	/* Write mask to enable reading/writing of watermark set B */
	addr = offset + mmDPG_WATERMARK_MASK_CONTROL;
	value = dm_read_reg(ctx, addr);
	set_reg_field_value(
		value,
		2,
		DPG_WATERMARK_MASK_CONTROL,
		NB_PSTATE_CHANGE_WATERMARK_MASK);
	dm_write_reg(ctx, addr, value);

	addr = offset + mmDPG_PIPE_NB_PSTATE_CHANGE_CONTROL;
	value = dm_read_reg(ctx, addr);
	set_reg_field_value(
		value,
		1,
		DPG_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_ENABLE);
	set_reg_field_value(
		value,
		1,
		DPG_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_URGENT_DURING_REQUEST);
	set_reg_field_value(
		value,
		1,
		DPG_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_NOT_SELF_REFRESH_DURING_REQUEST);
	dm_write_reg(ctx, addr, value);

	/* Write watermark set B */
	value = dm_read_reg(ctx, addr);
	set_reg_field_value(
		value,
		marks.a_mark,
		DPG_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_WATERMARK);
	dm_write_reg(ctx, addr, value);
}

void dce110_mem_input_program_display_marks(
	struct mem_input *mem_input,
	struct bw_watermarks nbp,
	struct bw_watermarks stutter,
	struct bw_watermarks urgent,
	uint32_t total_dest_line_time_ns)
{
	struct dce110_mem_input *bm_dce110 = TO_DCE110_MEM_INPUT(mem_input);

	program_urgency_watermark(
		mem_input->ctx,
		bm_dce110->offsets.dmif,
		urgent,
		total_dest_line_time_ns);

	program_nbp_watermark(
		mem_input->ctx,
		bm_dce110->offsets.dmif,
		nbp);

	program_stutter_watermark(
		mem_input->ctx,
		bm_dce110->offsets.dmif,
		stutter);
}

static uint32_t get_dmif_switch_time_us(
	uint32_t h_total,
	uint32_t v_total,
	uint32_t pix_clk_khz)
{
	uint32_t frame_time;
	uint32_t pixels_per_second;
	uint32_t pixels_per_frame;
	uint32_t refresh_rate;
	const uint32_t us_in_sec = 1000000;
	const uint32_t min_single_frame_time_us = 30000;
	/*return double of frame time*/
	const uint32_t single_frame_time_multiplier = 2;

	if (!h_total || v_total || !pix_clk_khz)
		return single_frame_time_multiplier * min_single_frame_time_us;

	/*TODO: should we use pixel format normalized pixel clock here?*/
	pixels_per_second = pix_clk_khz * 1000;
	pixels_per_frame = h_total * v_total;

	if (!pixels_per_second || !pixels_per_frame) {
		/* avoid division by zero */
		ASSERT(pixels_per_frame);
		ASSERT(pixels_per_second);
		return single_frame_time_multiplier * min_single_frame_time_us;
	}

	refresh_rate = pixels_per_second / pixels_per_frame;

	if (!refresh_rate) {
		/* avoid division by zero*/
		ASSERT(refresh_rate);
		return single_frame_time_multiplier * min_single_frame_time_us;
	}

	frame_time = us_in_sec / refresh_rate;

	if (frame_time < min_single_frame_time_us)
		frame_time = min_single_frame_time_us;

	frame_time *= single_frame_time_multiplier;

	return frame_time;
}

void dce110_allocate_mem_input(
	struct mem_input *mi,
	uint32_t h_total,/* for current stream */
	uint32_t v_total,/* for current stream */
	uint32_t pix_clk_khz,/* for current stream */
	uint32_t total_stream_num)
{
	const uint32_t retry_delay = 10;
	uint32_t retry_count = get_dmif_switch_time_us(
			h_total,
			v_total,
			pix_clk_khz) / retry_delay;

	struct dce110_mem_input *bm110 = TO_DCE110_MEM_INPUT(mi);
	uint32_t addr = bm110->offsets.pipe + mmPIPE0_DMIF_BUFFER_CONTROL;
	uint32_t value;
	uint32_t field;
	uint32_t pix_dur;

	if (bm110->supported_stutter_mode
			& STUTTER_MODE_NO_DMIF_BUFFER_ALLOCATION)
		goto register_underflow_int;

	/*Allocate DMIF buffer*/
	value = dm_read_reg(mi->ctx, addr);
	field = get_reg_field_value(
		value, PIPE0_DMIF_BUFFER_CONTROL, DMIF_BUFFERS_ALLOCATED);
	if (field == 2)
		goto register_underflow_int;

	set_reg_field_value(
			value,
			2,
			PIPE0_DMIF_BUFFER_CONTROL,
			DMIF_BUFFERS_ALLOCATED);

	dm_write_reg(mi->ctx, addr, value);

	do {
		value = dm_read_reg(mi->ctx, addr);
		field = get_reg_field_value(
			value,
			PIPE0_DMIF_BUFFER_CONTROL,
			DMIF_BUFFERS_ALLOCATION_COMPLETED);

		if (field)
			break;

		udelay(retry_delay);
		retry_count--;

	} while (retry_count > 0);

	if (field == 0)
		dal_logger_write(mi->ctx->logger,
				LOG_MAJOR_ERROR,
				LOG_MINOR_COMPONENT_GPU,
				"%s: DMIF allocation failed",
				__func__);

	if (pix_clk_khz != 0) {
		addr = mmDPG_PIPE_ARBITRATION_CONTROL1 + bm110->offsets.dmif;
		value = dm_read_reg(mi->ctx, addr);
		pix_dur = 1000000000ULL / pix_clk_khz;

		set_reg_field_value(
			value,
			pix_dur,
			DPG_PIPE_ARBITRATION_CONTROL1,
			PIXEL_DURATION);

		dm_write_reg(mi->ctx, addr, value);
	}

	/*
	 * Stella Wong proposed the following change
	 *
	 * Value of mcHubRdReqDmifLimit.ENABLE:
	 * 00 - disable DMIF rdreq limit
	 * 01 - enable DMIF rdreq limit, disabled by DMIF stall = 1 || urg != 0
	 * 02 - enable DMIF rdreq limit, disable by DMIF stall = 1
	 * 03 - force enable DMIF rdreq limit, ignore DMIF stall / urgent
	 */
	if (!IS_FPGA_MAXIMUS_DC(mi->ctx->dce_environment)) {
		addr = mmMC_HUB_RDREQ_DMIF_LIMIT;
		value = dm_read_reg(mi->ctx, addr);

		if (total_stream_num > 1)
			set_reg_field_value(value, 0, MC_HUB_RDREQ_DMIF_LIMIT, ENABLE);
		else
			set_reg_field_value(value, 3, MC_HUB_RDREQ_DMIF_LIMIT, ENABLE);
		dm_write_reg(mi->ctx, addr, value);
	}

register_underflow_int:
	/*todo*/;
	/*register_interrupt(bm110, irq_source, ctrl_id);*/
}

static void deallocate_dmif_buffer_helper(
				struct dc_context *ctx, uint32_t offset)
{
	uint32_t value;
	uint32_t count = 0xBB8; /* max retry count */

	value = dm_read_reg(ctx, mmPIPE0_DMIF_BUFFER_CONTROL + offset);

	if (!get_reg_field_value(
		value, PIPE0_DMIF_BUFFER_CONTROL, DMIF_BUFFERS_ALLOCATED))
		return;

	set_reg_field_value(
		value, 0, PIPE0_DMIF_BUFFER_CONTROL, DMIF_BUFFERS_ALLOCATED);

	dm_write_reg(
		ctx, mmPIPE0_DMIF_BUFFER_CONTROL + offset, value);

	do {
		value = dm_read_reg(ctx, mmPIPE0_DMIF_BUFFER_CONTROL + offset);
		udelay(10);
		count--;
	} while (count > 0 &&
		!get_reg_field_value(
			value,
			PIPE0_DMIF_BUFFER_CONTROL,
			DMIF_BUFFERS_ALLOCATION_COMPLETED));
}

void dce110_free_mem_input(
	struct mem_input *mi,
	uint32_t total_stream_num)
{
	struct dce110_mem_input *bm_dce110 = TO_DCE110_MEM_INPUT(mi);
	uint32_t value;

	if (!(bm_dce110->supported_stutter_mode &
		STUTTER_MODE_NO_DMIF_BUFFER_ALLOCATION)) {

		/* De-allocate DMIF buffer first */
		if (mmPIPE0_DMIF_BUFFER_CONTROL + bm_dce110->offsets.pipe != 0)
			deallocate_dmif_buffer_helper(
					mi->ctx, bm_dce110->offsets.pipe);
	}

	/* TODO: unregister underflow interrupt
	unregisterInterrupt();
	*/

	/* Value of mcHubRdReqDmifLimit.ENABLE.
	 * 00 - disable dmif rdreq limit
	 * 01 - enable dmif rdreq limit, disable by dmif stall=1||urg!=0
	 * 02 - enable dmif rdreq limit, disable by dmif stall=1
	 * 03 - force enable dmif rdreq limit, ignore dmif stall/urgent
	 * Stella Wong proposed this change. */
	if (!IS_FPGA_MAXIMUS_DC(mi->ctx->dce_environment)) {
		value = dm_read_reg(mi->ctx, mmMC_HUB_RDREQ_DMIF_LIMIT);
		if (total_stream_num > 1)
			set_reg_field_value(value, 0, MC_HUB_RDREQ_DMIF_LIMIT, ENABLE);
		else
			set_reg_field_value(value, 3, MC_HUB_RDREQ_DMIF_LIMIT, ENABLE);

		dm_write_reg(mi->ctx, mmMC_HUB_RDREQ_DMIF_LIMIT, value);
	}
}

static struct mem_input_funcs dce110_mem_input_funcs = {
	.mem_input_program_display_marks =
			dce110_mem_input_program_display_marks,
	.allocate_mem_input = dce110_allocate_mem_input,
	.free_mem_input = dce110_free_mem_input,
	.mem_input_program_surface_flip_and_addr =
			dce110_mem_input_program_surface_flip_and_addr,
	.mem_input_program_surface_config =
			dce110_mem_input_program_surface_config,
	.wait_for_no_surface_update_pending =
			dce110_mem_input_wait_for_no_surface_update_pending
};
/*****************************************/
/* Constructor, Destructor               */
/*****************************************/

bool dce110_mem_input_construct(
	struct dce110_mem_input *mem_input110,
	struct dc_context *ctx,
	uint32_t inst,
	const struct dce110_mem_input_reg_offsets *offsets)
{
	mem_input110->base.funcs = &dce110_mem_input_funcs;
	mem_input110->base.ctx = ctx;

	mem_input110->base.inst = inst;

	mem_input110->offsets = *offsets;

	mem_input110->supported_stutter_mode = 0;
	dal_adapter_service_get_feature_value(FEATURE_STUTTER_MODE,
			&(mem_input110->supported_stutter_mode),
			sizeof(mem_input110->supported_stutter_mode));

	return true;
}

void dce110_mem_input_destroy(struct mem_input **mem_input)
{
	dm_free(TO_DCE110_MEM_INPUT(*mem_input));
	*mem_input = NULL;
}
