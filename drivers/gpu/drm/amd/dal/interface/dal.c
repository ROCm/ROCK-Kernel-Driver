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

#include "include/dal_interface.h"
#include "include/adapter_service_interface.h"
#include "include/timing_service_interface.h"
#include "include/topology_mgr_interface.h"
#include "include/gpu_interface.h"
#include "include/timing_list_query_interface.h"
#include "include/display_service_interface.h"
#include "include/set_mode_interface.h"
#include "include/adjustment_interface.h"
#include "include/display_path_interface.h"
#include "include/controller_interface.h"
#include "include/hw_sequencer_interface.h"
#include "include/logger_interface.h"
#include "include/mode_manager_interface.h"
#include "include/dcs_interface.h"
#include "include/irq_service_interface.h"

/*
* DAL - Display Abstraction Layer
* represents the main high level object that provides
* abstracted display services. One such object needs to
* be created per GPU ASIC.
*/

struct dal {
	struct dal_init_data init_data;
	struct dal_context dal_context;
	struct adapter_service *adapter_srv;
	struct timing_service *timing_srv;
	struct topology_mgr *topology_mgr;
	struct display_service *display_service;
	struct hw_sequencer *hws;
	struct mode_manager *mm;
	struct irq_service *irqs;
};

/* debugging macro definitions */
#define DAL_IF_TRACE()	\
	dal_logger_write(dal_context->logger, \
		LOG_MAJOR_INTERFACE_TRACE, \
		LOG_MINOR_COMPONENT_DAL_INTERFACE, \
		"DAL_IF_TRACE: %s()\n", __func__)

#define DAL_IF_NOT_IMPLEMENTED() \
	DAL_LOGGER_NOT_IMPL(LOG_MINOR_COMPONENT_DAL_INTERFACE, \
			"DAL_IF:%s()\n", __func__)

#define DAL_IF_ERROR(...) \
	dal_logger_write(dal_context->logger, \
		LOG_MAJOR_ERROR, \
		LOG_MINOR_COMPONENT_DAL_INTERFACE, \
		__VA_ARGS__)

enum {
	MAX_PLANE_NUM = 4
};
/******************************************************************************
	Declarations for Timing List Query.
******************************************************************************/

/******************************************************************************
	Prototypes of private functions.
******************************************************************************/
static bool dal_construct(struct dal_init_data *init,
		struct dal *dal);
static void dal_destruct(struct dal *dal);
static struct adapter_service *create_as(
		struct dal *dal);
static bool dal_enable(struct dal *dal);

/******************************************************************************
	Implementation of the API provided by DAL.
******************************************************************************/
struct dal *dal_create(struct dal_init_data *init)
{
	struct dal *dal_instance = NULL;

	/* DAL_IF_TRACE();*/

	dal_instance = dal_alloc(sizeof(*dal_instance));

	if (NULL == dal_instance) {
		/*DAL_IF_ERROR("%s: failed to allocate %ld bytes!\n",
			__func__, sizeof(*dal_instance)); */
		return NULL;
	}

	if (!dal_construct(init, dal_instance)) {
		dal_free(dal_instance);
		return NULL;
	}

	return dal_instance;
}

void dal_destroy(struct dal **dal)
{
	struct dal_context *dal_context = NULL;

	if (dal == NULL || *dal == NULL)
		return;
	dal_context = &((*dal)->dal_context);

	DAL_IF_TRACE();

	/***************************************
	 * deallocate all subcomponents of DAL
	 ***************************************/
	dal_destruct(*dal);

	/*************************
	 * deallocate DAL itself
	 *************************/
	dal_free(*dal);
	*dal = NULL;
}

static bool dal_construct(struct dal_init_data *init,
		struct dal *dal_instance)
{
	struct dal_context *dal_context = &dal_instance->dal_context;

	dal_instance->init_data = *init;
	dal_instance->dal_context.driver_context = init->driver;
	dal_instance->dal_context.cgs_device = init->cgs_device;

	/* Logger */

	dal_instance->dal_context.logger = dal_logger_create();

	if (!dal_instance->dal_context.logger) {
		/* can *not* call logger. call base driver 'print error' */
		dal_error("%s: failed to create Logger!\n", __func__);
		goto logger_fail;
	}

	/* Adapter Service */

	dal_instance->adapter_srv = create_as(dal_instance);

	if (!dal_instance->adapter_srv) {
		DAL_IF_ERROR("%s: create_as() failed!\n", __func__);
		goto as_fail;
	}

	/* Initialise HW controlled by Adapter Service */
	if (false == dal_adapter_service_initialize_hw_data(
			dal_instance->adapter_srv)) {
		DAL_IF_ERROR("%s: dal_adapter_service_initialize_hw_data() failed!\n",
				__func__);
		/* Note that AS exist, so have to destroy it.*/
		goto ts_fail;
	}

	/* Timing Service */
	dal_instance->timing_srv =
			dal_timing_service_create(
					&dal_instance->dal_context,
					true);

	if (!dal_instance->timing_srv) {
		DAL_IF_ERROR(
			"%s: dal_timing_service_create() failed!\n",
			__func__);
		goto ts_fail;
	}

	{
		struct hws_init_data hws_init_data;

		hws_init_data.as = dal_instance->adapter_srv;
		hws_init_data.dal_context = &dal_instance->dal_context;

		dal_instance->hws = dal_hw_sequencer_create(&hws_init_data);

		if (!dal_instance->hws) {
			DAL_IF_ERROR("%s: dal_hw_sequencer_create() failed!\n",
					__func__);
			goto hws_fail;
		}
	}

	{
		struct mode_manager_init_data init_data;

		init_data.as = dal_instance->adapter_srv;
		init_data.default_modes =
			dal_timing_service_get_default_mode_list(
				dal_instance->timing_srv);
		init_data.dal_context = &dal_instance->dal_context;
		dal_instance->mm = dal_mode_manager_create(&init_data);

		if (!dal_instance->mm)
			goto mm_fail;
	}

