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
#include "dal_services.h"
#include "include/hw_sequencer_types.h"
#include "include/display_service_types.h"
#include "include/display_path_interface.h"
#include "include/set_mode_interface.h"
#include "include/adjustment_interface.h"
#include "include/hw_sequencer_interface.h"
#include "include/hw_adjustment_set.h"
#include "include/logger_interface.h"
#include "include/fixed31_32.h"

#include "ds_dispatch.h"
#include "adjustment_container.h"
#include "grph_colors_group.h"
#include "gamut_space.h"
#include "color_temperature.h"
#include "ds_translation.h"

enum ds_color_space dal_grph_colors_group_get_color_space(
		struct grph_colors_group *grph_colors_adj,
		const struct crtc_timing *timing,
		const struct display_path *disp_path,
		struct adj_container *adj_container)
{
	const struct adjustment_info *nominal_limited = NULL;
	enum ds_color_space color_space = DS_COLOR_SPACE_UNKNOWN;

	if (disp_path == NULL || timing == NULL)
		return color_space;

	if (adj_container != NULL) {
		nominal_limited = dal_adj_info_set_get_adj_info(
					&adj_container->adj_info_set,
					ADJ_ID_NOMINAL_RANGE_RGB_LIMITED);
		switch (dal_display_path_get_query_signal(
				disp_path, SINK_LINK_INDEX)) {
		case SIGNAL_TYPE_MVPU_A:
		case SIGNAL_TYPE_MVPU_B:
		case SIGNAL_TYPE_MVPU_AB:
			color_space = dal_adj_container_get_color_space(
						adj_container);
			break;
		default:
			break;
		}
	}
	if (color_space == DS_COLOR_SPACE_UNKNOWN) {
		enum ds_color_space hdmi_request_color_space =
				DS_COLOR_SPACE_SRGB_LIMITEDRANGE;
		if (nominal_limited != NULL &&
			(nominal_limited->adj_data.ranged.cur ==
				nominal_limited->adj_data.ranged.min))
			hdmi_request_color_space =
					DS_COLOR_SPACE_SRGB_FULLRANGE;
		color_space =
			dal_grph_colors_group_build_default_color_space(
				grph_colors_adj,
				timing,
				disp_path,
				hdmi_request_color_space);
		if (nominal_limited != NULL &&
			nominal_limited->adj_data.ranged.cur !=
				nominal_limited->adj_data.ranged.min &&
				color_space == DS_COLOR_SPACE_SRGB_FULLRANGE)
			color_space = DS_COLOR_SPACE_SRGB_LIMITEDRANGE;
		else if (nominal_limited != NULL &&
			nominal_limited->adj_data.ranged.cur ==
				nominal_limited->adj_data.ranged.min &&
				color_space == DS_COLOR_SPACE_SRGB_LIMITEDRANGE)
			color_space = DS_COLOR_SPACE_SRGB_FULLRANGE;

	}
	return color_space;
}

enum ds_return dal_grph_colors_group_set_adjustment(
		struct grph_colors_group *grph_colors_adj,
		struct display_path *disp_path,
		enum adjustment_id adj_id,
		uint32_t value)
{
	enum ds_return ret = DS_ERROR;
	const struct path_mode_set_with_data *pms_wd;
	struct adj_container *adj_container;
	const struct path_mode *path_mode;
	const struct adjustment_info *adj_info;
	struct gamut_parameter *gamut = NULL;
	struct ds_regamma_lut *regamma = NULL;
	struct hw_adjustment_color_control *color_control = NULL;
	uint32_t display_index;

	if (!disp_path)
		return DS_ERROR;

	display_index = dal_display_path_get_display_index(
				disp_path);

	pms_wd = dal_ds_dispatch_get_active_pms_with_data(
			grph_colors_adj->ds);
	if (!pms_wd)
		return DS_ERROR;

	adj_container = dal_ds_dispatch_get_adj_container_for_path(
			grph_colors_adj->ds,
			display_index);

	if (!adj_container)
		return DS_ERROR;

	path_mode =
		dal_pms_with_data_get_path_mode_for_display_index(
			pms_wd,
			display_index);

	if (!path_mode)
		return DS_ERROR;


	adj_info = dal_adj_info_set_get_adj_info(
			&adj_container->adj_info_set,
			adj_id);

	if (!adj_info)
		return DS_ERROR;

	if (dal_adj_container_is_adjustment_committed(
			adj_container, adj_id) &&
			(adj_info->adj_data.ranged.cur == value))
		return DS_SUCCESS;

	if (!dal_adj_info_set_update_cur_value(
			&adj_container->adj_info_set,
			adj_id, value))
		return DS_ERROR;

	gamut = dal_alloc(sizeof(*gamut));
	if (!gamut)
		goto gamut_fail;
	regamma = dal_alloc(sizeof(*regamma));
	if (!regamma)
		goto regamma_fail;
	color_control = dal_alloc(sizeof(*color_control));
	if (!color_control)
		goto color_control_fail;

	if (dal_grph_colors_group_compute_hw_adj_color_control(
				grph_colors_adj,
				adj_container,
				&path_mode->mode_timing->crtc_timing,
				disp_path,
				adj_id,
				gamut,
				regamma,
				color_control)) {
		enum ds_color_space color_space;

		color_control->surface_pixel_format =
				path_mode->pixel_format;

		dal_adj_container_set_regamma(
				adj_container,
				regamma);

		dal_hw_sequencer_set_color_control_adjustment(
			grph_colors_adj->hws,
			dal_display_path_get_controller(disp_path),
			color_control);

		color_space =
			dal_ds_translation_color_space_from_hw_color_space(
				color_control->color_space);

		dal_adj_container_update_color_space(
				adj_container,
				color_space);
		dal_adj_container_commit_adj(adj_container, adj_id);
		ret = DS_SUCCESS;
	}

	dal_free(color_control);
color_control_fail:
	dal_free(regamma);
regamma_fail:
	dal_free(gamut);
gamut_fail:
	return ret;
}

