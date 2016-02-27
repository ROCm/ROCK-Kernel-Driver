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
#include "dce110_opp.h"
#include "basics/conversion.h"
#include "video_csc_types.h"

/* include DCE11 register header files */
#include "dce/dce_11_0_d.h"
#include "dce/dce_11_0_sh_mask.h"
#include "dce/dce_11_0_enum.h"

enum {
	OUTPUT_CSC_MATRIX_SIZE = 12
};

/* constrast:0 - 2.0, default 1.0 */
#define UNDERLAY_CONTRAST_DEFAULT 100
#define UNDERLAY_CONTRAST_MAX     200
#define UNDERLAY_CONTRAST_MIN       0
#define UNDERLAY_CONTRAST_STEP      1
#define UNDERLAY_CONTRAST_DIVIDER 100

/* Saturation: 0 - 2.0; default 1.0 */
#define UNDERLAY_SATURATION_DEFAULT   100 /*1.00*/
#define UNDERLAY_SATURATION_MIN         0
#define UNDERLAY_SATURATION_MAX       200 /* 2.00 */
#define UNDERLAY_SATURATION_STEP        1 /* 0.01 */
/*actual max overlay saturation
 * value = UNDERLAY_SATURATION_MAX /UNDERLAY_SATURATION_DIVIDER
 */

/* Hue */
#define  UNDERLAY_HUE_DEFAULT      0
#define  UNDERLAY_HUE_MIN       -300
#define  UNDERLAY_HUE_MAX        300
#define  UNDERLAY_HUE_STEP         5
#define  UNDERLAY_HUE_DIVIDER   10 /* HW range: -30 ~ +30 */
#define UNDERLAY_SATURATION_DIVIDER   100

/* Brightness: in DAL usually -.25 ~ .25.
 * In MMD is -100 to +100 in 16-235 range; which when scaled to full range is
 *  ~-116 to +116. When normalized this is about 0.4566.
 * With 100 divider this becomes 46, but we may use another for better precision
 * The ideal one is 100/219 ((100/255)*(255/219)),
 * i.e. min/max = +-100, divider = 219
 * default 0.0
 */
#define  UNDERLAY_BRIGHTNESS_DEFAULT    0
#define  UNDERLAY_BRIGHTNESS_MIN      -46 /* ~116/255 */
#define  UNDERLAY_BRIGHTNESS_MAX       46
#define  UNDERLAY_BRIGHTNESS_STEP       1 /*  .01 */
#define  UNDERLAY_BRIGHTNESS_DIVIDER  100

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

static void program_color_matrix_v(
	struct dce110_opp *opp110,
	const struct out_csc_color_matrix *tbl_entry,
	enum grph_color_adjust_option options)
{
	struct dc_context *ctx = opp110->base.ctx;
	uint32_t cntl_value = dm_read_reg(ctx, mmCOL_MAN_OUTPUT_CSC_CONTROL);
	bool use_set_a = (get_reg_field_value(cntl_value,
			COL_MAN_OUTPUT_CSC_CONTROL,
			OUTPUT_CSC_MODE) != 4);

	set_reg_field_value(
			cntl_value,
		0,
		COL_MAN_OUTPUT_CSC_CONTROL,
		OUTPUT_CSC_MODE);

