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

#ifndef __DAL_HW_CTX_EXTERNAL_DIGITAL_ENCODER_TRAVIS_H__
#define __DAL_HW_CTX_EXTERNAL_DIGITAL_ENCODER_TRAVIS_H__

#include "hw_ctx_external_digital_encoder_hal.h"

enum lvds_pwrseq_state {
	LVDS_PWRSEQ_STATE_DISABLED = 0,
	LVDS_PWRSEQ_STATE_POWER_UP0 = 1,
	LVDS_PWRSEQ_STATE_POWER_UP1 = 2,
	LVDS_PWRSEQ_STATE_POWER_UP2 = 3,
	LVDS_PWRSEQ_STATE_POWER_UP_DONE = 4,
	LVDS_PWRSEQ_STATE_POWER_DOWN0 = 5,
	LVDS_PWRSEQ_STATE_POWER_DOWN1 = 6,
	LVDS_PWRSEQ_STATE_POWER_DOWN2 = 7,
	LVDS_PWRSEQ_STATE_DELAY = 8,
	LVDS_PWRSEQ_STATE_POWER_DOWN_DONE = 9,
	LVDS_PWRSEQ_STATE_INVALID,
};

union travis_pwrseq_status {
	struct {
		uint8_t DIG_ON:1;
		uint8_t SYNC_EN:1;
		uint8_t BL_ON:1;
		uint8_t DONE:1;
		uint8_t STATE:4;
	} bits;
	uint8_t raw;
};

struct hw_ctx_external_digital_encoder_hal *
	dal_hw_ctx_external_digital_encoder_travis_create(
		struct dal_context *dal_ctx);

union travis_pwrseq_status
	dal_hw_ctx_external_digital_encoder_travis_get_pwrseq_status(
	struct hw_ctx_external_digital_encoder_hal *ctx,
	enum channel_id channel_id);

#endif
