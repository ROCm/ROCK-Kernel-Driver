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

#ifndef __DISPLAY_PATH_INTERFACE_H__
#define __DISPLAY_PATH_INTERFACE_H__

#include "display_path_types.h"
#include "dcs_types.h"
#include "grph_object_ctrl_defs.h"
#include "signal_types.h"
#include "controller_types.h"

struct encoder;
struct controller;
struct connector;
struct audio;
struct clock_source;
struct link_service;
struct goc_link_service_data;
struct drr_config;

struct display_path;

struct display_path *dal_display_path_create(void);

struct display_path *dal_display_path_clone(
	const struct display_path *self,
	bool copy_active_state);

void dal_display_path_destroy(
	struct display_path **to_destroy);

bool dal_display_path_validate(
	struct display_path *path,
	enum signal_type sink_signal);

bool dal_display_path_add_link(
	struct display_path *path,
	struct encoder *encoder);

bool dal_display_path_add_connector(
	struct display_path *path,
	struct connector *connector);

struct connector *dal_display_path_get_connector(
	struct display_path *path);

int32_t dal_display_path_acquire(
	struct display_path *path);

bool dal_display_path_is_acquired(
	const struct display_path *path);

int32_t dal_display_path_get_ref_counter(
	const struct display_path *path);

int32_t dal_display_path_release(
	struct display_path *path);

void dal_display_path_release_resources(
	struct display_path *path);

void dal_display_path_acquire_links(
	struct display_path *path);

bool dal_display_path_is_source_blanked(
	const struct display_path *path);

bool dal_display_path_is_source_unblanked(
	const struct display_path *path);

void dal_display_path_set_source_blanked(
	struct display_path *path,
	enum display_tri_state state);

bool dal_display_path_is_target_blanked(
	const struct display_path *path);

bool dal_display_path_is_target_unblanked(
	const struct display_path *path);

void dal_display_path_set_target_blanked(
	struct display_path *path,
	enum display_tri_state state);

bool dal_display_path_is_target_powered_on(
	const struct display_path *path);

bool dal_display_path_is_target_powered_off(
	const struct display_path *path);

void dal_display_path_set_target_powered_on(
	struct display_path *path,
	enum display_tri_state state);

bool dal_display_path_is_target_connected(
	const struct display_path *path);

void dal_display_path_set_target_connected(
	struct display_path *path,
	bool c);

uint32_t dal_display_path_get_display_index(
	const struct display_path *path);

void dal_display_path_set_display_index(
	struct display_path *path,
	uint32_t index);

struct connector_device_tag_info *dal_display_path_get_device_tag(
	struct display_path *path);

void dal_display_path_set_device_tag(
	struct display_path *path,
	struct connector_device_tag_info tag);

enum clock_sharing_group dal_display_path_get_clock_sharing_group(
	const struct display_path *path);

void dal_display_path_set_clock_sharing_group(
	struct display_path *path,
	enum clock_sharing_group clock);

union display_path_properties dal_display_path_get_properties(
	const struct display_path *path);

void dal_display_path_set_properties(
	struct display_path *path,
	union display_path_properties p);

struct dcs *dal_display_path_get_dcs(
	const struct display_path *path);

void dal_display_path_set_dcs(
	struct display_path *path,
	struct dcs *dcs);

uint32_t dal_display_path_get_number_of_links(
	const struct display_path *path);

void dal_display_path_set_controller(
	struct display_path *path,
	struct controller *controller);

struct controller *dal_display_path_get_controller(
	const struct display_path *path);

void dal_display_path_set_clock_source(
	struct display_path *path,
	struct clock_source *clock);

struct clock_source *dal_display_path_get_clock_source(
	const struct display_path *path);

void dal_display_path_set_alt_clock_source(
	struct display_path *path,
	struct clock_source *clock);

struct clock_source *dal_display_path_get_alt_clock_source(
	const struct display_path *path);

void dal_display_path_set_fbc_info(
	struct display_path *path,
	struct fbc_info *clock);

struct fbc_info *dal_display_path_get_fbc_info(
	struct display_path *path);

void dal_display_path_set_drr_config(
	struct display_path *path,
	struct drr_config *clock);

void dal_display_path_get_drr_config(
	const struct display_path *path,
	struct drr_config *clock);

void dal_display_path_set_static_screen_triggers(
	struct display_path *path,
	const struct static_screen_events *events);

void dal_display_path_get_static_screen_triggers(
	const struct display_path *path,
	struct static_screen_events *events);

bool dal_display_path_is_psr_supported(
	const struct display_path *path);

bool dal_display_path_is_drr_supported(
	const struct display_path *path);

void dal_display_path_set_link_service_data(
	struct display_path *path,
	uint32_t idx,
	const struct goc_link_service_data *data);

bool dal_display_path_get_link_service_data(
	const struct display_path *path,
	uint32_t idx,
	struct goc_link_service_data *data);

struct link_service *dal_display_path_get_link_query_interface(
	const struct display_path *path,
	uint32_t idx);

void dal_display_path_set_link_query_interface(
	struct display_path *path,
	uint32_t idx,
	struct link_service *link);

struct link_service *dal_display_path_get_link_config_interface(
	const struct display_path *path,
	uint32_t idx);

struct link_service *dal_display_path_get_link_service_interface(
	const struct display_path *path,
	uint32_t idx);

