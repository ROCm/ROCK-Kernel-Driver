/* Copyright 2012-15 Advanced Micro Devices, Inc.
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

#include "include/grph_object_id.h"
#include "include/bios_parser_interface.h"
#include "include/fixed31_32.h"
#include "include/logger_interface.h"

#include "../scaler_filter.h"
#include "scaler_dce110.h"

enum scl_regs_idx {
	IDX_SCL_F_SHARP_CONTROL,
	IDX_SCL_AUTOMATIC_MODE_CONTROL,
	IDX_SCL_BYPASS_CONTROL,
	IDX_SCL_UPDATE,
	IDX_SCL_VERT_FILTER_CONTROL,
	IDX_SCL_HORZ_FILTER_CONTROL,
	IDX_SCL_ALU_CONTROL,

	IDX_SCL_HORZ_SCALE_RATIO,
	IDX_SCL_VERT_SCALE_RATIO,

	IDX_SCL_HORZ_FILTER_INIT,
	IDX_SCL_VERT_FILTER_INIT,
	IDX_SCL_VERT_FILTER_INIT_BOT,
	IDX_SCL_MANUAL_REPLICATE_CONTROL,

	IDX_SCL_COEF_RAM_SELECT,
	IDX_SCL_COEF_RAM_TAP_DATA,

	IDX_SCL_TAP_CONTROL,

	IDX_SCL_OVERSCAN_LEFT_RIGHT,
	IDX_SCL_OVERSCAN_TOP_BOTTOM,

	IDX_SCL_VIEWPORT_START,
	IDX_SCL_VIEWPORT_SIZE,
	IDX_SCL_MODE,
	IDX_SCL_ROUND_OFFSET,
	IDX_SCL_CONTROL,

	IDX_DCFE_MEM_PWR_CTRL,
	IDX_DCFE_MEM_PWR_STATUS,

	SCL_REGS_IDX_SIZE
};

#define regs_for_scaler(id)\
[CONTROLLER_ID_D ## id - 1] = {\
	[IDX_SCL_F_SHARP_CONTROL] = mmSCL ## id ## _SCL_F_SHARP_CONTROL,\
	[IDX_SCL_AUTOMATIC_MODE_CONTROL] =\
	mmSCL ## id ## _SCL_AUTOMATIC_MODE_CONTROL,\
	[IDX_SCL_BYPASS_CONTROL] = mmSCL ## id ## _SCL_BYPASS_CONTROL,\
	[IDX_SCL_UPDATE] = mmSCL ## id ## _SCL_UPDATE,\
	[IDX_SCL_VERT_FILTER_CONTROL] =\
	mmSCL ## id ## _SCL_VERT_FILTER_CONTROL,\
	[IDX_SCL_HORZ_FILTER_CONTROL] =\
	mmSCL ## id ## _SCL_HORZ_FILTER_CONTROL,\
	[IDX_SCL_ALU_CONTROL] = mmSCL ## id ## _SCL_ALU_CONTROL,\
	[IDX_SCL_HORZ_SCALE_RATIO] =\
	mmSCL ## id ## _SCL_HORZ_FILTER_SCALE_RATIO,\
	[IDX_SCL_VERT_SCALE_RATIO] =\
	mmSCL ## id ## _SCL_VERT_FILTER_SCALE_RATIO,\
	[IDX_SCL_HORZ_FILTER_INIT] = mmSCL ## id ## _SCL_HORZ_FILTER_INIT,\
	[IDX_SCL_VERT_FILTER_INIT] = mmSCL ## id ## _SCL_VERT_FILTER_INIT,\
	[IDX_SCL_VERT_FILTER_INIT_BOT] =\
	mmSCL ## id ## _SCL_VERT_FILTER_INIT_BOT,\
	[IDX_SCL_MANUAL_REPLICATE_CONTROL] =\
	mmSCL ## id ## _SCL_MANUAL_REPLICATE_CONTROL,\
	[IDX_SCL_COEF_RAM_SELECT] = mmSCL ## id ## _SCL_COEF_RAM_SELECT,\
	[IDX_SCL_COEF_RAM_TAP_DATA] = mmSCL ## id ## _SCL_COEF_RAM_TAP_DATA,\
	[IDX_SCL_TAP_CONTROL] = mmSCL ## id ## _SCL_TAP_CONTROL,\
	[IDX_SCL_OVERSCAN_LEFT_RIGHT] =\
	mmSCL ## id ## _EXT_OVERSCAN_LEFT_RIGHT,\
	[IDX_SCL_OVERSCAN_TOP_BOTTOM] =\
	mmSCL ## id ## _EXT_OVERSCAN_TOP_BOTTOM,\
	[IDX_SCL_VIEWPORT_START] = mmSCL ## id ## _VIEWPORT_START,\
	[IDX_SCL_VIEWPORT_SIZE] = mmSCL ## id ## _VIEWPORT_SIZE,\
	[IDX_SCL_MODE] = mmSCL ## id ## _SCL_MODE,\
	[IDX_SCL_ROUND_OFFSET] = mmSCL ## id ## _SCL_ROUND_OFFSET,\
	[IDX_SCL_CONTROL] = mmSCL ## id ## _SCL_CONTROL,\
	[IDX_DCFE_MEM_PWR_CTRL] = mmDCFE ## id ## _DCFE_MEM_PWR_CTRL,\
	[IDX_DCFE_MEM_PWR_STATUS] = mmDCFE ## id ## _DCFE_MEM_PWR_STATUS\
}

static const uint32_t scl_regs[][SCL_REGS_IDX_SIZE] = {
	regs_for_scaler(0),
	regs_for_scaler(1),
	regs_for_scaler(2),
};

static void disable_enhanced_sharpness(struct scaler *scl)
{
	uint32_t  value;

	value = dal_read_reg(scl->ctx,
			scl->regs[IDX_SCL_F_SHARP_CONTROL]);

	set_reg_field_value(value, 0,
			SCL_F_SHARP_CONTROL, SCL_HF_SHARP_EN);

	set_reg_field_value(value, 0,
			SCL_F_SHARP_CONTROL, SCL_VF_SHARP_EN);

	set_reg_field_value(value, 0,
			SCL_F_SHARP_CONTROL, SCL_HF_SHARP_SCALE_FACTOR);

	set_reg_field_value(value, 0,
			SCL_F_SHARP_CONTROL, SCL_VF_SHARP_SCALE_FACTOR);

	dal_write_reg(scl->ctx,
		scl->regs[IDX_SCL_F_SHARP_CONTROL], value);
}

/**
* Function:
* void setup_scaling_configuration
*
* Purpose: setup scaling mode : bypass, RGb, YCbCr and nummber of taps
* Input:   data
*
* Output:
   void
*/
static bool setup_scaling_configuration(
	struct scaler *scl,
	const struct scaler_data *data)
{
	struct dal_context *dal_ctx = scl->ctx;
	uint32_t addr;
	uint32_t value;

	if (data->taps.h_taps + data->taps.v_taps <= 2) {
		scl->funcs->set_scaler_bypass(scl);
		return false;
	}

	{
		addr = scl->regs[IDX_SCL_MODE];
		value = dal_read_reg(dal_ctx, addr);

		if (data->dal_pixel_format <= PIXEL_FORMAT_GRPH_END)
			set_reg_field_value(value, 1, SCL_MODE, SCL_MODE);
		else
			set_reg_field_value(value, 2, SCL_MODE, SCL_MODE);

		set_reg_field_value(value, 1, SCL_MODE, SCL_PSCL_EN);

		dal_write_reg(dal_ctx, addr, value);
	}
	{
		addr = scl->regs[IDX_SCL_TAP_CONTROL];
		value = dal_read_reg(dal_ctx, addr);

		set_reg_field_value(value, data->taps.h_taps - 1,
				SCL_TAP_CONTROL, SCL_H_NUM_OF_TAPS);

		set_reg_field_value(value, data->taps.v_taps - 1,
				SCL_TAP_CONTROL, SCL_V_NUM_OF_TAPS);

		dal_write_reg(dal_ctx, addr, value);
	}
	{
		addr = scl->regs[IDX_SCL_CONTROL];
		value = dal_read_reg(dal_ctx, addr);
		 /* 1 - Replaced out of bound pixels with edge */
		set_reg_field_value(value, 1, SCL_CONTROL, SCL_BOUNDARY_MODE);

		/* 1 - Replaced out of bound pixels with the edge pixel. */
		dal_write_reg(dal_ctx, addr, value);
	}

	return true;
}

