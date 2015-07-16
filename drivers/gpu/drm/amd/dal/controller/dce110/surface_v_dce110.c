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
#include "include/logger_interface.h"

#include "dce/dce_11_0_d.h"
#include "dce/dce_11_0_sh_mask.h"

#include "surface_v_dce110.h"

enum sf_regs_idx {
	IDX_GRPH_CONTROL,
	IDX_GRPH_CONTROL_C,
	IDX_GRPH_CONTROL_EXP,
	IDX_PRESCALE_GRPH_CONTROL,

	IDX_GRPH_X_START_L,
	IDX_GRPH_X_START_C,
	IDX_GRPH_Y_START_L,
	IDX_GRPH_Y_START_C,
	IDX_GRPH_X_END_L,
	IDX_GRPH_X_END_C,
	IDX_GRPH_Y_END_L,
	IDX_GRPH_Y_END_C,
	IDX_GRPH_PITCH_L,
	IDX_GRPH_PITCH_C,

	IDX_HW_ROTATION,
	IDX_LB_DESKTOP_HEIGHT,

	IDX_GRPH_PRIMARY_SURFACE_ADDRESS_HIGH_L,
	IDX_GRPH_PRIMARY_SURFACE_ADDRESS_L,
	IDX_GRPH_PRIMARY_SURFACE_ADDRESS_HIGH_C,
	IDX_GRPH_PRIMARY_SURFACE_ADDRESS_C,
	IDX_GRPH_SECONDARY_SURFACE_ADDRESS_HIGH,
	IDX_GRPH_SECONDARY_SURFACE_ADDRESS,

	IDX_GRPH_UPDATE,
	IDX_GRPH_ENABLE,

	SF_REGS_IDX_SIZE
};

#define regs_for_surface()\
[0] = {\
	[IDX_GRPH_CONTROL] = mmUNP_GRPH_CONTROL,\
	[IDX_GRPH_CONTROL_C] = mmUNP_GRPH_CONTROL_C,\
	[IDX_GRPH_CONTROL_EXP] = mmUNP_GRPH_CONTROL_EXP,\
	[IDX_GRPH_X_START_L] = mmUNP_GRPH_X_START_L,\
	[IDX_GRPH_X_START_C] = mmUNP_GRPH_X_START_C,\
	[IDX_GRPH_Y_START_L] = mmUNP_GRPH_Y_START_L,\
	[IDX_GRPH_Y_START_C] = mmUNP_GRPH_Y_START_C,\
	[IDX_GRPH_X_END_L] = mmUNP_GRPH_X_END_L,\
	[IDX_GRPH_X_END_C] = mmUNP_GRPH_X_END_C,\
	[IDX_GRPH_Y_END_L] = mmUNP_GRPH_Y_END_L,\
	[IDX_GRPH_Y_END_C] = mmUNP_GRPH_Y_END_C,\
	[IDX_GRPH_PITCH_L] = mmUNP_GRPH_PITCH_L,\
	[IDX_GRPH_PITCH_C] = mmUNP_GRPH_PITCH_C,\
	[IDX_HW_ROTATION] = mmUNP_HW_ROTATION,\
	[IDX_LB_DESKTOP_HEIGHT] = mmLBV_DESKTOP_HEIGHT,\
	[IDX_GRPH_PRIMARY_SURFACE_ADDRESS_HIGH_L] =\
	mmUNP_GRPH_PRIMARY_SURFACE_ADDRESS_HIGH_L,\
	[IDX_GRPH_PRIMARY_SURFACE_ADDRESS_L] =\
	mmUNP_GRPH_PRIMARY_SURFACE_ADDRESS_L,\
	[IDX_GRPH_PRIMARY_SURFACE_ADDRESS_HIGH_C] =\
	mmUNP_GRPH_PRIMARY_SURFACE_ADDRESS_HIGH_C,\
	[IDX_GRPH_PRIMARY_SURFACE_ADDRESS_C] =\
	mmUNP_GRPH_PRIMARY_SURFACE_ADDRESS_C,\
	[IDX_GRPH_SECONDARY_SURFACE_ADDRESS_HIGH] =\
	mmUNP_GRPH_SECONDARY_SURFACE_ADDRESS_HIGH_L,\
	[IDX_GRPH_SECONDARY_SURFACE_ADDRESS] =\
	mmUNP_GRPH_SECONDARY_SURFACE_ADDRESS_L,\
	[IDX_GRPH_UPDATE] = mmUNP_GRPH_UPDATE,\
	[IDX_GRPH_ENABLE] = mmUNP_GRPH_ENABLE,\
}

