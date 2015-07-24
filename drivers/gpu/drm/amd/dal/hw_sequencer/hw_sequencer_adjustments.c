/*
 * Copyright 2012-13 Advanced Micro Devices, Inc.
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

#include "include/hw_sequencer_interface.h"
#include "include/hw_sequencer_types.h"
#include "include/hw_adjustment_types.h"
#include "include/display_path_interface.h"
#include "include/hw_adjustment_set.h"
#include "include/adjustment_types.h"

#include "hw_sequencer.h"

struct hw_underscan_parameters {
	struct scaling_tap_info *taps;
	struct adjustment_factor scale_ratio_hp_factor;
	struct adjustment_factor scale_ratio_lp_factor;
	struct sharpness_adjustment sharp_gain;
	uint32_t path_id;
	struct hw_path_mode_set *path_set;
	struct hw_path_mode *hw_path_mode;
	struct minimum_clocks_calculation_result mini_clk_result;
	struct pll_settings *pll_setting_array;
	struct min_clock_params *min_clk_array;
	struct watermark_input_params *watermark_input_array;
	struct lb_params_data *line_buffer_array;
};

static struct hw_path_mode *get_required_mode_path(
	struct hw_path_mode_set *set,
	enum hw_path_action action,
	uint32_t *path_id)
{
	struct hw_path_mode *path_mode;
	uint32_t i;
	for (i = 0; i < dal_hw_path_mode_set_get_paths_number(set); i++) {
		path_mode = dal_hw_path_mode_set_get_path_by_index(set, i);
	if (path_mode->action == action)
		if (path_id)
			*path_id = i;
	return path_mode;
	}
	return NULL;
}

static enum hwss_result program_overscan(
	struct hw_sequencer *hws,
	struct hw_path_mode_set *set,
	struct hw_underscan_parameters *param,
	struct hwss_build_params *build_param,
	struct display_path *display_path,
	bool update_avi_info_frame,
	uint32_t param_count)
{
	struct scaler_data scaler_data;
	enum clocks_state required_clocks_state;
	struct hw_global_objects g_obj = { NULL };
	struct hw_crtc_timing hw_crtc_timing = {0};

	struct controller *controller = dal_display_path_get_controller(
		param->hw_path_mode->display_path);

	bool scaler_will_be_enabled;
	bool scaler_has_been_enabled = dal_controller_is_scaling_enabled(
		controller);
	/* for now used params for 0 plane always */
	uint32_t plane = 0;

	dal_memset(&scaler_data, 0, sizeof(scaler_data));
	scaler_data.hw_crtc_timing = &hw_crtc_timing;
	if (param->hw_path_mode)
		dal_hw_sequencer_build_scaler_parameter(
			param->hw_path_mode,
			&param->taps[plane],
			true,
			&scaler_data);
	scaler_data.scale_ratio_hp_factor = param->scale_ratio_hp_factor;
	scaler_data.scale_ratio_lp_factor = param->scale_ratio_lp_factor;
	scaler_data.sharp_gain = param->sharp_gain;
	scaler_will_be_enabled = (scaler_data.taps.h_taps > 1 ||
		scaler_data.taps.v_taps > 1);

	dal_hw_sequencer_get_global_objects(param->path_set, &g_obj);

	/* i skip HP/LP factor ,since i found it no needed since dce 8*/
	hws->funcs->set_safe_displaymark(hws, set,
		build_param->wm_input_params, build_param->params_num);

	/* Use minimum clocks for the mode set.
	 * (because minimum clocks == maximum power efficiency) */
	/* Raise Required Clock State PRIOR to programming of state-dependent
		 * clocks. */
	required_clocks_state = hws->funcs->get_required_clocks_state(hws,
		g_obj.dc, set, &build_param->min_clock_result);

	/* Call PPLib and Bandwidth Manager. */
	if (true == hws->use_pp_lib &&
		HWSS_RESULT_OK != dal_hw_sequencer_set_clocks_and_clock_state(
			hws,
			&g_obj,
			&build_param->min_clock_result,
			required_clocks_state)) {
		/* should never happen */
		return HWSS_RESULT_ERROR;
	}

	if (!scaler_has_been_enabled && scaler_will_be_enabled) {
		hws->funcs->set_display_clock(
			hws, set, &build_param->min_clock_result);
		dal_hw_sequencer_enable_line_buffer_power_gating(
			dal_controller_get_line_buffer(controller),
			dal_controller_get_id(controller),
			scaler_data.pixel_type,
			param->hw_path_mode->mode.scaling_info.src.width,
			param->hw_path_mode->mode.scaling_info.dst.width,
			&param->taps[plane],
			param->line_buffer_array[plane].depth,
			param->hw_path_mode->mode.scaling_info.src.height,
			param->hw_path_mode->mode.scaling_info.dst.height,
			param->hw_path_mode->mode.timing.flags.INTERLACED);

		hws->funcs->setup_line_buffer_pixel_depth(
			hws,
			controller,
			param->line_buffer_array[plane].depth,
			true);

		dal_controller_set_scaler_wrapper(controller, &scaler_data);
	} else if (scaler_has_been_enabled && !scaler_will_be_enabled) {
		dal_controller_set_scaler_wrapper(controller, &scaler_data);
		hws->funcs->setup_line_buffer_pixel_depth(
			hws,
			controller,
			param->line_buffer_array[plane].depth,
			true);

		dal_controller_wait_for_vblank(controller);
		dal_hw_sequencer_enable_line_buffer_power_gating(
			dal_controller_get_line_buffer(controller),
			dal_controller_get_id(controller),
			scaler_data.pixel_type,
			param->hw_path_mode->mode.scaling_info.src.width,
			param->hw_path_mode->mode.scaling_info.dst.width,
			&param->taps[plane],
			param->line_buffer_array[plane].depth,
			param->hw_path_mode->mode.scaling_info.src.height,
			param->hw_path_mode->mode.scaling_info.dst.height,
			param->hw_path_mode->mode.timing.flags.INTERLACED);

		hws->funcs->set_display_clock(
			hws, set, &build_param->min_clock_result);

	} else if (scaler_has_been_enabled && scaler_will_be_enabled) {
		hws->funcs->set_display_clock(
			hws, set, &build_param->min_clock_result);

		dal_controller_set_scaler_wrapper(controller, &scaler_data);
	}

	hws->funcs->set_displaymark(
		hws,
		set,
		build_param->wm_input_params,
		build_param->params_num);

	if (update_avi_info_frame)
		dal_hw_sequencer_update_info_frame(param->hw_path_mode);
	return HWSS_RESULT_OK;
}

