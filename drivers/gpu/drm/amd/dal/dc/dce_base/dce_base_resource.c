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
#include "dce_base_resource.h"

#include "dc_services.h"

#include "set_mode_types.h"
#include "stream_encoder.h"
#include "link_encoder.h"

#include "resource.h"

/* Maximum TMDS single link pixel clock 165MHz */
#define TMDS_MAX_PIXEL_CLOCK_IN_KHZ 165000

static void attach_stream_to_controller(
		struct resource_context *res_ctx,
		struct core_stream *stream)
{
	res_ctx->controller_ctx[stream->controller_idx].stream = stream;
}

static void set_stream_engine_in_use(
		struct resource_context *res_ctx,
		struct stream_encoder *stream_enc)
{
	int i;

	for (i = 0; i < res_ctx->pool.stream_enc_count; i++) {
		if (res_ctx->pool.stream_enc[i] == stream_enc)
			res_ctx->is_stream_enc_acquired[i] = true;
	}
}

/* TODO: release audio object */
static void set_audio_in_use(
		struct resource_context *res_ctx,
		struct audio *audio)
{
	int i;
	for (i = 0; i < res_ctx->pool.audio_count; i++) {
		if (res_ctx->pool.audios[i] == audio) {
			res_ctx->is_audio_acquired[i] = true;
		}
	}
}

static bool assign_first_free_controller(
		struct resource_context *res_ctx,
		struct core_stream *stream)
{
	uint8_t i;
	for (i = 0; i < res_ctx->pool.controller_count; i++) {
		if (!res_ctx->controller_ctx[i].stream) {
			stream->tg = res_ctx->pool.timing_generators[i];
			stream->mi = res_ctx->pool.mis[i];
			stream->ipp = res_ctx->pool.ipps[i];
			stream->xfm = res_ctx->pool.transforms[i];
			stream->opp = res_ctx->pool.opps[i];
			stream->controller_idx = i;
			stream->dis_clk = res_ctx->pool.display_clock;
			return true;
		}
	}
	return false;
}

static struct stream_encoder *find_first_free_match_stream_enc_for_link(
		struct resource_context *res_ctx,
		struct core_link *link)
{
	uint8_t i;
	int8_t j = -1;

	for (i = 0; i < res_ctx->pool.stream_enc_count; i++) {
		if (!res_ctx->is_stream_enc_acquired[i] &&
					res_ctx->pool.stream_enc[i]) {
			/* Store first available for MST second display
			 * in daisy chain use case */
			j = i;
			if (res_ctx->pool.stream_enc[i]->id ==
					link->link_enc->preferred_engine)
				return res_ctx->pool.stream_enc[i];
		}
	}

	/*
	 * below can happen in cases when stream encoder is acquired:
	 * 1) for second MST display in chain, so preferred engine already
	 * acquired;
	 * 2) for another link, which preferred engine already acquired by any
	 * MST configuration.
	 *
	 * If signal is of DP type and preferred engine not found, return last available
	 *
	 * TODO - This is just a patch up and a generic solution is
	 * required for non DP connectors.
	 */
	if (j >= 0 &&  dc_is_dp_signal(link->public.sink[0]->sink_signal))
		return res_ctx->pool.stream_enc[j];

	return NULL;
}

static struct audio *find_first_free_audio(struct resource_context *res_ctx)
{
	int i;
	for (i = 0; i < res_ctx->pool.audio_count; i++) {
		if (res_ctx->is_audio_acquired[i] == false) {
			return res_ctx->pool.audios[i];
		}
	}

	return 0;
}

static bool check_timing_change(struct core_stream *cur_stream,
		struct core_stream *new_stream)
{
	if (cur_stream == NULL)
		return true;

	/* If sink pointer changed, it means this is a hotplug, we should do
	 * full hw setting.
	 */
	if (cur_stream->sink != new_stream->sink)
		return true;

	return !is_same_timing(
					&cur_stream->public.timing,
					&new_stream->public.timing);
}

