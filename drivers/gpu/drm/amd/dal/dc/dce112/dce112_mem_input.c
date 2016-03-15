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
#include "dm_services.h"
#include "dce112_mem_input.h"


#include "dce/dce_11_2_d.h"
#include "dce/dce_11_2_sh_mask.h"


#define DCP_REG(reg) (reg + mem_input110->offsets.dcp)
#define DMIF_REG(reg) (reg + mem_input110->offsets.dmif)
#define PIPE_REG(reg) (reg + mem_input110->offsets.pipe)

static void program_urgency_watermark(
	const struct dc_context *ctx,
	const uint32_t offset,
	struct bw_watermarks marks_low,
	uint32_t total_dest_line_time_ns)
{
	/* register value */
	uint32_t urgency_cntl = 0;
	uint32_t wm_mask_cntl = 0;

	uint32_t urgency_addr = offset + mmDPG_PIPE_URGENCY_CONTROL;
	uint32_t wm_addr = offset + mmDPG_WATERMARK_MASK_CONTROL;

	/*Write mask to enable reading/writing of watermark set A*/
	wm_mask_cntl = dm_read_reg(ctx, wm_addr);
	set_reg_field_value(wm_mask_cntl,
			0,
			DPG_WATERMARK_MASK_CONTROL,
			URGENCY_WATERMARK_MASK);
	dm_write_reg(ctx, wm_addr, wm_mask_cntl);

	urgency_cntl = dm_read_reg(ctx, urgency_addr);

	set_reg_field_value(
		urgency_cntl,
		marks_low.a_mark,
		DPG_PIPE_URGENCY_CONTROL,
		URGENCY_LOW_WATERMARK);

	set_reg_field_value(
		urgency_cntl,
		total_dest_line_time_ns,
		DPG_PIPE_URGENCY_CONTROL,
		URGENCY_HIGH_WATERMARK);
	dm_write_reg(ctx, urgency_addr, urgency_cntl);

	/*Write mask to enable reading/writing of watermark set B*/
	wm_mask_cntl = dm_read_reg(ctx, wm_addr);
	set_reg_field_value(wm_mask_cntl,
			1,
			DPG_WATERMARK_MASK_CONTROL,
			URGENCY_WATERMARK_MASK);
	dm_write_reg(ctx, wm_addr, wm_mask_cntl);

	urgency_cntl = dm_read_reg(ctx, urgency_addr);

	set_reg_field_value(urgency_cntl,
		marks_low.b_mark,
		DPG_PIPE_URGENCY_CONTROL,
		URGENCY_LOW_WATERMARK);

	set_reg_field_value(urgency_cntl,
		total_dest_line_time_ns,
		DPG_PIPE_URGENCY_CONTROL,
		URGENCY_HIGH_WATERMARK);
	dm_write_reg(ctx, urgency_addr, urgency_cntl);

	/*Write mask to enable reading/writing of watermark set C*/
	wm_mask_cntl = dm_read_reg(ctx, wm_addr);
	set_reg_field_value(wm_mask_cntl,
			2,
			DPG_WATERMARK_MASK_CONTROL,
			URGENCY_WATERMARK_MASK);
	dm_write_reg(ctx, wm_addr, wm_mask_cntl);

	urgency_cntl = dm_read_reg(ctx, urgency_addr);

	set_reg_field_value(urgency_cntl,
		marks_low.c_mark,
		DPG_PIPE_URGENCY_CONTROL,
		URGENCY_LOW_WATERMARK);

	set_reg_field_value(urgency_cntl,
		total_dest_line_time_ns,
		DPG_PIPE_URGENCY_CONTROL,
		URGENCY_HIGH_WATERMARK);
	dm_write_reg(ctx, urgency_addr, urgency_cntl);

	/*Write mask to enable reading/writing of watermark set D*/
	wm_mask_cntl = dm_read_reg(ctx, wm_addr);
	set_reg_field_value(wm_mask_cntl,
			3,
			DPG_WATERMARK_MASK_CONTROL,
			URGENCY_WATERMARK_MASK);
	dm_write_reg(ctx, wm_addr, wm_mask_cntl);

	urgency_cntl = dm_read_reg(ctx, urgency_addr);

	set_reg_field_value(urgency_cntl,
		marks_low.d_mark,
		DPG_PIPE_URGENCY_CONTROL,
		URGENCY_LOW_WATERMARK);

	set_reg_field_value(urgency_cntl,
		total_dest_line_time_ns,
		DPG_PIPE_URGENCY_CONTROL,
		URGENCY_HIGH_WATERMARK);
	dm_write_reg(ctx, urgency_addr, urgency_cntl);
}

