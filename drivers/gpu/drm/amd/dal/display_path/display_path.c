/*
 * Copyright 2012-14 Advanced Micro Devices, Inc.
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

#include "include/connector_interface.h"
#include "include/link_service_interface.h"
#include "include/audio_interface.h"
#include "include/controller_interface.h"
#include "include/dcs_interface.h"
#include "include/clock_source_interface.h"
#include "include/encoder_interface.h"
#include "include/display_path_interface.h"
#include "include/vector.h"

#include "display_path.h"

DAL_VECTOR_AT_INDEX(display_path_plane, struct display_path_plane *)
DAL_VECTOR_APPEND(display_path_plane, struct display_path_plane *)

static bool allocate_mpo_resources(struct display_path *path)
{
	/* CZ: 3 Primary and 1 Underlay = 4
	 * TODO: this value should be received from Asic Caps
	 * No hardcode should be anywhere
	 * Proposal here is to support arbitrary number of planes by display
	 * path */
	const uint32_t max_planes_num = 4;

	/* will be storing structures, not pointers */
	path->planes =
		dal_vector_create(
			max_planes_num,
			sizeof(struct display_path_plane));

	if (NULL == path->planes)
		return false;
	else
		return true;
}

static void free_mpo_resources(struct display_path *path)
{
	dal_vector_destroy(&path->planes);
}

static bool display_path_construct(struct display_path *path)
{
	uint32_t i;

	path->number_of_links = 0;
	dal_memset(&path->stream_contexts, 0, sizeof(path->stream_contexts));

	for (i = 0; i < MAX_NUM_OF_LINKS_PER_PATH; i++)
		path->stream_contexts[i].engine = ENGINE_ID_UNKNOWN;

	path->connector = NULL;
	path->clock_source = NULL;
	path->alt_clock_source = NULL;
	path->dcs = NULL;

	/* TODO: GL sync*/
	path->stereo_sync_object = NULL;
	path->sync_output_object = NULL;
	path->sync_output_source = SYNC_SOURCE_NONE;
	path->sync_input_source = SYNC_SOURCE_NONE;

	path->connected = false;
	path->src_tgt_state.powered_on = DISPLAY_TRI_STATE_UNKNOWN;
	path->src_tgt_state.src_blanked = DISPLAY_TRI_STATE_UNKNOWN;
	path->src_tgt_state.tgt_blanked = DISPLAY_TRI_STATE_UNKNOWN;
	path->properties.raw = 0;
	path->acquired_counter = 0;
	path->valid = false;
	path->display_index = INVALID_DISPLAY_INDEX;
	path->clock_sharing_group = CLOCK_SHARING_GROUP_EXCLUSIVE;
	path->ss_supported = false;
	path->pixel_clock_safe_range.min_frequency = 0;
	path->pixel_clock_safe_range.max_frequency = 0;
	path->ddi_channel_mapping.raw = INVALID_DDI_CHANNEL_MAPPING;
	path->device_tag.acpi_device = 0;
	path->device_tag.dev_id.enum_id = 0;
	path->device_tag.dev_id.device_type = DEVICE_TYPE_UNKNOWN;
	path->drr_cfg.force_lock_on_event = 0;
	path->drr_cfg.lock_to_master_vsync = 0;
	path->drr_cfg.min_fps_in_microhz = 0;
	path->static_screen_config.u_all = 0;

	if (false == allocate_mpo_resources(path))
		return false;
	else
		return true;
}

static void display_path_destruct(struct display_path *path)
{
	free_mpo_resources(path);
}

