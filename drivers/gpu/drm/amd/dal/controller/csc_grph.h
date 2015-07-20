/* Copyright 2012-15 Advanced Micro Devices, Inc.
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

#ifndef __DAL_CSC_GRPH_H__
#define __DAL_CSC_GRPH_H__

#include "include/hw_sequencer_types.h"
#include "csc.h"
#include "graphics_and_video_gamma.h"

struct csc_grph;

void dal_csc_grph_wide_gamut_set_gamut_remap(
	struct csc_grph *cg,
	const struct grph_csc_adjustment *adjust);
void dal_csc_grph_wide_gamut_set_rgb_limited_range_adjustment(
	struct csc_grph *cg,
	const struct grph_csc_adjustment *adjust);
void dal_csc_grph_wide_gamut_set_yuv_adjustment(
	struct csc_grph *cg,
	const struct grph_csc_adjustment *adjust);
void dal_csc_grph_wide_gamut_set_rgb_adjustment_legacy(
	struct csc_grph *cg,
	const struct grph_csc_adjustment *adjust);
void dal_csc_grph_get_graphic_color_adjustment_range(
	enum grph_csc_adjust_item grph_adjust_item,
	struct hw_adjustment_range *adjust_range);

struct csc_grph_init_data {
	struct dal_context *ctx;
	enum controller_id id;
};

bool dal_csc_grph_construct(
	struct csc_grph *cg,
	struct csc_grph_init_data *init_data);

#define MATRIX_CONST 12

struct dcp_color_matrix {
	enum color_space color_space;
	uint16_t regval[MATRIX_CONST];
};

struct csc_grph_funcs {
	void (*set_overscan_color_black)(
		struct csc_grph *csc,
		enum color_space color_space);
	void (*set_grph_csc_default)(
		struct csc_grph *csc,
		const struct default_adjustment *adjustment);
	void (*set_grph_csc_adjustment)(
		struct csc_grph *csc,
		const struct grph_csc_adjustment *adjustment);
	void (*program_color_matrix)(
		struct csc_grph *cg,
		const struct dcp_color_matrix *tbl_entry,
		enum grph_color_adjust_option options);
	void (*program_gamut_remap)(
		struct csc_grph *cg,
		const uint16_t *reg_val);
	bool (*configure_graphics_mode)(
		struct csc_grph *cg,
		enum wide_gamut_color_mode config,
		enum graphics_csc_adjust_type csc_adjust_type,
		enum color_space color_space);
	void (*destroy)(struct csc_grph *csc);
};

struct csc_grph {
	const struct csc_grph_funcs *funcs;
	const uint32_t *regs;
	struct dal_context *ctx;
};

#endif