static void program_stutter_watermark(
	const struct dc_context *ctx,
	const uint32_t offset,
	struct bw_watermarks marks)
{
	/* register value */
	uint32_t stutter_cntl = 0;
	uint32_t wm_mask_cntl = 0;

	uint32_t stutter_addr = offset + mmDPG_PIPE_STUTTER_CONTROL;
	uint32_t wm_addr = offset + mmDPG_WATERMARK_MASK_CONTROL;

	/*Write mask to enable reading/writing of watermark set A*/

	wm_mask_cntl = dm_read_reg(ctx, wm_addr);
	set_reg_field_value(wm_mask_cntl,
		0,
		DPG_WATERMARK_MASK_CONTROL,
		STUTTER_EXIT_SELF_REFRESH_WATERMARK_MASK);
	dm_write_reg(ctx, wm_addr, wm_mask_cntl);

	stutter_cntl = dm_read_reg(ctx, stutter_addr);

	set_reg_field_value(stutter_cntl,
		1,
		DPG_PIPE_STUTTER_CONTROL,
		STUTTER_ENABLE);
	set_reg_field_value(stutter_cntl,
		1,
		DPG_PIPE_STUTTER_CONTROL,
		STUTTER_IGNORE_FBC);

	/*Write watermark set A*/
	set_reg_field_value(stutter_cntl,
		marks.a_mark,
		DPG_PIPE_STUTTER_CONTROL,
		STUTTER_EXIT_SELF_REFRESH_WATERMARK);
	dm_write_reg(ctx, stutter_addr, stutter_cntl);

	/*Write mask to enable reading/writing of watermark set B*/
	wm_mask_cntl = dm_read_reg(ctx, wm_addr);
	set_reg_field_value(wm_mask_cntl,
		1,
		DPG_WATERMARK_MASK_CONTROL,
		STUTTER_EXIT_SELF_REFRESH_WATERMARK_MASK);
	dm_write_reg(ctx, wm_addr, wm_mask_cntl);

	stutter_cntl = dm_read_reg(ctx, stutter_addr);
	set_reg_field_value(stutter_cntl,
		1,
		DPG_PIPE_STUTTER_CONTROL,
		STUTTER_ENABLE);
	set_reg_field_value(stutter_cntl,
		1,
		DPG_PIPE_STUTTER_CONTROL,
		STUTTER_IGNORE_FBC);

	/*Write watermark set B*/
	set_reg_field_value(stutter_cntl,
		marks.b_mark,
		DPG_PIPE_STUTTER_CONTROL,
		STUTTER_EXIT_SELF_REFRESH_WATERMARK);
	dm_write_reg(ctx, stutter_addr, stutter_cntl);

	/*Write mask to enable reading/writing of watermark set C*/
	wm_mask_cntl = dm_read_reg(ctx, wm_addr);
	set_reg_field_value(wm_mask_cntl,
		2,
		DPG_WATERMARK_MASK_CONTROL,
		STUTTER_EXIT_SELF_REFRESH_WATERMARK_MASK);
	dm_write_reg(ctx, wm_addr, wm_mask_cntl);