static const uint32_t sf_regs[][SF_REGS_IDX_SIZE] = {
	regs_for_surface(),
};

/*
#define FROM_SURFACE(ptr) \
	container_of((ptr), struct surface_dce110, base)
*/

static void program_pixel_format(
		struct surface *sf,
		enum surface_pixel_format format)
{
	if (format >= SURFACE_PIXEL_FORMAT_VIDEO_444_BEGIN ||
			format < SURFACE_PIXEL_FORMAT_VIDEO_BEGIN) {
		uint32_t value;
		uint32_t addr = sf->regs[IDX_GRPH_CONTROL];
		uint8_t grph_depth;
		uint8_t grph_format;

		value = dal_read_reg(sf->ctx, addr);

		grph_depth = get_reg_field_value(
			value,
			UNP_GRPH_CONTROL,
			GRPH_DEPTH);
		grph_format = get_reg_field_value(
			value,
			UNP_GRPH_CONTROL,
			GRPH_FORMAT);

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

		dal_write_reg(
				sf->ctx,
				sf->regs[IDX_GRPH_CONTROL],
				value);

		addr = sf->regs[IDX_GRPH_CONTROL_EXP];
		value = dal_read_reg(sf->ctx, addr);
		/* VIDEO FORMAT 0 */
		set_reg_field_value(
				value,
				0,
				UNP_GRPH_CONTROL_EXP,
				VIDEO_FORMAT);
		dal_write_reg(sf->ctx, addr, value);
	} else {
		/* Video 422 and 420 needs UNP_GRPH_CONTROL_EXP programmed */
		uint32_t value;
		uint32_t addr = sf->regs[IDX_GRPH_CONTROL_EXP];
		uint8_t video_format;

		value = dal_read_reg(sf->ctx, addr);

		switch (format) {
		case SURFACE_PIXEL_FORMAT_VIDEO_420_YCbCr:
			video_format = 2;
			break;
		case SURFACE_PIXEL_FORMAT_VIDEO_420_YCrCb:
			video_format = 3;
			break;
		case SURFACE_PIXEL_FORMAT_VIDEO_422_YCb:
			video_format = 4;
			break;
		case SURFACE_PIXEL_FORMAT_VIDEO_422_YCr:
			video_format = 5;
			break;
		case SURFACE_PIXEL_FORMAT_VIDEO_422_CbY:
			video_format = 6;
			break;
		case SURFACE_PIXEL_FORMAT_VIDEO_422_CrY:
			video_format = 7;
			break;
		default:
			break;
		}

		set_reg_field_value(
			value,
			video_format,
			UNP_GRPH_CONTROL_EXP,
			VIDEO_FORMAT);

		dal_write_reg(sf->ctx, addr, value);
	}
}


