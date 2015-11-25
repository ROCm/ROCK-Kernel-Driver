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

#ifndef __DAL_SET_MODE_PARAMS_INTERFACE_H__
#define __DAL_SET_MODE_PARAMS_INTERFACE_H__

struct set_mode_params;

struct set_mode_params_init_data {
	struct hw_sequencer *hws;
	struct dal_context *ctx;
	struct topology_mgr *tm;
};

struct view_stereo_3d_support dal_set_mode_params_get_stereo_3d_support(
	struct set_mode_params *smp,
	uint32_t display_index,
	enum dc_timing_3d_format);

bool dal_set_mode_params_update_view_on_path(
	struct set_mode_params *smp,
	uint32_t display_index,
	const struct view *vw);

bool dal_set_mode_params_update_mode_timing_on_path(
	struct set_mode_params *smp,
	uint32_t display_index,
	const struct dc_mode_timing *mode_timing,
	enum view_3d_format format);

bool dal_set_mode_params_update_scaling_on_path(
	struct set_mode_params *smp,
	uint32_t display_index,
	enum scaling_transformation st);

bool dal_set_mode_params_update_pixel_format_on_path(
	struct set_mode_params *smp,
	uint32_t display_index,
	enum pixel_format pf);

bool dal_set_mode_params_update_tiling_mode_on_path(
		struct set_mode_params *smp,
		uint32_t display_index,
		enum tiling_mode tm);

bool dal_set_mode_params_is_path_mode_set_supported(
	struct set_mode_params *smp);

bool dal_set_mode_params_is_path_mode_set_guaranteed(
	struct set_mode_params *smp);

bool dal_set_mode_params_report_single_selected_timing(
	struct set_mode_params *smp,
	uint32_t display_index);

bool dal_set_mode_params_report_ce_mode_only(
	struct set_mode_params *smp,
	uint32_t display_index);

struct set_mode_params *dal_set_mode_params_create(
		struct set_mode_params_init_data *init_data);

bool dal_set_mode_params_init_with_topology(
		struct set_mode_params *smp,
		const uint32_t display_indicies[],
		uint32_t idx_num);

bool dal_set_mode_params_is_multiple_pixel_encoding_supported(
	struct set_mode_params *smp,
	uint32_t display_index);

enum dc_pixel_encoding dal_set_mode_params_get_default_pixel_format_preference(
	struct set_mode_params *smp,
	unsigned int display_index);

void dal_set_mode_params_destroy(
		struct set_mode_params **set_mode_params);

#endif
