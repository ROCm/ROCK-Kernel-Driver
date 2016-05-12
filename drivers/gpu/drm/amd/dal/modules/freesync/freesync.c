/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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
#include "dc.h"
#include "mod_freesync.h"

static const MOD_FREESYNC_MAX_CONCURRENT_SINKS = 32;

struct sink_caps {
	const struct dc_sink *sink;
	struct mod_freesync_caps caps;
};

struct freesync_state {
	bool fullscreen;
	bool static_screen;
};

struct core_freesync {
	struct mod_freesync public;
	struct dc *dc;
	struct sink_caps *caps;
	struct freesync_state *state;
	struct mod_freesync_user_enable *user_enable;
	int num_sinks;
};

#define MOD_FREESYNC_TO_CORE(mod_freesync)\
		container_of(mod_freesync, struct core_freesync, public)

static bool check_dc_support(const struct dc *dc)
{
	if (dc->stream_funcs.dc_stream_adjust_vmin_vmax == NULL)
		return false;

	return true;
}

struct mod_freesync *mod_freesync_create(struct dc *dc)
{
	struct core_freesync *core_freesync =
			dm_alloc(sizeof(struct core_freesync));
	int i = 0;

	if (core_freesync == NULL)
		goto fail_alloc_context;

	core_freesync->caps = dm_alloc(sizeof(struct sink_caps) *
			MOD_FREESYNC_MAX_CONCURRENT_SINKS);

	if (core_freesync->caps == NULL)
		goto fail_alloc_caps;

	core_freesync->state = dm_alloc(sizeof(struct freesync_state) *
			MOD_FREESYNC_MAX_CONCURRENT_SINKS);

	if (core_freesync->state == NULL)
		goto fail_alloc_state;

	core_freesync->user_enable =
			dm_alloc(sizeof(struct mod_freesync_user_enable) *
					MOD_FREESYNC_MAX_CONCURRENT_SINKS);

	if (core_freesync->user_enable == NULL)
		goto fail_alloc_user_enable;

	for (i = 0; i < MOD_FREESYNC_MAX_CONCURRENT_SINKS; i++)
		core_freesync->caps[i].sink = NULL;

	core_freesync->num_sinks = 0;

	if (dc == NULL)
		goto fail_construct;

	core_freesync->dc = dc;

	if (!check_dc_support(dc))
		goto fail_construct;

	return &core_freesync->public;

fail_construct:
	dm_free(core_freesync->user_enable);

fail_alloc_user_enable:
	dm_free(core_freesync->state);

fail_alloc_state:
	dm_free(core_freesync->caps);

fail_alloc_caps:
	dm_free(core_freesync);

fail_alloc_context:
	return NULL;
}

void mod_freesync_destroy(struct mod_freesync *mod_freesync)
{
	if (mod_freesync != NULL) {
		int i;
		struct core_freesync *core_freesync =
				MOD_FREESYNC_TO_CORE(mod_freesync);

		for (i = 0; i < core_freesync->num_sinks; i++)
			dc_sink_release(core_freesync->caps[i].sink);

		dm_free(core_freesync->user_enable);

		dm_free(core_freesync->state);

		dm_free(core_freesync->caps);

		dm_free(core_freesync);
	}
}

bool mod_freesync_add_sink(struct mod_freesync *mod_freesync,
		const struct dc_sink *sink, struct mod_freesync_caps *caps)
{
	int i = 0;
	struct core_freesync *core_freesync =
			MOD_FREESYNC_TO_CORE(mod_freesync);

	if (core_freesync->num_sinks < MOD_FREESYNC_MAX_CONCURRENT_SINKS) {
		dc_sink_retain(sink);

		core_freesync->caps[core_freesync->num_sinks].sink = sink;
		core_freesync->caps[core_freesync->num_sinks].caps = *caps;
		core_freesync->num_sinks++;

		return true;
	}

	return false;
}

bool mod_freesync_remove_sink(struct mod_freesync *mod_freesync,
		const struct dc_sink *sink)
{
	int i = 0, j = 0;
	struct core_freesync *core_freesync =
			MOD_FREESYNC_TO_CORE(mod_freesync);

	for (i = 0; i < core_freesync->num_sinks; i++) {
		if (core_freesync->caps[i].sink == sink) {
			/* To remove this sink, shift everything after down */
			for (j = i; j < core_freesync->num_sinks - 1; j++) {
				core_freesync->caps[j].sink =
						core_freesync->caps[j + 1].sink;
				core_freesync->caps[j].caps =
						core_freesync->caps[j + 1].caps;
			}

			core_freesync->num_sinks--;

			dc_sink_release(sink);

			return true;
		}
	}

	return false;
}

void mod_freesync_update_stream(struct mod_freesync *mod_freesync,
		struct dc_stream *stream)
{
	int i = 0;
	struct core_freesync *core_freesync =
			MOD_FREESYNC_TO_CORE(mod_freesync);

	for (i = 0; i < core_freesync->num_sinks; i++)
		if (core_freesync->caps[i].sink == stream->sink &&
				core_freesync->caps[i].caps.supported)
			stream->ignore_msa_timing_param = 1;
}

static void calc_vmin_vmax (const struct dc_stream *stream,
		struct mod_freesync_caps *caps, int *vmin, int *vmax)
{
	/* TODO: This calculation is probably wrong... */

	unsigned int min_frame_duration_in_ns = 0, max_frame_duration_in_ns = 0;

	min_frame_duration_in_ns = (unsigned int)
			((1000000000ULL * 1000000) / caps->maxRefreshInMicroHz);
	max_frame_duration_in_ns = (unsigned int)
			((1000000000ULL * 1000000) / caps->minRefreshInMicroHz);

