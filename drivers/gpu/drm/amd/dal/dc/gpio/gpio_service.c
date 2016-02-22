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
#include "include/gpio_interface.h"
#include "include/ddc_interface.h"
#include "include/irq_interface.h"
#include "include/gpio_service_interface.h"
#include "hw_translate.h"
#include "hw_factory.h"

/*
 * Header of this unit
 */

#include "gpio_service.h"

/*
 * Post-requisites: headers required by this unit
 */

#include "hw_gpio_pin.h"
#include "gpio.h"
#include "ddc.h"
#include "irq.h"

/*
 * This unit
 */

/*
 * @brief
 * Public API.
 */

struct gpio_service *dal_gpio_service_create(
	enum dce_version dce_version_major,
	enum dce_version dce_version_minor,
	struct dc_context *ctx)
{
	struct gpio_service *service;

	uint32_t index_of_id;

	service = dm_alloc(sizeof(struct gpio_service));

	if (!service) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	if (!dal_hw_translate_init(&service->translate, dce_version_major,
			dce_version_minor)) {
		BREAK_TO_DEBUGGER();
		goto failure_1;
	}

	if (!dal_hw_factory_init(&service->factory, dce_version_major,
			dce_version_minor)) {
		BREAK_TO_DEBUGGER();
		goto failure_1;
	}

	/* allocate and initialize business storage */
	{
		const uint32_t bits_per_uint = sizeof(uint32_t) << 3;

		index_of_id = 0;
		service->ctx = ctx;

		do {
			uint32_t number_of_bits =
				service->factory.number_of_pins[index_of_id];

			uint32_t number_of_uints =
				(number_of_bits + bits_per_uint - 1) /
				bits_per_uint;

			uint32_t *slot;

			if (number_of_bits) {
				uint32_t index_of_uint = 0;

				slot = dm_alloc(number_of_uints * sizeof(uint32_t));

				if (!slot) {
					BREAK_TO_DEBUGGER();
					goto failure_2;
				}

				do {
					slot[index_of_uint] = 0;

					++index_of_uint;
				} while (index_of_uint < number_of_uints);
			} else
				slot = NULL;

			service->busyness[index_of_id] = slot;

			++index_of_id;
		} while (index_of_id < GPIO_ID_COUNT);
	}

	return service;

failure_2:
	while (index_of_id) {
		uint32_t *slot;

		--index_of_id;

		slot = service->busyness[index_of_id];

		if (slot)
			dm_free(slot);
	};

failure_1:
	dm_free(service);

	return NULL;
}

struct gpio *dal_gpio_service_create_gpio(
	struct gpio_service *service,
	uint32_t offset,
	uint32_t mask,
	enum gpio_pin_output_state output_state)
{
	enum gpio_id id;
	uint32_t en;

	if (!service->translate.funcs->offset_to_id(offset, mask, &id, &en)) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	return dal_gpio_create(service, id, en, output_state);
}

struct gpio *dal_gpio_service_create_gpio_ex(
	struct gpio_service *service,
	enum gpio_id id,
	uint32_t en,
	enum gpio_pin_output_state output_state)
{
	return dal_gpio_create(service, id, en, output_state);
}

void dal_gpio_service_destroy_gpio(
	struct gpio **gpio)
{
	dal_gpio_destroy(gpio);
}

struct ddc *dal_gpio_service_create_ddc(
	struct gpio_service *service,
	uint32_t offset,
	uint32_t mask,
	struct gpio_ddc_hw_info *info)
{
	return dal_gpio_create_ddc(service, offset, mask, info);
}

void dal_gpio_service_destroy_ddc(
	struct ddc **ddc)
{
	dal_gpio_destroy_ddc(ddc);
}

struct irq *dal_gpio_service_create_irq(
	struct gpio_service *service,
	uint32_t offset,
	uint32_t mask)
{
	enum gpio_id id;
	uint32_t en;

	if (!service->translate.funcs->offset_to_id(offset, mask, &id, &en)) {
		ASSERT_CRITICAL(false);
		return NULL;
	}

	return dal_gpio_create_irq(service, id, en);
}

struct irq *dal_gpio_service_create_irq_ex(
	struct gpio_service *service,
	enum gpio_id id,
	uint32_t en)
{
	return dal_gpio_create_irq(service, id, en);
}

