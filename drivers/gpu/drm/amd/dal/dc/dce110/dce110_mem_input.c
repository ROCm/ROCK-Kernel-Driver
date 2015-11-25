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
/* TODO: this needs to be looked at, used by Stella's workaround*/
#include "gmc/gmc_8_2_d.h"
#include "gmc/gmc_8_2_sh_mask.h"

#include "include/logger_interface.h"
#include "adapter_service_interface.h"
#include "inc/bandwidth_calcs.h"

#include "dce110_mem_input.h"

#define MAX_WATERMARK 0xFFFF
#define SAFE_NBP_MARK 0x7FFF

#define DCP_REG(reg) (reg + mem_input110->offsets.dcp)
#define DMIF_REG(reg) (reg + mem_input110->offsets.dmif)
#define PIPE_REG(reg) (reg + mem_input110->offsets.pipe)

static const struct dce110_mem_input_reg_offsets reg_offsets[] =  {
{
	.dcp = 0,
	.dmif = 0,
	.pipe = 0,
},
{
	.dcp = (mmDCP1_GRPH_CONTROL - mmDCP0_GRPH_CONTROL),
	.dmif = (mmDMIF_PG1_DPG_WATERMARK_MASK_CONTROL
				- mmDMIF_PG0_DPG_WATERMARK_MASK_CONTROL),
	.pipe = (mmPIPE1_DMIF_BUFFER_CONTROL - mmPIPE0_DMIF_BUFFER_CONTROL),
},
{
	.dcp = (mmDCP2_GRPH_CONTROL - mmDCP0_GRPH_CONTROL),
	.dmif = (mmDMIF_PG2_DPG_WATERMARK_MASK_CONTROL
				- mmDMIF_PG0_DPG_WATERMARK_MASK_CONTROL),
	.pipe = (mmPIPE2_DMIF_BUFFER_CONTROL - mmPIPE0_DMIF_BUFFER_CONTROL),
}
};

static void set_flip_control(
	struct dce110_mem_input *mem_input110,
	bool immediate)
{
	uint32_t value = 0;

	value = dal_read_reg(
			mem_input110->base.ctx,
			DCP_REG(mmGRPH_FLIP_CONTROL));
	set_reg_field_value(value, 0,
				GRPH_FLIP_CONTROL,
				GRPH_SURFACE_UPDATE_IMMEDIATE_EN);
	set_reg_field_value(value, 0,
				GRPH_FLIP_CONTROL,
				GRPH_SURFACE_UPDATE_H_RETRACE_EN);
	if (immediate == true)
		set_reg_field_value(value, 1,
			GRPH_FLIP_CONTROL,
			GRPH_SURFACE_UPDATE_IMMEDIATE_EN);

	dal_write_reg(
		mem_input110->base.ctx,
		DCP_REG(mmGRPH_FLIP_CONTROL),
		value);
}

static void program_sec_addr(
	struct dce110_mem_input *mem_input110,
	PHYSICAL_ADDRESS_LOC address)
{
	uint32_t value = 0;
	uint32_t temp = 0;
	/*high register MUST be programmed first*/
	temp = address.high_part &
GRPH_SECONDARY_SURFACE_ADDRESS_HIGH__GRPH_SECONDARY_SURFACE_ADDRESS_HIGH_MASK;

	set_reg_field_value(value, temp,
		GRPH_SECONDARY_SURFACE_ADDRESS_HIGH,
		GRPH_SECONDARY_SURFACE_ADDRESS_HIGH);

	dal_write_reg(
			mem_input110->base.ctx,
			DCP_REG(mmGRPH_SECONDARY_SURFACE_ADDRESS_HIGH),
			value);

	temp = 0;
	value = 0;
	temp = address.low_part >>
	GRPH_SECONDARY_SURFACE_ADDRESS__GRPH_SECONDARY_SURFACE_ADDRESS__SHIFT;

	set_reg_field_value(value, temp,
		GRPH_SECONDARY_SURFACE_ADDRESS,
		GRPH_SECONDARY_SURFACE_ADDRESS);

	dal_write_reg(
			mem_input110->base.ctx,
			DCP_REG(mmGRPH_SECONDARY_SURFACE_ADDRESS),
			value);
}