static enum signal_type calculate_upstream_signal(
	const struct display_path *path,
	enum signal_type downstream_signal,
	uint32_t link_idx)
{
	struct encoder *encoder;
	enum signal_type upstream_signal;
	uint32_t common_signals;
	struct encoder_feature_support features;

	if (link_idx >= path->number_of_links)
		return SIGNAL_TYPE_NONE;

	encoder = path->stream_contexts[link_idx].encoder;
	/* Make sure downstream object can support requested/downstream
	 * signal as an output signal.If cannot - we consider it as a failure.*/
	if ((dal_encoder_enumerate_output_signals(encoder) & downstream_signal)
		== 0)
		return SIGNAL_TYPE_NONE;

	/* Derive common signal for this link */
	common_signals = dal_encoder_enumerate_input_signals(encoder);
	if (link_idx > 0) {
		encoder = path->stream_contexts[link_idx - 1].encoder;
		common_signals &= dal_encoder_enumerate_output_signals(encoder);
		encoder = path->stream_contexts[link_idx].encoder;
	}

	/* Make sure upstream link can support current signal.
	 * If not, but downstream encoder can convert signals,
	 *  we choose first supported signal common for upstream link*/
	upstream_signal = downstream_signal;
	features = dal_encoder_get_supported_features(encoder);
	if ((common_signals & upstream_signal) == 0) {
		if (features.flags.bits.IS_CONVERTER)
			/* common signals is a bitmask, we need to get least
			 * significant 1 bit to convert bitmask to signal_type
			 * enum. (x & -x) or (x & ((~x)+1)) */
			upstream_signal = common_signals & (-common_signals);
		else
			upstream_signal = SIGNAL_TYPE_NONE;
	}

	return upstream_signal;
}

static enum signal_type calculate_asic_signal(
	const struct display_path *path,
	enum signal_type sink_signal)
{
	uint32_t i;
	uint32_t current_signal;
	if (path->connector == NULL)
		return SIGNAL_TYPE_NONE;

	if ((dal_connector_enumerate_output_signals(path->connector)
		& sink_signal) == 0)
		return SIGNAL_TYPE_NONE;

	current_signal = sink_signal;

	for (i = path->number_of_links; i > 0; i--) {
		current_signal = calculate_upstream_signal(
			path, current_signal, i - 1);
	}

	return current_signal;
}
static bool is_ss_forced_enable(const struct display_path *path)
{
	if (path->clock_source == NULL)
		return false;
	return (dal_clock_source_get_id(path->clock_source)
		== CLOCK_SOURCE_ID_EXTERNAL);
}

struct display_path *dal_display_path_create(void)
{
	struct display_path *display_path = NULL;

	display_path = dal_alloc(sizeof(struct display_path));
	if (NULL == display_path)
		return NULL;

	if (false == display_path_construct(display_path)) {
		dal_free(display_path);
		return NULL;
	}

	return display_path;
}

struct display_path *dal_display_path_clone(
	const struct display_path *origin,
	bool copy_active_state)
{
	struct display_path *path = dal_display_path_create();

	if (path == NULL)
		return NULL;

	/* Clean-up non-sharable pointers in new display path. */
	dal_vector_destroy(&path->planes);

	/* copy everything from origin */
	dal_memmove(path, origin, sizeof(struct display_path));

	/* All optional objects are not copied */
	path->stereo_sync_object = NULL;
	path->sync_output_object = NULL;
	path->sync_output_source = SYNC_SOURCE_NONE;
	path->sync_input_source = SYNC_SOURCE_NONE;

	/* Properly clone all non-sharable pointers from "this" */
	path->planes = dal_vector_clone(origin->planes);

	ASSERT_CRITICAL(path->planes);

	if (!path->planes)
		return NULL;

	path->acquired_counter = 0;

	if (copy_active_state && dal_display_path_is_acquired(origin))
		path->acquired_counter = origin->acquired_counter;
	else
		dal_display_path_release_resources(path);

	return path;
}

void dal_display_path_destroy(struct display_path **path)
{
	if (!path  || !*path) {
		BREAK_TO_DEBUGGER();
		return;
	}

	display_path_destruct(*path);

	dal_free(*path);

	*path = NULL;

}

bool dal_display_path_validate(
	struct display_path *path,
	enum signal_type sink_signal)
{
	uint32_t i;
	path->valid = false;
	/* verify connector and at least one link present*/
	if (path->number_of_links < 1 || path->connector == NULL) {
		ASSERT(false);/* display path missing core components*/
		return false;
	}
	/* verify encoder at each link*/
	for (i = 0; i < path->number_of_links; i++) {
		if (path->stream_contexts[i].encoder == NULL) {
			ASSERT(false);/*display path missing core components*/
			return false;
		}
	}
	/* verify signal support accross whole chain (however it is not a bug
	 * if not supported, just we shoudl skip this display path)*/
	if (sink_signal == SIGNAL_TYPE_NONE)
		return false;

