/*
 * Copyright 2012-14 Advanced Micro Devices, Inc.
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

#include "dal_services.h"

#include "include/logger_interface.h"

#include "dc_clock_gating_dce110.h"

/******************************************************************************
 * Macro definitions
 *****************************************************************************/

#define NOT_IMPLEMENTED() DAL_LOGGER_NOT_IMPL(LOG_MINOR_COMPONENT_GPU, \
		"%s:%s()\n", __FILE__, __func__);

/******************************************************************************
 * static functions
 *****************************************************************************/

static void enable_hw_base_light_sleep(struct dal_context *dal_context)
{
	NOT_IMPLEMENTED();
}

static void disable_sw_manual_control_light_sleep(
		struct dal_context *dal_context)
{
	NOT_IMPLEMENTED();
}

static void enable_sw_manual_control_light_sleep(
		struct dal_context *dal_context)
{
	NOT_IMPLEMENTED();
}

/******************************************************************************
 * public functions
 *****************************************************************************/

void dal_dc_clock_gating_dce110_power_up(struct dal_context *dal_context)
{
	enable_hw_base_light_sleep(dal_context);
	disable_sw_manual_control_light_sleep(dal_context);
}

void dal_dc_clock_gating_dce110_power_down(struct dal_context *dal_context)
{
	enable_sw_manual_control_light_sleep(dal_context);
}
