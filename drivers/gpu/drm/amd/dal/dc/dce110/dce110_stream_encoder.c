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
#include "dc_bios_types.h"
#include "dce110_stream_encoder.h"
#include "dce/dce_11_0_d.h"
#include "dce/dce_11_0_sh_mask.h"
#include "dce/dce_11_0_enum.h"

#define DIG_REG(reg)\
	(reg + enc110->offsets.dig)

#define DP_REG(reg)\
	(reg + enc110->offsets.dig)

#define VBI_LINE_0 0
#define DP_BLANK_MAX_RETRY 20
#define HDMI_CLOCK_CHANNEL_RATE_MORE_340M 340000

#ifndef HDMI_CONTROL__HDMI_DATA_SCRAMBLE_EN_MASK
	#define HDMI_CONTROL__HDMI_DATA_SCRAMBLE_EN_MASK 0x2
	#define HDMI_CONTROL__HDMI_DATA_SCRAMBLE_EN__SHIFT 0x1
#endif

enum {
	DP_MST_UPDATE_MAX_RETRY = 50
};

static struct stream_encoder_funcs dce110_str_enc_funcs = {
	.dp_set_stream_attribute =
		dce110_stream_encoder_dp_set_stream_attribute,
	.hdmi_set_stream_attribute =
		dce110_stream_encoder_hdmi_set_stream_attribute,
	.dvi_set_stream_attribute =
		dce110_stream_encoder_dvi_set_stream_attribute,
	.set_mst_bandwidth =
		dce110_stream_encoder_set_mst_bandwidth,
	.update_hdmi_info_packets =
		dce110_stream_encoder_update_hdmi_info_packets,
	.stop_hdmi_info_packets =
		dce110_stream_encoder_stop_hdmi_info_packets,
	.update_dp_info_packets =
		dce110_stream_encoder_update_dp_info_packets,
	.stop_dp_info_packets =
		dce110_stream_encoder_stop_dp_info_packets,
	.dp_blank =
		dce110_stream_encoder_dp_blank,
	.dp_unblank =
		dce110_stream_encoder_dp_unblank,
};

static void dce110_update_generic_info_packet(
	struct dce110_stream_encoder *enc110,
	uint32_t packet_index,
	const struct encoder_info_packet *info_packet)
{
	struct dc_context *ctx = enc110->base.ctx;
	uint32_t addr;
	uint32_t regval;
	/* choose which generic packet to use */
	{
		addr = DIG_REG(mmAFMT_VBI_PACKET_CONTROL);

		regval = dal_read_reg(ctx, addr);

		set_reg_field_value(
			regval,
			packet_index,
			AFMT_VBI_PACKET_CONTROL,
			AFMT_GENERIC_INDEX);

		dal_write_reg(ctx, addr, regval);
	}

	/* write generic packet header
	 * (4th byte is for GENERIC0 only) */
	{
		addr = DIG_REG(mmAFMT_GENERIC_HDR);

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

		dal_write_reg(ctx, addr, regval);
	}

	/* write generic packet contents
	 * (we never use last 4 bytes)
	 * there are 8 (0-7) mmDIG0_AFMT_GENERIC0_x registers */
	{
		const uint32_t *content =
			(const uint32_t *) &info_packet->sb[0];

		uint32_t counter = 0;

		addr = DIG_REG(mmAFMT_GENERIC_0);

		do {
			dal_write_reg(ctx, addr++, *content++);

			++counter;
		} while (counter < 7);
	}

	addr = DIG_REG(mmAFMT_GENERIC_7);

	dal_write_reg(
		ctx,
		addr,
		0);

	/* force double-buffered packet update */
	{
		addr = DIG_REG(mmAFMT_VBI_PACKET_CONTROL);

		regval = dal_read_reg(ctx, addr);

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

		dal_write_reg(ctx, addr, regval);
	}
}

