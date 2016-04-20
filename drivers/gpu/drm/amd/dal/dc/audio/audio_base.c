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

#include "dm_services.h"

#include "include/logger_interface.h"

#include "audio.h"
#include "hw_ctx_audio.h"

#if defined(CONFIG_DRM_AMD_DAL_DCE8_0)
#include "dce80/audio_dce80.h"
#include "dce80/hw_ctx_audio_dce80.h"
#endif

#if defined(CONFIG_DRM_AMD_DAL_DCE11_0)
#include "dce110/audio_dce110.h"
#include "dce110/hw_ctx_audio_dce110.h"
#endif


/***** static function : only used within audio.c *****/

/* stub for hook functions */
static void destroy(
	struct audio **audio)
{
	/*DCE specific, must be implemented in derived*/
	BREAK_TO_DEBUGGER();
}

static enum audio_result setup(
	struct audio *audio,
	struct audio_output *output,
	struct audio_info *info)
{
	/*DCE specific, must be implemented in derived*/
	BREAK_TO_DEBUGGER();
	return AUDIO_RESULT_OK;
}

static enum audio_result enable_output(
	struct audio *audio,
	enum engine_id engine_id,
	enum signal_type signal)
{
	/*DCE specific, must be implemented in derived*/
	BREAK_TO_DEBUGGER();
	return AUDIO_RESULT_OK;
}

static enum audio_result disable_output(
	struct audio *audio,
	enum engine_id engine_id,
	enum signal_type signal)
{
	/*DCE specific, must be implemented in derived*/
	BREAK_TO_DEBUGGER();
	return AUDIO_RESULT_OK;
}

static enum audio_result unmute(
	struct audio *audio,
	enum engine_id engine_id,
	enum signal_type signal)
{
	/*DCE specific, must be implemented in derived*/
	BREAK_TO_DEBUGGER();
	return AUDIO_RESULT_OK;
}

static enum audio_result mute(
	struct audio *audio,
	enum engine_id engine_id,
	enum signal_type signal)
{
	/*DCE specific, must be implemented in derived*/
	BREAK_TO_DEBUGGER();
	return AUDIO_RESULT_OK;
}

static enum audio_result initialize(
	struct audio *audio)
{
	/*DCE specific, must be implemented in derived. Implemeentaion of
	 *initialize will create audio hw context. create_hw_ctx
	 */
	BREAK_TO_DEBUGGER();
	return AUDIO_RESULT_OK;
}

static void enable_channel_splitting_mapping(
	struct audio *audio,
	enum engine_id engine_id,
	enum signal_type signal,
	const struct audio_channel_associate_info *audio_mapping,
	bool enable)
{
	/*DCE specific, must be implemented in derived*/
	BREAK_TO_DEBUGGER();
}

/* get current multi channel split. */
static enum audio_result get_channel_splitting_mapping(
	struct audio *audio,
	enum engine_id engine_id,
	struct audio_channel_associate_info *audio_mapping)
{
	/*DCE specific, must be implemented in derived*/
	BREAK_TO_DEBUGGER();
	return AUDIO_RESULT_OK;
}

/* set payload value for the unsolicited response */
static void set_unsolicited_response_payload(
	struct audio *audio,
	enum audio_payload payload)
{
	/*DCE specific, must be implemented in derived*/
	BREAK_TO_DEBUGGER();
}

/* update audio wall clock source */
static void setup_audio_wall_dto(
	struct audio *audio,
	enum signal_type signal,
	const struct audio_crtc_info *crtc_info,
	const struct audio_pll_info *pll_info)
{
	/*DCE specific, must be implemented in derived*/
	BREAK_TO_DEBUGGER();
}

static struct audio_feature_support get_supported_features(struct audio *audio)
{
	/*DCE specific, must be implemented in derived*/
	struct audio_feature_support features;

	memset(&features, 0, sizeof(features));

	features.ENGINE_DIGA = 1;
	features.ENGINE_DIGB = 1;

	return features;
}

static const struct audio_funcs audio_funcs = {
	.destroy = destroy,
	.setup = setup,
	.enable_output = enable_output,
	.disable_output = disable_output,
	.unmute = unmute,
	.mute = mute,
	.initialize = initialize,
	.enable_channel_splitting_mapping =
		enable_channel_splitting_mapping,
	.get_channel_splitting_mapping =
		get_channel_splitting_mapping,
	.set_unsolicited_response_payload =
		set_unsolicited_response_payload,
	.setup_audio_wall_dto = setup_audio_wall_dto,
	.get_supported_features = get_supported_features,
};

