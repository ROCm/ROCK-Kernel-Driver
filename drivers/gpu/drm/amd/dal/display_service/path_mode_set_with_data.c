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

#include "include/vector.h"
#include "include/plane_types.h"

#include "path_mode_set_with_data.h"

/* Set of path modes with corresponding data flags*/
struct path_mode_set_with_data {
	struct path_mode_set base;
	struct mode_timing mode_timing[MAX_COFUNC_PATH];
	struct active_path_data path_data[MAX_COFUNC_PATH];

	struct vector plane_configs[MAX_COFUNC_PATH];
};

#define FROM_PMS(ptr) \
	container_of((ptr), struct path_mode_set_with_data, base)

/* Constructor for path mode set with data */
static bool construct(struct path_mode_set_with_data *set)
{
	if (set == NULL)
		return false;

	if (!dal_pms_construct(&set->base))
		return false;

	return true;
}

/*
 * Path mode set with data
 */

/* Create the set for path mode with data */
struct path_mode_set_with_data *dal_pms_with_data_create()
{
	struct path_mode_set_with_data *set = NULL;

	set = dal_alloc(sizeof(struct path_mode_set_with_data));

	if (set == NULL)
		return NULL;

	if (construct(set))
		return set;

	dal_free(set);
	BREAK_TO_DEBUGGER();
	return NULL;
}

/* Add path mode with data into the set */
bool dal_pms_with_data_add_path_mode_with_data(
		struct path_mode_set_with_data *set,
		const struct path_mode *mode,
		const struct active_path_data *data)
{
	bool result = dal_pms_add_path_mode(&set->base, mode);
	uint32_t index = set->base.count - 1;
	const uint32_t preallocate_planes_num = 4;

	if (result) {
		set->mode_timing[index] = *mode->mode_timing;
		set->base.path_mode_set[index].mode_timing =
				&set->mode_timing[index];

		set->path_data[index].current_underscan.x = 0;
		set->path_data[index].current_underscan.y = 0;
		set->path_data[index].current_underscan.width = 0;
		set->path_data[index].current_underscan.height = 0;
		set->path_data[index].flags.all = 0;
		/* TODO: set->path_data[index].viewport_adjustment */

		/* TODO: viewport_adjustment
		uint32_t i = 0;
		for (i = 0; i < HW_MAX_NUM_VIEW_PORTS; i++) {

		}
		*/

		if (data != NULL) {
			set->path_data[index].ws_stereo_state =
					data->ws_stereo_state;
			set->path_data[index].gtc_group = data->gtc_group;
			set->path_data[index].display_state.OUTPUT_ENABLED =
					data->display_state.OUTPUT_ENABLED;
			set->path_data[index].display_state.OUTPUT_BLANKED =
					data->display_state.OUTPUT_BLANKED;
		} else {
			set->path_data[index].ws_stereo_state =
					WS_STEREO_STATE_INACTIVE;
			set->path_data[index].gtc_group = GTC_GROUP_DISABLED;
			set->path_data[index].display_state.OUTPUT_ENABLED = 0;
			set->path_data[index].display_state.OUTPUT_BLANKED = 0;
		}

		result = dal_vector_construct(
			&set->plane_configs[index],
			preallocate_planes_num,
			sizeof(struct plane_config));

	}

	return result;
}

/* Get the data at index */
struct active_path_data *dal_pms_with_data_get_path_data_at_index(
		struct path_mode_set_with_data *set,
		uint32_t index)
{
	if (index < set->base.count)
		return &set->path_data[index];
	return NULL;
}

/* Get the data for the given display index in the set*/
struct active_path_data *dal_pms_with_data_get_path_data_for_display_index(
		struct path_mode_set_with_data *set,
		uint32_t index)
{
	uint32_t i;

	for (i = 0; i < set->base.count; i++) {
		if (set->base.path_mode_set[i].display_path_index == index)
			return &set->path_data[i];
	}

	return NULL;
}

