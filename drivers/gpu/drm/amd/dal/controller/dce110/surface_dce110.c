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

#include "dce/dce_11_0_d.h"
#include "dce/dce_11_0_sh_mask.h"

#include "surface_dce110.h"

enum sf_regs_idx {
	IDX_GRPH_CONTROL,
	IDX_PRESCALE_GRPH_CONTROL,

	IDX_GRPH_X_START,
	IDX_GRPH_Y_START,
	IDX_GRPH_X_END,
	IDX_GRPH_Y_END,
	IDX_GRPH_PITCH,

	IDX_HW_ROTATION,
	IDX_LB_DESKTOP_HEIGHT,

	IDX_GRPH_PRIMARY_SURFACE_ADDRESS_HIGH,
	IDX_GRPH_PRIMARY_SURFACE_ADDRESS,
	IDX_GRPH_SECONDARY_SURFACE_ADDRESS_HIGH,
	IDX_GRPH_SECONDARY_SURFACE_ADDRESS,

	IDX_GRPH_UPDATE,
	IDX_GRPH_FLIP_CONTROL,
	IDX_GRPH_ENABLE,

	SF_REGS_IDX_SIZE
};

#define regs_for_surface(id)\
[CONTROLLER_ID_D ## id - 1] = {\
	[IDX_GRPH_CONTROL] = mmDCP ## id ## _GRPH_CONTROL,\
	[IDX_PRESCALE_GRPH_CONTROL] = mmDCP ## id ## _PRESCALE_GRPH_CONTROL,\
	[IDX_GRPH_X_START] = mmDCP ## id ## _GRPH_X_START,\
	[IDX_GRPH_Y_START] = mmDCP ## id ## _GRPH_Y_START,\
	[IDX_GRPH_X_END] = mmDCP ## id ## _GRPH_X_END,\
	[IDX_GRPH_Y_END] = mmDCP ## id ## _GRPH_Y_END,\
	[IDX_GRPH_PITCH] = mmDCP ## id ## _GRPH_PITCH,\
	[IDX_HW_ROTATION] = mmDCP ## id ## _HW_ROTATION,\
	[IDX_LB_DESKTOP_HEIGHT] = mmLB ## id ## _LB_DESKTOP_HEIGHT,\
	[IDX_GRPH_PRIMARY_SURFACE_ADDRESS_HIGH] =\
	mmDCP ## id ## _GRPH_PRIMARY_SURFACE_ADDRESS_HIGH,\
	[IDX_GRPH_PRIMARY_SURFACE_ADDRESS] =\
	mmDCP ## id ## _GRPH_PRIMARY_SURFACE_ADDRESS,\
	[IDX_GRPH_SECONDARY_SURFACE_ADDRESS_HIGH] =\
	mmDCP ## id ## _GRPH_SECONDARY_SURFACE_ADDRESS_HIGH,\
	[IDX_GRPH_SECONDARY_SURFACE_ADDRESS] =\
	mmDCP ## id ## _GRPH_SECONDARY_SURFACE_ADDRESS,\
	[IDX_GRPH_UPDATE] = mmDCP ## id ## _GRPH_UPDATE,\
	[IDX_GRPH_FLIP_CONTROL] = mmDCP ## id ## _GRPH_FLIP_CONTROL,\
	[IDX_GRPH_ENABLE] = mmDCP ## id ## _GRPH_ENABLE,\
}


static const uint32_t sf_regs[][SF_REGS_IDX_SIZE] = {
	regs_for_surface(0),
	regs_for_surface(1),
	regs_for_surface(2),
};


/*
#define FROM_SURFACE(ptr) \
	container_of((ptr), struct surface_dce110, base)
*/

static void program_pixel_format(
	struct surface *sf,
	enum surface_pixel_format format)
{
	/*TODOunion GRPH_SWAP_CNTL grph_swap_cntl;*/