/**
* Function:
* void program_overscan
*
* Purpose: Programs overscan border
* Input:   overscan
*
* Output:
   void
*/
static void program_overscan(
		struct scaler *scl,
		const struct overscan_info *overscan)
{
	uint32_t overscan_left_right = 0;
	uint32_t overscan_top_bottom = 0;

	set_reg_field_value(overscan_left_right, overscan->left,
			EXT_OVERSCAN_LEFT_RIGHT, EXT_OVERSCAN_LEFT);

	set_reg_field_value(overscan_left_right, overscan->right,
			EXT_OVERSCAN_LEFT_RIGHT, EXT_OVERSCAN_RIGHT);

	set_reg_field_value(overscan_top_bottom, overscan->top,
			EXT_OVERSCAN_TOP_BOTTOM, EXT_OVERSCAN_TOP);

	set_reg_field_value(overscan_top_bottom, overscan->bottom,
			EXT_OVERSCAN_TOP_BOTTOM, EXT_OVERSCAN_BOTTOM);

	dal_write_reg(scl->ctx,
			scl->regs[IDX_SCL_OVERSCAN_LEFT_RIGHT],
			overscan_left_right);

	dal_write_reg(scl->ctx,
			scl->regs[IDX_SCL_OVERSCAN_TOP_BOTTOM],
			overscan_top_bottom);
}

