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

#include "dal_services.h"

#include "include/adapter_service_interface.h"
#include "include/signal_types.h"
#include "include/controller_interface.h"
#include "include/line_buffer_interface.h"
#include "include/display_clock_interface.h"
#include "include/dc_clock_generator_interface.h"
#include "include/fixed31_32.h"

#include "controller.h"
#include "timing_generator.h"
#include "csc.h"
#include "scaler.h"
#include "surface.h"
#include "formatter.h"
#include "pipe_control.h"
#include "vga.h"
#include "line_buffer.h"
#include "cursor.h"

#if defined(CONFIG_DRM_AMD_DAL_DCE11_0)
#include "dce110/controller_dce110.h"
#include "dce110/controller_v_dce110.h"
#endif

/*
 * **************************************************************************
 * ************************* Basic functionality on Controllers ***************
 * **************************************************************************
 */

bool dal_controller_base_construct(
		struct controller *crtc,
		struct controller_init_data *init_data)
{
	if (!init_data)
		return false;

	if (!init_data->as)
		return false;

	crtc->dal_context = init_data->dal_context;
	crtc->id = init_data->controller;
	crtc->paired_id = init_data->paired_controller;

	return true;
}

/* destruct actions common for ALL versions of DCE. */
void dal_controller_base_destruct(struct controller *crtc)
{
	if (crtc->cursor)
		crtc->cursor->funcs->destroy(&crtc->cursor);

	if (crtc->surface)
		crtc->surface->funcs->destroy(&crtc->surface);

	if (crtc->pc)
		crtc->pc->funcs->destroy(&crtc->pc);

	if (crtc->vga)
		crtc->vga->funcs->destroy(&crtc->vga);

	if (crtc->fmt)
		crtc->fmt->funcs->destroy(&crtc->fmt);

	if (crtc->csc)
		crtc->csc->funcs->destroy(&crtc->csc);

	if (crtc->video_gamma)
		crtc->video_gamma->funcs->destroy(&crtc->video_gamma);

	if (crtc->grph_gamma)
		crtc->grph_gamma->funcs->destroy(&crtc->grph_gamma);

	if (crtc->scl)
		crtc->scl->funcs->destroy(&crtc->scl);

	if (crtc->tg)
		crtc->tg->funcs->destroy(&crtc->tg);

	if (crtc->lb)
		dal_line_buffer_destroy(&crtc->lb);
}

struct controller *dal_controller_create(struct controller_init_data *init_data)
{
	struct controller *crtc = NULL;
	enum dce_version dce_version;

	if (!init_data)
		return NULL;
	if (!init_data->as)
		return NULL;

	dce_version = dal_adapter_service_get_dce_version(init_data->as);

	switch (dce_version) {
#if defined(CONFIG_DRM_AMD_DAL_DCE11_0)
	case DCE_VERSION_11_0:
		if (IS_UNDERLAY_CONTROLLER(init_data->controller))
			crtc = dal_controller_v_dce110_create(init_data);
		else
			crtc = dal_controller_dce110_create(init_data);
		break;
#endif
	default:
		BREAK_TO_DEBUGGER();
		break;
	}

	if (NULL == crtc)
		return NULL;

	crtc->go_id = dal_graphics_object_id_init(init_data->controller,
			ENUM_ID_1, OBJECT_TYPE_CONTROLLER);

	return crtc;
}

void dal_controller_destroy(struct controller **crtc)
{
	if (!crtc || !*crtc) {
		BREAK_TO_DEBUGGER();
		return;
	}

	(*crtc)->funcs.destroy(crtc);
	*crtc = NULL;
}

const struct graphics_object_id dal_controller_get_graphics_object_id(
	const struct controller *crtc)
{
	return crtc->go_id;
}

void dal_controller_power_up(struct controller *crtc)
{
	dal_line_buffer_power_up(crtc->lb);
}

void dal_controller_power_down(struct controller *crtc)
{
	crtc->tg->funcs->disable_crtc(crtc->tg);
}

bool dal_controller_power_gating_enable(
	struct controller *crtc,
	enum pipe_gating_control cntl)
{
	if (crtc->pc)
		return crtc->pc->funcs->
				enable_disp_power_gating(crtc->pc, cntl);
	else
		return PIPE_GATING_CONTROL_DISABLE == cntl;
}

enum controller_id dal_controller_get_id(struct controller *crtc)
{
	return crtc->id;
}

