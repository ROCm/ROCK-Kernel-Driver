/*
 * Copyright 2012-14 Advanced Micro Devices, Inc.
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

#ifndef __DAL_TIMING_GENERATOR_H__
#define __DAL_TIMING_GENERATOR_H__

#include "include/signal_types.h"
#include "include/grph_object_id.h"
#include "include/grph_object_defs.h"
#include "include/timing_generator_types.h"
#include "include/grph_csc_types.h"

#define LEFT_EYE_3D_PRIMARY_SURFACE 1
#define RIGHT_EYE_3D_PRIMARY_SURFACE 0

enum test_pattern_dyn_range {
	TEST_PATTERN_DYN_RANGE_VESA = 0,
	TEST_PATTERN_DYN_RANGE_CEA
};

enum test_pattern_mode {
	TEST_PATTERN_MODE_COLORSQUARES_RGB = 0,
	TEST_PATTERN_MODE_COLORSQUARES_YCBCR601,
	TEST_PATTERN_MODE_COLORSQUARES_YCBCR709,
	TEST_PATTERN_MODE_VERTICALBARS,
	TEST_PATTERN_MODE_HORIZONTALBARS,
	TEST_PATTERN_MODE_SINGLERAMP_RGB,
	TEST_PATTERN_MODE_DUALRAMP_RGB
};

enum test_pattern_color_format {
	TEST_PATTERN_COLOR_FORMAT_BPC_6 = 0,
	TEST_PATTERN_COLOR_FORMAT_BPC_8,
	TEST_PATTERN_COLOR_FORMAT_BPC_10,
	TEST_PATTERN_COLOR_FORMAT_BPC_12
};

enum controller_dp_test_pattern {
	CONTROLLER_DP_TEST_PATTERN_D102 = 0,
	CONTROLLER_DP_TEST_PATTERN_SYMBOLERROR,
	CONTROLLER_DP_TEST_PATTERN_PRBS7,
	CONTROLLER_DP_TEST_PATTERN_COLORSQUARES,
	CONTROLLER_DP_TEST_PATTERN_VERTICALBARS,
	CONTROLLER_DP_TEST_PATTERN_HORIZONTALBARS,
	CONTROLLER_DP_TEST_PATTERN_COLORRAMP,
	CONTROLLER_DP_TEST_PATTERN_VIDEOMODE,
	CONTROLLER_DP_TEST_PATTERN_RESERVED_8,
	CONTROLLER_DP_TEST_PATTERN_RESERVED_9,
	CONTROLLER_DP_TEST_PATTERN_RESERVED_A,
	CONTROLLER_DP_TEST_PATTERN_COLORSQUARES_CEA
};

enum {
	STUTTER_MODE_NO_ADVANCED_REQUEST = 0x100
};

struct timing_generator;

struct timing_generator_funcs {
	bool (*validate_timing)(
		struct timing_generator *tg,
		const struct hw_crtc_timing *hw_crtc_timing,
		enum signal_type signal);
	void (*set_lock_timing_registers)(
		struct timing_generator *tg,
		bool lock);
	void (*set_lock_graph_surface_registers)(
		struct timing_generator *tg,
		bool lock);
	void (*unlock_graph_surface_registers)(struct timing_generator *tg);
	void (*set_lock_master)(struct timing_generator *tg, bool lock);
	bool (*enable_crtc)(struct timing_generator *tg);
	bool (*disable_crtc)(struct timing_generator *tg);
	bool (*program_timing_generator)(
		struct timing_generator *tg,
		struct hw_crtc_timing *hw_crtc_timing);
	void (*reprogram_timing)(
		struct timing_generator *tg,
		const struct hw_crtc_timing *ref_timing,
		const struct hw_crtc_timing *new_timing);
	void (*get_crtc_timing)(
		struct timing_generator *tg,
		struct hw_crtc_timing *hw_crtc_timing);
	bool (*blank_crtc)(
		struct timing_generator *tg,
		enum color_space color_space);
	bool (*unblank_crtc)(
		struct timing_generator *tg,
		enum color_space color_space);
	bool (*program_flow_control)(
		struct timing_generator *tg,
		enum sync_source source);
	void (*set_early_control)(
		struct timing_generator *tg,
		uint32_t early_cntl);
	void (*enable_stereo)(
		struct timing_generator *tg,
		const struct crtc_stereo_parameters *stereo_params);
	void (*disable_stereo)(struct timing_generator *tg);
	bool (*get_stereo_status)(
		struct timing_generator *tg,
		struct crtc_stereo_status *stereo_status);
	void (*force_stereo_next_eye)(
		struct timing_generator *tg,
		bool right_eye);
	void (*reset_stereo_3d_phase)(struct timing_generator *tg);
	void (*program_drr)(
		struct timing_generator *tg,
		const struct hw_ranged_timing *timing);
	void (*enable_advanced_request)(
		struct timing_generator *tg,
		bool enable,
		const struct hw_crtc_timing *timing);
	void (*set_vertical_sync_polarity)(
		struct timing_generator *tg,
		uint32_t positive_polarity);
	void (*set_horizontal_sync_polarity)(
		struct timing_generator *tg,
		uint32_t positive_polarity);
	void (*set_horizontal_sync_composite)(
		struct timing_generator *tg,
		uint32_t composite);
	bool (*enable_reset_trigger)(
		struct timing_generator *tg,
		const struct trigger_params *trigger_params);
	void (*disable_reset_trigger)(struct timing_generator *tg);
	bool (*did_triggered_reset_occur)(struct timing_generator *tg);
	void (*set_test_pattern)(
		struct timing_generator *tg,
		enum controller_dp_test_pattern test_pattern,
		enum crtc_color_depth color_depth);
	bool (*is_test_pattern_enabled)(struct timing_generator *tg);
	bool (*is_in_vertical_blank)(struct timing_generator *tg);
	bool (*is_counter_moving)(struct timing_generator *tg);
	void (*get_crtc_position)(
		struct timing_generator *tg,
		struct crtc_position *crtc_position);
	uint32_t (*get_current_frame_number)(struct timing_generator *tg);
	void (*setup_global_swap_lock)(
		struct timing_generator *tg,
		const struct dcp_gsl_params *gsl_params);
	void (*get_global_swap_lock_setup)(
		struct timing_generator *tg,
		struct dcp_gsl_params *gsl_params);
	bool (*get_io_sequence)(
		struct timing_generator *tg,
		enum io_register_sequence sequence,
		struct io_reg_sequence *io_reg_sequence);
	void (*program_vbi_end_signal)(
		struct timing_generator *tg,
		const struct vbi_end_signal_setup *setup);
	void (*program_blanking)(
		struct timing_generator *tg,
		const struct hw_crtc_timing *timing);
	void (*destroy)(struct timing_generator **tg);
	bool (*force_triggered_reset_now)(
		struct timing_generator *tg,
		const struct trigger_params *trigger_params);
	void (*wait_for_vactive)(struct timing_generator *tg);
	void (*wait_for_vblank)(struct timing_generator *tg);
	uint32_t (*get_crtc_scanoutpos)(
		struct timing_generator *tg,
		int32_t *vpos,
		int32_t *hpos);
	uint32_t (*get_vblank_counter)(struct timing_generator *tg);
};

struct timing_generator {
	const struct timing_generator_funcs *funcs;
	uint32_t *regs;
	struct bios_parser *bp;
	enum controller_id controller_id;
	struct dal_context *ctx;
	uint32_t max_h_total;
	uint32_t max_v_total;

	uint32_t min_h_blank;
	uint32_t min_h_front_porch;
	uint32_t min_h_back_porch;
};

bool dal_timing_generator_force_triggered_reset_now(
		struct timing_generator *tg,
		const struct trigger_params *trigger_params);
void dal_timing_generator_wait_for_vactive(struct timing_generator *tg);
void dal_timing_generator_wait_for_vblank(struct timing_generator *tg);
void dal_timing_generator_color_space_to_black_color(
		enum color_space colorspace,
		struct crtc_black_color *black_color);
void dal_timing_generator_apply_front_porch_workaround(
	struct timing_generator *tg,
	struct hw_crtc_timing *timing);
int32_t dal_timing_generator_get_vsynch_and_front_porch_size(
	const struct hw_crtc_timing *timing);

bool dal_timing_generator_validate_timing(
	struct timing_generator *tg,
	const struct hw_crtc_timing *hw_crtc_timing,
	enum signal_type signal);

bool dal_timing_generator_construct(
	struct timing_generator *tg,
	enum controller_id id);

#endif