	{
		struct topology_mgr_init_data tm_init_data;

		dal_memset(&tm_init_data, 0, sizeof(tm_init_data));

		/* Topology Manager */
		tm_init_data.dal_context = &dal_instance->dal_context;
		tm_init_data.adapter_srv = dal_instance->adapter_srv;
		tm_init_data.timing_srv = dal_instance->timing_srv;
		tm_init_data.hwss_srvr = dal_instance->hws;
		tm_init_data.mm = dal_instance->mm;

		dal_instance->topology_mgr = dal_tm_create(&tm_init_data);

		if (!dal_instance->topology_mgr) {
			DAL_IF_ERROR("%s: dal_tm_create() failed!\n", __func__);
			goto tm_fail;
		}

		/* TODO: Set embedded display index in AS for backlight ctrl?*/
	}

	{
		struct ds_init_data ds_init_data = { 0 };

		ds_init_data.dal_context = &dal_instance->dal_context;
		ds_init_data.as = dal_instance->adapter_srv;
		ds_init_data.ts = dal_instance->timing_srv;
		ds_init_data.tm = dal_instance->topology_mgr;
		ds_init_data.hwss = dal_instance->hws;

		ds_init_data.view_port_alignment.x_start_alignment =
			init->vp_alignment.x_start_alignment;
		ds_init_data.view_port_alignment.x_width_size_alignment =
			init->vp_alignment.x_width_size_alignment;
		ds_init_data.view_port_alignment.y_height_size_alignment =
			init->vp_alignment.y_height_size_alignment;
		ds_init_data.view_port_alignment.y_start_alignment =
			init->vp_alignment.y_start_alignment;

		dal_instance->display_service =
			dal_display_service_create(&ds_init_data);

		if (!dal_instance->display_service) {
			DAL_IF_ERROR(
				"%s: dal_display_service_create() failed!\n",
				__func__);
			goto ds_fail;
		}
	}

	{
		struct irq_service_init_data init_data;

		init_data.ctx = dal_context;
		dal_instance->irqs =
			dal_irq_service_create(
				dal_adapter_service_get_dce_version(
					dal_instance->adapter_srv),
				&init_data);

		if (!dal_instance->irqs)
			goto irqs_fail;
	}

	dal_mode_manager_set_ds_dispatch(
		dal_instance->mm,
		dal_display_service_get_set_mode_interface(
			dal_instance->display_service));

	if (!dal_timing_service_initialize_filters(
		dal_instance->timing_srv,
		dal_display_service_get_set_mode_interface(
			dal_instance->display_service)))
		goto fail;

	/* try to "enable" DAL */
	if (dal_enable(dal_instance))
		return true;

fail:
	dal_irq_service_destroy(&dal_instance->irqs);
irqs_fail:
	dal_display_service_destroy(&dal_instance->display_service);
ds_fail:
	dal_tm_destroy(&dal_instance->topology_mgr);
tm_fail:
	dal_mode_manager_destroy(&dal_instance->mm);
mm_fail:
	dal_hw_sequencer_destroy(&dal_instance->hws);
hws_fail:
	dal_timing_service_destroy(&dal_instance->timing_srv);
ts_fail:
	dal_adapter_service_destroy(&dal_instance->adapter_srv);
as_fail:
	dal_logger_destroy(&dal_instance->dal_context.logger);
logger_fail:

	return false;
}

static void release_hw(struct dal *dal)
{
	dal_tm_release_hw(dal->topology_mgr);
}

static void dal_destruct(struct dal *dal)
{
	release_hw(dal);

	dal_irq_service_destroy(&dal->irqs);

	dal_adapter_service_destroy(&dal->adapter_srv);

	dal_timing_service_destroy(&dal->timing_srv);

	dal_hw_sequencer_destroy(&dal->hws);

	dal_display_service_destroy(&dal->display_service);

	dal_tm_destroy(&dal->topology_mgr);

	dal_logger_destroy(&dal->dal_context.logger);

	dal_mode_manager_destroy(&dal->mm);
}

/**
 *****************************************************************************
 *  Function: dal_enable
 *
 *  @brief
 *	1. Enable DAL instance and initialise the display HW.
 *	2. Do initial detection.
 *
 *  @return
 *	If DAL instance is enabled successfully, return true,
 *	otherwise, return false.
 *
 *****************************************************************************
 */
static bool dal_enable(struct dal *dal)
{
	enum tm_result tm_rc;
	struct topology_mgr *tm_mgr = dal->topology_mgr;
	struct dal_context *dal_context = &dal->dal_context;

	DAL_IF_TRACE();

	/* TODO: call Display Service to notify all subcomponents
	 * about initial display configuration. */

	/* call TM to initialise HW */
	dal_tm_set_current_power_state(tm_mgr, DAL_VIDEO_POWER_UNSPECIFIED);

	tm_rc = dal_tm_init_hw(tm_mgr);

	if (TM_RESULT_SUCCESS == tm_rc) {
		/*blank all pipe.--> stop memory request to frame buffer*/
		dal_tm_disable_all_dcp_pipes(tm_mgr);

		/* no error */
		/* call TM to detect connected displays */
		dal_tm_do_initial_detection(tm_mgr);

		/* Only if *everything* is initialised successfully, register
		 * to receive HPD interrupts. */
		tm_rc = dal_tm_register_for_display_detection_interrupt(tm_mgr);
	}

	if (TM_RESULT_SUCCESS != tm_rc)
		/* error occurred */
		return false;

	return true;
}

/*
 * Enable the DAL instance after resume
 *
 * Enables instance of the DAL specific to an adapter after resume.
 * The function sets up the display mapping based on appropriate boot-up
 * behavior.
 */
