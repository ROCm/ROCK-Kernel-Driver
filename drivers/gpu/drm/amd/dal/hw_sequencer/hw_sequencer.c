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

#include "include/adapter_service_interface.h"
#include "include/asic_capability_interface.h"
#include "include/audio_interface.h"
#include "include/bios_parser_interface.h"
#include "include/bandwidth_manager_interface.h"
#include "include/connector_interface.h"
#include "include/dc_clock_generator_interface.h"
#include "include/dcs_interface.h"
#include "include/ddc_service_types.h"
#include "include/encoder_interface.h"
#include "include/logger_interface.h"
#include "include/signal_types.h"
#include "include/hw_sequencer_types.h"
#include "include/formatter_types.h"
#include "include/hw_adjustment_set.h"
#include "include/isr_config_types.h"
#include "include/line_buffer_interface.h"

#include "hw_sequencer.h"

#if defined(CONFIG_DRM_AMD_DAL_DCE11_0)
#include "dce110/hw_sequencer_dce110.h"
#endif

struct mpo_prototype_params {
	uint32_t window_start_x;
	uint32_t window_start_y;
	uint32_t window_width;
	uint32_t window_height;
	uint32_t overscan_left;
	uint32_t overscan_right;
	uint32_t overscan_top;
	uint32_t overscan_bottom;
	uint32_t viewport_x;
	uint32_t viewport_y;
	uint32_t viewport_width;
	uint32_t viewport_height;
};

static enum signal_type dal_get_signal(const struct hw_path_mode *path_mode)
{
	return dal_hw_sequencer_get_timing_adjusted_signal(
		path_mode,
		dal_display_path_get_config_signal(
			path_mode->display_path, SINK_LINK_INDEX));
}


uint32_t dal_hw_sequencer_translate_to_graphics_bpp(
	enum pixel_format pixel_format)
{
	switch (pixel_format) {
	case PIXEL_FORMAT_INDEX8:
		return 8;
	case PIXEL_FORMAT_RGB565:
		return 16;
	case PIXEL_FORMAT_ARGB8888:
		return 32;
	case PIXEL_FORMAT_ARGB2101010:
		return 32;
	case PIXEL_FORMAT_ARGB2101010_XRBIAS:
		return 32;
	case PIXEL_FORMAT_FP16:
		return 64;
	default:
		return 32;
	}
}

uint32_t dal_hw_sequencer_translate_to_backend_bpp(
	enum hw_overlay_backend_bpp backend_bpp)
{
	switch (backend_bpp) {
	case HW_OVERLAY_BACKEND_BPP32_FULL_BANDWIDTH:
		return 32;
	case HW_OVERLAY_BACKEND_BPP16_FULL_BANDWIDTH:
		return 16;
	case HW_OVERLAY_BACKEND_BPP32_HALF_BANDWIDTH:
		return 16;
	default:
		return 0;
	}
}

enum dc_deep_color_depth dal_hw_sequencer_translate_to_dec_deep_color_depth(
	enum hw_color_depth hw_color_depth)
{
	switch (hw_color_depth) {
	case HW_COLOR_DEPTH_101010:
		return DC_DEEP_COLOR_DEPTH_30;
	case HW_COLOR_DEPTH_121212:
		return DC_DEEP_COLOR_DEPTH_36;
	case HW_COLOR_DEPTH_161616:
		return DC_DEEP_COLOR_DEPTH_48;
	default:
		return DC_DEEP_COLOR_DEPTH_24;
	}
}
/* no used for now
static uint32_t get_validation_display_clock(
	struct hw_path_mode_set *set)
{
	struct hw_global_objects objs = { NULL };
	dal_hw_sequencer_get_global_objects(set, &objs);
	return dal_display_clock_get_validation_clock(objs.dc);
}

static enum scaler_validation_code validate_display_clock_for_scaling(
	struct display_path *display_path,
	struct min_clock_params *min_clock_params,
	struct scaler_validation_params *scaler_params,
	struct scaling_tap_info *tap_info)
{
	enum scaler_validation_code result = SCALER_VALIDATION_OK;
	struct controller *controller;
	struct display_clock *display_clock;
	ASSERT(display_path != NULL);
	ASSERT(min_clock_params != NULL);

	controller = dal_display_path_get_controller(display_path);
	display_clock = dal_controller_get_display_clock(controller);

	do {
		if (dal_display_clock_validate(
			display_clock, min_clock_params))
			break;

		result =
			dal_controller_get_next_lower_taps_number(
				controller,
				scaler_params,
				tap_info);

		if (result != SCALER_VALIDATION_OK) {
			BREAK_TO_DEBUGGER();
			break;
		}

		min_clock_params->scaling_info.v_taps = tap_info->v_taps;
		min_clock_params->scaling_info.h_taps = tap_info->h_taps;
	} while (result == SCALER_VALIDATION_OK);

	return result;
}
*/

static bool build_bit_depth_reduction_params(
	const struct hw_path_mode *path_mode,
	struct bit_depth_reduction_params *fmt_bit_depth)
{
	enum hw_color_depth color_depth =
		path_mode->mode.timing.flags.COLOR_DEPTH;
	enum hw_pixel_encoding pixel_encoding =
		path_mode->mode.timing.flags.PIXEL_ENCODING;

	/* For DPtoLVDS translators VBIOS sets FMT for LCD; VBIOS does not
	 * handle DPtoVGA case */
	if (SIGNAL_TYPE_DISPLAY_PORT ==
		dal_hw_sequencer_get_asic_signal(path_mode) &&
		SIGNAL_TYPE_LVDS == dal_get_signal(path_mode))
		return false;

	if (path_mode->mode.dithering == HW_DITHERING_OPTION_SKIP_PROGRAMMING)
		return false;

	/* dithering is disabled (usually due to restrictions) but programming
	 * (to disable it) is still required */
	if (path_mode->mode.dithering == HW_DITHERING_OPTION_DISABLE) {
		/* low level usually disables it if it gets all-zeros */
		/* but we will set disable bit just to indicate that output
		 * structure is changed */
		fmt_bit_depth->flags.TRUNCATE_ENABLED = 0;
		fmt_bit_depth->flags.SPATIAL_DITHER_ENABLED = 0;
		fmt_bit_depth->flags.FRAME_MODULATION_ENABLED = 0;
		return true;
	}

	/*TODO:
	if (pAdjustmentSet != NULL) {
		HWAdjustmentInterface* pBitDepthReductionAdjustment =
		pAdjustmentSet->GetAdjustmentById(
			HWAdjustmentId_BitDepthReduction);
		if (pBitDepthReductionAdjustment != NULL) {
		hw_bit_depth = pBitDepthReductionAdjustment->GetBitDepth();
		}
	}
	*/
	/* TODO: In order to apply the dithering from adjustment
	 * the above commented code should be used. Also in order
	 * to avoid translation from "hw_bit_depth_reduction" to
	 * "bit_depth_reduction_params", the adjustment code
	 * should use the struct "bit_depth_reduction_params"
	 * */


	/*TODO:if (hw_bit_depth == NULL) */
	/* apply spatial dithering */
	switch (color_depth) {
	case HW_COLOR_DEPTH_666:
		fmt_bit_depth->flags.SPATIAL_DITHER_DEPTH = 0;
		break;
	case HW_COLOR_DEPTH_888:
		fmt_bit_depth->flags.SPATIAL_DITHER_DEPTH = 1;
		break;
	case HW_COLOR_DEPTH_101010:
		/* bypass for 10-bit and 12-bit by default*/
		return true;
	case HW_COLOR_DEPTH_121212:
		/* bypass for 10-bit and 12-bit by default*/
		return true;
	default:
		/* unexpected case, skip programming dither*/
		return false;

	}
	fmt_bit_depth->flags.SPATIAL_DITHER_ENABLED = 1;
	/* frame random is on by default */
	fmt_bit_depth->flags.FRAME_RANDOM = 1;
	/* apply RGB dithering */
	fmt_bit_depth->flags.RGB_RANDOM =
		pixel_encoding == HW_PIXEL_ENCODING_RGB;

	/*TODO:else
	 * fmt_bit_depth = hw_bit_depth;
	 * return true;
	 */
	return true;
}

static bool setup_pixel_encoding(
	const struct hw_path_mode *path_mode,
	struct clamping_and_pixel_encoding_params *clamping)
{
	enum signal_type asic_signal =
		dal_hw_sequencer_get_asic_signal(path_mode);

	if (!clamping)
		return false;

	switch (asic_signal) {
	case SIGNAL_TYPE_LVDS:
	case SIGNAL_TYPE_EDP:
		/* pixel encoding programming will be done by VBIO's
		 * crtc_source_select()
		 * no Formatter programming is required
		 */
		break;
	default:
		/* set default clamping for specified pixel encoding */
		switch (path_mode->mode.timing.flags.PIXEL_ENCODING) {
		case HW_PIXEL_ENCODING_RGB:
			clamping->pixel_encoding = CNTL_PIXEL_ENCODING_RGB;
			return true;
		case HW_PIXEL_ENCODING_YCBCR422:
			clamping->pixel_encoding = CNTL_PIXEL_ENCODING_YCBCR422;
			return true;
		case HW_PIXEL_ENCODING_YCBCR444:
			clamping->pixel_encoding = CNTL_PIXEL_ENCODING_YCBCR444;
			return true;
		default:
			break;
		}
		break;
	}

	return false;
}

static enum color_depth translate_to_color_depth(
	enum hw_color_depth hw_color_depth)
{
	switch (hw_color_depth) {
	case HW_COLOR_DEPTH_101010:
		return COLOR_DEPTH_30;
	case HW_COLOR_DEPTH_121212:
		return COLOR_DEPTH_36;
	case HW_COLOR_DEPTH_161616:
		return COLOR_DEPTH_48;
	default:
		return COLOR_DEPTH_24;
	}
}

static enum color_space translate_to_color_space(
	enum hw_color_space hw_color_space)
{
	switch (hw_color_space) {
	case HW_COLOR_SPACE_UNKNOWN:
		return COLOR_SPACE_UNKNOWN;
	case HW_COLOR_SPACE_SRGB_FULL_RANGE:
		return COLOR_SPACE_SRGB_FULL_RANGE;
	case HW_COLOR_SPACE_SRGB_LIMITED_RANGE:
		return COLOR_SPACE_SRGB_LIMITED_RANGE;
	case HW_COLOR_SPACE_YPBPR601:
		return COLOR_SPACE_YPBPR601;
	case HW_COLOR_SPACE_YPBPR709:
		return COLOR_SPACE_YPBPR709;
	case HW_COLOR_SPACE_YCBCR601:
		return COLOR_SPACE_YCBCR601;
	case HW_COLOR_SPACE_YCBCR709:
		return COLOR_SPACE_YCBCR709;
	case HW_COLOR_SPACE_NMVPU_SUPERAA:
		return COLOR_SPACE_N_MVPU_SUPER_AA;
	default:
		return COLOR_SPACE_UNKNOWN;
	}
}

static enum display_output_bit_depth translate_to_display_output_bit_depth(
	enum hw_color_depth color_depth)
{
	switch (color_depth) {
	case HW_COLOR_DEPTH_666:
		return PANEL_6BIT_COLOR;
	case HW_COLOR_DEPTH_888:
		return PANEL_8BIT_COLOR;
	case HW_COLOR_DEPTH_101010:
		return PANEL_10BIT_COLOR;
	case HW_COLOR_DEPTH_121212:
		return PANEL_12BIT_COLOR;
	case HW_COLOR_DEPTH_161616:
		return PANEL_16BIT_COLOR;
	default:
		return PANEL_8BIT_COLOR;
	}
}

void build_encoder_context(
	struct display_path *dp,
	struct encoder *encoder,
	struct encoder_context *context)
{
	uint32_t i;
	struct connector_feature_support cfs;
	uint32_t links_number = dal_display_path_get_number_of_links(dp);

	ASSERT(dp != NULL);
	ASSERT(context != NULL);

	dal_connector_get_features(
		dal_display_path_get_connector(dp),
		&cfs);
	context->connector =
		dal_connector_get_graphics_object_id(
			dal_display_path_get_connector(dp));
	context->hpd_source = cfs.hpd_line;
	context->channel = cfs.ddc_line;
	context->engine = ENGINE_ID_UNKNOWN;
	context->signal = SIGNAL_TYPE_NONE;

	for (i = 0; i < links_number; i++) {
		if (dal_display_path_is_link_active(dp, i) &&
			context->engine == ENGINE_ID_UNKNOWN)
			context->engine =
				dal_display_path_get_stream_engine(dp, i);

		if (dal_display_path_get_upstream_encoder(dp, i) != encoder)
			continue;

		context->signal =
			dal_display_path_get_config_signal(dp, i);
		if (dal_display_path_get_stream_engine(dp, i) !=
			ENGINE_ID_UNKNOWN)
			context->engine =
				dal_display_path_get_stream_engine(dp, i);

		if (dal_display_path_get_downstream_encoder(dp, i) !=
			NULL)
			context->downstream =
				dal_encoder_get_graphics_object_id(
					dal_display_path_get_downstream_encoder(
						dp, i));
		else
			context->downstream =
				dal_connector_get_graphics_object_id(
					dal_display_path_get_connector(dp));
	}
}

static void update_coherent_adjustment(
	const struct hw_path_mode *path_mode,
	struct encoder_output *encoder_output)
{
	const struct hw_adjustment_value *value;

	if (!path_mode->adjustment_set)
		return;

	value = path_mode->adjustment_set->coherent;

	if (!value)
		return;

	/* update coherency mode (flag) in encoder output structure */
	encoder_output->flags.bits.COHERENT = value->ui_value == 1;
}

void update_hdmi_info(
	const struct hw_path_mode *path_mode,
	struct encoder_output *encoder_output)
{
	struct cea_vendor_specific_data_block cea_vendor_block = { 0 };

	if (encoder_output->ctx.signal == SIGNAL_TYPE_HDMI_TYPE_A) {
		if (dal_dcs_get_cea_vendor_specific_data_block(
			dal_display_path_get_dcs(path_mode->display_path),
			&cea_vendor_block)) {
			encoder_output->max_tmds_clk_from_edid_in_mhz =
				cea_vendor_block.max_tmds_clk_mhz;
		}
	}
}

void translate_info_frame(const struct hw_info_frame *hw_info_frame,
	struct encoder_info_frame *encoder_info_frame)
{
	dal_memset(encoder_info_frame, 0, sizeof(struct encoder_info_frame));

