/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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

#include "include/adapter_service_interface.h"
#include "include/set_mode_types.h"
#include "include/hw_sequencer_types.h"
#include "include/dcs_types.h"
#include "include/logger_interface.h"
#include "include/adjustment_interface.h"
#include "include/display_path_interface.h"
#include "include/timing_service_interface.h"
#include "include/topology_mgr_interface.h"

#include "ds_dispatch.h"
#include "adjustment_container.h"
#include "grph_colors_group.h"
#include "adjustment_types_internal.h"
#include "ds_translation.h"

/*
 * build_itc_cn0_cn1_flags
 *
 * @brief
 * build ITC, CN0 & CN1 flags
 *
 * ITC flag value comes from AVI Info Frame (GetAdjustmentVal)
 * If ITC flag is enabled, itc is set to 1
 * Support for CNC content is retrieved from CEA Block-Byte 8
 * (GetCeaVendorSpecificDataBlock)
 * If the content being sent is photo, cinema, or game, and the EDID does
 * not have the corresponding CNC flag set, ITC has to be 0 and (CN1, CN0)
 * have to be (0, 0) in order to meet HDMI compliance.
 *
 * @param
 * const struct adj_container *adj_container
 * - [in] adjustment container
 * const uint32_t disp_index - [in] display index
 * bool *itc - [out] ITC flag
 * uint8_t *cn0_cn1 - [out] CN0 CN1 flags
 *
 * @return
 * void
 */
static void build_itc_cn0_cn1_flags(
		const struct adj_container *adj_container, bool *itc,
		uint8_t *cn0_cn1)
{
	uint32_t itc_flag = 0;
	uint32_t cn0_cn1_temp = 0;
	enum display_content_type display_content;
	union display_content_support support;

	/* Initialize output values */
	*itc = true;
	*cn0_cn1 = 0;

	/* Check what kind of content display could support */
	if (adj_container == NULL
		|| !dal_adj_container_get_display_content_capability(
				adj_container, &support))
		return;

	if (!dal_adj_container_get_adjustment_val(adj_container,
			ADJ_ID_ITC_ENABLE, &itc_flag)) {
		/* This case is valid for:
		 * 1) non HDMI */
		*itc = false;
		return;
	}

	if (!dal_adj_container_get_adjustment_val(adj_container,
			ADJ_ID_CNC_CONTENT, &cn0_cn1_temp))
		/* This case is valid for:
		 * 1) non HDMI */
		return;

	*itc = itc_flag > 0;

	if (itc_flag == 0) {
		*cn0_cn1 = 0;
		return;
	}

	/* Get the requested content type from adjustment value */
	dal_ds_translation_translate_content_type(cn0_cn1_temp,
			&display_content);

	if (!support.bits.VALID_CONTENT_TYPE)
		/* We are dealing with HDMI version < 1.4 where CNC flags were
		 * not defined (i.e. these fields are reserved and are set to 0)
		 * In this case, we are allowed to support ITC if user wants it
		 * (CN1, CN0) are set to (0, 0) since they are reserved fields
		 * in HDMI version < 1.4 */
		*cn0_cn1 = 0;
	else {
		/* We are dealing with HDMI version 1.4 or later where
		 * CNC flags can be read from the vendor specific data block */
		if (display_content == DISPLAY_CONTENT_TYPE_GRAPHICS) {
			/* In this case, ITC = 1 or ITC = 0. */
			if (support.bits.GRAPHICS_CONTENT == 1)
				*cn0_cn1 = 0;
		} else if (display_content == DISPLAY_CONTENT_TYPE_PHOTO) {
			if (support.bits.PHOTO_CONTENT == 1)
				*cn0_cn1 = 1;
			else {
				/* To pass HDMI compliance test,
				 * ITC = 0, and (CN1, CN0) = (0, 0) */
				*cn0_cn1 = 0;
				*itc = 0;
			}
		} else if (display_content == DISPLAY_CONTENT_TYPE_CINEMA) {
			if (support.bits.CINEMA_CONTENT == 1)
				*cn0_cn1 = 2;
			else {
				/* To pass HDMI compliance test,
				 *ITC = 0, and (CN1, CN0) = (0, 0) */
				*cn0_cn1 = 0;
				*itc = 0;
			}
		} else if (display_content == DISPLAY_CONTENT_TYPE_GAME) {
			if (support.bits.GAME_CONTENT == 1)
				*cn0_cn1 = 3;
			else {
				/* To pass HDMI compliance test,
				 * ITC = 0, and (CN1, CN0) = (0, 0) */
				*cn0_cn1 = 0;
				*itc = 0;
			}
		}
	}
}

