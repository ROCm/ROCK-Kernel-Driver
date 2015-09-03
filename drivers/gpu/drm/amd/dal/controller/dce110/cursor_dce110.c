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

#include "cursor_dce110.h"

#define CURSOR_COLOR_BLACK 0x00000000
#define CURSOR_COLOR_WHITE 0xFFFFFFFF

enum cur_regs_idx {
	IDX_CUR_CONTROL,
	IDX_CUR_UPDATE,
	IDX_CUR_POSITION,
	IDX_CUR_HOT_SPOT,
	IDX_CUR_SIZE,

	IDX_CUR_COLOR1,
	IDX_CUR_COLOR2,

	IDX_CUR_SURFACE_ADDRESS,
	IDX_CUR_SURFACE_ADDRESS_HIGH,

	CUR_REGS_IDX_SIZE
};

#define regs_for_cursor(id)\
[CONTROLLER_ID_D ## id - 1] = {\
	[IDX_CUR_CONTROL] = mmDCP ## id ## _CUR_CONTROL,\
	[IDX_CUR_UPDATE] = mmDCP ## id ## _CUR_UPDATE,\
	[IDX_CUR_POSITION] = mmDCP ## id ## _CUR_POSITION,\
	[IDX_CUR_HOT_SPOT] = mmDCP ## id ## _CUR_HOT_SPOT,\
	[IDX_CUR_SIZE] = mmDCP ## id ## _CUR_SIZE,\
	[IDX_CUR_COLOR1] = mmDCP ## id ## _CUR_COLOR1,\
	[IDX_CUR_COLOR2] = mmDCP ## id ## _CUR_COLOR2,\
	[IDX_CUR_SURFACE_ADDRESS] = mmDCP ## id ## _CUR_SURFACE_ADDRESS,\
	[IDX_CUR_SURFACE_ADDRESS_HIGH] =\
	mmDCP ## id ## _CUR_SURFACE_ADDRESS_HIGH,\
}



static const uint32_t cur_regs[][CUR_REGS_IDX_SIZE] = {
	regs_for_cursor(0),
	regs_for_cursor(1),
	regs_for_cursor(2),
};

static void enable(
	struct cursor *cur, bool enable)
{
	uint32_t value = 0;

	value = dal_read_reg(cur->ctx, cur->regs[IDX_CUR_CONTROL]);
	set_reg_field_value(value, enable, CUR_CONTROL, CURSOR_EN);
	dal_write_reg(cur->ctx, cur->regs[IDX_CUR_CONTROL], value);
	cur->is_enabled = enable;
}

static void lock(
	struct cursor *cur, bool lock)
{
	uint32_t value = 0;

	value = dal_read_reg(cur->ctx, cur->regs[IDX_CUR_UPDATE]);
	set_reg_field_value(value, lock, CUR_UPDATE, CURSOR_UPDATE_LOCK);
	dal_write_reg(cur->ctx, cur->regs[IDX_CUR_UPDATE], value);
}

static void program_position(
	struct cursor *cur,
	uint32_t x,
	uint32_t y)
{
	uint32_t value = 0;

	value = dal_read_reg(cur->ctx, cur->regs[IDX_CUR_POSITION]);
	set_reg_field_value(value, x, CUR_POSITION, CURSOR_X_POSITION);
	set_reg_field_value(value, y, CUR_POSITION, CURSOR_Y_POSITION);
	dal_write_reg(cur->ctx, cur->regs[IDX_CUR_POSITION], value);
}

