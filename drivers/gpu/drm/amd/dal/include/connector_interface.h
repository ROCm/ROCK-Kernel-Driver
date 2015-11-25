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

#ifndef __DAL_CONNECTOR_INTERFACE_H__
#define __DAL_CONNECTOR_INTERFACE_H__

#include "adapter_service_interface.h"
#include "signal_types.h"

/* forward declaration */
struct connector;

struct connector_signals {
	const enum signal_type *signal;
	uint32_t number_of_signals;
};

struct connector_feature_support {
	bool HPD_FILTERING:1;
	bool HW_DDC_POLLING:1;
	enum hpd_source_id hpd_line;
	enum channel_id ddc_line;
};

void dal_connector_get_features(
		const struct connector *con,
		struct connector_feature_support *cfs);

struct connector *dal_connector_create(
	struct dc_context *ctx,
	struct adapter_service *as,
	struct graphics_object_id go_id);

void dal_connector_destroy(struct connector **connector);

void dal_connector_destroy(struct connector **connector);

const struct graphics_object_id dal_connector_get_graphics_object_id(
	const struct connector *connector);

uint32_t dal_connector_enumerate_output_signals(
	const struct connector *connector);
uint32_t dal_connector_enumerate_input_signals(
	const struct connector *connector);

struct connector_signals dal_connector_get_default_signals(
		const struct connector *connector);

bool dal_connector_program_hpd_filter(
	const struct connector *connector,
	const uint32_t delay_on_connect_in_ms,
	const uint32_t delay_on_disconnect_in_ms);

bool dal_connector_enable_ddc_polling(
	const struct connector *connector,
	const bool is_poll_for_connect);

bool dal_connector_disable_ddc_polling(const struct connector *connector);

#endif
