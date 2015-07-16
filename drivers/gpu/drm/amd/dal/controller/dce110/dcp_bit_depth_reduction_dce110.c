/*
 * Copyright 2012-14 Advanced Micro Devices, Inc.
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

#include "include/grph_object_id.h"
#include "include/adapter_service_interface.h"

#include "dcp_bit_depth_reduction_dce110.h"

enum dcp_bit_depth_reduction_regs_idx {
	IDX_OUT_CLAMP_CONTROL_B_CB,
	IDX_OUT_CLAMP_CONTROL_G_Y,
	IDX_OUT_CLAMP_CONTROL_R_CR,
	IDX_OUT_ROUND_CONTROL,
	IDX_DCP_SPATIAL_DITHER_CNTL,
	DCP_BIT_DEPTH_REDUCTION_REGS_IDX_SIZE
};

#define regs_for_dcp_bit_depth_reduction(id)\
	[CONTROLLER_ID_D ## id - 1] = {\
		[IDX_DCP_SPATIAL_DITHER_CNTL] =\
			mmDCP ## id ## _DCP_SPATIAL_DITHER_CNTL,\
		[IDX_OUT_CLAMP_CONTROL_B_CB] =\
			mmDCP ## id ## _OUT_CLAMP_CONTROL_B_CB,\
		[IDX_OUT_CLAMP_CONTROL_G_Y] =\
			mmDCP ## id ## _OUT_CLAMP_CONTROL_G_Y,\
		[IDX_OUT_CLAMP_CONTROL_R_CR] =\
			mmDCP ## id ## _OUT_CLAMP_CONTROL_R_CR,\
		[IDX_OUT_ROUND_CONTROL] = mmDCP ## id ## _OUT_ROUND_CONTROL,\
	}

static const uint32_t
dcp_bit_depth_reduction_regs[][DCP_BIT_DEPTH_REDUCTION_REGS_IDX_SIZE] = {
	regs_for_dcp_bit_depth_reduction(0),
	regs_for_dcp_bit_depth_reduction(1),
	regs_for_dcp_bit_depth_reduction(2)
};

static bool dcp_bit_depth_reduction_dce110_construct(
	struct dcp_bit_depth_reduction_dce110 *bdr,
	struct dal_context *ctx,
	enum controller_id id,
	struct adapter_service *as);

struct dcp_bit_depth_reduction_dce110
*dal_dcp_bit_depth_reduction_dce110_create(
	enum controller_id id,
	struct dal_context *ctx,
	struct adapter_service *as)
{
	struct dcp_bit_depth_reduction_dce110 *bdr =
		dal_alloc(sizeof(struct dcp_bit_depth_reduction_dce110));

	if (!bdr)
		return NULL;

	if (dcp_bit_depth_reduction_dce110_construct(bdr, ctx, id, as))
		return bdr;

	dal_free(bdr);
	return NULL;
}

void dal_dcp_bit_depth_reduction_dce110_destroy(
	struct dcp_bit_depth_reduction_dce110 **bdr)
{
	if (!bdr)
		return;
	if (!*bdr)
		return;

	dal_free(*bdr);
	*bdr = NULL;
}

static bool dcp_bit_depth_reduction_dce110_construct(
	struct dcp_bit_depth_reduction_dce110 *bdr,
	struct dal_context *ctx,
	enum controller_id id,
	struct adapter_service *as)
{
	if (!as)
		return false;
	bdr->ctx = ctx;
	bdr->regs = dcp_bit_depth_reduction_regs[id - 1];
	bdr->as = as;
	return true;
}

static bool set_clamp(
	struct dcp_bit_depth_reduction_dce110 *bdr,
	enum csc_color_depth depth);
static bool set_round(
	struct dcp_bit_depth_reduction_dce110 *bdr,
	enum dcp_out_trunc_round_mode mode,
	enum dcp_out_trunc_round_depth depth);
static bool set_dither(
	struct dcp_bit_depth_reduction_dce110 *bdr,
	bool dither_enable,
	enum dcp_spatial_dither_mode dither_mode,
	enum dcp_spatial_dither_depth dither_depth,
	bool frame_random_enable,
	bool rgb_random_enable,
	bool highpass_random_enable);

/**
 *******************************************************************************
 * dal_dcp_bit_depth_reduction_dce110_program
 *
 * @brief
 *     Programs the DCP bit depth reduction registers (Clamp, Round/Truncate,
 *      Dither)
 *
 * @param depth : bit depth to set the clamp to (should match denorm)
 *
 * @return
 *     true if succeeds.
 *******************************************************************************
 */
