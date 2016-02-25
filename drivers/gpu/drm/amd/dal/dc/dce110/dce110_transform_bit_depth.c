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

/* include DCE11 register header files */
#include "dce/dce_11_0_d.h"
#include "dce/dce_11_0_sh_mask.h"

#include "dce110_transform.h"
#include "dce110_transform_v.h"
#include "opp.h"
#include "include/logger_interface.h"
#include "include/fixed32_32.h"

#define DCP_REG(reg)\
	(reg + xfm110->offsets.dcp_offset)

#define LB_REG(reg)\
	(reg + xfm110->offsets.lb_offset)

#define LB_TOTAL_NUMBER_OF_ENTRIES 1712
#define LB_BITS_PER_ENTRY 144

enum dcp_out_trunc_round_mode {
	DCP_OUT_TRUNC_ROUND_MODE_TRUNCATE,
	DCP_OUT_TRUNC_ROUND_MODE_ROUND
};

enum dcp_out_trunc_round_depth {
	DCP_OUT_TRUNC_ROUND_DEPTH_14BIT,
	DCP_OUT_TRUNC_ROUND_DEPTH_13BIT,
	DCP_OUT_TRUNC_ROUND_DEPTH_12BIT,
	DCP_OUT_TRUNC_ROUND_DEPTH_11BIT,
	DCP_OUT_TRUNC_ROUND_DEPTH_10BIT,
	DCP_OUT_TRUNC_ROUND_DEPTH_9BIT,
	DCP_OUT_TRUNC_ROUND_DEPTH_8BIT
};

/*  defines the various methods of bit reduction available for use */
enum dcp_bit_depth_reduction_mode {
	DCP_BIT_DEPTH_REDUCTION_MODE_DITHER,
	DCP_BIT_DEPTH_REDUCTION_MODE_ROUND,
	DCP_BIT_DEPTH_REDUCTION_MODE_TRUNCATE,
	DCP_BIT_DEPTH_REDUCTION_MODE_DISABLED,
	DCP_BIT_DEPTH_REDUCTION_MODE_INVALID
};

enum dcp_spatial_dither_mode {
	DCP_SPATIAL_DITHER_MODE_AAAA,
	DCP_SPATIAL_DITHER_MODE_A_AA_A,
	DCP_SPATIAL_DITHER_MODE_AABBAABB,
	DCP_SPATIAL_DITHER_MODE_AABBCCAABBCC,
	DCP_SPATIAL_DITHER_MODE_INVALID
};

enum dcp_spatial_dither_depth {
	DCP_SPATIAL_DITHER_DEPTH_30BPP,
	DCP_SPATIAL_DITHER_DEPTH_24BPP
};

static bool set_clamp(
	struct dce110_transform *xfm110,
	enum dc_color_depth depth);

static bool set_round(
	struct dce110_transform *xfm110,
	enum dcp_out_trunc_round_mode mode,
	enum dcp_out_trunc_round_depth depth);

static bool set_dither(
	struct dce110_transform *xfm110,
	bool dither_enable,
	enum dcp_spatial_dither_mode dither_mode,
	enum dcp_spatial_dither_depth dither_depth,
	bool frame_random_enable,
	bool rgb_random_enable,
	bool highpass_random_enable);

/**
 *******************************************************************************
 * dce110_transform_bit_depth_reduction_program
 *
 * @brief
 *     Programs the DCP bit depth reduction registers (Clamp, Round/Truncate,
 *      Dither) for dce110
 *
 * @param depth : bit depth to set the clamp to (should match denorm)
 *
 * @return
 *     true if succeeds.
 *******************************************************************************
 */
