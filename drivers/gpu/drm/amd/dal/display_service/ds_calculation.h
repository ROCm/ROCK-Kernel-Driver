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

#ifndef __DAL_DS_CALCULATION_H__
#define __DAL_DS_CALCULATION_H__

/* Includes */
/* None */

struct hw_crtc_timing;
struct timing_limits;
struct ranged_timing_preference_flags;
struct display_path;

/* Adjust timing to the timing limits. */
void dal_ds_calculation_tuneup_timing(
		struct hw_crtc_timing *timing,
		const struct timing_limits *timing_limits);

/* Setup ranged timing parameters for features such as DRR or PSR. */
void dal_ds_calculation_setup_ranged_timing(
		struct hw_crtc_timing *timing,
		struct display_path *display_path,
		struct ranged_timing_preference_flags flags);

#endif /* __DAL_DS_CALCULATION_H__ */
