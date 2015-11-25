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

#include "resource.h"
#include "include/irq_service_interface.h"
#include "dce110/dce110_timing_generator.h"
#include "dce110/dce110_link_encoder.h"
#include "dce110/dce110_mem_input.h"
#include "dce110/dce110_ipp.h"
#include "dce110/dce110_transform.h"
#include "dce110/dce110_stream_encoder.h"
#include "dce110/dce110_opp.h"
#include "link_encoder_types.h"
#include "stream_encoder_types.h"

enum dce110_clk_src_array_id {
	DCE110_CLK_SRC_PLL0 = 0,
	DCE110_CLK_SRC_PLL1,
	DCE110_CLK_SRC_EXT,

	DCE110_CLK_SRC_TOTAL
};

#define DCE110_MAX_DISPCLK 643000
#define DCE110_MAX_SCLK 626000

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

static void build_info_frame(struct core_stream *stream)
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


bool dce110_construct_resource_pool(
	struct adapter_service *adapter_serv,
	struct dc *dc,
	struct resource_pool *pool)
{
	unsigned int i;
	struct clock_source_init_data clk_src_init_data = { 0 };
	struct audio_init_data audio_init_data = { 0 };
	struct dc_context *ctx = dc->ctx;
	pool->adapter_srv = adapter_serv;

	pool->stream_engines.engine.ENGINE_ID_DIGA = 1;
	pool->stream_engines.engine.ENGINE_ID_DIGB = 1;
	pool->stream_engines.engine.ENGINE_ID_DIGC = 1;

	clk_src_init_data.as = adapter_serv;
	clk_src_init_data.ctx = ctx;
	clk_src_init_data.clk_src_id.enum_id = ENUM_ID_1;
	clk_src_init_data.clk_src_id.type = OBJECT_TYPE_CLOCK_SOURCE;
	pool->clk_src_count = DCE110_CLK_SRC_TOTAL;

	clk_src_init_data.clk_src_id.id = CLOCK_SOURCE_ID_PLL0;
	pool->clock_sources[DCE110_CLK_SRC_PLL0] = dal_clock_source_create(
							&clk_src_init_data);
	clk_src_init_data.clk_src_id.id = CLOCK_SOURCE_ID_PLL1;
	pool->clock_sources[DCE110_CLK_SRC_PLL1] = dal_clock_source_create(
							&clk_src_init_data);
	clk_src_init_data.clk_src_id.id = CLOCK_SOURCE_ID_EXTERNAL;
	pool->clock_sources[DCE110_CLK_SRC_EXT] = dal_clock_source_create(
							&clk_src_init_data);

	for (i = 0; i < pool->clk_src_count; i++) {
		if (pool->clock_sources[i] == NULL) {
			dal_error("DC: failed to create clock sources!\n");
			BREAK_TO_DEBUGGER();
			goto clk_src_create_fail;
		}
	}

	pool->display_clock = dal_display_clock_dce110_create(ctx, adapter_serv);
	if (pool->display_clock == NULL) {
		dal_error("DC: failed to create display clock!\n");
		BREAK_TO_DEBUGGER();
		goto disp_clk_create_fail;
	}

	{
		struct irq_service_init_data init_data;
		init_data.ctx = dc->ctx;
		pool->irqs = dal_irq_service_create(
				dal_adapter_service_get_dce_version(
					dc->res_pool.adapter_srv),
				&init_data);
		if (!pool->irqs)
			goto irqs_create_fail;

	}

	pool->controller_count =
		dal_adapter_service_get_func_controllers_num(adapter_serv);
	pool->stream_enc_count = 3;
	pool->scaler_filter = dal_scaler_filter_create(ctx);
	if (pool->scaler_filter == NULL) {
		BREAK_TO_DEBUGGER();
		dal_error("DC: failed to create filter!\n");
		goto filter_create_fail;
	}