static int32_t get_hw_value_from_sw_value(
	const struct hw_adjustment_range *hw_range,
	const struct adjustment_info *sw_range)
{
	int32_t sw_val = sw_range->adj_data.ranged.cur;
	int32_t sw_min = sw_range->adj_data.ranged.min;
	int32_t sw_max = sw_range->adj_data.ranged.max;
	int32_t hw_max = hw_range->max;
	int32_t hw_min = hw_range->min;
	int32_t sw_data = sw_max - sw_min;
	int32_t hw_data = hw_max - hw_min;
	int32_t hw_val;

	if (sw_data == 0)
		return hw_min;
	if (sw_data != hw_data)
		hw_val =
			(sw_val - sw_min) * hw_data / sw_data + hw_min;
	else {
		hw_val = sw_val;
		if (sw_min != hw_min)
			hw_val += (hw_min - sw_min);
	}

	return hw_val;
}

static bool is_current_same_as_default(
	struct adjustment_info *adj_info)
{
	return adj_info->adj_data.ranged.cur ==
			adj_info->adj_data.ranged.def;
}

bool dal_grph_colors_group_compute_hw_adj_color_control(
	struct grph_colors_group *grph_colors_adj,
	struct adj_container *adj_container,
	const struct crtc_timing *timing,
	struct display_path *disp_path,
	enum adjustment_id reason_id,
	struct gamut_parameter *gamut,
	struct ds_regamma_lut *regamma,
	struct hw_adjustment_color_control *color_control)
{
	struct adjustment_info *brightness =
			dal_adj_info_set_get_adj_info(
				&adj_container->adj_info_set,
				ADJ_ID_BRIGHTNESS);
	struct adjustment_info *contrast =
			dal_adj_info_set_get_adj_info(
				&adj_container->adj_info_set,
				ADJ_ID_CONTRAST);
	struct adjustment_info *hue =
			dal_adj_info_set_get_adj_info(
				&adj_container->adj_info_set,
				ADJ_ID_HUE);
	struct adjustment_info *saturation =
			dal_adj_info_set_get_adj_info(
				&adj_container->adj_info_set,
				ADJ_ID_SATURATION);
	struct adjustment_info *temperature =
			dal_adj_info_set_get_adj_info(
				&adj_container->adj_info_set,
				ADJ_ID_TEMPERATURE);
	struct adjustment_info *temperature_src =
			dal_adj_info_set_get_adj_info(
				&adj_container->adj_info_set,
				ADJ_ID_TEMPERATURE_SOURCE);
	const struct display_characteristics *disp_char =
			dal_adj_container_get_disp_character(
				adj_container);
	struct hw_color_control_range hw_color_range;
	struct white_point_data white_point;
	const struct ds_regamma_lut *const_regamma = NULL;
	int32_t requested_temperature;
	bool gamut_adj_avails;
	enum signal_type signal;

	/* reset updated info */
	grph_colors_adj->regamma_updated = false;
	grph_colors_adj->gamut_dst_updated = false;
	grph_colors_adj->gamut_src_updated = false;

	if (NULL == grph_colors_adj->hws ||
			NULL == brightness ||
			NULL == contrast ||
			NULL == hue ||
			NULL == saturation ||
			NULL == temperature ||
			NULL == disp_path)
		return false;

	dal_memset(&hw_color_range, 0, sizeof(hw_color_range));
	dal_memset(&white_point, 0, sizeof(white_point));
	requested_temperature = temperature->adj_data.ranged.cur;
	signal = dal_display_path_get_query_signal(
			disp_path, SINK_LINK_INDEX);
	color_control->adjust_divider = ADJUST_DIVIDER;
	color_control->temperature_divider = GAMUT_DIVIDER;
	gamut_adj_avails =
			dal_hw_sequencer_is_support_custom_gamut_adj(
				grph_colors_adj->hws,
				disp_path,
				HW_GRAPHIC_SURFACE);

	if (!dal_adj_container_get_gamut(
			adj_container,
			ADJ_ID_GAMUT_SOURCE_GRPH,
			&gamut->gamut_src))
		return false;

	if (!dal_adj_container_get_gamut(
			adj_container,
			ADJ_ID_GAMUT_DESTINATION,
			&gamut->gamut_dst))
		return false;

	const_regamma = dal_adj_container_get_regamma(
			adj_container);
	if (!const_regamma)
		return false;
	dal_memmove(&gamut->regamma, const_regamma, sizeof(gamut->regamma));

