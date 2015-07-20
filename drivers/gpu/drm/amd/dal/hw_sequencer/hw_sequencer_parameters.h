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

#ifndef __DAL_HW_SEQUENCER_PARAMETERS_H__
#define __DAL_HW_SEQUENCER_PARAMETERS_H__

#include "include/bandwidth_manager_types.h"
#include "include/display_clock_interface.h"
#include "include/scaler_types.h"
#include "include/clock_source_types.h"
#include "include/line_buffer_interface.h"

/* Forward declarations */
struct vector;

enum build_option {
	/* for set mode on first encoder that configured for ASIC signal */
	BUILD_OPTION_SET_MODE,
	/* for set mode on first encoder that configured for display signal */
	BUILD_OPTION_SET_MODE2,
	BUILD_OPTION_ENABLE_UPSTREAM,
	BUILD_OPTION_ENABLE_DOWNSTREAM,
	BUILD_OPTION_DISABLE,
	BUILD_OPTION_DISABLE2,
	BUILD_OPTION_STATIC_VALIDATE_UPSTREAM,
	BUILD_OPTION_STATIC_VALIDATE_DOWNSTREAM,
};

union hwss_build_params_mask {
	struct {
		/* 0-3 */
		uint32_t SCALING_TAPS:1;
		uint32_t PLL_SETTINGS:1;
		uint32_t MIN_CLOCKS:1;
		uint32_t WATERMARK:1;
		/* 4-7 */
		uint32_t BANDWIDTH:1;
		uint32_t LINE_BUFFER:1;
		uint32_t INCLUDE_CHROMA:1;/* For Video on Underlay */
		/* 8-31 */
		uint32_t FUTURE_USE:25;
	} bits;
	uint32_t all;
};

#define PARAMS_MASK_ALL 0xFFFFFFFF

/* Checks if any of the bits in the mask require Plane Configs information
 * in order to build the parameters. */
#define PARAMS_MASK_REQUIRES_PLAIN_CONFIGS(mask) \
	(mask.bits.SCALING_TAPS || mask.bits.LINE_BUFFER)

struct hwss_build_params {

	/* filled when hwss_build_params_mask.MIN_CLOCKS==1 */
	struct minimum_clocks_calculation_result min_clock_result;

	/* array, indexed by path_id and plane_id
	 * These arrays are structured as follows:
	 * XXX_params[Per-Path-Pointers to Planes Area][param_type*planes_num]
	 *
	 * For example: XXX_params[0] points to
	 * XXX_params[Per-Path-Pointers][param_type*num_of_planes_for_path_zero]
	 */
	struct scaling_tap_info **scaling_taps_params;
	struct lb_params_data **line_buffer_params;

	/* array, indexed by path_id */
	struct pll_settings *pll_settings_params;

	/* array, indexed (compressed) by params number */
	struct min_clock_params *min_clock_params;
	struct watermark_input_params *wm_input_params;
	struct bandwidth_params *bandwidth_params;

	uint32_t params_num;
	/* pointer to all allocated memory for all members of this struct */
	void *mem_block;
};

struct hwss_build_params *dal_hw_sequencer_prepare_path_parameters(
	struct hw_sequencer *hws,
	struct hw_path_mode_set *path_set,
	union hwss_build_params_mask params_mask,
	bool use_predefined_hw_state);

void dal_hw_sequencer_free_path_parameters(
	struct hwss_build_params *build_params);

#endif /*__DAL_HW_SEQUENCER_PARAMETERS_H__ */
