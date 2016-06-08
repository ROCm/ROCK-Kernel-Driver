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

#define MOD_FREESYNC_MAX_CONCURRENT_SINKS  32

/* Refresh rate ramp at a fixed rate of 65 Hz/second */
#define STATIC_SCREEN_RAMP_DELTA_REFRESH_RATE_PER_FRAME ((1000 / 60) * 65)

struct sink_caps {
	const struct dc_sink *sink;
	struct mod_freesync_caps caps;
};

struct gradual_static_ramp {
	bool ramp_is_active;
	bool ramp_direction_is_up;
	unsigned int ramp_current_frame_duration_in_ns;
};

struct freesync_state {
	bool fullscreen;
	bool static_screen;
	bool video;

	unsigned int duration_in_ns;
	struct gradual_static_ramp static_ramp;
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

/* Given a specific dc_sink* this function finds its equivalent
 * on the dc_sink array and returns the corresponding index
 */
static unsigned int sink_index_from_sink(struct core_freesync *core_freesync,
		const struct dc_sink *sink)
{
	unsigned int index = 0;

	for (index = 0; index < core_freesync->num_sinks; index++)
		if (core_freesync->caps[index].sink == sink)
			return index;

	/* Could not find sink requested */
	ASSERT(false);
	return index;
}

static void calc_vmin_vmax (const struct dc_stream *stream,
		struct mod_freesync_caps *caps, int *vmin, int *vmax)
{
	/* TODO: This calculation is probably wrong... */

	unsigned int min_frame_duration_in_ns = 0, max_frame_duration_in_ns = 0;

	min_frame_duration_in_ns = ((unsigned int) (div64_u64(
					(1000000000ULL * 1000000),
					caps->max_refresh_in_micro_hz)));
	max_frame_duration_in_ns = ((unsigned int) (div64_u64(
					(1000000000ULL * 1000000),
					caps->min_refresh_in_micro_hz)));

	*vmax = div64_u64(div64_u64(((unsigned long long)(
			max_frame_duration_in_ns) * stream->timing.pix_clk_khz),
			stream->timing.h_total), 1000000);
	*vmin = div64_u64(div64_u64(((unsigned long long)(
			min_frame_duration_in_ns) * stream->timing.pix_clk_khz),
			stream->timing.h_total), 1000000);

