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

#include "include/logger_interface.h"

#include "dce/dce_11_0_d.h"
#include "dce/dce_11_0_sh_mask.h"

#include "include/encoder_types.h"
#include "include/fixed31_32.h"

#include "../hw_ctx_digital_encoder.h"
#include "../hw_ctx_digital_encoder_hal.h"

#include "hw_ctx_digital_encoder_dce110.h"

#ifndef mmDP_DPHY_INTERNAL_CTRL

#define mmDP_DPHY_INTERNAL_CTRL 0x4aa7
#define mmDP0_DP_DPHY_INTERNAL_CTRL 0x4aa7
#define mmDP1_DP_DPHY_INTERNAL_CTRL 0x4ba7
#define mmDP2_DP_DPHY_INTERNAL_CTRL 0x4ca7
#define mmDP3_DP_DPHY_INTERNAL_CTRL 0x4da7
#define mmDP4_DP_DPHY_INTERNAL_CTRL 0x4ea7
#define mmDP5_DP_DPHY_INTERNAL_CTRL 0x4fa7
#define mmDP6_DP_DPHY_INTERNAL_CTRL 0x54a7
#define mmDP7_DP_DPHY_INTERNAL_CTRL 0x56a7
#define mmDP8_DP_DPHY_INTERNAL_CTRL 0x57a7
#define DP_DPHY_INTERNAL_CTRL__DPHY_ALT_SCRAMBLER_RESET_EN_MASK 0x1
#define DP_DPHY_INTERNAL_CTRL__DPHY_ALT_SCRAMBLER_RESET_EN__SHIFT 0x0
#define DP_DPHY_INTERNAL_CTRL__DPHY_ALT_SCRAMBLER_RESET_SEL_MASK 0x10
#define DP_DPHY_INTERNAL_CTRL__DPHY_ALT_SCRAMBLER_RESET_SEL__SHIFT 0x4

#endif

#ifndef HDMI_CONTROL__HDMI_DATA_SCRAMBLE_EN_MASK
#define HDMI_CONTROL__HDMI_DATA_SCRAMBLE_EN_MASK 0x2
#define HDMI_CONTROL__HDMI_DATA_SCRAMBLE_EN__SHIFT 0x1
#endif

#define NOT_IMPLEMENTED() DAL_LOGGER_NOT_IMPL(LOG_MINOR_COMPONENT_ENCODER,\
		"ENC:%s\n", __func__)

enum hw_ctx_digital_encoder_dce110_constants {
	/* Sending blank requires to wait until stream is disabled.
	 * This counter limits amount of time (in 10 usec) to wait */
	DP_BLANK_MAX_RETRY = 20,
	HDMI_CLOCK_CHANNEL_RATE_MORE_340M = 340000
};

/*****************************************************************************
 * macro definitions
 *****************************************************************************/


/*****************************************************************************
 * functions
 *****************************************************************************/

static const uint32_t fe_engine_offsets[] = {
	mmDIG0_DIG_FE_CNTL - mmDIG0_DIG_FE_CNTL,
	mmDIG1_DIG_FE_CNTL - mmDIG0_DIG_FE_CNTL,
	mmDIG2_DIG_FE_CNTL - mmDIG0_DIG_FE_CNTL,
};

/*TODO  These macro may not be needed*/
#define FROM_HAL(ptr) \
	container_of((ptr), struct hw_ctx_digital_encoder_dce110, base)

static void destruct(
	struct hw_ctx_digital_encoder_dce110 *ctx110)
{
	dal_hw_ctx_digital_encoder_hal_destruct(&ctx110->base);
}

static void destroy(
	struct hw_ctx_digital_encoder **ptr)
{
	struct hw_ctx_digital_encoder_hal *hwctx_hal;
	struct hw_ctx_digital_encoder_dce110 *ctx110;

	hwctx_hal = HWCTX_HAL_FROM_HWCTX(*ptr);

	ctx110 = HWCTX_DIGITAL_ENC110_FROM_BASE(hwctx_hal);

	destruct(ctx110);

	dal_free(ctx110);

	*ptr = NULL;
}

static void set_dp_stream_attributes(
	struct hw_ctx_digital_encoder_hal *ctx,
	enum engine_id engine,
	const struct hw_crtc_timing *timing)
{
	const uint32_t addr = mmDP_PIXEL_FORMAT + fe_engine_offsets[engine];
	uint32_t value = dal_read_reg(ctx->base.dal_ctx, addr);

	/* set pixel encoding */
	switch (timing->flags.PIXEL_ENCODING) {
	case HW_PIXEL_ENCODING_YCBCR422:
		set_reg_field_value(value, DP_PIXEL_ENCODING_YCBCR422,
				DP_PIXEL_FORMAT, DP_PIXEL_ENCODING);
		break;
	case HW_PIXEL_ENCODING_YCBCR444:
		set_reg_field_value(value, DP_PIXEL_ENCODING_YCBCR444,
				DP_PIXEL_FORMAT, DP_PIXEL_ENCODING);

		if (timing->flags.Y_ONLY)
			if (timing->flags.COLOR_DEPTH != HW_COLOR_DEPTH_666)
				/* HW testing only, no use case yet.
				 * Color depth of Y-only could be
				 * 8, 10, 12, 16 bits */
				set_reg_field_value(value,
						DP_PIXEL_ENCODING_Y_ONLY,
						DP_PIXEL_FORMAT,
						DP_PIXEL_ENCODING);
				/* Note: DP_MSA_MISC1 bit 7 is the indicator
				 * of Y-only mode.
				 * This bit is set in HW if register
				 * DP_PIXEL_ENCODING is programmed to 0x4 */
		break;
	/* DP_PIXEL_FORMAT is changed - new format is added.
	 * This format should be retrieved from EDID.
	 * Upper layer change is needed. */
	/* [anaumov] in DAL2, following code was wrapped by
	 * '#if defined(VERIFICATION_REQUIRED)', so leave it commented out
	case PIXEL_ENCODING_RGB_WIDE_GAMUT:
		value.bits.DP_PIXEL_ENCODING = DP_PIXEL_ENCODING_RGB_WIDE_GAMUT;
	break;
	case PIXEL_ENCODING_Y_ONLY:
		value.bits.DP_PIXEL_ENCODING = DP_PIXEL_ENCODING_Y_ONLY;
	break;*/
	default:
		set_reg_field_value(value, DP_PIXEL_ENCODING_RGB,
				DP_PIXEL_FORMAT, DP_PIXEL_ENCODING);
		break;
	}

	/* set color depth */

	switch (timing->flags.COLOR_DEPTH) {
	case HW_COLOR_DEPTH_888:
		set_reg_field_value(value, DP_COMPONENT_DEPTH_8BPC,
				DP_PIXEL_FORMAT, DP_COMPONENT_DEPTH);
		break;
	case HW_COLOR_DEPTH_101010:
		set_reg_field_value(value, DP_COMPONENT_DEPTH_10BPC,
				DP_PIXEL_FORMAT, DP_COMPONENT_DEPTH);
		break;
	case HW_COLOR_DEPTH_121212:
		set_reg_field_value(value, DP_COMPONENT_DEPTH_12BPC,
				DP_PIXEL_FORMAT, DP_COMPONENT_DEPTH);
		break;
	default:
		set_reg_field_value(value, DP_COMPONENT_DEPTH_6BPC,
				DP_PIXEL_FORMAT, DP_COMPONENT_DEPTH);
		break;
	}

	/* set dynamic range and YCbCr range */
	set_reg_field_value(value, 0,
			DP_PIXEL_FORMAT, DP_DYN_RANGE);
	set_reg_field_value(value, 0,
			DP_PIXEL_FORMAT, DP_YCBCR_RANGE);

	dal_write_reg(ctx->base.dal_ctx, addr, value);
}

static void set_tmds_stream_attributes(
	struct hw_ctx_digital_encoder_hal *ctx,
	enum engine_id engine,
	enum signal_type signal,
	const struct hw_crtc_timing *timing)
{
	uint32_t addr = mmDIG_FE_CNTL + fe_engine_offsets[engine];
	uint32_t value = dal_read_reg(ctx->base.dal_ctx, addr);

	switch (timing->flags.PIXEL_ENCODING) {
	case HW_PIXEL_ENCODING_YCBCR422:
		set_reg_field_value(value, 1, DIG_FE_CNTL, TMDS_PIXEL_ENCODING);
		break;
	default:
		set_reg_field_value(value, 0, DIG_FE_CNTL, TMDS_PIXEL_ENCODING);
		break;
	}

	switch (timing->flags.COLOR_DEPTH) {
	case HW_COLOR_DEPTH_101010:
		if ((signal == SIGNAL_TYPE_DVI_SINGLE_LINK ||
				signal == SIGNAL_TYPE_DVI_DUAL_LINK) &&
				timing->flags.PIXEL_ENCODING ==
						HW_PIXEL_ENCODING_RGB)
			set_reg_field_value(
					value,
					2,
					DIG_FE_CNTL,
					TMDS_COLOR_FORMAT);
		else
			set_reg_field_value(
					value,
					0,
					DIG_FE_CNTL,
					TMDS_COLOR_FORMAT);
		break;
	default:
		set_reg_field_value(value, 0, DIG_FE_CNTL, TMDS_COLOR_FORMAT);
		break;
	}
	dal_write_reg(ctx->base.dal_ctx, addr, value);
}

static void set_dvo_stream_attributes(
	struct hw_ctx_digital_encoder_hal *ctx,
	enum signal_type signal,
	bool ddr_memory_rate)
{
	struct dal_context *dal_context = ctx->base.dal_ctx;

	NOT_IMPLEMENTED();
}

static bool setup_tmds_stereo_sync(
	struct hw_ctx_digital_encoder_hal *ctx,
	enum engine_id engine,
	enum tmds_stereo_sync_select stereo_select)
{
	struct dal_context *dal_context = ctx->base.dal_ctx;

	NOT_IMPLEMENTED();

	return false;
}

static bool setup_stereo_sync(
	struct hw_ctx_digital_encoder_hal *ctx,
	enum engine_id engine,
	enum sync_source source)
{
	struct dal_context *dal_context = ctx->base.dal_ctx;

	NOT_IMPLEMENTED();
	return false;
}

static bool control_stereo_sync(
	struct hw_ctx_digital_encoder_hal *ctx,
	enum engine_id engine,
	bool enable)
{
	struct dal_context *dal_context = ctx->base.dal_ctx;

	NOT_IMPLEMENTED();
	return false;
}

static void hpd_initialize(
	struct hw_ctx_digital_encoder_hal *ctx,
	enum engine_id engine,
	enum transmitter transmitter,
	enum hpd_source_id hpd_source)
{
	/* Associate HPD with DIG_BE */
	const uint32_t addr = mmDIG_BE_CNTL + FROM_HAL(ctx)->be_engine_offset;
	uint32_t value = dal_read_reg(ctx->base.dal_ctx, addr);

