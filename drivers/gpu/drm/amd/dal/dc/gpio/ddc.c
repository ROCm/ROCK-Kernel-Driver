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
#include "include/gpio_service_interface.h"
#include "hw_gpio_pin.h"
#include "hw_translate.h"
#include "hw_factory.h"
#include "gpio_service.h"
#include "gpio.h"

/*
 * Header of this unit
 */

#include "ddc.h"

/*
 * Post-requisites: headers required by this unit
 */

/*
 * This unit
 */

enum gpio_result dal_ddc_open(
	struct ddc *ddc,
	enum gpio_mode mode,
	enum gpio_ddc_config_type config_type)
{
	enum gpio_result result;

	struct gpio_ddc_open_options data_options;
	struct gpio_ddc_open_options clock_options;
	struct gpio_config_data config_data;

	result = dal_gpio_open_ex(ddc->pin_data, mode, &data_options);

	if (result != GPIO_RESULT_OK) {
		BREAK_TO_DEBUGGER();
		return result;
	}

	result = dal_gpio_open_ex(ddc->pin_clock, mode, &clock_options);

	if (result != GPIO_RESULT_OK) {
		BREAK_TO_DEBUGGER();
		goto failure;
	}

	/* DDC clock and data pins should belong
	 * to the same DDC block id,
	 * we use the data pin to set the pad mode. */

	if (mode == GPIO_MODE_INPUT)
		/* this is from detect_sink_type,
		 * we need extra delay there */
		config_data.type = GPIO_CONFIG_TYPE_I2C_AUX_DUAL_MODE;
	else
		config_data.type = GPIO_CONFIG_TYPE_DDC;

	config_data.config.ddc.type = config_type;
	config_data.config.ddc.data_en_bit_present =
		data_options.en_bit_present;
	config_data.config.ddc.clock_en_bit_present =
		clock_options.en_bit_present;

	result = dal_gpio_set_config(ddc->pin_data, &config_data);

	if (result == GPIO_RESULT_OK)
		return result;

	BREAK_TO_DEBUGGER();

	dal_gpio_close(ddc->pin_clock);

failure:
	dal_gpio_close(ddc->pin_data);

	return result;
}

enum gpio_result dal_ddc_get_clock(
	const struct ddc *ddc,
	uint32_t *value)
{
	return dal_gpio_get_value(ddc->pin_clock, value);
}

enum gpio_result dal_ddc_set_clock(
	const struct ddc *ddc,
	uint32_t value)
{
	return dal_gpio_set_value(ddc->pin_clock, value);
}

enum gpio_result dal_ddc_get_data(
	const struct ddc *ddc,
	uint32_t *value)
{
	return dal_gpio_get_value(ddc->pin_data, value);
}

enum gpio_result dal_ddc_set_data(
	const struct ddc *ddc,
	uint32_t value)
{
	return dal_gpio_set_value(ddc->pin_data, value);
}

enum gpio_result dal_ddc_change_mode(
	struct ddc *ddc,
	enum gpio_mode mode)
{
	enum gpio_result result;

	enum gpio_mode original_mode =
		dal_gpio_get_mode(ddc->pin_data);

	result = dal_gpio_change_mode(ddc->pin_data, mode);

	/* [anaumov] DAL2 code returns GPIO_RESULT_NON_SPECIFIC_ERROR
	 * in case of failures;
	 * set_mode() is so that, in case of failure,
	 * we must explicitly set original mode */

	if (result != GPIO_RESULT_OK)
		goto failure;

	result = dal_gpio_change_mode(ddc->pin_clock, mode);

	if (result == GPIO_RESULT_OK)
		return result;

	dal_gpio_change_mode(ddc->pin_clock, original_mode);

failure:
	dal_gpio_change_mode(ddc->pin_data, original_mode);

	return result;
}

bool dal_ddc_is_hw_supported(
	const struct ddc *ddc)
{
	return ddc->hw_info.hw_supported;
}

enum gpio_ddc_line dal_ddc_get_line(
	const struct ddc *ddc)
{
	return (enum gpio_ddc_line)dal_gpio_get_enum(ddc->pin_data);
}

bool dal_ddc_check_line_aborted(
	const struct ddc *self)
{
	/* No arbitration with VBIOS is performed since DCE 6.0 */

	return false;
}

enum gpio_result dal_ddc_set_config(
	struct ddc *ddc,
	enum gpio_ddc_config_type config_type)
{
	struct gpio_config_data config_data;

	config_data.type = GPIO_CONFIG_TYPE_DDC;

	config_data.config.ddc.type = config_type;
	config_data.config.ddc.data_en_bit_present = false;
	config_data.config.ddc.clock_en_bit_present = false;

	return dal_gpio_set_config(ddc->pin_data, &config_data);
}

void dal_ddc_close(
	struct ddc *ddc)
{
	dal_gpio_close(ddc->pin_clock);
	dal_gpio_close(ddc->pin_data);
}

/*
 * @brief
 * Creation and destruction
 */

struct ddc *dal_gpio_create_ddc(
	struct gpio_service *service,
	uint32_t offset,
	uint32_t mask,
	struct gpio_ddc_hw_info *info)
{
	enum gpio_id id;
	uint32_t en;
	struct ddc *ddc;

	if (!service->translate.funcs->offset_to_id(offset, mask, &id, &en))
		return NULL;

	ddc = dm_alloc(sizeof(struct ddc));

	if (!ddc) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	ddc->pin_data = dal_gpio_service_create_gpio_ex(
		service, GPIO_ID_DDC_DATA, en, GPIO_PIN_OUTPUT_STATE_DEFAULT);

	if (!ddc->pin_data) {
		BREAK_TO_DEBUGGER();
		goto failure_1;
	}

	ddc->pin_clock = dal_gpio_service_create_gpio_ex(
		service, GPIO_ID_DDC_CLOCK, en, GPIO_PIN_OUTPUT_STATE_DEFAULT);

	if (!ddc->pin_clock) {
		BREAK_TO_DEBUGGER();
		goto failure_2;
	}

	ddc->hw_info = *info;

	ddc->ctx = service->ctx;

	return ddc;

failure_2:
	dal_gpio_service_destroy_gpio(&ddc->pin_data);

failure_1:
	dm_free(ddc);

	return NULL;
}

static void destruct(struct ddc *ddc)
{
	dal_ddc_close(ddc);
	dal_gpio_service_destroy_gpio(&ddc->pin_data);
	dal_gpio_service_destroy_gpio(&ddc->pin_clock);

}

void dal_gpio_destroy_ddc(
	struct ddc **ddc)
{
	if (!ddc || !*ddc) {
		BREAK_TO_DEBUGGER();
		return;
	}

	destruct(*ddc);
	dm_free(*ddc);

	*ddc = NULL;
}
