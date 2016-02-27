/*
 * Copyright 2012-16 Advanced Micro Devices, Inc.
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
#include "inc/bandwidth_calcs.h"

#include "dce110_mem_input.h"

#define DCP_REG(reg) (reg + mem_input110->offsets.dcp)
/*#define DMIF_REG(reg) (reg + mem_input110->offsets.dmif)*/
/*#define PIPE_REG(reg) (reg + mem_input110->offsets.pipe)*/

static const struct dce110_mem_input_reg_offsets dce110_mi_v_reg_offsets[] = {
	{
		.dcp = 0,
		.dmif = 0,
		.pipe = 0,
	}
};

static void set_flip_control(
	struct dce110_mem_input *mem_input110,
	bool immediate)
{
	uint32_t value = 0;

	value = dm_read_reg(
			mem_input110->base.ctx,
			DCP_REG(mmUNP_FLIP_CONTROL));

	set_reg_field_value(value, 1,
			UNP_FLIP_CONTROL,
			GRPH_SURFACE_UPDATE_PENDING_MODE);

	dm_write_reg(
			mem_input110->base.ctx,
			DCP_REG(mmUNP_FLIP_CONTROL),
			value);
}

/* chroma part */
static void program_pri_addr_c(
	struct dce110_mem_input *mem_input110,
	PHYSICAL_ADDRESS_LOC address)
{
	uint32_t value = 0;
	uint32_t temp = 0;
	/*high register MUST be programmed first*/
	temp = address.high_part &
UNP_GRPH_PRIMARY_SURFACE_ADDRESS_HIGH_C__GRPH_PRIMARY_SURFACE_ADDRESS_HIGH_C_MASK;

	set_reg_field_value(value, temp,
		UNP_GRPH_PRIMARY_SURFACE_ADDRESS_HIGH_C,
		GRPH_PRIMARY_SURFACE_ADDRESS_HIGH_C);

	dm_write_reg(
		mem_input110->base.ctx,
		DCP_REG(mmUNP_GRPH_PRIMARY_SURFACE_ADDRESS_HIGH_C),
		value);

	temp = 0;
	value = 0;
	temp = address.low_part >>
	UNP_GRPH_PRIMARY_SURFACE_ADDRESS_C__GRPH_PRIMARY_SURFACE_ADDRESS_C__SHIFT;

	set_reg_field_value(value, temp,
		UNP_GRPH_PRIMARY_SURFACE_ADDRESS_C,
		GRPH_PRIMARY_SURFACE_ADDRESS_C);

	dm_write_reg(
		mem_input110->base.ctx,
		DCP_REG(mmUNP_GRPH_PRIMARY_SURFACE_ADDRESS_C),
		value);
}

/* luma part */
static void program_pri_addr_l(
	struct dce110_mem_input *mem_input110,
	PHYSICAL_ADDRESS_LOC address)
{
	uint32_t value = 0;
	uint32_t temp = 0;

	/*high register MUST be programmed first*/
	temp = address.high_part &
UNP_GRPH_PRIMARY_SURFACE_ADDRESS_HIGH_L__GRPH_PRIMARY_SURFACE_ADDRESS_HIGH_L_MASK;

	set_reg_field_value(value, temp,
		UNP_GRPH_PRIMARY_SURFACE_ADDRESS_HIGH_L,
		GRPH_PRIMARY_SURFACE_ADDRESS_HIGH_L);

	dm_write_reg(
		mem_input110->base.ctx,
		DCP_REG(mmUNP_GRPH_PRIMARY_SURFACE_ADDRESS_HIGH_L),
		value);

	temp = 0;
	value = 0;
	temp = address.low_part >>
	UNP_GRPH_PRIMARY_SURFACE_ADDRESS_L__GRPH_PRIMARY_SURFACE_ADDRESS_L__SHIFT;

	set_reg_field_value(value, temp,
		UNP_GRPH_PRIMARY_SURFACE_ADDRESS_L,
		GRPH_PRIMARY_SURFACE_ADDRESS_L);

	dm_write_reg(
		mem_input110->base.ctx,
		DCP_REG(mmUNP_GRPH_PRIMARY_SURFACE_ADDRESS_L),
		value);
}

