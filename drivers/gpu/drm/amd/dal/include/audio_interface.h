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

#ifndef __DAL_AUDIO_INTERFACE_H__
#define __DAL_AUDIO_INTERFACE_H__

#include "audio_types.h"
#include "adapter_service_interface.h"
#include "signal_types.h"
#include "link_service_types.h"

/* forward declaration */
struct audio;
struct dal_adapter_service;

/*****  audio initialization data  *****/
/*
 * by audio, it means audio endpoint id. ASIC may have many endpoints.
 * upper sw layer will create one audio object instance for each endpoints.
 * ASIC support internal audio only. So enum number is used to differ
 * each endpoint
 */
struct audio_init_data {
	struct adapter_service *as;
	struct graphics_object_id audio_stream_id;
	struct dc_context *ctx;
};

enum audio_result {
	AUDIO_RESULT_OK,
	AUDIO_RESULT_ERROR,
};

/****** audio object create, destroy ******/
struct audio *dal_audio_create(
	const struct audio_init_data *init_data);

void dal_audio_destroy(
	struct audio **audio);

/****** graphics object interface ******/
const struct graphics_object_id dal_audio_get_graphics_object_id(
	const struct audio *audio);

/* Enumerate Graphics Object supported Input/Output Signal Types */
uint32_t dal_audio_enumerate_input_signals(
	struct audio *audio);

uint32_t dal_audio_enumerate_output_signals(
	struct audio *audio);

/*  Check if signal supported by GraphicsObject  */
bool dal_audio_is_input_signal_supported(
	struct audio *audio,
	enum signal_type signal);

bool dal_audio_is_output_signal_supported(
	struct audio *audio,
	enum signal_type signal);

/***** programming interface *****/

/* perform power up sequence (boot up, resume, recovery) */
enum audio_result dal_audio_power_up(
	struct audio *audio);

/* perform power down (shut down, stand by) */
enum audio_result dal_audio_power_down(
	struct audio *audio);

/* setup audio */
enum audio_result dal_audio_setup(
	struct audio *audio,
	struct audio_output *output,
	struct audio_info *info);

/* enable audio */
enum audio_result dal_audio_enable_output(
	struct audio *audio,
	enum engine_id engine_id,
	enum signal_type signal);

/* disable audio */
enum audio_result dal_audio_disable_output(
	struct audio *audio,
	enum engine_id engine_id,
	enum signal_type signal);

/* enable azalia audio endpoint */
enum audio_result dal_audio_enable_azalia_audio_jack_presence(
	struct audio *audio,
	enum engine_id engine_id);

/* disable azalia audio endpoint */
enum audio_result dal_audio_disable_azalia_audio_jack_presence(
	struct audio *audio,
	enum engine_id engine_id);

/* unmute audio */
enum audio_result dal_audio_unmute(
	struct audio *audio,
	enum engine_id engine_id,
	enum signal_type signal);

/* mute audio */
enum audio_result dal_audio_mute(
	struct audio *audio,
	enum engine_id engine_id,
	enum signal_type signal);

/***** information interface *****/

struct audio_feature_support dal_audio_get_supported_features(
	struct audio *audio);

/* get audio bandwidth information */
void dal_audio_check_audio_bandwidth(
	struct audio *audio,
	const struct audio_crtc_info *info,
	uint32_t channel_count,
	enum signal_type signal,
	union audio_sample_rates *sample_rates);

/* Enable multi channel split */
void dal_audio_enable_channel_splitting_mapping(
	struct audio *audio,
	enum engine_id engine_id,
	enum signal_type signal,
	const struct audio_channel_associate_info *audio_mapping,
	bool enable);

/* get current multi channel split. */
enum audio_result dal_audio_get_channel_splitting_mapping(
	struct audio *audio,
	enum engine_id engine_id,
	struct audio_channel_associate_info *audio_mapping);

/* set payload value for the unsolicited response */
void dal_audio_set_unsolicited_response_payload(
	struct audio *audio,
	enum audio_payload payload);

/*Assign GTC group and enable GTC value embedding*/
void dal_audio_enable_gtc_embedding_with_group(
	struct audio *audio,
	uint32_t group_num,
	uint32_t audio_latency);

/* Disable GTC value embedding */
void dal_audio_disable_gtc_embedding(
	struct audio *audio);

/* Update audio wall clock source */
void dal_audio_setup_audio_wall_dto(
	struct audio *audio,
	enum signal_type signal,
	const struct audio_crtc_info *crtc_info,
	const struct audio_pll_info *pll_info);

#endif