enum controller_id dal_controller_get_paired_controller_id(
	struct controller *crtc)
{
	return crtc->paired_id;
}
enum sync_source dal_controller_get_sync_source(struct controller *crtc)
{
	switch (crtc->id) {
	case CONTROLLER_ID_D0:
		return SYNC_SOURCE_CONTROLLER0;
	case CONTROLLER_ID_D1:
		return SYNC_SOURCE_CONTROLLER1;
	case CONTROLLER_ID_D2:
		return SYNC_SOURCE_CONTROLLER2;
	case CONTROLLER_ID_D3:
		return SYNC_SOURCE_CONTROLLER3;
	case CONTROLLER_ID_D4:
		return SYNC_SOURCE_CONTROLLER4;
	case CONTROLLER_ID_D5:
		return SYNC_SOURCE_CONTROLLER5;
	default:
		return SYNC_SOURCE_NONE;
	}
}

/*
 * **************************************************************************
 * ******************************* Shared Objects Pointers ******************
 * **************************************************************************
 */

/* Get */
struct line_buffer *dal_controller_get_line_buffer(struct controller *crtc)
{
	return crtc->lb;
}

struct display_clock *dal_controller_get_display_clock(
	struct controller *crtc)
{
	return crtc->dc;
}

struct bandwidth_manager *dal_controller_get_bandwidth_manager(
	struct controller *crtc)
{
	return crtc->bm;
}

struct dc_clock_generator *dal_controller_get_dc_clock_generator(
	struct controller *crtc)
{
	return crtc->dc_clk_gen;
}

/* Set */
void dal_controller_set_display_clock(
	struct controller *crtc,
	struct display_clock *dc)
{
	crtc->dc = dc;
}

void dal_controller_set_bandwidth_manager(
	struct controller *crtc,
	struct bandwidth_manager *bm)
{
	crtc->bm = bm;
}

void dal_controller_set_dc_clock_generator(
	struct controller *crtc,
	struct dc_clock_generator *dc_clk_gen)
{
	crtc->dc_clk_gen = dc_clk_gen;
}

/*
 * **************************************************************************
 * ********************* Timing Generator Interface *************************
 * **************************************************************************
 */

/* Get */
void dal_controller_get_crtc_timing(
	struct controller *crtc,
	struct hw_crtc_timing *hw_crtc_timing)
{
	crtc->tg->funcs->get_crtc_timing(crtc->tg, hw_crtc_timing);
}

bool dal_controller_is_counter_moving(struct controller *crtc)
{
	return crtc->tg->funcs->is_counter_moving(crtc->tg);
}

void dal_controller_get_crtc_position(
	struct controller *crtc,
	struct crtc_position *crtc_position)
{
	crtc->tg->funcs->get_crtc_position(crtc->tg, crtc_position);
}

void dal_controller_wait_for_vblank(struct controller *crtc)
{
	crtc->tg->funcs->wait_for_vblank(crtc->tg);
}

void dal_controller_wait_for_vactive(struct controller *crtc)
{
	crtc->tg->funcs->wait_for_vactive(crtc->tg);
}

void dal_controller_wait_frames(struct controller *crtc, uint32_t num_of_frames)
{
	uint32_t i;

	for (i = 0; i < num_of_frames; i++) {
		crtc->tg->funcs->wait_for_vactive(crtc->tg);
		crtc->tg->funcs->wait_for_vblank(crtc->tg);
	}
}

bool dal_controller_validate_timing(
	struct controller *crtc,
	const struct hw_crtc_timing *timing,
	enum signal_type signal)
{
	return crtc->tg->funcs->validate_timing(crtc->tg, timing, signal);
}

bool dal_controller_get_active_pll_id(
	struct controller *crtc,
	enum signal_type signal,
	bool *dto_mode,
	enum clock_source_id *clk_src_id)
{
	return crtc->funcs.get_active_pll_id(
		crtc, signal, dto_mode, clk_src_id);
}

uint32_t dal_controller_get_crtc_scanoutpos(
		struct controller *crtc,
		int32_t *vpos,
		int32_t *hpos)
{
	return crtc->tg->funcs->get_crtc_scanoutpos(
			crtc->tg,
			vpos,
			hpos);
}

/* Set */
bool dal_controller_enable_timing_generator(struct controller *crtc)
{
	return crtc->tg->funcs->enable_crtc(crtc->tg);
}

bool dal_controller_disable_timing_generator(struct controller *crtc)
{
	return crtc->tg->funcs->disable_crtc(crtc->tg);
}

