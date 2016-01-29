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

#include "dm_services.h"

/*
 * Pre-requisites: headers required by header of this unit
 */
#include "include/gpio_types.h"
#include "hw_gpio_pin.h"

/*
 * Header of this unit
 */

#include "hw_gpio.h"

/*
 * Post-requisites: headers required by this unit
 */

/*
 * This unit
 */

enum gpio_result dal_hw_gpio_get_reg_value(
	struct dc_context *ctx,
	const struct addr_mask *reg,
	uint32_t *value)
{
	*value = dm_read_reg(ctx, reg->addr);

	*value &= reg->mask;

	return GPIO_RESULT_OK;
}

enum gpio_result dal_hw_gpio_set_reg_value(
	struct dc_context *ctx,
	const struct addr_mask *reg,
	uint32_t value)
{
	uint32_t prev_value;

	if ((value & reg->mask) != value) {
		BREAK_TO_DEBUGGER();
		return GPIO_RESULT_INVALID_DATA;
	}

	prev_value = dm_read_reg(ctx, reg->addr);

	prev_value &= ~reg->mask;
	prev_value |= (value & reg->mask);

	dm_write_reg(ctx, reg->addr, prev_value);

	return GPIO_RESULT_OK;
}

uint32_t dal_hw_gpio_get_shift_from_mask(
	uint32_t mask)
{
	uint32_t result = 0;

	if (!mask)
		return 32;

	do {
		if ((1 << result) & mask)
			break;

		++result;
	} while (result < 32);

	return result;
}

#define FROM_HW_GPIO_PIN(ptr) \
	container_of((ptr), struct hw_gpio, base)

static void store_registers(
	struct hw_gpio *pin)
{
	dal_hw_gpio_get_reg_value(
		pin->base.ctx,
		&pin->pin_reg.DC_GPIO_DATA_MASK,
		&pin->store.mask);
	dal_hw_gpio_get_reg_value(
		pin->base.ctx,
		&pin->pin_reg.DC_GPIO_DATA_A,
		&pin->store.a);
	dal_hw_gpio_get_reg_value(
		pin->base.ctx,
		&pin->pin_reg.DC_GPIO_DATA_EN,
		&pin->store.en);

	if (pin->mux_supported)
		dal_hw_gpio_get_reg_value(
			pin->base.ctx,
			&pin->mux_reg.GPIO_MUX_CONTROL,
			&pin->store.mux);
}

static void restore_registers(
	struct hw_gpio *pin)
{
	dal_hw_gpio_set_reg_value(
		pin->base.ctx,
		&pin->pin_reg.DC_GPIO_DATA_MASK,
		pin->store.mask);
	dal_hw_gpio_set_reg_value(
		pin->base.ctx,
		&pin->pin_reg.DC_GPIO_DATA_A,
		pin->store.a);
	dal_hw_gpio_set_reg_value(
		pin->base.ctx,
		&pin->pin_reg.DC_GPIO_DATA_EN,
		pin->store.en);

	if (pin->mux_supported)
		dal_hw_gpio_set_reg_value(
			pin->base.ctx,
			&pin->mux_reg.GPIO_MUX_CONTROL,
			pin->store.mux);
}

bool dal_hw_gpio_open(
	struct hw_gpio_pin *ptr,
	enum gpio_mode mode,
	void *options)
{
	struct hw_gpio *pin = FROM_HW_GPIO_PIN(ptr);

	store_registers(pin);

	ptr->opened = (pin->funcs->config_mode(pin, mode) == GPIO_RESULT_OK);

	return ptr->opened;
}