	/* For gamut we recalc checksum */
	if (hw_info_frame->gamut_packet.valid) {
		uint8_t chk_sum = 0;
		uint8_t *ptr;
		uint8_t i;

		dal_memmove(
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
		dal_memmove(
			&encoder_info_frame->avi,
			&hw_info_frame->avi_info_packet,
			sizeof(struct hw_info_packet));
	}

	if (hw_info_frame->vendor_info_packet.valid) {
		dal_memmove(
			&encoder_info_frame->vendor,
			&hw_info_frame->vendor_info_packet,
			sizeof(struct hw_info_packet));
	}

	if (hw_info_frame->spd_packet.valid) {
		dal_memmove(
			&encoder_info_frame->spd,
			&hw_info_frame->spd_packet,
			sizeof(struct hw_info_packet));
	}

	if (hw_info_frame->vsc_packet.valid) {
		dal_memmove(
			&encoder_info_frame->vsc,
			&hw_info_frame->vsc_packet,
			sizeof(struct hw_info_packet));
	}
}

static void build_encoder_output(
	const struct hw_path_mode *path_mode,
	enum build_option option,
	struct encoder_output *encoder_output)
{
	uint32_t link_idx = 0;
	struct display_path *display_path;
	struct encoder *upstream_encoder;
	struct encoder *downstream_encoder;
	struct display_sink_capability sink_capability = {
		DISPLAY_DONGLE_NONE };
	union dcs_monitor_patch_flags patch_flags;

	if (!path_mode || !encoder_output)
		return;

	display_path = path_mode->display_path;
	upstream_encoder =
		dal_display_path_get_upstream_encoder(display_path, link_idx);
	downstream_encoder =
		dal_display_path_get_downstream_encoder(display_path, link_idx);

	if (!downstream_encoder)
		downstream_encoder = upstream_encoder;

	/* TODO: do not sure if this logic is correct */
	if (!upstream_encoder || !downstream_encoder) {
		BREAK_TO_DEBUGGER();
		/* Failed to obtain encoder */
		return;
	}

	/* get sink capability for alternate scrambler */
	dal_dcs_get_sink_capability(
		dal_display_path_get_dcs(path_mode->display_path),
		&sink_capability);

	/* get dp receiver workarounds */
	patch_flags = dal_dcs_get_monitor_patch_flags(
			dal_display_path_get_dcs(path_mode->display_path));

	/* fill encoder output parameters */
	dal_memset(encoder_output, 0, sizeof(struct encoder_output));
	encoder_output->crtc_timing = path_mode->mode.timing;
	encoder_output->clock_source =
		dal_clock_source_get_id(
			dal_display_path_get_clock_source(
				path_mode->display_path));
	encoder_output->controller =
		dal_controller_get_id(
			dal_display_path_get_controller(
				path_mode->display_path));
	encoder_output->max_hdmi_pixel_clock =
		sink_capability.max_hdmi_pixel_clock;
	encoder_output->max_hdmi_deep_color =
		sink_capability.max_hdmi_deep_color;
	encoder_output->flags.bits.KEEP_RECEIVER_POWERED =
		patch_flags.flags.KEEP_DP_RECEIVER_POWERED;
	encoder_output->flags.bits.ENABLE_AUDIO =
		dal_display_path_get_audio(path_mode->display_path, link_idx) !=
			NULL;
	encoder_output->flags.bits.COHERENT = false;
	encoder_output->flags.bits.DELAY_AFTER_PIXEL_FORMAT_CHANGE =
		patch_flags.flags.DELAY_AFTER_PIXEL_FORMAT_CHANGE;
	encoder_output->flags.bits.VID_STREAM_DIFFER_TO_SYNC =
		patch_flags.flags.VID_STREAM_DIFFER_TO_SYNC;
	encoder_output->flags.bits.TURN_OFF_VCC =
		path_mode->action_flags.TURN_OFF_VCC;

	if (patch_flags.flags.DELAY_AFTER_DP_RECEIVER_POWER_UP) {
		encoder_output->delay_after_dp_receiver_power_up =
		dal_dcs_get_monitor_patch_info(
			dal_display_path_get_dcs(
				path_mode->display_path),
			MONITOR_PATCH_TYPE_DELAY_AFTER_DP_RECEIVER_POWER_UP)->
		param;

	}

	if (patch_flags.flags.DELAY_AFTER_PIXEL_FORMAT_CHANGE) {
		encoder_output->delay_after_pixel_format_change =
		dal_dcs_get_monitor_patch_info(
			dal_display_path_get_dcs(
				path_mode->display_path),
			MONITOR_PATCH_TYPE_DELAY_AFTER_PIXEL_FORMAT_CHANGE)->
		param;
	}

	/* Build encoder (upstream or downstream) context. We may need to adjust
	 * signal based on timing */
	switch (option) {
	case BUILD_OPTION_SET_MODE:
	case BUILD_OPTION_ENABLE_UPSTREAM:
	case BUILD_OPTION_STATIC_VALIDATE_UPSTREAM:
		build_encoder_context(path_mode->display_path, upstream_encoder,
			&encoder_output->ctx);
		encoder_output->ctx.signal =
			dal_hw_sequencer_get_timing_adjusted_signal(
				path_mode,
				encoder_output->ctx.signal);
		break;

	case BUILD_OPTION_SET_MODE2:
	case BUILD_OPTION_ENABLE_DOWNSTREAM:
	case BUILD_OPTION_STATIC_VALIDATE_DOWNSTREAM:
		build_encoder_context(
			path_mode->display_path,
			downstream_encoder,
			&encoder_output->ctx);
		encoder_output->ctx.signal =
			dal_hw_sequencer_get_timing_adjusted_signal(
				path_mode,
				encoder_output->ctx.signal);
		break;

	case BUILD_OPTION_DISABLE:
		build_encoder_context(path_mode->display_path, upstream_encoder,
			&encoder_output->ctx);
		break;

	case BUILD_OPTION_DISABLE2:
		build_encoder_context(
			path_mode->display_path,
			downstream_encoder,
			&encoder_output->ctx);
		break;

	default:
		build_encoder_context(path_mode->display_path, upstream_encoder,
			&encoder_output->ctx);
		break;
	}

	/* coherence mode override */
	update_coherent_adjustment(path_mode, encoder_output);

	/* get preferred settings */
	switch (option) {
	case BUILD_OPTION_SET_MODE:
	case BUILD_OPTION_ENABLE_UPSTREAM:
	case BUILD_OPTION_SET_MODE2:
	case BUILD_OPTION_ENABLE_DOWNSTREAM:
	case BUILD_OPTION_STATIC_VALIDATE_UPSTREAM:
	case BUILD_OPTION_STATIC_VALIDATE_DOWNSTREAM:
		/* HDMI information */
		update_hdmi_info(path_mode, encoder_output);
		/* Info Frame */
		translate_info_frame(&path_mode->info_frame,
			&encoder_output->info_frame);
		break;

	default:
		break;
	}
}

static struct audio_info *build_audio_info(
	struct hw_sequencer *hws,
	const struct hw_path_mode *path_mode)
{
	struct audio_info *audio_info = NULL;
	enum signal_type asic_signal =
		dal_hw_sequencer_get_asic_signal(path_mode);
	struct dcs *dcs =
		dal_display_path_get_dcs(
			path_mode->display_path);
	const struct dcs_cea_audio_mode_list *audio_modes =
		dal_dcs_get_cea_audio_modes(dcs, asic_signal);
	union cea_speaker_allocation_data_block cea_speaker_allocation = {
		{ 0 } };
	struct cea_vendor_specific_data_block cea_vendor_block = { 0 };
	bool speakers;
	uint32_t am_index;
	uint32_t am_count;
	uint32_t ai_size;
	struct dcs_container_id container_id = { { 0 } };

	if (audio_modes == NULL)
		return NULL;

	am_count = dal_dcs_cea_audio_mode_list_get_count(audio_modes);

	if (am_count == 0)
		return NULL;

	ai_size = sizeof(struct audio_info) + (am_count - 1) *
		sizeof(struct audio_mode);

	/* allocate array for audio modes */
	audio_info = dal_alloc(ai_size);
	if (!audio_info)
		return NULL;

	/* set the display path index */
	audio_info->display_index =
		dal_display_path_get_display_index(
			path_mode->display_path);

	/* set the display name */
	dal_dcs_get_display_name(
		dcs,
		audio_info->display_name,
		AUDIO_INFO_DISPLAY_NAME_SIZE_IN_CHARS);

	/* get speaker allocation */
	speakers =
		dal_dcs_get_cea_speaker_allocation_data_block(
			dcs,
			asic_signal,
			&cea_speaker_allocation);
	if (speakers) {
		/* speaker allocation information is in the first byte */
		audio_info->flags.speaker_flags =
			*(struct audio_speaker_flags *)
			&cea_speaker_allocation.raw;
	}
	if (dal_dcs_get_cea_vendor_specific_data_block(
		dcs, &cea_vendor_block)) {
		audio_info->flags.info.SUPPORT_AI =
			cea_vendor_block.byte6.SUPPORTS_AI;

		if (cea_vendor_block.byte8.LATENCY_FIELDS_PRESENT) {
			audio_info->video_latency =
				cea_vendor_block.video_latency;
			audio_info->audio_latency =
				cea_vendor_block.audio_latency;
		}
		if (cea_vendor_block.byte8.ILATENCY_FIELDS_PRESENT
			&& path_mode->mode.timing.flags.INTERLACED) {
			audio_info->video_latency =
				cea_vendor_block.i_video_latency;
			audio_info->audio_latency =
				cea_vendor_block.i_audio_latency;
		}
	}

	/* Needed for DP1.2 and DP A/V Sync */
	if (dal_is_dp_external_signal(asic_signal)) {
		struct display_sink_capability sink_capability = {
			DISPLAY_DONGLE_NONE };
		dal_dcs_get_sink_capability(
			dcs, &sink_capability);

		/* convert to LIPSYNC format ms/2 + 1 (= us/2000 + 1) */
		audio_info->audio_latency =
			(sink_capability.audio_latency / 2000)
			+ 1;
		if (path_mode->mode.timing.flags.INTERLACED) {
			audio_info->video_latency =
				(sink_capability.video_latency_interlace
					/ 2000) + 1;
		} else {
			audio_info->video_latency =
				(sink_capability.video_latency_progressive
					/ 2000) + 1;
		}
	}

	audio_info->mode_count = am_count;
	/* translate modes */
	for (am_index = 0; am_index < am_count; am_index++) {
		const struct cea_audio_mode *audio_mode =
			dal_dcs_cea_audio_mode_list_at_index(
				audio_modes,
				am_index);
		audio_info->modes[am_index].format_code =
			(enum audio_format_code)(audio_mode->format_code);
		audio_info->modes[am_index].channel_count =
			audio_mode->channel_count;
		audio_info->modes[am_index].sample_rates.all =
			audio_mode->sample_rate;
		audio_info->modes[am_index].max_bit_rate =
			audio_mode->max_bit_rate;
	}

	if (dal_dcs_get_container_id(dcs, &container_id)) {
		audio_info->manufacture_id =
			container_id.manufacturer_name;
		audio_info->product_id = container_id.product_code;
		audio_info->port_id[0] = container_id.port_id[0];
		audio_info->port_id[1] = container_id.port_id[1];
	}

	if (container_id.product_code == 0 &&
		container_id.manufacturer_name == 0) {
		struct vendor_product_id_info vendor_product_id = { 0 };
		struct bdf_info bdf_info =
			dal_adapter_service_get_adapter_info(hws->as);

		if (dal_dcs_get_vendor_product_id_info(
			dcs,
			&vendor_product_id)) {
			audio_info->manufacture_id =
				vendor_product_id.manufacturer_id;
			audio_info->product_id =
				vendor_product_id.product_id;
		}

		audio_info->port_id[0] =
			audio_info->display_index;
		audio_info->port_id[1] = (bdf_info.BUS_NUMBER << 8
			| bdf_info.DEVICE_NUMBER << 3
			| bdf_info.FUNCTION_NUMBER);
	}

	return audio_info;
}

static void build_adjustment_scaler_params(
	struct hw_adjustment_set *adjustment_set,
	struct sharpness_adjustment *sharpness,
	struct adjustment_factor *scale_ratio_hp_factor,
	struct adjustment_factor *scale_ratio_lp_factor)
{
	const struct hw_adjustment_deflicker *value;

	if (!adjustment_set)
		goto default_values;

	value = adjustment_set->deflicker_filter;

	if (value == NULL)
		goto default_values;

	scale_ratio_hp_factor->adjust = value->hp_factor;
	scale_ratio_hp_factor->divider = value->hp_divider;

	scale_ratio_lp_factor->adjust = value->lp_factor;
	scale_ratio_lp_factor->divider = value->lp_divider;

	sharpness->sharpness = value->sharpness;
	sharpness->enable_sharpening = value->enable_sharpening;

default_values:
	scale_ratio_hp_factor->adjust = 71;
	scale_ratio_hp_factor->divider = 100;
	scale_ratio_lp_factor->adjust = 99;
	scale_ratio_lp_factor->divider = 100;
	sharpness->sharpness = 0;
	sharpness->enable_sharpening = false;
}

void dal_hw_sequencer_build_scaler_parameter_plane(
	const struct plane_config *plane_config,
	const struct scaling_tap_info *taps,
	struct scaler_data *scaler_data,
	bool program_viewport,
	bool program_alpha,
	bool unlock)
{
	/*TODO: get from feature from adapterservice*/
	scaler_data->flags.bits.SHOW_COLOURED_BORDER = false;

	scaler_data->flags.bits.SHOULD_PROGRAM_ALPHA = 0;
	if (program_alpha)
		scaler_data->flags.bits.SHOULD_PROGRAM_ALPHA = 1;

	scaler_data->flags.bits.SHOULD_PROGRAM_VIEWPORT = 0;
	if (program_viewport)
		scaler_data->flags.bits.SHOULD_PROGRAM_VIEWPORT = 1;

	scaler_data->flags.bits.SHOULD_UNLOCK = 0;
	if (unlock)
		scaler_data->flags.bits.SHOULD_UNLOCK = 1;

	scaler_data->flags.bits.INTERLACED = 0;
	if (plane_config->attributes.video_scan_format !=
			PLANE_VID_SCAN_FMT_PROGRESSIVE)
		scaler_data->flags.bits.INTERLACED = 1;

	scaler_data->dal_pixel_format = plane_config->config.dal_pixel_format;

	scaler_data->taps.h_taps = taps->h_taps;
	scaler_data->taps.v_taps = taps->v_taps;
	scaler_data->taps.h_taps_c = taps->h_taps_c;
	scaler_data->taps.v_taps_c = taps->v_taps_c;
	/*hard code for bigbunny play  hard code 2 TAPS to use auto calc coeff
	 *TODO: find final solution */
	if (scaler_data->dal_pixel_format == PIXEL_FORMAT_420BPP12) {
		scaler_data->taps.h_taps_c = 2;
		scaler_data->taps.v_taps_c = 2;
	}

	scaler_data->viewport.x = plane_config->mp_scaling_data.viewport.x;
	scaler_data->viewport.y = plane_config->mp_scaling_data.viewport.y;
	scaler_data->viewport.width =
			plane_config->mp_scaling_data.viewport.width;
	scaler_data->viewport.height =
			plane_config->mp_scaling_data.viewport.height;

	if (scaler_data->viewport.width == 0 ||
			scaler_data->viewport.height == 0) {
		scaler_data->viewport.height =
			(scaler_data->hw_crtc_timing->v_addressable + 1) & ~1;
		scaler_data->viewport.width =
			scaler_data->hw_crtc_timing->h_addressable;
	}

	scaler_data->dst_res = plane_config->mp_scaling_data.dst_res;

	scaler_data->overscan.left =
			plane_config->mp_scaling_data.overscan.left;
	scaler_data->overscan.right =
			plane_config->mp_scaling_data.overscan.right;
	scaler_data->overscan.top =
			plane_config->mp_scaling_data.overscan.top;
	scaler_data->overscan.bottom =
			plane_config->mp_scaling_data.overscan.bottom;

	/*TODO rotation and adjustment */
	scaler_data->h_sharpness = 0;
	scaler_data->v_sharpness = 0;

	scaler_data->ratios = &plane_config->mp_scaling_data.ratios;

}

void dal_hw_sequencer_build_scaler_parameter(
	const struct hw_path_mode *path_mode,
	const struct scaling_tap_info *taps,
	bool build_timing_required,
	struct scaler_data *scaler_data)
{
	scaler_data->src_res.width =
		path_mode->mode.scaling_info.src.width;
	scaler_data->src_res.height =
		path_mode->mode.scaling_info.src.height;
	scaler_data->dst_res.width =
		path_mode->mode.scaling_info.dst.width;
	scaler_data->dst_res.height =
		path_mode->mode.scaling_info.dst.height;

