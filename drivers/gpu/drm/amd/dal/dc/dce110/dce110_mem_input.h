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

#include "mem_input.h"

#define TO_DCE110_MEM_INPUT(mi)\
	container_of(mi, struct dce110_mem_input, base)

struct dce110_mem_input_reg_offsets {
	uint32_t dcp;
	uint32_t dmif;
	uint32_t pipe;
};

enum stutter_mode_type {
/*	TODO: Clean up these enums, right now only one is being used
 *	STUTTER_MODE_LEGACY = 0X00000001,
 *	STUTTER_MODE_ENHANCED = 0X00000002,
 *	STUTTER_MODE_FID_NBP_STATE = 0X00000004,
 *	STUTTER_MODE_WATERMARK_NBP_STATE = 0X00000008,
 *	STUTTER_MODE_SINGLE_DISPLAY_MODEL = 0X00000010,
 *	STUTTER_MODE_MIXED_DISPLAY_MODEL = 0X00000020,
 *	STUTTER_MODE_DUAL_DMIF_BUFFER = 0X00000040,
 */
	STUTTER_MODE_NO_DMIF_BUFFER_ALLOCATION = 0X00000080,
/*
 *	STUTTER_MODE_NO_ADVANCED_REQUEST = 0X00000100,
 *	STUTTER_MODE_NO_LB_RESET = 0X00000200,
 *	STUTTER_MODE_DISABLED = 0X00000400,
 *	STUTTER_MODE_AGGRESSIVE_MARKS = 0X00000800,
 *	STUTTER_MODE_URGENCY = 0X00001000,
 *	STUTTER_MODE_QUAD_DMIF_BUFFER = 0X00002000,
 *	STUTTER_MODE_NOT_USED = 0X00008000
 */
};

struct dce110_mem_input {
	struct mem_input base;
	struct dce110_mem_input_reg_offsets offsets;
	uint32_t supported_stutter_mode;
};

bool dce110_mem_input_construct(
	struct dce110_mem_input *mem_input110,
	struct dc_context *ctx,
	uint32_t inst,
	const struct dce110_mem_input_reg_offsets *offsets);

/*
 * dce110_mem_input_program_display_marks
 *
 * This function will program nbp stutter and urgency watermarks to minimum
 * allowable values
 */
void dce110_mem_input_program_display_marks(
	struct mem_input *mem_input,
	struct bw_watermarks nbp,
	struct bw_watermarks stutter,
	struct bw_watermarks urgent,
	uint32_t total_dest_line_time_ns);

/*
 * dce110_allocate_mem_input
 *
 * This function will allocate a dmif buffer and program required
 * pixel duration for pipe
 */
void dce110_allocate_mem_input(
	struct mem_input *mem_input,
	uint32_t h_total,/* for current stream */
	uint32_t v_total,/* for current stream */
	uint32_t pix_clk_khz,/* for current stream */
	uint32_t total_stream_num);

/*
 * dce110_free_mem_input
 *
 * This function will deallocate a dmif buffer from pipe
 */
void dce110_free_mem_input(
	struct mem_input *mem_input,
	uint32_t total_stream_num);

/*
 * dce110_mem_input_program_surface_flip_and_addr
 *
 * This function programs hsync/vsync mode and surface address
 */
bool dce110_mem_input_program_surface_flip_and_addr(
	struct mem_input *mem_input,
	const struct dc_plane_address *address,
	bool flip_immediate);

/*
 * dce110_mem_input_program_surface_config
 *
 * This function will program surface tiling, size, rotation and pixel format
 * to corresponding dcp registers.
 */
bool  dce110_mem_input_program_surface_config(
	struct mem_input *mem_input,
	enum surface_pixel_format format,
	struct dc_tiling_info *tiling_info,
	union plane_size *plane_size,
	enum dc_rotation_angle rotation);

/*
 * dce110_mem_input_wait_for_no_surface_update_pending
 *
 * This function will wait until the surface update-pending bit is cleared.
 * This is necessary when a flip immediate call is requested as we shouldn't
 * return until the flip has actually occurred.
 */
void dce110_mem_input_wait_for_no_surface_update_pending(
	struct mem_input *mem_input);

#endif