enum gpio_result dal_hw_gpio_get_value(
	const struct hw_gpio_pin *ptr,
	uint32_t *value)
{
	const struct hw_gpio *pin = FROM_HW_GPIO_PIN(ptr);

	enum gpio_result result;

	switch (ptr->mode) {
	case GPIO_MODE_INPUT:
	case GPIO_MODE_OUTPUT:
	case GPIO_MODE_HARDWARE:
	case GPIO_MODE_FAST_OUTPUT:
		result = dal_hw_gpio_get_reg_value(
			ptr->ctx,
			&pin->pin_reg.DC_GPIO_DATA_Y,
			value);
		/* Clients does not know that the value
		 * comes from register and is shifted. */
		if (result == GPIO_RESULT_OK)
			*value >>= dal_hw_gpio_get_shift_from_mask(
				pin->pin_reg.DC_GPIO_DATA_Y.mask);
	break;
	default:
		result = GPIO_RESULT_NON_SPECIFIC_ERROR;
	}

	return result;
}

enum gpio_result dal_hw_gpio_set_value(
	const struct hw_gpio_pin *ptr,
	uint32_t value)
{
	struct hw_gpio *pin = FROM_HW_GPIO_PIN(ptr);

	/* This is the public interface
	 * where the input comes from client, not shifted yet
	 * (because client does not know the shifts). */

	switch (ptr->mode) {
	case GPIO_MODE_OUTPUT:
		return dal_hw_gpio_set_reg_value(
			ptr->ctx,
			&pin->pin_reg.DC_GPIO_DATA_A,
			value << dal_hw_gpio_get_shift_from_mask(
				pin->pin_reg.DC_GPIO_DATA_A.mask));
	case GPIO_MODE_FAST_OUTPUT:
		/* We use (EN) to faster switch (used in DDC GPIO).
		 * So (A) is grounded, output is driven by (EN = 0)
		 * to pull the line down (output == 0) and (EN=1)
		 * then output is tri-state */
		return dal_hw_gpio_set_reg_value(
			ptr->ctx,
			&pin->pin_reg.DC_GPIO_DATA_EN,
			pin->pin_reg.DC_GPIO_DATA_EN.mask &
			~(value << dal_hw_gpio_get_shift_from_mask(
				pin->pin_reg.DC_GPIO_DATA_EN.mask)));
	default:
		return GPIO_RESULT_NON_SPECIFIC_ERROR;
	}
}

enum gpio_result dal_hw_gpio_change_mode(
	struct hw_gpio_pin *ptr,
	enum gpio_mode mode)
{
	struct hw_gpio *pin = FROM_HW_GPIO_PIN(ptr);

	return pin->funcs->config_mode(pin, mode);
}

void dal_hw_gpio_close(
	struct hw_gpio_pin *ptr)
{
	struct hw_gpio *pin = FROM_HW_GPIO_PIN(ptr);

	restore_registers(pin);

	ptr->mode = GPIO_MODE_UNKNOWN;
	ptr->opened = false;
}

static enum gpio_result config_mode_input(
	struct hw_gpio *pin)
{
	enum gpio_result result;

	/* turn off output enable, act as input pin;
	 * program the pin as GPIO, mask out signal driven by HW */

	result = dal_hw_gpio_set_reg_value(
		pin->base.ctx,
		&pin->pin_reg.DC_GPIO_DATA_EN,
		0);

	if (result != GPIO_RESULT_OK)
		return GPIO_RESULT_NON_SPECIFIC_ERROR;

	result = dal_hw_gpio_set_reg_value(
		pin->base.ctx,
		&pin->pin_reg.DC_GPIO_DATA_MASK,
		pin->pin_reg.DC_GPIO_DATA_MASK.mask);

	if (result != GPIO_RESULT_OK)
		return GPIO_RESULT_NON_SPECIFIC_ERROR;

	return GPIO_RESULT_OK;
}

static enum gpio_result config_mode_output(
	struct hw_gpio *pin)
{
	enum gpio_result result;

	/* turn on output enable, act as output pin;
	 * program the pin as GPIO, mask out signal driven by HW */

	result = dal_hw_gpio_set_reg_value(
		pin->base.ctx,
		&pin->pin_reg.DC_GPIO_DATA_EN,
		pin->pin_reg.DC_GPIO_DATA_EN.mask);

	if (result != GPIO_RESULT_OK)
		return GPIO_RESULT_NON_SPECIFIC_ERROR;