static bool program_control(
	struct cursor *cur,
	enum cursor_color_format color_format,
	bool enable_magnifcation,
	bool inverse_transparent_clamping)
{
	uint32_t value = 0;
	uint32_t mode = 0;

	switch (color_format) {
	case CURSOR_MODE_MONO:
		mode = 0;
		break;
	case CURSOR_MODE_COLOR_1BIT_AND:
		mode = 1;
		break;
	case CURSOR_MODE_COLOR_PRE_MULTIPLIED_ALPHA:
		mode = 2;
		break;
	case CURSOR_MODE_COLOR_UN_PRE_MULTIPLIED_ALPHA:
		mode = 3;
		break;
	default:
		return false;
	}

	set_reg_field_value(value, mode, CUR_CONTROL, CURSOR_MODE);
	set_reg_field_value(value, enable_magnifcation,
			CUR_CONTROL, CURSOR_2X_MAGNIFY);
	set_reg_field_value(value, inverse_transparent_clamping,
			CUR_CONTROL, CUR_INV_TRANS_CLAMP);
	dal_write_reg(cur->ctx, cur->regs[IDX_CUR_CONTROL], value);

	if (color_format == CURSOR_MODE_MONO) {
		dal_write_reg(cur->ctx, cur->regs[IDX_CUR_COLOR1],
				CURSOR_COLOR_BLACK);
		dal_write_reg(cur->ctx, cur->regs[IDX_CUR_COLOR2],
				CURSOR_COLOR_WHITE);
	}
	return true;
}

static void program_hotspot(
	struct cursor *cur,
	uint32_t x,
	uint32_t y)
{
	uint32_t value = 0;

	value = dal_read_reg(cur->ctx, cur->regs[IDX_CUR_HOT_SPOT]);
	set_reg_field_value(value, x, CUR_HOT_SPOT, CURSOR_HOT_SPOT_X);
	set_reg_field_value(value, y, CUR_HOT_SPOT, CURSOR_HOT_SPOT_Y);
	dal_write_reg(cur->ctx, cur->regs[IDX_CUR_HOT_SPOT], value);
}

static void program_size(
	struct cursor *cur,
	uint32_t width,
	uint32_t height)
{
	uint32_t value = 0;

	value = dal_read_reg(cur->ctx, cur->regs[IDX_CUR_SIZE]);
	set_reg_field_value(value, width, CUR_SIZE, CURSOR_WIDTH);
	set_reg_field_value(value, height, CUR_SIZE, CURSOR_HEIGHT);
	dal_write_reg(cur->ctx, cur->regs[IDX_CUR_SIZE], value);
}

static void program_address(
	struct cursor *cur,
	PHYSICAL_ADDRESS_LOC address)
{
	/* SURFACE_ADDRESS_HIGH: Higher order bits (39:32) of hardware cursor
	 * surface base address in byte. It is 4K byte aligned.
	 * The correct way to program cursor surface address is to first write
	 * to CUR_SURFACE_ADDRESS_HIGH, and then write to CUR_SURFACE_ADDRESS */

	dal_write_reg(cur->ctx, cur->regs[IDX_CUR_SURFACE_ADDRESS_HIGH],
			address.high_part);

	dal_write_reg(cur->ctx, cur->regs[IDX_CUR_SURFACE_ADDRESS],
			address.low_part);
}


/*****************************************/
/* Constructor, Destructor, fcn pointers */
/*****************************************/

static void destroy(struct cursor **cur)
{
	dal_free(*cur);
	*cur = NULL;
}

static const struct cursor_funcs cur_funcs = {
		.enable = enable,
		.lock = lock,
		.program_position = program_position,
		.program_control = program_control,
		.program_hotspot = program_hotspot,
		.program_size = program_size,
		.program_address = program_address,
		.destroy = destroy
};

static bool cursor_dce110_construct(
	struct cursor *cur,
	struct cursor_init_data *init_data)
{
	if (!dal_cursor_construct(cur, init_data))
		return false;

	cur->regs = cur_regs[init_data->id - 1];

	cur->funcs = &cur_funcs;
	return true;
}

struct cursor *dal_cursor_dce110_create(
	struct cursor_init_data *init_data)
{
	struct cursor *cur = dal_alloc(sizeof(struct cursor));

	if (!cur)
		return NULL;

	if (!cursor_dce110_construct(cur, init_data))
		goto fail;

	return cur;
fail:
	dal_free(cur);
	return NULL;
}



