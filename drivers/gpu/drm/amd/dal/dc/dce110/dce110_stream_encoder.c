/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 *  and/or sell copies of the Software, and to permit persons to whom the
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
#include "stream_encoder_types.h"
#include "dce110_stream_encoder.h"
#include "dce/dce_11_0_d.h"
#include "dce/dce_11_0_sh_mask.h"
#include "dce/dce_11_0_enum.h"

static const uint32_t fe_engine_offsets[] = {
	mmDIG0_DIG_FE_CNTL - mmDIG0_DIG_FE_CNTL,
	mmDIG1_DIG_FE_CNTL - mmDIG0_DIG_FE_CNTL,
	mmDIG2_DIG_FE_CNTL - mmDIG0_DIG_FE_CNTL,
};

#define VBI_LINE_0 0
#define DP_BLANK_MAX_RETRY 20
#define HDMI_CLOCK_CHANNEL_RATE_MORE_340M 340000

#ifndef HDMI_CONTROL__HDMI_DATA_SCRAMBLE_EN_MASK
	#define HDMI_CONTROL__HDMI_DATA_SCRAMBLE_EN_MASK 0x2
	#define HDMI_CONTROL__HDMI_DATA_SCRAMBLE_EN__SHIFT 0x1
#endif

static void construct(
	struct stream_encoder *enc,
	struct stream_enc_init_data *init)
{
	enc->ctx = init->ctx;
	enc->id = init->stream_engine_id;
	enc->adapter_service = init->adapter_service;
}

static void stop_hdmi_info_packets(struct dc_context *ctx, uint32_t offset)
{
	uint32_t addr = 0;
	uint32_t value = 0;

	/* stop generic packets 0 & 1 on HDMI */
	addr = mmHDMI_GENERIC_PACKET_CONTROL0 + offset;

	value = dal_read_reg(ctx, addr);

	set_reg_field_value(
		value,
		0,
		HDMI_GENERIC_PACKET_CONTROL0,
		HDMI_GENERIC1_CONT);
	set_reg_field_value(
		value,
		0,
		HDMI_GENERIC_PACKET_CONTROL0,
		HDMI_GENERIC1_LINE);
	set_reg_field_value(
		value,
		0,
		HDMI_GENERIC_PACKET_CONTROL0,
		HDMI_GENERIC1_SEND);
	set_reg_field_value(
		value,
		0,
		HDMI_GENERIC_PACKET_CONTROL0,
		HDMI_GENERIC0_CONT);
	set_reg_field_value(
		value,
		0,
		HDMI_GENERIC_PACKET_CONTROL0,
		HDMI_GENERIC0_LINE);
	set_reg_field_value(
		value,
		0,
		HDMI_GENERIC_PACKET_CONTROL0,
		HDMI_GENERIC0_SEND);

	dal_write_reg(ctx, addr, value);

	/* stop generic packets 2 & 3 on HDMI */
	addr = mmHDMI_GENERIC_PACKET_CONTROL1 + offset;

	value = dal_read_reg(ctx, addr);

	set_reg_field_value(
		value,
		0,
		HDMI_GENERIC_PACKET_CONTROL1,
		HDMI_GENERIC2_CONT);
	set_reg_field_value(
		value,
		0,
		HDMI_GENERIC_PACKET_CONTROL1,
		HDMI_GENERIC2_LINE);
	set_reg_field_value(
		value,
		0,
		HDMI_GENERIC_PACKET_CONTROL1,
		HDMI_GENERIC2_SEND);
	set_reg_field_value(
		value,
		0,
		HDMI_GENERIC_PACKET_CONTROL1,
		HDMI_GENERIC3_CONT);
	set_reg_field_value(
		value,
		0,
		HDMI_GENERIC_PACKET_CONTROL1,
		HDMI_GENERIC3_LINE);
	set_reg_field_value(
		value,
		0,
		HDMI_GENERIC_PACKET_CONTROL1,
		HDMI_GENERIC3_SEND);

	dal_write_reg(ctx, addr, value);

	/* stop AVI packet on HDMI */
	addr = mmHDMI_INFOFRAME_CONTROL0 + offset;

	value = dal_read_reg(ctx, addr);

	set_reg_field_value(
		value,
		0,
		HDMI_INFOFRAME_CONTROL0,
		HDMI_AVI_INFO_SEND);
	set_reg_field_value(
		value,
		0,
		HDMI_INFOFRAME_CONTROL0,
		HDMI_AVI_INFO_CONT);

	dal_write_reg(ctx, addr, value);
}

