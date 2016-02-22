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

#include "audio_dce110.h"

/***** static functions  *****/

static void destruct(struct audio_dce110 *audio)
{
	/*release memory allocated for hw_ctx -- allocated is initiated
	 *by audio_dce110 power_up
	 *audio->base->hw_ctx = NULL is done within hw-ctx->destroy
	 */
	if (audio->base.hw_ctx)
		audio->base.hw_ctx->funcs->destroy(&(audio->base.hw_ctx));

	/* reset base_audio_block */
	dal_audio_destruct_base(&audio->base);
}

static void destroy(struct audio **ptr)
{
	struct audio_dce110 *audio = NULL;

	audio = container_of(*ptr, struct audio_dce110, base);

	destruct(audio);

	/* release memory allocated for audio_dce110*/
	dm_free(audio);
	*ptr = NULL;
}

/* The inital call of hook function comes from audio object level.
 *The passing object handle "struct audio *audio" point to base object
 *already.There is not need to get base object from audio_dce110.
 */

/**
* setup
*
* @brief
*  setup Audio HW block, to be called by dal_audio_setup
*
*/
static enum audio_result setup(
	struct audio *audio,
	struct audio_output *output,
	struct audio_info *info)
{
	switch (output->signal) {
	case SIGNAL_TYPE_HDMI_TYPE_A:
		/*setup HDMI audio engine*/
		audio->hw_ctx->funcs->enable_afmt_clock(
			audio->hw_ctx,
			output->engine_id,
			true);
		audio->hw_ctx->funcs->setup_hdmi_audio(
			audio->hw_ctx, output->engine_id, &output->crtc_info);

		audio->hw_ctx->funcs->setup_azalia(
			audio->hw_ctx,
			output->engine_id,
			output->signal,
			&output->crtc_info,
			&output->pll_info,
			info);
		break;

	case SIGNAL_TYPE_WIRELESS:
		/* setup Azalia block for Wireless Display - This
			is different than for wired
			displays because there is no
			DIG to program.*/
		/*TODO:
		audio->hw_ctx->funcs->setup_azalia_for_vce(
			audio->hw_ctx,
			audio->signal,
			audio->crtc_info,
			info);
		*/
		break;
	case SIGNAL_TYPE_DISPLAY_PORT:
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
	case SIGNAL_TYPE_EDP:
		/* setup DP audio engine will be done at enable output */

		/* setup Azalia block*/
		audio->hw_ctx->funcs->setup_azalia(
			audio->hw_ctx,
			output->engine_id,
			output->signal,
			&output->crtc_info,
			&output->pll_info,
			info);

		break;
	default:
		return AUDIO_RESULT_ERROR;
	}

	return AUDIO_RESULT_OK;
}

/**
* enable_output
*
* @brief
*  enable Audio HW block, to be called by dal_audio_enable_output
*/
static enum audio_result enable_output(
	struct audio *audio,
	enum engine_id engine_id,
	enum signal_type signal)
{
	/* enable audio output */
	switch (signal) {
	case SIGNAL_TYPE_HDMI_TYPE_A:
		break;
	case SIGNAL_TYPE_DISPLAY_PORT:
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
	case SIGNAL_TYPE_EDP: {
			/* enable AFMT clock before enable audio*/
			audio->hw_ctx->funcs->enable_afmt_clock(
				audio->hw_ctx, engine_id, true);
			/* setup DP audio engine */
			audio->hw_ctx->funcs->setup_dp_audio(
				audio->hw_ctx, engine_id);
			/* enabl DP audio packets will be done at unblank */
			audio->hw_ctx->funcs->enable_dp_audio(
				audio->hw_ctx, engine_id);
		}
		break;
	case SIGNAL_TYPE_WIRELESS:
		/* route audio to VCE block */
		audio->hw_ctx->funcs->setup_vce_audio(audio->hw_ctx);
		break;
	default:
		return AUDIO_RESULT_ERROR;
	}
	return AUDIO_RESULT_OK;
}

/**
* disable_output
*
* @brief
*  disable Audio HW block, to be called by dal_audio_disable_output
*
*/
static enum audio_result disable_output(
	struct audio *audio,
	enum engine_id engine_id,
	enum signal_type signal)
{
	switch (signal) {
	case SIGNAL_TYPE_HDMI_TYPE_A:
	case SIGNAL_TYPE_WIRELESS:
		/* disable HDMI audio */
		audio->hw_ctx->
			funcs->disable_azalia_audio(
					audio->hw_ctx, engine_id);
		audio->hw_ctx->
			funcs->enable_afmt_clock(
					audio->hw_ctx, engine_id,
					false);

		break;
	case SIGNAL_TYPE_DISPLAY_PORT:
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
	case SIGNAL_TYPE_EDP: {
			/* disable DP audio */
			audio->hw_ctx->funcs->disable_dp_audio(
				audio->hw_ctx, engine_id);
			audio->hw_ctx->funcs->disable_azalia_audio(
				audio->hw_ctx, engine_id);
			audio->hw_ctx->funcs->enable_afmt_clock(
				audio->hw_ctx, engine_id, false);
		}
		break;
	default:
		return AUDIO_RESULT_ERROR;
	}

	return AUDIO_RESULT_OK;
}