enum hwss_result dal_hw_sequencer_set_overscan_adj(
	struct hw_sequencer *hws,
	struct hw_path_mode_set *set,
	struct hw_underscan_adjustment_data *hw_underscan)
{
	struct hwss_build_params *build_params = NULL;
	union hwss_build_params_mask params_mask;
	struct hw_underscan_parameters underscan_params;
	const struct hw_underscan_adjustment *value;

	if (set == NULL || hw_underscan->hw_adj_id != HW_ADJUSTMENT_ID_OVERSCAN)
		return HWSS_RESULT_ERROR;

	value = &hw_underscan->hw_underscan_adj;
	if (!value)
		return HWSS_RESULT_ERROR;

	dal_memset(&underscan_params, 0, sizeof(underscan_params));

	underscan_params.hw_path_mode = get_required_mode_path(
		set,
		HW_PATH_ACTION_SET_ADJUSTMENT,
		&underscan_params.path_id);
	if (!underscan_params.hw_path_mode)
		return HWSS_RESULT_ERROR;

	params_mask.all = 0;
	params_mask.bits.SCALING_TAPS = true;
	params_mask.bits.PLL_SETTINGS = true;
	params_mask.bits.MIN_CLOCKS = true;
	params_mask.bits.WATERMARK = true;
	params_mask.bits.BANDWIDTH = true;
	params_mask.bits.LINE_BUFFER = true;