	if (dal_display_path_set_sink_signal(path, sink_signal))
		path->valid = true;

	return path->valid;
}

bool dal_display_path_add_link(
	struct display_path *path,
	struct encoder *encoder)
{
	if (path->number_of_links >= MAX_NUM_OF_LINKS_PER_PATH)
		return false;

	path->stream_contexts[path->number_of_links++].encoder = encoder;
	return true;
}

bool dal_display_path_add_connector(
	struct display_path *path,
	struct connector *connector)
{
	ASSERT(path != NULL && connector != NULL);
	path->connector = connector;
	return true;
}
struct connector *dal_display_path_get_connector(struct display_path *path)
{
	return path->connector;
}

int32_t dal_display_path_acquire(struct display_path *path)
{
	++path->acquired_counter;
	return path->acquired_counter;
}

int32_t dal_display_path_get_ref_counter(const struct display_path *path)
{
	return path->acquired_counter;
}

bool dal_display_path_is_acquired(const struct display_path *path)
{
	/* if counter is greater than zero than path is acquired */
	return (dal_display_path_get_ref_counter(path) > 0);
}

int32_t dal_display_path_release(struct display_path *path)
{
	--path->acquired_counter;
	return path->acquired_counter;
}

void dal_display_path_release_resources(struct display_path *path)
{
	uint32_t i;
	for (i = 0; i < path->number_of_links; i++) {
		struct stream_context *cntx = &path->stream_contexts[i];
		cntx->state.LINK = false;
		cntx->state.AUDIO = false;
		cntx->engine = ENGINE_ID_UNKNOWN;
		cntx->input_config_signal = SIGNAL_TYPE_NONE;
		cntx->output_config_signal = SIGNAL_TYPE_NONE;
		cntx->link_config_interface = NULL;
	}

	path->clock_source = NULL;
	path->alt_clock_source = NULL;
	/* TODO: GL sync*/
	path->stereo_sync_object = NULL;
	path->sync_output_object = NULL;
	path->sync_output_source = SYNC_SOURCE_NONE;
	path->sync_input_source = SYNC_SOURCE_NONE;

	dal_display_path_release_planes(path);
}

bool dal_display_path_is_source_blanked(const struct display_path *path)
{
	return path->src_tgt_state.src_blanked == DISPLAY_TRI_STATE_TRUE;
}
bool dal_display_path_is_source_unblanked(const struct display_path *path)
{
	return path->src_tgt_state.src_blanked == DISPLAY_TRI_STATE_FALSE;
}
void dal_display_path_set_source_blanked(
	struct display_path *path,
	enum display_tri_state state)
{
	path->src_tgt_state.src_blanked = state;
}
bool dal_display_path_is_target_blanked(const struct display_path *path)
{
	return path->src_tgt_state.tgt_blanked == DISPLAY_TRI_STATE_TRUE;
}

bool dal_display_path_is_target_unblanked(const struct display_path *path)
{
	return path->src_tgt_state.tgt_blanked == DISPLAY_TRI_STATE_FALSE;
}
void dal_display_path_set_target_blanked(
	struct display_path *path,
	enum display_tri_state state)
{
	path->src_tgt_state.tgt_blanked = state;
}

bool dal_display_path_is_target_powered_on(const struct display_path *path)
{
	return path->src_tgt_state.powered_on == DISPLAY_TRI_STATE_TRUE;
}
bool dal_display_path_is_target_powered_off(const struct display_path *path)
{
	return path->src_tgt_state.powered_on == DISPLAY_TRI_STATE_FALSE;
}
void dal_display_path_set_target_powered_on(
	struct display_path *path,
	enum display_tri_state state)
{
	path->src_tgt_state.powered_on = state;
}

bool dal_display_path_is_target_connected(const struct display_path *path)
{
	return path->connected;
}

void dal_display_path_set_target_connected(struct display_path *path, bool c)
{
	path->connected = c;
}

uint32_t dal_display_path_get_display_index(const struct display_path *path)
{
	return path->display_index;
}
void dal_display_path_set_display_index(
	struct display_path *path,
	uint32_t index)
{
	path->display_index = index;
}

