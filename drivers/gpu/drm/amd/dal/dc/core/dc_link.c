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

#include "dc_services.h"
#include "dc_helpers.h"
#include "dc.h"
#include "core_dc.h"
#include "adapter_service_interface.h"
#include "grph_object_id.h"
#include "connector_interface.h"
#include "gpio_service_interface.h"
#include "ddc_service_interface.h"
#include "core_status.h"
#include "dc_link_dp.h"
#include "link_hwss.h"
#include "stream_encoder.h"
#include "link_encoder.h"
#include "hw_sequencer.h"


#define LINK_INFO(...) \
	dal_logger_write(dc_ctx->logger, \
		LOG_MAJOR_HW_TRACE, LOG_MINOR_HW_TRACE_HOTPLUG, \
		__VA_ARGS__)

/*******************************************************************************
 * Private structures
 ******************************************************************************/


/*******************************************************************************
 * Private functions
 ******************************************************************************/
static void destruct(struct core_link *link)
{
	if (link->connector)
		dal_connector_destroy(&link->connector);

	if (link->ddc)
		dal_ddc_service_destroy(&link->ddc);

	if(link->link_enc)
		link->ctx->dc->hwss.encoder_destroy(&link->link_enc);
}

static bool detect_sink(struct core_link *link)
{
	uint32_t is_hpd_high = 0;
	struct irq *hpd_pin;

	/* todo: may need to lock gpio access */
	hpd_pin = dal_adapter_service_obtain_hpd_irq(
			link->adapter_srv,
			link->link_id);
	if (hpd_pin == NULL)
		goto hpd_gpio_failure;

	dal_irq_open(hpd_pin);
	dal_irq_get_value(hpd_pin, &is_hpd_high);
	dal_irq_close(hpd_pin);
	dal_adapter_service_release_irq(
		link->adapter_srv,
		hpd_pin);

	if (is_hpd_high) {
		link->public.type = dc_connection_single;
		/* TODO: need to do the actual detection */
	} else {
		link->public.type = dc_connection_none;
	}

	return true;

hpd_gpio_failure:
	return false;
}


enum ddc_transaction_type get_ddc_transaction_type(
		enum signal_type sink_signal)
{
	enum ddc_transaction_type transaction_type = DDC_TRANSACTION_TYPE_NONE;


	switch (sink_signal) {
	case SIGNAL_TYPE_DVI_SINGLE_LINK:
	case SIGNAL_TYPE_DVI_DUAL_LINK:
	case SIGNAL_TYPE_HDMI_TYPE_A:
	case SIGNAL_TYPE_LVDS:
	case SIGNAL_TYPE_RGB:
		transaction_type = DDC_TRANSACTION_TYPE_I2C;
		break;

	case SIGNAL_TYPE_DISPLAY_PORT:
	case SIGNAL_TYPE_EDP:
		transaction_type = DDC_TRANSACTION_TYPE_I2C_OVER_AUX;
		break;

	case SIGNAL_TYPE_DISPLAY_PORT_MST:
		/* MST does not use I2COverAux, but there is the
		 * SPECIAL use case for "immediate dwnstrm device
		 * access" (EPR#370830). */
		transaction_type = DDC_TRANSACTION_TYPE_I2C_OVER_AUX;
		break;

	default:
		break;
	}


	return transaction_type;
}

static enum signal_type get_basic_signal_type(
	struct graphics_object_id encoder,
	struct graphics_object_id downstream)
{
	if (downstream.type == OBJECT_TYPE_CONNECTOR) {
		switch (downstream.id) {
		case CONNECTOR_ID_SINGLE_LINK_DVII:
			switch (encoder.id) {
			case ENCODER_ID_INTERNAL_DAC1:
			case ENCODER_ID_INTERNAL_KLDSCP_DAC1:
			case ENCODER_ID_INTERNAL_DAC2:
			case ENCODER_ID_INTERNAL_KLDSCP_DAC2:
				return SIGNAL_TYPE_RGB;
			default:
				return SIGNAL_TYPE_DVI_SINGLE_LINK;
			}
		break;
		case CONNECTOR_ID_DUAL_LINK_DVII:
		{
			switch (encoder.id) {
			case ENCODER_ID_INTERNAL_DAC1:
			case ENCODER_ID_INTERNAL_KLDSCP_DAC1:
			case ENCODER_ID_INTERNAL_DAC2:
			case ENCODER_ID_INTERNAL_KLDSCP_DAC2:
				return SIGNAL_TYPE_RGB;
			default:
				return SIGNAL_TYPE_DVI_DUAL_LINK;
			}
		}
		break;
		case CONNECTOR_ID_SINGLE_LINK_DVID:
			return SIGNAL_TYPE_DVI_SINGLE_LINK;
		case CONNECTOR_ID_DUAL_LINK_DVID:
			return SIGNAL_TYPE_DVI_DUAL_LINK;
		case CONNECTOR_ID_VGA:
			return SIGNAL_TYPE_RGB;
		case CONNECTOR_ID_HDMI_TYPE_A:
			return SIGNAL_TYPE_HDMI_TYPE_A;
		case CONNECTOR_ID_LVDS:
			return SIGNAL_TYPE_LVDS;
		case CONNECTOR_ID_DISPLAY_PORT:
			return SIGNAL_TYPE_DISPLAY_PORT;
		case CONNECTOR_ID_EDP:
			return SIGNAL_TYPE_EDP;
		default:
			return SIGNAL_TYPE_NONE;
		}
	} else if (downstream.type == OBJECT_TYPE_ENCODER) {
		switch (downstream.id) {
		case ENCODER_ID_EXTERNAL_NUTMEG:
		case ENCODER_ID_EXTERNAL_TRAVIS:
			return SIGNAL_TYPE_DISPLAY_PORT;
		default:
			return SIGNAL_TYPE_NONE;
		}
	}

