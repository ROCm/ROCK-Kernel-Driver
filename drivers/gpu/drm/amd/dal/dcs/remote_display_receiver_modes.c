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
#include "include/audio_types.h"
#include "include/dcs_interface.h"

#include "remote_display_receiver_modes.h"

const struct cea_audio_mode default_audio_modes[] = {
	/* 0  -  LPCM,  2 channels,  44.1Hz, 16 bits*/
	{AUDIO_FORMAT_CODE_LINEARPCM, 2, 2, {1} },
	/* 1  -  LPCM,  2 channels,  48Hz,   16 bits,*/
	{AUDIO_FORMAT_CODE_LINEARPCM, 2, 4, {1} },
	/* 2  -  AAC,   2 channels,  48Hz,   16 bits,*/
	{AUDIO_FORMAT_CODE_AAC,       2, 4, {2} },
	/* 3  -  AAC,   4 channels,  48Hz,   16 bits,*/
	{AUDIO_FORMAT_CODE_AAC,       4, 4, {2} },
	/* 4  -  AAC,   6 channels,  48Hz,   16 bits,*/
	{AUDIO_FORMAT_CODE_AAC,       6, 4, {2} },
	/* 5  -  AAC,   8 channels,  48Hz,   16 bits,*/
	{AUDIO_FORMAT_CODE_AAC,       8, 4, {2} },
	/* 6  -  AC3,   2 channels,  48Hz,   16 bits,*/
	{AUDIO_FORMAT_CODE_AC3,       2, 4, {2} },
	/* 7  -  AC3,   4 channels,  48Hz,   16 bits,*/
	{AUDIO_FORMAT_CODE_AC3,       4, 4, {2} },
    /* 8  -  AC3,   6 channels , 48Hz,   16 bits,*/
	{AUDIO_FORMAT_CODE_AC3,       6, 4, {2} }
};


struct remote_display_receiver_modes {
	const struct timing_service *ts;
	bool supports_miracast;
	struct dal_remote_display_receiver_capability rdrm_caps;
};

const struct mode_info rdrm_default_cea_modes[] = {
	{640, 480, 60, TIMING_STANDARD_DMT, TIMING_SOURCE_DEFAULT,
			{0, 0, 0, 0, 0, 0} }, /* 0 */
	{720, 480, 60, TIMING_STANDARD_CEA861, TIMING_SOURCE_DEFAULT,
			{0, 0, 0, 0, 0, 0} }, /* 1 */
	{720, 480, 60, TIMING_STANDARD_CEA861, TIMING_SOURCE_DEFAULT,
			{1, 0, 0, 0, 0, 0} }, /* 2 */
	{720, 576, 50, TIMING_STANDARD_CEA861, TIMING_SOURCE_DEFAULT,
			{0, 0, 0, 0, 0, 0} }, /* 3 */
	{720, 576, 50, TIMING_STANDARD_CEA861, TIMING_SOURCE_DEFAULT,
			{1, 0, 0, 0, 0, 0} }, /* 4 */
	{1280, 720, 30, TIMING_STANDARD_CEA861, TIMING_SOURCE_DEFAULT,
			{0, 0, 0, 0, 0, 0} }, /* 5 */
	{1280, 720, 60, TIMING_STANDARD_CEA861, TIMING_SOURCE_DEFAULT,
			{0, 0, 0, 0, 0, 0} }, /* 6 */
	{1920, 1080, 30, TIMING_STANDARD_CEA861, TIMING_SOURCE_DEFAULT,
			{0, 0, 0, 0, 0, 0} }, /* 7 */
	{1920, 1080, 60, TIMING_STANDARD_CEA861, TIMING_SOURCE_DEFAULT,
			{0, 0, 0, 0, 0, 0} }, /* 8 */
	{1920, 1080, 60, TIMING_STANDARD_CEA861, TIMING_SOURCE_DEFAULT,
			{1, 0, 0, 0, 0, 0} }, /* 9 */
	{1280, 720, 25, TIMING_STANDARD_CEA861, TIMING_SOURCE_DEFAULT,
			{0, 0, 0, 0, 0, 0} }, /* 10 */
	{1280, 720, 50, TIMING_STANDARD_CEA861, TIMING_SOURCE_DEFAULT,
			{0, 0, 0, 0, 0, 0} }, /* 11 */
	{1920, 1080, 25, TIMING_STANDARD_CEA861, TIMING_SOURCE_DEFAULT,
			{0, 0, 0, 0, 0, 0} }, /* 12 */
	{1920, 1080, 50, TIMING_STANDARD_CEA861, TIMING_SOURCE_DEFAULT,
			{0, 0, 0, 0, 0, 0} }, /* 13 */
	{1920, 1080, 50, TIMING_STANDARD_CEA861, TIMING_SOURCE_DEFAULT,
			{1, 0, 0, 0, 0, 0} }, /* 14 */
	{1280, 720, 24, TIMING_STANDARD_CEA861, TIMING_SOURCE_DEFAULT,
			{0, 0, 0, 0, 0, 0} }, /* 15 */
	{1920, 1080, 24, TIMING_STANDARD_CEA861, TIMING_SOURCE_DEFAULT,
			{0, 0, 0, 0, 0, 0} }, /* 16 */
};