void dal_resume(struct dal *dal)
{
	struct dal_context *dal_context = &dal->dal_context;

	DAL_IF_TRACE();

	dal_tm_do_complete_detection(dal->topology_mgr,
			DETECTION_METHOD_DESTRUCTIVE,
			false);
}

/* HW Capability queries */
uint32_t dal_get_controllers_number(struct dal *dal)
{
	struct dal_context *dal_context = &dal->dal_context;

	DAL_IF_TRACE();

	return dal_tm_get_num_functional_controllers(dal->topology_mgr);
}

uint32_t dal_get_connected_targets_vector(struct dal *dal)
{
	struct topology_mgr *tm_mgr;
	uint32_t connected_displays;
	uint32_t ind;
	uint32_t disp_path_num;
	struct display_path *disp_path;

	/* For DAL disabled support, report 1 pipe for fake display */
	if (dal == NULL)
		return 1;

	tm_mgr = dal->topology_mgr;

	disp_path_num = dal_tm_get_num_display_paths(tm_mgr, false);

	connected_displays = 0;

	for (ind = 0; ind < disp_path_num; ind++) {
		disp_path = dal_tm_display_index_to_display_path(tm_mgr, ind);

		if (dal_display_path_is_target_connected(disp_path))
			connected_displays |= (1 << ind);
	} /* for() */

	return connected_displays;
}

uint32_t dal_get_cofunctional_targets_number(struct dal *dal)
{
	struct dal_context *dal_context = &dal->dal_context;

	DAL_IF_TRACE();

	return dal_tm_max_num_cofunctional_targets(dal->topology_mgr);
}

/* Return a bitvector of *all* Display Paths. */
uint32_t dal_get_supported_displays_vector(struct dal *dal)
{
	uint32_t disp_path_num;
	uint32_t supported_disp = 0;
	uint32_t ind;

	/* For DAL disabled support, report 1 pipe for fake display */
	if (dal == NULL)
		return 1;

	disp_path_num = dal_tm_get_num_display_paths(dal->topology_mgr,
			false);

	for (ind = 0; ind < disp_path_num; ind++)
		supported_disp |= dal_get_display_vector_by_index(dal, ind);

	return supported_disp;
}

uint32_t dal_get_a_display_index_by_type(struct dal *dal,
					 uint32_t display_type)
{
	struct dal_context *dal_context = &dal->dal_context;

	DAL_IF_NOT_IMPLEMENTED();

	/* TODO */
	return 0;
}

enum signal_type dal_get_display_signal(
	struct dal *dal,
	uint32_t display_index)
{
	struct display_path *path =
		dal_tm_display_index_to_display_path(
			dal->topology_mgr,
			display_index);

	if (!path)
		return SIGNAL_TYPE_NONE;

	return dal_display_path_get_active_signal(path, SINK_LINK_INDEX);
}

const uint8_t *dal_get_display_edid(
	struct dal *dal,
	uint32_t display_index,
	uint32_t *buff_size)
{
	struct dcs *dcs;
	struct display_path *path =
		dal_tm_display_index_to_display_path(
			dal->topology_mgr,
			display_index);

	if (!path)
		return NULL;

	dcs = dal_display_path_get_dcs(path);

	if (!dcs)
		return NULL;

	return dal_dcs_get_edid_raw_data(dcs, buff_size);
}

void dal_get_screen_info(
	struct dal *dal,
	uint32_t display_index,
	struct edid_screen_info *screen_info)
{
	struct dcs *dcs;
	struct display_path *path =
		dal_tm_display_index_to_display_path(
			dal->topology_mgr,
			display_index);

	if (!screen_info)
		return;

	if (!path)
		return;

	dcs = dal_display_path_get_dcs(path);

	if (!dcs)
		return;

	dal_dcs_get_screen_info(dcs, screen_info);
}

uint32_t dal_get_display_vector_by_index(struct dal *dal,
		uint32_t display_index)
{
	uint32_t disp_path_num = 0;
	uint32_t return_val;

	disp_path_num = dal_tm_get_num_display_paths(dal->topology_mgr, false);

	if (display_index >= disp_path_num)
		return_val = 0;
	else
		return_val = (1 << display_index);

	return return_val;
}

/* TODO: implement later
bool dal_get_display_output_descriptor (struct dal *dal,
		uint32_t display_index,
		struct dal_display_output_descriptor *display_descriptor);
*/

void dal_pin_active_path_modes(
	struct dal *dal,
	void *param,
	uint32_t display_index,
	void (*func)(void *, const struct path_mode *))
{
	struct ds_dispatch *ds_dispatch;
	struct dal_context *dal_context = &dal->dal_context;

	DAL_IF_TRACE();

	ds_dispatch = dal_display_service_get_set_mode_interface(
		dal->display_service);

	if (NULL == ds_dispatch) {
		DAL_IF_ERROR("%s: DS Dispatch Interface is NULL!\n", __func__);
		return;
	}


	dal_ds_dispatch_pin_active_path_modes(
		ds_dispatch,
		param,
		display_index,
		func);
}

static void dump_path_mode(struct dal *dal,
		uint32_t pm_num,
		const struct path_mode *pm)
{
	struct dal_context *dal_context = &dal->dal_context;

	dal_logger_write(dal_context->logger, LOG_MAJOR_INTERFACE_TRACE,
		LOG_MINOR_COMPONENT_DAL_INTERFACE,
		"PM[%02d]: PathIndex=%02d: view.height: %d, view.width: %d\n",
		pm_num, pm->display_path_index,
		pm->view.height, pm->view.width);
}