static void program_two_taps_filter(
	struct scaler *scl,
	bool enable,
	bool vertical)
{
	uint32_t addr;
	uint32_t value;
	/* 1: Hard coded 2 tap filter
	 * 0: Programmable 2 tap filter from coefficient RAM
	 */
	if (vertical) {
		addr = scl->regs[IDX_SCL_VERT_FILTER_CONTROL];
		value = dal_read_reg(scl->ctx, addr);
		set_reg_field_value(
			value,
			enable ? 1 : 0,
				SCL_VERT_FILTER_CONTROL,
				SCL_V_2TAP_HARDCODE_COEF_EN);

	} else {
		addr = scl->regs[IDX_SCL_HORZ_FILTER_CONTROL];
		value = dal_read_reg(scl->ctx, addr);
		set_reg_field_value(
			value,
			enable ? 1 : 0,
			SCL_HORZ_FILTER_CONTROL,
			SCL_H_2TAP_HARDCODE_COEF_EN);
	}

	dal_write_reg(scl->ctx, addr, value);
}

enum {
	DCE110_SCL_UPDATE_PENDING_DELAY = 1000,
	DCE110_SCL_UPDATE_PENDING_CHECKCOUNT = 5000
};

static void set_coeff_update_complete(struct scaler *scl)
{
	uint32_t value;
	uint32_t addr = scl->regs[IDX_SCL_UPDATE];

	value = dal_read_reg(scl->ctx, addr);
	set_reg_field_value(value, 1,
			SCL_UPDATE, SCL_COEF_UPDATE_COMPLETE);
	dal_write_reg(scl->ctx, addr, value);
}

