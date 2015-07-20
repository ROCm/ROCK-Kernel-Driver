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

#include "include/fixed31_32.h"
#include "include/fixed32_32.h"
#include "include/adapter_service_interface.h"
#include "include/logger_interface.h"

#include "grph_gamma_dce110.h"

#include "../lut_and_gamma_types.h"
#include "../grph_gamma.h"

enum gg_regs_idx {
	IDX_INPUT_GAMMA_CONTROL,
	IDX_DEGAMMA_CONTROL,
	IDX_REGAMMA_CONTROL,

	IDX_REGAMMA_CNTLA_START,
	IDX_REGAMMA_CNTLA_SLOPE,
	IDX_REGAMMA_CNTLA_END1,
	IDX_REGAMMA_CNTLA_END2,

	IDX_REGAMMA_CNTLA_REGION_0_1,
	IDX_REGAMMA_CNTLA_REGION_2_3,
	IDX_REGAMMA_CNTLA_REGION_4_5,
	IDX_REGAMMA_CNTLA_REGION_6_7,

	IDX_REGAMMA_CNTLA_REGION_8_9,
	IDX_REGAMMA_CNTLA_REGION_10_11,
	IDX_REGAMMA_CNTLA_REGION_12_13,
	IDX_REGAMMA_CNTLA_REGION_14_15,

	IDX_REGAMMA_LUT_DATA,
	IDX_REGAMMA_LUT_INDEX,
	IDX_REGAMMA_LUT_WRITE_EN_MASK,

	IDX_GRPH_CONTROL,

	IDX_DCFE_MEM_PWR_CTRL,
	IDX_DCFE_MEM_PWR_STATUS,

	GG_REGS_IDX_SIZE
};

#define regs_for_graphics_gamma(id)\
	[CONTROLLER_ID_D ## id - 1] = {\
[IDX_DEGAMMA_CONTROL] = mmDCP ## id ## _DEGAMMA_CONTROL,\
[IDX_REGAMMA_CONTROL] = mmDCP ## id ## _REGAMMA_CONTROL,\
\
[IDX_REGAMMA_CNTLA_START] = mmDCP ## id ## _REGAMMA_CNTLA_START_CNTL,\
[IDX_REGAMMA_CNTLA_SLOPE] = mmDCP ## id ## _REGAMMA_CNTLA_SLOPE_CNTL,\
[IDX_REGAMMA_CNTLA_END1] = mmDCP ## id ## _REGAMMA_CNTLA_END_CNTL1,\
[IDX_REGAMMA_CNTLA_END2] = mmDCP ## id ## _REGAMMA_CNTLA_END_CNTL2,\
\
[IDX_REGAMMA_CNTLA_REGION_0_1] = mmDCP ## id ## _REGAMMA_CNTLA_REGION_0_1,\
[IDX_REGAMMA_CNTLA_REGION_2_3] = mmDCP ## id ## _REGAMMA_CNTLA_REGION_2_3,\
[IDX_REGAMMA_CNTLA_REGION_4_5] = mmDCP ## id ## _REGAMMA_CNTLA_REGION_4_5,\
[IDX_REGAMMA_CNTLA_REGION_6_7] = mmDCP ## id ## _REGAMMA_CNTLA_REGION_6_7,\
\
[IDX_REGAMMA_CNTLA_REGION_8_9] = mmDCP ## id ## _REGAMMA_CNTLA_REGION_8_9,\
[IDX_REGAMMA_CNTLA_REGION_10_11] = mmDCP ## id ## _REGAMMA_CNTLA_REGION_10_11,\
[IDX_REGAMMA_CNTLA_REGION_12_13] = mmDCP ## id ## _REGAMMA_CNTLA_REGION_12_13,\
[IDX_REGAMMA_CNTLA_REGION_14_15] = mmDCP ## id ## _REGAMMA_CNTLA_REGION_14_15,\
\
[IDX_REGAMMA_LUT_DATA] = mmDCP ## id ## _REGAMMA_LUT_DATA,\
[IDX_REGAMMA_LUT_INDEX] = mmDCP ## id ## _REGAMMA_LUT_INDEX,\
[IDX_REGAMMA_LUT_WRITE_EN_MASK] = mmDCP ## id ## _REGAMMA_LUT_WRITE_EN_MASK,\
\
[IDX_GRPH_CONTROL] = mmDCP ## id ## _GRPH_CONTROL,\
[IDX_DCFE_MEM_PWR_CTRL] = mmDCFE ## id ## _DCFE_MEM_PWR_CTRL,\
[IDX_DCFE_MEM_PWR_STATUS] = mmDCFE ## id ## _DCFE_MEM_PWR_STATUS\
}

static const uint32_t gg_regs[][GG_REGS_IDX_SIZE] = {
	regs_for_graphics_gamma(0),
	regs_for_graphics_gamma(1),
	regs_for_graphics_gamma(2)
};

enum dce110_gg_legacy_regs_idx {
	IDX_LUT_30_COLOR,
	IDX_LUT_SEQ_COLOR,
	IDX_LUT_WRITE_EN_MASK,
	IDX_LUT_RW_MODE,
	IDX_LUT_RW_INDEX,
	IDX_LUT_PWL_DATA,

	IDX_GRPH_LUT_10BIT_BYPASS,

	IDX_PRESCALE_VALUES_GRPH_R,
	IDX_PRESCALE_VALUES_GRPH_G,
	IDX_PRESCALE_VALUES_GRPH_B,
	IDX_PRESCALE_GRPH_CONTROL,

	IDX_LUT_CONTROL,

	IDX_LUT_WHITE_OFFSET_RED,
	IDX_LUT_WHITE_OFFSET_GREEN,
	IDX_LUT_WHITE_OFFSET_BLUE,

	IDX_LUT_BLACK_OFFSET_RED,
	IDX_LUT_BLACK_OFFSET_GREEN,
	IDX_LUT_BLACK_OFFSET_BLUE,

	GG_dce110_LEGACY_REGS_IDX_SIZE
};

