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

#ifndef __DAL_HW_CTX_DIGITAL_ENCODER_DCE110_H__
#define __DAL_HW_CTX_DIGITAL_ENCODER_DCE110_H__

/*
 * @brief
 * Trigger Source Select
 * ASIC-dependent, actual values for register programming
 */
enum dce110_dig_fe_source_select {
	DCE110_DIG_FE_SOURCE_SELECT_INVALID = 0x0,
	DCE110_DIG_FE_SOURCE_SELECT_DIGA = 0x1,
	DCE110_DIG_FE_SOURCE_SELECT_DIGB = 0x2,
	DCE110_DIG_FE_SOURCE_SELECT_DIGC = 0x4,
};

struct hw_ctx_digital_encoder_dce110 {
	struct hw_ctx_digital_encoder_hal base;
	/* HW encoder block MM register offset */
	int32_t be_engine_offset;
	int32_t transmitter_offset;
	/* HPD_INT_XXX MM register offset */
	int32_t hpd_offset;
	/* DC_GPIO_DDC_XXX MM register offset */
	int32_t channel_offset;
	/* AUX_XXX MM register offset */
	int32_t aux_channel_offset;
	/*TODO add support for this test pattern*/
	bool support_dp_hbr2_eye_pattern;
};

#define HWCTX_DIGITAL_ENC110_FROM_BASE(encoder_hal) \
	container_of(encoder_hal, struct hw_ctx_digital_encoder_dce110, base)

bool dal_hw_ctx_digital_encoder_dce110_construct(
	struct hw_ctx_digital_encoder_dce110 *ctx,
	const struct hw_ctx_init *init);

void dal_hw_ctx_digital_encoder_dce110_destruct(
	struct hw_ctx_digital_encoder_dce110 *ctx);

struct hw_ctx_digital_encoder_hal *dal_hw_ctx_digital_encoder_dce110_create(
		const struct hw_ctx_init *init);

#endif /* __DAL_HW_CTX_DIGITAL_ENCODER_DCE110_H__ */