static void stop_dp_info_packets(struct dc_context *ctx, int32_t offset)
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
		set_reg_field_value(
			value,
			1,
			DP_SEC_CNTL,
			DP_SEC_STREAM_ENABLE);

	dal_write_reg(ctx, addr, value);
}

static void update_avi_info_packet(
	struct stream_encoder *enc,
	enum engine_id engine,
	enum signal_type signal,
	const struct encoder_info_packet *info_packet)
{
	const int32_t offset = fe_engine_offsets[engine];

	uint32_t regval;
	uint32_t addr;

	if (info_packet->valid) {
		const uint32_t *content =
			(const uint32_t *) &info_packet->sb[0];

		{
			regval = content[0];

			dal_write_reg(
				enc->ctx,
				mmAFMT_AVI_INFO0 + offset,
				regval);
		}
		{
			regval = content[1];

			dal_write_reg(
				enc->ctx,
				mmAFMT_AVI_INFO1 + offset,
				regval);
		}
		{
			regval = content[2];

			dal_write_reg(
				enc->ctx,
				mmAFMT_AVI_INFO2 + offset,
				regval);
		}
		{
			regval = content[3];

			/* move version to AVI_INFO3 */
			set_reg_field_value(
				regval,
				info_packet->hb1,
				AFMT_AVI_INFO3,
				AFMT_AVI_INFO_VERSION);

			dal_write_reg(
				enc->ctx,
				mmAFMT_AVI_INFO3 + offset,
				regval);
		}

		if (dc_is_hdmi_signal(signal)) {

			uint32_t control0val;
			uint32_t control1val;

			addr = mmHDMI_INFOFRAME_CONTROL0 + offset;

			control0val = dal_read_reg(enc->ctx, addr);

			set_reg_field_value(
				control0val,
				1,
				HDMI_INFOFRAME_CONTROL0,
				HDMI_AVI_INFO_SEND);

			set_reg_field_value(
				control0val,
				1,
				HDMI_INFOFRAME_CONTROL0,
				HDMI_AVI_INFO_CONT);

			dal_write_reg(enc->ctx, addr, control0val);

			addr = mmHDMI_INFOFRAME_CONTROL1 + offset;

			control1val = dal_read_reg(enc->ctx, addr);

			set_reg_field_value(
				control1val,
				VBI_LINE_0 + 2,
				HDMI_INFOFRAME_CONTROL1,
				HDMI_AVI_INFO_LINE);

			dal_write_reg(enc->ctx, addr, control1val);
		}
	} else if (dc_is_hdmi_signal(signal)) {
		addr = mmHDMI_INFOFRAME_CONTROL0 + offset;

		regval = dal_read_reg(enc->ctx, addr);

		set_reg_field_value(
			regval,
			0,
			HDMI_INFOFRAME_CONTROL0,
			HDMI_AVI_INFO_SEND);

		set_reg_field_value(
			regval,
			0,
			HDMI_INFOFRAME_CONTROL0,
			HDMI_AVI_INFO_CONT);

		dal_write_reg(enc->ctx, addr, regval);
	}
}

