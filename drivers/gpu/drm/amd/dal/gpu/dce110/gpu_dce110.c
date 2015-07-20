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

#include "dce/dce_11_0_d.h"
#include "dce/dce_11_0_sh_mask.h"

#include "include/logger_interface.h"
#include "include/clock_source_interface.h"
#include "include/controller_interface.h"

#include "gpu_dce110.h"

#include "dc_clock_gating_dce110.h"
#include "bandwidth_manager_dce110.h"
#include "display_clock_dce110.h"

/* number of controllers supported */
const enum controller_id controller_array[4] = {
	CONTROLLER_ID_D0,
	CONTROLLER_ID_D1,
	CONTROLLER_ID_D2,
	CONTROLLER_ID_UNDERLAY0
};
/*****************************************************************************
 * macro definitions
 *****************************************************************************/

#define FROM_BASE_TO_DCE11(ptr) \
	container_of((ptr), struct gpu_dce110, base)

/*****************************************************************************
 * static functions
 *****************************************************************************/

static void destruct(struct gpu_dce110 *gpu)
{
	dal_gpu_destruct_base(&gpu->base);
}

static void destroy(struct gpu **base)
{
	struct gpu_dce110 *gpu;

	gpu = FROM_BASE_TO_DCE11(*base);
	destruct(gpu);
	dal_free(gpu);
	*base = NULL;
}

/*
 * create_controller
 *
 * @brief
 * create controllers
 *
 * @param
 * struct gpu *base - [in/out] provide and also store gpu info
 * uint32_t idx - [in] index
 *
 * @return
 * controller pointer
 */
static struct controller *create_controller(
	struct gpu *base,
	uint32_t idx)
{
	struct controller_init_data init;
	struct controller *controller = NULL;

	ASSERT(idx < ARRAY_SIZE(controller_array));

	if (idx >= ARRAY_SIZE(controller_array))
		return NULL;

	dal_memset(&init, 0, sizeof(struct controller_init_data));

	init.controller = controller_array[idx];
	init.paired_controller = CONTROLLER_ID_UNDEFINED;
	init.dal_context = base->dal_context;
	init.as = base->adapter_service;

	/*we never create the controller with id CONTROLLER_ID_UNDEFINED,
	 * but for pair controller it is valid configuration to have
	 * CONTROLLER_ID_UNDEFINED or any valid Id's.*/
	ASSERT(init.controller != CONTROLLER_ID_UNDEFINED);

	if (!base->filter) {
		base->filter = dal_scaler_filter_create();

		if (!base->filter) {
			dal_logger_write(base->dal_context->logger,
				LOG_MAJOR_ERROR,
				LOG_MINOR_COMPONENT_GPU,
				"%s: failed to create scaler filter!\n",
				__func__);
			return NULL;
		}
	}

	controller = dal_controller_create(&init);

	if (controller == NULL) {
		dal_logger_write(base->dal_context->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_GPU,
			"%s: dal_controller_create() failed!\n",
			__func__);
		return NULL;
	}

	dal_controller_set_display_clock(controller, base->disp_clk);
	dal_controller_set_bandwidth_manager(controller, base->bw_mgr);
	dal_controller_set_dc_clock_generator(controller, base->dc_clk_gen);
	dal_controller_set_scaler_filter(controller, base->filter);

	return controller;
}

static struct clock_source *create_clock_source(
	struct gpu *base,
	uint32_t idx)
{
	struct dal_context *dal_context = base->dal_context;
	struct clock_source *clk_source = NULL;
	enum clock_source_id clk_id = CLOCK_SOURCE_ID_UNDEFINED;
	struct clock_source_init_data clk_src_init_data = {};

	ASSERT(idx < base->num_of_clock_sources);

	if (idx >= base->num_of_clock_sources)
		return NULL;

	switch (idx) {
	case 0:
		clk_id = CLOCK_SOURCE_ID_PLL0;
		break;

	case 1:
		clk_id = CLOCK_SOURCE_ID_PLL1;
		break;

	case 2:
	{
		struct firmware_info fw_info;
		bool res;

		dal_memset(&fw_info, 0, sizeof(struct firmware_info));

		res = dal_adapter_service_get_firmware_info(
				base->adapter_service, &fw_info);

		/* If External Clock Source should be created do it, otherwise
		 * create VCE Clock Source for index 3 */
		if (res &&
			(fw_info.external_clock_source_frequency_for_dp != 0)) {
			/* DP pixel clock (via DTO) */
			clk_id = CLOCK_SOURCE_ID_EXTERNAL;
		} else
			clk_id = CLOCK_SOURCE_ID_VCE;
	}
	break;

	case 3:
		clk_id = CLOCK_SOURCE_ID_VCE;
		break;

	default:
		/* Unexpected Clock Source */
	break;
	}

	if (CLOCK_SOURCE_ID_UNDEFINED == clk_id) {
		GPU_ERROR("Unexpected Clock Source ID!\n");
		return NULL;
	}

	clk_src_init_data.as = base->adapter_service;
	clk_src_init_data.clk_src_id.id = clk_id;
	clk_src_init_data.clk_src_id.enum_id = ENUM_ID_1;
	clk_src_init_data.clk_src_id.type = OBJECT_TYPE_CLOCK_SOURCE;
	clk_src_init_data.clk_src_id.reserved = 0;
	clk_src_init_data.dal_ctx = base->dal_context;
	clk_source = dal_clock_source_create(&clk_src_init_data);

	if (clk_source == NULL)
		GPU_ERROR("Failed to create Clock Source!\n");

	return clk_source;
}