	/* Field rate might not be the maximum rate
	 * in which case we should adjust our vmin
	 */
	if (*vmin < stream->timing.v_total)
		*vmin = stream->timing.v_total;
}

static void calc_v_total_from_duration(const struct dc_stream *stream,
		unsigned int duration_in_ns, int *v_total_nominal)
{
	*v_total_nominal = div64_u64(div64_u64(((unsigned long long)(
				duration_in_ns) * stream->timing.pix_clk_khz),
				stream->timing.h_total), 1000000);
}

static void calc_v_total_for_static_ramp(struct core_freesync *core_freesync,
		const struct dc_stream *stream,
		unsigned int sink_index, int *v_total)
{
	unsigned int frame_duration = 0;

	struct gradual_static_ramp *static_ramp_variables =
				&core_freesync->state[sink_index].static_ramp;

	/* Calc ratio between new and current frame duration with 3 digit */
	unsigned int frame_duration_ratio = div64_u64(1000000,
		(1000 +  div64_u64(((unsigned long long)(
		STATIC_SCREEN_RAMP_DELTA_REFRESH_RATE_PER_FRAME) *
		static_ramp_variables->ramp_current_frame_duration_in_ns),
		1000000000)));

	/* Calculate delta between new and current frame duration in ns */
	unsigned int frame_duration_delta = div64_u64(((unsigned long long)(
		static_ramp_variables->ramp_current_frame_duration_in_ns) *
		(1000 - frame_duration_ratio)), 1000);

	/* Adjust frame duration delta based on ratio between current and
	 * standard frame duration (frame duration at 60 Hz refresh rate).
	 */
	unsigned int ramp_rate_interpolated = div64_u64(((unsigned long long)(
		frame_duration_delta) * static_ramp_variables->
		ramp_current_frame_duration_in_ns), 16666666);

	/* Going to a higher refresh rate (lower frame duration) */
	if (static_ramp_variables->ramp_direction_is_up) {
		/* reduce frame duration */
		static_ramp_variables->ramp_current_frame_duration_in_ns -=
			ramp_rate_interpolated;

		/* min frame duration */
		frame_duration = ((unsigned int) (div64_u64(
			(1000000000ULL * 1000000),
			core_freesync->caps[sink_index].
			caps.max_refresh_in_micro_hz)));

		/* adjust for frame duration below min */
		if (static_ramp_variables->ramp_current_frame_duration_in_ns <=
			frame_duration) {

			static_ramp_variables->ramp_is_active = false;
			static_ramp_variables->
				ramp_current_frame_duration_in_ns =
				frame_duration;
		}
	/* Going to a lower refresh rate (larger frame duration) */
	} else {
		/* increase frame duration */
		static_ramp_variables->ramp_current_frame_duration_in_ns +=
			ramp_rate_interpolated;

		/* max frame duration */
		frame_duration = ((unsigned int) (div64_u64(
			(1000000000ULL * 1000000),
			core_freesync->caps[sink_index].
			caps.min_refresh_in_micro_hz)));

		/* adjust for frame duration above max */
		if (static_ramp_variables->ramp_current_frame_duration_in_ns >=
			frame_duration) {

			static_ramp_variables->ramp_is_active = false;
			static_ramp_variables->
				ramp_current_frame_duration_in_ns =
				frame_duration;
		}
	}

	calc_v_total_from_duration(stream, static_ramp_variables->
		ramp_current_frame_duration_in_ns, v_total);
}

void mod_freesync_handle_v_update(struct mod_freesync *mod_freesync,
		const struct dc_stream **streams, int num_streams)
{
	/* Currently we are only doing static screen ramping on v_update */

	struct core_freesync *core_freesync =
			MOD_FREESYNC_TO_CORE(mod_freesync);

	unsigned int sink_index = sink_index_from_sink(core_freesync,
				streams[0]->sink);

	unsigned int v_total = 0;

	/* If in fullscreen freesync mode or in video, do not program
	 * static screen ramp values
	 */
	if (core_freesync->state[sink_index].fullscreen == true ||
		core_freesync->state[sink_index].video == true) {

		core_freesync->state[sink_index].static_ramp.
			ramp_is_active = false;
		return;
	}


	/* Execute if ramp is active and user enabled freesync static screen*/
	if (core_freesync->state[sink_index].static_ramp.ramp_is_active &&
			core_freesync->user_enable->enable_for_static)	{

		calc_v_total_for_static_ramp(core_freesync, streams[0],
				sink_index, &v_total);

		/* Program static screen ramp values */
		core_freesync->dc->stream_funcs.dc_stream_adjust_vmin_vmax(
					core_freesync->dc, streams,
					num_streams, v_total,
					v_total);
	}
}
/*
 * Sets freesync mode on a stream depending on current freesync state.
 */
static bool set_freesync_on_streams(struct core_freesync *core_freesync,
		const struct dc_stream **streams, int num_streams)
{
	int v_total_nominal = 0, v_total_min = 0, v_total_max = 0;
	int i = 0;
	unsigned int stream_idx, sink_index = 0;

	if (num_streams == 0 || streams == NULL || num_streams > 1)
		return false;

	for (stream_idx = 0; stream_idx < num_streams; stream_idx++) {

		sink_index = sink_index_from_sink(core_freesync,
				streams[stream_idx]->sink);

		if (core_freesync->caps[sink_index].caps.supported) {

			/* Fullscreen has the topmost priority. If the
			 * fullscreen bit is set, we are in a fullscreen
			 * application where it should not matter if it is
			 * static screen. We should not check the static_screen
			 * bit - we want to enable freesync regardless.
			 */
			if (core_freesync->user_enable[sink_index].
				enable_for_gaming && core_freesync->
				state[sink_index].fullscreen == true) {
				/* Enable freesync */

				calc_vmin_vmax(streams[stream_idx],
						&core_freesync->caps[i].caps,
						&v_total_min, &v_total_max);

				core_freesync->dc->stream_funcs.
				dc_stream_adjust_vmin_vmax(
						core_freesync->dc, streams,
						num_streams, v_total_min,
						v_total_max);

				return true;

			} else if (core_freesync->user_enable[sink_index].
				enable_for_video && core_freesync->
				state[sink_index].video == true) {
				/* Enable 48Hz feature */

				calc_v_total_from_duration(streams[stream_idx],
					core_freesync->state[sink_index].
					duration_in_ns, &v_total_nominal);

				/* Program only if v_total_nominal is in range*/
				if (v_total_nominal >=
					streams[stream_idx]->timing.v_total)

					core_freesync->dc->stream_funcs.
					dc_stream_adjust_vmin_vmax(
						core_freesync->dc, streams,
						num_streams, v_total_nominal,
						v_total_nominal);

				return true;

			} else {
				/* Disable freesync */
				v_total_nominal = streams[stream_idx]->
					timing.v_total;

				core_freesync->dc->stream_funcs.
						dc_stream_adjust_vmin_vmax(
						core_freesync->dc, streams,
						num_streams, v_total_nominal,
						v_total_nominal);

				return true;
			}
		}

	}

	return false;
}

static void set_static_ramp_variables(struct core_freesync *core_freesync,
		unsigned int sink_index, bool enable_static_screen)
{
	unsigned int frame_duration = 0;

	struct gradual_static_ramp *static_ramp_variables =
			&core_freesync->state[sink_index].static_ramp;

	/* If ramp is not active, set initial frame duration depending on
	 * whether we are enabling/disabling static screen mode. If the ramp is
	 * already active, ramp should continue in the opposite direction
	 * starting with the current frame duration
	 */
	if (!static_ramp_variables->ramp_is_active) {

		static_ramp_variables->ramp_is_active = true;

		if (enable_static_screen == true) {
			/* Going to lower refresh rate, so start from max
			 * refresh rate (min frame duration)
			 */
			frame_duration = ((unsigned int) (div64_u64(
				(1000000000ULL * 1000000),
				core_freesync->caps[sink_index].
				caps.max_refresh_in_micro_hz)));
		} else {
			/* Going to higher refresh rate, so start from min
			 * refresh rate (max frame duration)
			 */
			frame_duration = ((unsigned int) (div64_u64(
				(1000000000ULL * 1000000),
				core_freesync->caps[sink_index].
				caps.min_refresh_in_micro_hz)));
		}

		static_ramp_variables->
			ramp_current_frame_duration_in_ns = frame_duration;
	}

	/* If we are ENABLING static screen, refresh rate should go DOWN.
	 * If we are DISABLING static screen, refresh rate should go UP.
	 */
	static_ramp_variables->ramp_direction_is_up = !enable_static_screen;
}

void mod_freesync_update_state(struct mod_freesync *mod_freesync,
		const struct dc_stream **streams, int num_streams,
		struct mod_freesync_params *freesync_params)
{
	struct core_freesync *core_freesync =
			MOD_FREESYNC_TO_CORE(mod_freesync);
	bool freesync_program_required = false;
	unsigned int stream_index;