/*
 * Some of the modes in this table are not "real modes". We have to keep these
 * entries in table because receiver cap is a bit vector and if we use a
 * different table format, we need to add translation logic else where in DAL.
 * For those modes, we set timing standard to undefined, and we will not insert
 * them into the mode list.
 */
const struct mode_info rdrm_default_vesa_modes[] = {
	{800, 600, 30, TIMING_STANDARD_DMT, TIMING_SOURCE_DEFAULT,
			{0, 0, 0, 0, 0, 0} }, /* 0 */
	{800, 600, 60, TIMING_STANDARD_DMT, TIMING_SOURCE_DEFAULT,
			{0, 0, 0, 0, 0, 0} }, /* 1 */
	{1024, 768, 30, TIMING_STANDARD_DMT, TIMING_SOURCE_DEFAULT,
			{0, 0, 0, 0, 0, 0} }, /* 2 */
	{1024, 768, 60, TIMING_STANDARD_DMT, TIMING_SOURCE_DEFAULT,
			{0, 0, 0, 0, 0, 0} }, /* 3 */
	{1152, 864, 30, TIMING_STANDARD_UNDEFINED, TIMING_SOURCE_DEFAULT,
			{0, 0, 0, 0, 0, 0} }, /* 4 */
	{1152, 864, 60, TIMING_STANDARD_UNDEFINED, TIMING_SOURCE_DEFAULT,
			{0, 0, 0, 0, 0, 0} }, /* 5 */
	{1280, 768, 30, TIMING_STANDARD_DMT, TIMING_SOURCE_DEFAULT,
			{0, 0, 0, 0, 0, 0} }, /* 6 */
	{1280, 768, 60, TIMING_STANDARD_DMT, TIMING_SOURCE_DEFAULT,
			{0, 0, 0, 0, 0, 0} }, /* 7 */
	{1280, 800, 30, TIMING_STANDARD_DMT, TIMING_SOURCE_DEFAULT,
			{0, 0, 0, 0, 0, 0} }, /* 8 */
	{1280, 800, 60, TIMING_STANDARD_DMT, TIMING_SOURCE_DEFAULT,
			{0, 0, 0, 0, 0, 0} }, /* 9 */
	{1360, 768, 30, TIMING_STANDARD_DMT, TIMING_SOURCE_DEFAULT,
			{0, 0, 0, 0, 0, 0} }, /* 10 */
	{1360, 768, 60, TIMING_STANDARD_DMT, TIMING_SOURCE_DEFAULT,
			{0, 0, 0, 0, 0, 0} }, /* 11 */
	{1366, 768, 30, TIMING_STANDARD_DMT, TIMING_SOURCE_DEFAULT,
			{0, 0, 0, 0, 0, 0} }, /* 12 */
	{1366, 768, 60, TIMING_STANDARD_DMT, TIMING_SOURCE_DEFAULT,
			{0, 0, 0, 0, 0, 0} }, /* 13 */
	{1280, 1024, 30, TIMING_STANDARD_DMT, TIMING_SOURCE_DEFAULT,
			{0, 0, 0, 0, 0, 0} }, /* 14 */
	{1280, 1024, 60, TIMING_STANDARD_DMT, TIMING_SOURCE_DEFAULT,
			{0, 0, 0, 0, 0, 0} }, /* 15 */
	{1400, 1050, 30, TIMING_STANDARD_DMT, TIMING_SOURCE_DEFAULT,
			{0, 0, 0, 0, 0, 0} }, /* 16 */
	{1400, 1050, 60, TIMING_STANDARD_DMT, TIMING_SOURCE_DEFAULT,
			{0, 0, 0, 0, 0, 0} }, /* 17 */
	{1400, 900, 30, TIMING_STANDARD_DMT, TIMING_SOURCE_DEFAULT,
			{0, 0, 0, 0, 0, 0} }, /* 18 */
	{1400, 900, 60, TIMING_STANDARD_DMT, TIMING_SOURCE_DEFAULT,
			{0, 0, 0, 0, 0, 0} }, /* 19 */
	{1600, 900, 30, TIMING_STANDARD_DMT, TIMING_SOURCE_DEFAULT,
			{0, 0, 0, 0, 0, 0} }, /* 20 */
	{1600, 900, 60, TIMING_STANDARD_DMT, TIMING_SOURCE_DEFAULT,
			{0, 0, 0, 0, 0, 0} }, /* 21 */
	{1600, 1200, 30, TIMING_STANDARD_DMT, TIMING_SOURCE_DEFAULT,
			{0, 0, 0, 0, 0, 0} }, /* 22 */
	{1600, 1200, 60, TIMING_STANDARD_DMT, TIMING_SOURCE_DEFAULT,
			{0, 0, 0, 0, 0, 0} }, /* 23 */
	{1680, 1024, 30, TIMING_STANDARD_UNDEFINED, TIMING_SOURCE_DEFAULT,
			{0, 0, 0, 0, 0, 0} }, /* 24 */
	{1680, 1024, 60, TIMING_STANDARD_UNDEFINED, TIMING_SOURCE_DEFAULT,
			{0, 0, 0, 0, 0, 0} }, /* 25 */
	{1680, 1050, 30, TIMING_STANDARD_DMT, TIMING_SOURCE_DEFAULT,
			{0, 0, 0, 0, 0, 0} }, /* 26 */
	{1680, 1050, 60, TIMING_STANDARD_DMT, TIMING_SOURCE_DEFAULT,
			{0, 0, 0, 0, 0, 0} }, /* 27 */
	{1920, 1200, 30, TIMING_STANDARD_DMT, TIMING_SOURCE_DEFAULT,
			{0, 0, 0, 0, 0, 0} }, /* 28 */
	{1920, 1200, 60, TIMING_STANDARD_DMT, TIMING_SOURCE_DEFAULT,
			{0, 0, 0, 0, 0, 0} }, /* 29 */
};

