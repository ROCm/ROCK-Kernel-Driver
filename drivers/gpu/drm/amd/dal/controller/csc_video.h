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

#ifndef __DAL_CSC_VIDEO_H__
#define __DAL_CSC_VIDEO_H__

#include "include/grph_csc_types.h"
#include "include/video_csc_types.h"
#include "include/hw_sequencer_types.h"
#include "internal_types_wide_gamut.h"
#include "graphics_and_video_gamma.h"

#define MAXTRIX_COEFFICIENTS_NUMBER 12
#define MAXTRIX_COEFFICIENTS_WRAP_NUMBER (MAXTRIX_COEFFICIENTS_NUMBER + 4)

#define MAX_OVL_MATRIX_COUNT 12

struct dcp_video_matrix {
	enum ovl_color_space color_space;
	int32_t value[MAXTRIX_COEFFICIENTS_NUMBER];
};

struct csc_video;

struct csc_video_funcs {
	void (*program_input_matrix)(
		struct csc_video *cv,
		const uint16_t regval[]);
	void (*program_ovl_prescale)(
		struct csc_video *cv,
		const struct ovl_csc_adjustment *adjust);
	void (*program_csc_input)(
		struct csc_video *cv,
		const struct ovl_csc_adjustment *adjust);
	bool (*configure_overlay_mode)(
		struct csc_video *cv,
		enum wide_gamut_color_mode config,
		enum overlay_csc_adjust_type adjust_csc_type,
		enum color_space color_space);
	void (*program_gamut_remap)(
		struct csc_video *cv,
		const uint16_t *matrix);
	void (*program_ovl_matrix)(
		struct csc_video *cv,
		const uint16_t *matrix);
	void (*destroy)(struct csc_video **csc_video);
};

struct csc_video {
	const struct csc_video_funcs *funcs;
	const uint32_t *regs;
	struct dal_context *ctx;
};


struct csc_video_init_data {
	struct dal_context *ctx;
	enum controller_id id;
};

void dal_csc_video_set_ovl_csc_adjustment(
	struct csc_video *cv,
	const struct ovl_csc_adjustment *adjust,
	enum color_space cs);
void dal_csc_video_build_input_matrix(
	const struct dcp_video_matrix *tbl_entry,
	struct fixed31_32 *matrix);
void dal_csc_video_apply_oem_matrix(
	const struct ovl_csc_adjustment *adjust,
	struct fixed31_32 *matrix);
void dal_csc_video_get_ovl_adjustment_range(
	struct csc_video *cv,
	enum ovl_csc_adjust_item overlay_adjust_item,
	struct hw_adjustment_range *adjust_range);

bool dal_csc_video_construct(
	struct csc_video *cv,
	struct csc_video_init_data *init_data);

#endif
