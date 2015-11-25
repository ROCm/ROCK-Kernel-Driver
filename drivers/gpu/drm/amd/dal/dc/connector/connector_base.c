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

#include "connector.h"
#include "include/irq_interface.h"
#include "include/ddc_interface.h"
#include "include/connector_interface.h"

struct connector {
	struct graphics_object_id id;
	uint32_t input_signals;
	uint32_t output_signals;
	struct adapter_service *as;
	struct connector_feature_support features;
	struct connector_signals default_signals;
	struct dc_context *ctx;
};

static bool connector_construct(
	struct connector *connector,
	struct dc_context *ctx,
	struct adapter_service *as,
	struct graphics_object_id go_id)
{
	bool hw_ddc_polling = false;
	struct ddc *ddc;
	struct irq *hpd;
	enum connector_id connector_id;
	uint32_t signals_vector = 0;
	uint32_t signals_num = 0;
	uint32_t i;

	if (!as) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	connector->as = as;
	connector->id = go_id;
	connector->features.ddc_line = CHANNEL_ID_UNKNOWN;
	connector->features.hpd_line = HPD_SOURCEID_UNKNOWN;
	connector->ctx = ctx;

	ddc = dal_adapter_service_obtain_ddc(as, connector->id);
	hpd = dal_adapter_service_obtain_hpd_irq(as, connector->id);

	connector_id = dal_graphics_object_id_get_connector_id(go_id);

	/* Initialize DDC line */
	if (ddc) {
		switch (dal_ddc_get_line(ddc)) {
		case GPIO_DDC_LINE_DDC1:
			connector->features.ddc_line = CHANNEL_ID_DDC1;
			break;
		case GPIO_DDC_LINE_DDC2:
			connector->features.ddc_line = CHANNEL_ID_DDC2;
			break;
		case GPIO_DDC_LINE_DDC3:
			connector->features.ddc_line = CHANNEL_ID_DDC3;
			break;
		case GPIO_DDC_LINE_DDC4:
			connector->features.ddc_line = CHANNEL_ID_DDC4;
			break;
		case GPIO_DDC_LINE_DDC5:
			connector->features.ddc_line = CHANNEL_ID_DDC5;
			break;
		case GPIO_DDC_LINE_DDC6:
			connector->features.ddc_line = CHANNEL_ID_DDC6;
			break;
		case GPIO_DDC_LINE_DDC_VGA:
			connector->features.ddc_line = CHANNEL_ID_DDC_VGA;
			break;
		case GPIO_DDC_LINE_I2C_PAD:
			connector->features.ddc_line = CHANNEL_ID_I2C_PAD;
			break;
		default:
			BREAK_TO_DEBUGGER();
			break;
		}

		/* Initialize HW DDC polling support
		 * On DCE6.0 only DDC lines support HW polling (I2cPad does not)
		 */

		if (dal_adapter_service_is_feature_supported(
			FEATURE_ENABLE_HW_EDID_POLLING)) {
			switch (dal_ddc_get_line(ddc)) {
			case GPIO_DDC_LINE_DDC1:
			case GPIO_DDC_LINE_DDC2:
			case GPIO_DDC_LINE_DDC3:
			case GPIO_DDC_LINE_DDC4:
			case GPIO_DDC_LINE_DDC5:
			case GPIO_DDC_LINE_DDC6:
			case GPIO_DDC_LINE_DDC_VGA:
				hw_ddc_polling = true;
			break;
			default:
			break;
			}
		}

		dal_adapter_service_release_ddc(as, ddc);
	}

	/* Initialize HPD line */
	if (hpd) {
		switch (dal_irq_get_source(hpd)) {
		case DC_IRQ_SOURCE_HPD1:
			connector->features.hpd_line = HPD_SOURCEID1;
		break;
		case DC_IRQ_SOURCE_HPD2:
			connector->features.hpd_line = HPD_SOURCEID2;
		break;
		case DC_IRQ_SOURCE_HPD3:
			connector->features.hpd_line = HPD_SOURCEID3;
		break;
		case DC_IRQ_SOURCE_HPD4:
			connector->features.hpd_line = HPD_SOURCEID4;
		break;
		case DC_IRQ_SOURCE_HPD5:
			connector->features.hpd_line = HPD_SOURCEID5;
		break;
		case DC_IRQ_SOURCE_HPD6:
			connector->features.hpd_line = HPD_SOURCEID6;
		break;
		default:
			BREAK_TO_DEBUGGER();
		break;
		}

		dal_adapter_service_release_irq(as, hpd);
	}

	if ((uint32_t)connector_id >= number_of_default_signals &&
		(uint32_t)connector_id >= number_of_signals)
		return false;

	/* Initialize default signals */
	connector->default_signals = default_signals[connector_id];

	/* Fill supported signals */
	signals_num = supported_signals[connector_id].number_of_signals;
	for (i = 0; i < signals_num; i++)
		signals_vector |= supported_signals[connector_id].signal[i];

	/* Connector supports same set for input and output signals */
	connector->input_signals = signals_vector;
	connector->output_signals = signals_vector;

	switch (connector_id) {
	case CONNECTOR_ID_VGA:
		if (hw_ddc_polling
			&& connector->features.ddc_line != CHANNEL_ID_UNKNOWN)
			connector->features.HW_DDC_POLLING = true;
	break;
	case CONNECTOR_ID_SINGLE_LINK_DVII:
	case CONNECTOR_ID_DUAL_LINK_DVII:
		if (connector->features.hpd_line != HPD_SOURCEID_UNKNOWN)
			connector->features.HPD_FILTERING = true;
		if (hw_ddc_polling
			&& connector->features.ddc_line != CHANNEL_ID_UNKNOWN)
			connector->features.HW_DDC_POLLING = true;
	break;
	case CONNECTOR_ID_SINGLE_LINK_DVID:
	case CONNECTOR_ID_DUAL_LINK_DVID:
	case CONNECTOR_ID_HDMI_TYPE_A:
	case CONNECTOR_ID_LVDS:
	case CONNECTOR_ID_DISPLAY_PORT:
	case CONNECTOR_ID_EDP:
		if (connector->features.hpd_line != HPD_SOURCEID_UNKNOWN)
			connector->features.HPD_FILTERING = true;
	break;
	default:
		connector->features.HPD_FILTERING = false;
		connector->features.HW_DDC_POLLING = false;
	break;
	}

