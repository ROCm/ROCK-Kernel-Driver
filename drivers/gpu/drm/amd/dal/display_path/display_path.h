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

#ifndef __DISPLAY_PATH_H__
#define __DISPLAY_PATH_H__

struct stream_context {
	struct active_state {
		bool LINK:1; /* if link is active, encoder always active*/
		bool AUDIO:1;
	} state;
	/* same on all streams on shared link*/
	struct encoder *encoder;
	struct link_service *link_query_interface;
	struct link_service *link_config_interface;
	/* distinct objects for each stream*/
	struct audio *audio;
	enum engine_id engine;

	enum signal_type input_query_signal;
	enum signal_type output_query_signal;
	enum signal_type input_config_signal;
	enum signal_type output_config_signal;
	/* temporary - eventually will move to link service*/
	struct goc_link_service_data link_service_data;
};

struct display_path {

	/*struct stream_context stream_contexts[MAX_NUM_OF_LINKS_PER_PATH];*/
	/*uint32_t number_of_links;*/
	struct stream_context stream_contexts[MAX_NUM_OF_LINKS_PER_PATH];
	uint32_t number_of_links;
	struct connector *connector;

	/* dcp components that attached only on acquired display path*/
	struct clock_source *clock_source;

	/* optional components that attached only on acquired display path*/
	/*struct glsync_connector *gl_sync_object;*/
	struct encoder *stereo_sync_object;
	/* always set when sync-output attached*/
	enum sync_source sync_output_source;
	/* optional complement to sync_output_source, can be null*/
	struct encoder *sync_output_object;

	enum sync_source sync_input_source;
	/* optional, alternative clk src for dpref signal*/
	struct clock_source *alt_clock_source;

	struct dcs *dcs;

	/* state*/
	struct src_tgt_state {
		/* display path enabled in hwss terminology*/
		enum display_tri_state powered_on;
		/* display path blanked, nothing to do with crtc blank*/
		enum display_tri_state tgt_blanked;
		/* display path blanked, nothing to do with crtc blank*/
		enum display_tri_state src_blanked;
	} src_tgt_state;

	bool connected; /* display connected*/
	int32_t acquired_counter; /* if zero then Path is not acquired by
					a "HW" method */
	bool valid;

	/* properties*/
	union display_path_properties properties;
	struct connector_device_tag_info device_tag;
	uint32_t display_index;
	enum clock_sharing_group clock_sharing_group;

	/* features*/
	struct fbc_info fbc_info;
	bool ss_supported;
	struct pixel_clock_safe_range pixel_clock_safe_range;
	union ddi_channel_mapping ddi_channel_mapping;
	struct drr_config drr_cfg;
	struct static_screen_events static_screen_config;

	/* Multi-Plane components */
	/* Each path will have at least one Plane. It could be driven
	 * by a Primary or an Underlay pipe. */
	struct vector *planes;
};

#endif /* __DISPLAY_PATH_H__ */
