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

#ifndef __DC_MEM_INPUT_DCE110_H__
#define __DC_MEM_INPUT_DCE110_H__

#include "inc/mem_input.h"

#define TO_DCE110_MEM_INPUT(mi)\
	container_of(mi, struct dce110_mem_input, base)

struct dce110_mem_input_reg_offsets {
	uint32_t dcp;
	uint32_t dmif;
	uint32_t pipe;
};

struct dce110_mem_input {
	struct mem_input base;
	struct dce110_mem_input_reg_offsets offsets;
	uint32_t supported_stutter_mode;
};

struct mem_input *dce110_mem_input_create(
	struct dc_context *ctx,
	uint32_t inst);

void dce110_mem_input_destroy(struct mem_input **mem_input);

void dce110_mem_input_program_safe_display_marks(struct mem_input *mi);

void dce110_mem_input_program_nbp_watermark(
	struct mem_input *mem_input,
	struct bw_watermarks marks);

void dce110_mem_input_program_stutter_watermark(
	struct mem_input *mem_input,
	struct bw_watermarks marks);

void dce110_mem_input_program_urgency_watermark(
	struct mem_input *mem_input,
	struct bw_watermarks marks,
	uint32_t h_total,
	uint32_t pixel_clk_in_khz,
	uint32_t pstate_blackout_duration_ns);

void dce110_mem_input_allocate_dmif_buffer(
		struct mem_input *mem_input,
		struct dc_crtc_timing *timing,
		uint32_t paths_num);

void dce110_mem_input_deallocate_dmif_buffer(
	struct mem_input *mem_input, uint32_t paths_num);

void dce110_mem_input_program_pix_dur(
	struct mem_input *mem_input, uint32_t pix_clk_khz);

bool dce110_mem_input_program_surface_flip_and_addr(
	struct mem_input *mem_input,
	const struct dc_plane_address *address,
	bool flip_immediate);

bool  dce110_mem_input_program_surface_config(
	struct mem_input *mem_input,
	const struct dc_surface *surface);


#endif
