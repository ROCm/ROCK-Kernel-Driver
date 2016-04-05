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

/* include DCE8 register header files */
#include "dce/dce_8_0_d.h"
#include "dce/dce_8_0_sh_mask.h"

#include "dce80_transform.h"

#define UP_SCALER_RATIO_MAX 16000
#define DOWN_SCALER_RATIO_MAX 250
#define SCALER_RATIO_DIVIDER 1000

#define SCL_REG(reg)\
	(reg + xfm80->offsets.scl_offset)

#define DCFE_REG(reg)\
	(reg + xfm80->offsets.crtc_offset)

static void disable_enhanced_sharpness(struct dce80_transform *xfm80)
{
	uint32_t  value;

	value = dm_read_reg(xfm80->base.ctx,
			SCL_REG(mmSCL_F_SHARP_CONTROL));

	set_reg_field_value(value, 0,
			SCL_F_SHARP_CONTROL, SCL_HF_SHARP_EN);

	set_reg_field_value(value, 0,
			SCL_F_SHARP_CONTROL, SCL_VF_SHARP_EN);

	set_reg_field_value(value, 0,
			SCL_F_SHARP_CONTROL, SCL_HF_SHARP_SCALE_FACTOR);

	set_reg_field_value(value, 0,
			SCL_F_SHARP_CONTROL, SCL_VF_SHARP_SCALE_FACTOR);

	dm_write_reg(xfm80->base.ctx,
			SCL_REG(mmSCL_F_SHARP_CONTROL), value);
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
	struct dce80_transform *xfm80,
	const struct scaler_data *data)
{
	struct dc_context *ctx = xfm80->base.ctx;
	uint32_t addr;
	uint32_t value;

	if (data->taps.h_taps + data->taps.v_taps <= 2) {
		dce80_transform_set_scaler_bypass(&xfm80->base, &data->viewport);
		return false;
	}

	{
		addr = SCL_REG(mmSCL_MODE);
		value = dm_read_reg(ctx, addr);

		if (data->format <= PIXEL_FORMAT_GRPH_END)
			set_reg_field_value(value, 1, SCL_MODE, SCL_MODE);
		else
			set_reg_field_value(value, 2, SCL_MODE, SCL_MODE);

		dm_write_reg(ctx, addr, value);
	}
	{
		addr = SCL_REG(mmSCL_TAP_CONTROL);
		value = dm_read_reg(ctx, addr);

		set_reg_field_value(value, data->taps.h_taps - 1,
				SCL_TAP_CONTROL, SCL_H_NUM_OF_TAPS);

		set_reg_field_value(value, data->taps.v_taps - 1,
				SCL_TAP_CONTROL, SCL_V_NUM_OF_TAPS);

		dm_write_reg(ctx, addr, value);
	}
	{
		addr = SCL_REG(mmSCL_CONTROL);
		value = dm_read_reg(ctx, addr);
		 /* 1 - Replaced out of bound pixels with edge */
		set_reg_field_value(value, 1, SCL_CONTROL, SCL_BOUNDARY_MODE);

		/* 1 - Replaced out of bound pixels with the edge pixel. */
		dm_write_reg(ctx, addr, value);
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
		struct dce80_transform *xfm80,
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

	dm_write_reg(xfm80->base.ctx,
			SCL_REG(mmEXT_OVERSCAN_LEFT_RIGHT),
			overscan_left_right);

	dm_write_reg(xfm80->base.ctx,
			SCL_REG(mmEXT_OVERSCAN_TOP_BOTTOM),
			overscan_top_bottom);
}

static void program_two_taps_filter(
	struct dce80_transform *xfm80,
	bool enable,
	bool vertical)
{
	uint32_t addr;
	uint32_t value;
	/* 1: Hard coded 2 tap filter
	 * 0: Programmable 2 tap filter from coefficient RAM
	 */
	if (vertical) {
		addr = SCL_REG(mmSCL_VERT_FILTER_CONTROL);
		value = dm_read_reg(xfm80->base.ctx, addr);
		set_reg_field_value(
			value,
			enable ? 1 : 0,
				SCL_VERT_FILTER_CONTROL,
				SCL_V_2TAP_HARDCODE_COEF_EN);

	} else {
		addr = SCL_REG(mmSCL_HORZ_FILTER_CONTROL);
		value = dm_read_reg(xfm80->base.ctx, addr);
		set_reg_field_value(
			value,
			enable ? 1 : 0,
			SCL_HORZ_FILTER_CONTROL,
			SCL_H_2TAP_HARDCODE_COEF_EN);
	}

	dm_write_reg(xfm80->base.ctx, addr, value);
}

static void set_coeff_update_complete(struct dce80_transform *xfm80)
{
	uint32_t value;
	uint32_t addr = SCL_REG(mmSCL_UPDATE);

	value = dm_read_reg(xfm80->base.ctx, addr);
	set_reg_field_value(value, 1,
			SCL_UPDATE, SCL_COEF_UPDATE_COMPLETE);
	dm_write_reg(xfm80->base.ctx, addr, value);
}

static void program_filter(
	struct dce80_transform *xfm80,
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

	addr = DCFE_REG(mmDCFE_MEM_LIGHT_SLEEP_CNTL);
	pwr_ctrl_orig = dm_read_reg(xfm80->base.ctx, addr);
	pwr_ctrl_off = pwr_ctrl_orig;
	set_reg_field_value(
		pwr_ctrl_off,
		1,
		DCFE_MEM_LIGHT_SLEEP_CNTL,
		SCL_LIGHT_SLEEP_DIS);
	dm_write_reg(xfm80->base.ctx, addr, pwr_ctrl_off);

	/* Wait to disable gating: */
	for (i = 0;
		i < 10 &&
		get_reg_field_value(
			dm_read_reg(xfm80->base.ctx, addr),
			DCFE_MEM_LIGHT_SLEEP_CNTL,
			SCL_MEM_PWR_STATE);
		i++)
		udelay(1);

	ASSERT(i < 10);

	select_addr = SCL_REG(mmSCL_COEF_RAM_SELECT);
	select = dm_read_reg(xfm80->base.ctx, select_addr);

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
			dm_write_reg(xfm80->base.ctx, select_addr, select);

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

			dm_write_reg(
				xfm80->base.ctx,
				SCL_REG(mmSCL_COEF_RAM_TAP_DATA),
				data);
		}
	}

	ASSERT(coeffs_num == array_idx);

	/* reset the power gating register */
	dm_write_reg(
		xfm80->base.ctx,
		DCFE_REG(mmDCFE_MEM_LIGHT_SLEEP_CNTL),
		pwr_ctrl_orig);

	set_coeff_update_complete(xfm80);
}

