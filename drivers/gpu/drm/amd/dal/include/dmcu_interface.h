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

#ifndef __DAL_DMCU_INTERFACE_H__
#define __DAL_DMCU_INTERFACE_H__

#include "grph_object_defs.h"
#include "dmcu_types.h"

/* Interface functions */

/* DMCU setup related interface functions */
struct dmcu *dal_dmcu_create(
	struct dmcu_init_data *init_data);

/* PSR feature related interface functions */
void dal_dmcu_psr_setup(
		struct dmcu *dmcu,
		struct dmcu_context *dmcu_context);

/* ABM feature related interface functions */
void dal_dmcu_abm_enable(
		struct dmcu *dmcu,
		enum controller_id controller_id,
		uint32_t vsync_rate_hz);

#endif /* __DAL_DMCU_INTERFACE_H__ */
