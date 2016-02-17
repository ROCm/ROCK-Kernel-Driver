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

/*
 * Pre-requisites: headers required by header of this unit
 */

#include "dm_services.h"
#include "include/gpio_types.h"
#include "../hw_factory.h"

/*
 * Header of this unit
 */

#include "hw_factory_dce80.h"

/*
 * Post-requisites: headers required by this unit
 */

#include "../hw_gpio_pin.h"
#include "../hw_gpio.h"
#include "../hw_gpio_pad.h"
#include "../hw_ddc.h"
#include "hw_ddc_dce80.h"
#include "../hw_hpd.h"
#include "hw_hpd_dce80.h"

/*
 * This unit
 */
static const struct hw_factory_funcs funcs = {
	.create_ddc_data = dal_hw_ddc_dce80_create,
	.create_ddc_clock = dal_hw_ddc_dce80_create,
	.create_generic = NULL,
	.create_hpd = dal_hw_hpd_dce80_create,
	.create_gpio_pad = NULL,
	.create_sync = NULL,
	.create_gsl = NULL,
};

void dal_hw_factory_dce80_init(
	struct hw_factory *factory)
{
	factory->number_of_pins[GPIO_ID_DDC_DATA] = 8;
	factory->number_of_pins[GPIO_ID_DDC_CLOCK] = 8;
	factory->number_of_pins[GPIO_ID_GENERIC] = 7;
	factory->number_of_pins[GPIO_ID_HPD] = 6;
	factory->number_of_pins[GPIO_ID_GPIO_PAD] = 31;
	factory->number_of_pins[GPIO_ID_VIP_PAD] = 0;
	factory->number_of_pins[GPIO_ID_SYNC] = 2;
	factory->number_of_pins[GPIO_ID_GSL] = 4;

	factory->funcs = &funcs;
}
