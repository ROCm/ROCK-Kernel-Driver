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

#include "dce110_transform.h"

#define NOT_IMPLEMENTED()  DAL_LOGGER_NOT_IMPL(LOG_MINOR_COMPONENT_CONTROLLER,\
			"TRANSFORM SCALER:%s()\n", __func__)

/*
*****************************************************************************
*  Function: calculateViewport
*
*  @brief
*     Calculates all of the data required to set the viewport
*
*  @param [in]  pData:      scaler settings data
*  @param [out] pLumaVp:    luma viewport information
*  @param [out] pChromaVp:  chroma viewport information
*  @param [out] srcResCx2:  source chroma resolution times 2  - for multi-taps
*
*****************************************************************************
*/
static void calculate_viewport(
		const struct scaler_data *scl_data,
		struct rect *luma_viewport,
		struct rect *chroma_viewport)
{
	/*Do not set chroma vp for rgb444 pixel format*/
	luma_viewport->x = scl_data->viewport.x - scl_data->viewport.x % 2;
	luma_viewport->y = scl_data->viewport.y - scl_data->viewport.y % 2;
	luma_viewport->width =
		scl_data->viewport.width - scl_data->viewport.width % 2;
	luma_viewport->height =
		scl_data->viewport.height - scl_data->viewport.height % 2;


	if (scl_data->dal_pixel_format == PIXEL_FORMAT_422BPP16) {
		luma_viewport->width += luma_viewport->width % 2;

		chroma_viewport->x = luma_viewport->x / 2;
		chroma_viewport->width = luma_viewport->width / 2;
	} else if (scl_data->dal_pixel_format == PIXEL_FORMAT_420BPP12) {
		luma_viewport->height += luma_viewport->height % 2;
		luma_viewport->width += luma_viewport->width % 2;
		/*for 420 video chroma is 1/4 the area of luma, scaled
		 *vertically and horizontally
		 */
		chroma_viewport->x = luma_viewport->x / 2;
		chroma_viewport->y = luma_viewport->y / 2;
		chroma_viewport->height = luma_viewport->height / 2;
		chroma_viewport->width = luma_viewport->width / 2;
	}
}


static void program_viewport(
	struct dce110_transform *xfm110,
	struct rect *luma_view_port,
	struct rect *chroma_view_port)
{
	struct dc_context *ctx = xfm110->base.ctx;
	uint32_t value = 0;
	uint32_t addr = 0;

	if (luma_view_port->width != 0 && luma_view_port->height != 0) {
		addr = mmSCLV_VIEWPORT_START;
		value = 0;
		set_reg_field_value(
			value,
			luma_view_port->x,
			SCLV_VIEWPORT_START,
			VIEWPORT_X_START);
		set_reg_field_value(
			value,
			luma_view_port->y,
			SCLV_VIEWPORT_START,
			VIEWPORT_Y_START);
		dal_write_reg(ctx, addr, value);

		addr = mmSCLV_VIEWPORT_SIZE;
		value = 0;
		set_reg_field_value(
			value,
			luma_view_port->height,
			SCLV_VIEWPORT_SIZE,
			VIEWPORT_HEIGHT);
		set_reg_field_value(
			value,
			luma_view_port->width,
			SCLV_VIEWPORT_SIZE,
			VIEWPORT_WIDTH);
		dal_write_reg(ctx, addr, value);
	}

	if (chroma_view_port->width != 0 && chroma_view_port->height != 0) {
		addr = mmSCLV_VIEWPORT_START_C;
		value = 0;
		set_reg_field_value(
			value,
			chroma_view_port->x,
			SCLV_VIEWPORT_START_C,
			VIEWPORT_X_START_C);
		set_reg_field_value(
			value,
			chroma_view_port->y,
			SCLV_VIEWPORT_START_C,
			VIEWPORT_Y_START_C);
		dal_write_reg(ctx, addr, value);

		addr = mmSCLV_VIEWPORT_SIZE_C;
		value = 0;
		set_reg_field_value(
			value,
			chroma_view_port->height,
			SCLV_VIEWPORT_SIZE_C,
			VIEWPORT_HEIGHT_C);
		set_reg_field_value(
			value,
			chroma_view_port->width,
			SCLV_VIEWPORT_SIZE_C,
			VIEWPORT_WIDTH_C);
		dal_write_reg(ctx, addr, value);
	}
	/* TODO: add stereo support */
}