/*************************
 *** private functions ***
 *************************/

static bool remote_display_receiver_modes_construct(
		struct remote_display_receiver_modes *rdrm,
		struct remote_display_receiver_modes_init_data *rdrm_init_data)
{
	rdrm->ts = rdrm_init_data->ts;
	rdrm->supports_miracast = rdrm_init_data->supports_miracast;
	return true;
}

/**
* This function only expects at most 1 bit set
* in both the sample rate and sample depth field (if LPCM).
*/
static bool is_cea_audio_mode_supported(
	struct remote_display_receiver_modes *rdrm,
	const struct cea_audio_mode *const audio_mode)
{
	uint32_t i = 0;
	unsigned int num_of_entries =
		sizeof(default_audio_modes)/
			sizeof(default_audio_modes[0]);

	for (i = 0; i < num_of_entries; i++) {

		if (audio_mode->format_code !=
			default_audio_modes[i].format_code &&
			audio_mode->channel_count !=
			default_audio_modes[i].channel_count &&
			audio_mode->sample_rate !=
			default_audio_modes[i].sample_rate)
			continue;

		switch (audio_mode->format_code) {
		case AUDIO_FORMAT_CODE_LINEARPCM:
			if (audio_mode->sample_size ==
				default_audio_modes[i].sample_size)
				return (rdrm->rdrm_caps.audio.raw &
					(1 << i)) ? true : false;

		break;
		case AUDIO_FORMAT_CODE_AC3:
		case AUDIO_FORMAT_CODE_MPEG1:
		case AUDIO_FORMAT_CODE_MP3:
		case AUDIO_FORMAT_CODE_MPEG2:
		case AUDIO_FORMAT_CODE_AAC:
		case AUDIO_FORMAT_CODE_DTS:
		case AUDIO_FORMAT_CODE_ATRAC:
			/*Format 2 to 8*/
			if (audio_mode->max_bit_rate ==
				default_audio_modes[i].max_bit_rate)
				return (rdrm->rdrm_caps.audio.raw &
					(1 << i)) ? true : false;
		break;
		default:
			return false;
		}
	}

	return false;
}