static bool program_bit_depth_reduction(
	struct dce110_transform *xfm110,
	enum dc_color_depth depth,
	const struct bit_depth_reduction_params *bit_depth_params)
{
	enum dcp_bit_depth_reduction_mode depth_reduction_mode;
	enum dcp_spatial_dither_mode spatial_dither_mode;
	bool frame_random_enable;
	bool rgb_random_enable;
	bool highpass_random_enable;

	if (depth > COLOR_DEPTH_121212) {
		ASSERT_CRITICAL(false); /* Invalid clamp bit depth */
		return false;
	}

	if (bit_depth_params->flags.SPATIAL_DITHER_ENABLED) {
		depth_reduction_mode = DCP_BIT_DEPTH_REDUCTION_MODE_DITHER;
		frame_random_enable = true;
		rgb_random_enable = true;
		highpass_random_enable = true;

	} else {
		depth_reduction_mode = DCP_BIT_DEPTH_REDUCTION_MODE_DISABLED;
		frame_random_enable = false;
		rgb_random_enable = false;
		highpass_random_enable = false;
	}

	spatial_dither_mode = DCP_SPATIAL_DITHER_MODE_A_AA_A;

	if (!set_clamp(xfm110, depth)) {
		/* Failure in set_clamp() */
		ASSERT_CRITICAL(false);
		return false;
	}

	switch (depth_reduction_mode) {
	case DCP_BIT_DEPTH_REDUCTION_MODE_DITHER:
		/*  Spatial Dither: Set round/truncate to bypass (12bit),
		 *  enable Dither (30bpp) */
		set_round(xfm110,
			DCP_OUT_TRUNC_ROUND_MODE_TRUNCATE,
			DCP_OUT_TRUNC_ROUND_DEPTH_12BIT);

		set_dither(xfm110, true, spatial_dither_mode,
			DCP_SPATIAL_DITHER_DEPTH_30BPP, frame_random_enable,
			rgb_random_enable, highpass_random_enable);
		break;
	case DCP_BIT_DEPTH_REDUCTION_MODE_ROUND:
		/*  Round: Enable round (10bit), disable Dither */
		set_round(xfm110,
			DCP_OUT_TRUNC_ROUND_MODE_ROUND,
			DCP_OUT_TRUNC_ROUND_DEPTH_10BIT);

		set_dither(xfm110, false, spatial_dither_mode,
			DCP_SPATIAL_DITHER_DEPTH_30BPP, frame_random_enable,
			rgb_random_enable, highpass_random_enable);
		break;
	case DCP_BIT_DEPTH_REDUCTION_MODE_TRUNCATE: /*  Truncate */
		/*  Truncate: Enable truncate (10bit), disable Dither */
		set_round(xfm110,
			DCP_OUT_TRUNC_ROUND_MODE_TRUNCATE,
			DCP_OUT_TRUNC_ROUND_DEPTH_10BIT);

		set_dither(xfm110, false, spatial_dither_mode,
			DCP_SPATIAL_DITHER_DEPTH_30BPP, frame_random_enable,
			rgb_random_enable, highpass_random_enable);
		break;

	case DCP_BIT_DEPTH_REDUCTION_MODE_DISABLED: /*  Disabled */
		/*  Truncate: Set round/truncate to bypass (12bit),
		 * disable Dither */
		set_round(xfm110,
			DCP_OUT_TRUNC_ROUND_MODE_TRUNCATE,
			DCP_OUT_TRUNC_ROUND_DEPTH_12BIT);

		set_dither(xfm110, false, spatial_dither_mode,
			DCP_SPATIAL_DITHER_DEPTH_30BPP, frame_random_enable,
			rgb_random_enable, highpass_random_enable);
		break;
	default:
		/* Invalid DCP Depth reduction mode */
		ASSERT_CRITICAL(false);
		break;
	}

	return true;
}

/**
 *******************************************************************************
 * set_clamp
 *
 * @param depth : bit depth to set the clamp to (should match denorm)
 *
 * @brief
 *     Programs clamp according to panel bit depth.
 *
 * @return
 *     true if succeeds
 *
 *******************************************************************************
 */
static bool set_clamp(
	struct dce110_transform *xfm110,
	enum dc_color_depth depth)
{
	uint32_t clamp_max = 0;

