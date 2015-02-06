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

#ifndef __DAL_CONTROLLER_INTERFACE_H__
#define __DAL_CONTROLLER_INTERFACE_H__

#include "controller_types.h"
#include "signal_types.h"
#include "hw_sequencer_types.h"
#include "timing_generator_types.h"
#include "formatter_types.h"
#include "scaler_types.h"
#include "grph_csc_types.h"
#include "video_csc_types.h"
#include "plane_types.h"
#include "isr_config_types.h"

/* forward declaration */
struct controller;
struct line_buffer;
struct display_clock;
struct bandwidth_manager;
struct dc_clock_generator;
enum color_space;
struct vbi_end_signal_setup;
struct dcp_gsl_params;
struct scaling_tap_info;
struct scaler_validation_params;
struct scaler_data;
struct gamma_parameters;
struct gamma_ramp;
enum pixel_format;
struct dev_c_lut;
struct ovl_signal;
struct bit_depth_reduction_params;
struct clamping_and_pixel_encoding_params;
enum color_depth;
struct surface;
struct scaler_filter;
struct plane_surface_config;
struct plane_internal;

/* Required information for creation and initialization of a controller */
struct controller_init_data {
	struct dal_context *dal_context;
	struct adapter_service *as;
	enum controller_id controller;
	enum controller_id paired_controller;
};

/*
 * **************************************************************************
 * ************************* Basic functionality on Controllers ***************
 * **************************************************************************
 */

struct controller *dal_controller_create(
	struct controller_init_data *init_data);
void dal_controller_destroy(struct controller **crtc);

struct scaler_filter *dal_scaler_filter_create(void);
void dal_scaler_filter_destroy(struct scaler_filter **ptr);

const struct graphics_object_id dal_controller_get_graphics_object_id(
	const struct controller *crtc);

void dal_controller_power_up(struct controller *crtc);
void dal_controller_power_down(struct controller *crtc);
bool dal_controller_power_gating_enable(
	struct controller *crtc,
	enum pipe_gating_control cntl);

enum controller_id dal_controller_get_id(struct controller *crtc);
enum controller_id dal_controller_get_paired_controller_id(
	struct controller *crtc);
enum sync_source dal_controller_get_sync_source(struct controller *crtc);

/*
 * **************************************************************************
 * ******************************* Shared Objects Pointers ******************
 * **************************************************************************
 */

/* Get */
struct line_buffer *dal_controller_get_line_buffer(struct controller *crtc);
struct display_clock *dal_controller_get_display_clock(
	struct controller *crtc);
struct bandwidth_manager *dal_controller_get_bandwidth_manager(
	struct controller *crtc);
struct dc_clock_generator *dal_controller_get_dc_clock_generator(
	struct controller *crtc);

/* Set */
void dal_controller_set_display_clock(
	struct controller *crtc,
	struct display_clock *display_clk);
void dal_controller_set_bandwidth_manager(
	struct controller *crtc,
	struct bandwidth_manager *bandwidth_mgr);
void dal_controller_set_dc_clock_generator(
	struct controller *crtc,
	struct dc_clock_generator *clk_generator);

void dal_controller_set_scaler_filter(
	struct controller *crtc,
	struct scaler_filter *filter);

/*
 * **************************************************************************
 * ********************* Timing Generator Interface *************************
 * **************************************************************************
 */

/* Get */
void dal_controller_get_crtc_timing(
	struct controller *crtc,
	struct hw_crtc_timing *hw_crtc_timing);
bool dal_controller_is_counter_moving(struct controller *crtc);
void dal_controller_get_crtc_position(
	struct controller *crtc,
	struct crtc_position *crtc_position);
void dal_controller_wait_for_vblank(struct controller *crtc);
void dal_controller_wait_for_vactive(struct controller *crtc);
void dal_controller_wait_frames(
	struct controller *crtc,
	uint32_t num_of_frames);

