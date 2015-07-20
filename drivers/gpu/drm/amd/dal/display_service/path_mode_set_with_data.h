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

#ifndef __DAL_PATH_MODE_SET_WITH_DATA_H__
#define __DAL_PATH_MODE_SET_WITH_DATA_H__

#include "include/set_mode_types.h"
#include "include/display_service_types.h"
#include "include/path_mode_set_interface.h"
#include "include/timing_service_types.h"

/* Data flags */
struct active_path_data {
	union active_data_flags {
		struct {
			uint32_t EXISTING:1;
			uint32_t REPROGRAM_HW:1;
			uint32_t ENABLE_HW:1;
			uint32_t DISABLE_HW:1;
			uint32_t KEEP_VCC_ON_DISABLE_HW:1;
			uint32_t RESYNC_HW:1;
			uint32_t POST_ACTION_DISPLAY_ON:1;
			uint32_t TURN_OFF_BACK_END_AND_RX:1;
			uint32_t VIEW_RES_CHANGED:1;
			uint32_t TIMING_CHANGED:1;
			uint32_t PIXEL_ENCODING_CHANGED:1;
			uint32_t GAMUT_CHANGED:1;
			uint32_t REDUCE_BLANK_ON:1;
			uint32_t PENDING_DISPLAY_STEREO:1;
			uint32_t SYNC_TIMING_SERVER:1;
			uint32_t DISPLAY_PATH_INVALID:1;
			uint32_t NO_DEFAULT_DOWN_SCALING:1;
			uint32_t AUDIO_BANDWIDTH_CHANGED:1;
			uint32_t SKIP_ENABLE:1;
			uint32_t SKIP_RESET_HW:1;
		} bits;

		uint32_t all;
	} flags;

	struct display_state {
		uint32_t OUTPUT_ENABLED:1;
		uint32_t OUTPUT_BLANKED:1;
	} display_state;

	struct ds_underscan_desc current_underscan;
	enum ws_stereo_state ws_stereo_state;
	enum gtc_group gtc_group;
	struct hw_get_viewport_x_adjustments *viewport_adjustment;
	struct ranged_timing_preference_flags ranged_timing_pref_flags;
};

struct path_mode_set_with_data;

/* Create the set for path mode with data */
struct path_mode_set_with_data *dal_pms_with_data_create(void);

void dal_pms_with_data_destroy(struct path_mode_set_with_data **set);

/* Add path mode with data into the set */
bool dal_pms_with_data_add_path_mode_with_data(
		struct path_mode_set_with_data *set,
		const struct path_mode *mode,
		const struct active_path_data *data);

/* Get the data at index */
struct active_path_data *dal_pms_with_data_get_path_data_at_index(
		struct path_mode_set_with_data *set,
		uint32_t index);

/* Get the data for the given display index in the set*/
struct active_path_data *dal_pms_with_data_get_path_data_for_display_index(
		struct path_mode_set_with_data *set,
		uint32_t index);

/* Get path mode at index */
const struct path_mode *dal_pms_with_data_get_path_mode_at_index(
	struct path_mode_set_with_data *set,
	uint32_t index);

/* Get path mode for the given display index in the set */
const struct path_mode *
dal_pms_with_data_get_path_mode_for_display_index(
	const struct path_mode_set_with_data *set,
	uint32_t index);

uint32_t dal_pms_with_data_get_path_mode_num(
	const struct path_mode_set_with_data *set);

bool dal_pms_with_data_remove_path_mode_at_index(
	struct path_mode_set_with_data *set_with_data,
	uint32_t index);

bool dal_pms_with_data_remove_path_mode_for_display_index(
	struct path_mode_set_with_data *set_with_data,
	uint32_t index);

void dal_pms_with_data_add_plane_configs(
	struct path_mode_set_with_data *set,
	uint32_t display_index,
	const struct plane_config *configs,
	uint32_t planes_num);

struct vector *dal_pms_with_data_get_plane_configs(
	struct path_mode_set_with_data *set,
	uint32_t display_index);

void dal_pms_with_data_clear_plane_configs(
	struct path_mode_set_with_data *set,
	uint32_t display_index);

#endif
