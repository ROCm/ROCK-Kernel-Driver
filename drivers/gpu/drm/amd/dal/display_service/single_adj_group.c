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
#include "include/timing_service_types.h"
#include "include/dcs_interface.h"
#include "include/signal_types.h"
#include "include/topology_mgr_interface.h"
#include "include/adjustment_interface.h"
#include "include/display_service_types.h"
#include "include/hw_adjustment_types.h"
#include "include/hw_sequencer_interface.h"
#include "include/set_mode_interface.h"
#include "include/logger_interface.h"
#include "include/hw_adjustment_set.h"

#include "ds_dispatch.h"
#include "adjustment_container.h"
#include "single_adj_group.h"

static void translate_to_hw_dither(
	uint32_t value,
	enum pixel_encoding pixel_encoding,
	union hw_adjustment_bit_depth_reduction *bit_depth)
{
	/* truncation */
	if (DS_BIT_DEPTH_REDUCTION_TRUN6 == value) {
		bit_depth->bits.TRUNCATE_ENABLED = 1;
		bit_depth->bits.TRUNCATE_DEPTH = 0;
	} else if (DS_BIT_DEPTH_REDUCTION_TRUN8 == value ||
			DS_BIT_DEPTH_REDUCTION_TRUN8_DITH6 == value ||
			DS_BIT_DEPTH_REDUCTION_TRUN8_FM6 == value) {
		bit_depth->bits.TRUNCATE_ENABLED = 1;
		bit_depth->bits.TRUNCATE_DEPTH = 1;
	} else if (DS_BIT_DEPTH_REDUCTION_TRUN10 == value ||
			DS_BIT_DEPTH_REDUCTION_TRUN10_DITH6 == value ||
			DS_BIT_DEPTH_REDUCTION_TRUN10_DITH8 == value ||
			DS_BIT_DEPTH_REDUCTION_TRUN10_FM8 == value ||
			DS_BIT_DEPTH_REDUCTION_TRUN10_FM6 == value ||
			DS_BIT_DEPTH_REDUCTION_TRUN10_DITH8_FM6 == value) {
		bit_depth->bits.TRUNCATE_ENABLED = 1;
		bit_depth->bits.TRUNCATE_DEPTH = 2;
	}

	if (DS_BIT_DEPTH_REDUCTION_DITH6 == value ||
		DS_BIT_DEPTH_REDUCTION_DITH6_NO_FRAME_RAND == value ||
		DS_BIT_DEPTH_REDUCTION_FM6 == value) {
		bit_depth->bits.TRUNCATE_ENABLED = 1;
		bit_depth->bits.TRUNCATE_DEPTH = 2;
		bit_depth->bits.TRUNCATE_MODE = 1;
	}

	/* spatial dither */
	if (DS_BIT_DEPTH_REDUCTION_DITH6 == value ||
		DS_BIT_DEPTH_REDUCTION_DITH6_NO_FRAME_RAND == value ||
		DS_BIT_DEPTH_REDUCTION_TRUN10_DITH6 == value ||
		DS_BIT_DEPTH_REDUCTION_TRUN8_DITH6 == value) {
		bit_depth->bits.SPATIAL_DITHER_ENABLED = 1;
		bit_depth->bits.SPATIAL_DITHER_DEPTH = 0;
		bit_depth->bits.HIGHPASS_RANDOM = 1;
		bit_depth->bits.RGB_RANDOM =
				(pixel_encoding == PIXEL_ENCODING_RGB) ? 1 : 0;
	} else if (DS_BIT_DEPTH_REDUCTION_DITH8 == value ||
			DS_BIT_DEPTH_REDUCTION_DITH8_NO_FRAME_RAND == value ||
			DS_BIT_DEPTH_REDUCTION_TRUN8_FM6 == value ||
			DS_BIT_DEPTH_REDUCTION_TRUN10_DITH8 == value ||
			DS_BIT_DEPTH_REDUCTION_TRUN10_DITH8_FM6 == value) {
		bit_depth->bits.SPATIAL_DITHER_ENABLED = 1;
		bit_depth->bits.SPATIAL_DITHER_DEPTH = 1;
		bit_depth->bits.HIGHPASS_RANDOM = 1;
		bit_depth->bits.RGB_RANDOM =
				(pixel_encoding == PIXEL_ENCODING_RGB) ? 1 : 0;
	} else if (DS_BIT_DEPTH_REDUCTION_DITH10 == value ||
			DS_BIT_DEPTH_REDUCTION_DITH10_NO_FRAME_RAND == value ||
			DS_BIT_DEPTH_REDUCTION_TRUN10_FM8 == value ||
			DS_BIT_DEPTH_REDUCTION_TRUN10_FM6 == value) {
		bit_depth->bits.SPATIAL_DITHER_ENABLED = 1;
		bit_depth->bits.SPATIAL_DITHER_DEPTH = 2;
		bit_depth->bits.HIGHPASS_RANDOM = 1;
		bit_depth->bits.RGB_RANDOM =
				(pixel_encoding == PIXEL_ENCODING_RGB) ? 1 : 0;
	}