static void set_stream_signal(struct core_stream *stream)
{
	struct dc_sink *dc_sink = (struct dc_sink *)stream->public.sink;

	/* For asic supports dual link DVI, we should adjust signal type
	 * based on timing pixel clock. If pixel clock more than 165Mhz,
	 * signal is dual link, otherwise, single link.
	 */
	if (dc_sink->sink_signal == SIGNAL_TYPE_DVI_SINGLE_LINK ||
			dc_sink->sink_signal == SIGNAL_TYPE_DVI_DUAL_LINK) {
		if (stream->public.timing.pix_clk_khz >
			TMDS_MAX_PIXEL_CLOCK_IN_KHZ)
			dc_sink->sink_signal = SIGNAL_TYPE_DVI_DUAL_LINK;
		else
			dc_sink->sink_signal = SIGNAL_TYPE_DVI_SINGLE_LINK;
	}

	stream->signal = dc_sink->sink_signal;
}

enum dc_status dce_base_map_resources(
		const struct dc *dc,
		struct validate_context *context)
{
	uint8_t i, j;

	/* mark resources used for targets that are already active */
	for (i = 0; i < context->target_count; i++) {
		struct core_target *target = context->targets[i];

		if (!context->target_flags[i].unchanged)
			continue;

		for (j = 0; j < target->public.stream_count; j++) {
			struct core_stream *stream =
				DC_STREAM_TO_CORE(target->public.streams[j]);

			attach_stream_to_controller(
				&context->res_ctx,
				stream);

			set_stream_engine_in_use(
				&context->res_ctx,
				stream->stream_enc);

			reference_clock_source(
				&context->res_ctx,
				stream->clock_source);

			if (stream->audio) {
				set_audio_in_use(&context->res_ctx,
					stream->audio);
			}
		}
	}

	/* acquire new resources */
	for (i = 0; i < context->target_count; i++) {
		struct core_target *target = context->targets[i];

		if (context->target_flags[i].unchanged)
			continue;

		for (j = 0; j < target->public.stream_count; j++) {
			struct core_stream *stream =
				DC_STREAM_TO_CORE(target->public.streams[j]);
			struct core_stream *curr_stream;

			if (!assign_first_free_controller(
					&context->res_ctx, stream))
				return DC_NO_CONTROLLER_RESOURCE;

			attach_stream_to_controller(&context->res_ctx, stream);

			set_stream_signal(stream);

			curr_stream =
				dc->current_context.res_ctx.controller_ctx
				[stream->controller_idx].stream;
			context->res_ctx.controller_ctx[stream->controller_idx]
			.flags.timing_changed =
				check_timing_change(curr_stream, stream);

			/*
			 * we do not need stream encoder or audio resources
			 * when connecting to virtual link
			 */
			if (stream->sink->link->public.connector_signal ==
							SIGNAL_TYPE_VIRTUAL)
				continue;

			stream->stream_enc =
				find_first_free_match_stream_enc_for_link(
					&context->res_ctx,
					stream->sink->link);

			if (!stream->stream_enc)
				return DC_NO_STREAM_ENG_RESOURCE;

			set_stream_engine_in_use(
					&context->res_ctx,
					stream->stream_enc);

			/* TODO: Add check if ASIC support and EDID audio */
			if (!stream->sink->converter_disable_audio &&
						dc_is_audio_capable_signal(
						stream->signal)) {
				stream->audio = find_first_free_audio(
						&context->res_ctx);

				if (!stream->audio)
					return DC_NO_STREAM_AUDIO_RESOURCE;

				set_audio_in_use(&context->res_ctx,
						stream->audio);
			}
		}
	}

	return DC_OK;
}

static enum ds_color_space build_default_color_space(
		struct core_stream *stream)
{
	enum ds_color_space color_space =
			DS_COLOR_SPACE_SRGB_FULLRANGE;
	struct dc_crtc_timing *timing = &stream->public.timing;

