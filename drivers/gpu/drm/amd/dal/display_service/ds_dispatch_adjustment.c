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

#include "include/adjustment_interface.h"
#include "include/display_path_interface.h"
#include "include/signal_types.h"
#include "include/dcs_interface.h"
#include "include/topology_mgr_interface.h"
#include "include/display_service_interface.h"
#include "include/set_mode_interface.h"
#include "include/logger_interface.h"
#include "include/fixed31_32.h"

#include "ds_dispatch.h"
#include "adjustment_container.h"
#include "scaler_adj_group.h"
#include "adjustment_api.h"
#include "backlight_adj_group.h"
#include "single_adj_group.h"
#include "grph_colors_group.h"
#include "gamut_space.h"
#include "gamma_lut.h"
#include "path_mode_set_with_data.h"

#define NOT_IMPLEMENTED() DAL_LOGGER_NOT_IMPL( \
	LOG_MINOR_COMPONENT_DISPLAY_SERVICE, "Display Service:%s\n", __func__)

/* NOTE make sure to update CURRENT_ADJUSTMENT_NUM when updating this array */
static const struct adj_global_info adj_global_info_array[CURRENT_ADJUSTMENT_NUM] = {
		{ ADJ_ID_SATURATION,			ADJ_RANGED,	{ 0x140 },	{ 1, 1, 1, 1, 1, 1, 1, 0, 1 } },
		{ ADJ_ID_BIT_DEPTH_REDUCTION,		ADJ_RANGED,	{ 0x14A },	{ 1, 1, 1, 1, 1, 1, 1, 0, 0 } },
		{ ADJ_ID_UNDERSCAN,			ADJ_RANGED,	{ 0x145 },	{ 0, 0, 1, 1, 1, 1, 1, 0, 1 } },
		{ ADJ_ID_UNDERSCAN_TYPE,		ADJ_RANGED,	{ 0x101 },	{ 0, 0, 1, 0, 1, 0, 0, 0, 1 } },
		{ ADJ_ID_BACKLIGHT,			ADJ_RANGED,	{ 0x1a0 },	{ 1, 1, 1, 1, 1, 1, 1, 0, 0 } },
		{ ADJ_ID_CONTRAST,			ADJ_RANGED,	{ 0x160 },	{ 1, 1, 1, 1, 1, 1, 1, 0, 1 } },
		{ ADJ_ID_BRIGHTNESS,			ADJ_RANGED,	{ 0x140 },	{ 1, 1, 1, 1, 1, 1, 1, 0, 1 } },
		{ ADJ_ID_HUE,				ADJ_RANGED,	{ 0x140 },	{ 1, 1, 1, 1, 1, 1, 1, 0, 1 } },
		{ ADJ_ID_TEMPERATURE,			ADJ_RANGED,	{ 0x140 },	{ 1, 1, 1, 1, 1, 1, 1, 0, 1 } },
		{ ADJ_ID_TEMPERATURE_SOURCE,		ADJ_RANGED,	{ 0x142 },	{ 1, 1, 1, 1, 0, 1, 1, 0, 1 } },
		{ ADJ_ID_NOMINAL_RANGE_RGB_LIMITED,	ADJ_RANGED,	{ 0x141 },	{ 1, 1, 1, 1, 1, 1, 1, 0, 1 } },
		{ ADJ_ID_GAMMA_RAMP,			ADJ_LUT,	{ 0x108 },	{ 1, 1, 1, 1, 1, 1, 1, 1, 1 } }
};

static void build_adj_container_for_path(struct ds_dispatch *ds,
		struct display_path *display_path);

static enum ds_signal_type get_ds_signal_from_display_path(
	struct ds_dispatch *ds,
	struct display_path *display_path,
	uint32_t idx);

/*get info from global table adj_global_info_array*/
static enum ds_return get_adj_type(
	struct ds_dispatch *ds,
	enum adjustment_id adj_id,
	enum adjustment_data_type *type)
{
	uint32_t i = 0;

	if (adj_id < ADJ_ID_BEGIN || ADJ_ID_END < adj_id)
		return DS_ERROR;
	for (i = 0; i < CURRENT_ADJUSTMENT_NUM; i++) {
		if (adj_global_info_array[i].adj_id == adj_id) {
			*type = adj_global_info_array[i].adj_data_type;
			return DS_SUCCESS;
		}
	}
	return DS_ERROR;
}

static enum ds_return get_adj_info_from_defaults(
	struct ds_dispatch *ds,
	uint32_t disp_index,
	struct display_path *path,
	enum adjustment_id adjust_id,
	struct adjustment_info *adj_info)
{
	struct adjustment_api *api = NULL;
	enum signal_type signal;
	union cea_video_capability_data_block video_cap = { {0} };
	struct dcs *dcs = dal_display_path_get_dcs(path);

