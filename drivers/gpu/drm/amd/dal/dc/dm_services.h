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

#ifndef __DM_SERVICES_H__

#define __DM_SERVICES_H__

/* TODO: remove when DC is complete. */
#include "dm_services_types.h"
#include "logger_interface.h"
#include "link_service_types.h"
#include <stdarg.h>

#undef DEPRECATED

#define dm_alloc(size) kzalloc(size, GFP_KERNEL)
#define dm_realloc(ptr, size) krealloc(ptr, size, GFP_KERNEL)
#define dm_free(ptr) kfree(ptr)

irq_handler_idx dm_register_interrupt(
	struct dc_context *ctx,
	struct dc_interrupt_params *int_params,
	interrupt_handler ih,
	void *handler_args);

void dm_unregister_interrupt(
	struct dc_context *ctx,
	enum dc_irq_source irq_source,
	irq_handler_idx handler_idx);

/*
 *
 * GPU registers access
 *
 */

#define dm_read_reg(ctx, address)	\
		dm_read_reg_func(ctx, address, __func__)

static inline uint32_t dm_read_reg_func(
	const struct dc_context *ctx,
	uint32_t address,
	const char *func_name)
{
	uint32_t value = cgs_read_register(ctx->cgs_device, address);

#if defined(__DAL_REGISTER_LOGGER__)
	if (true == dal_reg_logger_should_dump_register()) {
		dal_reg_logger_rw_count_increment();
		DRM_INFO("%s DC_READ_REG: 0x%x 0x%x\n", func_name, address, value);
	}
#endif
	return value;
}

#define dm_write_reg(ctx, address, value)	\
	dm_write_reg_func(ctx, address, value, __func__)

static inline void dm_write_reg_func(
	const struct dc_context *ctx,
	uint32_t address,
	uint32_t value,
	const char *func_name)
{
#if defined(__DAL_REGISTER_LOGGER__)
	if (true == dal_reg_logger_should_dump_register()) {
		dal_reg_logger_rw_count_increment();
		DRM_INFO("%s DC_WRITE_REG: 0x%x 0x%x\n", func_name, address, value);
	}
#endif
	cgs_write_register(ctx->cgs_device, address, value);
}

static inline uint32_t dm_read_index_reg(
	const struct dc_context *ctx,
	enum cgs_ind_reg addr_space,
	uint32_t index)
{
	return cgs_read_ind_register(ctx->cgs_device, addr_space, index);
}

static inline void dm_write_index_reg(
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


static inline void generic_reg_update_ex(const struct dc_context *ctx,
		uint32_t addr, uint32_t reg_val, int n, ...)
{
	int shift, mask, field_value;
	int i = 0;

	va_list ap;
	va_start(ap, n);

	 while (i < n) {
		shift = va_arg(ap, int);
		mask = va_arg(ap, int);
		field_value = va_arg(ap, int);

		reg_val = set_reg_field_value_ex(reg_val, field_value, mask, shift);
		i++;
	  }

	 dm_write_reg(ctx, addr, reg_val);
	 va_end(ap);


}

#define generic_reg_update(ctx, inst_offset, reg_name, n, ...)\
		generic_reg_update_ex(ctx, \
		mm##reg_name + inst_offset, dm_read_reg(ctx, mm##reg_name + inst_offset), n, \
		__VA_ARGS__)

#define generic_reg_set(ctx, inst_offset, reg_name, n, ...)\
		generic_reg_update_ex(ctx, \
		mm##reg_name + inst_offset, 0, n, \
		__VA_ARGS__)



#define FD(reg_field)	reg_field ## __SHIFT, \
						reg_field ## _MASK


/*
 * atombios services
 */

bool dm_exec_bios_cmd_table(
	struct dc_context *ctx,
	uint32_t index,
	void *params);

#ifdef BUILD_DAL_TEST
uint32_t dm_bios_cmd_table_para_revision(
struct dc_context *ctx,
	uint32_t index);

bool dm_bios_cmd_table_revision(
	struct dc_context *ctx,
	uint32_t index,
	uint8_t *frev,
	uint8_t *crev);
#endif

#ifndef BUILD_DAL_TEST
static inline uint32_t dm_bios_cmd_table_para_revision(
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
uint32_t dm_bios_cmd_table_para_revision(
		struct dc_context *ctx,
		uint32_t index);
#endif

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
bool dm_pp_pre_dce_clock_change(
	struct dc_context *ctx,
	struct dm_pp_gpu_clock_range *requested_state,
	struct dm_pp_gpu_clock_range *actual_state);

/* The returned clocks range are 'static' system clocks which will be used for
 * mode validation purposes.
 *
 * \returns	true - call is successful
 *		false - call failed
 */
bool dc_service_get_system_clocks_range(
	const struct dc_context *ctx,
	struct dm_pp_gpu_clock_range *sys_clks);

/* Gets valid clocks levels from pplib
 *
 * input: clk_type - display clk / sclk / mem clk
 *
 * output: array of valid clock levels for given type in ascending order,
 * with invalid levels filtered out
 *
 */
bool dm_pp_get_clock_levels_by_type(
	const struct dc_context *ctx,
	enum dm_pp_clock_type clk_type,
	struct dm_pp_clock_levels *clk_level_info);

bool dm_pp_apply_safe_state(
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
bool dm_pp_apply_display_requirements(
	const struct dc_context *ctx,
	const struct dm_pp_display_configuration *pp_display_cfg);

/****** end of PP interfaces ******/

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

bool dm_get_platform_info(
	struct dc_context *ctx,
	struct platform_info_params *params);

/*
 *
 * print-out services
 *
 */
#define dm_log_to_buffer(buffer, size, fmt, args)\
	vsnprintf(buffer, size, fmt, args)

long dm_get_pid(void);
long dm_get_tgid(void);

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

#endif /* CONFIG_DEBUG_KERNEL || CONFIG_DEBUG_DRIVER */

#endif /* __DM_SERVICES_H__ */
