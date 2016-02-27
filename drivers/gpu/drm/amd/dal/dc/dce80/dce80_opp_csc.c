/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 *  and/or sell copies of the Software, and to permit persons to whom the
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

#include "dce80_opp.h"
#include "basics/conversion.h"

/* include DCE8 register header files */
#include "dce/dce_8_0_d.h"
#include "dce/dce_8_0_sh_mask.h"

#define DCP_REG(reg)\
	(reg + opp80->offsets.dcp_offset)

enum {
	OUTPUT_CSC_MATRIX_SIZE = 12
};

struct out_csc_color_matrix {
	enum dc_color_space color_space;
	uint16_t regval[OUTPUT_CSC_MATRIX_SIZE];
};

static const struct out_csc_color_matrix global_color_matrix[] = {
{ COLOR_SPACE_SRGB,
	{ 0x2000, 0, 0, 0, 0, 0x2000, 0, 0, 0, 0, 0x2000, 0} },
{ COLOR_SPACE_SRGB_LIMITED,
	{ 0x1B60, 0, 0, 0x200, 0, 0x1B60, 0, 0x200, 0, 0, 0x1B60, 0x200} },
{ COLOR_SPACE_YCBCR601,
	{ 0xE00, 0xF447, 0xFDB9, 0x1000, 0x82F, 0x1012, 0x31F, 0x200, 0xFB47,
		0xF6B9, 0xE00, 0x1000} },
{ COLOR_SPACE_YCBCR709, { 0xE00, 0xF349, 0xFEB7, 0x1000, 0x5D2, 0x1394, 0x1FA,
	0x200, 0xFCCB, 0xF535, 0xE00, 0x1000} },
/* TODO: correct values below */
{ COLOR_SPACE_YCBCR601_LIMITED, { 0xE00, 0xF447, 0xFDB9, 0x1000, 0x991,
	0x12C9, 0x3A6, 0x200, 0xFB47, 0xF6B9, 0xE00, 0x1000} },
{ COLOR_SPACE_YCBCR709_LIMITED, { 0xE00, 0xF349, 0xFEB7, 0x1000, 0x6CE, 0x16E3,
	0x24F, 0x200, 0xFCCB, 0xF535, 0xE00, 0x1000} }
};

enum csc_color_mode {
	/* 00 - BITS2:0 Bypass */
	CSC_COLOR_MODE_GRAPHICS_BYPASS,
	/* 01 - hard coded coefficient TV RGB */
	CSC_COLOR_MODE_GRAPHICS_PREDEFINED,
	/* 04 - programmable OUTPUT CSC coefficient */
	CSC_COLOR_MODE_GRAPHICS_OUTPUT_CSC,
};

static void program_color_matrix(
	struct dce80_opp *opp80,
	const struct out_csc_color_matrix *tbl_entry,
	enum grph_color_adjust_option options)
{
	struct dc_context *ctx = opp80->base.ctx;
	{
		uint32_t value = 0;
		uint32_t addr = DCP_REG(mmOUTPUT_CSC_C11_C12);
		/* fixed S2.13 format */
		set_reg_field_value(
			value,
			tbl_entry->regval[0],
			OUTPUT_CSC_C11_C12,
			OUTPUT_CSC_C11);

		set_reg_field_value(
			value,
			tbl_entry->regval[1],
			OUTPUT_CSC_C11_C12,
			OUTPUT_CSC_C12);