	return SIGNAL_TYPE_NONE;
}

/*
 * @brief
 * Check whether there is a dongle on DP connector
 */
static bool is_dp_sink_present(struct core_link *link)
{
	enum gpio_result gpio_result;
	uint32_t clock_pin = 0;
	uint32_t data_pin = 0;

	struct ddc *ddc;

	enum connector_id connector_id =
		dal_graphics_object_id_get_connector_id(link->link_id);

	bool present =
		((connector_id == CONNECTOR_ID_DISPLAY_PORT) ||
		(connector_id == CONNECTOR_ID_EDP));

	ddc = dal_adapter_service_obtain_ddc(link->adapter_srv, link->link_id);

	if (!ddc)
		return present;

	/* Open GPIO and set it to I2C mode */
	/* Note: this GpioMode_Input will be converted
	 * to GpioConfigType_I2cAuxDualMode in GPIO component,
	 * which indicates we need additional delay */

	if (GPIO_RESULT_OK != dal_ddc_open(
		ddc, GPIO_MODE_INPUT, GPIO_DDC_CONFIG_TYPE_MODE_I2C)) {
		dal_adapter_service_release_ddc(link->adapter_srv, ddc);

		return present;
	}

	/* Read GPIO: DP sink is present if both clock and data pins are zero */
	/* [anaumov] in DAL2, there was no check for GPIO failure */

	gpio_result = dal_ddc_get_clock(ddc, &clock_pin);
	ASSERT(gpio_result == GPIO_RESULT_OK);

	if (gpio_result == GPIO_RESULT_OK)
		if (link->link_enc->features.flags.bits.
						DP_SINK_DETECT_POLL_DATA_PIN)
			gpio_result = dal_ddc_get_data(ddc, &data_pin);

	present = (gpio_result == GPIO_RESULT_OK) && !(clock_pin || data_pin);

	dal_ddc_close(ddc);

	dal_adapter_service_release_ddc(link->adapter_srv, ddc);

	return present;
}

/*
 * @brief
 * Detect output sink type
 */
static enum signal_type link_detect_sink(struct core_link *link)
{
	enum signal_type result = get_basic_signal_type(
		link->link_enc->id, link->link_id);

	/* Internal digital encoder will detect only dongles
	 * that require digital signal */

	/* Detection mechanism is different
	 * for different native connectors.
	 * LVDS connector supports only LVDS signal;
	 * PCIE is a bus slot, the actual connector needs to be detected first;
	 * eDP connector supports only eDP signal;
	 * HDMI should check straps for audio */

	/* PCIE detects the actual connector on add-on board */

	if (link->link_id.id == CONNECTOR_ID_PCIE) {
		/* ZAZTODO implement PCIE add-on card detection */
	}

	switch (link->link_id.id) {
	case CONNECTOR_ID_HDMI_TYPE_A: {
		/* check audio support:
		 * if native HDMI is not supported, switch to DVI */
		union audio_support audio_support =
			dal_adapter_service_get_audio_support(
				link->adapter_srv);

		if (!audio_support.bits.HDMI_AUDIO_NATIVE)
			if (link->link_id.id == CONNECTOR_ID_HDMI_TYPE_A)
				result = SIGNAL_TYPE_DVI_SINGLE_LINK;
	}
	break;
	case CONNECTOR_ID_DISPLAY_PORT: {

		/* Check whether DP signal detected: if not -
		 * we assume signal is DVI; it could be corrected
		 * to HDMI after dongle detection */
		if (!is_dp_sink_present(link))
			result = SIGNAL_TYPE_DVI_SINGLE_LINK;
	}
	break;
	default:
	break;
	}

	return result;
}

static enum signal_type decide_signal_from_strap_and_dongle_type(
		enum display_dongle_type dongle_type,
		union audio_support *audio_support)
{
	enum signal_type signal = SIGNAL_TYPE_NONE;

