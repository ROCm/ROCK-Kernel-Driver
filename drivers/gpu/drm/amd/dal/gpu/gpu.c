/*
 * Copyright 2012-14 Advanced Micro Devices, Inc.
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

#include "include/controller_interface.h"

#if defined(CONFIG_DRM_AMD_DAL_DCE11_0)
#include "dce110/gpu_dce110.h"
#endif

#include "dc_clock_generator.h"

void dal_gpu_init_controller_info_table(
	struct controller_info *infos,
	uint32_t combinations_num,
	uint32_t controllers_num,
	const enum controller_id *ids)
{
	uint32_t i;
	uint32_t j;

	for (i = 0; i < combinations_num; ++i)
		for (j = 0; j < controllers_num; ++j) {
			uint32_t idx = i * controllers_num + j;

			infos[idx].id = ids[idx];
			infos[idx].HARVESTED = 0;
			infos[idx].CREATED = 0;
		}
}

bool dal_gpu_is_dc_harvested_out(
	struct controller_info *infos,
	uint32_t infos_num,
	enum controller_id id)
{
	uint32_t i;

	/*If found such controller Id then it is not harvested out
	 *because if the pipe is harvested out then we assign its id
	 *as CONTROLLER_ID_UNDEFINED*/

	for (i = 0; i < infos_num; ++i)
		if (infos[i].id == id)
			return false;

	return true;
}

bool dal_gpu_harvest_out_controller(
	struct controller_info *infos,
	uint32_t infos_num,
	enum controller_id id)
{
	uint32_t i;

	for (i = 0; i < infos_num; ++i)
		if (infos[i].id == id) {

			infos[i].id = CONTROLLER_ID_UNDEFINED;
			infos[i].HARVESTED = 1;

			return true;
		}

	return false;
}

bool dal_gpu_construct_base(struct gpu *gpu, struct gpu_init_data *init_data)
{
	struct adapter_service *as = init_data->adapter_service;

	ASSERT(as != NULL);

	/* base class virtual methods assignment */
	gpu->dal_context = init_data->dal_context;
	gpu->adapter_service = as;

	gpu->max_num_of_controllers = dal_adapter_service_get_controllers_num(
			as);

	gpu->num_of_dcfev = dal_adapter_service_get_num_of_underlays(as);

	gpu->num_of_clock_sources = dal_adapter_service_get_clock_sources_num(
			as);

	gpu->num_of_functional_controllers =
			dal_adapter_service_get_func_controllers_num(as);

	gpu->use_100_percent_lb_split = false;
	gpu->disp_clk = NULL;
	gpu->bw_mgr = NULL;
	gpu->dc_clk_gen = NULL;

	return true;
}

void dal_gpu_destruct_base(struct gpu *gpu)
{
	if (gpu->bw_mgr != NULL)
		dal_bandwidth_manager_destroy(&gpu->bw_mgr);

	if (gpu->disp_clk != NULL)
		dal_display_clock_destroy(&gpu->disp_clk);

	if (gpu->dc_clk_gen != NULL)
		dal_dc_clock_generator_destroy(&gpu->dc_clk_gen);

	if (gpu->filter != NULL)
		dal_scaler_filter_destroy(&gpu->filter);
}

void dal_gpu_power_up_base(struct gpu *gpu)
{
}

void dal_gpu_power_down_base(
	struct gpu *gpu,
	enum dal_video_power_state power_state)
{
	dal_gpu_release_hw_base(gpu);
}

void dal_gpu_release_hw_base(struct gpu *gpu)
{
	dal_bandwidth_manager_release_hw(gpu->bw_mgr);
}

struct gpu *dal_gpu_create(struct gpu_init_data *init_data)
{
	if (init_data->adapter_service == NULL)
		return NULL;

	switch (dal_adapter_service_get_dce_version(
		init_data->adapter_service)) {
#if defined(CONFIG_DRM_AMD_DAL_DCE11_0)
	case DCE_VERSION_11_0:
		return dal_gpu_dce110_create(init_data);
#endif
	default:
		return NULL;
	}

}

void dal_gpu_power_up(struct gpu *gpu)
{
	gpu->funcs->power_up(gpu);
}