#define dce110_legacy_regs_for_graphics_gamma(id)\
	[CONTROLLER_ID_D ## id - 1] = {\
[IDX_LUT_30_COLOR] = mmDCP ## id ## _DC_LUT_30_COLOR,\
[IDX_LUT_SEQ_COLOR] = mmDCP ## id ## _DC_LUT_SEQ_COLOR,\
[IDX_LUT_WRITE_EN_MASK] = mmDCP ## id ## _DC_LUT_WRITE_EN_MASK,\
[IDX_LUT_RW_MODE] = mmDCP ## id ## _DC_LUT_RW_MODE,\
[IDX_LUT_RW_INDEX] = mmDCP ## id ## _DC_LUT_RW_INDEX,\
[IDX_LUT_PWL_DATA] = mmDCP ## id ## _DC_LUT_PWL_DATA,\
\
[IDX_GRPH_LUT_10BIT_BYPASS] = mmDCP ## id ## _GRPH_LUT_10BIT_BYPASS,\
\
[IDX_PRESCALE_VALUES_GRPH_R] = mmDCP ## id ## _PRESCALE_VALUES_GRPH_R,\
[IDX_PRESCALE_VALUES_GRPH_G] = mmDCP ## id ## _PRESCALE_VALUES_GRPH_G,\
[IDX_PRESCALE_VALUES_GRPH_B] = mmDCP ## id ## _PRESCALE_VALUES_GRPH_B,\
[IDX_PRESCALE_GRPH_CONTROL] = mmDCP ## id ## _PRESCALE_GRPH_CONTROL,\
\
[IDX_LUT_CONTROL] = mmDCP ## id ## _DC_LUT_CONTROL,\
\
[IDX_LUT_WHITE_OFFSET_RED] = mmDCP ## id ## _DC_LUT_WHITE_OFFSET_RED,\
[IDX_LUT_WHITE_OFFSET_GREEN] = mmDCP ## id ## _DC_LUT_WHITE_OFFSET_GREEN,\
[IDX_LUT_WHITE_OFFSET_BLUE] = mmDCP ## id ## _DC_LUT_WHITE_OFFSET_BLUE,\
\
[IDX_LUT_BLACK_OFFSET_RED] = mmDCP ## id ## _DC_LUT_BLACK_OFFSET_RED,\
[IDX_LUT_BLACK_OFFSET_GREEN] = mmDCP ## id ## _DC_LUT_BLACK_OFFSET_GREEN,\
[IDX_LUT_BLACK_OFFSET_BLUE] = mmDCP ## id ## _DC_LUT_BLACK_OFFSET_BLUE,\
}

static const uint32_t
dce110_gg_legacy_regs[][GG_dce110_LEGACY_REGS_IDX_SIZE] = {
	dce110_legacy_regs_for_graphics_gamma(0),
	dce110_legacy_regs_for_graphics_gamma(1),
	dce110_legacy_regs_for_graphics_gamma(2)
};

#define FROM_GRAPH_GAMMA(ptr)\
	(container_of((ptr), struct grph_gamma_dce110, base))

/*
 * set_legacy_mode
 *  uses HW in DCE40 manner
 */
static void set_legacy_mode(struct grph_gamma *gg, bool is_legacy)
{
	const uint32_t addr = gg->regs[IDX_INPUT_GAMMA_CONTROL];
	uint32_t value = dal_read_reg(gg->ctx, addr);

	set_reg_field_value(
		value,
		is_legacy,
		INPUT_GAMMA_CONTROL,
		GRPH_INPUT_GAMMA_MODE);

	dal_write_reg(gg->ctx, addr, value);
}

static bool setup_distribution_points(
	struct grph_gamma *gg)
{
	uint32_t hw_points_num = MAX_PWL_ENTRY * 2;

	struct curve_config cfg;

	cfg.offset = 0;

		cfg.segments[0] = 3;
		cfg.segments[1] = 4;
		cfg.segments[2] = 4;
		cfg.segments[3] = 4;
		cfg.segments[4] = 4;
		cfg.segments[5] = 4;
		cfg.segments[6] = 4;
		cfg.segments[7] = 4;
		cfg.segments[8] = 5;
		cfg.segments[9] = 5;
		cfg.segments[10] = 0;
		cfg.segments[11] = -1;
		cfg.segments[12] = -1;
		cfg.segments[13] = -1;
		cfg.segments[14] = -1;
		cfg.segments[15] = -1;

	cfg.begin = -10;

	if (!dal_grph_gamma_build_hw_curve_configuration(
		&cfg, gg->arr_curve_points, gg->arr_points,
		gg->coordinates_x, &hw_points_num)) {
		ASSERT_CRITICAL(false);
		return false;
	}

	gg->hw_points_num = hw_points_num;

	return true;
}

static void program_black_offsets(
	struct grph_gamma *gg,
	struct dev_c_lut16 *offset)
{
	dal_write_reg(gg->ctx,
		FROM_GRAPH_GAMMA(gg)->
		legacy_regs[IDX_LUT_BLACK_OFFSET_RED], offset->red);
	dal_write_reg(gg->ctx,
		FROM_GRAPH_GAMMA(gg)->
		legacy_regs[IDX_LUT_BLACK_OFFSET_GREEN], offset->green);
	dal_write_reg(gg->ctx,
		FROM_GRAPH_GAMMA(gg)->
		legacy_regs[IDX_LUT_BLACK_OFFSET_BLUE], offset->blue);
}

static void program_white_offsets(
	struct grph_gamma *gg,
	struct dev_c_lut16 *offset)
{
	dal_write_reg(gg->ctx,
		FROM_GRAPH_GAMMA(gg)->
		legacy_regs[IDX_LUT_WHITE_OFFSET_RED], offset->red);
	dal_write_reg(gg->ctx,
		FROM_GRAPH_GAMMA(gg)->
		legacy_regs[IDX_LUT_WHITE_OFFSET_GREEN], offset->green);
	dal_write_reg(gg->ctx,
		FROM_GRAPH_GAMMA(gg)->
		legacy_regs[IDX_LUT_WHITE_OFFSET_BLUE], offset->blue);
}

/*
 *****************************************************************************
 *  Function: configure_degamma_mode
 *
 *program regamma block, using ROM_X, predefined coeff.
 *if FP16 - by pass
 *
 *@param [in ] parameters interface parameters
 *@return void
 *
 *@note
 *@see
 *
 *****************************************************************************
 */
static void configure_degamma_mode(
	struct grph_gamma *gg,
	const struct gamma_parameters *parameters,
	bool force_bypass)
{
	uint32_t value;
	const uint32_t addr = gg->regs[IDX_DEGAMMA_CONTROL];

	/* 1 -RGB 2.4
	 * 2 -YCbCr 2.22 */

	uint32_t degamma_type =
		parameters->regamma.features.bits.GRAPHICS_DEGAMMA_SRGB == 1 ?
			1 : 2;

	value = dal_read_reg(gg->ctx, addr);

	/* if by pass - no degamma
	 * when legacy and regamma LUT's we do degamma */
	if (parameters->degamma_adjust_type == GRAPHICS_DEGAMMA_ADJUST_BYPASS ||
		(parameters->surface_pixel_format == PIXEL_FORMAT_FP16 &&
			parameters->selected_gamma_lut ==
				GRAPHICS_GAMMA_LUT_REGAMMA))
		degamma_type = 0;

	if (force_bypass)
		degamma_type = 0;

	set_reg_field_value(
		value,
		degamma_type,
		DEGAMMA_CONTROL,
		GRPH_DEGAMMA_MODE);

	set_reg_field_value(
		value,
		degamma_type,
		DEGAMMA_CONTROL,
		CURSOR_DEGAMMA_MODE);

	set_reg_field_value(
		value,
		degamma_type,
		DEGAMMA_CONTROL,
		CURSOR2_DEGAMMA_MODE);

	dal_write_reg(gg->ctx, addr, value);
}

