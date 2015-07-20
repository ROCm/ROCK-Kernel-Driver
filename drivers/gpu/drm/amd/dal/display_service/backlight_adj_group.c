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
#include "dal_services.h"
#include "include/topology_mgr_interface.h"
#include "include/display_path_interface.h"
#include "include/display_service_types.h"
#include "include/hw_adjustment_types.h"
#include "include/adjustment_interface.h"
#include "include/logger_interface.h"
#include "include/hw_adjustment_set.h"

#include "ds_dispatch.h"
#include "adjustment_container.h"
#include "backlight_adj_group.h"

static uint32_t adj_id_to_cache_index(
	enum adjustment_id adj_id)
{
	if (adj_id == ADJ_ID_BACKLIGHT)
		return BI_ADJ_INDEX_BACKLIGHT;
	else if (adj_id == ADJ_ID_BACKLIGHT_OPTIMIZATION)
		return BI_ADJ_INDEX_BACKLIGHT_OPTIMIZATION;

	return -1;
}

bool dal_backlight_adj_group_add_adj_to_post_mode_set(
	struct backlight_adj_group *backlight_adj,
	uint32_t value,
	struct hw_adjustment_set *set)
{
	struct hw_adjustment_value *adj_value = NULL;

	if (set->backlight != NULL) {
		dal_logger_write(
			backlight_adj->dal_context->logger,
			LOG_MAJOR_WARNING,
			LOG_MINOR_COMPONENT_DISPLAY_SERVICE,
			"HW Backlight adjustment  is NULL");
		return false;
	}
	adj_value = dal_alloc(sizeof(*adj_value));

	if (!adj_value)
		return false;

	adj_value->i_value = value;
	set->backlight = adj_value;

	return true;

}
bool dal_backlight_adj_group_include_backlight_opt_adj(
	struct backlight_adj_group *backlight_adj,
	struct display_path *disp_path,
	uint32_t value,
	struct hw_adjustment_set *set)
{
	uint32_t count = 0;

	switch (value) {
	case DS_BACKLIGHT_OPTIMIZATION_DISABLE:
	{
		uint32_t backlight;

		if (dal_backlight_adj_group_get_current_adj(
				backlight_adj,
				disp_path,
				ADJ_ID_BACKLIGHT,
				false,
				&backlight)) {
			if (dal_backlight_adj_group_add_adj_to_post_mode_set(
					backlight_adj,
					backlight,
					set))
				count++;
		}
		if (dal_backlight_adj_group_add_adj_to_post_mode_set(
					backlight_adj,
					0,
					set))
			count++;
	}
		break;
	case DS_BACKLIGHT_OPTIMIZATION_DESKTOP:
	case DS_BACKLIGHT_OPTIMIZATION_DYNAMIC:
	{
		uint32_t backlight;

		if (dal_backlight_adj_group_get_current_adj(
				backlight_adj,
				disp_path,
				ADJ_ID_BACKLIGHT,
				false,
				&backlight)) {
			if (dal_backlight_adj_group_add_adj_to_post_mode_set(
					backlight_adj,
					backlight,
					set))
				count++;
		}

	}
		break;
	case DS_BACKLIGHT_OPTIMIZATION_DIMMED:
	{
		struct panel_backlight_boundaries boundaries = {0};

		if (dal_adapter_service_get_panel_backlight_boundaries(
				backlight_adj->as,
				&boundaries)) {
			if (dal_backlight_adj_group_add_adj_to_post_mode_set(
					backlight_adj,
					boundaries.min_signal_level,
					set))
				count++;
		}
		if (dal_backlight_adj_group_add_adj_to_post_mode_set(
					backlight_adj,
					0,
					set))
			count++;
	}
		break;
	default:
		break;
	}
	if (count > 0)
		return true;
	else
		return false;
}

bool dal_backlight_adj_group_include_post_set_mode_adj(
	struct backlight_adj_group *backlight_adj,
	struct display_path *disp_path,
	struct ds_adj_id_value adj,
	struct hw_adjustment_set *set)
{
	uint32_t index = adj_id_to_cache_index(adj.adj_id);
	uint32_t opt_adjustment = 0;
	bool result = false;
	uint32_t value = backlight_adj->cache[index].value;

	if (index >= NUM_OF_BACKLIGHT_ADJUSTMENTS)
		return false;

	if (!backlight_adj->cache[index].pending)
		return false;


	if (adj.adj_id != ADJ_ID_BACKLIGHT_OPTIMIZATION) {
		if (!dal_backlight_adj_group_get_current_adj(
				backlight_adj,
				disp_path,
				ADJ_ID_BACKLIGHT_OPTIMIZATION,
				true,
				&opt_adjustment))
			return false;
	}