static void dump_path_mode_set(struct dal *dal,
		const struct path_mode_set *pms)
{
	uint32_t i;
	struct dal_context *dal_context = &dal->dal_context;

	if (false == dal_logger_should_log(dal_context->logger,
			LOG_MAJOR_INTERFACE_TRACE,
			LOG_MINOR_COMPONENT_DAL_INTERFACE))
		return;

	dal_logger_write(dal_context->logger, LOG_MAJOR_INTERFACE_TRACE,
		LOG_MINOR_COMPONENT_DAL_INTERFACE,
		"SetMode: Number of Path Modes in Set: %d\n", pms->count);

	for (i = 0; i < pms->count; i++)
		dump_path_mode(dal, i, &pms->path_mode_set[i]);
}

/* Mode Set */
bool dal_set_path_mode(struct dal *dal,
		const struct path_mode_set *pms)
{
	struct ds_dispatch *ds_dispatch;
	struct dal_context *dal_context = &dal->dal_context;

	ds_dispatch =
		dal_display_service_get_set_mode_interface(
			dal->display_service);

	if (!ds_dispatch)
		return false;

	dump_path_mode_set(dal, pms);

	if (DS_SUCCESS == dal_ds_dispatch_set_mode(ds_dispatch, pms))
		return true;

	DAL_IF_ERROR(
		"%s: dal_set_mode_interface_set_mode() failed!\n",
		__func__);

	return false;
}

bool dal_reset_path_mode(struct dal *dal,
		const uint32_t displays_num,
		const uint32_t *display_indexes)
{
	struct ds_dispatch *ds_dispatch;
	struct dal_context *dal_context = &dal->dal_context;

	ds_dispatch =
		dal_display_service_get_reset_mode_interface(
			dal->display_service);

	if (!ds_dispatch)
		return false;

	if (DS_SUCCESS == dal_ds_dispatch_reset_mode(ds_dispatch,
			displays_num, display_indexes))
		return true;

	DAL_IF_ERROR(
		"%s: dal_ds_dispatch_reset_mode() failed!\n",
		__func__);

	return false;
}

/* PowerPlay */

bool dal_pre_adapter_clock_change(struct dal *dal,
		struct power_to_dal_info *clks_info)
{
	if (dal_adapter_service_is_feature_supported(
			FEATURE_USE_PPLIB)) {
		struct dal_context *dal_context = &dal->dal_context;
		struct ds_dispatch *ds_dispatch;
		struct gpu *gpu = NULL;
		enum ds_return result;
		struct gpu_clock_info gpu_clk_info;

		ds_dispatch = dal_display_service_get_set_mode_interface(
				dal->display_service);

		if (NULL == ds_dispatch) {
			DAL_IF_ERROR("%s: Display Service is NULL!\n",
					__func__);
			return false;
		}

		result = dal_ds_dispatch_pre_adapter_clock_change(ds_dispatch);

		/* cache new clock values, to be applied when
		 * post_adapter_clock_change is called
		 */
		gpu_clk_info.min_sclk_khz = clks_info->min_sclk;
		gpu_clk_info.max_sclk_khz = clks_info->max_sclk;
		gpu_clk_info.min_mclk_khz = clks_info->min_mclk;
		gpu_clk_info.max_mclk_khz = clks_info->max_mclk;

		gpu = dal_tm_get_gpu(dal->topology_mgr);

		dal_gpu_update_dynamic_clock_info(gpu, &gpu_clk_info);

		if (result == DS_SUCCESS)
			return true;

	}

	return false;
}

bool dal_post_adapter_clock_change(struct dal *dal)
{
	/* Up to this point we are using 'safe' marks, which are not
	 * power-efficient.
	 * Program stutter and other display marks to values provided
	 * by PPLib. */
	if (dal_adapter_service_is_feature_supported(
			FEATURE_USE_PPLIB)) {

		struct dal_context *dal_context = &dal->dal_context;
		struct ds_dispatch *ds_dispatch;
		enum ds_return result;

		ds_dispatch = dal_display_service_get_set_mode_interface(
				dal->display_service);

		if (NULL == ds_dispatch) {
			DAL_IF_ERROR("%s: Display Service is NULL!\n",
					__func__);
			return false;
		}

		result = dal_ds_dispatch_post_adapter_clock_change(ds_dispatch);

		if (result == DS_SUCCESS)
			return true;
	}

	return false;
}

/* Power Control */
void dal_set_display_dpms(
	struct dal *dal,
	uint32_t display_index,
	enum dal_power_state state)
{
	switch (state) {
	case DAL_POWER_STATE_ON:
		dal_display_service_target_power_control(
			dal->display_service,
			display_index, true);
		break;
	case DAL_POWER_STATE_STANDBY:
	case DAL_POWER_STATE_SUSPEND:
	case DAL_POWER_STATE_OFF:
		dal_display_service_mem_request_control(
			dal->display_service,
			display_index,
			false);

		dal_display_service_target_power_control(
			dal->display_service,
			display_index, false);
		break;
	default:
		break;
	}

}

void dal_set_power_state(
	struct dal *dal,
	enum dal_acpi_cm_power_state power_state,
	enum dal_video_power_state video_power_state)
{
	dal_tm_set_current_power_state(
		dal->topology_mgr, video_power_state);
	switch (power_state) {
	case DAL_ACPI_CM_POWER_STATE_D0:
		/*TODO:NotifyMultiDisplayConfig*/
		dal_tm_init_hw(dal->topology_mgr);
		break;
	default:
		/*TODO: Synchronization code goes here*/
		dal_tm_power_down_hw(dal->topology_mgr);
		break;
	}

}

void dal_set_blanking(struct dal *dal, uint32_t display_index, bool blank)
{
	struct dal_context *dal_context = &dal->dal_context;

	DAL_IF_TRACE();

	/* Return value is not checked here because dal_set_blanking could
	 * be called while HW is not ready for programming (i.e. on boot up) */
	dal_display_service_mem_request_control(dal->display_service,
			display_index, blank);
}

