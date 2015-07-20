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
#include "include/timing_service_interface.h"
#include "include/dcs_interface.h"
#include "include/ddc_service_interface.h"
#include "include/asic_capability_types.h"
#include "include/audio_types.h"
#include "include/grph_object_ctrl_defs.h"
#include "include/bit_set.h"
#include "include/hw_sequencer_types.h"
#include "include/logger_interface.h"

#include "edid_mgr.h"
#include "edid_patch.h"
#include "vbios_dco.h"
#include "default_modes_dco.h"
#include "ddc_service.h"
#include "remote_display_receiver_modes.h" /* Wireless */

struct dcs_flags {
	uint32_t SS_SUPPORTED:1;
	uint32_t DP_AUDIO_FORCED:1;
	uint32_t DISABLE_DP_AUDIO:1;
	uint32_t DISABLE_MONITOR_RANGE_LIMITS_ON_CRT:1;
	uint32_t REPORT_SINGLE_SELECTED_TIMING:1;
	uint32_t CONTAINER_ID_INITIALIZED:1;
	uint32_t DEEP_COLOR_OVER_DP_DONGLE:1;
	uint32_t HIGH_PIXEL_CLK_OVER_DP_DONGLE:1;
	uint32_t TILED_DISPLAY_CAN_SCALE:1;
	uint32_t DP_Y_ONLY:1;
};

struct dco_funcs {
	bool (*add_mode_timing)(
		struct dcs_mode_timing_list *list,
		struct timing_service *ts);
};

struct dcs {
	struct dal_context *dal;
	struct adapter_service *as;
	struct timing_service *ts;
	struct ddc_service *ddc_service;
	struct edid_mgr *edid_mgr;
	struct dcs_flags flags;
	struct dcs_container_id container_id;
	struct dcs_cea_audio_mode_list *audio_modes;
	struct dcs_cea_audio_mode_list *default_wireless_audio_modes;
	struct remote_display_receiver_modes *rdrm;
	struct display_sink_capability sink_caps;
	enum dcs_interface_type display_type;
	enum dcs_packed_pixel_format packed_pixel_format;
	struct dcs_edid_supported_max_bw edid_max_bw;
	struct timing_limits timing_limits;
	struct drr_config drr_config;

	struct graphics_object_id graphics_object_id;

	struct vbios_dco *vbios_dco;

	struct dco_funcs dco_funcs;

	struct dcs_stereo_3d_features stereo_3d_features[TIMING_3D_FORMAT_MAX];
	union stereo_3d_support stereo_3d_support;
	bool supports_miracast;

	bool dp_y_only;
};

static bool get_default_color_depth(
	struct dcs *dcs,
	struct display_color_depth_support *color_depth)
{
	struct edid_base *edid = NULL;

	color_depth->mask = 0;
	color_depth->deep_color_native_res_only = false;

	/* Wireless has fixed color depth support */
	if (dcs->display_type == INTERFACE_TYPE_WIRELESS) {
		color_depth->mask |= COLOR_DEPTH_INDEX_888;
		return true;
	}

	/* Get color depth from EDID base block first */
	if (dcs->edid_mgr != NULL) {
		edid = dal_edid_mgr_get_edid(dcs->edid_mgr);
		if (edid != NULL && edid->error.BAD_CHECKSUM)
			edid = NULL;
	}

	if (edid != NULL &&
			edid->funcs->get_display_color_depth(
					edid, color_depth))
		return true;

	/* If EDID does not have color depth info, use fixed value */
	switch (dcs->display_type) {
	case INTERFACE_TYPE_TV:
	case INTERFACE_TYPE_CV:
		color_depth->mask |= COLOR_DEPTH_INDEX_101010;
		break;
	case INTERFACE_TYPE_VGA:
	case INTERFACE_TYPE_DVI:
	case INTERFACE_TYPE_LVDS:
	case INTERFACE_TYPE_WIRELESS:
		color_depth->mask |= COLOR_DEPTH_INDEX_888;
		break;
	case INTERFACE_TYPE_DP:
		color_depth->mask |= COLOR_DEPTH_INDEX_666;
		break;
	case INTERFACE_TYPE_EDP:
		color_depth->mask |= COLOR_DEPTH_INDEX_888;
		color_depth->mask |= COLOR_DEPTH_INDEX_666;
		break;

	default:
		/* unhandled signal */
		dal_logger_write(dcs->dal->logger,
				LOG_MAJOR_DCS,
				LOG_MINOR_COMPONENT_DISPLAY_CAPABILITY_SERVICE,
				"%s: unhandled signal type",
				__func__);
		break;
	}

	return color_depth->mask != 0;
}

static bool get_default_pixel_encoding(
	struct dcs *dcs,
	struct display_pixel_encoding_support *pixel_encoding)
{
	struct edid_base *edid = NULL;

	pixel_encoding->mask = 0;

	/* Wireless has fixed color depth support */
	if (dcs->display_type == INTERFACE_TYPE_WIRELESS) {
		pixel_encoding->mask |= PIXEL_ENCODING_MASK_YCBCR444;
		return true;
	}

	/* Get pixel encoding from EDID base block first */
	if (dcs->edid_mgr != NULL) {
		edid = dal_edid_mgr_get_edid(dcs->edid_mgr);
		if (edid != NULL && edid->error.BAD_CHECKSUM)
			edid = NULL;
	}

	if (edid != NULL &&
			edid->funcs->get_display_pixel_encoding(
					edid, pixel_encoding))
		return true;


	switch (dcs->display_type) {
	case INTERFACE_TYPE_TV:
	case INTERFACE_TYPE_CV:
		pixel_encoding->mask |= PIXEL_ENCODING_MASK_YCBCR422;
		break;
	case INTERFACE_TYPE_VGA:
	case INTERFACE_TYPE_DVI:
	case INTERFACE_TYPE_LVDS:
	case INTERFACE_TYPE_EDP:
	case INTERFACE_TYPE_DP:
		pixel_encoding->mask |= PIXEL_ENCODING_MASK_RGB;
		break;
	default:
		/* unhandled signal */
		dal_logger_write(dcs->dal->logger,
				LOG_MAJOR_DCS,
				LOG_MINOR_COMPONENT_DISPLAY_CAPABILITY_SERVICE,
				"%s: unhandled signal type",
				__func__);
		break;
	}

	return pixel_encoding->mask != 0;
}

static void add_dp_default_audio_modes(
	struct dcs_cea_audio_mode_list *audio_modes)
{
	/* CEA-861 short audio descriptor
	 * byte[0]:[6-3] - Audio Format Code
	 * byte[0]:[2-0] - (Channels - 1)
	 * byte[1]:[6-0] - sample rate
	 *	bit 0 - 32kHz,
	 *	bit 1 - 44.1 kHz,
	 *	bit 2 - 48 kHz
	 *	bit 3 - 88.2 kHz
	 *	bit 4 - 96 kHz
	 *	bit 5 - 176.4 kHz
	 *	bit 6 - 192 kHz
	 * byte[2]:[2-0] for Linear PCM defines sample size
	 *	bit 0 - 16 bit
	 *	bit 1 - 20 bit
	 *	bit 2 - 24 bit*/
	struct cea_audio_mode audio_mode = { 0 };

	audio_mode.format_code = AUDIO_FORMAT_CODE_LINEARPCM;
	audio_mode.channel_count = 2;/* 2 channels */
	audio_mode.sample_rate = 7;/* 32, 44.1, 48 kHz supported */
	audio_mode.sample_size = 1;/* 16bit */
	dal_dcs_cea_audio_mode_list_append(audio_modes, &audio_mode);
}

static void add_dp_forced_audio_modes(
	struct dcs_cea_audio_mode_list *audio_modes)
{
	/* CEA-861 short audio descriptor*/
	struct cea_audio_mode audio_mode = { 0 };

	audio_mode.format_code = AUDIO_FORMAT_CODE_LINEARPCM;
	audio_mode.channel_count = 8;/* 8 channels */
	/*32, 44.1, 48, 88.2, 96, 176.4 kHz */
	audio_mode.sample_rate =  0x3F;
	audio_mode.sample_size = 5;/* 16bit + 24bit*/
	dal_dcs_cea_audio_mode_list_append(audio_modes, &audio_mode);

	audio_mode.format_code = AUDIO_FORMAT_CODE_AC3;
	audio_mode.channel_count = 8;/* 8 channels */
	audio_mode.sample_rate =  7;/* 32, 44.1, 48 kHz supported */
	audio_mode.max_bit_rate =  1;/*maximum bit rate in [8kHz] units*/
	dal_dcs_cea_audio_mode_list_append(audio_modes, &audio_mode);
}

static void add_hdmi_default_audio_modes(
	struct dcs_cea_audio_mode_list *audio_modes)
{
	/* CEA-861 short audio descriptor*/
	struct cea_audio_mode audio_mode = { 0 };

	audio_mode.format_code = AUDIO_FORMAT_CODE_LINEARPCM;
	audio_mode.channel_count = 2;/* 2 channels */
	audio_mode.sample_rate = 7;/* 32, 44.1, 48 kHz supported */
	audio_mode.sample_size = 1;/* 16bit */
	dal_dcs_cea_audio_mode_list_append(audio_modes, &audio_mode);
}

static void add_wireless_default_audio_modes(
	struct dcs *dcs)
{
	/* whether wireless display supports audio or
	 * not depends solely on Receiver cap */
	if (dcs->default_wireless_audio_modes != NULL && dcs->rdrm != NULL) {
		uint32_t i = 0;

		for (i = 0; i < dal_dcs_cea_audio_mode_list_get_count(
			dcs->default_wireless_audio_modes); i++) {

			/*
			* For wireless displays, it is possible
			* that the receiver only supports a
			* subset of the audio modes that are
			* encoded in the sampling rate and
			* bitdepth fields (if LPCM).  The
			* interface will return true if some
			* subset is supported, and return
			* the actual subset.  This is the
			* mode that should be appended.
			*/
			struct cea_audio_mode resultant_cea_audio_mode;

			if (dal_rdr_get_supported_cea_audio_mode(
				dcs->rdrm,
				dal_dcs_cea_audio_mode_list_at_index(
				dcs->default_wireless_audio_modes, i),
				&resultant_cea_audio_mode))
				dal_dcs_cea_audio_mode_list_append(
					dcs->audio_modes,
					&resultant_cea_audio_mode);
		}
	}

}

static void build_audio_modes(
	struct dcs *dcs)
{
	enum dcs_edid_connector_type connector_type;
	struct edid_base *edid_base = NULL;

	if (!dcs->audio_modes)
		return;

	if (dcs->edid_mgr)
		edid_base = dal_edid_mgr_get_edid(dcs->edid_mgr);

	dal_dcs_cea_audio_mode_list_clear(dcs->audio_modes);

	/* Wireless display does not consider EDID since receiver
	 * can support secondary sink*/
	if (edid_base && dcs->display_type != INTERFACE_TYPE_WIRELESS)
		dal_edid_get_cea_audio_modes(edid_base, dcs->audio_modes);

	switch (dcs->display_type) {
	case INTERFACE_TYPE_LVDS:
	case INTERFACE_TYPE_EDP:
		/*eDP Audio is not supported,
		 * disable it to enable Audio on external DP monitor */
		dal_dcs_cea_audio_mode_list_clear(dcs->audio_modes);
		return;
	case INTERFACE_TYPE_WIRELESS:
		add_wireless_default_audio_modes(dcs);
		return;
	default:
		break;
	}

	connector_type = dal_dcs_get_connector_type(dcs);

	switch (connector_type) {
	case EDID_CONNECTOR_DISPLAYPORT:
	/*DP Audio is disabled by default (till DP Audio in production)*/
		if (dcs->flags.DISABLE_DP_AUDIO) {
			dal_dcs_cea_audio_mode_list_clear(dcs->audio_modes);
			return;
		}
		/* If panel has any Audio support, we must add default
		 * fail-safe audio for VESA Compliance for DP*/
		/* The fail-safe audio mode is 32, 44, 48, uncompressed PCM */
		if (dal_dcs_cea_audio_mode_list_get_count(dcs->audio_modes))
			add_dp_default_audio_modes(dcs->audio_modes);
		/*If DP Audio is forced even though panel doesn't support
		 * Audio we add it */
		else if (dcs->flags.DP_AUDIO_FORCED)
			add_dp_forced_audio_modes(dcs->audio_modes);

		break;
	case EDID_CONNECTOR_HDMIA: {
		struct cea861_support cea861_support = { 0 };

		if (dal_dcs_cea_audio_mode_list_get_count(dcs->audio_modes))
			return;

		dal_dcs_get_cea861_support(dcs, &cea861_support);

		if (!dal_adapter_service_is_feature_supported(
			FEATURE_ALLOW_HDMI_WITHOUT_AUDIO) ||
			cea861_support.features.BASE_AUDIO)
			add_hdmi_default_audio_modes(dcs->audio_modes);
	}
		break;
	default:
		break;
	}
}

