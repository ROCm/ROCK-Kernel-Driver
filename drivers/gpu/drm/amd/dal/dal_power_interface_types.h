/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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

#ifndef __DAL_POWER_INTERFACE_TYPES_H__
#define __DAL_POWER_INTERFACE_TYPES_H__

enum dal_to_power_clocks_state {
	PP_CLOCKS_STATE_INVALID,
	PP_CLOCKS_STATE_ULTRA_LOW,
	PP_CLOCKS_STATE_LOW,
	PP_CLOCKS_STATE_NOMINAL,
	PP_CLOCKS_STATE_PERFORMANCE
};

/* clocks in khz */
struct dal_to_power_info {
	enum dal_to_power_clocks_state required_clock;
	uint32_t min_sclk;
	uint32_t min_mclk;
	uint32_t min_deep_sleep_sclk;
};

/* clocks in khz */
struct power_to_dal_info {
	uint32_t min_sclk;
	uint32_t max_sclk;
	uint32_t min_mclk;
	uint32_t max_mclk;
};

/* clocks in khz */
struct dal_system_clock_range {
	uint32_t min_sclk;
	uint32_t max_sclk;

	uint32_t min_mclk;
	uint32_t max_mclk;

	uint32_t min_dclk;
	uint32_t max_dclk;

	/* Wireless Display */
	uint32_t min_eclk;
	uint32_t max_eclk;
};

/* clocks in khz */
struct dal_to_power_dclk {
	uint32_t optimal; /* input: best optimizes for stutter efficiency */
	uint32_t minimal; /* input: the lowest clk that DAL can support */
	uint32_t established; /* output: the actually set one */
};

#endif /* __DAL_POWER_INTERFACE_TYPES_H__ */
