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

/**
 * This file defines external dependencies of Display Core.
 */

#ifndef __DC_SERVICES_H__
#define __DC_SERVICES_H__

/* TODO: remove when DC is complete. */
#include "dal_services_types.h"
#include "logger_interface.h"
#include "include/dal_types.h"
#include "irq_types.h"
#include "dal_power_interface_types.h"
#include "link_service_types.h"

/* if the pointer is not NULL, the allocated memory is zeroed */
void *dc_service_alloc(struct dc_context *ctx, uint32_t size);

void dc_service_free(struct dc_context *ctx, void *p);

void dc_service_memset(void *p, int32_t c, uint32_t count);

void dc_service_memmove(void *dst, const void *src, uint32_t size);

/* TODO: rename to dc_memcmp*/
int32_t dal_memcmp(const void *p1, const void *p2, uint32_t count);

/* TODO: remove when windows_dm will start registering for IRQs */
irq_handler_idx dc_service_register_interrupt(
	struct dc_context *ctx,
	struct dc_interrupt_params *int_params,
	interrupt_handler ih,
	void *handler_args);

/* TODO: remove when windows_dm will start registering for IRQs */
void dc_service_unregister_interrupt(
	struct dc_context *ctx,
	enum dc_irq_source irq_source,
	irq_handler_idx handler_idx);

/**************************************
 * Power Play (PP) interfaces
 **************************************/

/* DAL calls this function to notify PP about clocks it needs for the Mode Set.
 * This is done *before* it changes DCE clock.
 *
 * If required clock is higher than current, then PP will increase the voltage.
 *
 * If required clock is lower than current, then PP will defer reduction of
 * voltage until the call to dc_service_pp_post_dce_clock_change().
 *
 * \input - Contains clocks needed for Mode Set.
 *
 * \output - Contains clocks adjusted by PP which DAL should use for Mode Set.
 *		Valid only if function returns zero.
 *
 * \returns	true - call is successful
 *		false - call failed
 */
bool dc_service_pp_pre_dce_clock_change(
	struct dc_context *ctx,
	struct dal_to_power_info *input,
	struct power_to_dal_info *output);

struct dc_pp_single_disp_config
{
	enum signal_type signal;
	uint8_t transmitter;
	uint8_t ddi_channel_mapping;
	uint8_t pipe_idx;
	uint32_t src_height;
	uint32_t src_width;
	uint32_t v_refresh;
	uint32_t sym_clock; /* HDMI only */
	struct link_settings link_settings; /* DP only */
};

struct dc_pp_display_configuration {
	bool nb_pstate_switch_disable;/* controls NB PState switch */
	bool cpu_cc6_disable; /* controls CPU CState switch ( on or off) */
	bool cpu_pstate_disable;
	uint32_t cpu_pstate_separation_time;

	/* 10khz steps */
	uint32_t min_memory_clock_khz;
	uint32_t min_engine_clock_khz;
	uint32_t min_engine_clock_deep_sleep_khz;

	uint32_t avail_mclk_switch_time_us;
	uint32_t avail_mclk_switch_time_in_disp_active_us;

	uint32_t disp_clk_khz;

	bool all_displays_in_sync;

	uint8_t display_count;
	struct dc_pp_single_disp_config disp_configs[3];

	/*Controller Index of primary display - used in MCLK SMC switching hang
	 * SW Workaround*/
	uint8_t crtc_index;
	/*htotal*1000/pixelclk - used in MCLK SMC switching hang SW Workaround*/
	uint32_t line_time_in_us;
};

enum dc_pp_clocks_state {
	DC_PP_CLOCKS_STATE_INVALID = 0,
	DC_PP_CLOCKS_STATE_ULTRA_LOW,
	DC_PP_CLOCKS_STATE_LOW,
	DC_PP_CLOCKS_STATE_NOMINAL,
	DC_PP_CLOCKS_STATE_PERFORMANCE,

