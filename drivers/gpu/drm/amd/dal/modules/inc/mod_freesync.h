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

#ifndef MOD_FREESYNC_H_
#define MOD_FREESYNC_H_

#include "dm_services.h"

struct mod_freesync {
	int dummy;
};

enum mod_freesync_state {
    FREESYNC_STATE_FULLSCREEN,
    FREESYNC_STATE_STATIC_SCREEN
};

enum mod_freesync_user_enable_mask {
    FREESYNC_USER_ENABLE_STATIC = 0x1,
    FREESYNC_USER_ENABLE_VIDEO = 0x2,
    FREESYNC_USER_ENABLE_GAMING = 0x4
};

struct mod_freesync_user_enable {
	bool enable_for_static;
	bool enable_for_video;
	bool enable_for_gaming;
};

struct mod_freesync_caps {
	bool supported;
	int minRefreshInMicroHz;
	int maxRefreshInMicroHz;
};

struct mod_freesync *mod_freesync_create(struct dc *dc);
void mod_freesync_destroy(struct mod_freesync *mod_freesync);

/*
 * Add sink to be tracked by module
 */
bool mod_freesync_add_sink(struct mod_freesync *mod_freesync,
		const struct dc_sink *sink, struct mod_freesync_caps *caps);

/*
 * Remove sink to be tracked by module
 */
bool mod_freesync_remove_sink(struct mod_freesync *mod_freesync,
		const struct dc_sink *sink);

/*
 * Build additional parameters for dc_stream when creating stream for
 * sink to support freesync
 */
void mod_freesync_update_stream(struct mod_freesync *mod_freesync,
		struct dc_stream *stream);

/*
 * Update the freesync state flags for each display and program
 * freesync accordingly
 */
void mod_freesync_update_state(struct mod_freesync *mod_freesync,
		const struct dc_stream **streams, int num_streams,
		enum mod_freesync_state freesyncState, bool enable);

/*
 * This interface must be called for on every VUPDATE event for every stream
 * which is not FREESYNC_MODE_DISABLED.  Calling this for a stream that is in
 * FREESYNC_MODE_DISABLED has no effect.
 */
void mod_freesync_vupdate_callback(struct mod_freesync *mod_freesync,
	struct dc_stream *stream);

bool mod_freesync_get_freesync_caps(struct mod_freesync *mod_freesync,
		const struct dc_sink *sink, struct mod_freesync_caps *caps);

bool mod_freesync_set_user_enable(struct mod_freesync *mod_freesync,
		const struct dc_stream **streams, int num_streams,
		struct mod_freesync_user_enable *user_enable);

bool mod_freesync_get_user_enable(struct mod_freesync *mod_freesync,
		const struct dc_sink *sink,
		struct mod_freesync_user_enable *user_enable);

#endif
