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

#ifndef __DM_SERVICES_TYPES_H__
#define __DM_SERVICES_TYPES_H__

#if defined __KERNEL__

#include <asm/byteorder.h>
#include <linux/types.h>
#include <drm/drmP.h>

#include "cgs_linux.h"

#if defined(__BIG_ENDIAN) && !defined(BIGENDIAN_CPU)
#define BIGENDIAN_CPU
#elif defined(__LITTLE_ENDIAN) && !defined(LITTLEENDIAN_CPU)
#define LITTLEENDIAN_CPU
#endif

#undef READ
#undef WRITE
#undef FRAME_SIZE

#define dm_output_to_console(fmt, ...) DRM_INFO(fmt, ##__VA_ARGS__)

#define dm_error(fmt, ...) DRM_ERROR(fmt, ##__VA_ARGS__)

#define dm_debug(fmt, ...) DRM_DEBUG_KMS(fmt, ##__VA_ARGS__)

#define dm_vlog(fmt, args) vprintk(fmt, args)

#define dm_min(x, y) min(x, y)
#define dm_max(x, y) max(x, y)

#elif defined BUILD_DAL_TEST

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include <stdio.h>

#include <stdarg.h>

#include "cgs_linux.h"

#define LONG_MAX	((long)(~0UL>>1))
#define LONG_MIN	(-LONG_MAX - 1)
#define LLONG_MAX	((long long)(~0ULL>>1))
#define LLONG_MIN	(-LLONG_MAX - 1)
#define UINT_MAX	(~0U)

typedef _Bool bool;
enum { false, true };

#ifndef NULL
#define NULL ((void *)0)
#endif

#define LITTLEENDIAN_CPU 1

#include <test_context.h>

#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)

#define container_of(ptr, type, member) \
	((type *)((char *)(ptr) - offsetof(type, member)))

#define dal_test_not_implemented() \
	printf("[DAL_TEST_NOT_IMPL]:%s\n", __func__)

#define dm_output_to_console(fmt, ...) do { \
	printf("[DAL_LOG]" fmt, ##__VA_ARGS__); } \
	while (false)

#define dm_error(fmt, ...) printf("[DAL_ERROR]" fmt, ##__VA_ARGS__)

#define dm_output_to_console(fmt, ...) do { \
			printf("[DAL_LOG]" fmt, ##__VA_ARGS__); } \
				while (false)

#define dm_debug(fmt, ...) printf("[DAL_DBG]" fmt, ##__VA_ARGS__)

#define dm_vlog(fmt, args) vprintf(fmt, args)

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

#define dm_min(x, y) ({\
	typeof(x) _min1 = (x);\
	typeof(y) _min2 = (y);\
	(void) (&_min1 == &_min2);\
	_min1 < _min2 ? _min1 : _min2; })

#define dm_max(x, y) ({\
	typeof(x) _max1 = (x);\
	typeof(y) _max2 = (y);\
	(void) (&_max1 == &_max2);\
	_max1 > _max2 ? _max1 : _max2; })

/* division functions */

static inline int64_t div64_s64(int64_t x, int64_t y)
{
	return x / y;
}

static inline uint64_t div64_u64(uint64_t x, uint64_t y)
{
	return x / y;
}

static inline uint64_t div_u64(uint64_t x, uint32_t y)
{
	return x / y;
}

static inline uint64_t div64_u64_rem(uint64_t x, uint64_t y, uint64_t *rem)
{
	if (rem)
		*rem = x % y;
	return x / y;
}

static inline uint64_t div_u64_rem(uint64_t x, uint32_t y, uint32_t *rem)
{
	if (rem)
		*rem = x % y;
	return x / y;
}

#define cpu_to_le16(do_nothing) do_nothing

#define le16_to_cpu(do_nothing) do_nothing

#define cpu_to_le32(do_nothing) do_nothing

#define le32_to_cpu(do_nothing) do_nothing

#endif

#include "dc_types.h"

struct dm_pp_clock_range {
	int min_khz;
	int max_khz;
};

enum dm_pp_clocks_state {
	DM_PP_CLOCKS_STATE_INVALID,
	DM_PP_CLOCKS_STATE_ULTRA_LOW,
	DM_PP_CLOCKS_STATE_LOW,
	DM_PP_CLOCKS_STATE_NOMINAL,
	DM_PP_CLOCKS_STATE_PERFORMANCE,