	if (DS_BIT_DEPTH_REDUCTION_DITH6_NO_FRAME_RAND == value ||
		DS_BIT_DEPTH_REDUCTION_DITH8_NO_FRAME_RAND == value ||
		DS_BIT_DEPTH_REDUCTION_DITH10_NO_FRAME_RAND == value) {
		bit_depth->bits.FRAME_RANDOM = 0;
	} else
		bit_depth->bits.FRAME_RANDOM = 1;

	/* temporal dither */
	if (DS_BIT_DEPTH_REDUCTION_FM6 == value ||
		DS_BIT_DEPTH_REDUCTION_DITH8_FM6 == value ||
		DS_BIT_DEPTH_REDUCTION_DITH10_FM6 == value ||
		DS_BIT_DEPTH_REDUCTION_TRUN10_FM6 == value ||
		DS_BIT_DEPTH_REDUCTION_TRUN8_FM6 == value ||
		DS_BIT_DEPTH_REDUCTION_TRUN10_DITH8_FM6 == value) {
		bit_depth->bits.FRAME_MODULATION_ENABLED = 1;
		bit_depth->bits.FRAME_MODULATION_DEPTH = 0;
	} else if (DS_BIT_DEPTH_REDUCTION_FM8 == value ||
			DS_BIT_DEPTH_REDUCTION_DITH10_FM8 == value ||
			DS_BIT_DEPTH_REDUCTION_TRUN10_FM8 == value) {
		bit_depth->bits.FRAME_MODULATION_ENABLED = 1;
		bit_depth->bits.FRAME_MODULATION_DEPTH = 1;
	} else if (DS_BIT_DEPTH_REDUCTION_FM10 == value) {
		bit_depth->bits.FRAME_MODULATION_ENABLED = 1;
		bit_depth->bits.FRAME_MODULATION_DEPTH = 2;
	}

}
enum ds_return dal_single_adj_group_set_adjustment(
	struct single_adj_group *single_adj,
	struct display_path *disp_path,
	enum adjustment_id adj_id,
	uint32_t value)
{
	enum ds_return result = DS_ERROR;
	enum hwss_result ret = HWSS_RESULT_ERROR;
	union hw_adjustment_bit_depth_reduction adj_bit_depth = {0};
	uint32_t display_index = dal_tm_display_path_to_display_index(
			single_adj->tm,
			disp_path);
	struct adj_container *adj_container =
			dal_ds_dispatch_get_adj_container_for_path(
				single_adj->ds,
				display_index);
	const struct adjustment_info *adj_info =
			dal_adj_info_set_get_adj_info(
				&adj_container->adj_info_set, adj_id);

	if (!adj_container)
		return result;

	if (!adj_info)
		return result;

	if (adj_info->adj_data_type == ADJ_RANGED) {
		if (value < adj_info->adj_data.ranged.min ||
				value > adj_info->adj_data.ranged.max)
			return result;
	}
	if (adj_id == ADJ_ID_BIT_DEPTH_REDUCTION) {
		if (dal_display_path_is_psr_supported(disp_path) ||
			!dal_single_adj_group_verify_bit_depth_reduction(
				single_adj,
				disp_path,
				value)) {
			dal_logger_write(single_adj->dal_context->logger,
				LOG_MAJOR_DCP,
				LOG_MINOR_COMPONENT_DISPLAY_SERVICE,
				"Dithering setting %d could not be applied\n",
				value);
			return result;
		}
	}
	if (!dal_adj_info_set_update_cur_value(
			&adj_container->adj_info_set, adj_id, value))
		return result;

	if (!dal_single_adj_group_setup_bit_depth_parameters(
			single_adj,
			disp_path,
			value,
			&adj_bit_depth))
		return result;

	ret = dal_hw_sequencer_set_bit_depth_reduction_adj(
			single_adj->hws,
			disp_path,
			&adj_bit_depth);

	if (ret == HWSS_RESULT_OK)
		result = DS_SUCCESS;

	if (result == DS_SUCCESS)
		dal_adj_container_commit_adj(adj_container, adj_id);

	return result;
}