		dm_write_reg(ctx, addr, value);
	}
	{
		uint32_t value = 0;
		uint32_t addr = DCP_REG(mmOUTPUT_CSC_C13_C14);
		/* fixed S2.13 format */
		set_reg_field_value(
			value,
			tbl_entry->regval[2],
			OUTPUT_CSC_C13_C14,
			OUTPUT_CSC_C13);
		/* fixed S0.13 format */
		set_reg_field_value(
			value,
			tbl_entry->regval[3],
			OUTPUT_CSC_C13_C14,
			OUTPUT_CSC_C14);

		dm_write_reg(ctx, addr, value);
	}
	{
		uint32_t value = 0;
		uint32_t addr = DCP_REG(mmOUTPUT_CSC_C21_C22);
		/* fixed S2.13 format */
		set_reg_field_value(
			value,
			tbl_entry->regval[4],
			OUTPUT_CSC_C21_C22,
			OUTPUT_CSC_C21);
		/* fixed S2.13 format */
		set_reg_field_value(
			value,
			tbl_entry->regval[5],
			OUTPUT_CSC_C21_C22,
			OUTPUT_CSC_C22);

		dm_write_reg(ctx, addr, value);
	}
	{
		uint32_t value = 0;
		uint32_t addr = DCP_REG(mmOUTPUT_CSC_C23_C24);
		/* fixed S2.13 format */
		set_reg_field_value(
			value,
			tbl_entry->regval[6],
			OUTPUT_CSC_C23_C24,
			OUTPUT_CSC_C23);
		/* fixed S0.13 format */
		set_reg_field_value(
			value,
			tbl_entry->regval[7],
			OUTPUT_CSC_C23_C24,
			OUTPUT_CSC_C24);

		dm_write_reg(ctx, addr, value);
	}
	{
		uint32_t value = 0;
		uint32_t addr = DCP_REG(mmOUTPUT_CSC_C31_C32);
		/* fixed S2.13 format */
		set_reg_field_value(
			value,
			tbl_entry->regval[8],
			OUTPUT_CSC_C31_C32,
			OUTPUT_CSC_C31);
		/* fixed S0.13 format */
		set_reg_field_value(
			value,
			tbl_entry->regval[9],
			OUTPUT_CSC_C31_C32,
			OUTPUT_CSC_C32);

		dm_write_reg(ctx, addr, value);
	}
	{
		uint32_t value = 0;
		uint32_t addr = DCP_REG(mmOUTPUT_CSC_C33_C34);
		/* fixed S2.13 format */
		set_reg_field_value(
			value,
			tbl_entry->regval[10],
			OUTPUT_CSC_C33_C34,
			OUTPUT_CSC_C33);
		/* fixed S0.13 format */
		set_reg_field_value(
			value,
			tbl_entry->regval[11],
			OUTPUT_CSC_C33_C34,
			OUTPUT_CSC_C34);

		dm_write_reg(ctx, addr, value);
	}
}

/*
 * initialize_color_float_adj_reference_values
 * This initialize display color adjust input from API to HW range for later
 * calculation use. This is shared by all the display color adjustment.
 * @param :
 * @return None
 */
static void initialize_color_float_adj_reference_values(
	const struct opp_grph_csc_adjustment *adjust,
	struct fixed31_32 *grph_cont,
	struct fixed31_32 *grph_sat,
	struct fixed31_32 *grph_bright,
	struct fixed31_32 *sin_grph_hue,
	struct fixed31_32 *cos_grph_hue)
{
	/* Hue adjustment could be negative. -45 ~ +45 */
	struct fixed31_32 hue =
		dal_fixed31_32_mul(
			dal_fixed31_32_from_fraction(adjust->grph_hue, 180),
			dal_fixed31_32_pi);

	*sin_grph_hue = dal_fixed31_32_sin(hue);
	*cos_grph_hue = dal_fixed31_32_cos(hue);

	if (adjust->adjust_divider) {
		*grph_cont =
			dal_fixed31_32_from_fraction(
				adjust->grph_cont,
				adjust->adjust_divider);
		*grph_sat =
			dal_fixed31_32_from_fraction(
				adjust->grph_sat,
				adjust->adjust_divider);
		*grph_bright =
			dal_fixed31_32_from_fraction(
				adjust->grph_bright,
				adjust->adjust_divider);
	} else {
		*grph_cont = dal_fixed31_32_from_int(adjust->grph_cont);
		*grph_sat = dal_fixed31_32_from_int(adjust->grph_sat);
		*grph_bright = dal_fixed31_32_from_int(adjust->grph_bright);
	}
}

static inline struct fixed31_32 fixed31_32_clamp(
	struct fixed31_32 value,
	int32_t min_numerator,
	int32_t max_numerator,
	int32_t denominator)
{
	return dal_fixed31_32_clamp(
		value,
		dal_fixed31_32_from_fraction(
			min_numerator,
			denominator),
		dal_fixed31_32_from_fraction(
			max_numerator,
			denominator));
}

static void setup_reg_format(
	struct fixed31_32 *coefficients,
	uint16_t *reg_values)
{
	enum {
		LENGTH = 12,
		DENOMINATOR = 10000
	};

	static const int32_t min_numerator[] = {
		-3 * DENOMINATOR,
		-DENOMINATOR
	};

	static const int32_t max_numerator[] = {
		DENOMINATOR,
		DENOMINATOR
	};

	static const uint8_t integer_bits[] = { 2, 0 };

	uint32_t i = 0;

	do {
		const uint32_t index = (i % 4) == 3;

		reg_values[i] = fixed_point_to_int_frac(
			fixed31_32_clamp(coefficients[(i + 8) % LENGTH],
				min_numerator[index],
				max_numerator[index],
				DENOMINATOR),
			integer_bits[index], 13);

		++i;
	} while (i != LENGTH);
}