	/* At the clamp block the data will be MSB aligned, so we set the max
	 * clamp accordingly.
	 * For example, the max value for 6 bits MSB aligned (14 bit bus) would
	 * be "11 1111 0000 0000" in binary, so 0x3F00.
	 */
	switch (depth) {
	case COLOR_DEPTH_666:
		/* 6bit MSB aligned on 14 bit bus '11 1111 0000 0000' */
		clamp_max = 0x3F00;
		break;
	case COLOR_DEPTH_888:
		/* 8bit MSB aligned on 14 bit bus '11 1111 1100 0000' */
		clamp_max = 0x3FC0;
		break;
	case COLOR_DEPTH_101010:
		/* 10bit MSB aligned on 14 bit bus '11 1111 1111 1100' */
		clamp_max = 0x3FFC;
		break;
	case COLOR_DEPTH_121212:
		/* 12bit MSB aligned on 14 bit bus '11 1111 1111 1111' */
		clamp_max = 0x3FFF;
		break;
	default:
		ASSERT_CRITICAL(false); /* Invalid clamp bit depth */
		return false;
	}

	{
		uint32_t value = 0;
		/*  always set min to 0 */
			set_reg_field_value(
			value,
			0,
			OUT_CLAMP_CONTROL_B_CB,
			OUT_CLAMP_MIN_B_CB);

		set_reg_field_value(
			value,
			clamp_max,
			OUT_CLAMP_CONTROL_B_CB,
			OUT_CLAMP_MAX_B_CB);

		dm_write_reg(xfm110->base.ctx,
			DCP_REG(mmOUT_CLAMP_CONTROL_B_CB),
			value);
	}

	{
		uint32_t value = 0;
		/*  always set min to 0 */
		set_reg_field_value(
			value,
			0,
			OUT_CLAMP_CONTROL_G_Y,
			OUT_CLAMP_MIN_G_Y);

		set_reg_field_value(
			value,
			clamp_max,
			OUT_CLAMP_CONTROL_G_Y,
			OUT_CLAMP_MAX_G_Y);

		dm_write_reg(xfm110->base.ctx,
			DCP_REG(mmOUT_CLAMP_CONTROL_G_Y),
			value);
	}

	{
		uint32_t value = 0;
		/*  always set min to 0 */
		set_reg_field_value(
			value,
			0,
			OUT_CLAMP_CONTROL_R_CR,
			OUT_CLAMP_MIN_R_CR);

		set_reg_field_value(
			value,
			clamp_max,
			OUT_CLAMP_CONTROL_R_CR,
			OUT_CLAMP_MAX_R_CR);

		dm_write_reg(xfm110->base.ctx,
			DCP_REG(mmOUT_CLAMP_CONTROL_R_CR),
			value);
	}

	return true;
}

/**
 *******************************************************************************
 * set_round
 *
 * @brief
 *     Programs Round/Truncate
 *
 * @param [in] mode  :round or truncate
 * @param [in] depth :bit depth to round/truncate to
 OUT_ROUND_TRUNC_MODE 3:0 0xA Output data round or truncate mode
 POSSIBLE VALUES:
      00 - truncate to u0.12
      01 - truncate to u0.11
      02 - truncate to u0.10
      03 - truncate to u0.9
      04 - truncate to u0.8
      05 - reserved
      06 - truncate to u0.14
      07 - truncate to u0.13		set_reg_field_value(
			value,
			clamp_max,
			OUT_CLAMP_CONTROL_R_CR,
			OUT_CLAMP_MAX_R_CR);
      08 - round to u0.12
      09 - round to u0.11
      10 - round to u0.10
      11 - round to u0.9
      12 - round to u0.8
      13 - reserved
      14 - round to u0.14
      15 - round to u0.13

 * @return
 *     true if succeeds.
 *******************************************************************************
 */
static bool set_round(
	struct dce110_transform *xfm110,
	enum dcp_out_trunc_round_mode mode,
	enum dcp_out_trunc_round_depth depth)
{
	uint32_t depth_bits = 0;
	uint32_t mode_bit = 0;
	/*  zero out all bits */
	uint32_t value = 0;