	result = dal_hw_gpio_set_reg_value(
		pin->base.ctx,
		&pin->pin_reg.DC_GPIO_DATA_MASK,
		pin->pin_reg.DC_GPIO_DATA_MASK.mask);

	if (result != GPIO_RESULT_OK)
		return GPIO_RESULT_NON_SPECIFIC_ERROR;

	return GPIO_RESULT_OK;
}

static enum gpio_result config_mode_fast_output(
	struct hw_gpio *pin)
{
	enum gpio_result result;

	/* grounding the A register then use the EN register bit
	 * will have faster effect on the rise time */

	result = dal_hw_gpio_set_reg_value(
		pin->base.ctx,
		&pin->pin_reg.DC_GPIO_DATA_A, 0);

	if (result != GPIO_RESULT_OK)
		return GPIO_RESULT_NON_SPECIFIC_ERROR;

	result = dal_hw_gpio_set_reg_value(
		pin->base.ctx,
		&pin->pin_reg.DC_GPIO_DATA_MASK,
		pin->pin_reg.DC_GPIO_DATA_MASK.mask);

	if (result != GPIO_RESULT_OK)
		return GPIO_RESULT_NON_SPECIFIC_ERROR;

	return GPIO_RESULT_OK;
}

static enum gpio_result config_mode_hardware(
	struct hw_gpio *pin)
{
	/* program the pin as tri-state, pin is driven by HW */

	enum gpio_result result =
		dal_hw_gpio_set_reg_value(
			pin->base.ctx,
			&pin->pin_reg.DC_GPIO_DATA_MASK,
			0);

	if (result != GPIO_RESULT_OK)
		return GPIO_RESULT_NON_SPECIFIC_ERROR;

	return GPIO_RESULT_OK;
}

enum gpio_result dal_hw_gpio_config_mode(
	struct hw_gpio *pin,
	enum gpio_mode mode)
{
	pin->base.mode = mode;

	switch (mode) {
	case GPIO_MODE_INPUT:
		return config_mode_input(pin);
	case GPIO_MODE_OUTPUT:
		return config_mode_output(pin);
	case GPIO_MODE_FAST_OUTPUT:
		return config_mode_fast_output(pin);
	case GPIO_MODE_HARDWARE:
		return config_mode_hardware(pin);
	default:
		return GPIO_RESULT_NON_SPECIFIC_ERROR;
	}
}

const struct hw_gpio_funcs func = {
	.config_mode = dal_hw_gpio_config_mode,
};

bool dal_hw_gpio_construct(
	struct hw_gpio *pin,
	enum gpio_id id,
	uint32_t en,
	struct dc_context *ctx)
{
	struct hw_gpio_pin *base = &pin->base;

	if (!dal_hw_gpio_pin_construct(base, id, en, ctx))
		return false;

	pin->funcs = &func;

	pin->pin_reg.DC_GPIO_DATA_MASK.addr = 0;
	pin->pin_reg.DC_GPIO_DATA_MASK.mask = 0;
	pin->pin_reg.DC_GPIO_DATA_A.addr = 0;
	pin->pin_reg.DC_GPIO_DATA_A.mask = 0;
	pin->pin_reg.DC_GPIO_DATA_EN.addr = 0;
	pin->pin_reg.DC_GPIO_DATA_EN.mask = 0;
	pin->pin_reg.DC_GPIO_DATA_Y.addr = 0;
	pin->pin_reg.DC_GPIO_DATA_Y.mask = 0;
	pin->mux_reg.GPIO_MUX_CONTROL.addr = 0;
	pin->mux_reg.GPIO_MUX_CONTROL.mask = 0;
	pin->mux_reg.GPIO_MUX_STEREO_SEL.addr = 0;
	pin->mux_reg.GPIO_MUX_STEREO_SEL.mask = 0;

	pin->store.mask = 0;
	pin->store.a = 0;
	pin->store.en = 0;
	pin->store.mux = 0;

	pin->mux_supported = false;

	return true;
}

void dal_hw_gpio_destruct(
	struct hw_gpio *pin)
{
	dal_hw_gpio_pin_destruct(&pin->base);
}
