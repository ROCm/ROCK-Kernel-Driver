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

#ifndef __DAL_AUDIO_H__
#define __DAL_AUDIO_H__

#include "include/audio_interface.h"
#include "hw_ctx_audio.h"
#include "include/link_service_types.h"

/***** only for hook functions  *****/
/**
 *which will be overwritten by derived audio object.
 *audio hw context object is independent object
 */

struct audio;

struct audio_funcs {
	/*
	 *get_object_id
	 *get_object_type
	 *enumerate_input_signals
	 *enumerate_output_signals
	 *is_input_signal_supported
	 *is_output_signal_supported
	 *set_object_properties
	 *get_object_properties
	 */

	void (*destroy)(struct audio **audio);
	/*power_up
	 *power_down
	 *release_hw_base
	 */

	/* setup audio */
	enum audio_result (*setup)(
		struct audio *audio,
		struct audio_output *output,
		struct audio_info *info);

	enum audio_result (*enable_output)(
		struct audio *audio,
		enum engine_id engine_id,
		enum signal_type signal);

	enum audio_result (*disable_output)(
		struct audio *audio,
		enum engine_id engine_id,
		enum signal_type signal);

	/*enable_azalia_audio_jack_presence
	 * disable_azalia_audio_jack_presence
	 */

	enum audio_result (*unmute)(
		struct audio *audio,
		enum engine_id engine_id,
		enum signal_type signal);

	enum audio_result (*mute)(
		struct audio *audio,
		enum engine_id engine_id,
		enum signal_type signal);

	/* SW initialization that cannot be done in constructor. This will
	 * be done is audio_power_up but is not in audio_interface. It is only
	 * called by power_up
	 */
	enum audio_result (*initialize)(
		struct audio *audio);

	/* enable channel splitting mapping */
	void (*enable_channel_splitting_mapping)(
		struct audio *audio,
		enum engine_id engine_id,
		enum signal_type signal,
		const struct audio_channel_associate_info *audio_mapping,
		bool enable);

	/* get current multi channel split. */
	enum audio_result (*get_channel_splitting_mapping)(
		struct audio *audio,
		enum engine_id engine_id,
		struct audio_channel_associate_info *audio_mapping);

	/* set payload value for the unsolicited response */
	void (*set_unsolicited_response_payload)(
		struct audio *audio,
		enum audio_payload payload);

	/* Update audio wall clock source */
	void (*setup_audio_wall_dto)(
		struct audio *audio,
		enum signal_type signal,
		const struct audio_crtc_info *crtc_info,
		const struct audio_pll_info *pll_info);

	/* options and features supported by Audio */
	struct audio_feature_support (*get_supported_features)(
		struct audio *audio);

	/*
	 *check_audio_bandwidth
	 *write_reg
	 *read_reg
	 *enable_gtc_embedding_with_group
	 *disable_gtc_embedding
	 *register_interrupt
	 *unregister_interrupt
	 *process_interrupt
	 *create_hw_ctx
	 *getHwCtx
	 *setHwCtx
	 *handle_interrupt
	 */
};

struct audio {
	/* hook functions. they will be overwritten by specific ASIC */
	const struct audio_funcs *funcs;
	/* TODO: static struct audio_funcs funcs;*/

	/*external structures - get service from external*/
	struct graphics_object_id id;
	struct adapter_service *adapter_service;
	/* audio HW context */
	struct hw_ctx_audio *hw_ctx;
	struct dc_context *ctx;
	/* audio supports input and output signals */
	uint32_t input_signals;
	uint32_t output_signals;
};

/* - functions defined by audio.h will be used by audio component only.
 *   but audio.c also implements some function defined by dal\include
 */

/* graphics_object_base implemention
 * 1.input_signals and output_signals are moved
 * into audio object.
 *
 * 2.Get the Graphics Object ID
 *
 * Outside audio:
 * use dal_graphics_object_id_get_audio_id
 * Within audio:
 *	use audio->go_base.id
 *
 * 3. Get the Graphics Object Type
 *
 *  use object_id.type
 *  not function implemented.
 * 4. Common Graphics Object Properties
 * use object id ->go_properties.multi_path
 * not function implemented.
 */

bool dal_audio_construct_base(
	struct audio *audio,
	const struct audio_init_data *init_data);

void dal_audio_destruct_base(
	struct audio *audio);

#endif  /* __DAL_AUDIO__ */
