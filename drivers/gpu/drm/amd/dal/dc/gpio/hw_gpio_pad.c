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
#include "hw_gpio.h"

/*
 * Header of this unit
 */

#include "hw_gpio_pad.h"

/*
 * Post-requisites: headers required by this unit
 */

/*
 * This unit
 */

#define FROM_HW_GPIO(ptr) \
	container_of((ptr), struct hw_gpio_pad, base)

#define FROM_HW_GPIO_PIN(ptr) \
	FROM_HW_GPIO(container_of((ptr), struct hw_gpio, base))

enum gpio_result dal_hw_gpio_pad_get_value(
	const struct hw_gpio_pin *ptr,
	uint32_t *value)
{
	const struct hw_gpio_pad *pin = FROM_HW_GPIO_PIN(ptr);

	if (ptr->mode == GPIO_MODE_INTERRUPT)
		/* in Interrupt mode, ask for interrupt status bit */
		return dal_hw_gpio_get_reg_value(
			ptr->ctx,
			&pin->gpiopad_int_status,
			value);
	else
		/* for any mode other than Interrupt,
		 * gpio_pad operates as normal GPIO */
		return dal_hw_gpio_get_value(ptr, value);
}

bool dal_hw_gpio_pad_construct(
	struct hw_gpio_pad *pin,
	enum gpio_id id,
	uint32_t en,
	struct dc_context *ctx)
{
	if (!dal_hw_gpio_construct(&pin->base, id, en, ctx))
		return false;

	pin->gpiopad_int_status.addr = 0;
	pin->gpiopad_int_status.mask = 0;

	return true;
}

void dal_hw_gpio_pad_destruct(
	struct hw_gpio_pad *pin)
{
	dal_hw_gpio_destruct(&pin->base);
}
