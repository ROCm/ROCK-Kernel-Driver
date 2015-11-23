/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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

#ifndef _CORE_TYPES_H_
#define _CORE_TYPES_H_

#include "dc.h"
#include "bandwidth_calcs.h"
#include "ddc_service_types.h"

struct core_stream;
/********* core_target *************/

#define CONST_DC_TARGET_TO_CORE(dc_target) \
	container_of(dc_target, const struct core_target, public)
#define DC_TARGET_TO_CORE(dc_target) \
	container_of(dc_target, struct core_target, public)

#define MAX_PIPES 6
#define MAX_STREAMS 6
#define MAX_CLOCK_SOURCES 4

struct core_target {
	struct dc_target public;
	struct dc_target_status status;

	struct core_stream *streams[MAX_STREAMS];
	uint8_t stream_count;
	struct dc_context *ctx;
};

/********* core_surface **********/
#define DC_SURFACE_TO_CORE(dc_surface) \
	container_of(dc_surface, struct core_surface, public)

struct core_surface {
	struct dc_surface public;
	struct dc_surface_status status;
	struct dc_context *ctx;
};

void enable_surface_flip_reporting(struct dc_surface *dc_surface,
		uint32_t controller_id);

/********* core_stream ************/
#include "grph_object_id.h"
#include "encoder_interface.h"
#include "clock_source_interface.h"
#include "audio_interface.h"

#define DC_STREAM_TO_CORE(dc_stream) container_of( \
	dc_stream, struct core_stream, public)

#define PIXEL_CLOCK	27030

struct core_stream {
	struct dc_stream public;

	/* field internal to DC */
	const struct core_sink *sink;

	struct clock_source *clock_source;

	struct mem_input *mi;
	struct input_pixel_processor *ipp;
	struct transform *xfm;
	struct output_pixel_processor *opp;
	struct timing_generator *tg;
	struct stream_encoder *stream_enc;
	struct display_clock *dis_clk;

	struct overscan_info overscan;
	struct scaling_ratios ratios;
	struct rect viewport;
	struct scaling_taps taps;
	enum pixel_format format;

	uint8_t controller_idx;

	struct audio *audio;

	enum signal_type signal;

	/* TODO: move these members into appropriate places (work in progress)*/
	/* timing validation (HDMI only) */
	uint32_t max_tmds_clk_from_edid_in_mhz;
	/* maximum supported deep color depth for HDMI */
	enum dc_color_depth max_hdmi_deep_color;
	/* maximum supported pixel clock for HDMI */
	uint32_t max_hdmi_pixel_clock;
	/* end of TODO */

	/*TODO: AUTO merge if possible*/
	struct pixel_clk_params pix_clk_params;
	struct pll_settings pll_settings;

	/*fmt*/
	/*TODO: AUTO new codepath in apply_context to hw to
	 * generate these bw unrelated/no fail params*/
	struct bit_depth_reduction_params fmt_bit_depth;
	struct clamping_and_pixel_encoding_params clamping;
	struct hw_info_frame info_frame;
	struct encoder_info_frame encoder_info_frame;

	struct audio_output audio_output;
	struct dc_context *ctx;
};


/************ core_sink *****************/

#define DC_SINK_TO_CORE(dc_sink) \
	container_of(dc_sink, struct core_sink, public)

struct core_sink {
	/** The public, read-only (for DM) area of sink. **/
	struct dc_sink public;
	/** End-of-public area. **/

	/** The 'protected' area - read/write access, for use only inside DC **/
	/* not used for now */
	struct core_link *link;
	struct dc_context *ctx;
	uint32_t dongle_max_pix_clk;
	bool converter_disable_audio;
};

/************ link *****************/
#define DC_LINK_TO_CORE(dc_link) container_of(dc_link, struct core_link, public)

struct link_init_data {
	const struct dc *dc;
	struct dc_context *ctx; /* TODO: remove 'dal' when DC is complete. */
	uint32_t connector_index; /* this will be mapped to the HPD pins */
	uint32_t link_index; /* this is mapped to DAL display_index
				TODO: remove it when DC is complete. */
	struct adapter_service *adapter_srv;
};

struct link_caps {
	/* support for Spread Spectrum(SS) */
	bool ss_supported;
	/* DP link settings (laneCount, linkRate, Spread) */
	uint32_t lane_count;
	uint32_t rate;
	uint32_t spread;
	enum dpcd_revision dpcd_revision;
};