	scaler_data->pixel_type =
		path_mode->mode.timing.flags.PIXEL_ENCODING ==
			HW_PIXEL_ENCODING_YCBCR422 ?
			PIXEL_TYPE_20BPP : PIXEL_TYPE_30BPP;
	scaler_data->flags.bits.INTERLACED =
		path_mode->mode.timing.flags.INTERLACED;
	scaler_data->flags.bits.DOUBLE_SCAN_MODE =
		path_mode->mode.timing.flags.DOUBLESCAN;
	scaler_data->flags.bits.PIPE_LOCK_REQ  = 1;

	scaler_data->overscan.left = path_mode->mode.overscan.left;
	scaler_data->overscan.right = path_mode->mode.overscan.right;
	scaler_data->overscan.top = path_mode->mode.overscan.top;
	scaler_data->overscan.bottom = path_mode->mode.overscan.bottom;

	scaler_data->taps = *taps;

	if (path_mode->mode.color_space == HW_COLOR_SPACE_SRGB_FULL_RANGE ||
		path_mode->mode.color_space ==
			HW_COLOR_SPACE_SRGB_LIMITED_RANGE)
		scaler_data->flags.bits.RGB_COLOR_SPACE = 1;
	else
		scaler_data->flags.bits.RGB_COLOR_SPACE = 0;

	build_adjustment_scaler_params(path_mode->adjustment_set,
		&scaler_data->sharp_gain,
		&scaler_data->scale_ratio_hp_factor,
		&scaler_data->scale_ratio_lp_factor);

	scaler_data->h_sharpness = 0;
	scaler_data->v_sharpness = 0;

	if (build_timing_required)
		scaler_data->hw_crtc_timing = &path_mode->mode.timing;
}

bool dal_hw_sequencer_enable_line_buffer_power_gating(
	struct line_buffer *lb,
	enum controller_id id,
	enum pixel_type pixel_type,
	uint32_t src_pixel_width,
	uint32_t dst_pixel_width,
	struct scaling_tap_info *taps,
	enum lb_pixel_depth lb_depth,
	uint32_t src_height,
	uint32_t dst_height,
	bool interlaced)
{
	struct lb_config_data lb_config_data;

	if (!lb)
		return false;

	dal_memset(&lb_config_data, 0, sizeof(struct lb_config_data));

	lb_config_data.src_pixel_width = src_pixel_width;
	lb_config_data.dst_pixel_width = dst_pixel_width;
	lb_config_data.taps = *taps;
	/* this parameter is meaningful for DCE8 and up */
	lb_config_data.depth = lb_depth;
	/* this parameter is meaningful for DCE8 and up */
	lb_config_data.src_height = src_height;
	/* this parameter is meaningful for DCE8 and up */
	lb_config_data.dst_height = dst_height;
	/* this parameter is meaningful for DCE8 and up */
	lb_config_data.interlaced = interlaced;

	return dal_line_buffer_enable_power_gating(lb, id, &lb_config_data);
}

static enum hwss_result reset_path_mode_back_end(
	struct hw_sequencer *hws,
	struct hw_path_mode_set *set,
	uint32_t path_id);

static enum hwss_result set_path_mode_back_end(
	struct hw_sequencer *hws,
	struct hw_path_mode_set *path_set,
	uint32_t path_id,
	struct hwss_build_params *build_params);

void dal_hw_sequencer_update_info_frame(
	const struct hw_path_mode *hw_path_mode)
{
	struct encoder *enc;
	struct encoder_info_frame_param encoder_info_frame_param = {
			{ { 0 } } };

	ASSERT(hw_path_mode != NULL);

	enc = dal_display_path_get_upstream_encoder(
			hw_path_mode->display_path,
			ASIC_LINK_INDEX);

	build_encoder_context(
				hw_path_mode->display_path,
				enc,
				&encoder_info_frame_param.ctx);

	translate_info_frame(
			&hw_path_mode->info_frame,
			&encoder_info_frame_param.packets);

	dal_encoder_update_info_frame(enc, &encoder_info_frame_param);
}


static void enable_hpd(
		const struct hw_sequencer *hws,
		struct display_path *display_path)
{
	struct encoder_context encoder_context;
	struct display_path_objects disp_path_obj;

	dal_hw_sequencer_get_objects(display_path, &disp_path_obj);
	build_encoder_context(
			display_path,
			disp_path_obj.upstream_encoder,
			&encoder_context);

	/* call encoder on GPU to enable master lock of HPD */
	dal_encoder_enable_hpd(disp_path_obj.upstream_encoder,
		&encoder_context);
}

static bool has_travis_or_nutmeg_encoder(
	struct display_path *display_path)
{
	bool has_travis_crt;
	bool has_travis_lcd;
	bool has_nutmeg_crt;

	/* For Travis, EnumId_1 is crt and EnumId_2 is lcd */
	/* For Nutmeg, EnumId_1 is crt */

	if (display_path == NULL)
		return false;

	has_travis_crt = dal_display_path_contains_object(
		display_path,
		dal_graphics_object_id_init(
			ENCODER_ID_EXTERNAL_TRAVIS,
			ENUM_ID_1,
			OBJECT_TYPE_ENCODER));

	has_travis_lcd = dal_display_path_contains_object(
		display_path,
		dal_graphics_object_id_init(
			ENCODER_ID_EXTERNAL_TRAVIS,
			ENUM_ID_2,
			OBJECT_TYPE_ENCODER));

	has_nutmeg_crt = dal_display_path_contains_object(
		display_path,
		dal_graphics_object_id_init(
			ENCODER_ID_EXTERNAL_NUTMEG,
			ENUM_ID_1,
			OBJECT_TYPE_ENCODER));

	return has_travis_crt || has_travis_lcd || has_nutmeg_crt;
}

static void update_coherent_overide(
	struct hw_sequencer *hws,
	const struct hw_path_mode *hw_path_mode,
	struct encoder_output *encoder_output)
{
	/*TODO to be implemented*/
}

static void build_upstream_encoder_output(
	struct hw_sequencer *hws,
	uint32_t link_idx,
	const struct hw_path_mode *hw_path_mode,
	const struct link_settings *link_settings,
	enum build_option build_option,
	struct encoder_output *encoder_output)
{
	struct dcs *dcs;
	struct encoder *enc;
	union dcs_monitor_patch_flags patch_flags;

	if (hw_path_mode == NULL || encoder_output == NULL)
		return;
	dcs = dal_display_path_get_dcs(hw_path_mode->display_path);
	enc = dal_display_path_get_upstream_encoder(
				hw_path_mode->display_path,
				link_idx);

	if (enc == NULL || dcs == NULL) {
		BREAK_TO_DEBUGGER();
		dal_logger_write(hws->dal_context->logger,
			LOG_MAJOR_WARNING,
			LOG_MINOR_COMPONENT_HWSS,
			"%s: Failed to obtain encoder or dcs", __func__);
		return;
	}

	/* get DP receiver workarounds */
	patch_flags =
			dal_dcs_get_monitor_patch_flags(dcs);

	/* fill encoder output parameters */
	dal_memset(encoder_output, 0, sizeof(struct encoder_output));
	dal_memmove(
			&encoder_output->crtc_timing,
			&hw_path_mode->mode.timing,
			sizeof(struct hw_crtc_timing));

	encoder_output->clock_source = dal_clock_source_get_id(
			dal_display_path_get_clock_source(
						hw_path_mode->display_path));

	encoder_output->flags.bits.KEEP_RECEIVER_POWERED =
			patch_flags.flags.KEEP_DP_RECEIVER_POWERED;
	encoder_output->flags.bits.ENABLE_AUDIO =
			(dal_display_path_get_audio
				(hw_path_mode->display_path, link_idx) != NULL);

	encoder_output->flags.bits.COHERENT = false;
	/*default coherent is false*/

	encoder_output->flags.bits.DELAY_AFTER_PIXEL_FORMAT_CHANGE =
			patch_flags.flags.DELAY_AFTER_PIXEL_FORMAT_CHANGE;

	encoder_output->flags.bits.TURN_OFF_VCC =
			hw_path_mode->action_flags.TURN_OFF_VCC;

	if (patch_flags.flags.DELAY_AFTER_DP_RECEIVER_POWER_UP) {
		const struct monitor_patch_info *info =
			dal_dcs_get_monitor_patch_info(dcs,
			MONITOR_PATCH_TYPE_DELAY_AFTER_DP_RECEIVER_POWER_UP);
		encoder_output->delay_after_dp_receiver_power_up =
			info->param;
	}

	if (patch_flags.flags.DELAY_AFTER_PIXEL_FORMAT_CHANGE) {
		const struct monitor_patch_info *info =
			dal_dcs_get_monitor_patch_info(dcs,
			MONITOR_PATCH_TYPE_DELAY_AFTER_PIXEL_FORMAT_CHANGE);
		encoder_output->delay_after_pixel_format_change =
			info->param;
	}

	build_encoder_context(hw_path_mode->display_path, enc,
			&encoder_output->ctx);

	/* Build encoder (upstream or downstream) context.
	 * May need to adjust signal based on timing */

	switch (build_option) {
	case BUILD_OPTION_SET_MODE:
	case BUILD_OPTION_ENABLE_UPSTREAM:
	case BUILD_OPTION_STATIC_VALIDATE_UPSTREAM:
		encoder_output->ctx.signal =
			dal_hw_sequencer_get_timing_adjusted_signal(
				hw_path_mode, encoder_output->ctx.signal);
		break;

	case BUILD_OPTION_DISABLE:
		break;

	default:
		BREAK_TO_DEBUGGER();
		break;
	}

	/* coherence mode override */
	update_coherent_overide(hws, hw_path_mode, encoder_output);

	/* get preferred settings */
	switch (build_option) {
	case BUILD_OPTION_SET_MODE:
	case BUILD_OPTION_ENABLE_UPSTREAM:
	case BUILD_OPTION_STATIC_VALIDATE_UPSTREAM:
		update_hdmi_info(hw_path_mode, encoder_output);
		translate_info_frame(&hw_path_mode->info_frame,
				&encoder_output->info_frame);

		/* Query and update link capabilities */
		dal_memmove(
				&encoder_output->link_settings,
				link_settings,
				sizeof(struct link_settings));
		break;

	case BUILD_OPTION_DISABLE:
		break;

	default:
		BREAK_TO_DEBUGGER();
		break;
	}

	/* set SS parameters */
	encoder_output->link_settings.link_spread = (
			dal_display_path_is_ss_supported(
				hw_path_mode->display_path) ?
					LINK_SPREAD_05_DOWNSPREAD_30KHZ :
					LINK_SPREAD_DISABLED);
}

/**
 * Validate video memory bandwidth with the default (i.e. highest) display
 * engine clock value.
 */
static bool validate_video_memory_bandwidth(
		struct controller *controller,
		uint32_t param_count,
		struct bandwidth_params *bandwidth_params)
{
	uint32_t display_clock_in_khz;
	struct display_clock *display_clock;
	struct bandwidth_manager *bandwidth_manager;

	display_clock = dal_controller_get_display_clock(controller);

	/* Get the DCE-specific default (highest) validation clock. */
	display_clock_in_khz = dal_display_clock_get_validation_clock(
			display_clock);

	bandwidth_manager = dal_controller_get_bandwidth_manager(controller);

	return dal_bandwidth_manager_validate_video_memory_bandwidth(
			bandwidth_manager,
			param_count,
			bandwidth_params,
			display_clock_in_khz);
}

void dal_hw_sequencer_enable_audio_endpoint(
	struct hw_sequencer *hws,
	struct link_settings *ls,
	struct display_path *display_path,
	bool enable)
{
	struct audio *audio;

	if (!display_path)
		return;

	audio = dal_display_path_get_audio(display_path, ASIC_LINK_INDEX);

	if (!audio)
		return;

	{
		enum engine_id e_id =
			dal_hw_sequencer_get_engine_id(display_path);
		enum signal_type asic_signal =
			dal_display_path_get_config_signal(
				display_path,
				ASIC_LINK_INDEX);

		if (enable) {
			dal_audio_enable_output(
				audio,
				e_id,
				asic_signal,
				ls->link_rate);
			dal_audio_enable_azalia_audio_jack_presence(
				audio,
				e_id);
		} else
			dal_audio_disable_output(
				audio,
				e_id,
				asic_signal);
	}

}

void dal_hw_sequencer_mute_audio_endpoint(
	struct hw_sequencer *hws,
	struct display_path *display_path,
	bool mute)
{
	struct audio *audio;
	struct encoder_context context = { 0 };
	struct encoder *enc;

	if (display_path == NULL) {
		BREAK_TO_DEBUGGER();
		return;
	}

	audio = dal_display_path_get_audio(
			display_path,
			ASIC_LINK_INDEX);

	if (audio == NULL)
		return;

	enc = dal_display_path_get_upstream_encoder(
		display_path,
		ASIC_LINK_INDEX);
	ASSERT(enc != NULL);

	build_encoder_context(display_path, enc, &context);

	if (mute)
		dal_audio_mute(audio,
				context.engine, context.signal);
	else
		dal_audio_unmute(audio,
				context.engine, context.signal);
}

enum hwss_result dal_hw_sequencer_reset_audio_device(
	struct hw_sequencer *hws,
	struct display_path *display_path)
{
	struct display_path_objects disp_path_obj;
	enum engine_id engine_id =
		dal_hw_sequencer_get_engine_id(display_path);
	enum signal_type asic_signal =
			dal_display_path_get_config_signal(
				display_path, ASIC_LINK_INDEX);
	dal_hw_sequencer_get_objects(display_path, &disp_path_obj);

