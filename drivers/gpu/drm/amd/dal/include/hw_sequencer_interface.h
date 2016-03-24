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

#ifndef __DAL_HW_SEQUENCER_INTERFACE_H__
#define __DAL_HW_SEQUENCER_INTERFACE_H__

#include "hw_sequencer_types.h"
#include "hw_adjustment_types.h"
#include "include/display_clock_interface.h"
#include "include/scaler_types.h"
#include "include/grph_csc_types.h"
#include "plane_types.h"

#include "adapter_service_interface.h"

enum hwss_result {
	HWSS_RESULT_OK,
	HWSS_RESULT_ERROR,
	HWSS_RESULT_NO_BANDWIDTH,
	HWSS_RESULT_OUT_OF_RANGE,
	HWSS_RESULT_NOT_SUPPORTED,
	HWSS_RESULT_UNKNOWN
};

struct hws_init_data {
	struct adapter_service *as;
	struct dal_context *dal_context;
};

/* TODO: below is three almost equal structures.
 * We should decide what to do with them */

struct enable_link_param {
	struct display_path *display_path;
	uint32_t link_idx;
	struct hw_crtc_timing timing;
	struct link_settings link_settings;

	bool optimized_programming;
	const struct hw_path_mode *path_mode;
};

struct validate_link_param {
	const struct display_path *display_path;
	uint32_t link_idx;
	struct link_settings link_settings;
};

struct set_dp_phy_pattern_param {
	struct display_path *display_path;
	uint32_t link_idx;
	enum dp_test_pattern test_pattern;
	const uint8_t *custom_pattern;
	uint32_t cust_pattern_size;
};

struct hw_global_objects;
struct hw_sequencer;
struct hw_adjustment;
struct hw_path_mode_set;
struct hw_path_mode;
struct hwss_build_params;
struct controller;


enum hwss_result dal_hw_sequencer_reset_audio_device(
	struct hw_sequencer *hws,
	struct display_path *display_path);

enum hwss_result dal_hw_sequencer_validate_link(
	struct hw_sequencer *hws,
	const struct validate_link_param *param);

enum hwss_result dal_hw_sequencer_set_dp_phy_pattern(
	struct hw_sequencer *hws,
	const struct set_dp_phy_pattern_param *param);

enum hwss_result dal_hw_sequencer_set_lane_settings(
	struct hw_sequencer *hws,
	struct display_path *display_path,
	const struct link_training_settings *link_settings);

/* Static validation for a SINGLE path mode.
 * Already "active" paths (if any) are NOT taken into account. */
enum hwss_result dal_hw_sequencer_validate_display_path_mode(
	struct hw_sequencer *hws,
	const struct hw_path_mode *path_mode);

/* Validation for a SET of path modes, including Video Memory Bandwidth
 * validation. */
enum hwss_result dal_hw_sequencer_validate_display_hwpms(
	struct hw_sequencer *hws,
	struct hw_path_mode_set *path_set);

struct hw_adjustment_gamma_ramp;

enum hwss_result dal_hw_sequencer_set_gamma_ramp_adjustment(
	struct hw_sequencer *hws,
	const struct display_path *display_path,
	struct hw_adjustment_gamma_ramp *adjusment);

enum hwss_result dal_hw_sequencer_set_color_control_adjustment(
	struct hw_sequencer *hws,
	struct controller *crtc,
	struct hw_adjustment_color_control *adjustment);

enum hwss_result dal_hw_sequencer_set_vertical_sync_adjustment(
	struct hw_sequencer *hws,
	struct display_path *display_path,
	struct hw_adjustment_value *adjustment);

enum hwss_result dal_hw_sequencer_set_horizontal_sync_adjustment(
	struct hw_sequencer *hws,
	struct display_path *display_path,
	struct hw_adjustment_value *adjustment);

enum hwss_result dal_hw_sequencer_set_composite_sync_adjustment(
	struct hw_sequencer *hws,
	struct display_path *display_path,
	struct hw_adjustment_value *adjustment);

enum hwss_result dal_hw_sequencer_enable_sync_output(
	struct hw_sequencer *hws,
	struct display_path *display_path);

enum hwss_result dal_hw_sequencer_disable_sync_output(
	struct hw_sequencer *hws,
	struct display_path *display_path);

enum hwss_result dal_hw_sequencer_set_backlight_adjustment(
	struct hw_sequencer *hws,
	struct display_path *display_path,
	struct hw_adjustment_value *adjustment);

enum hwss_result dal_hw_sequencer_enable_link(
	struct hw_sequencer *hws,
	const struct enable_link_param *in);

enum hwss_result dal_hw_sequencer_set_clocks_and_clock_state(
		struct hw_sequencer *hws,
		struct hw_global_objects *g_obj,
		const struct minimum_clocks_calculation_result *min_clk_in,
		enum clocks_state required_clocks_state);

enum hwss_result dal_hw_sequencer_set_mode(
	struct hw_sequencer *hws,
	struct hw_path_mode_set *path_set);

enum signal_type dal_hw_sequencer_detect_sink(
	struct hw_sequencer *hws,
	struct display_path *display_path);

enum signal_type dal_hw_sequencer_detect_load(
	struct hw_sequencer *hws,
	struct display_path *display_path);

enum hwss_result dal_hw_sequencer_set_safe_displaymark(
		struct hw_sequencer *hws,
		struct hw_path_mode_set *path_set);

enum hwss_result dal_hw_sequencer_set_displaymark(
		struct hw_sequencer *hws,
		struct hw_path_mode_set *path_set);

struct hw_sequencer *dal_hw_sequencer_create(
		struct hws_init_data *hws_init_data);

enum hwss_result dal_hw_sequencer_set_overscan_adj(
	struct hw_sequencer *hws,
	struct hw_path_mode_set *set,
	struct hw_underscan_adjustment_data *hw_underscan);

enum hwss_result dal_hw_sequencer_set_bit_depth_reduction_adj(
	struct hw_sequencer *hws,
	struct display_path *disp_path,
	union hw_adjustment_bit_depth_reduction *bit_depth);

enum hwss_result dal_hw_sequencer_get_hw_color_adj_range(
	struct hw_sequencer *hws,
	struct display_path *disp_path,
	struct hw_color_control_range *hw_color_range);

enum hwss_result dal_hw_sequencer_build_csc_adjust(
	struct hw_sequencer *hws,
	struct hw_adjustment_color_control *color_control,
	struct grph_csc_adjustment *adjust);

/* Cursor interface */
enum hwss_result dal_hw_sequencer_set_cursor_position(
		struct hw_sequencer *hws,
		struct display_path *dp,
		const struct dc_cursor_position *position);

enum hwss_result dal_hw_sequencer_set_cursor_attributes(
		struct hw_sequencer *hws,
		struct display_path *dp,
		const struct dc_cursor_attributes *attributes);

/* Underlay/MPO interface */
enum hwss_result dal_hw_sequencer_set_plane_config(
	struct hw_sequencer *hws,
	struct hw_path_mode_set *path_set,
	uint32_t display_index);

#endif /* __DAL_HW_SEQUENCER_INTERFACE_H__ */
