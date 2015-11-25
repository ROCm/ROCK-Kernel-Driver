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

#ifndef __DAL_TIMING_LIST_QUERY_INTERFACE_H__
#define __DAL_TIMING_LIST_QUERY_INTERFACE_H__

/* External dependencies */
#include "include/dcs_interface.h"

/* Forward declarations */
struct dal;
struct dal_timing_list_query;

enum timing_support_level {
	TIMING_SUPPORT_LEVEL_UNDEFINED,
	/* assumed to be guaranteed supported by display,
	* usually one timing is marked as native */
	TIMING_SUPPORT_LEVEL_NATIVE,
	/* user wants DAL to drive this timing as if Display supports it */
	TIMING_SUPPORT_LEVEL_GUARANTEED,
	/* user wants DAL to drive this timing even if display
	* may not support it */
	TIMING_SUPPORT_LEVEL_NOT_GUARANTEED
};

struct timing_list_query_init_data {
	struct dal *dal; /* an instance of DAL */
	struct timing_service *timing_srv;
	struct dcs *dcs;
	uint32_t display_index;
};

struct dal_timing_list_query *dal_timing_list_query_create(
		struct timing_list_query_init_data *init_data);

void dal_timing_list_query_destroy(struct dal_timing_list_query **tlsq);

/* Get count of mode timings in the list. */
uint32_t dal_timing_list_query_get_mode_timing_count(
	const struct dal_timing_list_query *tlsq);

const struct dc_mode_timing *dal_timing_list_query_get_mode_timing_at_index(
	const struct dal_timing_list_query *tlsq,
	uint32_t index);


#endif /* __DAL_TIMING_LIST_QUERY_INTERFACE_H__ */