bool dal_controller_validate_timing(
	struct controller *crtc,
	const struct hw_crtc_timing *timing,
	enum signal_type signal);
bool dal_controller_get_active_pll_id(
	struct controller *crtc,
	enum signal_type signal,
	bool *dto_mode,
	enum clock_source_id *clk_src_id);

uint32_t dal_controller_get_crtc_scanoutpos(
		struct controller *crtc,
		int32_t *vpos,
		int32_t *hpos);

/* Get counter of vblanks */
uint32_t dal_controller_get_vblank_counter(struct controller *crtc);

/* Set */
bool dal_controller_enable_timing_generator(struct controller *crtc);
bool dal_controller_disable_timing_generator(struct controller *crtc);
bool dal_controller_program_timing_generator(
	struct controller *crtc,
	struct hw_crtc_timing *timing);
bool dal_controller_blank_crtc(struct controller *crtc, enum color_space cs);
bool dal_controller_unblank_crtc(struct controller *crtc, enum color_space cs);
void dal_controller_reprogram_timing(
	struct controller *crtc,
	const struct hw_crtc_timing *ref_timing,
	const struct hw_crtc_timing *new_timing);
void dal_controller_program_vbi_end_signal(
	struct controller *crtc,
	const struct vbi_end_signal_setup *pSetup);
void dal_controller_program_blanking(
	struct controller *crtc,
	const struct hw_crtc_timing *timing);
void dal_controller_program_drr(
	struct controller *crtc,
	const struct hw_ranged_timing *timing);

/*
 * **************************************************************************
 * ********************* Generic Control Interface  ***************************
 * **************************************************************************
 */

void dal_controller_set_blender_mode(
	struct controller *crtc,
	enum blender_mode mode);

bool dal_controller_program_alpha_blending(
	struct controller *crtc,
	const struct alpha_mode_cfg *cfg);

/* (Get) Controller IO sequence */
bool dal_controller_get_io_sequence(
	struct controller *crtc,
	enum io_register_sequence sequence,
	struct io_reg_sequence *reg_sequence);

/* (Set) Pipe control */
void dal_controller_set_fe_clock(struct controller *crtc, bool enable);
void dal_controller_enable_display_pipe_clock_gating(
	struct controller *crtc,
	bool enable);
bool dal_controller_pipe_control_lock(
	struct controller *crtc,
	uint32_t control_mask,
	bool lock);

/* (Set) Enable/disable triggered CRTC reset */
bool dal_controller_enable_reset_trigger(
	struct controller *crtc,
	const struct trigger_params *trig_params);
void dal_controller_disable_reset_trigger(struct controller *crtc);
bool dal_controller_force_triggered_reset_now(
	struct controller *crtc,
	const struct trigger_params *trig_params);

bool dal_controller_program_flow_control(
	struct controller *crtc,
	enum sync_source source);
void dal_controller_set_early_control(
	struct controller *crtc,
	uint32_t early_control);
void dal_controller_set_advanced_request(
	struct controller *crtc,
	bool enable,
	const struct hw_crtc_timing *timing);

/* (Set) Double buffering */
void dal_controller_set_lock_timing_registers(
	struct controller *crtc,
	bool lock);
void dal_controller_set_lock_graph_surface_registers(
	struct controller *crtc,
	bool lock);
void dal_controller_set_lock_master(struct controller *crtc, bool lock);

/* (Set/Get) Global Swap Lock */
void dal_controller_setup_global_swaplock(
	struct controller *crtc,
	const struct dcp_gsl_params *gsl_params);
void dal_controller_get_global_swaplock_setup(
	struct controller *crtc,
	struct dcp_gsl_params *gsl_params);

void dal_controller_set_test_pattern(
	struct controller *crtc,
	enum dp_test_pattern test_pattern,
	enum hw_color_depth color_depth);

/*
 * **************************************************************************
 * ***************************** VGA  ***************************************
 * **************************************************************************
 */

