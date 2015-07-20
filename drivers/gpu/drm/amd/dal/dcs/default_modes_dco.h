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

#ifndef __DEFAULT_MODES_DCO_H__
#define __DEFAULT_MODES_DCO_H__

struct dcs_mode_timing_list;
struct timing_service;

bool dal_default_modes_dco_add_mode_timing(
	struct dcs_mode_timing_list *list,
	struct timing_service *ts);

bool dal_default_modes_dco_multi_sync_dco_add_mode_timing(
	struct dcs_mode_timing_list *list,
	struct timing_service *ts);

bool dal_default_wireless_dco_add_mode_timing(
	struct dcs_mode_timing_list *list,
	struct timing_service *ts);

#endif /* __DEFAULT_MODES_DCO_H__ */
