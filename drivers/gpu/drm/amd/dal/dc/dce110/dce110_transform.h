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

#ifndef __DAL_TRANSFORM_DCE110_H__
#define __DAL_TRANSFORM_DCE110_H__

#include "transform.h"

#define TO_DCE110_TRANSFORM(transform)\
	container_of(transform, struct dce110_transform, base)

struct dce110_transform_reg_offsets {
	uint32_t scl_offset;
	uint32_t dcfe_offset;
	uint32_t dcp_offset;
	uint32_t lb_offset;
};

struct dce110_transform {
	struct transform base;
	struct dce110_transform_reg_offsets offsets;

	uint32_t lb_pixel_depth_supported;
};

bool dce110_transform_construct(struct dce110_transform *xfm110,
	struct dc_context *ctx,
	uint32_t inst,
	const struct dce110_transform_reg_offsets *reg_offsets);

bool dce110_transform_power_up(struct transform *xfm);

/* SCALER RELATED */
bool dce110_transform_set_scaler(
	struct transform *xfm,
	const struct scaler_data *data);

void dce110_transform_set_scaler_bypass(
		struct transform *xfm,
		const struct rect *size);

void dce110_transform_set_scaler_filter(
	struct transform *xfm,
	struct scaler_filter *filter);

/* GAMUT RELATED */
void dce110_transform_set_gamut_remap(
	struct transform *xfm,
	const struct xfm_grph_csc_adjustment *adjust);

/* BIT DEPTH RELATED */
bool dce110_transform_set_pixel_storage_depth(
	struct transform *xfm,
	enum lb_pixel_depth depth,
	const struct bit_depth_reduction_params *bit_depth_params);

bool dce110_transform_get_current_pixel_storage_depth(
	struct transform *xfm,
	enum lb_pixel_depth *depth);

void dce110_transform_set_alpha(struct transform *xfm, bool enable);

#endif
