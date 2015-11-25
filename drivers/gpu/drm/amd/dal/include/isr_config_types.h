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

#ifndef __DAL_ISR_TYPES_H__
#define __DAL_ISR_TYPES_H__

#include "grph_object_id.h"
#include "dc_types.h"

struct plane_config;
enum {
	/*move to common*/
	MAX_COFUNC_PATH_COMMON = 6,
	/*CZ worst case*/
	MAX_NUM_PLANES = 4
};

enum plane_type {
	PLANE_TYPE_GRPH = 0,
	PLANE_TYPE_VIDEO
};

struct plane_id {
	enum plane_type select;
	enum controller_id controller_id;
};

union display_plane_mask {
	struct {
		uint32_t CLONE_PRIMARY_CONTROLLER_ID_SET:1;
		uint32_t INTERLEAVED_CONTROLLER_ID_SET:1;
		uint32_t RESERVED:30;
	} bits;
	uint32_t value;
};

struct display_plane_format {
    /* always valid */
	union display_plane_mask mask;
	/* always valid */
	uint32_t display_index;
    /* always valid */
	enum dc_timing_3d_format format;
	/* always valid */
	enum controller_id controller_id;
	/* valid only if CLONE_PRIMARY_CONTROLLER_ID_SET on */
	enum controller_id clone_primary_controller_id;
	/* valid only if stereo interleave mode is on */
	enum controller_id interleave_controller_id;
	/* valid only if crtc stereo  is on */
	uint32_t right_eye_3d_polarity:1;
};

struct display_plane_set {
	struct display_plane_format
		set_mode_formats[MAX_COFUNC_PATH_COMMON];
	uint32_t reset_mode_index[
		MAX_COFUNC_PATH_COMMON];
	uint32_t num_set_mode_formats;
	uint32_t num_reset_mode_index;
};

enum layers_setup {
	LAYERS_SETUP_NOTHING = 0,
	LAYERS_SETUP_SET,
	LAYERS_SETUP_FREE
};

union plane_cfg_internal_flags {
	struct {
		uint32_t PLANE_OWNS_CRTC:1;
		uint32_t RESERVED:31;
	} bits;
	uint32_t value;
};


struct plane_cfg_internal {
	const struct plane_config *config;
	enum layers_setup setup;
	union plane_cfg_internal_flags flags;
};

enum lock_type {
	LOCK_TYPE_GRPH = 0,
	LOCK_TYPE_SURF,
	LOCK_TYPE_SCL,
	LOCK_TYPE_BLND,
    /* lock the given pipe with options above */
	LOCK_TYPE_THIS
};

enum alpha_mode {
	ALPHA_MODE_PIXEL = 0,
	ALPHA_MODE_PIXEL_AND_GLOBAL = 1,
	ALPHA_MODE_GLOBAL = 2
};

union alpha_mode_cfg_flag {
	struct {
		uint32_t MODE_IS_SET:1;
		uint32_t MODE_MULTIPLIED_IS_SET:1;
		uint32_t GLOBAL_ALPHA_IS_SET:1;
		uint32_t GLOBAL_ALPHA_GAIN_IS_SET:1;

		uint32_t MULTIPLIED_MODE:1;
		uint32_t GLOBAL_ALPHA:8;
		/* total 21 bits! */
		uint32_t GLOBAL_ALPHA_GAIN:8;
	} bits;
	uint32_t value;
};

struct alpha_mode_cfg {
	union alpha_mode_cfg_flag flags;
	enum alpha_mode mode;
};

union pending_cfg_changes {
	struct {
		uint32_t SCL_UNLOCK_REQUIRED:1;
		uint32_t BLND_UNLOCK_REQUIRED:1;
		uint32_t INPUT_CSC_SWITCH_REQUIRED:1;
		uint32_t OUTPUT_CSC_SWITCH_REQUIRED:1;
	} bits;
	uint32_t value;
};

struct pending_plane_changes {
	union pending_cfg_changes changes;
	struct plane_id id;
};


#endif /* __DAL_ISR_TYPES_H__ */
