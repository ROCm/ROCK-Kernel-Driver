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

#include "hw_ddc.h"

/*
 * Post-requisites: headers required by this unit
 */

/*
 * This unit
 */

#define FROM_HW_GPIO(ptr) \
	container_of((ptr), struct hw_ddc, base)

#define FROM_HW_GPIO_PIN(ptr) \
	FROM_HW_GPIO(container_of((ptr), struct hw_gpio, base))

bool dal_hw_ddc_open(
	struct hw_gpio_pin *ptr,
	enum gpio_mode mode,
	void *options)
{
	struct hw_ddc *pin = FROM_HW_GPIO_PIN(ptr);

	uint32_t en;

	if (!options) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	/* get the EN bit before overwriting it */

	dal_hw_gpio_get_reg_value(
		ptr->ctx,
		&pin->base.pin_reg.DC_GPIO_DATA_EN,
		&en);

	((struct gpio_ddc_open_options *)options)->en_bit_present = (en != 0);

	return dal_hw_gpio_open(ptr, mode, options);
}

bool dal_hw_ddc_construct(
	struct hw_ddc *pin,
	enum gpio_id id,
	uint32_t en,
	struct dc_context *ctx)
{
	if (!dal_hw_gpio_construct(&pin->base, id, en, ctx))
		return false;

	pin->mask.DC_GPIO_DDC_MASK_MASK = 0;
	pin->mask.DC_GPIO_DDC_PD_EN_MASK = 0;
	pin->mask.DC_GPIO_DDC_RECV_MASK = 0;
	pin->mask.AUX_PAD_MODE_MASK = 0;
	pin->mask.AUX_POL_MASK = 0;
	pin->mask.DC_GPIO_DDCCLK_STR_MASK = 0;

	return true;
}

void dal_hw_ddc_destruct(
	struct hw_ddc *pin)
{
	dal_hw_gpio_destruct(&pin->base);
}
