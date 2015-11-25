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

#include "include/gpio_interface.h"
#include "include/dvo_interface.h"
#include "include/gpio_service_interface.h"
#include "hw_gpio_pin.h"
#include "hw_translate.h"
#include "hw_factory.h"
#include "gpio_service.h"
#include "gpio.h"

/*
 * Header of this unit
 */

#include "dvo.h"

/*
 * Post-requisites: headers required by this unit
 */

/*
 * This unit
 */

enum gpio_result dal_dvo_open(
	struct dvo *dvo,
	enum gpio_mode mode)
{
	return dal_gpio_open(dvo->pin, mode);
}

enum gpio_result dal_dvo_get_value(
	const struct dvo *dvo,
	uint32_t *value)
{
	return dal_gpio_get_value(dvo->pin, value);
}

enum gpio_result dal_dvo_set_value(
	const struct dvo *dvo,
	uint32_t value)
{
	return dal_gpio_set_value(dvo->pin, value);
}

void dal_dvo_close(
	struct dvo *dvo)
{
	dal_gpio_close(dvo->pin);
}

/*
 * @brief
 * Creation and destruction
 */

struct dvo *dal_dvo_create(
	struct gpio_service *service,
	enum gpio_id id,
	uint32_t en)
{
	struct dvo *dvo;

	switch (id) {
	case GPIO_ID_DVO12:
		if ((en < GPIO_DVO12_MIN) || (en > GPIO_DVO12_MAX)) {
			BREAK_TO_DEBUGGER();
			return NULL;
		}
	break;
	case GPIO_ID_DVO24:
		if ((en < GPIO_DVO24_MIN) || (en > GPIO_DVO24_MAX)) {
			BREAK_TO_DEBUGGER();
			return NULL;
		}
	break;
	default:
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	dvo = dc_service_alloc(service->ctx, sizeof(struct dvo));

	if (!dvo) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	dvo->pin = NULL;
	dvo->ctx = service->ctx;

	return dvo;
}

void dal_dvo_destroy(
	struct dvo **dvo)
{
	if (!dvo || !*dvo) {
		BREAK_TO_DEBUGGER();
		return;
	}

	dal_dvo_close(*dvo);

	dc_service_free((*dvo)->ctx, *dvo);

	*dvo = NULL;
}