	if (signal == SIGNAL_TYPE_HDMI_TYPE_A)
		gamut->source = GAMUT_SPACE_SOURCE_DEFAULT;
	else {
		gamut->source =
			temperature_src->adj_data.ranged.cur ==
				COLOR_TEMPERATURE_SOURCE_EDID ?
				GAMUT_SPACE_SOURCE_EDID :
				GAMUT_SPACE_SOURCE_USER_GAMUT;
		if (requested_temperature == -1)
			gamut->source = GAMUT_SPACE_SOURCE_EDID;

		if (disp_char == NULL && gamut->source ==
				GAMUT_SPACE_SOURCE_EDID)
			gamut->source = GAMUT_SPACE_SOURCE_DEFAULT;

		if (gamut->source == GAMUT_SPACE_SOURCE_EDID) {
			uint32_t i;

			dal_gamut_space_reset_gamut(
					&gamut->gamut_dst, true, true);

			for (i = 0;
				i < sizeof(gamut->color_charact.color_charact);
				i++) {
				gamut->color_charact.color_charact[i] =
					disp_char->color_characteristics[i];
			}
			if (disp_char->gamma > 0)
				gamut->color_charact.gamma =
					(disp_char->gamma + 100)*10;
			else
				gamut->color_charact.gamma = 0;
		}
	}
	if (gamut->source == GAMUT_SPACE_SOURCE_DEFAULT) {
		requested_temperature = temperature->adj_data.ranged.def;
		if (!dal_color_temperature_find_white_point(
				requested_temperature,
				&white_point))
			return false;
		dal_gamut_space_reset_gamut(
				&gamut->gamut_src, false, true);
		gamut->gamut_src.option.bits.CUSTOM_WHITE_POINT = 1;
		gamut->gamut_src.white_point.custom.white_x =
				white_point.white_x;
		gamut->gamut_src.white_point.custom.white_y =
				white_point.white_y;

		if (!dal_adj_container_validate_gamut(
				adj_container,
				&gamut->gamut_src))
			return false;

		dal_adj_container_update_gamut(
				adj_container,
				ADJ_ID_GAMUT_SOURCE_GRPH,
				&gamut->gamut_src);
		{
			struct adjustment_info *non_const_temp = temperature;

			non_const_temp->adj_data.ranged.cur =
					requested_temperature;
		}
	}
	{
		union update_color_flags flags = {0};

		if (!dal_gamut_space_update_gamut(
					gamut, false, &flags))
				return false;
		if (flags.bits.GAMUT_DST == 1)
			dal_adj_container_update_gamut(
					adj_container,
					ADJ_ID_GAMUT_DESTINATION,
					&gamut->gamut_dst);
		if (reason_id != ADJ_ID_GAMUT_SOURCE_GRPH &&
			reason_id != ADJ_ID_GAMUT_DESTINATION) {
			if (gamut->source == GAMUT_SPACE_SOURCE_EDID)
				dal_gamut_space_setup_default_gamut(
						reason_id,
						&gamut->gamut_src,
						false,
						true);
		}
		if (!dal_gamut_space_build_gamut_space_matrix(
				gamut,
				color_control->temperature_matrix,
				regamma,
				&flags))
			return false;

		if (flags.bits.REGAMMA > 0)
			grph_colors_adj->regamma_updated = true;
		if (flags.bits.GAMUT_SRC > 0)
			grph_colors_adj->gamut_src_updated = true;
		if (flags.bits.GAMUT_DST > 0)
			grph_colors_adj->gamut_dst_updated = true;
	}
	if (dal_hw_sequencer_get_hw_color_adj_range(
			grph_colors_adj->hws,
			disp_path,
			&hw_color_range) != HWSS_RESULT_OK)
		return false;
	color_control->color_space =
		dal_ds_translation_hw_color_space_from_color_space(
			dal_grph_colors_group_get_color_space(
					grph_colors_adj,
					timing,
					disp_path,
					adj_container));
	if (color_control->color_space ==
			HW_COLOR_SPACE_UNKNOWN)
		return false;
	{
		struct hw_crtc_timing hw_timing;
		enum signal_type asic_signal =
				dal_display_path_get_query_signal(
				disp_path, ASIC_LINK_INDEX);
		dal_ds_translation_hw_crtc_timing_from_crtc_timing(
			&hw_timing,
			timing,
			VIEW_3D_FORMAT_NONE,
			asic_signal);

		color_control->color_depth =
				hw_timing.flags.COLOR_DEPTH;
		color_control->brightness =
				get_hw_value_from_sw_value(
					&hw_color_range.brightness,
					brightness);
		color_control->contrast =
				get_hw_value_from_sw_value(
					&hw_color_range.contrast,
					contrast);
		color_control->hue =
				get_hw_value_from_sw_value(
					&hw_color_range.hue,
					hue);
		color_control->saturation =
				get_hw_value_from_sw_value(
					&hw_color_range.saturation,
					saturation);
		if (gamut->source == GAMUT_SPACE_SOURCE_USER_GAMUT &&
			gamut_adj_avails == false &&
			is_current_same_as_default(saturation) &&
			is_current_same_as_default(contrast) &&
			is_current_same_as_default(brightness) &&
			is_current_same_as_default(hue) &&
			is_current_same_as_default(temperature))
			color_control->option = HWS_COLOR_MATRIX_HW_DEFAULT;
		else
			color_control->option = HWS_COLOR_MATRIX_SW;

	}

	return true;
}

bool dal_grph_colors_group_build_color_control_adj(
	struct grph_colors_group *grph_colors_adj,
	const struct path_mode *mode,
	struct display_path *disp_path,
	struct hw_adjustment_set *set)
{
	struct adj_container *adj_container;
	struct gamut_parameter *gamut = NULL;
	struct ds_regamma_lut *regamma = NULL;
	struct hw_adjustment_color_control *color_control = NULL;
	enum ds_color_space color_space;