/*
 * ds_prepare_avi_info_frame
 *
 * @brief
 * Build AVI info frame
 *
 * @param
 * const struct path_mode *mode - [in] path mode
 * const struct display_path *disp_path - [in] display path
 * const struct hw_overscan overscan - [in] overscan config
 * const struct hw_scale_options underscan_rule -[in] scale options
 * struct hw_info_packet *info_packet - [out] info frame packet being set
 *
 * @return
 * void
 */
static void prepare_avi_info_frame(struct ds_dispatch *ds_dispatch,
		const struct path_mode *mode,
		const struct display_path *disp_path,
		const struct overscan_info overscan,
		const enum hw_scale_options underscan_rule,
		struct hw_info_packet *info_packet)
{
	struct adj_container *adj_container = NULL;
	enum ds_color_space color_space = DS_COLOR_SPACE_UNKNOWN;
	bool extended_colorimetry = false;
	struct info_frame info_frame = { {0} };
	uint32_t pixel_encoding = 0;
	enum scanning_type scan_type = SCANNING_TYPE_NODATA;
	enum aspect_ratio aspect = ASPECT_RATIO_NO_DATA;
	bool itc = false;
	uint8_t cn0_cn1 = 0;
	union cea_video_capability_data_block video_cap;
	bool video_cap_support = false;
	uint32_t hw_pixel_repetition = 0;
	uint8_t *check_sum = NULL;
	uint8_t byte_index = 0;

	if (mode == NULL || info_packet == NULL || mode->mode_timing == NULL
			|| disp_path == NULL) {
		dal_logger_write(ds_dispatch->dal_context->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_DISPLAY_SERVICE,
			"Invalid parameters");
		return;
	}

	/* TODO: Adjustment implementation. */
	adj_container = dal_ds_dispatch_get_adj_container_for_path(
		ds_dispatch, mode->display_path_index);

	/* TODO: Verify adjustment container
	if (adj_container == NULL) {
	dal_logger_write(ds_dispatch->dal_context,
	LOG_MAJOR_ERROR,
	LOG_MINOR_COMPONENT_DISPLAY_SERVICE,
	"Invalid adjustment container");
	return;
	}
	*/

	color_space = dal_grph_colors_group_get_color_space(
			ds_dispatch->grph_colors_adj,
			&mode->mode_timing->crtc_timing,
			disp_path,
			adj_container);

	/* TODO: Verify colors group
	if (color_space == DS_COLOR_SPACE_UNKNOWN) {
	dal_logger_write(ds_dispatch->dal_context,
	LOG_MAJOR_ERROR,
	LOG_MINOR_COMPONENT_DISPLAY_SERVICE,
	"Invalid color space");
	return;
	}
	*/

	extended_colorimetry = dal_ds_dispatch_is_gamut_change_required(
			ds_dispatch,
			mode->mode_timing->crtc_timing.pixel_encoding,
			mode->pixel_format, mode->display_path_index);

	/* Initialize header */
	info_frame.avi_info_packet.info_packet.bits.header.info_frame_type =
			INFO_FRAME_AVI;
	/* InfoFrameVersion_3 is defined by CEA861F (Section 6.4), but shall
	* not be used in HDMI 2.0 (Section 10.1) */
	info_frame.avi_info_packet.info_packet.bits.header.version =
			INFO_FRAME_VERSION_2;
	info_frame.avi_info_packet.info_packet.bits.header.length =
			INFO_FRAME_SIZE_AVI;