	/*  set up bit depth */
	switch (depth) {
	case DCP_OUT_TRUNC_ROUND_DEPTH_14BIT:
		depth_bits = 6;
		break;
	case DCP_OUT_TRUNC_ROUND_DEPTH_13BIT:
		depth_bits = 7;
		break;
	case DCP_OUT_TRUNC_ROUND_DEPTH_12BIT:
		depth_bits = 0;
		break;
	case DCP_OUT_TRUNC_ROUND_DEPTH_11BIT:
		depth_bits = 1;
		break;
	case DCP_OUT_TRUNC_ROUND_DEPTH_10BIT:
		depth_bits = 2;
		break;
	case DCP_OUT_TRUNC_ROUND_DEPTH_9BIT:
		depth_bits = 3;
		break;
	case DCP_OUT_TRUNC_ROUND_DEPTH_8BIT:
		depth_bits = 4;
		break;
	default:
		/* Invalid dcp_out_trunc_round_depth */
		ASSERT_CRITICAL(false);
		return false;
	}

	set_reg_field_value(
		value,
		depth_bits,
		OUT_ROUND_CONTROL,
		OUT_ROUND_TRUNC_MODE);

	/*  set up round or truncate */
	switch (mode) {
	case DCP_OUT_TRUNC_ROUND_MODE_TRUNCATE:
		mode_bit = 0;
		break;
	case DCP_OUT_TRUNC_ROUND_MODE_ROUND:
		mode_bit = 1;
		break;
	default:
		/* Invalid dcp_out_trunc_round_mode */
		ASSERT_CRITICAL(false);
		return false;
	}

	depth_bits |= mode_bit << 3;

	set_reg_field_value(
		value,
		depth_bits,
		OUT_ROUND_CONTROL,
		OUT_ROUND_TRUNC_MODE);

	/*  write the register */
	dm_write_reg(xfm110->base.ctx,
				DCP_REG(mmOUT_ROUND_CONTROL),
				value);

	return true;
}

/**
 *******************************************************************************
 * set_dither
 *
 * @brief
 *     Programs Dither
 *
 * @param [in] dither_enable        : enable dither
 * @param [in] dither_mode           : dither mode to set
 * @param [in] dither_depth          : bit depth to dither to
 * @param [in] frame_random_enable    : enable frame random
 * @param [in] rgb_random_enable      : enable rgb random
 * @param [in] highpass_random_enable : enable highpass random
 *
 * @return
 *     true if succeeds.
 *******************************************************************************
 */

static bool set_dither(
	struct dce110_transform *xfm110,
	bool dither_enable,
	enum dcp_spatial_dither_mode dither_mode,
	enum dcp_spatial_dither_depth dither_depth,
	bool frame_random_enable,
	bool rgb_random_enable,
	bool highpass_random_enable)
{
	uint32_t dither_depth_bits = 0;
	uint32_t dither_mode_bits = 0;
	/*  zero out all bits */
	uint32_t value = 0;

	/* set up the fields */
	if (dither_enable)
		set_reg_field_value(
			value,
			1,
			DCP_SPATIAL_DITHER_CNTL,
			DCP_SPATIAL_DITHER_EN);

	switch (dither_mode) {
	case DCP_SPATIAL_DITHER_MODE_AAAA:
		dither_mode_bits = 0;
		break;
	case DCP_SPATIAL_DITHER_MODE_A_AA_A:
		dither_mode_bits = 1;
		break;
	case DCP_SPATIAL_DITHER_MODE_AABBAABB:
		dither_mode_bits = 2;
		break;
	case DCP_SPATIAL_DITHER_MODE_AABBCCAABBCC:
		dither_mode_bits = 3;
		break;
	default:
		/* Invalid dcp_spatial_dither_mode */
		ASSERT_CRITICAL(false);
		return false;

	}
	set_reg_field_value(
		value,
		dither_mode_bits,
		DCP_SPATIAL_DITHER_CNTL,
		DCP_SPATIAL_DITHER_MODE);

	switch (dither_depth) {
	case DCP_SPATIAL_DITHER_DEPTH_30BPP:
		dither_depth_bits = 0;
		break;
	case DCP_SPATIAL_DITHER_DEPTH_24BPP:
		dither_depth_bits = 1;
		break;
	default:
		/* Invalid dcp_spatial_dither_depth */
		ASSERT_CRITICAL(false);
		return false;
	}

	set_reg_field_value(
		value,
		dither_depth_bits,
		DCP_SPATIAL_DITHER_CNTL,
		DCP_SPATIAL_DITHER_DEPTH);

	if (frame_random_enable)
		set_reg_field_value(
			value,
			1,
			DCP_SPATIAL_DITHER_CNTL,
			DCP_FRAME_RANDOM_ENABLE);

	if (rgb_random_enable)
		set_reg_field_value(
			value,
			1,
			DCP_SPATIAL_DITHER_CNTL,
			DCP_RGB_RANDOM_ENABLE);

	if (highpass_random_enable)
		set_reg_field_value(
			value,
			1,
			DCP_SPATIAL_DITHER_CNTL,
			DCP_HIGHPASS_RANDOM_ENABLE);

	/*  write the register */
	dm_write_reg(xfm110->base.ctx,
				DCP_REG(mmDCP_SPATIAL_DITHER_CNTL),
				value);

	return true;
}