	dal_dcs_get_cea_video_capability_data_block(dcs, &video_cap);
	signal = dal_display_path_get_query_signal(path, SINK_LINK_INDEX);

	api = dal_adj_parent_api_what_is_the_target_obj(
		ds->default_adjustments,
		signal);
	if (!api)
		return DS_ERROR;
	get_adj_type(
		ds,
		adjust_id,
		&adj_info->adj_data_type);

	if (adj_info->adj_data_type == ADJ_RANGED) {
		/*default value from table*/
		if (!dal_adj_api_get_range_adj_data(
			api, adjust_id, adj_info))
			return DS_ERROR;
		/*then override it by signal types and other requests*/
		if (adjust_id == ADJ_ID_UNDERSCAN) {
			if (signal == SIGNAL_TYPE_DVI_SINGLE_LINK ||
				signal == SIGNAL_TYPE_DVI_SINGLE_LINK1 ||
				signal == SIGNAL_TYPE_DVI_DUAL_LINK ||
				signal == SIGNAL_TYPE_HDMI_TYPE_A ||
				signal == SIGNAL_TYPE_WIRELESS) {
				/*reviewers:this parts i changed a little bit
				 * compare with dal2*/
				/*we set 0 underscan for non-HDMI or S_CE1 is
				 * 1,S_CE1 indicates monitor will underscan
				 * automaticlly*/
				if (video_cap.bits.S_CE1 || signal !=
					SIGNAL_TYPE_HDMI_TYPE_A)
					adj_info->adj_data.ranged.def = 0;
			}
		}

	}
	return DS_SUCCESS;
}

static bool is_underscan_supported(
	struct ds_dispatch *ds,
	struct display_path *path,
	enum dcs_edid_connector_type connector_type,
	enum ds_signal_type ds_signal
	)
{
	bool supported = true;
	/* This call will check if underscan requirements are met.
	Currently, this is checking to see if the display engine
	clock is high enough to support underscan.*/
	if (!dal_adapter_service_is_meet_underscan_req(ds->as))
		supported = false;
	/* Checks if underscan is for HDMI only */
	else if (dal_adapter_service_underscan_for_hdmi_only(ds->as)) {
		connector_type = dal_dcs_get_connector_type(
			dal_display_path_get_dcs(path));

	if (!((connector_type == EDID_CONNECTOR_HDMIA) ||
		(dal_adapter_service_is_feature_supported(
		FEATURE_INSTANT_UP_SCALE_DOWN_SCALE) &&
		(ds_signal == DS_SIGNAL_TYPE_EDP))))
			supported = false;

	} else if (!dal_adapter_service_is_feature_supported(
	FEATURE_INSTANT_UP_SCALE_DOWN_SCALE)) {
		if (ds_signal == DS_SIGNAL_TYPE_EDP)
			supported = false;
	} else
		supported = true;
	return supported;
}

static bool is_adjustment_supported(
	struct ds_dispatch *ds,
	struct display_path *path,
	enum adjustment_id adjust_id)
{
	bool supported = true;
	uint32_t display_index;
	uint32_t index = 0;
	enum ds_signal_type ds_signal;
	enum dcs_edid_connector_type connector_type;

	if (!path) {
		dal_logger_write(ds->dal_context->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_DISPLAY_SERVICE,
			"display path is NULL");
		return false;
	}
	display_index = dal_tm_display_path_to_display_index(ds->tm, path);
	ds_signal = get_ds_signal_from_display_path(ds, path, display_index);
	connector_type =
		dal_dcs_get_connector_type(
			dal_display_path_get_dcs(path));
	if (ds_signal == DS_SIGNAL_TYPE_UNKNOWN) {
		dal_logger_write(ds->dal_context->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_DISPLAY_SERVICE,
			"DS_SIGNAL_TYPE_UNKNOWN");
		return false;
	}
	for (index = 0; index < CURRENT_ADJUSTMENT_NUM; index++) {
		if (adjust_id == adj_global_info_array[index].adj_id) {
			if (false == adj_global_info_array[index].
				display_is_supported[ds_signal])
					return false;
			else {
				if ((adjust_id == ADJ_ID_UNDERSCAN) ||
					(adjust_id == ADJ_ID_UNDERSCAN_TYPE))
						supported =
							is_underscan_supported(
								ds,
								path,
								connector_type,
								ds_signal);
			break;
		}
	 }
	}
	return supported;
}