static void program_addr(
	struct dce110_mem_input *mem_input110,
	const struct dc_plane_address *addr)
{
	switch (addr->type) {
	case PLN_ADDR_TYPE_GRAPHICS:
		program_pri_addr_l(
			mem_input110,
			addr->grph.addr);
		break;
	case PLN_ADDR_TYPE_VIDEO_PROGRESSIVE:
		program_pri_addr_l(
			mem_input110,
			addr->video_progressive.luma_addr);
		program_pri_addr_c(
			mem_input110,
			addr->video_progressive.chroma_addr);
		break;
	default:
		/* not supported */
		BREAK_TO_DEBUGGER();
	}
}

static void enable(struct dce110_mem_input *mem_input110)
{
	uint32_t value = 0;

	value = dm_read_reg(mem_input110->base.ctx, DCP_REG(mmUNP_GRPH_ENABLE));
	set_reg_field_value(value, 1, UNP_GRPH_ENABLE, GRPH_ENABLE);
	dm_write_reg(mem_input110->base.ctx,
		DCP_REG(mmUNP_GRPH_ENABLE),
		value);
}

static void program_tiling(
	struct dce110_mem_input *mem_input110,
	const struct dc_tiling_info *info,
	const enum surface_pixel_format pixel_format)
{
	uint32_t value = 0;

	set_reg_field_value(value, info->num_banks,
		UNP_GRPH_CONTROL, GRPH_NUM_BANKS);

	set_reg_field_value(value, info->bank_width,
		UNP_GRPH_CONTROL, GRPH_BANK_WIDTH_L);

	set_reg_field_value(value, info->bank_height,
		UNP_GRPH_CONTROL, GRPH_BANK_HEIGHT_L);

	set_reg_field_value(value, info->tile_aspect,
		UNP_GRPH_CONTROL, GRPH_MACRO_TILE_ASPECT_L);

	set_reg_field_value(value, info->tile_split,
		UNP_GRPH_CONTROL, GRPH_TILE_SPLIT_L);

	set_reg_field_value(value, info->tile_mode,
		UNP_GRPH_CONTROL, GRPH_MICRO_TILE_MODE_L);

	set_reg_field_value(value, info->pipe_config,
		UNP_GRPH_CONTROL, GRPH_PIPE_CONFIG);

	set_reg_field_value(value, info->array_mode,
		UNP_GRPH_CONTROL, GRPH_ARRAY_MODE);

	set_reg_field_value(value, 1,
		UNP_GRPH_CONTROL, GRPH_COLOR_EXPANSION_MODE);

	set_reg_field_value(value, 0,
		UNP_GRPH_CONTROL, GRPH_Z);

	dm_write_reg(
		mem_input110->base.ctx,
		mmUNP_GRPH_CONTROL,
		value);

	value = 0;

	set_reg_field_value(value, info->bank_width_c,
		UNP_GRPH_CONTROL_C, GRPH_BANK_WIDTH_C);

	set_reg_field_value(value, info->bank_height_c,
		UNP_GRPH_CONTROL_C, GRPH_BANK_HEIGHT_C);

	set_reg_field_value(value, info->tile_aspect_c,
		UNP_GRPH_CONTROL_C, GRPH_MACRO_TILE_ASPECT_C);

	set_reg_field_value(value, info->tile_split_c,
		UNP_GRPH_CONTROL_C, GRPH_TILE_SPLIT_C);

	set_reg_field_value(value, info->tile_mode_c,
		UNP_GRPH_CONTROL_C, GRPH_MICRO_TILE_MODE_C);

	dm_write_reg(
		mem_input110->base.ctx,
		mmUNP_GRPH_CONTROL_C,
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
		swap = local_size.video.luma_size.x;
		local_size.video.luma_size.x =
			local_size.video.luma_size.y;
		local_size.video.luma_size.y  = swap;

		swap = local_size.video.luma_size.width;
		local_size.video.luma_size.width =
			local_size.video.luma_size.height;
		local_size.video.luma_size.height = swap;

		swap = local_size.video.chroma_size.x;
		local_size.video.chroma_size.x =
			local_size.video.chroma_size.y;
		local_size.video.chroma_size.y  = swap;

		swap = local_size.video.chroma_size.width;
		local_size.video.chroma_size.width =
			local_size.video.chroma_size.height;
		local_size.video.chroma_size.height = swap;
	}

	value = 0;
	set_reg_field_value(value, local_size.video.luma_pitch,
			UNP_GRPH_PITCH_L, GRPH_PITCH_L);

	dm_write_reg(
		mem_input110->base.ctx,
		DCP_REG(mmUNP_GRPH_PITCH_L),
		value);

	value = 0;
	set_reg_field_value(value, local_size.video.chroma_pitch,
			UNP_GRPH_PITCH_C, GRPH_PITCH_C);
	dm_write_reg(
		mem_input110->base.ctx,
		DCP_REG(mmUNP_GRPH_PITCH_C),
		value);

	value = 0;
	set_reg_field_value(value, 0,
			UNP_GRPH_X_START_L, GRPH_X_START_L);
	dm_write_reg(
		mem_input110->base.ctx,
		DCP_REG(mmUNP_GRPH_X_START_L),
		value);

	value = 0;
	set_reg_field_value(value, 0,
			UNP_GRPH_X_START_C, GRPH_X_START_C);
	dm_write_reg(
		mem_input110->base.ctx,
		DCP_REG(mmUNP_GRPH_X_START_C),
		value);

	value = 0;
	set_reg_field_value(value, 0,
			UNP_GRPH_Y_START_L, GRPH_Y_START_L);
	dm_write_reg(
		mem_input110->base.ctx,
		DCP_REG(mmUNP_GRPH_Y_START_L),
		value);

	value = 0;
	set_reg_field_value(value, 0,
			UNP_GRPH_Y_START_C, GRPH_Y_START_C);
	dm_write_reg(
		mem_input110->base.ctx,
		DCP_REG(mmUNP_GRPH_Y_START_C),
		value);

	value = 0;
	set_reg_field_value(value, local_size.video.luma_size.x +
			local_size.video.luma_size.width,
			UNP_GRPH_X_END_L, GRPH_X_END_L);
	dm_write_reg(
		mem_input110->base.ctx,
		DCP_REG(mmUNP_GRPH_X_END_L),
		value);

	value = 0;
	set_reg_field_value(value, local_size.video.chroma_size.x +
			local_size.video.chroma_size.width,
			UNP_GRPH_X_END_C, GRPH_X_END_C);
	dm_write_reg(
		mem_input110->base.ctx,
		DCP_REG(mmUNP_GRPH_X_END_C),
		value);

	value = 0;
	set_reg_field_value(value, local_size.video.luma_size.y +
			local_size.video.luma_size.height,
			UNP_GRPH_Y_END_L, GRPH_Y_END_L);
	dm_write_reg(
		mem_input110->base.ctx,
		DCP_REG(mmUNP_GRPH_Y_END_L),
		value);

	value = 0;
	set_reg_field_value(value, local_size.video.chroma_size.y +
			local_size.video.chroma_size.height,
			UNP_GRPH_Y_END_C, GRPH_Y_END_C);
	dm_write_reg(
		mem_input110->base.ctx,
		DCP_REG(mmUNP_GRPH_Y_END_C),
		value);

	value = 0;
	switch (rotation) {
	case ROTATION_ANGLE_90:
		set_reg_field_value(value, 3,
			UNP_HW_ROTATION, ROTATION_ANGLE);
		break;
	case ROTATION_ANGLE_180:
		set_reg_field_value(value, 2,
			UNP_HW_ROTATION, ROTATION_ANGLE);
		break;
	case ROTATION_ANGLE_270:
		set_reg_field_value(value, 1,
			UNP_HW_ROTATION, ROTATION_ANGLE);
		break;
	default:
		set_reg_field_value(value, 0,
			UNP_HW_ROTATION, ROTATION_ANGLE);
		break;
	}

	dm_write_reg(
		mem_input110->base.ctx,
		DCP_REG(mmUNP_HW_ROTATION),
		value);
}