static bool is_miracast_supported(struct dcs *dcs)
{
	if (CONNECTOR_ID_MIRACAST != dal_graphics_object_id_get_connector_id(
			dcs->graphics_object_id))
		return false;

	if (0 == dal_adapter_service_get_asic_runtime_flags(dcs->as).bits.
			MIRACAST_SUPPORTED)
		return false;

	return true;
}

static bool construct(
	struct dcs *dcs,
	const struct dcs_init_data *init_data)
{
	if (!init_data->as)
		return false;

	if (!init_data->ts)
		return false;

	dcs->dal = init_data->dal;
	dcs->as = init_data->as;
	dcs->ts = init_data->ts;
	dcs->display_type = init_data->interface_type;
	dcs->graphics_object_id = init_data->grph_obj_id;

	if (dcs->display_type != INTERFACE_TYPE_CF &&
		dcs->display_type != INTERFACE_TYPE_CV &&
		dcs->display_type != INTERFACE_TYPE_TV) {

		dcs->edid_mgr = dal_edid_mgr_create(dcs->ts, dcs->as);
		if (!dcs->edid_mgr)
			return false;
	}

	/*TODO: add initializers*/
	switch (dcs->display_type) {
	case INTERFACE_TYPE_VGA:
		dcs->dco_funcs.add_mode_timing =
			dal_default_modes_dco_multi_sync_dco_add_mode_timing;
		break;
	case INTERFACE_TYPE_DP:
	case INTERFACE_TYPE_DVI:
		dcs->audio_modes = dal_dcs_cea_audio_mode_list_create(64);
		if (!dcs->audio_modes)
			goto fail;
		dcs->dco_funcs.add_mode_timing =
				dal_default_modes_dco_add_mode_timing;
		break;

	case INTERFACE_TYPE_EDP:
	case INTERFACE_TYPE_LVDS:
		dcs->vbios_dco = dal_vbios_dco_create(dcs->as);
		if (!dcs->vbios_dco)
			goto fail;
		dcs->flags.SS_SUPPORTED =
			dal_vbios_dco_is_pixel_clk_ss_supported(dcs->vbios_dco);
		break;
	case INTERFACE_TYPE_WIRELESS:
		dcs->audio_modes = dal_dcs_cea_audio_mode_list_create(64);
		if (!dcs->audio_modes)
			goto fail;

		dcs->supports_miracast = is_miracast_supported(dcs);

		{
			struct remote_display_receiver_modes_init_data
				rdrm_init_data;

			dal_memset(&rdrm_init_data, 0, sizeof(rdrm_init_data));

			rdrm_init_data.supports_miracast =
					dcs->supports_miracast;

			rdrm_init_data.ts = dcs->ts;

			dcs->rdrm = dal_remote_display_receiver_modes_create(
					&rdrm_init_data);
			if (!dcs->rdrm)
				goto fail;
		}

		dcs->default_wireless_audio_modes =
			dal_dcs_cea_audio_mode_list_create(64);
		if (!dcs->default_wireless_audio_modes)
			goto fail;
		{
			struct cea_audio_mode audio_modes_uncompressed;
			/*uncompressed PCM*/
			audio_modes_uncompressed.format_code =
				AUDIO_FORMAT_CODE_LINEARPCM;
			/*stereo - two channels*/
			audio_modes_uncompressed.channel_count = 2;
			/*only 48 kHz supported*/
			audio_modes_uncompressed.sample_rate = 4;
			/*sample size in [bit]s*/
			audio_modes_uncompressed.sample_size = 7;

			dal_dcs_cea_audio_mode_list_append(
				dcs->default_wireless_audio_modes,
				&audio_modes_uncompressed);

			/*TODO: Add wireless
			 * compressed audio
			 * feature code here*/
		}

		dcs->dco_funcs.add_mode_timing =
				dal_default_wireless_dco_add_mode_timing;
		break;

	default:
		break;
	}

	if (dal_graphics_object_id_get_connector_id(dcs->graphics_object_id) ==
		CONNECTOR_ID_DISPLAY_PORT &&
		dal_adapter_service_is_feature_supported(
			FEATURE_ALLOW_HDMI_HIGH_CLK_DP_DONGLE)) {
			dcs->flags.DEEP_COLOR_OVER_DP_DONGLE = true;
			dcs->flags.HIGH_PIXEL_CLK_OVER_DP_DONGLE = true;
	}

	return true;

fail:
	if (dcs->edid_mgr)
		dal_edid_mgr_destroy(&dcs->edid_mgr);

	if (dcs->rdrm)
		dal_remote_display_receiver_modes_destroy(&dcs->rdrm);

	return false;

}

struct dcs *dal_dcs_create(const struct dcs_init_data *init_data)
{
	struct dcs *dcs = dal_alloc(sizeof(struct dcs));

	if (!dcs)
		return NULL;

	if (construct(dcs, init_data))
		return dcs;

	dal_free(dcs);
	return NULL;
}

static void destruct(
	struct dcs *dcs)
{
	if (dcs->edid_mgr)
		dal_edid_mgr_destroy(&dcs->edid_mgr);

	if (dcs->audio_modes)
		dal_dcs_cea_audio_mode_list_destroy(&dcs->audio_modes);

	if (dcs->vbios_dco)
		dal_vbios_dco_destroy(&dcs->vbios_dco);

	if (dcs->rdrm)
		dal_remote_display_receiver_modes_destroy(&dcs->rdrm);

	if (dcs->default_wireless_audio_modes)
		dal_dcs_cea_audio_mode_list_destroy(
			&dcs->default_wireless_audio_modes);
}

void dal_dcs_destroy(struct dcs **dcs)
{
	if (!dcs || !*dcs) {
		BREAK_TO_DEBUGGER();
		return;
	}

	destruct(*dcs);
	dal_free(*dcs);
	*dcs = NULL;
}

static void apply_non_edid_based_monitor_patches(struct dcs *dcs)
{
	struct dp_receiver_id_info info = { 0 };

	if (!dcs->ddc_service || !dcs->edid_mgr)
		return;
	/* apply dp receiver id based patches*/
	/* get dp receiver id info from DdcService*/
	if (!dal_ddc_service_get_dp_receiver_id_info(dcs->ddc_service, &info)) {
		BREAK_TO_DEBUGGER();
		return;
	}
	/* call edid manager to update patches based on dp receiver ids.*/
	dal_edid_mgr_update_dp_receiver_id_based_monitor_patches(
		dcs->edid_mgr, &info);

	/* apply other non-edid based patches (for future) */
}

enum edid_retrieve_status dal_dcs_retrieve_raw_edid(struct dcs *dcs)
{
	enum edid_retrieve_status ret = EDID_RETRIEVE_FAIL;

	if (!dcs->edid_mgr)
		return EDID_RETRIEVE_FAIL;

	/* vbios has always higher priority */
	/* If VBios has Edid, we use it instead reading DDC line
	 * (typically for embedded panels only)*/
	if (dcs->vbios_dco &&
		dal_vbios_dco_get_edid_buff(dcs->vbios_dco) &&
		dal_vbios_dco_get_edid_buff_len(dcs->vbios_dco) &&
		!dal_adapter_service_is_feature_supported(
			FEATURE_EDID_STRESS_READ))
		ret = EDID_RETRIEVE_SAME_EDID;
	/* Read Edid from DDC line (ask DDC service to sync up
	 * with the display connected to the DDC line) */
	else if (dcs->ddc_service) {
		union dcs_monitor_patch_flags patch_flags =
			dal_dcs_get_monitor_patch_flags(dcs);

		if (patch_flags.flags.DELAY_BEFORE_READ_EDID) {

			const struct monitor_patch_info *patch_info =
			dal_dcs_get_monitor_patch_info(
				dcs,
				MONITOR_PATCH_TYPE_DELAY_BEFORE_READ_EDID);
			if (patch_info)
				dal_sleep_in_milliseconds(patch_info->param);

		}

		dal_ddc_service_optimized_edid_query(dcs->ddc_service);

		ret = dal_edid_mgr_update_edid_raw_data(dcs->edid_mgr,
			dal_ddc_service_get_edid_buf_len(dcs->ddc_service),
			dal_ddc_service_get_edid_buf(dcs->ddc_service));
	}
	/* DDC serivce (temporary) not available - reset EDID*/
	else
		ret = dal_edid_mgr_update_edid_raw_data(
			dcs->edid_mgr, 0, NULL);

	if (ret == EDID_RETRIEVE_FAIL_WITH_PREVIOUS_SUCCESS)
		build_audio_modes(dcs);

	apply_non_edid_based_monitor_patches(dcs);

	return ret;
}

uint32_t dal_dcs_get_edid_raw_data_size(struct dcs *dcs)
{
	if (!dcs->edid_mgr)
		return 0;

	return dal_edid_mgr_get_edid_raw_data_size(dcs->edid_mgr);
}

const uint8_t *dal_dcs_get_edid_raw_data(
	struct dcs *dcs,
	uint32_t *buff_size)
{
	if (!dcs->edid_mgr)
		return NULL;

	return dal_edid_mgr_get_edid_raw_data(dcs->edid_mgr, buff_size);
}

static void update_monitor_packed_pixel_format(struct dcs *dcs)
{
	struct display_color_depth_support color_depth = { 0 };
	const struct monitor_patch_info *patch_info;
	struct edid_base *edid_base;
	struct asic_feature_flags feature_flags;

	feature_flags = dal_adapter_service_get_feature_flags(dcs->as);

	/* Verify Edid present and feature supported*/
	dcs->packed_pixel_format = DCS_PACKED_PIXEL_FORMAT_NOT_PACKED;

	if (!dcs->edid_mgr || !feature_flags.bits.PACKED_PIXEL_FORMAT)
		return;

	edid_base = dal_edid_mgr_get_edid(dcs->edid_mgr);

	/* Verify this is not native 10 bit*/
	if (edid_base &&
		dal_edid_get_display_color_depth(edid_base, &color_depth))
		if (color_depth.mask & COLOR_DEPTH_INDEX_101010)
			return;

	/* Obtain packed pixel info from relevant patch*/
	patch_info = dal_edid_mgr_get_monitor_patch_info(
		dcs->edid_mgr, MONITOR_PATCH_TYPE_PACKED_PIXEL_FORMAT);

	if (!patch_info)
		patch_info = dal_edid_mgr_get_monitor_patch_info(
			dcs->edid_mgr, MONITOR_PATCH_TYPE_MULTIPLE_PACKED_TYPE);

	if (patch_info)
		dcs->packed_pixel_format = patch_info->param;
}

static void build_drr_settings(struct dcs *dcs)
{
	uint32_t feature_value = 0;
	struct edid_base *edid_base = NULL;

	dal_memset(&dcs->timing_limits, 0, sizeof(struct timing_limits));
	dal_memset(&dcs->drr_config, 0, sizeof(struct drr_config));

	/* PART A */
	/* FID8860, check whether OEM specified percentage for maximum safe
	 * pixel clock get original pixel clock for LVDS panel from VBIOS*/
	if ((INTERFACE_TYPE_LVDS == dcs->display_type) ||
			(INTERFACE_TYPE_EDP == dcs->display_type)) {
		uint32_t pixel_clk = 0;
		uint64_t factor = 10000;
		/* Calculate safe range factor for max pixel clock*/

		/* maximum value for safe pixel clock is given in [0.01%] units
		 * get percentage factor, for example 325 (3.25%) the factor is
		 *  1.0325 (or 10325 in [0.01%] units)*/

		if (dal_adapter_service_get_feature_value(
			FEATURE_LVDS_SAFE_PIXEL_CLOCK_RANGE,
			&feature_value,
			sizeof(feature_value)))
			factor += feature_value;

		if (NULL != dcs->vbios_dco)
			pixel_clk = dal_vbios_dco_get_pixel_clk_for_drr_khz(
				dcs->vbios_dco);

		if (dcs->edid_mgr)
			edid_base = dal_edid_mgr_get_edid(dcs->edid_mgr);

		/* Check EDID if not found in VBIOS */
		if (0 == pixel_clk && edid_base)
			pixel_clk = dal_edid_get_drr_pixel_clk_khz(edid_base);

		/* set timing limits */
		dcs->timing_limits.min_pixel_clock_in_khz = pixel_clk;
		dcs->timing_limits.max_pixel_clock_in_khz =
			div_u64(factor * pixel_clk, 10000);
	}

	/* PART B - DRR Feature */
	/* VBIOS override and runtime parameter override is only for embedded */
	if ((INTERFACE_TYPE_LVDS == dcs->display_type) ||
			(INTERFACE_TYPE_EDP == dcs->display_type)) {
		/* First try to find DRR capability from runtime parameter.
		 * DRR runtime parameter may disable feature. */
		if (!dal_adapter_service_get_feature_value(
			FEATURE_DRR_SUPPORT,
			&feature_value,
			sizeof(feature_value)))
			return;

		/* DRR is not supported if disabled by runtime parameter. */
		if (feature_value == AS_DRR_SUPPORT_DISABLED)
			return;
		else if (feature_value >= AS_DRR_SUPPORT_MIN_FORCED_FPS) {
			dcs->drr_config.min_fps_in_microhz = feature_value;
			if (dcs->drr_config.min_fps_in_microhz > 0)
				dcs->drr_config.support_method.FORCED_BY_REGKEY_OR_ESCAPE
					= true;
		}


		/* Check VBIOS if not forced by runtime parameter */
		if (0 == dcs->drr_config.min_fps_in_microhz &&
				NULL != dcs->vbios_dco) {
			dcs->drr_config.min_fps_in_microhz =
				dal_vbios_dco_get_min_fps_for_drr(
						dcs->vbios_dco);
			if (dcs->drr_config.min_fps_in_microhz > 0) {
				dcs->drr_config.support_method.FORCED_BY_VBIOS
					= true;
			}
		}

	} else {
		/* For non-embedded displays, check if DRR support is disabled
		 * by runtime parameter */
		if (dal_adapter_service_is_feature_supported(
				FEATURE_SUPPORT_EXTERNAL_PANEL_DRR)) {
			/* DRR is not supported on external panels if disabled
			 * by runtime parameter. */
			return;
		}
	}

	/* Finally check EDID if not found in VBIOS or runtime parameters.
	 * EDID method of supporting DRR is possible on both external and
	 * internal panels. */
	if (0 == dcs->drr_config.min_fps_in_microhz && NULL != edid_base) {
		dcs->drr_config.min_fps_in_microhz =
			dal_edid_get_min_drr_fps(edid_base);
		if (dcs->drr_config.min_fps_in_microhz > 0) {
			dcs->drr_config.support_method.SUPPORTED_BY_EDID =
					true;
		}
	}

	dcs->drr_config.min_fps_in_microhz *= 1000000;/*convert to microHz*/
	dcs->drr_config.force_lock_on_event = 0;/* default no flip lock*/
	dcs->drr_config.lock_to_master_vsync = 0;/* default no vsync lock*/
}