	if (use_set_a) {
		{
			uint32_t value = 0;
			uint32_t addr = mmOUTPUT_CSC_C11_C12_A;
			/* fixed S2.13 format */
			set_reg_field_value(
				value,
				tbl_entry->regval[0],
				OUTPUT_CSC_C11_C12_A,
				OUTPUT_CSC_C11_A);

			set_reg_field_value(
				value,
				tbl_entry->regval[1],
				OUTPUT_CSC_C11_C12_A,
				OUTPUT_CSC_C12_A);

			dm_write_reg(ctx, addr, value);
		}
		{
			uint32_t value = 0;
			uint32_t addr = mmOUTPUT_CSC_C13_C14_A;
			/* fixed S2.13 format */
			set_reg_field_value(
				value,
				tbl_entry->regval[2],
				OUTPUT_CSC_C13_C14_A,
				OUTPUT_CSC_C13_A);
			/* fixed S0.13 format */
			set_reg_field_value(
				value,
				tbl_entry->regval[3],
				OUTPUT_CSC_C13_C14_A,
				OUTPUT_CSC_C14_A);

			dm_write_reg(ctx, addr, value);
		}
		{
			uint32_t value = 0;
			uint32_t addr = mmOUTPUT_CSC_C21_C22_A;
			/* fixed S2.13 format */
			set_reg_field_value(
				value,
				tbl_entry->regval[4],
				OUTPUT_CSC_C21_C22_A,
				OUTPUT_CSC_C21_A);
			/* fixed S2.13 format */
			set_reg_field_value(
				value,
				tbl_entry->regval[5],
				OUTPUT_CSC_C21_C22_A,
				OUTPUT_CSC_C22_A);

			dm_write_reg(ctx, addr, value);
		}
		{
			uint32_t value = 0;
			uint32_t addr = mmOUTPUT_CSC_C23_C24_A;
			/* fixed S2.13 format */
			set_reg_field_value(
				value,
				tbl_entry->regval[6],
				OUTPUT_CSC_C23_C24_A,
				OUTPUT_CSC_C23_A);
			/* fixed S0.13 format */
			set_reg_field_value(
				value,
				tbl_entry->regval[7],
				OUTPUT_CSC_C23_C24_A,
				OUTPUT_CSC_C24_A);

			dm_write_reg(ctx, addr, value);
		}
		{
			uint32_t value = 0;
			uint32_t addr = mmOUTPUT_CSC_C31_C32_A;
			/* fixed S2.13 format */
			set_reg_field_value(
				value,
				tbl_entry->regval[8],
				OUTPUT_CSC_C31_C32_A,
				OUTPUT_CSC_C31_A);
			/* fixed S0.13 format */
			set_reg_field_value(
				value,
				tbl_entry->regval[9],
				OUTPUT_CSC_C31_C32_A,
				OUTPUT_CSC_C32_A);

			dm_write_reg(ctx, addr, value);
		}
		{
			uint32_t value = 0;
			uint32_t addr = mmOUTPUT_CSC_C33_C34_A;
			/* fixed S2.13 format */
			set_reg_field_value(
				value,
				tbl_entry->regval[10],
				OUTPUT_CSC_C33_C34_A,
				OUTPUT_CSC_C33_A);
			/* fixed S0.13 format */
			set_reg_field_value(
				value,
				tbl_entry->regval[11],
				OUTPUT_CSC_C33_C34_A,
				OUTPUT_CSC_C34_A);

			dm_write_reg(ctx, addr, value);
		}
		set_reg_field_value(
			cntl_value,
			4,
			COL_MAN_OUTPUT_CSC_CONTROL,
			OUTPUT_CSC_MODE);
	} else {
		{
			uint32_t value = 0;
			uint32_t addr = mmOUTPUT_CSC_C11_C12_B;
			/* fixed S2.13 format */
			set_reg_field_value(
				value,
				tbl_entry->regval[0],
				OUTPUT_CSC_C11_C12_B,
				OUTPUT_CSC_C11_B);

			set_reg_field_value(
				value,
				tbl_entry->regval[1],
				OUTPUT_CSC_C11_C12_B,
				OUTPUT_CSC_C12_B);

			dm_write_reg(ctx, addr, value);
		}
		{
			uint32_t value = 0;
			uint32_t addr = mmOUTPUT_CSC_C13_C14_B;
			/* fixed S2.13 format */
			set_reg_field_value(
				value,
				tbl_entry->regval[2],
				OUTPUT_CSC_C13_C14_B,
				OUTPUT_CSC_C13_B);
			/* fixed S0.13 format */
			set_reg_field_value(
				value,
				tbl_entry->regval[3],
				OUTPUT_CSC_C13_C14_B,
				OUTPUT_CSC_C14_B);

			dm_write_reg(ctx, addr, value);
		}
		{
			uint32_t value = 0;
			uint32_t addr = mmOUTPUT_CSC_C21_C22_B;
			/* fixed S2.13 format */
			set_reg_field_value(
				value,
				tbl_entry->regval[4],
				OUTPUT_CSC_C21_C22_B,
				OUTPUT_CSC_C21_B);
			/* fixed S2.13 format */
			set_reg_field_value(
				value,
				tbl_entry->regval[5],
				OUTPUT_CSC_C21_C22_B,
				OUTPUT_CSC_C22_B);

			dm_write_reg(ctx, addr, value);
		}
		{
			uint32_t value = 0;
			uint32_t addr = mmOUTPUT_CSC_C23_C24_B;
			/* fixed S2.13 format */
			set_reg_field_value(
				value,
				tbl_entry->regval[6],
				OUTPUT_CSC_C23_C24_B,
				OUTPUT_CSC_C23_B);
			/* fixed S0.13 format */
			set_reg_field_value(
				value,
				tbl_entry->regval[7],
				OUTPUT_CSC_C23_C24_B,
				OUTPUT_CSC_C24_B);

			dm_write_reg(ctx, addr, value);
		}
		{
			uint32_t value = 0;
			uint32_t addr = mmOUTPUT_CSC_C31_C32_B;
			/* fixed S2.13 format */
			set_reg_field_value(
				value,
				tbl_entry->regval[8],
				OUTPUT_CSC_C31_C32_B,
				OUTPUT_CSC_C31_B);
			/* fixed S0.13 format */
			set_reg_field_value(
				value,
				tbl_entry->regval[9],
				OUTPUT_CSC_C31_C32_B,
				OUTPUT_CSC_C32_B);

			dm_write_reg(ctx, addr, value);
		}
		{
			uint32_t value = 0;
			uint32_t addr = mmOUTPUT_CSC_C33_C34_B;
			/* fixed S2.13 format */
			set_reg_field_value(
				value,
				tbl_entry->regval[10],
				OUTPUT_CSC_C33_C34_B,
				OUTPUT_CSC_C33_B);
			/* fixed S0.13 format */
			set_reg_field_value(
				value,
				tbl_entry->regval[11],
				OUTPUT_CSC_C33_C34_B,
				OUTPUT_CSC_C34_B);

			dm_write_reg(ctx, addr, value);
		}
		set_reg_field_value(
			cntl_value,
			5,
			COL_MAN_OUTPUT_CSC_CONTROL,
			OUTPUT_CSC_MODE);
	}

