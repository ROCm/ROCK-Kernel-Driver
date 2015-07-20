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

#ifndef __DAL_COFUNCTIONAL_MODE_VALIDATOR_H__
#define __DAL_COFUNCTIONAL_MODE_VALIDATOR_H__

#include "include/set_mode_interface.h"

enum sync_cap_state {
	SYNC_CAP_STATE_UNKNOWN,
	SYNC_CAP_STATE_CAPABLE,
	SYNC_CAP_STATE_INCAPABLE
};

struct path_mode_set;

struct cofunctional_mode_validator {
	struct path_mode_set *pms;
	const struct ds_dispatch *ds_dispatch;
	uint32_t pinned_path_mode_count;
	uint32_t validate_req_vector;
	enum sync_cap_state sync_capability;
	struct set_mode_params *set_mode_params;
};
bool dal_cofunctional_mode_validator_construct(
		struct cofunctional_mode_validator *cofunctional_mode_validator,
		const struct ds_dispatch *ds_dispatch);

void dal_cofunctional_mode_validator_destruct(
	struct cofunctional_mode_validator *cofunctional_mode_validator);

bool dal_cofunctional_mode_validator_is_cofunctional(
		struct cofunctional_mode_validator *cmv);

static inline uint32_t get_total_mode_count(
		const struct cofunctional_mode_validator *cmv)
{
	return cmv->pinned_path_mode_count + dal_pms_get_path_mode_num(
							cmv->pms);
}

static inline struct path_mode *cofunctional_mode_validator_get_at(
		struct cofunctional_mode_validator *cmv, uint32_t i)
{
	return &cmv->pms->path_mode_set[i];
}

static inline bool dal_cofunctional_mode_validator_add_path_mode(
		struct cofunctional_mode_validator *cmv,
		struct path_mode *path_mode)
{
	return dal_pms_add_path_mode(cmv->pms, path_mode);
}

void dal_cofunctional_mode_validator_flag_guaranteed_at(
		struct cofunctional_mode_validator *validator,
		uint32_t index,
		bool guaranteed);

#endif /* __DAL_COFUNCTIONAL_MODE_VALIDATOR_H__ */
