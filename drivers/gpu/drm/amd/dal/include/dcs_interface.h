/* Copyright 2012-15 Advanced Micro Devices, Inc.
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
#ifndef __DAL_DCS_INTERFACE_H__
#define __DAL_DCS_INTERFACE_H__

#include "dcs_types.h"
#include "grph_object_id.h"

struct dal_context;
struct dcs;
struct ddc_service;
enum ddc_transaction_type;
enum ddc_result;
struct display_sink_capability;
enum dc_timing_3d_format;

struct dcs_cea_audio_mode_list;
struct dcs_customized_mode_list;

struct dcs_init_data {
	struct dal_context *dal;
	struct adapter_service *as;
	struct timing_service *ts;
	enum dcs_interface_type interface_type;
	struct graphics_object_id grph_obj_id;
};

struct dcs_cea_audio_mode_list *dal_dcs_cea_audio_mode_list_create(
	uint32_t list_size);

void dal_dcs_cea_audio_mode_list_destroy(
	struct dcs_cea_audio_mode_list **list);

bool dal_dcs_cea_audio_mode_list_append(
	struct dcs_cea_audio_mode_list *list,
	struct cea_audio_mode *cea_audio_mode);
uint32_t dal_dcs_cea_audio_mode_list_get_count(
	const struct dcs_cea_audio_mode_list *list);
void dal_dcs_cea_audio_mode_list_clear(
	struct dcs_cea_audio_mode_list *list);

struct cea_audio_mode *dal_dcs_cea_audio_mode_list_at_index(
	const struct dcs_cea_audio_mode_list *list,
	uint32_t index);

struct dcs *dal_dcs_create(const struct dcs_init_data *init_data);

void dal_dcs_destroy(struct dcs **dcs);

enum edid_retrieve_status dal_dcs_retrieve_raw_edid(struct dcs *dcs);

uint32_t dal_dcs_get_edid_raw_data_size(struct dcs *dcs);

enum edid_retrieve_status dal_dcs_override_raw_edid(
	struct dcs *dcs,
	uint32_t len,
	uint8_t *data);

const uint8_t *dal_dcs_get_edid_raw_data(
	struct dcs *dcs,
	uint32_t *buff_size);

enum edid_retrieve_status dal_dcs_update_edid_from_last_retrieved(
	struct dcs *dcs);

/*Update DDC Service.  returns the old DdcService being replaced*/
struct ddc_service *dal_dcs_update_ddc(
	struct dcs *dcs,
	struct ddc_service *ddc);

void dal_dcs_set_transaction_type(
	struct dcs *dcs,
	enum ddc_transaction_type type);

/*updates the ModeTimingList of given path with
ModeTiming reported by this DCS*/
void dal_dcs_update_ts_timing_list_on_display(
	struct dcs *dcs,
	uint32_t display_index);

/* DDC query on generic slave address*/
bool dal_dcs_query_ddc_data(
	struct dcs *dcs,
	uint32_t address,
	uint8_t *write_buf,
	uint32_t write_buff_size,
	uint8_t *read_buff,
	uint32_t read_buff_size);

bool dal_dcs_get_vendor_product_id_info(
	struct dcs *dcs,
	struct vendor_product_id_info *info);

bool dal_dcs_get_display_name(struct dcs *dcs, uint8_t *name, uint32_t size);

bool dal_dcs_get_display_characteristics(
	struct dcs *dcs,
	struct display_characteristics *characteristics);

bool dal_dcs_get_screen_info(
	struct dcs *dcs,
	struct edid_screen_info *info);

enum dcs_edid_connector_type dal_dcs_get_connector_type(struct dcs *dcs);

bool dal_dcs_get_display_pixel_encoding(
	struct dcs *dcs,
	struct display_pixel_encoding_support *pe);

enum display_dongle_type dal_dcs_get_dongle_type(struct dcs *dcs);

void dal_dcs_query_sink_capability(
	struct dcs *dcs,
	struct display_sink_capability *sink_cap,
	bool hpd_sense_bit);

void dal_dcs_reset_sink_capability(struct dcs *dcs);

bool dal_dcs_get_sink_capability(
	struct dcs *dcs,
	struct display_sink_capability *sink_cap);

bool dal_dcs_emulate_sink_capability(
	struct dcs *dcs,
	struct display_sink_capability *sink_cap);

bool dal_dcs_get_display_color_depth(
	struct dcs *dcs,
	struct display_color_depth_support *color_depth);

bool dal_dcs_get_display_pixel_encoding(
	struct dcs *dcs,
	struct display_pixel_encoding_support *pixel_encoding);

bool dal_dcs_get_cea861_support(
	struct dcs *dcs,
	struct cea861_support *cea861_support);

bool dal_dcs_get_cea_vendor_specific_data_block(
	struct dcs *dcs,
	struct cea_vendor_specific_data_block *vendor_block);

bool dal_dcs_get_cea_speaker_allocation_data_block(
	struct dcs *dcs,
	enum signal_type signal,
	union cea_speaker_allocation_data_block *spkr_data);

bool dal_dcs_get_cea_colorimetry_data_block(
	struct dcs *dcs,
	struct cea_colorimetry_data_block *colorimetry_data_block);

bool dal_dcs_get_cea_video_capability_data_block(
	struct dcs *dcs,
	union cea_video_capability_data_block *video_capability_data_block);

uint32_t dal_dcs_get_extensions_num(struct dcs *dcs);

const struct dcs_cea_audio_mode_list *dal_dcs_get_cea_audio_modes(
	struct dcs *dcs,
	enum signal_type signal);

bool dal_dcs_is_audio_supported(struct dcs *dcs);