	if (!disp_path)
		return false;
	gamut = dal_alloc(sizeof(*gamut));
	if (!gamut)
		goto gamut_fail;
	regamma = dal_alloc(sizeof(*regamma));
	if (!regamma)
		goto regamma_fail;
	color_control = dal_alloc(sizeof(*color_control));
	if (!color_control)
		goto color_control_fail;

	adj_container = dal_ds_dispatch_get_adj_container_for_path(
		grph_colors_adj->ds,
		mode->display_path_index);
	if (!adj_container)
		goto adj_container_fail;

	color_control->color_space = HW_COLOR_SPACE_UNKNOWN;

	if (!dal_grph_colors_group_compute_hw_adj_color_control(
			grph_colors_adj,
			adj_container,
			&mode->mode_timing->crtc_timing,
			disp_path,
			ADJ_ID_INVALID,
			gamut,
			regamma,
			color_control))
		goto adj_container_fail;
	color_control->surface_pixel_format = mode->pixel_format;
	set->color_control = color_control;
	color_space = dal_ds_translation_color_space_from_hw_color_space(
			color_control->color_space);
	dal_adj_container_update_color_space(
			adj_container,
			color_space);

	dal_free(regamma);
	dal_free(gamut);

	return true;

adj_container_fail:
	dal_free(color_control);
color_control_fail:
	dal_free(regamma);
regamma_fail:
	dal_free(gamut);
gamut_fail:
	return false;
}

enum ds_color_space dal_grph_colors_group_build_default_color_space(
		struct grph_colors_group *grph_colors_adj,
		const struct crtc_timing *timing,
		const struct display_path *disp_path,
		enum ds_color_space hdmi_request_color_space)
{
	enum ds_color_space color_space =
			DS_COLOR_SPACE_SRGB_FULLRANGE;

	switch (dal_display_path_get_query_signal(
			disp_path, SINK_LINK_INDEX)) {
	case SIGNAL_TYPE_DISPLAY_PORT:
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
	case SIGNAL_TYPE_EDP:
		if (timing->pixel_encoding == PIXEL_ENCODING_YCBCR422 ||
			timing->pixel_encoding == PIXEL_ENCODING_YCBCR444) {
			color_space = (timing->pix_clk_khz > PIXEL_CLOCK) ?
					DS_COLOR_SPACE_YCBCR709 :
					DS_COLOR_SPACE_YCBCR601;
			if (timing->flags.YONLY == 1)
				color_space =
					(timing->pix_clk_khz > PIXEL_CLOCK) ?
						DS_COLOR_SPACE_YCBCR709_YONLY :
						DS_COLOR_SPACE_YCBCR601_YONLY;
		}
		break;
	case SIGNAL_TYPE_HDMI_TYPE_A:
	{
		uint32_t pix_clk_khz;

		color_space = hdmi_request_color_space;
		if (timing->pixel_encoding == PIXEL_ENCODING_YCBCR422 &&
			timing->pixel_encoding == PIXEL_ENCODING_YCBCR444) {
			if (timing->timing_standard ==
					TIMING_STANDARD_CEA770 &&
				timing->timing_standard ==
						TIMING_STANDARD_CEA861)
				color_space = DS_COLOR_SPACE_SRGB_FULLRANGE;
			/* else {
			union cea_video_capability_data_block
								vcdb = {{0 }};
			bool ret =
			dal_edid_base_get_cea_video_capability_data_block(
				disp_path->dcs->edid_mgr->edid_list,
				&vcdb);
			if ((ret == true) && (vcdb.bits.QS == 1))
			color_space = DS_COLOR_SPACE_SRGB_FULLRANGE;
			} */
			pix_clk_khz = timing->pix_clk_khz / 10;
			if (timing->h_addressable == 640 &&
				timing->v_addressable == 480 &&
				(pix_clk_khz == 2520 || pix_clk_khz == 2517))
				color_space = DS_COLOR_SPACE_SRGB_FULLRANGE;
		} else {
			if (timing->timing_standard ==
					TIMING_STANDARD_CEA770 ||
					timing->timing_standard ==
					TIMING_STANDARD_CEA861) {
				/* struct cea_colorimetry_data_block
				 * data_block = {0};
			if (dal_edid_base_get_cea_colorimetry_data_block(
					disp_path->dcs->edid_mgr->edid_list,
					&data_block)) {
			if (data_block->flag.XV_YCC601 == 1 &&
				data_block->flag.XV_YCC709 == 1)
				color_space =
				(timing->pix_clk_khz > PIXEL_CLOCK) ?
						DS_COLOR_SPACE_YCBCR709 :
						DS_COLOR_SPACE_YCBCR601;
			else
				color_space =
				(data_block->flag.XV_YCC709 == 1) ?
					DS_COLOR_SPACE_YCBCR709 :
					DS_COLOR_SPACE_YCBCR601;
			} else */
				color_space =
					(timing->pix_clk_khz > PIXEL_CLOCK) ?
						DS_COLOR_SPACE_YCBCR709 :
						DS_COLOR_SPACE_YCBCR601;
			}
		}
		break;
	}
	default:
		switch (timing->pixel_encoding) {
		case PIXEL_ENCODING_YCBCR422:
		case PIXEL_ENCODING_YCBCR444:
			if (timing->pix_clk_khz > PIXEL_CLOCK)
				color_space = DS_COLOR_SPACE_YCBCR709;
			else
				color_space = DS_COLOR_SPACE_YCBCR601;
			break;
		default:
			break;
		}
		break;
	}
	return color_space;
}
enum ds_color_space dal_grph_colors_group_adjust_color_space(
		struct grph_colors_group *grph_colors_adj,
		enum ds_color_space color_space,
		bool rgb_limited)
{
	if (rgb_limited && color_space == DS_COLOR_SPACE_SRGB_FULLRANGE)
		color_space = DS_COLOR_SPACE_SRGB_LIMITEDRANGE;
	return color_space;
}