bool dce110_transform_get_max_num_of_supported_lines(
	struct dce110_transform *xfm110,
	enum lb_pixel_depth depth,
	uint32_t pixel_width,
	uint32_t *lines)
{
	uint32_t pixels_per_entries = 0;
	uint32_t max_pixels_supports = 0;

	if (pixel_width == 0)
		return false;

	/* Find number of pixels that can fit into a single LB entry and
	 * take floor of the value since we cannot store a single pixel
	 * across multiple entries. */
	switch (depth) {
	case LB_PIXEL_DEPTH_18BPP:
		pixels_per_entries = LB_BITS_PER_ENTRY / 18;
		break;

	case LB_PIXEL_DEPTH_24BPP:
		pixels_per_entries = LB_BITS_PER_ENTRY / 24;
		break;

	case LB_PIXEL_DEPTH_30BPP:
		pixels_per_entries = LB_BITS_PER_ENTRY / 30;
		break;

	case LB_PIXEL_DEPTH_36BPP:
		pixels_per_entries = LB_BITS_PER_ENTRY / 36;
		break;

	default:
		dal_logger_write(xfm110->base.ctx->logger,
			LOG_MAJOR_WARNING,
			LOG_MINOR_COMPONENT_GPU,
			"%s: Invalid LB pixel depth",
			__func__);
		break;
	}

	if (pixels_per_entries == 0)
		return false;

	max_pixels_supports = pixels_per_entries * LB_TOTAL_NUMBER_OF_ENTRIES;

	*lines = max_pixels_supports / pixel_width;
	return true;
}

void dce110_transform_set_alpha(struct transform *xfm, bool enable)
{
	struct dce110_transform *xfm110 = TO_DCE110_TRANSFORM(xfm);
	struct dc_context *ctx = xfm->ctx;
	uint32_t value;
	uint32_t addr = LB_REG(mmLB_DATA_FORMAT);

	value = dm_read_reg(ctx, addr);

	if (enable == 1)
		set_reg_field_value(
				value,
				1,
				LB_DATA_FORMAT,
				ALPHA_EN);
	else
		set_reg_field_value(
				value,
				0,
				LB_DATA_FORMAT,
				ALPHA_EN);

	dm_write_reg(ctx, addr, value);
}

static enum lb_pixel_depth translate_display_bpp_to_lb_depth(
	uint32_t display_bpp)
{
	switch (display_bpp) {
	case 18:
		return LB_PIXEL_DEPTH_18BPP;
	case 24:
		return LB_PIXEL_DEPTH_24BPP;
	case 36:
	case 42:
	case 48:
		return LB_PIXEL_DEPTH_36BPP;
	case 30:
	default:
		return LB_PIXEL_DEPTH_30BPP;
	}
}

bool dce110_transform_get_next_lower_pixel_storage_depth(
	struct dce110_transform *xfm110,
	uint32_t display_bpp,
	enum lb_pixel_depth depth,
	enum lb_pixel_depth *lower_depth)
{
	enum lb_pixel_depth depth_req_by_display =
		translate_display_bpp_to_lb_depth(display_bpp);
	uint32_t current_required_depth = depth_req_by_display;
	uint32_t current_depth = depth;

