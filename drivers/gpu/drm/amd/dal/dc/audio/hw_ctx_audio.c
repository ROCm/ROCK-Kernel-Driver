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

#include "hw_ctx_audio.h"

/* 25.2MHz/1.001*/
/* 25.2MHz/1.001*/
/* 25.2MHz*/
/* 27MHz */
/* 27MHz*1.001*/
/* 27MHz*1.001*/
/* 54MHz*/
/* 54MHz*1.001*/
/* 74.25MHz/1.001*/
/* 74.25MHz*/
/* 148.5MHz/1.001*/
/* 148.5MHz*/

static const struct audio_clock_info audio_clock_info_table[12] = {
	{2517, 4576, 28125, 7007, 31250, 6864, 28125},
	{2518, 4576, 28125, 7007, 31250, 6864, 28125},
	{2520, 4096, 25200, 6272, 28000, 6144, 25200},
	{2700, 4096, 27000, 6272, 30000, 6144, 27000},
	{2702, 4096, 27027, 6272, 30030, 6144, 27027},
	{2703, 4096, 27027, 6272, 30030, 6144, 27027},
	{5400, 4096, 54000, 6272, 60000, 6144, 54000},
	{5405, 4096, 54054, 6272, 60060, 6144, 54054},
	{7417, 11648, 210937, 17836, 234375, 11648, 140625},
	{7425, 4096, 74250, 6272, 82500, 6144, 74250},
	{14835, 11648, 421875, 8918, 234375, 5824, 140625},
	{14850, 4096, 148500, 6272, 165000, 6144, 148500}
};

static const struct audio_clock_info audio_clock_info_table_36bpc[12] = {
	{2517, 9152, 84375, 7007, 48875, 9152, 56250},
	{2518, 9152, 84375, 7007, 48875, 9152, 56250},
	{2520, 4096, 37800, 6272, 42000, 6144, 37800},
	{2700, 4096, 40500, 6272, 45000, 6144, 40500},
	{2702, 8192, 81081, 6272, 45045, 8192, 54054},
	{2703, 8192, 81081, 6272, 45045, 8192, 54054},
	{5400, 4096, 81000, 6272, 90000, 6144, 81000},
	{5405, 4096, 81081, 6272, 90090, 6144, 81081},
	{7417, 11648, 316406, 17836, 351562, 11648, 210937},
	{7425, 4096, 111375, 6272, 123750, 6144, 111375},
	{14835, 11648, 632812, 17836, 703125, 11648, 421875},
	{14850, 4096, 222750, 6272, 247500, 6144, 222750}
};

static const struct audio_clock_info audio_clock_info_table_48bpc[12] = {
	{2517, 4576, 56250, 7007, 62500, 6864, 56250},
	{2518, 4576, 56250, 7007, 62500, 6864, 56250},
	{2520, 4096, 50400, 6272, 56000, 6144, 50400},
	{2700, 4096, 54000, 6272, 60000, 6144, 54000},
	{2702, 4096, 54054, 6267, 60060, 8192, 54054},
	{2703, 4096, 54054, 6272, 60060, 8192, 54054},
	{5400, 4096, 108000, 6272, 120000, 6144, 108000},
	{5405, 4096, 108108, 6272, 120120, 6144, 108108},
	{7417, 11648, 421875, 17836, 468750, 11648, 281250},
	{7425, 4096, 148500, 6272, 165000, 6144, 148500},
	{14835, 11648, 843750, 8918, 468750, 11648, 281250},
	{14850, 4096, 297000, 6272, 330000, 6144, 297000}
};

/***** static function *****/


/*****SCOPE : within audio hw context dal-audio-hw-ctx *****/

/* check whether specified sample rates can fit into a given timing */
void dal_hw_ctx_audio_check_audio_bandwidth(
	const struct hw_ctx_audio *hw_ctx,
	const struct audio_crtc_info *crtc_info,
	uint32_t channel_count,
	enum signal_type signal,
	union audio_sample_rates *sample_rates)
{
	switch (signal) {
	case SIGNAL_TYPE_HDMI_TYPE_A:
		dal_audio_hw_ctx_check_audio_bandwidth_hdmi(
			hw_ctx, crtc_info, channel_count, sample_rates);
		break;
	case SIGNAL_TYPE_EDP:
	case SIGNAL_TYPE_DISPLAY_PORT:
		dal_audio_hw_ctx_check_audio_bandwidth_dpsst(
			hw_ctx, crtc_info, channel_count, sample_rates);
		break;
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
		dal_audio_hw_ctx_check_audio_bandwidth_dpmst(
			hw_ctx,  crtc_info, channel_count, sample_rates);
		break;
	default:
		break;
	}
}