	if (disp_path_obj.audio != NULL) {
		struct audio_channel_associate_info audio_mapping;

		audio_mapping.u32all = 0;

		dal_audio_enable_channel_splitting_mapping(
				disp_path_obj.audio,
				engine_id,
				asic_signal,
				&audio_mapping,
				false);

		if (AUDIO_RESULT_OK == dal_audio_disable_output(
				disp_path_obj.audio,
				engine_id,
				asic_signal))
			return HWSS_RESULT_OK;

	}
	return HWSS_RESULT_ERROR;
}

bool dal_hw_sequencer_has_audio_bandwidth_changed(
	struct hw_sequencer *hws,
	const struct hw_path_mode *old,
	const struct hw_path_mode *new)
{
	/* TODO implement */

	return true;
}

void dal_hw_sequencer_enable_azalia_audio_jack_presence(
	struct hw_sequencer *hws,
	struct display_path *display_path)
{
	/* enable audio only when set mode.*/
	struct audio *audio = dal_display_path_get_audio(
		display_path, ASIC_LINK_INDEX);

	if (audio) {
		enum engine_id engine_id =
			dal_hw_sequencer_get_engine_id(display_path);

		dal_audio_enable_azalia_audio_jack_presence(audio, engine_id);
	}
}

void dal_hw_sequencer_disable_azalia_audio_jack_presence(
	struct hw_sequencer *hws,
	struct display_path *display_path)
{
	/* enable audio only when set mode.*/
	struct audio *audio = dal_display_path_get_audio(
		display_path, ASIC_LINK_INDEX);

	if (audio) {
		enum engine_id engine_id =
			dal_hw_sequencer_get_engine_id(display_path);

		dal_audio_disable_azalia_audio_jack_presence(audio, engine_id);
	}
}

void dal_hw_sequencer_enable_memory_requests(struct hw_sequencer *hws,
		struct hw_path_mode *hw_path_mode)
{
	struct controller *controller;
	enum color_space color_space = translate_to_color_space(
			hw_path_mode->mode.color_space);

	controller = dal_display_path_get_controller(
			hw_path_mode->display_path);

	if (!dal_display_path_is_source_blanked(hw_path_mode->display_path))
		/* no need to enable memory requests if source is not blanked */
		return;

	dal_line_buffer_reset_on_vblank(
			dal_controller_get_line_buffer(controller),
			dal_controller_get_id(controller));

	dal_controller_unblank_crtc(controller, color_space);

	dal_display_path_set_source_blanked(
			hw_path_mode->display_path,
			DISPLAY_TRI_STATE_FALSE);

	dal_hw_sequencer_psr_enable(hws, hw_path_mode->display_path);
}

void dal_hw_sequencer_disable_memory_requests(
	struct hw_sequencer *hws,
	const struct hw_path_mode *hw_path_mode)
{
	struct controller *controller;
	enum color_space color_space = translate_to_color_space(
			hw_path_mode->mode.color_space);
	controller = dal_display_path_get_controller(
			hw_path_mode->display_path);

	/* no need to disable memory requests if source is blanked */
	if (dal_display_path_is_source_blanked(hw_path_mode->display_path))
		return;

	dal_controller_blank_crtc(controller, color_space);

	dal_display_path_set_source_blanked(
			hw_path_mode->display_path,
			DISPLAY_TRI_STATE_TRUE);
}

void dal_hw_sequencer_update_info_packets(
	struct hw_sequencer *hws,
	struct hw_path_mode *path_mode)
{
	return dal_hw_sequencer_update_info_frame(path_mode);
}

static enum hwss_result encoder_validate_path_mode(
	const struct hw_path_mode *path_mode,
	struct encoder *encoder,
	enum build_option bo_validate,
	enum build_option bo_enable)
{
	struct encoder_output enc_output;
	enum build_option option =
		path_mode->action == HW_PATH_ACTION_STATIC_VALIDATE ?
			bo_validate : bo_enable;
	enum encoder_result validation_result;

	build_encoder_output(path_mode, option, &enc_output);

	validation_result =
		dal_encoder_validate_output(
			encoder,
			&enc_output);

	switch (validation_result) {
	case ENCODER_RESULT_OK:
		return HWSS_RESULT_OK;
	case ENCODER_RESULT_NOBANDWIDTH:
		return HWSS_RESULT_NO_BANDWIDTH;
	default:
		return HWSS_RESULT_ERROR;
	}
}

/* Validate display path mode against static capabilities of graphics objects
 * in the display path. */
enum hwss_result dal_hw_sequencer_validate_display_path_mode(
	struct hw_sequencer *hws,
	const struct hw_path_mode *path_mode)
{
	enum hwss_result validation_result;
	struct display_path_objects disp_path_obj;
	struct controller *controller;

	dal_hw_sequencer_get_objects(path_mode->display_path, &disp_path_obj);

	controller = dal_display_path_get_controller(path_mode->display_path);

	if (controller) {

		if (!dal_controller_validate_timing(
			controller,
			&path_mode->mode.timing,
			dal_hw_sequencer_get_asic_signal(path_mode)))
			return HWSS_RESULT_ERROR;
	}


	if (disp_path_obj.upstream_encoder) {
		validation_result = encoder_validate_path_mode(
			path_mode,
			disp_path_obj.upstream_encoder,
			BUILD_OPTION_STATIC_VALIDATE_UPSTREAM,
			BUILD_OPTION_ENABLE_UPSTREAM);

		if (validation_result != HWSS_RESULT_OK)
			return validation_result;
	}

	if (disp_path_obj.downstream_encoder) {
		validation_result = encoder_validate_path_mode(
				path_mode,
				disp_path_obj.downstream_encoder,
				BUILD_OPTION_STATIC_VALIDATE_DOWNSTREAM,
				BUILD_OPTION_ENABLE_DOWNSTREAM);

		if (validation_result != HWSS_RESULT_OK)
			return validation_result;
	}

	return HWSS_RESULT_OK;
}

enum hwss_result dal_hw_sequencer_set_gamma_ramp_adjustment(
	struct hw_sequencer *hws,
	const struct display_path *display_path,
	struct hw_adjustment_gamma_ramp *adjusment)
{
	struct gamma_ramp *ramp = NULL;
	struct gamma_parameters *gamma_param = NULL;
	enum hwss_result result = HWSS_RESULT_OK;
	struct controller *crtc;

	crtc = dal_display_path_get_controller(display_path);

	if (crtc == NULL)
		return HWSS_RESULT_ERROR;

	if (adjusment == NULL)
		return HWSS_RESULT_ERROR;

	ramp = dal_alloc(sizeof(struct gamma_ramp));
	gamma_param = dal_alloc(sizeof(struct gamma_parameters));

	if (ramp && gamma_param) {
		dal_hw_sequencer_build_gamma_ramp_adj_params(
				adjusment,
				gamma_param,
				ramp);

		if (!dal_controller_set_gamma_ramp(crtc, ramp, gamma_param))
			result = HWSS_RESULT_ERROR;
	}

	dal_free(ramp);
	dal_free(gamma_param);

	return HWSS_RESULT_OK;
}

enum hwss_result dal_hw_sequencer_set_color_control_adjustment(
	struct hw_sequencer *hws,
	struct controller *crtc,
	struct hw_adjustment_color_control *adjustment)
{
	struct grph_csc_adjustment adjust;

	dal_memset(&adjust, 0, sizeof(adjust));
	if (dal_hw_sequencer_build_csc_adjust(
			hws,
			adjustment,
			&adjust) != HWSS_RESULT_OK)
		return HWSS_RESULT_ERROR;

	dal_controller_set_grph_csc_adjustment(crtc, &adjust);

	return HWSS_RESULT_OK;
}

enum hwss_result dal_hw_sequencer_set_vertical_sync_adjustment(
	struct hw_sequencer *hws,
	struct display_path *display_path,
	struct hw_adjustment_value *adjustment)
{
	/* TODO: add implementation */
	return HWSS_RESULT_ERROR;
}

enum hwss_result dal_hw_sequencer_set_horizontal_sync_adjustment(
	struct hw_sequencer *hws,
	struct display_path *display_path,
	struct hw_adjustment_value *adjustment)
{
	/* TODO: add implementation */
	return HWSS_RESULT_ERROR;
}

enum hwss_result dal_hw_sequencer_set_composite_sync_adjustment(
	struct hw_sequencer *hws,
	struct display_path *display_path,
	struct hw_adjustment_value *adjustment)
{
	/* TODO: add implementation */
	return HWSS_RESULT_ERROR;
}

enum hwss_result dal_hw_sequencer_enable_sync_output(
	struct hw_sequencer *hws,
	struct display_path *display_path)
{
	/* TODO: add implementation */
	return HWSS_RESULT_ERROR;
}

enum hwss_result dal_hw_sequencer_disable_sync_output(
	struct hw_sequencer *hws,
	struct display_path *display_path)
{
	/* TODO: add implementation */
	return HWSS_RESULT_ERROR;
}

enum hwss_result dal_hw_sequencer_set_backlight_adjustment(
	struct hw_sequencer *hws,
	struct display_path *display_path,
	struct hw_adjustment_value *adjustment)
{
	struct display_path_objects obj;

	if (adjustment == NULL)
		return HWSS_RESULT_ERROR;

	dal_hw_sequencer_get_objects(display_path, &obj);

	if (dal_display_path_get_controller(display_path) == NULL
			|| obj.upstream_encoder == NULL)
		return HWSS_RESULT_ERROR;

	dal_encoder_set_lcd_backlight_level(obj.upstream_encoder,
			adjustment->ui_value);

