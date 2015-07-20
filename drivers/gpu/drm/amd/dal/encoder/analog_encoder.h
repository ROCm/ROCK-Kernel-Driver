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

#ifndef __DAL_ANALOG_ENCODER_H__
#define __DAL_ANALOG_ENCODER_H__

struct analog_encoder;

struct analog_encoder_funcs {
	enum encoder_result (*create_hw_ctx)(
		struct analog_encoder *enc);
};

enum signal_type dal_analog_encoder_detect_load(
	struct encoder_impl *enc,
	const struct encoder_context *ctx);

union supported_stream_engines dal_analog_encoder_get_supported_stream_engines(
	const struct encoder_impl *enc);

struct analog_encoder {
	struct encoder_impl base;
	const struct analog_encoder_funcs *funcs;
	struct hw_ctx_analog_encoder *hw_ctx;
};

enum encoder_result dal_analog_encoder_initialize(
	struct encoder_impl *enc,
	const struct encoder_context *ctx);

bool dal_analog_encoder_is_sink_present(
	struct encoder_impl *enc,
	struct graphics_object_id downstream);

enum encoder_result dal_analog_encoder_disable_sync_output(
	struct encoder_impl *enc);

enum encoder_result dal_analog_encoder_enable_sync_output(
	struct encoder_impl *enc,
	enum sync_source src);

enum encoder_result dal_analog_encoder_setup_stereo(
	struct encoder_impl *enc,
	const struct encoder_3d_setup *setup);

bool dal_analog_encoder_construct(
	struct analog_encoder *enc,
	const struct encoder_init_data *init_data);

void dal_analog_encoder_destruct(
	struct analog_encoder *enc);

enum encoder_result dal_analog_encoder_power_up(
	struct encoder_impl *enc,
	const struct encoder_context *ctx);



#endif