void dal_gpu_power_down(struct gpu *gpu, enum dal_video_power_state power_state)
{
	gpu->funcs->power_down(gpu, power_state);
}

void dal_gpu_release_hw(struct gpu *gpu)
{
	gpu->funcs->release_hw(gpu);
}

uint32_t dal_gpu_get_num_of_functional_controllers(const struct gpu *gpu)
{
	return gpu->num_of_functional_controllers;
}

uint32_t dal_gpu_get_max_num_of_primary_controllers(const struct gpu *gpu)
{
	return gpu->max_num_of_controllers;
}

uint32_t dal_gpu_get_max_num_of_underlay_controllers(const struct gpu *gpu)
{
	return gpu->num_of_dcfev;
}

struct controller *dal_gpu_create_controller(struct gpu *gpu, uint32_t index)
{
	return gpu->funcs->create_controller(gpu, index);
}

uint32_t dal_gpu_get_num_of_clock_sources(const struct gpu *gpu)
{
	return gpu->num_of_clock_sources;
}

struct clock_source *dal_gpu_create_clock_source(
	struct gpu *gpu,
	uint32_t index)
{
	return gpu->funcs->create_clock_source(gpu, index);
}

void dal_gpu_destroy(struct gpu **gpu)
{
	if (gpu == NULL || *gpu == NULL) {
		BREAK_TO_DEBUGGER();
		return;
	}

	(*gpu)->funcs->destroy(gpu);

	*gpu = NULL;
}

/**************************************
 * GPU Clock Interface implementation *
 **************************************/
bool dal_gpu_init_static_clock_info(
	struct gpu *gpu,
	struct gpu_clock_info *gpu_clk_info)
{
	struct bandwidth_mgr_clk_info bw_clk_info;

	dal_memset(&bw_clk_info, 0, sizeof(bw_clk_info));

	bw_clk_info.min_sclk_khz = gpu_clk_info->min_sclk_khz;
	bw_clk_info.max_sclk_khz = gpu_clk_info->max_sclk_khz;

	bw_clk_info.min_mclk_khz = gpu_clk_info->min_mclk_khz;
	bw_clk_info.max_mclk_khz = gpu_clk_info->max_mclk_khz;

	dal_bandwidth_manager_set_static_clock_info(gpu->bw_mgr, &bw_clk_info);

	/* TODO: can we get this information from PPLib?
	 * Or, we should assume that for Validation we'll always use
	 * the highest possible state?*/
	dal_display_clock_store_max_clocks_state(gpu->disp_clk,
			CLOCKS_STATE_PERFORMANCE);

	return true;
}

bool dal_gpu_update_dynamic_clock_info(
	struct gpu *gpu,
	struct gpu_clock_info *gpu_clk_info)
{
	struct bandwidth_mgr_clk_info bw_clk_info;

	dal_memset(&bw_clk_info, 0, sizeof(bw_clk_info));

	bw_clk_info.min_sclk_khz = gpu_clk_info->min_sclk_khz;
	bw_clk_info.max_sclk_khz = gpu_clk_info->max_sclk_khz;
	bw_clk_info.min_mclk_khz = gpu_clk_info->min_mclk_khz;
	bw_clk_info.max_mclk_khz = gpu_clk_info->max_mclk_khz;

	dal_bandwidth_manager_set_dynamic_clock_info(gpu->bw_mgr, &bw_clk_info);

	return true;
}

void dal_gpu_get_static_clock_info(
	struct gpu *gpu,
	struct gpu_clock_info *gpu_clk_info_out)
{
	struct bandwidth_mgr_clk_info bw_clk_info;

	dal_memset(&bw_clk_info, 0, sizeof(bw_clk_info));

	dal_bandwidth_manager_get_static_clock_info(gpu->bw_mgr, &bw_clk_info);

	gpu_clk_info_out->min_sclk_khz = bw_clk_info.min_sclk_khz;
	gpu_clk_info_out->max_sclk_khz = bw_clk_info.max_sclk_khz;

	gpu_clk_info_out->min_mclk_khz = bw_clk_info.min_mclk_khz;
	gpu_clk_info_out->max_mclk_khz = bw_clk_info.max_mclk_khz;
}