static void program_pri_addr(
	struct dce110_mem_input *mem_input110,
	PHYSICAL_ADDRESS_LOC address)
{
	uint32_t value = 0;
	uint32_t temp = 0;

	/*high register MUST be programmed first*/
	temp = address.high_part &
GRPH_PRIMARY_SURFACE_ADDRESS_HIGH__GRPH_PRIMARY_SURFACE_ADDRESS_HIGH_MASK;

	set_reg_field_value(value, temp,
		GRPH_PRIMARY_SURFACE_ADDRESS_HIGH,
		GRPH_PRIMARY_SURFACE_ADDRESS_HIGH);

	dal_write_reg(
		mem_input110->base.ctx,
		DCP_REG(mmGRPH_PRIMARY_SURFACE_ADDRESS_HIGH),
		value);

	temp = 0;
	value = 0;
	temp = address.low_part >>
	GRPH_PRIMARY_SURFACE_ADDRESS__GRPH_PRIMARY_SURFACE_ADDRESS__SHIFT;

	set_reg_field_value(value, temp,
		GRPH_PRIMARY_SURFACE_ADDRESS,
		GRPH_PRIMARY_SURFACE_ADDRESS);

	dal_write_reg(
		mem_input110->base.ctx,
		DCP_REG(mmGRPH_PRIMARY_SURFACE_ADDRESS),
		value);
}

static void program_addr(
	struct dce110_mem_input *mem_input110,
	const struct dc_plane_address *addr)
{
	switch (addr->type) {
	case PLN_ADDR_TYPE_GRAPHICS:
		program_pri_addr(
			mem_input110,
			addr->grph.addr);
		break;
	case PLN_ADDR_TYPE_GRPH_STEREO:
		program_pri_addr(
			mem_input110,
			addr->grph_stereo.left_addr);
		program_sec_addr(
			mem_input110,
			addr->grph_stereo.right_addr);
		break;
	case PLN_ADDR_TYPE_VIDEO_PROGRESSIVE:
	case PLN_ADDR_TYPE_VIDEO_INTERLACED:
	case PLN_ADDR_TYPE_VIDEO_PROGRESSIVE_STEREO:
	case PLN_ADDR_TYPE_VIDEO_INTERLACED_STEREO:
	default:
		/* not supported */
		BREAK_TO_DEBUGGER();
	}
}

static void enable(struct dce110_mem_input *mem_input110)
{
	uint32_t value = 0;

	value = dal_read_reg(mem_input110->base.ctx, DCP_REG(mmGRPH_ENABLE));
	set_reg_field_value(value, 1, GRPH_ENABLE, GRPH_ENABLE);
	dal_write_reg(mem_input110->base.ctx,
		DCP_REG(mmGRPH_ENABLE),
		value);
}

