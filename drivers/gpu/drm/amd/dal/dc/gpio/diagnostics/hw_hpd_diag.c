/*
 * Copyright 2012-16 Advanced Micro Devices, Inc.
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
#include "../hw_gpio_pin.h"
#include "../hw_gpio.h"
#include "../hw_hpd.h"

static void destruct(
	struct hw_hpd *pin)
{
	dal_hw_hpd_destruct(pin);
}

static void destroy(
	struct hw_gpio_pin **ptr)
{
	struct hw_hpd *pin = HW_HPD_FROM_BASE(*ptr);

	destruct(pin);

	dm_free(pin);

	*ptr = NULL;
}

static const struct hw_gpio_pin_funcs funcs = {
	.destroy = destroy,
	.open = NULL,
	.get_value = NULL,
	.set_value = NULL,
	.set_config = NULL,
	.change_mode = NULL,
	.close = NULL,
};

static bool construct(
	struct hw_hpd *pin,
	enum gpio_id id,
	uint32_t en,
	struct dc_context *ctx)
{
	if (!dal_hw_hpd_construct(pin, id, en, ctx)) {
		ASSERT_CRITICAL(false);
		return false;
	}

	pin->base.base.funcs = &funcs;

	return true;
}

struct hw_gpio_pin *dal_hw_hpd_diag_fpga_create(
	struct dc_context *ctx,
	enum gpio_id id,
	uint32_t en)
{
	struct hw_hpd *pin = dm_alloc(sizeof(struct hw_hpd));

	if (!pin) {
		ASSERT_CRITICAL(false);
		return NULL;
	}

	if (construct(pin, id, en, ctx))
		return &pin->base.base;

	ASSERT_CRITICAL(false);

	dm_free(pin);

	return NULL;
}