	if (obj.downstream_encoder != NULL)
		dal_encoder_set_lcd_backlight_level(
				obj.downstream_encoder,
				adjustment->ui_value);
	return HWSS_RESULT_OK;
}

enum hwss_result dal_hw_sequencer_validate_link(
	struct hw_sequencer *hws,
	const struct validate_link_param *param)
{
	const struct display_path *display_path = param->display_path;
	struct encoder *upstream;
	struct encoder *downstream =
		dal_display_path_get_downstream_encoder(
			display_path, param->link_idx);
	if (downstream)
		if (!dal_encoder_is_link_settings_supported(
				downstream, &param->link_settings))
			return HWSS_RESULT_OUT_OF_RANGE;

	upstream = dal_display_path_get_upstream_encoder(
		display_path, param->link_idx);
	if (!dal_encoder_is_link_settings_supported(
		upstream, &param->link_settings)) {
		BREAK_TO_DEBUGGER();
		return HWSS_RESULT_OUT_OF_RANGE;
	}

	return HWSS_RESULT_OK;
}

bool dal_hw_sequencer_is_supported_dp_training_pattern3(
	struct hw_sequencer *hws,
	struct display_path *display_path,
	uint32_t link_idx)
{
	struct encoder *encoder;
	struct encoder_feature_support features;

	if (!display_path) {
		BREAK_TO_DEBUGGER();

		return false;
	}

	encoder = dal_display_path_get_upstream_encoder(display_path, link_idx);

	if (!encoder) {
		BREAK_TO_DEBUGGER();

		return false;
	}

	features = dal_encoder_get_supported_features(encoder);

	return features.flags.bits.IS_TPS3_CAPABLE;
}

enum hwss_result dal_hw_sequencer_set_dp_phy_pattern(
	struct hw_sequencer *hws,
	const struct set_dp_phy_pattern_param *param)
{
	struct encoder_set_dp_phy_pattern_param dp_phy_pattern_param = {0};
	struct encoder *encoder = dal_display_path_get_upstream_encoder(
		param->display_path, param->link_idx);

	/* Build encoder context */
	struct encoder_context context;

	build_encoder_context(param->display_path, encoder, &context);

	/* Set EncoderDpPhyPattern */
	switch (param->test_pattern) {
	case DP_TEST_PATTERN_D102:
	case DP_TEST_PATTERN_SYMBOL_ERROR:
	case DP_TEST_PATTERN_PRBS7:
	case DP_TEST_PATTERN_80BIT_CUSTOM:
	case DP_TEST_PATTERN_HBR2_COMPLIANCE_EYE:
	case DP_TEST_PATTERN_TRAINING_PATTERN1:
	case DP_TEST_PATTERN_TRAINING_PATTERN2:
	case DP_TEST_PATTERN_TRAINING_PATTERN3:
	case DP_TEST_PATTERN_VIDEO_MODE:
		break;
	default:
		BREAK_TO_DEBUGGER();
		return HWSS_RESULT_ERROR;
	}

	/* Build SetTestPatternParam */
	dp_phy_pattern_param.ctx = &context;
	dp_phy_pattern_param.dp_phy_pattern = param->test_pattern;
	dp_phy_pattern_param.custom_pattern = param->custom_pattern;
	dp_phy_pattern_param.custom_pattern_size = param->cust_pattern_size;
	dp_phy_pattern_param.alt_scrambler_reset = param->alt_scrambler_reset;

	/* Call encoder to set test pattern */
		;

	/* Return proper result based on Encoder call */
	if (ENCODER_RESULT_OK !=
		dal_encoder_set_dp_phy_pattern(
			encoder, &dp_phy_pattern_param)) {
		BREAK_TO_DEBUGGER();
		return HWSS_RESULT_ERROR;
	}

	return HWSS_RESULT_OK;
}

enum hwss_result dal_hw_sequencer_set_lane_settings(
	struct hw_sequencer *hws,
	struct display_path *display_path,
	const struct link_training_settings *link_settings)
{
	struct display_path_objects obj;
	struct encoder *encoder;
	struct encoder_context context;

	if (!link_settings) {
		BREAK_TO_DEBUGGER();
		return HWSS_RESULT_ERROR;
	}

	dal_hw_sequencer_get_objects(display_path, &obj);

	/* use downstream encoder to handle the command */
	encoder = obj.downstream_encoder != NULL ?
		obj.downstream_encoder : obj.upstream_encoder;

	/* Build encoder context */
	build_encoder_context(display_path, encoder, &context);

	/* call Encoder to set lane settings */
	dal_encoder_set_lane_settings(encoder, &context, link_settings);

	return HWSS_RESULT_OK;
}

void dal_hw_sequencer_set_test_pattern(
	struct hw_sequencer *hws,
	struct hw_path_mode *path_mode,
	enum dp_test_pattern test_pattern,
	const struct link_training_settings *link_settings,
	const uint8_t *custom_pattern,
	uint8_t cust_pattern_size)
{
	struct controller *crtc =
		dal_display_path_get_controller(path_mode->display_path);
	/* handle test harness command */
	switch (test_pattern) {
	/* these patterns generated by controller */
	case DP_TEST_PATTERN_COLOR_SQUARES:
	case DP_TEST_PATTERN_COLOR_SQUARES_CEA:
	case DP_TEST_PATTERN_VERTICAL_BARS:
	case DP_TEST_PATTERN_HORIZONTAL_BARS:
	case DP_TEST_PATTERN_COLOR_RAMP: {
		/* disable bit depth reduction */
		struct bit_depth_reduction_params params = {{ 0 } };

		dal_controller_program_formatter_bit_depth_reduction(
			crtc,
			&params);

		/* call controller to set test pattern */
		dal_controller_set_test_pattern(
			dal_display_path_get_controller(
				path_mode->display_path),
			test_pattern,
			path_mode->mode.timing.flags.COLOR_DEPTH);
		break;
	}
		/* turn off test pattern mode */
	case DP_TEST_PATTERN_VIDEO_MODE: {
		/* restore bit depth reduction */
		struct bit_depth_reduction_params params = {{ 0 } };

		build_bit_depth_reduction_params(path_mode, &params);

		dal_controller_program_formatter_bit_depth_reduction(
			crtc,
			&params);

		/* reset test patterns on controller */
		dal_controller_set_test_pattern(
			dal_display_path_get_controller(
				path_mode->display_path),
			test_pattern,
			path_mode->mode.timing.flags.COLOR_DEPTH);
		break;
	}
	default:
		BREAK_TO_DEBUGGER(); /* invalid test pattern */
	}
}

enum hwss_result dal_hw_sequencer_enable_link(
	struct hw_sequencer *hws,
	const struct enable_link_param *in)
{
	return hws->funcs->hwss_enable_link(hws, in);
}

enum hwss_result dal_hw_sequencer_enable_link_base(
	struct hw_sequencer *hws,
	const struct enable_link_param *in)
{
	enum hwss_result ret = HWSS_RESULT_OK;
	struct display_path *display_path = in->display_path;
	const struct hw_path_mode *hw_path_mode = in->path_mode;
	struct encoder *upstream_enc;
	struct encoder *downstream_enc;

	/* Skip link reprogramming when optimization requested */
	if (!in->optimized_programming) {
		/* enable video output */
		struct encoder_output encoder_output = {
				{ ENGINE_ID_UNKNOWN } };
		downstream_enc = dal_display_path_get_downstream_encoder(
						in->display_path, in->link_idx);
		if (downstream_enc != NULL) {
			struct encoder_pre_enable_output_param
					pre_enable_output_param = { { 0 } };
			build_encoder_context(display_path, downstream_enc,
					&pre_enable_output_param.ctx);
			dal_memmove(&pre_enable_output_param.crtc_timing,
					&in->timing,
					sizeof(struct hw_crtc_timing));
			dal_memmove(&pre_enable_output_param.link_settings,
					&in->link_settings,
					sizeof(struct link_settings));
			dal_encoder_pre_enable_output(downstream_enc,
					&pre_enable_output_param);
		}

		upstream_enc = dal_display_path_get_upstream_encoder(
						in->display_path,
						in->link_idx);
		/* here we need to specify that encoder output settings
		 * need to be calculated as for the set mode,
		 * it will lead to querying dynamic link capabilities
		 * which should be done before enable output */
		build_upstream_encoder_output(
				hws,
				in->link_idx,
				hw_path_mode,
				&in->link_settings,
				BUILD_OPTION_SET_MODE,
				&encoder_output);

		if (dal_encoder_enable_output(upstream_enc, &encoder_output)
				!= ENCODER_RESULT_OK)
			ret = HWSS_RESULT_ERROR;
	}

	return ret;
}

void dal_hw_sequencer_disable_link(
	struct hw_sequencer *hws,
	const struct enable_link_param *in)
{
	struct encoder *enc;
	struct encoder_output encoder_output = { { ENGINE_ID_UNKNOWN } };
	const struct hw_path_mode *hw_path_mode = in->path_mode;

	/* Re-enable HPD if it is disabled */
	enable_hpd(hws, in->display_path);

	if (has_travis_or_nutmeg_encoder(in->display_path)) {
		/* Travis and Nutmeg require us to wait for one frame */
		struct controller *controller =
				dal_display_path_get_controller(
						in->display_path);

		dal_controller_wait_for_vblank(controller);
	}

	enc = dal_display_path_get_upstream_encoder(
			in->display_path,
			in->link_idx);

	build_upstream_encoder_output(
			hws,
			in->link_idx,
			hw_path_mode,
			&in->link_settings,
			BUILD_OPTION_DISABLE,
			&encoder_output);

	dal_encoder_disable_output(enc, &encoder_output);
}

void dal_hw_sequencer_enable_stream(
	struct hw_sequencer *hws,
	const struct enable_stream_param *in)
{
	const struct hw_path_mode *hw_path_mode = in->path_mode;
	struct display_path *display_path = in->display_path;
	struct audio *audio;

	if (in->link_idx == ASIC_LINK_INDEX) {
		/* 1. update AVI info frame (HDMI, DP)
		 * we always need to update info frame
		*/
		uint32_t active_total_with_borders;
		uint32_t early_control = 0;
		struct controller *controller =
				dal_display_path_get_controller(display_path);

		dal_hw_sequencer_update_info_frame(hw_path_mode);
		/* enable early control to avoid corruption on DP monitor*/
		active_total_with_borders =
				in->timing.h_addressable
					+ in->timing.h_overscan_left
					+ in->timing.h_overscan_right;

		if (in->link_settings.lane_count != LANE_COUNT_UNKNOWN) {
			early_control = active_total_with_borders
					% in->link_settings.lane_count;

			if (early_control == 0)
				early_control = in->link_settings.lane_count;
		}
		dal_controller_set_early_control(controller, early_control);
	}

	/* 3. enable audio only when set mode. */
	audio = dal_display_path_get_audio(display_path, in->link_idx);
	if (audio != NULL) {
		enum signal_type asic_signal =
			dal_hw_sequencer_get_asic_signal(hw_path_mode);
		enum engine_id engine_id =
			dal_hw_sequencer_get_engine_id(display_path);
		dal_audio_enable_output(audio, engine_id, asic_signal,
				in->link_settings.link_rate);
	}

}

void dal_hw_sequencer_disable_stream(
	struct hw_sequencer *hws,
	const struct enable_stream_param *in)
{
	struct encoder *enc;
	struct encoder_context enc_ctx;

	/* update AVI info frame (HDMI, DP), cleanup HW registers */
	enc = dal_display_path_get_upstream_encoder(
			in->display_path,
			in->link_idx);

	build_encoder_context(in->display_path, enc, &enc_ctx);
	dal_encoder_stop_info_frame(enc, &enc_ctx);
}

/**
* dal_hw_sequencer_blank_stream
*
* @brief
*  blanks the output stream associated to the input display path on the
*  specified link
*/
void dal_hw_sequencer_blank_stream(
	struct hw_sequencer *hws,
	const struct blank_stream_param *in)
{
	struct encoder *enc;
	struct encoder_context enc_ctx;

	ASSERT(in && in->display_path);
	enc = dal_display_path_get_upstream_encoder(in->display_path,
				in->link_idx);
	ASSERT(enc != NULL);
	build_encoder_context(in->display_path, enc, &enc_ctx);
	dal_encoder_blank(enc, &enc_ctx);
}

void dal_hw_sequencer_unblank_stream(
		struct hw_sequencer *hws,
		const struct blank_stream_param *in)
{
	struct encoder *enc;
	struct encoder_unblank_param enc_unbl_param;

	ASSERT(in != NULL && in->display_path != NULL);

	enc = dal_display_path_get_upstream_encoder(
			in->display_path,
			in->link_idx);

	ASSERT(enc != NULL);

	build_encoder_context(in->display_path, enc, &enc_unbl_param.ctx);
	dal_memmove(
			&enc_unbl_param.crtc_timing,
			&in->timing,
			sizeof(struct hw_crtc_timing));
	dal_memmove(
			&enc_unbl_param.link_settings,
			&in->link_settings,
			sizeof(struct link_settings));
	dal_encoder_unblank(enc, &enc_unbl_param);
}

static enum hwss_result hw_sequencer_pre_dce_clock_change(
		const struct hw_sequencer *hws,
		const struct minimum_clocks_calculation_result *min_clk_in,
		enum clocks_state required_clocks_state,
		struct power_to_dal_info *output)
{
	struct dal_to_power_info input;

	if (false == hws->use_pp_lib) {
		/* Usage of PPLib is disabled */
		return HWSS_RESULT_NOT_SUPPORTED;
	}

	dal_memset(&input, 0, sizeof(input));

	input.min_deep_sleep_sclk = min_clk_in->min_deep_sleep_sclk;
	input.min_mclk = min_clk_in->min_mclk_khz;
	input.min_sclk = min_clk_in->min_sclk_khz;

	switch (required_clocks_state) {
	case CLOCKS_STATE_ULTRA_LOW:
		input.required_clock = PP_CLOCKS_STATE_ULTRA_LOW;
		break;
	case CLOCKS_STATE_LOW:
		input.required_clock = PP_CLOCKS_STATE_LOW;
		break;
	case CLOCKS_STATE_NOMINAL:
		input.required_clock = PP_CLOCKS_STATE_NOMINAL;
		break;
	case CLOCKS_STATE_PERFORMANCE:
		input.required_clock = PP_CLOCKS_STATE_PERFORMANCE;
		break;
	default:
		input.required_clock = PP_CLOCKS_STATE_NOMINAL;
		break;
	}

	if (!dal_pp_pre_dce_clock_change(hws->dal_context, &input, output)) {
		/*dal_logger_write(hws->dal_context->logger,
			LOG_MAJOR_WARNING,
			LOG_MINOR_COMPONENT_HWSS,
			"%s: dal_pp_pre_dce_clock_change failed!\n",
			__func__);*/
		return HWSS_RESULT_ERROR;
	}

	return HWSS_RESULT_OK;
}

/* Notify PPLib about Clocks and Clock State values which WE need.
 * PPLib will return the actual values (it will program in HW) which
 * we'll use for stutter and display marks. */
enum hwss_result dal_hw_sequencer_set_clocks_and_clock_state(
		struct hw_sequencer *hws,
		struct hw_global_objects *g_obj,
		const struct minimum_clocks_calculation_result *min_clk_in,
		enum clocks_state required_clocks_state)
{
	struct power_to_dal_info output;
	struct bandwidth_mgr_clk_info bm_clk_info;

	dal_memset(&output, 0, sizeof(output));

	if (HWSS_RESULT_OK != hw_sequencer_pre_dce_clock_change(
					hws,
					min_clk_in,
					required_clocks_state,
					&output)) {
		/* "output" was not updated by PPLib.
		 * DAL will use default values for set mode.
		 *
		 * Do NOT fail this call. */
		return HWSS_RESULT_OK;
	}

	/* PPLib accepted the "clock state" that we need, that means we
	 * can store it as minimum state because PPLib guarantees not go below
	 * that state.
	 *
	 * Update the clock state here (prior to setting Pixel clock,
	 * DVO clock, or Display clock) */
	if (!dal_display_clock_set_min_clocks_state(g_obj->dc,
			required_clocks_state)) {
		dal_logger_write(hws->dal_context->logger,
			LOG_MAJOR_WARNING,
			LOG_MINOR_COMPONENT_HWSS,
			"%s: failed to set minimum clock state!\n",
			__func__);
	}

	dal_memset(&bm_clk_info, 0, sizeof(bm_clk_info));

	bm_clk_info.max_mclk_khz = output.max_mclk;
	bm_clk_info.min_mclk_khz = output.min_mclk;
	bm_clk_info.max_sclk_khz = output.max_sclk;
	bm_clk_info.min_sclk_khz = output.min_sclk;

	/* Now let Bandwidth Manager know about values we got from PPLib. */
	dal_bandwidth_manager_set_dynamic_clock_info(g_obj->bm, &bm_clk_info);

	return HWSS_RESULT_OK;
}

/**
*  perform HW blocks programming for each block in Chain to set the mode
*
*  first we need to fill minimum clock parameters for the existent paths,
*  then each time setMode is called fill parameters for the programming path
*  to be passed to the next setMode
*
* \param	path_set - display path set to operate on
*/
enum hwss_result dal_hw_sequencer_set_mode(
	struct hw_sequencer *hws,
	struct hw_path_mode_set *path_set)
{
	struct dal_context *dal_context = hws->dal_context;
	uint32_t path_id = 0;
	struct hwss_build_params *build_params = NULL;
	union hwss_build_params_mask params_mask;
	struct hw_global_objects g_obj = { NULL };
	uint32_t paths_num;
	enum clocks_state required_clocks_state;

	if (!path_set)
		return HWSS_RESULT_ERROR;

	paths_num = dal_hw_path_mode_set_get_paths_number(path_set);

	dal_hw_sequencer_get_global_objects(path_set, &g_obj);

	/* Fill information for new acquired set of display paths. */
	params_mask.all = PARAMS_MASK_ALL;
	/* At this point we DO NOT have any information on Planes Config.
	 * That means we can NOT request Parameters for SCALING_TAPS and for
	 * LINE_BUFFER */
	params_mask.bits.SCALING_TAPS = 0;
	params_mask.bits.LINE_BUFFER = 0;

	build_params = dal_hw_sequencer_prepare_path_parameters(
			hws,
			path_set,
			params_mask,
			false);

	if (NULL == build_params) {
		dal_logger_write(dal_context->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_HWSS,
			"%s: prepare_path_parameters failed!\n",
			__func__);
		return HWSS_RESULT_ERROR;
	}

	/* Set bit in scratch register to notify VBIOS that driver is under
	 * mode change sequence (critical state) and VBIOS will skip certain
	 * operations requested from other clients when this bit set. */
	dal_bios_parser_set_scratch_critical_state(
		dal_adapter_service_get_bios_parser(hws->as),
		true);

	/* Program stutter and other display marks to safe values to avoid
	 * corruption. */
	hws->funcs->set_safe_displaymark(hws, path_set,
		build_params->wm_input_params, build_params->params_num);

	/* Note that 'build_params->min_clock_result' contains the minimum
	 * clock for mode set because 'params_mask.all = PARAMS_MASK_ALL'
	 * (which means params_mask.bits.MIN_CLOCKS == true).
	 *
	 * We need the minimum clocks
	 * because minimum clocks == maximum power efficiency */

	/* Raise Required Clock State PRIOR to programming of state-dependent
	 * clocks. */
	required_clocks_state = hws->funcs->get_required_clocks_state(hws,
			g_obj.dc, path_set, &build_params->min_clock_result);

