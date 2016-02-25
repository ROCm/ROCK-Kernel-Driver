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

static struct transform_funcs dce110_transform_funcs = {
	.transform_power_up =
		dce110_transform_power_up,
	.transform_set_scaler =
		dce110_transform_set_scaler,
	.transform_set_scaler_bypass =
		dce110_transform_set_scaler_bypass,
	.transform_set_scaler_filter =
		dce110_transform_set_scaler_filter,
	.transform_set_gamut_remap =
		dce110_transform_set_gamut_remap,
	.transform_set_pixel_storage_depth =
		dce110_transform_set_pixel_storage_depth,
	.transform_get_current_pixel_storage_depth =
		dce110_transform_get_current_pixel_storage_depth,
	.transform_set_alpha = dce110_transform_set_alpha
};

/*****************************************/
/* Constructor, Destructor               */
/*****************************************/

bool dce110_transform_construct(
	struct dce110_transform *xfm110,
	struct dc_context *ctx,
	uint32_t inst,
	const struct dce110_transform_reg_offsets *reg_offsets)
{
	xfm110->base.ctx = ctx;

	xfm110->base.inst = inst;
	xfm110->base.funcs = &dce110_transform_funcs;

	xfm110->offsets = *reg_offsets;

	xfm110->lb_pixel_depth_supported =
			LB_PIXEL_DEPTH_18BPP |
			LB_PIXEL_DEPTH_24BPP |
			LB_PIXEL_DEPTH_30BPP;

	return true;
}

bool dce110_transform_power_up(struct transform *xfm)
{
	return dce110_transform_power_up_line_buffer(xfm);
}