static void dce110_update_hdmi_info_packet(
	struct dce110_stream_encoder *enc110,
	uint32_t packet_index,
	const struct encoder_info_packet *info_packet)
{
	struct dc_context *ctx = enc110->base.ctx;
	uint32_t cont, send, line;
	uint32_t addr;
	uint32_t regval;

	if (info_packet->valid) {
		dce110_update_generic_info_packet(
			enc110,
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
		addr = DIG_REG(mmHDMI_GENERIC_PACKET_CONTROL0);
		break;
	case 2:
	case 3:
		addr = DIG_REG(mmHDMI_GENERIC_PACKET_CONTROL1);
		break;
	default:
		/* invalid HW packet index */
		dal_logger_write(
			ctx->logger,
			LOG_MAJOR_WARNING,
			LOG_MINOR_COMPONENT_ENCODER,
			"Invalid HW packet index: %s()\n",
			__func__);
		return;
	}

	regval = dal_read_reg(ctx, addr);

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
			ctx->logger,
			LOG_MAJOR_WARNING,
			LOG_MINOR_COMPONENT_ENCODER,
			"Invalid HW packet index: %s()\n",
			__func__);
		return;
	}

	dal_write_reg(ctx, addr, regval);
}

bool dce110_stream_encoder_construct(
	struct dce110_stream_encoder *enc110,
	struct dc_context *ctx,
	struct dc_bios *bp,
	enum engine_id eng_id,
	const struct dce110_stream_enc_offsets *offsets)
{
	if (!enc110)
		return false;
	if (!bp)
		return false;

	enc110->base.funcs = &dce110_str_enc_funcs;
	enc110->base.ctx = ctx;
	enc110->base.id = eng_id;
	enc110->base.bp = bp;
	enc110->offsets = *offsets;

	return true;
}

/* setup stream encoder in dp mode */
void dce110_stream_encoder_dp_set_stream_attribute(
	struct stream_encoder *enc,
	struct dc_crtc_timing *crtc_timing)
{
	struct dce110_stream_encoder *enc110 = DCE110STRENC_FROM_STRENC(enc);
	struct dc_context *ctx = enc110->base.ctx;
	const uint32_t addr = DP_REG(mmDP_PIXEL_FORMAT);
	uint32_t value = dal_read_reg(ctx, addr);

