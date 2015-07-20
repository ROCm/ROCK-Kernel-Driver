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

#include "include/logger_interface.h"
#include "include/encoder_interface.h"
#include "include/gpio_interface.h"

/*
 * Header of this unit
 */

#include "encoder_impl.h"


/******************************************************************************
 * This unit
 *****************************************************************************/
void dal_encoder_impl_enable_hpd(
	struct encoder_impl *impl,
	const struct encoder_context *ctx)
{
	/* do nothing */
}

void dal_encoder_impl_disable_hpd(
	struct encoder_impl *impl,
	const struct encoder_context *ctx)
{
	/* do nothing */
}

void dal_encoder_impl_update_info_frame(
	struct encoder_impl *impl,
	const struct encoder_info_frame_param *param)
{
	/* do nothing */
}

void dal_encoder_impl_stop_info_frame(
	struct encoder_impl *impl,
	const struct encoder_context *ctx)
{
	/* do nothing */
}

enum signal_type dal_encoder_impl_convert_downstream_to_signal(
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
		case CONNECTOR_ID_COMPOSITE:
			return SIGNAL_TYPE_COMPOSITE;
		case CONNECTOR_ID_9PIN_DIN:
		case CONNECTOR_ID_7PIN_DIN:
			return SIGNAL_TYPE_YPBPR;
		case CONNECTOR_ID_SVIDEO:
			return SIGNAL_TYPE_SVIDEO;
		case CONNECTOR_ID_YPBPR:
			return SIGNAL_TYPE_YPBPR;
		case CONNECTOR_ID_SCART:
			return SIGNAL_TYPE_SCART;
		case CONNECTOR_ID_HDMI_TYPE_A:
			return SIGNAL_TYPE_HDMI_TYPE_A;
		case CONNECTOR_ID_LVDS:
			return SIGNAL_TYPE_LVDS;
		case CONNECTOR_ID_DISPLAY_PORT:
			return SIGNAL_TYPE_DISPLAY_PORT;
		case CONNECTOR_ID_EDP:
			return SIGNAL_TYPE_EDP;
		case CONNECTOR_ID_CROSSFIRE:
			return SIGNAL_TYPE_MVPU_A;
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

enum transmitter dal_encoder_impl_translate_encoder_to_transmitter(
	struct graphics_object_id encoder)
{
	switch (encoder.id) {
	case ENCODER_ID_INTERNAL_UNIPHY:
		switch (encoder.enum_id) {
		case ENUM_ID_1:
			return TRANSMITTER_UNIPHY_A;
		case ENUM_ID_2:
			return TRANSMITTER_UNIPHY_B;
		default:
			return TRANSMITTER_UNKNOWN;
		}
	break;
	case ENCODER_ID_INTERNAL_UNIPHY1:
		switch (encoder.enum_id) {
		case ENUM_ID_1:
			return TRANSMITTER_UNIPHY_C;
		case ENUM_ID_2:
			return TRANSMITTER_UNIPHY_D;
		default:
			return TRANSMITTER_UNKNOWN;
		}
	break;
	case ENCODER_ID_INTERNAL_UNIPHY2:
		switch (encoder.enum_id) {
		case ENUM_ID_1:
			return TRANSMITTER_UNIPHY_E;
		case ENUM_ID_2:
			return TRANSMITTER_UNIPHY_F;
		default:
			return TRANSMITTER_UNKNOWN;
		}
	break;
	case ENCODER_ID_INTERNAL_UNIPHY3:
		switch (encoder.enum_id) {
		case ENUM_ID_1:
			return TRANSMITTER_UNIPHY_G;
		default:
			return TRANSMITTER_UNKNOWN;
		}
	break;
	case ENCODER_ID_EXTERNAL_NUTMEG:
		switch (encoder.enum_id) {
		case ENUM_ID_1:
			return TRANSMITTER_NUTMEG_CRT;
		default:
			return TRANSMITTER_UNKNOWN;
		}
	break;
	case ENCODER_ID_EXTERNAL_TRAVIS:
		switch (encoder.enum_id) {
		case ENUM_ID_1:
			return TRANSMITTER_TRAVIS_CRT;
		case ENUM_ID_2:
			return TRANSMITTER_TRAVIS_LCD;
		default:
			return TRANSMITTER_UNKNOWN;
		}
	break;
	default:
		return TRANSMITTER_UNKNOWN;
	}
}

enum transmitter dal_encoder_impl_translate_encoder_to_paired_transmitter(
	struct graphics_object_id encoder)
{
	switch (encoder.id) {
	case ENCODER_ID_INTERNAL_UNIPHY:
		switch (encoder.enum_id) {
		case ENUM_ID_1:
			return TRANSMITTER_UNIPHY_B;
		case ENUM_ID_2:
			return TRANSMITTER_UNIPHY_A;
		default:
			return TRANSMITTER_UNKNOWN;
		}
	break;
	case ENCODER_ID_INTERNAL_UNIPHY1:
		switch (encoder.enum_id) {
		case ENUM_ID_1:
			return TRANSMITTER_UNIPHY_D;
		case ENUM_ID_2:
			return TRANSMITTER_UNIPHY_C;
		default:
			return TRANSMITTER_UNKNOWN;
		}
	break;
	case ENCODER_ID_INTERNAL_UNIPHY2:
		switch (encoder.enum_id) {
		case ENUM_ID_1:
			return TRANSMITTER_UNIPHY_F;
		break;
		case ENUM_ID_2:
			return TRANSMITTER_UNIPHY_E;
		default:
			return TRANSMITTER_UNKNOWN;
		}
	break;
	default:
		return TRANSMITTER_UNKNOWN;
	}
}

enum encoder_result dal_encoder_impl_power_up(
	struct encoder_impl *impl,
	const struct encoder_context *ctx)
{
	return impl->funcs->initialize(impl, ctx);
}

enum encoder_result dal_encoder_impl_power_down(
	struct encoder_impl *impl,
	const struct encoder_output *output)
{
	return impl->funcs->disable_output(impl, output);
}

enum encoder_result dal_encoder_impl_setup(
	struct encoder_impl *impl,
	const struct encoder_output *output)
{
	return ENCODER_RESULT_OK;
}

enum encoder_result dal_encoder_impl_pre_enable_output(
	struct encoder_impl *impl,
	const struct encoder_pre_enable_output_param *param)
{
	return ENCODER_RESULT_OK;
}

enum encoder_result dal_encoder_impl_blank(
	struct encoder_impl *impl,
	const struct encoder_context *ctx)
{
	return ENCODER_RESULT_OK;
}

enum encoder_result dal_encoder_impl_unblank(
	struct encoder_impl *impl,
	const struct encoder_unblank_param *param)
{
	return ENCODER_RESULT_OK;
}

enum encoder_result dal_encoder_impl_setup_stereo(
	struct encoder_impl *impl,
	const struct encoder_3d_setup *setup)
{
	return ENCODER_RESULT_ERROR;
}

enum encoder_result dal_encoder_impl_enable_sync_output(
	struct encoder_impl *impl,
	enum sync_source src)
{
	return ENCODER_RESULT_ERROR;
}

enum encoder_result dal_encoder_impl_disable_sync_output(
	struct encoder_impl *impl)
{
	return ENCODER_RESULT_ERROR;
}

enum encoder_result dal_encoder_impl_pre_ddc(
	struct encoder_impl *impl,
	const struct encoder_context *ctx)
{
	return ENCODER_RESULT_OK;
}

enum encoder_result dal_encoder_impl_post_ddc(
	struct encoder_impl *impl,
	const struct encoder_context *ctx)
{
	return ENCODER_RESULT_OK;
}

enum encoder_result dal_encoder_impl_set_dp_phy_pattern(
	struct encoder_impl *impl,
	const struct encoder_set_dp_phy_pattern_param *param)
{
	return ENCODER_RESULT_ERROR;
}

bool dal_encoder_impl_is_sink_present(
	struct encoder_impl *impl,
	struct graphics_object_id downstream)
{
	return false;
}

enum signal_type dal_encoder_impl_detect_load(
	struct encoder_impl *impl,
	const struct encoder_context *ctx)
{
	return SIGNAL_TYPE_NONE;
}

enum signal_type dal_encoder_impl_detect_sink(
	struct encoder_impl *impl,
	struct graphics_object_id downstream)
{
	return dal_encoder_impl_convert_downstream_to_signal(
		impl->id, downstream);
}

enum transmitter dal_encoder_impl_get_paired_transmitter_id(
	const struct encoder_impl *impl)
{
	return dal_encoder_impl_translate_encoder_to_paired_transmitter(
		impl->id);
}

enum physical_phy_id dal_encoder_impl_get_phy_id(
	const struct encoder_impl *impl)
{
	return (enum physical_phy_id)impl->transmitter;
}

enum physical_phy_id dal_encoder_impl_get_paired_phy_id(
	const struct encoder_impl *impl)
{
	return (enum physical_phy_id)
		impl->funcs->get_paired_transmitter_id(impl);
}

bool dal_encoder_impl_is_link_settings_supported(
	struct encoder_impl *impl,
	const struct link_settings *link_settings)
{
	return false;
}

union supported_stream_engines
	dal_encoder_impl_get_supported_stream_engines(
	const struct encoder_impl *impl)
{
	union supported_stream_engines result;

	result.u_all = 0;

	return result;
}

bool dal_encoder_impl_is_clock_source_supported(
	const struct encoder_impl *impl,
	enum clock_source_id clock_source)
{
	return true;
}

bool dal_encoder_impl_validate_dvi_output(
	const struct encoder_impl *impl,
	const struct encoder_output *output)
{
	uint32_t max_pixel_clock = TMDS_MAX_PIXEL_CLOCK;

	if (impl->features.max_pixel_clock < TMDS_MAX_PIXEL_CLOCK)
		max_pixel_clock = impl->features.max_pixel_clock;

	if (output->ctx.signal == SIGNAL_TYPE_DVI_DUAL_LINK)
		max_pixel_clock <<= 1;

	/* DVI only support RGB pixel encoding */
	if (output->crtc_timing.flags.PIXEL_ENCODING != HW_PIXEL_ENCODING_RGB)
		return false;

	if (output->crtc_timing.pixel_clock < TMDS_MIN_PIXEL_CLOCK)
		return false;

	if (output->crtc_timing.pixel_clock > max_pixel_clock)
		return false;

	/* DVI supports 6/8bpp single-link and 10/16bpp dual-link */
	switch (output->crtc_timing.flags.COLOR_DEPTH) {
	case HW_COLOR_DEPTH_666:
	case HW_COLOR_DEPTH_888:
	break;
	case HW_COLOR_DEPTH_101010:
	case HW_COLOR_DEPTH_161616:
		if (output->ctx.signal != SIGNAL_TYPE_DVI_DUAL_LINK)
			return false;
	break;
	default:
		return false;
	}

	return true;
}

bool dal_encoder_impl_validate_hdmi_output(
	const struct encoder_impl *impl,
	const struct encoder_output *output)
{
	enum hw_color_depth max_deep_color = output->max_hdmi_deep_color;

	/* expressed in KHz */
	uint32_t pixel_clock = 0;

	if (max_deep_color > impl->features.max_deep_color)
		max_deep_color = impl->features.max_deep_color;

	if (max_deep_color < output->crtc_timing.flags.COLOR_DEPTH)
		return false;

	if (output->crtc_timing.pixel_clock < TMDS_MIN_PIXEL_CLOCK)
		return false;

	switch (output->crtc_timing.flags.COLOR_DEPTH) {
	case HW_COLOR_DEPTH_666:
		pixel_clock = (output->crtc_timing.pixel_clock * 3) >> 2;
	break;
	case HW_COLOR_DEPTH_888:
		pixel_clock = output->crtc_timing.pixel_clock;
	break;
	case HW_COLOR_DEPTH_101010:
		pixel_clock = (output->crtc_timing.pixel_clock * 10) >> 3;
	break;
	case HW_COLOR_DEPTH_121212:
		pixel_clock = (output->crtc_timing.pixel_clock * 3) >> 1;
	break;
	case HW_COLOR_DEPTH_161616:
		pixel_clock = output->crtc_timing.pixel_clock << 1;
	break;
	}

	if (output->max_tmds_clk_from_edid_in_mhz > 0)
		if (pixel_clock > output->max_tmds_clk_from_edid_in_mhz * 1000)
			return false;

	if ((pixel_clock == 0) ||
		(pixel_clock > output->max_hdmi_pixel_clock) ||
		(pixel_clock > impl->features.max_pixel_clock))
		return false;

	/*
	 * Restriction: allow non-CE mode (IT mode) to support RGB only.
	 * When it is IT mode, the format mode will be 0,
	 * but currently the code is broken,
	 * VIDEO FORMAT is always 0 in validatepathMode().
	 * Due to overscan change - need fix there and test the impact - to do.
	 */
	if (output->crtc_timing.timing_standard != HW_TIMING_STANDARD_CEA861)
		if (output->crtc_timing.flags.PIXEL_ENCODING !=
			HW_PIXEL_ENCODING_RGB)
			return false;

	return true;
}

bool dal_encoder_impl_validate_rgb_output(
	const struct encoder_impl *impl,
	const struct encoder_output *output)
{
	if (output->crtc_timing.pixel_clock > impl->features.max_pixel_clock)
		return false;

	if (output->crtc_timing.flags.PIXEL_ENCODING != HW_PIXEL_ENCODING_RGB)
		return false;

	return true;
}

bool dal_encoder_impl_validate_component_output(
	const struct encoder_impl *impl,
	const struct encoder_output *output)
{
	if (output->crtc_timing.pixel_clock > impl->features.max_pixel_clock)
		return false;

	if (output->crtc_timing.flags.PIXEL_ENCODING !=
		HW_PIXEL_ENCODING_YCBCR422)
		return false;

	return true;
}

bool dal_encoder_impl_validate_dp_output(
	const struct encoder_impl *impl,
	const struct encoder_output *output)
{
	if (output->crtc_timing.pixel_clock > impl->features.max_pixel_clock)
		return false;

	/* default RGB only */
	if (output->crtc_timing.flags.PIXEL_ENCODING == HW_PIXEL_ENCODING_RGB)
		return true;

	if (impl->features.flags.bits.IS_YCBCR_CAPABLE)
		return true;

	/* for DCE 8.x or later DP Y-only feature,
	 * we need ASIC cap + FeatureSupportDPYonly, not support 666 */
	if (output->crtc_timing.flags.Y_ONLY &&
		impl->features.flags.bits.IS_YCBCR_CAPABLE &&
		output->crtc_timing.flags.COLOR_DEPTH != HW_COLOR_DEPTH_666)
		return true;

	return false;
}

bool dal_encoder_impl_validate_mvpu1_output(
	const struct encoder_impl *impl,
	const struct encoder_output *output)
{
	return (output->crtc_timing.pixel_clock <=
		impl->features.max_pixel_clock);
}

bool dal_encoder_impl_validate_mvpu2_output(
	const struct encoder_impl *impl,
	const struct encoder_output *output)
{
	return (output->crtc_timing.pixel_clock <=
		impl->features.max_pixel_clock << 1);
}

bool dal_encoder_impl_validate_wireless_output(
	const struct encoder_impl *impl,
	const struct encoder_output *output)
{
	if (output->crtc_timing.pixel_clock > impl->features.max_pixel_clock)
		return false;

	/* Wireless only supports YCbCr444 */
	if (output->crtc_timing.flags.PIXEL_ENCODING ==
			HW_PIXEL_ENCODING_YCBCR444)
		return true;

	return false;
}

enum encoder_result dal_encoder_impl_validate_output(
	struct encoder_impl *impl,
	const struct encoder_output *output)
{
	bool is_valid;

	switch (output->ctx.signal) {
	case SIGNAL_TYPE_DVI_SINGLE_LINK:
	case SIGNAL_TYPE_DVI_DUAL_LINK:
		is_valid = dal_encoder_impl_validate_dvi_output(
			impl, output);
	break;
	case SIGNAL_TYPE_HDMI_TYPE_A:
		is_valid = dal_encoder_impl_validate_hdmi_output(
			impl, output);
	break;
	case SIGNAL_TYPE_RGB:
		is_valid = dal_encoder_impl_validate_rgb_output(
			impl, output);
	break;
	case SIGNAL_TYPE_YPBPR:
	case SIGNAL_TYPE_SCART:
	case SIGNAL_TYPE_COMPOSITE:
	case SIGNAL_TYPE_SVIDEO:
		is_valid = dal_encoder_impl_validate_component_output(
			impl, output);
	break;
	case SIGNAL_TYPE_DISPLAY_PORT:
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
	case SIGNAL_TYPE_EDP:
		is_valid = dal_encoder_impl_validate_dp_output(
			impl, output);
	break;
	case SIGNAL_TYPE_MVPU_A:
	case SIGNAL_TYPE_MVPU_B:
		is_valid = dal_encoder_impl_validate_mvpu1_output(
			impl, output);
	break;
	case SIGNAL_TYPE_MVPU_AB:
		is_valid = dal_encoder_impl_validate_mvpu2_output(
			impl, output);
	break;
	case SIGNAL_TYPE_WIRELESS:
		is_valid = dal_encoder_impl_validate_wireless_output(
			impl, output);
	break;
	default:
		is_valid = true;
	break;
	}

	return is_valid ? ENCODER_RESULT_OK : ENCODER_RESULT_ERROR;
}

enum encoder_result dal_encoder_impl_set_lcd_backlight_level(
	struct encoder_impl *impl,
	uint32_t level)
{
	return ENCODER_RESULT_OK;
}

enum encoder_result dal_encoder_impl_backlight_control(
	struct encoder_impl *impl,
	bool enable)
{
	return ENCODER_RESULT_OK;
}

enum encoder_result dal_encoder_impl_update_mst_alloc_table(
	struct encoder_impl *impl,
	const struct dp_mst_stream_allocation_table *table,
	bool is_removal)
{
	return ENCODER_RESULT_OK;
}

enum encoder_result dal_encoder_impl_enable_stream(
	struct encoder_impl *impl,
	enum engine_id engine,
	struct fixed31_32 throttled_vcp_size)
{
	return ENCODER_RESULT_OK;
}

enum encoder_result dal_encoder_impl_disable_stream(
	struct encoder_impl *impl,
	enum engine_id engine)
{
	return ENCODER_RESULT_OK;
}

bool dal_encoder_impl_is_test_pattern_enabled(
	struct encoder_impl *impl,
	enum engine_id engine)
{
	return false;
}

enum encoder_result dal_encoder_impl_set_lane_settings(
	struct encoder_impl *impl,
	const struct encoder_context *ctx,
	const struct link_training_settings *link_settings)
{
	return ENCODER_RESULT_ERROR;
}

enum encoder_result dal_encoder_impl_get_lane_settings(
	struct encoder_impl *impl,
	const struct encoder_context *ctx,
	struct link_training_settings *link_settings)
{
	return ENCODER_RESULT_ERROR;
}

enum clock_source_id dal_encoder_impl_get_active_clock_source(
	const struct encoder_impl *impl)
{
	return CLOCK_SOURCE_ID_UNDEFINED;
}

enum engine_id dal_encoder_impl_get_active_engine(
	const struct encoder_impl *impl)
{
	return ENGINE_ID_UNKNOWN;
}

enum encoder_result dal_encoder_impl_initialize(
	struct encoder_impl *impl,
	const struct encoder_context *ctx)
{
	return ENCODER_RESULT_OK;
}

void dal_encoder_impl_set_stereo_gpio(
	struct encoder_impl *impl,
	struct gpio *gpio)
{
	if (impl->stereo_gpio)
		dal_adapter_service_release_gpio(
			impl->adapter_service, impl->stereo_gpio);

	impl->stereo_gpio = gpio;

	impl->features.flags.bits.STEREO_SYNC = (NULL != gpio);
}

void dal_encoder_impl_set_hsync_output_gpio(
	struct encoder_impl *impl,
	struct gpio *gpio)
{
	if (impl->hsync_output_gpio)
		dal_adapter_service_release_gpio(
			impl->adapter_service, impl->hsync_output_gpio);

	impl->hsync_output_gpio = gpio;
}

void dal_encoder_impl_set_vsync_output_gpio(
	struct encoder_impl *impl,
	struct gpio *gpio)
{
	if (impl->vsync_output_gpio)
		dal_adapter_service_release_gpio(
			impl->adapter_service, impl->vsync_output_gpio);

	impl->vsync_output_gpio = gpio;
}

void dal_encoder_impl_release_hw(
	struct encoder_impl *impl)
{
	if (impl->stereo_gpio)
		dal_gpio_close(impl->stereo_gpio);

	if (impl->hsync_output_gpio)
		dal_gpio_close(impl->hsync_output_gpio);

	if (impl->vsync_output_gpio)
		dal_gpio_close(impl->vsync_output_gpio);
}

bool dal_encoder_impl_construct(
	struct encoder_impl *impl,
	const struct encoder_init_data *init_data)
{
	impl->ctx = init_data->ctx;
	impl->id = init_data->encoder;

	impl->input_signals = SIGNAL_TYPE_ALL;
	impl->output_signals = SIGNAL_TYPE_ALL;

	impl->adapter_service = init_data->adapter_service;

	impl->preferred_engine = ENGINE_ID_UNKNOWN;

	impl->stereo_gpio = NULL;
	impl->hsync_output_gpio = NULL;
	impl->vsync_output_gpio = NULL;

	impl->features.flags.raw = 0;
	impl->features.max_deep_color = HW_COLOR_DEPTH_888;
	impl->features.max_pixel_clock = MAX_ENCODER_CLOCK;

	impl->transmitter = dal_encoder_impl_translate_encoder_to_transmitter(
		init_data->encoder);

	return true;
}

void dal_encoder_impl_destruct(
	struct encoder_impl *impl)
{
	if (impl->stereo_gpio)
		dal_adapter_service_release_gpio(
			impl->adapter_service, impl->stereo_gpio);

	if (impl->hsync_output_gpio)
		dal_adapter_service_release_gpio(
			impl->adapter_service, impl->hsync_output_gpio);

	if (impl->vsync_output_gpio)
		dal_adapter_service_release_gpio(
			impl->adapter_service, impl->vsync_output_gpio);
}