	if (format >= SURFACE_PIXEL_FORMAT_GRPH_BEGIN &&
		format < SURFACE_PIXEL_FORMAT_VIDEO_BEGIN) {
		uint32_t value;
		/*TODO:
		grph_swap_cntl.u32All=0;

		if (format == PIXEL_FORMAT_GRPH_ARGB8888 ||
			format == PIXEL_FORMAT_GRPH_ABGR2101010 ||
			format == PIXEL_FORMAT_GRPH_ABGR2101010_XR_BIAS ||
			format == PIXEL_FORMAT_GRPH_ABGR16161616F) {
			grph_swap_cntl.bits.GRPH_RED_CROSSBAR = 2;
			grph_swap_cntl.bits.GRPH_GREEN_CROSSBAR = 0;
			grph_swap_cntl.bits.GRPH_BLUE_CROSSBAR = 2;
			grph_swap_cntl.bits.GRPH_ALPHA_CROSSBAR  = 0;
		} else {
			grph_swap_cntl.bits.GRPH_RED_CROSSBAR = 0;
			grph_swap_cntl.bits.GRPH_GREEN_CROSSBAR = 0;
			grph_swap_cntl.bits.GRPH_BLUE_CROSSBAR = 0;
			grph_swap_cntl.bits.GRPH_ALPHA_CROSSBAR = 0;
		}
		dal_write_reg(
			base->context,
			mmGRPH_SWAP_CNTL+offset,
			grph_swap_cntl.u32All);*/


		value =	dal_read_reg(
				sf->ctx,
				sf->regs[IDX_GRPH_CONTROL]);

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
			sf->ctx,
			sf->regs[IDX_GRPH_CONTROL],
			value);

		/*TODO [hwentlan] MOVE THIS TO CONTROLLER GAMMA!!!!!*/
		value = dal_read_reg(
			sf->ctx,
			sf->regs[IDX_PRESCALE_GRPH_CONTROL]);

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
			sf->ctx,
			sf->regs[IDX_PRESCALE_GRPH_CONTROL],
			value);
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
		sf->ctx,
		sf->regs[IDX_GRPH_X_START],
		value);

	value = 0;
	set_reg_field_value(value, local_size.grph.surface_size.y,
			GRPH_Y_START, GRPH_Y_START);
	dal_write_reg(
		sf->ctx,
		sf->regs[IDX_GRPH_Y_START],
		value);

	value = 0;
	set_reg_field_value(value, local_size.grph.surface_size.width,
			GRPH_X_END, GRPH_X_END);
	dal_write_reg(
		sf->ctx,
		sf->regs[IDX_GRPH_X_END],
		value);

	value = 0;
	set_reg_field_value(value, local_size.grph.surface_size.height,
			GRPH_Y_END, GRPH_Y_END);
	dal_write_reg(
		sf->ctx,
		sf->regs[IDX_GRPH_Y_END],
		value);

	value = 0;
	set_reg_field_value(value, local_size.grph.surface_pitch,
			GRPH_PITCH, GRPH_PITCH);
	dal_write_reg(
		sf->ctx,
		sf->regs[IDX_GRPH_PITCH],
		value);


	value = 0;
	switch (rotation) {
	case PLANE_ROTATION_ANGLE_90:
		set_reg_field_value(value, 3,
			HW_ROTATION, GRPH_ROTATION_ANGLE);
		break;
	case PLANE_ROTATION_ANGLE_180:
		set_reg_field_value(value, 2,
			HW_ROTATION, GRPH_ROTATION_ANGLE);
		break;
	case PLANE_ROTATION_ANGLE_270:
		set_reg_field_value(value, 1,
			HW_ROTATION, GRPH_ROTATION_ANGLE);
		break;
	default:
		set_reg_field_value(value, 0,
			HW_ROTATION, GRPH_ROTATION_ANGLE);
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
		sf->ctx,
		sf->regs[IDX_GRPH_CONTROL],
		value);
}

static void program_pri_addr(
	struct surface *sf,
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
		sf->ctx,
		sf->regs[IDX_GRPH_PRIMARY_SURFACE_ADDRESS_HIGH],
		value);

	temp = 0;
	value = 0;
	temp = address.low_part >>
	GRPH_PRIMARY_SURFACE_ADDRESS__GRPH_PRIMARY_SURFACE_ADDRESS__SHIFT;

	set_reg_field_value(value, temp,
		GRPH_PRIMARY_SURFACE_ADDRESS,
		GRPH_PRIMARY_SURFACE_ADDRESS);

	dal_write_reg(
		sf->ctx,
		sf->regs[IDX_GRPH_PRIMARY_SURFACE_ADDRESS],
		value);
}