	switch (stream->signal) {
	/* TODO: implement other signal color space setting */
	case SIGNAL_TYPE_DISPLAY_PORT:
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
	case SIGNAL_TYPE_EDP:
		break;
	case SIGNAL_TYPE_HDMI_TYPE_A:
	{
		uint32_t pix_clk_khz;

		if (timing->pixel_encoding == PIXEL_ENCODING_YCBCR422 &&
			timing->pixel_encoding == PIXEL_ENCODING_YCBCR444) {
			if (timing->timing_standard ==
					TIMING_STANDARD_CEA770 &&
				timing->timing_standard ==
						TIMING_STANDARD_CEA861)
				color_space = DS_COLOR_SPACE_SRGB_FULLRANGE;

			pix_clk_khz = timing->pix_clk_khz / 10;
			if (timing->h_addressable == 640 &&
				timing->v_addressable == 480 &&
				(pix_clk_khz == 2520 || pix_clk_khz == 2517))
				color_space = DS_COLOR_SPACE_SRGB_FULLRANGE;
		} else {
			if (timing->timing_standard ==
					TIMING_STANDARD_CEA770 ||
					timing->timing_standard ==
					TIMING_STANDARD_CEA861) {

				color_space =
					(timing->pix_clk_khz > PIXEL_CLOCK) ?
						DS_COLOR_SPACE_YCBCR709 :
						DS_COLOR_SPACE_YCBCR601;
			}
		}
		break;
	}
	default:
		switch (timing->pixel_encoding) {
		case PIXEL_ENCODING_YCBCR422:
		case PIXEL_ENCODING_YCBCR444:
			if (timing->pix_clk_khz > PIXEL_CLOCK)
				color_space = DS_COLOR_SPACE_YCBCR709;
			else
				color_space = DS_COLOR_SPACE_YCBCR601;
			break;
		default:
			break;
		}
		break;
	}
	return color_space;
}

static void translate_info_frame(const struct hw_info_frame *hw_info_frame,
	struct encoder_info_frame *encoder_info_frame)
{
	dc_service_memset(
		encoder_info_frame, 0, sizeof(struct encoder_info_frame));

	/* For gamut we recalc checksum */
	if (hw_info_frame->gamut_packet.valid) {
		uint8_t chk_sum = 0;
		uint8_t *ptr;
		uint8_t i;

		dc_service_memmove(
						&encoder_info_frame->gamut,
						&hw_info_frame->gamut_packet,
						sizeof(struct hw_info_packet));

		/*start of the Gamut data. */
		ptr = &encoder_info_frame->gamut.sb[3];

		for (i = 0; i <= encoder_info_frame->gamut.sb[1]; i++)
			chk_sum += ptr[i];

		encoder_info_frame->gamut.sb[2] = (uint8_t) (0x100 - chk_sum);
	}

	if (hw_info_frame->avi_info_packet.valid) {
		dc_service_memmove(
						&encoder_info_frame->avi,
						&hw_info_frame->avi_info_packet,
						sizeof(struct hw_info_packet));
	}

	if (hw_info_frame->vendor_info_packet.valid) {
		dc_service_memmove(
						&encoder_info_frame->vendor,
						&hw_info_frame->vendor_info_packet,
						sizeof(struct hw_info_packet));
	}

	if (hw_info_frame->spd_packet.valid) {
		dc_service_memmove(
						&encoder_info_frame->spd,
						&hw_info_frame->spd_packet,
						sizeof(struct hw_info_packet));
	}

	if (hw_info_frame->vsc_packet.valid) {
		dc_service_memmove(
						&encoder_info_frame->vsc,
						&hw_info_frame->vsc_packet,
						sizeof(struct hw_info_packet));
	}
}

static void set_avi_info_frame(struct hw_info_packet *info_packet,
		struct core_stream *stream)
{
	enum ds_color_space color_space = DS_COLOR_SPACE_UNKNOWN;
	struct info_frame info_frame = { {0} };
	uint32_t pixel_encoding = 0;
	enum scanning_type scan_type = SCANNING_TYPE_NODATA;
	enum dc_aspect_ratio aspect = ASPECT_RATIO_NO_DATA;
	bool itc = false;
	uint8_t cn0_cn1 = 0;
	uint8_t *check_sum = NULL;
	uint8_t byte_index = 0;

	if (info_packet == NULL)
		return;

	color_space = build_default_color_space(stream);

	/* Initialize header */
	info_frame.avi_info_packet.info_packet_hdmi.bits.header.
			info_frame_type = INFO_FRAME_AVI;
	/* InfoFrameVersion_3 is defined by CEA861F (Section 6.4), but shall
	* not be used in HDMI 2.0 (Section 10.1) */
	info_frame.avi_info_packet.info_packet_hdmi.bits.header.version =
			INFO_FRAME_VERSION_2;
	info_frame.avi_info_packet.info_packet_hdmi.bits.header.length =
			INFO_FRAME_SIZE_AVI;