bool dal_dcs_validate_customized_mode(
	struct dcs *dcs,
	const struct dcs_customized_mode *customized_mode);

bool dal_dcs_add_customized_mode(
	struct dcs *dcs,
	struct dcs_customized_mode *customized_mode);

bool dal_dcs_delete_customized_mode(struct dcs *dcs, uint32_t index);

const struct dcs_customized_mode_list *dal_dcs_get_customized_modes(
	struct dcs *dcs);

bool dal_dcs_delete_mode_timing_override(
	struct dcs *dcs,
	struct dcs_override_mode_timing *dcs_mode_timing);

bool dal_dcs_set_mode_timing_override(
	struct dcs *dcs,
	uint32_t display_index,
	struct dcs_override_mode_timing *dcs_mode_timing);

bool dal_dcs_get_timing_override_for_mode(
	struct dcs *dcs,
	uint32_t display_index,
	struct dc_mode_info *mode_info,
	struct dcs_override_mode_timing_list *dcs_mode_timing_list);

uint32_t dal_dcs_get_num_mode_timing_overrides(struct dcs *dcs);

bool dal_dcs_get_timing_override_list(
	struct dcs *dcs,
	uint32_t display_index,
	struct dcs_override_mode_timing_list *dcs_mode_timing_list,
	uint32_t size);

bool dal_dcs_get_supported_force_hdtv_mode(
	struct dcs *dcs,
	union hdtv_mode_support *hdtv_mode);

bool dal_dcs_get_user_force_hdtv_mode(
	struct dcs *dcs,
	union hdtv_mode_support *hdtv_mode);

bool dal_dcs_set_user_force_hdtv_mode(
	struct dcs *dcs,
	const union hdtv_mode_support *hdtv_mode);

bool dal_dcs_get_fid9204_allow_ce_mode_only_option(
	struct dcs *dcs,
	bool is_hdmi,
	bool *enable);

bool dal_dcs_set_fid9204_allow_ce_mode_only_option(
	struct dcs *dcs,
	bool is_hdmi,
	bool enable);

bool dal_dcs_get_panel_misc_info(
	struct dcs *dcs,
	union panel_misc_info *panel_info);

enum ddc_result dal_dcs_dpcd_read(
	struct dcs *dcs,
	uint32_t address,
	uint8_t *buffer,
	uint32_t length);

enum ddc_result dal_dcs_dpcd_write(
	struct dcs *dcs,
	uint32_t address,
	const uint8_t *buffer,
	uint32_t length);

bool dal_dcs_get_range_limit(
	struct dcs *dcs,
	struct display_range_limits *limit);

bool dal_dcs_set_range_limit_override(
	struct dcs *dcs,
	struct display_range_limits *limit);

bool dal_dcs_get_user_select_limit(
	struct dcs *dcs,
	struct monitor_user_select_limits *limit);

bool dal_dcs_set_user_select_limit(
	struct dcs *dcs,
	struct monitor_user_select_limits *limit);

bool dal_dcs_get_dongle_mode_support(
	struct dcs *dcs,
	union hdtv_mode_support *hdtv_mode);

bool dal_dcs_get_timing_limits(
	struct dcs *dcs,
	struct timing_limits *timing_limits);

bool dal_dcs_get_drr_config(
	struct dcs *dcs,
	struct drr_config *config);

bool dal_dcs_force_dp_audio(struct dcs *dcs, bool force_audio_on);

bool dal_dcs_is_dp_audio_forced(struct dcs *dcs);

const struct monitor_patch_info *dal_dcs_get_monitor_patch_info(
	struct dcs *dcs,
	enum monitor_patch_type patch_type);

bool dal_dcs_set_monitor_patch_info(
	struct dcs *dcs,
	struct monitor_patch_info *patch_info);

union dcs_monitor_patch_flags dal_dcs_get_monitor_patch_flags(struct dcs *dcs);

enum  dcs_packed_pixel_format dal_dcs_get_enabled_packed_pixel_format(
	struct dcs *dcs);

enum  dcs_packed_pixel_format dal_dcs_get_monitor_packed_pixel_format(
	struct dcs *dcs);

bool dal_dcs_report_single_selected_timing(struct dcs *dcs);

bool dal_dcs_can_tile_scale(struct dcs *dcs);

void dal_dcs_set_single_selected_timing_restriction(
	struct dcs *dcs,
	bool value);

const struct dcs_edid_supported_max_bw *dal_dcs_get_edid_supported_max_bw(
	struct dcs *dcs);

bool dal_dcs_is_non_continous_frequency(struct dcs *dcs);

struct dcs_stereo_3d_features dal_dcs_get_stereo_3d_features(
	struct dcs *dcs,
	enum dc_timing_3d_format format);

union stereo_3d_support dal_dcs_get_stereo_3d_support(struct dcs *dcs);

void dal_dcs_override_stereo_3d_support(
	struct dcs *dcs,
	union stereo_3d_support support);

void dal_dcs_set_remote_display_receiver_capabilities(
	struct dcs *dcs,
	const struct dal_remote_display_receiver_capability *cap);

void dal_dcs_clear_remote_display_receiver_capabilities(struct dcs *dcs);

bool dal_dcs_get_display_tile_info(
	struct dcs *dcs,
	struct dcs_display_tile *display_tile,
	bool first_display);

bool dal_dcs_get_container_id(struct dcs *dcs,
	struct dcs_container_id *container_id);

bool dal_dcs_set_container_id(struct dcs *dcs,
	struct dcs_container_id *container_id);

void dal_dcs_invalidate_container_id(struct dcs *dcs);

union dcs_monitor_patch_flags dal_dcs_get_monitor_patch_flags(struct dcs *dcs);

#endif /* __DAL_DCS_INTERFACE_H__ */