/*
 *****************************************************************************
 *  Function: regamma_config_regions_and_segments
 *
 *     build regamma curve by using predefined hw points
 *     uses interface parameters ,like EDID coeff.
 *
 * @param   : parameters   interface parameters
 *  @return void
 *
 *  @note
 *
 *  @see
 *
 *****************************************************************************
 */
static void regamma_config_regions_and_segments(
	struct grph_gamma *gg)
{
	struct gamma_curve *curve;
	uint32_t value = 0;

	{
		set_reg_field_value(
			value,
			gg->arr_points[0].custom_float_x,
			REGAMMA_CNTLA_START_CNTL,
			REGAMMA_CNTLA_EXP_REGION_START);

		set_reg_field_value(
			value,
			0,
			REGAMMA_CNTLA_START_CNTL,
			REGAMMA_CNTLA_EXP_REGION_START_SEGMENT);

		dal_write_reg(gg->ctx,
			gg->regs[IDX_REGAMMA_CNTLA_START], value);
	}
	{
		value = 0;
		set_reg_field_value(
			value,
			gg->arr_points[0].custom_float_slope,
			REGAMMA_CNTLA_SLOPE_CNTL,
			REGAMMA_CNTLA_EXP_REGION_LINEAR_SLOPE);

		dal_write_reg(gg->ctx,
			gg->regs[IDX_REGAMMA_CNTLA_SLOPE], value);
	}
	{
		value = 0;
		set_reg_field_value(
			value,
			gg->arr_points[1].custom_float_x,
			REGAMMA_CNTLA_END_CNTL1,
			REGAMMA_CNTLA_EXP_REGION_END);

		dal_write_reg(gg->ctx,
			gg->regs[IDX_REGAMMA_CNTLA_END1], value);
	}
	{
		value = 0;
		set_reg_field_value(
			value,
			gg->arr_points[2].custom_float_slope,
			REGAMMA_CNTLA_END_CNTL2,
			REGAMMA_CNTLA_EXP_REGION_END_BASE);

		set_reg_field_value(
			value,
			gg->arr_points[1].custom_float_y,
			REGAMMA_CNTLA_END_CNTL2,
			REGAMMA_CNTLA_EXP_REGION_END_SLOPE);

		dal_write_reg(gg->ctx,
			gg->regs[IDX_REGAMMA_CNTLA_END2], value);
	}

	curve = gg->arr_curve_points;

	{
		value = 0;
		set_reg_field_value(
			value,
			curve[0].offset,
			REGAMMA_CNTLA_REGION_0_1,
			REGAMMA_CNTLA_EXP_REGION0_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[0].segments_num,
			REGAMMA_CNTLA_REGION_0_1,
			REGAMMA_CNTLA_EXP_REGION0_NUM_SEGMENTS);

		set_reg_field_value(
			value,
			curve[1].offset,
			REGAMMA_CNTLA_REGION_0_1,
			REGAMMA_CNTLA_EXP_REGION1_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[1].segments_num,
			REGAMMA_CNTLA_REGION_0_1,
			REGAMMA_CNTLA_EXP_REGION1_NUM_SEGMENTS);

		dal_write_reg(
			gg->ctx,
			gg->regs[IDX_REGAMMA_CNTLA_REGION_0_1],
			value);
	}

	curve += 2;
	{
		value = 0;
		set_reg_field_value(
			value,
			curve[0].offset,
			REGAMMA_CNTLA_REGION_2_3,
			REGAMMA_CNTLA_EXP_REGION2_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[0].segments_num,
			REGAMMA_CNTLA_REGION_2_3,
			REGAMMA_CNTLA_EXP_REGION2_NUM_SEGMENTS);

		set_reg_field_value(
			value,
			curve[1].offset,
			REGAMMA_CNTLA_REGION_2_3,
			REGAMMA_CNTLA_EXP_REGION3_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[1].segments_num,
			REGAMMA_CNTLA_REGION_2_3,
			REGAMMA_CNTLA_EXP_REGION3_NUM_SEGMENTS);

		dal_write_reg(gg->ctx,
			gg->regs[IDX_REGAMMA_CNTLA_REGION_2_3],
			value);
	}

	curve += 2;
	{
		value = 0;
		set_reg_field_value(
			value,
			curve[0].offset,
			REGAMMA_CNTLA_REGION_4_5,
			REGAMMA_CNTLA_EXP_REGION4_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[0].segments_num,
			REGAMMA_CNTLA_REGION_4_5,
			REGAMMA_CNTLA_EXP_REGION4_NUM_SEGMENTS);

		set_reg_field_value(
			value,
			curve[1].offset,
			REGAMMA_CNTLA_REGION_4_5,
			REGAMMA_CNTLA_EXP_REGION5_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[1].segments_num,
			REGAMMA_CNTLA_REGION_4_5,
			REGAMMA_CNTLA_EXP_REGION5_NUM_SEGMENTS);

		dal_write_reg(gg->ctx,
			gg->regs[IDX_REGAMMA_CNTLA_REGION_4_5],
			value);
	}

	curve += 2;
	{
		value = 0;
		set_reg_field_value(
			value,
			curve[0].offset,
			REGAMMA_CNTLA_REGION_6_7,
			REGAMMA_CNTLA_EXP_REGION6_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[0].segments_num,
			REGAMMA_CNTLA_REGION_6_7,
			REGAMMA_CNTLA_EXP_REGION6_NUM_SEGMENTS);

		set_reg_field_value(
			value,
			curve[1].offset,
			REGAMMA_CNTLA_REGION_6_7,
			REGAMMA_CNTLA_EXP_REGION7_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[1].segments_num,
			REGAMMA_CNTLA_REGION_6_7,
			REGAMMA_CNTLA_EXP_REGION7_NUM_SEGMENTS);

		dal_write_reg(gg->ctx,
			gg->regs[IDX_REGAMMA_CNTLA_REGION_6_7],
			value);
	}