struct connector_device_tag_info *dal_display_path_get_device_tag(
	struct display_path *path)
{
	return &path->device_tag;
}
void dal_display_path_set_device_tag(
	struct display_path *path,
	struct connector_device_tag_info tag)
{
	ASSERT(tag.dev_id.device_type != DEVICE_TYPE_UNKNOWN);
	path->device_tag = tag;
}
enum clock_sharing_group dal_display_path_get_clock_sharing_group(
	const struct display_path *path)
{
	return path->clock_sharing_group;
}
void dal_display_path_set_clock_sharing_group(
	struct display_path *path,
	enum clock_sharing_group clock)
{
	path->clock_sharing_group = clock;
}
union display_path_properties dal_display_path_get_properties(
	const struct display_path *path)
{
	return path->properties;
}
void dal_display_path_set_properties(
	struct display_path *path,
	union display_path_properties p)
{
	path->properties = p;
}

struct dcs *dal_display_path_get_dcs(const struct display_path *path)
{
	return path->dcs;
}
void dal_display_path_set_dcs(struct display_path *path, struct dcs *dcs)
{
	path->dcs = dcs;
}

uint32_t dal_display_path_get_number_of_links(const struct display_path *path)
{
	return path->number_of_links;
}

void dal_display_path_set_clock_source(
	struct display_path *path,
	struct clock_source *clock)
{
	path->clock_source = clock;
}

struct clock_source *dal_display_path_get_clock_source(
	const struct display_path *path)
{
	return path->clock_source;
}

void dal_display_path_set_alt_clock_source(
	struct display_path *path,
	struct clock_source *clock)
{
	path->alt_clock_source = clock;
}

struct clock_source *dal_display_path_get_alt_clock_source(
	const struct display_path *path)
{
	return path->alt_clock_source;
}

void dal_display_path_set_fbc_info(
	struct display_path *path,
	struct fbc_info *fbc_info)
{
	path->fbc_info = *fbc_info;
}

struct fbc_info *dal_display_path_get_fbc_info(struct display_path *path)
{
	return &path->fbc_info;
}

void dal_display_path_acquire_links(struct display_path *path)
{
	uint32_t i;
	for (i = 0; i < path->number_of_links; i++) {
		path->stream_contexts[i].input_config_signal =
			path->stream_contexts[i].input_query_signal;
		path->stream_contexts[i].output_config_signal =
			path->stream_contexts[i].output_query_signal;
		path->stream_contexts[i].link_config_interface =
			path->stream_contexts[i].link_query_interface;
	}
}

struct link_service *dal_display_path_get_mst_link_service(
	const struct display_path *path)
{
	uint32_t i;
	struct link_service *link_service = NULL;
	for (i = 0; i < path->number_of_links; i++) {
		link_service = path->stream_contexts[i].link_query_interface;
		if (link_service != NULL
			&& dal_ls_get_link_service_type(link_service)
				== LINK_SERVICE_TYPE_DP_MST) {
			return link_service;
		}
	}

	return NULL;
}

/* This function is for backward compatibility only (called from many places).
 * (All callers are interested in the "root" controller)
 * TODO: remove it when Planes code is finalised. */
struct controller *dal_display_path_get_controller(struct display_path *path)
{
	struct display_path_plane *plane_zero =
			dal_display_path_get_plane_at_index(path, 0);

	/* In many places code expects/checks for NULL and acts accordingly.
	 * That means we should NOT use CRITICAL_ASSERT() here. */
	if (NULL != plane_zero)
		return plane_zero->controller;
	else
		return NULL;
}

void dal_display_path_set_sync_output_object(
	struct display_path *path,
	enum sync_source o_source,
	struct encoder *o_object)
{
	/*Overwriting sync-output object*/
	ASSERT(path->sync_output_object == NULL || o_object == NULL);
	/*Overwriting sync-output source*/
	ASSERT(path->sync_input_source == SYNC_SOURCE_NONE
		|| o_source == SYNC_SOURCE_NONE);

	path->sync_output_source = o_source;
	path->sync_output_object = o_object;
}