	switch (dongle_type) {
	case DISPLAY_DONGLE_DP_HDMI_DONGLE:
		if (audio_support->bits.HDMI_AUDIO_ON_DONGLE)
			signal =  SIGNAL_TYPE_HDMI_TYPE_A;
		else
			signal = SIGNAL_TYPE_DVI_SINGLE_LINK;
		break;
	case DISPLAY_DONGLE_DP_DVI_DONGLE:
		signal = SIGNAL_TYPE_DVI_SINGLE_LINK;
		break;
	case DISPLAY_DONGLE_DP_HDMI_MISMATCHED_DONGLE:
		if (audio_support->bits.HDMI_AUDIO_NATIVE)
			signal =  SIGNAL_TYPE_HDMI_TYPE_A;
		else
			signal = SIGNAL_TYPE_DVI_SINGLE_LINK;
		break;
	default:
		signal = SIGNAL_TYPE_NONE;
		break;
	}

	return signal;
}

static enum signal_type dp_passive_dongle_detection(
		struct ddc_service *ddc,
		struct display_sink_capability *sink_cap,
		union audio_support *audio_support)
{
	/* TODO:These 2 functions should be protected for upstreaming purposes
	 * in case hackers want to save 10 cents hdmi license fee
	 */
	dal_ddc_service_i2c_query_dp_dual_mode_adaptor(
						ddc, sink_cap);
	return decide_signal_from_strap_and_dongle_type(
			sink_cap->dongle_type,
			audio_support);
}

static bool is_dp_active_dongle(enum display_dongle_type dongle_type)
{
	return (dongle_type == DISPLAY_DONGLE_DP_VGA_CONVERTER ||
		dongle_type == DISPLAY_DONGLE_DP_DVI_CONVERTER ||
		dongle_type == DISPLAY_DONGLE_DP_HDMI_CONVERTER);
}

static void link_disconnect_all_sinks(struct core_link *link)
{
	int i;

	for (i = 0; i < link->public.sink_count; i++)
		dc_link_remove_sink(&link->public, link->public.sink[i]);

	link->dpcd_sink_count = 0;
}

static enum dc_edid_status read_edid(
	struct core_link *link,
	struct core_sink *sink)
{
	uint32_t edid_retry = 3;
	enum dc_edid_status edid_status;

	/* some dongles read edid incorrectly the first time,
	 * do check sum and retry to make sure read correct edid.
	 */
	do {
		sink->public.dc_edid.length =
				dal_ddc_service_edid_query(link->ddc);

		if (0 == sink->public.dc_edid.length)
			return EDID_NO_RESPONSE;

		dal_ddc_service_get_edid_buf(link->ddc,
				sink->public.dc_edid.raw_edid);
		edid_status = dc_helpers_parse_edid_caps(
				sink->ctx,
				&sink->public.dc_edid,
				&sink->public.edid_caps);
		--edid_retry;
		if (edid_status == EDID_BAD_CHECKSUM)
			dal_logger_write(link->ctx->logger,
					LOG_MAJOR_WARNING,
					LOG_MINOR_DETECTION_EDID_PARSER,
					"Bad EDID checksum, retry remain: %d\n",
					edid_retry);
	} while (edid_status == EDID_BAD_CHECKSUM && edid_retry > 0);

	return edid_status;
}