	build_params = dal_hw_sequencer_prepare_path_parameters(
		hws,
		set,
		params_mask,
		false);

	if (NULL == build_params)
		return HWSS_RESULT_ERROR;

	underscan_params.path_set = set;
	underscan_params.pll_setting_array =
			build_params->pll_settings_params;
	underscan_params.watermark_input_array =
			build_params->wm_input_params;
	underscan_params.min_clk_array =
			build_params->min_clock_params;
	underscan_params.line_buffer_array =
		build_params->line_buffer_params[underscan_params.path_id];
	underscan_params.taps =
		build_params->scaling_taps_params[underscan_params.path_id];
	underscan_params.mini_clk_result =
			build_params->min_clock_result;

	underscan_params.scale_ratio_hp_factor.adjust =
			value->deflicker.hp_factor;
	underscan_params.scale_ratio_hp_factor.divider =
			value->deflicker.hp_divider;
	underscan_params.scale_ratio_lp_factor.adjust =
			value->deflicker.lp_factor;
	underscan_params.scale_ratio_lp_factor.divider =
			value->deflicker.lp_divider;

	underscan_params.sharp_gain.sharpness =
			value->deflicker.sharpness;
	underscan_params.sharp_gain.enable_sharpening =
			value->deflicker.enable_sharpening;

	if (program_overscan(
		hws,
		set,
		&underscan_params,
		build_params,
		underscan_params.hw_path_mode->display_path,
		true,
		build_params->params_num) != HWSS_RESULT_OK) {
		dal_hw_sequencer_free_path_parameters(build_params);
		return HWSS_RESULT_ERROR;
	}

	dal_hw_sequencer_free_path_parameters(build_params);
	return HWSS_RESULT_OK;
}

bool dal_hw_sequencer_is_support_custom_gamut_adj(
	struct hw_sequencer *hws,
	struct display_path *disp_path,
	enum hw_surface_type surface_type)
{
	struct controller *controller = NULL;

	if (!disp_path)
		return false;

	controller =
			dal_display_path_get_controller(disp_path);

	if (!controller)
		return false;

	return	dal_controller_is_supported_custom_gamut_adjustment(
				controller,
				(surface_type == HW_GRAPHIC_SURFACE ?
						GRAPHIC_SURFACE :
						OVERLAY_SURFACE));
}

enum hwss_result dal_hw_sequencer_get_hw_color_adj_range(
	struct hw_sequencer *hws,
	struct display_path *disp_path,
	struct hw_color_control_range *hw_color_range)
{
	struct controller *controller = NULL;

	if (!disp_path)
		return HWSS_RESULT_ERROR;
	if (!hw_color_range)
		return HWSS_RESULT_ERROR;

	controller = dal_display_path_get_controller(disp_path);
	if (!controller)
		return HWSS_RESULT_ERROR;

	dal_controller_get_grph_adjustment_range(
			controller,
			GRPH_ADJUSTMENT_HUE,
			&hw_color_range->hue);
	dal_controller_get_grph_adjustment_range(
			controller,
			GRPH_ADJUSTMENT_SATURATION,
			&hw_color_range->saturation);
	dal_controller_get_grph_adjustment_range(
			controller,
			GRPH_ADJUSTMENT_BRIGHTNESS,
			&hw_color_range->brightness);
	dal_controller_get_grph_adjustment_range(
			controller,
			GRPH_ADJUSTMENT_CONTRAST,
			&hw_color_range->contrast);
	dal_controller_get_grph_adjustment_range(
			controller,
			GRPH_ADJUSTMENT_COLOR_TEMPERATURE,
			&hw_color_range->temperature);
	return HWSS_RESULT_OK;

}

bool dal_hw_sequencer_is_support_custom_gamma_coefficients(
	struct hw_sequencer *hws,
	struct display_path *disp_path,
	enum hw_surface_type surface_type)
{
	struct controller *controller = NULL;

	if (!disp_path)
		return false;

	controller = dal_display_path_get_controller(disp_path);
	if (!controller)
		return false;