	curve += 2;
	{
		value = 0;
		set_reg_field_value(
			value,
			curve[0].offset,
			REGAMMA_CNTLA_REGION_8_9,
			REGAMMA_CNTLA_EXP_REGION8_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[0].segments_num,
			REGAMMA_CNTLA_REGION_8_9,
			REGAMMA_CNTLA_EXP_REGION8_NUM_SEGMENTS);

		set_reg_field_value(
			value,
			curve[1].offset,
			REGAMMA_CNTLA_REGION_8_9,
			REGAMMA_CNTLA_EXP_REGION9_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[1].segments_num,
			REGAMMA_CNTLA_REGION_8_9,
			REGAMMA_CNTLA_EXP_REGION9_NUM_SEGMENTS);

		dal_write_reg(gg->ctx,
			gg->regs[IDX_REGAMMA_CNTLA_REGION_8_9],
			value);
	}

	curve += 2;
	{
		value = 0;
		set_reg_field_value(
			value,
			curve[0].offset,
			REGAMMA_CNTLA_REGION_10_11,
			REGAMMA_CNTLA_EXP_REGION10_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[0].segments_num,
			REGAMMA_CNTLA_REGION_10_11,
			REGAMMA_CNTLA_EXP_REGION10_NUM_SEGMENTS);

		set_reg_field_value(
			value,
			curve[1].offset,
			REGAMMA_CNTLA_REGION_10_11,
			REGAMMA_CNTLA_EXP_REGION11_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[1].segments_num,
			REGAMMA_CNTLA_REGION_10_11,
			REGAMMA_CNTLA_EXP_REGION11_NUM_SEGMENTS);

		dal_write_reg(gg->ctx,
			gg->regs[IDX_REGAMMA_CNTLA_REGION_10_11],
			value);
	}

	curve += 2;
	{
		value = 0;
		set_reg_field_value(
			value,
			curve[0].offset,
			REGAMMA_CNTLA_REGION_12_13,
			REGAMMA_CNTLA_EXP_REGION12_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[0].segments_num,
			REGAMMA_CNTLA_REGION_12_13,
			REGAMMA_CNTLA_EXP_REGION12_NUM_SEGMENTS);

		set_reg_field_value(
			value,
			curve[1].offset,
			REGAMMA_CNTLA_REGION_12_13,
			REGAMMA_CNTLA_EXP_REGION13_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[1].segments_num,
			REGAMMA_CNTLA_REGION_12_13,
			REGAMMA_CNTLA_EXP_REGION13_NUM_SEGMENTS);

		dal_write_reg(gg->ctx,
			gg->regs[IDX_REGAMMA_CNTLA_REGION_12_13],
			value);
	}

	curve += 2;
	{
		value = 0;
		set_reg_field_value(
			value,
			curve[0].offset,
			REGAMMA_CNTLA_REGION_14_15,
			REGAMMA_CNTLA_EXP_REGION14_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[0].segments_num,
			REGAMMA_CNTLA_REGION_14_15,
			REGAMMA_CNTLA_EXP_REGION14_NUM_SEGMENTS);

		set_reg_field_value(
			value,
			curve[1].offset,
			REGAMMA_CNTLA_REGION_14_15,
			REGAMMA_CNTLA_EXP_REGION15_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[1].segments_num,
			REGAMMA_CNTLA_REGION_14_15,
			REGAMMA_CNTLA_EXP_REGION15_NUM_SEGMENTS);

		dal_write_reg(gg->ctx,
			gg->regs[IDX_REGAMMA_CNTLA_REGION_14_15],
			value);
	}
}

static void configure_regamma_mode(
	struct grph_gamma *gg,
	const struct gamma_parameters *params,
	bool force_bypass)
{
	const uint32_t addr = gg->regs[IDX_REGAMMA_CONTROL];

	enum wide_gamut_regamma_mode mode =
		WIDE_GAMUT_REGAMMA_MODE_GRAPHICS_MATRIX_A;

	uint32_t value = dal_read_reg(gg->ctx, addr);

	if (force_bypass) {

		set_reg_field_value(
			value,
			0,
			REGAMMA_CONTROL,
			GRPH_REGAMMA_MODE);

		dal_write_reg(gg->ctx, addr, value);

		return;
	}

	if (params->regamma_adjust_type == GRAPHICS_REGAMMA_ADJUST_BYPASS)
		mode = WIDE_GAMUT_REGAMMA_MODE_GRAPHICS_BYPASS;
	else if (params->regamma_adjust_type == GRAPHICS_REGAMMA_ADJUST_HW) {
		if (params->surface_pixel_format == PIXEL_FORMAT_FP16)
			mode = WIDE_GAMUT_REGAMMA_MODE_GRAPHICS_BYPASS;
		else
			mode = WIDE_GAMUT_REGAMMA_MODE_GRAPHICS_SRGB24;
	}

	switch (mode) {
	case WIDE_GAMUT_REGAMMA_MODE_GRAPHICS_BYPASS:
		set_reg_field_value(
			value,
			0,
			REGAMMA_CONTROL,
			GRPH_REGAMMA_MODE);
		break;
	case WIDE_GAMUT_REGAMMA_MODE_GRAPHICS_SRGB24:
		set_reg_field_value(
			value,
			1,
			REGAMMA_CONTROL,
			GRPH_REGAMMA_MODE);
		break;
	case WIDE_GAMUT_REGAMMA_MODE_GRAPHICS_XYYCC22:
		set_reg_field_value(
			value,
			2,
			REGAMMA_CONTROL,
			GRPH_REGAMMA_MODE);
		break;
	case WIDE_GAMUT_REGAMMA_MODE_GRAPHICS_MATRIX_A:
		set_reg_field_value(
			value,
			3,
			REGAMMA_CONTROL,
			GRPH_REGAMMA_MODE);
		break;
	case WIDE_GAMUT_REGAMMA_MODE_GRAPHICS_MATRIX_B:
		set_reg_field_value(
			value,
			4,
			REGAMMA_CONTROL,
			GRPH_REGAMMA_MODE);
		break;
	default:
		break;
	}

	dal_write_reg(gg->ctx, addr, value);
}