static void update_cached_data(struct dcs *dcs)
{
	/* build audio modes for this display*/
	build_audio_modes(dcs);

	/* Update cached display packed pixel format from Edid */
	update_monitor_packed_pixel_format(dcs);

	/* update ranged timing information */
	build_drr_settings(dcs);

}

enum edid_retrieve_status dal_dcs_update_edid_from_last_retrieved(
	struct dcs *dcs)
{
	enum edid_retrieve_status ret;

	if (!dcs->edid_mgr) {
		BREAK_TO_DEBUGGER();
		return EDID_RETRIEVE_FAIL;
	}

	ret = dal_edid_mgr_update_edid_from_last_retrieved(dcs->edid_mgr);

	if (ret == EDID_RETRIEVE_SUCCESS) {

		if (dcs->display_type != INTERFACE_TYPE_DP) {
			/* For any other interface then DP we fail detection
			 * if EDID has bad checksum */
			if (dal_edid_get_errors(
				dal_edid_mgr_get_edid(
					dcs->edid_mgr))->BAD_CHECKSUM)
				return EDID_RETRIEVE_FAIL;

			/* new range limit dco or update the range limit */
			/*TODO: add range limit dco */
		}

		update_cached_data(dcs);

	} else if (ret == EDID_RETRIEVE_FAIL_WITH_PREVIOUS_SUCCESS)
		build_audio_modes(dcs);


	return ret;
}

/*Update DDC Service.  returns the old DdcService being replaced*/
struct ddc_service *dal_dcs_update_ddc(
	struct dcs *dcs,
	struct ddc_service *ddc)
{
	struct ddc_service *old_ddc_service = dcs->ddc_service;

	dcs->ddc_service = ddc;
	return old_ddc_service;
}

union dcs_monitor_patch_flags dal_dcs_get_monitor_patch_flags(struct dcs *dcs)
{
	union dcs_monitor_patch_flags flags = { { 0 } };

	if (dcs->edid_mgr)
		return dal_edid_mgr_get_monitor_patch_flags(dcs->edid_mgr);

	return flags;
}

void dal_dcs_set_transaction_type(
	struct dcs *dcs,
	enum ddc_transaction_type type)
{
	union dcs_monitor_patch_flags patch_flags =
		dal_dcs_get_monitor_patch_flags(dcs);

	if (!dcs->ddc_service)
		return;

	/* overwrite transaction type to increase retry attempt*/
	if (DDC_TRANSACTION_TYPE_I2C != type &&
		patch_flags.flags.INCREASE_DEFER_WRITE_RETRY_I2C_OVER_AUX)
		type = DDC_TRANSACTION_TYPE_I2C_OVER_AUX_RETRY_DEFER;

	dal_ddc_service_set_transaction_type(dcs->ddc_service, type);
}

static void update_edid_supported_max_bw(
	struct dcs *dcs,
	struct mode_timing *mt)
{
	uint32_t bpp = 24;

	switch (mt->mode_info.timing_source) {
	case TIMING_SOURCE_EDID_DETAILED:
	case TIMING_SOURCE_EDID_ESTABLISHED:
	case TIMING_SOURCE_EDID_STANDARD:
	case TIMING_SOURCE_EDID_CEA_SVD:
	case TIMING_SOURCE_EDID_CEA_SVD_3D:
	case TIMING_SOURCE_EDID_CVT_3BYTE:
	case TIMING_SOURCE_EDID_4BYTE:
		break;
	default:
		return;
	}

	switch (mt->crtc_timing.display_color_depth) {
	case DISPLAY_COLOR_DEPTH_666:
		bpp = 18;
		break;
	case DISPLAY_COLOR_DEPTH_888:
		bpp = 24;
		break;
	case DISPLAY_COLOR_DEPTH_101010:
		bpp = 30;
		break;
	case DISPLAY_COLOR_DEPTH_121212:
		bpp = 36;
		break;
	case DISPLAY_COLOR_DEPTH_141414:
		bpp = 42;
		break;
	case DISPLAY_COLOR_DEPTH_161616:
		bpp = 48;
		break;
	default:
		bpp = 24;
		break;
	}

	if ((dcs->edid_max_bw.pix_clock_khz * dcs->edid_max_bw.bits_per_pixel) <
		(mt->crtc_timing.pix_clk_khz * bpp)) {
		dcs->edid_max_bw.pix_clock_khz = mt->crtc_timing.pix_clk_khz;
		dcs->edid_max_bw.bits_per_pixel = bpp;
	}
}

static bool is_mode_timing_tiled(
	struct dcs_display_tile *display_tile,
	struct mode_timing *mode_timing)
{
	uint32_t tw_mh =
		display_tile->width * mode_timing->mode_info.pixel_height;
	uint32_t th_mw =
		display_tile->height * mode_timing->mode_info.pixel_width;

	return (tw_mh * 90 < th_mw * 100) && (tw_mh * 110 < th_mw * 100);
}

static void get_tile_info(
	struct dcs *dcs,
	struct edid_base *edid_base,
	struct dcs_display_tile *display_tile)
{
	const struct monitor_patch_info *patch_info;
	struct vendor_product_id_info tiled_vendor_info = { 0 };

	if (dal_edid_get_display_tile_info(edid_base, display_tile)) {
		dcs->flags.TILED_DISPLAY_CAN_SCALE =
			display_tile->flags.CAN_SCALE;
		return;
	}

	patch_info = dal_dcs_get_monitor_patch_info(
			dcs, MONITOR_PATCH_TYPE_TILED_DISPLAY);

	dcs->flags.TILED_DISPLAY_CAN_SCALE = false;

	if (!patch_info || patch_info->param == EDID_TILED_DISPLAY_NONE)
		return;

	if (!dal_dcs_get_vendor_product_id_info(dcs, &tiled_vendor_info))
		return;

	display_tile->flags.CAN_SCALE = 0;
	display_tile->height = TILED_DISPLAY_VERTICAL;
	display_tile->width = TILED_DISPLAY_HORIZONTAL;
	/*row and column not important in here.*/
	/* No bezel Info */
	/* No single Enclosure */
	display_tile->topology_id.manufacturer_id =
		tiled_vendor_info.manufacturer_id;
	display_tile->topology_id.product_id =
		tiled_vendor_info.product_id;
	display_tile->topology_id.serial_id = dcs->graphics_object_id.enum_id;
}

static enum pixel_encoding dcs_pixel_encoding_to_ts_pixel_encoding(
	enum pixel_encoding_mask encoding_value)
{
	switch (encoding_value) {
	case PIXEL_ENCODING_MASK_YCBCR444:
		return PIXEL_ENCODING_YCBCR444;
	case PIXEL_ENCODING_MASK_YCBCR422:
		return PIXEL_ENCODING_YCBCR422;
	case PIXEL_ENCODING_MASK_RGB:
		return PIXEL_ENCODING_RGB;
	default:
		break;
	}

	return PIXEL_ENCODING_UNDEFINED;
}

static enum display_color_depth dcs_color_depth_to_ts_color_depth(
	uint32_t color_value)
{
	switch (color_value) {
	case COLOR_DEPTH_INDEX_666:
		return DISPLAY_COLOR_DEPTH_666;
	case COLOR_DEPTH_INDEX_888:
		return DISPLAY_COLOR_DEPTH_888;
	case COLOR_DEPTH_INDEX_101010:
		return DISPLAY_COLOR_DEPTH_101010;
	case COLOR_DEPTH_INDEX_121212:
		return DISPLAY_COLOR_DEPTH_121212;
	case COLOR_DEPTH_INDEX_141414:
		return DISPLAY_COLOR_DEPTH_141414;
	case COLOR_DEPTH_INDEX_161616:
		return DISPLAY_COLOR_DEPTH_161616;
	default:
		break;
	}

	return DISPLAY_COLOR_DEPTH_UNDEFINED;
}
static bool should_insert_mode(
	struct dcs *dcs,
	struct mode_timing *mode_timing,
	bool deep_color)
{
	bool ce_mode = dal_timing_service_is_ce_timing_standard(
		mode_timing->crtc_timing.timing_standard);
	bool insert_mode = false;

	switch (mode_timing->crtc_timing.pixel_encoding) {
	case PIXEL_ENCODING_YCBCR444:
		/* 3) for CE timing, we allow all color depth for YCbCr444
		 * if deep color for YCbCr supported.
		 * 888 color depths always supported
		 * 4) VCE/Wireless only supports YCbCr444 so always allow it */
		if (ce_mode || dcs->display_type == INTERFACE_TYPE_WIRELESS)
			if (deep_color ||
				mode_timing->crtc_timing.display_color_depth <=
				DISPLAY_COLOR_DEPTH_888)
				insert_mode = true;

		if (dcs->display_type == INTERFACE_TYPE_DP &&
			dcs->flags.DP_Y_ONLY)
			mode_timing->crtc_timing.flags.YONLY = 1;

		break;
	case PIXEL_ENCODING_YCBCR422:
		/* 2) for CE timing, we allow 888 only for YCbCr422.
		 * All other depths can be supported by YCbCr444 */
		if (ce_mode && mode_timing->crtc_timing.display_color_depth <=
			DISPLAY_COLOR_DEPTH_121212)
			insert_mode = true;

		break;
	case PIXEL_ENCODING_RGB:
		/* 1) for RGB, we allow all color depth.*/
		insert_mode = true;
		break;


	default:
		break;

	}
	return insert_mode;
}

static void add_edid_mode(
	struct dcs *dcs,
	struct dcs_mode_timing_list *list,
	struct mode_timing *mode_timing,
	struct display_color_and_pixel_support *support)
{

	uint32_t color_value;
	uint32_t pixel_value;
	struct bit_set_iterator_32 color_depth_it;
	struct bit_set_iterator_32 pixel_encoding_it;

	bit_set_iterator_construct(
			&color_depth_it,
			support->color_depth_support.mask);

	while ((color_value = get_next_significant_bit(&color_depth_it)) != 0) {

		mode_timing->crtc_timing.display_color_depth =
			dcs_color_depth_to_ts_color_depth(color_value);

		/* Skip deep color on non-native timing (typically DVI only) */
		if (mode_timing->mode_info.timing_source !=
			TIMING_SOURCE_EDID_DETAILED &&
			mode_timing->crtc_timing.display_color_depth >
			DISPLAY_COLOR_DEPTH_888 &&
			support->color_depth_support.deep_color_native_res_only)
			continue;

		bit_set_iterator_construct(
				&pixel_encoding_it,
				support->pixel_encoding_support.mask);

		while ((pixel_value =
			get_next_significant_bit(&pixel_encoding_it)) != 0) {

			mode_timing->crtc_timing.pixel_encoding =
				dcs_pixel_encoding_to_ts_pixel_encoding(
					pixel_value);

			if (should_insert_mode(
				dcs,
				mode_timing,
				support->deep_color_y444_support))
				dal_dcs_mode_timing_list_append(
					list, mode_timing);
		}
	}
}

