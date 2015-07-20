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

#ifndef __DAL_MODE_QUERY_INTERFACE_H__
#define __DAL_MODE_QUERY_INTERFACE_H__

#include "include/set_mode_types.h"
#include "include/mode_manager_types.h"

enum query_option {
	QUERY_OPTION_ALLOW_PAN,
	QUERY_OPTION_ALLOW_PAN_NO_VIEW_RESTRICTION,
	QUERY_OPTION_PAN_ON_LIMITED_RESOLUTION_DISP_PATH,
	QUERY_OPTION_NO_PAN,
	QUERY_OPTION_NO_PAN_NO_DISPLAY_VIEW_RESTRICTION,
	QUERY_OPTION_3D_LIMITED_CANDIDATES,
	QUERY_OPTION_TILED_DISPLAY_PREFERRED,
	QUERY_OPTION_MAX,
};

struct topology {
	uint32_t disp_path_num;
	uint32_t display_index[MAX_COFUNC_PATH];
};

struct path_mode;
struct mode_query;

bool dal_mode_query_pin_path_mode(
		struct mode_query *mq,
		const struct path_mode *path_mode);

const struct render_mode *dal_mode_query_get_current_render_mode(
		const struct mode_query *mq);

const struct stereo_3d_view *dal_mode_query_get_current_3d_view(
		const struct mode_query *mq);

const struct refresh_rate *dal_mode_query_get_current_refresh_rate(
		const struct mode_query *mq);

const struct path_mode_set *dal_mode_query_get_current_path_mode_set(
		const struct mode_query *mq);

bool dal_mode_query_select_first(struct mode_query *mq);
bool dal_mode_query_select_next_render_mode(struct mode_query *mq);

bool dal_mode_query_select_render_mode(struct mode_query *mq,
		const struct render_mode *render_mode);

bool dal_mode_query_select_next_view_3d_format(struct mode_query *mq);
bool dal_mode_query_select_view_3d_format(
		struct mode_query *mq,
		enum view_3d_format format);

bool dal_mode_query_select_refresh_rate(struct mode_query *mq,
		const struct refresh_rate *refresh_rate);

bool dal_mode_query_select_refresh_rate_ex(struct mode_query *mq,
		uint32_t refresh_rate,
		bool interlaced);

bool dal_mode_query_select_next_scaling(struct mode_query *mq);

bool dal_mode_query_select_next_refresh_rate(struct mode_query *mq);

bool dal_mode_query_base_select_next_scaling(struct mode_query *mq);

void dal_mode_query_destroy(struct mode_query **mq);

#endif /* __DAL_MODE_QUERY_INTERFACE_H__ */