	set_reg_field_value(value, hpd_source, DIG_BE_CNTL, DIG_HPD_SELECT);
	dal_write_reg(ctx->base.dal_ctx, addr, value);
}

static void aux_initialize(
	struct hw_ctx_digital_encoder_hal *ctx,
	enum hpd_source_id hpd_source,
	enum channel_id channel)
{
	uint32_t addr = mmAUX_CONTROL +
			FROM_HAL(ctx)->aux_channel_offset;

	uint32_t value = dal_read_reg(ctx->base.dal_ctx, addr);

	struct hw_ctx_digital_encoder_dce110 *ctx110 =
			HWCTX_DIGITAL_ENC110_FROM_BASE(ctx);

	set_reg_field_value(value, hpd_source, AUX_CONTROL, AUX_HPD_SEL);
	set_reg_field_value(value, 0, AUX_CONTROL, AUX_LS_READ_EN);
	dal_write_reg(ctx->base.dal_ctx, addr, value);

	/*increase AUX_RX_RECEIVE_WINDOW from 1/8 to 1/4 to avoid aux read
	 * problems. These problems are likely due to the fact that we're
	 * driving our aux CLK at 27/13 = 2.08 instead of exactly at 2Mhz.
	 * If the receiver's aux clock is low this can lead to problems with
	 * aux reads
	 */

	addr = mmAUX_DPHY_RX_CONTROL0 + ctx110->aux_channel_offset;
	value = dal_read_reg(ctx->base.dal_ctx, addr);

	/* 1/4 window (the maximum allowed) */
	set_reg_field_value(value, 1,
			AUX_DPHY_RX_CONTROL0, AUX_RX_RECEIVE_WINDOW);
	dal_write_reg(ctx->base.dal_ctx,
			mmAUX_DPHY_RX_CONTROL0 + ctx110->aux_channel_offset,
			value);

}

static void setup_encoder(
	struct hw_ctx_digital_encoder_hal *ctx,
	enum engine_id engine,
	enum transmitter transmitter,
	enum dig_encoder_mode dig_encoder_mode)
{
	const uint32_t addr = mmDIG_BE_CNTL + FROM_HAL(ctx)->be_engine_offset;
	uint32_t value = dal_read_reg(ctx->base.dal_ctx, addr);

	switch (dig_encoder_mode) {
	case DIG_ENCODER_MODE_DP:
	case DIG_ENCODER_MODE_DP_WITH_AUDIO:
		/* DP SST */
		set_reg_field_value(value, 0, DIG_BE_CNTL, DIG_MODE);
		break;
	case DIG_ENCODER_MODE_LVDS:
		/* LVDS */
		set_reg_field_value(value, 1, DIG_BE_CNTL, DIG_MODE);
		break;
	case DIG_ENCODER_MODE_DVI:
		/* TMDS-DVI */
		set_reg_field_value(value, 2, DIG_BE_CNTL, DIG_MODE);
		break;
	case DIG_ENCODER_MODE_HDMI:
		/* TMDS-HDMI */
		set_reg_field_value(value, 3, DIG_BE_CNTL, DIG_MODE);
		break;
	case DIG_ENCODER_MODE_SDVO:
		/* SDVO - reserved */
		set_reg_field_value(value, 4, DIG_BE_CNTL, DIG_MODE);
		break;
	case DIG_ENCODER_MODE_DP_MST:
		/* DP MST */
		set_reg_field_value(value, 5, DIG_BE_CNTL, DIG_MODE);
		break;
	default:
		ASSERT_CRITICAL(false);
		/* invalid mode ! */
		break;
	}

	dal_write_reg(ctx->base.dal_ctx, addr, value);
}

static void disable_encoder(
	struct hw_ctx_digital_encoder_hal *ctx,
	enum engine_id engine,
	enum transmitter transmitter,
	enum channel_id channel)
{
	uint32_t addr;
	uint32_t value;

	/* reset training pattern */
	addr = mmDP_DPHY_TRAINING_PATTERN_SEL +
		FROM_HAL(ctx)->be_engine_offset;
	value = dal_read_reg(ctx->base.dal_ctx, addr);
	set_reg_field_value(value, 0,
			DP_DPHY_TRAINING_PATTERN_SEL,
			DPHY_TRAINING_PATTERN_SEL);
	dal_write_reg(ctx->base.dal_ctx, addr, value);

	/* reset training complete */
	addr = mmDP_LINK_CNTL +
		FROM_HAL(ctx)->be_engine_offset;
	value = dal_read_reg(ctx->base.dal_ctx, addr);
	set_reg_field_value(value, 0, DP_LINK_CNTL, DP_LINK_TRAINING_COMPLETE);
	dal_write_reg(ctx->base.dal_ctx, addr, value);

	/* reset alternative scrambler */
	addr = mmDP_DPHY_INTERNAL_CTRL + FROM_HAL(ctx)->be_engine_offset;
	value = dal_read_reg(ctx->base.dal_ctx, addr);
	set_reg_field_value(value, 0,
			DP_DPHY_INTERNAL_CTRL, DPHY_ALT_SCRAMBLER_RESET_EN);
	set_reg_field_value(value, 0,
			DP_DPHY_INTERNAL_CTRL, DPHY_ALT_SCRAMBLER_RESET_SEL);
	dal_write_reg(ctx->base.dal_ctx, addr, value);

}

static void dp_steer_fifo_reset(
	struct dal_context *ctx,
	enum engine_id engine,
	bool reset)
{
	const uint32_t addr = mmDP_STEER_FIFO +
		fe_engine_offsets[engine];

	uint32_t value = dal_read_reg(ctx, addr);

	set_reg_field_value(value, reset, DP_STEER_FIFO, DP_STEER_FIFO_RESET);

	dal_write_reg(ctx, addr, value);
}

static void blank_dp_output(
	struct hw_ctx_digital_encoder_hal *ctx,
	enum engine_id engine,
	bool vid_stream_differ_to_sync)
{

	const uint32_t addr = mmDP_VID_STREAM_CNTL + fe_engine_offsets[engine];
	uint32_t value = dal_read_reg(ctx->base.dal_ctx, addr);
	uint32_t retries = 0;
	uint32_t max_retries = DP_BLANK_MAX_RETRY*10;

	/* DP_VID_STREAM_DIS_DEFER is used to specify
	 * the video stream disable point, which can be immediate,
	 * at the start of the next horizontal blank,
	 * or the start of the next vertical blank.
	 * It is recommended to always use the default value of
	 * "start of the vertical blank". */

	/*if (vid_stream_differ_to_sync == false)	{*/
		/* this is driver default
		 * Note from testing, change of DP_VID_STREAM_DIS_DEFER takes
		 * effect immediately and does not require two register writes.
		 * Specify the video stream disable point
		 * (1 = start of the next horizontal blank) */
		/*set_reg_field_value(value, 1,
			DP_VID_STREAM_CNTL, DP_VID_STREAM_DIS_DEFER); */
		/* Shorter delay since we defer to HBLANK*/
		/*max_retries = DP_BLANK_MAX_RETRY;
	} else */ {

		/* Note: For CZ, we are changing driver default to disable
		 * stream deferred to next VBLANK. If results are positive, we
		 * will make the same change to all DCE versions. There are a
		 * handful of panels that cannot handle disable stream at
		 * HBLANK and will result in a white line flash across the
		 * screen on stream disable. */

		/* Specify the video stream disable point
		 * (2 = start of the next vertical blank) */
		set_reg_field_value(value, 2,
				DP_VID_STREAM_CNTL, DP_VID_STREAM_DIS_DEFER);
		/* Larger delay to wait until VBLANK - use max retry of
		 * 10us*3000=30ms. This covers 16.6ms of typical 60 Hz mode +
		 * a little more because we may not trust delay accuracy. */
		max_retries = DP_BLANK_MAX_RETRY*150;
	}

	/* disable DP stream */
	set_reg_field_value(value, 0,
			DP_VID_STREAM_CNTL, DP_VID_STREAM_ENABLE);
	dal_write_reg(ctx->base.dal_ctx, addr, value);

	/* the encoder stops sending the video stream
	 * at the start of the vertical blanking.
	 * Poll for DP_VID_STREAM_STATUS == 0 */

	do {
		value = dal_read_reg(ctx->base.dal_ctx, addr);

		if (!get_reg_field_value(value,
				DP_VID_STREAM_CNTL, DP_VID_STREAM_STATUS))
			break;

		dal_delay_in_microseconds(10);

		++retries;
	} while (retries < max_retries);

	ASSERT(retries <= max_retries);


	/* Tell the DP encoder to ignore timing from CRTC, must be done after
	 * the polling. If we set DP_STEER_FIFO_RESET before DP stream blank is
	 * complete, stream status will be stuck in video stream enabled state,
	 * i.e. DP_VID_STREAM_STATUS stuck at 1. */
	dp_steer_fifo_reset(ctx->base.dal_ctx, engine, true);
}

static void unblank_dp_output(
	struct hw_ctx_digital_encoder_hal *ctx,
	enum engine_id engine)
{
	uint32_t addr;
	uint32_t value;

	/* set DIG_START to 0x1 to resync FIFO */
	addr = mmDIG_FE_CNTL + fe_engine_offsets[engine];
	value = dal_read_reg(ctx->base.dal_ctx, addr);
	set_reg_field_value(value, 1, DIG_FE_CNTL, DIG_START);
	dal_write_reg(ctx->base.dal_ctx, addr, value);


	/* switch DP encoder to CRTC data */
	dp_steer_fifo_reset(ctx->base.dal_ctx, engine, false);

	/* wait 100us for DIG/DP logic to prime
	 * (i.e. a few video lines) */
	dal_delay_in_microseconds(100);


	/* the hardware would start sending video at the start of the next DP
	 * frame (i.e. rising edge of the vblank).
	 * NOTE: We used to program DP_VID_STREAM_DIS_DEFER = 2 here, but this
	 * register has no effect on enable transition! HW always guarantees
	 * VID_STREAM enable at start of next frame, and this is not
	 * programmable */
	addr = mmDP_VID_STREAM_CNTL + fe_engine_offsets[engine];
	value = dal_read_reg(ctx->base.dal_ctx, addr);
	set_reg_field_value(
		value, true, DP_VID_STREAM_CNTL, DP_VID_STREAM_ENABLE);
	dal_write_reg(ctx->base.dal_ctx, addr, value);

}