static void add_edid_modes(
	struct dcs *dcs,
	struct dcs_mode_timing_list *list,
	bool *preffered_found)
{
	struct edid_base *edid_base;
	struct display_color_and_pixel_support clr_pix_support = { { 0 } };
	struct cea_vendor_specific_data_block vendor_block = { 0 };
	struct dcs_display_tile display_tile = { 0 };
	bool ret_color_depth, ret_pixel_encoding;
	struct dcs_mode_timing_list *edid_list;

	if (!dcs->edid_mgr)
		return;

	edid_base = dal_edid_mgr_get_edid(dcs->edid_mgr);

	if (!edid_base)
		return;

	edid_list = dal_dcs_mode_timing_list_create(128);

	dal_edid_get_supported_mode_timing(
		edid_base, edid_list, preffered_found);

	ret_color_depth = dal_edid_get_display_color_depth(
		edid_base, &clr_pix_support.color_depth_support);

	ret_pixel_encoding = dal_edid_get_display_pixel_encoding(
		edid_base, &clr_pix_support.pixel_encoding_support);

	if (dcs->display_type == INTERFACE_TYPE_WIRELESS) {
		clr_pix_support.color_depth_support.mask =
			COLOR_DEPTH_INDEX_888;

		clr_pix_support.pixel_encoding_support.mask =
			PIXEL_ENCODING_MASK_YCBCR444;
	}

	if (dal_edid_get_cea_vendor_specific_data_block(
		edid_base, &vendor_block))
		clr_pix_support.deep_color_y444_support =
			vendor_block.byte6.DC_Y444;

	get_tile_info(dcs, edid_base, &display_tile);

	if (ret_color_depth && ret_pixel_encoding) {

		uint32_t i;
		uint32_t list_count =
			dal_dcs_mode_timing_list_get_count(edid_list);

		for (i = 0; i < list_count; ++i) {

			struct mode_timing *mode_timing =
				dal_dcs_mode_timing_list_at_index(edid_list, i);
			mode_timing->mode_info.flags.TILED_MODE =
				is_mode_timing_tiled(
					&display_tile, mode_timing);

			add_edid_mode(dcs, list, mode_timing, &clr_pix_support);
		}
	}

	dal_dcs_mode_timing_list_destroy(&edid_list);
}

static void add_overriden_modes(
	struct dcs *dcs,
	struct dcs_mode_timing_list *list,
	bool *preffered_found)
{
	/* TODO: add implementation */
}

enum { DEFAULT_3D_RIGHT_EYE_POLARITY = false };

/**
 *****************************************************************************
 * setup_projector_stereo_3d_timings
 *
 * For Projectors we have following policy: If it supports (reported in EDID)
 * 120Hz progressive mode, this mode considered as Frame Alternate 3D.
 * To make sure 3D mode gets higher priority in ModeMgr we force each such 3D
 * mode to be considered as coming from Detailed Timing section
 * NOTE: This function can be called only if Frame Alternate 3D format supported
 * in current config
 *
 * list: DCS list of modes/timings
 *****************************************************************************
 */
static void setup_projector_stereo_3d_timings(struct dcs_mode_timing_list *list)
{
	uint32_t i;

	for (i = 0; i < dal_dcs_mode_timing_list_get_count(list); i++) {
		struct mode_timing *mt =
			dal_dcs_mode_timing_list_at_index(list, i);

		/* 120Hz projector timing considered to be stereo capable
		 * (FrameAlternate, no stereosync) */
		bool progresive_120hz = mt->mode_info.field_rate == 120 &&
			!mt->mode_info.flags.INTERLACE;
		bool edid_source = false;

		switch (mt->mode_info.timing_source) {
		case TIMING_SOURCE_EDID_CEA_SVD_3D:
		case TIMING_SOURCE_EDID_DETAILED:
		case TIMING_SOURCE_EDID_ESTABLISHED:
		case TIMING_SOURCE_EDID_STANDARD:
		case TIMING_SOURCE_EDID_CEA_SVD:
		case TIMING_SOURCE_EDID_CVT_3BYTE:
		case TIMING_SOURCE_EDID_4BYTE:
			edid_source = true;
			break;

		default:
			break;
		}

		if (mt->crtc_timing.timing_3d_format == TIMING_3D_FORMAT_NONE &&
			progresive_120hz && edid_source) {
			mt->mode_info.timing_source =
				TIMING_SOURCE_EDID_DETAILED;
			mt->crtc_timing.timing_3d_format =
				TIMING_3D_FORMAT_FRAME_ALTERNATE;
			mt->crtc_timing.flags.EXCLUSIVE_3D = false;
			mt->crtc_timing.flags.RIGHT_EYE_3D_POLARITY =
				DEFAULT_3D_RIGHT_EYE_POLARITY;
			/* We will not create corresponding 2D timing */
			mt->crtc_timing.flags.USE_IN_3D_VIEW_ONLY = false;
		}
	}
}

/*
 * get_supported_3d_format
 *
 * Given requested 3D format, returns supported 3D format (in most cases same
 * as given), considering validation of supported 3D features
 *
 * format - requested 3D format
 * connectType - display connection type
 * interlace - true if requested supported for interlace timing
 *
 */
static enum timing_3d_format get_supported_3d_format(
	struct dcs *dcs,
	enum timing_3d_format format,
	enum dcs_edid_connector_type connect_type,
	bool interlace)
{
	enum timing_3d_format supported_format = TIMING_3D_FORMAT_NONE;

	/* Special case is when there is an active DP->HDMI converter.
	 * If converter has the "Frame Sequential-to-Frame Pack" capability,
	 * then positively validate *only* the TIMING_3D_FORMAT_HW_FRAME_PACKING
	 * format (because it will be converted to
	 * TIMING_3D_FORMAT_HW_FRAME_PACKING by the active converter). */
	if (connect_type == EDID_CONNECTOR_HDMIA &&
		DISPLAY_DONGLE_DP_HDMI_CONVERTER ==
			dcs->sink_caps.dongle_type) {
		supported_format = TIMING_3D_FORMAT_NONE;

		if (TIMING_3D_FORMAT_HW_FRAME_PACKING == format
			&& dcs->sink_caps.is_dp_hdmi_s3d_converter) {
			if (dcs->stereo_3d_support.
				bits.DISPLAY_PORT_FRAME_ALT)
				supported_format =
					TIMING_3D_FORMAT_DP_HDMI_INBAND_FA;
		}
	}

	/* TODO: finish implementation */

	return supported_format;
}

static bool are_3d_formats_compatible(
	enum timing_3d_format f1,
	enum timing_3d_format f2)
{
	bool compatible = true;

	/* First invert the order since the comparison operation itself is not
	 * symmetric */
	if (f2 == TIMING_3D_FORMAT_SW_FRAME_PACKING) {
		enum timing_3d_format tmp = f1;

		f1 = f2;
		f2 = tmp;
	}

	/* Now do comparision */
	if (f1 == TIMING_3D_FORMAT_SW_FRAME_PACKING)
		switch (f2) {
		case TIMING_3D_FORMAT_FRAME_ALTERNATE:
		case TIMING_3D_FORMAT_INBAND_FA:
		case TIMING_3D_FORMAT_DP_HDMI_INBAND_FA:
		case TIMING_3D_FORMAT_SIDEBAND_FA:
		case TIMING_3D_FORMAT_HW_FRAME_PACKING:
		case TIMING_3D_FORMAT_ROW_INTERLEAVE:
		case TIMING_3D_FORMAT_COLUMN_INTERLEAVE:
		case TIMING_3D_FORMAT_PIXEL_INTERLEAVE:
			compatible = false;
			break;

		default:
			break;
		}

	return compatible;
}

static void update_stereo_3d_features(
	struct dcs *dcs,
	struct dcs_mode_timing_list *list)
{
	bool override_per_timing_format = false;
	bool all_right_eye_polarity = DEFAULT_3D_RIGHT_EYE_POLARITY;
	enum timing_3d_format all_timings_format = TIMING_3D_FORMAT_NONE;
	enum dcs_edid_connector_type conn_type =
		dal_dcs_get_connector_type(dcs);
	uint32_t i;

	uint32_t list_count = dal_dcs_mode_timing_list_get_count(list);

	dal_memset(
		&dcs->stereo_3d_features, 0, sizeof(dcs->stereo_3d_features));

	/* Setup 3D timings for projector (Projectors default format is Frame
	 * Alternate) */
	if (dcs->stereo_3d_support.bits.FRAME_ALTERNATE && dcs->edid_mgr &&
		dal_edid_mgr_get_edid(dcs->edid_mgr)) {
		struct edid_screen_info screen_info = {
			EDID_SCREEN_AR_UNKNOWN };
		dal_edid_get_screen_info(
			dal_edid_mgr_get_edid(dcs->edid_mgr),
			&screen_info);

		if (screen_info.aspect_ratio == EDID_SCREEN_AR_PROJECTOR)
			setup_projector_stereo_3d_timings(list);
	}

	/* Sideband Frame Alternate we treat as global 3D properties (applies to
	 * all timings) */
	if (dcs->stereo_3d_support.bits.SIDEBAND_FRAME_ALT) {
		struct gpio *stereo_gpio =
			dal_adapter_service_obtain_stereo_gpio(dcs->as);

		all_timings_format = TIMING_3D_FORMAT_SIDEBAND_FA;
		override_per_timing_format = true;
		dcs->stereo_3d_features[all_timings_format].flags.ALL_TIMINGS =
			1;

		if (stereo_gpio) {
			all_right_eye_polarity =
				dal_gpio_get_output_state(stereo_gpio) ==
					GPIO_PIN_OUTPUT_STATE_ACTIVE_HIGH;
			dal_adapter_service_release_gpio(dcs->as, stereo_gpio);
		}
	}

	/* Initialize global 3D support (only if not defined yet as Sideband FA)
	 */
	if (all_timings_format == TIMING_3D_FORMAT_NONE && dcs->edid_mgr &&
		dal_edid_mgr_get_edid(dcs->edid_mgr)) {
		/* Obtain Edid for global stereo support
		 * If this format applies to all timings then we can cache it
		 * now */
		struct edid_stereo_3d_capability stereo_capability = {
			TIMING_3D_FORMAT_NONE };
		if (dal_edid_get_stereo_3d_support(
			dal_edid_mgr_get_edid(dcs->edid_mgr),
			&stereo_capability))
			all_timings_format =
				get_supported_3d_format(
					dcs,
					stereo_capability.timing_3d_format,
					conn_type,
					false);

		if (all_timings_format != TIMING_3D_FORMAT_NONE) {
			override_per_timing_format =
				stereo_capability.override_per_timing_format;

			switch (stereo_capability.timing_3d_format) {
			case TIMING_3D_FORMAT_FRAME_ALTERNATE:
			case TIMING_3D_FORMAT_SIDEBAND_FA:
			case TIMING_3D_FORMAT_INBAND_FA:
			case TIMING_3D_FORMAT_DP_HDMI_INBAND_FA:
				all_right_eye_polarity =
					stereo_capability.frame_alternate_data.
					right_eye_polarity;
				break;

			case TIMING_3D_FORMAT_ROW_INTERLEAVE:
			case TIMING_3D_FORMAT_COLUMN_INTERLEAVE:
			case TIMING_3D_FORMAT_PIXEL_INTERLEAVE:
				all_right_eye_polarity =
					stereo_capability.interleaved_data.
					right_eye_polarity;
				break;
			default:
				break;
			}

			dcs->stereo_3d_features[all_timings_format].
			flags.ALL_TIMINGS = 1;
		}
	}