void dc_link_detect(const struct dc_link *dc_link)
{
	struct core_link *link = DC_LINK_TO_LINK(dc_link);
	struct sink_init_data sink_init_data = { 0 };
	enum ddc_transaction_type transaction_type = DDC_TRANSACTION_TYPE_NONE;
	struct display_sink_capability sink_caps = { 0 };
	uint8_t i;
	enum signal_type signal = SIGNAL_TYPE_NONE;
	bool converter_disable_audio = false;
	union audio_support audio_support =
		dal_adapter_service_get_audio_support(
			link->adapter_srv);
	enum dc_edid_status edid_status;
	struct dc_context *dc_ctx = link->ctx;
	struct dc_sink *dc_sink;
	struct core_sink *sink = NULL;

	if (false == detect_sink(link)) {
		BREAK_TO_DEBUGGER();
		return;
	}

	if (link->public.type != dc_connection_none) {
		/* From Disconnected-to-Connected. */
		switch (link->public.connector_signal) {
		case SIGNAL_TYPE_HDMI_TYPE_A: {
			transaction_type = DDC_TRANSACTION_TYPE_I2C;
			if (audio_support.bits.HDMI_AUDIO_NATIVE)
				signal = SIGNAL_TYPE_HDMI_TYPE_A;
			else
				signal = SIGNAL_TYPE_DVI_SINGLE_LINK;
			break;
		}

		case SIGNAL_TYPE_DVI_SINGLE_LINK: {
			transaction_type = DDC_TRANSACTION_TYPE_I2C;
			signal = SIGNAL_TYPE_DVI_SINGLE_LINK;
			break;
		}

		case SIGNAL_TYPE_DVI_DUAL_LINK: {
			transaction_type = DDC_TRANSACTION_TYPE_I2C;
			signal = SIGNAL_TYPE_DVI_DUAL_LINK;
			break;
		}

		case SIGNAL_TYPE_EDP: {
			detect_dp_sink_caps(link);
			transaction_type = DDC_TRANSACTION_TYPE_I2C_OVER_AUX;
			signal = SIGNAL_TYPE_EDP;
			break;
		}

		case SIGNAL_TYPE_DISPLAY_PORT: {
			signal = link_detect_sink(link);
			transaction_type = get_ddc_transaction_type(
					signal);

			if (transaction_type ==
				DDC_TRANSACTION_TYPE_I2C_OVER_AUX) {
				signal =
					SIGNAL_TYPE_DISPLAY_PORT;
				detect_dp_sink_caps(link);

				/* DP active dongles */
				if (is_dp_active_dongle(
					link->dpcd_caps.dongle_type)) {
					if (!link->dpcd_caps.
						sink_count.bits.SINK_COUNT) {
						link->public.type =
							dc_connection_none;
						/* active dongle unplug
						 * processing for short irq
						 */
						link_disconnect_all_sinks(link);
						return;
					}

					if (link->dpcd_caps.dongle_type !=
					DISPLAY_DONGLE_DP_HDMI_CONVERTER) {
						converter_disable_audio = true;
					}
				}
				if (is_mst_supported(link)) {
					signal = SIGNAL_TYPE_DISPLAY_PORT_MST;

					/*
					 * This call will initiate MST topology
					 * discovery. Which will detect
					 * MST ports and add new DRM connector
					 * DRM framework. Then read EDID via
					 * remote i2c over aux.In the end, will
					 * notify DRM detect result and save
					 * EDID into DRM framework.
					 *
					 * .detect is called by .fill_modes.
					 * .fill_modes is called by user mode
					 *  ioctl DRM_IOCTL_MODE_GETCONNECTOR.
					 *
					 * .get_modes is called by .fill_modes.
					 *
					 * call .get_modes, AMDGPU DM
					 * implementation will create new
					 * dc_sink and add to dc_link.
					 * For long HPD plug in/out, MST has its
					 * own handle.
					 *
					 * Therefore, just after dc_create,
					 * link->sink is not created for MST
					 * until user mode app calls
					 * DRM_IOCTL_MODE_GETCONNECTOR.
					 *
					 * Need check ->sink usages in case
					 * ->sink = NULL
					 * TODO: s3 resume check*/

					if (dc_helpers_dp_mst_start_top_mgr(link->ctx, &link->public)) {
						return;
					} else {
						/* MST not supported */
						signal = SIGNAL_TYPE_DISPLAY_PORT;
					}
				}
			}
			else {
				/* DP passive dongles */
				signal = dp_passive_dongle_detection(link->ddc,
						&sink_caps,
						&audio_support);
			}
			break;
		}

		default:
			DC_ERROR("Invalid connector type! signal:%d\n",
				link->public.connector_signal);
			return;
		} /* switch() */

		if (link->dpcd_caps.sink_count.bits.SINK_COUNT)
			link->dpcd_sink_count = link->dpcd_caps.sink_count.
					bits.SINK_COUNT;
			else
				link->dpcd_sink_count = 1;


		dal_ddc_service_set_transaction_type(
						link->ddc,
						transaction_type);

		sink_init_data.link = &link->public;
		sink_init_data.sink_signal = signal;
		sink_init_data.dongle_max_pix_clk =
			sink_caps.max_hdmi_pixel_clock;
		sink_init_data.converter_disable_audio =
				converter_disable_audio;

		dc_sink = sink_create(&sink_init_data);
		if (!dc_sink) {
			DC_ERROR("Failed to create sink!\n");
			return;
		}

		sink = DC_SINK_TO_CORE(dc_sink);

		/*AG TODO handle failure */
		/*Only non MST case here */
		if (!dc_link_add_sink(&link->public, &sink->public))
				BREAK_TO_DEBUGGER();

		edid_status = read_edid(link, sink);

		switch (edid_status) {
		case EDID_BAD_CHECKSUM:
			dal_logger_write(link->ctx->logger,
				LOG_MAJOR_ERROR,
				LOG_MINOR_DETECTION_EDID_PARSER,
				"EDID checksum invalid.\n");
			break;
		case EDID_NO_RESPONSE:
			dal_logger_write(link->ctx->logger,
				LOG_MAJOR_ERROR,
				LOG_MINOR_DETECTION_EDID_PARSER,
				"No EDID read.\n");
			return;

		default:
			break;
		}

		dal_logger_write(link->ctx->logger,
			LOG_MAJOR_DETECTION,
			LOG_MINOR_DETECTION_EDID_PARSER,
			"%s: "
			"manufacturer_id = %X, "
			"product_id = %X, "
			"serial_number = %X, "
			"manufacture_week = %d, "
			"manufacture_year = %d, "
			"display_name = %s, "
			"speaker_flag = %d, "
			"audio_mode_count = %d\n",
			__func__,
			sink->public.edid_caps.manufacturer_id,
			sink->public.edid_caps.product_id,
			sink->public.edid_caps.serial_number,
			sink->public.edid_caps.manufacture_week,
			sink->public.edid_caps.manufacture_year,
			sink->public.edid_caps.display_name,
			sink->public.edid_caps.speaker_flags,
			sink->public.edid_caps.audio_mode_count);

		for (i = 0; i < sink->public.edid_caps.audio_mode_count; i++) {
			dal_logger_write(link->ctx->logger,
				LOG_MAJOR_DETECTION,
				LOG_MINOR_DETECTION_EDID_PARSER,
				"%s: mode number = %d, "
				"format_code = %d, "
				"channel_count = %d, "
				"sample_rate = %d, "
				"sample_size = %d\n",
				__func__,
				i,
				sink->public.edid_caps.audio_modes[i].format_code,
				sink->public.edid_caps.audio_modes[i].channel_count,
				sink->public.edid_caps.audio_modes[i].sample_rate,
				sink->public.edid_caps.audio_modes[i].sample_size);
		}

	} else {
		/* From Connected-to-Disconnected. */
		switch (link->public.connector_signal) {
		case SIGNAL_TYPE_DISPLAY_PORT:
			dc_helpers_dp_mst_stop_top_mgr(link->ctx, &link->public);
			break;
		default:
			break;
		}
		link_disconnect_all_sinks(link);
	}

	LINK_INFO("link=%d, dc_sink_in=%p is now %s\n",
		link->link_index, &sink->public,
		(signal == SIGNAL_TYPE_NONE ? "Disconnected":"Connected"));

	/* TODO: */

	return;
}