bool dal_dcp_bit_depth_reduction_dce110_program(
	struct dcp_bit_depth_reduction_dce110 *bdr,
	enum csc_color_depth depth)
{
	enum dcp_bit_depth_reduction_mode depth_reduction_mode;
	enum dcp_spatial_dither_mode spatial_dither_mode;
	bool frame_random_enable;
	bool rgb_random_enable;
	bool highpass_random_enable;

	if (depth > CSC_COLOR_DEPTH_121212) {
		ASSERT_CRITICAL(false); /* Invalid clamp bit depth */
		return false;
	}

	depth_reduction_mode = DCP_BIT_DEPTH_REDUCTION_MODE_INVALID;
	if (!dal_adapter_service_get_feature_value(
			FEATURE_DCP_BIT_DEPTH_REDUCTION_MODE,
			&depth_reduction_mode,
			sizeof(depth_reduction_mode))) {
		/* Failed to get value for
		 * FEATURE_DCP_BIT_DEPTH_REDUCTION_MODE */
		ASSERT_CRITICAL(false);
		return false;
	}

	spatial_dither_mode = DCP_SPATIAL_DITHER_MODE_INVALID;
	if (!dal_adapter_service_get_feature_value(
			FEATURE_DCP_DITHER_MODE, &spatial_dither_mode,
			sizeof(spatial_dither_mode))) {
		/* Failed to get value for
		 * FEATURE_DCP_DITHER_MODE */
		ASSERT_CRITICAL(false);
		return false;
	}

	frame_random_enable = false;
	if (!dal_adapter_service_get_feature_value(
			FEATURE_DCP_DITHER_FRAME_RANDOM_ENABLE,
			&frame_random_enable,
			sizeof(frame_random_enable))) {
		/* Failed to get value for
		 * FEATURE_DCP_DITHER_FRAME_RANDOM_ENABLE */
		ASSERT_CRITICAL(false);
		return false;
	}

	rgb_random_enable = false;
	if (!dal_adapter_service_get_feature_value(
			FEATURE_DCP_DITHER_RGB_RANDOM_ENABLE,
			&rgb_random_enable, sizeof(rgb_random_enable))) {
		/* Failed to get value for
		 * FEATURE_DCP_DITHER_RGB_RANDOM_ENABLE */
		ASSERT_CRITICAL(false);
		return false;
	}

	highpass_random_enable = false;
	if (!dal_adapter_service_get_feature_value(
			FEATURE_DCP_DITHER_HIGH_PASS_RANDOM_ENABLE,
			&highpass_random_enable,
			sizeof(highpass_random_enable))) {
		/* Failed to get value for
		 * FEATURE_DCP_DITHER_HIGH_PASS_RANDOM_ENABLE */
		ASSERT_CRITICAL(false);
		return false;
	}