/* Until and For MPO video play story, to reduce time for implementation,
 * below limits are applied for now: 2_TAPS only
 * Use auto-calculated filter values
 * Following routines will be empty for now:
 *
 * programSclRatiosInits -- calcualate scaler ratio manually
 * calculateInits --- calcualate scaler ratio manually
 * programFilter -- multi-taps
 * GetOptimalNumberOfTaps -- will hard coded to 2 TAPS
 * GetNextLowerNumberOfTaps -- will hard coded to 2TAPS
 * validateRequestedScaleRatio - used by GetOptimalNumberOfTaps internally
 */

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
	struct dce110_transform *xfm110,
	const struct scaler_data *data)
{
	bool is_scaling_needed = false;
	struct dc_context *ctx = xfm110->base.ctx;
	uint32_t value = 0;

	if (data->taps.h_taps + data->taps.v_taps > 2) {
		set_reg_field_value(value, 1, SCLV_MODE, SCL_MODE);
		set_reg_field_value(value, 1, SCLV_MODE, SCL_PSCL_EN);
		is_scaling_needed = true;
	} else {
		set_reg_field_value(value, 0, SCLV_MODE, SCL_MODE);
		set_reg_field_value(value, 0, SCLV_MODE, SCL_PSCL_EN);
	}

	if (data->taps.h_taps_c + data->taps.v_taps_c > 2) {
		set_reg_field_value(value, 1, SCLV_MODE, SCL_MODE_C);
		set_reg_field_value(value, 1, SCLV_MODE, SCL_PSCL_EN_C);
		is_scaling_needed = true;
	} else if (data->dal_pixel_format != PIXEL_FORMAT_420BPP12 &&
		data->dal_pixel_format != PIXEL_FORMAT_422BPP16) {
		set_reg_field_value(
			value,
			get_reg_field_value(value, SCLV_MODE, SCL_MODE),
			SCLV_MODE,
			SCL_MODE_C);
		set_reg_field_value(
			value,
			get_reg_field_value(value, SCLV_MODE, SCL_PSCL_EN),
			SCLV_MODE,
			SCL_PSCL_EN_C);
	} else {
		set_reg_field_value(value, 0, SCLV_MODE, SCL_MODE_C);
		set_reg_field_value(value, 0, SCLV_MODE, SCL_PSCL_EN_C);
	}
	dal_write_reg(ctx, mmSCLV_MODE, value);

	{
		value = dal_read_reg(ctx, mmSCLV_TAP_CONTROL);

		set_reg_field_value(value, data->taps.h_taps - 1,
				SCLV_TAP_CONTROL, SCL_H_NUM_OF_TAPS);

		set_reg_field_value(value, data->taps.v_taps - 1,
				SCLV_TAP_CONTROL, SCL_V_NUM_OF_TAPS);

		set_reg_field_value(value, data->taps.h_taps_c - 1,
				SCLV_TAP_CONTROL, SCL_H_NUM_OF_TAPS_C);

		set_reg_field_value(value, data->taps.v_taps_c - 1,
				SCLV_TAP_CONTROL, SCL_V_NUM_OF_TAPS_C);

		dal_write_reg(ctx, mmSCLV_TAP_CONTROL, value);
	}

	{
		/* we can ignore this register because we are ok with hw
		 * default 0 -- change to 1 according to dal2 code*/
		value = dal_read_reg(ctx, mmSCLV_CONTROL);
		 /* 0 - Replaced out of bound pixels with black pixel
		  * (or any other required color) */
		set_reg_field_value(value, 1, SCLV_CONTROL, SCL_BOUNDARY_MODE);

		/* 1 - Replaced out of bound pixels with the edge pixel. */
		dal_write_reg(ctx, mmSCLV_CONTROL, value);
	}

	return is_scaling_needed;
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
		struct dce110_transform *xfm110,
		const struct overscan_info *overscan)
{
	uint32_t overscan_left_right = 0;
	uint32_t overscan_top_bottom = 0;

	set_reg_field_value(overscan_left_right, overscan->left,
		SCLV_EXT_OVERSCAN_LEFT_RIGHT, EXT_OVERSCAN_LEFT);

	set_reg_field_value(overscan_left_right, overscan->right,
		SCLV_EXT_OVERSCAN_LEFT_RIGHT, EXT_OVERSCAN_RIGHT);

	set_reg_field_value(overscan_top_bottom, overscan->top,
		SCLV_EXT_OVERSCAN_TOP_BOTTOM, EXT_OVERSCAN_TOP);

	set_reg_field_value(overscan_top_bottom, overscan->bottom,
		SCLV_EXT_OVERSCAN_TOP_BOTTOM, EXT_OVERSCAN_BOTTOM);

	dal_write_reg(xfm110->base.ctx,
			mmSCLV_EXT_OVERSCAN_LEFT_RIGHT,
			overscan_left_right);

	dal_write_reg(xfm110->base.ctx,
			mmSCLV_EXT_OVERSCAN_TOP_BOTTOM,
			overscan_top_bottom);
}
/*
static void setup_auto_scaling(struct dce110_transform *xfm110)
{
	uint32_t value = 0;
	set_reg_field_value(value, 1, SCLV_AUTOMATIC_MODE_CONTROL,
			SCL_V_CALC_AUTO_RATIO_EN);
	set_reg_field_value(value, 1, SCLV_AUTOMATIC_MODE_CONTROL,
			SCL_H_CALC_AUTO_RATIO_EN);
	dal_write_reg(xfm->ctx,
			xfm->regs[IDX_SCL_AUTOMATIC_MODE_CONTROL],
			value);
}
*/