static void power_up(struct gpu *base)
{
	struct gpu_dce110 *gpu110 = FROM_BASE_TO_DCE11(base);

	dal_gpu_power_up_base(base);

	dal_dc_clock_gating_dce110_power_up(
			base->dal_context,
			gpu110->dc_clock_gating);
}

static void power_down(
	struct gpu *gpu,
	enum dal_video_power_state power_state)
{
	/* If entering S4, we only need to invalidate states. */
	if (DAL_VIDEO_POWER_HIBERNATE == power_state ||
		DAL_VIDEO_POWER_ULPS == power_state)
		dal_display_clock_invalid_clock_state(gpu->disp_clk);
	/* Otherwise power down necessary GPU elements. */
	else {
		/* If S1 or S3, then enter light sleep. */
		if (DAL_VIDEO_POWER_STANDBY == power_state ||
			DAL_VIDEO_POWER_SUSPEND == power_state) {
			dal_dc_clock_gating_dce110_power_down(gpu->dal_context);

			if (gpu->disp_clk != NULL)
				dal_display_clock_set_clock(gpu->disp_clk, 0);
		}

		if (gpu->disp_clk) {
			/* reset display engine clock state, like bypass mode */
			struct display_clock_state disp_clk_state =
				dal_display_clock_get_clock_state(
					gpu->disp_clk);
			disp_clk_state.DFS_BYPASS_ACTIVE = 0;
			dal_display_clock_set_clock_state(
				gpu->disp_clk,
				disp_clk_state);

			/* clean the bandwidthManager's internal DFSPassEnabled
			 * State: i.e. DfsBypassEnabled=0 */
			if (gpu->bw_mgr)
				dal_bandwidth_manager_update_dc_state(
					gpu->bw_mgr,
					0,
					false);
		}

		/* Disable GTC counter */
		if (gpu->dc_clk_gen)
			dal_dc_clock_generator_disable_gtc_counter(
				gpu->dc_clk_gen);
	}

	dal_gpu_power_down_base(gpu, power_state);
}

static const struct gpu_funcs funcs = {
	.destroy = destroy,
	.power_up = power_up,
	.power_down = power_down,
	.release_hw = NULL,
	.create_controller = create_controller,
	.create_clock_source = create_clock_source,
};

static bool construct(
		struct gpu_dce110 *gpu110,
		struct gpu_init_data *init_data)
{
	struct gpu *base;
	bool ret = true;

	base = &gpu110->base;

	/* base GPU construct*/
	if (!dal_gpu_construct_base(base, init_data))
		return false;

	base->funcs = &funcs;

	if (ret == true) {
		base->disp_clk = dal_display_clock_dce110_create(
				base->dal_context,
				base->adapter_service);

		if (base->disp_clk == NULL)
			ret = false;
	}

	if (ret == true) {
		base->bw_mgr = dal_bandwidth_manager_dce110_create(
				base->dal_context,
				base->adapter_service);
		if (base->bw_mgr == NULL)
			ret = false;
	}

	gpu110->dc_clock_gating = dal_adapter_service_is_feature_supported(
			FEATURE_LIGHT_SLEEP);
	return ret;
}

/*****************************************************************************
 * non-static functions
 *****************************************************************************/

struct gpu *dal_gpu_dce110_create(struct gpu_init_data *init_data)
{
	struct gpu_dce110 *gpu = dal_alloc(sizeof(struct gpu_dce110));

	if (gpu == NULL)
		return NULL;

	if (construct(gpu, init_data))
		return &gpu->base;
	dal_free(gpu);
	return NULL;

}