static enum ds_return get_adjustment_info(
	struct ds_dispatch *ds,
	struct display_path *disp_path,
	enum adjustment_id adjust_id,
	bool fallback_to_default,
	struct adjustment_info *adj_info)
{
	uint32_t display_index =
		dal_display_path_get_display_index(disp_path);
	struct adj_container *adj_container =
		dal_ds_dispatch_get_adj_container_for_path(ds, display_index);
	const struct adjustment_info *cont_info =
		dal_adj_info_set_get_adj_info(
			&adj_container->adj_info_set, adjust_id);

	if (disp_path  == NULL || adj_info == NULL ||
		!is_adjustment_supported(ds, disp_path, adjust_id))
		return DS_ERROR;

	if (!adj_container)
		return DS_ERROR;

	if (cont_info != NULL)
		*adj_info = *cont_info;
	else if (fallback_to_default)
		return get_adj_info_from_defaults(
			ds,
			display_index,
			disp_path,
			adjust_id,
			adj_info);
	else
		return DS_ERROR;

	return DS_SUCCESS;
}

enum ds_return dal_ds_dispatch_get_adjustment_info(
	struct ds_dispatch *ds,
	uint32_t display_index,
	enum adjustment_id adjust_id,
	struct adjustment_info *adj_info)
{
	struct display_path *display_path =
		dal_tm_display_index_to_display_path(
			ds->tm, display_index);
	return get_adjustment_info(
		ds,
		display_path,
		adjust_id,
		true,
		adj_info);
}

static enum ds_return get_adj_property(
	struct ds_dispatch *ds,
	uint32_t disp_index,
	enum adjustment_id adjust_id,
	union adjustment_property *adj_property)
{
	enum ds_return result = DS_ERROR;
	uint32_t i = 0;

	if (disp_index >= dal_tm_get_num_display_paths(ds->tm, false))
		return DS_ERROR;
	for (i = 0; i < CURRENT_ADJUSTMENT_NUM; i++) {
		if (adj_global_info_array[i].adj_id == adjust_id) {
			*adj_property = adj_global_info_array[i].adj_prop;
			result = DS_SUCCESS;
			break;
		}
	}
	return result;
}
/* no used for now
static void update_adj_container_use_edid(
	struct ds_dispatch *ds,
	struct display_path *display_path)
{

}*/

static void build_gamut_adj_for_path(
	struct ds_dispatch *ds,
	uint32_t disp_index,
	struct adj_container *adj_container,
	struct display_path *display_path)
{
	struct gamut_data gamut_source_grph;
	struct gamut_data gamut_destination;
	struct ds_regamma_lut *regamma = NULL;

	dal_memset(&gamut_source_grph, 0, sizeof(gamut_source_grph));
	dal_memset(&gamut_destination, 0, sizeof(gamut_destination));

	/* Gamut source grph */
	dal_gamut_space_setup_default_gamut(
			ADJ_ID_GAMUT_SOURCE_GRPH,
			&gamut_source_grph,
			true,
			true);
	dal_adj_container_update_gamut(
			adj_container,
			ADJ_ID_GAMUT_SOURCE_GRPH,
			&gamut_source_grph);

	/* Gamut Destination */
	dal_gamut_space_setup_default_gamut(
			ADJ_ID_GAMUT_DESTINATION,
			&gamut_destination,
			true,
			true);
	dal_adj_container_update_gamut(
			adj_container,
			ADJ_ID_GAMUT_DESTINATION,
			&gamut_destination);

	regamma = dal_alloc(sizeof(*regamma));
	if (!dal_gamut_space_setup_predefined_regamma_coefficients(
			&gamut_destination, regamma))
		dal_ds_dispatch_setup_default_regamma(
				ds, regamma);

	dal_adj_container_set_regamma(
			adj_container,
			regamma);

	dal_free(regamma);
	regamma = NULL;

}

void dal_ds_dispatch_setup_default_regamma(
	struct ds_dispatch *ds,
	struct ds_regamma_lut *regamma)
{
	uint32_t i;

	regamma->flags.u32all = 0;
	regamma->flags.bits.COEFF_FROM_USER = 1;

	for (i = 0 ; i < COEFF_RANGE ; i++) {
		regamma->coeff.coeff_a0[i] = REGAMMA_COEFF_A0;
		regamma->coeff.coeff_a1[i] = REGAMMA_COEFF_A1;
		regamma->coeff.coeff_a2[i] = REGAMMA_COEFF_A2;
		regamma->coeff.coeff_a3[i] = REGAMMA_COEFF_A3;
		regamma->coeff.gamma[i] = REGAMMA_COEFF_GAMMA;
	}

}

