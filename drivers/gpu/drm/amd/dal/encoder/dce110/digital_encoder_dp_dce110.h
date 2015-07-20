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

#ifndef __DAL_DIGITAL_ENCODER_DP_DCE110_H__
#define __DAL_DIGITAL_ENCODER_DP_DCE110_H__

struct digital_encoder_dp_dce110 {
	struct digital_encoder_dp base;
};

enum encoder_result dal_digital_encoder_dp_dce110_initialize(
	struct encoder_impl *enc,
	const struct encoder_context *ctx);

union supported_stream_engines
	dal_digital_encoder_dp_dce110_get_supported_stream_engines(
	const struct encoder_impl *impl);

bool dal_digital_encoder_dp_dce110_is_link_settings_supported(
	struct encoder_impl *impl,
	const struct link_settings *link_settings);

bool dal_digital_encoder_dp_dce110_construct(
	struct digital_encoder_dp_dce110 *enc,
	const struct encoder_init_data *init_data);

void dal_digital_encoder_dp_dce110_destruct(
	struct digital_encoder_dp_dce110 *enc);

struct encoder_impl *dal_digital_encoder_dp_dce110_create(
	const struct encoder_init_data *init);

#endif
