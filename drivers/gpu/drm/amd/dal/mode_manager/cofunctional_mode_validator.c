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
#include "cofunctional_mode_validator.h"

struct ds_dispatch;

bool dal_cofunctional_mode_validator_construct(
	struct cofunctional_mode_validator *cofunctional_mode_validator,
	const struct ds_dispatch *ds_dispatch)
{
	cofunctional_mode_validator->ds_dispatch = ds_dispatch;
	cofunctional_mode_validator->pinned_path_mode_count = 0;
	cofunctional_mode_validator->validate_req_vector = 0;
	cofunctional_mode_validator->set_mode_params = NULL;
	cofunctional_mode_validator->sync_capability = SYNC_CAP_STATE_UNKNOWN;
	cofunctional_mode_validator->pms = dal_pms_create();
	if (cofunctional_mode_validator->pms == NULL)
		return false;
	return true;
}

void dal_cofunctional_mode_validator_destruct(
	struct cofunctional_mode_validator *cofunctional_mode_validator)
{
	dal_pms_destroy(&cofunctional_mode_validator->pms);
}

static bool validate(struct cofunctional_mode_validator *cmv)
{
	uint32_t i;
	uint32_t mode_count = get_total_mode_count(cmv);

	if (cmv->set_mode_params == NULL) {
		uint32_t disp_idx[MAX_COFUNC_PATH];

		for (i = 0; i < mode_count; i++)
			disp_idx[i] = cofunctional_mode_validator_get_at(
						cmv, i)->display_path_index;

		cmv->set_mode_params =
				dal_ds_dispatch_create_resource_context(
						cmv->ds_dispatch,
						disp_idx,
						get_total_mode_count(cmv));
	}

	if (cmv->set_mode_params == NULL)
		return false;


	for (i = 0; i < mode_count; i++) {
		const struct path_mode *path_mode =
				cofunctional_mode_validator_get_at(cmv, i);

		if (!dal_set_mode_params_update_view_on_path(
				cmv->set_mode_params,
				path_mode->display_path_index,
				&path_mode->view))
			return false;

		if (!dal_set_mode_params_update_pixel_format_on_path(
				cmv->set_mode_params,
				path_mode->display_path_index,
				path_mode->pixel_format))
			return false;

		if (!dal_set_mode_params_update_mode_timing_on_path(
				cmv->set_mode_params,
				path_mode->display_path_index,
				path_mode->mode_timing,
				path_mode->view_3d_format))
			return false;

		if (!dal_set_mode_params_update_scaling_on_path(
				cmv->set_mode_params,
				path_mode->display_path_index,
				path_mode->scaling))
			return false;

		if (!dal_set_mode_params_update_tiling_mode_on_path(
				cmv->set_mode_params,
				path_mode->display_path_index,
				path_mode->tiling_mode))
			return false;
	}

	return dal_set_mode_params_is_path_mode_set_supported(
				cmv->set_mode_params);
}

bool dal_cofunctional_mode_validator_is_cofunctional(
		struct cofunctional_mode_validator *cmv)
{
	if (get_total_mode_count(cmv) > 1)
		if (cmv->validate_req_vector != 0)
			return validate(cmv);

	return true;
}

void dal_cofunctional_mode_validator_flag_guaranteed_at(
		struct cofunctional_mode_validator *validator,
		uint32_t index,
		bool guaranteed)
{
	ASSERT(index < validator->pms->count);

	if (guaranteed)
		validator->validate_req_vector &= ~(1 << index);
	else
		validator->validate_req_vector |= (1 << index);
}