	switch (adj.adj_id)	{
	case ADJ_ID_BACKLIGHT:
	{
		if (opt_adjustment != DS_BACKLIGHT_OPTIMIZATION_DIMMED)
			result =
			dal_backlight_adj_group_add_adj_to_post_mode_set(
					backlight_adj,
					value,
					set);
	}
		break;
	case ADJ_ID_BACKLIGHT_OPTIMIZATION:
	{
		result =
			dal_backlight_adj_group_include_backlight_opt_adj(
				backlight_adj,
				disp_path,
				value,
				set);
	}
		break;
	default:
		break;
	}

	if (backlight_adj->cache[index].pending_sw_commit) {
		uint32_t display_index =
				dal_display_path_get_display_index(disp_path);
		struct adj_container *adj_container =
				dal_ds_dispatch_get_adj_container_for_path(
					backlight_adj->ds, display_index);
		const struct adjustment_info *adj_info = NULL;

		bool commit = (adj_container != NULL);

		if (commit) {
			adj_info = dal_adj_info_set_get_adj_info(
					&adj_container->adj_info_set,
					adj.adj_id);

			commit = (adj_info != NULL);
		}
		if (commit)
			commit = dal_adj_info_set_update_cur_value(
					&adj_container->adj_info_set,
					adj.adj_id, value);

		if (commit)
			commit = dal_adj_container_commit_adj(
					adj_container, adj.adj_id);
	}
	backlight_adj->cache[index].pending = false;
	backlight_adj->cache[index].pending_sw_commit = false;
	return result;
}


enum ds_return dal_backlight_adj_group_set_adjustment(
	struct backlight_adj_group *backlight_adj,
	struct display_path *disp_path,
	enum adjustment_id adj_id,
	uint32_t value)
{
	enum ds_return result = DS_SUCCESS;
	uint32_t display_index =
			dal_display_path_get_display_index(disp_path);
	uint32_t adj_index = adj_id_to_cache_index(adj_id);

	struct adj_container *adj_container =
			dal_ds_dispatch_get_adj_container_for_path(
				backlight_adj->ds,
				display_index);
	const struct adjustment_info *adj_info =
			dal_adj_info_set_get_adj_info(
				&adj_container->adj_info_set, adj_id);
	uint32_t opt_adj = 0;

	if (adj_container == NULL ||
			adj_index >= NUM_OF_BACKLIGHT_ADJUSTMENTS)
		return DS_ERROR;


	if (adj_info == NULL) {

		struct adjustment_info default_adj_info;

		if (dal_ds_dispatch_get_adjustment_info(
				backlight_adj->ds,
				display_index,
				adj_id,
				&default_adj_info) != DS_SUCCESS)
			return DS_ERROR;

		if (value < default_adj_info.adj_data.ranged.min ||
				value > default_adj_info.adj_data.ranged.max)
			return DS_ERROR;
		backlight_adj->cache[adj_index].pending = true;
		backlight_adj->cache[adj_index].pending_sw_commit = true;
		backlight_adj->cache[adj_index].value = value;
		return DS_SUCCESS;
	}
	if (value < adj_info->adj_data.ranged.min ||
			value > adj_info->adj_data.ranged.max)
		return DS_ERROR;

	if (!dal_adj_info_set_update_cur_value(
			&adj_container->adj_info_set, adj_id, value))
		return DS_ERROR;

	if (adj_id != ADJ_ID_BACKLIGHT_OPTIMIZATION) {
		if (!dal_backlight_adj_group_get_current_adj(
				backlight_adj,
				disp_path,
				ADJ_ID_BACKLIGHT_OPTIMIZATION,
				true, &opt_adj))
			return DS_ERROR;
	}

	if (dal_display_path_is_acquired(disp_path) &&
			dal_tm_is_hw_state_valid(backlight_adj->tm)) {

		switch (adj_id)	{
		case ADJ_ID_BACKLIGHT:
		{
			if (opt_adj != DS_BACKLIGHT_OPTIMIZATION_DIMMED)
				result =
				dal_backlight_adj_group_set_backlight_adj(
						backlight_adj,
						disp_path,
						value);
		}
			break;
		case ADJ_ID_BACKLIGHT_OPTIMIZATION:
		{
			result =
			dal_backlight_adj_group_set_backlight_optimization_adj(
					backlight_adj,
					disp_path,
					value);
		}
			break;
		default:
			result = DS_ERROR;
			break;
		}
		backlight_adj->cache[adj_index].pending = false;
		backlight_adj->cache[adj_index].pending_sw_commit = false;
	} else {
		backlight_adj->cache[adj_index].pending = true;
		backlight_adj->cache[adj_index].pending_sw_commit = false;
		backlight_adj->cache[adj_index].value = value;
	}
	if (result == DS_SUCCESS)
		dal_adj_container_commit_adj(adj_container, adj_id);
	return result;
}