enum ds_return dal_ds_dispatch_get_adjustment_current_value(
	struct ds_dispatch *ds,
	struct adj_container *container,
	struct adjustment_info *info,
	enum adjustment_id id,
	bool fall_back_to_default)
{
	if (info)
		if (info->adj_data_type == ADJ_RANGED)
			info->adj_data.ranged.cur = info->adj_data.ranged.def;
	if (id == ADJ_ID_UNDERSCAN || id == ADJ_ID_UNDERSCAN_TYPE)
		if (container &&
			dal_adj_container_get_default_underscan_allow(
				container))
			info->adj_data.ranged.cur = 0;
	return DS_SUCCESS;
}

enum ds_return dal_ds_dispatch_get_adjustment_value(
	struct ds_dispatch *ds,
	struct display_path *disp_path,
	enum adjustment_id adj_id,
	bool fall_back_to_default,
	int32_t *value)
{
	uint32_t display_index;
	struct adj_container *adj_container;
	struct adjustment_info adj_info;

	if (!disp_path)
		return DS_ERROR;

	if (!is_adjustment_supported(ds, disp_path, adj_id))
		return DS_ERROR;

	display_index = dal_display_path_get_display_index(
			disp_path);
	adj_container = dal_ds_dispatch_get_adj_container_for_path(
			ds, display_index);

	if (DS_SUCCESS != get_adjustment_info(ds,
			disp_path, adj_id,
			fall_back_to_default, &adj_info))
		return DS_ERROR;

	*value = adj_info.adj_data.ranged.cur;

	if (adj_id == ADJ_ID_UNDERSCAN || adj_id == ADJ_ID_UNDERSCAN_TYPE)
			if (adj_container &&
				dal_adj_container_get_default_underscan_allow(
					adj_container))
				*value = 0;
	if (adj_info.adj_data_type == ADJ_RANGED)
		adj_info.adj_data.ranged.cur = *value;
	else if (adj_info.adj_data_type == ADJ_BITVECTOR)
		adj_info.adj_data.bit_vector.current_supported = *value;

	return DS_SUCCESS;
}

void dal_ds_dispatch_update_adj_container_for_path_with_edid(
	struct ds_dispatch *ds,
	struct display_path *path)
{
	uint32_t index = dal_display_path_get_display_index(path);
	struct adj_container *container;

	if (!path)
		dal_logger_write(ds->dal_context->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_DISPLAY_SERVICE,
			"display_path is unknown");
	container = dal_ds_dispatch_get_adj_container_for_path(ds, index);
	dal_adj_container_update_display_cap(container, path);
	dal_adj_container_update_signal_type(
		container,
		dal_display_path_get_query_signal(path, SINK_LINK_INDEX));
	build_adj_container_for_path(ds, path);
}

void dal_ds_dispatch_update_adj_container_for_path_with_mode_info(
	struct ds_dispatch *ds,
	struct display_path *display_path,
	const struct path_mode *path_mode)
{
	uint32_t index = dal_display_path_get_display_index(display_path);
	struct adj_container *container;

	if (!display_path)
		dal_logger_write(ds->dal_context->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_DISPLAY_SERVICE,
			"display_path is unknown");
	container = dal_ds_dispatch_get_adj_container_for_path(ds, index);
	if (container) {
		dal_adj_container_update_timing_mode(
			container,
			&path_mode->mode_timing->mode_info,
			&path_mode->view);
		dal_ds_dispatch_update_adj_container_for_path_with_edid(
			ds,
			display_path);
	}

}

static enum ds_signal_type get_ds_signal_from_display_path(
	struct ds_dispatch *ds,
	struct display_path *display_path,
	uint32_t idx)
{	enum ds_signal_type ds_signal = DS_SIGNAL_TYPE_CRT;
	enum signal_type signal = dal_display_path_get_query_signal(
		display_path,
		SINK_LINK_INDEX);
	switch (signal) {
	case SIGNAL_TYPE_DVI_SINGLE_LINK:
	case SIGNAL_TYPE_DVI_SINGLE_LINK1:
	case SIGNAL_TYPE_DVI_DUAL_LINK:
		ds_signal = DS_SIGNAL_TYPE_DFP;
		break;
	case SIGNAL_TYPE_HDMI_TYPE_A:
		ds_signal = DS_SIGNAL_TYPE_HDMI;
		break;
	case SIGNAL_TYPE_DISPLAY_PORT:
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
		ds_signal = DS_SIGNAL_TYPE_DP;
		break;
	case SIGNAL_TYPE_EDP:
		ds_signal = DS_SIGNAL_TYPE_EDP;
		break;
	case SIGNAL_TYPE_RGB:
		if (dal_dcs_is_non_continous_frequency(
			dal_display_path_get_dcs(display_path)))
			ds_signal = DS_SIGNAL_TYPE_DISCRETEVGA;
		else
			ds_signal = DS_SIGNAL_TYPE_CRT;
		break;
	case SIGNAL_TYPE_MVPU_A:
	case SIGNAL_TYPE_MVPU_B:
	case SIGNAL_TYPE_MVPU_AB:
		ds_signal = DS_SIGNAL_TYPE_CF;
		break;
	case SIGNAL_TYPE_WIRELESS:
		ds_signal = DS_SIGNAL_TYPE_WIRELESS;
		break;
	default:
		ds_signal = DS_SIGNAL_TYPE_UNKNOWN;
		dal_logger_write(ds->dal_context->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_DISPLAY_SERVICE,
			"dignal type is unknown");
		break;
	}
	return ds_signal;
}