/*For HDMI, calculate if specified sample rates can fit into a given timing */
void dal_audio_hw_ctx_check_audio_bandwidth_hdmi(
	const struct hw_ctx_audio *hw_ctx,
	const struct audio_crtc_info *crtc_info,
	uint32_t channel_count,
	union audio_sample_rates *sample_rates)
{
	uint32_t samples;
	uint32_t  h_blank;
	bool limit_freq_to_48_khz = false;
	bool limit_freq_to_88_2_khz = false;
	bool limit_freq_to_96_khz = false;
	bool limit_freq_to_174_4_khz = false;

	/* For two channels supported return whatever sink support,unmodified*/
	if (channel_count > 2) {

		/* Based on HDMI spec 1.3 Table 7.5 */
		if ((crtc_info->requested_pixel_clock <= 27000) &&
		(crtc_info->v_active <= 576) &&
		!(crtc_info->interlaced) &&
		!(crtc_info->pixel_repetition == 2 ||
		crtc_info->pixel_repetition == 4)) {
			limit_freq_to_48_khz = true;

		} else if ((crtc_info->requested_pixel_clock <= 27000) &&
				(crtc_info->v_active <= 576) &&
				(crtc_info->interlaced) &&
				(crtc_info->pixel_repetition == 2)) {
			limit_freq_to_88_2_khz = true;

		} else if ((crtc_info->requested_pixel_clock <= 54000) &&
				(crtc_info->v_active <= 576) &&
				!(crtc_info->interlaced)) {
			limit_freq_to_174_4_khz = true;
		}
	}

	/* Also do some calculation for the available Audio Bandwidth for the
	 * 8 ch (i.e. for the Layout 1 => ch > 2)
	 */
	h_blank = crtc_info->h_total - crtc_info->h_active;

	if (crtc_info->pixel_repetition)
		h_blank *= crtc_info->pixel_repetition;

	/*based on HDMI spec 1.3 Table 7.5 */
	h_blank -= 58;
	/*for Control Period */
	h_blank -= 16;

	samples = h_blank * 10;
	/* Number of Audio Packets (multiplied by 10) per Line (for 8 ch number
	 * of Audio samples per line multiplied by 10 - Layout 1)
	 */
	 samples /= 32;
	 samples *= crtc_info->v_active;
	 /*Number of samples multiplied by 10, per second */
	 samples *= crtc_info->refresh_rate;
	 /*Number of Audio samples per second */
	 samples /= 10;

	 /* @todo do it after deep color is implemented
	  * 8xx - deep color bandwidth scaling
	  * Extra bandwidth is avaliable in deep color b/c link runs faster than
	  * pixel rate. This has the effect of allowing more tmds characters to
	  * be transmitted during blank
	  */

	switch (crtc_info->color_depth) {
	case COLOR_DEPTH_888:
		samples *= 4;
		break;
	case COLOR_DEPTH_101010:
		samples *= 5;
		break;
	case COLOR_DEPTH_121212:
		samples *= 6;
		break;
	default:
		samples *= 4;
		break;
	}

	samples /= 4;

	/*check limitation*/
	if (samples < 88200)
		limit_freq_to_48_khz = true;
	else if (samples < 96000)
		limit_freq_to_88_2_khz = true;
	else if (samples < 176400)
		limit_freq_to_96_khz = true;
	else if (samples < 192000)
		limit_freq_to_174_4_khz = true;

	if (sample_rates != NULL) {
		/* limit frequencies */
		if (limit_freq_to_174_4_khz)
			sample_rates->rate.RATE_192 = 0;

		if (limit_freq_to_96_khz) {
			sample_rates->rate.RATE_192 = 0;
			sample_rates->rate.RATE_176_4 = 0;
		}
		if (limit_freq_to_88_2_khz) {
			sample_rates->rate.RATE_192 = 0;
			sample_rates->rate.RATE_176_4 = 0;
			sample_rates->rate.RATE_96 = 0;
		}
		if (limit_freq_to_48_khz) {
			sample_rates->rate.RATE_192 = 0;
			sample_rates->rate.RATE_176_4 = 0;
			sample_rates->rate.RATE_96 = 0;
			sample_rates->rate.RATE_88_2 = 0;
		}
	}
}

/*For DP SST, calculate if specified sample rates can fit into a given timing */
void dal_audio_hw_ctx_check_audio_bandwidth_dpsst(
	const struct hw_ctx_audio *hw_ctx,
	const struct audio_crtc_info *crtc_info,
	uint32_t channel_count,
	union audio_sample_rates *sample_rates)
{
	/* do nothing */
}

