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

#ifndef __DAL_AUX_ENGINE_DCE110_H__
#define __DAL_AUX_ENGINE_DCE110_H__

#include "../aux_engine.h"

struct aux_engine_dce110 {
	struct aux_engine base;
	struct {
		uint32_t aux_control;
		uint32_t aux_arb_control;
		uint32_t aux_sw_data;
		uint32_t aux_sw_control;
		uint32_t aux_interrupt_control;
		uint32_t aux_sw_status;
		uint32_t aux_gtc_sync_control;
		uint32_t aux_gtc_sync_status;
		uint32_t aux_gtc_sync_controller_status;
	} addr;
	uint32_t timeout_period;
};

struct aux_engine_dce110_init_data {
	uint32_t engine_id;
	uint32_t timeout_period;
	struct dc_context *ctx;
};

struct aux_engine *dal_aux_engine_dce110_create(
	const struct aux_engine_dce110_init_data *aux_init_data);

#endif