	return true;
}

struct connector *dal_connector_create(
	struct dc_context *ctx,
	struct adapter_service *as,
	struct graphics_object_id go_id)
{
	struct connector *connector = NULL;

	connector = dc_service_alloc(ctx, sizeof(struct connector));

	if (!connector) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	if (connector_construct(connector, ctx, as, go_id))
		return connector;

	BREAK_TO_DEBUGGER();

	dc_service_free(ctx, connector);

	return NULL;
}

void dal_connector_destroy(struct connector **connector)
{
	if (!connector || !*connector) {
		BREAK_TO_DEBUGGER();
		return;
	}

	dc_service_free((*connector)->ctx, *connector);

	*connector = NULL;
}

uint32_t dal_connector_enumerate_output_signals(
	const struct connector *connector)
{
	return connector->output_signals;
}

uint32_t dal_connector_enumerate_input_signals(
	const struct connector *connector)
{
	return connector->input_signals;
}

struct connector_signals dal_connector_get_default_signals(
		const struct connector *connector)
{
	return connector->default_signals;
}

const struct graphics_object_id dal_connector_get_graphics_object_id(
	const struct connector *connector)
{
	return connector->id;
}

/*
 *  Function: program_hpd_filter
 *
 *  @brief
 *     Programs HPD filter on associated HPD line
 *
 *  @param [in] delay_on_connect_in_ms: Connect filter timeout
 *  @param [in] delay_on_disconnect_in_ms: Disconnect filter timeout
 *
 *  @return
 *     true on success, false otherwise
 */
bool dal_connector_program_hpd_filter(
	const struct connector *connector,
	const uint32_t delay_on_connect_in_ms,
	const uint32_t delay_on_disconnect_in_ms)
{
	bool result = false;

	struct irq *hpd;

	/* Verify feature is supported */

	if (!connector->features.HPD_FILTERING)
		return result;

	/* Obtain HPD handle */

	hpd = dal_adapter_service_obtain_hpd_irq(
		connector->as, connector->id);

	if (!hpd)
		return result;

	/* Setup HPD filtering */

	if (GPIO_RESULT_OK == dal_irq_open(hpd)) {
		struct gpio_hpd_config config;

		config.delay_on_connect = delay_on_connect_in_ms;
		config.delay_on_disconnect = delay_on_disconnect_in_ms;

		dal_irq_setup_hpd_filter(hpd, &config);

		dal_irq_close(hpd);

		result = true;
	} else {
		ASSERT_CRITICAL(false);
	}

	/* Release HPD handle */

	dal_adapter_service_release_irq(connector->as, hpd);

	return result;
}

/*
 *  Function: setup_ddc_polling
 *
 *  @brief
 *     Enables/Disables HW polling on associated DDC line
 *
 *  @param [in] ddc_config: Specifies polling mode
 *
 *  @return
 *     true on success, false otherwise
 */
static bool setup_ddc_polling(
	const struct connector *connector,
	enum gpio_ddc_config_type ddc_config)
{
	bool result = false;

	struct ddc *ddc;

	/* Verify feature is supported */

	if (!connector->features.HW_DDC_POLLING)
		return result;

	/* Obtain DDC handle */

	ddc = dal_adapter_service_obtain_ddc(
		connector->as, connector->id);

	if (!ddc) {
		BREAK_TO_DEBUGGER();
		return result;
	}

	/* Setup DDC polling */

	if (GPIO_RESULT_OK == dal_ddc_open(ddc, GPIO_MODE_HARDWARE,
		GPIO_DDC_CONFIG_TYPE_MODE_I2C)) {
		dal_ddc_set_config(ddc, ddc_config);

		dal_ddc_close(ddc);

		result = true;
	} else {
		BREAK_TO_DEBUGGER();
	}

	/* Release DDC handle */

	dal_adapter_service_release_ddc(connector->as, ddc);

	return result;
}

/*
 *  Function: enable_ddc_polling
 *
 *  @brief
 *     Enables HW polling on associated DDC line
 *
 *  @param [in] is_poll_for_connect: Specifies polling mode
 *
 *  @return
 *     true on success, false otherwise
 */
bool dal_connector_enable_ddc_polling(
	const struct connector *connector,
	const bool is_poll_for_connect)
{
	enum gpio_ddc_config_type ddc_config = is_poll_for_connect ?
		GPIO_DDC_CONFIG_TYPE_POLL_FOR_CONNECT :
		GPIO_DDC_CONFIG_TYPE_POLL_FOR_DISCONNECT;

	return setup_ddc_polling(connector, ddc_config);
}

/*
 *  Function: disable_ddc_polling
 *
 *  @brief
 *     Disables HW polling on associated DDC line
 *
 *  @return
 *     true on success, false otherwise
 */
bool dal_connector_disable_ddc_polling(const struct connector *connector)
{
	return setup_ddc_polling(connector,
		GPIO_DDC_CONFIG_TYPE_DISABLE_POLLING);
}

void dal_connector_get_features(
		const struct connector *con,
		struct connector_feature_support *cfs)
{
	dc_service_memmove(cfs, &con->features,
			sizeof(struct connector_feature_support));
}
