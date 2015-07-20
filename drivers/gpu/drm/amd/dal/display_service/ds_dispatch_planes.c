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

/* Include */
#include "dal_services.h"

#include "include/set_mode_interface.h"
#include "include/hw_sequencer_interface.h"
#include "include/hw_path_mode_set_interface.h"
#include "include/topology_mgr_interface.h"

#include "ds_dispatch.h"


enum ds_return dal_ds_dispatch_setup_plane_configurations(
	struct ds_dispatch *ds,
	uint32_t display_index,
	uint32_t planes_num,
	const struct plane_config *configs)
{
	enum ds_return result = DS_SUCCESS;
	uint32_t path_num = dal_pms_with_data_get_path_mode_num(ds->set);
	struct hw_path_mode_set *hw_mode_set = dal_hw_path_mode_set_create();

	if (!hw_mode_set) {
		BREAK_TO_DEBUGGER();
		return DS_ERROR;
	}

	/* Convert path mode set into HW path mode set for HWSS
	 * This is needed for dal_hw_sequencer_prepare_to_release_planes
	 * to get  hw_mode_set*/
	if (false == dal_ds_dispatch_build_hw_path_set(
			ds,
			path_num,
			dal_pms_with_data_get_path_mode_at_index(
					ds->set,
					0),
			hw_mode_set,
			BUILD_PATH_SET_REASON_SET_MODE,
			NULL)) {
		/* error */
		result = DS_ERROR;
	}

	/* HW programming */
	if (DS_SUCCESS == result) {
		/* clean-up part */
		dal_hw_sequencer_prepare_to_release_planes(
			ds->hwss,
			hw_mode_set,
			display_index);
		dal_tm_release_plane_resources(
			ds->tm,
			display_index);
		dal_pms_with_data_clear_plane_configs(ds->set, display_index);

		dal_tm_acquire_plane_resources(
			ds->tm,
			display_index,
			planes_num,
			configs);

		dal_pms_with_data_add_plane_configs(
			ds->set,
			display_index,
			configs,
			planes_num);

		/*Because configs may be update, hw_mode_set needs
		 *to be re-built*/
		if (false == dal_ds_dispatch_build_hw_path_set(
				ds,
				path_num,
				dal_pms_with_data_get_path_mode_at_index(
						ds->set,
						0),
				hw_mode_set,
				BUILD_PATH_SET_REASON_GET_ACTIVE_PATHS,
				NULL)) {
			/* error */
			result = DS_ERROR;
		}

		if (DS_SUCCESS == result)
			if (dal_hw_sequencer_set_plane_config(
					ds->hwss,
					hw_mode_set,
					display_index) != HWSS_RESULT_OK)
				result = DS_ERROR;
	}

	dal_ds_dispatch_destroy_hw_path_set(hw_mode_set);

	return result;
}

bool dal_ds_dispatch_validate_plane_configurations(
	struct ds_dispatch *ds,
	uint32_t num_planes,
	const struct plane_config *pl_configs,
	bool *supported)
{
	 uint32_t i;

	/* For now, we just return true for all planes */
	for (i = 0; i < num_planes; i++)
		supported[i] = true;

	return true;
}
