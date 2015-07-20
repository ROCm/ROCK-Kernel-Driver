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

#include "cofunctional_mode_validator.h"

struct cofunctional_mode_query_validator;

struct ds_dispatch;
struct mode_query;

struct cofunctional_mode_query_validator *dal_cmqv_create(
	struct ds_dispatch *ds_dispatch);

void dal_cmqv_destroy(struct cofunctional_mode_query_validator **cmqv);

bool dal_cmqv_add_mode_query(
	struct cofunctional_mode_query_validator *validator,
	struct mode_query *mq);

void dal_cmqv_update_mode_query(
	struct cofunctional_mode_query_validator *validator,
	struct mode_query *mq);

bool dal_cmqv_is_cofunctional(
	struct cofunctional_mode_query_validator *validator);