bool dal_single_adj_group_verify_bit_depth_reduction(
	struct single_adj_group *single_adj,
	struct display_path *disp_path,
	uint32_t value)
{
	enum color_depth_index color_depth;
	uint32_t display_index = dal_tm_display_path_to_display_index(
			single_adj->tm,
			disp_path);
	const struct path_mode *path_mode =
		dal_pms_with_data_get_path_mode_for_display_index(
			single_adj->ds->set, display_index);

	if (value < 0 || value > DS_BIT_DEPTH_REDUCTION_MAX)
		return false;

	if (value == DS_BIT_DEPTH_REDUCTION_DISABLE ||
			value == DS_BIT_DEPTH_REDUCTION_DRIVER_DEFAULT)
		return true;

	if (path_mode == NULL || path_mode->mode_timing == NULL)
		return false;

	color_depth =
			path_mode->mode_timing->crtc_timing.display_color_depth;
	if (color_depth == COLOR_DEPTH_INDEX_888) {
		if (value == DS_BIT_DEPTH_REDUCTION_DITH8 ||
			value == DS_BIT_DEPTH_REDUCTION_DITH8_NO_FRAME_RAND ||
			value == DS_BIT_DEPTH_REDUCTION_FM8 ||
			value == DS_BIT_DEPTH_REDUCTION_TRUN8 ||
			value == DS_BIT_DEPTH_REDUCTION_DITH10_FM8 ||
			value == DS_BIT_DEPTH_REDUCTION_TRUN10_DITH8 ||
			value == DS_BIT_DEPTH_REDUCTION_TRUN10_FM8)
			return true;
	} else if (color_depth == COLOR_DEPTH_INDEX_666) {
		if (value == DS_BIT_DEPTH_REDUCTION_DITH6 ||
			value == DS_BIT_DEPTH_REDUCTION_DITH6_NO_FRAME_RAND ||
			value == DS_BIT_DEPTH_REDUCTION_FM6 ||
			value == DS_BIT_DEPTH_REDUCTION_TRUN6 ||
			value == DS_BIT_DEPTH_REDUCTION_DITH10_FM6 ||
			value == DS_BIT_DEPTH_REDUCTION_TRUN10_DITH6 ||
			value == DS_BIT_DEPTH_REDUCTION_TRUN10_FM6 ||
			value == DS_BIT_DEPTH_REDUCTION_DITH8_FM6 ||
			value == DS_BIT_DEPTH_REDUCTION_TRUN8_DITH6 ||
			value == DS_BIT_DEPTH_REDUCTION_TRUN8_FM6 ||
			value == DS_BIT_DEPTH_REDUCTION_TRUN10_DITH8_FM6)
			return true;
	} else if (color_depth == COLOR_DEPTH_INDEX_101010) {
		if (value == DS_BIT_DEPTH_REDUCTION_DITH10 ||
			value == DS_BIT_DEPTH_REDUCTION_DITH10_NO_FRAME_RAND ||
			value == DS_BIT_DEPTH_REDUCTION_FM10 ||
			value == DS_BIT_DEPTH_REDUCTION_TRUN10)
			return true;
	}
	return false;
}

bool dal_single_adj_group_setup_bit_depth_parameters(
	struct single_adj_group *single_adj,
	struct display_path *disp_path,
	uint32_t value,
	union hw_adjustment_bit_depth_reduction *bit_depth)
{
	bool ret = true;
	enum display_color_depth color_depth;
	enum pixel_encoding pixel_encoding;
	uint32_t display_index = dal_tm_display_path_to_display_index(
			single_adj->tm,
			disp_path);
	const struct path_mode *path_mode =
		dal_pms_with_data_get_path_mode_for_display_index(
			single_adj->ds->set, display_index);

	if (path_mode == NULL || path_mode->mode_timing == NULL)
		return false;

	color_depth =
		path_mode->mode_timing->crtc_timing.display_color_depth;
	pixel_encoding =
		path_mode->mode_timing->crtc_timing.pixel_encoding;

	/* Disable diethering if FEATURE_PIXEL_PERFECT_OUTPUT
	 * is set and the display color depth matches the
	 * surface pixel format (only applies to 8-bit) */
	if (dal_adapter_service_is_feature_supported(
			FEATURE_PIXEL_PERFECT_OUTPUT) &&
			color_depth == DISPLAY_COLOR_DEPTH_888 &&
			path_mode->pixel_format == PIXEL_FORMAT_ARGB8888)
		return true;

	if (value == DS_BIT_DEPTH_REDUCTION_DRIVER_DEFAULT) {
		if (color_depth == DISPLAY_COLOR_DEPTH_666)
			bit_depth->bits.SPATIAL_DITHER_DEPTH = 0;
		else if (color_depth == DISPLAY_COLOR_DEPTH_888)
			bit_depth->bits.SPATIAL_DITHER_DEPTH = 1;
		else if (color_depth == DISPLAY_COLOR_DEPTH_101010 ||
				 color_depth == DISPLAY_COLOR_DEPTH_121212)
			return true;
		else
			return false;
		bit_depth->bits.SPATIAL_DITHER_ENABLED = 1;
		bit_depth->bits.FRAME_RANDOM = 1;
		bit_depth->bits.RGB_RANDOM =
				(pixel_encoding == PIXEL_ENCODING_RGB) ? 1 : 0;
		return true;
	}
	translate_to_hw_dither(value, pixel_encoding, bit_depth);

