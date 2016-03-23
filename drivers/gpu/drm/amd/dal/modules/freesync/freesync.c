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

#include "freesync.h"
#include "dm_services.h"
#include "dc.h"

static bool check_dc_support(const struct dc *dc)
{
	if (dc->stream_funcs.dc_stream_adjust_vmin_vmax == NULL)
		return false;

	return true;
}

struct mod_freesync *mod_freesync_create(struct dc *dc)
{
	struct core_freesync *core_freesync = dm_alloc(sizeof(struct core_freesync));

	if (core_freesync == NULL)
		goto fail_alloc_context;

	if (dc == NULL)
		goto fail_construct;

	core_freesync->dc = dc;

	if (!check_dc_support(dc))
		goto fail_construct;

	return &core_freesync->public;

fail_construct:
	dm_free(core_freesync);

fail_alloc_context:
	return NULL;
}

void mod_freesync_destroy(struct mod_freesync *mod_freesync)
{
	if (mod_freesync != NULL) {
		struct core_freesync *core_freesync = MOD_FREESYNC_TO_CORE(mod_freesync);

		dm_free(core_freesync);
	}
}

bool mod_freesync_set_freesync_on_streams(struct mod_freesync *mod_freesync,
		struct dc_stream **streams,	int num_streams,
		struct mod_freesync_params *params)
{
	int v_total_nominal = 0;
	int i = 0;
	struct core_freesync *core_freesync = NULL;

	if (num_streams == 0 || streams == NULL || mod_freesync == NULL
			|| params == NULL)
		return false;

	core_freesync = MOD_FREESYNC_TO_CORE(mod_freesync);

	if (params->mode == FREESYNC_MODE_DISABLED)	{
		/* Disable freesync */
		for (i = 0; i < num_streams; i++) {
			v_total_nominal = streams[i]->timing.v_total;

			core_freesync->dc->stream_funcs.dc_stream_adjust_vmin_vmax(core_freesync->dc,
					streams[i], v_total_nominal, v_total_nominal);
		}

		return true;
	} else if (params->mode == FREESYNC_MODE_VARIABLE) {
		/* Enable freesync */
	}

	return false;
}

void mod_freesync_vupdate_callback(struct mod_freesync *mod_freesync,
		struct dc_stream *stream)
{

}