static void program_pwl(
	struct grph_gamma *gg,
	const struct gamma_parameters *params)
{
	uint32_t value;

	{
		uint8_t max_tries = 10;
		uint8_t counter = 0;

		/* Power on LUT memory */
		value = dal_read_reg(gg->ctx, gg->regs[IDX_DCFE_MEM_PWR_CTRL]);

		set_reg_field_value(
			value,
			1,
			DCFE_MEM_PWR_CTRL,
			DCP_REGAMMA_MEM_PWR_DIS);

		dal_write_reg(gg->ctx, gg->regs[IDX_DCFE_MEM_PWR_CTRL], value);

		while (counter < max_tries) {
			value =
				dal_read_reg(
					gg->ctx,
					gg->regs[IDX_DCFE_MEM_PWR_STATUS]);

			if (get_reg_field_value(
				value,
				DCFE_MEM_PWR_STATUS,
				DCP_REGAMMA_MEM_PWR_STATE) == 0)
				break;

			++counter;
		}

		if (counter == max_tries) {
			dal_logger_write(gg->ctx->logger,
				LOG_MAJOR_WARNING,
				LOG_MINOR_COMPONENT_CONTROLLER,
				"%s: regamma lut was not powered on in a timely manner, programming still proceeds\n",
				__func__);
		}
	}

	value = 0;

	set_reg_field_value(
		value,
		7,
		REGAMMA_LUT_WRITE_EN_MASK,
		REGAMMA_LUT_WRITE_EN_MASK);

	dal_write_reg(gg->ctx,
		gg->regs[IDX_REGAMMA_LUT_WRITE_EN_MASK], value);
	dal_write_reg(gg->ctx,
		gg->regs[IDX_REGAMMA_LUT_INDEX], 0);

	/* Program REGAMMA_LUT_DATA */
	{
		const uint32_t addr = gg->regs[IDX_REGAMMA_LUT_DATA];

		uint32_t i = 0;

		struct pwl_result_data *rgb = gg->rgb_resulted;

		while (i != gg->hw_points_num) {
			dal_write_reg(gg->ctx, addr, rgb->red_reg);
			dal_write_reg(gg->ctx, addr, rgb->green_reg);
			dal_write_reg(gg->ctx, addr, rgb->blue_reg);

			dal_write_reg(gg->ctx, addr, rgb->delta_red_reg);
			dal_write_reg(gg->ctx, addr, rgb->delta_green_reg);
			dal_write_reg(gg->ctx, addr, rgb->delta_blue_reg);

			++rgb;
			++i;
		}
	}

	/*  we are done with DCP LUT memory; re-enable low power mode */
	value = dal_read_reg(gg->ctx, gg->regs[IDX_DCFE_MEM_PWR_CTRL]);

	set_reg_field_value(
		value,
		0,
		DCFE_MEM_PWR_CTRL,
		DCP_REGAMMA_MEM_PWR_DIS);

	dal_write_reg(gg->ctx, gg->regs[IDX_DCFE_MEM_PWR_CTRL], value);
}


/*
 *****************************************************************************
 *  Function: set_gamma_ramp
 *
 *     main interface method which operates the helper functions
 *     calculates and programs hardware blocks : degamma and regamma.
 *
 *  @param [in ] gamma_ramp given gamma
 *  @param [in ] params given describe the gamma
 *
 *  @return true if success
 *
 *  @note
 *
 *  @see
 *
 *****************************************************************************
 */
static bool set_gamma_ramp(
	struct grph_gamma *gg,
	const struct gamma_ramp *gamma_ramp,
	const struct gamma_parameters *params)
{
	bool use_palette;

	{
		/*Power on LUT memory*/
		uint32_t value =
			dal_read_reg(gg->ctx, gg->regs[IDX_DCFE_MEM_PWR_CTRL]);

		set_reg_field_value(
			value,
			1,
			DCFE_MEM_PWR_CTRL,
			DCP_REGAMMA_MEM_PWR_DIS);

		set_reg_field_value(
			value,
			1,
			DCFE_MEM_PWR_CTRL,
			DCP_LUT_MEM_PWR_DIS);

		dal_write_reg(gg->ctx, gg->regs[IDX_DCFE_MEM_PWR_CTRL], value);
	}

	/* we can go to new DCP or legacy Old DCP */
	if (params->surface_pixel_format == PIXEL_FORMAT_INDEX8 ||
		params->selected_gamma_lut == GRAPHICS_GAMMA_LUT_LEGACY) {
		/* do legacy DCP for 256 colors if we are requested to do so */
		gg->funcs->set_gamma_ramp_legacy(
			gg, gamma_ramp, params);

		/* set bypass */
		gg->funcs->program_prescale_legacy(
			gg, PIXEL_FORMAT_UNINITIALIZED);

		gg->funcs->set_legacy_mode(gg, true);

		configure_degamma_mode(gg, params, true);

		configure_regamma_mode(gg, params, true);

		return true;
	} else if (params->selected_gamma_lut == GRAPHICS_GAMMA_LUT_REGAMMA) {
		gg->funcs->program_prescale_legacy(
			gg, params->surface_pixel_format);

		gg->funcs->set_legacy_mode(gg, false);
	}

	use_palette = params->surface_pixel_format == PIXEL_FORMAT_INDEX8;

	/* 1. Scale gamma to 0 - 1 to m_pRgbUser */
	if (gamma_ramp->type == GAMMA_RAMP_RBG256X3X16)
		dal_grph_gamma_scale_rgb256x3x16(gg, use_palette,
			&gamma_ramp->gamma_ramp_rgb256x3x16);
	else
		dal_grph_gamma_scale_dx(gg, params->surface_pixel_format,
			gamma_ramp->gamma_ramp_dxgi1.gamma_curve);

	/*
	 * 2. Do degamma step : remove the given gamma value from FB.
	 * For FP16 or no degamma do by pass
	 */
	configure_degamma_mode(gg, params, false);

	/* 3. Configure regamma curve without analysis (future task) */
	/*    and program the PWL regions and segments */
	if (params->regamma_adjust_type == GRAPHICS_REGAMMA_ADJUST_SW ||
		params->surface_pixel_format == PIXEL_FORMAT_FP16) {

		/* 4. Setup x exponentially distributed points */
		if (!gg->funcs->setup_distribution_points(gg)) {
			ASSERT_CRITICAL(false);
			/* invalid option */
			return false;
		}

		/* 5. Build ideal regamma curve */
		if (!dal_grph_gamma_build_regamma_curve(gg, params)) {
			ASSERT_CRITICAL(false);
			/* invalid parameters or bug */
			return false;
		}

		/* 6. Map user gamma (evenly distributed x points) to new curve
		 * when x is y from ideal regamma , step 5 */
		if (!dal_grph_gamma_map_regamma_hw_to_x_user(
			gg, gamma_ramp, params)) {
			ASSERT_CRITICAL(false);
			/* invalid parameters or bug */
			return false;
		}
		/* 7.Build and verify resulted curve */
		dal_grph_gamma_build_new_custom_resulted_curve(gg, params);

		/* 8. Build and translate x to hw format */
		if (!dal_grph_gamma_rebuild_curve_configuration_magic(gg)) {
			ASSERT_CRITICAL(false);
			/* invalid parameters or bug */
			return false;
		}

		/* 9 convert all parameters to the custom float format */
		if (!dal_grph_gamma_convert_to_custom_float(gg)) {
			ASSERT_CRITICAL(false);
			/* invalid parameters or bug */
			return false;
		}

		/* 10 program regamma curve configuration */
		regamma_config_regions_and_segments(gg);

		/* 11. Programm PWL */
		program_pwl(gg, params);
	}

	/*
	 * 12. program regamma config
	 */
	configure_regamma_mode(gg, params, false);

	{
		/*re-enable low power mode for LUT memory*/
		uint32_t value = dal_read_reg(gg->ctx, mmDCFE_MEM_PWR_CTRL);

		set_reg_field_value(
			value,
			0,
			DCFE_MEM_PWR_CTRL,
			DCP_REGAMMA_MEM_PWR_DIS);

		set_reg_field_value(
			value,
			0,
			DCFE_MEM_PWR_CTRL,
			DCP_LUT_MEM_PWR_DIS);

		dal_write_reg(gg->ctx, mmDCFE_MEM_PWR_CTRL, value);
	}

	return true;
}

