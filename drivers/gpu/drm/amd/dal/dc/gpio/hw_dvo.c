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

#include "dal_services.h"

/*
 * Pre-requisites: headers required by header of this unit
 */

#include "include/gpio_types.h"
#include "hw_gpio_pin.h"

/*
 * Header of this unit
 */

#include "hw_dvo.h"

/*
 * Post-requisites: headers required by this unit
 */

/*
 * This unit
 */

#define FROM_HW_GPIO_PIN(ptr) \
	container_of((ptr), struct hw_dvo, base)

static void store_dvo_registers(
	struct hw_dvo *pin)
{
	pin->store.dvo_mask = dal_read_reg(
		pin->base.ctx, pin->addr.DC_GPIO_DVODATA_MASK);
	pin->store.dvo_en = dal_read_reg(
		pin->base.ctx, pin->addr.DC_GPIO_DVODATA_EN);
	pin->store.dvo_data_a = dal_read_reg(
		pin->base.ctx, pin->addr.DC_GPIO_DVODATA_A);
}

static void restore_dvo_registers(
	struct hw_dvo *pin)
{
	{
		const uint32_t addr = pin->addr.DC_GPIO_DVODATA_MASK;

		uint32_t data = dal_read_reg(pin->base.ctx, addr);

		data &= ~pin->dvo_mask;
		data |= pin->store.dvo_mask & pin->dvo_mask;

		dal_write_reg(pin->base.ctx, addr, data);
	}

	{
		const uint32_t addr = pin->addr.DC_GPIO_DVODATA_EN;

		uint32_t data = dal_read_reg(pin->base.ctx, addr);

		data &= ~pin->dvo_mask;
		data |= pin->store.dvo_en & pin->dvo_mask;

		dal_write_reg(pin->base.ctx, addr, data);
	}

	{
		const uint32_t addr = pin->addr.DC_GPIO_DVODATA_A;

		uint32_t data = dal_read_reg(pin->base.ctx, addr);

		data &= ~pin->dvo_mask;
		data |= pin->store.dvo_data_a & pin->dvo_mask;

		dal_write_reg(pin->base.ctx, addr, data);
	}
}

static void program_dvo(
	struct hw_dvo *pin,
	bool output)
{
	/* Turn on Mask bits for the requested channel,
	 * this will enable the channel for software control. */
	{
		const uint32_t addr = pin->addr.DC_GPIO_DVODATA_MASK;

		uint32_t mask = dal_read_reg(pin->base.ctx, addr);

		uint32_t data = pin->dvo_mask | mask;

		dal_write_reg(pin->base.ctx, addr, data);
	}

	/* Turn off/on the Enable bits on the requested channel,
	 * this will set it to Input/Output mode. */
	{
		const uint32_t addr = pin->addr.DC_GPIO_DVODATA_EN;

		uint32_t enable = dal_read_reg(pin->base.ctx, addr);

		uint32_t data = output ?
			(pin->dvo_mask | enable) :
			(~pin->dvo_mask & enable);

		dal_write_reg(pin->base.ctx, addr, data);
	}
}

static void program_dvo_strength(
	struct hw_dvo *pin)
{
	const uint32_t addr = pin->addr.DVO_STRENGTH_CONTROL;

	uint32_t data = dal_read_reg(pin->base.ctx, addr);

	data &= ~pin->dvo_strength_mask;
	data |= pin->dvo_strength & pin->dvo_strength_mask;

	dal_write_reg(pin->base.ctx, addr, data);
}

static void disable_on_chip_terminators(
	struct hw_dvo *pin)
{
	const uint32_t addr = pin->addr.D1CRTC_MVP_CONTROL1;

	uint32_t data = dal_read_reg(pin->base.ctx, addr);

	pin->store.mvp_terminator_state = (data & pin->mvp_termination_mask);

	data &= ~pin->mvp_termination_mask;

	dal_write_reg(pin->base.ctx, addr, data);
}

static void restore_on_chip_terminators(
	struct hw_dvo *pin)
{
	const uint32_t addr = pin->addr.D1CRTC_MVP_CONTROL1;

	uint32_t data = dal_read_reg(pin->base.ctx, addr);

	data &= ~pin->mvp_termination_mask;

	if (pin->store.mvp_terminator_state)
		data |= pin->mvp_termination_mask;

	dal_write_reg(pin->base.ctx, addr, data);
}