static void program_filter(
	struct scaler *scl,
	enum ram_filter_type filter_type,
	struct scaler_filter_params *scl_filter_params,
	uint32_t *coeffs,
	uint32_t coeffs_num)
{
	uint32_t phase = 0;
	uint32_t array_idx = 0;
	uint32_t pair = 0;

	uint32_t taps_pairs = (scl_filter_params->taps + 1) / 2;
	uint32_t phases_to_program = scl_filter_params->phases / 2 + 1;

	uint32_t i;
	uint32_t addr;
	uint32_t select_addr;
	uint32_t select;
	uint32_t data;
	/* We need to disable power gating on coeff memory to do programming */

	uint32_t pwr_ctrl_orig;
	uint32_t pwr_ctrl_off;

	addr = scl->regs[IDX_DCFE_MEM_PWR_CTRL];
	pwr_ctrl_orig = dal_read_reg(scl->ctx, addr);
	pwr_ctrl_off = pwr_ctrl_orig;
	set_reg_field_value(
		pwr_ctrl_off,
		1,
		DCFE_MEM_PWR_CTRL,
		SCL_COEFF_MEM_PWR_DIS);
	dal_write_reg(scl->ctx, addr, pwr_ctrl_off);

	addr = scl->regs[IDX_DCFE_MEM_PWR_STATUS];
	/* Wait to disable gating: */
	for (i = 0;
		i < 10 &&
		get_reg_field_value(
			dal_read_reg(scl->ctx, addr),
			DCFE_MEM_PWR_STATUS,
			SCL_COEFF_MEM_PWR_STATE);
		i++)
		dal_delay_in_microseconds(1);

	ASSERT(i < 10);

	select_addr = scl->regs[IDX_SCL_COEF_RAM_SELECT];
	select = dal_read_reg(scl->ctx, select_addr);

	set_reg_field_value(
		select,
		filter_type,
		SCL_COEF_RAM_SELECT,
		SCL_C_RAM_FILTER_TYPE);
	set_reg_field_value(
		select,
		0,
		SCL_COEF_RAM_SELECT,
		SCL_C_RAM_TAP_PAIR_IDX);
	set_reg_field_value(
		select,
		0,
		SCL_COEF_RAM_SELECT,
		SCL_C_RAM_PHASE);

	data = 0;

	for (phase = 0; phase < phases_to_program; phase++) {
		/* we always program N/2 + 1 phases, total phases N, but N/2-1
		 * are just mirror phase 0 is unique and phase N/2 is unique
		 * if N is even
		 */

		set_reg_field_value(
			select,
			phase,
			SCL_COEF_RAM_SELECT,
			SCL_C_RAM_PHASE);

		for (pair = 0; pair < taps_pairs; pair++) {
			set_reg_field_value(
				select,
				pair,
				SCL_COEF_RAM_SELECT,
				SCL_C_RAM_TAP_PAIR_IDX);
			dal_write_reg(scl->ctx, select_addr, select);

			/* even tap write enable */
			set_reg_field_value(
				data,
				1,
				SCL_COEF_RAM_TAP_DATA,
				SCL_C_RAM_EVEN_TAP_COEF_EN);
			/* even tap data */
			set_reg_field_value(
				data,
				coeffs[array_idx],
				SCL_COEF_RAM_TAP_DATA,
				SCL_C_RAM_EVEN_TAP_COEF);

			/* if we have odd number of taps and the last pair is
			 * here then we do not need to program
			 */
			if (scl_filter_params->taps % 2 &&
				pair == taps_pairs - 1) {
				/* odd tap write disable */
				set_reg_field_value(
					data,
					0,
					SCL_COEF_RAM_TAP_DATA,
					SCL_C_RAM_ODD_TAP_COEF_EN);
				set_reg_field_value(
					data,
					0,
					SCL_COEF_RAM_TAP_DATA,
					SCL_C_RAM_ODD_TAP_COEF);
				array_idx += 1;
			} else {
				/* odd tap write enable */
				set_reg_field_value(
					data,
					1,
					SCL_COEF_RAM_TAP_DATA,
					SCL_C_RAM_ODD_TAP_COEF_EN);
				/* dbg_val: 0x1000 / sclFilterParams->taps; */
				set_reg_field_value(
					data,
					coeffs[array_idx + 1],
					SCL_COEF_RAM_TAP_DATA,
					SCL_C_RAM_ODD_TAP_COEF);

				array_idx += 2;
			}

			dal_write_reg(
				scl->ctx,
				scl->regs[IDX_SCL_COEF_RAM_TAP_DATA],
				data);
		}
	}

	ASSERT(coeffs_num == array_idx);

	/* reset the power gating register */
	dal_write_reg(
		scl->ctx,
		scl->regs[IDX_DCFE_MEM_PWR_CTRL],
		pwr_ctrl_orig);

	set_coeff_update_complete(scl);
}