bool dal_controller_enable_vga_render(struct controller *crtc);
void dal_controller_disable_vga(struct controller *crtc);

/*
 * **************************************************************************
 * *************************** Display Scaler  *******************************
 * **************************************************************************
 */

/* Get */
enum scaler_validation_code dal_controller_get_optimal_taps_number(
	struct controller *crtc,
	struct scaler_validation_params *scaler_params,
	struct scaling_tap_info *taps);
enum scaler_validation_code dal_controller_get_next_lower_taps_number(
	struct controller *crtc,
	struct scaler_validation_params *scaler_params,
	struct scaling_tap_info *taps);

/* Set */
/* General purpose scaler programming interface */
bool dal_controller_set_scaler_wrapper(
	struct controller *crtc,
	const struct scaler_data *scaler_data);
/* not use scaling */
void dal_controller_set_scaler_bypass(struct controller *crtc);

bool dal_controller_is_scaling_enabled(struct controller *crtc);

bool dal_controller_update_viewport(
		struct controller *crtc,
		const struct rect *view_port,
		bool is_fbc_attached);
/*
 * **************************************************************************
 * *************************** LUT and Gamma ******************************
 * **************************************************************************
 */

/* Set */
bool dal_controller_set_gamma_ramp(
	struct controller *crtc,
	const struct gamma_ramp *ramp,
	const struct gamma_parameters *params);
bool dal_controller_set_palette(
	struct controller *crtc,
	const struct dev_c_lut *palette,
	uint32_t start,
	uint32_t length,
	enum pixel_format surface_format);
/* set driver default gamma without any adjustment */
bool dal_controller_set_default_gamma(
	struct controller *crtc,
	enum pixel_format surface_format);

/*
 *******************************************************************************
 ************************* Display color space and color adjustment  ***********
 *******************************************************************************
 */

void dal_controller_get_grph_adjustment_range(
	struct controller *crtc,
	enum grph_csc_adjust_item adjust_item,
	struct hw_adjustment_range *adjust_range);

bool dal_controller_is_supported_custom_gamut_adjustment(
	struct controller *crtc,
	enum surface_type surface_type);

bool dal_controller_is_supported_custom_gamma_coefficients(
	struct controller *crtc,
	enum surface_type surface_type);

bool dal_controller_is_supported_overlay_alpha_adjustment(
	struct controller *crtc);

bool dal_controller_set_input_csc(
	struct controller *crtc,
	const enum color_space color_space);

void dal_controller_get_overlay_adjustment_range(
	struct controller *crtc,
	enum ovl_csc_adjust_item overlay_adjust_item,
	struct hw_adjustment_range *adjust_range);

void dal_controller_set_grph_csc_default(
	struct controller *crtc,
	const struct default_adjustment *default_adjust);

void dal_controller_set_grph_csc_adjustment(
	struct controller *crtc,
	const struct grph_csc_adjustment *adjust);

void dal_controller_set_overscan_color_black(
	struct controller *crtc,
	enum color_space color_space);

void dal_controller_set_ovl_csc_adjustment(
	struct controller *crtc,
	const struct ovl_csc_adjustment *adjust,
	enum color_space color_space);

void dal_controller_set_vertical_sync_adjustment(
	struct controller *crtc,
	uint32_t v_sync_polarity);

void dal_controller_set_horizontal_sync_adjustment(
	struct controller *crtc,
	uint32_t h_sync_polarity);

void dal_controller_set_horizontal_sync_composite(
	struct controller *crtc,
	uint32_t h_sync_composite);

/* ****************************************************************************
 * ****************** Display color space and color adjustment  ***************
 * ****************************************************************************
 */

struct hw_adjustment_range;
enum ovl_csc_adjust_item;
struct ovl_csc_adjustment;
enum ovl_alpha_blending_mode;
/* Get*/
void dal_controller_get_grph_adjustment_range(
	struct controller *crtc,
	enum grph_csc_adjust_item adjust,
	struct hw_adjustment_range *adjust_range);