static void program_two_taps_filter_horz(
	struct dce110_transform *xfm110,
	bool hardcode_coff)
{
	uint32_t value = 0;

	if (hardcode_coff)
		set_reg_field_value(
				value,
				1,
				SCLV_HORZ_FILTER_CONTROL,
				SCL_H_2TAP_HARDCODE_COEF_EN);

	dal_write_reg(xfm110->base.ctx,
			mmSCLV_HORZ_FILTER_CONTROL,
			value);
}

static void program_two_taps_filter_vert(
	struct dce110_transform *xfm110,
	bool hardcode_coff)
{
	uint32_t value = 0;

	if (hardcode_coff)
		set_reg_field_value(value, 1, SCLV_VERT_FILTER_CONTROL,
				SCL_V_2TAP_HARDCODE_COEF_EN);

	dal_write_reg(xfm110->base.ctx,
			mmSCLV_VERT_FILTER_CONTROL,
			value);
}

static void set_coeff_update_complete(
		struct dce110_transform *xfm110)
{
	/*TODO: Until now, only scaler bypass, up-scaler 2 -TAPS coeff auto
	 * calculation are implemented. Coefficient RAM is not used
	 * Do not check this flag yet
	 */

	/*uint32_t value;
	uint32_t addr = xfm->regs[IDX_SCL_UPDATE];

	value = dal_read_reg(xfm->ctx, addr);
	set_reg_field_value(value, 0,
			SCL_UPDATE, SCL_COEF_UPDATE_COMPLETE);
	dal_write_reg(xfm->ctx, addr, value);*/
}

static bool program_multi_taps_filter(
	struct dce110_transform *xfm110,
	const struct scaler_data *data,
	bool horizontal)
{
	struct dc_context *ctx = xfm110->base.ctx;

	NOT_IMPLEMENTED();
	return false;
}

static void calculate_inits(
	struct dce110_transform *xfm110,
	const struct scaler_data *data,
	struct sclv_ratios_inits *inits,
	struct rect *luma_viewport,
	struct rect *chroma_viewport)
{
	if (data->dal_pixel_format == PIXEL_FORMAT_420BPP12 ||
		data->dal_pixel_format == PIXEL_FORMAT_422BPP16)
		inits->chroma_enable = true;

	/* TODO: implement rest of this function properly */
	if (inits->chroma_enable) {
		inits->h_int_scale_ratio_luma = 0x1000000;
		inits->v_int_scale_ratio_luma = 0x1000000;
		inits->h_int_scale_ratio_chroma = 0x800000;
		inits->v_int_scale_ratio_chroma = 0x800000;
	}
}

static void program_scl_ratios_inits(
	struct dce110_transform *xfm110,
	struct sclv_ratios_inits *inits)
{
	struct dc_context *ctx = xfm110->base.ctx;
	uint32_t addr = mmSCLV_HORZ_FILTER_SCALE_RATIO;
	uint32_t value = dal_read_reg(ctx, addr);

	set_reg_field_value(
		value,
		inits->h_int_scale_ratio_luma,
		SCLV_HORZ_FILTER_SCALE_RATIO,
		SCL_H_SCALE_RATIO);
	dal_write_reg(ctx, addr, value);

	addr = mmSCLV_VERT_FILTER_SCALE_RATIO;
	value = dal_read_reg(ctx, addr);
	set_reg_field_value(
		value,
		inits->v_int_scale_ratio_luma,
		SCLV_VERT_FILTER_SCALE_RATIO,
		SCL_V_SCALE_RATIO);
	dal_write_reg(ctx, addr, value);