struct encoder *dal_display_path_get_sync_output_object(
	const struct display_path *path)
{
	return path->sync_output_object;
}

void dal_display_path_set_stereo_sync_object(
	struct display_path *path,
	struct encoder *stereo_sync)
{
	path->stereo_sync_object = stereo_sync;
}

struct encoder *dal_display_path_get_stereo_sync_object(
	const struct display_path *path)
{
	return path->stereo_sync_object;
}

void dal_display_path_set_sync_input_source(
	struct display_path *path,
	enum sync_source s)
{
	path->sync_input_source = s;
}

enum sync_source dal_display_path_get_sync_input_source(
	const struct display_path *path)
{
	return path->sync_input_source;
}

void dal_display_path_set_sync_output_source(
	struct display_path *path,
	enum sync_source s)
{
	path->sync_output_source = s;
}

enum sync_source dal_display_path_get_sync_output_source(
	const struct display_path *path)
{
	return path->sync_output_source;
}

bool dal_display_path_set_pixel_clock_safe_range(
	struct display_path *path,
	struct pixel_clock_safe_range *range)
{
	struct timing_limits timing_limits = { 0 };
	if (range == NULL)
		return false;

	dal_dcs_get_timing_limits(path->dcs, &timing_limits);

	if (range->min_frequency > range->max_frequency) {
		/* minimum grater than maximum
		 * we can actually swap them and continue
		 * , but the caller should not rely on that*/
		return false;
	}

	if (range->min_frequency < timing_limits.min_pixel_clock_in_khz
		|| range->max_frequency
			> timing_limits.max_pixel_clock_in_khz) {
		/* provided range is out of timing limits*/
		return false;
	}

	path->pixel_clock_safe_range = *range;
	return true;
}
bool dal_display_path_get_pixel_clock_safe_range(
	const struct display_path *path,
	struct pixel_clock_safe_range *range)
{
	if (range != NULL && path->pixel_clock_safe_range.max_frequency) {
		*range = path->pixel_clock_safe_range;
		return true;
	}

	return false;
}

void dal_display_path_set_drr_config(
	struct display_path *path,
	struct drr_config *drr_cfg)
{
	if (drr_cfg != NULL)
		path->drr_cfg = *drr_cfg;
	else
		dal_memset(&path->drr_cfg, 0, sizeof(path->drr_cfg));
}
void dal_display_path_get_drr_config(
	const struct display_path *path,
	struct drr_config *drr_cfg)
{
	if (drr_cfg != NULL)
		*drr_cfg = path->drr_cfg;
}

bool dal_display_path_set_sink_signal(
	struct display_path *path,
	enum signal_type sink_signal)
{
	int32_t i;
	enum signal_type current_signal;

	/* for hdmi connector, we want to ensure that
	 * the display path supports hdmi*/
	if ((sink_signal == SIGNAL_TYPE_HDMI_TYPE_A)
		&& !path->properties.bits.IS_HDMI_AUDIO_SUPPORTED) {
		ASSERT(false);/*HDMI audio not supported on this display path*/
		return false;
	}

	if (calculate_asic_signal(path, sink_signal) == SIGNAL_TYPE_NONE) {
		ASSERT(false);/* Failed to set requested signal */
		return false;
	}

	current_signal = sink_signal;
	for (i = path->number_of_links - 1; i >= 0; --i) {
		path->stream_contexts[i].output_query_signal =
			current_signal;
		current_signal = calculate_upstream_signal(path, current_signal,
			i);
		path->stream_contexts[i].input_query_signal = current_signal;
	}
	return true;

}

enum signal_type dal_display_path_sink_signal_to_asic_signal(
	struct display_path *path,
	enum signal_type sink_signal)
{
	return calculate_asic_signal(path, sink_signal);
}
enum signal_type dal_display_path_sink_signal_to_link_signal(
	struct display_path *path,
	enum signal_type sink_signal,
	uint32_t idx)
{
	enum signal_type current_signal = sink_signal;
	int32_t i;

	if (path->connector == NULL)
		return SIGNAL_TYPE_NONE;
	if ((dal_connector_enumerate_output_signals(path->connector)
		& sink_signal) == 0) {
		return SIGNAL_TYPE_NONE;
	}

	for (i = path->number_of_links - 1; i >= 0; --i) {
		if (idx == i)
			return current_signal;

		current_signal = calculate_upstream_signal(path, current_signal,
			i);
	}

	return current_signal;
}