	dm_write_reg(ctx, mmCOL_MAN_OUTPUT_CSC_CONTROL, cntl_value);
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
	struct dce110_opp *opp110,
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
	 * Sin(GrphHue) * K2))
	 * (Cos(GrphHue) * K1 + Sin(GrphHue) * K2)
	 */
	matrix[0] =
		dal_fixed31_32_add(
			dal_fixed31_32_mul(cos_grph_hue, k1),
			dal_fixed31_32_mul(sin_grph_hue, k2));
	/* GrphSat * (Cos(GrphHue) * K1 + Sin(GrphHue) * K2 */
	matrix[0] = dal_fixed31_32_mul(grph_sat, matrix[0]);
	/* (LumaR + GrphSat * (Cos(GrphHue) * K1 + Sin(GrphHue) * K2)) */
	matrix[0] = dal_fixed31_32_add(luma_r, matrix[0]);
	/* GrphCont * (LumaR + GrphSat * (Cos(GrphHue) * K1 + Sin(GrphHue) *
	 * K2))
	 */
	matrix[0] = dal_fixed31_32_mul(grph_cont, matrix[0]);

	/* COEF_1_2 = GrphCont * (LumaG + GrphSat * (Cos(GrphHue) * K3 +
	 * Sin(GrphHue) * K4))
	 * (Cos(GrphHue) * K3 + Sin(GrphHue) * K4)
	 */
	matrix[1] =
		dal_fixed31_32_add(
			dal_fixed31_32_mul(cos_grph_hue, k3),
			dal_fixed31_32_mul(sin_grph_hue, k4));
	/* GrphSat * (Cos(GrphHue) * K3 + Sin(GrphHue) * K4) */
	matrix[1] = dal_fixed31_32_mul(grph_sat, matrix[1]);
	/* (LumaG + GrphSat * (Cos(GrphHue) * K3 + Sin(GrphHue) * K4)) */
	matrix[1] = dal_fixed31_32_add(luma_g, matrix[1]);
	/* GrphCont * (LumaG + GrphSat * (Cos(GrphHue) * K3 + Sin(GrphHue) *
	 * K4))
	 */
	matrix[1] = dal_fixed31_32_mul(grph_cont, matrix[1]);

	/* COEF_1_3 = GrphCont * (LumaB + GrphSat * (Cos(GrphHue) * K5 +
	 * Sin(GrphHue) * K6))
	 * (Cos(GrphHue) * K5 + Sin(GrphHue) * K6)
	 */
	matrix[2] =
		dal_fixed31_32_add(
			dal_fixed31_32_mul(cos_grph_hue, k5),
			dal_fixed31_32_mul(sin_grph_hue, k6));
	/* GrphSat * (Cos(GrphHue) * K5 + Sin(GrphHue) * K6) */
	matrix[2] = dal_fixed31_32_mul(grph_sat, matrix[2]);
	/* LumaB + GrphSat * (Cos(GrphHue) * K5 + Sin(GrphHue) * K6) */
	matrix[2] = dal_fixed31_32_add(luma_b, matrix[2]);
	/* GrphCont  * (LumaB + GrphSat * (Cos(GrphHue) * K5 + Sin(GrphHue) *
	 * K6))
	 */
	matrix[2] = dal_fixed31_32_mul(grph_cont, matrix[2]);

	/* COEF_1_4 = GrphBright */
	matrix[3] = grph_bright;

	/* COEF_2_1 = GrphCont * (LumaR + GrphSat * (Cos(GrphHue) * K7 +
	 * Sin(GrphHue) * K8))
	 * (Cos(GrphHue) * K7 + Sin(GrphHue) * K8)
	 */
	matrix[4] =
		dal_fixed31_32_add(
			dal_fixed31_32_mul(cos_grph_hue, k7),
			dal_fixed31_32_mul(sin_grph_hue, k8));
	/* GrphSat * (Cos(GrphHue) * K7 + Sin(GrphHue) * K8) */
	matrix[4] = dal_fixed31_32_mul(grph_sat, matrix[4]);
	/* (LumaR + GrphSat * (Cos(GrphHue) * K7 + Sin(GrphHue) * K8)) */
	matrix[4] = dal_fixed31_32_add(luma_r, matrix[4]);
	/* GrphCont * (LumaR + GrphSat * (Cos(GrphHue) * K7 + Sin(GrphHue) *
	 * K8))
	 */
	matrix[4] = dal_fixed31_32_mul(grph_cont, matrix[4]);

	/* COEF_2_2 = GrphCont * (LumaG + GrphSat * (Cos(GrphHue) * K9 +
	 * Sin(GrphHue) * K10))
	 * (Cos(GrphHue) * K9 + Sin(GrphHue) * K10))
	 */
	matrix[5] =
		dal_fixed31_32_add(
			dal_fixed31_32_mul(cos_grph_hue, k9),
			dal_fixed31_32_mul(sin_grph_hue, k10));
	/* GrphSat * (Cos(GrphHue) * K9 + Sin(GrphHue) * K10)) */
	matrix[5] = dal_fixed31_32_mul(grph_sat, matrix[5]);
	/* (LumaG + GrphSat * (Cos(GrphHue) * K9 + Sin(GrphHue) * K10)) */
	matrix[5] = dal_fixed31_32_add(luma_g, matrix[5]);
	/* GrphCont * (LumaG + GrphSat * (Cos(GrphHue) * K9 + Sin(GrphHue) *
	 * K10))
	 */
	matrix[5] = dal_fixed31_32_mul(grph_cont, matrix[5]);

	/*  COEF_2_3 = GrphCont * (LumaB + GrphSat * (Cos(GrphHue) * K11 +
	 * Sin(GrphHue) * K12))
	 * (Cos(GrphHue) * K11 + Sin(GrphHue) * K12))
	 */
	matrix[6] =
		dal_fixed31_32_add(
			dal_fixed31_32_mul(cos_grph_hue, k11),
			dal_fixed31_32_mul(sin_grph_hue, k12));
	/* GrphSat * (Cos(GrphHue) * K11 + Sin(GrphHue) * K12)) */
	matrix[6] = dal_fixed31_32_mul(grph_sat, matrix[6]);
	/*  (LumaB + GrphSat * (Cos(GrphHue) * K11 + Sin(GrphHue) * K12)) */
	matrix[6] = dal_fixed31_32_add(luma_b, matrix[6]);
	/* GrphCont * (LumaB + GrphSat * (Cos(GrphHue) * K11 + Sin(GrphHue) *
	 * K12))
	 */
	matrix[6] = dal_fixed31_32_mul(grph_cont, matrix[6]);

	/* COEF_2_4 = GrphBright */
	matrix[7] = grph_bright;

	/* COEF_3_1 = GrphCont  * (LumaR + GrphSat * (Cos(GrphHue) * K13 +
	 * Sin(GrphHue) * K14))
	 * (Cos(GrphHue) * K13 + Sin(GrphHue) * K14))
	 */
	matrix[8] =
		dal_fixed31_32_add(
			dal_fixed31_32_mul(cos_grph_hue, k13),
			dal_fixed31_32_mul(sin_grph_hue, k14));
	/* GrphSat * (Cos(GrphHue) * K13 + Sin(GrphHue) * K14)) */
	matrix[8] = dal_fixed31_32_mul(grph_sat, matrix[8]);
	/* (LumaR + GrphSat * (Cos(GrphHue) * K13 + Sin(GrphHue) * K14)) */
	matrix[8] = dal_fixed31_32_add(luma_r, matrix[8]);
	/* GrphCont  * (LumaR + GrphSat * (Cos(GrphHue) * K13 + Sin(GrphHue) *
	 * K14))
	 */
	matrix[8] = dal_fixed31_32_mul(grph_cont, matrix[8]);

	/* COEF_3_2    = GrphCont * (LumaG + GrphSat * (Cos(GrphHue) * K15 +
	 * Sin(GrphHue) * K16))
	 * GrphSat * (Cos(GrphHue) * K15 + Sin(GrphHue) * K16)
	 */
	matrix[9] =
		dal_fixed31_32_add(
			dal_fixed31_32_mul(cos_grph_hue, k15),
			dal_fixed31_32_mul(sin_grph_hue, k16));
	/* (LumaG + GrphSat * (Cos(GrphHue) * K15 + Sin(GrphHue) * K16)) */
	matrix[9] = dal_fixed31_32_mul(grph_sat, matrix[9]);
	/* (LumaG + GrphSat * (Cos(GrphHue) * K15 + Sin(GrphHue) * K16)) */
	matrix[9] = dal_fixed31_32_add(luma_g, matrix[9]);
	 /* GrphCont * (LumaG + GrphSat * (Cos(GrphHue) * K15 + Sin(GrphHue) *
	  * K16))
	  */
	matrix[9] = dal_fixed31_32_mul(grph_cont, matrix[9]);

	/*  COEF_3_3 = GrphCont * (LumaB + GrphSat * (Cos(GrphHue) * K17 +
	 * Sin(GrphHue) * K18))
	 * (Cos(GrphHue) * K17 + Sin(GrphHue) * K18))
	 */
	matrix[10] =
		dal_fixed31_32_add(
			dal_fixed31_32_mul(cos_grph_hue, k17),
			dal_fixed31_32_mul(sin_grph_hue, k18));
	/*  GrphSat * (Cos(GrphHue) * K17 + Sin(GrphHue) * K18)) */
	matrix[10] = dal_fixed31_32_mul(grph_sat, matrix[10]);
	/* (LumaB + GrphSat * (Cos(GrphHue) * K17 + Sin(GrphHue) * K18)) */
	matrix[10] = dal_fixed31_32_add(luma_b, matrix[10]);
	 /* GrphCont * (LumaB + GrphSat * (Cos(GrphHue) * K17 + Sin(GrphHue) *
	  * K18))
	  */
	matrix[10] = dal_fixed31_32_mul(grph_cont, matrix[10]);

	/*  COEF_3_4    = GrphBright */
	matrix[11] = grph_bright;

	tbl_entry.color_space = adjust->c_space;

	convert_float_matrix(tbl_entry.regval, matrix, OUTPUT_CSC_MATRIX_SIZE);

	program_color_matrix_v(
		opp110, &tbl_entry, adjust->color_adjust_option);
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
	struct dce110_opp *opp110,
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

	program_color_matrix_v(opp110, &reg_matrix, GRPH_COLOR_MATRIX_SW);
}

