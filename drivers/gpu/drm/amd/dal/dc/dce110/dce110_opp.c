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

/* include DCE11 register header files */
#include "dce/dce_11_0_d.h"
#include "dce/dce_11_0_sh_mask.h"

#include "dce110_opp.h"

enum {
	MAX_LUT_ENTRY = 256,
	MAX_NUMBER_OF_ENTRIES = 256
};

static void build_evenly_distributed_points(
	struct gamma_pixel *points,
	uint32_t numberof_points,
	struct fixed31_32 max_value,
	struct fixed31_32 divider1,
	struct fixed31_32 divider2,
	struct fixed31_32 divider3)
{
	struct gamma_pixel *p = points;
	struct gamma_pixel *p_last = p + numberof_points - 1;

	uint32_t i = 0;

	do {
		struct fixed31_32 value = dal_fixed31_32_div_int(
			dal_fixed31_32_mul_int(max_value, i),
			numberof_points - 1);

		p->r = value;
		p->g = value;
		p->b = value;

		++p;
		++i;
	} while (i != numberof_points);

	p->r = dal_fixed31_32_div(p_last->r, divider1);
	p->g = dal_fixed31_32_div(p_last->g, divider1);
	p->b = dal_fixed31_32_div(p_last->b, divider1);

	++p;

	p->r = dal_fixed31_32_div(p_last->r, divider2);
	p->g = dal_fixed31_32_div(p_last->g, divider2);
	p->b = dal_fixed31_32_div(p_last->b, divider2);

	++p;

	p->r = dal_fixed31_32_div(p_last->r, divider3);
	p->g = dal_fixed31_32_div(p_last->g, divider3);
	p->b = dal_fixed31_32_div(p_last->b, divider3);
}

/*****************************************/
/* Constructor, Destructor               */
/*****************************************/

struct opp_funcs funcs = {
		.opp_map_legacy_and_regamma_hw_to_x_user = dce110_opp_map_legacy_and_regamma_hw_to_x_user,
		.opp_power_on_regamma_lut = dce110_opp_power_on_regamma_lut,
		.opp_program_bit_depth_reduction = dce110_opp_program_bit_depth_reduction,
		.opp_program_clamping_and_pixel_encoding = dce110_opp_program_clamping_and_pixel_encoding,
		.opp_set_csc_adjustment = dce110_opp_set_csc_adjustment,
		.opp_set_csc_default = dce110_opp_set_csc_default,
		.opp_set_dyn_expansion = dce110_opp_set_dyn_expansion,
		.opp_set_regamma = dce110_opp_set_regamma
};