struct adj_container *dal_ds_dispatch_get_adj_container_for_path(
	const struct ds_dispatch *ds,
	uint32_t display_index)
{

	if (display_index < ds->disp_path_num)
		return ds->applicable_adj[display_index];
	return NULL;
}

bool dal_ds_dispatch_initialize_adjustment(struct ds_dispatch *ds)
{
	uint32_t i;
	uint32_t num;

	/* TODO unnecesary init_data. can just pass ds */
	struct backlight_adj_group_init_data backlight_init_data;
	struct single_adj_group_init_data single_init_data;
	struct grph_colors_group_init_data colors_init_data;
	struct grph_gamma_lut_group_init_data gamma_init_data;

	ds->disp_path_num = dal_tm_get_num_display_paths(ds->tm, false);
	num = ds->disp_path_num;

	if (num == 0)
		return false;

	ds->applicable_adj = dal_alloc((sizeof(*ds->applicable_adj) * num));
	if (ds->applicable_adj == NULL)
		return false;
	for (i = 0; i < num; i++) {
		ds->applicable_adj[i] = dal_adj_container_create();
		if (!ds->applicable_adj[i]) {
			dal_logger_write(
				ds->dal_context->logger,
				LOG_MAJOR_ERROR,
				LOG_MINOR_COMPONENT_DISPLAY_SERVICE,
				"initilize_adjustment has error");
			dal_adj_container_destroy(ds->applicable_adj);
			return false;
		}
	}

	ds->default_adjustments = dal_adj_parent_api_create();
	if (!ds->default_adjustments)
		return false;
	dal_adj_parent_api_build_child_objs(ds->default_adjustments);

	backlight_init_data.ds = ds;
	backlight_init_data.as = ds->as;
	backlight_init_data.hws = ds->hwss;
	backlight_init_data.tm = ds->tm;
	backlight_init_data.dal_context = ds->dal_context;
	ds->backlight_adj = dal_backlight_adj_group_create(
		&backlight_init_data);
	if (!ds->backlight_adj) {
		dal_ds_dispatch_cleanup_adjustment(ds);
		return false;
	}
	single_init_data.ds = ds;
	single_init_data.hws = ds->hwss;
	single_init_data.tm = ds->tm;
	single_init_data.dal_context = ds->dal_context;
	ds->single_adj = dal_single_adj_group_create(&single_init_data);

	if (!ds->single_adj) {
		dal_ds_dispatch_cleanup_adjustment(ds);
		return false;
	}
	colors_init_data.ds = ds;
	colors_init_data.hws = ds->hwss;
	colors_init_data.dal_context = ds->dal_context;
	ds->grph_colors_adj =
		dal_grph_colors_group_create(&colors_init_data);
	if (!ds->grph_colors_adj) {
		dal_ds_dispatch_cleanup_adjustment(ds);
		return false;
	}

	gamma_init_data.ds = ds;
	gamma_init_data.hws = ds->hwss;
	gamma_init_data.dal_context = ds->dal_context;
	ds->grph_gamma_adj = dal_gamma_adj_group_create(&gamma_init_data);
	if (!ds->grph_gamma_adj) {
		dal_ds_dispatch_cleanup_adjustment(ds);
		return false;
	}

	return true;
}

void dal_ds_dispatch_cleanup_adjustment(struct ds_dispatch *ds)
{
	uint32_t i;
	uint32_t num;

	num = ds->disp_path_num;

	for (i = 0; i < num; i++)
		dal_adj_container_destroy(ds->applicable_adj + i);
	dal_free(ds->applicable_adj);

	if (ds->default_adjustments)
		dal_adj_parent_api_destroy(&ds->default_adjustments);

	if (ds->backlight_adj != NULL)
		dal_backlight_adj_group_destroy(&ds->backlight_adj);

	if (ds->single_adj != NULL)
			dal_single_adj_group_destroy(&ds->single_adj);

	if (ds->grph_colors_adj != NULL)
		dal_grph_colors_adj_group_destroy(&ds->grph_colors_adj);

	if (ds->grph_gamma_adj != NULL)
		dal_grph_gamma_adj_group_destroy(&ds->grph_gamma_adj);

}