	/* IDO-defined (Y2,Y1,Y0 = 1,1,1) shall not be used by devices built
	* according to HDMI 2.0 spec (Section 10.1)
	* Add "case PixelEncoding_YCbCr420:    pixelEncoding = 3; break;"
	* when YCbCr 4:2:0 is supported by DAL hardware. */

	switch (stream->public.timing.pixel_encoding) {
	case PIXEL_ENCODING_YCBCR422:
		pixel_encoding = 1;
		break;

	case PIXEL_ENCODING_YCBCR444:
		pixel_encoding = 2;
		break;

	case PIXEL_ENCODING_RGB:
	default:
		pixel_encoding = 0;
	}

	/* Y0_Y1_Y2 : The pixel encoding */
	/* H14b AVI InfoFrame has extension on Y-field from 2 bits to 3 bits */
	info_frame.avi_info_packet.info_packet_hdmi.bits.Y0_Y1_Y2 =
		pixel_encoding;


	/* A0 = 1 Active Format Information valid */
	info_frame.avi_info_packet.info_packet_hdmi.bits.A0 =
		ACTIVE_FORMAT_VALID;

	/* B0, B1 = 3; Bar info data is valid */
	info_frame.avi_info_packet.info_packet_hdmi.bits.B0_B1 =
		BAR_INFO_BOTH_VALID;

	info_frame.avi_info_packet.info_packet_hdmi.bits.SC0_SC1 =
			PICTURE_SCALING_UNIFORM;

	/* S0, S1 : Underscan / Overscan */
	/* TODO: un-hardcode scan type */
	scan_type = SCANNING_TYPE_UNDERSCAN;
	info_frame.avi_info_packet.info_packet_hdmi.bits.S0_S1 = scan_type;

	/* C0, C1 : Colorimetry */
	if (color_space == DS_COLOR_SPACE_YCBCR709)
		info_frame.avi_info_packet.info_packet_hdmi.bits.C0_C1 =
				COLORIMETRY_ITU709;
	else if (color_space == DS_COLOR_SPACE_YCBCR601)
		info_frame.avi_info_packet.info_packet_hdmi.bits.C0_C1 =
				COLORIMETRY_ITU601;
	else
		info_frame.avi_info_packet.info_packet_hdmi.bits.C0_C1 =
				COLORIMETRY_NO_DATA;


	/* TODO: un-hardcode aspect ratio */
	aspect = stream->public.timing.aspect_ratio;

	switch (aspect) {
	case ASPECT_RATIO_4_3:
	case ASPECT_RATIO_16_9:
		info_frame.avi_info_packet.info_packet_hdmi.bits.M0_M1 = aspect;
		break;

	case ASPECT_RATIO_NO_DATA:
	case ASPECT_RATIO_64_27:
	case ASPECT_RATIO_256_135:
	default:
		info_frame.avi_info_packet.info_packet_hdmi.bits.M0_M1 = 0;
	}

	/* Active Format Aspect ratio - same as Picture Aspect Ratio. */
	info_frame.avi_info_packet.info_packet_hdmi.bits.R0_R3 =
			ACTIVE_FORMAT_ASPECT_RATIO_SAME_AS_PICTURE;

	/* TODO: un-hardcode cn0_cn1 and itc */
	cn0_cn1 = 0;
	itc = false;

	if (itc) {
		info_frame.avi_info_packet.info_packet_hdmi.bits.ITC = 1;
		info_frame.avi_info_packet.info_packet_hdmi.bits.CN0_CN1 =
			cn0_cn1;
	}

	/* TODO: un-hardcode q0_q1 */
	if (color_space == DS_COLOR_SPACE_SRGB_FULLRANGE)
		info_frame.avi_info_packet.info_packet_hdmi.bits.Q0_Q1 =
						RGB_QUANTIZATION_FULL_RANGE;
	else if (color_space == DS_COLOR_SPACE_SRGB_LIMITEDRANGE)
		info_frame.avi_info_packet.info_packet_hdmi.bits.Q0_Q1 =
						RGB_QUANTIZATION_LIMITED_RANGE;
	else
		info_frame.avi_info_packet.info_packet_hdmi.bits.Q0_Q1 =
						RGB_QUANTIZATION_DEFAULT_RANGE;