/**
 *****************************************************************************
 *  Function: setup_adjustments
 *  @note prepare to setup the values
 *
 *  @see
 *
 *****************************************************************************
 */
static void setup_adjustments(const struct opp_grph_csc_adjustment *adjust,
	struct dc_csc_adjustments *adjustments)
{
	if (adjust->adjust_divider != 0) {
		adjustments->brightness =
			dal_fixed31_32_from_fraction(adjust->grph_bright,
			adjust->adjust_divider);
		adjustments->contrast =
			dal_fixed31_32_from_fraction(adjust->grph_cont,
			adjust->adjust_divider);
		adjustments->saturation =
			dal_fixed31_32_from_fraction(adjust->grph_sat,
			adjust->adjust_divider);
	} else {
		adjustments->brightness =
			dal_fixed31_32_from_fraction(adjust->grph_bright, 1);
		adjustments->contrast =
			dal_fixed31_32_from_fraction(adjust->grph_cont, 1);
		adjustments->saturation =
			dal_fixed31_32_from_fraction(adjust->grph_sat, 1);
	}

	/* convert degrees into radians */
	adjustments->hue =
		dal_fixed31_32_mul(
			dal_fixed31_32_from_fraction(adjust->grph_hue, 180),
			dal_fixed31_32_pi);
}

static void prepare_tv_rgb_ideal(
	struct fixed31_32 *matrix)
{
	static const int32_t matrix_[] = {
		85546875, 0, 0, 6250000,
		0, 85546875, 0, 6250000,
		0, 0, 85546875, 6250000
	};

	uint32_t i = 0;

	do {
		matrix[i] = dal_fixed31_32_from_fraction(
			matrix_[i],
			100000000);
		++i;
	} while (i != ARRAY_SIZE(matrix_));
}

/**
 *****************************************************************************
 *  Function: dal_transform_wide_gamut_set_rgb_adjustment_legacy
 *
 *  @param [in] const struct opp_grph_csc_adjustment *adjust
 *
 *  @return
 *     void
 *
 *  @note calculate and program color adjustments for sRGB color space
 *
 *  @see
 *
 *****************************************************************************
 */