bool dal_ds_dispatch_build_post_set_mode_adj(
	struct ds_dispatch *ds,
	const struct path_mode *mode,
	struct display_path *display_path,
	struct hw_adjustment_set *set)
{
	/*TODO: add implementation*/
	return false;
}

bool dal_ds_dispatch_apply_scaling(
	struct ds_dispatch *ds,
	const struct path_mode *mode,
	struct adj_container *container,
	enum build_path_set_reason reason,
	struct hw_path_mode *hw_path_mode)
{
	enum adjustment_id adj_id = ADJ_ID_UNDERSCAN;
	struct ds_adjustment_scaler scaler;
	const struct adjustment_info *info;
	struct adj_container *set;

	if (!hw_path_mode || !container)
		return false;
	info = dal_adj_info_set_get_adj_info(&container->adj_info_set, adj_id);

	if (!info)
		return false;
	if (reason == BUILD_PATH_SET_REASON_FALLBACK_UNDERSCAN &&
		hw_path_mode->action == HW_PATH_ACTION_SET) {
		set = dal_ds_dispatch_get_adj_container_for_path(
			ds,
			mode->display_path_index);
		dal_adj_info_set_update_cur_value(
			&set->adj_info_set, adj_id, 0);
	}
	if (!dal_scaler_adj_group_build_scaler_parameter(
		mode,
		container,
		reason,
		adj_id,
		info->adj_data.ranged.cur,
		NULL,
		hw_path_mode->display_path,
		&scaler))
		return false;
	return dal_scaler_adj_group_apply_scaling(
			&scaler,
			container,
			reason,
			hw_path_mode);
}

bool dal_ds_dispatch_is_adjustment_supported(
	struct ds_dispatch *ds,
	uint32_t display_index,
	enum adjustment_id adjust_id)
{
	struct display_path *display_path =
		dal_tm_display_index_to_display_path(
			ds->tm, display_index);
	return is_adjustment_supported(ds, display_path, adjust_id);
}

enum ds_return dal_ds_dispatch_get_property(
	struct ds_dispatch *adj,
	uint32_t display_index,
	enum adjustment_id adjust_id,
	union adjustment_property *property)
{
	/*TODO: add implementation*/
	return DS_ERROR;
}

enum ds_return dal_ds_dispatch_set_adjustment(
	struct ds_dispatch *ds,
	const uint32_t display_index,
	enum adjustment_id adjust_id,
	int32_t value)
{
	enum ds_return  result = DS_ERROR;
	struct display_path *display_path =
		dal_tm_display_index_to_display_path(
			ds->tm, display_index);
	if (display_path == NULL || !is_adjustment_supported(
		ds, display_path, adjust_id)) {
		dal_logger_write(ds->dal_context->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_DISPLAY_SERVICE,
			"display path is NULL");
		return DS_ERROR;
	}
	switch (adjust_id) {
	case ADJ_ID_SATURATION:
		result = dal_grph_colors_group_set_adjustment(
				ds->grph_colors_adj,
				display_path,
				adjust_id,
				value);
		break;

	case ADJ_ID_UNDERSCAN:
	case ADJ_ID_UNDERSCAN_TYPE:
		result = dal_scaler_adj_group_set_adjustment(
				ds,
				display_index,
				display_path,
				adjust_id,
				value);
		break;

	case ADJ_ID_BACKLIGHT:
		result = dal_backlight_adj_group_set_adjustment(
				ds->backlight_adj,
				display_path,
				adjust_id,
				value);
		break;
	case ADJ_ID_BIT_DEPTH_REDUCTION:
		result = dal_single_adj_group_set_adjustment(
				ds->single_adj,
				display_path,
				adjust_id,
				value);
		break;
	default:
		dal_logger_write(ds->dal_context->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_DISPLAY_SERVICE,
			"set_adjustment failed");
		result = DS_ERROR;
	}
	return result;
}