/*
 *
 * Populates an array with filter coefficients in 1.1.12 fixed point form
*/
static bool get_filter_coefficients(
	struct scaler *scl,
	uint32_t taps,
	uint32_t **data_tab,
	uint32_t *data_size)
{
	uint32_t num = 0;
	uint32_t i;
	const struct fixed31_32 *filter =
		dal_scaler_filter_get(
			scl->filter,
			data_tab,
			&num);
	uint32_t *data_row;

	if (!filter) {
		BREAK_TO_DEBUGGER();
		return false;
	}
	data_row = *data_tab;

	for (i = 0; i < num; ++i) {
		/* req. format sign fixed 1.1.12, the values are always between
		 * [-1; 1]
		 *
		 * Each phase is mirrored as follows :
		 * 0 : Phase 0
		 * 1 : Phase 1 or Phase 64 - 1 / 128 - 1
		 * N : Phase N or Phase 64 - N / 128 - N
		 *
		 * Convert from Fixed31_32 to 1.1.12 by using floor on value
		 * shifted by number of required fractional bits(12)
		 */
		struct fixed31_32 value = filter[i];

		data_row[i] =
			dal_fixed31_32_floor(dal_fixed31_32_shl(value, 12)) &
			0x3FFC;
	}
	*data_size = num;

	return true;
}

static bool program_multi_taps_filter(
	struct scaler *scl,
	const struct scaler_data *data,
	bool horizontal)
{
	struct scaler_filter_params filter_params;
	enum ram_filter_type filter_type;
	uint32_t src_size;
	uint32_t dst_size;

	uint32_t *filter_data = NULL;
	uint32_t filter_data_size = 0;

	/* 16 phases total for DCE11 */
	filter_params.phases = 16;

	if (horizontal) {
		filter_params.taps = data->taps.h_taps;
		filter_params.sharpness = data->h_sharpness;
		filter_params.flags.bits.HORIZONTAL = 1;

		src_size = data->viewport.width;
		dst_size =
			dal_fixed31_32_floor(
				dal_fixed31_32_div(
					dal_fixed31_32_from_int(
						data->viewport.width),
					data->ratios->horz));

		filter_type = FILTER_TYPE_RGB_Y_HORIZONTAL;
	} else {
		filter_params.taps = data->taps.v_taps;
		filter_params.sharpness = data->v_sharpness;
		filter_params.flags.bits.HORIZONTAL = 0;

		src_size = data->viewport.height;
		dst_size =
			dal_fixed31_32_floor(
				dal_fixed31_32_div(
					dal_fixed31_32_from_int(
						data->viewport.height),
					data->ratios->vert));

		filter_type = FILTER_TYPE_RGB_Y_VERTICAL;
	}

	/* 1. Generate the coefficients */
	if (!dal_scaler_filter_generate(
		scl->filter,
		&filter_params,
		src_size,
		dst_size))
		return false;

	/* 2. Convert coefficients to fixed point format 1.12 (note coeff.
	 * could be negative(!) and  range is [ from -1 to 1 ]) */
	if (!get_filter_coefficients(
		scl,
		filter_params.taps,
		&filter_data,
		&filter_data_size))
		return false;

	/* 3. Program the filter */
	program_filter(
		scl,
		filter_type,
		&filter_params,
		filter_data,
		filter_data_size);

	/* 4. Program the alpha if necessary */
	if (data->flags.bits.SHOULD_PROGRAM_ALPHA) {
		if (horizontal)
			filter_type = FILTER_TYPE_ALPHA_HORIZONTAL;
		else
			filter_type = FILTER_TYPE_ALPHA_VERTICAL;

		program_filter(
			scl,
			filter_type,
			&filter_params,
			filter_data,
			filter_data_size);
	}

	return true;
}