	stutter_cntl = dm_read_reg(ctx, stutter_addr);
	set_reg_field_value(stutter_cntl,
		1,
		DPG_PIPE_STUTTER_CONTROL,
		STUTTER_ENABLE);
	set_reg_field_value(stutter_cntl,
		1,
		DPG_PIPE_STUTTER_CONTROL,
		STUTTER_IGNORE_FBC);

	/*Write watermark set C*/
	set_reg_field_value(stutter_cntl,
		marks.c_mark,
		DPG_PIPE_STUTTER_CONTROL,
		STUTTER_EXIT_SELF_REFRESH_WATERMARK);
	dm_write_reg(ctx, stutter_addr, stutter_cntl);

	/*Write mask to enable reading/writing of watermark set D*/
	wm_mask_cntl = dm_read_reg(ctx, wm_addr);
	set_reg_field_value(wm_mask_cntl,
		3,
		DPG_WATERMARK_MASK_CONTROL,
		STUTTER_EXIT_SELF_REFRESH_WATERMARK_MASK);
	dm_write_reg(ctx, wm_addr, wm_mask_cntl);

	stutter_cntl = dm_read_reg(ctx, stutter_addr);
	set_reg_field_value(stutter_cntl,
		1,
		DPG_PIPE_STUTTER_CONTROL,
		STUTTER_ENABLE);
	set_reg_field_value(stutter_cntl,
		1,
		DPG_PIPE_STUTTER_CONTROL,
		STUTTER_IGNORE_FBC);

	/*Write watermark set D*/
	set_reg_field_value(stutter_cntl,
		marks.d_mark,
		DPG_PIPE_STUTTER_CONTROL,
		STUTTER_EXIT_SELF_REFRESH_WATERMARK);
	dm_write_reg(ctx, stutter_addr, stutter_cntl);
}

static void program_nbp_watermark(
	const struct dc_context *ctx,
	const uint32_t offset,
	struct bw_watermarks marks)
{
	uint32_t value;
	uint32_t addr;
	/* Write mask to enable reading/writing of watermark set A */
	addr = offset + mmDPG_WATERMARK_MASK_CONTROL;
	value = dm_read_reg(ctx, addr);
	set_reg_field_value(
		value,
		0,
		DPG_WATERMARK_MASK_CONTROL,
		NB_PSTATE_CHANGE_WATERMARK_MASK);
	dm_write_reg(ctx, addr, value);

	addr = offset + mmDPG_PIPE_NB_PSTATE_CHANGE_CONTROL;
	value = dm_read_reg(ctx, addr);
	set_reg_field_value(
		value,
		1,
		DPG_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_ENABLE);
	set_reg_field_value(
		value,
		1,
		DPG_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_URGENT_DURING_REQUEST);
	set_reg_field_value(
		value,
		1,
		DPG_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_NOT_SELF_REFRESH_DURING_REQUEST);
	dm_write_reg(ctx, addr, value);

	/* Write watermark set A */
	value = dm_read_reg(ctx, addr);
	set_reg_field_value(
		value,
		marks.a_mark,
		DPG_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_WATERMARK);
	dm_write_reg(ctx, addr, value);

	/* Write mask to enable reading/writing of watermark set B */
	addr = offset + mmDPG_WATERMARK_MASK_CONTROL;
	value = dm_read_reg(ctx, addr);
	set_reg_field_value(
		value,
		1,
		DPG_WATERMARK_MASK_CONTROL,
		NB_PSTATE_CHANGE_WATERMARK_MASK);
	dm_write_reg(ctx, addr, value);

	addr = offset + mmDPG_PIPE_NB_PSTATE_CHANGE_CONTROL;
	value = dm_read_reg(ctx, addr);
	set_reg_field_value(
		value,
		1,
		DPG_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_ENABLE);
	set_reg_field_value(
		value,
		1,
		DPG_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_URGENT_DURING_REQUEST);
	set_reg_field_value(
		value,
		1,
		DPG_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_NOT_SELF_REFRESH_DURING_REQUEST);
	dm_write_reg(ctx, addr, value);