	/* Go over the whole list and patch every timing with 3D info */
	for (i = 0; i < list_count; ++i) {
		struct mode_timing *mt =
			dal_dcs_mode_timing_list_at_index(list, i);
		enum timing_3d_format timing_3d_format =
			mt->crtc_timing.timing_3d_format;

		/* Override 3D format with global format */
		if (all_timings_format != TIMING_3D_FORMAT_NONE &&
			(override_per_timing_format || timing_3d_format ==
				TIMING_3D_FORMAT_NONE)) {
			/* If this is extra 3D timing (i.e. there is also
			 * corresponding 2D timing which will be overridden),
			 * remove it from the list, so we do not get 2 identical
			 * timings with overridden 3D format */
			if (timing_3d_format != TIMING_3D_FORMAT_NONE &&
				mt->crtc_timing.flags.USE_IN_3D_VIEW_ONLY) {
				dal_dcs_mode_timing_list_remove_at_index(
					list, i);
				/* TODO: this code should be refactored */
				--i;
				--list_count;
				continue;
			}

			timing_3d_format = all_timings_format;
			mt->crtc_timing.flags.RIGHT_EYE_3D_POLARITY =
				all_right_eye_polarity;
			mt->crtc_timing.flags.EXCLUSIVE_3D = false;
			mt->crtc_timing.flags.SUB_SAMPLE_3D = true;
			/* This timing used for both 3D and 2D */
			mt->crtc_timing.flags.USE_IN_3D_VIEW_ONLY = false;
		}

		/* Force default right eye polarity according to DP spec */
		else if (timing_3d_format == TIMING_3D_FORMAT_INBAND_FA &&
			conn_type == EDID_CONNECTOR_DISPLAYPORT)
			mt->crtc_timing.flags.RIGHT_EYE_3D_POLARITY =
				DEFAULT_3D_RIGHT_EYE_POLARITY;

		/* Update supported 3D format */
		timing_3d_format = get_supported_3d_format(
			dcs,
			timing_3d_format,
			conn_type, mt->crtc_timing.flags.INTERLACE);
		if (timing_3d_format != TIMING_3D_FORMAT_NONE) {
			struct dcs_stereo_3d_features *s3d_features =
				&dcs->stereo_3d_features[timing_3d_format];
			mt->crtc_timing.timing_3d_format = timing_3d_format;

			if (!s3d_features->flags.SUPPORTED) {
				s3d_features->flags.SUPPORTED = true;

				if (timing_3d_format !=
					TIMING_3D_FORMAT_SW_FRAME_PACKING) {
					s3d_features->flags.CLONE_MODE = true;
					s3d_features->flags.SCALING = true;
				} else
					s3d_features->flags.
					SINGLE_FRAME_SW_PACKED = true;
			}
		} else if (mt->crtc_timing.timing_3d_format !=
			TIMING_3D_FORMAT_NONE) {
			/* Remove timing which intended to be used for 3D only,
			 * but now it cannot be used in 3D (means cannot be used
			 * at all) */
			if (mt->crtc_timing.flags.USE_IN_3D_VIEW_ONLY ||
				mt->crtc_timing.flags.EXCLUSIVE_3D) {
				dal_dcs_mode_timing_list_remove_at_index(
					list, i);
				/* TODO: this code should be refactored */
				--i;
				--list_count;
				continue;
			}

			mt->crtc_timing.timing_3d_format =
				TIMING_3D_FORMAT_NONE;
		}
	}

	/* Disallow all the rest formats when SW Frame Pack enabled */
	if (dcs->stereo_3d_features[TIMING_3D_FORMAT_SW_FRAME_PACKING].
		flags.SUPPORTED) {
		for (i = 0; i < list_count; ++i) {
			struct mode_timing *mt =
				dal_dcs_mode_timing_list_at_index(list, i);
			enum timing_3d_format timing_3d_format =
				mt->crtc_timing.timing_3d_format;

			if (!are_3d_formats_compatible(
				TIMING_3D_FORMAT_SW_FRAME_PACKING,
				timing_3d_format)) {
				dcs->stereo_3d_features[timing_3d_format].
				flags.SUPPORTED = false;
				mt->crtc_timing.timing_3d_format =
					TIMING_3D_FORMAT_NONE;
				mt->crtc_timing.flags.EXCLUSIVE_3D = false;
				mt->crtc_timing.
				flags.USE_IN_3D_VIEW_ONLY = false;
			}
		}
	}
}

static void add_default_modes(
		struct dcs *dcs,
		struct dcs_mode_timing_list *list)
{
	bool add_default_timings = false;
	union dcs_monitor_patch_flags flags;
	enum dcs_edid_connector_type conn_type = EDID_CONNECTOR_UNKNOWN;

	flags = dal_edid_mgr_get_monitor_patch_flags(dcs->edid_mgr);

	if (flags.flags.NO_DEFAULT_TIMINGS)
		return;

	conn_type = dal_dcs_get_connector_type(dcs);

	if (conn_type == EDID_CONNECTOR_DISPLAYPORT ||
			conn_type == EDID_CONNECTOR_HDMIA)
		add_default_timings = true;
	else {
		bool explicit_mode_exists = false;
		struct mode_timing *mode_timing = NULL;
		uint32_t list_count = dal_dcs_mode_timing_list_get_count(list);
		uint32_t i;

		for (i = list_count; i > 0; --i) {
			mode_timing =
				dal_dcs_mode_timing_list_at_index(list, i-1);

			switch (mode_timing->mode_info.timing_source) {
			case TIMING_SOURCE_EDID_CEA_SVD_3D:
			case TIMING_SOURCE_EDID_DETAILED:
			case TIMING_SOURCE_EDID_ESTABLISHED:
			case TIMING_SOURCE_EDID_STANDARD:
			case TIMING_SOURCE_EDID_CEA_SVD:
			case TIMING_SOURCE_EDID_CVT_3BYTE:
			case TIMING_SOURCE_EDID_4BYTE:
			case TIMING_SOURCE_VBIOS:
			case TIMING_SOURCE_CV:
			case TIMING_SOURCE_TV:
			case TIMING_SOURCE_HDMI_VIC:
				explicit_mode_exists = true;
				break;

			default:
				break;
			}
		}

		if (!explicit_mode_exists)
			add_default_timings = true;
	}

	if (add_default_timings && dcs->dco_funcs.add_mode_timing != NULL)
		dcs->dco_funcs.add_mode_timing(list, dcs->ts);
}

static void get_intersect_for_timing_lists(
		struct dcs *dcs,
		struct dcs_mode_timing_list *list_1,
		struct dcs_mode_timing_list *list_2,
		struct dcs_mode_timing_list *output_list)
{
	struct mode_timing *mt_1 = NULL;
	struct mode_timing *mt_2 = NULL;
	uint32_t i = 0;
	uint32_t j = 0;
	uint32_t list_count_1 = 0;
	uint32_t list_count_2 = 0;

	if (list_1 == NULL || list_2 == NULL || output_list == NULL) {
		dal_logger_write(dcs->dal->logger, LOG_MAJOR_DCS,
				LOG_MINOR_COMPONENT_DISPLAY_CAPABILITY_SERVICE,
				"%s: Invalid input", __func__);
		return;
	}

	dal_dcs_mode_timing_list_clear(output_list);

	list_count_1 = dal_dcs_mode_timing_list_get_count(list_1);
	for (i = 0; i < list_count_1; ++i) {
		mt_1 = dal_dcs_mode_timing_list_at_index(list_1, i);
		list_count_2 = dal_dcs_mode_timing_list_get_count(list_2);

		for (j = 0; j < list_count_2; ++j) {
			mt_2 = dal_dcs_mode_timing_list_at_index(list_2, j);

			if (mt_1->mode_info.pixel_height ==
				mt_2->mode_info.pixel_height &&
				mt_1->mode_info.pixel_width ==
				mt_2->mode_info.pixel_width &&
				mt_1->mode_info.field_rate ==
				mt_2->mode_info.field_rate &&
				mt_1->mode_info.flags.INTERLACE ==
				mt_2->mode_info.flags.INTERLACE &&
				mt_1->mode_info.flags.VIDEO_OPTIMIZED_RATE ==
				mt_2->mode_info.flags.VIDEO_OPTIMIZED_RATE) {
				dal_dcs_mode_timing_list_append(
						output_list, mt_1);
				break;
			}
		}
	}
}

static void filter_default_modes_for_wireless(struct dcs *dcs,
		struct dcs_mode_timing_list *list)
{
	/* 128 is just a magic number. Should be updated */
	struct dcs_mode_timing_list *edid_mode_list =
			dal_dcs_mode_timing_list_create(128);
	struct dcs_mode_timing_list *receiver_mode_list =
			dal_dcs_mode_timing_list_create(128);
	bool is_limited_to_720p = false;
	bool include_unverified_timings = false;
	uint32_t i = 0;
	uint32_t list_count = 0;
	struct mode_timing new_timing;

	dal_memset(&new_timing, 0, sizeof(new_timing));

	ASSERT_CRITICAL(edid_mode_list != NULL);
	ASSERT_CRITICAL(receiver_mode_list != NULL);

	if (list == NULL || dcs->rdrm == NULL)
		dal_logger_write(dcs->dal->logger, LOG_MAJOR_DCS,
				LOG_MINOR_COMPONENT_DISPLAY_CAPABILITY_SERVICE,
				"%s: Invalid input or receiver modes");

	/* Get the feature values for wireless modes */
	dal_adapter_service_get_feature_value(
			FEATURE_WIRELESS_LIMIT_720P,
			&is_limited_to_720p,
			sizeof(is_limited_to_720p));

	dal_adapter_service_get_feature_value(
			FEATURE_WIRELESS_INCLUDE_UNVERIFIED_TIMINGS,
			&include_unverified_timings,
			sizeof(include_unverified_timings));

	/* Filter out unwanted timing. Order matters.
	 * - Modes greater than 1920*1080 or interlaced
	 * - For 720p limited, remove modes higher than 720 pixel in height
	 * - Add additional 30Hz/25Hz option
	 */
	list_count = dal_dcs_mode_timing_list_get_count(list);
	for (i = 0; i < list_count; ++i) {
		const struct mode_timing *timing =
				dal_dcs_mode_timing_list_at_index(list, i);

		if (timing->mode_info.pixel_width > 1920 ||
			timing->mode_info.pixel_height > 1080 ||
			timing->mode_info.flags.INTERLACE)
			continue;

		if (is_limited_to_720p && timing->mode_info.pixel_height > 720)
			continue;

		if (timing->mode_info.field_rate == 60 ||
				timing->mode_info.field_rate == 50) {

			/* Add 30/25 Hz */
			new_timing = *timing;
			new_timing.mode_info.field_rate /= 2;
			new_timing.crtc_timing.pix_clk_khz /= 2;
			new_timing.crtc_timing.vic = 0;
			new_timing.crtc_timing.hdmi_vic = 0;

			dal_dcs_mode_timing_list_append(
					edid_mode_list, &new_timing);

			/* Special case: Modes bigger than 720p at 60Hz is not
			 * added as they are unverified. ex. 1080p@60 */
			if (!include_unverified_timings &&
				(timing->mode_info.pixel_height > 720 ||
					timing->mode_info.pixel_width > 1280))
				continue;
		}

		/* Add the timing if not filtered out by above conditions */
		dal_dcs_mode_timing_list_append(edid_mode_list, timing);
	}

	/* Find intersection between EDID mode list and receiver mode list */
	dal_remote_display_receiver_get_supported_mode_timing(
			dcs->rdrm, receiver_mode_list);
	get_intersect_for_timing_lists(
			dcs, edid_mode_list, receiver_mode_list, list);

	/* If the result of intersect is empty, include 640x480 WFD mandatory */
	if (dal_dcs_mode_timing_list_get_count(list) == 0)
		dcs->dco_funcs.add_mode_timing(list, dcs->ts);

	if (edid_mode_list != NULL)
		dal_dcs_mode_timing_list_destroy(&edid_mode_list);

	if (receiver_mode_list != NULL)
		dal_dcs_mode_timing_list_destroy(&receiver_mode_list);
}

static void insert_edid_dco_mode_timing(
	struct dcs *dcs,
	struct dcs_mode_timing_list *mtl,
	struct mode_timing *mt,
	struct display_color_and_pixel_support *color_and_pixel_support)
{
	bool is_ce_mode =
		dal_timing_service_is_ce_timing_standard(
			mt->crtc_timing.timing_standard);
	enum color_depth_index color_depth;

	for (color_depth = COLOR_DEPTH_INDEX_666;
		color_depth < COLOR_DEPTH_INDEX_LAST;
		color_depth <<= 1) {
		enum pixel_encoding_mask pixel_encoding;

		if ((color_depth &
			color_and_pixel_support->color_depth_support.mask) == 0)
			continue;

		mt->crtc_timing.display_color_depth =
			dcs_color_depth_to_ts_color_depth(color_depth);

		/* Skip deep color on non-native timing (typically DVI only) */
		if (mt->mode_info.timing_source !=
			TIMING_SOURCE_EDID_DETAILED &&
			mt->crtc_timing.display_color_depth >
				DISPLAY_COLOR_DEPTH_888 &&
				color_and_pixel_support->color_depth_support.
				deep_color_native_res_only)
			continue;

		for (pixel_encoding = PIXEL_ENCODING_MASK_YCBCR444;
			pixel_encoding <= PIXEL_ENCODING_MASK_RGB;
			pixel_encoding <<= 1) {
			bool insert_mode = false;

			if (!(pixel_encoding &
				color_and_pixel_support->
				pixel_encoding_support.mask))
				continue;

			mt->crtc_timing.pixel_encoding =
				dcs_pixel_encoding_to_ts_pixel_encoding(
					pixel_encoding);

			switch (pixel_encoding) {
			case PIXEL_ENCODING_MASK_RGB:
				/* 1) for RGB, we allow all color depth. */
				insert_mode = true;
				break;
			case PIXEL_ENCODING_MASK_YCBCR422:
				/* 2) for CE timing, we allow 888 only for
				 * YCbCr422. All other depths can be supported
				 * by YCbCr444
				 */
				if (is_ce_mode &&
					(color_depth <=
						COLOR_DEPTH_INDEX_121212))
					insert_mode = true;
				break;
			case PIXEL_ENCODING_MASK_YCBCR444:
				/* 3) for CE timing, we allow all color depth
				 * for YCbCr444 if deep color for YCbCr
				 * supported. 888 color depths always supported
				 * 4) VCE/Wireless only supports YCbCr444 so
				 * always allow it
				 */
				if ((is_ce_mode ||
					(dcs->display_type ==
						INTERFACE_TYPE_WIRELESS)) &&
					(color_and_pixel_support->
					deep_color_y444_support ||
					(color_depth <=
						COLOR_DEPTH_INDEX_888)))
					insert_mode = true;
				/* Note: currently we don't have any feature to
				 * support DP Yonly, this is for HW testing only
				 * following logic should be replaced once
				 * display spec about YOnly available, at this
				 * implementation time, no Edid spec or DP spec
				 * report the display capability.
				 */
				if ((dcs->display_type == INTERFACE_TYPE_DP) &&
					dcs->dp_y_only)
					mt->crtc_timing.flags.YONLY = 1;
				break;
			default:
				break;
			}

			if (insert_mode)
				dal_dcs_mode_timing_list_append(mtl, mt);
		}
	}
}