bool dal_controller_program_timing_generator(
	struct controller *crtc,
	struct hw_crtc_timing *timing)
{
	return crtc->tg->funcs->program_timing_generator(crtc->tg, timing);
}

void dal_controller_program_drr(
	struct controller *crtc,
	const struct hw_ranged_timing *timing)
{
	crtc->tg->funcs->program_drr(crtc->tg, timing);
}

bool dal_controller_blank_crtc(struct controller *crtc, enum color_space cs)
{
	return crtc->tg->funcs->blank_crtc(crtc->tg, cs);
}

bool dal_controller_unblank_crtc(struct controller *crtc, enum color_space cs)
{
	return crtc->tg->funcs->unblank_crtc(crtc->tg, cs);
}

void dal_controller_reprogram_timing(
	struct controller *crtc,
	const struct hw_crtc_timing *ref_timing,
	const struct hw_crtc_timing *new_timing)
{
	crtc->tg->funcs->reprogram_timing(crtc->tg, ref_timing, new_timing);
}

void dal_controller_program_vbi_end_signal(
	struct controller *crtc,
	const struct vbi_end_signal_setup *setup)
{
	crtc->tg->funcs->program_vbi_end_signal(crtc->tg, setup);
}

void dal_controller_program_blanking(
	struct controller *crtc,
	const struct hw_crtc_timing *timing)
{
	crtc->tg->funcs->program_blanking(crtc->tg, timing);
}

/*
 * *****************************************************************************
 * ********************* Generic Control Interface  ****************************
 * *****************************************************************************
 */

/* (Get) Controller IO sequence */
bool dal_controller_get_io_sequence(
	struct controller *crtc,
	enum io_register_sequence sequence,
	struct io_reg_sequence *reg_sequence)
{
	return crtc->tg->funcs->
		get_io_sequence(crtc->tg, sequence, reg_sequence);
}

/* (Set) Pipe control */
void dal_controller_set_fe_clock(struct controller *crtc, bool enable)
{
	if (crtc->pc)
		crtc->pc->funcs->enable_fe_clock(crtc->pc, enable);
}

void dal_controller_enable_display_pipe_clock_gating(
	struct controller *crtc,
	bool enable)
{
	if (crtc->pc)
		crtc->pc->funcs->
		enable_display_pipe_clock_gating(crtc->pc, enable);
}

bool dal_controller_pipe_control_lock(
	struct controller *crtc,
	uint32_t control_mask,
	bool lock)
{
	if (crtc->pc)
		return crtc->pc->funcs->pipe_control_lock(
			crtc->pc,
			control_mask,
			lock);
	else
		return false;
}

/* (Set) Enable/disable triggered CRTC reset */
bool dal_controller_enable_reset_trigger(
	struct controller *crtc,
	const struct trigger_params *params)
{
	return crtc->tg->funcs->enable_reset_trigger(crtc->tg, params);
}

void dal_controller_disable_reset_trigger(struct controller *crtc)
{
	crtc->tg->funcs->disable_reset_trigger(crtc->tg);
}

bool dal_controller_force_triggered_reset_now(
	struct controller *crtc,
	const struct trigger_params *params)
{
	return crtc->tg->funcs->force_triggered_reset_now(crtc->tg, params);
}

bool dal_controller_program_flow_control(
	struct controller *crtc,
	enum sync_source source)
{
	return crtc->tg->funcs->program_flow_control(crtc->tg, source);
}

void dal_controller_set_early_control(
	struct controller *crtc,
	uint32_t early_cntl)
{
	crtc->tg->funcs->set_early_control(crtc->tg, early_cntl);
}

void dal_controller_set_advanced_request(
	struct controller *crtc,
	bool enable,
	const struct hw_crtc_timing *timing)
{
	crtc->tg->funcs->enable_advanced_request(crtc->tg, enable, timing);
}

/* (Set) Double buffering */
void dal_controller_set_lock_timing_registers(
	struct controller *crtc,
	bool lock)
{
	crtc->tg->funcs->set_lock_timing_registers(crtc->tg, lock);
}

void dal_controller_set_lock_graph_surface_registers(
	struct controller *crtc,
	bool lock)
{
	crtc->tg->funcs->set_lock_graph_surface_registers(crtc->tg, lock);
}

void dal_controller_set_lock_master(struct controller *crtc, bool lock)
{
	crtc->tg->funcs->set_lock_master(crtc->tg, lock);
}

