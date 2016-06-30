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
#include "mod_color.h"
#include "core_types.h"
#include "fixed32_32.h"

#define MOD_COLOR_MAX_CONCURRENT_SINKS 32

struct sink_caps {
	const struct dc_sink *sink;
};

struct core_color {
	struct mod_color public;
	struct dc *dc;
	struct sink_caps *caps;
	int num_sinks;
	bool *user_enable_color_temperature;
	int color_temperature;
};

#define MOD_COLOR_TO_CORE(mod_color)\
		container_of(mod_color, struct core_color, public)

static bool check_dc_support(const struct dc *dc)
{
	if (dc->stream_funcs.adjust_color_temperature == NULL)
		return false;

	return true;
}

struct mod_color *mod_color_create(struct dc *dc)
{
	int i = 0;
	struct core_color *core_color =
				dm_alloc(sizeof(struct core_color));
	if (core_color == NULL)
		goto fail_alloc_context;

	core_color->caps = dm_alloc(sizeof(struct sink_caps) *
			MOD_COLOR_MAX_CONCURRENT_SINKS);

	if (core_color->caps == NULL)
		goto fail_alloc_caps;

	for (i = 0; i < MOD_COLOR_MAX_CONCURRENT_SINKS; i++)
		core_color->caps[i].sink = NULL;

	core_color->user_enable_color_temperature =
			dm_alloc(sizeof(bool) *
					MOD_COLOR_MAX_CONCURRENT_SINKS);

	if (core_color->user_enable_color_temperature == NULL)
		goto fail_alloc_user_enable;

	core_color->num_sinks = 0;
	core_color->color_temperature = 6500;

	if (dc == NULL)
		goto fail_construct;

	core_color->dc = dc;

	if (!check_dc_support(dc))
		goto fail_construct;

	return &core_color->public;

fail_construct:
		dm_free(core_color->user_enable_color_temperature);

fail_alloc_user_enable:
		dm_free(core_color->caps);

fail_alloc_caps:
		dm_free(core_color);

fail_alloc_context:
		return NULL;

}

void mod_color_destroy(struct mod_color *mod_color)
{
	if (mod_color != NULL) {
		int i;
		struct core_color *core_color =
				MOD_COLOR_TO_CORE(mod_color);

		dm_free(core_color->user_enable_color_temperature);

		for (i = 0; i < core_color->num_sinks; i++)
			dc_sink_release(core_color->caps[i].sink);

		dm_free(core_color->caps);

		dm_free(core_color);
	}
}

/* Given a specific dc_sink* this function finds its equivalent
 * on the dc_sink array and returns the corresponding index
 */
static unsigned int sink_index_from_sink(struct core_color *core_color,
		const struct dc_sink *sink)
{
	unsigned int index = 0;

	for (index = 0; index < core_color->num_sinks; index++)
		if (core_color->caps[index].sink == sink)
			return index;

	/* Could not find sink requested */
	ASSERT(false);
	return index;
}

bool mod_color_adjust_temperature(struct mod_color *mod_color,
		const struct dc_stream **streams, int num_streams,
		int temperature)
{
	struct core_color *core_color = MOD_COLOR_TO_CORE(mod_color);

	/*TODO: calculations for the matrix and add to the streams*/
	struct fixed31_32 identity_matrix[12];

	identity_matrix[0] = dal_fixed31_32_from_fraction(temperature, 6500);
	identity_matrix[1] = dal_fixed31_32_zero;
	identity_matrix[2] = dal_fixed31_32_zero;
	identity_matrix[3] = dal_fixed31_32_zero;
	identity_matrix[4] = dal_fixed31_32_zero;
	identity_matrix[5] = dal_fixed31_32_from_fraction(temperature, 6500);
	identity_matrix[6] = dal_fixed31_32_zero;
	identity_matrix[7] = dal_fixed31_32_zero;
	identity_matrix[8] = dal_fixed31_32_zero;
	identity_matrix[9] = dal_fixed31_32_zero;
	identity_matrix[10] = dal_fixed31_32_from_fraction(temperature, 6500);
	identity_matrix[11] = dal_fixed31_32_zero;

	int i;
	int j;

	for (i = 0; i < num_streams; i++) {
		struct core_stream *core_stream = DC_STREAM_TO_CORE(streams[i]);

		core_stream->public.csc_matrix.bypass = false;

		for (j = 0; j < 12; j++)
			core_stream->public.csc_matrix.matrix[j] =
					identity_matrix[j];
	}

	core_color->dc->stream_funcs.adjust_color_temperature(
						core_color->dc, streams,
						num_streams);

	return true;
}

/*TODO: user enable for color temperature button*/

bool mod_color_set_user_enable(struct mod_color *mod_color,
		const struct dc_stream **streams, int num_streams,
		bool user_enable)
{
	struct core_color *core_color = MOD_COLOR_TO_CORE(mod_color);

	unsigned int stream_index, sink_index;

	for (stream_index = 0; stream_index < num_streams; stream_index++) {

		sink_index = sink_index_from_sink(core_color,
				streams[stream_index]->sink);

		core_color->user_enable_color_temperature[sink_index] =
				user_enable;
	}

	/*set_color_on_streams(core_color, streams, num_streams);*/

	return true;
}