	/* IDO-defined (Y2,Y1,Y0 = 1,1,1) shall not be used by devices built
	* according to HDMI 2.0 spec (Section 10.1)
	* Add "case PixelEncoding_YCbCr420:    pixelEncoding = 3; break;"
	* when YCbCr 4:2:0 is supported by DAL hardware. */
	switch (mode->mode_timing->crtc_timing.pixel_encoding) {
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
	info_frame.avi_info_packet.info_packet.bits.Y0_Y1_Y2 = pixel_encoding;

	dal_logger_write(ds_dispatch->dal_context->logger,
		LOG_MAJOR_INFO_PACKETS,
		LOG_MINOR_INFO_PACKETS_HDMI,
		"====AVIInfoFrame pixEnc (%d) %s===",
		info_frame.avi_info_packet.info_packet.bits.Y0_Y1_Y2,
		pixel_encoding == 1 ? "YCbCr422" :
		pixel_encoding == 2 ? "YCbCr444" : "RGB");

	/* A0 = 1 Active Format Information valid */
	info_frame.avi_info_packet.info_packet.bits.A0 = ACTIVE_FORMAT_VALID;

	/* B0, B1 = 3; Bar info data is valid */
	info_frame.avi_info_packet.info_packet.bits.B0_B1 = BAR_INFO_BOTH_VALID;

	info_frame.avi_info_packet.info_packet.bits.SC0_SC1 =
			PICTURE_SCALING_UNIFORM;

	/* S0, S1 : Underscan / Overscan */

	if (dal_adj_container_get_scan_type(adj_container, &scan_type))
		info_frame.avi_info_packet.info_packet.bits.S0_S1 = scan_type;
	else
		info_frame.avi_info_packet.info_packet.bits.S0_S1 =
				underscan_rule;

	dal_logger_write(ds_dispatch->dal_context->logger,
		LOG_MAJOR_INFO_PACKETS,
		LOG_MINOR_INFO_PACKETS_HDMI, "scanType (%d) %s",
		info_frame.avi_info_packet.info_packet.bits.S0_S1,
		info_frame.avi_info_packet.info_packet.bits.S0_S1 == 1 ?
				"Overscan" :
		info_frame.avi_info_packet.info_packet.bits.S0_S1 == 2 ?
				"Underscan" : "Unknown");

	/* C0, C1 : Colorimetry */
	if (color_space == DS_COLOR_SPACE_YPBPR709)
		info_frame.avi_info_packet.info_packet.bits.C0_C1 =
				COLORIMETRY_ITU709;
	else if (color_space == DS_COLOR_SPACE_YPBPR601)
		info_frame.avi_info_packet.info_packet.bits.C0_C1 =
				COLORIMETRY_ITU601;
	else
		info_frame.avi_info_packet.info_packet.bits.C0_C1 =
				COLORIMETRY_NO_DATA;

	/* EC2,EC1,EC0 - Valid only if C0,C1 = 1,1 */

	if (extended_colorimetry) {
		/* GBD enabled, we need update EC0~EC2,
		* also change C0, C1 to 1, 1. */
		if (info_frame.avi_info_packet.info_packet.bits.C0_C1
				== COLORIMETRY_ITU601)
			info_frame.avi_info_packet.info_packet.bits.EC0_EC2 =
					COLORIMETRY_EX_XVYCC601;
		else if (info_frame.avi_info_packet.info_packet.bits.C0_C1
				== COLORIMETRY_ITU709)
			info_frame.avi_info_packet.info_packet.bits.EC0_EC2 =
					COLORIMETRY_EX_XVYCC709;

		info_frame.avi_info_packet.info_packet.bits.C0_C1 =
				COLORIMETRY_EXTENDED;
	}

	dal_logger_write(ds_dispatch->dal_context->logger,
		LOG_MAJOR_INFO_PACKETS,
		LOG_MINOR_INFO_PACKETS_HDMI, "colometry (%d) %s",
		info_frame.avi_info_packet.info_packet.bits.C0_C1,
		info_frame.avi_info_packet.info_packet.bits.C0_C1 == 1 ?
				"ITU601" :
		info_frame.avi_info_packet.info_packet.bits.C0_C1 == 2 ?
				"ITU709" :
		info_frame.avi_info_packet.info_packet.bits.C0_C1 == 3 ?
				"Underscan" : "Unknown");

	dal_logger_write(ds_dispatch->dal_context->logger,
		LOG_MAJOR_INFO_PACKETS,
		LOG_MINOR_INFO_PACKETS_HDMI, "excolometry (%d) %s",
		info_frame.avi_info_packet.info_packet.bits.EC0_EC2,
		info_frame.avi_info_packet.info_packet.bits.EC0_EC2
			== 1 ? "xvYCC601" :
		info_frame.avi_info_packet.info_packet.bits.EC0_EC2
			== 2 ? "xvYCC709" : "not supported");

	/* Get aspect ratio from timing service */
	aspect = dal_timing_service_get_aspect_ratio_for_timing(ds_dispatch->ts,
			&mode->mode_timing->crtc_timing);

	switch (aspect) {
	case ASPECT_RATIO_4_3:
	case ASPECT_RATIO_16_9:
		info_frame.avi_info_packet.info_packet.bits.M0_M1 = aspect;
		break;

	case ASPECT_RATIO_NO_DATA:
	default:
		info_frame.avi_info_packet.info_packet.bits.M0_M1 = 0;
	}

	dal_logger_write(ds_dispatch->dal_context->logger,
		LOG_MAJOR_INFO_PACKETS,
		LOG_MINOR_INFO_PACKETS_HDMI, "aspect ratio (%d) %s",
		info_frame.avi_info_packet.info_packet.bits.M0_M1,
		info_frame.avi_info_packet.info_packet.bits.M0_M1 == 1 ?
				"4_3" :
		info_frame.avi_info_packet.info_packet.bits.M0_M1 == 2 ?
				"16_9" : "unknown");

	/* Active Format Aspect ratio - same as Picture Aspect Ratio. */
	info_frame.avi_info_packet.info_packet.bits.R0_R3 =
			ACTIVE_FORMAT_ASPECT_RATIO_SAME_AS_PICTURE;

	/* Check for ITC Flag and CNC content Value from AVI Info Packet
	 * the method always set the required values */
	build_itc_cn0_cn1_flags(adj_container, &itc, &cn0_cn1);

	if (itc) {
		info_frame.avi_info_packet.info_packet.bits.ITC = 1;
		info_frame.avi_info_packet.info_packet.bits.CN0_CN1 = cn0_cn1;
	}

	dal_logger_write(ds_dispatch->dal_context->logger,
		LOG_MAJOR_INFO_PACKETS,
		LOG_MINOR_INFO_PACKETS_HDMI, "Itc %d CNC %d",
		info_frame.avi_info_packet.info_packet.bits.ITC,
		info_frame.avi_info_packet.info_packet.bits.CN0_CN1);

	video_cap_support =
		dal_adj_container_get_cea_video_cap_data_block(
				adj_container, &video_cap);

	if (video_cap_support && video_cap.bits.QS == 1) {
		if (color_space == DS_COLOR_SPACE_SRGB_FULLRANGE)
			info_frame.avi_info_packet.info_packet.bits.Q0_Q1 =
					RGB_QUANTIZATION_FULL_RANGE;
		else if (color_space == DS_COLOR_SPACE_SRGB_LIMITEDRANGE)
			info_frame.avi_info_packet.info_packet.bits.Q0_Q1 =
					RGB_QUANTIZATION_LIMITED_RANGE;
		else
			info_frame.avi_info_packet.info_packet.bits.Q0_Q1 =
					RGB_QUANTIZATION_DEFAULT_RANGE;
	} else
		info_frame.avi_info_packet.info_packet.bits.Q0_Q1 =
				RGB_QUANTIZATION_DEFAULT_RANGE;

	/* TODO : We should handle YCC quantization,
	 * but we do not have matrix calculation */
	if (video_cap_support && video_cap.bits.QY == 1)
		info_frame.avi_info_packet.info_packet.bits.YQ0_YQ1 =
				RGB_QUANTIZATION_LIMITED_RANGE;
	else
		info_frame.avi_info_packet.info_packet.bits.YQ0_YQ1 =
				RGB_QUANTIZATION_LIMITED_RANGE;

	dal_logger_write(ds_dispatch->dal_context->logger,
		LOG_MAJOR_INFO_PACKETS,
		LOG_MINOR_INFO_PACKETS_HDMI, "RGB quantization %d %s",
		info_frame.avi_info_packet.info_packet.bits.Q0_Q1,
		info_frame.avi_info_packet.info_packet.bits.Q0_Q1 == 2 ?
				"full rgb" :
		info_frame.avi_info_packet.info_packet.bits.Q0_Q1 == 1 ?
				"lim rgb" : "default");

	dal_logger_write(ds_dispatch->dal_context->logger,
		LOG_MAJOR_INFO_PACKETS,
		LOG_MINOR_INFO_PACKETS_HDMI, "YCC quantization %d %s",
		info_frame.avi_info_packet.info_packet.bits.YQ0_YQ1,
		info_frame.avi_info_packet.info_packet.bits.YQ0_YQ1 ==
				0 ? "lim ycc" :
		info_frame.avi_info_packet.info_packet.bits.YQ0_YQ1 ==
				1 ? "full ycc" : "reserved");

	/* VIC */
	info_frame.avi_info_packet.info_packet.bits.VIC0_VIC7 =
			mode->mode_timing->crtc_timing.vic;

	dal_logger_write(ds_dispatch->dal_context->logger,
		LOG_MAJOR_INFO_PACKETS,
		LOG_MINOR_INFO_PACKETS_HDMI, "VIC0_VIC7 %d",
		info_frame.avi_info_packet.info_packet.bits.VIC0_VIC7);

	hw_pixel_repetition =
			mode->mode_timing->crtc_timing.flags.PIXEL_REPETITION
				== 0 ? 1 :
			mode->mode_timing->crtc_timing.flags.PIXEL_REPETITION;
	/* pixel repetition
	 * PR0 - PR3 start from 0 whereas pHwPathMode->mode.timing.flags.pixel
	 * repetition start from 1 */
	info_frame.avi_info_packet.info_packet.bits.PR0_PR3 =
			hw_pixel_repetition - 1;

	dal_logger_write(ds_dispatch->dal_context->logger,
		LOG_MAJOR_INFO_PACKETS,
		LOG_MINOR_INFO_PACKETS_HDMI, "Pixel repetition %d",
		info_frame.avi_info_packet.info_packet.bits.PR0_PR3);

	/* Bar Info
	 * barTop:    Line Number of End of Top Bar.
	 * barBottom: Line Number of Start of Bottom Bar.
	 * barLeft:   Pixel Number of End of Left Bar.
	 * barRight:  Pixel Number of Start of Right Bar. */
	info_frame.avi_info_packet.info_packet.bits.bar_top =
			(uint16_t) overscan.top;
	info_frame.avi_info_packet.info_packet.bits.bar_bottom =
			(uint16_t) (mode->mode_timing->crtc_timing.v_border_top
					- overscan.bottom + 1);
	info_frame.avi_info_packet.info_packet.bits.bar_left =
			(uint16_t) overscan.left;
	info_frame.avi_info_packet.info_packet.bits.bar_right =
			(uint16_t) (mode->mode_timing->crtc_timing.h_total
					- overscan.right + 1);

	dal_logger_write(ds_dispatch->dal_context->logger,
		LOG_MAJOR_INFO_PACKETS,
		LOG_MINOR_INFO_PACKETS_HDMI,
		"top %d, bottom %d, left %d, right %d",
		info_frame.avi_info_packet.info_packet.bits.bar_top,
		info_frame.avi_info_packet.info_packet.bits.bar_bottom,
		info_frame.avi_info_packet.info_packet.bits.bar_left,
		info_frame.avi_info_packet.info_packet.bits.bar_right);

	/* check_sum - Calculate AFMT_AVI_INFO0 ~ AFMT_AVI_INFO3 */
	check_sum =
		&info_frame.avi_info_packet.info_packet.packet_raw_data.sb[0];
	*check_sum = INFO_FRAME_AVI + INFO_FRAME_SIZE_AVI
			+ INFO_FRAME_VERSION_2;

	for (byte_index = 1; byte_index <= INFO_FRAME_SIZE_AVI; byte_index++)
		*check_sum += info_frame.avi_info_packet.info_packet.
				packet_raw_data.sb[byte_index];

	/* one byte complement */
	*check_sum = (uint8_t) (0x100 - *check_sum);

	dal_logger_write(ds_dispatch->dal_context->logger,
		LOG_MAJOR_INFO_PACKETS,
		LOG_MINOR_INFO_PACKETS_HDMI, "===check sum %x===",
		(uint32_t) *check_sum);

	/* Store in hw_path_mode */
	info_packet->hb0 =
		info_frame.avi_info_packet.info_packet.packet_raw_data.hb0;
	info_packet->hb1 =
		info_frame.avi_info_packet.info_packet.packet_raw_data.hb1;
	info_packet->hb2 =
		info_frame.avi_info_packet.info_packet.packet_raw_data.hb2;

	for (byte_index = 0; byte_index < sizeof(info_packet->sb); byte_index++)
		info_packet->sb[byte_index] = info_frame.avi_info_packet.
				info_packet.packet_raw_data.sb[byte_index];

	info_packet->valid = true;
}

/*
 * prepare_vendor_info_packet
 *
 * @brief
 * Build vendor info frame
 *
 * Currently only thing we put in VSIF, is 3D stereo support
 *
 * @param
 * mode - [in] path mode which contains data may need to construct VSIF
 * pPacket - [out] output structre where to store VSIF
 *
 * @return
 * void
 */
static void prepare_vendor_info_packet(const struct path_mode *mode,
		struct hw_info_packet *info_packet)
{
	uint32_t length = 0;
	bool hdmi_vic_mode = false;
	uint8_t checksum = 0;
	uint32_t i = 0;
	enum timing_3d_format format;