struct dpcd_caps {
	union dpcd_rev dpcd_rev;
	union max_lane_count max_ln_count;

	/* dongle type (DP converter, CV smart dongle) */
	enum display_dongle_type dongle_type;
	/* Dongle's downstream count. */
	union sink_count sink_count;
	/* If dongle_type == DISPLAY_DONGLE_DP_HDMI_CONVERTER,
	indicates 'Frame Sequential-to-lllFrame Pack' conversion capability.*/
	bool is_dp_hdmi_s3d_converter;

	bool allow_invalid_MSA_timing_param;
	bool panel_mode_edp;
	uint32_t sink_dev_id;
	uint32_t branch_dev_id;
	int8_t branch_dev_name[6];
};

union dp_wa {
	struct {
		/* keep DP receiver powered up on DisplayOutput */
		uint32_t KEEP_RECEIVER_POWERED:1;

		/* TODO: may add other member in.*/
	} bits;
	uint32_t raw;
};

struct core_link {
	struct dc_link public;
	const struct dc *dc;

	struct dc_context *ctx; /* TODO: AUTO remove 'dal' when DC is complete*/

	uint8_t connector_index; /* this will be mapped to the HPD pins */
	uint8_t link_index; /* this is mapped to DAL display_index
				TODO: #flip remove it as soon as possible. */

	struct adapter_service *adapter_srv;
	struct connector *connector;
	struct link_encoder *link_enc;
	struct ddc_service *ddc;
	struct graphics_object_id link_id;
	/* caps is the same as reported_link_cap. link_traing use
	 * reported_link_cap. Will clean up.  TODO */
	struct link_settings reported_link_cap;
	struct link_settings verified_link_cap;
	struct link_settings max_link_setting;
	struct link_settings cur_link_settings;
	struct lane_settings ln_setting;
	struct dpcd_caps dpcd_caps;
	unsigned int dpcd_sink_count;

	enum edp_revision edp_revision;
	union dp_wa dp_wa;

	/* MST record stream using this link */
	uint8_t stream_count;
};

#define DC_LINK_TO_LINK(dc_link) container_of(dc_link, struct core_link, public)

struct core_link *link_create(const struct link_init_data *init_params);
void link_destroy(struct core_link **link);
enum dc_status core_link_enable(struct core_stream *stream);

enum dc_status core_link_disable(struct core_stream *stream);

enum dc_status dc_link_validate_mode_timing(
		const struct core_sink *sink,
		struct core_link *link,
		const struct dc_crtc_timing *timing);

void core_link_resume(struct core_link *link);

/********** DAL Core*********************/
#include "display_clock_interface.h"

struct resource_pool {
	struct scaler_filter * scaler_filter;

	struct mem_input *mis[MAX_PIPES];
	struct input_pixel_processor *ipps[MAX_PIPES];
	struct transform *transforms[MAX_PIPES];
	struct output_pixel_processor *opps[MAX_PIPES];
	struct timing_generator *timing_generators[MAX_STREAMS];
	struct stream_encoder *stream_enc[MAX_STREAMS];

	uint8_t controller_count;
	uint8_t stream_enc_count;

	union supported_stream_engines stream_engines;

	struct clock_source *clock_sources[MAX_CLOCK_SOURCES];
	uint8_t clk_src_count;

	struct audio *audios[MAX_STREAMS];
	uint8_t audio_count;

	struct display_clock *display_clock;
	struct adapter_service *adapter_srv;
	struct irq_service *irqs;
};

struct controller_ctx {
	struct core_surface *surface;
	struct core_stream *stream;
	struct flags {
		bool unchanged;
		bool timing_changed;
	} flags;
};

struct resource_context {
	struct resource_pool pool;
	struct controller_ctx controller_ctx[MAX_PIPES];
	union supported_stream_engines used_stream_engines;
	bool is_stream_enc_acquired[MAX_STREAMS];
	bool is_audio_acquired[MAX_STREAMS];
	uint8_t clock_source_ref_count[MAX_CLOCK_SOURCES];
 };

struct target_flags {
	bool unchanged;
};
struct validate_context {
	struct core_target *targets[MAX_PIPES];
	struct target_flags target_flags[MAX_PIPES];
	uint8_t target_count;

	struct resource_context res_ctx;

	struct bw_calcs_input_mode_data bw_mode_data;
	/* The output from BW and WM calculations. */
	struct bw_calcs_output bw_results;
};


#endif /* _CORE_TYPES_H_ */