	/* Call PPLib and Bandwidth Manager. */
	if (true == hws->use_pp_lib &&
		HWSS_RESULT_OK != dal_hw_sequencer_set_clocks_and_clock_state(
			hws, &g_obj,
			&build_params->min_clock_result,
			required_clocks_state)) {
		/* should never happen */
		return HWSS_RESULT_ERROR;
	}

	/* reset modes and turn off displays */
	for (path_id = 0; path_id < paths_num; path_id++) {
		const struct hw_path_mode *path_mode =
			dal_hw_path_mode_set_get_path_by_index(
				path_set, path_id);

		/* If we require full mode sequence, then reset path first. */
		if (path_mode->action == HW_PATH_ACTION_RESET ||
			path_mode->action_flags.TIMING_CHANGED) {
			reset_path_mode_back_end(hws, path_set, path_id);
		}
	}

	/* PPLib takes care of Mclk and Sclk.
	 * We program Dclk here. */
	hws->funcs->set_display_clock(hws, path_set,
			&build_params->min_clock_result);

	/* set new modes */
	for (path_id = 0; path_id < paths_num; path_id++) {
		const struct hw_path_mode *path_mode =
			dal_hw_path_mode_set_get_path_by_index(
				path_set, path_id);

		switch (path_mode->action) {
		case HW_PATH_ACTION_SET:
			set_path_mode_back_end(
				hws, path_set, path_id, build_params);
			break;
		default:
			break;
		}
	}

	/* Setup audio rate clock source */
	hws->funcs->setup_audio_wall_dto(hws, path_set, build_params);

	/* Up to this point we are using 'safe' marks, which are not
	 * power-efficient.
	 * Program stutter and other display marks to values provided
	 * by PPLib. */
	hws->funcs->set_displaymark(hws, path_set,
			build_params->wm_input_params,
			build_params->params_num);

	if (true == hws->use_pp_lib) {
		/* Let PPLib know that we are using the clocks it
		 * provided to us. */
		if (!dal_pp_post_dce_clock_change(dal_context)) {
			/*dal_logger_write(dal_context->logger,
				LOG_MAJOR_WARNING,
				LOG_MINOR_COMPONENT_HWSS,
				"%s: dal_pp_post_dce_clock_change() failed!\n",
				__func__);*/
		}
	}

	dal_hw_sync_control_inter_path_synchronize(hws->sync_control, path_set);

	/* reset critical state bit in scratch register (end of mode change) */
	dal_bios_parser_set_scratch_critical_state(
		dal_adapter_service_get_bios_parser(hws->as),
		false);

	/* release allocated memory */
	dal_hw_sequencer_free_path_parameters(build_params);

	return HWSS_RESULT_OK;
}

static void program_fmt(
	const struct hw_path_mode *path_mode)
{
	/* dithering is affected by <CrtcSourceSelect>, hence should be
	 * programmed afterwards */
	bool program_fmt;
	struct clamping_and_pixel_encoding_params fmt_clamping;
	struct bit_depth_reduction_params fmt_bit_depth = {{ 0 } };
	struct controller *crtc =
		dal_display_path_get_controller(path_mode->display_path);

	program_fmt = build_bit_depth_reduction_params(
		path_mode, &fmt_bit_depth);

	if (program_fmt)
		dal_controller_program_formatter_bit_depth_reduction(
			crtc,
			&fmt_bit_depth);

	/* set pixel encoding based on adjustment of pixel format if any! */
	dal_memset(&fmt_clamping, 0, sizeof(fmt_clamping));
	program_fmt = setup_pixel_encoding(path_mode, &fmt_clamping);

	if (!program_fmt)
		return;

	dal_controller_program_formatter_clamping_and_pixel_encoding(
		crtc,
		&fmt_clamping);
}

static void program_adjustments(
	struct hw_sequencer *hws,
	const struct hw_path_mode *path_mode,
	struct hwss_build_params *build_params,
	enum color_space color_space,
	uint32_t path_id,
	struct controller *crtc)
{
	/*
	 * TODO should we look at plane_config instead of path_mode?
	 */

	struct hw_adjustment_set *adjustment_set = path_mode->adjustment_set;

	struct hw_adjustment_color_control *color_adjustment = NULL;
	struct hw_adjustment_gamma_ramp *gamma_adjustment = NULL;

	struct hw_adjustment_value *v_sync_adjustment = NULL;
	struct hw_adjustment_value *h_sync_adjustment = NULL;
	struct hw_adjustment_value *composite_sync_adjustment = NULL;
	struct hw_adjustment_value *backlight_adjustment = NULL;
	struct hw_adjustment_value *vb_level_adjustment = NULL;

	/* TODO: using 0 plane for now always */
	uint32_t plane_id = 0;

	if (adjustment_set) {
		gamma_adjustment = adjustment_set->gamma_ramp;
		color_adjustment = adjustment_set->color_control;

		/* could be zero's, only for CRT non zero's */
		v_sync_adjustment = adjustment_set->v_sync;
		h_sync_adjustment = adjustment_set->h_sync;
		composite_sync_adjustment = adjustment_set->composite_sync;
		backlight_adjustment = adjustment_set->backlight;
		vb_level_adjustment = adjustment_set->vb_level;
	}

	if (gamma_adjustment != NULL) {
		/* user gamma */
		dal_hw_sequencer_set_gamma_ramp_adjustment(
			hws, path_mode->display_path, gamma_adjustment);
	} else {
		/* default gamma */
		dal_controller_set_default_gamma(
			crtc,
			path_mode->mode.pixel_format);
	}
	if (color_adjustment != NULL) {
		/* user colors */
		color_adjustment->lb_color_depth =
			dal_hw_sequencer_translate_to_lb_color_depth(
				build_params->
				line_buffer_params[path_id][plane_id].depth);
		dal_hw_sequencer_set_color_control_adjustment(
			hws,
			crtc,
			color_adjustment);
	} else {
		/* default colors */
		struct default_adjustment default_adjust;

		dal_memset(&default_adjust, 0, sizeof(default_adjust));

		default_adjust.force_hw_default = false;
		default_adjust.color_space = color_space;
		default_adjust.csc_adjust_type = GRAPHICS_CSC_ADJUST_TYPE_SW;
		default_adjust.surface_pixel_format =
				path_mode->mode.pixel_format;
		/* display color depth */
		default_adjust.color_depth =
			dal_hw_sequencer_translate_to_csc_color_depth(
				path_mode->mode.timing.flags.COLOR_DEPTH);
		/* Lb color depth */
		default_adjust.lb_color_depth =
			dal_hw_sequencer_translate_to_lb_color_depth(
				build_params->
				line_buffer_params[path_id][plane_id].depth);
		dal_controller_set_grph_csc_default(
			crtc,
			&default_adjust);
	}

	dal_controller_set_input_csc(crtc, color_space);

	/* the adjustments routines check for NULL hw_adjustment*,
	 * so no problem! */
	dal_hw_sequencer_set_vertical_sync_adjustment(
		hws,
		path_mode->display_path,
		v_sync_adjustment);
	dal_hw_sequencer_set_horizontal_sync_adjustment(
		hws,
		path_mode->display_path,
		h_sync_adjustment);
	dal_hw_sequencer_set_composite_sync_adjustment(
		hws,
		path_mode->display_path,
		composite_sync_adjustment);
	dal_hw_sequencer_set_backlight_adjustment(
		hws,
		path_mode->display_path,
		backlight_adjustment);
}

static void program_encoder_and_audio(
	struct hw_sequencer *hws,
	const struct hw_path_mode *path_mode,
	struct display_path_objects *objs,
	struct hwss_build_params *build_params,
	enum engine_id engine_id,
	uint32_t path_id)
{
	/* if pixel format changed on HDMI we should update gamut packet
	 * information for example when you go from A888 to XR_BIAS or FP16
	 */
	bool is_hdmi = false;

	if (path_mode->action_flags.GAMUT_CHANGED) {
		enum dcs_edid_connector_type conn_type =
			dal_dcs_get_connector_type(
				dal_display_path_get_dcs(
					path_mode->display_path));

		if (conn_type == EDID_CONNECTOR_HDMIA)
			is_hdmi = true;
	}

	if (is_hdmi || path_mode->action_flags.TIMING_CHANGED ||
		path_mode->action_flags.PIXEL_ENCODING_CHANGED) {
		/* setup Encoder - prepare enable encoder parameters */
		struct encoder_output encoder_output;

		build_encoder_output(path_mode, BUILD_OPTION_ENABLE_UPSTREAM,
			&encoder_output);

		/* setup Encoder */
		dal_encoder_setup(objs->upstream_encoder, &encoder_output);
		if (objs->downstream_encoder != NULL)
			dal_encoder_setup(
				objs->downstream_encoder,
				&encoder_output);

		/* setup Audio */
		if (objs->audio != NULL) {
			struct audio_output audio_output;
			struct audio_info *audio_info = NULL;

			dal_hw_sequencer_build_audio_output(
				hws,
				path_mode,
				engine_id,
				&build_params->pll_settings_params[path_id],
				&audio_output);

			audio_info = build_audio_info(hws, path_mode);

			ASSERT(audio_info != NULL);
			if (audio_info != NULL) {
				/* setup audio */
				dal_audio_setup(objs->audio,
					&audio_output, audio_info);
				dal_free(audio_info);
			}

		}
	}
}

static void reprogram_crtc_and_pll(
	struct hw_sequencer *hws,
	const struct hw_path_mode *path_mode,
	struct hwss_build_params *build_params,
	struct hw_crtc_timing *hw_crtc_timing,
	struct bandwidth_manager *bm,
	struct controller *controller,
	uint32_t path_id)
{
	if (path_mode->action_flags.TIMING_CHANGED) {
		enum controller_id id = dal_controller_get_id(controller);

		/* program PLL */
		struct pixel_clk_params pixel_clk_params;

		dal_memset(&pixel_clk_params, 0, sizeof(pixel_clk_params));
		dal_hw_sequencer_get_pixel_clock_parameters(
			path_mode, &pixel_clk_params);

		pixel_clk_params.flags.PROGRAM_PIXEL_CLOCK = true;

		dal_clock_source_program_pix_clk(
			dal_display_path_get_clock_source(
				path_mode->display_path),
			&pixel_clk_params,
			&build_params->pll_settings_params[path_id]);

		/* program CRTC with the original timing and setup stereo mixer
		 * while CRTC is off (does nothing on regular path) */
		hws->funcs->setup_timing_and_blender(
			hws, controller, path_mode, hw_crtc_timing);

		dal_bandwidth_manager_allocate_dmif_buffer(bm,
			id, build_params->params_num,
			build_params->bandwidth_params);

		dal_controller_enable_timing_generator(controller);
	} else
		/* Partial setMode sequence - only need to blank CRTC */
		dal_hw_sequencer_disable_memory_requests(hws, path_mode);
}

DAL_VECTOR_AT_INDEX(plane_configs, const struct plane_config *)

static void program_scaler(
	struct hw_sequencer *hws,
	struct hw_path_mode *path_mode,
	const struct hw_crtc_timing *hw_crtc_timing,
	struct hwss_build_params *build_params,
	struct controller *controller,
	struct bandwidth_manager *bm,
	uint32_t path_id,
	uint32_t plane_id,
	enum color_space color_space)
{
	struct scaler_data scaler_data;
	const struct plane_config *plane_config = NULL;
	struct scaling_tap_info *taps;

	dal_memset(&scaler_data, 0, sizeof(scaler_data));
	scaler_data.hw_crtc_timing = hw_crtc_timing;
	taps = &build_params->scaling_taps_params[path_id][plane_id];

	if (path_mode->plane_configs != NULL)
		plane_config = plane_configs_vector_at_index(
					path_mode->plane_configs,
					plane_id);

	if (plane_config == NULL) {
		dal_logger_write(hws->dal_context->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_HWSS,
			"%s: plane_config is NULL!\n", __func__);
		BREAK_TO_DEBUGGER();
		return;
	}


	dal_hw_sequencer_build_scaler_parameter_plane(
		plane_config,
		taps,
		&scaler_data,
		true, /*program viewport*/
		true, /*program_alpha*/
		false); /*unlock scaler*/

	hws->funcs->setup_line_buffer_pixel_depth(
		hws,
		controller,
		build_params->line_buffer_params[path_id][plane_id].depth,
		false);

	/* ADJUST WIDTH FOR WIDE DISPLAY try to enable line buffer power gating
	 * feature */
	dal_hw_sequencer_enable_line_buffer_power_gating(
		dal_controller_get_line_buffer(controller),
		dal_controller_get_id(controller),
		scaler_data.pixel_type,
		path_mode->mode.scaling_info.src.width,
		path_mode->mode.scaling_info.dst.width,
		&build_params->scaling_taps_params[path_id][plane_id],
		build_params->line_buffer_params[path_id][plane_id].depth,
		path_mode->mode.scaling_info.src.height,
		path_mode->mode.scaling_info.dst.height,
		path_mode->mode.timing.flags.INTERLACED);

	dal_bandwidth_manager_setup_pipe_max_request(bm,
		dal_controller_get_id(controller),
		&build_params->bandwidth_params->color_info);

	dal_controller_set_overscan_color_black(controller, color_space);

	dal_controller_set_scaler_wrapper(controller, &scaler_data);
	dal_controller_update_viewport(
			controller,
			&scaler_data.viewport,
			false);
}



static enum hwss_result set_path_mode_back_end(
	struct hw_sequencer *hws,
	struct hw_path_mode_set *path_set,
	uint32_t path_id,
	struct hwss_build_params *build_params)
{
	const struct hw_path_mode *path_mode =
		dal_hw_path_mode_set_get_path_by_index(path_set, path_id);

	struct display_path_objects display_path_objs;
	struct hw_global_objects global_objs = { NULL };
	enum color_space color_space;
	struct display_path *display_path = path_mode->display_path;
	const struct hw_mode_info *mode_info = &path_mode->mode;
	enum engine_id engine_id = dal_hw_sequencer_get_engine_id(display_path);
	enum signal_type asic_signal =
		dal_hw_sequencer_get_asic_signal(path_mode);
	struct controller *controller =
		dal_display_path_get_controller(display_path);

	if (engine_id == ENGINE_ID_UNKNOWN)
		return HWSS_RESULT_ERROR;

	/* Extract global objects */
	dal_hw_sequencer_get_global_objects(path_set, &global_objs);

	/* Full setMode sequence (path was already reset in resetPathMode) */
	reprogram_crtc_and_pll(
		hws,
		path_mode,
		build_params,
		(struct hw_crtc_timing *)&mode_info->timing,
		global_objs.bm,
		controller, path_id);

	/* Program dynamic refresh rate - simply write a few CRTC registers
	 * (no BIOS involvement). Must be done after CRTC programmed. Also needs
	 * to be programmed for case where optimization skips full timing
	 * reprogramming. */
	dal_controller_program_drr(
		controller, &mode_info->timing.ranged_timing);

