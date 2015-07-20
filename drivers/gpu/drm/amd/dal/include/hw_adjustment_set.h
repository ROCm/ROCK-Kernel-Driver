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

#ifndef __DAL_HW_ADJUSTMENT_SET_H__
#define __DAL_HW_ADJUSTMENT_SET_H__

#include "include/hw_adjustment_types.h"

struct hw_adjustment_gamma_ramp;

struct hw_adjustment_set {
	struct hw_adjustment_gamma_ramp *gamma_ramp;
	struct hw_adjustment_deflicker *deflicker_filter;
	struct hw_adjustment_value *coherent;
	struct hw_adjustment_value *h_sync;
	struct hw_adjustment_value *v_sync;
	struct hw_adjustment_value *composite_sync;
	struct hw_adjustment_value *backlight;
	struct hw_adjustment_value *vb_level;
	struct hw_adjustment_color_control *color_control;
	union hw_adjustment_bit_depth_reduction *bit_depth;
};
/*
struct hw_adjustment *dal_adjustment_set_get_by_id(
	struct hw_adjustment_set *adjustment_set,
	enum hw_adjustment_id id);*/

#endif