	/* Write watermark set B */
	value = dm_read_reg(ctx, addr);
	set_reg_field_value(
		value,
		marks.b_mark,
		DPG_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_WATERMARK);
	dm_write_reg(ctx, addr, value);

	/* Write mask to enable reading/writing of watermark set C */
	addr = offset + mmDPG_WATERMARK_MASK_CONTROL;
	value = dm_read_reg(ctx, addr);
	set_reg_field_value(
		value,
		2,
		DPG_WATERMARK_MASK_CONTROL,
		NB_PSTATE_CHANGE_WATERMARK_MASK);
	dm_write_reg(ctx, addr, value);

	addr = offset + mmDPG_PIPE_NB_PSTATE_CHANGE_CONTROL;
	value = dm_read_reg(ctx, addr);
	set_reg_field_value(
		value,
		1,
		DPG_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_ENABLE);
	set_reg_field_value(
		value,
		1,
		DPG_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_URGENT_DURING_REQUEST);
	set_reg_field_value(
		value,
		1,
		DPG_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_NOT_SELF_REFRESH_DURING_REQUEST);
	dm_write_reg(ctx, addr, value);

	/* Write watermark set C */
	value = dm_read_reg(ctx, addr);
	set_reg_field_value(
		value,
		marks.c_mark,
		DPG_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_WATERMARK);
	dm_write_reg(ctx, addr, value);

	/* Write mask to enable reading/writing of watermark set D */
	addr = offset + mmDPG_WATERMARK_MASK_CONTROL;
	value = dm_read_reg(ctx, addr);
	set_reg_field_value(
		value,
		3,
		DPG_WATERMARK_MASK_CONTROL,
		NB_PSTATE_CHANGE_WATERMARK_MASK);
	dm_write_reg(ctx, addr, value);

	addr = offset + mmDPG_PIPE_NB_PSTATE_CHANGE_CONTROL;
	value = dm_read_reg(ctx, addr);
	set_reg_field_value(
		value,
		1,
		DPG_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_ENABLE);
	set_reg_field_value(
		value,
		1,
		DPG_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_URGENT_DURING_REQUEST);
	set_reg_field_value(
		value,
		1,
		DPG_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_NOT_SELF_REFRESH_DURING_REQUEST);
	dm_write_reg(ctx, addr, value);

	/* Write watermark set D */
	value = dm_read_reg(ctx, addr);
	set_reg_field_value(
		value,
		marks.d_mark,
		DPG_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_WATERMARK);
	dm_write_reg(ctx, addr, value);
}

static void dce112_mem_input_program_display_marks(
	struct mem_input *mem_input,
	struct bw_watermarks nbp,
	struct bw_watermarks stutter,
	struct bw_watermarks urgent,
	uint32_t total_dest_line_time_ns)
{
	struct dce110_mem_input *bm_dce110 = TO_DCE110_MEM_INPUT(mem_input);

	program_urgency_watermark(
		mem_input->ctx,
		bm_dce110->offsets.dmif,
		urgent,
		total_dest_line_time_ns);

	program_nbp_watermark(
		mem_input->ctx,
		bm_dce110->offsets.dmif,
		nbp);

	program_stutter_watermark(
		mem_input->ctx,
		bm_dce110->offsets.dmif,
		stutter);
}

/*****************************************/
/* Constructor, Destructor               */
/*****************************************/

bool dce112_mem_input_construct(
	struct dce110_mem_input *mem_input110,
	struct dc_context *ctx,
	uint32_t inst,
	const struct dce110_mem_input_reg_offsets *offsets)
{
	if (!dce110_mem_input_construct(mem_input110, ctx, inst, offsets))
		return false;

	mem_input110->base.funcs->mem_input_program_display_marks =
					dce112_mem_input_program_display_marks;

	return true;
}