static void program_viewport(
	struct scaler *scl,
	const struct rect *view_port)
{
	struct dal_context *dal_ctx = scl->ctx;
	uint32_t value = 0;
	uint32_t addr = 0;

	addr = scl->regs[IDX_SCL_VIEWPORT_START];
	value = dal_read_reg(dal_ctx, addr);
	set_reg_field_value(
		value,
		view_port->x,
		VIEWPORT_START,
		VIEWPORT_X_START);
	set_reg_field_value(
		value,
		view_port->y,
		VIEWPORT_START,
		VIEWPORT_Y_START);
	dal_write_reg(dal_ctx, addr, value);

	addr = scl->regs[IDX_SCL_VIEWPORT_SIZE];
	value = dal_read_reg(dal_ctx, addr);
	set_reg_field_value(
		value,
		view_port->height,
		VIEWPORT_SIZE,
		VIEWPORT_HEIGHT);
	set_reg_field_value(
		value,
		view_port->width,
		VIEWPORT_SIZE,
		VIEWPORT_WIDTH);
	dal_write_reg(dal_ctx, addr, value);

	/* TODO: add stereo support */
}

static void calculate_inits(
	struct scaler *scl,
	const struct scaler_data *data,
	struct scl_ratios_inits *inits)
{
	struct fixed31_32 h_init;
	struct fixed31_32 v_init;
	struct fixed31_32 v_init_bot;

	inits->bottom_enable = 0;
	inits->h_int_scale_ratio =
		dal_fixed31_32_u2d19(data->ratios->horz) << 5;
	inits->v_int_scale_ratio =
		dal_fixed31_32_u2d19(data->ratios->vert) << 5;

	h_init =
		dal_fixed31_32_div_int(
			dal_fixed31_32_add(
				data->ratios->horz,
				dal_fixed31_32_from_int(data->taps.h_taps + 1)),
				2);
	inits->h_init.integer = dal_fixed31_32_floor(h_init);
	inits->h_init.fraction = dal_fixed31_32_u0d19(h_init) << 5;

	v_init =
		dal_fixed31_32_div_int(
			dal_fixed31_32_add(
				data->ratios->vert,
				dal_fixed31_32_from_int(data->taps.v_taps + 1)),
				2);
	inits->v_init.integer = dal_fixed31_32_floor(v_init);
	inits->v_init.fraction = dal_fixed31_32_u0d19(v_init) << 5;

	if (data->flags.bits.INTERLACED) {
		v_init_bot =
			dal_fixed31_32_add(
				dal_fixed31_32_div_int(
					dal_fixed31_32_add(
						data->ratios->vert,
						dal_fixed31_32_from_int(
							data->taps.v_taps + 1)),
					2),
				data->ratios->vert);
		inits->v_init_bottom.integer = dal_fixed31_32_floor(v_init_bot);
		inits->v_init_bottom.fraction =
			dal_fixed31_32_u0d19(v_init_bot) << 5;

		inits->bottom_enable = 1;
	}
}

static void program_scl_ratios_inits(
	struct scaler *scl,
	struct scl_ratios_inits *inits)
{
	uint32_t addr = scl->regs[IDX_SCL_HORZ_SCALE_RATIO];
	uint32_t value = 0;