enum signal_type dal_display_path_downstream_to_upstream_signal(
	struct display_path *path,
	enum signal_type signal,
	uint32_t idx)
{
	return calculate_upstream_signal(path, signal, idx);
}

bool dal_display_path_is_ss_supported(const struct display_path *path)
{
	return is_ss_forced_enable(path) ? true : path->ss_supported;
}
bool dal_display_path_is_ss_configurable(const struct display_path *path)
{
	return !is_ss_forced_enable(path);
}
void dal_display_path_set_ss_support(struct display_path *path, bool s)
{
	path->ss_supported = s;
}

void dal_display_path_set_static_screen_triggers(
	struct display_path *path,
	const struct static_screen_events *events)
{
	if (events != NULL)
		path->static_screen_config.u_all = events->u_all;
	else
		path->static_screen_config.u_all = 0;
}
void dal_display_path_get_static_screen_triggers(
	const struct display_path *path,
	struct static_screen_events *events)
{
	if (events != NULL)
		events->u_all = path->static_screen_config.u_all;
}

bool dal_display_path_is_psr_supported(const struct display_path *path)
{
	return false;
}

bool dal_display_path_is_drr_supported(const struct display_path *path)
{
	struct link_service *ls =
		dal_display_path_get_link_query_interface(
				path, SINK_LINK_INDEX);
	struct drr_config drr_config;
	dal_display_path_get_drr_config(path, &drr_config);

	/* The check for DRR supported returns true means it satisfied:
	 * 1. EDID reported DRR capability and Stream supports DRR or
	 * 2. Forced capability through runtime parameter or
	 * 3. Forced capability through VBIOS */
	if (((drr_config.support_method.SUPPORTED_BY_EDID == 1) &&
		dal_ls_is_stream_drr_supported(ls)) ||
		(drr_config.support_method.FORCED_BY_REGKEY_OR_ESCAPE == 1) ||
		(drr_config.support_method.FORCED_BY_VBIOS == 1))
		return true;
	return false;
}

void dal_display_path_set_link_service_data(
	struct display_path *path,
	uint32_t idx,
	const struct goc_link_service_data *data)
{

	if (idx < path->number_of_links && data != NULL)
		path->stream_contexts[idx].link_service_data = *data;
}
bool dal_display_path_get_link_service_data(
	const struct display_path *path,
	uint32_t idx,
	struct goc_link_service_data *data)
{
	if (idx < path->number_of_links && data != NULL) {
		*data = path->stream_contexts[idx].link_service_data;
		return true;
	}
	return false;
}

bool dal_display_path_is_audio_present(
	const struct display_path *path,
	uint32_t *audio_pin)
{
	uint32_t i;
	const struct stream_context *link;
	struct audio_feature_support features;
	for (i = 0; i < path->number_of_links; i++) {
		link = &path->stream_contexts[i];

		if (!link->state.AUDIO)
			continue;
		if (link->audio == NULL)
			continue;
		features = dal_audio_get_supported_features(link->audio);
		/*For DCE6.x and higher*/
		if (features.MULTISTREAM_AUDIO && audio_pin != NULL)
			*audio_pin = dal_audio_get_graphics_object_id(
				link->audio).enum_id - 1;
		return true;
	}
	return false;
}