/*
 * Search through the supported mode timing list and update the pixel encoding
 * and display color depth for all modes that are undefined. The value to set is
 * determined by the interface type.
 */
static void update_undefined_timing_parameters_with_defaults(
	struct dcs *dcs,
	struct dcs_mode_timing_list *mtl)
{
	/* determine the default PixelEncoding and DisplayColorDepth for
	 * undefined mode timings */
	struct mode_timing *mt;
	struct display_pixel_encoding_support default_encoding_support;
	struct display_color_depth_support default_color_depth_support;
	struct display_color_and_pixel_support color_and_pixel_support;
	struct cea_vendor_specific_data_block vendor_block = {0};
	bool insert_mode = false;
	uint32_t i;

	if (!mtl)
		return;

	get_default_color_depth(dcs, &default_color_depth_support);
	get_default_pixel_encoding(dcs, &default_encoding_support);

	dal_memset(
		&color_and_pixel_support,
		0,
		sizeof(color_and_pixel_support));

	if (dal_dcs_get_cea_vendor_specific_data_block(dcs, &vendor_block))
		color_and_pixel_support.deep_color_y444_support =
			vendor_block.byte6.DC_Y444;

	/* update the pixel encoding and display color depth for all undefined
	 * mode timings
	 */
	for (i = 0; i < dal_dcs_mode_timing_list_get_count(mtl);
		/* not always incremented */) {
		mt = dal_dcs_mode_timing_list_at_index(mtl, i);
		insert_mode = false;

		color_and_pixel_support.color_depth_support.mask =
			mt->crtc_timing.display_color_depth;
		color_and_pixel_support.pixel_encoding_support.mask =
			mt->crtc_timing.pixel_encoding;

		if (mt->crtc_timing.display_color_depth ==
			DISPLAY_COLOR_DEPTH_UNDEFINED) {
			color_and_pixel_support.color_depth_support =
				default_color_depth_support;
			insert_mode = true;
		}
		if (mt->crtc_timing.pixel_encoding ==
			PIXEL_ENCODING_UNDEFINED) {
			color_and_pixel_support.pixel_encoding_support =
				default_encoding_support;
			insert_mode = true;
		}

		if (insert_mode) {
			dal_dcs_mode_timing_list_remove_at_index(mtl, i);
			insert_edid_dco_mode_timing(
				dcs,
				mtl,
				mt,
				&color_and_pixel_support);
		} else
			i++;
	}
}

static bool build_mode_timing_list(
	struct dcs *dcs,
	struct dcs_mode_timing_list *list)
{
	bool preferred_found = false;
	uint32_t list_count;

	if (!list)
		return false;

	add_edid_modes(dcs, list, &preferred_found);

	add_overriden_modes(dcs, list, &preferred_found);

	if (dcs->vbios_dco)
		dal_vbios_dco_add_mode_timing(
			dcs->vbios_dco, list, &preferred_found);

	/*TODO: add other dco*/

	/* Add default modes if there is no mode coming from EDID, TV or BIOS.
	 * For non-wireless interface and wireless without valid timing.
	 */
	if (dcs->display_type != INTERFACE_TYPE_WIRELESS) {
		add_default_modes(dcs, list);
		/* TODO: Multiple Refresh Rate Timing for DRR */
	} else {
		filter_default_modes_for_wireless(dcs, list);
	}

	list_count = dal_dcs_mode_timing_list_get_count(list);
	/* If preferred mode yet not found,
	 * try to choose maximum progressive mode as preferred */
	if (!preferred_found) {
		int32_t i;

		for (i = list_count; i > 0; --i) {
			struct mode_timing *mode_timing =
				dal_dcs_mode_timing_list_at_index(list, i-1);
			if (!mode_timing->mode_info.flags.INTERLACE) {
				mode_timing->mode_info.flags.PREFERRED = 1;
				preferred_found = true;
				break;
			}
		}
	}

	/*If preferred mode yet not found,
	 * try to choose maximum mode as preferred*/
	if (!preferred_found && list_count) {
		struct mode_timing *mode_timing =
			dal_dcs_mode_timing_list_at_index(list, list_count-1);
		mode_timing->mode_info.flags.PREFERRED = 1;
		preferred_found = true;
	}

	update_undefined_timing_parameters_with_defaults(dcs, list);

	update_stereo_3d_features(dcs, list);

	return true;
}

/*updates the dcs_mode_timing_list of given path with
mode_timing reported by this DCS*/
void dal_dcs_update_ts_timing_list_on_display(
	struct dcs *dcs,
	uint32_t display_index)
{
	uint32_t i;
	uint32_t size;
	struct dcs_mode_timing_list *list =
		dal_dcs_mode_timing_list_create(256);

	if (!list)
		return;

	if (!build_mode_timing_list(dcs, list))
		goto update_list_exit;

	dal_timing_service_clear_mode_timing_list_for_path(
		dcs->ts, display_index);

	size = dal_dcs_mode_timing_list_get_count(list);

	for (i = 0; i < size; ++i) {

		struct mode_timing mode_timing_2d;

		struct mode_timing *mode_timing =
			dal_dcs_mode_timing_list_at_index(list, i);

		bool added = dal_timing_service_add_mode_timing_to_path(
			dcs->ts, display_index, mode_timing);

		if (!added && mode_timing->crtc_timing.timing_3d_format !=
			TIMING_3D_FORMAT_NONE) {
			mode_timing_2d = *mode_timing;
			mode_timing_2d.crtc_timing.timing_3d_format =
				TIMING_3D_FORMAT_NONE;
			mode_timing = &mode_timing_2d;
			added = dal_timing_service_add_mode_timing_to_path(
				dcs->ts, display_index, mode_timing);
		}

		if (added)
			update_edid_supported_max_bw(dcs, mode_timing);

	}
	/*TODO: add customized mode updates*/
update_list_exit:

	if (list)
		dal_dcs_mode_timing_list_destroy(&list);
}

bool dal_dcs_query_ddc_data(
	struct dcs *dcs,
	uint32_t address,
	uint8_t *write_buf,
	uint32_t write_buff_size,
	uint8_t *read_buff,
	uint32_t read_buff_size)
{
	if (!dcs->ddc_service)
		return false;

	return dal_ddc_service_query_ddc_data(
		dcs->ddc_service,
		address,
		write_buf, write_buff_size,
		read_buff, read_buff_size);
}

bool dal_dcs_get_vendor_product_id_info(
	struct dcs *dcs,
	struct vendor_product_id_info *info)
{
	struct edid_base *edid_base;

	if (!dcs->edid_mgr)
		return false;

	edid_base = dal_edid_mgr_get_edid(dcs->edid_mgr);

	if (!edid_base)
		return false;

	return dal_edid_get_vendor_product_id_info(edid_base, info);
}

bool dal_dcs_get_display_name(
	struct dcs *dcs,
	uint8_t *name,
	uint32_t size)
{
	struct edid_base *edid_base;

	if (!dcs->edid_mgr)
		return false;

	edid_base = dal_edid_mgr_get_edid(dcs->edid_mgr);

	if (!edid_base)
		return false;

	return dal_edid_get_display_name(edid_base, name, size);
}

bool dal_dcs_get_display_characteristics(
	struct dcs *dcs,
	struct display_characteristics *characteristics)
{
	struct edid_base *edid_base;

	if (!dcs->edid_mgr)
		return false;

	edid_base = dal_edid_mgr_get_edid(dcs->edid_mgr);

	if (!edid_base)
		return false;

	return dal_edid_get_display_characteristics(edid_base, characteristics);
}

bool dal_dcs_get_screen_info(
	struct dcs *dcs,
	struct edid_screen_info *info)
{
	struct edid_base *edid_base;

	if (!dcs->edid_mgr)
		return false;

	edid_base = dal_edid_mgr_get_edid(dcs->edid_mgr);

	if (!edid_base)
		return false;

	return dal_edid_get_screen_info(edid_base, info);
}

enum dcs_edid_connector_type dal_dcs_get_connector_type(struct dcs *dcs)
{
	struct edid_base *edid_base;

	if (!dcs->edid_mgr)
		return false;

	edid_base = dal_edid_mgr_get_edid(dcs->edid_mgr);

	if (!edid_base)
		return false;

	return dal_edid_get_connector_type(edid_base);
}

enum display_dongle_type dal_dcs_get_dongle_type(struct dcs *dcs)
{
	return dcs->sink_caps.dongle_type;
}

static void calculate_av_sync(
	struct display_sink_capability *sink_cap,
	struct av_sync_data *sync_data)
{
	uint32_t granularity_factor = 0; /* in microsecond (us) */
	/* Get Audio Decode Latency and Granularity Factor for DP 1.2 or greater
	 */
	if (sink_cap->dpcd_revision < DCS_DPCD_REV_12)
		return;

	{
		uint32_t a_decode_latency;
		uint32_t a_post_process_latency;
		uint32_t a_delay_insert;
		/* Convert DPCD registers to proper value av_granularity[3:0] */
		switch (sync_data->av_granularity & 0xF) {
		case 0:
			granularity_factor = 3000;
			break;
		case 1:
			granularity_factor = 2000;
			break;
		case 2:
			granularity_factor = 1000;
			break;
		case 3:
			granularity_factor = 500;
			break;
		case 4:
			granularity_factor = 200;
			break;
		case 5:
			granularity_factor = 100;
			break;
		case 6:
			granularity_factor = 10;
			break;
		case 7:
			granularity_factor = 1;
			break;
		default:
			granularity_factor = 2000;
			break;
		}

		a_decode_latency = sync_data->aud_dec_lat1 +
			(sync_data->aud_dec_lat2 << 8);
		a_decode_latency *= granularity_factor;

		a_post_process_latency = sync_data->aud_pp_lat1 +
			(sync_data->aud_pp_lat2 << 8);
		a_post_process_latency *= granularity_factor;

		a_delay_insert = sync_data->aud_del_ins1 +
			(sync_data->aud_del_ins2 << 8) +
			(sync_data->aud_del_ins3 << 16);
		a_delay_insert *= granularity_factor;

		sink_cap->audio_latency = a_decode_latency +
			a_post_process_latency;

		sink_cap->additional_audio_delay = (uint8_t)a_delay_insert;

	}

	{
		/* av_granularity[7:4] */
		switch ((sync_data->av_granularity >> 4) & 0x0F) {
		case 0:
			granularity_factor = 3000;
			break;
		case 1:
			granularity_factor = 2000;
			break;
		case 2:
			granularity_factor = 1000;
			break;
		case 3:
			granularity_factor = 500;
			break;
		case 4:
			granularity_factor = 200;
			break;
		case 5:
			granularity_factor = 100;
			break;
		default:
			granularity_factor = 2000;
			break;
		}

		sink_cap->video_latency_interlace = sync_data->vid_inter_lat *
			granularity_factor;
		sink_cap->video_latency_progressive = sync_data->vid_prog_lat *
			granularity_factor;
	}
}

static void setup_default_hdmi_sink_cap(
	struct dcs *dcs,
	struct display_sink_capability *sink_cap)
{
	if (!sink_cap)
		return;

	switch (dal_graphics_object_id_get_connector_id(
		dcs->graphics_object_id)) {
	case CONNECTOR_ID_HDMI_TYPE_A:
		sink_cap->max_hdmi_deep_color = HW_COLOR_DEPTH_121212;
		sink_cap->max_hdmi_pixel_clock =
			NATIVE_HDMI_MAX_PIXEL_CLOCK_IN_KHZ;
		break;

	default:
		sink_cap->max_hdmi_deep_color = HW_COLOR_DEPTH_888;
		sink_cap->max_hdmi_pixel_clock = TMDS_MAX_PIXEL_CLOCK_IN_KHZ;
		break;
	}
}