	if (bit_depth->bits.FRAME_MODULATION_ENABLED == 1) {
		switch (dal_display_path_get_config_signal(
				disp_path, SINK_LINK_INDEX)) {
		{
		case SIGNAL_TYPE_RGB:
		case SIGNAL_TYPE_DVI_SINGLE_LINK:
		case SIGNAL_TYPE_DVI_SINGLE_LINK1:
		case SIGNAL_TYPE_DVI_DUAL_LINK:
		case SIGNAL_TYPE_HDMI_TYPE_A:
		case SIGNAL_TYPE_DISPLAY_PORT:
		case SIGNAL_TYPE_DISPLAY_PORT_MST:
			bit_depth->bits.TEMPORAL_LEVEL = 0;
		}
			break;
		case SIGNAL_TYPE_LVDS:
		case SIGNAL_TYPE_EDP:
		{
			union panel_misc_info panel_info;
			struct dcs *dcs = dal_display_path_get_dcs(disp_path);

			bit_depth->bits.TEMPORAL_LEVEL = 0;
			if (dal_dcs_get_panel_misc_info(dcs, &panel_info)) {
				if (panel_info.bits.GREY_LEVEL)
					bit_depth->bits.TEMPORAL_LEVEL = 1;
			}
		}
			break;
		default:
			ret = false;
			break;
		}
		bit_depth->bits.FRC_25 = 0;
		bit_depth->bits.FRC_50 = 0;
		bit_depth->bits.FRC_75 = 0;
	}
	return ret;
}

bool dal_single_adj_group_include_adjustment(
	struct single_adj_group *single_adj,
	struct display_path *disp_path,
	struct ds_adj_id_value adj,
	struct hw_adjustment_set *set)
{
	union hw_adjustment_bit_depth_reduction *bit_depth = NULL;
	uint32_t display_index = dal_tm_display_path_to_display_index(
					single_adj->tm, disp_path);
	struct adj_container *adj_container =
			dal_ds_dispatch_get_adj_container_for_path(
				single_adj->ds, display_index);
	struct adjustment_info *adj_info =
			dal_adj_info_set_get_adj_info(
				&adj_container->adj_info_set, adj.adj_id);

	bit_depth = dal_alloc(sizeof(*bit_depth));
	if (!bit_depth)
		return false;

	if (adj.adj_id == ADJ_ID_BIT_DEPTH_REDUCTION) {
		if (dal_display_path_is_psr_supported(disp_path))
			return false;
		if (!dal_single_adj_group_verify_bit_depth_reduction(
				single_adj,
				disp_path,
				adj.value)) {
			adj.value = adj_info->adj_data.ranged.def;
			dal_adj_info_set_update_cur_value(
				&adj_container->adj_info_set,
				adj.adj_id, adj.value);
			dal_adj_container_commit_adj(
					adj_container, adj.adj_id);
			dal_logger_write(single_adj->dal_context->logger,
				LOG_MAJOR_DCP,
				LOG_MINOR_COMPONENT_DISPLAY_SERVICE,
				"Dithering setting %d no longer matching color depth ,resetting to default\n",
				adj.value);
		}
		dal_single_adj_group_setup_bit_depth_parameters(
				single_adj,
				disp_path,
				adj.value,
				bit_depth);

		set->bit_depth = bit_depth;

	} else
		return false;

	return true;
}

static bool single_adj_construct(
	struct single_adj_group *single_adj,
	struct single_adj_group_init_data *init_data)
{
	if (!init_data)
		return false;

	single_adj->ds = init_data->ds;
	single_adj->hws = init_data->hws;
	single_adj->tm = init_data->tm;
	single_adj->dal_context = init_data->dal_context;
	return true;
}

struct single_adj_group *dal_single_adj_group_create(
	struct single_adj_group_init_data *init_data)
{
	struct single_adj_group *single_adj = NULL;

	single_adj = dal_alloc(sizeof(*single_adj));

	if (!single_adj)
		return NULL;

	if (single_adj_construct(single_adj, init_data))
		return single_adj;

	dal_free(single_adj);

	return NULL;
}

static void destruct(
	struct single_adj_group *single_adj)
{
}

void dal_single_adj_group_destroy(
	struct single_adj_group **single_adj)
{
	if (single_adj == NULL || *single_adj == NULL)
		return;
	destruct(*single_adj);
	dal_free(*single_adj);
	*single_adj = NULL;
}