static void program_pixel_format(
	struct dce110_mem_input *mem_input110,
	enum surface_pixel_format format)
{
	if (format < SURFACE_PIXEL_FORMAT_VIDEO_BEGIN) {
		uint32_t value;
		uint8_t grph_depth;
		uint8_t grph_format;

		value =	dm_read_reg(
				mem_input110->base.ctx,
				DCP_REG(mmUNP_GRPH_CONTROL));

		switch (format) {
		case SURFACE_PIXEL_FORMAT_GRPH_PALETA_256_COLORS:
			grph_depth = 0;
			grph_format = 0;
			break;
		case SURFACE_PIXEL_FORMAT_GRPH_RGB565:
			grph_depth = 1;
			grph_format = 1;
			break;
		case SURFACE_PIXEL_FORMAT_GRPH_ARGB8888:
		case SURFACE_PIXEL_FORMAT_GRPH_BGRA8888:
			grph_depth = 2;
			grph_format = 0;
			break;
		case SURFACE_PIXEL_FORMAT_GRPH_ARGB2101010:
		case SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010:
		case SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010_XR_BIAS:
			grph_depth = 2;
			grph_format = 1;
			break;
		case SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616:
		case SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616F:
			grph_depth = 3;
			grph_format = 0;
			break;
		default:
			grph_depth = 2;
			grph_format = 0;
			break;
		}

		set_reg_field_value(
				value,
				grph_depth,
				UNP_GRPH_CONTROL,
				GRPH_DEPTH);
		set_reg_field_value(
				value,
				grph_format,
				UNP_GRPH_CONTROL,
				GRPH_FORMAT);

		dm_write_reg(
				mem_input110->base.ctx,
				DCP_REG(mmUNP_GRPH_CONTROL),
				value);

		value =	dm_read_reg(
				mem_input110->base.ctx,
				DCP_REG(mmUNP_GRPH_CONTROL_EXP));

		/* VIDEO FORMAT 0 */
		set_reg_field_value(
				value,
				0,
				UNP_GRPH_CONTROL_EXP,
				VIDEO_FORMAT);
		dm_write_reg(
				mem_input110->base.ctx,
				DCP_REG(mmUNP_GRPH_CONTROL_EXP),
				value);

	} else {
		/* Video 422 and 420 needs UNP_GRPH_CONTROL_EXP programmed */
		uint32_t value;
		uint8_t video_format;

		value =	dm_read_reg(
				mem_input110->base.ctx,
				DCP_REG(mmUNP_GRPH_CONTROL_EXP));

		switch (format) {
		case SURFACE_PIXEL_FORMAT_VIDEO_420_YCbCr:
			video_format = 2;
			break;
		case SURFACE_PIXEL_FORMAT_VIDEO_420_YCrCb:
			video_format = 3;
			break;
		default:
			video_format = 0;
			break;
		}

		set_reg_field_value(
			value,
			video_format,
			UNP_GRPH_CONTROL_EXP,
			VIDEO_FORMAT);

		dm_write_reg(
			mem_input110->base.ctx,
			DCP_REG(mmUNP_GRPH_CONTROL_EXP),
			value);
	}
}