static bool configure_graphics_mode_v(
	struct dce110_opp *opp110,
	enum csc_color_mode config,
	enum graphics_csc_adjust_type csc_adjust_type,
	enum dc_color_space color_space)
{
	struct dc_context *ctx = opp110->base.ctx;
	uint32_t addr = mmCOL_MAN_OUTPUT_CSC_CONTROL;
	uint32_t value = dm_read_reg(ctx, addr);

	set_reg_field_value(
		value,
		0,
		COL_MAN_OUTPUT_CSC_CONTROL,
		OUTPUT_CSC_MODE);

	if (csc_adjust_type == GRAPHICS_CSC_ADJUST_TYPE_SW) {
		if (config == CSC_COLOR_MODE_GRAPHICS_OUTPUT_CSC)
			return true;

		switch (color_space) {
		case COLOR_SPACE_SRGB:
			/* by pass */
			set_reg_field_value(
				value,
				0,
				COL_MAN_OUTPUT_CSC_CONTROL,
				OUTPUT_CSC_MODE);
			break;
		case COLOR_SPACE_SRGB_LIMITED:
			/* not supported for underlay on CZ */
			return false;

		case COLOR_SPACE_YCBCR601:
		case COLOR_SPACE_YPBPR601:
		case COLOR_SPACE_YCBCR601_LIMITED:
			/* YCbCr601 */
			set_reg_field_value(
				value,
				2,
				COL_MAN_OUTPUT_CSC_CONTROL,
				OUTPUT_CSC_MODE);
			break;
		case COLOR_SPACE_YCBCR709:
		case COLOR_SPACE_YPBPR709:
		case COLOR_SPACE_YCBCR709_LIMITED:
			/* YCbCr709 */
			set_reg_field_value(
				value,
				3,
				COL_MAN_OUTPUT_CSC_CONTROL,
				OUTPUT_CSC_MODE);
			break;
		default:
			return false;
		}

	} else if (csc_adjust_type == GRAPHICS_CSC_ADJUST_TYPE_HW) {
		switch (color_space) {
		case COLOR_SPACE_SRGB:
			/* by pass */
			set_reg_field_value(
				value,
				0,
				COL_MAN_OUTPUT_CSC_CONTROL,
				OUTPUT_CSC_MODE);
			break;
		case COLOR_SPACE_SRGB_LIMITED:
			/* not supported for underlay on CZ */
			return false;
		case COLOR_SPACE_YCBCR601:
		case COLOR_SPACE_YPBPR601:
		case COLOR_SPACE_YCBCR601_LIMITED:
			/* YCbCr601 */
			set_reg_field_value(
				value,
				2,
				COL_MAN_OUTPUT_CSC_CONTROL,
				OUTPUT_CSC_MODE);
			break;
		case COLOR_SPACE_YCBCR709:
		case COLOR_SPACE_YPBPR709:
		case COLOR_SPACE_YCBCR709_LIMITED:
			 /* YCbCr709 */
			set_reg_field_value(
				value,
				3,
				COL_MAN_OUTPUT_CSC_CONTROL,
				OUTPUT_CSC_MODE);
			break;
		default:
			return false;
		}

	} else
		/* by pass */
		set_reg_field_value(
			value,
			0,
			COL_MAN_OUTPUT_CSC_CONTROL,
			OUTPUT_CSC_MODE);

