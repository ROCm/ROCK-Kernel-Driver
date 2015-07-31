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

#include <linux/slab.h>

#include "dal_services_types.h"
#include "dal_power_interface_types.h"

#include "include/irq_types.h"
#include "include/dal_types.h"

/*
 *
 * interrupt services to register and unregister handlers
 *
 */

/* the timer "interrupt" current implementation supports only
'one-shot' type, and LOW level (asynchronous) context */
void dal_register_timer_interrupt(
	struct dal_context *context,
	struct dal_timer_interrupt_params *int_params,
	interrupt_handler ih,
	void *handler_args);

irq_handler_idx dal_register_interrupt(
	struct dal_context *context,
	struct dal_interrupt_params *int_params,
	interrupt_handler ih,
	void *handler_args);

void dal_unregister_interrupt(
	struct dal_context *context,
	enum dal_irq_source irq_source,
	irq_handler_idx handler_idx);

/*
 *
 * kernel memory manipulation
 *
 */

/* if the pointer is not NULL, the allocated memory is zeroed */
#define dal_alloc(size) kzalloc(size, GFP_KERNEL)

/* Reallocate memory. The contents will remain unchanged.*/
#define dal_realloc(ptr, size) krealloc(ptr, size, GFP_KERNEL)

#define dal_memmove(dst, src, size) memmove(dst, src, size)

/* free the memory which was allocated by dal_alloc or dal_realloc */
#define dal_free(p) kfree(p)

#define dal_memset(ptr, value, count) memset(ptr, value, count)

#define dal_memcmp(p1, p2, count) memcmp(p1, p2, count)

/* comparison of null-terminated strings */
#define dal_strcmp(p1, p2) strcmp(p1, p2)

#define dal_strncmp(p1, p2, count) strncmp(p1, p2, count)

/*
 *
 * GPU registers access
 *
 */
#ifndef BUILD_DAL_TEST
static inline uint32_t dal_read_reg(
	struct dal_context *ctx,
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
#else
uint32_t dal_read_reg(struct dal_context *ctx, uint32_t address);
#endif

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

#ifndef BUILD_DAL_TEST
static inline void dal_write_reg(
	struct dal_context *ctx,
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
#else
void dal_write_reg(struct dal_context *ctx, uint32_t address, uint32_t value);
#endif

static inline uint32_t dal_read_index_reg(
	struct dal_context *ctx,
	uint32_t index_reg_offset,
	uint32_t index,
	uint32_t data_reg_offset)
{
	dal_write_reg(ctx, index_reg_offset, index);
	return dal_read_reg(ctx, data_reg_offset);
}

static inline void dal_write_index_reg(
	struct dal_context *ctx,
	uint32_t index_reg_offset,
	uint32_t index,
	uint32_t data_reg_offset,
	uint32_t value)
{
	dal_write_reg(ctx, index_reg_offset, index);
	dal_write_reg(ctx, data_reg_offset, value);
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

bool dal_get_platform_info(
	struct dal_context *dal_context,
	struct platform_info_params *params);


#ifndef BUILD_DAL_TEST
static inline uint32_t dal_bios_cmd_table_para_revision(
	struct dal_context *ctx,
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
		struct dal_context *ctx,
		uint32_t index);
#endif

/**************************************
 * Calls to Power Play (PP) component
 **************************************/

/* DAL calls this function to notify PP about clocks it needs for the Mode Set.
 * This is done *before* it changes DCE clock.
 *
 * If required clock is higher than current, then PP will increase the voltage.
 *
 * If required clock is lower than current, then PP will defer reduction of
 * voltage until the call to dal_pp_post_dce_clock_change().
 *
 * \input - Contains clocks needed for Mode Set.
 *
 * \output - Contains clocks adjusted by PP which DAL should use for Mode Set.
 *		Valid only if function returns zero.
 *
 * \returns	true - call is successful
 *		false - call failed
 */
bool dal_pp_pre_dce_clock_change(
	struct dal_context *ctx,
	struct dal_to_power_info *input,
	struct power_to_dal_info *output);

/* DAL calls this function to notify PP about completion of Mode Set.
 * For PP it means that current DCE clocks are those which were returned
 * by dal_pp_pre_dce_clock_change(), in the 'output' parameter.
 *
 * If the clocks are higher than before, then PP does nothing.
 *
 * If the clocks are lower than before, then PP reduces the voltage.
 *
 * \returns	true - call is successful
 *		false - call failed
 */
bool dal_pp_post_dce_clock_change(struct dal_context *ctx);

/* The returned clocks range are 'static' system clocks which will be used for
 * mode validation purposes.
 *
 * \returns	true - call is successful
 *		false - call failed
 */
bool dal_get_system_clocks_range(
	struct dal_context *ctx,
	struct dal_system_clock_range *sys_clks);

/* for future use */
bool dal_pp_set_display_clock(
	struct dal_context *ctx,
	struct dal_to_power_dclk *dclk);


/* end of power component calls */


/* Calls to notification */

/* Notify display manager for hotplug event */
void dal_notify_hotplug(
	struct dal_context *ctx,
	uint32_t display_index,
	bool is_connected);

/* Notify display manager for capability change event */
void dal_notify_capability_change(
	struct dal_context *ctx,
	uint32_t display_index);

void dal_notify_setmode_complete(
	struct dal_context *ctx,
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
#define dal_delay_in_nanoseconds(nanoseconds) ndelay(nanoseconds)

/* Following the guidance:
 * https://www.kernel.org/doc/Documentation/timers/timers-howto.txt
 *
 * This is a busy wait for micro seconds and should.
 */
#define dal_delay_in_microseconds(microseconds) udelay(microseconds)

/* Following the guidances:
 * https://www.kernel.org/doc/Documentation/timers/timers-howto.txt
 * http://lkml.indiana.edu/hypermail/linux/kernel/1008.0/00733.html
 *
 * This is a sleep (not busy-waiting) for milliseconds with a
 * good precision.
 */
#define dal_sleep_in_milliseconds(milliseconds) \
{ \
	if (milliseconds >= 20) \
		msleep(milliseconds); \
	else \
		usleep_range(milliseconds*1000, milliseconds*1000+1); \
}

/*
 *
 * atombios services
 *
 */

bool dal_exec_bios_cmd_table(
	struct dal_context *ctx,
	uint32_t index,
	void *params);

#ifdef BUILD_DAL_TEST
uint32_t dal_bios_cmd_table_para_revision(
struct dal_context *ctx,
	uint32_t index);

bool dal_bios_cmd_table_revision(
	struct dal_context *ctx,
	uint32_t index,
	uint8_t *frev,
	uint8_t *crev);
#endif

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

#endif /* __DAL_SERVICES_H__ */
