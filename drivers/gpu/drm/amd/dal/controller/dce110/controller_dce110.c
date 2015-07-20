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
#include "include/adapter_service_interface.h"

#include "controller_dce110.h"
#include "csc_dce110.h"
#include "timing_generator_dce110.h"
#include "formatter_dce110.h"
#include "vga_dce110.h"
#include "grph_gamma_dce110.h"
#include "scaler_dce110.h"
#include "pipe_control_dce110.h"
#include "line_buffer_dce110.h"
#include "surface_dce110.h"
#include "cursor_dce110.h"


/*****************************************************************************
 * functions
 *****************************************************************************/
static bool is_surface_supported(
	struct controller *crtc,
	const struct plane_config *pl_cfg)
{
	bool rc;

	/* TODO: check programming guide or DAL2 for types of supported
	 * surfaces. */
	/* Primary Pipe doesn't support 4:2:0 video surfaces (maybe more
	 * see the TODO). */
	switch (pl_cfg->config.format) {
	case SURFACE_PIXEL_FORMAT_VIDEO_420_YCbCr:
	case SURFACE_PIXEL_FORMAT_VIDEO_420_YCrCb:
		rc = false;
		break;
	default:
		rc = true;
		break;
	}

	return rc;
}

void dal_controller_dce110_destruct(struct controller_dce110 *controller110)
{
	dal_controller_base_destruct(&controller110->base);
}

static void destroy(struct controller **controller)
{
	struct controller_dce110 *controller110 =
		CONTROLLER_DCE110_FROM_BASE(*controller);

	dal_controller_dce110_destruct(controller110);

	dal_free(controller110);

	*controller = NULL;
}

bool dal_controller_dce110_construct(
	struct controller_dce110 *controller110,
	struct controller_init_data *init_data)
{
	struct controller *base = &controller110->base;

	if (false == dal_controller_base_construct(base, init_data))
		return false;

	switch (base->id) {
	case CONTROLLER_ID_D0:
	case CONTROLLER_ID_D1:
	case CONTROLLER_ID_D2:
		break;
	default:
		dal_controller_base_destruct(base);
		return false;
	}

	base->funcs.destroy = destroy;
	base->funcs.is_surface_supported = is_surface_supported;

	{
		struct grph_gamma_init_data gg_init_data = {0};

		gg_init_data.as = init_data->as;
		gg_init_data.ctx = base->dal_context;
		gg_init_data.id = base->id;
		base->grph_gamma = dal_grph_gamma_dce110_create(&gg_init_data);
	}

	if (!base->grph_gamma)
		goto grph_gamma_fail;

	{
		struct csc_init_data csc_init_data = {0};

		csc_init_data.id = base->id;
		csc_init_data.ctx = base->dal_context;
		csc_init_data.as = init_data->as;

		base->csc =
			dal_csc_dce110_create(&csc_init_data);
	}

	if (!base->csc)
		goto csc_fail;

	base->vga = dal_vga_dce110_create(init_data->as,
			base->dal_context,
			base->id);

	if (!base->vga)
		goto vga_fail;

	base->pc =
		dal_pipe_control_dce110_create(init_data->as,
			base->dal_context,
			base->id);

	if (!base->pc)
		goto pipe_control_fail;

	base->tg = dal_timing_generator_dce110_create(init_data->as,
				base->dal_context,
				base->id);
	if (!base->tg)
		goto tg_fail;

	{
		struct formatter_init_data fmt_init_data = {0};

		fmt_init_data.ctx = base->dal_context;
		fmt_init_data.id = base->id;
		base->fmt = dal_formatter_dce110_create(&fmt_init_data);
	}

	if (base->fmt == NULL)
		goto fmt_fail;

	{
		struct scaler_init_data scl_init_data = {0};

		scl_init_data.bp =
			dal_adapter_service_get_bios_parser(init_data->as);
		scl_init_data.dal_ctx = base->dal_context;
		scl_init_data.id = base->id;
		base->scl = dal_scaler_dce110_create(&scl_init_data);
	}

	if (base->scl == NULL)
		goto scl_fail;

	{

		struct line_buffer_init_data lb_init_data = {0};

		lb_init_data.dal_context = base->dal_context;
		lb_init_data.as = init_data->as;
		lb_init_data.id = init_data->controller;
		base->lb = dal_line_buffer_dce110_create(&lb_init_data);
	}

	if (!base->lb)
		goto line_buffer_fail;

	{
		struct surface_init_data surf_init_data = {0};

		surf_init_data.dal_ctx = base->dal_context;
		surf_init_data.id = base->id;
		base->surface = dal_surface_dce110_create(&surf_init_data);

	}

	if (base->surface == NULL)
		goto surface_fail;

	{
		struct cursor_init_data cur_init_data = {0};

		cur_init_data.dal_ctx = base->dal_context;
		cur_init_data.id = base->id;
		base->cursor = dal_cursor_dce110_create(&cur_init_data);

	}

	if (base->cursor == NULL)
		goto cursor_fail;



	return true;

cursor_fail:
	base->surface->funcs->destroy(&base->surface);
surface_fail:
	base->lb->funcs->destroy(&base->lb);
line_buffer_fail:
	base->scl->funcs->destroy(&base->scl);
scl_fail:
	base->fmt->funcs->destroy(&base->fmt);
fmt_fail:
	base->tg->funcs->destroy(&base->tg);
tg_fail:
	base->pc->funcs->destroy(&base->pc);
pipe_control_fail:
	base->vga->funcs->destroy(&base->vga);
vga_fail:
	base->csc->funcs->destroy(&base->csc);
csc_fail:
	base->grph_gamma->funcs->destroy(&base->grph_gamma);

grph_gamma_fail:
	return false;
}

struct controller *dal_controller_dce110_create(
		struct controller_init_data *init_data)
{
	struct controller_dce110 *controller110;

	controller110 = dal_alloc(sizeof(struct controller_dce110));

	if (!controller110)
		return NULL;

	if (dal_controller_dce110_construct(controller110, init_data))
		return &controller110->base;

	dal_free(controller110);

	return NULL;
}
