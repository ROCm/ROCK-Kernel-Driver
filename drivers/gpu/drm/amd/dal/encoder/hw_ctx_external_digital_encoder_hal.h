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

#ifndef __DAL_HW_CTX_EXTERNAL_DIGITAL_ENCODER_HAL_H__
#define __DAL_HW_CTX_EXTERNAL_DIGITAL_ENCODER_HAL_H__

#include "hw_ctx_digital_encoder.h"

struct display_ppll_divider {
	uint32_t ref_div;
	uint32_t post_div;
	uint32_t feedback_div_integer;
	uint32_t feedback_div_fraction;
};

struct hw_ctx_external_digital_encoder_hal;
struct hw_crtc_timing;

struct hw_ctx_external_digital_encoder_hal_funcs {
	void (*power_up)(
		struct hw_ctx_external_digital_encoder_hal *ctx,
		enum channel_id channel_id);
	void (*power_down)(
		struct hw_ctx_external_digital_encoder_hal *ctx,
		enum channel_id channel_id);
	void (*setup_frontend_phy)(
		struct hw_ctx_external_digital_encoder_hal *ctx,
		enum channel_id channel_id,
		const struct link_settings *link_settings);
	void (*setup_display_engine)(
		struct hw_ctx_external_digital_encoder_hal *ctx,
		enum channel_id channel_id,
		const struct display_ppll_divider *display_ppll_divider);
	void (*enable_output)(
		struct hw_ctx_external_digital_encoder_hal *ctx,
		enum channel_id channel_id,
		const struct link_settings *link_settings,
		const struct hw_crtc_timing *timing);
	void (*disable_output)(
		struct hw_ctx_external_digital_encoder_hal *ctx,
		enum channel_id channel_id);
	void (*blank)(
		struct hw_ctx_external_digital_encoder_hal *ctx,
		enum channel_id channel_id);
	void (*unblank)(
		struct hw_ctx_external_digital_encoder_hal *ctx,
		enum channel_id channel_id);
	enum signal_type (*detect_load)(
		struct hw_ctx_external_digital_encoder_hal *ctx,
		enum channel_id channel_id,
		enum signal_type display_signal);
	void (*pre_ddc)(
		struct hw_ctx_external_digital_encoder_hal *ctx,
		enum channel_id channel_id);
	void (*post_ddc)(
		struct hw_ctx_external_digital_encoder_hal *ctx,
		enum channel_id channel_id);
	bool (*requires_authentication)(
		const struct hw_ctx_external_digital_encoder_hal *ctx,
		enum channel_id channel_id);
};

struct hw_ctx_external_digital_encoder_hal {
	struct hw_ctx_digital_encoder base;
	const struct hw_ctx_external_digital_encoder_hal_funcs *funcs;
};

bool dal_hw_ctx_external_digital_encoder_hal_requires_authentication(
	const struct hw_ctx_external_digital_encoder_hal *ctx,
	enum channel_id channel_id);

bool dal_hw_ctx_external_digital_encoder_hal_construct(
	struct hw_ctx_external_digital_encoder_hal *ctx,
	struct dal_context *dal_ctx);

void dal_hw_ctx_external_digital_encoder_hal_destruct(
	struct hw_ctx_external_digital_encoder_hal *ctx);

#endif