static void program_tiling(
	struct dce110_mem_input *mem_input110,
	const union plane_tiling_info *info,
	const enum surface_pixel_format pixel_format)
{
	uint32_t value = 0;

	value = dal_read_reg(
			mem_input110->base.ctx,
			DCP_REG(mmGRPH_CONTROL));

	set_reg_field_value(value, info->grph.NUM_BANKS,
		GRPH_CONTROL, GRPH_NUM_BANKS);

	set_reg_field_value(value, info->grph.BANK_WIDTH,
		GRPH_CONTROL, GRPH_BANK_WIDTH);

	set_reg_field_value(value, info->grph.BANK_HEIGHT,
		GRPH_CONTROL, GRPH_BANK_HEIGHT);

	set_reg_field_value(value, info->grph.TILE_ASPECT,
		GRPH_CONTROL, GRPH_MACRO_TILE_ASPECT);

	set_reg_field_value(value, info->grph.TILE_SPLIT,
		GRPH_CONTROL, GRPH_TILE_SPLIT);

	set_reg_field_value(value, info->grph.TILE_MODE,
		GRPH_CONTROL, GRPH_MICRO_TILE_MODE);

	set_reg_field_value(value, info->grph.PIPE_CONFIG,
		GRPH_CONTROL, GRPH_PIPE_CONFIG);

	set_reg_field_value(value, info->grph.ARRAY_MODE,
		GRPH_CONTROL, GRPH_ARRAY_MODE);

	set_reg_field_value(value, 1,
		GRPH_CONTROL, GRPH_COLOR_EXPANSION_MODE);

	set_reg_field_value(value, 0,
		GRPH_CONTROL, GRPH_Z);

	dal_write_reg(
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
	dal_write_reg(
		mem_input110->base.ctx,
		DCP_REG(mmGRPH_X_START),
		value);

	value = 0;
	set_reg_field_value(value, local_size.grph.surface_size.y,
			GRPH_Y_START, GRPH_Y_START);
	dal_write_reg(
		mem_input110->base.ctx,
		DCP_REG(mmGRPH_Y_START),
		value);

	value = 0;
	set_reg_field_value(value, local_size.grph.surface_size.width,
			GRPH_X_END, GRPH_X_END);
	dal_write_reg(
		mem_input110->base.ctx,
		DCP_REG(mmGRPH_X_END),
		value);

	value = 0;
	set_reg_field_value(value, local_size.grph.surface_size.height,
			GRPH_Y_END, GRPH_Y_END);
	dal_write_reg(
		mem_input110->base.ctx,
		DCP_REG(mmGRPH_Y_END),
		value);

	value = 0;
	set_reg_field_value(value, local_size.grph.surface_pitch,
			GRPH_PITCH, GRPH_PITCH);
	dal_write_reg(
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
	dal_write_reg(
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

		dal_write_reg(
			mem_input110->base.ctx,
			DCP_REG(mmGRPH_SWAP_CNTL),
			value);


		value =	dal_read_reg(
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
		dal_write_reg(
			mem_input110->base.ctx,
			DCP_REG(mmGRPH_CONTROL),
			value);

		/*TODO [hwentlan] MOVE THIS TO CONTROLLER GAMMA!!!!!*/
		value = dal_read_reg(
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
		dal_write_reg(
			mem_input110->base.ctx,
			DCP_REG(mmPRESCALE_GRPH_CONTROL),
			value);
	}
}

bool dce110_mem_input_program_surface_flip_and_addr(
	struct mem_input *mem_input,
	const struct dc_plane_address *address,
	bool flip_immediate)
{
	struct dce110_mem_input *mem_input110 = TO_DCE110_MEM_INPUT(mem_input);

	set_flip_control(mem_input110, flip_immediate);
	program_addr(mem_input110,
		address);

	return true;
}

bool dce110_mem_input_program_surface_config(
	struct mem_input *mem_input,
	const struct dc_surface *surface)
{
	struct dce110_mem_input *mem_input110 = TO_DCE110_MEM_INPUT(mem_input);

	enable(mem_input110);
	program_tiling(mem_input110, &surface->tiling_info, surface->format);
	program_size_and_rotation(mem_input110,
		surface->rotation, &surface->plane_size);
	program_pixel_format(mem_input110, surface->format);

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
	wm_mask_cntl = dal_read_reg(ctx, wm_addr);
	set_reg_field_value(wm_mask_cntl,
			1,
			DPG_WATERMARK_MASK_CONTROL,
			URGENCY_WATERMARK_MASK);
	dal_write_reg(ctx, wm_addr, wm_mask_cntl);

	urgency_cntl = dal_read_reg(ctx, urgency_addr);

	set_reg_field_value(
		urgency_cntl,
		marks_low.a_mark,
		DPG_PIPE_URGENCY_CONTROL,
		URGENCY_LOW_WATERMARK);

	set_reg_field_value(
		urgency_cntl,
		total_dest_line_time_ns,
		DPG_PIPE_URGENCY_CONTROL,
		URGENCY_HIGH_WATERMARK);
	dal_write_reg(ctx, urgency_addr, urgency_cntl);


	/*Write mask to enable reading/writing of watermark set B*/
	wm_mask_cntl = dal_read_reg(ctx, wm_addr);
	set_reg_field_value(wm_mask_cntl,
			2,
			DPG_WATERMARK_MASK_CONTROL,
			URGENCY_WATERMARK_MASK);
	dal_write_reg(ctx, wm_addr, wm_mask_cntl);

	urgency_cntl = dal_read_reg(ctx, urgency_addr);

	set_reg_field_value(urgency_cntl,
		marks_low.b_mark,
		DPG_PIPE_URGENCY_CONTROL,
		URGENCY_LOW_WATERMARK);

	set_reg_field_value(urgency_cntl,
		total_dest_line_time_ns,
		DPG_PIPE_URGENCY_CONTROL,
		URGENCY_HIGH_WATERMARK);
	dal_write_reg(ctx, urgency_addr, urgency_cntl);
}

void dce110_mem_input_program_stutter_watermark(
	struct mem_input *mi,
	struct bw_watermarks marks)
{
	const struct dc_context *ctx = mi->ctx;
	const uint32_t offset = TO_DCE110_MEM_INPUT(mi)->offsets.dmif;
	/* register value */
	uint32_t stutter_cntl = 0;
	uint32_t wm_mask_cntl = 0;

	uint32_t stutter_addr = offset + mmDPG_PIPE_STUTTER_CONTROL;
	uint32_t wm_addr = offset + mmDPG_WATERMARK_MASK_CONTROL;

	/*Write mask to enable reading/writing of watermark set A*/

	wm_mask_cntl = dal_read_reg(ctx, wm_addr);
	set_reg_field_value(wm_mask_cntl,
		1,
		DPG_WATERMARK_MASK_CONTROL,
		STUTTER_EXIT_SELF_REFRESH_WATERMARK_MASK);
	dal_write_reg(ctx, wm_addr, wm_mask_cntl);

	stutter_cntl = dal_read_reg(ctx, stutter_addr);

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
		marks.a_mark,
		DPG_PIPE_STUTTER_CONTROL,
		STUTTER_EXIT_SELF_REFRESH_WATERMARK);
	dal_write_reg(ctx, stutter_addr, stutter_cntl);

	/*Write mask to enable reading/writing of watermark set B*/
	wm_mask_cntl = dal_read_reg(ctx, wm_addr);
	set_reg_field_value(wm_mask_cntl,
		2,
		DPG_WATERMARK_MASK_CONTROL,
		STUTTER_EXIT_SELF_REFRESH_WATERMARK_MASK);
	dal_write_reg(ctx, wm_addr, wm_mask_cntl);

	stutter_cntl = dal_read_reg(ctx, stutter_addr);
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
		marks.b_mark,
		DPG_PIPE_STUTTER_CONTROL,
		STUTTER_EXIT_SELF_REFRESH_WATERMARK);
	dal_write_reg(ctx, stutter_addr, stutter_cntl);
}

void dce110_mem_input_program_nbp_watermark(
	struct mem_input *mi,
	struct bw_watermarks marks)
{
	const struct dc_context *ctx = mi->ctx;
	const uint32_t offset = TO_DCE110_MEM_INPUT(mi)->offsets.dmif;
	uint32_t value;
	uint32_t addr;
	/* Write mask to enable reading/writing of watermark set A */
	addr = offset + mmDPG_WATERMARK_MASK_CONTROL;
	value = dal_read_reg(ctx, addr);
	set_reg_field_value(
		value,
		1,
		DPG_WATERMARK_MASK_CONTROL,
		NB_PSTATE_CHANGE_WATERMARK_MASK);
	dal_write_reg(ctx, addr, value);

	addr = offset + mmDPG_PIPE_NB_PSTATE_CHANGE_CONTROL;
	value = dal_read_reg(ctx, addr);
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
	dal_write_reg(ctx, addr, value);

	/* Write watermark set A */
	value = dal_read_reg(ctx, addr);
	set_reg_field_value(
		value,
		marks.a_mark,
		DPG_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_WATERMARK);
	dal_write_reg(ctx, addr, value);

	/* Write mask to enable reading/writing of watermark set B */
	addr = offset + mmDPG_WATERMARK_MASK_CONTROL;
	value = dal_read_reg(ctx, addr);
	set_reg_field_value(
		value,
		2,
		DPG_WATERMARK_MASK_CONTROL,
		NB_PSTATE_CHANGE_WATERMARK_MASK);
	dal_write_reg(ctx, addr, value);

	addr = offset + mmDPG_PIPE_NB_PSTATE_CHANGE_CONTROL;
	value = dal_read_reg(ctx, addr);
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
	dal_write_reg(ctx, addr, value);

	/* Write watermark set B */
	value = dal_read_reg(ctx, addr);
	set_reg_field_value(
		value,
		marks.b_mark,
		DPG_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_WATERMARK);
	dal_write_reg(ctx, addr, value);
}

void dce110_mem_input_program_safe_display_marks(struct mem_input *mi)
{
	struct dce110_mem_input *bm_dce110 = TO_DCE110_MEM_INPUT(mi);
	struct bw_watermarks max_marks = { MAX_WATERMARK, MAX_WATERMARK };
	struct bw_watermarks nbp_marks = { SAFE_NBP_MARK, SAFE_NBP_MARK };

	program_urgency_watermark(
		mi->ctx, bm_dce110->offsets.dmif, max_marks, MAX_WATERMARK);
	dce110_mem_input_program_stutter_watermark(mi, max_marks);
	dce110_mem_input_program_nbp_watermark(mi, nbp_marks);

}

void dce110_mem_input_program_urgency_watermark(
	struct mem_input *mi,
	struct bw_watermarks marks,
	uint32_t h_total,
	uint32_t pixel_clk_in_khz,
	uint32_t pstate_blackout_duration_ns)
{
	struct dce110_mem_input *bm_dce110 = TO_DCE110_MEM_INPUT(mi);
	uint32_t total_dest_line_time_ns = 1000000UL * h_total
		/ pixel_clk_in_khz + pstate_blackout_duration_ns;

	program_urgency_watermark(
		mi->ctx,
		bm_dce110->offsets.dmif,
		marks,
		total_dest_line_time_ns);
}

static uint32_t get_dmif_switch_time_us(struct dc_crtc_timing *timing)
{
	uint32_t frame_time;
	uint32_t pixels_per_second;
	uint32_t pixels_per_frame;
	uint32_t refresh_rate;
	const uint32_t us_in_sec = 1000000;
	const uint32_t min_single_frame_time_us = 30000;
	/*return double of frame time*/
	const uint32_t single_frame_time_multiplier = 2;

	if (timing == NULL)
		return single_frame_time_multiplier * min_single_frame_time_us;

	/*TODO: should we use pixel format normalized pixel clock here?*/
	pixels_per_second = timing->pix_clk_khz * 1000;
	pixels_per_frame = timing->h_total * timing->v_total;

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

void dce110_mem_input_allocate_dmif_buffer(
		struct mem_input *mi,
		struct dc_crtc_timing *timing,
		uint32_t paths_num)
{
	const uint32_t retry_delay = 10;
	uint32_t retry_count = get_dmif_switch_time_us(timing) / retry_delay;

	struct dce110_mem_input *bm110 = TO_DCE110_MEM_INPUT(mi);
	uint32_t addr = bm110->offsets.pipe + mmPIPE0_DMIF_BUFFER_CONTROL;
	uint32_t value;
	uint32_t field;

	if (bm110->supported_stutter_mode
			& STUTTER_MODE_NO_DMIF_BUFFER_ALLOCATION)
		goto register_underflow_int;

	/*Allocate DMIF buffer*/
	value = dal_read_reg(mi->ctx, addr);
	field = get_reg_field_value(
		value, PIPE0_DMIF_BUFFER_CONTROL, DMIF_BUFFERS_ALLOCATED);
	if (field == 2)
		goto register_underflow_int;

	set_reg_field_value(
			value,
			2,
			PIPE0_DMIF_BUFFER_CONTROL,
			DMIF_BUFFERS_ALLOCATED);

	dal_write_reg(mi->ctx, addr, value);

	do {
		value = dal_read_reg(mi->ctx, addr);
		field = get_reg_field_value(
			value,
			PIPE0_DMIF_BUFFER_CONTROL,
			DMIF_BUFFERS_ALLOCATION_COMPLETED);

		if (field)
			break;

		dc_service_delay_in_microseconds(mi->ctx, retry_delay);
		retry_count--;

	} while (retry_count > 0);

	if (field == 0)
		dal_logger_write(mi->ctx->logger,
				LOG_MAJOR_ERROR,
				LOG_MINOR_COMPONENT_GPU,
				"%s: DMIF allocation failed",
				__func__);

	/*
	 * Stella Wong proposed the following change
	 *
	 * Value of mcHubRdReqDmifLimit.ENABLE:
	 * 00 - disable DMIF rdreq limit
	 * 01 - enable DMIF rdreq limit, disabled by DMIF stall = 1 || urg != 0
	 * 02 - enable DMIF rdreq limit, disable by DMIF stall = 1
	 * 03 - force enable DMIF rdreq limit, ignore DMIF stall / urgent
	 */
	addr = mmMC_HUB_RDREQ_DMIF_LIMIT;
	value = dal_read_reg(mi->ctx, addr);
	if (paths_num > 1)
		set_reg_field_value(value, 0, MC_HUB_RDREQ_DMIF_LIMIT, ENABLE);
	else
		set_reg_field_value(value, 3, MC_HUB_RDREQ_DMIF_LIMIT, ENABLE);
	dal_write_reg(mi->ctx, addr, value);

register_underflow_int:
	/*todo*/;
	/*register_interrupt(bm110, irq_source, ctrl_id);*/
}

static void deallocate_dmif_buffer_helper(
				struct dc_context *ctx, uint32_t offset)
{
	uint32_t value;
	uint32_t count = 0xBB8; /* max retry count */

	value = dal_read_reg(ctx, mmPIPE0_DMIF_BUFFER_CONTROL + offset);

	if (!get_reg_field_value(
		value, PIPE0_DMIF_BUFFER_CONTROL, DMIF_BUFFERS_ALLOCATED))
		return;

	set_reg_field_value(
		value, 0, PIPE0_DMIF_BUFFER_CONTROL, DMIF_BUFFERS_ALLOCATED);

	dal_write_reg(
		ctx, mmPIPE0_DMIF_BUFFER_CONTROL + offset, value);

	do {
		value = dal_read_reg(ctx, mmPIPE0_DMIF_BUFFER_CONTROL + offset);
		dc_service_delay_in_microseconds(ctx, 10);
		count--;
	} while (count > 0 &&
		!get_reg_field_value(
			value,
			PIPE0_DMIF_BUFFER_CONTROL,
			DMIF_BUFFERS_ALLOCATION_COMPLETED));
}

void dce110_mem_input_deallocate_dmif_buffer(
	struct mem_input *mi, uint32_t paths_num)
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
	value = dal_read_reg(mi->ctx, mmMC_HUB_RDREQ_DMIF_LIMIT);
	if (paths_num > 1)
		set_reg_field_value(value, 0, MC_HUB_RDREQ_DMIF_LIMIT, ENABLE);
	else
		set_reg_field_value(value, 3, MC_HUB_RDREQ_DMIF_LIMIT, ENABLE);

	dal_write_reg(mi->ctx, mmMC_HUB_RDREQ_DMIF_LIMIT, value);
}

void dce110_mem_input_program_pix_dur(
	struct mem_input *mi, uint32_t pix_clk_khz)
{
	uint64_t pix_dur;
	uint32_t addr = mmDMIF_PG0_DPG_PIPE_ARBITRATION_CONTROL1
					+ TO_DCE110_MEM_INPUT(mi)->offsets.dmif;
	uint32_t value = dal_read_reg(mi->ctx, addr);

	if (pix_clk_khz == 0)
		return;

	pix_dur = 1000000000 / pix_clk_khz;

	set_reg_field_value(
		value,
		pix_dur,
		DPG_PIPE_ARBITRATION_CONTROL1,
		PIXEL_DURATION);

	dal_write_reg(mi->ctx, addr, value);
}

/*****************************************/
/* Constructor, Destructor               */
/*****************************************/

bool dce110_mem_input_construct(
	struct dce110_mem_input *mem_input110,
	struct dc_context *ctx,
	uint32_t inst)
{
	if ((inst < 1) || (inst > ARRAY_SIZE(reg_offsets)))
		return false;

	mem_input110->base.ctx = ctx;

	mem_input110->base.inst = inst;

	mem_input110->offsets = reg_offsets[inst - 1];

	mem_input110->supported_stutter_mode = 0;
	dal_adapter_service_get_feature_value(FEATURE_STUTTER_MODE,
			&(mem_input110->supported_stutter_mode),
			sizeof(mem_input110->supported_stutter_mode));

	return true;
}

void dce110_mem_input_destroy(struct mem_input **mem_input)
{
	dc_service_free((*mem_input)->ctx, TO_DCE110_MEM_INPUT(*mem_input));
	*mem_input = NULL;
}

struct mem_input *dce110_mem_input_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct dce110_mem_input *mem_input110 =
		dc_service_alloc(ctx, sizeof(struct dce110_mem_input));

	if (!mem_input110)
		return NULL;

	if (dce110_mem_input_construct(mem_input110,
			ctx, inst))
		return &mem_input110->base;

	BREAK_TO_DEBUGGER();
	dc_service_free(ctx, mem_input110);
	return NULL;
}
