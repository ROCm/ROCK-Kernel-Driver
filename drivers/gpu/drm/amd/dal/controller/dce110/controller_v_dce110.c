/*
 * Copyright 2012-14 Advanced Micro Devices, Inc.
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

#include "include/grph_object_id.h"
#include "include/controller_interface.h"
#include "include/adapter_service_interface.h"

#include "controller_v_dce110.h"
#include "pipe_control_v_dce110.h"
#include "timing_generator_v_dce110.h"
#include "scaler_v_dce110.h"
#include "line_buffer_v_dce110.h"
#include "col_man_csc_dce110.h"
#include "col_man_gamma_dce110.h"
#include "surface_v_dce110.h"
#include "include/logger_interface.h"

#include "../controller.h"

static bool is_surface_supported(
	struct controller *crtc,
	const struct plane_config *pl_cfg)
{
	bool rc;

	/* TODO: check programming guide or DAL2 for types of supported
	 * surfaces. */

	/* Underlay Pipe supports all pixel formats. */

	/* Underlay has upper limit of 1080. */
	if (pl_cfg->config.format >= SURFACE_PIXEL_FORMAT_VIDEO_BEGIN) {
		/* this is a video surface */
		if (pl_cfg->attributes.src_rect.height > 1080) {
			dal_logger_write(crtc->dal_context->logger,
				LOG_MAJOR_WARNING,
				LOG_MINOR_COMPONENT_CONTROLLER,
				"%s: surface height is more than 1080\n",
				__func__);
			rc = false;
		} else {
			rc = true;
		}

	} else {
		/* This is a graphics surface, no special limitations
		 * (only regular Bandwidth limitiations apply) */
		rc = true;
	}

	return rc;
}

static void destroy(struct controller **crtc)
{
	dal_controller_base_destruct(*crtc);

	dal_free(*crtc);

	*crtc = NULL;
}

static bool construct(
	struct controller *crtc,
	struct controller_init_data *init_data)
{
	struct scaler_init_data scl_init_data = {0};
	struct surface_init_data surf_init_data = {0};

	if (!dal_controller_base_construct(crtc, init_data))
		return false;

	scl_init_data.bp = dal_adapter_service_get_bios_parser(
			init_data->as);
	scl_init_data.dal_ctx = init_data->dal_context;
	scl_init_data.id = CONTROLLER_ID_UNDERLAY0;

	crtc->scl = dal_scaler_v_dce110_create(&scl_init_data);

	if (!crtc->scl)
		goto scl_fail;


	surf_init_data.dal_ctx = init_data->dal_context;
	surf_init_data.id = CONTROLLER_ID_UNDERLAY0;
	crtc->surface = dal_surface_v_dce110_create(&surf_init_data);

	if (!crtc->surface)
		goto surface_fail;

	crtc->pc = dal_pipe_control_v_dce110_create(
		init_data->as,
		init_data->dal_context,
		init_data->controller);

	if (!crtc->pc)
		goto pc_fail;

	crtc->tg = dal_timing_generator_v_dce110_create(
		init_data->as,
		init_data->dal_context,
		init_data->controller);

	if (!crtc->tg)
		goto tg_fail;

	{
		struct line_buffer_init_data lb_init_data = {0};

		lb_init_data.dal_context = crtc->dal_context;
		lb_init_data.as = init_data->as;
		lb_init_data.id = init_data->controller;
		crtc->lb =
			dal_line_buffer_v_dce110_create(&lb_init_data);
	}

	if (!crtc->lb)
		goto lb_fail;

	{
		struct csc_init_data csc_init_data = {0};

		csc_init_data.id = init_data->controller;
		csc_init_data.ctx = crtc->dal_context;
		csc_init_data.as = init_data->as;
		crtc->csc =
			dal_col_man_csc_dce110_create(&csc_init_data);
	}

	if (!crtc->csc)
		goto csc_fail;

	{
		struct grph_gamma_init_data gg_init_data = {0};

		gg_init_data.as = init_data->as;
		gg_init_data.ctx = crtc->dal_context;
		gg_init_data.id = init_data->controller;
		crtc->grph_gamma =
			dal_col_man_grph_dce110_create(&gg_init_data);
	}

	if (!crtc->grph_gamma)
		goto gamma_fail;

	/* all OK */
	crtc->funcs.destroy = destroy;
	crtc->funcs.is_surface_supported = is_surface_supported;
	return true;

gamma_fail:
	crtc->csc->funcs->destroy(&crtc->csc);
csc_fail:
	crtc->lb->funcs->destroy(&crtc->lb);
lb_fail:
	crtc->tg->funcs->destroy(&crtc->tg);
tg_fail:
	crtc->pc->funcs->destroy(&crtc->pc);
pc_fail:
	crtc->surface->funcs->destroy(&crtc->surface);
surface_fail:
	crtc->scl->funcs->destroy(&crtc->scl);
scl_fail:
	return false;
}

struct controller *dal_controller_v_dce110_create(
		struct controller_init_data *init_data)
{
	struct controller *crtc;

	crtc = dal_alloc(sizeof(*crtc));

	if (!crtc)
		return NULL;

	if (construct(crtc, init_data))
		return crtc;

	dal_free(crtc);

	return NULL;
}
