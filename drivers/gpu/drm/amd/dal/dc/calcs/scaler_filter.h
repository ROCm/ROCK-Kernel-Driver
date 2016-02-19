/* Copyright 2012-15 Advanced Micro Devices, Inc.
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

#ifndef __DAL_SCALER_FILTER_H__
#define __DAL_SCALER_FILTER_H__

struct scaler_filter_params {
	uint32_t taps; /* 3...16 */
	uint32_t phases;
	int32_t sharpness; /* -50...50 */
	union {
		struct {
			uint32_t HORIZONTAL:1;
			uint32_t RESERVED:31;
		} bits;
		uint32_t value;
	} flags;
};

struct scaler_filter {
	struct scaler_filter_params params;
	uint32_t src_size;
	uint32_t dst_size;
	struct fixed31_32 *filter;
	uint32_t *integer_filter;
	uint32_t filter_size_allocated;
	uint32_t filter_size_effective;
	struct fixed31_32 *coefficients;
	uint32_t coefficients_quantity;
	struct fixed31_32 *coefficients_sum;
	uint32_t coefficients_sum_quantity;
	struct fixed31_32 ***downscaling_table;
	struct fixed31_32 ***upscaling_table;
	struct dc_context *ctx;
};

struct scaler_filter *dal_scaler_filter_create(struct dc_context *ctx);
void dal_scaler_filter_destroy(struct scaler_filter **ptr);

bool dal_scaler_filter_generate(
	struct scaler_filter *filter,
	const struct scaler_filter_params *params,
	uint32_t src_size,
	uint32_t dst_size);

const struct fixed31_32 *dal_scaler_filter_get(
	const struct scaler_filter *filter,
	uint32_t **data,
	uint32_t *number);

#endif