bool dal_grph_colors_group_synch_color_temperature_with_gamut(
		struct grph_colors_group *grph_colors_adj,
		struct adj_container *adj_container)
{
	struct gamut_data gamut_data;
	struct ds_white_point_coordinates data;
	struct white_point_data white_data;
	int32_t temperature;
	bool extra_match;

	dal_memset(&gamut_data, 0, sizeof(gamut_data));
	dal_memset(&data, 0, sizeof(data));
	dal_memset(&white_data, 0, sizeof(white_data));

	dal_adj_container_get_gamut(
			adj_container,
			ADJ_ID_GAMUT_SOURCE_GRPH,
			&gamut_data);
	if (!dal_gamut_space_setup_white_point(
			&gamut_data,
			&data))
		return false;

	white_data.white_x = data.white_x;
	white_data.white_y = data.white_y;

	if (!dal_color_temperature_find_color_temperature(
			&white_data,
			&temperature,
			&extra_match))
		return false;

	if (!dal_adj_info_set_update_cur_value(
			&adj_container->adj_info_set,
			ADJ_ID_TEMPERATURE,
			temperature))
		return false;

	return true;
}

bool dal_grph_colors_group_synch_gamut_with_color_temperature(
	struct grph_colors_group *grph_colors_adj,
	struct adj_container *adj_container)
{
	struct gamut_data gamut_data;
	struct white_point_data white_data;
	struct adjustment_info *temperature =
				dal_adj_info_set_get_adj_info(
					&adj_container->adj_info_set,
					ADJ_ID_TEMPERATURE);

	dal_memset(&gamut_data, 0, sizeof(gamut_data));
	dal_memset(&white_data, 0, sizeof(white_data));

	if (!dal_color_temperature_find_white_point(
			temperature->adj_data.ranged.cur,
			&white_data))
		return false;
	dal_adj_container_get_gamut(
			adj_container,
			ADJ_ID_GAMUT_SOURCE_GRPH,
			&gamut_data);
	dal_gamut_space_reset_gamut(
			&gamut_data,
			false,
			true);

	gamut_data.option.bits.CUSTOM_WHITE_POINT = 1;
	gamut_data.white_point.custom.white_x = white_data.white_x;
	gamut_data.white_point.custom.white_y = white_data.white_y;

	if (!dal_adj_container_validate_gamut(
			adj_container,
			&gamut_data))
		return false;

	dal_adj_container_update_gamut(
			adj_container,
			ADJ_ID_GAMUT_SOURCE_GRPH,
			&gamut_data);
	return true;

}

bool dal_grph_colors_group_get_color_temperature(
	struct grph_colors_group *grph_colors_adj,
	struct adj_container *adj_container,
	int32_t *temp)
{
	struct gamut_data gamut_data;
	struct ds_white_point_coordinates data;
	struct white_point_data white_data;
	bool exact_match;

	dal_adj_container_get_gamut(
			adj_container,
			ADJ_ID_GAMUT_SOURCE_GRPH,
			&gamut_data);
	if (dal_gamut_space_setup_white_point(
			&gamut_data,
			&data)) {
		white_data.white_x = data.white_x;
		white_data.white_y = data.white_y;
		return dal_color_temperature_find_color_temperature(
				&white_data, temp, &exact_match);
	}
	return false;
}