	/* Starting from DCE11, Max 8 levels of DPM state supported. */
	DC_PP_CLOCKS_DPM_STATE_LEVEL_INVALID = DC_PP_CLOCKS_STATE_INVALID,
	DC_PP_CLOCKS_DPM_STATE_LEVEL_0 = DC_PP_CLOCKS_STATE_ULTRA_LOW,
	DC_PP_CLOCKS_DPM_STATE_LEVEL_1 = DC_PP_CLOCKS_STATE_LOW,
	DC_PP_CLOCKS_DPM_STATE_LEVEL_2 = DC_PP_CLOCKS_STATE_NOMINAL,
	/* to be backward compatible */
	DC_PP_CLOCKS_DPM_STATE_LEVEL_3 = DC_PP_CLOCKS_STATE_PERFORMANCE,
	DC_PP_CLOCKS_DPM_STATE_LEVEL_4 = DC_PP_CLOCKS_DPM_STATE_LEVEL_3 + 1,
	DC_PP_CLOCKS_DPM_STATE_LEVEL_5 = DC_PP_CLOCKS_DPM_STATE_LEVEL_4 + 1,
	DC_PP_CLOCKS_DPM_STATE_LEVEL_6 = DC_PP_CLOCKS_DPM_STATE_LEVEL_5 + 1,
	DC_PP_CLOCKS_DPM_STATE_LEVEL_7 = DC_PP_CLOCKS_DPM_STATE_LEVEL_6 + 1,
};

struct dc_pp_static_clock_info {
	uint32_t max_engine_clock_hz;
	uint32_t max_memory_clock_hz;
	 /* max possible display block clocks state */
	enum dc_pp_clocks_state max_clocks_state;
};

/* The returned clocks range are 'static' system clocks which will be used for
 * mode validation purposes.
 *
 * \returns	true - call is successful
 *		false - call failed
 */
bool dc_service_get_system_clocks_range(
	const struct dc_context *ctx,
	struct dal_system_clock_range *sys_clks);

enum dc_pp_clock_type {
	DC_PP_CLOCK_TYPE_DISPLAY_CLK = 1,
	DC_PP_CLOCK_TYPE_ENGINE_CLK,
	DC_PP_CLOCK_TYPE_MEMORY_CLK
};

#define DC_PP_MAX_CLOCK_LEVELS 8

struct dc_pp_clock_levels {
	uint32_t num_levels;
	uint32_t clocks_in_hz[DC_PP_MAX_CLOCK_LEVELS];

	/* TODO: add latency
	 * do we need to know invalid (unsustainable boost) level for watermark
	 * programming? if not we can just report less elements in array
	 */
};

/* Gets valid clocks levels from pplib
 *
 * input: clk_type - display clk / sclk / mem clk
 *
 * output: array of valid clock levels for given type in ascending order,
 * with invalid levels filtered out
 *
 */
bool dc_service_pp_get_clock_levels_by_type(
	const struct dc_context *ctx,
	enum dc_pp_clock_type clk_type,
	struct dc_pp_clock_levels *clk_level_info);


/* DAL calls this function to notify PP about completion of Mode Set.
 * For PP it means that current DCE clocks are those which were returned
 * by dc_service_pp_pre_dce_clock_change(), in the 'output' parameter.
 *
 * If the clocks are higher than before, then PP does nothing.
 *
 * If the clocks are lower than before, then PP reduces the voltage.
 *
 * \returns	true - call is successful
 *		false - call failed
 */
bool dc_service_pp_apply_display_requirements(
	const struct dc_context *ctx,
	const struct dc_pp_display_configuration *pp_display_cfg);


/****** end of PP interfaces ******/

void dc_service_sleep_in_milliseconds(struct dc_context *ctx, uint32_t milliseconds);

/* end of power component calls */

void dc_service_delay_in_microseconds(struct dc_context *ctx, uint32_t microseconds);

/*
 *
 * general debug capabilities
 *
 */
#if defined(CONFIG_DEBUG_KERNEL) || defined(CONFIG_DEBUG_DRIVER)

#if defined(CONFIG_HAVE_KGDB) || defined(CONFIG_KGDB)
#define ASSERT_CRITICAL(expr) do {	\
	if (WARN_ON(!(expr))) { \
		kgdb_breakpoint(); \
	} \
} while (0)
#else
#define ASSERT_CRITICAL(expr) do {	\
	if (WARN_ON(!(expr))) { \
		; \
	} \
} while (0)
#endif

#if defined(CONFIG_DEBUG_KERNEL_DAL)
#define ASSERT(expr) ASSERT_CRITICAL(expr)

#else
#define ASSERT(expr) WARN_ON(!(expr))
#endif

#define BREAK_TO_DEBUGGER() ASSERT(0)

#else

#define ASSERT_CRITICAL(expr)  do {if (expr)/* Do nothing */; } while (0)

#define ASSERT(expr) do {if (expr)/* Do nothing */; } while (0)

#define BREAK_TO_DEBUGGER() do {} while (0)

#endif /* CONFIG_DEBUG_KERNEL || CONFIG_DEBUG_DRIVER */

#endif /* __DC_SERVICES_H__ */