	/* Starting from DCE11, Max 8 levels of DPM state supported. */
	DM_PP_CLOCKS_DPM_STATE_LEVEL_INVALID = DM_PP_CLOCKS_STATE_INVALID,
	DM_PP_CLOCKS_DPM_STATE_LEVEL_0 = DM_PP_CLOCKS_STATE_ULTRA_LOW,
	DM_PP_CLOCKS_DPM_STATE_LEVEL_1 = DM_PP_CLOCKS_STATE_LOW,
	DM_PP_CLOCKS_DPM_STATE_LEVEL_2 = DM_PP_CLOCKS_STATE_NOMINAL,
	/* to be backward compatible */
	DM_PP_CLOCKS_DPM_STATE_LEVEL_3 = DM_PP_CLOCKS_STATE_PERFORMANCE,
	DM_PP_CLOCKS_DPM_STATE_LEVEL_4 = DM_PP_CLOCKS_DPM_STATE_LEVEL_3 + 1,
	DM_PP_CLOCKS_DPM_STATE_LEVEL_5 = DM_PP_CLOCKS_DPM_STATE_LEVEL_4 + 1,
	DM_PP_CLOCKS_DPM_STATE_LEVEL_6 = DM_PP_CLOCKS_DPM_STATE_LEVEL_5 + 1,
	DM_PP_CLOCKS_DPM_STATE_LEVEL_7 = DM_PP_CLOCKS_DPM_STATE_LEVEL_6 + 1,
};

struct dm_pp_gpu_clock_range {
	enum dm_pp_clocks_state clock_state;
	struct dm_pp_clock_range sclk;
	struct dm_pp_clock_range mclk;
	struct dm_pp_clock_range eclk;
	struct dm_pp_clock_range dclk;
};

enum dm_pp_clock_type {
	DM_PP_CLOCK_TYPE_DISPLAY_CLK = 1,
	DM_PP_CLOCK_TYPE_ENGINE_CLK, /* System clock */
	DM_PP_CLOCK_TYPE_MEMORY_CLK
};

#define DC_DECODE_PP_CLOCK_TYPE(clk_type) \
	(clk_type) == DM_PP_CLOCK_TYPE_DISPLAY_CLK ? "Display" : \
	(clk_type) == DM_PP_CLOCK_TYPE_ENGINE_CLK ? "Engine" : \
	(clk_type) == DM_PP_CLOCK_TYPE_MEMORY_CLK ? "Memory" : "Invalid"

#define DM_PP_MAX_CLOCK_LEVELS 8

struct dm_pp_clock_levels {
	uint32_t num_levels;
	uint32_t clocks_in_khz[DM_PP_MAX_CLOCK_LEVELS];

	/* TODO: add latency for polaris11
	 * do we need to know invalid (unsustainable boost) level for watermark
	 * programming? if not we can just report less elements in array
	 */
};

struct dm_pp_single_disp_config {
	enum signal_type signal;
	uint8_t transmitter;
	uint8_t ddi_channel_mapping;
	uint8_t pipe_idx;
	uint32_t src_height;
	uint32_t src_width;
	uint32_t v_refresh;
	uint32_t sym_clock; /* HDMI only */
	struct dc_link_settings link_settings; /* DP only */
};

#define MAX_DISPLAY_CONFIGS 6

struct dm_pp_display_configuration {
	bool nb_pstate_switch_disable;/* controls NB PState switch */
	bool cpu_cc6_disable; /* controls CPU CState switch ( on or off) */
	bool cpu_pstate_disable;
	uint32_t cpu_pstate_separation_time;

	uint32_t min_memory_clock_khz;
	uint32_t min_engine_clock_khz;
	uint32_t min_engine_clock_deep_sleep_khz;

	uint32_t avail_mclk_switch_time_us;
	uint32_t avail_mclk_switch_time_in_disp_active_us;

	uint32_t disp_clk_khz;

	bool all_displays_in_sync;

	uint8_t display_count;
	struct dm_pp_single_disp_config disp_configs[MAX_DISPLAY_CONFIGS];

	/*Controller Index of primary display - used in MCLK SMC switching hang
	 * SW Workaround*/
	uint8_t crtc_index;
	/*htotal*1000/pixelclk - used in MCLK SMC switching hang SW Workaround*/
	uint32_t line_time_in_us;
};

#endif