bool dal_backlight_adj_group_get_current_adj(
	struct backlight_adj_group *backlight_adj,
	struct display_path *disp_path,
	enum adjustment_id adj_id,
	bool allow_default,
	uint32_t *value)
{
	uint32_t index = adj_id_to_cache_index(adj_id);

	if (backlight_adj->cache[index].pending) {
		*value = backlight_adj->cache[index].value;
		return true;
	} else if (dal_ds_dispatch_get_adjustment_value(
			backlight_adj->ds,
			disp_path,
			adj_id,
			allow_default,
			value) == DS_SUCCESS)
		return true;

	return false;
}

enum ds_return dal_backlight_adj_group_set_backlight_adj(
	struct backlight_adj_group *backlight_adj,
	struct display_path *disp_path,
	uint32_t value)
{
	enum ds_return result = DS_ERROR;
	struct hw_adjustment_value hw_adj_value;

	hw_adj_value.ui_value = value;
	if (dal_hw_sequencer_set_backlight_adjustment(
			backlight_adj->hws,
			disp_path,
			&hw_adj_value) == HWSS_RESULT_OK)
		result = DS_SUCCESS;

	return result;
}

enum ds_return dal_backlight_adj_group_set_backlight_optimization_adj(
	struct backlight_adj_group *backlight_adj,
	struct display_path *disp_path,
	uint32_t value)
{
	switch (value) {
	case DS_BACKLIGHT_OPTIMIZATION_DISABLE:
	{
		uint32_t backlight;

		if (dal_backlight_adj_group_get_current_adj(
				backlight_adj,
				disp_path,
				ADJ_ID_BACKLIGHT,
				false,
				&backlight)) {

			if (dal_backlight_adj_group_set_backlight_adj(
					backlight_adj,
					disp_path,
					backlight) != DS_SUCCESS)
				return DS_ERROR;
		}
	}
		break;
	case DS_BACKLIGHT_OPTIMIZATION_DESKTOP:
	case DS_BACKLIGHT_OPTIMIZATION_DYNAMIC:
	{
		uint32_t backlight;

		if (dal_backlight_adj_group_get_current_adj(
				backlight_adj,
				disp_path,
				ADJ_ID_BACKLIGHT,
				false,
				&backlight)) {
			if (dal_backlight_adj_group_set_backlight_adj(
					backlight_adj,
					disp_path,
					backlight) != DS_SUCCESS)
				return DS_ERROR;
		}

	}
		break;
	case DS_BACKLIGHT_OPTIMIZATION_DIMMED:
	{
		struct panel_backlight_boundaries boundaries = {0};
		uint32_t backlight;

		if (!dal_adapter_service_get_panel_backlight_boundaries(
				backlight_adj->as,
				&boundaries))
			return DS_ERROR;

		backlight = boundaries.min_signal_level;
		if (dal_backlight_adj_group_set_backlight_adj(
				backlight_adj,
				disp_path,
				backlight) != DS_SUCCESS)
			return DS_ERROR;

	}
		break;
	default:
		return DS_ERROR;
	}
	return DS_SUCCESS;

}

static bool backlight_adj_group_construct(
	struct backlight_adj_group *backlight_adj,
	struct backlight_adj_group_init_data *init_data)
{
	if (!init_data)
		return false;

	backlight_adj->ds = init_data->ds;
	backlight_adj->as = init_data->as;
	backlight_adj->hws = init_data->hws;
	backlight_adj->tm = init_data->tm;
	backlight_adj->dal_context = init_data->dal_context;

	return true;
}

struct backlight_adj_group *dal_backlight_adj_group_create(
		struct backlight_adj_group_init_data *init_data)
{
	struct backlight_adj_group *backlight_adj = NULL;

	backlight_adj = dal_alloc(sizeof(*backlight_adj));

	if (!backlight_adj)
		return NULL;

	if (backlight_adj_group_construct(backlight_adj, init_data))
		return backlight_adj;

	dal_free(backlight_adj);

	return NULL;
}

static void destruct(
	struct backlight_adj_group *backlight_adj)
{
}

void dal_backlight_adj_group_destroy(
	struct backlight_adj_group **backlight_adj)
{
	if (backlight_adj == NULL || *backlight_adj == NULL)
		return;
	destruct(*backlight_adj);
	dal_free(*backlight_adj);
	*backlight_adj = NULL;
}