/**
* unmute
*
* @brief
*  unmute audio, to be called by dal_audio_unmute
*
*/
static enum audio_result unmute(
	struct audio *audio,
	enum engine_id engine_id,
	enum signal_type signal)
{
	switch (signal) {
	case SIGNAL_TYPE_HDMI_TYPE_A:
	case SIGNAL_TYPE_DISPLAY_PORT:
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
	case SIGNAL_TYPE_EDP:
		/* unmute Azalia audio */
		audio->hw_ctx->funcs->unmute_azalia_audio(
				audio->hw_ctx, engine_id);
		break;
	case SIGNAL_TYPE_WIRELESS:
		/*Do nothing for wireless display*/
		break;
	default:
		return AUDIO_RESULT_ERROR;
	}
	return AUDIO_RESULT_OK;
}

/**
* mute
*
* @brief
*  mute audio, to be called  by dal_audio_nmute
*
*/
static enum audio_result mute(
	struct audio *audio,
	enum engine_id engine_id,
	enum signal_type signal)
{
	switch (signal) {
	case SIGNAL_TYPE_HDMI_TYPE_A:
	case SIGNAL_TYPE_DISPLAY_PORT:
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
	case SIGNAL_TYPE_EDP:
		/* mute Azalia audio */
		audio->hw_ctx->funcs->mute_azalia_audio(
				audio->hw_ctx, engine_id);
		break;
	case SIGNAL_TYPE_WIRELESS:
		/*Do nothing for wireless display*/
		break;
	default:
		return AUDIO_RESULT_ERROR;
	}
	return AUDIO_RESULT_OK;
}

/**
* initialize
*
* @brief
*  Perform SW initialization - create audio hw context. Then do HW
*  initialization. this function is called at dal_audio_power_up.
*
*/
static enum audio_result initialize(
	struct audio *audio)
{
	uint8_t audio_endpoint_enum_id = 0;

	audio_endpoint_enum_id = audio->id.enum_id;

	/* HW CTX already create*/
	if (audio->hw_ctx != NULL)
		return AUDIO_RESULT_OK;

	audio->hw_ctx = dal_hw_ctx_audio_dce110_create(
			audio->ctx,
			audio_endpoint_enum_id);

	if (audio->hw_ctx == NULL)
		return AUDIO_RESULT_ERROR;

	/* override HW default settings */
	audio->hw_ctx->funcs->hw_initialize(audio->hw_ctx);

	return AUDIO_RESULT_OK;
}

/* enable multi channel split */
static void enable_channel_splitting_mapping(
	struct audio *audio,
	enum engine_id engine_id,
	enum signal_type signal,
	const struct audio_channel_associate_info *audio_mapping,
	bool enable)
{
	audio->hw_ctx->funcs->setup_channel_splitting_mapping(
		audio->hw_ctx,
		engine_id,
		signal,
		audio_mapping, enable);
}

/* get current multi channel split. */
static enum audio_result get_channel_splitting_mapping(
	struct audio *audio,
	enum engine_id engine_id,
	struct audio_channel_associate_info *audio_mapping)
{
	if (audio->hw_ctx->funcs->get_channel_splitting_mapping(
		audio->hw_ctx, engine_id, audio_mapping)) {
		return AUDIO_RESULT_OK;
	} else {
		return AUDIO_RESULT_ERROR;
	}
}

/**
* set_unsolicited_response_payload
*
* @brief
*  Set payload value for the unsolicited response
*/
static void set_unsolicited_response_payload(
	struct audio *audio,
	enum audio_payload payload)
{
	audio->hw_ctx->funcs->set_unsolicited_response_payload(
			audio->hw_ctx, payload);
}

/**
* setup_audio_wall_dto
*
* @brief
*  Update audio source clock from hardware context.
*
*/
static void setup_audio_wall_dto(
	struct audio *audio,
	enum signal_type signal,
	const struct audio_crtc_info *crtc_info,
	const struct audio_pll_info *pll_info)
{
	audio->hw_ctx->funcs->setup_audio_wall_dto(
		audio->hw_ctx, signal, crtc_info, pll_info);
}

/**
* get_supported_features
*
* @brief
*  options and features supported by Audio
*  returns supported engines, signals.
*  features are reported for HW audio/Azalia block rather then Audio object
*  itself the difference for DCE6.x is that MultiStream Audio is now supported
*
*/
static struct audio_feature_support get_supported_features(struct audio *audio)
{
	struct audio_feature_support afs = {0};

	afs.ENGINE_DIGA = 1;
	afs.ENGINE_DIGB = 1;
	afs.ENGINE_DIGC = 1;
	afs.MULTISTREAM_AUDIO = 1;

	return afs;
}

static const struct audio_funcs funcs = {
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

static bool construct(
	struct audio_dce110 *audio,
	const struct audio_init_data *init_data)
{
	struct audio *base = &audio->base;

	/* base audio construct*/
	if (!dal_audio_construct_base(base, init_data))
		return false;

	/*vtable methods*/
	base->funcs = &funcs;
	return true;
}

/* --- audio scope functions  --- */

struct audio *dal_audio_create_dce110(
	const struct audio_init_data *init_data)
{
	/*allocate memory for audio_dce110 */
	struct audio_dce110 *audio = dm_alloc(sizeof(*audio));

	if (audio == NULL) {
		ASSERT_CRITICAL(audio);
		return NULL;
	}
	/*pointer to base_audio_block of audio_dce110 ==> audio base object */
	if (construct(audio, init_data))
		return &audio->base;

	dal_logger_write(
		init_data->ctx->logger,
		LOG_MAJOR_ERROR,
		LOG_MINOR_COMPONENT_AUDIO,
		"Failed to create audio object for DCE11\n");

	 /*release memory allocated if fail */
	dm_free(audio);
	return NULL;
}

/* Do not need expose construct_dce110 and destruct_dce110 becuase there is
 *derived object after dce110
 */