void dal_shut_down_display_block(struct dal *dal)
{
	struct dal_context *dal_context = &dal->dal_context;

	DAL_IF_NOT_IMPLEMENTED();
	/* TODO */
}

/* Brightness Interface */
bool dal_set_backlight_level(
	struct dal *dal,
	uint32_t display_index,
	uint32_t brightness)
{
	struct ds_dispatch *ds_dispatch =
		dal_display_service_get_adjustment_interface(
			dal->display_service);
	enum ds_return result;

	if (dal_tm_get_embedded_device_index(dal->topology_mgr) !=
		display_index)
		return false;

	if (!ds_dispatch)
		return NULL;

	result =
		dal_ds_dispatch_set_adjustment(
			ds_dispatch,
			display_index,
			ADJ_ID_BACKLIGHT,
			brightness);

	return result == DS_SUCCESS;
}

bool dal_get_backlight_level_old(
	struct dal *dal,
	uint32_t display_index,
	void *adjustment)
{
	struct dal_context *dal_context = &dal->dal_context;

	DAL_IF_NOT_IMPLEMENTED();
	/* TODO */
	return 0;
}

bool dal_backlight_control_on(struct dal *dal)
{
	struct dal_context *dal_context = &dal->dal_context;

	DAL_IF_NOT_IMPLEMENTED();
	/* TODO */
	return 0;
}

bool dal_backlight_control_off(struct dal *dal)
{
	struct dal_context *dal_context = &dal->dal_context;

	DAL_IF_NOT_IMPLEMENTED();
	/* TODO */
	return 0;
}

/* Brightness and color Control */
void dal_set_palette(struct dal *dal,
		uint32_t display_index,
		struct dal_dev_c_lut palette,
		uint32_t start,
		uint32_t length)
{
	struct dal_context *dal_context = &dal->dal_context;

	DAL_IF_NOT_IMPLEMENTED();
	/* TODO */
}

bool dal_set_gamma(
	struct dal *dal,
	uint32_t display_index,
	struct raw_gamma_ramp *gamma)
{
	enum ds_return result;
	struct dal_context *dal_context = &dal->dal_context;
	struct ds_dispatch *ds_dispatch =
		dal_display_service_get_adjustment_interface(
			dal->display_service);

	if (NULL == ds_dispatch) {
		DAL_IF_ERROR("%s: Display Service is NULL!\n",
				__func__);
		return false;
	}

	result = dal_ds_dispatch_set_gamma_adjustment(
			ds_dispatch,
			display_index,
			ADJ_ID_GAMMA_RAMP,
			gamma);

	return result == DS_SUCCESS;
}

/* Active Stereo Interface */
bool dal_control_stereo(struct dal *dal,
		uint32_t display_index,
		bool enable)
{
	struct dal_context *dal_context = &dal->dal_context;

	DAL_IF_NOT_IMPLEMENTED();
	/* TODO */
	return 0;
}

/* Disable All DC Pipes - blank CRTC and disable memory requests */
void dal_disable_all_dc_pipes(struct dal *dal)
{
	struct dal_context *dal_context = &dal->dal_context;

	DAL_IF_NOT_IMPLEMENTED();
	/* TODO */
}

/******************************************************************************
	Timing List Query
******************************************************************************/
struct dal_timing_list_query *dal_create_timing_list_query(struct dal *dal,
		uint32_t display_index)
{
	struct display_path *display_path;
	struct timing_list_query_init_data init_data = { 0 };
	struct dal_timing_list_query *tlsq;
	struct dal_context *dal_context = &dal->dal_context;

	display_path = dal_tm_display_index_to_display_path(dal->topology_mgr,
		display_index);
	if (!display_path) {
		DAL_IF_ERROR(
			"%s: dal_tm_display_index_to_display_path() failed!\n",
			__func__);
		return NULL;
	}

	init_data.dal = dal;
	init_data.dcs = dal_display_path_get_dcs(display_path);
	init_data.timing_srv = dal->timing_srv;
	init_data.display_index = display_index;

	tlsq = dal_timing_list_query_create(&init_data);
	if (!tlsq) {
		DAL_IF_ERROR("%s: timing_list_query_create() failed!\n",
			__func__);
		return NULL;
	}

	return tlsq;
}

/*****************************************************************************
	Implementation of private functions.
 *****************************************************************************/
static struct adapter_service *create_as(struct dal *dal)
{
	struct adapter_service *as = NULL;
	struct as_init_data init_data;
	struct dal_init_data *init = &dal->init_data;

	dal_memset(&init_data, 0, sizeof(init_data));

	init_data.dal_context = &dal->dal_context;

	/* BIOS parser init data */
	init_data.bp_init_data.dal_context = &dal->dal_context;
	init_data.bp_init_data.bios = init->asic_id.atombios_base_address;

	/* HW init data */
	init_data.hw_init_data.chip_family = init->asic_id.chip_family;
	init_data.hw_init_data.chip_id = init->asic_id.chip_id;
	init_data.hw_init_data.fake_paths_num = init->asic_id.fake_paths_num;
	init_data.hw_init_data.feature_flags = init->asic_id.feature_flags;
	init_data.hw_init_data.hw_internal_rev = init->asic_id.hw_internal_rev;
	init_data.hw_init_data.runtime_flags = init->asic_id.runtime_flags;
	init_data.hw_init_data.vram_width = init->asic_id.vram_width;
	init_data.hw_init_data.vram_type = init->asic_id.vram_type;

	/* bdf is BUS,DEVICE,FUNCTION*/
	init_data.bdf_info = init->bdf_info;

	init_data.display_param = &init->display_param;

	as = dal_adapter_service_create(&init_data);

	return as;
}