static void program_size_and_rotation(
	struct surface *sf,
	enum plane_rotation_angle rotation,
	const union plane_size *plane_size)
{
	uint32_t value = 0;
	union plane_size local_size = *plane_size;

	if (rotation == PLANE_ROTATION_ANGLE_90 ||
		rotation == PLANE_ROTATION_ANGLE_270) {

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
	dal_write_reg(
		sf->ctx,
		sf->regs[IDX_GRPH_PITCH_L],
		value);

	value = 0;
	set_reg_field_value(value, local_size.video.chroma_pitch,
			UNP_GRPH_PITCH_C, GRPH_PITCH_C);
	dal_write_reg(
		sf->ctx,
		sf->regs[IDX_GRPH_PITCH_C],
		value);

	value = 0;
	set_reg_field_value(value, 0,
			UNP_GRPH_X_START_L, GRPH_X_START_L);
	dal_write_reg(
		sf->ctx,
		sf->regs[IDX_GRPH_X_START_L],
		value);

	value = 0;
	set_reg_field_value(value, 0,
			UNP_GRPH_X_START_C, GRPH_X_START_C);
	dal_write_reg(
		sf->ctx,
		sf->regs[IDX_GRPH_X_START_C],
		value);

	value = 0;
	set_reg_field_value(value, 0,
			UNP_GRPH_Y_START_L, GRPH_Y_START_L);
	dal_write_reg(
		sf->ctx,
		sf->regs[IDX_GRPH_Y_START_L],
		value);

	value = 0;
	set_reg_field_value(value, 0,
			UNP_GRPH_Y_START_C, GRPH_Y_START_C);
	dal_write_reg(
		sf->ctx,
		sf->regs[IDX_GRPH_Y_START_C],
		value);

	value = 0;
	set_reg_field_value(value, local_size.video.luma_size.x +
			local_size.video.luma_size.width,
			UNP_GRPH_X_END_L, GRPH_X_END_L);
	dal_write_reg(
		sf->ctx,
		sf->regs[IDX_GRPH_X_END_L],
		value);

	value = 0;
	set_reg_field_value(value, local_size.video.chroma_size.x +
			local_size.video.chroma_size.width,
			UNP_GRPH_X_END_C, GRPH_X_END_C);
	dal_write_reg(
		sf->ctx,
		sf->regs[IDX_GRPH_X_END_C],
		value);

	value = 0;
	set_reg_field_value(value, local_size.video.luma_size.y +
			local_size.video.luma_size.height,
			UNP_GRPH_Y_END_L, GRPH_Y_END_L);
	dal_write_reg(
		sf->ctx,
		sf->regs[IDX_GRPH_Y_END_L],
		value);

	value = 0;
	set_reg_field_value(value, local_size.video.chroma_size.y +
			local_size.video.chroma_size.height,
			UNP_GRPH_Y_END_C, GRPH_Y_END_C);
	dal_write_reg(
		sf->ctx,
		sf->regs[IDX_GRPH_Y_END_C],
		value);

	value = 0;
	switch (rotation) {
	case PLANE_ROTATION_ANGLE_90:
		set_reg_field_value(value, 3,
			UNP_HW_ROTATION, ROTATION_ANGLE);
		break;
	case PLANE_ROTATION_ANGLE_180:
		set_reg_field_value(value, 2,
			UNP_HW_ROTATION, ROTATION_ANGLE);
		break;
	case PLANE_ROTATION_ANGLE_270:
		set_reg_field_value(value, 1,
			UNP_HW_ROTATION, ROTATION_ANGLE);
		break;
	default:
		set_reg_field_value(value, 0,
			UNP_HW_ROTATION, ROTATION_ANGLE);
		break;
	}
	dal_write_reg(
		sf->ctx,
		sf->regs[IDX_HW_ROTATION],
		value);
}

static void program_tiling(
	struct surface *sf,
	const union plane_tiling_info *info,
	const enum surface_pixel_format pixel_format)
{
	uint32_t value = 0;

	value = dal_read_reg(
			sf->ctx,
			sf->regs[IDX_GRPH_CONTROL]);

	if (pixel_format < SURFACE_PIXEL_FORMAT_VIDEO_BEGIN) {

		set_reg_field_value(value, info->grph.NUM_BANKS,
				UNP_GRPH_CONTROL, GRPH_NUM_BANKS);

		set_reg_field_value(value, info->grph.BANK_WIDTH,
				UNP_GRPH_CONTROL, GRPH_BANK_WIDTH_L);

		set_reg_field_value(value, info->grph.BANK_HEIGHT,
				UNP_GRPH_CONTROL, GRPH_BANK_HEIGHT_L);

		set_reg_field_value(value, info->grph.TILE_ASPECT,
				UNP_GRPH_CONTROL, GRPH_MACRO_TILE_ASPECT_L);

		set_reg_field_value(value, info->grph.TILE_MODE,
				UNP_GRPH_CONTROL, GRPH_MICRO_TILE_MODE_L);

		set_reg_field_value(value, info->grph.TILE_SPLIT,
				UNP_GRPH_CONTROL, GRPH_TILE_SPLIT_L);

		set_reg_field_value(value, info->grph.PIPE_CONFIG,
				UNP_GRPH_CONTROL, GRPH_PIPE_CONFIG);

		set_reg_field_value(value, info->grph.ARRAY_MODE,
				UNP_GRPH_CONTROL, GRPH_ARRAY_MODE);
	} else {
		uint32_t value_chroma = 0;

		set_reg_field_value(value, info->video.NUM_BANKS,
				UNP_GRPH_CONTROL, GRPH_NUM_BANKS);

		set_reg_field_value(value, info->video.BANK_WIDTH_LUMA,
				UNP_GRPH_CONTROL, GRPH_BANK_WIDTH_L);

		set_reg_field_value(value, info->video.BANK_HEIGHT_LUMA,
				UNP_GRPH_CONTROL, GRPH_BANK_HEIGHT_L);

		set_reg_field_value(value, info->video.TILE_ASPECT_LUMA,
				UNP_GRPH_CONTROL, GRPH_MACRO_TILE_ASPECT_L);

		set_reg_field_value(value, info->video.TILE_MODE_LUMA,
				UNP_GRPH_CONTROL, GRPH_MICRO_TILE_MODE_L);

		set_reg_field_value(value, info->video.TILE_SPLIT_LUMA,
				UNP_GRPH_CONTROL, GRPH_TILE_SPLIT_L);

		set_reg_field_value(value, info->video.PIPE_CONFIG,
				UNP_GRPH_CONTROL, GRPH_PIPE_CONFIG);

		set_reg_field_value(value, info->video.ARRAY_MODE,
				UNP_GRPH_CONTROL, GRPH_ARRAY_MODE);

		value_chroma = dal_read_reg(
				sf->ctx,
				sf->regs[IDX_GRPH_CONTROL_C]);

		set_reg_field_value(value_chroma,
				info->video.BANK_WIDTH_CHROMA,
				UNP_GRPH_CONTROL_C, GRPH_BANK_WIDTH_C);

		set_reg_field_value(value_chroma,
				info->video.BANK_HEIGHT_CHROMA,
				UNP_GRPH_CONTROL_C, GRPH_BANK_HEIGHT_C);

		set_reg_field_value(value_chroma,
				info->video.TILE_ASPECT_CHROMA,
				UNP_GRPH_CONTROL_C, GRPH_MACRO_TILE_ASPECT_C);

		set_reg_field_value(value_chroma,
				info->video.TILE_MODE_CHROMA,
				UNP_GRPH_CONTROL_C, GRPH_MICRO_TILE_MODE_C);

		set_reg_field_value(value_chroma,
				info->video.TILE_SPLIT_CHROMA,
				UNP_GRPH_CONTROL_C, GRPH_TILE_SPLIT_C);

		dal_write_reg(
			sf->ctx,
			sf->regs[IDX_GRPH_CONTROL_C],
			value_chroma);
	}

	set_reg_field_value(value, 1,
		UNP_GRPH_CONTROL, GRPH_COLOR_EXPANSION_MODE);

	set_reg_field_value(value, 0,
		UNP_GRPH_CONTROL, GRPH_Z);

	dal_write_reg(
		sf->ctx,
		sf->regs[IDX_GRPH_CONTROL],
		value);
}

static void program_pri_addr_l(
	struct surface *sf,
	PHYSICAL_ADDRESS_LOC address)
{
	uint32_t value = 0;
	uint32_t temp = 0;

	/* high register MUST be programmed first */

	temp = address.high_part &
	UNP_GRPH_PRIMARY_SURFACE_ADDRESS_HIGH_L__GRPH_PRIMARY_SURFACE_ADDRESS_HIGH_L_MASK;

	set_reg_field_value(value, temp,
		UNP_GRPH_PRIMARY_SURFACE_ADDRESS_HIGH_L,
		GRPH_PRIMARY_SURFACE_ADDRESS_HIGH_L);

	dal_write_reg(
		sf->ctx,
		sf->regs[IDX_GRPH_PRIMARY_SURFACE_ADDRESS_HIGH_L],
		value);

	temp = 0;
	value = 0;
	temp = address.low_part >>
	UNP_GRPH_PRIMARY_SURFACE_ADDRESS_L__GRPH_PRIMARY_SURFACE_ADDRESS_L__SHIFT;

	set_reg_field_value(value, temp,
		UNP_GRPH_PRIMARY_SURFACE_ADDRESS_L,
		GRPH_PRIMARY_SURFACE_ADDRESS_L);

	dal_write_reg(
		sf->ctx,
		sf->regs[IDX_GRPH_PRIMARY_SURFACE_ADDRESS_L],
		value);
}

static void program_pri_addr_c(
	struct surface *sf,
	PHYSICAL_ADDRESS_LOC address)
{
	/* high register MUST be programmed first */
	uint32_t value = 0;
	uint32_t temp = address.high_part &
	UNP_GRPH_PRIMARY_SURFACE_ADDRESS_HIGH_C__GRPH_PRIMARY_SURFACE_ADDRESS_HIGH_C_MASK;

	set_reg_field_value(value, temp,
		UNP_GRPH_PRIMARY_SURFACE_ADDRESS_HIGH_C,
		GRPH_PRIMARY_SURFACE_ADDRESS_HIGH_C);

	dal_write_reg(
		sf->ctx,
		sf->regs[IDX_GRPH_PRIMARY_SURFACE_ADDRESS_HIGH_C],
		value);

	value = 0;
	temp = address.low_part >>
	UNP_GRPH_PRIMARY_SURFACE_ADDRESS_C__GRPH_PRIMARY_SURFACE_ADDRESS_C__SHIFT;

	set_reg_field_value(value, temp,
		UNP_GRPH_PRIMARY_SURFACE_ADDRESS_C,
		GRPH_PRIMARY_SURFACE_ADDRESS_C);

	dal_write_reg(
		sf->ctx,
		sf->regs[IDX_GRPH_PRIMARY_SURFACE_ADDRESS_C],
		value);
}

static void program_addr(
	struct surface *sf,
	const struct plane_address *addr)
{
	switch (addr->type) {
	case PLN_ADDR_TYPE_GRAPHICS:
		program_pri_addr_l(
			sf,
			addr->grph.addr);
		break;
	case PLN_ADDR_TYPE_VIDEO_PROGRESSIVE:
		program_pri_addr_l(
			sf,
			addr->video_progressive.luma_addr);
		program_pri_addr_c(
			sf,
			addr->video_progressive.chroma_addr);
		break;
	case PLN_ADDR_TYPE_GRPH_STEREO:
	case PLN_ADDR_TYPE_VIDEO_INTERLACED:
	case PLN_ADDR_TYPE_VIDEO_PROGRESSIVE_STEREO:
	case PLN_ADDR_TYPE_VIDEO_INTERLACED_STEREO:
	default:
		/* not supported */
		BREAK_TO_DEBUGGER();
	}
}

static void set_flip_control(
	struct surface *sf,
	bool immediate)
{
}


static void enable(struct surface *sf)
{
	uint32_t value = 0;

	value = dal_read_reg(sf->ctx, sf->regs[IDX_GRPH_ENABLE]);
	set_reg_field_value(value, 1, UNP_GRPH_ENABLE, GRPH_ENABLE);
	dal_write_reg(sf->ctx, sf->regs[IDX_GRPH_ENABLE], value);
}


/*****************************************/
/* Constructor, Destructor, fcn pointers */
/*****************************************/

static void destroy(struct surface **sf)
{
	dal_free(*sf);
	*sf = NULL;
}

static const struct surface_funcs surface_funcs = {
		.destroy = destroy,
		.enable = enable,
		.program_addr = program_addr,
		.program_pixel_format = program_pixel_format,
		.program_size_and_rotation = program_size_and_rotation,
		.program_tiling = program_tiling,
		.set_flip_control = set_flip_control
};

static bool surface_v_dce110_construct(
	struct surface *sf,
	struct surface_init_data *init_data)
{
	if (!dal_surface_construct(sf, init_data))
		return false;

	sf->regs = sf_regs[init_data->id - CONTROLLER_ID_UNDERLAY0];

	sf->funcs = &surface_funcs;
	return true;
}

struct surface *dal_surface_v_dce110_create(
	struct surface_init_data *init_data)
{
	struct surface *sf = dal_alloc(sizeof(struct surface));

	if (!sf)
		return NULL;

	if (!surface_v_dce110_construct(sf, init_data))
		goto fail;

	return sf;
fail:
	dal_free(sf);
	return NULL;
}