bool dce110_opp_construct(struct dce110_opp *opp110,
	struct dc_context *ctx,
	uint32_t inst,
	const struct dce110_opp_reg_offsets *offsets)
{

	opp110->base.funcs = &funcs;

	opp110->base.ctx = ctx;

	opp110->base.inst = inst;

	opp110->offsets = *offsets;

	opp110->regamma.hw_points_num = 128;
	opp110->regamma.coordinates_x = NULL;
	opp110->regamma.rgb_resulted = NULL;
	opp110->regamma.rgb_regamma = NULL;
	opp110->regamma.coeff128 = NULL;
	opp110->regamma.coeff128_oem = NULL;
	opp110->regamma.coeff128_dx = NULL;
	opp110->regamma.axis_x_256 = NULL;
	opp110->regamma.axis_x_1025 = NULL;
	opp110->regamma.rgb_oem = NULL;
	opp110->regamma.rgb_user = NULL;
	opp110->regamma.extra_points = 3;
	opp110->regamma.use_half_points = false;
	opp110->regamma.x_max1 = dal_fixed31_32_one;
	opp110->regamma.x_max2 = dal_fixed31_32_from_int(2);
	opp110->regamma.x_min = dal_fixed31_32_zero;
	opp110->regamma.divider1 = dal_fixed31_32_from_fraction(3, 2);
	opp110->regamma.divider2 = dal_fixed31_32_from_int(2);
	opp110->regamma.divider3 = dal_fixed31_32_from_fraction(5, 2);

	opp110->regamma.rgb_user = dc_service_alloc(
		ctx,
		sizeof(struct pwl_float_data) *
		(DX_GAMMA_RAMP_MAX + opp110->regamma.extra_points));
	if (!opp110->regamma.rgb_user)
		goto failure_1;

	opp110->regamma.rgb_oem = dc_service_alloc(
		ctx,
		sizeof(struct pwl_float_data) *
		(DX_GAMMA_RAMP_MAX + opp110->regamma.extra_points));
	if (!opp110->regamma.rgb_oem)
		goto failure_2;

	opp110->regamma.rgb_resulted = dc_service_alloc(
		ctx,
		sizeof(struct pwl_result_data) *
		(MAX_NUMBER_OF_ENTRIES + opp110->regamma.extra_points));
	if (!opp110->regamma.rgb_resulted)
		goto failure_3;

	opp110->regamma.rgb_regamma = dc_service_alloc(
		ctx,
		sizeof(struct pwl_float_data_ex) *
		(MAX_NUMBER_OF_ENTRIES + opp110->regamma.extra_points));
	if (!opp110->regamma.rgb_regamma)
		goto failure_4;

	opp110->regamma.coordinates_x = dc_service_alloc(
		ctx,
		sizeof(struct hw_x_point) *
		(MAX_NUMBER_OF_ENTRIES + opp110->regamma.extra_points));
	if (!opp110->regamma.coordinates_x)
		goto failure_5;

	opp110->regamma.axis_x_256 = dc_service_alloc(
		ctx,
		sizeof(struct gamma_pixel) *
		(MAX_LUT_ENTRY + opp110->regamma.extra_points));
	if (!opp110->regamma.axis_x_256)
		goto failure_6;

	opp110->regamma.axis_x_1025 = dc_service_alloc(
		ctx,
		sizeof(struct gamma_pixel) *
		(DX_GAMMA_RAMP_MAX + opp110->regamma.extra_points));
	if (!opp110->regamma.axis_x_1025)
		goto failure_7;

	opp110->regamma.coeff128 = dc_service_alloc(
		ctx,
		sizeof(struct pixel_gamma_point) *
		(MAX_NUMBER_OF_ENTRIES + opp110->regamma.extra_points));
	if (!opp110->regamma.coeff128)
		goto failure_8;

	opp110->regamma.coeff128_oem = dc_service_alloc(
		ctx,
		sizeof(struct pixel_gamma_point) *
		(MAX_NUMBER_OF_ENTRIES + opp110->regamma.extra_points));
	if (!opp110->regamma.coeff128_oem)
		goto failure_9;

	opp110->regamma.coeff128_dx = dc_service_alloc(
		ctx,
		sizeof(struct pixel_gamma_point) *
		(MAX_NUMBER_OF_ENTRIES + opp110->regamma.extra_points));
	if (!opp110->regamma.coeff128_dx)
		goto failure_10;

	/* init palette */
	{
		uint32_t i = 0;

		do {
			opp110->regamma.saved_palette[i].red = (uint8_t)i;
			opp110->regamma.saved_palette[i].green = (uint8_t)i;
			opp110->regamma.saved_palette[i].blue = (uint8_t)i;

			++i;
		} while (i != MAX_LUT_ENTRY);
	}

	build_evenly_distributed_points(
		opp110->regamma.axis_x_256,
		MAX_LUT_ENTRY,
		opp110->regamma.x_max1,
		opp110->regamma.divider1,
		opp110->regamma.divider2,
		opp110->regamma.divider3);

	build_evenly_distributed_points(
		opp110->regamma.axis_x_1025,
		DX_GAMMA_RAMP_MAX,
		opp110->regamma.x_max1,
		opp110->regamma.divider1,
		opp110->regamma.divider2,
		opp110->regamma.divider3);

	return true;

failure_10:
	dc_service_free(ctx, opp110->regamma.coeff128_oem);
failure_9:
	dc_service_free(ctx, opp110->regamma.coeff128);
failure_8:
	dc_service_free(ctx, opp110->regamma.axis_x_1025);
failure_7:
	dc_service_free(ctx, opp110->regamma.axis_x_256);
failure_6:
	dc_service_free(ctx, opp110->regamma.coordinates_x);
failure_5:
	dc_service_free(ctx, opp110->regamma.rgb_regamma);
failure_4:
	dc_service_free(ctx, opp110->regamma.rgb_resulted);
failure_3:
	dc_service_free(ctx, opp110->regamma.rgb_oem);
failure_2:
	dc_service_free(ctx, opp110->regamma.rgb_user);
failure_1:

	return true;
}

void dce110_opp_destroy(struct output_pixel_processor **opp)
{
	dc_service_free((*opp)->ctx, FROM_DCE11_OPP(*opp)->regamma.coeff128_dx);
	dc_service_free((*opp)->ctx, FROM_DCE11_OPP(*opp)->regamma.coeff128_oem);
	dc_service_free((*opp)->ctx, FROM_DCE11_OPP(*opp)->regamma.coeff128);
	dc_service_free((*opp)->ctx, FROM_DCE11_OPP(*opp)->regamma.axis_x_1025);
	dc_service_free((*opp)->ctx, FROM_DCE11_OPP(*opp)->regamma.axis_x_256);
	dc_service_free((*opp)->ctx, FROM_DCE11_OPP(*opp)->regamma.coordinates_x);
	dc_service_free((*opp)->ctx, FROM_DCE11_OPP(*opp)->regamma.rgb_regamma);
	dc_service_free((*opp)->ctx, FROM_DCE11_OPP(*opp)->regamma.rgb_resulted);
	dc_service_free((*opp)->ctx, FROM_DCE11_OPP(*opp)->regamma.rgb_oem);
	dc_service_free((*opp)->ctx, FROM_DCE11_OPP(*opp)->regamma.rgb_user);
	dc_service_free((*opp)->ctx, FROM_DCE11_OPP(*opp));
	*opp = NULL;
}