static bool construct(
	struct core_link *link,
	const struct link_init_data *init_params)
{
	struct irq *hpd_gpio = NULL;
	struct ddc_service_init_data ddc_service_init_data = { 0 };
	struct dc_context *dc_ctx = init_params->ctx;
	struct encoder_init_data enc_init_data = { 0 };
	struct connector_feature_support cfs = { 0 };

	link->dc = init_params->dc;
	link->adapter_srv = init_params->adapter_srv;
	link->connector_index = init_params->connector_index;
	link->ctx = dc_ctx;
	link->link_index = init_params->link_index;

	link->link_id = dal_adapter_service_get_connector_obj_id(
			init_params->adapter_srv,
			init_params->connector_index);

	if (link->link_id.type != OBJECT_TYPE_CONNECTOR) {
		dal_error("%s: Invalid Connector ObjectID from Adapter Service for connector index:%d!\n",
				__func__, init_params->connector_index);
		goto create_fail;
	}

	switch (link->link_id.id) {
	case CONNECTOR_ID_HDMI_TYPE_A:
		link->public.connector_signal = SIGNAL_TYPE_HDMI_TYPE_A;
		break;
	case CONNECTOR_ID_SINGLE_LINK_DVID:
	case CONNECTOR_ID_SINGLE_LINK_DVII:
		link->public.connector_signal = SIGNAL_TYPE_DVI_SINGLE_LINK;
		break;
	case CONNECTOR_ID_DUAL_LINK_DVID:
	case CONNECTOR_ID_DUAL_LINK_DVII:
		link->public.connector_signal = SIGNAL_TYPE_DVI_DUAL_LINK;
		break;
	case CONNECTOR_ID_DISPLAY_PORT:
		link->public.connector_signal =	SIGNAL_TYPE_DISPLAY_PORT;
		hpd_gpio = dal_adapter_service_obtain_hpd_irq(
					init_params->adapter_srv,
					link->link_id);

		if (hpd_gpio != NULL) {
			link->public.irq_source_hpd_rx =
					dal_irq_get_rx_source(hpd_gpio);
			dal_adapter_service_release_irq(
					init_params->adapter_srv, hpd_gpio);
		}

		break;
	case CONNECTOR_ID_EDP:
		link->public.connector_signal = SIGNAL_TYPE_EDP;
		hpd_gpio = dal_adapter_service_obtain_hpd_irq(
					init_params->adapter_srv,
					link->link_id);

		if (hpd_gpio != NULL) {
			link->public.irq_source_hpd_rx =
					dal_irq_get_rx_source(hpd_gpio);
			dal_adapter_service_release_irq(
					init_params->adapter_srv, hpd_gpio);
		}
		break;
	default:
		dal_logger_write(dc_ctx->logger,
			LOG_MAJOR_WARNING, LOG_MINOR_TM_LINK_SRV,
			"Unsupported Connector type:%d!\n", link->link_id.id);
		goto create_fail;
	}

	/* TODO: #DAL3 Implement id to str function.*/
	LINK_INFO("Connector[%d] description:\n",
			init_params->connector_index);

	link->connector = dal_connector_create(dc_ctx,
			init_params->adapter_srv,
			link->link_id);
	if (NULL == link->connector) {
		DC_ERROR("Failed to create connector object!\n");
		goto create_fail;
	}


	hpd_gpio = dal_adapter_service_obtain_hpd_irq(
			init_params->adapter_srv,
			link->link_id);

	if (hpd_gpio != NULL) {
		link->public.irq_source_hpd = dal_irq_get_source(hpd_gpio);
		dal_adapter_service_release_irq(
					init_params->adapter_srv, hpd_gpio);
	}

	ddc_service_init_data.as = link->adapter_srv;
	ddc_service_init_data.ctx = link->ctx;
	ddc_service_init_data.id = link->link_id;
	link->ddc = dal_ddc_service_create(&ddc_service_init_data);

	if (NULL == link->ddc) {
		DC_ERROR("Failed to create ddc_service!\n");
		goto create_fail;
	}

	dal_connector_get_features(link->connector, &cfs);

	enc_init_data.adapter_service = link->adapter_srv;
	enc_init_data.ctx = dc_ctx;
	enc_init_data.encoder = dal_adapter_service_get_src_obj(
					link->adapter_srv, link->link_id, 0);
	enc_init_data.connector = link->link_id;
	enc_init_data.channel = cfs.ddc_line;
	enc_init_data.hpd_source = cfs.hpd_line;
	link->link_enc = dc_ctx->dc->hwss.encoder_create(&enc_init_data);

	if( link->link_enc == NULL) {
		DC_ERROR("Failed to create link encoder!\n");
		goto create_fail;
	}

	/*
	 * TODO check if GPIO programmed correctly
	 *
	 * If GPIO isn't programmed correctly HPD might not rise or drain
	 * fast enough, leading to bounces.
	 */
#define DELAY_ON_CONNECT_IN_MS 500
#define DELAY_ON_DISCONNECT_IN_MS 500

	dal_connector_program_hpd_filter(
		link->connector,
		DELAY_ON_CONNECT_IN_MS,
		DELAY_ON_DISCONNECT_IN_MS);

	return true;

create_fail:
	return false;
}

