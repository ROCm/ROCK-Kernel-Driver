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

/* include DCE11 register header files */
#include "dce/dce_11_0_d.h"
#include "dce/dce_11_0_sh_mask.h"

#include "dc_types.h"
#include "core_types.h"

#include "include/grph_object_id.h"
#include "include/fixed31_32.h"
#include "include/logger_interface.h"

#include "dce110_transform.h"
#include "dce110_transform_bit_depth.h"

static const struct dce110_transform_reg_offsets reg_offsets[] = {
{
	.scl_offset = (mmSCL0_SCL_CONTROL - mmSCL0_SCL_CONTROL),
	.dcfe_offset = (mmDCFE0_DCFE_MEM_PWR_CTRL - mmDCFE0_DCFE_MEM_PWR_CTRL),
	.dcp_offset = (mmDCP0_GRPH_CONTROL - mmDCP0_GRPH_CONTROL),
	.lb_offset = (mmLB0_LB_DATA_FORMAT - mmLB0_LB_DATA_FORMAT),
},
{	.scl_offset = (mmSCL1_SCL_CONTROL - mmSCL0_SCL_CONTROL),
	.dcfe_offset = (mmDCFE1_DCFE_MEM_PWR_CTRL - mmDCFE0_DCFE_MEM_PWR_CTRL),
	.dcp_offset = (mmDCP1_GRPH_CONTROL - mmDCP0_GRPH_CONTROL),
	.lb_offset = (mmLB1_LB_DATA_FORMAT - mmLB0_LB_DATA_FORMAT),
},
{	.scl_offset = (mmSCL2_SCL_CONTROL - mmSCL0_SCL_CONTROL),
	.dcfe_offset = (mmDCFE2_DCFE_MEM_PWR_CTRL - mmDCFE0_DCFE_MEM_PWR_CTRL),
	.dcp_offset = (mmDCP2_GRPH_CONTROL - mmDCP0_GRPH_CONTROL),
	.lb_offset = (mmLB2_LB_DATA_FORMAT - mmLB0_LB_DATA_FORMAT),
}
};

/*****************************************/
/* Constructor, Destructor               */
/*****************************************/

bool dce110_transform_construct(
	struct dce110_transform *xfm110,
	struct dc_context *ctx,
	uint32_t inst)
{
	if (inst >= ARRAY_SIZE(reg_offsets))
		return false;

	xfm110->base.ctx = ctx;

	xfm110->base.inst = inst;

	xfm110->offsets = reg_offsets[inst];

	xfm110->lb_pixel_depth_supported =
			LB_PIXEL_DEPTH_18BPP |
			LB_PIXEL_DEPTH_24BPP |
			LB_PIXEL_DEPTH_30BPP;

	return true;
}

void dce110_transform_destroy(struct transform **xfm)
{
	dc_service_free((*xfm)->ctx, TO_DCE110_TRANSFORM(*xfm));
	*xfm = NULL;
}

struct transform *dce110_transform_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct dce110_transform *transform =
		dc_service_alloc(ctx, sizeof(struct dce110_transform));

	if (!transform)
		return NULL;

	if (dce110_transform_construct(transform,
			ctx, inst))
		return &transform->base;

	BREAK_TO_DEBUGGER();
	dc_service_free(ctx, transform);
	return NULL;
}

bool dce110_transform_power_up(struct transform *xfm)
{
	return dce110_transform_power_up_line_buffer(xfm);
}