	for (i = 0; i < pool->controller_count; i++) {
		pool->timing_generators[i] = dce110_timing_generator_create(
				adapter_serv,
				ctx,
				i + 1);
		if (pool->timing_generators[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dal_error("DC: failed to create tg!\n");
			goto controller_create_fail;
		}

		pool->mis[i] = dce110_mem_input_create(
			ctx,
			i + 1);
		if (pool->mis[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dal_error(
				"DC: failed to create memory input!\n");
			goto controller_create_fail;
		}

		pool->ipps[i] = dce110_ipp_create(
			ctx,
			i + 1);
		if (pool->ipps[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dal_error(
				"DC: failed to create input pixel processor!\n");
			goto controller_create_fail;
		}

		pool->transforms[i] = dce110_transform_create(
				ctx,
				i + 1);
		if (pool->transforms[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dal_error(
				"DC: failed to create transform!\n");
			goto controller_create_fail;
		}
		dce110_transform_set_scaler_filter(
				pool->transforms[i],
				pool->scaler_filter);

		pool->opps[i] = dce110_opp_create(
			ctx,
			i + 1);
		if (pool->opps[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dal_error(
				"DC: failed to create output pixel processor!\n");
			goto controller_create_fail;
		}
	}

	audio_init_data.as = adapter_serv;
	audio_init_data.ctx = ctx;
	pool->audio_count = 0;
	for (i = 0; i < pool->controller_count; i++) {
		struct graphics_object_id obj_id;

		obj_id = dal_adapter_service_enum_audio_object(adapter_serv, i);
		if (false == dal_graphics_object_id_is_valid(obj_id)) {
			/* no more valid audio objects */
			break;
		}

		audio_init_data.audio_stream_id = obj_id;
		pool->audios[i] = dal_audio_create(&audio_init_data);
		if (pool->audios[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dal_error("DC: failed to create DPPs!\n");
			goto audio_create_fail;
		}
		pool->audio_count++;
	}

	for (i = 0; i < pool->stream_enc_count; i++) {
		struct stream_enc_init_data enc_init_data = { 0 };
		/* TODO: rework fragile code*/
		enc_init_data.stream_engine_id = i;
		enc_init_data.ctx = dc->ctx;
		enc_init_data.adapter_service = adapter_serv;
		if (pool->stream_engines.u_all & 1 << i) {
			pool->stream_enc[i] = dce110_stream_encoder_create(
					&enc_init_data);

			if (pool->stream_enc[i] == NULL) {
				BREAK_TO_DEBUGGER();
				dal_error("DC: failed to create stream_encoder!\n");
				goto stream_enc_create_fail;
			}
		}
	}

	return true;

stream_enc_create_fail:
	for (i = 0; i < pool->stream_enc_count; i++) {
		if (pool->stream_enc[i] != NULL)
			dce110_stream_encoder_destroy(&pool->stream_enc[i]);
	}

audio_create_fail:
	for (i = 0; i < pool->controller_count; i++) {
		if (pool->audios[i] != NULL)
			dal_audio_destroy(&pool->audios[i]);
	}

controller_create_fail:
	for (i = 0; i < pool->controller_count; i++) {
		if (pool->opps[i] != NULL)
			dce110_opp_destroy(&pool->opps[i]);

		if (pool->transforms[i] != NULL)
			dce110_transform_destroy(&pool->transforms[i]);

		if (pool->ipps[i] != NULL)
			dce110_ipp_destroy(&pool->ipps[i]);

		if (pool->mis[i] != NULL)
			dce110_mem_input_destroy(&pool->mis[i]);

		if (pool->timing_generators[i] != NULL)
			dce110_timing_generator_destroy(
				&pool->timing_generators[i]);
	}

filter_create_fail:
	dal_irq_service_destroy(&pool->irqs);

irqs_create_fail:
	dal_display_clock_destroy(&pool->display_clock);

disp_clk_create_fail:
clk_src_create_fail:
	for (i = 0; i < pool->clk_src_count; i++) {
		if (pool->clock_sources[i] != NULL)
			dal_clock_source_destroy(&pool->clock_sources[i]);
	}
	return false;
}

void dce110_destruct_resource_pool(struct resource_pool *pool)
{
	unsigned int i;

	for (i = 0; i < pool->controller_count; i++) {
		if (pool->opps[i] != NULL)
			dce110_opp_destroy(&pool->opps[i]);

		if (pool->transforms[i] != NULL)
			dce110_transform_destroy(&pool->transforms[i]);

		if (pool->ipps[i] != NULL)
			dce110_ipp_destroy(&pool->ipps[i]);

		if (pool->mis[i] != NULL)
			dce110_mem_input_destroy(&pool->mis[i]);

		if (pool->timing_generators[i] != NULL)
			dce110_timing_generator_destroy(
				&pool->timing_generators[i]);
	}

	for (i = 0; i < pool->stream_enc_count; i++) {
		if (pool->stream_enc[i] != NULL)
			dce110_stream_encoder_destroy(&pool->stream_enc[i]);
	}

	for (i = 0; i < pool->clk_src_count; i++) {
		if (pool->clock_sources[i] != NULL) {
			dal_clock_source_destroy(&pool->clock_sources[i]);
		}
	}

	for (i = 0; i < pool->audio_count; i++)	{
		if (pool->audios[i] != NULL) {
			dal_audio_destroy(&pool->audios[i]);
		}
	}
	if (pool->display_clock != NULL) {
		dal_display_clock_destroy(&pool->display_clock);
	}

	if (pool->scaler_filter != NULL) {
		dal_scaler_filter_destroy(&pool->scaler_filter);
	}
	if (pool->irqs != NULL) {
		dal_irq_service_destroy(&pool->irqs);
	}

	if (pool->adapter_srv != NULL) {
		dal_adapter_service_destroy(&pool->adapter_srv);
	}
}

static void attach_stream_to_controller(
		struct resource_context *res_ctx,
		struct core_stream *stream)
{
	res_ctx->controller_ctx[stream->controller_idx].stream = stream;
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

	/* TODO: Handle MST properly
	 * Currently pick next available stream encoder if found*/
	if (j >= 0 && link->public.sink[0]->sink_signal ==
			SIGNAL_TYPE_DISPLAY_PORT_MST)
		return res_ctx->pool.stream_enc[j];

	return NULL;
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


static struct clock_source *find_first_free_pll(
		struct resource_context *res_ctx)
{
	if (res_ctx->clock_source_ref_count[DCE110_CLK_SRC_PLL0] == 0) {
		return res_ctx->pool.clock_sources[DCE110_CLK_SRC_PLL0];
	}
	if (res_ctx->clock_source_ref_count[DCE110_CLK_SRC_PLL1] == 0) {
		return res_ctx->pool.clock_sources[DCE110_CLK_SRC_PLL1];
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

static enum dc_status map_resources(
		const struct dc *dc,
		struct validate_context *context)
{
	uint8_t i, j;

	/* mark resources used for targets that are already active */
	for (i = 0; i < context->target_count; i++) {
		struct core_target *target = context->targets[i];
		if (context->target_flags[i].unchanged)
			for (j = 0; j < target->stream_count; j++) {
				struct core_stream *stream = target->streams[j];
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
		for (j = 0; j < target->stream_count; j++) {
			struct core_stream *stream = target->streams[j];
			struct core_stream *curr_stream;

			if (!assign_first_free_controller(
					&context->res_ctx, stream))
				return DC_NO_CONTROLLER_RESOURCE;

			attach_stream_to_controller(&context->res_ctx, stream);

			stream->stream_enc =
				find_first_free_match_stream_enc_for_link(
					&context->res_ctx,
					stream->sink->link);

			if (!stream->stream_enc)
				return DC_NO_STREAM_ENG_RESOURCE;

			set_stream_engine_in_use(
					&context->res_ctx,
					stream->stream_enc);
			stream->signal =
				stream->sink->public.sink_signal;

			if (dc_is_dp_signal(stream->signal))
				stream->clock_source = context->res_ctx.
					pool.clock_sources[DCE110_CLK_SRC_EXT];
			else
				stream->clock_source =
					find_used_clk_src_for_sharing(
							context, stream);
			if (stream->clock_source == NULL)
				stream->clock_source =
					find_first_free_pll(&context->res_ctx);

			if (stream->clock_source == NULL)
				return DC_NO_CLOCK_SOURCE_RESOURCE;

			reference_clock_source(
					&context->res_ctx,
					stream->clock_source);

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
			curr_stream =
				dc->current_context.res_ctx.controller_ctx
				[stream->controller_idx].stream;
			context->res_ctx.controller_ctx[stream->controller_idx]
			.flags.timing_changed =
				check_timing_change(curr_stream, stream);

		}
	}

	return DC_OK;
}

static enum audio_dto_source translate_to_dto_source(enum controller_id crtc_id)
{
	switch (crtc_id) {
	case CONTROLLER_ID_D0:
		return DTO_SOURCE_ID0;
	case CONTROLLER_ID_D1:
		return DTO_SOURCE_ID1;
	case CONTROLLER_ID_D2:
		return DTO_SOURCE_ID2;
	case CONTROLLER_ID_D3:
		return DTO_SOURCE_ID3;
	case CONTROLLER_ID_D4:
		return DTO_SOURCE_ID4;
	case CONTROLLER_ID_D5:
		return DTO_SOURCE_ID5;
	default:
		return DTO_SOURCE_UNKNOWN;
	}
}

static void build_audio_output(
	const struct core_stream *stream,
	struct audio_output *audio_output)
{
	audio_output->engine_id = stream->stream_enc->id;

	audio_output->signal = stream->signal;

	/* audio_crtc_info  */

	audio_output->crtc_info.h_total =
		stream->public.timing.h_total;

	/* Audio packets are sent during actual CRTC blank physical signal, we
	 * need to specify actual active signal portion */
	audio_output->crtc_info.h_active =
			stream->public.timing.h_addressable
			+ stream->public.timing.h_border_left
			+ stream->public.timing.h_border_right;

	audio_output->crtc_info.v_active =
			stream->public.timing.v_addressable
			+ stream->public.timing.v_border_top
			+ stream->public.timing.v_border_bottom;

	audio_output->crtc_info.pixel_repetition = 1;

	audio_output->crtc_info.interlaced =
			stream->public.timing.flags.INTERLACE;

	audio_output->crtc_info.refresh_rate =
		(stream->public.timing.pix_clk_khz*1000)/
		(stream->public.timing.h_total*stream->public.timing.v_total);

	audio_output->crtc_info.color_depth =
		stream->public.timing.display_color_depth;

	audio_output->crtc_info.requested_pixel_clock =
			stream->pix_clk_params.requested_pix_clk;

	/* TODO - Investigate why calculated pixel clk has to be
	 * requested pixel clk */
	audio_output->crtc_info.calculated_pixel_clock =
			stream->pix_clk_params.requested_pix_clk;

	/* TODO: This is needed for DP */
	if (stream->signal == SIGNAL_TYPE_DISPLAY_PORT) {
		audio_output->pll_info.dp_dto_source_clock_in_khz =
			dal_display_clock_get_dp_ref_clk_frequency(
				stream->dis_clk);
	}

	audio_output->pll_info.feed_back_divider =
			stream->pll_settings.feedback_divider;

	audio_output->pll_info.dto_source =
		translate_to_dto_source(
			stream->controller_idx + 1);

	/* TODO hard code to enable for now. Need get from stream */
	audio_output->pll_info.ss_enabled = true;

	audio_output->pll_info.ss_percentage =
			stream->pll_settings.ss_percentage;
}

static void get_pixel_clock_parameters(
	const struct core_stream *stream,
	struct pixel_clk_params *pixel_clk_params)
{
	pixel_clk_params->requested_pix_clk = stream->public.timing.pix_clk_khz;
	pixel_clk_params->encoder_object_id = stream->sink->link->link_enc->id;
	pixel_clk_params->signal_type = stream->sink->public.sink_signal;
	pixel_clk_params->controller_id = stream->controller_idx + 1;
	/* TODO: un-hardcode*/
	pixel_clk_params->requested_sym_clk = LINK_RATE_LOW *
		LINK_RATE_REF_FREQ_IN_KHZ;
	pixel_clk_params->flags.ENABLE_SS = 0;
	pixel_clk_params->color_depth =
		stream->public.timing.display_color_depth;
	pixel_clk_params->flags.DISPLAY_BLANKED = 1;
}

static enum dc_status build_stream_hw_param(struct core_stream *stream)
{
	/*TODO: unhardcode*/
	stream->max_tmds_clk_from_edid_in_mhz = 0;
	stream->max_hdmi_deep_color = COLOR_DEPTH_121212;
	stream->max_hdmi_pixel_clock = 600000;

	get_pixel_clock_parameters(stream, &stream->pix_clk_params);
	dal_clock_source_get_pix_clk_dividers(
		stream->clock_source,
		&stream->pix_clk_params,
		&stream->pll_settings);

	build_audio_output(stream, &stream->audio_output);

	return DC_OK;
}

static enum dc_status validate_mapped_resource(
		const struct dc *dc,
		struct validate_context *context)
{
	enum dc_status status = DC_OK;
	uint8_t i, j;

	for (i = 0; i < context->target_count; i++) {
		struct core_target *target = context->targets[i];
		if (context->target_flags[i].unchanged)
			continue;
		for (j = 0; j < target->stream_count; j++) {
			struct core_stream *stream = target->streams[j];
			struct core_link *link = stream->sink->link;
			status = build_stream_hw_param(stream);

			if (status != DC_OK)
				return status;

			if (!dce110_timing_generator_validate_timing(
					stream->tg,
					&stream->public.timing,
					SIGNAL_TYPE_HDMI_TYPE_A))
				return DC_FAIL_CONTROLLER_VALIDATE;


			if (dce110_link_encoder_validate_output_with_stream(
					link->link_enc,
					stream)
							!= ENCODER_RESULT_OK)
				return DC_FAIL_ENC_VALIDATE;

			/* TODO: validate audio ASIC caps, encoder */

			status = dc_link_validate_mode_timing(stream->sink,
					link,
					&stream->public.timing);

			if (status != DC_OK)
				return status;

			build_info_frame(stream);
		}
	}

	return DC_OK;
}

enum dc_status dce110_validate_bandwidth(
	const struct dc *dc,
	struct validate_context *context)
{
	uint8_t i, j;
	enum dc_status result = DC_ERROR_UNEXPECTED;
	uint8_t number_of_displays = 0;

	memset(&context->bw_mode_data, 0, sizeof(context->bw_mode_data));

	for (i = 0; i < context->target_count; i++) {
		struct core_target *target = context->targets[i];
		for (j = 0; j < target->stream_count; j++) {
			struct core_stream *stream = target->streams[j];
			struct bw_calcs_input_single_display *disp = &context->
				bw_mode_data.displays_data[number_of_displays];

			if (target->status.surface_count == 0) {
				disp->graphics_scale_ratio = int_to_fixed(1);
				disp->graphics_h_taps = 4;
				disp->graphics_v_taps = 4;

			} else {
				disp->graphics_scale_ratio =
					fixed31_32_to_bw_fixed(
						stream->ratios.vert.value);
				disp->graphics_h_taps = stream->taps.h_taps;
				disp->graphics_v_taps = stream->taps.v_taps;
			}

			disp->graphics_src_width =
					stream->public.timing.h_addressable;
			disp->graphics_src_height =
					stream->public.timing.v_addressable;
			disp->h_total = stream->public.timing.h_total;
			disp->pixel_rate = frc_to_fixed(
				stream->public.timing.pix_clk_khz, 1000);

			/*TODO: get from surface*/
			disp->graphics_bytes_per_pixel = 4;
			disp->graphics_tiling_mode = tiled;

			/* DCE11 defaults*/
			disp->graphics_lb_bpc = 10;
			disp->graphics_interlace_mode = false;
			disp->fbc_enable = false;
			disp->lpt_enable = false;
			disp->graphics_stereo_mode = mono;
			disp->underlay_mode = ul_none;

			number_of_displays++;
		}
	}

	context->bw_mode_data.number_of_displays = number_of_displays;
	context->bw_mode_data.display_synchronization_enabled = false;

	dal_logger_write(dc->ctx->logger,
		LOG_MAJOR_BWM,
		LOG_MINOR_BWM_REQUIRED_BANDWIDTH_CALCS,
		"%s: Start bandwidth calculations",
		__func__);
	if (!bw_calcs(
			dc->ctx,
			&dc->bw_dceip,
			&dc->bw_vbios,
			&context->bw_mode_data,
			&context->bw_results))
		result =  DC_FAIL_BANDWIDTH_VALIDATE;
	else
		result =  DC_OK;


	if (context->bw_results.dispclk_khz > DCE110_MAX_DISPCLK
		|| context->bw_results.required_sclk > DCE110_MAX_SCLK)
		result =  DC_FAIL_BANDWIDTH_VALIDATE;

	if (result == DC_FAIL_BANDWIDTH_VALIDATE)
		dal_logger_write(dc->ctx->logger,
			LOG_MAJOR_BWM,
			LOG_MINOR_BWM_MODE_VALIDATION,
			"%s: Bandwidth validation failed!",
			__func__);

	dal_logger_write(dc->ctx->logger,
		LOG_MAJOR_BWM,
		LOG_MINOR_BWM_REQUIRED_BANDWIDTH_CALCS,
		"%s: Finish bandwidth calculations\n nbpMark: %d",
		__func__,
		context->bw_results.nbp_state_change_watermark[0].b_mark);

	return result;
}

static void set_target_unchanged(
		struct validate_context *context,
		uint8_t target_idx)
{
	uint8_t i;
	struct core_target *target = context->targets[target_idx];
	context->target_flags[target_idx].unchanged = true;
	for (i = 0; i < target->stream_count; i++) {
		uint8_t index = target->streams[i]->controller_idx;
		context->res_ctx.controller_ctx[index].flags.unchanged = true;
	}
}

enum dc_status dce110_validate_with_context(
		const struct dc *dc,
		const struct dc_validation_set set[],
		uint8_t set_count,
		struct validate_context *context)
{
	enum dc_status result = DC_ERROR_UNEXPECTED;
	uint8_t i, j;
	struct dc_context *dc_ctx = dc->ctx;

	for (i = 0; i < set_count; i++) {
		context->targets[i] = DC_TARGET_TO_CORE(set[i].target);

		for (j = 0; j < dc->current_context.target_count; j++)
			if (dc->current_context.targets[j] == context->targets[i])
				set_target_unchanged(context, i);

		if (!context->target_flags[i].unchanged)
			if (!logical_attach_surfaces_to_target(
							(struct dc_surface **)set[i].surfaces,
							set[i].surface_count,
							&context->targets[i]->public)) {
				DC_ERROR("Failed to attach surface to target!\n");
				return DC_FAIL_ATTACH_SURFACES;
			}
	}

	context->target_count = set_count;

	context->res_ctx.pool = dc->res_pool;

	result = map_resources(dc, context);

	if (result == DC_OK)
		result = validate_mapped_resource(dc, context);

	if (result == DC_OK)
		build_scaling_params_for_context(dc, context);

	if (result == DC_OK)
		result = dce110_validate_bandwidth(dc, context);

	return result;
}