/*******************************************************************************
 * Public functions
 ******************************************************************************/
struct core_link *link_create(const struct link_init_data *init_params)
{
	struct core_link *link =
			dc_service_alloc(init_params->ctx, sizeof(*link));
	link->ctx = init_params->ctx;

	if (NULL == link)
		goto alloc_fail;

	if (false == construct(link, init_params))
		goto construct_fail;

	return link;

construct_fail:
	dc_service_free(init_params->ctx, link);

alloc_fail:
	return NULL;
}

void link_destroy(struct core_link **link)
{
	destruct(*link);
	dc_service_free((*link)->ctx, *link);
	*link = NULL;
}

static void dpcd_configure_panel_mode(
	struct core_link *link,
	enum dp_panel_mode panel_mode)
{
	union dpcd_edp_config edp_config_set;
	bool panel_mode_edp = false;

	dc_service_memset(&edp_config_set, '\0', sizeof(union dpcd_edp_config));

	if (DP_PANEL_MODE_DEFAULT != panel_mode) {

		switch (panel_mode) {
		case DP_PANEL_MODE_EDP:
		case DP_PANEL_MODE_SPECIAL:
			panel_mode_edp = true;
			break;

		default:
			break;
		}

		/*set edp panel mode in receiver*/
		core_link_read_dpcd(
			link,
			DPCD_ADDRESS_EDP_CONFIG_SET,
			&edp_config_set.raw,
			sizeof(edp_config_set.raw));

		if (edp_config_set.bits.PANEL_MODE_EDP
			!= panel_mode_edp) {
			enum ddc_result result = DDC_RESULT_UNKNOWN;

			edp_config_set.bits.PANEL_MODE_EDP =
			panel_mode_edp;
			result = core_link_write_dpcd(
				link,
				DPCD_ADDRESS_EDP_CONFIG_SET,
				&edp_config_set.raw,
				sizeof(edp_config_set.raw));

			ASSERT(result == DDC_RESULT_SUCESSFULL);
		}
	}
	dal_logger_write(link->ctx->logger, LOG_MAJOR_DETECTION,
			LOG_MINOR_DETECTION_DP_CAPS,
			"Connector: %d eDP panel mode supported: %d "
			"eDP panel mode enabled: %d \n",
			link->connector_index,
			link->dpcd_caps.panel_mode_edp,
			panel_mode_edp);
}

static enum dc_status enable_link_dp(struct core_stream *stream)
{
	enum dc_status status;
	bool skip_video_pattern;
	struct core_link *link = stream->sink->link;
	struct link_settings link_settings = {0};
	enum dp_panel_mode panel_mode;

	/* get link settings for video mode timing */
	decide_link_settings(stream, &link_settings);
	dp_enable_link_phy(
		stream->sink->link,
		stream->signal,
		stream->stream_enc->id,
		&link_settings);

	panel_mode = dp_get_panel_mode(link);
	dpcd_configure_panel_mode(link, panel_mode);

	skip_video_pattern = true;

	if (link_settings.link_rate == LINK_RATE_LOW)
			skip_video_pattern = false;

	if (perform_link_training(link, &link_settings, skip_video_pattern)) {
		link->cur_link_settings = link_settings;
		status = DC_OK;
	}
	else
		status = DC_ERROR_UNEXPECTED;

	return status;
}

static enum dc_status enable_link_dp_mst(struct core_stream *stream)
{
	struct core_link *link = stream->sink->link;
	bool already_enabled = false;
	int i;

	for (i = 0; i < link->enabled_stream_count; i++) {
		if (link->enabled_streams[i] == stream)
			already_enabled = true;
	}

