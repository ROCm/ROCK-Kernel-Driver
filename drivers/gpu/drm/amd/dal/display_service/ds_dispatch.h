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

#ifndef __DAL_DS_DISPATCH_H__
#define __DAL_DS_DISPATCH_H__

#include "include/adjustment_types.h"

#include "path_mode_set_with_data.h"

#define MAX_NUM_SUPPORT_SIGNAL (DS_SIGNAL_TYPE_END - DS_SIGNAL_TYPE_BEGIN + 1)
#define REGAMMA_COEFF_A0	31308
#define REGAMMA_COEFF_A1	12920
#define REGAMMA_COEFF_A2	55
#define REGAMMA_COEFF_A3	55
#define REGAMMA_COEFF_GAMMA	2400

/* TODO: remove this once defined */
struct hw_get_viewport_x_adjustments;
struct topology_mgr;
struct hw_sequencer;
struct adapter_service;
struct hw_path_mode;

enum ds_signal_type {
	DS_SIGNAL_TYPE_CRT = 0,  /* the first */
	DS_SIGNAL_TYPE_DISCRETEVGA,
	DS_SIGNAL_TYPE_DFP,
	DS_SIGNAL_TYPE_LVDS,
	DS_SIGNAL_TYPE_HDMI,
	DS_SIGNAL_TYPE_DP,
	DS_SIGNAL_TYPE_EDP,
	DS_SIGNAL_TYPE_CF,
	DS_SIGNAL_TYPE_WIRELESS, /* the last */
	DS_SIGNAL_TYPE_UNKNOWN,

	DS_SIGNAL_TYPE_BEGIN = DS_SIGNAL_TYPE_CRT,
	DS_SIGNAL_TYPE_END = DS_SIGNAL_TYPE_WIRELESS
};
/* Purpose to build the path set for */
enum build_path_set_reason {
	BUILD_PATH_SET_REASON_SET_MODE = 0,
	BUILD_PATH_SET_REASON_WATERMARKS,
	BUILD_PATH_SET_REASON_VALIDATE,
	BUILD_PATH_SET_REASON_SET_ADJUSTMENT,
	BUILD_PATH_SET_REASON_GET_ACTIVE_PATHS,
	BUILD_PATH_SET_REASON_FALLBACK_UNDERSCAN
};

struct adj_global_info {
	enum adjustment_id adj_id;
	enum adjustment_data_type adj_data_type;
	union adjustment_property adj_prop;
	bool display_is_supported[MAX_NUM_SUPPORT_SIGNAL];
};

/* Display service dispatch init data */
struct ds_dispatch_init_data {
	struct dal_context *dal_context;
	struct hw_sequencer *hwss;
	struct topology_mgr *tm;
	struct adapter_service *as;
	struct timing_service *ts;
};

/* Display service dispatch */
struct ds_dispatch {
	struct dal_context *dal_context;
	struct path_mode_set_with_data *set;
	struct topology_mgr *tm;
	struct hw_sequencer *hwss;
	struct adapter_service *as;
	struct timing_service *ts;
	struct adj_container **applicable_adj;
	struct adjustment_parent_api *default_adjustments;

	/* Temporary storage for Path Mode Set validation. */
	struct path_mode path_modes[MAX_COFUNC_PATH];
	uint32_t disp_path_num;
	struct backlight_adj_group *backlight_adj;
	struct single_adj_group *single_adj;
	struct grph_colors_group *grph_colors_adj;
	struct grph_gamma_lut_group *grph_gamma_adj;
};


/*
 * DS dispatch functions
 */

/* Create DS dispatch */
struct ds_dispatch *dal_ds_dispatch_create(
		const struct ds_dispatch_init_data *data);

/* Destroy DS dispatch */
void dal_ds_dispatch_destroy(struct ds_dispatch **ds_dispatch);

/* Set up info frames */
void dal_ds_dispatch_setup_info_frame(
		struct ds_dispatch *ds_dispatch,
		const struct path_mode *mode,
		struct hw_path_mode *hw_mode);

/* Check if gamut needs reprogramming */
bool dal_ds_dispatch_is_gamut_change_required(
		struct ds_dispatch *ds_dispatch,
		enum pixel_encoding pixel_encoding,
		enum pixel_format pixel_format,
		uint32_t disp_index);

/* Return active path modes */
struct path_mode_set_with_data *dal_ds_dispatch_get_active_pms_with_data(
	struct ds_dispatch *ds_dispatch);

#endif /* __DAL_DS_DISPATCH_H__ */
