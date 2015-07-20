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
#include "include/set_mode_types.h"

#include "../scaler_filter.h"
#include "scaler_v_dce110.h"

enum scl_regs_idx {
	IDX_SCL_AUTOMATIC_MODE_CONTROL,
	IDX_SCL_UPDATE,
	IDX_SCL_VERT_FILTER_CONTROL,
	IDX_SCL_HORZ_FILTER_CONTROL,
	IDX_SCL_ALU_CONTROL,

	IDX_SCL_HORZ_SCALE_RATIO,
	IDX_SCL_VERT_SCALE_RATIO,
	IDX_SCL_HORZ_SCALE_RATIO_C,
	IDX_SCL_VERT_SCALE_RATIO_C,

	IDX_SCL_HORZ_FILTER_INIT,
	IDX_SCL_VERT_FILTER_INIT,
	IDX_SCL_VERT_FILTER_INIT_BOT,
	IDX_SCL_MANUAL_REPLICATE_CONTROL,

	IDX_SCL_SCL_COEF_RAM_SELECT,
	IDX_SCL_SCL_COEF_RAM_TAP_DATA,

	IDX_SCL_TAP_CONTROL,

	IDX_SCL_OVERSCAN_LEFT_RIGHT,
	IDX_SCL_OVERSCAN_TOP_BOTTOM,

	IDX_SCL_VIEWPORT_START,
	IDX_SCL_VIEWPORT_SIZE,
	IDX_SCL_MODE,
	IDX_SCL_ROUND_OFFSET,
	IDX_SCL_CONTROL,

	IDX_SCL_VIEWPORT_START_C,
	IDX_SCL_VIEWPORT_SIZE_C,

	SCL_REGS_IDX_SIZE
};

#define regs_for_underlay_scaler(id)\
[CONTROLLER_ID_UNDERLAY ## id - CONTROLLER_ID_UNDERLAY0] = {\
	[IDX_SCL_AUTOMATIC_MODE_CONTROL] = mmSCLV_AUTOMATIC_MODE_CONTROL,\
	[IDX_SCL_UPDATE] = mmSCLV_UPDATE,\
	[IDX_SCL_VERT_FILTER_CONTROL] = mmSCLV_VERT_FILTER_CONTROL,\
	[IDX_SCL_HORZ_FILTER_CONTROL] = mmSCLV_HORZ_FILTER_CONTROL,\
	[IDX_SCL_ALU_CONTROL] = mmSCLV_ALU_CONTROL,\
	[IDX_SCL_HORZ_SCALE_RATIO] = mmSCLV_HORZ_FILTER_SCALE_RATIO,\
	[IDX_SCL_VERT_SCALE_RATIO] = mmSCLV_VERT_FILTER_SCALE_RATIO,\
	[IDX_SCL_HORZ_SCALE_RATIO_C] = mmSCLV_HORZ_FILTER_SCALE_RATIO_C,\
	[IDX_SCL_VERT_SCALE_RATIO_C] = mmSCLV_VERT_FILTER_SCALE_RATIO_C,\
	[IDX_SCL_HORZ_FILTER_INIT] = mmSCLV_HORZ_FILTER_INIT,\
	[IDX_SCL_VERT_FILTER_INIT] = mmSCLV_VERT_FILTER_INIT,\
	[IDX_SCL_VERT_FILTER_INIT_BOT] = mmSCLV_VERT_FILTER_INIT_BOT,\
	[IDX_SCL_MANUAL_REPLICATE_CONTROL] = mmSCLV_MANUAL_REPLICATE_CONTROL,\
	[IDX_SCL_SCL_COEF_RAM_SELECT] = mmSCLV_COEF_RAM_SELECT,\
	[IDX_SCL_SCL_COEF_RAM_TAP_DATA] = mmSCLV_COEF_RAM_TAP_DATA,\
	[IDX_SCL_TAP_CONTROL] = mmSCLV_TAP_CONTROL,\
	[IDX_SCL_OVERSCAN_LEFT_RIGHT] = mmSCLV_EXT_OVERSCAN_LEFT_RIGHT,\
	[IDX_SCL_OVERSCAN_TOP_BOTTOM] = mmSCLV_EXT_OVERSCAN_TOP_BOTTOM,\
	[IDX_SCL_VIEWPORT_START] = mmSCLV_VIEWPORT_START,\
	[IDX_SCL_VIEWPORT_SIZE] = mmSCLV_VIEWPORT_SIZE,\
	[IDX_SCL_MODE] = mmSCLV_MODE,\
	[IDX_SCL_ROUND_OFFSET] = mmSCL_ROUND_OFFSET,\
	[IDX_SCL_CONTROL] = mmSCLV_CONTROL,\
	[IDX_SCL_VIEWPORT_START_C] = mmSCLV_VIEWPORT_START_C,\
	[IDX_SCL_VIEWPORT_SIZE_C] = mmSCLV_VIEWPORT_SIZE_C,\
}