enum ds_return dal_grph_colors_group_set_color_graphics_gamut(
	struct grph_colors_group *grph_colors_adj,
	struct display_path *disp_path,
	struct gamut_data *gamut_data,
	enum adjustment_id adj_id,
	bool apply_to_hw)
{
	uint32_t display_index;
	struct adj_container *adj_container = NULL;
	const struct path_mode_set_with_data *pms_wd = NULL;
	const struct path_mode *path_mode = NULL;
	struct hw_adjustment_color_control *color_control = NULL;
	struct gamut_parameter *gamut = NULL;
	struct ds_regamma_lut *regamma = NULL;
	struct ds_regamma_lut *old_regamma = NULL;

	if (!disp_path)
		return DS_ERROR;
	display_index = dal_display_path_get_display_index(
			disp_path);

	adj_container = dal_ds_dispatch_get_adj_container_for_path(
			grph_colors_adj->ds, display_index);
	if (!adj_container)
		return DS_ERROR;

	pms_wd = dal_ds_dispatch_get_active_pms_with_data(
			grph_colors_adj->ds);
	if (!pms_wd)
		return DS_ERROR;

	path_mode = dal_pms_with_data_get_path_mode_for_display_index(
			pms_wd, display_index);
	if (!path_mode)
		return DS_ERROR;

	if (!dal_hw_sequencer_is_support_custom_gamut_adj(
			grph_colors_adj->hws,
			disp_path,
			HW_GRAPHIC_SURFACE))
		return DS_ERROR;

	if (!dal_adj_container_validate_gamut(
			adj_container,
			gamut_data))
		return DS_ERROR;

	dal_adj_container_update_gamut(
			adj_container,
			adj_id, gamut_data);

	if (apply_to_hw) {
		enum hwss_result result;
		enum ds_color_space color_space;

		color_control = dal_alloc(sizeof(*color_control));
		if (!color_control)
			return DS_ERROR;
		gamut = dal_alloc(sizeof(*gamut));
		if (!gamut)
			goto gamut_fail;
		regamma = dal_alloc(sizeof(*regamma));
		if (!regamma)
			goto regamma_fail;
		old_regamma = dal_alloc(sizeof(*old_regamma));
		if (!old_regamma)
			goto old_regamma_fail;

		if (!dal_grph_colors_group_compute_hw_adj_color_control(
				grph_colors_adj,
				adj_container,
				&path_mode->mode_timing->crtc_timing,
				disp_path,
				adj_id,
				gamut,
				regamma,
				color_control))
			goto adj_color_control_fail;

		color_control->surface_pixel_format =
				path_mode->pixel_format;

		result = dal_hw_sequencer_set_color_control_adjustment(
				grph_colors_adj->hws,
				dal_display_path_get_controller(disp_path),
				color_control);

		if (result == HWSS_RESULT_OK) {
			if (grph_colors_adj->regamma_updated) {
				enum ds_return ret;

				if (!dal_adj_container_get_regamma_copy(
						adj_container,
						old_regamma))
					goto adj_color_control_fail;

				dal_adj_container_set_regamma(
						adj_container,
						regamma);
				ret = dal_ds_dispatch_set_gamma_adjustment(
					grph_colors_adj->ds,
					display_index,
					ADJ_ID_GAMMA_RAMP,
					dal_ds_dispatch_get_current_gamma(
						grph_colors_adj->ds,
						display_index,
						ADJ_ID_GAMMA_RAMP));
				if (ret != DS_SUCCESS)
					dal_adj_container_set_regamma(
							adj_container,
							regamma);
			}
		}
		color_space =
			dal_ds_translation_color_space_from_hw_color_space(
				color_control->color_space);
		dal_ds_dispatch_update_adj_container_for_path_with_color_space(
				grph_colors_adj->ds,
				display_index,
				color_space);
	}
	dal_grph_colors_group_synch_color_temperature_with_gamut(
			grph_colors_adj,
			adj_container);
	return DS_SUCCESS;

adj_color_control_fail:
	dal_free(old_regamma);
old_regamma_fail:
	dal_free(regamma);
regamma_fail:
	dal_free(gamut);
gamut_fail:
	dal_free(color_control);

	return DS_ERROR;
}

enum ds_return dal_grph_colors_group_get_color_gamut(
	struct grph_colors_group *grph_colors_adj,
	struct display_path *disp_path,
	const struct ds_gamut_reference_data *ref,
	struct ds_get_gamut_data *data)
{
	enum ds_return ret;
	uint32_t display_index;
	struct adj_container *adj_container = NULL;
	struct adjustment_info *temperature_src = NULL;
	const struct display_characteristics *disp_char = NULL;
	struct color_characteristic color_charact = {{0 } };
	struct gamut_data gamut_data;
	enum adjustment_id adj_id;
	struct fixed31_32 result;

	if (!disp_path)
		return DS_ERROR;
	display_index = dal_display_path_get_display_index(
				disp_path);

	adj_container = dal_ds_dispatch_get_adj_container_for_path(
			grph_colors_adj->ds, display_index);
	if (!adj_container)
		return DS_ERROR;

	if (!dal_hw_sequencer_is_support_custom_gamut_adj(
			grph_colors_adj->hws,
			disp_path,
			HW_GRAPHIC_SURFACE))
		return DS_ERROR;

	dal_memset(&gamut_data, 0, sizeof(gamut_data));
	if (!dal_ds_translate_gamut_reference(
			ref, &adj_id))
		return DS_ERROR;

