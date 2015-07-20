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

#ifndef __DAL_HW_SYNC_CONTROL_H__
#define __DAL_HW_SYNC_CONTROL_H__

#include "hw_sequencer.h"

struct hw_resync_flags {
	bool INTERPATH:1;
	bool GL_SYNC:1;
	bool NOTIFY_SYNCED:1;
};

struct hw_sync_control;
struct hw_path_mode_set;

struct hw_sync_control_funcs {
	bool (*switch_dp_clock_source)(
		struct hw_sync_control *hw_sync_control,
		struct hw_path_mode_set *path_mode_set);
	enum hwss_result (*resync_display_paths)(
		struct hw_sync_control *hw_sync_control,
		struct hw_path_mode_set *path_mode_set,
		struct hw_resync_flags resync_flags);
	void (*destroy)(struct hw_sync_control **cntrl);
};

struct hw_sync_control {
	const struct hw_sync_control_funcs *funcs;
	struct adapter_service *as;
};

bool dal_hw_sync_control_construct_base(
	struct hw_sync_control *hw_sync_control);

enum hwss_result dal_hw_sync_control_inter_path_synchronize(
	struct hw_sync_control *sync_control,
	struct hw_path_mode_set *path_mode_set);

bool dal_hw_sync_control_resync_required(
	enum hw_sync_request sync_request,
	struct hw_resync_flags resync_flags);

void dal_hw_sync_control_notify_sync_established(
	struct hw_sync_control *sync_control,
	struct display_path *display_path,
	struct hw_resync_flags resync_flags);


#endif
