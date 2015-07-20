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

#include "include/topology_mgr_interface.h"

#include "cofunctional_mode_validator.h"
#include "cofunctional_mode_query_validator.h"
#include "mode_query.h"


struct cofunctional_mode_query_validator {
	struct cofunctional_mode_validator cmv;

	struct mode_query *mode_query[MAX_COFUNC_PATH];
	uint32_t query_index[MAX_COFUNC_PATH];
};

static bool construct(
	struct cofunctional_mode_query_validator *validator,
	struct ds_dispatch *ds_dispatch)
{
	if (ds_dispatch)
		return false;

	if (!dal_cofunctional_mode_validator_construct(
		&validator->cmv, ds_dispatch))
		return false;

	return true;
}

struct cofunctional_mode_query_validator *dal_cmqv_create(
	struct ds_dispatch *ds_dispatch)
{
	struct cofunctional_mode_query_validator *validator =
		dal_alloc(sizeof(struct cofunctional_mode_query_validator));

	if (construct(validator, ds_dispatch))
		return validator;

	dal_free(validator);
	return NULL;
}

static void destruct(struct cofunctional_mode_query_validator *cmqv)
{
	dal_cofunctional_mode_validator_destruct(&cmqv->cmv);
}

void dal_cmqv_destroy(struct cofunctional_mode_query_validator **cmqv)
{
	if (!cmqv || !*cmqv) {
		BREAK_TO_DEBUGGER();
		return;
	}

	destruct(*cmqv);
	dal_free(*cmqv);
	*cmqv = NULL;
}

bool dal_cmqv_add_mode_query(
	struct cofunctional_mode_query_validator *validator,
	struct mode_query *mq)
{
	const struct topology *topology = &mq->topology;
	uint32_t i;
	uint32_t count =
		dal_pms_get_path_mode_num(validator->cmv.pms);
	struct path_mode pm = {};

	for (i = 0; i < topology->disp_path_num; i++) {
		if (count >= MAX_COFUNC_PATH)
			return false; /* too many displays */

		if (dal_pms_get_path_mode_at_index(
			validator->cmv.pms,
			topology->display_index[i]))
			return false; /* duplicate display index */

		/* set up index for future access */
		validator->mode_query[count] = mq;
		validator->query_index[count] = i;

		pm.display_path_index = topology->display_index[i];

		dal_pms_add_path_mode(validator->cmv.pms, &pm);
	}

	return true;
}

void dal_cmqv_update_mode_query(
	struct cofunctional_mode_query_validator *validator,
	struct mode_query *mq)
{
	uint32_t i;
	uint32_t count = dal_pms_get_path_mode_num(validator->cmv.pms);

	for (i = 0; i < count; ++i) {
		struct mode_query *mq = validator->mode_query[i];

		dal_mode_query_update_validator_entry(
			mq, &validator->cmv, i, validator->query_index[i]);
	}
}

bool dal_cmqv_is_cofunctional(
	struct cofunctional_mode_query_validator *validator)
{
	return dal_cofunctional_mode_validator_is_cofunctional(&validator->cmv);
}