/* Get mode timing at index */
const struct path_mode *dal_pms_with_data_get_path_mode_at_index(
		struct path_mode_set_with_data *set,
		uint32_t index)
{
	return dal_pms_get_path_mode_at_index(&set->base, index);
}

/* Get path mode for the given display index in the set */
const struct path_mode *
dal_pms_with_data_get_path_mode_for_display_index(
	const struct path_mode_set_with_data *set,
	uint32_t index)
{
	return dal_pms_get_path_mode_for_display_index(&set->base, index);
}

bool dal_pms_with_data_remove_path_mode_at_index(
	struct path_mode_set_with_data *set_with_data,
	uint32_t index)
{
	if (dal_pms_remove_path_mode_at_index(&set_with_data->base, index)) {
		uint32_t i;

		for (i = index; i < set_with_data->base.count; i++) {
			set_with_data->mode_timing[i] =
					set_with_data->mode_timing[i + 1];

			set_with_data->base.path_mode_set[i].mode_timing =
					&set_with_data->mode_timing[i];

			set_with_data->path_data[i] =
					set_with_data->path_data[i + 1];
			dal_vector_destruct(&set_with_data->plane_configs[i]);
			set_with_data->plane_configs[i] =
				set_with_data->plane_configs[i + 1];
		}

		/* this is the case when we do not enter loop, so destructor
		 * should be called */
		if (index == set_with_data->base.count)
			dal_vector_destruct(
				&set_with_data->plane_configs[index]);
	} else
		return false;

	return true;
}

bool dal_pms_with_data_remove_path_mode_for_display_index(
	struct path_mode_set_with_data *set_with_data,
	uint32_t index)
{
	uint32_t i;

	for (i = 0; i < set_with_data->base.count; i++) {
		if (set_with_data->base.path_mode_set[i].display_path_index == index)
			return dal_pms_with_data_remove_path_mode_at_index(
					set_with_data,
					i);
	}

	return false;
}

uint32_t dal_pms_with_data_get_path_mode_num(
	const struct path_mode_set_with_data *set)
{
	return dal_pms_get_path_mode_num(&set->base);
}

static bool get_path_mode_index(
	struct path_mode_set_with_data *set,
	uint32_t display_index,
	uint32_t *index)
{
	uint32_t i;

	for (i = 0; i < set->base.count; ++i) {
		if (set->base.path_mode_set[i].display_path_index ==
			display_index) {
			*index = i;
			return true;
		}
	}

	return false;
}

DAL_VECTOR_APPEND(plane_configs, const struct plane_config *)

void dal_pms_with_data_add_plane_configs(
	struct path_mode_set_with_data *set,
	uint32_t display_index,
	const struct plane_config *configs,
	uint32_t planes_num)
{
	uint32_t i;
	uint32_t index;

	if (!get_path_mode_index(set, display_index, &index))
		return;

	for (i = 0; i < planes_num; ++i)
		plane_configs_vector_append(
			&set->plane_configs[index],
			&configs[i]);
}

struct vector *dal_pms_with_data_get_plane_configs(
	struct path_mode_set_with_data *set,
	uint32_t display_index)
{
	uint32_t index;

	if (!get_path_mode_index(set, display_index, &index))
		return NULL;

	return &set->plane_configs[index];
}

void dal_pms_with_data_clear_plane_configs(
	struct path_mode_set_with_data *set,
	uint32_t display_index)
{
	uint32_t index;

	if (!get_path_mode_index(set, display_index, &index))
		return;

	dal_vector_clear(&set->plane_configs[index]);
}

static void destruct(struct path_mode_set_with_data *set)
{
	uint32_t i;

	for (i = 0; i < dal_pms_get_path_mode_num(&set->base); ++i)
		dal_vector_destruct(&set->plane_configs[i]);
}

void dal_pms_with_data_destroy(struct path_mode_set_with_data **set)
{
	if (!set || !*set)
		return;

	destruct(*set);
	dal_free(*set);
	*set = NULL;
}
