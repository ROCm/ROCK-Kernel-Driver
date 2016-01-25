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
#include "dc_services_types.h"
#include "logger_interface.h"
#include "include/dal_types.h"
#include "irq_types.h"
#include "link_service_types.h"

#undef DEPRECATED

/* if the pointer is not NULL, the allocated memory is zeroed */
void *dc_service_alloc(struct dc_context *ctx, uint32_t size);

/* reallocate memory. The contents will remain unchanged.*/
void *dc_service_realloc(struct dc_context *ctx, const void *ptr, uint32_t size);

void dc_service_free(struct dc_context *ctx, void *p);

void dc_service_memset(void *p, int32_t c, uint32_t count);

void dc_service_memmove(void *dst, const void *src, uint32_t size);

/* TODO: rename to dc_memcmp*/
int32_t dal_memcmp(const void *p1, const void *p2, uint32_t count);

int32_t dal_strncmp(const int8_t *p1, const int8_t *p2, uint32_t count);

irq_handler_idx dc_service_register_interrupt(
	struct dc_context *ctx,
	struct dc_interrupt_params *int_params,
	interrupt_handler ih,
	void *handler_args);

void dc_service_unregister_interrupt(
	struct dc_context *ctx,
	enum dc_irq_source irq_source,
	irq_handler_idx handler_idx);

/*
 *
 * GPU registers access
 *
 */
static inline uint32_t dal_read_reg(
	const struct dc_context *ctx,
	uint32_t address)
{
	uint32_t value = cgs_read_register(ctx->cgs_device, address);

#if defined(__DAL_REGISTER_LOGGER__)
	if (true == dal_reg_logger_should_dump_register()) {
		dal_reg_logger_rw_count_increment();
		DRM_INFO("%s 0x%x 0x%x\n", __func__, address, value);
	}
#endif
	return value;
}

static inline void dal_write_reg(
	const struct dc_context *ctx,
	uint32_t address,
	uint32_t value)
{
#if defined(__DAL_REGISTER_LOGGER__)
	if (true == dal_reg_logger_should_dump_register()) {
		dal_reg_logger_rw_count_increment();
		DRM_INFO("%s 0x%x 0x%x\n", __func__, address, value);
	}
#endif
	cgs_write_register(ctx->cgs_device, address, value);
}

static inline uint32_t dal_read_index_reg(
	const struct dc_context *ctx,
	enum cgs_ind_reg addr_space,
	uint32_t index)
{
	return cgs_read_ind_register(ctx->cgs_device, addr_space, index);
}

static inline void dal_write_index_reg(
	const struct dc_context *ctx,
	enum cgs_ind_reg addr_space,
	uint32_t index,
	uint32_t value)
{
	cgs_write_ind_register(ctx->cgs_device, addr_space, index, value);
}

static inline uint32_t get_reg_field_value_ex(
	uint32_t reg_value,
	uint32_t mask,
	uint8_t shift)
{
	return (mask & reg_value) >> shift;
}

#define get_reg_field_value(reg_value, reg_name, reg_field)\
	get_reg_field_value_ex(\
		(reg_value),\
		reg_name ## __ ## reg_field ## _MASK,\
		reg_name ## __ ## reg_field ## __SHIFT)

static inline uint32_t set_reg_field_value_ex(
	uint32_t reg_value,
	uint32_t value,
	uint32_t mask,
	uint8_t shift)
{
	return (reg_value & ~mask) | (mask & (value << shift));
}

#define set_reg_field_value(reg_value, value, reg_name, reg_field)\
	(reg_value) = set_reg_field_value_ex(\
		(reg_value),\
		(value),\
		reg_name ## __ ## reg_field ## _MASK,\
		reg_name ## __ ## reg_field ## __SHIFT)

/*
 * atombios services
 */

bool dal_exec_bios_cmd_table(
	struct dc_context *ctx,
	uint32_t index,
	void *params);

#ifdef BUILD_DAL_TEST
uint32_t dal_bios_cmd_table_para_revision(
struct dc_context *ctx,
	uint32_t index);

bool dal_bios_cmd_table_revision(
	struct dc_context *ctx,
	uint32_t index,
	uint8_t *frev,
	uint8_t *crev);
#endif

#ifndef BUILD_DAL_TEST
static inline uint32_t dal_bios_cmd_table_para_revision(
	struct dc_context *ctx,
	uint32_t index)
{
	uint8_t frev;
	uint8_t crev;

	if (cgs_atom_get_cmd_table_revs(
			ctx->cgs_device,
			index,
			&frev,
			&crev) != 0)
		return 0;

	return crev;
}
#else
uint32_t dal_bios_cmd_table_para_revision(
		struct dc_context *ctx,
		uint32_t index);
#endif

/**************************************
 * Power Play (PP) interfaces
 **************************************/

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
	uint32_t max_sclk_khz;
	uint32_t max_mclk_khz;

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
	DC_PP_CLOCK_TYPE_ENGINE_CLK, /* System clock */
	DC_PP_CLOCK_TYPE_MEMORY_CLK
};

#define DC_DECODE_PP_CLOCK_TYPE(clk_type) \
	(clk_type) == DC_PP_CLOCK_TYPE_DISPLAY_CLK ? "Display" : \
	(clk_type) == DC_PP_CLOCK_TYPE_ENGINE_CLK ? "Engine" : \
	(clk_type) == DC_PP_CLOCK_TYPE_MEMORY_CLK ? "Memory" : "Invalid"

#define DC_PP_MAX_CLOCK_LEVELS 8

struct dc_pp_clock_levels {
	uint32_t num_levels;
	uint32_t clocks_in_khz[DC_PP_MAX_CLOCK_LEVELS];

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


bool dc_service_pp_apply_safe_state(
		const struct dc_context *ctx);

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

void dc_service_delay_in_microseconds(struct dc_context *ctx, uint32_t microseconds);

enum platform_method {
	PM_GET_AVAILABLE_METHODS = 1 << 0,
	PM_GET_LID_STATE = 1 << 1,
	PM_GET_EXTENDED_BRIGHNESS_CAPS = 1 << 2
};

struct platform_info_params {
	enum platform_method method;
	void *data;
};

struct platform_info_brightness_caps {
	uint8_t ac_level_percentage;
	uint8_t dc_level_percentage;
};

struct platform_info_ext_brightness_caps {
	struct platform_info_brightness_caps basic_caps;
	struct data_point {
		uint8_t luminance;
		uint8_t	signal_level;
	} data_points[99];

	uint8_t	data_points_num;
	uint8_t	min_input_signal;
	uint8_t	max_input_signal;
};

bool dal_get_platform_info(
	struct dc_context *ctx,
	struct platform_info_params *params);

/*
 *
 * print-out services
 *
 */
#define dal_log_to_buffer(buffer, size, fmt, args)\
	vsnprintf(buffer, size, fmt, args)

long dal_get_pid(void);
long dal_get_tgid(void);

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
