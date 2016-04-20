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

#ifndef __DAL_HW_CTX_AUDIO_H__
#define __DAL_HW_CTX_AUDIO_H__

#include "include/audio_interface.h"
#include "include/link_service_types.h"

struct hw_ctx_audio;

struct azalia_reg_offsets {
	uint32_t azf0endpointx_azalia_f0_codec_endpoint_index;
	uint32_t azf0endpointx_azalia_f0_codec_endpoint_data;
};

/***** hook functions *****/

struct hw_ctx_audio_funcs {

	/* functions for hw_ctx creation */
	void (*destroy)(
		struct hw_ctx_audio **ptr);

	/***** from dal2 hwcontextaudio.hpp *****/

	void (*setup_audio_wall_dto)(
		const struct hw_ctx_audio *hw_ctx,
		enum signal_type signal,
		const struct audio_crtc_info *crtc_info,
		const struct audio_pll_info *pll_info);

	/* MM register access  read_register  write_register */

	/***** from dal2 hwcontextaudio_hal.hpp *****/

	/* setup HDMI audio */
	void (*setup_hdmi_audio)(
		const struct hw_ctx_audio *hw_ctx,
		enum engine_id engine_id,
		const struct audio_crtc_info *crtc_info);

	/* setup DP audio */
	void (*setup_dp_audio)(
		const struct hw_ctx_audio *hw_ctx,
		enum engine_id engine_id);

	/* setup VCE audio */
	void (*setup_vce_audio)(
		const struct hw_ctx_audio *hw_ctx);

	/* enable Azalia audio */
	void (*enable_azalia_audio)(
		const struct hw_ctx_audio *hw_ctx,
		enum engine_id engine_id);

	/* disable Azalia audio */
	void (*disable_azalia_audio)(
		const struct hw_ctx_audio *hw_ctx,
		enum engine_id engine_id);

	/* enable DP audio */
	void (*enable_dp_audio)(
		const struct hw_ctx_audio *hw_ctx,
		enum engine_id engine_id);

	/* disable DP audio */
	void (*disable_dp_audio)(
		const struct hw_ctx_audio *hw_ctx,
		enum engine_id engine_id);

	/* setup Azalia HW block */
	void (*setup_azalia)(
		const struct hw_ctx_audio *hw_ctx,
		enum engine_id engine_id,
		enum signal_type signal,
		const struct audio_crtc_info *crtc_info,
		const struct audio_pll_info *pll_info,
		const struct audio_info *audio_info);

	/* unmute audio */
	void (*unmute_azalia_audio)(
		const struct hw_ctx_audio *hw_ctx,
		enum engine_id engine_id);

	/* mute audio */
	void (*mute_azalia_audio)(
		const struct hw_ctx_audio *hw_ctx,
		enum engine_id engine_id);

	/* enable channel splitting mapping */
	void (*setup_channel_splitting_mapping)(
		const struct hw_ctx_audio *hw_ctx,
		enum engine_id engine_id,
		enum signal_type signal,
		const struct audio_channel_associate_info *audio_mapping,
		bool enable);

	/* get current channel spliting */
	bool (*get_channel_splitting_mapping)(
		const struct hw_ctx_audio *hw_ctx,
		enum engine_id engine_id,
		struct audio_channel_associate_info *audio_mapping);

	/* set the payload value for the unsolicited response */
	void (*set_unsolicited_response_payload)(
		const struct hw_ctx_audio *hw_ctx,
		enum audio_payload payload);

	/* initialize HW state */
	void (*hw_initialize)(
		const struct hw_ctx_audio *hw_ctx);

	/* check_audio_bandwidth */

	/* Assign GTC group and enable GTC value embedding */
	void (*enable_gtc_embedding_with_group)(
		const struct hw_ctx_audio *hw_ctx,
		uint32_t groupNum,
		uint32_t audioLatency);

	/* Disable GTC value embedding */
	void (*disable_gtc_embedding)(
		const struct hw_ctx_audio *hw_ctx);

	/* Disable Azalia Clock Gating Feature */
	void (*disable_az_clock_gating)(
		const struct hw_ctx_audio *hw_ctx);

	/* ~~~~  protected: ~~~~*/

	/* calc_max_audio_packets_per_line */
	/* speakers_to_channels */
	/* is_audio_format_supported */
	/* get_audio_clock_info */

