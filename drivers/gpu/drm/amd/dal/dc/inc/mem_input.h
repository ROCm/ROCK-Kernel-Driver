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
#ifndef __DAL_MEM_INPUT_H__
#define __DAL_MEM_INPUT_H__

#include "include/plane_types.h"
#include "include/grph_object_id.h"
#include "dc.h"

struct mem_input {
	struct dc_context *ctx;
	uint32_t inst;
};

enum stutter_mode_type {
	STUTTER_MODE_LEGACY = 0X00000001,
	STUTTER_MODE_ENHANCED = 0X00000002,
	STUTTER_MODE_FID_NBP_STATE = 0X00000004,
	STUTTER_MODE_WATERMARK_NBP_STATE = 0X00000008,
	STUTTER_MODE_SINGLE_DISPLAY_MODEL = 0X00000010,
	STUTTER_MODE_MIXED_DISPLAY_MODEL = 0X00000020,
	STUTTER_MODE_DUAL_DMIF_BUFFER = 0X00000040,
	STUTTER_MODE_NO_DMIF_BUFFER_ALLOCATION = 0X00000080,
	STUTTER_MODE_NO_ADVANCED_REQUEST = 0X00000100,
	STUTTER_MODE_NO_LB_RESET = 0X00000200,
	STUTTER_MODE_DISABLED = 0X00000400,
	STUTTER_MODE_AGGRESSIVE_MARKS = 0X00000800,
	STUTTER_MODE_URGENCY = 0X00001000,
	STUTTER_MODE_QUAD_DMIF_BUFFER = 0X00002000,
	STUTTER_MODE_NOT_USED = 0X00008000
};

#endif