	ASSERT_CRITICAL(mode != NULL);
	ASSERT_CRITICAL(info_packet != NULL);

	format = dal_ds_translation_get_active_timing_3d_format(
			mode->mode_timing->crtc_timing.timing_3d_format,
			mode->view_3d_format);

	/* Can be different depending on packet content */
	length = 5;

	if (mode->mode_timing->crtc_timing.hdmi_vic != 0 &&
			mode->view.width >= 3840 &&
			mode->view.height == 2160)
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
		info_packet->sb[5] =
			(uint8_t) (mode->mode_timing->crtc_timing.hdmi_vic);

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

/*
 * prepare_default_gamut_packet
 *
 * @brief
 * Build default gamut packet
 *
 * @param
 * mode - [in] path mode that contains the gamut change flag
 *
 * @return
 * True if packet is buit. False otherwise.
 */
static bool prepare_default_gamut_packet(struct ds_dispatch *ds_dispatch,
		const struct path_mode *mode,
		struct hw_info_packet *info_packet)
{
	bool result = false;
	uint8_t base = 0; /* GBD profile 0 */
	uint32_t xgdb_color_precision = 0;
	uint32_t xgdb_color_space = 0;
	uint32_t xgdb_min_red_data = 0;
	uint32_t xgdb_max_red_data = 0;
	uint32_t xgdb_min_green_data = 0;
	uint32_t xgdb_max_green_data = 0;
	uint32_t xgdb_min_blue_data = 0;
	uint32_t xgdb_max_blue_data = 0;