static void set_rgb_adjustment_legacy(
	struct dce80_opp *opp80,
	const struct opp_grph_csc_adjustment *adjust)
{
	const struct fixed31_32 k1 =
		dal_fixed31_32_from_fraction(701000, 1000000);
	const struct fixed31_32 k2 =
		dal_fixed31_32_from_fraction(236568, 1000000);
	const struct fixed31_32 k3 =
		dal_fixed31_32_from_fraction(-587000, 1000000);
	const struct fixed31_32 k4 =
		dal_fixed31_32_from_fraction(464432, 1000000);
	const struct fixed31_32 k5 =
		dal_fixed31_32_from_fraction(-114000, 1000000);
	const struct fixed31_32 k6 =
		dal_fixed31_32_from_fraction(-701000, 1000000);
	const struct fixed31_32 k7 =
		dal_fixed31_32_from_fraction(-299000, 1000000);
	const struct fixed31_32 k8 =
		dal_fixed31_32_from_fraction(-292569, 1000000);
	const struct fixed31_32 k9 =
		dal_fixed31_32_from_fraction(413000, 1000000);
	const struct fixed31_32 k10 =
		dal_fixed31_32_from_fraction(-92482, 1000000);
	const struct fixed31_32 k11 =
		dal_fixed31_32_from_fraction(-114000, 1000000);
	const struct fixed31_32 k12 =
		dal_fixed31_32_from_fraction(385051, 1000000);
	const struct fixed31_32 k13 =
		dal_fixed31_32_from_fraction(-299000, 1000000);
	const struct fixed31_32 k14 =
		dal_fixed31_32_from_fraction(886000, 1000000);
	const struct fixed31_32 k15 =
		dal_fixed31_32_from_fraction(-587000, 1000000);
	const struct fixed31_32 k16 =
		dal_fixed31_32_from_fraction(-741914, 1000000);
	const struct fixed31_32 k17 =
		dal_fixed31_32_from_fraction(886000, 1000000);
	const struct fixed31_32 k18 =
		dal_fixed31_32_from_fraction(-144086, 1000000);

	const struct fixed31_32 luma_r =
		dal_fixed31_32_from_fraction(299, 1000);
	const struct fixed31_32 luma_g =
		dal_fixed31_32_from_fraction(587, 1000);
	const struct fixed31_32 luma_b =
		dal_fixed31_32_from_fraction(114, 1000);

	struct out_csc_color_matrix tbl_entry;
	struct fixed31_32 matrix[OUTPUT_CSC_MATRIX_SIZE];

	struct fixed31_32 grph_cont;
	struct fixed31_32 grph_sat;
	struct fixed31_32 grph_bright;
	struct fixed31_32 sin_grph_hue;
	struct fixed31_32 cos_grph_hue;

	initialize_color_float_adj_reference_values(
		adjust, &grph_cont, &grph_sat,
		&grph_bright, &sin_grph_hue, &cos_grph_hue);

	/* COEF_1_1 = GrphCont * (LumaR + GrphSat * (Cos(GrphHue) * K1 +
	 * Sin(GrphHue) * K2)) */
	/* (Cos(GrphHue) * K1 + Sin(GrphHue) * K2) */
	matrix[0] =
		dal_fixed31_32_add(
			dal_fixed31_32_mul(cos_grph_hue, k1),
			dal_fixed31_32_mul(sin_grph_hue, k2));
	/* GrphSat * (Cos(GrphHue) * K1 + Sin(GrphHue) * K2 */
	matrix[0] = dal_fixed31_32_mul(grph_sat, matrix[0]);
	/* (LumaR + GrphSat * (Cos(GrphHue) * K1 + Sin(GrphHue) * K2)) */
	matrix[0] = dal_fixed31_32_add(luma_r, matrix[0]);
	/* GrphCont * (LumaR + GrphSat * (Cos(GrphHue) * K1 + Sin(GrphHue) *
	 * K2)) */
	matrix[0] = dal_fixed31_32_mul(grph_cont, matrix[0]);

	/* COEF_1_2 = GrphCont * (LumaG + GrphSat * (Cos(GrphHue) * K3 +
	 * Sin(GrphHue) * K4)) */
	/* (Cos(GrphHue) * K3 + Sin(GrphHue) * K4) */
	matrix[1] =
		dal_fixed31_32_add(
			dal_fixed31_32_mul(cos_grph_hue, k3),
			dal_fixed31_32_mul(sin_grph_hue, k4));
	/* GrphSat * (Cos(GrphHue) * K3 + Sin(GrphHue) * K4) */
	matrix[1] = dal_fixed31_32_mul(grph_sat, matrix[1]);
	/* (LumaG + GrphSat * (Cos(GrphHue) * K3 + Sin(GrphHue) * K4)) */
	matrix[1] = dal_fixed31_32_add(luma_g, matrix[1]);
	/* GrphCont * (LumaG + GrphSat * (Cos(GrphHue) * K3 + Sin(GrphHue) *
	 * K4)) */
	matrix[1] = dal_fixed31_32_mul(grph_cont, matrix[1]);

	/* COEF_1_3 = GrphCont * (LumaB + GrphSat * (Cos(GrphHue) * K5 +
	 * Sin(GrphHue) * K6)) */
	/* (Cos(GrphHue) * K5 + Sin(GrphHue) * K6) */
	matrix[2] =
		dal_fixed31_32_add(
			dal_fixed31_32_mul(cos_grph_hue, k5),
			dal_fixed31_32_mul(sin_grph_hue, k6));
	/* GrphSat * (Cos(GrphHue) * K5 + Sin(GrphHue) * K6) */
	matrix[2] = dal_fixed31_32_mul(grph_sat, matrix[2]);
	/* LumaB + GrphSat * (Cos(GrphHue) * K5 + Sin(GrphHue) * K6) */
	matrix[2] = dal_fixed31_32_add(luma_b, matrix[2]);
	/* GrphCont  * (LumaB + GrphSat * (Cos(GrphHue) * K5 + Sin(GrphHue) *
	 * K6)) */
	matrix[2] = dal_fixed31_32_mul(grph_cont, matrix[2]);

	/* COEF_1_4 = GrphBright */
	matrix[3] = grph_bright;

	/* COEF_2_1 = GrphCont * (LumaR + GrphSat * (Cos(GrphHue) * K7 +
	 * Sin(GrphHue) * K8)) */
	/* (Cos(GrphHue) * K7 + Sin(GrphHue) * K8) */
	matrix[4] =
		dal_fixed31_32_add(
			dal_fixed31_32_mul(cos_grph_hue, k7),
			dal_fixed31_32_mul(sin_grph_hue, k8));
	/* GrphSat * (Cos(GrphHue) * K7 + Sin(GrphHue) * K8) */
	matrix[4] = dal_fixed31_32_mul(grph_sat, matrix[4]);
	/* (LumaR + GrphSat * (Cos(GrphHue) * K7 + Sin(GrphHue) * K8)) */
	matrix[4] = dal_fixed31_32_add(luma_r, matrix[4]);
	/* GrphCont * (LumaR + GrphSat * (Cos(GrphHue) * K7 + Sin(GrphHue) *
	 * K8)) */
	matrix[4] = dal_fixed31_32_mul(grph_cont, matrix[4]);

	/* COEF_2_2 = GrphCont * (LumaG + GrphSat * (Cos(GrphHue) * K9 +
	 * Sin(GrphHue) * K10)) */
	/* (Cos(GrphHue) * K9 + Sin(GrphHue) * K10)) */
	matrix[5] =
		dal_fixed31_32_add(
			dal_fixed31_32_mul(cos_grph_hue, k9),
			dal_fixed31_32_mul(sin_grph_hue, k10));
	/* GrphSat * (Cos(GrphHue) * K9 + Sin(GrphHue) * K10)) */
	matrix[5] = dal_fixed31_32_mul(grph_sat, matrix[5]);
	/* (LumaG + GrphSat * (Cos(GrphHue) * K9 + Sin(GrphHue) * K10)) */
	matrix[5] = dal_fixed31_32_add(luma_g, matrix[5]);
	/* GrphCont * (LumaG + GrphSat * (Cos(GrphHue) * K9 + Sin(GrphHue) *
	 * K10)) */
	matrix[5] = dal_fixed31_32_mul(grph_cont, matrix[5]);

	/*  COEF_2_3 = GrphCont * (LumaB + GrphSat * (Cos(GrphHue) * K11 +
	 * Sin(GrphHue) * K12)) */
	/* (Cos(GrphHue) * K11 + Sin(GrphHue) * K12)) */
	matrix[6] =
		dal_fixed31_32_add(
			dal_fixed31_32_mul(cos_grph_hue, k11),
			dal_fixed31_32_mul(sin_grph_hue, k12));
	/* GrphSat * (Cos(GrphHue) * K11 + Sin(GrphHue) * K12)) */
	matrix[6] = dal_fixed31_32_mul(grph_sat, matrix[6]);
	/*  (LumaB + GrphSat * (Cos(GrphHue) * K11 + Sin(GrphHue) * K12)) */
	matrix[6] = dal_fixed31_32_add(luma_b, matrix[6]);
	/* GrphCont * (LumaB + GrphSat * (Cos(GrphHue) * K11 + Sin(GrphHue) *
	 * K12)) */
	matrix[6] = dal_fixed31_32_mul(grph_cont, matrix[6]);

	/* COEF_2_4 = GrphBright */
	matrix[7] = grph_bright;

	/* COEF_3_1 = GrphCont  * (LumaR + GrphSat * (Cos(GrphHue) * K13 +
	 * Sin(GrphHue) * K14)) */
	/* (Cos(GrphHue) * K13 + Sin(GrphHue) * K14)) */
	matrix[8] =
		dal_fixed31_32_add(
			dal_fixed31_32_mul(cos_grph_hue, k13),
			dal_fixed31_32_mul(sin_grph_hue, k14));
	/* GrphSat * (Cos(GrphHue) * K13 + Sin(GrphHue) * K14)) */
	matrix[8] = dal_fixed31_32_mul(grph_sat, matrix[8]);
	/* (LumaR + GrphSat * (Cos(GrphHue) * K13 + Sin(GrphHue) * K14)) */
	matrix[8] = dal_fixed31_32_add(luma_r, matrix[8]);
	/* GrphCont  * (LumaR + GrphSat * (Cos(GrphHue) * K13 + Sin(GrphHue) *
	 * K14)) */
	matrix[8] = dal_fixed31_32_mul(grph_cont, matrix[8]);

	/* COEF_3_2    = GrphCont * (LumaG + GrphSat * (Cos(GrphHue) * K15 +
	 * Sin(GrphHue) * K16)) */
	/* GrphSat * (Cos(GrphHue) * K15 + Sin(GrphHue) * K16) */
	matrix[9] =
		dal_fixed31_32_add(
			dal_fixed31_32_mul(cos_grph_hue, k15),
			dal_fixed31_32_mul(sin_grph_hue, k16));
	/* (LumaG + GrphSat * (Cos(GrphHue) * K15 + Sin(GrphHue) * K16)) */
	matrix[9] = dal_fixed31_32_mul(grph_sat, matrix[9]);
	/* (LumaG + GrphSat * (Cos(GrphHue) * K15 + Sin(GrphHue) * K16)) */
	matrix[9] = dal_fixed31_32_add(luma_g, matrix[9]);
	 /* GrphCont * (LumaG + GrphSat * (Cos(GrphHue) * K15 + Sin(GrphHue) *
	  * K16)) */
	matrix[9] = dal_fixed31_32_mul(grph_cont, matrix[9]);

	/*  COEF_3_3 = GrphCont * (LumaB + GrphSat * (Cos(GrphHue) * K17 +
	 * Sin(GrphHue) * K18)) */
	/* (Cos(GrphHue) * K17 + Sin(GrphHue) * K18)) */
	matrix[10] =
		dal_fixed31_32_add(
			dal_fixed31_32_mul(cos_grph_hue, k17),
			dal_fixed31_32_mul(sin_grph_hue, k18));
	/*  GrphSat * (Cos(GrphHue) * K17 + Sin(GrphHue) * K18)) */
	matrix[10] = dal_fixed31_32_mul(grph_sat, matrix[10]);
	/* (LumaB + GrphSat * (Cos(GrphHue) * K17 + Sin(GrphHue) * K18)) */
	matrix[10] = dal_fixed31_32_add(luma_b, matrix[10]);
	 /* GrphCont * (LumaB + GrphSat * (Cos(GrphHue) * K17 + Sin(GrphHue) *
	  * K18)) */
	matrix[10] = dal_fixed31_32_mul(grph_cont, matrix[10]);

	/*  COEF_3_4    = GrphBright */
	matrix[11] = grph_bright;

	tbl_entry.color_space = adjust->c_space;

	convert_float_matrix(tbl_entry.regval, matrix, OUTPUT_CSC_MATRIX_SIZE);

	program_color_matrix(
		opp80, &tbl_entry, adjust->color_adjust_option);
}