	if (adj_id == ADJ_ID_GAMUT_DESTINATION) {
		temperature_src = dal_adj_info_set_get_adj_info(
				&adj_container->adj_info_set,
				ADJ_ID_TEMPERATURE_SOURCE);
		if (temperature_src != NULL &&
			temperature_src->adj_data.ranged.cur ==
					COLOR_TEMPERATURE_SOURCE_EDID) {

			disp_char = dal_adj_container_get_disp_character(
							adj_container);
			if (!disp_char)
				return DS_ERROR;
			if (!dal_gamut_space_convert_edid_format_color_charact(
					disp_char->color_characteristics,
					&color_charact))
				return DS_ERROR;
			gamut_data.option.bits.CUSTOM_GAMUT_SPACE = 1;
			gamut_data.option.bits.CUSTOM_WHITE_POINT = 1;

			result = dal_fixed31_32_mul(
					dal_fixed31_32_from_int(
							(int64_t)GAMUT_DIVIDER),
					color_charact.red_x);
			gamut_data.gamut.custom.red_x =
					dal_fixed31_32_floor(result);

			result = dal_fixed31_32_mul(
					dal_fixed31_32_from_int(
							(int64_t)GAMUT_DIVIDER),
					color_charact.red_y);
			gamut_data.gamut.custom.red_y =
					dal_fixed31_32_floor(result);

			result = dal_fixed31_32_mul(
					dal_fixed31_32_from_int(
							(int64_t)GAMUT_DIVIDER),
					color_charact.blue_x);
			gamut_data.gamut.custom.blue_x =
					dal_fixed31_32_floor(result);

			result = dal_fixed31_32_mul(
					dal_fixed31_32_from_int(
							(int64_t)GAMUT_DIVIDER),
					color_charact.blue_y);
			gamut_data.gamut.custom.blue_y =
					dal_fixed31_32_floor(result);

			result = dal_fixed31_32_mul(
					dal_fixed31_32_from_int(
							(int64_t)GAMUT_DIVIDER),
					color_charact.green_x);
			gamut_data.gamut.custom.green_x =
					dal_fixed31_32_floor(result);

			result = dal_fixed31_32_mul(
					dal_fixed31_32_from_int(
							(int64_t)GAMUT_DIVIDER),
					color_charact.green_y);
			gamut_data.gamut.custom.green_y =
					dal_fixed31_32_floor(result);

			result = dal_fixed31_32_mul(
					dal_fixed31_32_from_int(
							(int64_t)GAMUT_DIVIDER),
					color_charact.white_x);
			gamut_data.white_point.custom.white_x =
					dal_fixed31_32_floor(result);

			result = dal_fixed31_32_mul(
					dal_fixed31_32_from_int(
							(int64_t)GAMUT_DIVIDER),
					color_charact.white_y);
			gamut_data.white_point.custom.white_y =
					dal_fixed31_32_floor(result);

			ret = DS_SUCCESS;
		}
	}
	if (ret != DS_SUCCESS)
		if (!dal_adj_container_get_gamut(
				adj_container,
				adj_id,
				&gamut_data))
			return DS_ERROR;
	if (!dal_ds_translate_internal_gamut_to_external_parameter(
			&gamut_data,
			&data->gamut))
		return DS_ERROR;
	return ret;
}

enum ds_return dal_grph_colors_group_get_color_gamut_info(
	struct grph_colors_group *grph_colors_adj,
	struct display_path *disp_path,
	const struct ds_gamut_reference_data *ref,
	struct ds_gamut_info *data)
{
	if (!disp_path)
		return DS_ERROR;

	if (!dal_hw_sequencer_is_support_custom_gamut_adj(
			grph_colors_adj->hws,
			disp_path,
			HW_GRAPHIC_SURFACE))
		return DS_ERROR;

	if (!dal_gamut_space_get_supported_gamuts(data))
		return DS_ERROR;

	return DS_SUCCESS;
}

enum ds_return dal_grph_colors_group_get_regamma_lut(
	struct grph_colors_group *grph_colors_adj,
	struct display_path *disp_path,
	struct ds_regamma_lut *data)
{
	uint32_t display_index;
	struct adj_container *adj_container = NULL;
	const struct ds_regamma_lut *regamma = NULL;

	if (!disp_path)
		return DS_ERROR;
	display_index = dal_display_path_get_display_index(
					disp_path);
	adj_container = dal_ds_dispatch_get_adj_container_for_path(
			grph_colors_adj->ds, display_index);
	if (!adj_container)
		return DS_ERROR;

	if (!dal_hw_sequencer_is_support_custom_gamma_coefficients(
			grph_colors_adj->hws,
			disp_path,
			HW_GRAPHIC_SURFACE))
		return DS_ERROR;

	regamma = dal_adj_container_get_regamma(adj_container);
	if (!regamma)
		return DS_ERROR;

	dal_ds_translate_regamma_to_external(regamma, data);
	return DS_SUCCESS;
}

enum ds_return dal_grph_colors_group_set_regamma_lut(
	struct grph_colors_group *grph_colors_adj,
	struct display_path *disp_path,
	const struct ds_regamma_lut *data)
{
	uint32_t display_index;
	enum ds_return ret;
	struct adj_container *adj_container = NULL;
	struct ds_regamma_lut *regamma = NULL;
	struct ds_regamma_lut *old_regamma = NULL;
	const struct raw_gamma_ramp *ramp = NULL;
	struct gamut_data original_gamut = {{0 } };
	struct gamut_data updated_gamut = {{0 } };
	bool updated_gamut_dst = false;

	if (!disp_path)
		return DS_ERROR;
	display_index = dal_display_path_get_display_index(
					disp_path);
	adj_container = dal_ds_dispatch_get_adj_container_for_path(
			grph_colors_adj->ds, display_index);
	if (!adj_container)
		return DS_ERROR;

	if (data->flags.bits.GAMMA_FROM_EDID_EX == 1 ||
		data->flags.bits.COEFF_FROM_EDID == 1)
		return DS_ERROR;

	if (!dal_hw_sequencer_is_support_custom_gamma_coefficients(
			grph_colors_adj->hws,
			disp_path,
			HW_GRAPHIC_SURFACE))
		return DS_ERROR;

	regamma = dal_alloc(sizeof(*regamma));
	if (!regamma)
		goto regamma_fail;

	old_regamma = dal_alloc(sizeof(*old_regamma));
	if (!old_regamma)
		goto old_regamma_fail;

	ramp = dal_ds_dispatch_get_current_gamma(
			grph_colors_adj->ds,
			display_index,
			ADJ_ID_GAMMA_RAMP);
	if (!dal_adj_container_get_regamma_copy(
			adj_container,
			old_regamma))
		goto regamma_copy_fail;

	dal_memmove(regamma, old_regamma, sizeof(*regamma));

	if (!dal_ds_translate_regamma_to_internal(
			data, regamma))
		goto regamma_copy_fail;

	if (!dal_adj_container_get_gamut(
			adj_container,
			ADJ_ID_GAMUT_DESTINATION,
			&original_gamut))
		goto regamma_copy_fail;

	if (!dal_adj_container_set_regamma(
			adj_container,
			regamma))
		goto regamma_copy_fail;

	updated_gamut_dst = dal_grph_colors_group_update_gamut(
			grph_colors_adj,
			disp_path,
			adj_container);

	ret = dal_ds_dispatch_set_gamma_adjustment(
			grph_colors_adj->ds,
			display_index,
			ADJ_ID_GAMMA_RAMP_REGAMMA_UPDATE,
			ramp);

	if (ret != DS_SUCCESS) {
		if (updated_gamut_dst)
			dal_adj_container_update_gamut(
					adj_container,
					ADJ_ID_GAMUT_DESTINATION,
					&original_gamut);
		if (!dal_adj_container_set_regamma(
				adj_container,
				old_regamma))
			goto regamma_copy_fail;
	} else {
		if (!dal_adj_container_get_gamut(
			adj_container,
			ADJ_ID_GAMUT_DESTINATION,
			&updated_gamut))
			goto regamma_copy_fail;

		return ret;
	}

regamma_copy_fail:
	dal_free(old_regamma);
old_regamma_fail:
	dal_free(regamma);
regamma_fail:
	return DS_ERROR;
}