	if (mode == NULL || mode->mode_timing == NULL)
		return result;

	if (!dal_ds_dispatch_is_gamut_change_required(
			ds_dispatch,
			mode->mode_timing->crtc_timing.pixel_encoding,
			mode->pixel_format, mode->display_path_index))
		return result;

	/* uint32_t xgdb_format_Flag = 1; */

	/* 0->8-bit precision
	* 1->10-bit precision
	* 2->12-bit precision */
	xgdb_color_precision = 2;
	xgdb_color_space = 2;
	xgdb_min_red_data = 0x84f;
	xgdb_max_red_data = 0x7ff;
	xgdb_min_green_data = 0x84f;
	xgdb_max_green_data = 0x7ff;
	xgdb_min_blue_data = 0x84f;
	xgdb_max_blue_data = 0x7ff;

	/* Header */
	info_packet->hb0 = 0x0A; /* Gamut packed type. */
	info_packet->hb1 = 0x81; /*  HDMI spec Rev1.3 page 78.
				<these settings are recommended by OEM.) */
	info_packet->hb2 = 0x31; /* page 78  //0x80 bit is for stop GBD use. */

	/* translate to Fixed point format first:
	* 1 signed bit, 2 bits integer, 9 bits fraction. */

	/* Min_Red_H */
	info_packet->sb[base + 1] |= (xgdb_min_red_data & 0xff0) >> 4;