	/* program Scaler */
	color_space = translate_to_color_space(
		path_mode->mode.color_space);

	dal_hw_sequencer_get_objects(
		path_mode->display_path, &display_path_objs);

	{
		/* select CRTC source for encoder */
		struct bp_crtc_source_select crtc_source_select;

		dal_memset(&crtc_source_select, 0, sizeof(crtc_source_select));
		crtc_source_select.engine_id = engine_id;
		crtc_source_select.controller_id =
			dal_controller_get_id(controller);
		crtc_source_select.signal = asic_signal;
		crtc_source_select.enable_dp_audio =
			display_path_objs.audio != NULL;
		crtc_source_select.sink_signal = dal_get_signal(path_mode);
		crtc_source_select.display_output_bit_depth =
			translate_to_display_output_bit_depth(
				path_mode->mode.timing.flags.COLOR_DEPTH);

		/* call VBIOS table to set CRTC source for the HW encoder block
		 * note: video bios clears all FMT setting here. */
		dal_bios_parser_crtc_source_select(
			dal_adapter_service_get_bios_parser(hws->as),
			&crtc_source_select);
	}

	/* deep color enable for HDMI, set FMAT */
	dal_controller_formatter_set_dyn_expansion(
		controller,
		color_space,
		translate_to_color_depth(
			path_mode->mode.timing.flags.COLOR_DEPTH),
		asic_signal);

	{
		/* Setup Engine Stereosync - stereosync select should always
		 * match source select */
		struct encoder_3d_setup enconder_3d_setup = {
			ENGINE_ID_UNKNOWN };
		enconder_3d_setup.engine = engine_id;
		enconder_3d_setup.source =
			dal_controller_get_sync_source(controller);
		enconder_3d_setup.flags.bits.SETUP_SYNC_SOURCE = true;
		dal_encoder_setup_stereo(
			display_path_objs.upstream_encoder, &enconder_3d_setup);

		if (display_path_objs.downstream_encoder != NULL)
			dal_encoder_setup_stereo(
				display_path_objs.downstream_encoder,
				&enconder_3d_setup);
	}

	program_fmt(path_mode);

	program_encoder_and_audio(
		hws,
		path_mode,
		&display_path_objs,
		build_params,
		engine_id,
		path_id);

	return HWSS_RESULT_OK;
}

/**
 * reset_path_mode_back_end
 *
 * @brief
 *  reset (disable) all HW blocks for the specified path
 *
 */
static enum hwss_result reset_path_mode_back_end(
	struct hw_sequencer *hws,
	struct hw_path_mode_set *set,
	uint32_t path_id)
{
	const struct hw_path_mode *path_mode =
		dal_hw_path_mode_set_get_path_by_index(set, path_id);
	struct display_path *display_path = path_mode->display_path;
	struct controller *controller =
		dal_display_path_get_controller(display_path);
	/* Extract global objects */
	struct hw_global_objects global_objs = { NULL };

	if (dal_hw_sequencer_get_engine_id(display_path) == ENGINE_ID_UNKNOWN)
		return HWSS_RESULT_ERROR;

	dal_hw_sequencer_get_global_objects(set, &global_objs);

	dal_hw_sequencer_disable_memory_requests(hws, path_mode);

	dal_controller_disable_timing_generator(controller);

	dal_bandwidth_manager_deallocate_dmif_buffer(
		global_objs.bm,
		dal_controller_get_id(controller),
		dal_hw_path_mode_set_get_paths_number(set));

	dal_controller_set_scaler_bypass(controller);

	dal_controller_disable_stereo_mixer(controller);

	return HWSS_RESULT_OK;
}

void dal_hw_sequencer_destroy(struct hw_sequencer **hws)
{
	if (!hws || !*hws) {
		BREAK_TO_DEBUGGER();
		return;
	}

	(*hws)->funcs->destroy(hws);

	*hws = NULL;
}

/* Derived objects call this function to do common initialization. */
bool dal_hw_sequencer_construct_base(
		struct hw_sequencer *hws,
		struct hws_init_data *init_data)
{
	if (!init_data->as || !init_data->dal_context) {
		/* TODO: call Logger with an error message. */
		return false;
	}

	hws->as = init_data->as;
	hws->dal_context = init_data->dal_context;

	hws->use_pp_lib = dal_adapter_service_is_feature_supported(
			FEATURE_USE_PPLIB);

	return true;
}

struct hw_sequencer *dal_hw_sequencer_create(struct hws_init_data *init_data)
{
	struct hw_sequencer *hws = NULL;

	switch (dal_adapter_service_get_dce_version(init_data->as)) {
#if defined(CONFIG_DRM_AMD_DAL_DCE11_0)
	case DCE_VERSION_11_0:
		hws = dal_hw_sequencer_dce110_create(init_data);
		break;
#endif
	default:
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	return hws;
}

enum signal_type dal_hw_sequencer_detect_sink(
	struct hw_sequencer *hws,
	struct display_path *display_path)
{
	enum signal_type signal =
		dal_display_path_get_config_signal(
			display_path, SINK_LINK_INDEX);

	/* the signal in the display path here is the one from reported by
	 * connector for analog signals we will report the same signal as in
	 * display path
	 */
	switch (signal) {
	case SIGNAL_TYPE_RGB:
	case SIGNAL_TYPE_YPBPR:
	case SIGNAL_TYPE_SCART:
	case SIGNAL_TYPE_COMPOSITE:
	case SIGNAL_TYPE_SVIDEO:
		return signal;
	default:
		break;
	}

	/* when calling detect_sink, we should pass in the downstream object.
	 * This is not always the connector. But here we are ok to call
	 * detect_sink with connect_object_id since we are performing this for
	 * the SINK_LINK_INDEX.
	 * The downstream object in this case will always be the connector
	 * object.
	 */
	return dal_encoder_detect_sink(
		dal_display_path_get_upstream_encoder(
			display_path, SINK_LINK_INDEX),
		dal_connector_get_graphics_object_id(
			dal_display_path_get_connector(display_path)));
}

/**
 * dal_hws_detect_load
 *
 * detect load on the sink, if non-destructive requested and detection is
 * destructive skip it and return false
 *
 */
enum signal_type dal_hw_sequencer_detect_load(
	struct hw_sequencer *hws,
	struct display_path *display_path)
{
	struct display_path_objects objs;
	struct encoder *encoder;
	struct encoder_context context;

	dal_hw_sequencer_get_objects(display_path, &objs);

	encoder = objs.downstream_encoder != NULL ?
		objs.downstream_encoder : objs.upstream_encoder;

	build_encoder_context(display_path, encoder, &context);

	return dal_encoder_detect_load(encoder, &context);
}

bool dal_hw_sequencer_is_sink_present(
	struct hw_sequencer *hws,
	struct display_path *display_path)
{
	/* When calling is_sink_present, we should pass in the downstream
	 * object. We are doing detection on the encoder closest to the
	 * connector by specifying the SINK_LINK_INDEX. The downstream object in
	 * this case will always be the connector object.
	 */
	return dal_encoder_is_sink_present(
		dal_display_path_get_upstream_encoder(
			display_path, SINK_LINK_INDEX),
		dal_connector_get_graphics_object_id(
			dal_display_path_get_connector(display_path)));
}

void dal_hw_sequencer_program_drr(
		struct hw_sequencer *hws,
		const struct hw_path_mode *path_mode)
{
	struct controller *controller =
		dal_display_path_get_controller(path_mode->display_path);

	dal_controller_program_drr(
		controller, &path_mode->mode.timing.ranged_timing);
}

void dal_hw_sequencer_psr_setup(
	struct hw_sequencer *hws,
	const struct hw_path_mode *path_mode,
	const struct psr_caps *psr_caps)
{
}

void dal_hw_sequencer_psr_enable(
	struct hw_sequencer *hws,
	struct display_path *display_path)
{
}

/**
 * Validate a set of display path modes including scaler and bandwidth in the
 * multi-or-single display configuration.
 *
 * Assume static validation (by dal_hw_sequencer_validate_display_path_mode())
 * passed, and will not be performed here. The idea is that modes which can't
 * be supported "statically" are already filtered out (by timing service).
 */
enum hwss_result dal_hw_sequencer_validate_display_hwpms(
	struct hw_sequencer *hws,
	struct hw_path_mode_set *path_set)
{
	const struct hw_path_mode *path_mode;
	struct controller *controller = NULL;
	enum hwss_result result = HWSS_RESULT_OK;
	struct hwss_build_params *build_params = NULL;
	union hwss_build_params_mask params_mask;

	if (path_set == NULL) {
		dal_logger_write(hws->dal_context->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_HWSS,
			"%s: invalid input!\n", __func__);
		return HWSS_RESULT_ERROR;
	}

	/* We need a Controller for Bandwidth Manager input.
	 * Get Controller from any path (first one is good enough). */
	path_mode = dal_hw_path_mode_set_get_path_by_index(path_set, 0);
	if (path_mode == NULL) {
		dal_logger_write(hws->dal_context->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_HWSS,
			"%s: path mode set is empty!\n", __func__);
		return HWSS_RESULT_ERROR;
	}

	if (path_mode->display_path == NULL) {
		dal_logger_write(hws->dal_context->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_HWSS,
			"%s: no Display Path in path mode!\n",
			__func__);
		return HWSS_RESULT_ERROR;
	}

	controller = dal_display_path_get_controller(path_mode->display_path);
	if (controller == NULL) {
		dal_logger_write(hws->dal_context->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_HWSS,
			"%s: no controller on the Path!\n", __func__);
		return HWSS_RESULT_ERROR;
	}

	/*********************************************
			validate scaler
	 *********************************************/
	/* For validate Path mode, we don't need PLL settings, the PLL setting
	 * is not being used here at all. */
	/* Note:GetPLLDivider() will call video BIOS to adjustPLLRate. */
	params_mask.all = 0;
	params_mask.bits.BANDWIDTH = true;

	/* fill information for active set of display paths */
	build_params =
		dal_hw_sequencer_prepare_path_parameters(
			hws,
			path_set,
			params_mask,
			true);

	if (NULL == build_params)
		return HWSS_RESULT_ERROR;

	/**************************************************
			bandwidth validation
	 **************************************************/
	if (!validate_video_memory_bandwidth(
			controller,
			build_params->params_num,
			build_params->bandwidth_params))
		result = HWSS_RESULT_NO_BANDWIDTH;

	/* release allocated memory */
	dal_hw_sequencer_free_path_parameters(build_params);

	return result;
}

void dal_hw_sequencer_psr_disable(
	struct hw_sequencer *hws,
	struct display_path *display_path)
{
}


enum hwss_result dal_hw_sequencer_set_safe_displaymark(
		struct hw_sequencer *hws,
		struct hw_path_mode_set *path_set)
{
	struct hwss_build_params *build_params = NULL;
	union hwss_build_params_mask params_mask;

	if (!path_set)
		return HWSS_RESULT_ERROR;

	dal_logger_write(hws->dal_context->logger,
		LOG_MAJOR_INTERFACE_TRACE,
		LOG_MINOR_COMPONENT_HWSS,
		"%s: Setting safe display mark\n", __func__);

	/* Fill information for new acquired set of display paths.
	 * For SetMode we need ALL parameters. */
	params_mask.all = 0;
	params_mask.bits.WATERMARK = true;
	params_mask.bits.BANDWIDTH = true;

	/* TODO: do we really need to calculate anything for SAFE marks?
	 * If not, we don't need to "prepare parameters" too. */
	build_params =
		dal_hw_sequencer_prepare_path_parameters(
			hws,
			path_set,
			params_mask,
			false);

	if (NULL == build_params) {
		dal_logger_write(hws->dal_context->logger, LOG_MAJOR_ERROR,
				LOG_MINOR_COMPONENT_HWSS,
				"%s: failed to prepare HWSS parameters\n",
				__func__);
		return HWSS_RESULT_ERROR;
	}

	/* Program stutter and other display marks to safe values to avoid
	 * corruption. */
	hws->funcs->set_safe_displaymark(hws, path_set,
				build_params->wm_input_params,
				build_params->params_num);

	dal_hw_sequencer_free_path_parameters(build_params);

	return HWSS_RESULT_OK;
}

enum hwss_result dal_hw_sequencer_set_displaymark(
		struct hw_sequencer *hws,
		struct hw_path_mode_set *path_set)
{
	struct hwss_build_params *build_params = NULL;
	union hwss_build_params_mask params_mask;

	if (!path_set)
		return HWSS_RESULT_ERROR;

	dal_logger_write(hws->dal_context->logger,
		LOG_MAJOR_INTERFACE_TRACE,
		LOG_MINOR_COMPONENT_HWSS,
		"%s: Setting real display mark\n", __func__);

	/* Fill information for new acquired set of display paths.
	 * We should only need watermark and bandwidth parameters for
	 * setting display marks */
	params_mask.all = 0;
	params_mask.bits.WATERMARK = true;
	params_mask.bits.BANDWIDTH = true;

	build_params = dal_hw_sequencer_prepare_path_parameters(
		hws, path_set, params_mask, false);

	if (NULL == build_params) {
		dal_logger_write(hws->dal_context->logger, LOG_MAJOR_ERROR,
				LOG_MINOR_COMPONENT_HWSS,
				"%s: failed to prepare path parameters!\n",
				__func__);
		return HWSS_RESULT_ERROR;
	}

	/* Program actual display marks from values cached in bandwidth manager
	 * gpu dynamic clock info. (see dal_gpu_update_dynamic_clock_info) */
	hws->funcs->set_displaymark(hws, path_set,
				build_params->wm_input_params,
				build_params->params_num);

	dal_hw_sequencer_free_path_parameters(build_params);

	return HWSS_RESULT_OK;
}

enum hwss_result dal_hw_sequencer_set_bit_depth_reduction_adj(
		struct hw_sequencer *hws,
		struct display_path *disp_path,
		union hw_adjustment_bit_depth_reduction *bit_depth)
{
	struct dcs *dcs = dal_display_path_get_dcs(disp_path);
	enum signal_type signal = dal_display_path_get_config_signal(
			disp_path, SINK_LINK_INDEX);
	struct bit_depth_reduction_params fmt_bit_depth = {{ 0 } };

	if (!bit_depth)
		return HWSS_RESULT_ERROR;

	if (disp_path != NULL && dcs != NULL) {
		if (dal_dcs_get_enabled_packed_pixel_format(dcs) !=
				DCS_PACKED_PIXEL_FORMAT_NOT_PACKED)
			return HWSS_RESULT_ERROR;
	}
	if ((dal_is_dvi_signal(signal) &&
		dal_adapter_service_is_feature_supported(
				FEATURE_TMDS_DISABLE_DITHERING)) ||
		(dal_is_dp_signal(signal) &&
		dal_adapter_service_is_feature_supported(
				FEATURE_DP_DISABLE_DITHERING)) ||
		(dal_is_hdmi_signal(signal) &&
		dal_adapter_service_is_feature_supported(
				FEATURE_HDMI_DISABLE_DITHERING)) ||
		(dal_is_embedded_signal(signal) &&
		dal_adapter_service_is_feature_supported(
				FEATURE_EMBEDDED_DISABLE_DITHERING)) ||
		(signal == SIGNAL_TYPE_WIRELESS))
		return HWSS_RESULT_ERROR;

