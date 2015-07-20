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

#ifndef __DAL_HW_CTX_ANALOG_ENCODER_H__
#define __DAL_HW_CTX_ANALOG_ENCODER_H__

struct hw_ctx_analog_encoder;

struct hw_ctx_analog_encoder_funcs {
	/* destroy instance - mandatory method! */
	void (*destroy)(
		struct hw_ctx_analog_encoder **ptr);
	bool (*setup_stereo_sync)(
		struct hw_ctx_analog_encoder *ctx,
		enum engine_id engine,
		enum sync_source source);
	bool (*enable_sync_output)(
		struct hw_ctx_analog_encoder *ctx,
		enum engine_id engine,
		enum sync_source source);
	bool (*disable_sync_output)(
		struct hw_ctx_analog_encoder *ctx,
		enum engine_id engine);
	bool (*is_output_enabled)(
		struct hw_ctx_analog_encoder *ctx,
		enum engine_id engine);
};

struct hw_ctx_analog_encoder {
	const struct hw_ctx_analog_encoder_funcs *funcs;
	struct dal_context *dal_ctx;
};

bool dal_hw_ctx_analog_encoder_construct(
	struct hw_ctx_analog_encoder *ctx,
	struct dal_context *dal_ctx);

void dal_hw_ctx_analog_encoder_destruct(
	struct hw_ctx_analog_encoder *ctx);

#endif