/*hard code color square at CRTC0 for HDMI light up*/
void dal_set_crtc_test_pattern(struct dal *dal)
{
	struct topology_mgr *tm = dal->topology_mgr;
	struct display_path *dp = dal_tm_display_index_to_display_path(
				tm, 0);
	struct controller *crtc = NULL;

	if (dal_display_path_is_acquired(dp))
		crtc = dal_display_path_get_controller(dp);

	if (crtc != NULL)
		dal_controller_set_test_pattern(
				crtc,
				DP_TEST_PATTERN_COLOR_SQUARES,
				HW_COLOR_DEPTH_888);
}

uint32_t dal_get_display_index_from_int_src(struct dal *dal,
						enum dal_irq_source src)
{
	enum controller_id crtc_id;

	/* check if DAL is disabled */
	if (dal == NULL)
			return INVALID_DISPLAY_INDEX;

	/* the assumption here is that INT_Y int was generated by
	   CRTC_Y hardware block. That might not be true in the future
	   TODO: greate CRTC to IRQ SRC tables */
	switch (src) {
	case DAL_IRQ_SOURCE_CRTC1VSYNC:
	case DAL_IRQ_SOURCE_CRTC2VSYNC:
	case DAL_IRQ_SOURCE_CRTC3VSYNC:
	case DAL_IRQ_SOURCE_CRTC4VSYNC:
	case DAL_IRQ_SOURCE_CRTC5VSYNC:
	case DAL_IRQ_SOURCE_CRTC6VSYNC:
	/*TODO: should we add one for Underlay? */
		crtc_id = src - DAL_IRQ_SOURCE_CRTC1VSYNC + 1;
		break;
	case DAL_IRQ_SOURCE_PFLIP1:
	case DAL_IRQ_SOURCE_PFLIP2:
	case DAL_IRQ_SOURCE_PFLIP3:
	case DAL_IRQ_SOURCE_PFLIP4:
	case DAL_IRQ_SOURCE_PFLIP5:
	case DAL_IRQ_SOURCE_PFLIP6:
	case DAL_IRQ_SOURCE_PFLIP_UNDERLAY0:
		crtc_id = src - DAL_IRQ_SOURCE_PFLIP1 + 1;
		break;
	default:
		dal_logger_write(dal->dal_context.logger,
				LOG_MAJOR_ERROR, LOG_MINOR_COMPONENT_ISR,
				"Unhandled interrupt source %d\n", src);
		return INVALID_DISPLAY_INDEX;
	}

	return dal_tm_get_display_path_index_for_controller(
						dal->topology_mgr,
						crtc_id);
}

/* return crtc scanout v/h counter, return if inside v_blank*/
uint32_t dal_get_crtc_scanoutpos(
		struct dal *dal,
		uint32_t display_index,
		int32_t *vpos,
		int32_t *hpos)
{
	struct topology_mgr *tm = dal->topology_mgr;
	struct display_path *dp = dal_tm_display_index_to_display_path(
				tm, display_index);
	struct controller *cont = dal_display_path_get_controller(dp);

	if (!cont) {
		dal_logger_write(dal->dal_context.logger,
				LOG_MAJOR_ERROR,
				LOG_MINOR_COMPONENT_DAL_INTERFACE,
				"Failed to find controller at display index %d\n",
				display_index);
		return 0;
	}

	return dal_controller_get_crtc_scanoutpos(cont, vpos, hpos);

}

static enum dal_irq_source get_irq_src_from_display_index_helper(
	struct dal *dal,
	uint32_t display_index,
	uint32_t plane_no,
	enum dal_irq_source base_irq_source)
{
	struct display_path *display_path;
	struct controller *controller;
	enum controller_id crtc_id;
	enum dal_irq_source src;

	/* return base irq source in DAL bypass mode */
	if (dal == NULL)
		return base_irq_source;

	display_path = dal_tm_display_index_to_display_path(dal->topology_mgr,
			display_index);

	controller = dal_display_path_get_controller_for_layer_index(
			display_path, plane_no);

	if (NULL == controller)
		return DAL_IRQ_SOURCE_INVALID;

	crtc_id = dal_controller_get_graphics_object_id(controller).id;

	src = crtc_id - 1 + base_irq_source;

	return src;
}

enum dal_irq_source dal_get_vblank_irq_src_from_display_index(
	struct dal *dal,
	uint32_t display_index)
{
	return get_irq_src_from_display_index_helper(
		dal,
		display_index,
		0,
		DAL_IRQ_SOURCE_CRTC1VSYNC);
}

void dal_set_vblank_irq(
	struct dal *dal,
	uint32_t display_index,
	bool enable)
{
	struct display_path *display_path;
	struct controller *controller;
	struct line_buffer *lb;

	display_path = dal_tm_display_index_to_display_path(dal->topology_mgr,
				display_index);

	controller = dal_tm_get_controller_from_display_path(dal->topology_mgr,
				display_path);

	if (NULL == controller)
		return;

	lb = dal_controller_get_line_buffer(controller);
	if (NULL == lb)
		return;
	dal_line_buffer_set_vblank_irq(lb, enable);
}

enum dal_irq_source dal_get_pflip_irq_src_from_display_index(
	struct dal *dal,
	uint32_t display_index,
	uint32_t plane_no)
{
	return get_irq_src_from_display_index_helper(
		dal,
		display_index,
		plane_no,
		DAL_IRQ_SOURCE_PFLIP1);
}

struct is_display_active_param {
	bool active;
	uint32_t display_index;
};

struct mode_manager *dal_get_mode_manager(struct dal *dal)
{
	return dal->mm;
}

struct mode_query *dal_get_mode_query(
	struct dal *dal,
	struct topology *tp,
	enum query_option query_option)
{
	return dal_mode_manager_create_mode_query(dal->mm, tp, query_option);
}


/* DSAT Accessors, definition is in dsat.c */
struct topology_mgr *dal_get_tm(struct dal *dal)
{
	return dal->topology_mgr;
}