static void program_black_white_offset(
	struct grph_gamma_dce110 *gg,
	enum pixel_format surface_pixel_format)
{
	struct dev_c_lut16 black_offset;
	struct dev_c_lut16 white_offset;

	/* get black offset */

	switch (surface_pixel_format) {
	case PIXEL_FORMAT_FP16:
		/* sRGB gamut, [0.0...1.0] */
		black_offset.red = 0;
		black_offset.green = 0;
		black_offset.blue = 0;
	break;

	case PIXEL_FORMAT_ARGB2101010_XRBIAS:
		/* [-1.0...3.0] */
		black_offset.red = 0x100;
		black_offset.green = 0x100;
		black_offset.blue = 0x100;
	break;

	default:
		black_offset.red = 0;
		black_offset.green = 0;
		black_offset.blue = 0;
	}

	/* get white offset */

	switch (surface_pixel_format) {
	case PIXEL_FORMAT_FP16:
		white_offset.red = 0x3BFF;
		white_offset.green = 0x3BFF;
		white_offset.blue = 0x3BFF;
	break;

	case PIXEL_FORMAT_ARGB2101010_XRBIAS:
		white_offset.red = 0x37E;
		white_offset.green = 0x37E;
		white_offset.blue = 0x37E;
		break;

	case PIXEL_FORMAT_ARGB8888:
		white_offset.red = 0xFF;
		white_offset.green = 0xFF;
		white_offset.blue = 0xFF;
		break;

	default:
		white_offset.red = 0x3FF;
		white_offset.green = 0x3FF;
		white_offset.blue = 0x3FF;
	}

	gg->base.funcs->program_black_offsets(&gg->base, &black_offset);
	gg->base.funcs->program_white_offsets(&gg->base, &white_offset);
}

static void set_lut_inc(
	struct grph_gamma *gg,
	uint8_t inc,
	bool is_float,
	bool is_signed)
{
	const uint32_t addr =
		FROM_GRAPH_GAMMA(gg)->legacy_regs[IDX_LUT_CONTROL];

	uint32_t value = dal_read_reg(gg->ctx, addr);

	set_reg_field_value(
		value,
		inc,
		DC_LUT_CONTROL,
		DC_LUT_INC_R);

	set_reg_field_value(
		value,
		inc,
		DC_LUT_CONTROL,
		DC_LUT_INC_G);

	set_reg_field_value(
		value,
		inc,
		DC_LUT_CONTROL,
		DC_LUT_INC_B);

	set_reg_field_value(
		value,
		is_float,
		DC_LUT_CONTROL,
		DC_LUT_DATA_R_FLOAT_POINT_EN);

	set_reg_field_value(
		value,
		is_float,
		DC_LUT_CONTROL,
		DC_LUT_DATA_G_FLOAT_POINT_EN);

	set_reg_field_value(
		value,
		is_float,
		DC_LUT_CONTROL,
		DC_LUT_DATA_B_FLOAT_POINT_EN);

	set_reg_field_value(
		value,
		is_signed,
		DC_LUT_CONTROL,
		DC_LUT_DATA_R_SIGNED_EN);

	set_reg_field_value(
		value,
		is_signed,
		DC_LUT_CONTROL,
		DC_LUT_DATA_G_SIGNED_EN);

	set_reg_field_value(
		value,
		is_signed,
		DC_LUT_CONTROL,
		DC_LUT_DATA_B_SIGNED_EN);

	dal_write_reg(gg->ctx, addr, value);
}

static void select_lut(
	struct grph_gamma *gg)
{
	uint32_t value = 0;

	set_lut_inc(gg, 0, false, false);

	{
		const uint32_t addr =
			FROM_GRAPH_GAMMA(gg)->
			legacy_regs[IDX_LUT_WRITE_EN_MASK];

		value = dal_read_reg(gg->ctx, addr);

		/* enable all */
		set_reg_field_value(
			value,
			0x7,
			DC_LUT_WRITE_EN_MASK,
			DC_LUT_WRITE_EN_MASK);

		dal_write_reg(gg->ctx, addr, value);
	}

	{
		const uint32_t addr =
			FROM_GRAPH_GAMMA(gg)->
			legacy_regs[IDX_LUT_RW_MODE];

		value = dal_read_reg(gg->ctx, addr);

		set_reg_field_value(
			value,
			0,
			DC_LUT_RW_MODE,
			DC_LUT_RW_MODE);

		dal_write_reg(gg->ctx, addr, value);
	}

	{
		const uint32_t addr =
			FROM_GRAPH_GAMMA(gg)->
			legacy_regs[IDX_LUT_CONTROL];

		value = dal_read_reg(gg->ctx, addr);

		/* 00 - new u0.12 */
		set_reg_field_value(
			value,
			3,
			DC_LUT_CONTROL,
			DC_LUT_DATA_R_FORMAT);

		set_reg_field_value(
			value,
			3,
			DC_LUT_CONTROL,
			DC_LUT_DATA_G_FORMAT);

		set_reg_field_value(
			value,
			3,
			DC_LUT_CONTROL,
			DC_LUT_DATA_B_FORMAT);

		dal_write_reg(gg->ctx, addr, value);
	}