	for(stream_index = 0; stream_index < num_streams; stream_index++) {

		unsigned int sink_index = sink_index_from_sink(core_freesync,
				streams[stream_index]->sink);

		switch (freesync_params->state){
		case FREESYNC_STATE_FULLSCREEN:
			core_freesync->state[sink_index].fullscreen =
					freesync_params->enable;
			freesync_program_required = true;
			break;
		case FREESYNC_STATE_STATIC_SCREEN:
			/* Change core variables only if there is a change*/
			if (core_freesync->state[sink_index].static_screen !=
				freesync_params->enable) {

				/* Change the state flag */
				core_freesync->state[sink_index].static_screen =
					freesync_params->enable;

				/* Change static screen ramp variables */
				set_static_ramp_variables(core_freesync,
						sink_index,
						freesync_params->enable);
			}
			/* We program the ramp starting next VUpdate */
			break;
		case FREESYNC_STATE_VIDEO:
			/* Change core variables only if there is a change*/
			if(freesync_params->duration_in_ns != core_freesync->
					state[sink_index].duration_in_ns) {

				core_freesync->state[sink_index].video =
						freesync_params->enable;
				core_freesync->
					state[sink_index].duration_in_ns =
					freesync_params->duration_in_ns;

				freesync_program_required = true;
			}
			break;
		}
	}

	if (freesync_program_required)
		/* Program freesync according to current state*/
		set_freesync_on_streams(core_freesync, streams, num_streams);
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

void mod_freesync_reapply_current_state(struct mod_freesync *mod_freesync,
		const struct dc_stream **streams, int num_streams)
{
	struct core_freesync *core_freesync =
			MOD_FREESYNC_TO_CORE(mod_freesync);

	/* Program freesync according to current state*/
	set_freesync_on_streams(core_freesync, streams, num_streams);
}