	/* Min_Red_L|MaxRed_H */
	info_packet->sb[base + 2] |= (xgdb_min_red_data & 0x00f) << 4;
	info_packet->sb[base + 2] |= (xgdb_max_red_data & 0xf00) >> 8;
	info_packet->sb[base + 3] |= (xgdb_max_red_data & 0x0ff); /*Max_Red_L*/

	/* Min_Green_H */
	info_packet->sb[base + 4] |= (xgdb_min_green_data & 0xff0) >> 4;

	/* Min_Green_L|MaxGreen_H */
	info_packet->sb[base + 5] |= (xgdb_min_green_data & 0x00f) << 4;
	info_packet->sb[base + 5] |= (xgdb_max_green_data & 0xf00) >> 8;

	/* Max_Green_L */
	info_packet->sb[base + 6] |= (xgdb_max_green_data & 0x0ff);

	/* Min_Blue_H */
	info_packet->sb[base + 7] |= (xgdb_min_blue_data & 0xff0) >> 4;

	/* Min_Blue_L|MaxBlue_H */
	info_packet->sb[base + 8] |= (xgdb_min_blue_data & 0x00f) << 4;
	info_packet->sb[base + 8] |= (xgdb_max_blue_data & 0xf00) >> 8;

	/* Max_Blue_L */
	info_packet->sb[base + 9] |= (xgdb_max_blue_data & 0x0ff);

