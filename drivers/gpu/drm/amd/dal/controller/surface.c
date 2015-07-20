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

#include "surface.h"

bool dal_surface_construct(
		struct surface *sf,
		struct surface_init_data *init_data)
{
	if (!init_data)
		return false;

	sf->id = init_data->id;
	sf->ctx = init_data->dal_ctx;
	return true;
}

bool dal_surface_program_flip_and_addr(
	struct surface *sf,
	const struct plane_addr_flip_info *flip_info)
{
	sf->funcs->set_flip_control(
		sf,
		flip_info->flip_immediate == 1);

	sf->funcs->program_addr(sf,
		&flip_info->address_info.address);

	return true;
}

bool dal_surface_program_config(
	struct surface *sf,
	const struct plane_surface_config *configs)
{
	sf->funcs->enable(sf);

	sf->funcs->program_tiling(
		sf,
		&configs->tiling_info,
		configs->format);

	sf->funcs->program_size_and_rotation(
		sf,
		configs->rotation,
		&configs->plane_size);

	sf->funcs->program_pixel_format(
		sf,
		configs->format);

	return true;
}