/*
 * insert_into_timing_list
 *
 * @brief
 * Add the given mode into timing list
 *
 * @param
 * struct remote_display_receiver_modes *rdrm - [in] remote display receiver
 * struct dcs_mode_timing_list *list - [out] list to be appended at.
 * const struct mode_info *mode - [in] desired mode to be added to the list
 */
static bool insert_into_timing_list(
		struct remote_display_receiver_modes *rdrm,
		struct dcs_mode_timing_list *list,
		const struct mode_info *mode)
{
	struct mode_timing mt;
	struct mode_info mi = {0};
	bool result = false;

	dal_memset(&mt, 0, sizeof(mt));
	mi = *mode;

	/* For 30 Hz case, we want to grab timing of 60Hz, and halve the pixel
	 * clock later on */
	if (mode->field_rate == 30)
		mi.field_rate = 60;

	/* Query the timing for a given mode */
	if (dal_timing_service_get_timing_for_mode(
			rdrm->ts, &mi, &mt.crtc_timing)) {
		mt.mode_info = *mode;

		if (mode->field_rate == 30) {
			mt.crtc_timing.pix_clk_khz /= 2;
			mt.crtc_timing.vic = 0;
			mt.crtc_timing.hdmi_vic = 0;
		}

		if (dal_dcs_mode_timing_list_append(list, &mt))
			result = true;
	}

	return result;
}

/************************
 *** public functions ***
 ************************/

struct remote_display_receiver_modes*
dal_remote_display_receiver_modes_create(
		struct remote_display_receiver_modes_init_data *rdrm_init_data)
{
	struct remote_display_receiver_modes *rdrm;

	rdrm = dal_alloc(sizeof(struct remote_display_receiver_modes));

	if (!rdrm)
		return NULL;

	if (remote_display_receiver_modes_construct(rdrm, rdrm_init_data))
		return rdrm;

	dal_free(rdrm);

	return NULL;
}

void dal_remote_display_receiver_modes_destroy(
		struct remote_display_receiver_modes **rdrm)
{
	dal_free(*rdrm);
	*rdrm = NULL;
}

void dal_remote_display_receiver_set_capabilities(
		struct remote_display_receiver_modes *rdrm,
		const struct dal_remote_display_receiver_capability *rdrm_caps)
{
	rdrm->rdrm_caps.audio.raw = rdrm_caps->audio.raw;
	rdrm->rdrm_caps.vesa_mode.raw = rdrm_caps->vesa_mode.raw;
	rdrm->rdrm_caps.cea_mode.raw = rdrm_caps->cea_mode.raw;
	rdrm->rdrm_caps.hh_mode.raw = rdrm_caps->hh_mode.raw;
	rdrm->rdrm_caps.stereo_3d_mode.raw = rdrm_caps->stereo_3d_mode.raw;
	rdrm->rdrm_caps.cea_mode.raw |= 0x1; /* Mandatory mode for WFD */
}