bool dal_controller_is_custom_gamut_adjust_supported(
	struct controller *crtc,
	enum surface_type surface_type);

bool dal_controller_is_custom_gammma_coeff_supported(
	struct controller *crtc,
	enum surface_type surface_type);

bool dal_controller_is_ovl_alfa_adjust_supported(
	struct controller *crtc);

void dal_controller_get_ovl_adjustment_range(
	struct controller *crtc,
	enum ovl_csc_adjust_item adjust,
	struct hw_adjustment_range *adjust_range);

/* Set*/
void dal_controller_set_grph_csc_default(
	struct controller *crtc,
	const struct default_adjustment *adjust);


void dal_controller_set_grph_csc_adjustment(
	struct controller *crtc,
	const struct grph_csc_adjustment *adjust);

void dal_controller_set_overscan_color_black(
	struct controller *crtc,
	enum color_space color_space);

void dal_controller_set_ovl_csc_adjustment(
	struct controller *crtc,
	const struct ovl_csc_adjustment *adjust,
	enum color_space color_space);

void dal_controller_set_ovl_alpha_blending(
	struct controller *crtc,
	enum ovl_alpha_blending_mode alpha_type,
	uint32_t adjust);

void dal_controller_set_vsync_adjustment(
	struct controller *crtc,
	uint32_t vsync_polarity);

void dal_controller_set_hsync_adjustment(
	struct controller *crtc,
	uint32_t hsync_polarity);

void dal_controller_set_hsync_composite(
	struct controller *crtc,
	uint32_t hsync_composite);

/*
 * **************************************************************************
 * ************************** FMT (Dithering) ********************************
 * **************************************************************************
 */

/* Formatter Block */
void dal_controller_program_formatter_bit_depth_reduction(
	struct controller *crtc,
	const struct bit_depth_reduction_params *formatter_info);
void dal_controller_program_formatter_clamping_and_pixel_encoding(
	struct controller *crtc,
	const struct clamping_and_pixel_encoding_params *formatter);
/* Deep color in FMAT */
void dal_controller_formatter_set_dyn_expansion(
	struct controller *crtc,
	enum color_space color_space,
	enum color_depth color_depth,
	enum signal_type signal);

/*
 *******************************************************************************
 **************************** Stereo *******************************************
 *******************************************************************************
 */

bool dal_controller_get_stereo_status(
	struct controller *crtc,
	struct crtc_stereo_status *status);

void dal_controller_enable_stereo(
	struct controller *crtc,
	const struct crtc_stereo_parameters *params);

void dal_controller_disable_stereo(struct controller *crtc);

void dal_controller_force_stereo_next_eye(
	struct controller *crtc,
	bool right_eye);

void dal_controller_reset_stereo_3d_phase(struct controller *crtc);

void dal_controller_enable_stereo_mixer(
	struct controller *crtc,
	const struct crtc_mixer_params *params);

void dal_controller_disable_stereo_mixer(struct controller *crtc);

/*
 *******************************************************************************
 **************************** Surface ******************************************
 *******************************************************************************
 */
bool dal_controller_is_surface_supported(
	struct controller *crtc,
	const struct plane_config *pl_cfg);

bool dal_controller_get_possible_controllers_above(
	struct controller *crtc,
	const struct plane_surface_config *plscfg);

bool dal_controller_program_surface_config(
	struct controller *crtc,
	const struct plane_surface_config *configs);

bool dal_controller_program_surface_flip_and_addr(
	struct controller *crtc,
	const struct plane_addr_flip_info *flip_info);

/*
 *******************************************************************************
 **************************** Cursor *******************************************
 *******************************************************************************
 */

bool dal_controller_set_cursor_position(
		struct controller *crtc,
		const struct cursor_position *position);

bool dal_controller_set_cursor_attributes(
		struct controller *crtc,
		const struct cursor_attributes *attributes);


#endif /* __DAL_CONTROLLER_INTERFACE_H__ */
