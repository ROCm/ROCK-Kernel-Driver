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

#include "col_man_gamma_dce110.h"

static void program_lut_gamma(
	struct grph_gamma *gg,
	const struct dev_c_lut16 *gamma,
	const struct gamma_parameters *params)
{
	/* TODO: add implementation */
}

static void destroy(struct grph_gamma **gg)
{
	dal_grph_gamma_destruct(*gg);

	dal_free(*gg);
	*gg = NULL;
}

static const struct grph_gamma_funcs funcs = {
	.program_lut_gamma = program_lut_gamma,
	.destroy = destroy
};

static bool construct(
	struct grph_gamma *gg,
	struct grph_gamma_init_data *data)
{
	if (!dal_grph_gamma_construct(gg, data))
		return false;

	gg->funcs = &funcs;

	return true;
}

struct grph_gamma *dal_col_man_grph_dce110_create(
	struct grph_gamma_init_data *data)
{
	struct grph_gamma *gg = dal_alloc(sizeof(*gg));

	if (!gg)
		return NULL;

	if (construct(gg, data))
		return gg;

	dal_free(gg);
	return NULL;
}