	addr = mmCOL_MAN_OUTPUT_CSC_CONTROL;
	dm_write_reg(ctx, addr, value);

	return true;
}

static void set_Denormalization(struct output_pixel_processor *opp,
		enum dc_color_depth color_depth)
{
	uint32_t value = dm_read_reg(opp->ctx, mmDENORM_CLAMP_CONTROL);

	switch (color_depth) {
	case COLOR_DEPTH_888:
		/* 255/256 for 8 bit output color depth */
		set_reg_field_value(
			value,
			1,
			DENORM_CLAMP_CONTROL,
			DENORM_MODE);
		break;
	case COLOR_DEPTH_101010:
		/* 1023/1024 for 10 bit output color depth */
		set_reg_field_value(
			value,
			2,
			DENORM_CLAMP_CONTROL,
			DENORM_MODE);
		break;
	case COLOR_DEPTH_121212:
		/* 4095/4096 for 12 bit output color depth */
		set_reg_field_value(
			value,
			3,
			DENORM_CLAMP_CONTROL,
			DENORM_MODE);
		break;
	default:
		/* not valid case */
		break;
	}

	set_reg_field_value(
		value,
		1,
		DENORM_CLAMP_CONTROL,
		DENORM_10BIT_OUT);

	dm_write_reg(opp->ctx, mmDENORM_CLAMP_CONTROL, value);
}