	/* TODO : We should handle YCC quantization,
	 * but we do not have matrix calculation */
	info_frame.avi_info_packet.info_packet_hdmi.bits.YQ0_YQ1 =
					YYC_QUANTIZATION_LIMITED_RANGE;

	info_frame.avi_info_packet.info_packet_hdmi.bits.VIC0_VIC7 =
					stream->public.timing.vic;

	/* pixel repetition
	 * PR0 - PR3 start from 0 whereas pHwPathMode->mode.timing.flags.pixel
	 * repetition start from 1 */
	info_frame.avi_info_packet.info_packet_hdmi.bits.PR0_PR3 = 0;

	/* Bar Info
	 * barTop:    Line Number of End of Top Bar.
	 * barBottom: Line Number of Start of Bottom Bar.
	 * barLeft:   Pixel Number of End of Left Bar.
	 * barRight:  Pixel Number of Start of Right Bar. */
	info_frame.avi_info_packet.info_packet_hdmi.bits.bar_top =
			stream->public.timing.v_border_top;
	info_frame.avi_info_packet.info_packet_hdmi.bits.bar_bottom =
		(stream->public.timing.v_border_top
			- stream->public.timing.v_border_bottom + 1);
	info_frame.avi_info_packet.info_packet_hdmi.bits.bar_left =
			stream->public.timing.h_border_left;
	info_frame.avi_info_packet.info_packet_hdmi.bits.bar_right =
		(stream->public.timing.h_total
			- stream->public.timing.h_border_right + 1);

	/* check_sum - Calculate AFMT_AVI_INFO0 ~ AFMT_AVI_INFO3 */
	check_sum =
		&info_frame.
		avi_info_packet.info_packet_hdmi.packet_raw_data.sb[0];
	*check_sum = INFO_FRAME_AVI + INFO_FRAME_SIZE_AVI
			+ INFO_FRAME_VERSION_2;

	for (byte_index = 1; byte_index <= INFO_FRAME_SIZE_AVI; byte_index++)
		*check_sum += info_frame.avi_info_packet.info_packet_hdmi.
				packet_raw_data.sb[byte_index];

	/* one byte complement */
	*check_sum = (uint8_t) (0x100 - *check_sum);

	/* Store in hw_path_mode */
	info_packet->hb0 =
		info_frame.avi_info_packet.info_packet_hdmi.packet_raw_data.hb0;
	info_packet->hb1 =
		info_frame.avi_info_packet.info_packet_hdmi.packet_raw_data.hb1;
	info_packet->hb2 =
		info_frame.avi_info_packet.info_packet_hdmi.packet_raw_data.hb2;

	for (byte_index = 0; byte_index < sizeof(info_packet->sb); byte_index++)
		info_packet->sb[byte_index] = info_frame.avi_info_packet.
		info_packet_hdmi.packet_raw_data.sb[byte_index];

	info_packet->valid = true;
}

static void set_vendor_info_packet(struct core_stream *stream,
		struct hw_info_packet *info_packet)
{
	uint32_t length = 0;
	bool hdmi_vic_mode = false;
	uint8_t checksum = 0;
	uint32_t i = 0;
	enum dc_timing_3d_format format;

	ASSERT_CRITICAL(stream != NULL);
	ASSERT_CRITICAL(info_packet != NULL);

	format = stream->public.timing.timing_3d_format;

	/* Can be different depending on packet content */
	length = 5;

	if (stream->public.timing.hdmi_vic != 0
			&& stream->public.timing.h_total >= 3840
			&& stream->public.timing.v_total >= 2160)
		hdmi_vic_mode = true;

	/* According to HDMI 1.4a CTS, VSIF should be sent
	 * for both 3D stereo and HDMI VIC modes.
	 * For all other modes, there is no VSIF sent.  */

	if (format == TIMING_3D_FORMAT_NONE && !hdmi_vic_mode)
		return;

