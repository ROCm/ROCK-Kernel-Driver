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

#include "include/encoder_interface.h"
#include "include/gpio_interface.h"
#include "../encoder_impl.h"
#include "../hw_ctx_digital_encoder.h"
#include "../hw_ctx_digital_encoder_hal.h"
#include "../digital_encoder.h"
#include "../digital_encoder_dp.h"

/*
 * Header of this unit
 */

#include "digital_encoder_dp_dce110.h"

/*
 * Post-requisites: headers required by this unit
 */

/*
 * This unit
 */

#define FROM_ENCODER_IMPL(ptr) \
	container_of(container_of(container_of((ptr), \
	struct digital_encoder, base), \
	struct digital_encoder_dp, base), \
	struct digital_encoder_dp_dce110, base)


/******************/
/* Implementation */
/******************/

/**
* dal_digital_encoder_dce110_dp_initialize
*
* @brief
*  Perform SW initialization
*
* @param
*  NONE
*/
enum encoder_result dal_digital_encoder_dp_dce110_initialize(
	struct encoder_impl *enc,
	const struct encoder_context *ctx)
{
	struct adapter_service *as = enc->adapter_service;
	struct gpio *stereo_gpio = dal_adapter_service_obtain_stereo_gpio(as);

	/* Initialize Encoder */
	enum encoder_result result = dal_digital_encoder_dp_initialize(
			enc, ctx);

	if (result != ENCODER_RESULT_OK)
		return result;

	/* Obtain stereo output gpio.
	 * If fails, stereo features will not be supported
	 */

	enc->funcs->set_stereo_gpio(enc, stereo_gpio);

	return result;
}


/**
* dal_encoder_impl_dce110_get_supported_stream_engines
*
* @brief
*  Reports list of supported stream engines
*  On DCE8.0 DisplayPort encoder supports all digital engines
*
* @return
*  List of supported stream engines
*/

union supported_stream_engines
	dal_digital_encoder_dp_dce110_get_supported_stream_engines(
	const struct encoder_impl *impl)
{
	union supported_stream_engines result;

	result.u_all = 0;

	result.engine.ENGINE_ID_DIGA = 1;
	result.engine.ENGINE_ID_DIGB = 1;
	result.engine.ENGINE_ID_DIGC = 1;

	return result;
}

bool dal_digital_encoder_dp_dce110_is_link_settings_supported(
	struct encoder_impl *impl,
	const struct link_settings *link_settings)
{
	struct encoder_feature_support features = impl->features;
	bool hbr2_capable = features.flags.bits.IS_HBR2_CAPABLE;
	enum link_rate max_rate;

	if (hbr2_capable)
		max_rate = LINK_RATE_HIGH2;
	else
		max_rate = LINK_RATE_HIGH;

	if ((link_settings->link_rate > max_rate) ||
			(link_settings->link_rate < LINK_RATE_LOW))
		return false;

	return true;
}

/*****************************************/
/* Constructor, Destructor, fcn pointers */
/*****************************************/

static void destroy(
	struct encoder_impl **ptr)
{
	struct digital_encoder_dp_dce110 *enc = FROM_ENCODER_IMPL(*ptr);

	dal_digital_encoder_dp_dce110_destruct(enc);

	dal_free(enc);

	*ptr = NULL;
}

static const struct encoder_impl_funcs dp_dce110_funcs = {
	.destroy = destroy,
	.power_up = dal_digital_encoder_power_up,
	.power_down = dal_encoder_impl_power_down,
	.setup = dal_digital_encoder_dp_setup,
	.pre_enable_output = dal_encoder_impl_pre_enable_output,
	.enable_output = dal_digital_encoder_dp_enable_output,
	.disable_output = dal_digital_encoder_dp_disable_output,
	.blank = dal_digital_encoder_dp_blank,
	.unblank = dal_digital_encoder_dp_unblank,
	.setup_stereo = dal_digital_encoder_setup_stereo,
	.enable_sync_output = dal_encoder_impl_enable_sync_output,
	.disable_sync_output = dal_encoder_impl_disable_sync_output,
	.pre_ddc = dal_encoder_impl_pre_ddc,
	.post_ddc = dal_encoder_impl_post_ddc,
	.set_dp_phy_pattern = dal_digital_encoder_dp_set_dp_phy_pattern,
	.is_sink_present = dal_digital_encoder_is_sink_present,
	.detect_load = dal_encoder_impl_detect_load,
	.detect_sink = dal_digital_encoder_detect_sink,
	.get_paired_transmitter_id =
		dal_encoder_impl_get_paired_transmitter_id,
	.get_phy_id = dal_encoder_impl_get_phy_id,
	.get_paired_phy_id = dal_encoder_impl_get_paired_phy_id,
	.is_link_settings_supported =
		dal_digital_encoder_dp_dce110_is_link_settings_supported,
	.get_supported_stream_engines =
		dal_digital_encoder_dp_dce110_get_supported_stream_engines,
	.is_clock_source_supported =
		dal_encoder_impl_is_clock_source_supported,
	.validate_output = dal_encoder_impl_validate_output,
	.update_info_frame = dal_digital_encoder_update_info_frame,
	.stop_info_frame = dal_digital_encoder_stop_info_frame,
	.set_lcd_backlight_level =
		dal_digital_encoder_set_lcd_backlight_level,
	.backlight_control = dal_digital_encoder_backlight_control,
	.update_mst_alloc_table =
		dal_encoder_impl_update_mst_alloc_table,
	.enable_stream = dal_encoder_impl_enable_stream,
	.disable_stream = dal_encoder_impl_disable_stream,
	.is_test_pattern_enabled =
		dal_digital_encoder_dp_is_test_pattern_enabled,
	.set_lane_settings = dal_digital_encoder_dp_set_lane_settings,
	.get_lane_settings = dal_digital_encoder_dp_get_lane_settings,
	.enable_hpd = dal_digital_encoder_enable_hpd,
	.disable_hpd = dal_digital_encoder_disable_hpd,
	.get_active_clock_source =
		dal_digital_encoder_get_active_clock_source,
	.get_active_engine = dal_digital_encoder_get_active_engine,
	.initialize = dal_digital_encoder_dp_dce110_initialize,
	.set_stereo_gpio = dal_encoder_impl_set_stereo_gpio,
	.set_hsync_output_gpio = dal_encoder_impl_set_hsync_output_gpio,
	.set_vsync_output_gpio = dal_encoder_impl_set_vsync_output_gpio,
	.release_hw = dal_encoder_impl_release_hw,
};