	addr = mmSCLV_HORZ_FILTER_SCALE_RATIO_C;
	value = dal_read_reg(ctx, addr);
	set_reg_field_value(
		value,
		inits->h_int_scale_ratio_chroma,
		SCLV_HORZ_FILTER_SCALE_RATIO_C,
		SCL_H_SCALE_RATIO_C);
	dal_write_reg(ctx, addr, value);

	addr = mmSCLV_VERT_FILTER_SCALE_RATIO_C;
	value = dal_read_reg(ctx, addr);
	set_reg_field_value(
		value,
		inits->v_int_scale_ratio_chroma,
		SCLV_VERT_FILTER_SCALE_RATIO_C,
		SCL_V_SCALE_RATIO_C);
	dal_write_reg(ctx, addr, value);
}

void dce110_transform_underlay_set_scalerv_bypass(struct transform *xfm)
{
	uint32_t addr = mmSCLV_MODE;
	uint32_t value = dal_read_reg(xfm->ctx, addr);

	set_reg_field_value(value, 0, SCLV_MODE, SCL_MODE);
	set_reg_field_value(value, 0, SCLV_MODE, SCL_MODE_C);
	set_reg_field_value(value, 0, SCLV_MODE, SCL_PSCL_EN);
	set_reg_field_value(value, 0, SCLV_MODE, SCL_PSCL_EN_C);
	dal_write_reg(xfm->ctx, addr, value);
}

bool dce110_transform_underlay_is_scaling_enabled(struct transform *xfm)
{
	uint32_t value = dal_read_reg(xfm->ctx, mmSCLV_MODE);
	uint8_t scl_mode = get_reg_field_value(value, SCLV_MODE, SCL_MODE);

	return scl_mode == 0;
}

/* TODO: sync this one with DAL2 */
bool dce110_transform_underlay_set_scaler(
	struct transform *xfm,
	const struct scaler_data *data)
{
	struct dce110_transform *xfm110 = TO_DCE110_TRANSFORM(xfm);
	bool is_scaling_required;
	struct rect luma_viewport = {0};
	struct rect chroma_viewport = {0};
	struct dc_context *ctx = xfm->ctx;

	/* 1. Lock Scaler TODO: enable?*/
	/*set_scaler_update_lock(xfm, true);*/

	/* 2. Calculate viewport, viewport programming should happen after init
	 * calculations as they may require an adjustment in the viewport.
	 */

	calculate_viewport(data, &luma_viewport, &chroma_viewport);

	/* 3. Program overscan */
	program_overscan(xfm110, &data->overscan);

	/* 4. Program taps and configuration */
	is_scaling_required = setup_scaling_configuration(xfm110, data);

	if (is_scaling_required) {
		/* 5. Calculate and program ratio, filter initialization */

		struct sclv_ratios_inits inits = { 0 };

		calculate_inits(
			xfm110,
			data,
			&inits,
			&luma_viewport,
			&chroma_viewport);

		program_scl_ratios_inits(xfm110, &inits);

		/*scaler coeff of 2-TAPS use hardware auto calculated value*/

		/* 6. Program vertical filters */
		if (data->taps.v_taps > 2) {
			program_two_taps_filter_vert(xfm110, false);

			if (!program_multi_taps_filter(xfm110, data, false)) {
				dal_logger_write(ctx->logger,
					LOG_MAJOR_DCP,
					LOG_MINOR_DCP_SCALER,
					"Failed vertical taps programming\n");
				return false;
			}
		} else
			program_two_taps_filter_vert(xfm110, true);

		/* 7. Program horizontal filters */
		if (data->taps.h_taps > 2) {
			program_two_taps_filter_horz(xfm110, false);

			if (!program_multi_taps_filter(xfm110, data, true)) {
				dal_logger_write(ctx->logger,
					LOG_MAJOR_DCP,
					LOG_MINOR_DCP_SCALER,
					"Failed horizontal taps programming\n");
				return false;
			}
		} else
			program_two_taps_filter_horz(xfm110, true);
	}

	/* 8. Program the viewport */
	if (data->flags.bits.SHOULD_PROGRAM_VIEWPORT)
		program_viewport(xfm110, &luma_viewport, &chroma_viewport);

	/* 9. Unlock the Scaler TODO: enable?*/
	/* Every call to "set_scaler_update_lock(xfm, TRUE)"
	 * must have a corresponding call to
	 * "set_scaler_update_lock(xfm, FALSE)" */
	/*set_scaler_update_lock(xfm, false);*/

	/* TODO: investigate purpose/need of SHOULD_UNLOCK */
	if (data->flags.bits.SHOULD_UNLOCK == false)
		set_coeff_update_complete(xfm110);

	return true;
}

