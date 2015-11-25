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

#include "dal_services.h"
#include "include/logger_interface.h"

#include "dce/dce_11_0_d.h"
#include "dce/dce_11_0_sh_mask.h"

#include "dce110_ipp.h"

static const struct dce110_ipp_reg_offsets reg_offsets[] = {
{
	.dcp_offset = (mmDCP0_CUR_CONTROL - mmDCP0_CUR_CONTROL),
},
{
	.dcp_offset = (mmDCP1_CUR_CONTROL - mmDCP0_CUR_CONTROL),
},
{
	.dcp_offset = (mmDCP2_CUR_CONTROL - mmDCP0_CUR_CONTROL),
}
};

bool dce110_ipp_construct(
	struct dce110_ipp* ipp,
	struct dc_context *ctx,
	uint32_t inst)
{
	if ((inst < 1) || (inst > ARRAY_SIZE(reg_offsets)))
		return false;

	ipp->base.ctx = ctx;

	ipp->base.inst = inst;

	ipp->offsets = reg_offsets[inst-1];

	return true;
}

void dce110_ipp_destroy(struct input_pixel_processor **ipp)
{
	dc_service_free((*ipp)->ctx, TO_DCE110_IPP(*ipp));
	*ipp = NULL;
}

struct input_pixel_processor *dce110_ipp_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct dce110_ipp *ipp =
		dc_service_alloc(ctx, sizeof(struct dce110_ipp));

	if (!ipp)
		return NULL;

	if (dce110_ipp_construct(ipp, ctx, inst))
			return &ipp->base;

	BREAK_TO_DEBUGGER();
	dc_service_free(ctx, ipp);
	return NULL;
}