	if (!already_enabled && link->enabled_stream_count < MAX_SINKS_PER_LINK)
		link->enabled_streams[link->enabled_stream_count++] = stream;
	else if (link->enabled_stream_count >= MAX_SINKS_PER_LINK)
		return DC_ERROR_UNEXPECTED;

	/* sink signal type after MST branch is MST. Multiple MST sinks
	 * share one link. Link DP PHY is enable or training only once.
	 */
	if (link->cur_link_settings.lane_count != LANE_COUNT_UNKNOWN)
		return DC_OK;

	return enable_link_dp(stream);
}

static void enable_link_hdmi(struct core_stream *stream)
{
	struct core_link *link = stream->sink->link;

	/* enable video output */
	/* here we need to specify that encoder output settings
	 * need to be calculated as for the set mode,
	 * it will lead to querying dynamic link capabilities
	 * which should be done before enable output */
	uint32_t normalized_pix_clk = stream->public.timing.pix_clk_khz;
	switch (stream->public.timing.display_color_depth) {
	case COLOR_DEPTH_888:
		break;
	case COLOR_DEPTH_101010:
		normalized_pix_clk = (normalized_pix_clk * 30) / 24;
		break;
	case COLOR_DEPTH_121212:
		normalized_pix_clk = (normalized_pix_clk * 36) / 24;
		break;
	case COLOR_DEPTH_161616:
		normalized_pix_clk = (normalized_pix_clk * 48) / 24;
		break;
	default:
		break;
	}

	if (stream->signal == SIGNAL_TYPE_HDMI_TYPE_A)
		dal_ddc_service_write_scdc_data(
			stream->sink->link->ddc,
			normalized_pix_clk,
			stream->public.timing.flags.LTE_340MCSC_SCRAMBLE);

	stream->sink->link->cur_link_settings.lane_count =
		(stream->signal == SIGNAL_TYPE_DVI_DUAL_LINK)
					? LANE_COUNT_EIGHT : LANE_COUNT_FOUR;

	link->ctx->dc->hwss.encoder_enable_output(
			stream->sink->link->link_enc,
			&stream->sink->link->cur_link_settings,
			stream->stream_enc->id,
			dal_clock_source_get_id(stream->clock_source),
			stream->signal,
			stream->public.timing.display_color_depth,
			stream->public.timing.pix_clk_khz);

	if (stream->signal == SIGNAL_TYPE_HDMI_TYPE_A)
		dal_ddc_service_read_scdc_data(link->ddc);
}

/****************************enable_link***********************************/
static enum dc_status enable_link(struct core_stream *stream)
{
	enum dc_status status;
	switch (stream->signal) {
	case SIGNAL_TYPE_DISPLAY_PORT:
	case SIGNAL_TYPE_EDP:
		status = enable_link_dp(stream);
		break;
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
		status = enable_link_dp_mst(stream);
		dc_service_sleep_in_milliseconds(stream->ctx, 200);
		break;
	case SIGNAL_TYPE_DVI_SINGLE_LINK:
	case SIGNAL_TYPE_DVI_DUAL_LINK:
	case SIGNAL_TYPE_HDMI_TYPE_A:
		enable_link_hdmi(stream);
		status = DC_OK;
		break;

	default:
		status = DC_ERROR_UNEXPECTED;
		break;
	}

	if (stream->audio && status == DC_OK) {
		/* notify audio driver for audio modes of monitor */
		dal_audio_enable_azalia_audio_jack_presence(stream->audio,
				stream->stream_enc->id);

		/* un-mute audio */
		dal_audio_unmute(stream->audio, stream->stream_enc->id,
				stream->signal);
	}

	return status;
}

static void disable_link(struct core_stream *stream)
{
	struct dc *dc = stream->ctx->dc;

	/* TODO  dp_set_hw_test_pattern */

	/* here we need to specify that encoder output settings
	 * need to be calculated as for the set mode,
	 * it will lead to querying dynamic link capabilities
	 * which should be done before enable output */

	if (dc_is_dp_signal(stream->signal)) {
		/* SST DP, eDP */
		if (dc_is_dp_sst_signal(stream->signal))
			dp_disable_link_phy(
					stream->sink->link, stream->signal);
		else {
			dp_disable_link_phy_mst(
					stream->sink->link, stream);
		}
	} else {
		dc->hwss.encoder_disable_output(
				stream->sink->link->link_enc, stream->signal);
	}
}

enum dc_status dc_link_validate_mode_timing(
		const struct core_sink *sink,
		struct core_link *link,
		const struct dc_crtc_timing *timing)
{
	uint32_t max_pix_clk = sink->dongle_max_pix_clk;

	if (0 != max_pix_clk && timing->pix_clk_khz > max_pix_clk)
		return DC_EXCEED_DONGLE_MAX_CLK;

	switch (sink->public.sink_signal) {
		case SIGNAL_TYPE_DISPLAY_PORT:
			if(!dp_validate_mode_timing(
					link,
					timing))
				return DC_NO_DP_LINK_BANDWIDTH;
			break;

		default:
			break;
	}

	return DC_OK;
}