static void setup_vid_stream(
	struct hw_ctx_digital_encoder_hal *ctx,
	enum engine_id engine,
	uint32_t m_vid,
	uint32_t n_vid)
{
	uint32_t addr;
	uint32_t value;

	/* enable auto measurement */
	addr = mmDP_VID_TIMING + fe_engine_offsets[engine];
	value = dal_read_reg(ctx->base.dal_ctx, addr);
	set_reg_field_value(value, 0, DP_VID_TIMING, DP_VID_M_N_GEN_EN);
	dal_write_reg(ctx->base.dal_ctx, addr, value);


	/* auto measurement need 1 full 0x8000 symbol cycle to kick in,
	 * therefore program initial value for Mvid and Nvid */
	addr = mmDP_VID_N + fe_engine_offsets[engine];
	value = dal_read_reg(ctx->base.dal_ctx, addr);
	set_reg_field_value(value, n_vid, DP_VID_N, DP_VID_N);
	dal_write_reg(ctx->base.dal_ctx, addr, value);


	addr = mmDP_VID_M + fe_engine_offsets[engine];
	value = dal_read_reg(ctx->base.dal_ctx, addr);
	set_reg_field_value(value, m_vid, DP_VID_M, DP_VID_M);
	dal_write_reg(ctx->base.dal_ctx, addr, value);


	addr = mmDP_VID_TIMING + fe_engine_offsets[engine];
	value = dal_read_reg(ctx->base.dal_ctx, addr);
	set_reg_field_value(value, 1, DP_VID_TIMING, DP_VID_M_N_GEN_EN);
	dal_write_reg(ctx->base.dal_ctx, addr, value);
}

static enum dce110_dig_fe_source_select get_frontend_source(
	enum engine_id engine)
{
	switch (engine) {
	case ENGINE_ID_DIGA:
		return DCE110_DIG_FE_SOURCE_SELECT_DIGA;
	case ENGINE_ID_DIGB:
		return DCE110_DIG_FE_SOURCE_SELECT_DIGB;
	case ENGINE_ID_DIGC:
		return DCE110_DIG_FE_SOURCE_SELECT_DIGC;
	default:
		ASSERT_CRITICAL(false);
		return DCE110_DIG_FE_SOURCE_SELECT_INVALID;
	}
}

static void configure_encoder(
	struct hw_ctx_digital_encoder_hal *ctx,
	enum engine_id engine,
	enum transmitter transmitter,
	enum channel_id channel,
	const struct link_settings *link_settings,
	uint32_t pixel_clock_in_khz,
	enum dp_alt_scrambler_reset alt_scrambler_reset)
{
	uint32_t addr;
	uint32_t value;

	/* set number of lanes */
	addr = mmDP_CONFIG + FROM_HAL(ctx)->be_engine_offset;
	value = dal_read_reg(ctx->base.dal_ctx, addr);
	set_reg_field_value(value, link_settings->lane_count - LANE_COUNT_ONE,
			DP_CONFIG, DP_UDI_LANES);
	dal_write_reg(ctx->base.dal_ctx, addr, value);

	/* set enhanced frame mode */
	addr = mmDP_LINK_FRAMING_CNTL + fe_engine_offsets[engine];
	value = dal_read_reg(ctx->base.dal_ctx, addr);
	set_reg_field_value(value, 1,
			DP_LINK_FRAMING_CNTL, DP_VID_ENHANCED_FRAME_MODE);
	dal_write_reg(ctx->base.dal_ctx, addr, value);


	addr = mmDIG_BE_CNTL + FROM_HAL(ctx)->be_engine_offset;
	value = dal_read_reg(ctx->base.dal_ctx, addr);
	set_reg_field_value(value, get_frontend_source(engine),
			DIG_BE_CNTL, DIG_FE_SOURCE_SELECT);
	dal_write_reg(ctx->base.dal_ctx, addr, value);

}

static bool get_lane_settings(
	struct hw_ctx_digital_encoder_hal *ctx,
	enum transmitter transmitter,
	struct link_training_settings *link_training_settings)
{
	struct dal_context *dal_context = ctx->base.dal_ctx;

	NOT_IMPLEMENTED();
	return true;
}

static bool enable_dvo_sync_output(
	struct hw_ctx_digital_encoder_hal *ctx,
	enum sync_source source)
{
	struct dal_context *dal_context = ctx->base.dal_ctx;

	NOT_IMPLEMENTED();
	return false;
}

static bool disable_dvo_sync_output(
	struct hw_ctx_digital_encoder_hal *ctx)
{
	struct dal_context *dal_context = ctx->base.dal_ctx;

	NOT_IMPLEMENTED();
	return false;
}

static void stop_hdmi_info_packets(
	struct dal_context *ctx,
	uint32_t offset)
{
	uint32_t addr = 0;
	uint32_t value = 0;

	/* stop generic packets 0 & 1 on HDMI */
	addr = mmHDMI_GENERIC_PACKET_CONTROL0 + offset;

	value = dal_read_reg(ctx, addr);

	set_reg_field_value(
		value, 0, HDMI_GENERIC_PACKET_CONTROL0, HDMI_GENERIC1_CONT);
	set_reg_field_value(
		value, 0, HDMI_GENERIC_PACKET_CONTROL0, HDMI_GENERIC1_LINE);
	set_reg_field_value(
		value, 0, HDMI_GENERIC_PACKET_CONTROL0, HDMI_GENERIC1_SEND);
	set_reg_field_value(
		value, 0, HDMI_GENERIC_PACKET_CONTROL0, HDMI_GENERIC0_CONT);
	set_reg_field_value(
		value, 0, HDMI_GENERIC_PACKET_CONTROL0, HDMI_GENERIC0_LINE);
	set_reg_field_value(
		value, 0, HDMI_GENERIC_PACKET_CONTROL0, HDMI_GENERIC0_SEND);

	dal_write_reg(ctx, addr, value);

	/* stop generic packets 2 & 3 on HDMI */
	addr = mmHDMI_GENERIC_PACKET_CONTROL1 + offset;

	value = dal_read_reg(ctx, addr);

	set_reg_field_value(
		value, 0, HDMI_GENERIC_PACKET_CONTROL1, HDMI_GENERIC2_CONT);
	set_reg_field_value(
		value, 0, HDMI_GENERIC_PACKET_CONTROL1, HDMI_GENERIC2_LINE);
	set_reg_field_value(
		value, 0, HDMI_GENERIC_PACKET_CONTROL1, HDMI_GENERIC2_SEND);
	set_reg_field_value(
		value, 0, HDMI_GENERIC_PACKET_CONTROL1, HDMI_GENERIC3_CONT);
	set_reg_field_value(
		value, 0, HDMI_GENERIC_PACKET_CONTROL1, HDMI_GENERIC3_LINE);
	set_reg_field_value(
		value, 0, HDMI_GENERIC_PACKET_CONTROL1, HDMI_GENERIC3_SEND);

	dal_write_reg(ctx, addr, value);

	/* stop AVI packet on HDMI */
	addr = mmHDMI_INFOFRAME_CONTROL0 + offset;

	value = dal_read_reg(ctx, addr);

	set_reg_field_value(
		value, 0, HDMI_INFOFRAME_CONTROL0, HDMI_AVI_INFO_SEND);
	set_reg_field_value(
		value, 0, HDMI_INFOFRAME_CONTROL0, HDMI_AVI_INFO_CONT);

	dal_write_reg(ctx, addr, value);
}

static void stop_dp_info_packets(
	struct dal_context *ctx,
	int32_t offset)
{
	/* stop generic packets on DP */

	const uint32_t addr = mmDP_SEC_CNTL + offset;

	uint32_t value = dal_read_reg(ctx, addr);

	set_reg_field_value(value, 0, DP_SEC_CNTL, DP_SEC_GSP0_ENABLE);
	set_reg_field_value(value, 0, DP_SEC_CNTL, DP_SEC_GSP1_ENABLE);
	set_reg_field_value(value, 0, DP_SEC_CNTL, DP_SEC_GSP2_ENABLE);
	set_reg_field_value(value, 0, DP_SEC_CNTL, DP_SEC_GSP3_ENABLE);
	set_reg_field_value(value, 0, DP_SEC_CNTL, DP_SEC_AVI_ENABLE);
	set_reg_field_value(value, 0, DP_SEC_CNTL, DP_SEC_MPG_ENABLE);
	set_reg_field_value(value, 0, DP_SEC_CNTL, DP_SEC_STREAM_ENABLE);

	/* this register shared with audio info frame.
		* therefore we need to keep master enabled
		* if at least one of the fields is not 0 */

	if (value)
		set_reg_field_value(value, 1,
				DP_SEC_CNTL, DP_SEC_STREAM_ENABLE);

	dal_write_reg(ctx, addr, value);
}


static void stop_info_packets(
	struct hw_ctx_digital_encoder_hal *ctx,
	enum engine_id engine,
	enum signal_type signal)
{
	if (dal_is_hdmi_signal(signal))
		stop_hdmi_info_packets(
			ctx->base.dal_ctx,
			fe_engine_offsets[engine]);
	else if (dal_is_dp_signal(signal))
		stop_dp_info_packets(
			ctx->base.dal_ctx,
			fe_engine_offsets[engine]);
}

static void update_generic_info_packet(
	struct hw_ctx_digital_encoder_hal *ctx,
	enum engine_id engine,
	uint32_t packet_index,
	const struct encoder_info_packet *info_packet)
{
	uint32_t addr;
	uint32_t regval;
	/* choose which generic packet to use */
	{
		addr = mmAFMT_VBI_PACKET_CONTROL +
			fe_engine_offsets[engine];

		regval = dal_read_reg(ctx->base.dal_ctx, addr);

		set_reg_field_value(regval, packet_index,
				AFMT_VBI_PACKET_CONTROL,
				AFMT_GENERIC_INDEX);

		dal_write_reg(ctx->base.dal_ctx, addr, regval);
	}

	/* write generic packet header
	 * (4th byte is for GENERIC0 only) */
	{
		addr = mmAFMT_GENERIC_HDR +
			fe_engine_offsets[engine];

		regval = 0;

		set_reg_field_value(regval, info_packet->hb0,
				AFMT_GENERIC_HDR,
				AFMT_GENERIC_HB0);

		set_reg_field_value(regval, info_packet->hb1,
				AFMT_GENERIC_HDR,
				AFMT_GENERIC_HB1);

		set_reg_field_value(regval, info_packet->hb2,
				AFMT_GENERIC_HDR,
				AFMT_GENERIC_HB2);

		set_reg_field_value(regval, info_packet->hb3,
				AFMT_GENERIC_HDR,
				AFMT_GENERIC_HB3);

		dal_write_reg(ctx->base.dal_ctx, addr, regval);
	}

	/* write generic packet contents
	 * (we never use last 4 bytes)
	 * there are 8 (0-7) mmDIG0_AFMT_GENERIC0_x registers */
	{
		const uint32_t *content = (const uint32_t *)&info_packet->sb[0];

		uint32_t counter = 0;

		addr = mmAFMT_GENERIC_0 +
			fe_engine_offsets[engine];

		do {
			dal_write_reg(ctx->base.dal_ctx, addr++, *content++);

			++counter;
		} while (counter < 7);
	}

	dal_write_reg(ctx->base.dal_ctx,
		mmAFMT_GENERIC_7 + fe_engine_offsets[engine], 0);

	/* force double-buffered packet update */
	{
		addr = mmAFMT_VBI_PACKET_CONTROL +
			fe_engine_offsets[engine];

		regval = dal_read_reg(ctx->base.dal_ctx, addr);

		set_reg_field_value(regval, (packet_index == 0),
				AFMT_VBI_PACKET_CONTROL,
				AFMT_GENERIC0_UPDATE);

		set_reg_field_value(regval, (packet_index == 2),
				AFMT_VBI_PACKET_CONTROL,
				AFMT_GENERIC2_UPDATE);

		dal_write_reg(ctx->base.dal_ctx, addr, regval);
	}
}