	return	dal_controller_is_supported_custom_gamma_coefficients(
					controller,
					(surface_type == HW_GRAPHIC_SURFACE ?
							GRAPHIC_SURFACE :
							OVERLAY_SURFACE));
}

static enum ds_color_space translation_to_color_space(
	enum hw_color_space hw_color_space)
{
		enum ds_color_space color_space;

		switch (hw_color_space) {
		case HW_COLOR_SPACE_SRGB_FULL_RANGE:
			color_space = DS_COLOR_SPACE_SRGB_FULLRANGE;
			break;
		case HW_COLOR_SPACE_SRGB_LIMITED_RANGE:
			color_space = DS_COLOR_SPACE_SRGB_LIMITEDRANGE;
			break;
		case HW_COLOR_SPACE_YPBPR601:
			color_space = DS_COLOR_SPACE_YPBPR601;
			break;
		case HW_COLOR_SPACE_YPBPR709:
			color_space = DS_COLOR_SPACE_YPBPR709;
			break;
		case HW_COLOR_SPACE_YCBCR601:
			color_space = DS_COLOR_SPACE_YCBCR601;
			break;
		case HW_COLOR_SPACE_YCBCR709:
			color_space = DS_COLOR_SPACE_YCBCR709;
			break;
		case HW_COLOR_SPACE_NMVPU_SUPERAA:
			color_space = DS_COLOR_SPACE_NMVPU_SUPERAA;
			break;
		default:
			color_space = DS_COLOR_SPACE_UNKNOWN;
			break;
		}
		return color_space;
}

static enum csc_color_depth translation_to_csc_color_depth(
	enum hw_color_depth hw_color_depth)
{
	enum csc_color_depth csc_color_depth;

	switch (hw_color_depth) {
	case HW_COLOR_DEPTH_666:
		csc_color_depth = CSC_COLOR_DEPTH_666;
		break;
	case HW_COLOR_DEPTH_888:
		csc_color_depth = CSC_COLOR_DEPTH_888;
		break;
	case HW_COLOR_DEPTH_101010:
		csc_color_depth = CSC_COLOR_DEPTH_101010;
		break;
	case HW_COLOR_DEPTH_121212:
		csc_color_depth = CSC_COLOR_DEPTH_121212;
		break;
	case HW_COLOR_DEPTH_141414:
		csc_color_depth = CSC_COLOR_DEPTH_141414;
		break;
	case HW_COLOR_DEPTH_161616:
		csc_color_depth = CSC_COLOR_DEPTH_161616;
		break;
	default:
		csc_color_depth = CSC_COLOR_DEPTH_888;
		break;
	}
	return csc_color_depth;
}

enum hwss_result dal_hw_sequencer_build_csc_adjust(
	struct hw_sequencer *hws,
	struct hw_adjustment_color_control *adjustment,
	struct grph_csc_adjustment *adjust)
{
	if (!adjustment)
		return HWSS_RESULT_ERROR;

	if (adjustment->temperature_divider == 0 ||
		adjustment->adjust_divider == 0)
		return HWSS_RESULT_ERROR;

	adjust->c_space = translation_to_color_space(
			adjustment->color_space);
	adjust->color_depth = translation_to_csc_color_depth(
			adjustment->color_depth);
	adjust->surface_pixel_format = adjustment->surface_pixel_format;

	switch (adjustment->option) {
	case HWS_COLOR_MATRIX_HW_DEFAULT:
		adjust->color_adjust_option = GRPH_COLOR_MATRIX_HW_DEFAULT;
		break;
	case HWS_COLOR_MATRIX_SW:
		adjust->color_adjust_option = GRPH_COLOR_MATRIX_SW;
		break;
	default:
		adjust->color_adjust_option = GRPH_COLOR_MATRIX_HW_DEFAULT;
		break;
	}