struct input_csc_matrix {
	enum dc_color_space color_space;
	uint32_t regval[12];
};

static const struct input_csc_matrix input_csc_matrix[] = {
	{COLOR_SPACE_SRGB,
/*1_1   1_2   1_3   1_4   2_1   2_2   2_3   2_4   3_1   3_2   3_3   3_4 */
		{0x2000, 0, 0, 0, 0, 0x2000, 0, 0, 0, 0, 0x2000, 0} },
	{COLOR_SPACE_SRGB_LIMITED,
		{0x2000, 0, 0, 0, 0, 0x2000, 0, 0, 0, 0, 0x2000, 0} },
	{COLOR_SPACE_YCBCR601,
		{0x2cdd, 0x2000, 0x0, 0xe991, 0xe926, 0x2000, 0xf4fd, 0x10ef,
						0x0, 0x2000, 0x38b4, 0xe3a6} },
	{COLOR_SPACE_YCBCR601_LIMITED,
		{0x3353, 0x2568, 0x0, 0xe400, 0xe5dc, 0x2568, 0xf367, 0x1108,
						0x0, 0x2568, 0x40de, 0xdd3a} },
	{COLOR_SPACE_YCBCR709,
		{0x3265, 0x2000, 0, 0xe6ce, 0xf105, 0x2000, 0xfa01, 0xa7d, 0,
						0x2000, 0x3b61, 0xe24f} },
	{COLOR_SPACE_YCBCR709_LIMITED,
		{0x39a6, 0x2568, 0, 0xe0d6, 0xeedd, 0x2568, 0xf925, 0x9a8, 0,
						0x2568, 0x43ee, 0xdbb2} }
};