	/* if required display depth < current we could go down, for example
	 * from LB_PIXEL_DEPTH_30BPP to LB_PIXEL_DEPTH_24BPP
	 */
	if (current_required_depth < current_depth) {
		current_depth = current_depth >> 1;
		if (xfm110->lb_pixel_depth_supported & current_depth) {
			*lower_depth = current_depth;
			return true;
		}
	}
	return false;
}

bool dce110_transform_is_prefetch_enabled(
	struct dce110_transform *xfm110)
{
	uint32_t value = dm_read_reg(
			xfm110->base.ctx, LB_REG(mmLB_DATA_FORMAT));

	if (get_reg_field_value(value, LB_DATA_FORMAT, PREFETCH) == 1)
		return true;

	return false;
}

bool dce110_transform_get_current_pixel_storage_depth(
	struct transform *xfm,
	enum lb_pixel_depth *depth)
{
	struct dce110_transform *xfm110 = TO_DCE110_TRANSFORM(xfm);
	uint32_t value = 0;

	if (depth == NULL)
		return false;

	value = dm_read_reg(
			xfm->ctx,
			LB_REG(mmLB_DATA_FORMAT));

	switch (get_reg_field_value(value, LB_DATA_FORMAT, PIXEL_DEPTH)) {
	case 0:
		*depth = LB_PIXEL_DEPTH_30BPP;
		break;
	case 1:
		*depth = LB_PIXEL_DEPTH_24BPP;
		break;
	case 2:
		*depth = LB_PIXEL_DEPTH_18BPP;
		break;
	case 3:
		*depth = LB_PIXEL_DEPTH_36BPP;
		break;
	default:
		dal_logger_write(xfm->ctx->logger,
			LOG_MAJOR_WARNING,
			LOG_MINOR_COMPONENT_GPU,
			"%s: Invalid LB pixel depth",
			__func__);
		*depth = LB_PIXEL_DEPTH_30BPP;
		break;
	}
	return true;

}

static void set_denormalization(
	struct dce110_transform *xfm110,
	enum dc_color_depth depth)
{
	uint32_t value = dm_read_reg(xfm110->base.ctx,
			DCP_REG(mmDENORM_CONTROL));

	switch (depth) {
	case COLOR_DEPTH_666:
		/* 63/64 for 6 bit output color depth */
		set_reg_field_value(
			value,
			1,
			DENORM_CONTROL,
			DENORM_MODE);
		break;
	case COLOR_DEPTH_888:
		/* Unity for 8 bit output color depth
		 * because prescale is disabled by default */
		set_reg_field_value(
			value,
			0,
			DENORM_CONTROL,
			DENORM_MODE);
		break;
	case COLOR_DEPTH_101010:
		/* 1023/1024 for 10 bit output color depth */
		set_reg_field_value(
			value,
			3,
			DENORM_CONTROL,
			DENORM_MODE);
		break;
	case COLOR_DEPTH_121212:
		/* 4095/4096 for 12 bit output color depth */
		set_reg_field_value(
			value,
			5,
			DENORM_CONTROL,
			DENORM_MODE);
		break;
	case COLOR_DEPTH_141414:
	case COLOR_DEPTH_161616:
	default:
		/* not valid used case! */
		break;
	}

	dm_write_reg(xfm110->base.ctx,
			DCP_REG(mmDENORM_CONTROL),
			value);

}