	adjust->grph_cont = adjustment->contrast;
	adjust->grph_sat = adjustment->saturation;
	adjust->grph_bright = adjustment->brightness;
	adjust->grph_hue = adjustment->hue;
	adjust->adjust_divider = adjustment->adjust_divider;
	adjust->temperature_divider = adjustment->temperature_divider;
	adjust->csc_adjust_type = GRAPHICS_CSC_ADJUST_TYPE_SW;
	adjust->gamut_adjust_type = GRAPHICS_GAMUT_ADJUST_TYPE_SW;
	dal_memmove(adjust->temperature_matrix,
			adjustment->temperature_matrix,
			sizeof(adjust->temperature_matrix));
	return HWSS_RESULT_OK;

}

void dal_hw_sequencer_build_gamma_ramp_adj_params(
		const struct hw_adjustment_gamma_ramp *adjustment,
		struct gamma_parameters *gamma_param,
		struct gamma_ramp *ramp) {
	ramp->type = GAMMA_RAMP_DEFAULT;
	ramp->size = adjustment->size;

	switch (adjustment->type) {
	case HW_GAMMA_RAMP_UNITIALIZED:
		ramp->type = GAMMA_RAMP_UNINITIALIZED;
		break;
	case HW_GAMMA_RAMP_DEFAULT:
		ramp->type = GAMMA_RAMP_DEFAULT;
		break;
	case HW_GAMMA_RAMP_RBG_256x3x16:
		ramp->type = GAMMA_RAMP_RBG256X3X16;
		dal_memmove(&ramp->gamma_ramp_rgb256x3x16,
			&adjustment->gamma_ramp_rgb256x3x16,
			adjustment->size);
		break;
	default:
		break;
	}
	/* translate parameters */
	gamma_param->surface_pixel_format = adjustment->surface_pixel_format;

	translate_from_hw_to_controller_regamma(
			&adjustment->regamma,
			&gamma_param->regamma);

	gamma_param->regamma_adjust_type = GRAPHICS_REGAMMA_ADJUST_SW;
	gamma_param->degamma_adjust_type = GRAPHICS_REGAMMA_ADJUST_SW;

	gamma_param->selected_gamma_lut = GRAPHICS_GAMMA_LUT_LEGACY;

	/* TODO support non-legacy gamma */

	gamma_param->disable_adjustments = false;
	gamma_param->flag.bits.config_is_changed =
			adjustment->flag.bits.config_is_changed;
	gamma_param->flag.bits.regamma_update =
			adjustment->flag.bits.regamma_update;
	gamma_param->flag.bits.gamma_update =
			adjustment->flag.bits.gamma_update;
}

void translate_from_hw_to_controller_regamma(
		const struct hw_regamma_lut *hw_regamma,
		struct regamma_lut *regamma)
{
	unsigned int i;

	regamma->features.bits.GRAPHICS_DEGAMMA_SRGB =
			hw_regamma->flags.bits.graphics_degamma_srgb;
	regamma->features.bits.OVERLAY_DEGAMMA_SRGB =
			hw_regamma->flags.bits.overlay_degamma_srgb;
	regamma->features.bits.GAMMA_RAMP_ARRAY =
			hw_regamma->flags.bits.gamma_ramp_array;

	if (hw_regamma->flags.bits.gamma_ramp_array == 1) {
		regamma->features.bits.APPLY_DEGAMMA =
				hw_regamma->flags.bits.apply_degamma;

		for (i = 0; i < 256 * 3; i++)
			regamma->regamma_ramp.gamma[i] =
					hw_regamma->gamma.gamma[i];

	} else {
		regamma->features.bits.APPLY_DEGAMMA = 0;

		for (i = 0; i < 3; i++) {
			regamma->gamma_coeff.a0[i] = hw_regamma->coeff.a0[i];
			regamma->gamma_coeff.a1[i] = hw_regamma->coeff.a1[i];
			regamma->gamma_coeff.a2[i] = hw_regamma->coeff.a2[i];
			regamma->gamma_coeff.a3[i] = hw_regamma->coeff.a3[i];
			regamma->gamma_coeff.gamma[i] =
					hw_regamma->coeff.gamma[i];
		}
	}

}