void dal_dcs_query_sink_capability(
	struct dcs *dcs,
	struct display_sink_capability *sink_cap,
	bool hpd_sense_bit)
{
	/* We allow passing NULL pointer */
	struct display_sink_capability dummy_sink_cap;
	enum connector_id connector =
		dal_graphics_object_id_get_connector_id(
			dcs->graphics_object_id);
	if (sink_cap == NULL || hpd_sense_bit == false) {
		dal_memset(&dummy_sink_cap, 0, sizeof(dummy_sink_cap));
		sink_cap = &dummy_sink_cap;
	} else
		dal_memset(sink_cap, 0, sizeof(struct display_sink_capability));

	/* reset the dp receiver id info */
	if (dcs->ddc_service)
		dal_ddc_service_reset_dp_receiver_id_info(dcs->ddc_service);

	switch (connector) {
	case CONNECTOR_ID_SINGLE_LINK_DVII:
	case CONNECTOR_ID_DUAL_LINK_DVII:
	case CONNECTOR_ID_SINGLE_LINK_DVID:
	case CONNECTOR_ID_DUAL_LINK_DVID:
	case CONNECTOR_ID_LVDS:
	case CONNECTOR_ID_HDMI_TYPE_A:
		sink_cap->ss_supported = true;
		break;
	case CONNECTOR_ID_DISPLAY_PORT:
		if (dcs->ddc_service)
			sink_cap->ss_supported =
				!dal_ddc_service_is_in_aux_transaction_mode(
					dcs->ddc_service);
		break;

	default:
		sink_cap->ss_supported = false;
		break;
	}

	setup_default_hdmi_sink_cap(dcs, sink_cap);

	if (!dcs->ddc_service) {
		/* save currently retrieved capabilities */
		dcs->sink_caps = *sink_cap;
		return;
	}

	switch (connector) {
	case CONNECTOR_ID_EDP:
		/* eDP's capability not change. */
		*sink_cap = dcs->sink_caps;
		if (dcs->sink_caps.is_edp_sink_cap_valid == false) {
			struct av_sync_data av_sync_data = {0};

			/* query some PSR etc info. */
			dal_ddc_service_aux_query_dp_sink_capability(
				dcs->ddc_service, sink_cap);
			dal_ddc_service_retrieve_dpcd_data(
				dcs->ddc_service, &av_sync_data);

			calculate_av_sync(sink_cap, &av_sync_data);
			/* query once. */
			sink_cap->is_edp_sink_cap_valid = true;
		}
		break;

	case CONNECTOR_ID_DISPLAY_PORT:
		if (dal_ddc_service_is_in_aux_transaction_mode(
			dcs->ddc_service)) {
			struct av_sync_data av_sync_data = {0};

			dal_ddc_service_aux_query_dp_sink_capability(
				dcs->ddc_service, sink_cap);
			dal_ddc_service_retrieve_dpcd_data(
				dcs->ddc_service, &av_sync_data);
			calculate_av_sync(sink_cap, &av_sync_data);
			sink_cap->is_edp_sink_cap_valid = true;
		} else {
			dal_ddc_service_i2c_query_dp_dual_mode_adaptor(
				dcs->ddc_service, sink_cap);

			if (sink_cap->dongle_type ==
				DISPLAY_DONGLE_DP_HDMI_DONGLE) {
				if (dcs->flags.DEEP_COLOR_OVER_DP_DONGLE ||
					sink_cap->max_hdmi_pixel_clock >
					TMDS_MAX_PIXEL_CLOCK_IN_KHZ)
					sink_cap->max_hdmi_deep_color =
						HW_COLOR_DEPTH_121212;

				if (dcs->flags.HIGH_PIXEL_CLK_OVER_DP_DONGLE)
					sink_cap->max_hdmi_pixel_clock =
					NATIVE_HDMI_MAX_PIXEL_CLOCK_IN_KHZ;
			}

		}

	/* Update dongle data even if dongle was removed */
	if (sink_cap->dongle_type != dcs->sink_caps.dongle_type) {
		/* for DP->VGA or DP->DualLink DVI dongles we want to
		 * use the HiRes set of default modes */
		if (sink_cap->dongle_type ==
			DISPLAY_DONGLE_DP_VGA_CONVERTER ||
			sink_cap->dongle_type ==
				DISPLAY_DONGLE_DP_DVI_CONVERTER)
			dcs->dco_funcs.add_mode_timing =
			dal_default_modes_dco_multi_sync_dco_add_mode_timing;
		else
			dcs->dco_funcs.add_mode_timing =
			dal_default_modes_dco_add_mode_timing;
	}
	break;

	case CONNECTOR_ID_VGA:
		/* DP2RGB translator */
		if (dal_ddc_service_is_in_aux_transaction_mode(
			dcs->ddc_service)) {
			/* For on-board DP translator, board connector is VGA or
			 * LCD. DAL treat this display as CRT or LCD different
			 * from DP dongle case which DAL treat it as DP based
			 * on sink_cap.dongle_type. */
			dal_ddc_service_aux_query_dp_sink_capability(
				dcs->ddc_service, sink_cap);
			sink_cap->dongle_type = DISPLAY_DONGLE_NONE;

		}
		break;

	case CONNECTOR_ID_LVDS:
		/* DP2LVDS translator */
		if (dal_ddc_service_is_in_aux_transaction_mode(
			dcs->ddc_service)) {
			/* For on-board DP translator, board connector is VGA or
			 * LCD.  DAL treat this display as CRT or LCD different
			 * from DP dongle case which DAL treat it as DP based on
			 * sink_cap.dongle_type. */
			dal_ddc_service_aux_query_dp_sink_capability(
				dcs->ddc_service, sink_cap);
			sink_cap->dongle_type = DISPLAY_DONGLE_NONE;
		}
		break;
	default:
		break;
	}

	/* save currently retrieved capabilities */
	dcs->sink_caps = *sink_cap;
}

void dal_dcs_reset_sink_capability(struct dcs *dcs)
{
	dal_memset(&dcs->sink_caps, 0, sizeof(dcs->sink_caps));
	setup_default_hdmi_sink_cap(dcs, &dcs->sink_caps);

	if (dcs->ddc_service)
		/* reset the dp receiver id info */
		dal_ddc_service_reset_dp_receiver_id_info(dcs->ddc_service);

	dcs->flags.CONTAINER_ID_INITIALIZED = false;
}

bool dal_dcs_get_sink_capability(
	struct dcs *dcs,
	struct display_sink_capability *sink_cap)
{
	if (!sink_cap)
		return false;

	*sink_cap = dcs->sink_caps;

	return true;
}

bool dal_dcs_emulate_sink_capability(
	struct dcs *dcs,
	struct display_sink_capability *sink_cap)
{
	if (!sink_cap)
		return false;

	dcs->sink_caps = *sink_cap;

	return true;
}

bool dal_dcs_get_display_color_depth(
	struct dcs *dcs,
	struct display_color_depth_support *color_depth)
{
	bool ret;
	struct edid_base *edid_base;

	if (!dcs->edid_mgr)
		return false;

	edid_base = dal_edid_mgr_get_edid(dcs->edid_mgr);

	if (!edid_base)
		return false;

	ret =  dal_edid_get_display_color_depth(edid_base, color_depth);

	if (!ret)
		ret = get_default_color_depth(dcs, color_depth);

	return ret;
}

bool dal_dcs_get_display_pixel_encoding(
	struct dcs *dcs,
	struct display_pixel_encoding_support *pixel_encoding)
{
	bool ret;
	struct edid_base *edid_base;

	if (!dcs->edid_mgr)
		return false;

	edid_base = dal_edid_mgr_get_edid(dcs->edid_mgr);

	if (!edid_base)
		return false;

	ret =  dal_edid_get_display_pixel_encoding(edid_base, pixel_encoding);

	if (!ret)
		ret = get_default_pixel_encoding(dcs, pixel_encoding);

	return ret;
}

bool dal_dcs_get_cea861_support(
	struct dcs *dcs,
	struct cea861_support *cea861_support)
{
	struct edid_base *edid_base;

	if (!dcs->edid_mgr)
		return false;

	edid_base = dal_edid_mgr_get_edid(dcs->edid_mgr);

	if (!edid_base)
		return false;

	return dal_edid_get_cea861_support(edid_base, cea861_support);
}

bool dal_dcs_get_cea_vendor_specific_data_block(
	struct dcs *dcs,
	struct cea_vendor_specific_data_block *vendor_block)
{
	struct edid_base *edid_base;

	if (!dcs->edid_mgr)
		return false;

	edid_base = dal_edid_mgr_get_edid(dcs->edid_mgr);

	if (!edid_base)
		return false;

	return dal_edid_get_cea_vendor_specific_data_block(
		edid_base, vendor_block);
}

bool dal_dcs_get_cea_speaker_allocation_data_block(
	struct dcs *dcs,
	enum signal_type signal,
	union cea_speaker_allocation_data_block *spkr_data)
{
	bool ret = false;
	struct edid_base *edid_base = NULL;

	if (dcs->edid_mgr)
		edid_base = dal_edid_mgr_get_edid(dcs->edid_mgr);

	ASSERT(spkr_data != NULL);


	if (edid_base)
		ret = dal_edid_get_cea_speaker_allocation_data_block(
			edid_base, spkr_data);

	switch (signal) {
	case SIGNAL_TYPE_EDP:
		ret = false;
		break;

	case SIGNAL_TYPE_DISPLAY_PORT:
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
		if (dcs->flags.DISABLE_DP_AUDIO) {
			ret = false;
			break;
		}

		if (edid_base) {
			/*TODO: ??????*/
			if (dal_edid_get_cea_audio_modes(edid_base, NULL)) {
				spkr_data->bits.FL_FR = 1;
				ret = true;
			}
		} else if (!ret && dcs->flags.DP_AUDIO_FORCED) {
			/* These are the speakers for default audio modes*/
			spkr_data->raw = 0;
			spkr_data->bits.FL_FR = 1;
			spkr_data->bits.FLC_FRC = 1;
			spkr_data->bits.RL_RR = 1;
			spkr_data->bits.RC = 1;
			spkr_data->bits.LFE = 1;
			ret = true;
		}
		break;

	case SIGNAL_TYPE_WIRELESS:
	case SIGNAL_TYPE_HDMI_TYPE_A:
		if (!ret) {
			/*HDMI should always have audio modes,
			since some panels do not support HDMI signal w/o audio
			these are the speakers for default audio modes*/
			spkr_data->raw = 0;
			spkr_data->bits.FL_FR = 1;
			ret = true;
		}
		break;

	default:
		break;
	}

	return ret;
}

bool dal_dcs_get_cea_colorimetry_data_block(
	struct dcs *dcs,
	struct cea_colorimetry_data_block *colorimetry_data_block)
{
	struct edid_base *edid_base;

	if (!dcs->edid_mgr)
		return false;

	edid_base = dal_edid_mgr_get_edid(dcs->edid_mgr);

	if (!edid_base)
		return false;

	return dal_edid_get_cea_colorimetry_data_block(
		edid_base, colorimetry_data_block);
}

bool dal_dcs_get_cea_video_capability_data_block(
	struct dcs *dcs,
	union cea_video_capability_data_block *video_capability_data_block)
{
	struct edid_base *edid_base;

	if (!dcs->edid_mgr)
		return false;

	edid_base = dal_edid_mgr_get_edid(dcs->edid_mgr);

	if (!edid_base)
		return false;

	return dal_edid_get_cea_video_capability_data_block(
		edid_base, video_capability_data_block);
}

uint32_t dal_dcs_get_extensions_num(struct dcs *dcs)
{
	struct edid_base *edid_base;

	if (!dcs->edid_mgr)
		return false;

	edid_base = dal_edid_mgr_get_edid(dcs->edid_mgr);

	if (!edid_base)
		return false;

	return dal_edid_get_num_of_extension(edid_base);
}

const struct dcs_cea_audio_mode_list *dal_dcs_get_cea_audio_modes(
	struct dcs *dcs,
	enum signal_type signal)
{
	if (!dcs->audio_modes ||
		dal_dcs_cea_audio_mode_list_get_count(dcs->audio_modes) == 0)
		return NULL;

	return dcs->audio_modes;
}

bool dal_dcs_is_audio_supported(struct dcs *dcs)
{
	if (!dcs->audio_modes ||
		dal_dcs_cea_audio_mode_list_get_count(dcs->audio_modes) == 0)
		return false;

	return true;
}

bool dal_dcs_validate_customized_mode(
	struct dcs *dcs,
	const struct dcs_customized_mode *customized_mode)
{
	/*TODO: add implementation */
	return false;
}

bool dal_dcs_add_customized_mode(
	struct dcs *dcs,
	struct dcs_customized_mode *customized_mode)
{
	/*TODO: add implementation */
	return false;
}

bool dal_dcs_delete_customized_mode(struct dcs *dcs, uint32_t index)
{
	/*TODO: add implementation*/
	return false;
}

const struct dcs_customized_mode_list *dal_dcs_get_customized_modes(
	struct dcs *dcs)
{
	/*TODO: add implementation*/
	return NULL;
}

bool dal_dcs_delete_mode_timing_override(
	struct dcs *dcs,
	struct dcs_override_mode_timing *dcs_mode_timing)
{
	/*TODO: add implementation*/
	return false;
}

