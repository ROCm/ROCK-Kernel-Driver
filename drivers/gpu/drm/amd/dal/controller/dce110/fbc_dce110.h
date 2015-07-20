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

#ifndef __DAL_FBC_DCE110_H__
#define __DAL_FBC_DCE110_H__

#include "../fbc.h"

struct fbc *dal_fbc_dce110_create(struct fbc_init_data *data);

bool dal_fbc_dce110_construct(struct fbc *fbc, struct fbc_init_data *data);

void dal_fbc_dce110_power_up_fbc(struct fbc *fbc);

void dal_fbc_dce110_enable_fbc(
	struct fbc *fbc,
	uint32_t paths_num,
	struct compr_addr_and_pitch_params *params);

bool dal_fbc_dce110_is_fbc_enabled_in_hw(
	struct fbc *fbc,
	enum controller_id *fbc_mapped_crtc_id);

bool dal_fbc_dce110_is_lpt_enabled_in_hw(struct fbc *fbc);

bool dal_fbc_dce110_get_required_compressed_surface_size(
	struct fbc *fbc,
	struct fbc_input_info *input_info,
	struct fbc_requested_compressed_size *size);

void dal_fbc_dce110_program_compressed_surface_address_and_pitch(
	struct fbc *fbc,
	struct compr_addr_and_pitch_params *params);

void dal_fbc_dce110_disable_lpt(struct fbc *fbc);

void dal_fbc_dce110_enable_lpt(
	struct fbc *fbc,
	uint32_t paths_num,
	enum controller_id cntl_id);

void dal_fbc_dce110_program_lpt_control(
	struct fbc *fbc,
	struct compr_addr_and_pitch_params *params);

void dal_fbc_dce110_wait_for_fbc_state_changed(
	struct fbc *fbc,
	bool enabled);

#endif