static void program_sec_addr(
	struct surface *sf,
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
			sf->ctx,
			sf->regs[IDX_GRPH_SECONDARY_SURFACE_ADDRESS_HIGH],
			value);

	temp = 0;
	value = 0;
	temp = address.low_part >>
	GRPH_SECONDARY_SURFACE_ADDRESS__GRPH_SECONDARY_SURFACE_ADDRESS__SHIFT;

	set_reg_field_value(value, temp,
		GRPH_SECONDARY_SURFACE_ADDRESS,
		GRPH_SECONDARY_SURFACE_ADDRESS);

	dal_write_reg(
			sf->ctx,
			sf->regs[IDX_GRPH_SECONDARY_SURFACE_ADDRESS],
			value);
}

/*
static void program_sec_video_surface_addr(
	uint32_t offset,
	const PHYSICAL_ADDRESS_LOC *address_luma,
	const PHYSICAL_ADDRESS_LOC *address_chroma)
{
	NOT_IMPLEMENTED();
}

static void program_sec_video_surface_bottom_addr(
	uint32_t offset,
	const PHYSICAL_ADDRESS_LOC *address_luma,
	const PHYSICAL_ADDRESS_LOC *address_chroma)
{
	NOT_IMPLEMENTED();
}

static void program_pri_video_surface_addr(
	uint32_t offset,
	const PHYSICAL_ADDRESS_LOC *address_luma,
	const PHYSICAL_ADDRESS_LOC *address_chroma)
{
	NOT_IMPLEMENTED();
}

static void program_pri_video_surface_bottom_addr(
	uint32_t offset,
	const PHYSICAL_ADDRESS_LOC *address_luma,
	const PHYSICAL_ADDRESS_LOC *address_chroma)
{
	NOT_IMPLEMENTED();
}
*/

static void program_addr(
	struct surface *sf,
	const struct plane_address *addr)
{
	switch (addr->type) {
	case PLN_ADDR_TYPE_GRAPHICS:
		program_pri_addr(
			sf,
			addr->grph.addr);
		break;
	case PLN_ADDR_TYPE_GRPH_STEREO:
		program_pri_addr(
			sf,
			addr->grph_stereo.left_addr);
		program_sec_addr(
			sf,
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

static void set_flip_control(
	struct surface *sf,
	bool immediate)
{
	uint32_t value = 0;

	value = dal_read_reg(
			sf->ctx,
			sf->regs[IDX_GRPH_FLIP_CONTROL]);
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
		sf->ctx,
		sf->regs[IDX_GRPH_FLIP_CONTROL],
		value);
}

/*TODO: move to base class (isr.c)
static bool is_phy_addr_equal(
	const PHYSICAL_ADDRESS_LOC *addr,
	const PHYSICAL_ADDRESS_LOC *cached_addr)
{
	return false;
}
*/

static void enable(struct surface *sf)
{
	uint32_t value = 0;

	value = dal_read_reg(sf->ctx, sf->regs[IDX_GRPH_ENABLE]);
	set_reg_field_value(value, 1, GRPH_ENABLE, GRPH_ENABLE);
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

static bool surface_dce110_construct(
	struct surface *sf,
	struct surface_init_data *init_data)
{
	if (!dal_surface_construct(sf, init_data))
		return false;

	sf->regs = sf_regs[init_data->id - 1];

	sf->funcs = &surface_funcs;
	return true;
}

struct surface *dal_surface_dce110_create(
	struct surface_init_data *init_data)
{
	struct surface *sf = dal_alloc(sizeof(struct surface));

	if (!sf)
		return NULL;

	if (!surface_dce110_construct(sf, init_data))
		goto fail;

	return sf;
fail:
	dal_free(sf);
	return NULL;
}