bool dc_link_set_backlight_level(const struct dc_link *public, uint32_t level)
{
	struct core_link *protected = DC_LINK_TO_CORE(public);
	struct dc_context *ctx = protected->ctx;

	dal_logger_write(ctx->logger, LOG_MAJOR_BACKLIGHT,
			LOG_MINOR_BACKLIGHT_INTERFACE,
			"New Backlight level: %d (0x%X)\n", level, level);

	ctx->dc->hwss.encoder_set_lcd_backlight_level(protected->link_enc, level);

	return true;
}

void core_link_resume(struct core_link *link)
{
	dal_connector_program_hpd_filter(
		link->connector,
		DELAY_ON_CONNECT_IN_MS,
		DELAY_ON_DISCONNECT_IN_MS);
}


static enum dc_status allocate_mst_payload(struct core_stream *stream)
{
	struct core_link *link = stream->sink->link;
	struct link_encoder *link_encoder = link->link_enc;
	struct stream_encoder *stream_encoder = stream->stream_enc;
	struct dp_mst_stream_allocation_table table = {0};
	struct fixed31_32 avg_time_slots_per_mtp;
	uint8_t cur_stream_payload_idx;
	struct dc *dc = stream->ctx->dc;

	/* enable_link_dp_mst already check link->enabled_stream_count
	 * and stream is in link->stream[]. This is called during set mode,
	 * stream_enc is available.
	 */

	/* get calculate VC payload for stream: stream_alloc */
	dc_helpers_dp_mst_write_payload_allocation_table(
		stream->ctx,
		&stream->public,
		&table,
		true);

	/* program DP source TX for payload */
	dc->hwss.update_mst_stream_allocation_table(
		link_encoder,
		&table);

	/* send down message */
	dc_helpers_dp_mst_poll_for_allocation_change_trigger(
			stream->ctx,
			&stream->public);

	dc_helpers_dp_mst_send_payload_allocation(
			stream->ctx,
			&stream->public,
			true);

	/* slot X.Y for only current stream */
	cur_stream_payload_idx = table.cur_stream_payload_idx;
	avg_time_slots_per_mtp = dal_fixed31_32_from_fraction(
		table.stream_allocations[cur_stream_payload_idx].pbn,
		table.stream_allocations[cur_stream_payload_idx].pbn_per_slot);

	dc->hwss.set_mst_bandwidth(
		stream_encoder,
		avg_time_slots_per_mtp);

	return DC_OK;

}

static enum dc_status deallocate_mst_payload(struct core_stream *stream)
{
	struct core_link *link = stream->sink->link;
	struct link_encoder *link_encoder = link->link_enc;
	struct stream_encoder *stream_encoder = stream->stream_enc;
	struct dp_mst_stream_allocation_table table = {0};
	struct fixed31_32 avg_time_slots_per_mtp = dal_fixed31_32_from_int(0);
	uint8_t i;
	struct dc *dc = stream->ctx->dc;

	/* deallocate_mst_payload is called before disable link. When mode or
	 * disable/enable monitor, new stream is created which is not in link
	 * stream[] yet. For this, payload is not allocated yet, so de-alloc
	 * should not done. For new mode set, map_resources will get engine
	 * for new stream, so stream_enc->id should be validated until here.
	 */
	if (link->enabled_stream_count == 0)
		return DC_OK;

	for (i = 0; i < link->enabled_stream_count; i++) {
		if (link->enabled_streams[i] == stream)
			break;
	}
	/* stream is not in link stream list */
	if (i == link->enabled_stream_count)
		return DC_OK;

	/* slot X.Y */
	dc->hwss.set_mst_bandwidth(
		stream_encoder,
		avg_time_slots_per_mtp);

	/* TODO: which component is responsible for remove payload table? */
	dc_helpers_dp_mst_write_payload_allocation_table(
		stream->ctx,
		&stream->public,
		&table,
		false);

	dc->hwss.update_mst_stream_allocation_table(
		link_encoder,
		&table);

	dc_helpers_dp_mst_poll_for_allocation_change_trigger(
			stream->ctx,
			&stream->public);

	dc_helpers_dp_mst_send_payload_allocation(
			stream->ctx,
			&stream->public,
			false);

	return DC_OK;
}

void core_link_enable_stream(
		struct core_link *link,
		struct core_stream *stream)
{
	struct dc *dc = stream->ctx->dc;

	dc->hwss.enable_stream(stream);

	if (DC_OK != enable_link(stream)) {
			BREAK_TO_DEBUGGER();
			return;
	}
	if (stream->signal == SIGNAL_TYPE_DISPLAY_PORT_MST)
		allocate_mst_payload(stream);
}

void core_link_disable_stream(
		struct core_link *link,
		struct core_stream *stream)
{
	struct dc *dc = stream->ctx->dc;

	if (stream->signal == SIGNAL_TYPE_DISPLAY_PORT_MST)
		deallocate_mst_payload(stream);

	dc->hwss.disable_stream(stream);

	disable_link(stream);

}

