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

#ifndef _DCE110_TYPES_H_
#define __DCE110_TYPES_H_

#define GAMMA_SEGMENTS_NUM 16
struct end_point {
	uint32_t x_value;
	uint32_t y_value;
	uint32_t slope;
};

struct pwl_segment {
	uint32_t r_value;
	uint32_t g_value;
	uint32_t b_value;
	uint32_t r_delta;
	uint32_t g_delta;
	uint32_t b_delta;
};

struct dce110_opp_regamma_params {
	struct {
		uint8_t num_segments[GAMMA_SEGMENTS_NUM];
		uint16_t offsets[GAMMA_SEGMENTS_NUM];
		struct end_point first;
		struct end_point last;
	} region_config;

	struct {
		struct pwl_segment *segments;
		int num_pwl_segments;
	} pwl_config;
};

#endif /* DRIVERS_GPU_DRM_AMD_DAL_DEV_DC_DCE110_DCE110_TYPES_H_ */