bool dal_dcs_set_mode_timing_override(
	struct dcs *dcs,
	uint32_t display_index,
	struct dcs_override_mode_timing *dcs_mode_timing)
{
	/*TODO: add implementation*/
	return false;
}

bool dal_dcs_get_timing_override_for_mode(
	struct dcs *dcs,
	uint32_t display_index,
	struct mode_info *mode_info,
	struct dcs_override_mode_timing_list *dcs_mode_timing_list)
{
	/*TODO: add implementation*/
	return false;
}

uint32_t dal_dcs_get_num_mode_timing_overrides(struct dcs *dcs)
{
	/*TODO: add implementation*/
	return 0;
}

bool dal_dcs_get_timing_override_list(
	struct dcs *dcs,
	uint32_t display_index,
	struct dcs_override_mode_timing_list *dcs_mode_timing_list,
	uint32_t size)
{
	/*TODO: add implementation*/
	return false;
}

bool dal_dcs_get_supported_force_hdtv_mode(
	struct dcs *dcs,
	union hdtv_mode_support *hdtv_mode)
{
	/*TODO: add implementation*/
	return false;
}

bool dal_dcs_get_user_force_hdtv_mode(
	struct dcs *dcs,
	union hdtv_mode_support *hdtv_mode)
{
	/*TODO: add implementation*/
	return false;
}

bool dal_dcs_set_user_force_hdtv_mode(
	struct dcs *dcs,
	const union hdtv_mode_support *hdtv_mode)
{
	/*TODO: add implementation*/
	return false;
}

bool dal_dcs_get_fid9204_allow_ce_mode_only_option(
	struct dcs *dcs,
	bool is_hdmi,
	bool *enable)
{
	/*TODO: add implementation*/
	return false;
}

bool dal_dcs_set_fid9204_allow_ce_mode_only_option(
	struct dcs *dcs,
	bool is_hdmi,
	bool enable)
{
	/*TODO: add implementation*/
	return false;
}

bool dal_dcs_get_panel_misc_info(
	struct dcs *dcs,
	union panel_misc_info *panel_info)
{
	if (!dcs->vbios_dco)
		return false;

	return dal_vbios_dco_get_panel_misc_info(dcs->vbios_dco, panel_info);
}

enum ddc_result dal_dcs_dpcd_read(
	struct dcs *dcs,
	uint32_t address,
	uint8_t *buffer,
	uint32_t length)
{
	if (!dcs->ddc_service)
		return DDC_RESULT_UNKNOWN;

	return dal_ddc_service_read_dpcd_data(
		dcs->ddc_service, address, buffer, length);
}

enum ddc_result dal_dcs_dpcd_write(
	struct dcs *dcs,
	uint32_t address,
	const uint8_t *buffer,
	uint32_t length)
{
	if (!dcs->ddc_service)
		return DDC_RESULT_UNKNOWN;

	return dal_ddc_service_write_dpcd_data(
		dcs->ddc_service, address, buffer, length);
}

bool dal_dcs_get_range_limit(
	struct dcs *dcs,
	struct display_range_limits *limit)
{
	/*TODO: add implementation*/
	return false;
}

bool dal_dcs_set_range_limit_override(
	struct dcs *dcs,
	struct display_range_limits *limit)
{
	/*TODO: add implementation*/
	return false;
}

bool dal_dcs_get_user_select_limit(
	struct dcs *dcs,
	struct monitor_user_select_limits *limit)
{
	/*TODO: add implementation*/
	return false;
}

bool dal_dcs_set_user_select_limit(
	struct dcs *dcs,
	struct monitor_user_select_limits *limit)
{
	/*TODO: add implementation*/
	return false;
}

bool dal_dcs_get_dongle_mode_support(
	struct dcs *dcs,
	union hdtv_mode_support *hdtv_mode)
{
	/*TODO: add implementation*/
	return false;
}

bool dal_dcs_get_timing_limits(
	struct dcs *dcs,
	struct timing_limits *timing_limits)
{
	if (!timing_limits)
		return false;

	*timing_limits = dcs->timing_limits;
	return true;
}

bool dal_dcs_get_drr_config(
	struct dcs *dcs,
	struct drr_config *config)
{
	if (!config)
		return false;

	*config = dcs->drr_config;
	return true;
}

bool dal_dcs_force_dp_audio(struct dcs *dcs, bool force_audio_on)
{
	dcs->flags.DP_AUDIO_FORCED = force_audio_on;
	build_audio_modes(dcs);
	return true;
}

bool dal_dcs_is_dp_audio_forced(struct dcs *dcs)
{
	return dcs->flags.DP_AUDIO_FORCED;
}

const struct monitor_patch_info *dal_dcs_get_monitor_patch_info(
	struct dcs *dcs,
	enum monitor_patch_type patch_type)
{
	if (!dcs->edid_mgr)
		return NULL;

	return dal_edid_mgr_get_monitor_patch_info(dcs->edid_mgr, patch_type);
}

bool dal_dcs_set_monitor_patch_info(
	struct dcs *dcs,
	struct monitor_patch_info *patch_info)
{
	if (!dcs->edid_mgr)
		return false;

	return dal_edid_mgr_set_monitor_patch_info(dcs->edid_mgr, patch_info);
}

enum dcs_packed_pixel_format dal_dcs_get_enabled_packed_pixel_format(
	struct dcs *dcs)
{
	return dcs->packed_pixel_format;
}

enum dcs_packed_pixel_format dal_dcs_get_monitor_packed_pixel_format(
	struct dcs *dcs)
{
	/* Default to unpacked.*/
	enum dcs_packed_pixel_format format =
		DCS_PACKED_PIXEL_FORMAT_NOT_PACKED;
	const struct monitor_patch_info *patch_info;
	struct display_color_depth_support color_depth;
	struct edid_base *edid_base;

	if (!dcs->edid_mgr)
		return DCS_PACKED_PIXEL_FORMAT_NOT_PACKED;

	/* Obtain packed pixel info from relevant patch*/
	patch_info = dal_edid_mgr_get_monitor_patch_info(
		dcs->edid_mgr, MONITOR_PATCH_TYPE_PACKED_PIXEL_FORMAT);

	if (!patch_info)
		/* Try other patch instead */
		patch_info = dal_edid_mgr_get_monitor_patch_info(
			dcs->edid_mgr, MONITOR_PATCH_TYPE_MULTIPLE_PACKED_TYPE);

	/* Retrieve packed pixel format from patch*/
	if (patch_info)
		format = patch_info->param;

	/* Check for native 10-bit support */
	edid_base = dal_edid_mgr_get_edid(dcs->edid_mgr);

	if (!edid_base)
		return format;

	if (dal_edid_get_display_color_depth(edid_base, &color_depth)) {
		if (color_depth.mask & COLOR_DEPTH_INDEX_101010) {
			/* No current 10-bit display supports packed pixel.*/
			ASSERT(format == DCS_PACKED_PIXEL_FORMAT_NOT_PACKED);
			format = DCS_PACKED_PIXEL_FORMAT_NOT_PACKED;
		}
	}
	return format;
}

bool dal_dcs_report_single_selected_timing(struct dcs *dcs)
{
	return dcs->flags.REPORT_SINGLE_SELECTED_TIMING;
}

bool dal_dcs_can_tile_scale(struct dcs *dcs)
{
	return dcs->flags.TILED_DISPLAY_CAN_SCALE;
}

void dal_dcs_set_single_selected_timing_restriction(
	struct dcs *dcs,
	bool value)
{
	dcs->flags.REPORT_SINGLE_SELECTED_TIMING = value;
}

const struct dcs_edid_supported_max_bw *dal_dcs_get_edid_supported_max_bw(
	struct dcs *dcs)
{
	return &dcs->edid_max_bw;
}

bool dal_dcs_get_container_id(struct dcs *dcs,
	struct dcs_container_id *container_id)
{
	if (!container_id || !dcs->flags.CONTAINER_ID_INITIALIZED)
		return false;

	*container_id = dcs->container_id;
	return true;
}

bool dal_dcs_set_container_id(struct dcs *dcs,
	struct dcs_container_id *container_id)
{
	if (!container_id)
		return false;

	dcs->container_id = *container_id;
	dcs->flags.CONTAINER_ID_INITIALIZED = 1;
	return true;
}

bool dal_dcs_is_non_continous_frequency(struct dcs *dcs)
{
	struct edid_base *edid_base;

	if (!dcs->edid_mgr)
		return false;

	edid_base = dal_edid_mgr_get_edid(dcs->edid_mgr);

	if (!edid_base)
		return false;

	return dal_edid_is_non_continous_frequency(edid_base);
}

struct dcs_stereo_3d_features dal_dcs_get_stereo_3d_features(
	struct dcs *dcs,
	enum timing_3d_format format)
{
	if (format < TIMING_3D_FORMAT_MAX)
		return dcs->stereo_3d_features[format];

	return dcs->stereo_3d_features[TIMING_3D_FORMAT_NONE];
}

union stereo_3d_support dal_dcs_get_stereo_3d_support(struct dcs *dcs)
{
	return dcs->stereo_3d_support;
}

void dal_dcs_override_stereo_3d_support(
	struct dcs *dcs,
	union stereo_3d_support support)
{
	/*TODO: add implementation*/
}

void dal_dcs_set_remote_display_receiver_capabilities(
	struct dcs *dcs,
	const struct dal_remote_display_receiver_capability *cap)
{
	if (dcs->rdrm != NULL) {
		dal_remote_display_receiver_set_capabilities(dcs->rdrm, cap);
		build_audio_modes(dcs);
	}
}

void dal_dcs_clear_remote_display_receiver_capabilities(struct dcs *dcs)
{
	if (dcs->rdrm != NULL)
		dal_remote_display_receiver_clear_capabilities(dcs->rdrm);
}

static bool patch_tiled_display_info(
	struct dcs *dcs,
	struct dcs_display_tile *display_tile,
	struct vendor_product_id_info *tiled_vendor_info,
	bool first_display)
{
	const struct monitor_patch_info *patch_info;
	bool status = false;
	/* search for the first Tiled Display data block*/
	dal_memset(display_tile, 0, sizeof(struct dcs_display_tile));
	patch_info = dal_dcs_get_monitor_patch_info(
		dcs, MONITOR_PATCH_TYPE_TILED_DISPLAY);

	if (!patch_info)
		return false;

	if ((patch_info->param == EDID_TILED_DISPLAY_1) ||
		(patch_info->param == EDID_TILED_DISPLAY_2)) {

		display_tile->flags.CAN_SCALE = 0;
		display_tile->height = TILED_DISPLAY_VERTICAL;
		display_tile->width = TILED_DISPLAY_HORIZONTAL;
		display_tile->rows = 1;
		display_tile->cols = 2;
		display_tile->row = 0;
		display_tile->col = (first_display) ? 0 : 1;

		/* No bezel Info */
		/* No single Enclosure */

		display_tile->topology_id.manufacturer_id =
			tiled_vendor_info->manufacturer_id;
		display_tile->topology_id.product_id =
			tiled_vendor_info->product_id;
		display_tile->topology_id.serial_id =
			dcs->graphics_object_id.enum_id;
		status = true;
	}
	return status;
}

bool dal_dcs_get_display_tile_info(
	struct dcs *dcs,
	struct dcs_display_tile *display_tile,
	bool first_display)
{
	bool success;
	struct edid_base *edid_base;

	if (!dcs->edid_mgr)
		return false;

	edid_base = dal_edid_mgr_get_edid(dcs->edid_mgr);

	if (!edid_base)
		return false;

	success = dal_edid_get_display_tile_info(edid_base, display_tile);

	if (!success) {
		struct vendor_product_id_info tiled_vendor_info = { 0 };

		if (dal_edid_get_vendor_product_id_info(
			edid_base, &tiled_vendor_info))
				success = patch_tiled_display_info(
					dcs,
					display_tile,
					&tiled_vendor_info,
					first_display);
	}

	if (success) {
		/* use 2 bytes from manufacturerId*/
		/* use 2 bytes from productId*/
		/* use 4 bytes from serialId*/
		display_tile->id = display_tile->topology_id.manufacturer_id +
		(display_tile->topology_id.product_id << 16) +
		((uint64_t)(display_tile->topology_id.serial_id) << 32);

	}

	return success;
}

enum edid_retrieve_status dal_dcs_override_raw_edid(
	struct dcs *dcs,
	uint32_t len,
	uint8_t *data)
{
	enum edid_retrieve_status ret = EDID_RETRIEVE_FAIL;

	if (dcs->edid_mgr)
		ret = dal_edid_mgr_override_raw_data(dcs->edid_mgr, len, data);

	if (ret != EDID_RETRIEVE_SUCCESS)
		return ret;

	if (!dal_edid_mgr_get_edid(dcs->edid_mgr))
		return ret;

	update_cached_data(dcs);

	/*TODO: update range limits for VGA*/

	return ret;
}