void dal_remote_display_receiver_clear_capabilities(
		struct remote_display_receiver_modes *rdrm)
{
	dal_memset(&rdrm->rdrm_caps, 0, sizeof(rdrm->rdrm_caps));
}

bool dal_rdr_get_supported_cea_audio_mode(
	struct remote_display_receiver_modes *rdrm,
	const struct cea_audio_mode *const cea_audio_mode,
	struct cea_audio_mode *const output_mode)
{
	const uint8_t size_of_sample_rate_field = 8;
	const uint8_t size_of_sample_size_field = 8;
	uint32_t cur_sample_rate_bit = 0;
	uint32_t cur_sample_size_bit = 0;

	bool at_least_one_mode_valid = false;
	struct cea_audio_mode temp_mode = {0};

	/* Create a copy of the cea_audio_mode, but zero
	 * out the sample_rate and bitfield as we don't
	 * actually know which ones are supported.
	 */
	*output_mode = *cea_audio_mode;
	output_mode->sample_rate = 0;

    /* Create a copy if the input audio mode.
     * We will use this copy to check the audio mode
     * one sample rate/sample size combination at a time.
     */
	temp_mode = *cea_audio_mode;

	switch (cea_audio_mode->format_code) {
	case AUDIO_FORMAT_CODE_LINEARPCM:
		output_mode->sample_size = 0;

		/* Iterate through all sample rate bits.*/
		for (cur_sample_rate_bit = 0;
			cur_sample_rate_bit <
			size_of_sample_rate_field;
			cur_sample_rate_bit++) {
			/* Mask the current sample rate bit.*/
			temp_mode.sample_rate =
				cea_audio_mode->sample_rate &
				(0x1 << cur_sample_rate_bit);

			/* If the sample rate bit is set,
			 * we check if the wireless receiver supports it.
			 */
			if (!temp_mode.sample_rate)
				continue;

			/* For LPCM, though we must also
			 * check the sample size bitfield.*/
			for (cur_sample_size_bit = 0;
				cur_sample_size_bit <
				size_of_sample_size_field;
				cur_sample_size_bit++) {

				/* Mask the current sample size bit*/
				temp_mode.sample_size =
					cea_audio_mode->sample_size &
					(0x1 << cur_sample_size_bit);

				/* If the sample size bit
				 * is set, we check if
				 * the wireless receiver
				 * supports it.*/
				if (!temp_mode.sample_size)
					continue;

				/* If the sample rate/size is supported,
				 * then we add both to the resultant set.*/
				if (!is_cea_audio_mode_supported(
				rdrm,
				&temp_mode))
					continue;

				output_mode->sample_rate |=
					temp_mode.sample_rate;
				output_mode->sample_size |=
					temp_mode.sample_size;
				at_least_one_mode_valid = true;

			}
		}
		break;
	case AUDIO_FORMAT_CODE_AC3:
	case AUDIO_FORMAT_CODE_MPEG1:
	case AUDIO_FORMAT_CODE_MP3:
	case AUDIO_FORMAT_CODE_MPEG2:
	case AUDIO_FORMAT_CODE_AAC:
	case AUDIO_FORMAT_CODE_DTS:
	case AUDIO_FORMAT_CODE_ATRAC:
		output_mode->max_bit_rate = 0;
		temp_mode.max_bit_rate =
			cea_audio_mode->max_bit_rate;

		/* Iterate through all sample rate bits.*/
		for (cur_sample_rate_bit = 0; cur_sample_rate_bit <
			size_of_sample_rate_field; cur_sample_rate_bit++) {
			/* Mask the current sample rate bit.*/
			temp_mode.sample_rate =
				cea_audio_mode->sample_rate &
				(0x1 << cur_sample_rate_bit);

			/* If the sample rate bit is set,
			 * we check if the wireless receiver supports it.*/
			if (!temp_mode.sample_rate)
				continue;

			/* If this sample rate is supported,
			 * then add it to the resultant set.*/
			if (!is_cea_audio_mode_supported(
				rdrm,
				&temp_mode))
				continue;

			output_mode->sample_rate |=
				temp_mode.sample_rate;
			output_mode->max_bit_rate =
				temp_mode.max_bit_rate;
			at_least_one_mode_valid = true;
		}
		break;
	case AUDIO_FORMAT_CODE_1BITAUDIO:
	case AUDIO_FORMAT_CODE_DOLBYDIGITALPLUS:
	case AUDIO_FORMAT_CODE_DTS_HD:
	case AUDIO_FORMAT_CODE_MAT_MLP:
	case AUDIO_FORMAT_CODE_DST:
	case AUDIO_FORMAT_CODE_WMAPRO:
		output_mode->audio_codec_vendor_specific = 0;
		temp_mode.audio_codec_vendor_specific =
			cea_audio_mode->audio_codec_vendor_specific;

		/* Iterate through all sample rate bits.*/
		for (cur_sample_rate_bit = 0; cur_sample_rate_bit <
			size_of_sample_rate_field; cur_sample_rate_bit++) {
			/* Mask the current sample rate bit.*/
			temp_mode.sample_rate =
				cea_audio_mode->sample_rate &
				(0x1 << cur_sample_rate_bit);

			/* If the sample rate bit is set,
			 * we check if the wireless receiver supports it.*/
			if (!temp_mode.sample_rate)
				continue;

			/* If this sample rate is supported,
			 * then add it to the resultant set.*/
			if (!is_cea_audio_mode_supported(rdrm,
				&temp_mode))
				continue;

			output_mode->sample_rate |=
				temp_mode.sample_rate;
			output_mode->audio_codec_vendor_specific =
				temp_mode.audio_codec_vendor_specific;
			at_least_one_mode_valid = true;

		}
		break;
	default:
		break;
	}