/**
 *****************************************************************************
 *  Function: dal_transform_wide_gamut_set_rgb_limited_range_adjustment
 *
 *  @param [in] const struct opp_grph_csc_adjustment *adjust
 *
 *  @return
 *     void
 *
 *  @note calculate and program color adjustments for sRGB limited color space
 *
 *  @see
 *
 *****************************************************************************
 */
static void set_rgb_limited_range_adjustment(
	struct dce80_opp *opp80,
	const struct opp_grph_csc_adjustment *adjust)
{
	struct out_csc_color_matrix reg_matrix;
	struct fixed31_32 change_matrix[OUTPUT_CSC_MATRIX_SIZE];
	struct fixed31_32 matrix[OUTPUT_CSC_MATRIX_SIZE];
	struct dc_csc_adjustments adjustments;
	struct fixed31_32 ideals[OUTPUT_CSC_MATRIX_SIZE];

	prepare_tv_rgb_ideal(ideals);

	setup_adjustments(adjust, &adjustments);

	calculate_adjustments(ideals, &adjustments, matrix);

	memmove(change_matrix, matrix, sizeof(matrix));

	/* from 1 -> 3 */
	matrix[8] = change_matrix[0];
	matrix[9] = change_matrix[1];
	matrix[10] = change_matrix[2];
	matrix[11] = change_matrix[3];

	/* from 2 -> 1 */
	matrix[0] = change_matrix[4];
	matrix[1] = change_matrix[5];
	matrix[2] = change_matrix[6];
	matrix[3] = change_matrix[7];

	/* from 3 -> 2 */
	matrix[4] = change_matrix[8];
	matrix[5] = change_matrix[9];
	matrix[6] = change_matrix[10];
	matrix[7] = change_matrix[11];

	memset(&reg_matrix, 0, sizeof(struct out_csc_color_matrix));

	setup_reg_format(matrix, reg_matrix.regval);

	program_color_matrix(opp80, &reg_matrix, GRPH_COLOR_MATRIX_SW);
}