	info_packet->sb[base + 0] |= 0x80; /* RANGE FORMAT */
	info_packet->sb[base + 0] |= (xgdb_color_precision) << 0x3;
	info_packet->sb[base + 0] |= (xgdb_color_space & 0x3);

	info_packet->valid = true;
	result = true;

	return result;

}

/*
 * ds_prepare_video_stream_configuration_packet
 *
 * @brief
 * Build video stream configuration packet
 *
 * @param
 *
 * @return
 * void
 */
static void prepare_video_stream_configuration_packet(
	struct ds_dispatch *ds_dispatch,
	const struct path_mode *mode,
	struct hw_info_packet *info_packet)
{
	bool psr_panel_support = false;
	enum timing_3d_format format;

	/* Check for invalid input parameters */
	if (mode == NULL || info_packet == NULL) {
		dal_logger_write(ds_dispatch->dal_context->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_DISPLAY_SERVICE,
			"Invalid parameters");
		return;
	}

	/* Check if timing is 3D format. If so, VSC packet should be updated to
	 * indicate to Sink we are outputting 3D. */
	format = dal_ds_translation_get_active_timing_3d_format(
			mode->mode_timing->crtc_timing.timing_3d_format,
			mode->view_3d_format);

	/* Check if PSR is supported. If so, we need to enable and send the VSC
	 * packet to allow DMCU to update PSR specific fields. */
	if (dal_adapter_service_is_feature_supported(FEATURE_PSR_ENABLE)) {
		struct display_path *display_path =
				dal_tm_display_index_to_display_path(
						ds_dispatch->tm,
						mode->display_path_index);

		if ((display_path != NULL) &&
				(dal_display_path_is_psr_supported(
						display_path)))
			psr_panel_support = true;
	}

	/* Packet Header */
	/* Secondary data packet ID byte 0 = 0h */
	info_packet->hb0 = 0x00;
	/* Secondary data packet ID byte 0 = 07h */
	info_packet->hb1 = 0x07;

	if (psr_panel_support) {
		/* If PSR panel is supported, VSC packet header should be
		 * initialized with packet revision 2 set in byte 2 */
		info_packet->hb2 = 0x02;

		/* ===========================================================|
		 * PSR uses 7 Bytes. We need to set 8 Bytes here since 3D
		 * Stereo is occupying the first Byte in the VSC packet.
		 *
		 * ===========================================================|
		 * Byte 0 is set by driver for 3D Stereo purposes.
		 * -----------------------------------------------
		 *
		 *      Byte 0   - Used by 3D Stereo. See (VSC Payload (1 byte)
		 *                 From DP1.2 spec) below for usage.
		 *
		 * ===========================================================|
		 * Bytes 1-7 are set by DMCU for PSR purposes.
		 * However, driver must enable and send the VSC packet by
		 * setting up the packet header. DMCU will only fill in the PSR
		 * specific fields in the VSC packet and must not modify
		 * non-PSR related fields.
		 * ------------------------------------------------------------
		 *
		 *      Byte 1   - Used by DMCU to indicate action to panel.
		 *                      1. PSR active (bit 0 - 0x1)
		 *                      2. RFB update (bit 1 - 0x2)
		 *                      3. CRC update (bit 2 - 0x4)
		 *      Bytes 2-3- Used by DMCU for CRC by sending value of
		 *                 FMT_CRC_SIG_RED_GREEN:FMT_CRC_SIG_RED.
		 *      Bytes 4-5- Used by DMCU for CRC by sending value of
		 *                 FMT_CRC_SIG_RED_GREEN:FMT_CRC_SIG_GREEN.
		 *      Bytes 6-7- Used by DMCU for CRC by sending value of
		 *                 FMT_CRC_SIG_BLUE_CONTROL:FMT_CRC_SIG_BLUE.
		 *
		 * ==========================================================*/

		 /* Bits 4:0 = Number of valid data bytes = 08h,
		  * Bits 7:5 = RESERVED (all 0's)*/
		info_packet->hb3   = 0x08;
		info_packet->valid = true;
	} else {
		/* Bits 4:0 = Revision Number = 01h,
		 * Bits 7:5 = RESERVED (all 0's) */
		info_packet->hb2 = 0x01;
		/* Bits 4:0 = Number of valid data bytes = 01h
		 * Bits 7:5 = RESERVED (all 0's) */
		info_packet->hb3 = 0x01;
	}