	{
		const uint32_t addr =
			FROM_GRAPH_GAMMA(gg)->
			legacy_regs[IDX_LUT_RW_INDEX];

		value = dal_read_reg(gg->ctx, addr);

		set_reg_field_value(
			value,
			0,
			DC_LUT_RW_INDEX,
			DC_LUT_RW_INDEX);

		dal_write_reg(gg->ctx, addr, value);
	}
}

static void program_lut_gamma(
	struct grph_gamma *gg,
	const struct dev_c_lut16 *gamma,
	const struct gamma_parameters *params)
{
	uint32_t i = 0;
	uint32_t value = 0;

	{
		uint8_t max_tries = 10;
		uint8_t counter = 0;

		/* Power on LUT memory */
		value = dal_read_reg(gg->ctx, gg->regs[IDX_DCFE_MEM_PWR_CTRL]);

		set_reg_field_value(
			value,
			1,
			DCFE_MEM_PWR_CTRL,
			DCP_REGAMMA_MEM_PWR_DIS);

		dal_write_reg(gg->ctx, gg->regs[IDX_DCFE_MEM_PWR_CTRL], value);

		while (counter < max_tries) {
			value =
				dal_read_reg(
					gg->ctx,
					gg->regs[IDX_DCFE_MEM_PWR_STATUS]);

			if (get_reg_field_value(
				value,
				DCFE_MEM_PWR_STATUS,
				DCP_REGAMMA_MEM_PWR_STATE) == 0)
				break;

			++counter;
		}

		if (counter == max_tries) {
			dal_logger_write(gg->ctx->logger,
				LOG_MAJOR_WARNING,
				LOG_MINOR_COMPONENT_CONTROLLER,
				"%s: regamma lut was not powered on in a timely manner, programming still proceeds\n",
				__func__);
		}
	}

	program_black_white_offset(
		FROM_GRAPH_GAMMA(gg), params->surface_pixel_format);

	select_lut(gg);

	if (params->surface_pixel_format == PIXEL_FORMAT_INDEX8) {
		const uint32_t addr =
			FROM_GRAPH_GAMMA(gg)->
			legacy_regs[IDX_LUT_SEQ_COLOR];

		do {
			struct dev_c_lut *index = gg->saved_palette + i;

			set_reg_field_value(
				value,
				gamma[index->red].red,
				DC_LUT_SEQ_COLOR,
				DC_LUT_SEQ_COLOR);
			dal_write_reg(gg->ctx, addr, value);


			set_reg_field_value(
				value,
				gamma[index->green].green,
				DC_LUT_SEQ_COLOR,
				DC_LUT_SEQ_COLOR);
			dal_write_reg(gg->ctx, addr, value);


			set_reg_field_value(
				value,
				gamma[index->blue].blue,
				DC_LUT_SEQ_COLOR,
				DC_LUT_SEQ_COLOR);
			dal_write_reg(gg->ctx, addr, value);

			++i;
		} while (i != RGB_256X3X16);
	} else {
		const uint32_t addr =
			FROM_GRAPH_GAMMA(gg)->
			legacy_regs[IDX_LUT_SEQ_COLOR];

		do {
			set_reg_field_value(
				value,
				gamma[i].red,
				DC_LUT_SEQ_COLOR,
				DC_LUT_SEQ_COLOR);
			dal_write_reg(gg->ctx, addr, value);


			set_reg_field_value(
				value,
				gamma[i].green,
				DC_LUT_SEQ_COLOR,
				DC_LUT_SEQ_COLOR);
			dal_write_reg(gg->ctx, addr, value);


			set_reg_field_value(
				value,
				gamma[i].blue,
				DC_LUT_SEQ_COLOR,
				DC_LUT_SEQ_COLOR);
			dal_write_reg(gg->ctx, addr, value);

			++i;
		} while (i != RGB_256X3X16);
	}

	/*  we are done with DCP LUT memory; re-enable low power mode */
	value = dal_read_reg(gg->ctx, gg->regs[IDX_DCFE_MEM_PWR_CTRL]);

	set_reg_field_value(
		value,
		0,
		DCFE_MEM_PWR_CTRL,
		DCP_REGAMMA_MEM_PWR_DIS);

	dal_write_reg(gg->ctx, gg->regs[IDX_DCFE_MEM_PWR_CTRL], value);
}

static bool set_gamma_ramp_legacy_rgb256x3x16(
	struct grph_gamma *gg,
	const struct gamma_ramp *gamma_ramp,
	const struct gamma_parameters *params)
{
	struct dev_c_lut16 *gamma16 =
		dal_alloc(sizeof(struct dev_c_lut16) * MAX_LUT_ENTRY);

	if (!gamma16)
		return false;

	dal_grph_gamma_convert_256_lut_entries_to_gxo_format(
		&gamma_ramp->gamma_ramp_rgb256x3x16, gamma16);

	if ((params->surface_pixel_format != PIXEL_FORMAT_ARGB2101010) &&
		(params->surface_pixel_format !=
			PIXEL_FORMAT_ARGB2101010_XRBIAS) &&
		(params->surface_pixel_format != PIXEL_FORMAT_FP16)) {
		gg->funcs->program_lut_gamma(gg, gamma16, params);
		dal_free(gamma16);
		return true;
	}

	/* TODO process DirectX-specific formats*/
	dal_free(gamma16);
	return false;
}

static bool set_gamma_ramp_legacy_dxgi1(
	struct grph_gamma *gg,
	const struct gamma_ramp *gamma_ramp,
	const struct gamma_parameters *params)
{
	struct dev_c_lut16 *gamma16 =
		dal_alloc(sizeof(struct dev_c_lut16) * MAX_LUT_ENTRY);

	if (!gamma16)
		return false;

	if ((params->surface_pixel_format != PIXEL_FORMAT_ARGB2101010) &&
		(params->surface_pixel_format !=
			PIXEL_FORMAT_ARGB2101010_XRBIAS) &&
		(params->surface_pixel_format != PIXEL_FORMAT_FP16)) {
		dal_grph_gamma_convert_udx_gamma_entries_to_gxo_format(
			&gamma_ramp->gamma_ramp_dxgi1, gamma16);
		gg->funcs->program_lut_gamma(gg, gamma16, params);
		dal_free(gamma16);
		return true;
	}

	/* TODO process DirectX-specific formats*/
	dal_free(gamma16);
	return false;
}

static bool set_gamma_ramp_legacy(
	struct grph_gamma *gg,
	const struct gamma_ramp *gamma_ramp,
	const struct gamma_parameters *params)
{
	switch (gamma_ramp->type) {
	case GAMMA_RAMP_RBG256X3X16:
		return set_gamma_ramp_legacy_rgb256x3x16(
			gg, gamma_ramp, params);
	case GAMMA_RAMP_DXGI_1:
		return set_gamma_ramp_legacy_dxgi1(
			gg, gamma_ramp, params);
	default:
		ASSERT_CRITICAL(false);
		return false;
	}
}