/* (Set/Get) Global Swap Lock */
void dal_controller_setup_global_swaplock(
	struct controller *crtc,
	const struct dcp_gsl_params *params)
{
	crtc->tg->funcs->setup_global_swap_lock(crtc->tg, params);
}

void dal_controller_get_global_swaplock_setup(
	struct controller *crtc,
	struct dcp_gsl_params *params)
{
	crtc->tg->funcs->get_global_swap_lock_setup(crtc->tg, params);
}

/*
 * **************************************************************************
 * ***************************** VGA  ***************************************
 * **************************************************************************
 */

void dal_controller_disable_vga(struct controller *crtc)
{
	if (crtc->vga)
		crtc->vga->funcs->disable_vga(crtc->vga);
}

/*
 * **************************************************************************
 * *************************** Display Scaler  *******************************
 * **************************************************************************
 */

/* Get */
enum scaler_validation_code dal_controller_get_optimal_taps_number(
	struct controller *crtc,
	struct scaler_validation_params *params,
	struct scaling_tap_info *taps)
{
	return crtc->scl->funcs->get_optimal_taps_number(params, taps);
}

enum scaler_validation_code dal_controller_get_next_lower_taps_number(
	struct controller *crtc,
	struct scaler_validation_params *params,
	struct scaling_tap_info *taps)
{
	return crtc->scl->funcs->get_next_lower_taps_number(params, taps);
}

/* Set */
/* General purpose scaler programming interface */
bool dal_controller_set_scaler_wrapper(
	struct controller *crtc,
	const struct scaler_data *data)
{
	return crtc->scl->funcs->set_scaler_wrapper(crtc->scl, data);
}

/* not use scaling */
void dal_controller_set_scaler_bypass(struct controller *crtc)
{
	crtc->scl->funcs->set_scaler_bypass(crtc->scl);
}

bool dal_controller_is_scaling_enabled(struct controller *crtc)
{
	return crtc->scl->funcs->is_scaling_enabled(crtc->scl);
}

bool dal_controller_update_viewport(
		struct controller *crtc,
		const struct rect *view_port,
		bool is_fbc_attached)
{
	return
		dal_scaler_update_viewport(
			crtc->scl,
			view_port,
			is_fbc_attached);
}

/*
 * **************************************************************************
 * *************************** LUT and Gamma ******************************
 * **************************************************************************
 */

/* Set */
bool dal_controller_set_gamma_ramp(
	struct controller *crtc,
	const struct gamma_ramp *ramp,
	const struct gamma_parameters *params)
{
	return crtc->grph_gamma->funcs->set_gamma_ramp(
		crtc->grph_gamma, ramp, params);
}

bool dal_controller_set_palette(
	struct controller *crtc,
	const struct dev_c_lut *palette,
	uint32_t start,
	uint32_t length,
	enum pixel_format surface_format)
{
	return dal_grph_gamma_set_palette(
		crtc->grph_gamma, palette, start, length, surface_format);
}

/* set driver default gamma without any adjustment */
bool dal_controller_set_default_gamma(
	struct controller *crtc,
	enum pixel_format surface_format)
{
	return dal_grph_gamma_set_default_gamma(
		crtc->grph_gamma, surface_format);
}

/*
 *******************************************************************************
 ************************* Display color space and color adjustment  ***********
 *******************************************************************************
 */

/*******************************************************************************
* GetGrphAdjustmentRange
*  @param  [in] alphatype: one of the graphic matrix adjustment type
*  @param  [out] pAdjustRange:  adjustment HW range.
*  @return
*  @note
*    HW graphic color matrix adjustment range, DS provides API range, HWSS
*    converts HW adjustment unit to do adjustment.
*  @see
*******************************************************************************/
void dal_controller_get_grph_adjustment_range(
	struct controller *crtc,
	enum grph_csc_adjust_item adjust_item,
	struct hw_adjustment_range *adjust_range)
{
	dal_csc_grph_get_graphic_color_adjustment_range(
		adjust_item, adjust_range);
}

bool dal_controller_is_supported_custom_gamut_adjustment(
	struct controller *crtc,
	enum surface_type surface_type)
{
	return crtc->csc->funcs->
			is_supported_custom_gamut_adjustment(crtc->csc);
}

bool dal_controller_is_supported_custom_gamma_coefficients(
	struct controller *crtc,
	enum surface_type surface_type)
{
	return surface_type == GRAPHIC_SURFACE;
}

