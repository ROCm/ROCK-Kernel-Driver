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

#ifndef __DAL_GPU_INTERFACE__
#define __DAL_GPU_INTERFACE__

#include "include/adapter_service_interface.h"
#include "include/grph_object_ctrl_defs.h"

enum gpu_clocks_state {
	GPU_CLOCKS_STATE_INVALID,
	GPU_CLOCKS_STATE_ULTRA_LOW,
	GPU_CLOCKS_STATE_LOW,
	GPU_CLOCKS_STATE_NOMINAL,
	GPU_CLOCKS_STATE_PERFORMANCE
};

struct gpu_clock_info {
	uint32_t min_sclk_khz;
	uint32_t max_sclk_khz;

	uint32_t min_mclk_khz;
	uint32_t max_mclk_khz;

	uint32_t min_dclk_khz;
	uint32_t max_dclk_khz;
};

struct gpu;
struct irq_manager;

struct gpu_init_data {
	struct dc_context *ctx;
	struct adapter_service *adapter_service;
	struct irq_manager *irq_manager;
};

struct gpu *dal_gpu_create(struct gpu_init_data *init_data);
void dal_gpu_destroy(struct gpu **);

void dal_gpu_power_up(struct gpu *);
void dal_gpu_power_down(
		struct gpu *gpu,
		enum dc_video_power_state power_state);
void dal_gpu_light_sleep_vbios_wa(struct gpu *gpu, bool enable);
void dal_gpu_release_hw(struct gpu *gpu);

uint32_t dal_gpu_get_num_of_functional_controllers(const struct gpu *gpu);
uint32_t dal_gpu_get_max_num_of_primary_controllers(const struct gpu *gpu);
uint32_t dal_gpu_get_max_num_of_underlay_controllers(const struct gpu *gpu);
struct controller *dal_gpu_create_controller(
				struct gpu *gpu,
				uint32_t index);
uint32_t dal_gpu_get_num_of_clock_sources(const struct gpu *gpu);
struct clock_source *dal_gpu_create_clock_source(
	 struct gpu *gpu,
	uint32_t index);

/* gpu_clock_interface implementation */
bool dal_gpu_init_static_clock_info(struct gpu *gpu,
		struct gpu_clock_info *gpu_clk_info);

bool dal_gpu_update_dynamic_clock_info(struct gpu *gpu,
		struct gpu_clock_info *gpu_clk_info);

void dal_gpu_get_static_clock_info(struct gpu *gpu,
		struct gpu_clock_info *gpu_clk_info);

#endif