/***** SCOPE : declare in audio.h. use within dal-audio. *****/

bool dal_audio_construct_base(
	struct audio *audio,
	const struct audio_init_data *init_data)
{
	enum signal_type signals = SIGNAL_TYPE_HDMI_TYPE_A;

	ASSERT(init_data->as != NULL);

	/* base hook functions */
	audio->funcs = &audio_funcs;

	/*setup pointers to get service from dal service compoenents*/
	audio->adapter_service = init_data->as;

	audio->ctx = init_data->ctx;

	/* save audio endpoint number to identify object creating */
	audio->id = init_data->audio_stream_id;

	/* Fill supported signals. !!! be aware that android definition is
	 * already shift to vector.
	 */
	signals |= SIGNAL_TYPE_DISPLAY_PORT;
	signals |= SIGNAL_TYPE_DISPLAY_PORT_MST;
	signals |= SIGNAL_TYPE_EDP;
	signals |= SIGNAL_TYPE_DISPLAY_PORT;
	signals |= SIGNAL_TYPE_WIRELESS;

	/* Audio supports same set for input and output signals */
	audio->input_signals = signals;
	audio->output_signals = signals;

	return true;
}

/* except hw_ctx, no other hw need reset. so do nothing */
void dal_audio_destruct_base(
	struct audio *audio)
{
}

/* Enumerate Graphics Object supported Input/Output Signal Types */
uint32_t dal_audio_enumerate_input_signals(
	struct audio *audio)
{
	return audio->input_signals;
}

uint32_t dal_audio_enumerate_output_signals(
	struct audio *audio)
{
	return audio->output_signals;
}

/*  Check if signal supported by GraphicsObject  */
bool dal_audio_is_input_signal_supported(
	struct audio *audio,
	enum signal_type signal)
{
	return (signal & audio->output_signals) != 0;
}

bool dal_audio_is_output_signal_supported(
	struct audio *audio,
	enum signal_type signal)
{
	return (signal & audio->input_signals) != 0;
}

/***** SCOPE : declare in dal\include  *****/

/* audio object creator triage. memory allocate and release will be
 * done within dal_audio_create_dcexx
 */