	/* search pixel clock value for Azalia HDMI Audio */
	bool (*get_azalia_clock_info_hdmi)(
		const struct hw_ctx_audio *hw_ctx,
		uint32_t crtc_pixel_clock_in_khz,
		uint32_t actual_pixel_clock_in_khz,
		struct azalia_clock_info *azalia_clock_info);

	/* search pixel clock value for Azalia DP Audio */
	bool (*get_azalia_clock_info_dp)(
		const struct hw_ctx_audio *hw_ctx,
		uint32_t requested_pixel_clock_in_khz,
		const struct audio_pll_info *pll_info,
		struct azalia_clock_info *azalia_clock_info);

	void (*enable_afmt_clock)(
		const struct hw_ctx_audio *hw_ctx,
		enum engine_id engine_id,
		bool enable);

	/* @@@@   private:  @@@@  */

	/* check_audio_bandwidth_hdmi  */
	/* check_audio_bandwidth_dpsst */
	/* check_audio_bandwidth_dpmst */

};

struct hw_ctx_audio {
	const struct hw_ctx_audio_funcs *funcs;
	struct dc_context *ctx;

	/*audio_clock_infoTable[12];
	 *audio_clock_infoTable_36bpc[12];
	 *audio_clock_infoTable_48bpc[12];
	 *used by hw_ctx_audio.c file only. Will declare as static array
	 *azaliaclockinfoTable[12]  -- not used
	 *BusNumberMask;   BusNumberShift; DeviceNumberMask;
	 *not used by dce6 and after
	 */
};

/* --- object construct, destruct --- */

/*
 *called by derived audio object for specific ASIC. In case no derived object,
 *these two functions do not need exposed.
 */
bool dal_audio_construct_hw_ctx_audio(
	struct hw_ctx_audio *hw_ctx);

/*
 *creator of audio HW context will be implemented by specific ASIC object only.
 *Top base or interface object does not have implementation of creator.
 */

/* --- functions called by audio hw context itself --- */

/* MM register access */
/*read_register  - dal_read_reg */
/*write_register - dal_write_reg*/

/*check whether specified sample rates can fit into a given timing */
void dal_hw_ctx_audio_check_audio_bandwidth(
	const struct hw_ctx_audio *hw_ctx,
	const struct audio_crtc_info *crtc_info,
	uint32_t channel_count,
	enum signal_type signal,
	union audio_sample_rates *sample_rates);

/*For HDMI, calculate if specified sample rates can fit into a given timing */
void dal_audio_hw_ctx_check_audio_bandwidth_hdmi(
	const struct hw_ctx_audio *hw_ctx,
	const struct audio_crtc_info *crtc_info,
	uint32_t channel_count,
	union audio_sample_rates *sample_rates);

/*For DPSST, calculate if specified sample rates can fit into a given timing */
void dal_audio_hw_ctx_check_audio_bandwidth_dpsst(
	const struct hw_ctx_audio *hw_ctx,
	const struct audio_crtc_info *crtc_info,
	uint32_t channel_count,
	union audio_sample_rates *sample_rates);

/*For DPMST, calculate if specified sample rates can fit into a given timing */
void dal_audio_hw_ctx_check_audio_bandwidth_dpmst(
	const struct hw_ctx_audio *hw_ctx,
	const struct audio_crtc_info *crtc_info,
	uint32_t channel_count,
	union audio_sample_rates *sample_rates);

/* calculate max number of Audio packets per line */
uint32_t dal_audio_hw_ctx_calc_max_audio_packets_per_line(
	const struct hw_ctx_audio *hw_ctx,
	const struct audio_crtc_info *crtc_info);

/* translate speakers to channels */
union audio_cea_channels dal_audio_hw_ctx_speakers_to_channels(
	const struct hw_ctx_audio *hw_ctx,
	struct audio_speaker_flags speaker_flags);

/* check whether specified audio format supported */
bool dal_audio_hw_ctx_is_audio_format_supported(
	const struct hw_ctx_audio *hw_ctx,
	const struct audio_info *audio_info,
	enum audio_format_code audio_format_code,
	uint32_t *format_index);

/* search pixel clock value for HDMI */
bool dal_audio_hw_ctx_get_audio_clock_info(
	const struct hw_ctx_audio *hw_ctx,
	enum dc_color_depth color_depth,
	uint32_t crtc_pixel_clock_in_khz,
	uint32_t actual_pixel_clock_in_khz,
	struct audio_clock_info *audio_clock_info);

#endif  /* __DAL_HW_CTX_AUDIO_H__ */

