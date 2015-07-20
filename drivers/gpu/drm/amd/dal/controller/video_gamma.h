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

#ifndef __DAL_VIDEO_GAMMA_H__
#define __DAL_VIDEO_GAMMA_H__

#include "include/grph_object_id.h"
#include "include/video_gamma_types.h"

#include "graphics_and_video_gamma.h"

/* overlay could use 1/2 of max 256 points which are hardwire capabilities */
#define MAX_GAMMA_OVERLAY_POINTS 128
#define MAX_GAMMA_256X3X16 256 /* number of points used by OS */
/* begin point, end point, end + 1 point for slope */
#define CONFIG_POINTS_NUMBER 3

struct gamma_sample {
	struct fixed31_32 point_x;
	struct fixed31_32 point_y;
};

struct pixel_gamma_point_ex {
	struct gamma_point r;
	struct gamma_point g;
	struct gamma_point b;
};

struct gamma_work_item {
	struct gamma_coefficients coeff;
	struct gamma_sample gamma_sample[MAX_GAMMA_256X3X16];

	struct fixed31_32 x_axis[MAX_GAMMA_OVERLAY_POINTS];
	struct fixed31_32 y_axis[MAX_GAMMA_OVERLAY_POINTS];

	struct gamma_point gamma_points[MAX_GAMMA_OVERLAY_POINTS];
	struct pwl_float_data regamma[MAX_GAMMA_OVERLAY_POINTS];
	struct pwl_float_data_ex resulted[MAX_GAMMA_OVERLAY_POINTS];
	struct gamma_curve curve[MAX_REGIONS_NUMBER];
	struct curve_points points[CONFIG_POINTS_NUMBER];

	struct gamma_point coeff_oem128[MAX_GAMMA_OVERLAY_POINTS];
	struct pwl_float_data oem_regamma[MAX_GAMMA_256X3X16];
	struct fixed31_32 axis_x256[MAX_GAMMA_256X3X16];

	struct pixel_gamma_point_ex coeff_128[MAX_GAMMA_OVERLAY_POINTS];
};

struct video_gamma;
struct overlay_gamma_parameters;

bool dal_video_gamma_build_resulted_gamma(
	struct video_gamma *vg,
	const struct overlay_gamma_parameters *data,
	uint32_t *lut,
	uint32_t *lut_delta,
	uint32_t *entries_num);

struct video_gamma_funcs {
	bool (*set_overlay_pwl_adjustment)(
		struct video_gamma *vg,
		const struct overlay_gamma_parameters *data);
	void (*regamma_config_regions_and_segments)(
		struct video_gamma *vg,
		const struct curve_points *points,
		const struct gamma_curve *curve);
	void (*set_legacy_mode)(
		struct video_gamma *vg,
		bool is_legacy);
	bool (*set_overlay_gamma)(
		struct video_gamma *vg,
		const struct overlay_gamma_parameters *data);
	void (*destroy)(struct video_gamma **vg);
};

struct video_gamma {
	const struct video_gamma_funcs *funcs;
	const uint32_t *regs;
	struct dal_context *ctx;
};

struct video_gamma_init_data {
	struct adapter_service *as;
	struct dal_context *ctx;
	enum controller_id id;
};

bool dal_video_gamma_construct(
	struct video_gamma *vg,
	struct video_gamma_init_data *init_data);

#endif