static void update_avi_info_packet(
	struct hw_ctx_digital_encoder_hal *ctx,
	enum engine_id engine,
	enum signal_type signal,
	const struct encoder_info_packet *info_packet)
{
	const int32_t offset = fe_engine_offsets[engine];

	uint32_t regval;
	uint32_t addr;

	if (info_packet->valid) {
		const uint32_t *content =
			(const uint32_t *)&info_packet->sb[0];

		{
			regval = content[0];

			dal_write_reg(ctx->base.dal_ctx,
				mmAFMT_AVI_INFO0 + offset, regval);
		}
		{
			regval = content[1];

			dal_write_reg(ctx->base.dal_ctx,
				mmAFMT_AVI_INFO1 + offset, regval);
		}
		{
			regval = content[2];

			dal_write_reg(ctx->base.dal_ctx,
				mmAFMT_AVI_INFO2 + offset, regval);
		}
		{
			regval = content[3];

			/* move version to AVI_INFO3 */
			set_reg_field_value(regval, info_packet->hb1,
					AFMT_AVI_INFO3, AFMT_AVI_INFO_VERSION);

			dal_write_reg(ctx->base.dal_ctx,
				mmAFMT_AVI_INFO3 + offset, regval);
		}

		if (dal_is_hdmi_signal(signal)) {

			uint32_t control0val;
			uint32_t control1val;

			addr = mmHDMI_INFOFRAME_CONTROL0 + offset;

			control0val = dal_read_reg(ctx->base.dal_ctx, addr);

			set_reg_field_value(control0val, 1,
					HDMI_INFOFRAME_CONTROL0,
					HDMI_AVI_INFO_SEND);

			set_reg_field_value(control0val, 1,
					HDMI_INFOFRAME_CONTROL0,
					HDMI_AVI_INFO_CONT);

			dal_write_reg(ctx->base.dal_ctx, addr, control0val);

			addr = mmHDMI_INFOFRAME_CONTROL1 + offset;

			control1val = dal_read_reg(ctx->base.dal_ctx, addr);

			set_reg_field_value(control1val, VBI_LINE_0 + 2,
					HDMI_INFOFRAME_CONTROL1,
					HDMI_AVI_INFO_LINE);

			dal_write_reg(ctx->base.dal_ctx, addr, control1val);
		}
	} else if (dal_is_hdmi_signal(signal)) {
		addr = mmHDMI_INFOFRAME_CONTROL0 + offset;

		regval = dal_read_reg(ctx->base.dal_ctx, addr);

		set_reg_field_value(regval, 0,
					HDMI_INFOFRAME_CONTROL0,
					HDMI_AVI_INFO_SEND);

		set_reg_field_value(regval, 0,
					HDMI_INFOFRAME_CONTROL0,
					HDMI_AVI_INFO_CONT);

		dal_write_reg(ctx->base.dal_ctx, addr, regval);
	}
}

static void update_hdmi_info_packet(
	struct hw_ctx_digital_encoder_hal *ctx,
	enum engine_id engine,
	uint32_t packet_index,
	const struct encoder_info_packet *info_packet)
{
	uint32_t cont, send, line;
	uint32_t addr = fe_engine_offsets[engine];
	uint32_t regval;

	if (info_packet->valid) {
		update_generic_info_packet(
			ctx, engine, packet_index, info_packet);

		/* enable transmission of packet(s) -
		 * packet transmission begins on the next frame */
		cont = 1;
		/* send packet(s) every frame */
		send = 1;
		/* select line number to send packets on */
		line = 2;
	} else {
		cont = 0;
		send = 0;
		line = 0;
	}

	/* choose which generic packet control to use */

	switch (packet_index) {
	case 0:
	case 1:
		addr += mmHDMI_GENERIC_PACKET_CONTROL0;
		break;
	case 2:
	case 3:
		addr += mmHDMI_GENERIC_PACKET_CONTROL1;
		break;
	default:
		/* invalid HW packet index */
		dal_logger_write(ctx->base.dal_ctx->logger,
				LOG_MAJOR_WARNING,
				LOG_MINOR_COMPONENT_ENCODER,
				"Invalid HW packet index: %s()\n",
				__func__);
		break;
	}

	regval = dal_read_reg(ctx->base.dal_ctx, addr);

	switch (packet_index) {
	case 0:
	case 2:
		set_reg_field_value(regval, cont,
					HDMI_GENERIC_PACKET_CONTROL0,
					HDMI_GENERIC0_CONT);
		set_reg_field_value(regval, send,
					HDMI_GENERIC_PACKET_CONTROL0,
					HDMI_GENERIC0_SEND);
		set_reg_field_value(regval, line,
					HDMI_GENERIC_PACKET_CONTROL0,
					HDMI_GENERIC0_LINE);
		break;
	case 1:
	case 3:
		set_reg_field_value(regval, cont,
					HDMI_GENERIC_PACKET_CONTROL0,
					HDMI_GENERIC1_CONT);
		set_reg_field_value(regval, send,
					HDMI_GENERIC_PACKET_CONTROL0,
					HDMI_GENERIC1_SEND);
		set_reg_field_value(regval, line,
					HDMI_GENERIC_PACKET_CONTROL0,
					HDMI_GENERIC1_LINE);
		break;
	default:
		/* invalid HW packet index */
		dal_logger_write(ctx->base.dal_ctx->logger,
				LOG_MAJOR_WARNING,
				LOG_MINOR_COMPONENT_ENCODER,
				"Invalid HW packet index: %s()\n",
				__func__);
		break;
	}

	dal_write_reg(ctx->base.dal_ctx, addr, regval);
}


static void update_dp_info_packet(
	struct hw_ctx_digital_encoder_hal *ctx,
	enum engine_id engine,
	uint32_t packet_index,
	const struct encoder_info_packet *info_packet)
{
	const uint32_t addr = mmDP_SEC_CNTL + fe_engine_offsets[engine];

	uint32_t value;

	if (info_packet->valid)
		update_generic_info_packet(
			ctx, engine, packet_index, info_packet);

	/* enable/disable transmission of packet(s).
	 * If enabled, packet transmission begins on the next frame */

	value = dal_read_reg(ctx->base.dal_ctx, addr);

	switch (packet_index) {
	case 0:
		set_reg_field_value(value, info_packet->valid,
				DP_SEC_CNTL, DP_SEC_GSP0_ENABLE);
		break;
	case 1:
		set_reg_field_value(value, info_packet->valid,
				DP_SEC_CNTL, DP_SEC_GSP1_ENABLE);
		break;
	case 2:
		set_reg_field_value(value, info_packet->valid,
				DP_SEC_CNTL, DP_SEC_GSP2_ENABLE);
		break;
	case 3:
		set_reg_field_value(value, info_packet->valid,
				DP_SEC_CNTL, DP_SEC_GSP3_ENABLE);
	break;
	default:
		/* invalid HW packet index */
		ASSERT_CRITICAL(false);
		return;
	}

	/* This bit is the master enable bit.
	 * When enabling secondary stream engine,
	 * this master bit must also be set.
	 * This register shared with audio info frame.
	 * Therefore we need to enable master bit
	 * if at least on of the fields is not 0 */

	if (value)
		set_reg_field_value(value, 1,
				DP_SEC_CNTL, DP_SEC_STREAM_ENABLE);

	dal_write_reg(ctx->base.dal_ctx, addr, value);
}

static void update_info_packets(
	struct hw_ctx_digital_encoder_hal *ctx,
	enum engine_id engine,
	enum signal_type signal,
	const struct encoder_info_frame *info_frame)
{
	if (dal_is_hdmi_signal(signal)) {
		update_avi_info_packet(
				ctx, engine, signal, &info_frame->avi);
		update_hdmi_info_packet(
				ctx, engine, 0, &info_frame->vendor);
		update_hdmi_info_packet(
				ctx, engine, 1, &info_frame->gamut);
		update_hdmi_info_packet(
				ctx, engine, 2, &info_frame->spd);
	} else if (dal_is_dp_signal(signal))
		update_dp_info_packet(
				ctx, engine, 0, &info_frame->vsc);
}

