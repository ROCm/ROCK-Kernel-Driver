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
#include "include/grph_object_defs.h"

#include "include/grph_object_id.h"
#include "include/display_path_interface.h"
#include "include/controller_interface.h"
#include "include/clock_source_interface.h"
#include "hw_sync_control_dce110.h"


struct hw_sync_control_dce110 {
	struct hw_sync_control control;
};

#define FROM_HW_SYNC_CONTROL(c)\
	container_of((c), struct hw_sync_control_dce110, control)

static bool switch_dp_clock_source(
	struct hw_sync_control *hw_sync_control,
	struct hw_path_mode_set *path_mode_set)
{
	/*TODO: add implementation after dal_pixel_clock_switch_dp_clk_src*/
	return false;
}

static enum hwss_result resync_display_paths(
	struct hw_sync_control *hw_sync_control,
	struct hw_path_mode_set *path_mode_set,
	struct hw_resync_flags resync_flags)
{
	/* TODO: Add implementation */
	return HWSS_RESULT_ERROR;
}

static void destruct(struct hw_sync_control *cntrl)
{

}

static void destroy(struct hw_sync_control **cntrl)
{
	destruct(*cntrl);

	dal_free(*cntrl);

	*cntrl = NULL;
}

static const struct hw_sync_control_funcs sync_funcs = {
	.resync_display_paths = resync_display_paths,
	.switch_dp_clock_source = switch_dp_clock_source,
	.destroy = destroy,
};

static bool construct(struct hw_sync_control_dce110 *cntrl,
		struct dal_context *ctx,
		struct adapter_service *as)
{
	if (!dal_hw_sync_control_construct_base(&cntrl->control))
		return false;

	/* TODO: Create GSL Mgr/
	if (!dal_hw_gsl_mgr_construct_dce110(&cntrl->gsl_mgr, ctx, as))
		return false;
	*/

	cntrl->control.funcs = &sync_funcs;
	return true;

}

struct hw_sync_control *dal_hw_sync_control_dce110_create(
		struct dal_context *ctx,
		struct adapter_service *as)
{
	struct hw_sync_control_dce110 *cntrl;

	cntrl = dal_alloc(sizeof(*cntrl));

	if (!cntrl)
		return NULL;

	if (construct(cntrl, ctx, as))
		return &cntrl->control;

	ASSERT_CRITICAL(false);
	dal_free(cntrl);
	return NULL;
}