enum ds_return dal_grph_colors_group_update_gamut(
	struct grph_colors_group *grph_colors_adj,
	struct display_path *disp_path,
	struct adj_container *adj_container)
{
	struct gamut_parameter *gamut = NULL;
	enum signal_type signal;
	const struct adjustment_info *temperature = NULL;
	const struct adjustment_info *temperature_src = NULL;
	const struct ds_regamma_lut *const_regamma = NULL;

	if (!disp_path)
		return false;

	signal = dal_display_path_get_query_signal(
			disp_path, SINK_LINK_INDEX);
	if (signal == SIGNAL_TYPE_HDMI_TYPE_A)
		return false;

	gamut = dal_alloc(sizeof(*gamut));
	if (!gamut)
		return false;

	temperature = dal_adj_info_set_get_adj_info(
			&adj_container->adj_info_set,
			ADJ_ID_TEMPERATURE);
	temperature_src = dal_adj_info_set_get_adj_info(
			&adj_container->adj_info_set,
			ADJ_ID_TEMPERATURE_SOURCE);
	if (temperature == NULL ||
		temperature->adj_data.ranged.cur == -1)
		goto gamut_fail;

	gamut->source = ((temperature_src != NULL) &&
			(temperature_src->adj_data.ranged.cur ==
				COLOR_TEMPERATURE_SOURCE_EDID)) ?
				GAMUT_SPACE_SOURCE_EDID :
				GAMUT_SPACE_SOURCE_USER_GAMUT;
	if (gamut->source != GAMUT_SPACE_SOURCE_USER_GAMUT)
		goto gamut_fail;

	if (!dal_adj_container_get_gamut(
			adj_container,
			ADJ_ID_GAMUT_SOURCE_GRPH,
			&gamut->gamut_src))
		goto gamut_fail;

	if (!dal_adj_container_get_gamut(
			adj_container,
			ADJ_ID_GAMUT_DESTINATION,
			&gamut->gamut_dst))
		goto gamut_fail;

	const_regamma = dal_adj_container_get_regamma(
			adj_container);
	if (!const_regamma)
		goto gamut_fail;
	dal_memmove(&gamut->regamma, const_regamma, sizeof(gamut->regamma));
	{
		union update_color_flags flags = {0};

		if (!dal_gamut_space_update_gamut(
				gamut, false, &flags))
			goto gamut_fail;
		if (flags.bits.GAMUT_DST == 1)
			return dal_adj_container_update_gamut(
					adj_container,
					ADJ_ID_GAMUT_DESTINATION,
					&gamut->gamut_dst);
	}

gamut_fail:
	dal_free(gamut);
	return false;
}

static bool grph_colors_group_construct(
		struct grph_colors_group *grph_colors_adj,
		struct grph_colors_group_init_data *init_data)
{
	if (!init_data)
		return false;

	grph_colors_adj->ds = init_data->ds;
	grph_colors_adj->hws = init_data->hws;
	grph_colors_adj->dal_context = init_data->dal_context;

	return true;
}

struct grph_colors_group *dal_grph_colors_group_create(
		struct grph_colors_group_init_data *init_data)
{
	struct grph_colors_group *grph_colors_adj = NULL;

	grph_colors_adj = dal_alloc(sizeof(*grph_colors_adj));

	if (!grph_colors_adj)
		return NULL;

	if (grph_colors_group_construct(grph_colors_adj, init_data))
		return grph_colors_adj;

	dal_free(grph_colors_adj);

	return NULL;
}

static void destruct(
	struct grph_colors_group *grph_colors_adj)
{

}

void dal_grph_colors_adj_group_destroy(
	struct grph_colors_group **grph_colors_adj)
{
	if (grph_colors_adj == NULL || *grph_colors_adj == NULL)
		return;
	destruct(*grph_colors_adj);
	dal_free(*grph_colors_adj);
	*grph_colors_adj = NULL;
}