static void setup_hdmi(
	struct hw_ctx_digital_encoder_hal *ctx,
	enum engine_id engine,
	const struct hw_crtc_timing *timing)
{
	uint32_t output_pixel_clock = timing->pixel_clock;
	uint32_t value;
	uint32_t addr;


	addr = mmHDMI_CONTROL + fe_engine_offsets[engine];
	value = dal_read_reg(ctx->base.dal_ctx, addr);
	set_reg_field_value(value, 1,
			HDMI_CONTROL, HDMI_PACKET_GEN_VERSION);
	set_reg_field_value(value, 1,
			HDMI_CONTROL, HDMI_KEEPOUT_MODE);
	set_reg_field_value(value, 0,
			HDMI_CONTROL, HDMI_DEEP_COLOR_ENABLE);
	set_reg_field_value(value, 0,
			HDMI_CONTROL, HDMI_DATA_SCRAMBLE_EN);
	set_reg_field_value(value, 0,
			HDMI_CONTROL, HDMI_CLOCK_CHANNEL_RATE);


	switch (timing->flags.COLOR_DEPTH) {
	case HW_COLOR_DEPTH_888:
		set_reg_field_value(value, 0,
				HDMI_CONTROL, HDMI_DEEP_COLOR_DEPTH);
		break;
	case HW_COLOR_DEPTH_101010:
		set_reg_field_value(value, 1,
				HDMI_CONTROL, HDMI_DEEP_COLOR_DEPTH);
		set_reg_field_value(value, 1,
				HDMI_CONTROL, HDMI_DEEP_COLOR_ENABLE);
		output_pixel_clock = (timing->pixel_clock*30)/24;
		break;
	case HW_COLOR_DEPTH_121212:
		set_reg_field_value(value, 2,
				HDMI_CONTROL, HDMI_DEEP_COLOR_DEPTH);
		set_reg_field_value(value, 1,
				HDMI_CONTROL, HDMI_DEEP_COLOR_ENABLE);
		output_pixel_clock = (timing->pixel_clock*36)/24;
		break;
	case HW_COLOR_DEPTH_161616:
		set_reg_field_value(value, 3,
				HDMI_CONTROL, HDMI_DEEP_COLOR_DEPTH);
		set_reg_field_value(value, 1,
				HDMI_CONTROL, HDMI_DEEP_COLOR_ENABLE);
		output_pixel_clock = (timing->pixel_clock * 48)/24;
		break;
	default:
		break;
	}

	if (output_pixel_clock >= HDMI_CLOCK_CHANNEL_RATE_MORE_340M) {
		/* enable HDMI data scrambler */
		set_reg_field_value(value, 1,
				HDMI_CONTROL, HDMI_DATA_SCRAMBLE_EN);

		/* HDMI_CLOCK_CHANNEL_RATE_MORE_340M
		 * Clock channel frequency is 1/4 of character rate.*/
		set_reg_field_value(value, 1,
				HDMI_CONTROL, HDMI_CLOCK_CHANNEL_RATE);
	} else if (timing->flags.LTE_340MCSC_SCRAMBLE) {

		/* TODO: New feature for DCE11, still need to implement */
		struct dal_context *dal_context = ctx->base.dal_ctx;

		NOT_IMPLEMENTED();

		/* enable HDMI data scrambler */
		set_reg_field_value(value, 1,
				HDMI_CONTROL, HDMI_DATA_SCRAMBLE_EN);

		/* HDMI_CLOCK_CHANNEL_FREQ_EQUAL_TO_CHAR_RATE
		 * Clock channel frequency is the same
		 * as character rate */
		set_reg_field_value(value, 0,
				HDMI_CONTROL, HDMI_CLOCK_CHANNEL_RATE);
	}

	dal_write_reg(ctx->base.dal_ctx, addr, value);


	addr = mmHDMI_VBI_PACKET_CONTROL + fe_engine_offsets[engine];
	value = dal_read_reg(ctx->base.dal_ctx, addr);
	set_reg_field_value(value, 1,
			HDMI_VBI_PACKET_CONTROL, HDMI_GC_CONT);
	set_reg_field_value(value, 1,
			HDMI_VBI_PACKET_CONTROL, HDMI_GC_SEND);
	set_reg_field_value(value, 1,
			HDMI_VBI_PACKET_CONTROL, HDMI_NULL_SEND);

	dal_write_reg(ctx->base.dal_ctx, addr, value);


	/* following belongs to audio */
	addr = mmHDMI_INFOFRAME_CONTROL0 + fe_engine_offsets[engine];
	value = dal_read_reg(ctx->base.dal_ctx, addr);
	set_reg_field_value(value, 1,
			HDMI_INFOFRAME_CONTROL0, HDMI_AUDIO_INFO_SEND);
	dal_write_reg(ctx->base.dal_ctx, addr, value);


	addr = mmAFMT_INFOFRAME_CONTROL0 + fe_engine_offsets[engine];
	value = dal_read_reg(ctx->base.dal_ctx, addr);
	set_reg_field_value(value, 1,
			AFMT_INFOFRAME_CONTROL0,
			AFMT_AUDIO_INFO_UPDATE);
	dal_write_reg(ctx->base.dal_ctx, addr, value);


	addr = mmHDMI_INFOFRAME_CONTROL1 + fe_engine_offsets[engine];
	value = dal_read_reg(ctx->base.dal_ctx, addr);
	set_reg_field_value(value, VBI_LINE_0 + 2,
			HDMI_INFOFRAME_CONTROL1, HDMI_AUDIO_INFO_LINE);
	dal_write_reg(ctx->base.dal_ctx, addr, value);


	addr = mmHDMI_GC + fe_engine_offsets[engine];
	value = dal_read_reg(ctx->base.dal_ctx, addr);
	set_reg_field_value(value, 0,
			HDMI_GC, HDMI_GC_AVMUTE);
	dal_write_reg(ctx->base.dal_ctx, addr, value);

}

static void set_lcd_backlight_level(
	struct hw_ctx_digital_encoder_hal *ctx,
	uint32_t level)
{
	struct dal_context *dal_context = ctx->base.dal_ctx;

	const uint32_t backlight_update_pending_max_retry = 1000;

	uint32_t backlight;
	uint32_t backlight_period;
	uint32_t backlight_lock;

	uint32_t i;
	uint32_t backlight_24bit;
	uint32_t backlight_17bit;
	uint32_t backlight_16bit;
	uint32_t masked_pwm_period;
	uint8_t rounding_bit;
	uint8_t bit_count;
	uint64_t active_duty_cycle;

	backlight = dal_read_reg(dal_context, mmBL_PWM_CNTL);
	backlight_period = dal_read_reg(dal_context, mmBL_PWM_PERIOD_CNTL);
	backlight_lock = dal_read_reg(dal_context, mmBL_PWM_GRP1_REG_LOCK);

	/*
	 * 1. Convert 8-bit value to 17 bit U1.16 format
	 * (1 integer, 16 fractional bits)
	 */

	/* 1.1 multiply 8 bit value by 0x10101 to get a 24 bit value,
	 * effectively multiplying value by 256/255
	 * eg. for a level of 0xEF, backlight_24bit = 0xEF * 0x10101 = 0xEFEFEF
	 */
	backlight_24bit = level * 0x10101;

	/* 1.2 The upper 16 bits of the 24 bit value is the fraction, lower 8
	 * used for rounding, take most significant bit of fraction for
	 * rounding, e.g. for 0xEFEFEF, rounding bit is 1
	 */
	rounding_bit = (backlight_24bit >> 7) & 1;

	/* 1.3 Add the upper 16 bits of the 24 bit value with the rounding bit
	 * resulting in a 17 bit value e.g. 0xEFF0 = (0xEFEFEF >> 8) + 1
	 */
	backlight_17bit = (backlight_24bit >> 8) + rounding_bit;

	/*
	 * 2. Find  16 bit backlight active duty cycle, where 0 <= backlight
	 * active duty cycle <= backlight period
	 */

	/* 2.1 Apply bitmask for backlight period value based on value of BITCNT
	 */
	{
		uint32_t pwm_period_bitcnt =
			get_reg_field_value(
				backlight_period,
				BL_PWM_PERIOD_CNTL,
				BL_PWM_PERIOD_BITCNT);
		if (pwm_period_bitcnt == 0)
			bit_count = 16;
		else
			bit_count = pwm_period_bitcnt;
	}

	/* e.g. maskedPwmPeriod = 0x24 when bitCount is 6 */
	masked_pwm_period =
		get_reg_field_value(
			backlight_period,
			BL_PWM_PERIOD_CNTL,
			BL_PWM_PERIOD) & ((1 << bit_count) - 1);

	/* 2.2 Calculate integer active duty cycle required upper 16 bits
	 * contain integer component, lower 16 bits contain fractional component
	 * of active duty cycle e.g. 0x21BDC0 = 0xEFF0 * 0x24
	 */
	active_duty_cycle = backlight_17bit * masked_pwm_period;

	/* 2.3 Calculate 16 bit active duty cycle from integer and fractional
	 * components shift by bitCount then mask 16 bits and add rounding bit
	 * from MSB of fraction e.g. 0x86F7 = ((0x21BDC0 >> 6) & 0xFFF) + 0
	 */
	backlight_16bit = active_duty_cycle >> bit_count;
	backlight_16bit &= 0xFFFF;
	backlight_16bit += (active_duty_cycle >> (bit_count - 1)) & 0x1;
	set_reg_field_value(
		backlight,
		backlight_16bit,
		BL_PWM_CNTL,
		BL_ACTIVE_INT_FRAC_CNT);

	/*
	 * 3. Program register with updated value
	 */

	/* 3.1 Lock group 2 backlight registers */
	set_reg_field_value(
		backlight_lock,
		1,
		BL_PWM_GRP1_REG_LOCK,
		BL_PWM_GRP1_IGNORE_MASTER_LOCK_EN);
	set_reg_field_value(
		backlight_lock,
		1,
		BL_PWM_GRP1_REG_LOCK,
		BL_PWM_GRP1_REG_LOCK);
	dal_write_reg(dal_context, mmBL_PWM_GRP1_REG_LOCK, backlight_lock);

	/* 3.2 Write new active duty cycle */
	dal_write_reg(dal_context, mmBL_PWM_CNTL, backlight);

	/* 3.3 Unlock group 2 backlight registers */
	set_reg_field_value(
		backlight_lock,
		0,
		BL_PWM_GRP1_REG_LOCK,
		BL_PWM_GRP1_REG_LOCK);
	dal_write_reg(dal_context, mmBL_PWM_GRP1_REG_LOCK, backlight_lock);

	/* 5.4.4 Wait for pending bit to be cleared */
	for (i = 0; i < backlight_update_pending_max_retry; ++i) {
		backlight_lock =
			dal_read_reg(dal_context, mmBL_PWM_GRP1_REG_LOCK);
		if (!get_reg_field_value(
			backlight_lock,
			BL_PWM_GRP1_REG_LOCK,
			BL_PWM_GRP1_REG_UPDATE_PENDING))
			break;

		dal_delay_in_microseconds(10);
	}
}

static void backlight_control(
	struct hw_ctx_digital_encoder_hal *ctx,
	bool enable)
{
	/*TODO: double check if backlight level control is for LVDS only.
	 * If yes, this function is not needed for android becuase LVDS is not
	 * supported. If yes, need implemented.*/
	struct dal_context *dal_context = ctx->base.dal_ctx;

	NOT_IMPLEMENTED();
}

static void enable_mvpu_downstream(
	struct hw_ctx_digital_encoder_hal *ctx,
	enum signal_type downstream_signal)
{
	/*TODO android does not support mvpu, should remove functions
	 * for upper layer  */
	struct dal_context *dal_context = ctx->base.dal_ctx;

	NOT_IMPLEMENTED();
}

static void disable_mvpu_downstream(struct hw_ctx_digital_encoder_hal *ctx)
{
	/*TODO android does not support mvpu, should remove functions
	 * for upper layer  */
	struct dal_context *dal_context = ctx->base.dal_ctx;

	NOT_IMPLEMENTED();
}

static void update_mst_stream_allocation_table(
	struct hw_ctx_digital_encoder_hal *ctx,
	enum transmitter transmitter,
	const struct dp_mst_stream_allocation_table *table,
	bool is_removal)
{
	struct dal_context *dal_context = ctx->base.dal_ctx;

	NOT_IMPLEMENTED();
}

