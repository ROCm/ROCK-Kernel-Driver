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

#ifndef __DAL_TIMING_SERVICE_H__
#define __DAL_TIMING_SERVICE_H__

#include "include/vector.h"
#include "include/timing_service_interface.h"
#include "mode_timing_list.h"
#include "mode_timing_filter.h"

#define TIMING_SOURCES_NUM	TIMING_STANDARD_MAX

struct timing_service {
	struct dal_context *dal_context;
	struct mode_timing_source_funcs *timing_sources[TIMING_SOURCES_NUM];
	struct mode_timing_filter mode_timing_filter_dynamic_validation;
/* NOTE in DAL2 there were an array
 * with the only static validation filter element */
	struct mode_timing_filter mode_timing_filter_static_validation;
	struct vector mtl_vec;
	struct default_mode_list *default_mode_list;
};

#endif /*__DAL_TIMING_SERVICE_H__*/
