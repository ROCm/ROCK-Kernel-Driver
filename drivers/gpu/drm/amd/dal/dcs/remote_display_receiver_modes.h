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

#ifndef __DAL_REMOTE_DISPLAY_RECEIVER_H
#define __DAL_REMOTE_DISPLAY_RECEIVER_H

#include "include/dcs_types.h"

struct remote_display_receiver_modes;

struct remote_display_receiver_modes_init_data {
	struct timing_service *ts;
	bool supports_miracast;
};


struct remote_display_receiver_modes *dal_remote_display_receiver_modes_create(
		struct remote_display_receiver_modes_init_data *rdrm_init_data);

void dal_remote_display_receiver_modes_destroy(
		struct remote_display_receiver_modes **rdrm);


void dal_remote_display_receiver_set_capabilities(
		struct remote_display_receiver_modes *rdrm,
		const struct dal_remote_display_receiver_capability *rdrm_caps);

void dal_remote_display_receiver_clear_capabilities(
		struct remote_display_receiver_modes *rdrm);

bool dal_rdr_get_supported_cea_audio_mode(
	struct remote_display_receiver_modes *rdrm,
	const struct cea_audio_mode *const cea_audio_mode,
	struct cea_audio_mode *const actual_cea_audio_mode);

bool dal_remote_display_receiver_get_supported_mode_timing(
		struct remote_display_receiver_modes *rdrm,
		struct dcs_mode_timing_list *list);

#endif /* __DAL_REMOTE_DISPLAY_RECEIVER_H */