bool dce110_transform_set_pixel_storage_depth(
	struct transform *xfm,
	enum lb_pixel_depth depth,
	const struct bit_depth_reduction_params *bit_depth_params)
{
	struct dce110_transform *xfm110 = TO_DCE110_TRANSFORM(xfm);
	bool ret = true;
	uint32_t value;
	enum dc_color_depth color_depth;

	value = dm_read_reg(xfm->ctx, LB_REG(mmLB_DATA_FORMAT));
	switch (depth) {
	case LB_PIXEL_DEPTH_18BPP:
		color_depth = COLOR_DEPTH_666;
		set_reg_field_value(value, 2, LB_DATA_FORMAT, PIXEL_DEPTH);
		set_reg_field_value(value, 1, LB_DATA_FORMAT, PIXEL_EXPAN_MODE);
		break;
	case LB_PIXEL_DEPTH_24BPP:
		color_depth = COLOR_DEPTH_888;
		set_reg_field_value(value, 1, LB_DATA_FORMAT, PIXEL_DEPTH);
		set_reg_field_value(value, 1, LB_DATA_FORMAT, PIXEL_EXPAN_MODE);
		break;
	case LB_PIXEL_DEPTH_30BPP:
		color_depth = COLOR_DEPTH_101010;
		set_reg_field_value(value, 0, LB_DATA_FORMAT, PIXEL_DEPTH);
		set_reg_field_value(value, 1, LB_DATA_FORMAT, PIXEL_EXPAN_MODE);
		break;
	case LB_PIXEL_DEPTH_36BPP:
		color_depth = COLOR_DEPTH_121212;
		set_reg_field_value(value, 3, LB_DATA_FORMAT, PIXEL_DEPTH);
		set_reg_field_value(value, 0, LB_DATA_FORMAT, PIXEL_EXPAN_MODE);
		break;
	default:
		ret = false;
		break;
	}

	if (ret == true) {
		set_denormalization(xfm110, color_depth);
		ret = program_bit_depth_reduction(xfm110, color_depth,
				bit_depth_params);

		set_reg_field_value(value, 0, LB_DATA_FORMAT, ALPHA_EN);
		dm_write_reg(xfm->ctx, LB_REG(mmLB_DATA_FORMAT), value);
		if (!(xfm110->lb_pixel_depth_supported & depth)) {
			/*we should use unsupported capabilities
			 *  unless it is required by w/a*/
			dal_logger_write(xfm->ctx->logger,
				LOG_MAJOR_WARNING,
				LOG_MINOR_COMPONENT_GPU,
				"%s: Capability not supported",
				__func__);
		}
	}

	return ret;
}

/* LB_MEMORY_CONFIG
 *  00 - Use all three pieces of memory
 *  01 - Use only one piece of memory of total 720x144 bits
 *  10 - Use two pieces of memory of total 960x144 bits
 *  11 - reserved
 *
 * LB_MEMORY_SIZE
 *  Total entries of LB memory.
 *  This number should be larger than 960. The default value is 1712(0x6B0) */
bool dce110_transform_power_up_line_buffer(struct transform *xfm)
{
	struct dce110_transform *xfm110 = TO_DCE110_TRANSFORM(xfm);
	uint32_t value;

	value = dm_read_reg(xfm110->base.ctx, LB_REG(mmLB_MEMORY_CTRL));

	/*Use all three pieces of memory always*/
	set_reg_field_value(value, 0, LB_MEMORY_CTRL, LB_MEMORY_CONFIG);
	/*hard coded number DCE11 1712(0x6B0) Partitions: 720/960/1712*/
	set_reg_field_value(value, LB_TOTAL_NUMBER_OF_ENTRIES, LB_MEMORY_CTRL,
			LB_MEMORY_SIZE);

	dm_write_reg(xfm110->base.ctx, LB_REG(mmLB_MEMORY_CTRL), value);

	return true;
}

/* Underlay pipe functions*/

bool dce110_transform_v_get_current_pixel_storage_depth(
	struct transform *xfm,
	enum lb_pixel_depth *depth)
{
	uint32_t value = 0;

	if (depth == NULL)
		return false;

	value = dm_read_reg(
			xfm->ctx,
			mmLBV_DATA_FORMAT);

	switch (get_reg_field_value(value, LBV_DATA_FORMAT, PIXEL_DEPTH)) {
	case 0:
		*depth = LB_PIXEL_DEPTH_30BPP;
		break;
	case 1:
		*depth = LB_PIXEL_DEPTH_24BPP;
		break;
	case 2:
		*depth = LB_PIXEL_DEPTH_18BPP;
		break;
	case 3:
		*depth = LB_PIXEL_DEPTH_36BPP;
		break;
	default:
		dal_logger_write(xfm->ctx->logger,
			LOG_MAJOR_WARNING,
			LOG_MINOR_COMPONENT_GPU,
			"%s: Invalid LB pixel depth",
			__func__);
		*depth = LB_PIXEL_DEPTH_30BPP;
		break;
	}
	return true;

}

