/*
 * Copyright 2014 Advanced Micro Devices, Inc.
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

#ifndef __DAL_GAMMA_LUT_H__
#define __DAL_GAMMA_LUT_H__

#include "adjustment_types_internal.h"
#include "include/display_service_types.h"

struct ds_dispatch;
struct grph_colors_group;
struct crtc_timing;
struct display_path;
struct adj_container;

struct grph_gamma_lut_group {
	struct ds_dispatch *ds;
	struct hw_sequencer *hws;
	struct dal_context *dal_context;
	struct ds_adjustment_status status_gamma_ramp;
	struct ds_adjustment_status status_original_ramp;
	struct raw_gamma_ramp gamma_ramp;
	struct raw_gamma_ramp oroginal_ramp;
};

struct grph_gamma_lut_group_init_data {
	struct ds_dispatch *ds;
	struct hw_sequencer *hws;
	struct dal_context *dal_context;
};

struct grph_gamma_lut_group *dal_gamma_adj_group_create(
	struct grph_gamma_lut_group_init_data *init_data);

void dal_grph_gamma_adj_group_destroy(
	struct grph_gamma_lut_group **grph_gamma_adj);


enum ds_return dal_grph_gamma_lut_set_adjustment(
		struct ds_dispatch *ds,
		const struct display_path *disp_path,
		const struct path_mode *disp_path_mode,
		enum adjustment_id adj_id,
		const struct raw_gamma_ramp *gamma,
		const struct ds_regamma_lut *regumma_lut);

bool dal_gamma_lut_validate(
		enum adjustment_id adj_id,
		const struct raw_gamma_ramp *gamma,
		bool validate_all);

bool dal_gamma_lut_translate_to_hw(
		struct ds_dispatch *ds,
		const struct path_mode *disp_path_mode,
		const struct display_path *disp_path,
		const struct raw_gamma_ramp *gamma_in,
		struct hw_adjustment_gamma_ramp *gamma_out);

const struct raw_gamma_ramp *dal_gamma_lut_get_current_gamma(
		struct ds_dispatch *ds,
		enum adjustment_id adj_id);

bool dal_gamma_lut_set_current_gamma(struct ds_dispatch *ds,
		enum adjustment_id adj_id,
		const struct raw_gamma_ramp *gamma);

#endif /* __DAL_GAMMA_LUT_H__ */
