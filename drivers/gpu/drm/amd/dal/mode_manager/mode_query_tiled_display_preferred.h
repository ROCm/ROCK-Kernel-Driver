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

#ifndef __DAL_MODE_QUERY_TILED_DISPLAY_PREFERRED_H__
#define __DAL_MODE_QUERY_TILED_DISPLAY_PREFERRED_H__

#include "mode_query_no_pan.h"

enum {
	MAX_TILED_DISPLAY_PREFERRED_MODES = 3,
};

struct mode_query_tiled_display_preferred {
	struct mode_query base;
	uint32_t tiled_preferred_mode_count;
	struct view render_modes_enumerated[MAX_TILED_DISPLAY_PREFERRED_MODES];
};

struct mode_query *dal_mode_query_tiled_display_preferred_create(
		struct mode_query_init_data *mq_init_data);

#endif /* __DAL_MODE_QUERY_TILED_DISPLAY_PREFERRED_H__ */