	/* Set VSC data fields for 3D Stereo if supported */
	if (format != TIMING_3D_FORMAT_NONE) {
		/* VSC Payload (1 byte) From DP1.2 spec
		 *
		 * Bits 3:0                  | Bits 7:4
		 * (Stereo Interface         | (Stereo Interface
		 *  Method Code)             |  Method Specific Parameter)
		 * ------------------------------------------------------------
		 * 0 = Non Stereo Video      |  Must be set to 0x0
		 * ------------------------------------------------------------
		 * 1 = Frame/Field Sequential|  0x0: L + R view indication
		 *                           |       based on MISC1 bit 2:1
		 *                           |  0x1: Right when Stereo
		 *                           |       Signal = 1
		 *                           |  0x2: Left when Stereo
		 *                           |       Signal = 1
		 *                           |  (others reserved)
		 * ------------------------------------------------------------
		 * 2 = Stacked Frame         |  0x0: Left view is on top and
		 *                           |       right view on bottom
		 *                           |  (others reserved)
		 * ------------------------------------------------------------
		 * 3 = Pixel Interleaved     |  0x0: horiz interleaved, right
		 *                           |       view pixels on even lines
		 *                           |  0x1: horiz interleaved, right
		 *                           |       view pixels on odd lines
		 *                           |  0x2: checker board, start with
		 *                           |       left view pixel
		 *                           |  0x3: vertical interleaved,
		 *                           |       start w/ left view pixels
		 *                           |  0x4: vertical interleaved,
		 *                           |       start w/ right view pixels
		 *                           |  (others reserved)
		 * ------------------------------------------------------------
		 * 4 = Side-by-side          |  0x0: left half represents left
		 *                           |       eye view
		 *                           |  0x1: left half represents right
		 *                           |       eye view
		 * ==========================================================*/

		switch (format) {
		case TIMING_3D_FORMAT_HW_FRAME_PACKING:
		case TIMING_3D_FORMAT_SW_FRAME_PACKING:
			/* Stacked Frame */
			info_packet->sb[0] = 0x02;
			info_packet->valid = true;
			break;

		case TIMING_3D_FORMAT_DP_HDMI_INBAND_FA:
		case TIMING_3D_FORMAT_INBAND_FA:
			/* Frame/Field Sequential,
			 * L + R view indication based on MISC1 bit 2:1 */
			info_packet->sb[0] = 0x01;
			info_packet->valid = true;
			break;

		default:
			break;
		}
	}
}

/*
 * dal_ds_dispatch_setup_info_frame
 *
 * @brief
 * Set up info frames
 *
 * @param
 * path_mode *mode - [in] path mode
 * hw_path_mode *hw_mode - [out] HW path mode whose info frame would be set
 *
 * @return
 * void
 */
void dal_ds_dispatch_setup_info_frame(struct ds_dispatch *ds_dispatch,
		const struct path_mode *mode, struct hw_path_mode *hw_mode)
{
	enum signal_type signal = SIGNAL_TYPE_NONE;

	/* default all packets to invalid */
	hw_mode->info_frame.avi_info_packet.valid = false;
	hw_mode->info_frame.gamut_packet.valid = false;
	hw_mode->info_frame.vendor_info_packet.valid = false;
	hw_mode->info_frame.spd_packet.valid = false;
	hw_mode->info_frame.vsc_packet.valid = false;

	signal = dal_display_path_get_config_signal(hw_mode->display_path,
			SINK_LINK_INDEX);

	/* HDMi and DP have different info packets*/
	if (dal_is_hdmi_signal(signal)) {
		prepare_avi_info_frame(ds_dispatch, mode, hw_mode->display_path,
				hw_mode->mode.overscan,
				hw_mode->mode.underscan_rule,
				&hw_mode->info_frame.avi_info_packet);
		prepare_vendor_info_packet(mode,
				&hw_mode->info_frame.vendor_info_packet);
		prepare_default_gamut_packet(ds_dispatch, mode,
				&hw_mode->info_frame.gamut_packet);
	} else if (dal_is_dp_signal(signal))
		prepare_video_stream_configuration_packet(ds_dispatch,
				mode,
				&hw_mode->info_frame.vsc_packet);
}
