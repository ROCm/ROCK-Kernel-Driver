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

#ifndef __DAL_SERVICES_H__
#define __DAL_SERVICES_H__

/* DC headers*/
#include "dc/dc_services.h"

#include "dal_power_interface_types.h"

#include "irq_types.h"
#include "include/dal_types.h"

/* TODO: investigate if it can be removed. */
/* Undefine DEPRECATED because it conflicts with printk.h */
#undef DEPRECATED

/*
 *
 * interrupt services to register and unregister handlers
 *
 */

/* the timer "interrupt" current implementation supports only
'one-shot' type, and LOW level (asynchronous) context */
void dal_register_timer_interrupt(
	struct dc_context *ctx,
	struct dc_timer_interrupt_params *int_params,
	interrupt_handler ih,
	void *handler_args);

/*
 *
 * kernel memory manipulation
 *
 */

/* Reallocate memory. The contents will remain unchanged.*/
void *dc_service_realloc(struct dc_context *ctx, const void *ptr, uint32_t size);

void dc_service_memset(void *p, int32_t c, uint32_t count);

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
	return cgs_read_ind_register(ctx->cgs_device,addr_space,index);
}

static inline void dal_write_index_reg(
	const struct dc_context *ctx,
	enum cgs_ind_reg addr_space,
	uint32_t index,
	uint32_t value)
{
	cgs_write_ind_register(ctx->cgs_device,addr_space,index,value);
}

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

/* Calls to notification */

/* Notify display manager for hotplug event */
void dal_notify_hotplug(
	struct dc_context *ctx,
	uint32_t display_index,
	bool is_connected);


void dal_notify_setmode_complete(
	struct dc_context *ctx,
	uint32_t h_total,
	uint32_t v_total,
	uint32_t h_active,
	uint32_t v_active,
	uint32_t pix_clk_in_khz);

/* End of notification calls */

/*
 *
 * Delay functions.
 *
 *
 */

/* Following the guidance:
 * https://www.kernel.org/doc/Documentation/timers/timers-howto.txt
 *
 * This is a busy wait for nano seconds and should be used only for
 * extremely short ranges
 */
void dal_delay_in_nanoseconds(uint32_t nanoseconds);


/*
 *
 * atombios services
 *
 */

bool dal_exec_bios_cmd_table(
	struct dc_context *ctx,
	uint32_t index,
	void *params);

/*
 *
 * print-out services
 *
 */
#define dal_log_to_buffer(buffer, size, fmt, args)\
	vsnprintf(buffer, size, fmt, args)

long dal_get_pid(void);

/*
 *
 * general debug capabilities
 *
 */

#endif /* __DAL_SERVICES_H__ */