static void prepare_yuv_ideal(
	bool b601,
	struct fixed31_32 *matrix)
{
	static const int32_t matrix_1[] = {
		25578516, 50216016, 9752344, 6250000,
		-14764391, -28985609, 43750000, 50000000,
		43750000, -36635164, -7114836, 50000000
	};

	static const int32_t matrix_2[] = {
		18187266, 61183125, 6176484, 6250000,
		-10025059, -33724941, 43750000, 50000000,
		43750000, -39738379, -4011621, 50000000
	};

	const int32_t *matrix_x = b601 ? matrix_1 : matrix_2;

	uint32_t i = 0;

	do {
		matrix[i] = dal_fixed31_32_from_fraction(
			matrix_x[i],
			100000000);
		++i;
	} while (i != ARRAY_SIZE(matrix_1));
}

/**
 *****************************************************************************
 *  Function: dal_transform_wide_gamut_set_yuv_adjustment
 *
 *  @param [in] const struct opp_grph_csc_adjustment *adjust
 *
 *  @return
 *     void
 *
 *  @note calculate and program color adjustments for YUV  color spaces
 *
 *  @see
 *
 *****************************************************************************
 */
static void set_yuv_adjustment(
	struct dce80_opp *opp80,
	const struct opp_grph_csc_adjustment *adjust)
{
	bool b601 = (adjust->c_space == COLOR_SPACE_YPBPR601) ||
		(adjust->c_space == COLOR_SPACE_YCBCR601) ||
		(adjust->c_space == COLOR_SPACE_YCBCR601_LIMITED);
	struct out_csc_color_matrix reg_matrix;
	struct fixed31_32 matrix[OUTPUT_CSC_MATRIX_SIZE];
	struct dc_csc_adjustments adjustments;
	struct fixed31_32 ideals[OUTPUT_CSC_MATRIX_SIZE];

	prepare_yuv_ideal(b601, ideals);

	setup_adjustments(adjust, &adjustments);

	if ((adjust->c_space == COLOR_SPACE_YCBCR601_LIMITED) ||
		(adjust->c_space == COLOR_SPACE_YCBCR709_LIMITED))
		calculate_adjustments_y_only(
			ideals, &adjustments, matrix);
	else
		calculate_adjustments(
			ideals, &adjustments, matrix);

	memset(&reg_matrix, 0, sizeof(struct out_csc_color_matrix));

	setup_reg_format(matrix, reg_matrix.regval);

	program_color_matrix(opp80, &reg_matrix, GRPH_COLOR_MATRIX_SW);
}