struct encoder *dal_display_path_get_upstream_encoder(
	const struct display_path *path,
	uint32_t idx);

struct encoder *dal_display_path_get_upstream_object(
	const struct display_path *path,
	uint32_t idx);

struct encoder *dal_display_path_get_downstream_encoder(
	const struct display_path *path,
	uint32_t idx);

struct encoder *dal_display_path_get_downstream_object(
	const struct display_path *path,
	uint32_t idx);

struct audio *dal_display_path_get_audio(
	const struct display_path *path,
	uint32_t idx);

void dal_display_path_set_audio(
	struct display_path *path,
	uint32_t idx,
	struct audio *a);

struct audio *dal_display_path_get_audio_object(
	const struct display_path *path,
	uint32_t idx);

void dal_display_path_set_audio_active_state(
	struct display_path *path,
	uint32_t idx,
	bool state);

enum engine_id dal_display_path_get_stream_engine(
	const struct display_path *path,
	uint32_t idx);

void dal_display_path_set_stream_engine(
	struct display_path *path,
	uint32_t idx,
	enum engine_id id);

bool dal_display_path_is_link_active(
	const struct display_path *path,
	uint32_t idx);

void dal_display_path_set_link_active_state(
	struct display_path *path,
	uint32_t idx,
	bool state);

enum signal_type dal_display_path_get_config_signal(
	const struct display_path *path,
	uint32_t idx);

enum signal_type dal_display_path_get_query_signal(
	const struct display_path *path,
	uint32_t idx);

struct link_service *dal_display_path_get_mst_link_service(
	const struct display_path *path);

void dal_display_path_set_sync_output_object(
	struct display_path *path,
	enum sync_source o_source,
	struct encoder *o_object);

struct encoder *dal_display_path_get_sync_output_object(
	const struct display_path *path);

void dal_display_path_set_stereo_sync_object(
	struct display_path *path,
	struct encoder *stereo_sync);

struct encoder *dal_display_path_get_stereo_sync_object(
	const struct display_path *path);

void dal_display_path_set_sync_input_source(
	struct display_path *path,
	enum sync_source s);

enum sync_source dal_display_path_get_sync_input_source(
	const struct display_path *path);

void dal_display_path_set_sync_output_source(
	struct display_path *path,
	enum sync_source s);

enum sync_source dal_display_path_get_sync_output_source(
	const struct display_path *path);

bool dal_display_path_set_pixel_clock_safe_range(
	struct display_path *path,
	struct pixel_clock_safe_range *range);

bool dal_display_path_get_pixel_clock_safe_range(
	const struct display_path *path,
	struct pixel_clock_safe_range *range);

void dal_display_path_set_ddi_channel_mapping(
	struct display_path *path,
	union ddi_channel_mapping mapping);

bool dal_display_path_set_sink_signal(
	struct display_path *path,
	enum signal_type sink_signal);

enum signal_type dal_display_path_sink_signal_to_asic_signal(
	struct display_path *path,
	enum signal_type sink_signal);

enum signal_type dal_display_path_sink_signal_to_link_signal(
	struct display_path *path,
	enum signal_type sink_signal,
	uint32_t idx);

enum signal_type dal_display_path_downstream_to_upstream_signal(
	struct display_path *path,
	enum signal_type signal,
	uint32_t idx);

bool dal_display_path_is_audio_present(
	const struct display_path *path,
	uint32_t *audio_pin);

bool dal_display_path_is_dp_auth_supported(
	struct display_path *path);

bool dal_display_path_is_vce_supported(
	const struct display_path *path);

bool dal_display_path_is_sls_capable(
	const struct display_path *path);

bool dal_display_path_is_gen_lock_capable(
	const struct display_path *path);

struct transmitter_configuration dal_display_path_get_transmitter_configuration(
	const struct display_path *path,
	bool physical);

bool dal_display_path_is_ss_supported(
	const struct display_path *path);

bool dal_display_path_is_ss_configurable(
	const struct display_path *path);

void dal_display_path_set_ss_support(
	struct display_path *path,
	bool s);

enum signal_type dal_display_path_get_active_signal(
	struct display_path *path,
	uint32_t idx);

bool dal_display_path_contains_object(
	struct display_path *path,
	struct graphics_object_id id);

/* Multi-plane declarations.
 * This structure should also be used for Stereo. */
struct display_path_plane {
	struct controller *controller;
	/* During dal_tm_acquire_plane_resources() set blnd_mode, because
	 * "layer index" is known at that point, and we must decide how
	 * "controller" should do the blending */
	enum blender_mode blnd_mode;
	/* Some use-cases allow to power-gate FE.
	 * For example, with Full Screen Video on Underlay we can
	 * disable the 'root' plane.
	 * This flag indicates that FE should be power-gated */
	bool disabled;
};

bool dal_display_path_add_plane(
	struct display_path *path,
	struct display_path_plane *plane);

uint8_t dal_display_path_get_number_of_planes(
	const struct display_path *path);

struct display_path_plane *dal_display_path_get_plane_at_index(
	const struct display_path *path,
	uint8_t index);

struct controller *dal_display_path_get_controller_for_layer_index(
	const struct display_path *path,
	uint8_t layer_index);

void dal_display_path_release_planes(
	struct display_path *path);

void dal_display_path_release_non_root_planes(
	struct display_path *path);

#endif /* __DISPLAY_PATH_INTERFACE_H__ */