/*For DP MST, calculate if specified sample rates can fit into a given timing */
void dal_audio_hw_ctx_check_audio_bandwidth_dpmst(
	const struct hw_ctx_audio *hw_ctx,
	const struct audio_crtc_info *crtc_info,
	uint32_t channel_count,
	union audio_sample_rates *sample_rates)
{
	/* do nothing  */
}

/* calculate max number of Audio packets per line */
uint32_t dal_audio_hw_ctx_calc_max_audio_packets_per_line(
	const struct hw_ctx_audio *hw_ctx,
	const struct audio_crtc_info *crtc_info)
{
	uint32_t max_packets_per_line;

	max_packets_per_line =
		crtc_info->h_total - crtc_info->h_active;

	if (crtc_info->pixel_repetition)
		max_packets_per_line *= crtc_info->pixel_repetition;

	/* for other hdmi features */
	max_packets_per_line -= 58;
	/* for Control Period */
	max_packets_per_line -= 16;
	/* Number of Audio Packets per Line */
	max_packets_per_line /= 32;

	return max_packets_per_line;
}

/**
* speakersToChannels
*
* @brief
*  translate speakers to channels
*
*  FL  - Front Left
*  FR  - Front Right
*  RL  - Rear Left
*  RR  - Rear Right
*  RC  - Rear Center
*  FC  - Front Center
*  FLC - Front Left Center
*  FRC - Front Right Center
*  RLC - Rear Left Center
*  RRC - Rear Right Center
*  LFE - Low Freq Effect
*
*               FC
*          FLC      FRC
*    FL                    FR
*
*                    LFE
*              ()
*
*
*    RL                    RR
*          RLC      RRC
*               RC
*
*             ch  8   7   6   5   4   3   2   1
* 0b00000011      -   -   -   -   -   -   FR  FL
* 0b00000111      -   -   -   -   -   LFE FR  FL
* 0b00001011      -   -   -   -   FC  -   FR  FL
* 0b00001111      -   -   -   -   FC  LFE FR  FL
* 0b00010011      -   -   -   RC  -   -   FR  FL
* 0b00010111      -   -   -   RC  -   LFE FR  FL
* 0b00011011      -   -   -   RC  FC  -   FR  FL
* 0b00011111      -   -   -   RC  FC  LFE FR  FL
* 0b00110011      -   -   RR  RL  -   -   FR  FL
* 0b00110111      -   -   RR  RL  -   LFE FR  FL
* 0b00111011      -   -   RR  RL  FC  -   FR  FL
* 0b00111111      -   -   RR  RL  FC  LFE FR  FL
* 0b01110011      -   RC  RR  RL  -   -   FR  FL
* 0b01110111      -   RC  RR  RL  -   LFE FR  FL
* 0b01111011      -   RC  RR  RL  FC  -   FR  FL
* 0b01111111      -   RC  RR  RL  FC  LFE FR  FL
* 0b11110011      RRC RLC RR  RL  -   -   FR  FL
* 0b11110111      RRC RLC RR  RL  -   LFE FR  FL
* 0b11111011      RRC RLC RR  RL  FC  -   FR  FL
* 0b11111111      RRC RLC RR  RL  FC  LFE FR  FL
* 0b11000011      FRC FLC -   -   -   -   FR  FL
* 0b11000111      FRC FLC -   -   -   LFE FR  FL
* 0b11001011      FRC FLC -   -   FC  -   FR  FL
* 0b11001111      FRC FLC -   -   FC  LFE FR  FL
* 0b11010011      FRC FLC -   RC  -   -   FR  FL
* 0b11010111      FRC FLC -   RC  -   LFE FR  FL
* 0b11011011      FRC FLC -   RC  FC  -   FR  FL
* 0b11011111      FRC FLC -   RC  FC  LFE FR  FL
* 0b11110011      FRC FLC RR  RL  -   -   FR  FL
* 0b11110111      FRC FLC RR  RL  -   LFE FR  FL
* 0b11111011      FRC FLC RR  RL  FC  -   FR  FL
* 0b11111111      FRC FLC RR  RL  FC  LFE FR  FL
*
* @param
*  speakers - speaker information as it comes from CEA audio block
*/
/* translate speakers to channels */
union audio_cea_channels dal_audio_hw_ctx_speakers_to_channels(
	const struct hw_ctx_audio *hw_ctx,
	struct audio_speaker_flags speaker_flags)
{
	union audio_cea_channels cea_channels = {0};

	/* these are one to one */
	cea_channels.channels.FL = speaker_flags.FL_FR;
	cea_channels.channels.FR = speaker_flags.FL_FR;
	cea_channels.channels.LFE = speaker_flags.LFE;
	cea_channels.channels.FC = speaker_flags.FC;

