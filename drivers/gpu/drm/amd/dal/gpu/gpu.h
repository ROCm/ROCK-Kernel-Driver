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

#ifndef __DAL_GPU_H__
#define __DAL_GPU_H__

#include "include/gpu_interface.h"
#include "include/bandwidth_manager_interface.h"
#include "include/line_buffer_interface.h"
#include "include/display_clock_interface.h"
#include "include/dc_clock_generator_interface.h"

/*forward declaration*/
struct line_buffer;

struct gpu_funcs {
	void (*destroy)(struct gpu **);
	void (*power_up)(struct gpu *);
	void (*power_down)(
			struct gpu *gpu,
			enum dal_video_power_state power_state);
	void (*release_hw)(struct gpu *gpu);
	struct controller *(*create_controller)(
					struct gpu *gpu,
					uint32_t index);
	struct clock_source *(*create_clock_source)(
			struct gpu *gpu,
			uint32_t index);
};

struct gpu {
	struct dal_context *dal_context;
	const struct gpu_funcs *funcs;
	/*external structures*/
	struct adapter_service *adapter_service;
	struct irq_manager *irq_mgr;

	/*internal structures*/
	struct display_clock *disp_clk;
	struct dc_clock_generator *dc_clk_gen;
	struct bandwidth_manager *bw_mgr;

	struct scaler_filter *filter;

	/* non-underlay (aka Primary) controllers */
	uint32_t max_num_of_controllers;
	uint32_t num_of_functional_controllers;
	uint32_t num_of_clock_sources;
	bool use_100_percent_lb_split;
	/* underlay controllers */
	uint32_t num_of_dcfev;
};

bool dal_gpu_construct_base(
		struct gpu *gpu,
		struct gpu_init_data *init_data);

void dal_gpu_destruct_base(struct gpu *gpu);
void dal_gpu_release_hw_base(struct gpu *gpu);
void dal_gpu_power_up_base(struct gpu *gpu);
void dal_gpu_power_down_base(
	struct gpu *gpu,
	enum dal_video_power_state power_state);

struct controller_info {
	enum controller_id id;
	uint32_t HARVESTED:1;
	uint32_t CREATED:1;
};

struct controller_table_info {

};

void dal_gpu_init_controller_info_table(
	struct controller_info *infos,
	uint32_t combinations_num,
	uint32_t controllers_num,
	const enum controller_id *ids);
bool dal_gpu_is_dc_harvested_out(
	struct controller_info *info,
	uint32_t infos_num,
	enum controller_id id);
bool dal_gpu_harvest_out_controller(
	struct controller_info *info,
	uint32_t infos_num,
	enum controller_id id);

/*****************************************************************************
 * Macro definitions for Logger usage in GPU.
 *****************************************************************************/
#define GPU_ERROR(...) dal_logger_write(dal_context->logger, \
	LOG_MAJOR_ERROR, LOG_MINOR_COMPONENT_GPU, __VA_ARGS__)

#define GPU_WARNING(...) dal_logger_write(dal_context->logger, \
	LOG_MAJOR_WARNING, LOG_MINOR_COMPONENT_GPU, __VA_ARGS__)

#endif