void dal_gpio_service_destroy_irq(
	struct irq **irq)
{
	dal_gpio_destroy_irq(irq);
}

void dal_gpio_service_destroy(
	struct gpio_service **ptr)
{
	if (!ptr || !*ptr) {
		BREAK_TO_DEBUGGER();
		return;
	}

	/* free business storage */
	{
		uint32_t index_of_id = 0;

		do {
			uint32_t *slot = (*ptr)->busyness[index_of_id];

			if (slot)
				dm_free(slot);

			++index_of_id;
		} while (index_of_id < GPIO_ID_COUNT);
	}

	dm_free(*ptr);

	*ptr = NULL;
}

/*
 * @brief
 * Private API.
 */

static bool is_pin_busy(
	const struct gpio_service *service,
	enum gpio_id id,
	uint32_t en)
{
	const uint32_t bits_per_uint = sizeof(uint32_t) << 3;

	const uint32_t *slot = service->busyness[id] + (en / bits_per_uint);

	return 0 != (*slot & (1 << (en % bits_per_uint)));
}

static void set_pin_busy(
	struct gpio_service *service,
	enum gpio_id id,
	uint32_t en)
{
	const uint32_t bits_per_uint = sizeof(uint32_t) << 3;

	service->busyness[id][en / bits_per_uint] |=
		(1 << (en % bits_per_uint));
}

static void set_pin_free(
	struct gpio_service *service,
	enum gpio_id id,
	uint32_t en)
{
	const uint32_t bits_per_uint = sizeof(uint32_t) << 3;

	service->busyness[id][en / bits_per_uint] &=
		~(1 << (en % bits_per_uint));
}

enum gpio_result dal_gpio_service_open(
	struct gpio_service *service,
	enum gpio_id id,
	uint32_t en,
	enum gpio_mode mode,
	void *options,
	struct hw_gpio_pin **ptr)
{
	struct hw_gpio_pin *pin;

	if (!service->busyness[id]) {
		ASSERT_CRITICAL(false);
		return GPIO_RESULT_OPEN_FAILED;
	}

	if (is_pin_busy(service, id, en)) {
		ASSERT_CRITICAL(false);
		return GPIO_RESULT_DEVICE_BUSY;
	}

	switch (id) {
	case GPIO_ID_DDC_DATA:
		pin = service->factory.funcs->create_ddc_data(
			service->ctx, id, en);
	break;
	case GPIO_ID_DDC_CLOCK:
		pin = service->factory.funcs->create_ddc_clock(
			service->ctx, id, en);
	break;
	case GPIO_ID_GENERIC:
		pin = service->factory.funcs->create_generic(
			service->ctx, id, en);
	break;
	case GPIO_ID_HPD:
		pin = service->factory.funcs->create_hpd(
			service->ctx, id, en);
	break;
	case GPIO_ID_GPIO_PAD:
		pin = service->factory.funcs->create_gpio_pad(
			service->ctx, id, en);
	break;
	case GPIO_ID_SYNC:
		pin = service->factory.funcs->create_sync(
			service->ctx, id, en);
	break;
	case GPIO_ID_GSL:
		pin = service->factory.funcs->create_gsl(
			service->ctx, id, en);
	break;
	default:
		ASSERT_CRITICAL(false);
		return GPIO_RESULT_NON_SPECIFIC_ERROR;
	}

	if (!pin) {
		ASSERT_CRITICAL(false);
		return GPIO_RESULT_NON_SPECIFIC_ERROR;
	}

	if (!pin->funcs->open(pin, mode, options)) {
		ASSERT_CRITICAL(false);
		dal_gpio_service_close(service, &pin);
		return GPIO_RESULT_OPEN_FAILED;
	}

	set_pin_busy(service, id, en);
	*ptr = pin;
	return GPIO_RESULT_OK;
}

void dal_gpio_service_close(
	struct gpio_service *service,
	struct hw_gpio_pin **ptr)
{
	struct hw_gpio_pin *pin;

	if (!ptr) {
		ASSERT_CRITICAL(false);
		return;
	}

	pin = *ptr;

	if (pin) {
		set_pin_free(service, pin->id, pin->en);

		pin->funcs->close(pin);

		pin->funcs->destroy(ptr);
	}
}
