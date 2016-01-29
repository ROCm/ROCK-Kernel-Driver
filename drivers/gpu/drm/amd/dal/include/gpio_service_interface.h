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

#ifndef __DAL_GPIO_SERVICE_INTERFACE_H__
#define __DAL_GPIO_SERVICE_INTERFACE_H__

#include "gpio_types.h"
#include "gpio_interface.h"
#include "ddc_interface.h"
#include "irq_interface.h"

struct gpio_service;

struct gpio_service *dal_gpio_service_create(
	enum dce_version dce_version_major,
	enum dce_version dce_version_minor,
	struct dc_context *ctx);

struct gpio *dal_gpio_service_create_gpio(
	struct gpio_service *service,
	uint32_t offset,
	uint32_t mask,
	enum gpio_pin_output_state output_state);

struct gpio *dal_gpio_service_create_gpio_ex(
	struct gpio_service *service,
	enum gpio_id id,
	uint32_t en,
	enum gpio_pin_output_state output_state);

void dal_gpio_service_destroy_gpio(
	struct gpio **gpio);

struct ddc *dal_gpio_service_create_ddc(
	struct gpio_service *service,
	uint32_t offset,
	uint32_t mask,
	struct gpio_ddc_hw_info *info);

void dal_gpio_service_destroy_ddc(
	struct ddc **ddc);

struct irq *dal_gpio_service_create_irq(
	struct gpio_service *service,
	uint32_t offset,
	uint32_t mask);

struct irq *dal_gpio_service_create_irq_ex(
	struct gpio_service *service,
	enum gpio_id id,
	uint32_t en);

void dal_gpio_service_destroy_irq(
	struct irq **ptr);

void dal_gpio_service_destroy(
	struct gpio_service **ptr);

#endif
