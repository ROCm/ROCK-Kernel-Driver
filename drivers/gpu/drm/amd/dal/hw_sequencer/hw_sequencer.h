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

#ifndef __DAL_HW_SEQUENCER_H__
#define __DAL_HW_SEQUENCER_H__

#include "include/hw_sequencer_interface.h"
#include "include/controller_interface.h"
#include "include/clock_source_interface.h"
#include "include/display_path_interface.h"
#include "include/hw_path_mode_set_interface.h"
#include "include/audio_interface.h"

#include "hw_sequencer_parameters.h"
#include "hw_sync_control.h"

struct hw_global_objects {
	struct bandwidth_manager *bm;
	struct dc_clock_generator *dccg;
	struct display_clock *dc;
};

struct hw_vce_adjust_timing_params {
	struct hw_crtc_timing *hw_crtc_timing;
	struct overscan_info *hw_overscan;
	uint32_t refresh_rate;
	bool extend_vblank;
	bool full_timing_adjustment;
	bool vce_multi_instance;
};

struct display_path_objects {
	struct encoder *upstream_encoder;
	struct encoder *downstream_encoder;
	struct connector *connector;
	struct audio *audio;
	enum engine_id engine;
};

struct hw_sequencer;
struct hw_path_mode;

struct hw_sequencer_funcs {
	void (*set_displaymark)(
		struct hw_sequencer *hws,
		struct hw_path_mode_set *set,
		struct watermark_input_params *wm_params,
		uint32_t param_number);
	enum clocks_state (*get_required_clocks_state)(
		struct hw_sequencer *hws,
		struct display_clock *display_clock,
		struct hw_path_mode_set *set,
		const struct minimum_clocks_calculation_result *min_clk_result);
	void (*set_safe_displaymark)(
		struct hw_sequencer *hws,
		struct hw_path_mode_set *set,
		struct watermark_input_params *wm_params,
		uint32_t params_number);
	void (*set_display_clock)(
		struct hw_sequencer *hws,
		struct hw_path_mode_set *path_set,
		const struct minimum_clocks_calculation_result *min_clk_result);
	void (*setup_audio_wall_dto)(
		struct hw_sequencer *hws,
		const struct hw_path_mode_set *path_set,
		const struct hwss_build_params *build_params);
	void (*setup_timing_and_blender)(
		struct hw_sequencer *hws,
		struct controller *current_controller,
		const struct hw_path_mode *path_mode,
		struct hw_crtc_timing *crtc_timing);
	bool (*setup_line_buffer_pixel_depth)(
		struct hw_sequencer *hws,
		struct controller *controller,
		enum lb_pixel_depth depth,
		bool blank);
	void (*apply_vce_timing_adjustment)(
		struct hw_sequencer *hws,
		struct hw_vce_adjust_timing_params *vce_adj_timing_params);
	uint32_t (*get_dp_dto_source_clock)(
		struct hw_sequencer *hws,
		struct display_path *display_path);
	void (*start_gtc_counter)(
		struct hw_sequencer *hws,
		const struct hw_path_mode_set *set);
	void (*destroy)(struct hw_sequencer **hws);
	enum hwss_result (*hwss_enable_link)(
		struct hw_sequencer *hws,
		const struct enable_link_param *in);
};

struct hw_sequencer {
	struct dal_context *dal_context;
	const struct hw_sequencer_funcs *funcs;
	struct adapter_service *as;
	struct hw_sync_control *sync_control;
	bool use_pp_lib;
};

bool dal_hw_sequencer_construct_base(
	struct hw_sequencer *hws,
	struct hws_init_data *init_data);

bool dal_hw_sequencer_get_global_objects(
	const struct hw_path_mode_set *path_set,
	struct hw_global_objects *obj);

enum signal_type dal_hw_sequencer_get_timing_adjusted_signal(
	const struct hw_path_mode *path_mode,
	enum signal_type signal);

enum signal_type dal_hw_sequencer_get_asic_signal(
	const struct hw_path_mode *path_mode);

void dal_hw_sequencer_get_objects(
	struct display_path *dp,
	struct display_path_objects *objs);

enum engine_id dal_hw_sequencer_get_engine_id(
	struct display_path *dp);

void dal_hw_sequencer_build_audio_output(
	struct hw_sequencer *hws,
	const struct hw_path_mode *path_mode,
	enum engine_id engine_id,
	const struct pll_settings *pll_settings,
	struct audio_output *audio_output);

void dal_hw_sequencer_extend_vblank(
	struct hw_sequencer *hws,
	struct hw_vce_adjust_timing_params *params);

void dal_hw_sequencer_extend_hblank(
	struct hw_sequencer *hws,
	struct hw_vce_adjust_timing_params *params);

void dal_hw_sequencer_wireless_full_timing_adjustment(
	struct hw_sequencer *hws,
	struct hw_vce_adjust_timing_params *params);

void dal_hw_sequencer_get_pixel_clock_parameters(
	const struct hw_path_mode *path_mode,
	struct pixel_clk_params *pixel_clk_params);

uint32_t dal_hw_sequencer_translate_to_graphics_bpp(
	enum pixel_format pixel_format);

uint32_t dal_hw_sequencer_translate_to_backend_bpp(
	enum hw_overlay_backend_bpp backend_bpp);

enum dc_deep_color_depth dal_hw_sequencer_translate_to_dec_deep_color_depth(
	enum hw_color_depth hw_color_depth);


enum hwss_result dal_hw_sequencer_enable_link_base(
	struct hw_sequencer *hws,
	const struct enable_link_param *in);

uint32_t dal_hw_sequencer_translate_to_lb_color_depth(
	enum lb_pixel_depth lb_color_depth);

enum csc_color_depth dal_hw_sequencer_translate_to_csc_color_depth(
	enum hw_color_depth color_depth);

/*enum pixel_format dal_hw_sequencer_translate_to_pixel_format(
	enum hw_pixel_format pixel_format);*/

#endif /* __DAL_HW_SEQUENCER_H__ */
