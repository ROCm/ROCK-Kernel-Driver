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

#ifndef __DAL_SCALER_TYPES_H__
#define __DAL_SCALER_TYPES_H__

#include "dc_types.h"

struct init_int_and_frac {
	uint32_t integer;
	uint32_t fraction;
};

struct scl_ratios_inits {
	uint32_t h_int_scale_ratio;
	uint32_t v_int_scale_ratio;
	struct init_int_and_frac h_init;
	struct init_int_and_frac v_init;
};

/* same as Hw register */
enum filter_type {
	FILTER_TYPE_V_LOW_PASS	= 0x0,
	FILTER_TYPE_V_HIGH_PASS	= 0x1,
	FILTER_TYPE_H_LUMA	= 0x2,
	FILTER_TYPE_H_CHROMA	= 0x3
};

enum ram_filter_type {
	FILTER_TYPE_RGB_Y_VERTICAL	= 0, /* 0 - RGB/Y Vertical filter */
	FILTER_TYPE_CBCR_VERTICAL	= 1, /* 1 - CbCr  Vertical filter */
	FILTER_TYPE_RGB_Y_HORIZONTAL	= 2, /* 1 - RGB/Y Horizontal filter */
	FILTER_TYPE_CBCR_HORIZONTAL	= 3, /* 3 - CbCr  Horizontal filter */
	FILTER_TYPE_ALPHA_VERTICAL	= 4, /* 4 - Alpha Vertical filter. */
	FILTER_TYPE_ALPHA_HORIZONTAL	= 5, /* 5 - Alpha Horizontal filter. */
};

#endif
