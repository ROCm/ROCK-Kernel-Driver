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

#ifndef __DAL_TRANSFORM_H__
#define __DAL_TRANSFORM_H__

#include "include/scaler_types.h"
#include "include/grph_csc_types.h"
#include "calcs/scaler_filter.h"
#include "grph_object_id.h"

enum scaling_type {
	SCALING_TYPE_NO_SCALING = 0,
	SCALING_TYPE_UPSCALING,
	SCALING_TYPE_DOWNSCALING
};

struct transform {
	struct transform_funcs *funcs;
	struct dc_context *ctx;
	uint32_t inst;
	struct scaler_filter *filter;
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

struct sclv_ratios_inits {
	uint32_t chroma_enable;
	uint32_t h_int_scale_ratio_luma;
	uint32_t h_int_scale_ratio_chroma;
	uint32_t v_int_scale_ratio_luma;
	uint32_t v_int_scale_ratio_chroma;
	struct init_int_and_frac h_init_luma;
	struct init_int_and_frac h_init_chroma;
	struct init_int_and_frac v_init_luma;
	struct init_int_and_frac v_init_chroma;
	struct init_int_and_frac h_init_lumabottom;
	struct init_int_and_frac h_init_chromabottom;
	struct init_int_and_frac v_init_lumabottom;
	struct init_int_and_frac v_init_chromabottom;
};

enum lb_pixel_depth {
	/* do not change the values because it is used as bit vector */
	LB_PIXEL_DEPTH_18BPP = 1,
	LB_PIXEL_DEPTH_24BPP = 2,
	LB_PIXEL_DEPTH_30BPP = 4,
	LB_PIXEL_DEPTH_36BPP = 8
};

struct transform_funcs {
	bool (*transform_power_up)(struct transform *xfm);

	bool (*transform_set_scaler)(
		struct transform *xfm,
		const struct scaler_data *data);

	void (*transform_set_scaler_bypass)(struct transform *xfm);

	bool (*transform_update_viewport)(
		struct transform *xfm,
		const struct rect *view_port,
		bool is_fbc_attached);

	void (*transform_set_scaler_filter)(
		struct transform *xfm,
		struct scaler_filter *filter);

	void (*transform_set_gamut_remap)(
		struct transform *xfm,
		const struct grph_csc_adjustment *adjust);

	bool (*transform_set_pixel_storage_depth)(
		struct transform *xfm,
		enum lb_pixel_depth depth);

	bool (*transform_get_current_pixel_storage_depth)(
		struct transform *xfm,
		enum lb_pixel_depth *depth);
};

#endif