	set_reg_field_value(
		value,
		inits->h_int_scale_ratio,
		SCL_HORZ_FILTER_SCALE_RATIO,
		SCL_H_SCALE_RATIO);
	dal_write_reg(scl->ctx, addr, value);

	addr = scl->regs[IDX_SCL_VERT_SCALE_RATIO];
	value = 0;
	set_reg_field_value(
		value,
		inits->v_int_scale_ratio,
		SCL_VERT_FILTER_SCALE_RATIO,
		SCL_V_SCALE_RATIO);
	dal_write_reg(scl->ctx, addr, value);

	addr = scl->regs[IDX_SCL_HORZ_FILTER_INIT];
	value = 0;
	set_reg_field_value(
		value,
		inits->h_init.integer,
		SCL_HORZ_FILTER_INIT,
		SCL_H_INIT_INT);
	set_reg_field_value(
		value,
		inits->h_init.fraction,
		SCL_HORZ_FILTER_INIT,
		SCL_H_INIT_FRAC);
	dal_write_reg(scl->ctx, addr, value);

	addr = scl->regs[IDX_SCL_VERT_FILTER_INIT];
	value = 0;
	set_reg_field_value(
		value,
		inits->v_init.integer,
		SCL_VERT_FILTER_INIT,
		SCL_V_INIT_INT);
	set_reg_field_value(
		value,
		inits->v_init.fraction,
		SCL_VERT_FILTER_INIT,
		SCL_V_INIT_FRAC);
	dal_write_reg(scl->ctx, addr, value);

	if (inits->bottom_enable) {
		addr = scl->regs[IDX_SCL_VERT_FILTER_INIT_BOT];
		value = 0;
		set_reg_field_value(
			value,
			inits->v_init_bottom.integer,
			SCL_VERT_FILTER_INIT_BOT,
			SCL_V_INIT_INT_BOT);
		set_reg_field_value(
			value,
			inits->v_init_bottom.fraction,
			SCL_VERT_FILTER_INIT_BOT,
			SCL_V_INIT_FRAC_BOT);
		dal_write_reg(scl->ctx, addr, value);
	}

	addr = scl->regs[IDX_SCL_AUTOMATIC_MODE_CONTROL];
	value = 0;
	set_reg_field_value(
		value,
		0,
		SCL_AUTOMATIC_MODE_CONTROL,
		SCL_V_CALC_AUTO_RATIO_EN);
	set_reg_field_value(
		value,
		0,
		SCL_AUTOMATIC_MODE_CONTROL,
		SCL_H_CALC_AUTO_RATIO_EN);
	dal_write_reg(scl->ctx, addr, value);
}

static bool set_scaler_wrapper(
	struct scaler *scl,
	const struct scaler_data *data)
{
	bool is_scaling_required;
	struct dal_context *dal_ctx = scl->ctx;

	{
		uint32_t addr = scl->regs[IDX_SCL_BYPASS_CONTROL];
		uint32_t value = dal_read_reg(scl->ctx, addr);

		set_reg_field_value(
			value,
			0,
			SCL_BYPASS_CONTROL,
			SCL_BYPASS_MODE);
		dal_write_reg(scl->ctx, addr, value);
	}

	disable_enhanced_sharpness(scl);

	/* 3. Program overscan */
	program_overscan(scl, &data->overscan);

	/* 4. Program taps and configuration */
	is_scaling_required = setup_scaling_configuration(scl, data);
	if (is_scaling_required) {
		/* 5. Calculate and program ratio, filter initialization */
		struct scl_ratios_inits inits = { 0 };

		calculate_inits(scl, data, &inits);

		program_scl_ratios_inits(scl, &inits);

		/* 6. Program vertical filters */
		if (data->taps.v_taps > 2) {
			program_two_taps_filter(scl, false, true);

			if (!program_multi_taps_filter(scl, data, false)) {
				dal_logger_write(dal_ctx->logger,
					LOG_MAJOR_DCP,
					LOG_MINOR_DCP_SCALER,
					"Failed vertical taps programming\n");
				return false;
			}
		} else
			program_two_taps_filter(scl, true, true);

		/* 7. Program horizontal filters */
		if (data->taps.h_taps > 2) {
			program_two_taps_filter(scl, false, false);

			if (!program_multi_taps_filter(scl, data, true)) {
				dal_logger_write(dal_ctx->logger,
					LOG_MAJOR_DCP,
					LOG_MINOR_DCP_SCALER,
					"Failed horizontal taps programming\n");
				return false;
			}
		} else
			program_two_taps_filter(scl, true, false);
	}

	return true;
}