bool dal_display_path_is_dp_auth_supported(struct display_path *path)
{
	uint32_t i;
	struct encoder_feature_support features;
	struct stream_context *link;
	for (i = 0; i < path->number_of_links; i++) {
		link = &path->stream_contexts[i];
		features = dal_encoder_get_supported_features(link->encoder);

		if (link->state.LINK
			&& features.flags.bits.CPLIB_DP_AUTHENTICATION)
			return true;
	}
	return false;

}
bool dal_display_path_is_vce_supported(const struct display_path *path)
{
	uint32_t i;
	const struct stream_context *link;
	struct encoder_feature_support features;
	for (i = 0; i < path->number_of_links; i++) {
		link = &path->stream_contexts[i];
		features = dal_encoder_get_supported_features(link->encoder);
		if (features.flags.bits.IS_VCE_SUPPORTED)
			return true;
	}
	return false;
}
bool dal_display_path_is_sls_capable(const struct display_path *path)
{
	switch (dal_display_path_get_query_signal(path, SINK_LINK_INDEX)) {
	case SIGNAL_TYPE_DVI_SINGLE_LINK:
	case SIGNAL_TYPE_DVI_SINGLE_LINK1:
	case SIGNAL_TYPE_DVI_DUAL_LINK:
	case SIGNAL_TYPE_HDMI_TYPE_A:
	case SIGNAL_TYPE_LVDS:
	case SIGNAL_TYPE_RGB:
	case SIGNAL_TYPE_DISPLAY_PORT:
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
	case SIGNAL_TYPE_EDP:
		return true;
		break;
	default:
		break;
	}

	return false;
}
bool dal_display_path_is_gen_lock_capable(const struct display_path *path)
{
	switch (dal_display_path_get_query_signal(path, SINK_LINK_INDEX)) {
	case SIGNAL_TYPE_DVI_SINGLE_LINK:
	case SIGNAL_TYPE_DVI_SINGLE_LINK1:
	case SIGNAL_TYPE_DVI_DUAL_LINK:
	case SIGNAL_TYPE_HDMI_TYPE_A:
	case SIGNAL_TYPE_DISPLAY_PORT:
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
	case SIGNAL_TYPE_EDP:
		return true;
		break;
	default:
		break;
	}

	return false;
}
struct transmitter_configuration dal_display_path_get_transmitter_configuration(
	const struct display_path *path,
	bool physical)
{
	struct encoder *encoder = NULL;
	int32_t i;
	struct transmitter_configuration config = { { .transmitter_phy_id =
		TRANSMITTER_UNKNOWN, .output_channel_mapping.raw =
		INVALID_DDI_CHANNEL_MAPPING }, { .transmitter_phy_id =
		TRANSMITTER_UNKNOWN, .output_channel_mapping.raw =
		INVALID_DDI_CHANNEL_MAPPING } };

	/* Find last internal encoder*/
	for (i = path->number_of_links - 1; i >= 0; --i) {
		encoder = path->stream_contexts[i].encoder;
		if (!dal_encoder_get_supported_features(
			encoder).flags.bits.EXTERNAL_ENCODER)
			break;
		else
			encoder = NULL;
	}

	if (encoder == NULL)
		return config;

	/* Fetch mapping for primary transmitter*/
	if (physical)
		/* this is for SMU per PCIe lane power gating */
		config.primary_transmitter_config.transmitter_phy_id =
			dal_encoder_get_phy(encoder);
	else
		/* this is for CPlib HW register r/w */
		config.primary_transmitter_config.transmitter_phy_id =
			dal_encoder_get_transmitter(encoder);

	if (config.primary_transmitter_config.transmitter_phy_id
		!= TRANSMITTER_UNKNOWN)
		config.primary_transmitter_config.output_channel_mapping =
			path->ddi_channel_mapping;

	/* Fetch mapping for secondary transmitter*/
	switch (dal_display_path_get_query_signal(path, ASIC_LINK_INDEX)) {
	case SIGNAL_TYPE_DVI_DUAL_LINK:
		if (physical)
			config.secondary_transmitter_config.transmitter_phy_id =
				dal_encoder_get_paired_phy(encoder);
		else
			config.secondary_transmitter_config.transmitter_phy_id =
				dal_encoder_get_paired_transmitter(encoder);

		config.secondary_transmitter_config.output_channel_mapping =
			path->ddi_channel_mapping;
		break;

	default:
		break;
	}

	return config;

}
enum signal_type dal_display_path_get_active_signal(
	struct display_path *path,
	uint32_t idx)
{
	if (dal_display_path_is_acquired(path))
		return dal_display_path_get_config_signal(path, idx);
	else
		return dal_display_path_get_query_signal(path, idx);
}

void dal_display_path_set_ddi_channel_mapping(
	struct display_path *path,
	union ddi_channel_mapping mapping)
{
	path->ddi_channel_mapping = mapping;
}