bool dal_controller_is_supported_overlay_alpha_adjustment(
	struct controller *crtc)
{
	return crtc->csc->funcs->
		is_supported_overlay_alpha_adjustment(crtc->csc);
}

bool dal_controller_set_input_csc(
	struct controller *crtc,
	const enum color_space color_space)
{
	return crtc->csc->funcs->
			set_input_csc(crtc->csc, color_space);
}

void dal_controller_get_overlay_adjustment_range(
	struct controller *crtc,
	enum ovl_csc_adjust_item overlay_adjust_item,
	struct hw_adjustment_range *adjust_range)
{
	dal_csc_video_get_ovl_adjustment_range(
		crtc->csc->csc_video,
		overlay_adjust_item,
		adjust_range);
}

void dal_controller_set_grph_csc_default(
	struct controller *crtc,
	const struct default_adjustment *default_adjust)
{
	if (default_adjust)
		crtc->csc->funcs->
		set_grph_csc_default(crtc->csc,	default_adjust);
}

void dal_controller_set_grph_csc_adjustment(
	struct controller *crtc,
	const struct grph_csc_adjustment *adjust)
{
	crtc->csc->funcs->set_grph_csc_adjustment(crtc->csc, adjust);
}

void dal_controller_set_overscan_color_black(
	struct controller *crtc,
	enum color_space color_space)
{
	crtc->csc->funcs->set_overscan_color_black(crtc->csc, color_space);
}

void dal_controller_set_ovl_csc_adjustment(
	struct controller *crtc,
	const struct ovl_csc_adjustment *adjust,
	enum color_space color_space)
{
	crtc->csc->funcs->
	set_ovl_csc_adjustment(crtc->csc, adjust, color_space);

	ASSERT(0 != adjust->overlay_gamma.adjust_divider);
	if (adjust->overlay_gamma.adjust_divider) {
		struct overlay_gamma_parameters *data = NULL;

		data = dal_alloc(sizeof(*data));

		if (!data) {
			BREAK_TO_DEBUGGER();
			/* memory allocation failure */
			return;
		}

		data->ovl_gamma_cont = adjust->overlay_gamma.adjust
			/ adjust->overlay_gamma.adjust_divider;
		data->adjust_type = adjust->adjust_gamma_type;
		data->desktop_surface = adjust->desktop_surface_pixel_format;
		data->flag.u_all = adjust->flag.u_all;

		dal_memmove(&data->regamma, &adjust->regamma,
			sizeof(data->regamma));

		crtc->video_gamma->funcs->
		set_overlay_pwl_adjustment(crtc->video_gamma, data);

		dal_free(data);
	}
}

void dal_controller_set_vertical_sync_adjustment(
	struct controller *crtc,
	uint32_t v_sync_polarity)
{
	crtc->tg->funcs->set_vertical_sync_polarity(crtc->tg, v_sync_polarity);
}

void dal_controller_set_horizontal_sync_adjustment(
	struct controller *crtc,
	uint32_t h_sync_polarity)
{
	crtc->tg->funcs->set_horizontal_sync_polarity(
		crtc->tg,
		h_sync_polarity);
}

void dal_controller_set_horizontal_sync_composite(
	struct controller *crtc,
	uint32_t h_sync_composite)
{
	crtc->tg->funcs->
	set_horizontal_sync_composite(crtc->tg, h_sync_composite);
}

/*
 *******************************************************************************
 **************************** FMT (Dithering) **********************************
 *******************************************************************************
 */

/* Formatter Block */
void dal_controller_program_formatter_bit_depth_reduction(
	struct controller *crtc,
	const struct bit_depth_reduction_params *info)
{
	crtc->fmt->funcs->program_bit_depth_reduction(crtc->fmt, info);
}

void dal_controller_program_formatter_clamping_and_pixel_encoding(
	struct controller *crtc,
	const struct clamping_and_pixel_encoding_params
	*info)
{
	crtc->fmt->funcs->program_clamping_and_pixel_encoding(crtc->fmt, info);
}

/* Deep color in FMAT */
void dal_controller_formatter_set_dyn_expansion(
	struct controller *crtc,
	enum color_space color_space,
	enum color_depth color_depth,
	enum signal_type signal)
{
	crtc->fmt->funcs->
	set_dyn_expansion(crtc->fmt, color_space, color_depth, signal);
}

/*
 * *****************************************************************************
 * ************************** Stereo *******************************************
 * *****************************************************************************
 */