/*
 *
 * Populates an array with filter coefficients in 1.1.12 fixed point form
*/
static bool get_filter_coefficients(
	struct dce80_transform *xfm80,
	uint32_t taps,
	uint32_t **data_tab,
	uint32_t *data_size)
{
	uint32_t num = 0;
	uint32_t i;
	const struct fixed31_32 *filter =
		dal_scaler_filter_get(
			xfm80->base.filter,
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
	struct dce80_transform *xfm80,
	const struct scaler_data *data,
	bool horizontal)
{
	struct scaler_filter_params filter_params;
	enum ram_filter_type filter_type;
	uint32_t src_size;
	uint32_t dst_size;

	uint32_t *filter_data = NULL;
	uint32_t filter_data_size = 0;

	/* 16 phases total for DCE8 */
	filter_params.phases = 16;

	if (horizontal) {
		filter_params.taps = data->taps.h_taps;
		filter_params.sharpness = 0; /* TODO */
		filter_params.flags.bits.HORIZONTAL = 1;

		src_size = data->viewport.width;
		dst_size =
			dal_fixed31_32_floor(
				dal_fixed31_32_div(
					dal_fixed31_32_from_int(
						data->viewport.width),
					data->ratios.horz));

		filter_type = FILTER_TYPE_RGB_Y_HORIZONTAL;
	} else {
		filter_params.taps = data->taps.v_taps;
		filter_params.sharpness = 0; /* TODO */
		filter_params.flags.bits.HORIZONTAL = 0;

		src_size = data->viewport.height;
		dst_size =
			dal_fixed31_32_floor(
				dal_fixed31_32_div(
					dal_fixed31_32_from_int(
						data->viewport.height),
					data->ratios.vert));

		filter_type = FILTER_TYPE_RGB_Y_VERTICAL;
	}

	/* 1. Generate the coefficients */
	if (!dal_scaler_filter_generate(
		xfm80->base.filter,
		&filter_params,
		src_size,
		dst_size))
		return false;

	/* 2. Convert coefficients to fixed point format 1.12 (note coeff.
	 * could be negative(!) and  range is [ from -1 to 1 ]) */
	if (!get_filter_coefficients(
		xfm80,
		filter_params.taps,
		&filter_data,
		&filter_data_size))
		return false;

	/* 3. Program the filter */
	program_filter(
		xfm80,
		filter_type,
		&filter_params,
		filter_data,
		filter_data_size);

	return true;
}

static void program_viewport(
	struct dce80_transform *xfm80,
	const struct rect *view_port)
{
	struct dc_context *ctx = xfm80->base.ctx;
	uint32_t value = 0;
	uint32_t addr = 0;

	addr = SCL_REG(mmVIEWPORT_START);
	value = dm_read_reg(ctx, addr);
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
	dm_write_reg(ctx, addr, value);

	addr = SCL_REG(mmVIEWPORT_SIZE);
	value = dm_read_reg(ctx, addr);
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
	dm_write_reg(ctx, addr, value);

	/* TODO: add stereo support */
}

static void calculate_inits(
	struct dce80_transform *xfm80,
	const struct scaler_data *data,
	struct scl_ratios_inits *inits)
{
	struct fixed31_32 h_init;
	struct fixed31_32 v_init;

	inits->h_int_scale_ratio =
		dal_fixed31_32_u2d19(data->ratios.horz) << 5;
	inits->v_int_scale_ratio =
		dal_fixed31_32_u2d19(data->ratios.vert) << 5;

	h_init =
		dal_fixed31_32_div_int(
			dal_fixed31_32_add(
				data->ratios.horz,
				dal_fixed31_32_from_int(data->taps.h_taps + 1)),
				2);
	inits->h_init.integer = dal_fixed31_32_floor(h_init);
	inits->h_init.fraction = dal_fixed31_32_u0d19(h_init) << 5;

	v_init =
		dal_fixed31_32_div_int(
			dal_fixed31_32_add(
				data->ratios.vert,
				dal_fixed31_32_from_int(data->taps.v_taps + 1)),
				2);
	inits->v_init.integer = dal_fixed31_32_floor(v_init);
	inits->v_init.fraction = dal_fixed31_32_u0d19(v_init) << 5;
}

static void program_scl_ratios_inits(
	struct dce80_transform *xfm80,
	struct scl_ratios_inits *inits)
{
	uint32_t addr = SCL_REG(mmSCL_HORZ_FILTER_SCALE_RATIO);
	uint32_t value = 0;

	set_reg_field_value(
		value,
		inits->h_int_scale_ratio,
		SCL_HORZ_FILTER_SCALE_RATIO,
		SCL_H_SCALE_RATIO);
	dm_write_reg(xfm80->base.ctx, addr, value);

	addr = SCL_REG(mmSCL_VERT_FILTER_SCALE_RATIO);
	value = 0;
	set_reg_field_value(
		value,
		inits->v_int_scale_ratio,
		SCL_VERT_FILTER_SCALE_RATIO,
		SCL_V_SCALE_RATIO);
	dm_write_reg(xfm80->base.ctx, addr, value);

	addr = SCL_REG(mmSCL_HORZ_FILTER_INIT);
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
	dm_write_reg(xfm80->base.ctx, addr, value);

	addr = SCL_REG(mmSCL_VERT_FILTER_INIT);
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
	dm_write_reg(xfm80->base.ctx, addr, value);

	addr = SCL_REG(mmSCL_AUTOMATIC_MODE_CONTROL);
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
	dm_write_reg(xfm80->base.ctx, addr, value);
}

bool dce80_transform_set_scaler(
	struct transform *xfm,
	const struct scaler_data *data)
{
	struct dce80_transform *xfm80 = TO_DCE80_TRANSFORM(xfm);
	bool is_scaling_required;
	struct dc_context *ctx = xfm->ctx;

	{
		uint32_t addr = SCL_REG(mmSCL_BYPASS_CONTROL);
		uint32_t value = dm_read_reg(xfm->ctx, addr);

		set_reg_field_value(
			value,
			0,
			SCL_BYPASS_CONTROL,
			SCL_BYPASS_MODE);
		dm_write_reg(xfm->ctx, addr, value);
	}

	disable_enhanced_sharpness(xfm80);

	/* 3. Program overscan */
	program_overscan(xfm80, &data->overscan);

	/* 4. Program taps and configuration */
	is_scaling_required = setup_scaling_configuration(xfm80, data);
	if (is_scaling_required) {
		/* 5. Calculate and program ratio, filter initialization */
		struct scl_ratios_inits inits = { 0 };

		calculate_inits(xfm80, data, &inits);

		program_scl_ratios_inits(xfm80, &inits);

		/* 6. Program vertical filters */
		if (data->taps.v_taps > 2) {
			program_two_taps_filter(xfm80, false, true);

			if (!program_multi_taps_filter(xfm80, data, false)) {
				dal_logger_write(ctx->logger,
					LOG_MAJOR_DCP,
					LOG_MINOR_DCP_SCALER,
					"Failed vertical taps programming\n");
				return false;
			}
		} else
			program_two_taps_filter(xfm80, true, true);

		/* 7. Program horizontal filters */
		if (data->taps.h_taps > 2) {
			program_two_taps_filter(xfm80, false, false);

			if (!program_multi_taps_filter(xfm80, data, true)) {
				dal_logger_write(ctx->logger,
					LOG_MAJOR_DCP,
					LOG_MINOR_DCP_SCALER,
					"Failed horizontal taps programming\n");
				return false;
			}
		} else
			program_two_taps_filter(xfm80, true, false);
	}

	/* 7. Program the viewport */
	program_viewport(xfm80, &data->viewport);

	return true;
}

void dce80_transform_set_scaler_bypass(
		struct transform *xfm,
		const struct rect *size)
{
	struct dce80_transform *xfm80 = TO_DCE80_TRANSFORM(xfm);
	uint32_t sclv_mode;

	disable_enhanced_sharpness(xfm80);

	sclv_mode = dm_read_reg(xfm->ctx, SCL_REG(mmSCL_MODE));
	set_reg_field_value(sclv_mode, 0, SCL_MODE, SCL_MODE);
	dm_write_reg(xfm->ctx, SCL_REG(mmSCL_MODE), sclv_mode);
}

void dce80_transform_set_scaler_filter(
	struct transform *xfm,
	struct scaler_filter *filter)
{
	xfm->filter = filter;
}