enum ds_return dal_ds_dispatch_set_gamma_adjustment(struct ds_dispatch *ds,
		uint32_t display_index, enum adjustment_id adjust_id,
		const struct raw_gamma_ramp *gamma) {
	struct adj_container *adj_container = NULL;
	const struct adjustment_info *cont_info = NULL;
	const struct ds_regamma_lut *regumma_lut = NULL;
	struct display_path *disp_path;
	const struct path_mode *disp_path_mode;

	if (ds == NULL)
		return DS_ERROR;

	disp_path = dal_tm_display_index_to_display_path(
			ds->tm, display_index);

	disp_path_mode = dal_pms_with_data_get_path_mode_for_display_index(
				ds->set,
				display_index);

	if (disp_path == NULL || disp_path_mode == NULL)
		return DS_ERROR;

	adj_container = dal_ds_dispatch_get_adj_container_for_path(ds,
			display_index);

	if (adj_container == NULL)
		return DS_ERROR;

	cont_info = dal_adj_info_set_get_adj_info(&adj_container->adj_info_set,
			adjust_id);

	if (cont_info == NULL)
		return DS_ERROR;

	if (disp_path  == NULL || cont_info == NULL ||
		!is_adjustment_supported(ds, disp_path, adjust_id))
		return DS_ERROR;

	if (!is_adjustment_supported(ds, disp_path, adjust_id))
		return DS_ERROR;

	regumma_lut = dal_adj_container_get_regamma(
			adj_container);

	if (regumma_lut == NULL)
		return DS_ERROR;

	if (dal_grph_gamma_lut_set_adjustment(ds, disp_path, disp_path_mode,
			adjust_id, gamma, regumma_lut))
		return DS_SUCCESS;

	return DS_ERROR;
}

const struct raw_gamma_ramp *dal_ds_dispatch_get_current_gamma(
	struct ds_dispatch *ds,
	uint32_t display_index,
	enum adjustment_id adjust_id)
{
	/*TODO: add implementation*/
	return NULL;
}

const struct raw_gamma_ramp *dal_ds_dispatch_get_default_gamma(
	struct ds_dispatch *ds,
	uint32_t display_index,
	enum adjustment_id adjust_id)
{
	/*TODO: add implementation*/
	return NULL;
}

enum ds_return dal_ds_dispatch_set_current_gamma(
	struct ds_dispatch *ds,
	uint32_t display_index,
	enum adjustment_id adjust_id,
	const struct raw_gamma_ramp *gamma)
{
	/*TODO: add implementation*/
	return DS_ERROR;
}

enum ds_return dal_ds_dispatch_set_gamma(
	struct ds_dispatch *ds,
	uint32_t display_index,
	enum adjustment_id adjust_id,
	const struct raw_gamma_ramp *gamma)
{
	/*TODO: add implementation*/
	return DS_ERROR;
}

bool dal_ds_dispatch_get_underscan_info(
	struct ds_dispatch *ds,
	uint32_t display_index,
	struct ds_underscan_info *info)
{
	bool result = false;
	struct display_path *display_path =
		dal_tm_display_index_to_display_path(
			ds->tm, display_index);
	const struct path_mode *path_mode =
		dal_pms_with_data_get_path_mode_for_display_index(
			ds->set, display_index);
	if (!display_path || !path_mode)
		return false;
	/*TODO: add implementation*
	 * result = scaler_adj->get_underscan_info();*/
	return result;
}

bool dal_ds_dispatch_get_underscan_mode(
	struct ds_dispatch *ds,
	uint32_t display_index,
	struct ds_underscan_desc *desc)
{
	/*TODO: add implementation*/
	return false;
}

bool dal_ds_dispatch_set_underscan_mode(
	struct ds_dispatch *ds,
	uint32_t display_index,
	struct ds_underscan_desc *desc)
{
	/*TODO: add implementation*/
	return false;
}

bool dal_ds_dispatch_setup_overlay(
	struct ds_dispatch *adj,
	uint32_t display_index,
	struct overlay_data *data)
{
	/*TODO: add implementation*/
	return false;
}

void dal_ds_dispatch_set_applicable_adj(
	struct ds_dispatch *adj,
	uint32_t display_index,
	const struct adj_container *applicable)
{
	/*TODO: add implementation*/
}

enum ds_return dal_ds_dispatch_set_color_gamut(
	struct ds_dispatch *adj,
	uint32_t display_index,
	const struct ds_set_gamut_data *data)
{
	/*TODO: add implementation*/
	return DS_ERROR;
}

enum ds_return dal_ds_dispatch_get_color_gamut(
	struct ds_dispatch *adj,
	uint32_t display_index,
	const struct ds_gamut_reference_data *ref,
	struct ds_get_gamut_data *data)
{
	/*TODO: add implementation*/
	return DS_ERROR;
}