bool dal_hw_dvo_open(
	struct hw_gpio_pin *ptr,
	enum gpio_mode mode,
	void *options)
{
	struct hw_dvo *pin = FROM_HW_GPIO_PIN(ptr);

	store_dvo_registers(pin);

	ptr->mode = mode;

	switch (mode) {
	case GPIO_MODE_INPUT:
		program_dvo_strength(pin);
		disable_on_chip_terminators(pin);
		program_dvo(pin, false);

		ptr->opened = true;
	break;
	case GPIO_MODE_OUTPUT:
		program_dvo_strength(pin);
		disable_on_chip_terminators(pin);
		program_dvo(pin, true);

		ptr->opened = true;
	break;
	default:
		/* unsupported mode */
		BREAK_TO_DEBUGGER();

		ptr->opened = false;
	}

	return ptr->opened;
}

enum gpio_result dal_hw_dvo_get_value(
	const struct hw_gpio_pin *ptr,
	uint32_t *value)
{
	const struct hw_dvo *pin = FROM_HW_GPIO_PIN(ptr);

	if (ptr->mode != GPIO_MODE_INPUT)
		return GPIO_RESULT_NON_SPECIFIC_ERROR;

	*value = dal_read_reg(ptr->ctx, pin->addr.DC_GPIO_DVODATA_Y);

	*value &= pin->dvo_mask;
	*value >>= pin->dvo_shift;

	return GPIO_RESULT_OK;
}

enum gpio_result dal_hw_dvo_set_value(
	const struct hw_gpio_pin *ptr,
	uint32_t value)
{
	const struct hw_dvo *pin = FROM_HW_GPIO_PIN(ptr);

	uint32_t masked_value;

	if (ptr->mode != GPIO_MODE_OUTPUT) {
		BREAK_TO_DEBUGGER();
		return GPIO_RESULT_NON_SPECIFIC_ERROR;
	}

	/* Ensure there is no overflow of the value written.
	 * Value cannot be more than 12 bits for a 12-bit channel. */

	masked_value = value << pin->dvo_shift;

	if (masked_value != (masked_value & pin->dvo_mask)) {
		BREAK_TO_DEBUGGER();
		return GPIO_RESULT_INVALID_DATA;
	}

	masked_value &= pin->dvo_mask;

	/* read the DataA register
	 * mask off the Bundle that we want to write to
	 * or the data into the register */
	{
		const uint32_t addr = pin->addr.DC_GPIO_DVODATA_A;

		uint32_t data = dal_read_reg(ptr->ctx, addr);

		data &= ~pin->dvo_mask;
		data |= masked_value;

		dal_write_reg(ptr->ctx, addr, data);
	}

	return GPIO_RESULT_OK;
}

void dal_hw_dvo_close(
	struct hw_gpio_pin *ptr)
{
	struct hw_dvo *pin = FROM_HW_GPIO_PIN(ptr);

	restore_dvo_registers(pin);
	restore_on_chip_terminators(pin);

	ptr->mode = GPIO_MODE_UNKNOWN;

	ptr->opened = false;
}

bool dal_hw_dvo_construct(
	struct hw_dvo *pin,
	enum gpio_id id,
	uint32_t en,
	struct dc_context *ctx)
{
	struct hw_gpio_pin *base = &pin->base;

	if (!dal_hw_gpio_pin_construct(base, id, en, ctx))
		return false;

	pin->addr.DC_GPIO_DVODATA_MASK = 0;
	pin->addr.DC_GPIO_DVODATA_EN = 0;
	pin->addr.DC_GPIO_DVODATA_A = 0;
	pin->addr.DC_GPIO_DVODATA_Y = 0;
	pin->addr.DVO_STRENGTH_CONTROL = 0;
	pin->addr.D1CRTC_MVP_CONTROL1 = 0;

	pin->dvo_mask = 0;
	pin->dvo_shift = 0;
	pin->dvo_strength_mask = 0;
	pin->mvp_termination_mask = 0;

	pin->dvo_strength = 0;

	pin->store.dvo_mask = 0;
	pin->store.dvo_en = 0;
	pin->store.dvo_data_a = 0;
	pin->store.mvp_terminator_state = false;

	return true;
}

void dal_hw_dvo_destruct(
	struct hw_dvo *pin)
{
	dal_hw_gpio_pin_destruct(&pin->base);
}