void dce110_mem_input_v_wait_for_no_surface_update_pending(
				struct mem_input *mem_input)
{
	struct dce110_mem_input *mem_input110 = TO_DCE110_MEM_INPUT(mem_input);
	uint32_t value;

	do  {
		value = dm_read_reg(mem_input110->base.ctx, DCP_REG(mmUNP_GRPH_UPDATE));
	} while (get_reg_field_value(value, UNP_GRPH_UPDATE, GRPH_SURFACE_UPDATE_PENDING));
}

bool dce110_mem_input_v_program_surface_flip_and_addr(
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

bool dce110_mem_input_v_program_surface_config(
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
	const uint32_t urgency_addr,
	const uint32_t wm_addr,
	struct bw_watermarks marks_low,
	uint32_t total_dest_line_time_ns)
{
	/* register value */
	uint32_t urgency_cntl = 0;
	uint32_t wm_mask_cntl = 0;

	/*Write mask to enable reading/writing of watermark set A*/
	wm_mask_cntl = dm_read_reg(ctx, wm_addr);
	set_reg_field_value(wm_mask_cntl,
			1,
			DPGV0_WATERMARK_MASK_CONTROL,
			URGENCY_WATERMARK_MASK);
	dm_write_reg(ctx, wm_addr, wm_mask_cntl);

	urgency_cntl = dm_read_reg(ctx, urgency_addr);

	set_reg_field_value(
		urgency_cntl,
		marks_low.a_mark,
		DPGV0_PIPE_URGENCY_CONTROL,
		URGENCY_LOW_WATERMARK);

	set_reg_field_value(
		urgency_cntl,
		total_dest_line_time_ns,
		DPGV0_PIPE_URGENCY_CONTROL,
		URGENCY_HIGH_WATERMARK);
	dm_write_reg(ctx, urgency_addr, urgency_cntl);

	/*Write mask to enable reading/writing of watermark set B*/
	wm_mask_cntl = dm_read_reg(ctx, wm_addr);
	set_reg_field_value(wm_mask_cntl,
			2,
			DPGV0_WATERMARK_MASK_CONTROL,
			URGENCY_WATERMARK_MASK);
	dm_write_reg(ctx, wm_addr, wm_mask_cntl);

	urgency_cntl = dm_read_reg(ctx, urgency_addr);

	set_reg_field_value(urgency_cntl,
		marks_low.b_mark,
		DPGV0_PIPE_URGENCY_CONTROL,
		URGENCY_LOW_WATERMARK);

	set_reg_field_value(urgency_cntl,
		total_dest_line_time_ns,
		DPGV0_PIPE_URGENCY_CONTROL,
		URGENCY_HIGH_WATERMARK);

	dm_write_reg(ctx, urgency_addr, urgency_cntl);
}

static void program_urgency_watermark_l(
	const struct dc_context *ctx,
	struct bw_watermarks marks_low,
	uint32_t total_dest_line_time_ns)
{
	program_urgency_watermark(
		ctx,
		mmDPGV0_PIPE_URGENCY_CONTROL,
		mmDPGV0_WATERMARK_MASK_CONTROL,
		marks_low,
		total_dest_line_time_ns);
}

static void program_urgency_watermark_c(
	const struct dc_context *ctx,
	struct bw_watermarks marks_low,
	uint32_t total_dest_line_time_ns)
{
	program_urgency_watermark(
		ctx,
		mmDPGV1_PIPE_URGENCY_CONTROL,
		mmDPGV1_WATERMARK_MASK_CONTROL,
		marks_low,
		total_dest_line_time_ns);
}

static void program_stutter_watermark(
	const struct dc_context *ctx,
	const uint32_t stutter_addr,
	const uint32_t wm_addr,
	struct bw_watermarks marks)
{
	/* register value */
	uint32_t stutter_cntl = 0;
	uint32_t wm_mask_cntl = 0;

	/*Write mask to enable reading/writing of watermark set A*/

	wm_mask_cntl = dm_read_reg(ctx, wm_addr);
	set_reg_field_value(wm_mask_cntl,
		1,
		DPGV0_WATERMARK_MASK_CONTROL,
		STUTTER_EXIT_SELF_REFRESH_WATERMARK_MASK);
	dm_write_reg(ctx, wm_addr, wm_mask_cntl);

	stutter_cntl = dm_read_reg(ctx, stutter_addr);

	set_reg_field_value(stutter_cntl,
		1,
		DPGV0_PIPE_STUTTER_CONTROL,
		STUTTER_ENABLE);
	set_reg_field_value(stutter_cntl,
		1,
		DPGV0_PIPE_STUTTER_CONTROL,
		STUTTER_IGNORE_FBC);

	/*Write watermark set A*/
	set_reg_field_value(stutter_cntl,
		marks.a_mark,
		DPGV0_PIPE_STUTTER_CONTROL,
		STUTTER_EXIT_SELF_REFRESH_WATERMARK);
	dm_write_reg(ctx, stutter_addr, stutter_cntl);

	/*Write mask to enable reading/writing of watermark set B*/
	wm_mask_cntl = dm_read_reg(ctx, wm_addr);
	set_reg_field_value(wm_mask_cntl,
		2,
		DPGV0_WATERMARK_MASK_CONTROL,
		STUTTER_EXIT_SELF_REFRESH_WATERMARK_MASK);
	dm_write_reg(ctx, wm_addr, wm_mask_cntl);

	stutter_cntl = dm_read_reg(ctx, stutter_addr);
	set_reg_field_value(stutter_cntl,
		1,
		DPGV0_PIPE_STUTTER_CONTROL,
		STUTTER_ENABLE);
	set_reg_field_value(stutter_cntl,
		1,
		DPGV0_PIPE_STUTTER_CONTROL,
		STUTTER_IGNORE_FBC);

	/*Write watermark set B*/
	set_reg_field_value(stutter_cntl,
		marks.b_mark,
		DPGV0_PIPE_STUTTER_CONTROL,
		STUTTER_EXIT_SELF_REFRESH_WATERMARK);
	dm_write_reg(ctx, stutter_addr, stutter_cntl);
}

static void program_stutter_watermark_l(
	const struct dc_context *ctx,
	struct bw_watermarks marks)
{
	program_stutter_watermark(ctx,
			mmDPGV0_PIPE_STUTTER_CONTROL,
			mmDPGV0_WATERMARK_MASK_CONTROL,
			marks);
}

static void program_stutter_watermark_c(
	const struct dc_context *ctx,
	struct bw_watermarks marks)
{
	program_stutter_watermark(ctx,
			mmDPGV1_PIPE_STUTTER_CONTROL,
			mmDPGV1_WATERMARK_MASK_CONTROL,
			marks);
}

static void program_nbp_watermark(
	const struct dc_context *ctx,
	const uint32_t wm_mask_ctrl_addr,
	const uint32_t nbp_pstate_ctrl_addr,
	struct bw_watermarks marks)
{
	uint32_t value;

	/* Write mask to enable reading/writing of watermark set A */

	value = dm_read_reg(ctx, wm_mask_ctrl_addr);

	set_reg_field_value(
		value,
		1,
		DPGV0_WATERMARK_MASK_CONTROL,
		NB_PSTATE_CHANGE_WATERMARK_MASK);
	dm_write_reg(ctx, wm_mask_ctrl_addr, value);

	value = dm_read_reg(ctx, nbp_pstate_ctrl_addr);

	set_reg_field_value(
		value,
		1,
		DPGV0_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_ENABLE);
	set_reg_field_value(
		value,
		1,
		DPGV0_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_URGENT_DURING_REQUEST);
	set_reg_field_value(
		value,
		1,
		DPGV0_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_NOT_SELF_REFRESH_DURING_REQUEST);
	dm_write_reg(ctx, nbp_pstate_ctrl_addr, value);

	/* Write watermark set A */
	value = dm_read_reg(ctx, nbp_pstate_ctrl_addr);
	set_reg_field_value(
		value,
		marks.a_mark,
		DPGV0_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_WATERMARK);
	dm_write_reg(ctx, nbp_pstate_ctrl_addr, value);

	/* Write mask to enable reading/writing of watermark set B */
	value = dm_read_reg(ctx, wm_mask_ctrl_addr);
	set_reg_field_value(
		value,
		2,
		DPGV0_WATERMARK_MASK_CONTROL,
		NB_PSTATE_CHANGE_WATERMARK_MASK);
	dm_write_reg(ctx, wm_mask_ctrl_addr, value);

	value = dm_read_reg(ctx, nbp_pstate_ctrl_addr);
	set_reg_field_value(
		value,
		1,
		DPGV0_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_ENABLE);
	set_reg_field_value(
		value,
		1,
		DPGV0_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_URGENT_DURING_REQUEST);
	set_reg_field_value(
		value,
		1,
		DPGV0_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_NOT_SELF_REFRESH_DURING_REQUEST);
	dm_write_reg(ctx, nbp_pstate_ctrl_addr, value);

	/* Write watermark set B */
	value = dm_read_reg(ctx, nbp_pstate_ctrl_addr);
	set_reg_field_value(
		value,
		marks.b_mark,
		DPGV0_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_WATERMARK);
	dm_write_reg(ctx, nbp_pstate_ctrl_addr, value);
}

static void program_nbp_watermark_l(
	const struct dc_context *ctx,
	struct bw_watermarks marks)
{
	program_nbp_watermark(ctx,
			mmDPGV0_WATERMARK_MASK_CONTROL,
			mmDPGV0_PIPE_NB_PSTATE_CHANGE_CONTROL,
			marks);
}

static void program_nbp_watermark_c(
	const struct dc_context *ctx,
	struct bw_watermarks marks)
{
	program_nbp_watermark(ctx,
			mmDPGV1_WATERMARK_MASK_CONTROL,
			mmDPGV1_PIPE_NB_PSTATE_CHANGE_CONTROL,
			marks);
}

void dce110_mem_input_v_program_display_marks(
	struct mem_input *mem_input,
	struct bw_watermarks nbp,
	struct bw_watermarks stutter,
	struct bw_watermarks urgent,
	uint32_t total_dest_line_time_ns)
{
	program_urgency_watermark_l(
		mem_input->ctx,
		urgent,
		total_dest_line_time_ns);

	program_urgency_watermark_c(
		mem_input->ctx,
		urgent,
		total_dest_line_time_ns);

	program_nbp_watermark_l(
		mem_input->ctx,
		nbp);

	program_nbp_watermark_c(
		mem_input->ctx,
		nbp);

	program_stutter_watermark_l(
		mem_input->ctx,
		stutter);

	program_stutter_watermark_c(
		mem_input->ctx,
		stutter);
}

void dce110_allocate_mem_input_v(
	struct mem_input *mi,
	uint32_t h_total,/* for current stream */
	uint32_t v_total,/* for current stream */
	uint32_t pix_clk_khz,/* for current stream */
	uint32_t total_stream_num)
{
}

void dce110_free_mem_input_v(
	struct mem_input *mi,
	uint32_t total_stream_num)
{
}

static struct mem_input_funcs dce110_mem_input_v_funcs = {
	.mem_input_program_display_marks =
			dce110_mem_input_v_program_display_marks,
	.allocate_mem_input = dce110_allocate_mem_input_v,
	.free_mem_input = dce110_free_mem_input_v,
	.mem_input_program_surface_flip_and_addr =
			dce110_mem_input_v_program_surface_flip_and_addr,
	.mem_input_program_surface_config =
			dce110_mem_input_v_program_surface_config,
	.wait_for_no_surface_update_pending =
			dce110_mem_input_v_wait_for_no_surface_update_pending
};
/*****************************************/
/* Constructor, Destructor               */
/*****************************************/

bool dce110_mem_input_v_construct(
	struct dce110_mem_input *mem_input110,
	struct dc_context *ctx)
{
	mem_input110->base.funcs = &dce110_mem_input_v_funcs;
	mem_input110->base.ctx = ctx;

	mem_input110->base.inst = 0;

	mem_input110->offsets = dce110_mi_v_reg_offsets[0];

	mem_input110->supported_stutter_mode = 0;
	dal_adapter_service_get_feature_value(FEATURE_STUTTER_MODE,
			&(mem_input110->supported_stutter_mode),
			sizeof(mem_input110->supported_stutter_mode));

	return true;
}

#if 0
void dce110_mem_input_v_destroy(struct mem_input **mem_input)
{
	dm_free(TO_DCE110_MEM_INPUT(*mem_input));
	*mem_input = NULL;
}
#endif