bool dal_digital_encoder_dp_dce110_construct(
	struct digital_encoder_dp_dce110 *enc,
	const struct encoder_init_data *init_data)
{
	struct encoder_impl *base = &enc->base.base.base;
	struct graphics_object_encoder_cap_info enc_cap_info = {0};

	if (!dal_digital_encoder_dp_construct(
		&enc->base, init_data)) {
		ASSERT_CRITICAL(false);
		return false;
	}

	base->funcs = &dp_dce110_funcs;

	base->output_signals =
		SIGNAL_TYPE_DVI_SINGLE_LINK |
		SIGNAL_TYPE_DVI_SINGLE_LINK1 |
		SIGNAL_TYPE_DVI_DUAL_LINK |
		SIGNAL_TYPE_LVDS |
		SIGNAL_TYPE_DISPLAY_PORT |
		SIGNAL_TYPE_DISPLAY_PORT_MST |
		SIGNAL_TYPE_EDP |
		SIGNAL_TYPE_HDMI_TYPE_A;

	/* For DCE 8.0 and 8.1, by design, UNIPHY is hardwired to DIG_BE.
	 * SW always assign DIG_FE 1:1 mapped to DIG_FE for non-MST UNIPHY.
	 * SW assign DIG_FE to non-MST UNIPHY first and MST last. So prefer
	 * DIG is per UNIPHY and used by SST DP, eDP, HDMI, DVI and LVDS.
	 * Prefer DIG assignment is decided by board design.
	 * For DCE 8.0, there are only max 6 UNIPHYs, we assume board design
	 * and VBIOS will filter out 7 UNIPHY for DCE 8.0.
	 * By this, adding DIGG should not hurt DCE 8.0.
	 * This will let DCE 8.1 share DCE 8.0 as much as possible */

	switch (base->transmitter) {
	case TRANSMITTER_UNIPHY_A:
		base->preferred_engine = ENGINE_ID_DIGA;
	break;
	case TRANSMITTER_UNIPHY_B:
		base->preferred_engine = ENGINE_ID_DIGB;
	break;
	case TRANSMITTER_UNIPHY_C:
		base->preferred_engine = ENGINE_ID_DIGC;
	break;
	default:
		ASSERT_CRITICAL(false);
		base->preferred_engine = ENGINE_ID_UNKNOWN;
	}

	/* Override features with DCE-specific values */
	if (dal_adapter_service_get_encoder_cap_info(base->adapter_service,
			base->id, &enc_cap_info))
		base->features.flags.bits.IS_HBR2_CAPABLE =
				enc_cap_info.dp_hbr2_cap;

	/* test pattern 3 support */
	base->features.flags.bits.IS_TPS3_CAPABLE = true;
	base->features.max_deep_color = HW_COLOR_DEPTH_121212;

	base->features.flags.bits.IS_Y_ONLY_CAPABLE =
		dal_adapter_service_is_feature_supported(
			FEATURE_SUPPORT_DP_Y_ONLY);

	base->features.flags.bits.IS_YCBCR_CAPABLE =
		dal_adapter_service_is_feature_supported(
			FEATURE_SUPPORT_DP_YUV);

	return true;
}

void dal_digital_encoder_dp_dce110_destruct(
	struct digital_encoder_dp_dce110 *enc)
{
	dal_digital_encoder_dp_destruct(&enc->base);
}

struct encoder_impl *dal_digital_encoder_dp_dce110_create(
	const struct encoder_init_data *init)
{
	struct digital_encoder_dp_dce110 *enc =
		dal_alloc(sizeof(struct digital_encoder_dp_dce110));

	if (!enc) {
		ASSERT_CRITICAL(false);
		return NULL;
	}

	if (dal_digital_encoder_dp_dce110_construct(enc, init))
		return &enc->base.base.base;

	ASSERT_CRITICAL(false);

	dal_free(enc);

	return NULL;
}