static void set_mst_bandwidth(
	struct hw_ctx_digital_encoder_hal *ctx,
	enum engine_id engine,
	struct fixed31_32 avg_time_slots_per_mtp)
{
	struct dal_context *dal_context = ctx->base.dal_ctx;

	NOT_IMPLEMENTED();
}

static void setup_assr(
	struct dal_context *ctx,
	const int32_t addr_offset,
	enum dp_alt_scrambler_reset reset)
{
	const uint32_t addr = mmDP_DPHY_INTERNAL_CTRL + addr_offset;

	uint32_t value = dal_read_reg(ctx, addr);

	switch (reset) {
	case DP_ALT_SCRAMBLER_RESET_STANDARD:
		set_reg_field_value(value, 1,
				DP_DPHY_INTERNAL_CTRL,
				DPHY_ALT_SCRAMBLER_RESET_EN);
		set_reg_field_value(value, 0,
				DP_DPHY_INTERNAL_CTRL,
				DPHY_ALT_SCRAMBLER_RESET_SEL);
		break;
	case DP_ALT_SCRAMBLER_RESET_SPECIAL:
		set_reg_field_value(value, 1,
				DP_DPHY_INTERNAL_CTRL,
				DPHY_ALT_SCRAMBLER_RESET_EN);
		set_reg_field_value(value, 1,
				DP_DPHY_INTERNAL_CTRL,
				DPHY_ALT_SCRAMBLER_RESET_SEL);
		break;
	default:
		set_reg_field_value(value, 0,
				DP_DPHY_INTERNAL_CTRL,
				DPHY_ALT_SCRAMBLER_RESET_EN);
		set_reg_field_value(value, 0,
				DP_DPHY_INTERNAL_CTRL,
				DPHY_ALT_SCRAMBLER_RESET_SEL);
		break;
	}

	dal_write_reg(ctx, addr, value);
}

static void set_link_training_complete(
	struct dal_context *ctx,
	const int32_t be_addr_offset,
	bool complete)
{
	/* This register resides in DP back end block;
	 * transmitter is used for the offset */

	const uint32_t addr = mmDP_LINK_CNTL + be_addr_offset;

	uint32_t value = dal_read_reg(ctx, addr);

	set_reg_field_value(value, complete,
			DP_LINK_CNTL, DP_LINK_TRAINING_COMPLETE);

	dal_write_reg(ctx, addr, value);
}

static void enable_phy_bypass_mode(
	struct dal_context *ctx,
	const int32_t be_addr_offset,
	bool enable)
{
	/* This register resides in DP back end block;
	 * transmitter is used for the offset */

	const uint32_t addr = mmDP_DPHY_CNTL + be_addr_offset;

	uint32_t value = dal_read_reg(ctx, addr);

	set_reg_field_value(value, enable, DP_DPHY_CNTL, DPHY_BYPASS);

	dal_write_reg(ctx, addr, value);
}

static void disable_prbs_symbols(
	struct dal_context *ctx,
	const int32_t be_addr_offset,
	bool disable)
{
	/* This register resides in DP back end block;
	 * transmitter is used for the offset */

	const uint32_t addr = mmDP_DPHY_CNTL + be_addr_offset;

	uint32_t value = dal_read_reg(ctx, addr);

	set_reg_field_value(value, disable,
			DP_DPHY_CNTL, DPHY_ATEST_SEL_LANE0);

	set_reg_field_value(value, disable,
			DP_DPHY_CNTL, DPHY_ATEST_SEL_LANE1);

	set_reg_field_value(value, disable,
			DP_DPHY_CNTL, DPHY_ATEST_SEL_LANE2);

	set_reg_field_value(value, disable,
			DP_DPHY_CNTL, DPHY_ATEST_SEL_LANE3);

	dal_write_reg(ctx, addr, value);
}

static void disable_prbs_mode(
	struct dal_context *ctx,
	const int32_t be_addr_offset)
{
	/* This register resides in DP back end block;
	 * transmitter is used for the offset */

	const uint32_t addr = mmDP_DPHY_PRBS_CNTL + be_addr_offset;
	uint32_t value;

	value = dal_read_reg(ctx, addr);

	set_reg_field_value(value, 0, DP_DPHY_PRBS_CNTL, DPHY_PRBS_EN);

	dal_write_reg(ctx, addr, value);
}

static void program_pattern_symbols(
	struct dal_context *ctx,
	const int32_t be_addr_offset,
	uint16_t pattern_symbols[8])
{
	uint32_t addr;
	uint32_t value;

	/* This register resides in DP back end block;
	 * transmitter is used for the offset */

	addr = mmDP_DPHY_SYM0 + be_addr_offset;

	value = 0;
	set_reg_field_value(value, pattern_symbols[0],
			DP_DPHY_SYM0, DPHY_SYM1);
	set_reg_field_value(value, pattern_symbols[1],
			DP_DPHY_SYM0, DPHY_SYM2);
	set_reg_field_value(value, pattern_symbols[2],
			DP_DPHY_SYM0, DPHY_SYM3);
	dal_write_reg(ctx, addr, value);

	/* This register resides in DP back end block;
	 * transmitter is used for the offset */

	addr = mmDP_DPHY_SYM1 + be_addr_offset;

	value = 0;
	set_reg_field_value(value, pattern_symbols[3],
			DP_DPHY_SYM1, DPHY_SYM4);
	set_reg_field_value(value, pattern_symbols[4],
			DP_DPHY_SYM1, DPHY_SYM5);
	set_reg_field_value(value, pattern_symbols[5],
			DP_DPHY_SYM1, DPHY_SYM6);
	dal_write_reg(ctx, addr, value);

	/* This register resides in DP back end block;
	 * transmitter is used for the offset */
	addr = mmDP_DPHY_SYM2 + be_addr_offset;
	value = 0;
	set_reg_field_value(value, pattern_symbols[6],
			DP_DPHY_SYM2, DPHY_SYM7);
	set_reg_field_value(value, pattern_symbols[6],
			DP_DPHY_SYM2, DPHY_SYM8);

	dal_write_reg(ctx, addr, value);
}

static void set_dp_phy_pattern_training_pattern(
	struct dal_context *ctx,
	const int32_t be_addr_offset,
	uint32_t index)
{
	/* Write Training Pattern */

	dal_write_reg(ctx,
		mmDP_DPHY_TRAINING_PATTERN_SEL + be_addr_offset, index);

	/* Set HW Register Training Complete to false */

	set_link_training_complete(ctx, be_addr_offset, false);

	/* Disable PHY Bypass mode to output Training Pattern */

	enable_phy_bypass_mode(ctx, be_addr_offset, false);

	/* Disable PRBS mode,
	 * make sure DPHY_PRBS_CNTL.DPHY_PRBS_EN=0 */

	disable_prbs_mode(ctx, be_addr_offset);
}

static void set_dp_phy_pattern_d102(
	struct dal_context *ctx,
	const int32_t be_addr_offset)
{
	/* Disable PHY Bypass mode to setup the test pattern */

	enable_phy_bypass_mode(ctx, be_addr_offset, false);

	/* For 10-bit PRBS or debug symbols
	 * please use the following sequence: */

	/* Enable debug symbols on the lanes */

	disable_prbs_symbols(ctx, be_addr_offset, true);

	/* Disable PRBS mode,
	 * make sure DPHY_PRBS_CNTL.DPHY_PRBS_EN=0 */

	disable_prbs_mode(ctx, be_addr_offset);

	/* Program debug symbols to be output */
	{
		uint16_t pattern_symbols[8] = {
			0x2AA, 0x2AA, 0x2AA, 0x2AA,
			0x2AA, 0x2AA, 0x2AA, 0x2AA
		};

		program_pattern_symbols(ctx,
			be_addr_offset, pattern_symbols);
	}

	/* Enable phy bypass mode to enable the test pattern */

	enable_phy_bypass_mode(ctx, be_addr_offset, true);
}

static void set_dp_phy_pattern_symbol_error(
	struct dal_context *ctx,
	const int32_t addr_offset)
{
	/* Disable PHY Bypass mode to setup the test pattern */

	enable_phy_bypass_mode(ctx, addr_offset, false);

	/* ensure ASSR is disabled */

	setup_assr(ctx, addr_offset, DP_ALT_SCRAMBLER_RESET_NONE);

	/* A PRBS23 pattern is used for most DP electrical measurements. */

	/* Enable PRBS symbols on the lanes */

	disable_prbs_symbols(ctx, addr_offset, false);

	/* For PRBS23 Set bit DPHY_PRBS_SEL=1 and Set bit DPHY_PRBS_EN=1 */
	{
		const uint32_t addr = mmDP_DPHY_PRBS_CNTL + addr_offset;
		uint32_t value = dal_read_reg(ctx, addr);

		set_reg_field_value(value, 1,
				DP_DPHY_PRBS_CNTL, DPHY_PRBS_SEL);
		set_reg_field_value(value, 1,
				DP_DPHY_PRBS_CNTL, DPHY_PRBS_EN);
		dal_write_reg(ctx, addr, value);
	}

	/* Enable phy bypass mode to enable the test pattern */

	enable_phy_bypass_mode(ctx, addr_offset, true);
}

static void set_dp_phy_pattern_prbs7(
	struct dal_context *ctx,
	const int32_t addr_offset)
{
	/* Disable PHY Bypass mode to setup the test pattern */

	enable_phy_bypass_mode(ctx, addr_offset, false);

	/* A PRBS7 pattern is used for most DP electrical measurements. */

	/* Enable PRBS symbols on the lanes */

	disable_prbs_symbols(ctx, addr_offset, false);

	/* For PRBS7 Set bit DPHY_PRBS_SEL=0 and Set bit DPHY_PRBS_EN=1 */
	{
		const uint32_t addr = mmDP_DPHY_PRBS_CNTL + addr_offset;

		uint32_t value = dal_read_reg(ctx, addr);

		set_reg_field_value(value, 0,
				DP_DPHY_PRBS_CNTL, DPHY_PRBS_SEL);

		set_reg_field_value(value, 1,
				DP_DPHY_PRBS_CNTL, DPHY_PRBS_EN);

		dal_write_reg(ctx, addr, value);
	}

	/* Enable phy bypass mode to enable the test pattern */

	enable_phy_bypass_mode(ctx, addr_offset, true);
}

