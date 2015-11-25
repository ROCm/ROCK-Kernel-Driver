/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 *  and/or sell copies of the Software, and to permit persons to whom the
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

#ifndef __DC_STREAM_ENCODER_DCE110_H__
#define __DC_STREAM_ENCODER_DCE110_H__

struct stream_enc_init_data {
	enum engine_id stream_engine_id;
	struct dc_context *ctx;
};

struct stream_encoder *dce110_stream_encoder_create(
	struct stream_enc_init_data *init);

void dce110_stream_encoder_destroy(struct stream_encoder **enc);

/***** HW programming ***********/
enum encoder_result dce110_stream_encoder_setup(
	struct stream_encoder *enc,
	struct dc_crtc_timing *crtc_timing,
	enum signal_type signal,
	bool enable_audio);

void dce110_stream_encoder_stop_hdmi_info_packets(
	struct dc_context *ctx,
	enum engine_id engine);

void dce110_stream_encoder_stop_dp_info_packets(
	struct dc_context *ctx,
	enum engine_id engine);


/* setup stream encoder in dp mode */
void dce110_stream_encoder_dp_set_stream_attribute(
	struct stream_encoder *enc,
	struct dc_crtc_timing *crtc_timing);

/* setup stream encoder in hdmi mode */
void dce110_stream_encoder_hdmi_set_stream_attribute(
	struct stream_encoder *enc,
	struct dc_crtc_timing *crtc_timing);

/* setup stream encoder in dvi mode */
void dce110_stream_encoder_dvi_set_stream_attribute(
	struct stream_encoder *enc,
	struct dc_crtc_timing *crtc_timing);

/* set throttling for DP MST */
void dce110_stream_encoder_set_mst_bandwidth(
	struct stream_encoder *enc,
	enum engine_id engine,
	struct fixed31_32 avg_time_slots_per_mtp);

void dce110_stream_encoder_set_afmt_memory_power_state(
	const struct dc_context *ctx,
	enum engine_id id,
	bool enable);

void dce110_stream_encoder_update_hdmi_info_packets(
	struct stream_encoder *enc,
	const struct encoder_info_frame *info_frame);

void dce110_stream_encoder_update_dp_info_packets(
	struct stream_encoder *enc,
	const struct encoder_info_frame *info_frame);

/* output blank/idle stream to link encoder */
void dce110_stream_encoder_dp_blank(
	struct stream_encoder *enc);

/* output video stream to link encoder */
void dce110_stream_encoder_dp_unblank(
	struct stream_encoder *enc,
	const struct encoder_unblank_param *param);

#endif /* __DC_STREAM_ENCODER_DCE110_H__ */