	if (!set_clamp(bdr, depth)) {
		/* Failure in set_clamp() */
		ASSERT_CRITICAL(false);
		return false;
	}
	switch (depth_reduction_mode) {
	case DCP_BIT_DEPTH_REDUCTION_MODE_DITHER:
		/*  Spatial Dither: Set round/truncate to bypass (12bit),
		 *  enable Dither (30bpp) */
		set_round(bdr,
			DCP_OUT_TRUNC_ROUND_MODE_TRUNCATE,
			DCP_OUT_TRUNC_ROUND_DEPTH_12BIT);

		set_dither(bdr, true, spatial_dither_mode,
			DCP_SPATIAL_DITHER_DEPTH_30BPP, frame_random_enable,
			rgb_random_enable, highpass_random_enable);
		break;
	case DCP_BIT_DEPTH_REDUCTION_MODE_ROUND:
		/*  Round: Enable round (10bit), disable Dither */
		set_round(bdr,
			DCP_OUT_TRUNC_ROUND_MODE_ROUND,
			DCP_OUT_TRUNC_ROUND_DEPTH_10BIT);

		set_dither(bdr, false, spatial_dither_mode,
			DCP_SPATIAL_DITHER_DEPTH_30BPP, frame_random_enable,
			rgb_random_enable, highpass_random_enable);
		break;
	case DCP_BIT_DEPTH_REDUCTION_MODE_TRUNCATE: /*  Truncate */
		/*  Truncate: Enable truncate (10bit), disable Dither */
		set_round(bdr,
			DCP_OUT_TRUNC_ROUND_MODE_TRUNCATE,
			DCP_OUT_TRUNC_ROUND_DEPTH_10BIT);

		set_dither(bdr, false, spatial_dither_mode,
			DCP_SPATIAL_DITHER_DEPTH_30BPP, frame_random_enable,
			rgb_random_enable, highpass_random_enable);
		break;

	case DCP_BIT_DEPTH_REDUCTION_MODE_DISABLED: /*  Disabled */
		/*  Truncate: Set round/truncate to bypass (12bit),
		 * disable Dither */
		set_round(bdr,
			DCP_OUT_TRUNC_ROUND_MODE_TRUNCATE,
			DCP_OUT_TRUNC_ROUND_DEPTH_12BIT);

		set_dither(bdr, false, spatial_dither_mode,
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
	struct dcp_bit_depth_reduction_dce110 *bdr,
	enum csc_color_depth depth)
{
	uint32_t clamp_max = 0;

	/* At the clamp block the data will be MSB aligned, so we set the max
	 * clamp accordingly.
	 * For example, the max value for 6 bits MSB aligned (14 bit bus) would
	 * be "11 1111 0000 0000" in binary, so 0x3F00.
	 */
	switch (depth) {
	case CSC_COLOR_DEPTH_666:
		/* 6bit MSB aligned on 14 bit bus '11 1111 0000 0000' */
		clamp_max = 0x3F00;
		break;
	case CSC_COLOR_DEPTH_888:
		/* 8bit MSB aligned on 14 bit bus '11 1111 1100 0000' */
		clamp_max = 0x3FC0;
		break;
	case CSC_COLOR_DEPTH_101010:
		/* 10bit MSB aligned on 14 bit bus '11 1111 1111 1100' */
		clamp_max = 0x3FFC;
		break;
	case CSC_COLOR_DEPTH_111111:
		/* 11bit MSB aligned on 14 bit bus '11 1111 1111 1110' */
		clamp_max = 0x3FFE;
		break;
	case CSC_COLOR_DEPTH_121212:
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

		dal_write_reg(bdr->ctx,
			bdr->regs[IDX_OUT_CLAMP_CONTROL_B_CB],
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

		dal_write_reg(bdr->ctx,
			bdr->regs[IDX_OUT_CLAMP_CONTROL_G_Y],
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

		dal_write_reg(bdr->ctx,
			bdr->regs[IDX_OUT_CLAMP_CONTROL_R_CR],
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
	struct dcp_bit_depth_reduction_dce110 *bdr,
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
	dal_write_reg(bdr->ctx,
		bdr->regs[IDX_OUT_ROUND_CONTROL], value);

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
	struct dcp_bit_depth_reduction_dce110 *bdr,
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
	dal_write_reg(bdr->ctx,
		bdr->regs[IDX_DCP_SPATIAL_DITHER_CNTL], value);

	return true;
}