struct adapter_service *dal_get_as(struct dal *dal)
{
	return dal->adapter_srv;
}

struct timing_service *dal_get_ts(struct dal *dal)
{
	return dal->timing_srv;
}

struct hw_sequencer *dal_get_hws(struct dal *dal)
{
	return dal->hws;
}

struct display_service *dal_get_ds(struct dal *dal)
{
	return dal->display_service;
}

struct mode_manager *dal_get_mm(struct dal *dal)
{
	return dal->mm;
}

struct dal_context *dal_get_dal_ctx(struct dal *dal)
{
	return &dal->dal_context;
}

struct dal_init_data *dal_get_init_data(struct dal *dal)
{
	return &dal->init_data;
}

bool dal_set_cursor_position(
		struct dal *dal,
		const uint32_t display_index,
		const struct cursor_position *position
		)
{
	struct display_path *dp_path;

	if (!position)
		return false;

	dp_path =
		dal_tm_display_index_to_display_path(
			dal->topology_mgr,
			display_index);

	return dal_hw_sequencer_set_cursor_position(
		dal->hws,
		dp_path,
		position);
}

bool dal_set_cursor_attributes(
		struct dal *dal,
		const uint32_t display_index,
		const struct cursor_attributes *attributes
		)
{
	struct display_path *dp_path;

	if (!attributes)
		return false;

	dp_path =
		dal_tm_display_index_to_display_path(
			dal->topology_mgr,
			display_index);

	return dal_hw_sequencer_set_cursor_attributes(
		dal->hws,
		dp_path,
		attributes);
}


static enum pixel_format convert_pixel_format_plane_to_dalsurface(
		enum surface_pixel_format surface_pixel_format)
{
	enum pixel_format dal_pixel_format = PIXEL_FORMAT_UNKNOWN;

	switch (surface_pixel_format) {
	case SURFACE_PIXEL_FORMAT_GRPH_PALETA_256_COLORS:
		dal_pixel_format = PIXEL_FORMAT_INDEX8;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB1555:
		dal_pixel_format = PIXEL_FORMAT_RGB565;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_RGB565:
		dal_pixel_format = PIXEL_FORMAT_RGB565;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB8888:
		dal_pixel_format = PIXEL_FORMAT_ARGB8888;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_BGRA8888:
		dal_pixel_format = PIXEL_FORMAT_ARGB8888;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB2101010:
		dal_pixel_format = PIXEL_FORMAT_ARGB2101010;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010:
		dal_pixel_format = PIXEL_FORMAT_ARGB2101010;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010_XR_BIAS:
		dal_pixel_format = PIXEL_FORMAT_ARGB2101010_XRBIAS;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616:
		dal_pixel_format = PIXEL_FORMAT_FP16;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616F:
		dal_pixel_format = PIXEL_FORMAT_FP16;
		break;


	case SURFACE_PIXEL_FORMAT_VIDEO_420_YCbCr:
		dal_pixel_format = PIXEL_FORMAT_420BPP12;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_420_YCrCb:
		dal_pixel_format = PIXEL_FORMAT_420BPP12;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_422_YCb:
		dal_pixel_format = PIXEL_FORMAT_422BPP16;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_422_YCr:
		dal_pixel_format = PIXEL_FORMAT_422BPP16;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_422_CbY:
		dal_pixel_format = PIXEL_FORMAT_422BPP16;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_422_CrY:
		dal_pixel_format = PIXEL_FORMAT_422BPP16;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_444_ACrYCb1555:
		dal_pixel_format = PIXEL_FORMAT_444BPP16;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_444_CrYCb565:
		dal_pixel_format = PIXEL_FORMAT_444BPP16;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_444_ACrYCb4444:
		dal_pixel_format = PIXEL_FORMAT_444BPP16;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_444_CbYCrA5551:
		dal_pixel_format = PIXEL_FORMAT_444BPP16;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_444_ACrYCb8888:
		dal_pixel_format = PIXEL_FORMAT_444BPP32;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_444_ACrYCb2101010:
		dal_pixel_format = PIXEL_FORMAT_444BPP32;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_444_CbYCrA1010102:
		dal_pixel_format = PIXEL_FORMAT_444BPP32;
		break;
	default:
		dal_pixel_format = PIXEL_FORMAT_UNKNOWN;
		break;
	}
	return dal_pixel_format;
}

bool dal_validate_plane_configurations(struct dal *dal,
		int num_planes,
		const struct plane_config *pl_config,
		bool *supported)
{
	struct ds_dispatch *ds_dispatch =
		dal_display_service_get_set_mode_interface(
			dal->display_service);

	if (NULL == ds_dispatch) {
		struct dal_context *dal_context = &dal->dal_context;

		DAL_IF_ERROR(
			"%s: ds_dispatch is NULL!\n",
			__func__);

		return false;
	}

	if (dal_ds_dispatch_validate_plane_configurations(
				ds_dispatch,
				num_planes,
				pl_config,
				supported))
		return true;

	return false;
}

/*TODO fill struct plane_attributes attributes;
 * for scaler*/
bool dal_setup_plane_configurations(
	struct dal *dal,
	uint32_t num_planes,
	struct plane_config *configs)
{
	struct dal_context *dal_context = &dal->dal_context;
	struct ds_dispatch *ds_dispatch;
	uint32_t display_index = configs[0].display_index;
	uint32_t i = 0;

	if (configs == NULL)
		return false;

	if (num_planes == 0 || num_planes > MAX_PLANE_NUM)
		return false;

	ds_dispatch =
		dal_display_service_get_set_mode_interface(
			dal->display_service);

	if (!ds_dispatch) {
		DAL_IF_ERROR("%s: ds_dispatch is NULL!\n", __func__);
		return false;
	}

	/*convert dm surface pixel format into DAL internal surface format*/
	for (i = 0; i < num_planes; i++) {
		configs[i].config.dal_pixel_format =
			convert_pixel_format_plane_to_dalsurface(
				configs[i].config.format);
	}