bool dal_controller_get_stereo_status(
	struct controller *crtc,
	struct crtc_stereo_status *status)
{
	return crtc->tg->funcs->get_stereo_status(crtc->tg, status);
}

void dal_controller_enable_stereo(
	struct controller *crtc,
	const struct crtc_stereo_parameters *params)
{
	if (!params->FRAME_PACKED) {
		if (params->PROGRAM_STEREO)
			crtc->fmt->funcs->setup_stereo_polarity(crtc->fmt,
				FMT_STEREO_ACTION_ENABLE,
				params->RIGHT_EYE_POLARITY);
		else if (params->PROGRAM_POLARITY)
			crtc->fmt->funcs->setup_stereo_polarity(crtc->fmt,
				FMT_STEREO_ACTION_UPDATE_POLARITY,
				params->RIGHT_EYE_POLARITY);
	}

	crtc->tg->funcs->enable_stereo(crtc->tg, params);
}

void dal_controller_disable_stereo(struct controller *crtc)
{
	crtc->fmt->funcs->setup_stereo_polarity(crtc->fmt,
			FMT_STEREO_ACTION_DISABLE,
			false);
	crtc->tg->funcs->disable_stereo(crtc->tg);
}

void dal_controller_force_stereo_next_eye(
	struct controller *crtc,
	bool right_eye)
{
	crtc->tg->funcs->force_stereo_next_eye(crtc->tg, right_eye);
}

void dal_controller_reset_stereo_3d_phase(struct controller *crtc)
{
	crtc->tg->funcs->reset_stereo_3d_phase(crtc->tg);
}

void dal_controller_enable_stereo_mixer(
	struct controller *crtc,
	const struct crtc_mixer_params *params)
{
	if (crtc->pc)
		crtc->pc->funcs->enable_stereo_mixer(crtc->pc, params);
}

void dal_controller_disable_stereo_mixer(struct controller *crtc)
{
	if (crtc->pc)
		crtc->pc->funcs->disable_stereo_mixer(crtc->pc);
}

void dal_controller_set_test_pattern(
	struct controller *crtc,
	enum dp_test_pattern test_pattern,
	enum hw_color_depth color_depth)
{
	crtc->tg->funcs->set_test_pattern(crtc->tg, test_pattern, color_depth);
}

void dal_controller_set_scaler_filter(
	struct controller *crtc,
	struct scaler_filter *filter)
{
	crtc->scl->filter = filter;
}

/*
 * dal_controller_get_vblank_counter
 *
 * @brief
 * Get counter of vblanks
 *
 * @param
 * struct controller *crtc - [in] desired controller
 *
 * @return
 * Counter of frames
 */
uint32_t dal_controller_get_vblank_counter(struct controller *crtc)
{
	return crtc->tg->funcs->get_vblank_counter(crtc->tg);
}

void dal_controller_set_blender_mode(
	struct controller *crtc,
	enum blender_mode mode)
{
	crtc->pc->funcs->set_blender_mode(crtc->pc, mode);
}

bool dal_controller_program_alpha_blending(
	struct controller *crtc,
	const struct alpha_mode_cfg *cfg)
{
	return crtc->pc->funcs->program_alpha_blending(crtc->pc, cfg);
}

bool dal_controller_is_surface_supported(
	struct controller *crtc,
	const struct plane_config *pl_cfg)
{
	return crtc->funcs.is_surface_supported(crtc, pl_cfg);
}

/*
 * **************************************************************************
 * ********************* Surface Interface *************************
 * **************************************************************************
 */

bool dal_controller_program_surface_config(
	struct controller *crtc,
	const struct plane_surface_config *configs)
{
	return dal_surface_program_config(
		crtc->surface,
		configs);
}

bool dal_controller_program_surface_flip_and_addr(
	struct controller *crtc,
	const struct plane_addr_flip_info *flip_info)
{
	return dal_surface_program_flip_and_addr(
		crtc->surface,
		flip_info);
}

/*
 * **************************************************************************
 * ********************* Cursor Interface *************************
 * **************************************************************************
 */
bool dal_controller_set_cursor_position(
		struct controller *crtc,
		const struct cursor_position *position)
{
	return dal_cursor_set_position(
			crtc->cursor,
			position);
}

bool dal_controller_set_cursor_attributes(
		struct controller *crtc,
		const struct cursor_attributes *attributes)
{
	return dal_cursor_set_attributes(
			crtc->cursor,
			attributes);
}