struct audio *dal_audio_create(
	const struct audio_init_data *init_data)
{
	struct adapter_service *as;

	if (init_data->as == NULL)
		return NULL;

	as = init_data->as;
	switch (dal_adapter_service_get_dce_version(as)) {
#if defined(CONFIG_DRM_AMD_DAL_DCE8_0)
	case DCE_VERSION_8_0:
		return dal_audio_create_dce80(init_data);
#endif
#if defined(CONFIG_DRM_AMD_DAL_DCE10_0)
	case DCE_VERSION_10_0:
		return dal_audio_create_dce110(init_data);
#endif
#if defined(CONFIG_DRM_AMD_DAL_DCE11_0)
	case DCE_VERSION_11_0:
		return dal_audio_create_dce110(init_data);
#endif
#if defined(CONFIG_DRM_AMD_DAL_DCE11_2)
	case DCE_VERSION_11_2:
		return dal_audio_create_dce110(init_data);
#endif
	default:
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	return NULL;
}

/* audio object creator triage.
 * memory for "struct audio   dal_audio_create_dce8x" allocate
 * will happens within dal_audio_dce8x. memory allocate is done
 * with dal_audio_create_dce8x. memory release is initiated by
 * dal_audio_destroy. It will call hook function which will finially
 * used destroy() of dal_audio_dce8x. therefore, no memroy allocate
 *and release happen physcially at audio base object.
 */
void dal_audio_destroy(
	struct audio **audio)
{
	if (!audio || !*audio) {
		BREAK_TO_DEBUGGER();
		return;
	}

	(*audio)->funcs->destroy(audio);

	*audio = NULL;
}

const struct graphics_object_id dal_audio_get_graphics_object_id(
	const struct audio *audio)
{
	return audio->id;
}

/* enable azalia audio endpoint. This function call hw_ctx directly
 *not overwitten at audio level.
 */
enum audio_result dal_audio_enable_azalia_audio_jack_presence(
	struct audio *audio,
	enum engine_id engine_id)
{
	audio->hw_ctx->funcs->enable_azalia_audio(audio->hw_ctx, engine_id);
	return AUDIO_RESULT_OK;
}

/* disable azalia audio endpoint. This function call hw_ctx directly
 *not overwitten at audio level.
 */
enum audio_result dal_audio_disable_azalia_audio_jack_presence(
	struct audio *audio,
	enum engine_id engine_id)
{
	audio->hw_ctx->funcs->disable_azalia_audio(audio->hw_ctx, engine_id);
	return AUDIO_RESULT_OK;
}

/* get audio bandwidth information. This function call hw_ctx directly
 *not overwitten at audio level.
 */
void dal_audio_check_audio_bandwidth(
	struct audio *audio,
	const struct audio_crtc_info *info,
	uint32_t channel_count,
	enum signal_type signal,
	union audio_sample_rates *sample_rates)
{
	dal_hw_ctx_audio_check_audio_bandwidth(
		audio->hw_ctx, info, channel_count, signal, sample_rates);
}

/* DP Audio register write access. This function call hw_ctx directly
 * not overwitten at audio level.
 */

/*assign GTC group and enable GTC value embedding*/
void dal_audio_enable_gtc_embedding_with_group(
	struct audio *audio,
	uint32_t group_num,
	uint32_t audio_latency)
{
	audio->hw_ctx->funcs->enable_gtc_embedding_with_group(
		audio->hw_ctx, group_num, audio_latency);
}

/* disable GTC value embedding */
void dal_audio_disable_gtc_embedding(
	struct audio *audio)
{
	audio->hw_ctx->funcs->disable_gtc_embedding(audio->hw_ctx);
}

/* perform power up sequence (boot up, resume, recovery) */
enum audio_result dal_audio_power_up(
	struct audio *audio)
{
	return audio->funcs->initialize(audio);
}

/* perform power down (shut down, stand by) */
enum audio_result dal_audio_power_down(
	struct audio *audio)
{
	return AUDIO_RESULT_OK;
}

/* setup audio */
enum audio_result dal_audio_setup(
	struct audio *audio,
	struct audio_output *output,
	struct audio_info *info)
{
	return audio->funcs->setup(audio, output, info);
}

/* enable audio */
enum audio_result dal_audio_enable_output(
	struct audio *audio,
	enum engine_id engine_id,
	enum signal_type signal)
{
	return audio->funcs->enable_output(audio, engine_id, signal);
}

/* disable audio */
enum audio_result dal_audio_disable_output(
	struct audio *audio,
	enum engine_id engine_id,
	enum signal_type signal)
{
	return audio->funcs->disable_output(audio, engine_id, signal);
}

/* unmute audio */
enum audio_result dal_audio_unmute(
	struct audio *audio,
	enum engine_id engine_id,
	enum signal_type signal)
{
	return audio->funcs->unmute(audio, engine_id, signal);
}

/* mute audio */
enum audio_result dal_audio_mute(
	struct audio *audio,
	enum engine_id engine_id,
	enum signal_type signal)
{
	return audio->funcs->mute(audio, engine_id, signal);
}

/* Enable multi channel split */
void dal_audio_enable_channel_splitting_mapping(
	struct audio *audio,
	enum engine_id engine_id,
	enum signal_type signal,
	const struct audio_channel_associate_info *audio_mapping,
	bool enable)
{
	audio->funcs->enable_channel_splitting_mapping(
		audio, engine_id, signal, audio_mapping, enable);
}

/* get current multi channel split. */
enum audio_result dal_audio_get_channel_splitting_mapping(
	struct audio *audio,
	enum engine_id engine_id,
	struct audio_channel_associate_info *audio_mapping)
{
	return audio->funcs->get_channel_splitting_mapping(
		audio, engine_id, audio_mapping);
}

/* set payload value for the unsolicited response */
void dal_audio_set_unsolicited_response_payload(
	struct audio *audio,
	enum audio_payload payload)
{
	audio->funcs->set_unsolicited_response_payload(audio, payload);
}

/* update audio wall clock source */
void dal_audio_setup_audio_wall_dto(
	struct audio *audio,
	enum signal_type signal,
	const struct audio_crtc_info *crtc_info,
	const struct audio_pll_info *pll_info)
{
	audio->funcs->setup_audio_wall_dto(audio, signal, crtc_info, pll_info);
}

struct audio_feature_support dal_audio_get_supported_features(
	struct audio *audio)
{
	return audio->funcs->get_supported_features(audio);
}