static void update_generic_info_packet(
	struct stream_encoder *enc,
	enum engine_id engine,
	uint32_t packet_index,
	const struct encoder_info_packet *info_packet)
{
	uint32_t addr;
	uint32_t regval;
	/* choose which generic packet to use */
	{
		addr = mmAFMT_VBI_PACKET_CONTROL + fe_engine_offsets[engine];

		regval = dal_read_reg(enc->ctx, addr);

		set_reg_field_value(
			regval,
			packet_index,
			AFMT_VBI_PACKET_CONTROL,
			AFMT_GENERIC_INDEX);

		dal_write_reg(enc->ctx, addr, regval);
	}

	/* write generic packet header
	 * (4th byte is for GENERIC0 only) */
	{
		addr = mmAFMT_GENERIC_HDR + fe_engine_offsets[engine];

		regval = 0;

		set_reg_field_value(
			regval,
			info_packet->hb0,
			AFMT_GENERIC_HDR,
			AFMT_GENERIC_HB0);

		set_reg_field_value(
			regval,
			info_packet->hb1,
			AFMT_GENERIC_HDR,
			AFMT_GENERIC_HB1);

		set_reg_field_value(
			regval,
			info_packet->hb2,
			AFMT_GENERIC_HDR,
			AFMT_GENERIC_HB2);

		set_reg_field_value(
			regval,
			info_packet->hb3,
			AFMT_GENERIC_HDR,
			AFMT_GENERIC_HB3);

		dal_write_reg(enc->ctx, addr, regval);
	}

	/* write generic packet contents
	 * (we never use last 4 bytes)
	 * there are 8 (0-7) mmDIG0_AFMT_GENERIC0_x registers */
	{
		const uint32_t *content =
			(const uint32_t *) &info_packet->sb[0];

		uint32_t counter = 0;

		addr = mmAFMT_GENERIC_0 + fe_engine_offsets[engine];

		do {
			dal_write_reg(enc->ctx, addr++, *content++);

			++counter;
		} while (counter < 7);
	}

	dal_write_reg(
		enc->ctx,
		mmAFMT_GENERIC_7 + fe_engine_offsets[engine],
		0);

	/* force double-buffered packet update */
	{
		addr = mmAFMT_VBI_PACKET_CONTROL + fe_engine_offsets[engine];

		regval = dal_read_reg(enc->ctx, addr);

		set_reg_field_value(
			regval,
			(packet_index == 0),
			AFMT_VBI_PACKET_CONTROL,
			AFMT_GENERIC0_UPDATE);

		set_reg_field_value(
			regval,
			(packet_index == 2),
			AFMT_VBI_PACKET_CONTROL,
			AFMT_GENERIC2_UPDATE);

		dal_write_reg(enc->ctx, addr, regval);
	}
}

static void update_hdmi_info_packet(
	struct stream_encoder *enc,
	enum engine_id engine,
	uint32_t packet_index,
	const struct encoder_info_packet *info_packet)
{
	uint32_t cont, send, line;
	uint32_t addr = fe_engine_offsets[engine];
	uint32_t regval;

	if (info_packet->valid) {
		update_generic_info_packet(
			enc,
			engine,
			packet_index,
			info_packet);

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
		dal_logger_write(
			enc->ctx->logger,
			LOG_MAJOR_WARNING,
			LOG_MINOR_COMPONENT_ENCODER,
			"Invalid HW packet index: %s()\n",
			__func__);
		break;
	}

	regval = dal_read_reg(enc->ctx, addr);

	switch (packet_index) {
	case 0:
	case 2:
		set_reg_field_value(
			regval,
			cont,
			HDMI_GENERIC_PACKET_CONTROL0,
			HDMI_GENERIC0_CONT);
		set_reg_field_value(
			regval,
			send,
			HDMI_GENERIC_PACKET_CONTROL0,
			HDMI_GENERIC0_SEND);
		set_reg_field_value(
			regval,
			line,
			HDMI_GENERIC_PACKET_CONTROL0,
			HDMI_GENERIC0_LINE);
		break;
	case 1:
	case 3:
		set_reg_field_value(
			regval,
			cont,
			HDMI_GENERIC_PACKET_CONTROL0,
			HDMI_GENERIC1_CONT);
		set_reg_field_value(
			regval,
			send,
			HDMI_GENERIC_PACKET_CONTROL0,
			HDMI_GENERIC1_SEND);
		set_reg_field_value(
			regval,
			line,
			HDMI_GENERIC_PACKET_CONTROL0,
			HDMI_GENERIC1_LINE);
		break;
	default:
		/* invalid HW packet index */
		dal_logger_write(
			enc->ctx->logger,
			LOG_MAJOR_WARNING,
			LOG_MINOR_COMPONENT_ENCODER,
			"Invalid HW packet index: %s()\n",
			__func__);
		break;
	}

	dal_write_reg(enc->ctx, addr, regval);
}