static void program_input_csc(
	struct output_pixel_processor *opp, enum dc_color_space color_space)
{
	int arr_size = sizeof(input_csc_matrix)/sizeof(struct input_csc_matrix);
	struct dc_context *ctx = opp->ctx;
	const uint32_t *regval = NULL;
	bool use_set_a;
	uint32_t value;
	int i;

	for (i = 0; i < arr_size; i++)
		if (input_csc_matrix[i].color_space == color_space) {
			regval = input_csc_matrix[i].regval;
			break;
		}
	if (regval == NULL) {
		BREAK_TO_DEBUGGER();
		return;
	}

	/*
	 * 1 == set A, the logic is 'if currently we're not using set A,
	 * then use set A, otherwise use set B'
	 */
	value = dm_read_reg(ctx, mmCOL_MAN_INPUT_CSC_CONTROL);
	use_set_a = get_reg_field_value(
		value, COL_MAN_INPUT_CSC_CONTROL, INPUT_CSC_MODE) != 1;

	if (use_set_a) {
		/* fixed S2.13 format */
		value = 0;
		set_reg_field_value(
			value, regval[0], INPUT_CSC_C11_C12_A, INPUT_CSC_C11_A);
		set_reg_field_value(
			value, regval[1], INPUT_CSC_C11_C12_A, INPUT_CSC_C12_A);
		dm_write_reg(ctx, mmINPUT_CSC_C11_C12_A, value);

		value = 0;
		set_reg_field_value(
			value, regval[2], INPUT_CSC_C13_C14_A, INPUT_CSC_C13_A);
		set_reg_field_value(
			value, regval[3], INPUT_CSC_C13_C14_A, INPUT_CSC_C14_A);
		dm_write_reg(ctx, mmINPUT_CSC_C13_C14_A, value);

		value = 0;
		set_reg_field_value(
			value, regval[4], INPUT_CSC_C21_C22_A, INPUT_CSC_C21_A);
		set_reg_field_value(
			value, regval[5], INPUT_CSC_C21_C22_A, INPUT_CSC_C22_A);
		dm_write_reg(ctx, mmINPUT_CSC_C21_C22_A, value);

		value = 0;
		set_reg_field_value(
			value, regval[6], INPUT_CSC_C23_C24_A, INPUT_CSC_C23_A);
		set_reg_field_value(
			value, regval[7], INPUT_CSC_C23_C24_A, INPUT_CSC_C24_A);
		dm_write_reg(ctx, mmINPUT_CSC_C23_C24_A, value);

		value = 0;
		set_reg_field_value(
			value, regval[8], INPUT_CSC_C31_C32_A, INPUT_CSC_C31_A);
		set_reg_field_value(
			value, regval[9], INPUT_CSC_C31_C32_A, INPUT_CSC_C32_A);
		dm_write_reg(ctx, mmINPUT_CSC_C31_C32_A, value);

		value = 0;
		set_reg_field_value(
			value, regval[10], INPUT_CSC_C33_C34_A, INPUT_CSC_C33_A);
		set_reg_field_value(
			value, regval[11], INPUT_CSC_C33_C34_A, INPUT_CSC_C34_A);
		dm_write_reg(ctx, mmINPUT_CSC_C33_C34_A, value);
	} else {
		/* fixed S2.13 format */
		value = 0;
		set_reg_field_value(
			value, regval[0], INPUT_CSC_C11_C12_B, INPUT_CSC_C11_B);
		set_reg_field_value(
			value, regval[1], INPUT_CSC_C11_C12_B, INPUT_CSC_C12_B);
		dm_write_reg(ctx, mmINPUT_CSC_C11_C12_B, value);

		value = 0;
		set_reg_field_value(
			value, regval[2], INPUT_CSC_C13_C14_B, INPUT_CSC_C13_B);
		set_reg_field_value(
			value, regval[3], INPUT_CSC_C13_C14_B, INPUT_CSC_C14_B);
		dm_write_reg(ctx, mmINPUT_CSC_C13_C14_B, value);

		value = 0;
		set_reg_field_value(
			value, regval[4], INPUT_CSC_C21_C22_B, INPUT_CSC_C21_B);
		set_reg_field_value(
			value, regval[5], INPUT_CSC_C21_C22_B, INPUT_CSC_C22_B);
		dm_write_reg(ctx, mmINPUT_CSC_C21_C22_B, value);

		value = 0;
		set_reg_field_value(
			value, regval[6], INPUT_CSC_C23_C24_B, INPUT_CSC_C23_B);
		set_reg_field_value(
			value, regval[7], INPUT_CSC_C23_C24_B, INPUT_CSC_C24_B);
		dm_write_reg(ctx, mmINPUT_CSC_C23_C24_B, value);

		value = 0;
		set_reg_field_value(
			value, regval[8], INPUT_CSC_C31_C32_B, INPUT_CSC_C31_B);
		set_reg_field_value(
			value, regval[9], INPUT_CSC_C31_C32_B, INPUT_CSC_C32_B);
		dm_write_reg(ctx, mmINPUT_CSC_C31_C32_B, value);

		value = 0;
		set_reg_field_value(
			value, regval[10], INPUT_CSC_C33_C34_B, INPUT_CSC_C33_B);
		set_reg_field_value(
			value, regval[11], INPUT_CSC_C33_C34_B, INPUT_CSC_C34_B);
		dm_write_reg(ctx, mmINPUT_CSC_C33_C34_B, value);
	}