static const uint32_t scl_underlay_regs[][SCL_REGS_IDX_SIZE] = {
	regs_for_underlay_scaler(0),
};

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
	struct scaler *scl,
	struct rect *luma_view_port,
	struct rect *chroma_view_port)
{
	struct dal_context *dal_ctx = scl->ctx;
	uint32_t value = 0;
	uint32_t addr = 0;

	if (luma_view_port->width != 0 && luma_view_port->height != 0) {
		addr = scl->regs[IDX_SCL_VIEWPORT_START];
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
		dal_write_reg(dal_ctx, addr, value);

		addr = scl->regs[IDX_SCL_VIEWPORT_SIZE];
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
		dal_write_reg(dal_ctx, addr, value);
	}

	if (chroma_view_port->width != 0 && chroma_view_port->height != 0) {
		addr = scl->regs[IDX_SCL_VIEWPORT_START_C];
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
		dal_write_reg(dal_ctx, addr, value);

		addr = scl->regs[IDX_SCL_VIEWPORT_SIZE_C];
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
		dal_write_reg(dal_ctx, addr, value);
	}
	/* TODO: add stereo support */
}

/*****************************************************************************
 * macro definitions
 *****************************************************************************/
#define NOT_IMPLEMENTED()  DAL_LOGGER_NOT_IMPL(LOG_MINOR_COMPONENT_CONTROLLER,\
			"SCALER:%s()\n", __func__)

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
	struct scaler *scl,
	const struct scaler_data *data)
{
	bool is_scaling_needed = false;
	struct dal_context *dal_ctx = scl->ctx;
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
	dal_write_reg(dal_ctx, scl->regs[IDX_SCL_MODE], value);

	{
		value = dal_read_reg(dal_ctx,
				scl->regs[IDX_SCL_TAP_CONTROL]);

		set_reg_field_value(value, data->taps.h_taps - 1,
				SCLV_TAP_CONTROL, SCL_H_NUM_OF_TAPS);

		set_reg_field_value(value, data->taps.v_taps - 1,
				SCLV_TAP_CONTROL, SCL_V_NUM_OF_TAPS);

		set_reg_field_value(value, data->taps.h_taps_c - 1,
				SCLV_TAP_CONTROL, SCL_H_NUM_OF_TAPS_C);

		set_reg_field_value(value, data->taps.v_taps_c - 1,
				SCLV_TAP_CONTROL, SCL_V_NUM_OF_TAPS_C);

		dal_write_reg(dal_ctx,
			scl->regs[IDX_SCL_TAP_CONTROL], value);
	}

	{
		/* we can ignore this register because we are ok with hw
		 * default 0 -- change to 1 according to dal2 code*/
		value = dal_read_reg(dal_ctx,
				scl->regs[IDX_SCL_CONTROL]);
		 /* 0 - Replaced out of bound pixels with black pixel
		  * (or any other required color) */
		set_reg_field_value(value, 1, SCLV_CONTROL, SCL_BOUNDARY_MODE);

		/* 1 - Replaced out of bound pixels with the edge pixel. */
		dal_write_reg(dal_ctx,
			scl->regs[IDX_SCL_CONTROL], value);
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
		struct scaler *scl,
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

	dal_write_reg(scl->ctx,
			scl->regs[IDX_SCL_OVERSCAN_LEFT_RIGHT],
			overscan_left_right);

	dal_write_reg(scl->ctx,
			scl->regs[IDX_SCL_OVERSCAN_TOP_BOTTOM],
			overscan_top_bottom);
}
/*
static void setup_auto_scaling(struct scaler *scl)
{
	uint32_t value = 0;
	set_reg_field_value(value, 1, SCLV_AUTOMATIC_MODE_CONTROL,
			SCL_V_CALC_AUTO_RATIO_EN);
	set_reg_field_value(value, 1, SCLV_AUTOMATIC_MODE_CONTROL,
			SCL_H_CALC_AUTO_RATIO_EN);
	dal_write_reg(scl->ctx,
			scl->regs[IDX_SCL_AUTOMATIC_MODE_CONTROL],
			value);
}
*/
static void set_scaler_bypass(struct scaler *scl)
{
	uint32_t addr = scl->regs[IDX_SCL_MODE];
	uint32_t value = dal_read_reg(scl->ctx, addr);

	set_reg_field_value(value, 0, SCLV_MODE, SCL_MODE);
	set_reg_field_value(value, 0, SCLV_MODE, SCL_MODE_C);
	set_reg_field_value(value, 0, SCLV_MODE, SCL_PSCL_EN);
	set_reg_field_value(value, 0, SCLV_MODE, SCL_PSCL_EN_C);
	dal_write_reg(scl->ctx, addr, value);
}

static bool is_scaling_enabled(struct scaler *scl)
{
	uint32_t value = dal_read_reg(scl->ctx, scl->regs[IDX_SCL_MODE]);
	uint8_t scl_mode = get_reg_field_value(value, SCLV_MODE, SCL_MODE);

	return scl_mode == 0;
}

static void program_two_taps_filter_horz(
	struct scaler *scl,
	bool hardcode_coff)
{
	uint32_t value = 0;

	if (hardcode_coff)
		set_reg_field_value(
				value,
				1,
				SCLV_HORZ_FILTER_CONTROL,
				SCL_H_2TAP_HARDCODE_COEF_EN);

	dal_write_reg(scl->ctx,
			scl->regs[IDX_SCL_HORZ_FILTER_CONTROL],
			value);
}

static void program_two_taps_filter_vert(
	struct scaler *scl,
	bool hardcode_coff)
{
	uint32_t value = 0;

	if (hardcode_coff)
		set_reg_field_value(value, 1, SCLV_VERT_FILTER_CONTROL,
				SCL_V_2TAP_HARDCODE_COEF_EN);

	dal_write_reg(scl->ctx,
			scl->regs[IDX_SCL_VERT_FILTER_CONTROL],
			value);
}


enum {
	DCE110_SCL_UPDATE_PENDING_DELAY = 1000,
	DCE110_SCL_UPDATE_PENDING_CHECKCOUNT = 5000
};

static void set_coeff_update_complete(struct scaler *scl)
{
	/*TODO: Until now, only scaler bypass, up-scaler 2 -TAPS coeff auto
	 * calculation are implemented. Coefficient RAM is not used
	 * Do not check this flag yet
	 */

	/*uint32_t value;
	uint32_t addr = scl->regs[IDX_SCL_UPDATE];

	value = dal_read_reg(scl->ctx, addr);
	set_reg_field_value(value, 0,
			SCL_UPDATE, SCL_COEF_UPDATE_COMPLETE);
	dal_write_reg(scl->ctx, addr, value);*/
}

static bool program_multi_taps_filter(
	struct scaler *scl,
	const struct scaler_data *data,
	bool horizontal)
{
	struct dal_context *dal_context = scl->ctx;

	NOT_IMPLEMENTED();
	return false;
}

static void get_viewport(
		struct scaler *scl,
		struct rect *viewport)
{
	uint32_t value = 0;
	if (viewport != NULL) {
		value = dal_read_reg(scl->ctx,
				scl->regs[IDX_SCL_VIEWPORT_START]);
		viewport->x = get_reg_field_value(value,
				SCLV_VIEWPORT_START, VIEWPORT_X_START);
		viewport->y = get_reg_field_value(value,
				SCLV_VIEWPORT_START, VIEWPORT_Y_START);

		value = dal_read_reg(scl->ctx,
				scl->regs[IDX_SCL_VIEWPORT_SIZE]);
		viewport->height = get_reg_field_value(value,
				SCLV_VIEWPORT_SIZE, VIEWPORT_HEIGHT);
		viewport->width = get_reg_field_value(value,
				SCLV_VIEWPORT_SIZE, VIEWPORT_WIDTH);
	}
}

enum scaler_validation_code get_optimal_taps_number(
		struct scaler_validation_params *scaler_param,
		struct scaling_tap_info *taps)
{
	/*TODO hard code to 2-TAPs for MPO video play story*/
	taps->h_taps = 2;
	taps->v_taps = 2;
	taps->h_taps_c = 2;
	taps->v_taps_c = 2;
	return SCALER_VALIDATION_OK;
}

enum scaler_validation_code get_next_lower_taps_number(
		struct scaler_validation_params *scaler_param,
		struct scaling_tap_info *taps)
{
	/*TODO hard code to 2-TAPs for MPO video play story*/
	return SCALER_VALIDATION_INVALID_INPUT_PARAMETERS;
}

struct sclv_ratios_inits {
	uint32_t chroma_enable;
	uint32_t h_int_scale_ratio_luma;
	uint32_t h_int_scale_ratio_chroma;
	uint32_t v_int_scale_ratio_luma;
	uint32_t v_int_scale_ratio_chroma;
	struct init_int_and_frac h_init_luma;
	struct init_int_and_frac h_init_chroma;
	struct init_int_and_frac v_init_luma;
	struct init_int_and_frac v_init_chroma;
	struct init_int_and_frac h_init_lumabottom;
	struct init_int_and_frac h_init_chromabottom;
	struct init_int_and_frac v_init_lumabottom;
	struct init_int_and_frac v_init_chromabottom;
};

static void calculate_inits(
	struct scaler *scl,
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
	struct scaler *scl,
	struct sclv_ratios_inits *inits)
{
	struct dal_context *ctx = scl->ctx;
	uint32_t addr = scl->regs[IDX_SCL_HORZ_SCALE_RATIO];
	uint32_t value = dal_read_reg(ctx, addr);

	set_reg_field_value(
		value,
		inits->h_int_scale_ratio_luma,
		SCLV_HORZ_FILTER_SCALE_RATIO,
		SCL_H_SCALE_RATIO);
	dal_write_reg(ctx, addr, value);

	addr = scl->regs[IDX_SCL_VERT_SCALE_RATIO];
	value = dal_read_reg(ctx, addr);
	set_reg_field_value(
		value,
		inits->v_int_scale_ratio_luma,
		SCLV_VERT_FILTER_SCALE_RATIO,
		SCL_V_SCALE_RATIO);
	dal_write_reg(ctx, addr, value);

	addr = scl->regs[IDX_SCL_HORZ_SCALE_RATIO_C];
	value = dal_read_reg(ctx, addr);
	set_reg_field_value(
		value,
		inits->h_int_scale_ratio_chroma,
		SCLV_HORZ_FILTER_SCALE_RATIO_C,
		SCL_H_SCALE_RATIO_C);
	dal_write_reg(ctx, addr, value);

	addr = scl->regs[IDX_SCL_VERT_SCALE_RATIO_C];
	value = dal_read_reg(ctx, addr);
	set_reg_field_value(
		value,
		inits->v_int_scale_ratio_chroma,
		SCLV_VERT_FILTER_SCALE_RATIO_C,
		SCL_V_SCALE_RATIO_C);
	dal_write_reg(ctx, addr, value);
}

/* TODO: sync this one with DAL2 */
static bool set_scaler_wrapper(
	struct scaler *scl,
	const struct scaler_data *data)
{
	bool is_scaling_required;
	struct rect luma_viewport = {0};
	struct rect chroma_viewport = {0};
	struct dal_context *dal_ctx = scl->ctx;

	/* 1. Lock Scaler TODO: enable?*/
	/*set_scaler_update_lock(scl, true);*/

	/* 2. Calculate viewport, viewport programming should happen after init
	 * calculations as they may require an adjustment in the viewport.
	 */

	calculate_viewport(data, &luma_viewport, &chroma_viewport);

	/* 3. Program overscan */
	program_overscan(scl, &data->overscan);

	/* 4. Program taps and configuration */
	is_scaling_required = setup_scaling_configuration(scl, data);

	if (is_scaling_required) {
		/* 5. Calculate and program ratio, filter initialization */

		struct sclv_ratios_inits inits = { 0 };

		calculate_inits(
			scl,
			data,
			&inits,
			&luma_viewport,
			&chroma_viewport);

		program_scl_ratios_inits(scl, &inits);

		/*scaler coeff of 2-TAPS use hardware auto calculated value*/

		/* 6. Program vertical filters */
		if (data->taps.v_taps > 2) {
			program_two_taps_filter_vert(scl, false);

			if (!program_multi_taps_filter(scl, data, false)) {
				dal_logger_write(dal_ctx->logger,
					LOG_MAJOR_DCP,
					LOG_MINOR_DCP_SCALER,
					"Failed vertical taps programming\n");
				return false;
			}
		} else
			program_two_taps_filter_vert(scl, true);

		/* 7. Program horizontal filters */
		if (data->taps.h_taps > 2) {
			program_two_taps_filter_horz(scl, false);

			if (!program_multi_taps_filter(scl, data, true)) {
				dal_logger_write(dal_ctx->logger,
					LOG_MAJOR_DCP,
					LOG_MINOR_DCP_SCALER,
					"Failed horizontal taps programming\n");
				return false;
			}
		} else
			program_two_taps_filter_horz(scl, true);
	}

	/* 8. Program the viewport */
	if (data->flags.bits.SHOULD_PROGRAM_VIEWPORT)
		program_viewport(scl, &luma_viewport, &chroma_viewport);

	/* 9. Unlock the Scaler TODO: enable?*/
	/* Every call to "set_scaler_update_lock(scl, TRUE)"
	 * must have a corresponding call to
	 * "set_scaler_update_lock(scl, FALSE)" */
	/*set_scaler_update_lock(scl, false);*/

	/* TODO: investigate purpose/need of SHOULD_UNLOCK */
	if (data->flags.bits.SHOULD_UNLOCK == false)
		set_coeff_update_complete(scl);

	return true;
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
	.get_optimal_taps_number = get_optimal_taps_number,
	.get_next_lower_taps_number = get_next_lower_taps_number,
	.get_viewport = get_viewport,
};

static bool scaler_v_dce110_construct(
	struct scaler *scl,
	struct scaler_init_data *init_data)
{
	enum controller_id id;

	if (!dal_scaler_construct(scl, init_data))
		return false;

	id = init_data->id;

	scl->regs = scl_underlay_regs[id - CONTROLLER_ID_UNDERLAY0];

	scl->funcs = &scaler_funcs;
	return true;
}

struct scaler *dal_scaler_v_dce110_create(
	struct scaler_init_data *init_data)
{
	struct scaler *scl = dal_alloc(sizeof(struct scaler));

	if (!scl)
		return NULL;

	if (!scaler_v_dce110_construct(scl, init_data))
		goto fail;

	return scl;
fail:
	dal_free(scl);
	return NULL;
}


