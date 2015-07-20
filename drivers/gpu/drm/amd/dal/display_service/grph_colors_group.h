/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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

#ifndef __DAL_GRPH_COLORS_GROUP_H__
#define __DAL_GRPH_COLORS_GROUP_H__

#include "include/adjustment_types.h"

struct grph_colors_group;
struct crtc_timing;
struct display_path;
struct adj_container;
struct gamut_parameter;

#define PIXEL_CLOCK	27030

struct grph_colors_group {
	struct ds_dispatch *ds;
	struct hw_sequencer *hws;
	struct dal_context *dal_context;
	bool regamma_updated;
	bool gamut_dst_updated;
	bool gamut_src_updated;
};

struct grph_colors_group_init_data {
	struct ds_dispatch *ds;
	struct hw_sequencer *hws;
	struct dal_context *dal_context;
};

enum ds_color_space dal_grph_colors_group_get_color_space(
		struct grph_colors_group *grph_colors_adj,
		const struct crtc_timing *timing,
		const struct display_path *disp_path,
		struct adj_container *adj_container);

enum ds_return dal_grph_colors_group_set_adjustment(
		struct grph_colors_group *grph_colors_adj,
		struct display_path *disp_path,
		enum adjustment_id adj_id,
		uint32_t value);

bool dal_grph_colors_group_compute_hw_adj_color_control(
	struct grph_colors_group *grph_colors_adj,
	struct adj_container *adj_container,
	const struct crtc_timing *timing,
	struct display_path *disp_path,
	enum adjustment_id adj_id,
	struct gamut_parameter *gamut,
	struct ds_regamma_lut *regamma,
	struct hw_adjustment_color_control *color_control);

bool dal_grph_colors_group_build_color_control_adj(
	struct grph_colors_group *grph_colors_adj,
	const struct path_mode *mode,
	struct display_path *disp_path,
	struct hw_adjustment_set *set);

enum ds_color_space dal_grph_colors_group_build_default_color_space(
		struct grph_colors_group *grph_colors_adj,
		const struct crtc_timing *timing,
		const struct display_path *disp_path,
		enum ds_color_space hdmi_request_color_space);

enum ds_color_space dal_grph_colors_group_adjust_color_space(
		struct grph_colors_group *grph_colors_adj,
		enum ds_color_space color_space,
		bool rgb_limited);

bool dal_grph_colors_group_synch_color_temperature_with_gamut(
		struct grph_colors_group *grph_colors_adj,
		struct adj_container *adj_container);

bool dal_grph_colors_group_synch_gamut_with_color_temperature(
	struct grph_colors_group *grph_colors_adj,
	struct adj_container *adj_container);

bool dal_grph_colors_group_get_color_temperature(
	struct grph_colors_group *grph_colors_adj,
	struct adj_container *adj_container,
	int32_t *temp);

enum ds_return dal_grph_colors_group_set_color_graphics_gamut(
	struct grph_colors_group *grph_colors_adj,
	struct display_path *disp_path,
	struct gamut_data *gamut_data,
	enum adjustment_id adj_id,
	bool apply_to_hw);

enum ds_return dal_grph_colors_group_update_gamut(
	struct grph_colors_group *grph_colors_adj,
	struct display_path *disp_path,
	struct adj_container *adj_container);

struct grph_colors_group *dal_grph_colors_group_create(
	struct grph_colors_group_init_data *init_data);

void dal_grph_colors_adj_group_destroy(
	struct grph_colors_group **grph_colors_adj);

#endif /* __DAL_GRPH_COLORS_GROUP_H__ */