static void update_dp_info_packet(
	struct stream_encoder *enc,
	enum engine_id engine,
	uint32_t packet_index,
	const struct encoder_info_packet *info_packet)
{
	const uint32_t addr = mmDP_SEC_CNTL + fe_engine_offsets[engine];

	uint32_t value;

	if (info_packet->valid)
		update_generic_info_packet(
			enc,
			engine,
			packet_index,
			info_packet);

	/* enable/disable transmission of packet(s).
	 * If enabled, packet transmission begins on the next frame */

	value = dal_read_reg(enc->ctx, addr);

	switch (packet_index) {
	case 0:
		set_reg_field_value(
			value,
			info_packet->valid,
			DP_SEC_CNTL,
			DP_SEC_GSP0_ENABLE);
		break;
	case 1:
		set_reg_field_value(
			value,
			info_packet->valid,
			DP_SEC_CNTL,
			DP_SEC_GSP1_ENABLE);
		break;
	case 2:
		set_reg_field_value(
			value,
			info_packet->valid,
			DP_SEC_CNTL,
			DP_SEC_GSP2_ENABLE);
		break;
	case 3:
		set_reg_field_value(
			value,
			info_packet->valid,
			DP_SEC_CNTL,
			DP_SEC_GSP3_ENABLE);
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
		set_reg_field_value(
			value,
			1,
			DP_SEC_CNTL,
			DP_SEC_STREAM_ENABLE);

	dal_write_reg(enc->ctx, addr, value);
}

static void dp_steer_fifo_reset(
	struct dc_context *ctx,
	enum engine_id engine,
	bool reset)
{
	const uint32_t addr = mmDP_STEER_FIFO + fe_engine_offsets[engine];

	uint32_t value = dal_read_reg(ctx, addr);

	set_reg_field_value(value, reset, DP_STEER_FIFO, DP_STEER_FIFO_RESET);

	dal_write_reg(ctx, addr, value);
}

static void unblank_dp_output(
	struct stream_encoder *enc,
	enum engine_id engine)
{
	uint32_t addr;
	uint32_t value;

	/* set DIG_START to 0x1 to resync FIFO */
	addr = mmDIG_FE_CNTL + fe_engine_offsets[engine];
	value = dal_read_reg(enc->ctx, addr);
	set_reg_field_value(value, 1, DIG_FE_CNTL, DIG_START);
	dal_write_reg(enc->ctx, addr, value);

	/* switch DP encoder to CRTC data */
	dp_steer_fifo_reset(enc->ctx, engine, false);

	/* wait 100us for DIG/DP logic to prime
	 * (i.e. a few video lines) */
	dc_service_delay_in_microseconds(enc->ctx, 100);

	/* the hardware would start sending video at the start of the next DP
	 * frame (i.e. rising edge of the vblank).
	 * NOTE: We used to program DP_VID_STREAM_DIS_DEFER = 2 here, but this
	 * register has no effect on enable transition! HW always guarantees
	 * VID_STREAM enable at start of next frame, and this is not
	 * programmable */
	addr = mmDP_VID_STREAM_CNTL + fe_engine_offsets[engine];
	value = dal_read_reg(enc->ctx, addr);
	set_reg_field_value(
		value,
		true,
		DP_VID_STREAM_CNTL,
		DP_VID_STREAM_ENABLE);
	dal_write_reg(enc->ctx, addr, value);

}

static void setup_vid_stream(
	struct stream_encoder *enc,
	enum engine_id engine,
	uint32_t m_vid,
	uint32_t n_vid)
{
	uint32_t addr;
	uint32_t value;

	/* enable auto measurement */
	addr = mmDP_VID_TIMING + fe_engine_offsets[engine];
	value = dal_read_reg(enc->ctx, addr);
	set_reg_field_value(value, 0, DP_VID_TIMING, DP_VID_M_N_GEN_EN);
	dal_write_reg(enc->ctx, addr, value);

	/* auto measurement need 1 full 0x8000 symbol cycle to kick in,
	 * therefore program initial value for Mvid and Nvid */
	addr = mmDP_VID_N + fe_engine_offsets[engine];
	value = dal_read_reg(enc->ctx, addr);
	set_reg_field_value(value, n_vid, DP_VID_N, DP_VID_N);
	dal_write_reg(enc->ctx, addr, value);

	addr = mmDP_VID_M + fe_engine_offsets[engine];
	value = dal_read_reg(enc->ctx, addr);
	set_reg_field_value(value, m_vid, DP_VID_M, DP_VID_M);
	dal_write_reg(enc->ctx, addr, value);

	addr = mmDP_VID_TIMING + fe_engine_offsets[engine];
	value = dal_read_reg(enc->ctx, addr);
	set_reg_field_value(value, 1, DP_VID_TIMING, DP_VID_M_N_GEN_EN);
	dal_write_reg(enc->ctx, addr, value);
}

static void set_dp_stream_attributes(
	struct stream_encoder *enc,
	enum engine_id engine,
	const struct dc_crtc_timing *timing)
{
	const uint32_t addr = mmDP_PIXEL_FORMAT + fe_engine_offsets[engine];
	uint32_t value = dal_read_reg(enc->ctx, addr);

	/* set pixel encoding */
	switch (timing->pixel_encoding) {
	case PIXEL_ENCODING_YCBCR422:
		set_reg_field_value(
			value,
			DP_PIXEL_ENCODING_YCBCR422,
			DP_PIXEL_FORMAT,
			DP_PIXEL_ENCODING);
		break;
	case PIXEL_ENCODING_YCBCR444:
		set_reg_field_value(
			value,
			DP_PIXEL_ENCODING_YCBCR444,
			DP_PIXEL_FORMAT,
			DP_PIXEL_ENCODING);

		if (timing->flags.Y_ONLY)
			if (timing->display_color_depth != COLOR_DEPTH_666)
				/* HW testing only, no use case yet.
				 * Color depth of Y-only could be
				 * 8, 10, 12, 16 bits */
				set_reg_field_value(
					value,
					DP_PIXEL_ENCODING_Y_ONLY,
					DP_PIXEL_FORMAT,
					DP_PIXEL_ENCODING);
		/* Note: DP_MSA_MISC1 bit 7 is the indicator
		 * of Y-only mode.
		 * This bit is set in HW if register
		 * DP_PIXEL_ENCODING is programmed to 0x4 */
		break;
	default:
		set_reg_field_value(
			value,
			DP_PIXEL_ENCODING_RGB444,
			DP_PIXEL_FORMAT,
			DP_PIXEL_ENCODING);
		break;
	}

	/* set color depth */

	switch (timing->display_color_depth) {
	case COLOR_DEPTH_888:
		set_reg_field_value(
			value,
			DP_COMPONENT_DEPTH_8BPC,
			DP_PIXEL_FORMAT,
			DP_COMPONENT_DEPTH);
		break;
	case COLOR_DEPTH_101010:
		set_reg_field_value(
			value,
			DP_COMPONENT_DEPTH_10BPC,
			DP_PIXEL_FORMAT,
			DP_COMPONENT_DEPTH);
		break;
	case COLOR_DEPTH_121212:
		set_reg_field_value(
			value,
			DP_COMPONENT_DEPTH_12BPC,
			DP_PIXEL_FORMAT,
			DP_COMPONENT_DEPTH);
		break;
	default:
		set_reg_field_value(
			value,
			DP_COMPONENT_DEPTH_6BPC,
			DP_PIXEL_FORMAT,
			DP_COMPONENT_DEPTH);
		break;
	}

	/* set dynamic range and YCbCr range */
	set_reg_field_value(value, 0, DP_PIXEL_FORMAT, DP_DYN_RANGE);
	set_reg_field_value(value, 0, DP_PIXEL_FORMAT, DP_YCBCR_RANGE);

	dal_write_reg(enc->ctx, addr, value);
}

static void setup_hdmi(
	struct stream_encoder *enc,
	enum engine_id engine,
	const struct dc_crtc_timing *timing)
{
	uint32_t output_pixel_clock = timing->pix_clk_khz;
	uint32_t value;
	uint32_t addr;

	addr = mmHDMI_CONTROL + fe_engine_offsets[engine];
	value = dal_read_reg(enc->ctx, addr);
	set_reg_field_value(value, 1, HDMI_CONTROL, HDMI_PACKET_GEN_VERSION);
	set_reg_field_value(value, 1, HDMI_CONTROL, HDMI_KEEPOUT_MODE);
	set_reg_field_value(value, 0, HDMI_CONTROL, HDMI_DEEP_COLOR_ENABLE);
	set_reg_field_value(value, 0, HDMI_CONTROL, HDMI_DATA_SCRAMBLE_EN);
	set_reg_field_value(value, 0, HDMI_CONTROL, HDMI_CLOCK_CHANNEL_RATE);

	switch (timing->display_color_depth) {
	case COLOR_DEPTH_888:
		set_reg_field_value(
			value,
			0,
			HDMI_CONTROL,
			HDMI_DEEP_COLOR_DEPTH);
		break;
	case COLOR_DEPTH_101010:
		set_reg_field_value(
			value,
			1,
			HDMI_CONTROL,
			HDMI_DEEP_COLOR_DEPTH);
		set_reg_field_value(
			value,
			1,
			HDMI_CONTROL,
			HDMI_DEEP_COLOR_ENABLE);
		output_pixel_clock = (timing->pix_clk_khz * 30) / 24;
		break;
	case COLOR_DEPTH_121212:
		set_reg_field_value(
			value,
			2,
			HDMI_CONTROL,
			HDMI_DEEP_COLOR_DEPTH);
		set_reg_field_value(
			value,
			1,
			HDMI_CONTROL,
			HDMI_DEEP_COLOR_ENABLE);
		output_pixel_clock = (timing->pix_clk_khz * 36) / 24;
		break;
	case COLOR_DEPTH_161616:
		set_reg_field_value(
			value,
			3,
			HDMI_CONTROL,
			HDMI_DEEP_COLOR_DEPTH);
		set_reg_field_value(
			value,
			1,
			HDMI_CONTROL,
			HDMI_DEEP_COLOR_ENABLE);
		output_pixel_clock = (timing->pix_clk_khz * 48) / 24;
		break;
	default:
		break;
	}

	if (output_pixel_clock >= HDMI_CLOCK_CHANNEL_RATE_MORE_340M) {
		/* enable HDMI data scrambler */
		set_reg_field_value(
			value,
			1,
			HDMI_CONTROL,
			HDMI_DATA_SCRAMBLE_EN);

		/* HDMI_CLOCK_CHANNEL_RATE_MORE_340M
		 * Clock channel frequency is 1/4 of character rate.*/
		set_reg_field_value(
			value,
			1,
			HDMI_CONTROL,
			HDMI_CLOCK_CHANNEL_RATE);
	} else if (timing->flags.LTE_340MCSC_SCRAMBLE) {

		/* TODO: New feature for DCE11, still need to implement */

		/* enable HDMI data scrambler */
		set_reg_field_value(
			value,
			1,
			HDMI_CONTROL,
			HDMI_DATA_SCRAMBLE_EN);

		/* HDMI_CLOCK_CHANNEL_FREQ_EQUAL_TO_CHAR_RATE
		 * Clock channel frequency is the same
		 * as character rate */
		set_reg_field_value(
			value,
			0,
			HDMI_CONTROL,
			HDMI_CLOCK_CHANNEL_RATE);
	}

	dal_write_reg(enc->ctx, addr, value);

	addr = mmHDMI_VBI_PACKET_CONTROL + fe_engine_offsets[engine];
	value = dal_read_reg(enc->ctx, addr);
	set_reg_field_value(value, 1, HDMI_VBI_PACKET_CONTROL, HDMI_GC_CONT);
	set_reg_field_value(value, 1, HDMI_VBI_PACKET_CONTROL, HDMI_GC_SEND);
	set_reg_field_value(value, 1, HDMI_VBI_PACKET_CONTROL, HDMI_NULL_SEND);

	dal_write_reg(enc->ctx, addr, value);

	/* following belongs to audio */
	addr = mmHDMI_INFOFRAME_CONTROL0 + fe_engine_offsets[engine];
	value = dal_read_reg(enc->ctx, addr);
	set_reg_field_value(
		value,
		1,
		HDMI_INFOFRAME_CONTROL0,
		HDMI_AUDIO_INFO_SEND);
	dal_write_reg(enc->ctx, addr, value);

	addr = mmAFMT_INFOFRAME_CONTROL0 + fe_engine_offsets[engine];
	value = dal_read_reg(enc->ctx, addr);
	set_reg_field_value(
		value,
		1,
		AFMT_INFOFRAME_CONTROL0,
		AFMT_AUDIO_INFO_UPDATE);
	dal_write_reg(enc->ctx, addr, value);

	addr = mmHDMI_INFOFRAME_CONTROL1 + fe_engine_offsets[engine];
	value = dal_read_reg(enc->ctx, addr);
	set_reg_field_value(
		value,
		VBI_LINE_0 + 2,
		HDMI_INFOFRAME_CONTROL1,
		HDMI_AUDIO_INFO_LINE);
	dal_write_reg(enc->ctx, addr, value);

	addr = mmHDMI_GC + fe_engine_offsets[engine];
	value = dal_read_reg(enc->ctx, addr);
	set_reg_field_value(value, 0, HDMI_GC, HDMI_GC_AVMUTE);
	dal_write_reg(enc->ctx, addr, value);

}

static void set_tmds_stream_attributes(
	struct stream_encoder *enc,
	enum engine_id engine,
	enum signal_type signal,
	const struct dc_crtc_timing *timing)
{
	uint32_t addr = mmDIG_FE_CNTL + fe_engine_offsets[engine];
	uint32_t value = dal_read_reg(enc->ctx, addr);

	switch (timing->pixel_encoding) {
	case PIXEL_ENCODING_YCBCR422:
		set_reg_field_value(value, 1, DIG_FE_CNTL, TMDS_PIXEL_ENCODING);
		break;
	default:
		set_reg_field_value(value, 0, DIG_FE_CNTL, TMDS_PIXEL_ENCODING);
		break;
	}

	switch (timing->pixel_encoding) {
	case COLOR_DEPTH_101010:
		if ((signal == SIGNAL_TYPE_DVI_SINGLE_LINK
			|| signal == SIGNAL_TYPE_DVI_DUAL_LINK)
			&& timing->pixel_encoding == PIXEL_ENCODING_RGB)
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
	dal_write_reg(enc->ctx, addr, value);
}

struct stream_encoder *dce110_stream_encoder_create(
	struct stream_enc_init_data *init)
{
	struct stream_encoder *enc =
		dc_service_alloc(init->ctx, sizeof(struct stream_encoder));

	if (!enc)
		goto enc_create_fail;

	construct(enc, init);

	return enc;

enc_create_fail:
	return NULL;
}

void dce110_stream_encoder_destroy(struct stream_encoder **enc)
{
	dc_service_free((*enc)->ctx, *enc);
	*enc = NULL;
}

/*
 * @brief
 * Associate digital encoder with specified output transmitter
 * and configure to output signal.
 * Encoder will be activated later in enable_output()
 */
enum encoder_result dce110_stream_encoder_setup(
	struct stream_encoder *enc,
	struct dc_crtc_timing *crtc_timing,
	enum signal_type signal,
	bool enable_audio)
{
	if (!dc_is_dp_signal(signal)) {
		struct bp_encoder_control cntl;

		cntl.action = ENCODER_CONTROL_SETUP;
		cntl.engine_id = enc->id;
		cntl.signal = signal;
		cntl.enable_dp_audio = enable_audio;
		cntl.pixel_clock = crtc_timing->pix_clk_khz;
		cntl.lanes_number = (signal == SIGNAL_TYPE_DVI_DUAL_LINK) ?
					LANE_COUNT_EIGHT : LANE_COUNT_FOUR;
		cntl.color_depth = crtc_timing->display_color_depth;

		if (dal_bios_parser_encoder_control(
			dal_adapter_service_get_bios_parser(
				enc->adapter_service),
			&cntl) != BP_RESULT_OK)
			return ENCODER_RESULT_ERROR;
	}

	switch (signal) {
	case SIGNAL_TYPE_DVI_SINGLE_LINK:
	case SIGNAL_TYPE_DVI_DUAL_LINK:
		/* set signal format */
		set_tmds_stream_attributes(
			enc, enc->id, signal,
			crtc_timing);
		break;
	case SIGNAL_TYPE_HDMI_TYPE_A:
		/* set signal format */
		set_tmds_stream_attributes(
			enc, enc->id, signal,
			crtc_timing);
		/* setup HDMI engine */
		setup_hdmi(
			enc, enc->id, crtc_timing);
		break;
	case SIGNAL_TYPE_DISPLAY_PORT:
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
	case SIGNAL_TYPE_EDP:
		/* set signal format */
		set_dp_stream_attributes(enc, enc->id, crtc_timing);
		break;
	default:
		break;
	}

	return ENCODER_RESULT_OK;
}

void dce110_stream_encoder_update_info_packets(
	struct stream_encoder *enc,
	enum signal_type signal,
	const struct encoder_info_frame *info_frame)
{
	if (dc_is_hdmi_signal(signal)) {
		update_avi_info_packet(
			enc,
			enc->id,
			signal,
			&info_frame->avi);
		update_hdmi_info_packet(enc, enc->id, 0, &info_frame->vendor);
		update_hdmi_info_packet(enc, enc->id, 1, &info_frame->gamut);
		update_hdmi_info_packet(enc, enc->id, 2, &info_frame->spd);
	} else if (dc_is_dp_signal(signal))
		update_dp_info_packet(enc, enc->id, 0, &info_frame->vsc);
}

void dce110_stream_encoder_stop_info_packets(
	struct stream_encoder *enc,
	enum engine_id engine,
	enum signal_type signal)
{
	if (dc_is_hdmi_signal(signal))
		stop_hdmi_info_packets(
			enc->ctx,
			fe_engine_offsets[engine]);
	else if (dc_is_dp_signal(signal))
		stop_dp_info_packets(
			enc->ctx,
			fe_engine_offsets[engine]);
}

/*
 * @brief
 * Output blank data,
 * prevents output of the actual surface data on active transmitter
 */
enum encoder_result dce110_stream_encoder_blank(
				struct stream_encoder *enc,
				enum signal_type signal)
{
	enum engine_id engine = enc->id;
	const uint32_t addr = mmDP_VID_STREAM_CNTL + fe_engine_offsets[engine];
	uint32_t value = dal_read_reg(enc->ctx, addr);
	uint32_t retries = 0;
	uint32_t max_retries = DP_BLANK_MAX_RETRY * 10;

	if (!dc_is_dp_signal(signal))
		return ENCODER_RESULT_OK;

	/* Note: For CZ, we are changing driver default to disable
	 * stream deferred to next VBLANK. If results are positive, we
	 * will make the same change to all DCE versions. There are a
	 * handful of panels that cannot handle disable stream at
	 * HBLANK and will result in a white line flash across the
	 * screen on stream disable. */

	/* Specify the video stream disable point
	 * (2 = start of the next vertical blank) */
	set_reg_field_value(
		value,
		2,
		DP_VID_STREAM_CNTL,
		DP_VID_STREAM_DIS_DEFER);
	/* Larger delay to wait until VBLANK - use max retry of
	 * 10us*3000=30ms. This covers 16.6ms of typical 60 Hz mode +
	 * a little more because we may not trust delay accuracy. */
	max_retries = DP_BLANK_MAX_RETRY * 150;

	/* disable DP stream */
	set_reg_field_value(value, 0, DP_VID_STREAM_CNTL, DP_VID_STREAM_ENABLE);
	dal_write_reg(enc->ctx, addr, value);

	/* the encoder stops sending the video stream
	 * at the start of the vertical blanking.
	 * Poll for DP_VID_STREAM_STATUS == 0 */

	do {
		value = dal_read_reg(enc->ctx, addr);

		if (!get_reg_field_value(
			value,
			DP_VID_STREAM_CNTL,
			DP_VID_STREAM_STATUS))
			break;

		dc_service_delay_in_microseconds(enc->ctx, 10);

		++retries;
	} while (retries < max_retries);

	ASSERT(retries <= max_retries);

	/* Tell the DP encoder to ignore timing from CRTC, must be done after
	 * the polling. If we set DP_STEER_FIFO_RESET before DP stream blank is
	 * complete, stream status will be stuck in video stream enabled state,
	 * i.e. DP_VID_STREAM_STATUS stuck at 1. */
	dp_steer_fifo_reset(enc->ctx, engine, true);

	return ENCODER_RESULT_OK;
}

/*
 * @brief
 * Stop sending blank data,
 * output the actual surface data on active transmitter
 */
enum encoder_result dce110_stream_encoder_unblank(
	struct stream_encoder *enc,
	const struct encoder_unblank_param *param)
{
	bool is_dp_signal = param->signal == SIGNAL_TYPE_DISPLAY_PORT
		|| param->signal == SIGNAL_TYPE_DISPLAY_PORT_MST
		|| param->signal == SIGNAL_TYPE_EDP;

	if (!is_dp_signal)
		return ENCODER_RESULT_OK;

	if (param->link_settings.link_rate != LINK_RATE_UNKNOWN) {
		uint32_t n_vid = 0x8000;
		uint32_t m_vid;

		/* M / N = Fstream / Flink
		 * m_vid / n_vid = pixel rate / link rate */

		uint64_t m_vid_l = n_vid;

		m_vid_l *= param->crtc_timing.pixel_clock;
		m_vid_l = div_u64(m_vid_l,
			param->link_settings.link_rate
				* LINK_RATE_REF_FREQ_IN_KHZ);

		m_vid = (uint32_t) m_vid_l;

		setup_vid_stream(enc,
			enc->id, m_vid, n_vid);
	}

	unblank_dp_output(enc, enc->id);

	return ENCODER_RESULT_OK;
}

