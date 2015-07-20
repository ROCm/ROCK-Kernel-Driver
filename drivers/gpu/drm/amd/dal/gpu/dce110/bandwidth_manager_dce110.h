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

#ifndef __DAL_BANDWIDTH_MANAGER_DCE110_H__
#define __DAL_BANDWIDTH_MANAGER_DCE110_H__

#include "include/bandwidth_manager_interface.h"
#include "gpu/bandwidth_manager.h"

#define BM110_MAX_NUM_OF_PIPES 3

struct bandwidth_manager_dce110 {
	struct bandwidth_manager base;

	bool blender_underflow_interrupt;
	uint32_t supported_stutter_mode;
	uint32_t controllers_num;
	uint32_t underlays_num;
	uint32_t pipes_num;
	uint32_t enhanced_stutter_watermark_high_clks[BM110_MAX_NUM_OF_PIPES];
	uint32_t enhanced_stutter_watermark_low_clks[BM110_MAX_NUM_OF_PIPES];
	bool mc_power_gating_enabled;
	bool limit_outstanding_chunk_requests;
	uint32_t dmif_request_buffer_size;
	uint32_t cursor_width_in_pixels;
	uint32_t cursor_dcp_buffer_size;
	uint32_t program_stutter_mode;
	uint32_t reconnection_latency_for_enhanced_stutter;
	uint32_t display_dram_allocation;

	uint32_t lpt_bandwidth_efficiency;
	bool allow_watermark_adjustment;

	bool maximize_nbp_marks;

	/* Hardware Latencies */
	uint32_t reconnection_latency_for_urgency;

	/*
	 To do: define integratedInfo table format for Trinity
	 It will be simlar to previous Fusion implementation
	 */

	uint32_t gmc_restore_reset_time;
	uint32_t minimum_n_clk;
	uint32_t ddr_dll_power_up_time;
	uint32_t ddr_pll_power_up_time;
	uint32_t boot_up_uma_clk_khz;
	uint32_t system_config;

	/*private members*/
	struct adapter_service *as;
};

#define BM110_FROM_BM_BASE(bm_base) \
	container_of(bm_base, struct bandwidth_manager_dce110, base)

void dal_bandwidth_manager_dce110_program_pix_dur(
		struct bandwidth_manager *base,
		enum controller_id ctrl_id,
		uint32_t pix_clk_khz);

bool dal_bandwidth_manager_dce110_construct(
	struct bandwidth_manager_dce110 *bm_dce110,
	struct dal_context *dal_ctx,
	struct adapter_service *as);

void dal_bandwidth_manager_dce110_destruct(
	struct bandwidth_manager_dce110 *bm_dce110);

struct bandwidth_manager *dal_bandwidth_manager_dce110_create(
	struct dal_context *dal_ctx,
	struct adapter_service *as);

#endif /* __DAL_BANDWIDTH_MANAGER_DCE110_H__ */