static void set_scaler_bypass(struct scaler *scl)
{
	uint32_t sclv_mode;

	disable_enhanced_sharpness(scl);

	sclv_mode = dal_read_reg(scl->ctx, scl->regs[IDX_SCL_MODE]);
	set_reg_field_value(sclv_mode, 0, SCL_MODE, SCL_MODE);
	set_reg_field_value(sclv_mode, 0, SCL_MODE, SCL_PSCL_EN);
	dal_write_reg(scl->ctx, scl->regs[IDX_SCL_MODE], sclv_mode);
}

static bool is_scaling_enabled(struct scaler *scl)
{
	uint32_t value;
	uint8_t scl_mode;

	value = dal_read_reg(scl->ctx,
			scl->regs[IDX_SCL_MODE]);

	scl_mode = get_reg_field_value(value, SCL_MODE, SCL_MODE);

	return scl_mode == 0;
}

static void get_viewport(
		struct scaler *scl,
		struct rect *current_view_port)
{
	uint32_t value_start;
	uint32_t value_size;

	if (current_view_port == NULL)
		return;

	value_start = dal_read_reg(scl->ctx, scl->regs[IDX_SCL_VIEWPORT_START]);
	value_size = dal_read_reg(scl->ctx, scl->regs[IDX_SCL_VIEWPORT_SIZE]);

	current_view_port->x = get_reg_field_value(
			value_start,
			VIEWPORT_START,
			VIEWPORT_X_START);
	current_view_port->y = get_reg_field_value(
			value_start,
			VIEWPORT_START,
			VIEWPORT_Y_START);
	current_view_port->height = get_reg_field_value(
			value_size,
			VIEWPORT_SIZE,
			VIEWPORT_HEIGHT);
	current_view_port->width = get_reg_field_value(
			value_size,
			VIEWPORT_SIZE,
			VIEWPORT_WIDTH);
}


/*****************************************/
/* Constructor, Destructor, fcn pointers */
/*****************************************/

static void destroy(struct scaler **scl)
{
	dal_free(*scl);
	*scl = NULL;
}

static const struct scaler_funcs scaler_funcs = {
	.set_scaler_bypass = set_scaler_bypass,
	.is_scaling_enabled = is_scaling_enabled,
	.set_scaler_wrapper = set_scaler_wrapper,
	.destroy = destroy,
	.get_optimal_taps_number = dal_scaler_get_optimal_taps_number,
	.get_next_lower_taps_number = dal_scaler_get_next_lower_taps_number,
	.get_viewport = get_viewport,
	.program_viewport = program_viewport,
};

static bool scaler_dce110_construct(
	struct scaler *scl,
	struct scaler_init_data *init_data)
{
	enum controller_id id;

	if (!dal_scaler_construct(scl, init_data))
		return false;

	id = init_data->id;

	scl->regs = scl_regs[id - 1];

	scl->funcs = &scaler_funcs;
	return true;
}

struct scaler *dal_scaler_dce110_create(
	struct scaler_init_data *init_data)
{
	struct scaler *scl = dal_alloc(sizeof(struct scaler));

	if (!scl)
		return NULL;

	if (!scaler_dce110_construct(scl, init_data))
		goto fail;

	return scl;
fail:
	dal_free(scl);
	return NULL;
}