enum ds_return dal_ds_dispatch_get_color_gamut_info(
	struct ds_dispatch *adj,
	uint32_t display_index,
	const struct ds_gamut_reference_data *ref,
	struct ds_gamut_info *data)
{
	/*TODO: add implementation*/
	return DS_ERROR;
}

enum ds_return dal_ds_dispatch_get_regamma_lut(
	struct ds_dispatch *adj,
	uint32_t display_index,
	struct ds_regamma_lut *data)
{
	/*TODO: add implementation*/
	return DS_ERROR;
}

enum ds_return dal_ds_dispatch_set_regamma_lut(
	struct ds_dispatch *adj,
	uint32_t display_index,
	struct ds_regamma_lut *data)
{
	/*TODO: add implementation*/
	return DS_ERROR;
}

enum ds_return dal_ds_dispatch_set_info_packets(
	struct ds_dispatch *adj,
	uint32_t display_index,
	const struct info_frame *info_frames)
{
	/*TODO: add implementation*/
	return DS_ERROR;
}

enum ds_return dal_ds_dispatch_get_info_packets(
	struct ds_dispatch *adj,
	uint32_t display_index,
	struct info_frame *info_frames)
{
	/*TODO: add implementation*/
	return DS_ERROR;
}

static void build_adj_container_for_path(
	struct ds_dispatch *ds,
	struct display_path *display_path)
{
	uint32_t index = 0;
	uint32_t i = 0;

	struct adjustment_info info;
	struct adj_container *container = NULL;

	dal_memset(&info, 0, sizeof(struct adjustment_info));
	index = dal_display_path_get_display_index(display_path);
	if (!display_path)
		dal_logger_write(ds->dal_context->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_DISPLAY_SERVICE,
			"display_path is unknown");
	container = dal_ds_dispatch_get_adj_container_for_path(ds, index);

	if (!container)
		return;

	if (!dal_adj_container_is_update_required(container))
		return;

	dal_adj_info_set_clear(&container->adj_info_set);

	build_gamut_adj_for_path(ds, index, container, display_path);

	for (i = ADJ_ID_BEGIN; i < ADJ_ID_END; i++) {
		enum adjustment_id adj_id = i;

		if (is_adjustment_supported(ds, display_path, adj_id)) {
			if (DS_SUCCESS != get_adj_info_from_defaults(
				ds,
				index,
				display_path,
				adj_id,
				&info))
				continue;
			if (DS_SUCCESS != get_adj_property(
				ds,
				index,
				adj_id,
				&info.adj_prop))
				continue;
			info.adj_id = adj_id;
			info.adj_state = ADJUSTMENT_STATE_VALID;
			dal_adj_info_set_add_adj_info(
				&container->adj_info_set,
				&info);
			if (ADJ_RANGED == info.adj_data_type) {
				dal_ds_dispatch_get_adjustment_current_value(
					ds,
					container,
					&info,
					adj_id,
					true);
			dal_adj_info_set_update_cur_value(
				&container->adj_info_set,
				adj_id,
				info.adj_data.ranged.cur);
			}

		}
	}

	dal_adj_container_updated(container);
}

bool dal_ds_dispatch_include_adjustment(
	struct ds_dispatch *ds,
	struct display_path *disp_path,
	struct ds_adj_id_value adj,
	struct hw_adjustment_set *set)
{
	bool ret = false;

	if (adj.adj_id == ADJ_ID_BIT_DEPTH_REDUCTION)
		ret = dal_single_adj_group_include_adjustment(
				ds->single_adj,
				disp_path, adj, set);
	return ret;
}

bool dal_ds_dispatch_build_include_adj(
	struct ds_dispatch *ds,
	const struct path_mode *mode,
	struct display_path *display_path,
	struct hw_path_mode *hw_mode,
	struct hw_adjustment_set *set)
{
	/* TODO implement build_include_adjustments */
	return false;
}

bool dal_ds_dispatch_build_color_control_adj(
	struct ds_dispatch *ds,
	const struct path_mode *mode,
	struct display_path *disp_path,
	struct hw_adjustment_set *set)
{
	if (!ds->grph_colors_adj || !disp_path)
		return false;

	return dal_grph_colors_group_build_color_control_adj(
			ds->grph_colors_adj,
			mode,
			disp_path,
			set);
}

void dal_ds_dispatch_update_adj_container_for_path_with_color_space(
	struct ds_dispatch *ds,
	uint32_t display_index,
	enum ds_color_space color_space)
{
	struct adj_container *adj_container;

	adj_container = dal_ds_dispatch_get_adj_container_for_path(
				ds, display_index);

	if (adj_container != NULL)
		dal_adj_container_update_color_space(
				adj_container,
				color_space);
}