static bool configure_graphics_mode(
	struct dce80_opp *opp80,
	enum csc_color_mode config,
	enum graphics_csc_adjust_type csc_adjust_type,
	enum dc_color_space color_space)
{
	struct dc_context *ctx = opp80->base.ctx;
	uint32_t addr = DCP_REG(mmOUTPUT_CSC_CONTROL);
	uint32_t value = dm_read_reg(ctx, addr);

	set_reg_field_value(
		value,
		0,
		OUTPUT_CSC_CONTROL,
		OUTPUT_CSC_GRPH_MODE);

	if (csc_adjust_type == GRAPHICS_CSC_ADJUST_TYPE_SW) {
		if (config == CSC_COLOR_MODE_GRAPHICS_OUTPUT_CSC) {
			set_reg_field_value(
				value,
				4,
				OUTPUT_CSC_CONTROL,
				OUTPUT_CSC_GRPH_MODE);
		} else {

			switch (color_space) {
			case COLOR_SPACE_SRGB:
				/* by pass */
				set_reg_field_value(
					value,
					0,
					OUTPUT_CSC_CONTROL,
					OUTPUT_CSC_GRPH_MODE);
				break;
			case COLOR_SPACE_SRGB_LIMITED:
				/* TV RGB */
				set_reg_field_value(
					value,
					1,
					OUTPUT_CSC_CONTROL,
					OUTPUT_CSC_GRPH_MODE);
				break;
			case COLOR_SPACE_YCBCR601:
			case COLOR_SPACE_YPBPR601:
			case COLOR_SPACE_YCBCR601_LIMITED:
				/* YCbCr601 */
				set_reg_field_value(
					value,
					2,
					OUTPUT_CSC_CONTROL,
					OUTPUT_CSC_GRPH_MODE);
				break;
			case COLOR_SPACE_YCBCR709:
			case COLOR_SPACE_YPBPR709:
			case COLOR_SPACE_YCBCR709_LIMITED:
				/* YCbCr709 */
				set_reg_field_value(
					value,
					3,
					OUTPUT_CSC_CONTROL,
					OUTPUT_CSC_GRPH_MODE);
				break;
			default:
				return false;
			}
		}
	} else if (csc_adjust_type == GRAPHICS_CSC_ADJUST_TYPE_HW) {
		switch (color_space) {
		case COLOR_SPACE_SRGB:
			/* by pass */
			set_reg_field_value(
				value,
				0,
				OUTPUT_CSC_CONTROL,
				OUTPUT_CSC_GRPH_MODE);
			break;
		case COLOR_SPACE_SRGB_LIMITED:
			/* TV RGB */
			set_reg_field_value(
				value,
				1,
				OUTPUT_CSC_CONTROL,
				OUTPUT_CSC_GRPH_MODE);
			break;
		case COLOR_SPACE_YCBCR601:
		case COLOR_SPACE_YPBPR601:
		case COLOR_SPACE_YCBCR601_LIMITED:
			/* YCbCr601 */
			set_reg_field_value(
				value,
				2,
				OUTPUT_CSC_CONTROL,
				OUTPUT_CSC_GRPH_MODE);
			break;
		case COLOR_SPACE_YCBCR709:
		case COLOR_SPACE_YPBPR709:
		case COLOR_SPACE_YCBCR709_LIMITED:
			 /* YCbCr709 */
			set_reg_field_value(
				value,
				3,
				OUTPUT_CSC_CONTROL,
				OUTPUT_CSC_GRPH_MODE);
			break;
		default:
			return false;
		}

	} else
		/* by pass */
		set_reg_field_value(
			value,
			0,
			OUTPUT_CSC_CONTROL,
			OUTPUT_CSC_GRPH_MODE);