	fmt_bit_depth.flags.TRUNCATE_ENABLED =
			bit_depth->bits.TRUNCATE_ENABLED;
	fmt_bit_depth.flags.TRUNCATE_DEPTH =
			bit_depth->bits.TRUNCATE_DEPTH;
	fmt_bit_depth.flags.TRUNCATE_MODE =
			bit_depth->bits.TRUNCATE_MODE;

	fmt_bit_depth.flags.SPATIAL_DITHER_ENABLED =
			bit_depth->bits.SPATIAL_DITHER_ENABLED;
	fmt_bit_depth.flags.SPATIAL_DITHER_DEPTH =
			bit_depth->bits.SPATIAL_DITHER_DEPTH;
	fmt_bit_depth.flags.SPATIAL_DITHER_MODE =
			bit_depth->bits.SPATIAL_DITHER_MODE;
	fmt_bit_depth.flags.RGB_RANDOM =
			bit_depth->bits.RGB_RANDOM;
	fmt_bit_depth.flags.FRAME_RANDOM =
			bit_depth->bits.FRAME_RANDOM;
	fmt_bit_depth.flags.HIGHPASS_RANDOM =
			bit_depth->bits.HIGHPASS_RANDOM;

	fmt_bit_depth.flags.FRAME_MODULATION_ENABLED =
			bit_depth->bits.FRAME_MODULATION_ENABLED;
	fmt_bit_depth.flags.FRAME_MODULATION_DEPTH =
			bit_depth->bits.FRAME_MODULATION_DEPTH;
	fmt_bit_depth.flags.TEMPORAL_LEVEL =
			bit_depth->bits.TEMPORAL_LEVEL;
	fmt_bit_depth.flags.FRC25 =
			bit_depth->bits.FRC_25;
	fmt_bit_depth.flags.FRC50 =
			bit_depth->bits.FRC_50;
	fmt_bit_depth.flags.FRC75 =
			bit_depth->bits.FRC_75;

	dal_controller_program_formatter_bit_depth_reduction(
			dal_display_path_get_controller(disp_path),
			&fmt_bit_depth);

	return HWSS_RESULT_OK;
}

void dal_hw_sequencer_enable_wireless_idle_detection(
		struct hw_sequencer *hws,
		bool enable)
{
	dal_logger_write(hws->dal_context->logger,
		LOG_MAJOR_WARNING,
		LOG_MINOR_COMPONENT_HWSS,
		"dal_hw_sequencer_enable_wireless_idle_detection is empty.");
}

static void program_alpha_mode(
	struct controller *crtc,
	const struct plane_blend_flags *blend_flags,
	const enum hw_pixel_encoding pixel_encoding)
{
	struct alpha_mode_cfg blending_config;
	bool alpha_enable;

	dal_memset(&blending_config, 0, sizeof(struct alpha_mode_cfg));

	/* GLOBAL_ALPHA_BLEND is not currently used, meaning that
	 * PER_PIXEL_ALPHA_BLEND is our only used case. */
	if (blend_flags->bits.PER_PIXEL_ALPHA_BLEND == 1) {
		blending_config.flags.bits.MODE_IS_SET = 1;
		blending_config.mode = ALPHA_MODE_PIXEL;
		/* TODO we need to understand why MODE_MULTIPLIED bits
		 * are set. These seem to be the root of
		 * color corruption on HDMI.
		 * We'll only set these for RGB for now to avoid color
		 * corruption with YCbCr outputs.
		 */
		if (pixel_encoding == HW_PIXEL_ENCODING_RGB) {
			blending_config.flags.bits.MODE_MULTIPLIED_IS_SET = 1;
			blending_config.flags.bits.MULTIPLIED_MODE = 1;
		}
	}

	alpha_enable = dal_controller_program_alpha_blending(
			crtc,
			&blending_config);

	dal_line_buffer_enable_alpha(
			dal_controller_get_line_buffer(crtc),
			alpha_enable);
}

static enum hwss_result set_path_mode_front_end(
	struct hw_sequencer *hws,
	struct hw_path_mode *path_mode,
	uint32_t hw_path_mode_id,
	uint32_t plane_id,
	struct display_path_plane *plane,
	struct hwss_build_params *build_params)
{
	enum color_space color_space;
	const struct hw_mode_info *mode_info = &path_mode->mode;
	struct controller *crtc = plane->controller;
	struct controller *root_controller =
		dal_display_path_get_controller(path_mode->display_path);
	struct vector *plane_configs = path_mode->plane_configs;
	const struct plane_config *pl_cfg = NULL;

	dal_bandwidth_manager_program_pix_dur(
		dal_controller_get_bandwidth_manager(crtc),
		dal_controller_get_id(crtc),
		path_mode->mode.timing.pixel_clock);

	/* EPR #: 412143 - [AOSP][LL] 4K single DP light up failed
	 * This call to enable_advanced_request is added for MPO. However, it
	 * causes this EPR. The real root cause is unknown. For now, we just
	 * comment it out.
	 * TODO: Investigate this issue and find the real root cause. */
	/* dal_controller_set_advanced_request(crtc,
	true, &path_mode->mode.timing); */

	dal_controller_program_blanking(crtc, &path_mode->mode.timing);

	dal_controller_set_fe_clock(crtc, true);

	if (crtc != root_controller) {
		uint32_t display_clock = 643000;

		dal_display_clock_set_clock(
			dal_controller_get_display_clock(crtc), display_clock);

		{
			struct watermark_input_params params;

			params.controller_id = dal_controller_get_id(crtc);

			/*
			 * TODO setting pixel_format here as a temporary fix
			 * to force max watermarks. Fix properly once video
			 * playback through underlay is working.
			 */
			params.surface_pixel_format = PIXEL_FORMAT_420BPP12;
			dal_bandwidth_manager_program_watermark(
				dal_controller_get_bandwidth_manager(crtc),
				1,
				&params, display_clock);
			dal_bandwidth_manager_program_display_mark(
				dal_controller_get_bandwidth_manager(crtc),
				1,
				&params,
				display_clock);
		}
	}

	if (plane_configs)
		pl_cfg = plane_configs_vector_at_index(
				plane_configs,
				plane_id);


	color_space = translate_to_color_space(
			path_mode->mode.color_space);

	program_adjustments(
		hws,
		path_mode,
		build_params,
		color_space,
		hw_path_mode_id,
		crtc);

	/* program Scaler */
	program_scaler(
		hws,
		path_mode,
		&mode_info->timing,
		build_params,
		crtc,
		dal_controller_get_bandwidth_manager(crtc),
		hw_path_mode_id,
		plane_id,
		color_space);

	dal_controller_set_blender_mode(
		crtc,
		plane->blnd_mode);

	if (pl_cfg) {
		program_alpha_mode(
				crtc,
				&pl_cfg->attributes.blend_flags,
				path_mode->mode.timing.flags.PIXEL_ENCODING);

		dal_controller_program_surface_config(
			crtc,
			&pl_cfg->config);
	}

	return HWSS_RESULT_OK;
}

static void configure_locking(struct display_path *dp, bool enable)
{
	uint8_t i;
	uint8_t num_planes = dal_display_path_get_number_of_planes(dp);
	struct display_path_plane *root_plane =
		dal_display_path_get_plane_at_index(dp, 0);

	/* main controller should be in mode 0 (master pipe) */
	dal_controller_pipe_control_lock(
		root_plane->controller,
		PIPE_LOCK_CONTROL_MODE,
		false);

	/* other contoller's mode should be 1 in order to make atomic
	 * locking/unlocking take effect. Please, refer to pseudocode for
	 * locks in BLND.doc. */
	for (i = 1; i < num_planes; ++i) {
		struct display_path_plane *plane =
			dal_display_path_get_plane_at_index(dp, i);
		struct controller *crtc = plane->controller;

		dal_controller_pipe_control_lock(
			crtc,
			PIPE_LOCK_CONTROL_MODE,
			enable);
	}
}

/**
 * Update the cursor position.
 *
 * Need to call dal_hw_sequencer_set_cursor_attributes first to program
 * cursor surface address.
 */
enum hwss_result dal_hw_sequencer_set_cursor_position(
		struct hw_sequencer *hws,
		struct display_path *dp,
		const struct cursor_position *position)
{
	struct controller *crtc = dal_display_path_get_controller(dp);

	/* TODO implement functionality for paired controllers when needed */

	if (crtc && dal_controller_set_cursor_position(crtc, position))
		return HWSS_RESULT_OK;

	return HWSS_RESULT_ERROR;
}

/**
 * Update the cursor attributes and set cursor surface address
 */
enum hwss_result dal_hw_sequencer_set_cursor_attributes(
		struct hw_sequencer *hws,
		struct display_path *dp,
		const struct cursor_attributes *attributes)
{
	struct controller *crtc = dal_display_path_get_controller(dp);

	/* TODO implement functionality for paired controllers when needed */

	if (crtc && dal_controller_set_cursor_attributes(crtc, attributes))
		return HWSS_RESULT_OK;

	return HWSS_RESULT_ERROR;
}


/**
 * Program the Front End of the Pipe.
 *
 * The Back End was already programmed by dal_hw_sequencer_set_mode().
 */
enum hwss_result dal_hw_sequencer_set_plane_config(
	struct hw_sequencer *hws,
	struct hw_path_mode_set *path_set,
	uint32_t display_index)
{
	struct dal_context *dal_context = hws->dal_context;
	uint32_t paths_num;
	struct hwss_build_params *build_params;
	union hwss_build_params_mask mask;

	paths_num = dal_hw_path_mode_set_get_paths_number(path_set);

	mask.all = 0;
	mask.bits.SCALING_TAPS = true;
	mask.bits.PLL_SETTINGS = true;
	mask.bits.MIN_CLOCKS = true;
	mask.bits.WATERMARK = true;
	mask.bits.BANDWIDTH = true;
	mask.bits.LINE_BUFFER = true;

	build_params =
		dal_hw_sequencer_prepare_path_parameters(
			hws,
			path_set,
			mask,
			false);

	if (!build_params) {
		dal_logger_write(dal_context->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_HWSS,
			"%s: prepare_path_parameters failed!\n",
			__func__);
		return HWSS_RESULT_ERROR;
	}

	{
		uint8_t i;
		uint8_t hw_path_mode_id;
		struct controller *root_controller;
		struct hw_path_mode *path_mode;
		struct display_path *dp;
		uint8_t planes_num;
		struct display_path_plane *plane;

		for (i = 0; i < paths_num; ++i) {
			path_mode =
				dal_hw_path_mode_set_get_path_by_index(
					path_set, i);
			dp = path_mode->display_path;

			if (dal_display_path_get_display_index(dp) ==
				display_index) {
				break;
			}
		}
		hw_path_mode_id = i;

		if (hw_path_mode_id == paths_num) {
			dal_logger_write(dal_context->logger,
				LOG_MAJOR_ERROR,
				LOG_MINOR_COMPONENT_HWSS,
				"%s: can't find hw_path_mode for display index %d!\n",
				__func__, display_index);
			BREAK_TO_DEBUGGER();
			return HWSS_RESULT_ERROR;
		}

		configure_locking(dp, true);

		planes_num = dal_display_path_get_number_of_planes(dp);
		root_controller = dal_display_path_get_controller(dp);

		/* While a non-root controller is programmed we
		 * have to lock the root controller. */
		dal_controller_pipe_control_lock(
			root_controller,
			PIPE_LOCK_CONTROL_GRAPHICS |
			PIPE_LOCK_CONTROL_SCL |
			PIPE_LOCK_CONTROL_BLENDER |
			PIPE_LOCK_CONTROL_SURFACE,
			true);

		for (i = 0; i < planes_num; ++i) {
			plane = dal_display_path_get_plane_at_index(dp, i);

			set_path_mode_front_end(
				hws,
				path_mode,
				hw_path_mode_id,
				i,
				plane,
				build_params);
		}

		dal_controller_pipe_control_lock(
			root_controller,
			PIPE_LOCK_CONTROL_GRAPHICS |
			PIPE_LOCK_CONTROL_SCL |
			PIPE_LOCK_CONTROL_BLENDER |
			PIPE_LOCK_CONTROL_SURFACE,
			false);
	}

	dal_hw_sequencer_free_path_parameters(build_params);

	return HWSS_RESULT_OK;
}

bool dal_hw_sequencer_update_plane_address(
	struct hw_sequencer *hws,
	struct display_path *dp,
	uint32_t num_planes,
	struct plane_addr_flip_info *info)
{
	uint32_t i;

	struct controller *crtc =  dal_display_path_get_controller(dp);

	if (!crtc)
		return false;

	dal_controller_pipe_control_lock(
		crtc,
		PIPE_LOCK_CONTROL_SURFACE,
		true);

	for (i = 0; i < num_planes; i++) {
		const struct plane_addr_flip_info *plane_info = NULL;
		struct display_path_plane *plane;

		plane_info = &info[i];
		plane = dal_display_path_get_plane_at_index(dp, i);

		if (!plane ||
			!dal_controller_program_surface_flip_and_addr(
				plane->controller,
				plane_info))
			return false;
	}

	dal_controller_pipe_control_lock(
		crtc,
		PIPE_LOCK_CONTROL_SURFACE,
		false);

	return true;
}

void dal_hw_sequencer_prepare_to_release_planes(
	struct hw_sequencer *hws,
	struct hw_path_mode_set *path_set,
	uint32_t display_index)
{
	struct controller *crtc;
	uint8_t i;
	uint8_t paths_num = dal_hw_path_mode_set_get_paths_number(path_set);
	struct display_path *dp = NULL;

	for (i = 0; i < paths_num; ++i) {
		dp =
			dal_hw_path_mode_set_get_path_by_index(
				path_set, i)->display_path;

		if (dal_display_path_get_display_index(dp) ==
			display_index) {
			break;
		}
	}

	if (dp == NULL) {
		dal_logger_write(hws->dal_context->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_HWSS,
			"%s: can't find display_path for display index %d!\n",
			__func__, display_index);
		BREAK_TO_DEBUGGER();
		return;
	}

	crtc = dal_display_path_get_controller(dp);

	dal_controller_pipe_control_lock(crtc, PIPE_LOCK_CONTROL_BLENDER, true);

	dal_controller_set_blender_mode(crtc, BLENDER_MODE_CURRENT_PIPE);

	dal_controller_pipe_control_lock(
		crtc,
		PIPE_LOCK_CONTROL_BLENDER,
		false);
}
