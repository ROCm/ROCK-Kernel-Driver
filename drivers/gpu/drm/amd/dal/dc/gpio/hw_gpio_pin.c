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

/*
 * Header of this unit
 */

#include "hw_gpio_pin.h"

/*
 * Post-requisites: headers required by this unit
 */

/*
 * This unit
 */
enum gpio_result dal_hw_gpio_pin_set_config(
	struct hw_gpio_pin *pin,
	const struct gpio_config_data *config_data)
{
	/* Attention!
	 * You must override this method in derived class */

	return GPIO_RESULT_NON_SPECIFIC_ERROR;
}

enum gpio_result dal_hw_gpio_pin_change_mode(
	struct hw_gpio_pin *pin,
	enum gpio_mode mode)
{
	/* Attention!
	 * You must override this method in derived class */

	return GPIO_RESULT_NON_SPECIFIC_ERROR;
}

bool dal_hw_gpio_pin_construct(
	struct hw_gpio_pin *pin,
	enum gpio_id id,
	uint32_t en,
	struct dc_context *ctx)
{
	pin->ctx = ctx;
	pin->id = id;
	pin->en = en;
	pin->mode = GPIO_MODE_UNKNOWN;
	pin->opened = false;

	return true;
}

void dal_hw_gpio_pin_destruct(
	struct hw_gpio_pin *pin)
{
	ASSERT(!pin->opened);
}
