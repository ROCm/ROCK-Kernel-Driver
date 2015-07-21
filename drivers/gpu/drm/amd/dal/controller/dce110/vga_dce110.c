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

#include "dce/dce_11_0_d.h"
#include "dce/dce_11_0_sh_mask.h"

#include "include/grph_object_id.h"
#include "include/adapter_service_interface.h"

#include "../vga.h"

static bool vga_dce110_construct(
	struct vga *vga,
	struct dal_context *ctx,
	struct adapter_service *as,
	enum controller_id id);

struct vga *dal_vga_dce110_create(
	struct adapter_service *as,
	struct dal_context *ctx,
	enum controller_id id)
{
	struct vga *vga;

	if (!as)
		return NULL;

	vga = dal_alloc(sizeof(struct vga));

	if (!vga)
		return NULL;

	if (vga_dce110_construct(vga, ctx, as, id))
		return vga;

	dal_free(vga);
	return NULL;
}

static void disable_vga(struct vga *vga);
static void destroy(struct vga **vga);
static const struct vga_funcs vga_funcs = {
	.destroy = destroy,
	.disable_vga = disable_vga,
};

static bool vga_dce110_construct(
	struct vga *vga,
	struct dal_context *ctx,
	struct adapter_service *as,
	enum controller_id id)
{
	vga->bp = dal_adapter_service_get_bios_parser(as);

	switch (id) {
	case CONTROLLER_ID_D0:
		vga->vga_control = mmD1VGA_CONTROL;
		break;

	case CONTROLLER_ID_D1:
		vga->vga_control = mmD2VGA_CONTROL;
		break;

	case CONTROLLER_ID_D2:
		vga->vga_control = mmD3VGA_CONTROL;
		break;

	case CONTROLLER_ID_D3:
		vga->vga_control = mmD4VGA_CONTROL;
		break;

	case CONTROLLER_ID_D4:
		vga->vga_control = mmD5VGA_CONTROL;
		break;

	case CONTROLLER_ID_D5:
		vga->vga_control = mmD6VGA_CONTROL;
		break;

	case CONTROLLER_ID_UNDERLAY0:
		vga->vga_control = 0;/* dal_reg_r/w will filter out addr=0 */
		break;

	default:
		ASSERT_CRITICAL(false); /* Invalid ControllerId */
		return false;
	}

	vga->ctx = ctx;
	vga->funcs = &vga_funcs;
	return true;
}

/**
 * disable_vga
 * Turn OFF VGA Mode and Timing  - DxVGA_CONTROL
 * VGA Mode and VGA Timing is used by VBIOS on CRT Monitors;
 */
static void disable_vga(struct vga *vga)
{
	uint32_t addr = vga->vga_control;
	uint32_t value = dal_read_reg(vga->ctx, addr);

	set_reg_field_value(value, 0, D1VGA_CONTROL, D1VGA_MODE_ENABLE);
	set_reg_field_value(value, 0, D1VGA_CONTROL, D1VGA_TIMING_SELECT);
	set_reg_field_value(
			value, 0, D1VGA_CONTROL, D1VGA_SYNC_POLARITY_SELECT);
	set_reg_field_value(value, 0, D1VGA_CONTROL, D1VGA_OVERSCAN_COLOR_EN);

	dal_write_reg(vga->ctx, addr, value);
}

static void destroy(struct vga **vga)
{
	dal_free(*vga);
	*vga = NULL;
}