static void program_prescale_legacy(
	struct grph_gamma *gg,
	enum pixel_format pixel_format)
{
	uint32_t prescale_control;
	uint32_t prescale_values_grph_r = 0;
	uint32_t prescale_values_grph_g = 0;
	uint32_t prescale_values_grph_b = 0;

	uint32_t prescale_num;
	uint32_t prescale_denom = 1;
	uint16_t prescale_hw;
	uint32_t bias_num = 0;
	uint32_t bias_denom = 1;
	uint16_t bias_hw;

	const uint32_t addr_control =
		FROM_GRAPH_GAMMA(gg)->
		legacy_regs[IDX_PRESCALE_GRPH_CONTROL];

	prescale_control = dal_read_reg(gg->ctx, addr_control);

	set_reg_field_value(
		prescale_control,
		0,
		PRESCALE_GRPH_CONTROL,
		GRPH_PRESCALE_BYPASS);

	switch (pixel_format) {
	case PIXEL_FORMAT_RGB565:
		prescale_num = 64;
		prescale_denom = 63;
	break;

	case PIXEL_FORMAT_ARGB8888:
		/* This function should only be called when using regamma
		 * and bypassing legacy INPUT GAMMA LUT (function name is
		 * misleading)
		 */
		prescale_num = 256;
		prescale_denom = 255;
	break;

	case PIXEL_FORMAT_ARGB2101010:
		prescale_num = 1024;
		prescale_denom = 1023;
	break;

	case PIXEL_FORMAT_ARGB2101010_XRBIAS:
		prescale_num = 1024;
		prescale_denom = 510;
		bias_num = 384;
		bias_denom = 1024;
	break;

	case PIXEL_FORMAT_FP16:
		prescale_num = 1;
	break;

	default:
		prescale_num = 1;

		set_reg_field_value(
			prescale_control,
			1,
			PRESCALE_GRPH_CONTROL,
			GRPH_PRESCALE_BYPASS);
	}

	prescale_hw = dal_controller_float_to_hw_setting(
		dal_fixed31_32_from_fraction(prescale_num, prescale_denom),
		2, 13);

	bias_hw = dal_controller_float_to_hw_setting(
		dal_fixed31_32_from_fraction(bias_num, bias_denom),
		2, 13);


	set_reg_field_value(
		prescale_values_grph_r,
		prescale_hw,
		PRESCALE_VALUES_GRPH_R,
		GRPH_PRESCALE_SCALE_R);

	set_reg_field_value(
		prescale_values_grph_r,
		bias_hw,
		PRESCALE_VALUES_GRPH_R,
		GRPH_PRESCALE_BIAS_R);


	set_reg_field_value(
		prescale_values_grph_g,
		prescale_hw,
		PRESCALE_VALUES_GRPH_G,
		GRPH_PRESCALE_SCALE_G);

	set_reg_field_value(
		prescale_values_grph_g,
		bias_hw,
		PRESCALE_VALUES_GRPH_G,
		GRPH_PRESCALE_BIAS_G);


	set_reg_field_value(
		prescale_values_grph_b,
		prescale_hw,
		PRESCALE_VALUES_GRPH_B,
		GRPH_PRESCALE_SCALE_B);

	set_reg_field_value(
		prescale_values_grph_b,
		bias_hw,
		PRESCALE_VALUES_GRPH_B,
		GRPH_PRESCALE_BIAS_B);

	dal_write_reg(gg->ctx,
		addr_control, prescale_control);

	{
		const uint32_t addr =
			FROM_GRAPH_GAMMA(gg)->
			legacy_regs[IDX_PRESCALE_VALUES_GRPH_R];
		dal_write_reg(gg->ctx,
			addr,	prescale_values_grph_r);
	}

	{
		const uint32_t addr =
			FROM_GRAPH_GAMMA(gg)->
			legacy_regs[IDX_PRESCALE_VALUES_GRPH_G];
		dal_write_reg(gg->ctx,
			addr,	prescale_values_grph_g);
	}

	{
		const uint32_t addr =
			FROM_GRAPH_GAMMA(gg)->
			legacy_regs[IDX_PRESCALE_VALUES_GRPH_B];
		dal_write_reg(gg->ctx,
			addr,	prescale_values_grph_b);
	}
}

static void dal_grph_gamma_dce110_destruct(
	struct grph_gamma_dce110 *gamma)
{
	dal_grph_gamma_destruct(&gamma->base);
}

static void destroy(
	struct grph_gamma **ptr)
{
	struct grph_gamma_dce110 *gamma;

	gamma = FROM_GRAPH_GAMMA(*ptr);

	if (!gamma) {
		ASSERT_CRITICAL(false);
		return;
	}

	dal_grph_gamma_dce110_destruct(gamma);

	dal_free(gamma);

	*ptr = NULL;
}
static const struct grph_gamma_funcs grph_gamma_funcs = {
	.set_gamma_ramp = set_gamma_ramp,
	.set_gamma_ramp_legacy = set_gamma_ramp_legacy,
	.set_legacy_mode = set_legacy_mode,
	.program_prescale_legacy = program_prescale_legacy,
	.setup_distribution_points = setup_distribution_points,
	.program_black_offsets = program_black_offsets,
	.program_white_offsets = program_white_offsets,
	.program_lut_gamma = program_lut_gamma,
	.set_lut_inc = set_lut_inc,
	.select_lut = select_lut,
	.destroy = destroy,
};
static bool construct(
	struct grph_gamma_dce110 *gamma,
	struct grph_gamma_init_data *init_data)
{
	if (!dal_grph_gamma_construct(&gamma->base, init_data)) {
		ASSERT_CRITICAL(false);
		return false;
	}

	gamma->base.funcs = &grph_gamma_funcs;
	gamma->base.regs = gg_regs[init_data->id - 1];
	gamma->legacy_regs = dce110_gg_legacy_regs[init_data->id - 1];

	return true;
}

struct grph_gamma *dal_grph_gamma_dce110_create(
	struct grph_gamma_init_data *init_data)
{
	struct grph_gamma_dce110 *gamma =
		dal_alloc(sizeof(*gamma));

	if (!gamma) {
		ASSERT_CRITICAL(false);
		return NULL;
	}

	if (construct(gamma, init_data))
		return &gamma->base;

	ASSERT_CRITICAL(false);

	dal_free(gamma);
	return NULL;
}