	addr = DCP_REG(mmOUTPUT_CSC_CONTROL);
	dm_write_reg(ctx, addr, value);

	return true;
}

void dce80_opp_set_csc_adjustment(
	struct output_pixel_processor *opp,
	const struct opp_grph_csc_adjustment *adjust)
{
	struct dce80_opp *opp80 = TO_DCE80_OPP(opp);
	enum csc_color_mode config =
			CSC_COLOR_MODE_GRAPHICS_OUTPUT_CSC;

	/* Apply color adjustments: brightness, saturation, hue, contrast and
	 * CSC. No need for different color space routine, color space defines
	 * the ideal values only, but keep original design to allow quick switch
	 * to the old legacy routines */
	switch (adjust->c_space) {
	case COLOR_SPACE_SRGB:
		set_rgb_adjustment_legacy(opp80, adjust);
		break;
	case COLOR_SPACE_SRGB_LIMITED:
		set_rgb_limited_range_adjustment(
			opp80, adjust);
		break;
	case COLOR_SPACE_YCBCR601:
	case COLOR_SPACE_YCBCR709:
	case COLOR_SPACE_YCBCR601_LIMITED:
	case COLOR_SPACE_YCBCR709_LIMITED:
	case COLOR_SPACE_YPBPR601:
	case COLOR_SPACE_YPBPR709:
		set_yuv_adjustment(opp80, adjust);
		break;
	default:
		set_rgb_adjustment_legacy(opp80, adjust);
		break;
	}

	/*  We did everything ,now program DxOUTPUT_CSC_CONTROL */
	configure_graphics_mode(opp80, config, adjust->csc_adjust_type,
		adjust->c_space);
}

void dce80_opp_set_csc_default(
	struct output_pixel_processor *opp,
	const struct default_adjustment *default_adjust)
{
	struct dce80_opp *opp80 = TO_DCE80_OPP(opp);
	enum csc_color_mode config =
			CSC_COLOR_MODE_GRAPHICS_PREDEFINED;

	if (default_adjust->force_hw_default == false) {
		const struct out_csc_color_matrix *elm;
		/* currently parameter not in use */
		enum grph_color_adjust_option option =
			GRPH_COLOR_MATRIX_HW_DEFAULT;
		uint32_t i;
		/*
		 * HW default false we program locally defined matrix
		 * HW default true  we use predefined hw matrix and we
		 * do not need to program matrix
		 * OEM wants the HW default via runtime parameter.
		 */
		option = GRPH_COLOR_MATRIX_SW;

		for (i = 0; i < ARRAY_SIZE(global_color_matrix); ++i) {
			elm = &global_color_matrix[i];
			if (elm->color_space != default_adjust->out_color_space)
				continue;
			/* program the matrix with default values from this
			 * file */
			program_color_matrix(opp80, elm, option);
			config = CSC_COLOR_MODE_GRAPHICS_OUTPUT_CSC;
			break;
		}
	}

	/* configure the what we programmed :
	 * 1. Default values from this file
	 * 2. Use hardware default from ROM_A and we do not need to program
	 * matrix */

	configure_graphics_mode(opp80, config,
		default_adjust->csc_adjust_type,
		default_adjust->out_color_space);
}