static void set_dp_phy_pattern_80bit_custom(
	struct dal_context *ctx,
	const int32_t be_addr_offset,
	const uint8_t *pattern)
{
	/* Disable PHY Bypass mode to setup the test pattern */

	enable_phy_bypass_mode(ctx, be_addr_offset, false);

	/* Enable debug symbols on the lanes */

	disable_prbs_symbols(ctx, be_addr_offset, true);

	/* Enable PHY bypass mode to enable the test pattern */
	/* TODO is it really needed ? */

	enable_phy_bypass_mode(ctx, be_addr_offset, true);

	/* Program 80 bit custom pattern */
	{
		uint16_t pattern_symbols[8];

		pattern_symbols[0] =
			((pattern[1] & 0x03) << 8) | pattern[0];
		pattern_symbols[1] =
			((pattern[2] & 0x0f) << 6) | ((pattern[1] >> 2) & 0x3f);
		pattern_symbols[2] =
			((pattern[3] & 0x3f) << 4) | ((pattern[2] >> 4) & 0x0f);
		pattern_symbols[3] =
			(pattern[4] << 2) | ((pattern[3] >> 6) & 0x03);
		pattern_symbols[4] =
			((pattern[6] & 0x03) << 8) | pattern[5];
		pattern_symbols[5] =
			((pattern[7] & 0x0f) << 6) | ((pattern[6] >> 2) & 0x3f);
		pattern_symbols[6] =
			((pattern[8] & 0x3f) << 4) | ((pattern[7] >> 4) & 0x0f);
		pattern_symbols[7] =
			(pattern[9] << 2) | ((pattern[8] >> 6) & 0x03);

		program_pattern_symbols(ctx,
			be_addr_offset, pattern_symbols);
	}

	/* Enable phy bypass mode to enable the test pattern */

	enable_phy_bypass_mode(ctx, be_addr_offset, true);
}

static void set_dp_phy_pattern_hbr2_compliance(
	struct hw_ctx_digital_encoder_hal *ctx,
	enum transmitter transmitter,
	const struct encoder_set_dp_phy_pattern_param *param)
{
	const int32_t fe_addr_offset = fe_engine_offsets[param->ctx->engine];

	const int32_t be_addr_offset = FROM_HAL(ctx)->be_engine_offset;

	uint32_t addr;
	uint32_t value;

	/* previously there is a register DP_HBR2_EYE_PATTERN
	 * that is enabled to get the pattern.
	 * But it does not work with the latest spec change,
	 * so we are programming the following registers manually.
	 *
	 * The following settings have been confirmed
	 * by Nick Chorney and Sandra Liu */

	/* Disable PHY Bypass mode to setup the test pattern */

	enable_phy_bypass_mode(ctx->base.dal_ctx, be_addr_offset, false);

	/* Setup DIG encoder in DP SST mode */

	setup_encoder(
		ctx, param->ctx->engine, transmitter, DIG_ENCODER_MODE_DP);

	/* ensure ASSR is disabled */

	setup_assr(ctx->base.dal_ctx,
			be_addr_offset, DP_ALT_SCRAMBLER_RESET_NONE);

	/* no vbid after BS (SR)
	 * DP_LINK_FRAMING_CNTL changed history Sandra Liu
	 * 11000260 / 11000104 / 110000FC */

	addr = mmDP_LINK_FRAMING_CNTL + fe_addr_offset;

	value = dal_read_reg(ctx->base.dal_ctx, addr);

	set_reg_field_value(value, 0xFC,
			DP_LINK_FRAMING_CNTL, DP_IDLE_BS_INTERVAL);
	set_reg_field_value(value, 1,
			DP_LINK_FRAMING_CNTL, DP_VBID_DISABLE);
	set_reg_field_value(value, 1,
			DP_LINK_FRAMING_CNTL, DP_VID_ENHANCED_FRAME_MODE);

	dal_write_reg(ctx->base.dal_ctx, addr, value);

	/*TODO add support for this test pattern
	 * support_dp_hbr2_eye_pattern
	 */

	/* set link training complete */
	set_link_training_complete(ctx->base.dal_ctx, be_addr_offset, true);

	/* do not enable video stream */
	addr = mmDP_VID_STREAM_CNTL + be_addr_offset;

	value = dal_read_reg(ctx->base.dal_ctx, addr);

	set_reg_field_value(value, 0, DP_VID_STREAM_CNTL, DP_VID_STREAM_ENABLE);

	dal_write_reg(ctx->base.dal_ctx, addr, value);

	/* Disable PHY Bypass mode to setup the test pattern */

	enable_phy_bypass_mode(ctx->base.dal_ctx, be_addr_offset, false);
}

static void set_dp_phy_pattern_passthrough_mode(
	struct dal_context *ctx,
	const int32_t be_addr_offset,
	const int32_t fe_addr_offset,
	enum dp_alt_scrambler_reset reset)
{
	uint32_t addr;
	uint32_t value;
	/* reprogram ASSR */

	setup_assr(ctx, be_addr_offset, reset);

	/* Disable HBR2 eye pattern
	 * need to revert the following registers
	 * when reverting from HBR2 eye pattern */

	addr = mmDP_LINK_FRAMING_CNTL + fe_addr_offset;
	value = dal_read_reg(ctx, addr);
	set_reg_field_value(value, 0x2000,
			DP_LINK_FRAMING_CNTL, DP_IDLE_BS_INTERVAL);
	set_reg_field_value(value, 0,
			DP_LINK_FRAMING_CNTL, DP_VBID_DISABLE);
	set_reg_field_value(value, 1,
			DP_LINK_FRAMING_CNTL, DP_VID_ENHANCED_FRAME_MODE);
	dal_write_reg(ctx, addr, value);

	/* set link training complete */

	set_link_training_complete(ctx, be_addr_offset, true);

	/* Disable PHY Bypass mode to setup the test pattern */

	enable_phy_bypass_mode(ctx, be_addr_offset, false);

	/* Disable PRBS mode,
	 * make sure DPHY_PRBS_CNTL.DPHY_PRBS_EN=0 */

	disable_prbs_mode(ctx, be_addr_offset);
}

static void set_dp_phy_pattern(
	struct hw_ctx_digital_encoder_hal *ctx,
	enum transmitter transmitter,
	const struct encoder_set_dp_phy_pattern_param *param)
{
	const int32_t offset = FROM_HAL(ctx)->be_engine_offset;

	switch (param->dp_phy_pattern) {
	case DP_TEST_PATTERN_TRAINING_PATTERN1:
		set_dp_phy_pattern_training_pattern(ctx->base.dal_ctx,
			offset, 0);
		break;
	case DP_TEST_PATTERN_TRAINING_PATTERN2:
		set_dp_phy_pattern_training_pattern(ctx->base.dal_ctx,
			offset, 1);
		break;
	case DP_TEST_PATTERN_TRAINING_PATTERN3:
		set_dp_phy_pattern_training_pattern(ctx->base.dal_ctx,
			offset, 2);
		break;
	case DP_TEST_PATTERN_D102:
		set_dp_phy_pattern_d102(ctx->base.dal_ctx, offset);
		break;
	case DP_TEST_PATTERN_SYMBOL_ERROR:
		set_dp_phy_pattern_symbol_error(ctx->base.dal_ctx, offset);
		break;
	case DP_TEST_PATTERN_PRBS7:
		set_dp_phy_pattern_prbs7(ctx->base.dal_ctx, offset);
		break;
	case DP_TEST_PATTERN_80BIT_CUSTOM:
		set_dp_phy_pattern_80bit_custom(
			ctx->base.dal_ctx,
			offset, param->custom_pattern);
		break;
	case DP_TEST_PATTERN_HBR2_COMPLIANCE_EYE:
		set_dp_phy_pattern_hbr2_compliance(
			ctx, transmitter, param);
		break;
	case DP_TEST_PATTERN_VIDEO_MODE:
		set_dp_phy_pattern_passthrough_mode(
			ctx->base.dal_ctx,
			offset, fe_engine_offsets[param->ctx->engine],
			param->alt_scrambler_reset);
		break;
	default:
		/* invalid phy pattern */
		ASSERT_CRITICAL(false);
		break;
	}
}

static bool is_test_pattern_enabled(
	struct hw_ctx_digital_encoder_hal *ctx,
	enum engine_id engine,
	enum transmitter transmitter)
{
	uint32_t value;

	value = dal_read_reg(ctx->base.dal_ctx,
		mmDP_DPHY_CNTL +
		FROM_HAL(ctx)->be_engine_offset);

	if (get_reg_field_value(value, DP_DPHY_CNTL, DPHY_BYPASS))
		return true;

	value = dal_read_reg(ctx->base.dal_ctx,
		mmDP_VID_STREAM_CNTL +
		FROM_HAL(ctx)->be_engine_offset);

	if (!get_reg_field_value(value,
			DP_VID_STREAM_CNTL, DP_VID_STREAM_ENABLE))
		return true;

	return false;
}


static void enable_hpd(
	struct hw_ctx_digital_encoder_hal *ctx,
	enum hpd_source_id hpd_source)
{
	/* enable master clock of HPD if it is not turned on */
	uint32_t value;
	uint32_t field;

	const uint32_t addr = mmHPD0_DC_HPD_CONTROL +
			HWCTX_DIGITAL_ENC110_FROM_BASE(ctx)->hpd_offset;

	value = dal_read_reg(ctx->base.dal_ctx, addr);
	field = get_reg_field_value(value, DC_HPD_CONTROL, DC_HPD_EN);
	if (field == 0) {
		set_reg_field_value(value, 1, DC_HPD_CONTROL, DC_HPD_EN);
		dal_write_reg(ctx->base.dal_ctx, addr, value);
	}
}

static void disable_hpd(
	struct hw_ctx_digital_encoder_hal *ctx,
	enum hpd_source_id hpd_source)
{
	/* disable master clock of HPD.
	 * All interrupt on HPD will be disabled. */

	const uint32_t addr = mmHPD0_DC_HPD_CONTROL +
		FROM_HAL(ctx)->hpd_offset;

	uint32_t value = dal_read_reg(ctx->base.dal_ctx, addr);

	set_reg_field_value(value, 0, DC_HPD_CONTROL, DC_HPD_EN);

	dal_write_reg(ctx->base.dal_ctx, addr, value);
}

static bool is_panel_backlight_on(
	struct hw_ctx_digital_encoder_hal *ctx)
{
	uint32_t value;

	value = dal_read_reg(ctx->base.dal_ctx,
			mmLVTMA_PWRSEQ_CNTL);

	return get_reg_field_value(value, LVTMA_PWRSEQ_CNTL, LVTMA_BLON);
}

static bool is_panel_powered_on(
	struct hw_ctx_digital_encoder_hal *ctx)
{
	uint32_t value;
	bool ret;

	value = dal_read_reg(ctx->base.dal_ctx,
			mmLVTMA_PWRSEQ_STATE);

	ret = get_reg_field_value(value,
			LVTMA_PWRSEQ_STATE, LVTMA_PWRSEQ_TARGET_STATE_R);

	return ret == 1;
}

static bool is_panel_powered_off(
	struct hw_ctx_digital_encoder_hal *ctx)
{
	/* TODO: Is this wrong for dce11 or not needed? */
	uint32_t value;
	bool ret;