	/* 24bit IEEE Registration identifier (0x000c03). LSB first. */
	info_packet->sb[1] = 0x03;
	info_packet->sb[2] = 0x0C;
	info_packet->sb[3] = 0x00;

	/*PB4: 5 lower bytes = 0 (reserved). 3 higher bits = HDMI_Video_Format.
	 * The value for HDMI_Video_Format are:
	 * 0x0 (0b000) - No additional HDMI video format is presented in this
	 * packet
	 * 0x1 (0b001) - Extended resolution format present. 1 byte of HDMI_VIC
	 * parameter follows
	 * 0x2 (0b010) - 3D format indication present. 3D_Structure and
	 * potentially 3D_Ext_Data follows
	 * 0x3..0x7 (0b011..0b111) - reserved for future use */
	if (format != TIMING_3D_FORMAT_NONE)
		info_packet->sb[4] = (2 << 5);
	else if (hdmi_vic_mode)
		info_packet->sb[4] = (1 << 5);

	/* PB5: If PB4 claims 3D timing (HDMI_Video_Format = 0x2):
	 * 4 lower bites = 0 (reserved). 4 higher bits = 3D_Structure.
	 * The value for 3D_Structure are:
	 * 0x0 - Frame Packing
	 * 0x1 - Field Alternative
	 * 0x2 - Line Alternative
	 * 0x3 - Side-by-Side (full)
	 * 0x4 - L + depth
	 * 0x5 - L + depth + graphics + graphics-depth
	 * 0x6 - Top-and-Bottom
	 * 0x7 - Reserved for future use
	 * 0x8 - Side-by-Side (Half)
	 * 0x9..0xE - Reserved for future use
	 * 0xF - Not used */
	switch (format) {
	case TIMING_3D_FORMAT_HW_FRAME_PACKING:
	case TIMING_3D_FORMAT_SW_FRAME_PACKING:
		info_packet->sb[5] = (0x0 << 4);
		break;

	case TIMING_3D_FORMAT_SIDE_BY_SIDE:
	case TIMING_3D_FORMAT_SBS_SW_PACKED:
		info_packet->sb[5] = (0x8 << 4);
		length = 6;
		break;

	case TIMING_3D_FORMAT_TOP_AND_BOTTOM:
	case TIMING_3D_FORMAT_TB_SW_PACKED:
		info_packet->sb[5] = (0x6 << 4);
		break;

	default:
		break;
	}

	/*PB5: If PB4 is set to 0x1 (extended resolution format)
	 * fill PB5 with the correct HDMI VIC code */
	if (hdmi_vic_mode)
		info_packet->sb[5] = stream->public.timing.hdmi_vic;

	/* Header */
	info_packet->hb0 = 0x81; /* VSIF packet type. */
	info_packet->hb1 = 0x01; /* Version */

	/* 4 lower bits = Length, 4 higher bits = 0 (reserved) */
	info_packet->hb2 = (uint8_t) (length);

	/* Calculate checksum */
	checksum = 0;
	checksum += info_packet->hb0;
	checksum += info_packet->hb1;
	checksum += info_packet->hb2;

	for (i = 1; i <= length; i++)
		checksum += info_packet->sb[i];

	info_packet->sb[0] = (uint8_t) (0x100 - checksum);

	info_packet->valid = true;
}

void dce_base_build_info_frame(struct core_stream *stream)
{
	enum signal_type signal = SIGNAL_TYPE_NONE;
	struct hw_info_frame info_frame = { { 0 } };

	/* default all packets to invalid */
	info_frame.avi_info_packet.valid = false;
	info_frame.gamut_packet.valid = false;
	info_frame.vendor_info_packet.valid = false;
	info_frame.spd_packet.valid = false;
	info_frame.vsc_packet.valid = false;

	signal = stream->sink->public.sink_signal;

	/* HDMi and DP have different info packets*/
	if (signal == SIGNAL_TYPE_HDMI_TYPE_A) {
		set_avi_info_frame(&info_frame.avi_info_packet,
				stream);
		set_vendor_info_packet(stream, &info_frame.vendor_info_packet);
	}

	translate_info_frame(&info_frame,
			&stream->encoder_info_frame);
}