bool dce110_transform_v_set_pixel_storage_depth(
	struct transform *xfm,
	enum lb_pixel_depth depth,
	const struct bit_depth_reduction_params *bit_depth_params)
{
	struct dce110_transform *xfm110 = TO_DCE110_TRANSFORM(xfm);
	bool ret = true;
	uint32_t value;
	enum dc_color_depth color_depth;

	value = dm_read_reg(xfm->ctx, mmLBV_DATA_FORMAT);
	switch (depth) {
	case LB_PIXEL_DEPTH_18BPP:
		color_depth = COLOR_DEPTH_666;
		set_reg_field_value(value, 2, LBV_DATA_FORMAT, PIXEL_DEPTH);
		set_reg_field_value(value, 1, LBV_DATA_FORMAT, PIXEL_EXPAN_MODE);
		set_reg_field_value(value, 1, LBV_DATA_FORMAT, PIXEL_REDUCE_MODE);
		set_reg_field_value(value, 1, LBV_DATA_FORMAT, DITHER_EN);
		set_reg_field_value(value, 1, LBV_DATA_FORMAT, DOWNSCALE_PREFETCH_EN);
		break;
	case LB_PIXEL_DEPTH_24BPP:
		color_depth = COLOR_DEPTH_888;
		set_reg_field_value(value, 1, LBV_DATA_FORMAT, PIXEL_DEPTH);
		set_reg_field_value(value, 1, LBV_DATA_FORMAT, PIXEL_EXPAN_MODE);
		set_reg_field_value(value, 1, LBV_DATA_FORMAT, PIXEL_REDUCE_MODE);
		set_reg_field_value(value, 0, LBV_DATA_FORMAT, DITHER_EN);
		set_reg_field_value(value, 1, LBV_DATA_FORMAT, DOWNSCALE_PREFETCH_EN);
		break;
	case LB_PIXEL_DEPTH_30BPP:
		color_depth = COLOR_DEPTH_101010;
		set_reg_field_value(value, 0, LBV_DATA_FORMAT, PIXEL_DEPTH);
		set_reg_field_value(value, 1, LBV_DATA_FORMAT, PIXEL_EXPAN_MODE);
		set_reg_field_value(value, 1, LBV_DATA_FORMAT, PIXEL_REDUCE_MODE);
		set_reg_field_value(value, 0, LBV_DATA_FORMAT, DITHER_EN);
		set_reg_field_value(value, 1, LBV_DATA_FORMAT, DOWNSCALE_PREFETCH_EN);
		break;
	case LB_PIXEL_DEPTH_36BPP:
		color_depth = COLOR_DEPTH_121212;
		set_reg_field_value(value, 3, LBV_DATA_FORMAT, PIXEL_DEPTH);
		set_reg_field_value(value, 0, LBV_DATA_FORMAT, PIXEL_EXPAN_MODE);
		set_reg_field_value(value, 0, LBV_DATA_FORMAT, PIXEL_REDUCE_MODE);
		set_reg_field_value(value, 0, LBV_DATA_FORMAT, DITHER_EN);
		set_reg_field_value(value, 1, LBV_DATA_FORMAT, DOWNSCALE_PREFETCH_EN);
		break;
	default:
		ret = false;
		break;
	}

	if (ret == true) {
		set_denormalization(xfm110, color_depth);
		ret = program_bit_depth_reduction(xfm110, color_depth,
				bit_depth_params);

		set_reg_field_value(value, 0, LBV_DATA_FORMAT, ALPHA_EN);
		dm_write_reg(xfm->ctx, mmLBV_DATA_FORMAT, value);
		if (!(xfm110->lb_pixel_depth_supported & depth)) {
			/*we should use unsupported capabilities
			 *  unless it is required by w/a*/
			dal_logger_write(xfm->ctx->logger,
				LOG_MAJOR_WARNING,
				LOG_MINOR_COMPONENT_GPU,
				"%s: Capability not supported",
				__func__);
		}
	}

	return ret;
}