	dal_ds_dispatch_setup_plane_configurations(
		ds_dispatch,
		display_index,
		num_planes,
		configs);

	return true;
}

bool dal_update_plane_addresses(
	struct dal *dal,
	uint32_t num_planes,
	struct plane_addr_flip_info *info)
{
	struct display_path *dp_path;

	if (num_planes == 0 || num_planes > MAX_PLANE_NUM)
		return false;

	if (!info)
		return false;

	dp_path =
		dal_tm_display_index_to_display_path(
			dal->topology_mgr,
			info[0].display_index);

	return dal_hw_sequencer_update_plane_address(
		dal->hws,
		dp_path,
		num_planes,
		info);
}

static bool remote_display_receiver_caps_set(
		struct dal *dal,
		struct display_path *disp_path,
		struct dal_remote_display_receiver_capability *caps)
{
	struct dcs *dcs;

	dcs = dal_display_path_get_dcs(disp_path);
	if (NULL == dcs) {
		dal_logger_write(dal->dal_context.logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_DAL_INTERFACE,
			"%s: DCS is NULL!\n", __func__);
		return false;
	}

	dal_dcs_set_remote_display_receiver_capabilities(dcs, caps);

	return true;
}

uint32_t dal_wifi_display_acquire(
	struct dal *dal,
	uint8_t *edid_data,
	uint32_t edid_size,
	struct dal_remote_display_receiver_capability *caps,
	uint32_t *display_index)
{
	struct display_path *disp_path = NULL;

	if (edid_data == NULL || caps == NULL) {
		ASSERT_CRITICAL(0);
		dal_logger_write(dal->dal_context.logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_DAL_INTERFACE,
			"Wifi display capability is NULL");
		/*TODO: define an enum for error codes
		 * and make this function return that enum
		 */
		return -1;
	}

	*display_index = dal_tm_get_wireless_display_index(
			dal->topology_mgr);

	if (*display_index == INVALID_DISPLAY_INDEX) {
		ASSERT_CRITICAL(0);
		dal_logger_write(dal->dal_context.logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_DAL_INTERFACE,
			"Wireless display index is invalid");
		return -1;
	}

	disp_path = dal_tm_display_index_to_display_path(
			dal->topology_mgr, *display_index);

	if (disp_path == NULL)
		return -1;

	if (dal_display_path_is_target_connected(disp_path)) {
		dal_logger_write(dal->dal_context.logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_DAL_INTERFACE,
			"%s: Failed to acquire disp path - already in use\n",
			__func__);
		return -1;
	}

	if (false == remote_display_receiver_caps_set(dal, disp_path, caps)) {
		dal_logger_write(dal->dal_context.logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_DAL_INTERFACE,
			"%s: Failed to set remote display caps!\n", __func__);
		return -1;
	}

	dal_tm_update_display_edid(
		dal->topology_mgr, *display_index,
		edid_data, edid_size);

	dal_tm_set_force_connected(
		dal->topology_mgr,
		*display_index,
		true);

	return 0;
}

static void remote_display_receiver_caps_clear(
		struct dal *dal,
		struct display_path *disp_path)
{
	struct dcs *dcs = dal_display_path_get_dcs(disp_path);

	if (NULL == dcs) {
		dal_logger_write(dal->dal_context.logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_DAL_INTERFACE,
			"%s: DCS is NULL!\n", __func__);
		return;
	}

	dal_dcs_clear_remote_display_receiver_capabilities(dcs);
}

uint32_t dal_wifi_display_release(
	struct dal *dal,
	uint32_t *display_index)
{
	struct display_path *disp_path = NULL;

	*display_index = dal_tm_get_wireless_display_index(
			dal->topology_mgr);

	if (*display_index == INVALID_DISPLAY_INDEX) {
		ASSERT_CRITICAL(0);
		dal_logger_write(dal->dal_context.logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_DAL_INTERFACE,
			"Wireless display index is invalid");
		return -1;
	}

	disp_path = dal_tm_display_index_to_display_path(
			dal->topology_mgr, *display_index);

	if (false == dal_display_path_is_target_connected(disp_path)) {
		dal_logger_write(dal->dal_context.logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_DAL_INTERFACE,
			"Display path is not connected, display path probably not acquired");
		return -1;
	}

	dal_tm_set_force_connected(
		dal->topology_mgr,
		*display_index,
		false);

	remote_display_receiver_caps_clear(dal, disp_path);

	return 0;
}

/*
 * dal_get_vblank_counter
 *
 * @brief
 * Get the current vertical blank counts for given CRTC.
 *
 * @param
 * struct dal *dal - [in] DAL instance
 * int disp_idx - [in] display index
 *
 * @return
 * Return the CRTC counter for frame
 */
uint32_t dal_get_vblank_counter(struct dal *dal, int disp_idx)
{
	struct topology_mgr *tm = dal->topology_mgr;
	struct display_path *dp = dal_tm_display_index_to_display_path(
				tm, disp_idx);
	struct controller *cont = NULL;

	if (dal_display_path_is_acquired(dp))
		cont = dal_display_path_get_controller(dp);
	else
		return 0;

	return dal_controller_get_vblank_counter(cont);
}

void dal_interrupt_set(struct dal *dal, enum dal_irq_source src, bool enable)
{
	dal_irq_service_set(dal->irqs, src, enable);
}

void dal_interrupt_ack(struct dal *dal, enum dal_irq_source src)
{
	dal_irq_service_ack(dal->irqs, src);
}

enum dal_irq_source dal_interrupt_to_irq_source(
		struct dal *dal,
		uint32_t src_id,
		uint32_t ext_id)
{
	return dal_irq_service_to_irq_source(dal->irqs, src_id, ext_id);
}
