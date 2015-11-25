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
#include "include/dal_types.h"
#include "logger_interface.h"
#include "irq_types.h"
#include "dal_power_interface_types.h"


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
 * Calls to Power Play (PP) component
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

struct dc_pp_display_configuration {
	bool nb_pstate_switch_disable;/* controls NB PState switch */
	bool cpu_cc6_disable; /* controls CPU CState switch ( on or off) */
	bool cpu_pstate_disable;
	uint32_t cpu_pstate_separation_time;
};

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
bool dc_service_pp_post_dce_clock_change(
	struct dc_context *ctx,
	const struct dc_pp_display_configuration *pp_display_cfg);

/* The returned clocks range are 'static' system clocks which will be used for
 * mode validation purposes.
 *
 * \returns	true - call is successful
 *		false - call failed
 */
bool dc_service_get_system_clocks_range(
	struct dc_context *ctx,
	struct dal_system_clock_range *sys_clks);

/* for future use */
bool dc_service_pp_set_display_clock(
	struct dc_context *ctx,
	struct dal_to_power_dclk *dclk);

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
