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

struct connector *dal_display_path_get_connector(
	struct display_path *path);

struct connector_device_tag_info *dal_display_path_get_device_tag(
	struct display_path *path);

enum clock_sharing_group dal_display_path_get_clock_sharing_group(
	const struct display_path *path);

union display_path_properties dal_display_path_get_properties(
	const struct display_path *path);

struct dcs *dal_display_path_get_dcs(
	const struct display_path *path);

struct controller *dal_display_path_get_controller(
	const struct display_path *path);

struct clock_source *dal_display_path_get_clock_source(
	const struct display_path *path);

struct clock_source *dal_display_path_get_alt_clock_source(
	const struct display_path *path);

struct fbc_info *dal_display_path_get_fbc_info(
	struct display_path *path);

struct link_service *dal_display_path_get_link_query_interface(
	const struct display_path *path,
	uint32_t idx);

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

struct audio *dal_display_path_get_audio_object(
	const struct display_path *path,
	uint32_t idx);

enum engine_id dal_display_path_get_stream_engine(
	const struct display_path *path,
	uint32_t idx);

enum signal_type dal_display_path_get_config_signal(
	const struct display_path *path,
	uint32_t idx);

enum signal_type dal_display_path_get_query_signal(
	const struct display_path *path,
	uint32_t idx);

struct link_service *dal_display_path_get_mst_link_service(
	const struct display_path *path);

struct encoder *dal_display_path_get_sync_output_object(
	const struct display_path *path);

struct encoder *dal_display_path_get_stereo_sync_object(
	const struct display_path *path);

enum sync_source dal_display_path_get_sync_input_source(
	const struct display_path *path);

enum sync_source dal_display_path_get_sync_output_source(
	const struct display_path *path);


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

bool dal_display_path_is_ss_supported(
	const struct display_path *path);

enum signal_type dal_display_path_get_active_signal(
	struct display_path *path,
	uint32_t idx);

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

struct display_path_plane *dal_display_path_get_plane_at_index(
	const struct display_path *path,
	uint8_t index);

struct controller *dal_display_path_get_controller_for_layer_index(
	const struct display_path *path,
	uint8_t layer_index);

#endif /* __DISPLAY_PATH_INTERFACE_H__ */