	/* set pixel encoding */
	switch (crtc_timing->pixel_encoding) {
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

		if (crtc_timing->flags.Y_ONLY)
			if (crtc_timing->display_color_depth != COLOR_DEPTH_666)
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

	switch (crtc_timing->display_color_depth) {
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

	dal_write_reg(ctx, addr, value);
}

/* setup stream encoder in hdmi mode */
void dce110_stream_encoder_hdmi_set_stream_attribute(
	struct stream_encoder *enc,
	struct dc_crtc_timing *crtc_timing,
	bool enable_audio)
{
	struct dce110_stream_encoder *enc110 = DCE110STRENC_FROM_STRENC(enc);
	struct dc_context *ctx = enc110->base.ctx;
	uint32_t output_pixel_clock = crtc_timing->pix_clk_khz;
	uint32_t value;
	uint32_t addr;
	struct bp_encoder_control cntl = {0};

	cntl.action = ENCODER_CONTROL_SETUP;
	cntl.engine_id = enc110->base.id;
	cntl.signal = SIGNAL_TYPE_HDMI_TYPE_A;
	cntl.enable_dp_audio = enable_audio;
	cntl.pixel_clock = crtc_timing->pix_clk_khz;
	cntl.lanes_number = LANE_COUNT_FOUR;
	cntl.color_depth = crtc_timing->display_color_depth;

	if (enc110->base.bp->funcs->encoder_control(
			enc110->base.bp, &cntl) != BP_RESULT_OK)
		return;

	addr = DIG_REG(mmDIG_FE_CNTL);
	value = dal_read_reg(ctx, addr);

	switch (crtc_timing->pixel_encoding) {
	case PIXEL_ENCODING_YCBCR422:
		set_reg_field_value(value, 1, DIG_FE_CNTL, TMDS_PIXEL_ENCODING);
		break;
	default:
		set_reg_field_value(value, 0, DIG_FE_CNTL, TMDS_PIXEL_ENCODING);
		break;
	}
	set_reg_field_value(value, 0, DIG_FE_CNTL, TMDS_COLOR_FORMAT);
	dal_write_reg(ctx, addr, value);

	/* setup HDMI engine */
	addr = DIG_REG(mmHDMI_CONTROL);
	value = dal_read_reg(ctx, addr);
	set_reg_field_value(value, 1, HDMI_CONTROL, HDMI_PACKET_GEN_VERSION);
	set_reg_field_value(value, 1, HDMI_CONTROL, HDMI_KEEPOUT_MODE);
	set_reg_field_value(value, 0, HDMI_CONTROL, HDMI_DEEP_COLOR_ENABLE);
	set_reg_field_value(value, 0, HDMI_CONTROL, HDMI_DATA_SCRAMBLE_EN);
	set_reg_field_value(value, 0, HDMI_CONTROL, HDMI_CLOCK_CHANNEL_RATE);

	switch (crtc_timing->display_color_depth) {
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
		output_pixel_clock = (crtc_timing->pix_clk_khz * 30) / 24;
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
		output_pixel_clock = (crtc_timing->pix_clk_khz * 36) / 24;
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
		output_pixel_clock = (crtc_timing->pix_clk_khz * 48) / 24;
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
	} else if (crtc_timing->flags.LTE_340MCSC_SCRAMBLE) {

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

	dal_write_reg(ctx, addr, value);

	addr = DIG_REG(mmHDMI_VBI_PACKET_CONTROL);
	value = dal_read_reg(ctx, addr);
	set_reg_field_value(value, 1, HDMI_VBI_PACKET_CONTROL, HDMI_GC_CONT);
	set_reg_field_value(value, 1, HDMI_VBI_PACKET_CONTROL, HDMI_GC_SEND);
	set_reg_field_value(value, 1, HDMI_VBI_PACKET_CONTROL, HDMI_NULL_SEND);

	dal_write_reg(ctx, addr, value);

	/* following belongs to audio */
	addr = DIG_REG(mmHDMI_INFOFRAME_CONTROL0);
	value = dal_read_reg(ctx, addr);
	set_reg_field_value(
		value,
		1,
		HDMI_INFOFRAME_CONTROL0,
		HDMI_AUDIO_INFO_SEND);
	dal_write_reg(ctx, addr, value);

	addr = DIG_REG(mmAFMT_INFOFRAME_CONTROL0);
	value = dal_read_reg(ctx, addr);
	set_reg_field_value(
		value,
		1,
		AFMT_INFOFRAME_CONTROL0,
		AFMT_AUDIO_INFO_UPDATE);
	dal_write_reg(ctx, addr, value);

	addr = DIG_REG(mmHDMI_INFOFRAME_CONTROL1);
	value = dal_read_reg(ctx, addr);
	set_reg_field_value(
		value,
		VBI_LINE_0 + 2,
		HDMI_INFOFRAME_CONTROL1,
		HDMI_AUDIO_INFO_LINE);
	dal_write_reg(ctx, addr, value);

	addr = DIG_REG(mmHDMI_GC);
	value = dal_read_reg(ctx, addr);
	set_reg_field_value(value, 0, HDMI_GC, HDMI_GC_AVMUTE);
	dal_write_reg(ctx, addr, value);
}

/* setup stream encoder in dvi mode */
void dce110_stream_encoder_dvi_set_stream_attribute(
	struct stream_encoder *enc,
	struct dc_crtc_timing *crtc_timing,
	bool is_dual_link)
{
	struct dce110_stream_encoder *enc110 = DCE110STRENC_FROM_STRENC(enc);
	struct dc_context *ctx = enc110->base.ctx;
	uint32_t addr = DIG_REG(mmDIG_FE_CNTL);
	uint32_t value = dal_read_reg(ctx, addr);
	struct bp_encoder_control cntl = {0};

	cntl.action = ENCODER_CONTROL_SETUP;
	cntl.engine_id = enc110->base.id;
	cntl.signal = is_dual_link ?
		SIGNAL_TYPE_DVI_DUAL_LINK :
		SIGNAL_TYPE_DVI_SINGLE_LINK;
	cntl.enable_dp_audio = false;
	cntl.pixel_clock = crtc_timing->pix_clk_khz;
	cntl.lanes_number = (is_dual_link) ?
				LANE_COUNT_EIGHT : LANE_COUNT_FOUR;
	cntl.color_depth = crtc_timing->display_color_depth;

	if (enc110->base.bp->funcs->encoder_control(
			enc110->base.bp, &cntl) != BP_RESULT_OK)
		return;

	switch (crtc_timing->pixel_encoding) {
	case PIXEL_ENCODING_YCBCR422:
		set_reg_field_value(value, 1, DIG_FE_CNTL, TMDS_PIXEL_ENCODING);
		break;
	default:
		set_reg_field_value(value, 0, DIG_FE_CNTL, TMDS_PIXEL_ENCODING);
		break;
	}

	switch (crtc_timing->display_color_depth) {
	case COLOR_DEPTH_101010:
		if (crtc_timing->pixel_encoding == PIXEL_ENCODING_RGB)
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
	dal_write_reg(ctx, addr, value);
}

void dce110_stream_encoder_set_mst_bandwidth(
	struct stream_encoder *enc,
	struct fixed31_32 avg_time_slots_per_mtp)
{
	struct dce110_stream_encoder *enc110 = DCE110STRENC_FROM_STRENC(enc);
	struct dc_context *ctx = enc110->base.ctx;
	uint32_t addr;
	uint32_t field;
	uint32_t value;
	uint32_t retries = 0;
	uint32_t x = dal_fixed31_32_floor(
		avg_time_slots_per_mtp);
	uint32_t y = dal_fixed31_32_ceil(
		dal_fixed31_32_shl(
			dal_fixed31_32_sub_int(
				avg_time_slots_per_mtp,
				x),
			26));

	{
		addr = DP_REG(mmDP_MSE_RATE_CNTL);
		value = dal_read_reg(ctx, addr);

		set_reg_field_value(
			value,
			x,
			DP_MSE_RATE_CNTL,
			DP_MSE_RATE_X);

		set_reg_field_value(
			value,
			y,
			DP_MSE_RATE_CNTL,
			DP_MSE_RATE_Y);

		dal_write_reg(ctx, addr, value);
	}

	/* wait for update to be completed on the link */
	/* i.e. DP_MSE_RATE_UPDATE_PENDING field (read only) */
	/* is reset to 0 (not pending) */
	{
		addr = DP_REG(mmDP_MSE_RATE_UPDATE);

		do {
			value = dal_read_reg(ctx, addr);

			field = get_reg_field_value(
					value,
					DP_MSE_RATE_UPDATE,
					DP_MSE_RATE_UPDATE_PENDING);

			if (!(field &
			DP_MSE_RATE_UPDATE__DP_MSE_RATE_UPDATE_PENDING_MASK))
				break;

			dc_service_delay_in_microseconds(ctx, 10);

			++retries;
		} while (retries < DP_MST_UPDATE_MAX_RETRY);
	}
}

void dce110_stream_encoder_update_hdmi_info_packets(
	struct stream_encoder *enc,
	const struct encoder_info_frame *info_frame)
{
	struct dce110_stream_encoder *enc110 = DCE110STRENC_FROM_STRENC(enc);
	struct dc_context *ctx = enc110->base.ctx;
	uint32_t regval;
	uint32_t addr;
	uint32_t control0val;
	uint32_t control1val;

	if (info_frame->avi.valid) {
		const uint32_t *content =
			(const uint32_t *) &info_frame->avi.sb[0];

		addr = DIG_REG(mmAFMT_AVI_INFO0);
		regval = content[0];
		dal_write_reg(
			ctx,
			addr,
			regval);
		regval = content[1];

		addr = DIG_REG(mmAFMT_AVI_INFO1);
		dal_write_reg(
			ctx,
			addr,
			regval);
		regval = content[2];

		addr = DIG_REG(mmAFMT_AVI_INFO2);
		dal_write_reg(
			ctx,
			addr,
			regval);
		regval = content[3];

		/* move version to AVI_INFO3 */
		addr = DIG_REG(mmAFMT_AVI_INFO3);
		set_reg_field_value(
			regval,
			info_frame->avi.hb1,
			AFMT_AVI_INFO3,
			AFMT_AVI_INFO_VERSION);

		dal_write_reg(
			ctx,
			addr,
			regval);

		addr = DIG_REG(mmHDMI_INFOFRAME_CONTROL0);

		control0val = dal_read_reg(ctx, addr);

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

		dal_write_reg(ctx, addr, control0val);

		addr = DIG_REG(mmHDMI_INFOFRAME_CONTROL1);

		control1val = dal_read_reg(ctx, addr);

		set_reg_field_value(
			control1val,
			VBI_LINE_0 + 2,
			HDMI_INFOFRAME_CONTROL1,
			HDMI_AVI_INFO_LINE);

		dal_write_reg(ctx, addr, control1val);
	} else {
		addr = DIG_REG(mmHDMI_INFOFRAME_CONTROL0);

		regval = dal_read_reg(ctx, addr);

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

		dal_write_reg(ctx, addr, regval);
	}

	dce110_update_hdmi_info_packet(enc110, 0, &info_frame->vendor);
	dce110_update_hdmi_info_packet(enc110, 1, &info_frame->gamut);
	dce110_update_hdmi_info_packet(enc110, 2, &info_frame->spd);
}

void dce110_stream_encoder_stop_hdmi_info_packets(
	struct stream_encoder *enc)
{
	struct dce110_stream_encoder *enc110 = DCE110STRENC_FROM_STRENC(enc);
	struct dc_context *ctx = enc110->base.ctx;
	uint32_t addr = 0;
	uint32_t value = 0;

	/* stop generic packets 0 & 1 on HDMI */
	addr = DIG_REG(mmHDMI_GENERIC_PACKET_CONTROL0);

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
	addr = DIG_REG(mmHDMI_GENERIC_PACKET_CONTROL1);

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
	addr = DIG_REG(mmHDMI_INFOFRAME_CONTROL0);

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
void dce110_stream_encoder_update_dp_info_packets(
	struct stream_encoder *enc,
	const struct encoder_info_frame *info_frame)
{
	struct dce110_stream_encoder *enc110 = DCE110STRENC_FROM_STRENC(enc);
	struct dc_context *ctx = enc110->base.ctx;
	uint32_t addr = DP_REG(mmDP_SEC_CNTL);
	uint32_t value;

	if (info_frame->vsc.valid)
		dce110_update_generic_info_packet(
			enc110,
			0,
			&info_frame->vsc);

	/* enable/disable transmission of packet(s).
	*  If enabled, packet transmission begins on the next frame
	*/

	value = dal_read_reg(ctx, addr);

	set_reg_field_value(
		value,
		info_frame->vsc.valid,
		DP_SEC_CNTL,
		DP_SEC_GSP0_ENABLE);
	/* This bit is the master enable bit.
	* When enabling secondary stream engine,
	* this master bit must also be set.
	* This register shared with audio info frame.
	* Therefore we need to enable master bit
	* if at least on of the fields is not 0
	*/
	if (value)
		set_reg_field_value(
			value,
			1,
			DP_SEC_CNTL,
			DP_SEC_STREAM_ENABLE);

	dal_write_reg(ctx, addr, value);
}

void dce110_stream_encoder_stop_dp_info_packets(
	struct stream_encoder *enc)
{
	/* stop generic packets on DP */
	struct dce110_stream_encoder *enc110 = DCE110STRENC_FROM_STRENC(enc);
	struct dc_context *ctx = enc110->base.ctx;
	uint32_t addr = DP_REG(mmDP_SEC_CNTL);
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

void dce110_stream_encoder_dp_blank(
	struct stream_encoder *enc)
{
	struct dce110_stream_encoder *enc110 = DCE110STRENC_FROM_STRENC(enc);
	struct dc_context *ctx = enc110->base.ctx;
	uint32_t addr = DP_REG(mmDP_VID_STREAM_CNTL);
	uint32_t value = dal_read_reg(ctx, addr);
	uint32_t retries = 0;
	uint32_t max_retries = DP_BLANK_MAX_RETRY * 10;

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
	* a little more because we may not trust delay accuracy.
	*/
	max_retries = DP_BLANK_MAX_RETRY * 150;

	/* disable DP stream */
	set_reg_field_value(value, 0, DP_VID_STREAM_CNTL, DP_VID_STREAM_ENABLE);
	dal_write_reg(ctx, addr, value);

	/* the encoder stops sending the video stream
	* at the start of the vertical blanking.
	* Poll for DP_VID_STREAM_STATUS == 0
	*/

	do {
		value = dal_read_reg(ctx, addr);

		if (!get_reg_field_value(
			value,
			DP_VID_STREAM_CNTL,
			DP_VID_STREAM_STATUS))
			break;

		dc_service_delay_in_microseconds(ctx, 10);

		++retries;
	} while (retries < max_retries);

	ASSERT(retries <= max_retries);

	/* Tell the DP encoder to ignore timing from CRTC, must be done after
	* the polling. If we set DP_STEER_FIFO_RESET before DP stream blank is
	* complete, stream status will be stuck in video stream enabled state,
	* i.e. DP_VID_STREAM_STATUS stuck at 1.
	*/
	addr = DP_REG(mmDP_STEER_FIFO);
	value = dal_read_reg(ctx, addr);
	set_reg_field_value(value, true, DP_STEER_FIFO, DP_STEER_FIFO_RESET);
	dal_write_reg(ctx, addr, value);
}

/* output video stream to link encoder */
void dce110_stream_encoder_dp_unblank(
	struct stream_encoder *enc,
	const struct encoder_unblank_param *param)
{
	struct dce110_stream_encoder *enc110 = DCE110STRENC_FROM_STRENC(enc);
	struct dc_context *ctx = enc110->base.ctx;
	uint32_t addr;
	uint32_t value;

	if (param->link_settings.link_rate != LINK_RATE_UNKNOWN) {
		uint32_t n_vid = 0x8000;
		uint32_t m_vid;

		/* M / N = Fstream / Flink
		* m_vid / n_vid = pixel rate / link rate
		*/

		uint64_t m_vid_l = n_vid;

		m_vid_l *= param->crtc_timing.pixel_clock;
		m_vid_l = div_u64(m_vid_l,
			param->link_settings.link_rate
				* LINK_RATE_REF_FREQ_IN_KHZ);

		m_vid = (uint32_t) m_vid_l;

		/* enable auto measurement */
		addr = DP_REG(mmDP_VID_TIMING);
		value = dal_read_reg(ctx, addr);
		set_reg_field_value(value, 0, DP_VID_TIMING, DP_VID_M_N_GEN_EN);
		dal_write_reg(ctx, addr, value);

		/* auto measurement need 1 full 0x8000 symbol cycle to kick in,
		* therefore program initial value for Mvid and Nvid
		*/
		addr = DP_REG(mmDP_VID_N);
		value = dal_read_reg(ctx, addr);
		set_reg_field_value(value, n_vid, DP_VID_N, DP_VID_N);
		dal_write_reg(ctx, addr, value);

		addr = DP_REG(mmDP_VID_M);
		value = dal_read_reg(ctx, addr);
		set_reg_field_value(value, m_vid, DP_VID_M, DP_VID_M);
		dal_write_reg(ctx, addr, value);

		addr = DP_REG(mmDP_VID_TIMING);
		value = dal_read_reg(ctx, addr);
		set_reg_field_value(value, 1, DP_VID_TIMING, DP_VID_M_N_GEN_EN);
		dal_write_reg(ctx, addr, value);
	}

	/* set DIG_START to 0x1 to resync FIFO */
	addr = DIG_REG(mmDIG_FE_CNTL);
	value = dal_read_reg(ctx, addr);
	set_reg_field_value(value, 1, DIG_FE_CNTL, DIG_START);
	dal_write_reg(ctx, addr, value);

	/* switch DP encoder to CRTC data */
	addr = DP_REG(mmDP_STEER_FIFO);
	value = dal_read_reg(ctx, addr);
	set_reg_field_value(value, 0, DP_STEER_FIFO, DP_STEER_FIFO_RESET);
	dal_write_reg(ctx, addr, value);

	/* wait 100us for DIG/DP logic to prime
	* (i.e. a few video lines)
	*/
	dc_service_delay_in_microseconds(ctx, 100);

	/* the hardware would start sending video at the start of the next DP
	* frame (i.e. rising edge of the vblank).
	* NOTE: We used to program DP_VID_STREAM_DIS_DEFER = 2 here, but this
	* register has no effect on enable transition! HW always guarantees
	* VID_STREAM enable at start of next frame, and this is not
	* programmable
	*/
	addr = DP_REG(mmDP_VID_STREAM_CNTL);
	value = dal_read_reg(ctx, addr);
	set_reg_field_value(
		value,
		true,
		DP_VID_STREAM_CNTL,
		DP_VID_STREAM_ENABLE);
	dal_write_reg(ctx, addr, value);
}