	return at_least_one_mode_valid;
}

/*
 * dal_remote_display_receiver_get_supported_mode_timing
 *
 * @brief
 * Add CEA mode and VESA mode into supported timing list
 *
 * @param
 * struct remote_display_receiver_modes *rdrm - [in] remote dipslay receiver
 * struct dcs_mode_timing_list *list - [out] new modes are added in this list
 *
 * @return
 * true if any mode is added, false otherwise.
 */
bool dal_remote_display_receiver_get_supported_mode_timing(
		struct remote_display_receiver_modes *rdrm,
		struct dcs_mode_timing_list *list)
{
	uint32_t list_count = 0;
	uint32_t i = 0;
	bool result = false;
	struct mode_info mode = {0};

	if (list == NULL)
		return false;

	/* Add all the CEA modes */
	list_count = sizeof(rdrm_default_cea_modes) /
			sizeof(rdrm_default_cea_modes[0]);

	for (i = 0; i < list_count; ++i) {
		if (rdrm->rdrm_caps.cea_mode.raw & (1 << i)) {
			mode = rdrm_default_cea_modes[i];

			if (insert_into_timing_list(rdrm, list, &mode))
				result = true;

			/* Insert the video-optimized timing as well */
			mode.flags.VIDEO_OPTIMIZED_RATE =
				(mode.flags.VIDEO_OPTIMIZED_RATE == 1) ? 0 : 1;
			if (insert_into_timing_list(rdrm, list, &mode))
				result = true;
		}
	}

	/* Add all the VESA modes */
	list_count = sizeof(rdrm_default_vesa_modes) /
			sizeof(rdrm_default_vesa_modes[0]);

	for (i = 0; i < list_count; ++i) {
		if (rdrm->rdrm_caps.vesa_mode.raw & (1 << i)) {

			mode = rdrm_default_vesa_modes[i];

			if (mode.timing_standard == TIMING_STANDARD_UNDEFINED)
				continue;

			if (insert_into_timing_list(rdrm, list, &mode))
				result = true;
		}
	}

	/* return true if any mode was added */
	return result;
}