	/* KK: leave INPUT_CSC_CONVERSION_MODE at default */
	value = 0;
	/*
	 * select 8.4 input type instead of default 12.0. From the discussion
	 * with HW team, this format depends on the UNP surface format, so for
	 * 8-bit we should select 8.4 (4 bits truncated). For 10 it should be
	 * 10.2. For Carrizo we only support 8-bit surfaces on underlay pipe
	 * so we can always keep this at 8.4 (input_type=2). If the later asics
	 * start supporting 10+ bits, we will have a problem: surface
	 * programming including UNP_GRPH* is being done in DalISR after this,
	 * so either we pass surface format to here, or move this logic to ISR
	 */

	set_reg_field_value(
		value, 2, COL_MAN_INPUT_CSC_CONTROL, INPUT_CSC_INPUT_TYPE);
	set_reg_field_value(
		value,
		use_set_a ? 1 : 2,
		COL_MAN_INPUT_CSC_CONTROL,
		INPUT_CSC_MODE);

	dm_write_reg(ctx, mmCOL_MAN_INPUT_CSC_CONTROL, value);
}

void dce110_opp_v_set_csc_default(
	struct output_pixel_processor *opp,
	const struct default_adjustment *default_adjust)
{
	struct dce110_opp *opp110 = TO_DCE110_OPP(opp);
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
			 * file
			 */
			program_color_matrix_v(opp110, elm, option);
			config = CSC_COLOR_MODE_GRAPHICS_OUTPUT_CSC;
			break;
		}
	}

	program_input_csc(opp, default_adjust->in_color_space);

	/* configure the what we programmed :
	 * 1. Default values from this file
	 * 2. Use hardware default from ROM_A and we do not need to program
	 * matrix
	 */

	configure_graphics_mode_v(opp110, config,
		default_adjust->csc_adjust_type,
		default_adjust->out_color_space);

	set_Denormalization(opp, default_adjust->color_depth);
}

void dce110_opp_v_set_csc_adjustment(
	struct output_pixel_processor *opp,
	const struct opp_grph_csc_adjustment *adjust)
{
	struct dce110_opp *opp110 = TO_DCE110_OPP(opp);
	enum csc_color_mode config =
			CSC_COLOR_MODE_GRAPHICS_OUTPUT_CSC;

	/* Apply color adjustments: brightness, saturation, hue, contrast and
	 * CSC. No need for different color space routine, color space defines
	 * the ideal values only, but keep original design to allow quick switch
	 * to the old legacy routines
	 */
	switch (adjust->c_space) {
	case COLOR_SPACE_SRGB:
		set_rgb_adjustment_legacy(opp110, adjust);
		break;
	case COLOR_SPACE_YCBCR601:
	case COLOR_SPACE_YCBCR709:
	case COLOR_SPACE_YCBCR601_LIMITED:
	case COLOR_SPACE_YCBCR709_LIMITED:
	case COLOR_SPACE_YPBPR601:
	case COLOR_SPACE_YPBPR709:
		set_yuv_adjustment(opp110, adjust);
		break;
	default:
		set_rgb_adjustment_legacy(opp110, adjust);
		break;
	}

	/*  We did everything ,now program DxOUTPUT_CSC_CONTROL */
	configure_graphics_mode_v(opp110, config, adjust->csc_adjust_type,
		adjust->c_space);

	set_Denormalization(opp, adjust->color_depth);
}