bool dal_display_path_contains_object(
	struct display_path *path,
	struct graphics_object_id id)
{
	struct graphics_object_id path_obj;
	uint32_t i;
	/* 1. Check Connector*/
	if (path->connector != NULL) {
		path_obj = dal_connector_get_graphics_object_id(
			path->connector);
		if (dal_graphics_object_id_is_equal(path_obj, id))
			return true;
	}

	/* 2. Check link components*/
	for (i = 0; i < path->number_of_links; i++) {
		if (path->stream_contexts[i].encoder != NULL) {
			path_obj = dal_encoder_get_graphics_object_id(
				path->stream_contexts[i].encoder);
			if (dal_graphics_object_id_is_equal(path_obj, id))
				return true;
		}

		if (path->stream_contexts[i].audio != NULL) {
			path_obj = dal_audio_get_graphics_object_id(
				path->stream_contexts[i].audio);
			if (dal_graphics_object_id_is_equal(path_obj, id))
				return true;
		}
	}

	/* 3. Check DCP mandatory components*/
	if (path->clock_source != NULL) {
		path_obj = dal_clock_source_get_graphics_object_id(
			path->clock_source);
		if (dal_graphics_object_id_is_equal(path_obj, id))
			return true;
	}

	if (path->alt_clock_source != NULL) {
		path_obj = dal_clock_source_get_graphics_object_id(
			path->alt_clock_source);
		if (dal_graphics_object_id_is_equal(path_obj, id))
			return true;
	}

	{
		uint8_t planes_no = dal_display_path_get_number_of_planes(path);
		uint8_t i;
		struct controller *controller;

		for (i = 0; i < planes_no; i++) {
			controller =
				dal_display_path_get_controller_for_layer_index(
					path, i);

			path_obj = dal_controller_get_graphics_object_id(
					controller);

			if (dal_graphics_object_id_is_equal(path_obj, id))
				return true;
		}
	}

	if (path->stereo_sync_object != NULL) {
		path_obj = dal_encoder_get_graphics_object_id(
			path->stereo_sync_object);
		if (dal_graphics_object_id_is_equal(path_obj, id))
			return true;
	}

	if (path->sync_output_object != NULL) {
		path_obj = dal_encoder_get_graphics_object_id(
			path->sync_output_object);
		if (dal_graphics_object_id_is_equal(path_obj, id))
			return true;
	}

	/*
	 * TODO:Add GLSync here
	 */
	return false;
}

/********************
 * Multi-plane code
 ********************/

bool dal_display_path_add_plane(
	struct display_path *path,
	struct display_path_plane *plane)
{
	if (false == display_path_plane_vector_append(
			path->planes,
			plane)) {
		/* TODO: add a debug message here */
		return false;
	}

	/* this is the case when root controller added */
	if (dal_vector_get_count(path->planes) == 1)
		path->src_tgt_state.src_blanked = DISPLAY_TRI_STATE_UNKNOWN;

	return true;
}

uint8_t dal_display_path_get_number_of_planes(
	const struct display_path *path)
{
	return dal_vector_get_count(path->planes);
}

struct display_path_plane *dal_display_path_get_plane_at_index(
	const struct display_path *path,
	uint8_t index)
{
	return display_path_plane_vector_at_index(path->planes, index);
}

struct controller *dal_display_path_get_controller_for_layer_index(
	const struct display_path *path,
	uint8_t layer_index)
{
	struct display_path_plane *plane;

	/* the planes are ordered starting from zero */
	plane = dal_display_path_get_plane_at_index(path, layer_index);
	if (plane)
		return plane->controller;
	else {
		/* Not found. TODO: add a debug message here. */
		return NULL;
	}
}

void dal_display_path_release_planes(
	struct display_path *path)
{
	dal_vector_clear(path->planes);
}

void dal_display_path_release_non_root_planes(
	struct display_path *path)
{
	uint32_t last_plane = dal_vector_get_count(path->planes) - 1;
	for (; last_plane > 0; last_plane--)
		dal_vector_remove_at_index(path->planes, last_plane);
}
/********************
 * End-of-Multi-plane code
 ********************/
