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

#include "include/encoder_interface.h"
#include "include/gpio_interface.h"

#include "../encoder_impl.h"
#include "../hw_ctx_digital_encoder.h"
#include "../digital_encoder.h"

#include "digital_encoder_uniphy_dce110.h"


/*****************************************************************************
 * macro definitions
 *****************************************************************************/


/*****************************************************************************
 * functions
 *****************************************************************************/

static void destroy(struct encoder_impl **enc_impl)
{
	struct digital_encoder_uniphy_dce110 *enc110;

	enc110 = ENC_UNIPHY110_FROM_BASE(*enc_impl);

	dal_digital_encoder_uniphy_dce110_destruct(enc110);

	dal_free(enc110);

	*enc_impl = NULL;
}

/*
 * get_paired_transmitter_id
 *
 * @brief
 * Find the transmitter with given encoder
 *
 * @param
 * const struct encoder_impl *enc - [in] contains encoder info
 *
 * @return
 * paired transmitter ID. Unknown if none is found.
 */
static enum transmitter get_paired_transmitter_id(
	const struct encoder_impl *enc)
{
	return dal_encoder_impl_translate_encoder_to_paired_transmitter(
			enc->id);
}

/*
 * initialize
 *
 * @brief
 * Initialize digital encoder and its HW context
 *
 * @param
 * struct encoder_impl *enc - [in/out] encoder implementation layer
 * const struct encoder_context *ctx - [in] encoder context
 *
 * @return
 * ENCODER_RESULT_OK if succeed, ENCODER_RESULT_ERROR if fail.
 */
enum encoder_result initialize(
	struct encoder_impl *enc,
	const struct encoder_context *ctx)
{
	enum encoder_result result;

	result = dal_digital_encoder_initialize(enc, ctx);

	if (result != ENCODER_RESULT_OK) {
		ASSERT_CRITICAL(false);
		return result;
	}

	return ENCODER_RESULT_OK;
}

/*
 * get_supported_stream_engines
 *
 * @brief
 * get a list of supported engine
 *
 * @param
 * const struct encoder_impl *enc - not used.
 *
 * @return
 * list of engines with supported ones enabled.
 */
union supported_stream_engines get_supported_stream_engines(
	const struct encoder_impl *enc)
{
	union supported_stream_engines result = {.u_all = 0};

	result.engine.ENGINE_ID_DIGA = 1;
	result.engine.ENGINE_ID_DIGB = 1;
	result.engine.ENGINE_ID_DIGC = 1;

	return result;
}

void dal_digital_encoder_uniphy_dce110_destruct(
	struct digital_encoder_uniphy_dce110 *enc110)
{
	struct digital_encoder *dig_enc = &enc110->base;

	dal_digital_encoder_destruct(dig_enc);
}

static const struct encoder_impl_funcs uniphy_dce110_funcs = {
	.destroy = destroy,
	.power_up = dal_digital_encoder_power_up,
	.power_down = dal_encoder_impl_power_down,
	.setup = dal_digital_encoder_setup,
	.pre_enable_output = dal_encoder_impl_pre_enable_output,
	.enable_output = dal_digital_encoder_enable_output,
	.disable_output = dal_digital_encoder_disable_output,
	.blank = dal_encoder_impl_blank,
	.unblank = dal_encoder_impl_unblank,
	.setup_stereo = dal_digital_encoder_setup_stereo,
	.enable_sync_output = NULL,
	.disable_sync_output = NULL,
	.pre_ddc = NULL,
	.post_ddc = NULL,
	.set_dp_phy_pattern = NULL,
	.is_sink_present = dal_digital_encoder_is_sink_present,
	.detect_load = NULL,
	.detect_sink = dal_digital_encoder_detect_sink,
	.get_paired_transmitter_id = get_paired_transmitter_id,
	.get_phy_id = NULL,
	.get_paired_phy_id = NULL,
	.is_link_settings_supported =
		dal_digital_encoder_is_link_settings_supported,
	.get_supported_stream_engines = get_supported_stream_engines,
	.is_clock_source_supported = dal_encoder_impl_is_clock_source_supported,
	.validate_output = dal_encoder_impl_validate_output,
	.update_info_frame = dal_digital_encoder_update_info_frame,
	.stop_info_frame = dal_digital_encoder_stop_info_frame,
	.set_lcd_backlight_level = NULL,
	.backlight_control = NULL,
	.update_mst_alloc_table = NULL,
	.enable_stream = NULL,
	.disable_stream = NULL,
	.is_test_pattern_enabled = NULL,
	.set_lane_settings = NULL,
	.get_lane_settings = NULL,
	.enable_hpd = dal_digital_encoder_enable_hpd,
	.disable_hpd = NULL,
	.get_active_clock_source = NULL,
	.get_active_engine = NULL,
	.initialize = initialize,
	.set_stereo_gpio = NULL,
	.set_hsync_output_gpio = NULL,
	.set_vsync_output_gpio = NULL,
	.release_hw = dal_encoder_impl_release_hw,
};

/* max pixel clock to support 3GHz HDMI */

const uint32_t DCE11_UNIPHY_MAX_PIXEL_CLOCK_IN_KHZ_3GB = 297000;

bool dal_digital_encoder_uniphy_dce110_construct(
	struct digital_encoder_uniphy_dce110 *enc110,
	const struct encoder_init_data *init_data)
{
	struct digital_encoder *dig_enc = &enc110->base;
	struct encoder_impl *enc_impl = &dig_enc->base;

	if (!dal_digital_encoder_construct(dig_enc, init_data))
		return false;

	enc_impl->output_signals =
		SIGNAL_TYPE_DVI_SINGLE_LINK |
		SIGNAL_TYPE_DVI_SINGLE_LINK1 |
		SIGNAL_TYPE_DVI_DUAL_LINK |
		SIGNAL_TYPE_LVDS |
		SIGNAL_TYPE_DISPLAY_PORT |
		SIGNAL_TYPE_DISPLAY_PORT_MST |
		SIGNAL_TYPE_EDP |
		SIGNAL_TYPE_HDMI_TYPE_A;

	switch (enc_impl->transmitter) {
	case TRANSMITTER_UNIPHY_A:
		enc_impl->preferred_engine = ENGINE_ID_DIGA;
	break;
	case TRANSMITTER_UNIPHY_B:
		enc_impl->preferred_engine = ENGINE_ID_DIGB;
	break;
	case TRANSMITTER_UNIPHY_C:
		enc_impl->preferred_engine = ENGINE_ID_DIGC;
	break;
	default:
		ASSERT_CRITICAL(false);
		enc_impl->preferred_engine = ENGINE_ID_UNKNOWN;
	break;
	}

	enc_impl->funcs = &uniphy_dce110_funcs;

	enc_impl->features.max_pixel_clock =
		DCE11_UNIPHY_MAX_PIXEL_CLOCK_IN_KHZ_3GB;
	enc_impl->features.max_deep_color = HW_COLOR_DEPTH_121212;
	/* TODO: implement */

	return true;
}

struct encoder_impl *dal_digital_encoder_uniphy_dce110_create(
	const struct encoder_init_data *init)
{
	struct digital_encoder_uniphy_dce110 *enc =
		dal_alloc(sizeof(struct digital_encoder_uniphy_dce110));

	if (!enc)
		return NULL;

	if (dal_digital_encoder_uniphy_dce110_construct(enc, init))
		return &enc->base.base;

	dal_free(enc);

	return NULL;
}
