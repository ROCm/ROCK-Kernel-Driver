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

#include "hw_sync_control.h"

bool dal_hw_sync_control_construct_base(
	struct hw_sync_control *hw_sync_control)
{
	return true;
}

enum hwss_result dal_hw_sync_control_inter_path_synchronize(
	struct hw_sync_control *sync_control,
	struct hw_path_mode_set *path_mode_set)
{
	/*Check if synchronization needed*/
	uint32_t i = 0;
	bool sync_needed = false;
	uint32_t size = dal_hw_path_mode_set_get_paths_number(path_mode_set);

	for (i = 0; i < size; ++i) {

		const struct hw_path_mode *path_mode =
			dal_hw_path_mode_set_get_path_by_index(
				path_mode_set, i);

		if (path_mode->sync_info.sync_request ==
			HW_SYNC_REQUEST_SET_INTERPATH) {
			sync_needed = true;
			break;
		}
	}

	/* Switch DP clock to active PLL if
	 * applicable for timing synchronization.*/
	sync_control->funcs->switch_dp_clock_source(
			sync_control, path_mode_set);

	/* Perform CRTC synchronization*/
	if (sync_needed) {
		struct hw_resync_flags resync_flags = { false, false, false };

		resync_flags.INTERPATH = true;
		resync_flags.NOTIFY_SYNCED = true;
		return sync_control->funcs->resync_display_paths(
			sync_control, path_mode_set, resync_flags);
	}

	return HWSS_RESULT_OK;
}

bool dal_hw_sync_control_resync_required(
	enum hw_sync_request sync_request,
	struct hw_resync_flags resync_flags)
{
	bool required = false;

	switch (sync_request) {
	case HW_SYNC_REQUEST_SET_INTERPATH:
		required = resync_flags.INTERPATH;
		break;

	case HW_SYNC_REQUEST_SET_GL_SYNC_GENLOCK:
	case HW_SYNC_REQUEST_SET_GL_SYNC_FREE_RUN:
	case HW_SYNC_REQUEST_SET_GL_SYNC_SHADOW:
	case HW_SYNC_REQUEST_RESYNC_GLSYNC:
		required = resync_flags.GL_SYNC;
		break;

	default:
		break;
	}

	return required;
}

void dal_hw_sync_control_notify_sync_established(
	struct hw_sync_control *sync_control,
	struct display_path *display_path,
	struct hw_resync_flags resync_flags)
{

}