	/* if Rear Left and Right exist move RC speaker to channel 7
	 * otherwise to channel 5
	 */
	if (speaker_flags.RL_RR) {
		cea_channels.channels.RL_RC = speaker_flags.RL_RR;
		cea_channels.channels.RR = speaker_flags.RL_RR;
		cea_channels.channels.RC_RLC_FLC = speaker_flags.RC;
	} else {
		cea_channels.channels.RL_RC = speaker_flags.RC;
	}

	/* FRONT Left Right Center and REAR Left Right Center are exclusive */
	if (speaker_flags.FLC_FRC) {
		cea_channels.channels.RC_RLC_FLC = speaker_flags.FLC_FRC;
		cea_channels.channels.RRC_FRC = speaker_flags.FLC_FRC;
	} else {
		cea_channels.channels.RC_RLC_FLC = speaker_flags.RLC_RRC;
		cea_channels.channels.RRC_FRC = speaker_flags.RLC_RRC;
	}

	return cea_channels;
}

/* check whether specified audio format supported */
bool dal_audio_hw_ctx_is_audio_format_supported(
	const struct hw_ctx_audio *hw_ctx,
	const struct audio_info *audio_info,
	enum audio_format_code audio_format_code,
	uint32_t *format_index)
{
	uint32_t index;
	uint32_t max_channe_index = 0;
	bool found = false;

	if (audio_info == NULL)
		return found;

	/* pass through whole array */
	for (index = 0; index < audio_info->mode_count; index++) {
		if (audio_info->modes[index].format_code == audio_format_code) {
			if (found) {
				/* format has multiply entries, choose one with
				 *  highst number of channels */
				if (audio_info->modes[index].channel_count >
		audio_info->modes[max_channe_index].channel_count) {
					max_channe_index = index;
				}
			} else {
				/* format found, save it's index */
				found = true;
				max_channe_index = index;
			}
		}
	}

	/* return index */
	if (found && format_index != NULL)
		*format_index = max_channe_index;

	return found;
}

/* search pixel clock value for HDMI */
bool dal_audio_hw_ctx_get_audio_clock_info(
	const struct hw_ctx_audio *hw_ctx,
	enum dc_color_depth color_depth,
	uint32_t crtc_pixel_clock_in_khz,
	uint32_t actual_pixel_clock_in_khz,
	struct audio_clock_info *audio_clock_info)
{
	const struct audio_clock_info *clock_info;
	uint32_t index;
	uint32_t crtc_pixel_clock_in_10khz = crtc_pixel_clock_in_khz / 10;
	uint32_t audio_array_size;

	if (audio_clock_info == NULL)
		return false; /* should not happen */

	switch (color_depth) {
	case COLOR_DEPTH_161616:
		clock_info = audio_clock_info_table_48bpc;
		audio_array_size = ARRAY_SIZE(
				audio_clock_info_table_48bpc);
		break;
	case COLOR_DEPTH_121212:
		clock_info = audio_clock_info_table_36bpc;
		audio_array_size = ARRAY_SIZE(
				audio_clock_info_table_36bpc);
		break;
	default:
		clock_info = audio_clock_info_table;
		audio_array_size = ARRAY_SIZE(
				audio_clock_info_table);
		break;
	}

	if (clock_info != NULL) {
		/* search for exact pixel clock in table */
		for (index = 0; index < audio_array_size; index++) {
			if (clock_info[index].pixel_clock_in_10khz >
				crtc_pixel_clock_in_10khz)
				break;  /* not match */
			else if (clock_info[index].pixel_clock_in_10khz ==
					crtc_pixel_clock_in_10khz) {
				/* match found */
				if (audio_clock_info != NULL) {
					*audio_clock_info = clock_info[index];
					return true;
				}
			}
		}
	}

	/* not found */
	if (actual_pixel_clock_in_khz == 0)
		actual_pixel_clock_in_khz = crtc_pixel_clock_in_khz;

	/* See HDMI spec  the table entry under
	 *  pixel clock of "Other". */
	audio_clock_info->pixel_clock_in_10khz =
			actual_pixel_clock_in_khz / 10;
	audio_clock_info->cts_32khz = actual_pixel_clock_in_khz;
	audio_clock_info->cts_44khz = actual_pixel_clock_in_khz;
	audio_clock_info->cts_48khz = actual_pixel_clock_in_khz;

	audio_clock_info->n_32khz = 4096;
	audio_clock_info->n_44khz = 6272;
	audio_clock_info->n_48khz = 6144;

	return true;
}