	value = dal_read_reg(ctx->base.dal_ctx,
			mmLVTMA_PWRSEQ_CNTL);

	ret = get_reg_field_value(value,
			LVTMA_PWRSEQ_CNTL, LVTMA_PWRSEQ_TARGET_STATE);
	return ret == 0;
}

static bool is_dig_enabled(
	struct hw_ctx_digital_encoder_hal *ctx,
	enum engine_id engine,
	enum transmitter transmitter)
{
	uint32_t value;

	value = dal_read_reg(ctx->base.dal_ctx,
		mmDIG_BE_EN_CNTL +
		FROM_HAL(ctx)->be_engine_offset);

	return get_reg_field_value(value, DIG_BE_EN_CNTL, DIG_ENABLE);
}

static enum clock_source_id get_active_clock_source(
	const struct hw_ctx_digital_encoder_hal *ctx,
	enum transmitter transmitter)
{
	struct dal_context *dal_context = ctx->base.dal_ctx;

	NOT_IMPLEMENTED();
	return CLOCK_SOURCE_ID_UNDEFINED;
}

/**
* set_afmt_memory_power_state
*
* @brief
*  Power up audio formatter memory that is mapped to specified DIG
*/
static void set_afmt_memory_power_state(
	const struct hw_ctx_digital_encoder_hal *ctx,
	enum engine_id id,
	bool enable)
{
	struct dal_context *dal_context = ctx->base.dal_ctx;
	uint32_t value;
	uint32_t mem_pwr_force;

	value = dal_read_reg(dal_context, mmDCO_MEM_PWR_CTRL);

	if (enable)
		mem_pwr_force = 0;
	else
		mem_pwr_force = 3;

	/* force shutdown mode for appropriate AFMT memory */
	switch (id) {
	case ENGINE_ID_DIGA:
		set_reg_field_value(
			value,
			mem_pwr_force,
			DCO_MEM_PWR_CTRL,
			HDMI0_MEM_PWR_FORCE);
		break;
	case ENGINE_ID_DIGB:
		set_reg_field_value(
			value,
			mem_pwr_force,
			DCO_MEM_PWR_CTRL,
			HDMI1_MEM_PWR_FORCE);
		break;
	case ENGINE_ID_DIGC:
		set_reg_field_value(
			value,
			mem_pwr_force,
			DCO_MEM_PWR_CTRL,
			HDMI2_MEM_PWR_FORCE);
		break;
	default:
		dal_logger_write(
			dal_context->logger,
			LOG_MAJOR_WARNING,
			LOG_MINOR_COMPONENT_ENCODER,
			"%s: Invalid Engine Id\n",
			__func__);
		break;
	}

	dal_write_reg(dal_context, mmDCO_MEM_PWR_CTRL, value);
}

static enum engine_id get_active_engine(
	const struct hw_ctx_digital_encoder_hal *ctx,
	enum transmitter transmitter)
{
	struct dal_context *dal_context = ctx->base.dal_ctx;

	NOT_IMPLEMENTED();
	return ENGINE_ID_UNKNOWN;
}

static const struct hw_ctx_digital_encoder_hal_funcs hal_funcs = {
	.set_dp_stream_attributes = set_dp_stream_attributes,
	.set_tmds_stream_attributes = set_tmds_stream_attributes,
	.set_dvo_stream_attributes = set_dvo_stream_attributes,
	.setup_tmds_stereo_sync = setup_tmds_stereo_sync,
	.setup_stereo_sync = setup_stereo_sync,
	.control_stereo_sync = control_stereo_sync,
	.hpd_initialize = hpd_initialize,
	.aux_initialize = aux_initialize,
	.setup_encoder = setup_encoder,
	.disable_encoder = disable_encoder,
	.blank_dp_output = blank_dp_output,
	.unblank_dp_output = unblank_dp_output,
	.setup_vid_stream = setup_vid_stream,
	.configure_encoder = configure_encoder,
	.enable_dvo_sync_output = enable_dvo_sync_output,
	.disable_dvo_sync_output = disable_dvo_sync_output,
	.update_info_packets = update_info_packets,
	.stop_info_packets = stop_info_packets,
	.update_avi_info_packet = update_avi_info_packet,
	.update_hdmi_info_packet = update_hdmi_info_packet,
	.update_dp_info_packet = update_dp_info_packet,
	.setup_hdmi = setup_hdmi,
	.set_lcd_backlight_level = set_lcd_backlight_level,
	.backlight_control = backlight_control,
	.enable_mvpu_downstream = enable_mvpu_downstream,
	.disable_mvpu_downstream = disable_mvpu_downstream,
	.update_mst_stream_allocation_table =
			update_mst_stream_allocation_table,
	.set_mst_bandwidth = set_mst_bandwidth,
	.set_dp_phy_pattern = set_dp_phy_pattern,
	.is_test_pattern_enabled = is_test_pattern_enabled,
	.enable_hpd = enable_hpd,
	.disable_hpd = disable_hpd,
	.is_panel_backlight_on = is_panel_backlight_on,
	.is_panel_powered_on = is_panel_powered_on,
	.is_panel_powered_off = is_panel_powered_off,
	.is_dig_enabled = is_dig_enabled,
	.get_active_clock_source = get_active_clock_source,
	.get_active_engine = get_active_engine,
	.get_dp_downstream_port_type =
		dal_hw_ctx_digital_encoder_hal_get_dp_downstream_port_type,
	.get_dp_sink_count = dal_hw_ctx_digital_encoder_hal_get_dp_sink_count,
	.dp_receiver_power_up =
		dal_hw_ctx_digital_encoder_hal_dp_receiver_power_up,
	.dp_receiver_power_down =
		dal_hw_ctx_digital_encoder_hal_dp_receiver_power_down,
	.get_lane_settings = get_lane_settings,
	.is_single_pll_mode = dal_hw_ctx_digital_encoder_hal_is_single_pll_mode,
	.get_link_cap = dal_hw_ctx_digital_encoder_hal_get_link_cap,
	.set_afmt_memory_power_state = set_afmt_memory_power_state
};

static const struct hw_ctx_digital_encoder_funcs func = {
		.destroy = destroy,
		.initialize =
			dal_encoder_hw_ctx_digital_encoder_initialize,
		.submit_command =
			dal_encoder_hw_ctx_digital_encoder_submit_command,
		.dpcd_read_register =
			dal_encoder_hw_ctx_digital_encoder_dpcd_read_register,
		.dpcd_write_register =
			dal_encoder_hw_ctx_digital_encoder_dpcd_write_register,
		.dpcd_read_registers =
			dal_encoder_hw_ctx_digital_encoder_dpcd_read_registers,
		.dpcd_write_registers =
			dal_encoder_hw_ctx_digital_encoder_dpcd_write_registers,
};

bool dal_hw_ctx_digital_encoder_dce110_construct(
	struct hw_ctx_digital_encoder_dce110 *ctx110,
	const struct hw_ctx_init *init_data)
{
	struct hw_ctx_digital_encoder_hal *base = &ctx110->base;

	if (!dal_hw_ctx_digital_encoder_hal_construct(base,
			init_data->dal_ctx)) {
		return false;
	}

	base->base.funcs = &func;
	base->funcs = &hal_funcs;

	switch (init_data->transmitter) {
	case TRANSMITTER_UNIPHY_A:
		ctx110->transmitter_offset = 0;
		ctx110->be_engine_offset = 0;
		break;
	case TRANSMITTER_UNIPHY_B:
		ctx110->transmitter_offset =
			mmBPHYC_UNIPHY1_UNIPHY_TX_CONTROL1 -
			mmBPHYC_UNIPHY0_UNIPHY_TX_CONTROL1;
		ctx110->be_engine_offset =
			mmDIG1_DIG_BE_CNTL - mmDIG0_DIG_BE_CNTL;
		break;
	case TRANSMITTER_UNIPHY_C:
		ctx110->transmitter_offset =
			mmBPHYC_UNIPHY2_UNIPHY_TX_CONTROL1 -
			mmBPHYC_UNIPHY0_UNIPHY_TX_CONTROL1;
		ctx110->be_engine_offset =
			mmDIG2_DIG_BE_CNTL - mmDIG0_DIG_BE_CNTL;
		break;
	default:
		/* check BIOS object table ! */
		/* we may be called from DCE8.1 HW CTX, for UNIPHY-G
		 * ASSERT_CRITICAL(false);*/
		ctx110->transmitter_offset = 0;
		ctx110->be_engine_offset = 0;
		break;
	}

	switch (init_data->hpd_source) {
	case HPD_SOURCEID1:
		ctx110->hpd_offset = 0;
		break;
	case HPD_SOURCEID2:
		ctx110->hpd_offset =
			mmHPD1_DC_HPD_INT_STATUS - mmHPD0_DC_HPD_INT_STATUS;
		break;
	case HPD_SOURCEID3:
		ctx110->hpd_offset =
			mmHPD2_DC_HPD_INT_STATUS - mmHPD0_DC_HPD_INT_STATUS;
		break;
	default:
		/* check BIOS object table ! */
		ctx110->hpd_offset = 0;
		break;
	}

	switch (init_data->channel) {
	case CHANNEL_ID_DDC1:
		ctx110->aux_channel_offset = 0;
		ctx110->channel_offset = 0;
		break;
	case CHANNEL_ID_DDC2:
		ctx110->aux_channel_offset =
			mmDP_AUX1_AUX_CONTROL - mmDP_AUX0_AUX_CONTROL;
		ctx110->channel_offset =
			mmDC_GPIO_DDC2_EN - mmDC_GPIO_DDC1_EN;
		break;
	case CHANNEL_ID_DDC3:
		ctx110->aux_channel_offset =
			mmDP_AUX2_AUX_CONTROL - mmDP_AUX0_AUX_CONTROL;
		ctx110->channel_offset =
			mmDC_GPIO_DDC3_EN - mmDC_GPIO_DDC1_EN;
		break;
	default:
		/* check BIOS object table ! */
		dal_logger_write(init_data->dal_ctx->logger,
				LOG_MAJOR_WARNING,
				LOG_MINOR_COMPONENT_ENCODER,
				"%s: Invalid channel ID\n",
				__func__);
		ctx110->aux_channel_offset = 0;
		ctx110->channel_offset = 0;
	}

	return true;
}

struct hw_ctx_digital_encoder_hal *
	dal_hw_ctx_digital_encoder_dce110_create(
		const struct hw_ctx_init *init)
{
	struct hw_ctx_digital_encoder_dce110 *ctx110 =
		dal_alloc(sizeof(struct hw_ctx_digital_encoder_dce110));

	if (!ctx110)
		return NULL;

	if (dal_hw_ctx_digital_encoder_dce110_construct(ctx110, init))
		return &ctx110->base;

	dal_free(ctx110);

	return NULL;
}
