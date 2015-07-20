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

#ifndef __DAL_EXTERNAL_DIGITAL_ENCODER_H__
#define __DAL_EXTERNAL_DIGITAL_ENCODER_H__

#include "encoder_impl.h"

struct external_digital_encoder;

struct external_digital_encoder_funcs {
	/* create HW context */
	enum encoder_result (*create_hw_ctx)(
		struct external_digital_encoder *enc,
		const struct hw_ctx_init *init);
};

struct external_digital_encoder {
	struct encoder_impl base;
	const struct external_digital_encoder_funcs *funcs;
	struct hw_ctx_external_digital_encoder_hal *hw_ctx;
};

#define FROM_ENCODER_IMPL(ptr) \
	container_of((ptr), struct external_digital_encoder, base)

bool dal_external_digital_encoder_construct(
	struct external_digital_encoder *enc,
	const struct encoder_init_data *init_data);

void dal_external_digital_encoder_destruct(
	struct external_digital_encoder *enc);

enum encoder_result dal_external_digital_encoder_initialize(
	struct encoder_impl *enc,
	const struct encoder_context *ctx);

enum encoder_result dal_external_digital_encoder_power_up(
	struct encoder_impl *enc,
	const struct encoder_context *ctx);

#endif
