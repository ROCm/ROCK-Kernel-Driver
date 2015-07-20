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

#ifndef __DAL_SCALER_H__
#define __DAL_SCALER_H__

#include "include/bios_parser_interface.h"
#include "include/scaler_types.h"
#include "scaler_filter.h"

struct scaler;

struct scaler_funcs {
	enum scaler_validation_code (*get_optimal_taps_number)(
		struct scaler_validation_params *params,
		struct scaling_tap_info *taps);
	enum scaler_validation_code (*get_next_lower_taps_number)(
		struct scaler_validation_params *params,
		struct scaling_tap_info *taps);
	void (*set_scaler_bypass)(struct scaler *scl);
	bool (*is_scaling_enabled)(struct scaler *scl);
	bool (*set_scaler_wrapper)(
		struct scaler *scl,
		const struct scaler_data *data);
	void (*get_viewport)(
			struct scaler *scl,
			struct rect *current_view_port);
	void (*program_viewport)(
		struct scaler *scl,
		const struct rect *view_port);
	void (*destroy)(struct scaler **scl);
};

struct scaler_init_data {
	struct bios_parser *bp;
	struct dal_context *dal_ctx;
	enum controller_id id;
};

struct scaler {
	const struct scaler_funcs *funcs;
	struct bios_parser *bp;
	enum controller_id id;
	const uint32_t *regs;
	struct scaler_filter *filter;
	struct dal_context *ctx;
};

enum scaling_type {
	SCALING_TYPE_NO_SCALING = 0,
	SCALING_TYPE_UPSCALING,
	SCALING_TYPE_DOWNSCALING
};

struct scaler_taps_and_ratio {
	uint32_t h_tap;
	uint32_t v_tap;
	uint32_t lo_ratio;
	uint32_t hi_ratio;
};

struct scaler_taps {
	uint32_t h_tap;
	uint32_t v_tap;
};

bool dal_scaler_construct(
	struct scaler *scl,
	struct scaler_init_data *init_data);

enum scaler_validation_code dal_scaler_get_optimal_taps_number(
	struct scaler_validation_params *params,
	struct scaling_tap_info *taps);
enum scaler_validation_code dal_scaler_get_next_lower_taps_number(
	struct scaler_validation_params *params,
	struct scaling_tap_info *taps);

bool dal_scaler_update_viewport(
	struct scaler *scl,
	const struct rect *view_port,
	bool is_fbc_attached);

#endif