	*vmax = (unsigned long long)(max_frame_duration_in_ns) *
		stream->timing.pix_clk_khz / stream->timing.h_total / 1000000;
	*vmin = (unsigned long long)(min_frame_duration_in_ns) *
		stream->timing.pix_clk_khz / stream->timing.h_total / 1000000;

	if (*vmin < stream->timing.v_total)
		*vmin = stream->timing.v_total;
}


/*
 * Sets freesync mode on a stream depending on current freesync state.
 */
static bool set_freesync_on_streams(struct core_freesync *core_freesync,
		const struct dc_stream **streams,	int num_streams)
{
	int v_total_nominal = 0, v_total_min = 0, v_total_max = 0;
	int i = 0;

	if (num_streams == 0 || streams == NULL || num_streams > 1)
		return false;

	/* TODO: Multi-stream support */

	/* Fullscreen has the topmost priority. If the fullscreen bit is
	 * set, we are in a fullscreen application where it should not
	 * matter if it is static screen. We should not check the
	 * static_screen bit - we want to enable freesync regardless.
	 */
	if (core_freesync->user_enable->enable_for_gaming == true &&
			core_freesync->state->fullscreen == true) {
		/* Enable freesync */
		for (i = 0; i < core_freesync->num_sinks; i++) {
			if (core_freesync->caps[i].sink == streams[0]->sink &&
					core_freesync->caps[i].caps.supported) {

				calc_vmin_vmax(streams[0],
						&core_freesync->caps[i].caps,
						&v_total_min, &v_total_max);

				core_freesync->dc->stream_funcs.
				dc_stream_adjust_vmin_vmax(
						core_freesync->dc, streams,
						num_streams, v_total_min,
						v_total_max);

				return true;
			}
		}
	} else if ( core_freesync->user_enable->enable_for_static == true &&
			core_freesync->state->static_screen == true)	{
		/* Enable Static Screen */
		for (i = 0; i < core_freesync->num_sinks; i++) {
			if (core_freesync->caps[i].sink == streams[0]->sink &&
					core_freesync->caps[i].caps.supported) {

				calc_vmin_vmax(streams[0],
						&core_freesync->caps[i].caps,
						&v_total_min, &v_total_max);

				core_freesync->dc->stream_funcs.
				dc_stream_adjust_vmin_vmax(
						core_freesync->dc, streams,
						num_streams, v_total_max,
						v_total_max);

				return true;
			}
		}
	} else {
		/* Disable freesync */
		v_total_nominal = streams[0]->timing.v_total;

		core_freesync->dc->stream_funcs.
		dc_stream_adjust_vmin_vmax(
				core_freesync->dc, streams, num_streams,
				v_total_nominal, v_total_nominal);

		return true;
	}

	return false;
}


/* Given a specific dc_sink* this function finds its equivalent
 * on the dc_sink array and returns the corresponding index
 */
static unsigned int sink_index_from_sink(struct core_freesync *core_freesync,
		const struct dc_sink * sink)
{
	unsigned int index;

	for(index = 0; index < core_freesync->num_sinks; index++)
		if(core_freesync->caps[index].sink == sink)
			break;

	return index;
}


void mod_freesync_update_state(struct mod_freesync *mod_freesync,
		const struct dc_stream **streams, int num_streams,
		enum mod_freesync_state freesyncState, bool enable)
{
	struct core_freesync *core_freesync =
			MOD_FREESYNC_TO_CORE(mod_freesync);

	unsigned int stream_index;

	for(stream_index = 0; stream_index < num_streams;
			stream_index++){

		unsigned int sink_index = sink_index_from_sink(core_freesync,
				streams[stream_index]->sink);

		switch (freesyncState){
		case FREESYNC_STATE_FULLSCREEN:
			core_freesync->state[sink_index].fullscreen = enable;
			break;

		case FREESYNC_STATE_STATIC_SCREEN:
			core_freesync->state[sink_index].static_screen = enable;
			break;
		}
	}

	/* Program freesync according to current state*/
	set_freesync_on_streams(core_freesync, streams, num_streams);
}

void mod_freesync_vupdate_callback(struct mod_freesync *mod_freesync,
		struct dc_stream *stream)
{

}


bool mod_freesync_get_freesync_caps(struct mod_freesync *mod_freesync,
		const struct dc_sink *sink, struct mod_freesync_caps *caps)
{
	struct core_freesync *core_freesync =
			MOD_FREESYNC_TO_CORE(mod_freesync);

	unsigned int sink_index = sink_index_from_sink(core_freesync, sink);

	*caps = core_freesync->caps[sink_index].caps;

	return true;
}

bool mod_freesync_set_user_enable(struct mod_freesync *mod_freesync,
		const struct dc_stream **streams, int num_streams,
		struct mod_freesync_user_enable *user_enable)
{
	struct core_freesync *core_freesync =
			MOD_FREESYNC_TO_CORE(mod_freesync);

	unsigned int stream_index;

	for(stream_index = 0; stream_index < num_streams;
			stream_index++){

		unsigned int sink_index = sink_index_from_sink(core_freesync,
				streams[stream_index]->sink);

		core_freesync->user_enable[sink_index] = *user_enable;
	}

	set_freesync_on_streams(core_freesync, streams, num_streams);

	return true;
}

bool mod_freesync_get_user_enable(struct mod_freesync *mod_freesync,
		const struct dc_sink *sink,
		struct mod_freesync_user_enable *user_enable)
{
	struct core_freesync *core_freesync =
			MOD_FREESYNC_TO_CORE(mod_freesync);

	unsigned int sink_index = sink_index_from_sink(core_freesync, sink);

	*user_enable = core_freesync->user_enable[sink_index];

	return true;
}
