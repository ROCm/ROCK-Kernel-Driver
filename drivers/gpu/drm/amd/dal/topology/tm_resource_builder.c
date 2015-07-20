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

/* External includes */
#include "include/topology_mgr_interface.h"
#include "include/adapter_service_interface.h"
#include "include/gpu_interface.h"
#include "include/connector_interface.h"
#include "include/encoder_types.h"
#include "include/encoder_interface.h"
#include "include/dcs_interface.h"
#include "include/link_service_interface.h"
#include "include/ddc_service_interface.h"
#include "include/controller_interface.h"
#include "include/audio_interface.h"

/* Internal includes */
#include "tm_resource_builder.h"
#include "tm_resource_mgr.h"
#include "tm_internal_types.h"
#include "tm_utils.h"

static const enum dal_device_type tmrb_device_enumeration_order[] = {
		DEVICE_TYPE_LCD,
		DEVICE_TYPE_CRT,
		DEVICE_TYPE_DFP,
		DEVICE_TYPE_CV,
		DEVICE_TYPE_TV,
		DEVICE_TYPE_WIRELESS,
		DEVICE_TYPE_CF };

static const uint32_t tmrb_num_of_devices_in_order_enumeration =
		sizeof(tmrb_device_enumeration_order)
				/ sizeof(tmrb_device_enumeration_order[0]);

/* local macro definitions */
#define TM_RB_MAX_NUM_OF_DISPLAY_PATHS	20


/*****************************************************************************
 *	private data structures
 ***************************************************************************/

struct tm_resource_builder {
	struct dal_context *dal_context;
	struct adapter_service *as;
	struct timing_service *timing_srvc;
	struct hw_sequencer *hwss;
	struct tm_resource_mgr *tm_rm;
	struct irq_manager *irq_manager;
	struct topology_mgr *tm;

	struct display_path *display_paths[TM_RB_MAX_NUM_OF_DISPLAY_PATHS];
	struct display_path *root_display_paths[TM_RB_MAX_NUM_OF_DISPLAY_PATHS];
	uint32_t num_of_display_paths;
	uint32_t num_of_cf_paths;
};

/*****************************************************************************
 *	private functions
 ***************************************************************************/

static bool tm_resource_builder_construct(
		struct tm_resource_builder *tm_rb,
		const struct tm_resource_builder_init_data *init_data)
{
	tm_rb->dal_context = init_data->dal_context;
	tm_rb->as = init_data->adapter_service;
	tm_rb->hwss = init_data->hwss;
	tm_rb->timing_srvc = init_data->timing_service;
	tm_rb->tm_rm = init_data->resource_mgr;
	tm_rb->irq_manager = init_data->irq_manager;
	tm_rb->tm = init_data->tm;
	return true;
}

/** create the TM Resource Builder */
struct tm_resource_builder*
tm_resource_builder_create(
		const struct tm_resource_builder_init_data *init_data)
{
	struct tm_resource_builder *tm_rb;

	tm_rb = dal_alloc(sizeof(*tm_rb));

	if (!tm_rb) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	if (tm_resource_builder_construct(tm_rb, init_data))
		return tm_rb;

	dal_free(tm_rb);
	return NULL;
}

/** destroy the TM Builder Manager */
void tm_resource_builder_destroy(struct tm_resource_builder **tm_rb)
{
	if (!tm_rb || !*tm_rb) {
		BREAK_TO_DEBUGGER();
		return;
	}

	/* TODO: deallocate everything here */

	dal_free(*tm_rb);
	*tm_rb = NULL;
}

static void init_gpu_static_clocks(struct tm_resource_builder *tm_rb,
		struct gpu *gpu)
{
	struct dal_system_clock_range dal_sys_clks = {};

	if (false == dal_adapter_service_is_feature_supported(
					FEATURE_USE_PPLIB))
		return;

	if (false == dal_get_system_clocks_range(tm_rb->dal_context,
			&dal_sys_clks)) {
		/* GPU can continue with default clocks.
		 * Do NOT fail this call - only log a warning. */
		dal_logger_write(tm_rb->dal_context->logger,
			LOG_MAJOR_WARNING,
			LOG_MINOR_COMPONENT_GPU,
			"Failed to get BM Static Clock Ranges! Will be using default values.\n");
	} else {
		struct gpu_clock_info gpu_clk_info;

		dal_memset(&gpu_clk_info, 0, sizeof(gpu_clk_info));

		gpu_clk_info.max_dclk_khz = dal_sys_clks.max_dclk;
		gpu_clk_info.min_dclk_khz = dal_sys_clks.min_dclk;

		gpu_clk_info.max_mclk_khz = dal_sys_clks.max_mclk;
		gpu_clk_info.min_mclk_khz = dal_sys_clks.min_mclk;

		gpu_clk_info.max_sclk_khz = dal_sys_clks.max_sclk;
		gpu_clk_info.min_sclk_khz = dal_sys_clks.min_sclk;

		dal_gpu_init_static_clock_info(gpu, &gpu_clk_info);
	}
}

static struct gpu *tm_resource_builder_create_gpu(
		struct tm_resource_builder *tm_rb)
{
	struct dal_context *dal_context = tm_rb->dal_context;
	struct gpu_init_data gpu_init_data;
	struct gpu *gpu;

	/* initialise GPU init data */
	dal_memset(&gpu_init_data, 0, sizeof(gpu_init_data));

	gpu_init_data.dal_context = tm_rb->dal_context;
	gpu_init_data.adapter_service = tm_rb->as;
	gpu_init_data.irq_manager = tm_rb->irq_manager;

	/* create GPU object */
	gpu = dal_gpu_create(&gpu_init_data);
	if (!gpu) {
		TM_ERROR("%s: failed to instantiate GPU object!\n", __func__);
		return NULL;
	}

	tm_resource_mgr_set_gpu_interface(tm_rb->tm_rm, gpu);

	init_gpu_static_clocks(tm_rb, gpu);

	return gpu;
}

static enum tm_result tm_resource_builder_add_engines(
		struct tm_resource_builder *tm_rb)
{
	enum tm_result rc = TM_RESULT_SUCCESS;
	uint32_t eng_id;
	struct tm_resource *tm_resource;
	struct dal_context *dal_context = tm_rb->dal_context;

	for (eng_id = 0; eng_id < ENGINE_ID_COUNT; eng_id++) {

		tm_resource = tm_resource_mgr_add_engine(tm_rb->tm_rm,
				eng_id);

		if (NULL == tm_resource) {
			rc = TM_RESULT_FAILURE;
			TM_ERROR("%s: Failed to add engine",
					__func__);
			break;
		}
	}

	return rc;
}

static enum tm_result tm_resource_builder_add_clock_sources(
		struct tm_resource_builder *tm_rb,
		struct gpu *gpu)
{
	enum tm_result rc = TM_RESULT_SUCCESS;
	uint32_t clock_sources_num;
	uint32_t clk_src_ind;
	struct tm_resource *tm_resource;
	struct clock_source *clock_source;
	struct dal_context *dal_context = tm_rb->dal_context;

	clock_sources_num = dal_gpu_get_num_of_clock_sources(gpu);

	TM_RESOURCES("TM_RB: number of Clock Sources: %d\n",
			clock_sources_num);

	for (clk_src_ind = 0;
		clk_src_ind < clock_sources_num;
		clk_src_ind++) {

		clock_source = dal_gpu_create_clock_source(gpu,
				clk_src_ind);
		if (NULL == clock_source) {
			rc = TM_RESULT_FAILURE;
			TM_ERROR("%s: Failed to create ClockSource\n",
					__func__);
			break;
		}

		tm_resource =
			dal_tm_resource_mgr_add_resource(tm_rb->tm_rm,
				dal_tm_resource_clock_source_create(
					clock_source));

		if (NULL == tm_resource) {
			rc = TM_RESULT_FAILURE;
			TM_ERROR("%s: Failed to add ClockSource number:%d\n",
					__func__, clk_src_ind);
			dal_clock_source_destroy(&clock_source);
			break;
		}

	}  /* for() */

	return rc;
}

static enum tm_result tm_resource_builder_add_controllers(
		struct tm_resource_builder *tm_rb,
		struct gpu *gpu)

{
	enum tm_result rc = TM_RESULT_SUCCESS;
	uint32_t i;
	uint32_t controllers_num;
	struct controller *controller;
	struct tm_resource *tm_resource;
	struct dal_context *dal_context = tm_rb->dal_context;

	controllers_num = dal_gpu_get_max_num_of_primary_controllers(gpu) +
			dal_gpu_get_max_num_of_underlay_controllers(gpu);

	for (i = 0; i < controllers_num; i++) {

		controller = dal_gpu_create_controller(gpu, i);

		if (controller == NULL) {
			TM_ERROR("%s: Failed to create Controller!\n",
					__func__);
			rc = TM_RESULT_FAILURE;
			break;
		}

		tm_resource = dal_tm_resource_mgr_add_resource(
			tm_rb->tm_rm,
			dal_tm_resource_controller_create(controller));

		if (tm_resource == NULL) {
			TM_ERROR("%s: Failed to add Controller!\n", __func__);
			rc = TM_RESULT_FAILURE;
			break;
		}
	}

	return rc;
}

enum tm_result tm_resource_builder_create_gpu_resources(
		struct tm_resource_builder *tm_rb)
{
	enum tm_result rc = TM_RESULT_SUCCESS;
	struct gpu *gpu;

	do {
		gpu = tm_resource_builder_create_gpu(tm_rb);
		if (!gpu) {
			rc = TM_RESULT_FAILURE;
			break;
		}

		/********************************************************
		Add Engines
		*********************************************************/
		rc = tm_resource_builder_add_engines(tm_rb);
		if (TM_RESULT_FAILURE == rc)
			break;

		/********************************************************
		Add Clock Sources
		*********************************************************/
		rc = tm_resource_builder_add_clock_sources(tm_rb, gpu);
		if (TM_RESULT_FAILURE == rc)
			break;

		/********************************************************
		Add Controllers
		*********************************************************/
		rc = tm_resource_builder_add_controllers(tm_rb, gpu);
		if (TM_RESULT_FAILURE == rc)
			break;

		/* TODO: add the rest of resources */

	} while (0);

	/* handle an error */
	if (TM_RESULT_FAILURE == rc) {
		if (gpu) {
			tm_resource_mgr_set_gpu_interface(tm_rb->tm_rm, NULL);
			dal_gpu_destroy(&gpu);
		}
	}

	return rc;
}

/**
 * Adds link to display path, which means mandatory encoder
 * Link is added to the end of the chain (if counting from GPU).
 *
 * \param [in] display_path: Display path to which add the link
 * \param [in] encoder:     Mandatory object of the link.
 */
static enum tm_result tmrb_add_link(
		struct tm_resource_builder *tm_rb,
		struct display_path *display_path,
		struct encoder *encoder)
{
	uint32_t current_num_of_links;
	struct encoder_feature_support efs;
	struct dal_context *dal_context = tm_rb->dal_context;

	/* keep number of links *before* a new one was added */
	current_num_of_links = dal_display_path_get_number_of_links(
			display_path);

	efs = dal_encoder_get_supported_features(encoder);

	/* Add link to display path */
	if (false == dal_display_path_add_link(display_path, encoder)) {
		TM_ERROR("%s: dal_display_path_add_link failed!\n", __func__);
		return TM_RESULT_FAILURE;
	}

	return TM_RESULT_SUCCESS;
}

/**
 * Set the Display Path properties
 * Also set TM resource for connector used for edid ddc polling feature
 *
 * \param [in] init_data:    Display Path Init Data
 * \param [in] display_path: Display path which is currently built
 */
static void tmrb_set_display_path_properties(struct tm_resource_builder *tm_rb,
		const struct tm_display_path_init_data *init_data,
		struct display_path *display_path)
{
	union display_path_properties props;
	enum signal_type sink_signal;
	enum signal_type asic_signal;
	bool hpd_supported = false;
	bool ddc_supported = false;
	struct connector *connector;
	struct connector_feature_support cfs;
	union audio_support aud_support;
	enum connector_id connector_id;
	uint32_t output_signals;
	struct dal_context *dal_context = tm_rb->dal_context;

	props.raw = 0;

	sink_signal = init_data->sink_signal;
	asic_signal = dal_display_path_sink_signal_to_asic_signal(display_path,
			sink_signal);

	/* Obtain HPD/DDC and audio support from connector */
	connector = dal_display_path_get_connector(display_path);
	dal_connector_get_features(connector, &cfs);

	if (asic_signal == SIGNAL_TYPE_RGB) {
		/* For VGA the hardware may poll DDC and
		 * if this is the case, HPD is supported. */
		hpd_supported = cfs.HW_DDC_POLLING;
	} else {
		/* For non-VGA, if there is HPD line, then
		 * HPD is supported. */
		hpd_supported = (cfs.hpd_line != HPD_SOURCEID_UNKNOWN);
	}

	ddc_supported = (cfs.ddc_line != CHANNEL_ID_UNKNOWN);

	/* get audio support from adapter service */
	aud_support = dal_adapter_service_get_audio_support(tm_rb->as);

	/* get this connector's id, and supported output signals */
	connector_id = dal_graphics_object_id_get_connector_id(
			dal_connector_get_graphics_object_id(connector));

	output_signals = dal_connector_enumerate_output_signals(connector);

	/* handle setting this display path's DP audio supported bit */
	if ((output_signals & SIGNAL_TYPE_DISPLAY_PORT)
			|| (output_signals & SIGNAL_TYPE_DISPLAY_PORT_MST)
			|| (output_signals & SIGNAL_TYPE_EDP)) {
		/* Connector can output a DP signal, so if DP audio is
		 * supported, then DP audio bit is set accordingly. */
		props.bits.IS_DP_AUDIO_SUPPORTED = aud_support.bits.DP_AUDIO;
	}

	/* handle setting this display path's HDMI audio supported bit */
	if (output_signals & SIGNAL_TYPE_HDMI_TYPE_A) {
		/* Signal is HDMI, if connector is HDMI, we have the
		 * native case, so we check the HDMI native bit. */
		if (connector_id == CONNECTOR_ID_HDMI_TYPE_A) {
			props.bits.IS_HDMI_AUDIO_SUPPORTED =
					aud_support.bits.HDMI_AUDIO_NATIVE;

			if (!aud_support.bits.HDMI_AUDIO_NATIVE) {
				/* this should not happen */
				TM_ERROR("%s: HDMI connector exists, but HDMI native audio not supported",
						__func__);
			}
		} else {
			/* Otherwise, we have the dongle case, so we check
			 * the HDMI on dongle bit. */
			props.bits.IS_HDMI_AUDIO_SUPPORTED =
					aud_support.bits.HDMI_AUDIO_ON_DONGLE;
		}
	}

	/* Common initialisation */
	props.bits.HPD_SUPPORTED = hpd_supported;
	props.bits.NON_DESTRUCTIVE_POLLING = ddc_supported;
	props.bits.FAKED_PATH = (init_data->faked_path_device_id.device_type
			!= DEVICE_TYPE_UNKNOWN);

	/* Override based on sink signal */
	if (dal_is_analog_signal(sink_signal)) {

		props.bits.FORCE_CONNECT_SUPPORTED = 1;
	} else if (sink_signal == SIGNAL_TYPE_LVDS) {

		props.bits.HPD_SUPPORTED = 1;
		props.bits.NON_DESTRUCTIVE_POLLING = 1;
	} else if (sink_signal == SIGNAL_TYPE_WIRELESS) {

		props.bits.HPD_SUPPORTED = 1;
		props.bits.FORCE_CONNECT_SUPPORTED = 1;
	}

	dal_display_path_set_properties(display_path, props);
}

/**
 * Updates device tag on display path.
 * For real display path we query VBIOS.
 * For CF paths and fake paths we generate device tag according
 * to internal policy.
 *
 * \param [in] init_data:     Display Path Init Data
 * \param [in] display_path: Display path which is currently built
 *
 * \return TM_RESULT_SUCCESS if at device tag was successfully updated,
 *	TM_RESULT_FAILURE otherwise.
 */
static enum tm_result tmrb_update_device_tag(
		struct tm_resource_builder *tm_rb,
		const struct tm_display_path_init_data *init_data,
		struct display_path *display_path)
{
	struct connector_device_tag_info device_tag = { 0 };
	struct graphics_object_id conn_object_id;
	struct connector *connector;
	uint32_t i;
	struct dal_context *dal_context = tm_rb->dal_context;

	connector = dal_display_path_get_connector(display_path);

	conn_object_id = dal_connector_get_graphics_object_id(connector);

	if (conn_object_id.type != OBJECT_TYPE_CONNECTOR) {
		TM_ERROR("%s: This path doesn't have connector, something is wrong!\n",
				__func__);
		return TM_RESULT_FAILURE;
	}

	/* Case 1 - fake display path */
	if (init_data->faked_path_device_id.device_type
			!= DEVICE_TYPE_UNKNOWN) {

		device_tag.dev_id.device_type =
				init_data->faked_path_device_id.device_type;
		device_tag.dev_id.enum_id =
				init_data->faked_path_device_id.enum_id;

		dal_display_path_set_device_tag(display_path, device_tag);

		TM_BUILD_DSP_PATH("  Device tag \"fake\" [%u:%u] was set on display path [0x%p].\n",
			device_tag.dev_id.device_type,
			device_tag.dev_id.enum_id,
			display_path);

		return TM_RESULT_SUCCESS;
	}

	/* case 2 - CrossFire display path */
	if (conn_object_id.id == CONNECTOR_ID_CROSSFIRE) {

		device_tag.dev_id.device_type = DEVICE_TYPE_CF;
		device_tag.dev_id.enum_id = 1;

		for (i = 0; i < tm_rb->num_of_display_paths; i++) {

			struct connector_device_tag_info *current_device_tag =
				dal_display_path_get_device_tag(
						tm_rb->display_paths[i]);

			if (device_tag.dev_id.device_type ==
				current_device_tag->dev_id.device_type) {
				/* We found a CF path.*/
				/* In VBIOS we usually have 2 CF path, one
				 * representing bundleA and one representing
				 * bundleB.
				 * To simplify TM, TM will only enumerate 1 CF
				 * path. The CF path can be configured for
				 * bundleA, bundleB or bundleA+B.
				 * Here if a CF path has already been created
				 * prior to this CF path, this CF path will
				 * fail to create so we'll have only 1 CF path.
				 */
				return TM_RESULT_FAILURE;
			}
		}

		dal_display_path_set_device_tag(display_path, device_tag);

		TM_BUILD_DSP_PATH("  Device tag \"CF\" [%u:%u] was set on display path [0x%p].\n",
				device_tag.dev_id.device_type,
				device_tag.dev_id.enum_id,
				display_path);

		return TM_RESULT_SUCCESS;
	}

	/* case 3 - Wireless display path */
	if (conn_object_id.id == CONNECTOR_ID_WIRELESS ||
		conn_object_id.id == CONNECTOR_ID_MIRACAST) {

		device_tag.dev_id.device_type = DEVICE_TYPE_WIRELESS;
		device_tag.dev_id.enum_id = 1;

		dal_display_path_set_device_tag(display_path, device_tag);

		TM_BUILD_DSP_PATH("  Device tag \"wireless\" [%u:%u] was set on display path [0x%p].\n",
				device_tag.dev_id.device_type,
				device_tag.dev_id.enum_id,
				display_path);

		return TM_RESULT_SUCCESS;
	}

	/* case 4 - VBIOS lookup
	 * Index to src table does not correspond to index of device tag.
	 * Therefore here we have to loop through the DeviceTags and find a
	 * matching DeviceTag (to SignalType) to assign to DisplayPath */
	for (i = 0; ; i++) {
		if (dal_adapter_service_get_device_tag(tm_rb->as,
				conn_object_id, i, &device_tag)
				!= true) {
			break;
		}

		if (device_tag.dev_id.device_type
			== tm_utils_signal_type_to_device_type(
				dal_display_path_get_query_signal(
					display_path, SINK_LINK_INDEX))) {

			if (true == dal_adapter_service_is_device_id_supported(
					tm_rb->as, device_tag.dev_id)) {

				dal_display_path_set_device_tag(display_path,
						device_tag);

				TM_BUILD_DSP_PATH(
					"  Device tag [%u:%u] was set on display path [0x%p].\n",
						device_tag.dev_id.device_type,
						device_tag.dev_id.enum_id,
						display_path);

				return TM_RESULT_SUCCESS;
			}
			break;
		}
	}

	/* Probably connector (as reported in VBIOS) does not support this
	 * signal and display path should be destroyed */
	return TM_RESULT_FAILURE;
}

/**
 * Creates Link Service of requested type
 *
 * \param [in] display_path - Display path which has link associated with
 *				this service.
 * \param [in] link_index    - Link inside display path associated with
 *				this service
 * \param [in] link_type     - Type of link service
 *
 * \return pointer to created Link Service
 */
static struct link_service *tmrb_create_link_service(
		struct tm_resource_builder *tm_rb,
		struct display_path *display_path,
		uint32_t link_index,
		enum link_service_type link_type)
{
	struct connector *connector;
	struct link_service *link_service = NULL;
	uint32_t num_of_paths_per_connector = 0;
	struct tm_resource *connector_rsrc;
	enum dal_irq_source hpd_rx_irq_src = DAL_IRQ_SOURCE_INVALID;
	struct graphics_object_id conn_object_id;
	struct irq *irq;
	struct link_service_init_data link_service_init_data;
	struct dal_context *dal_context = tm_rb->dal_context;

	connector = dal_display_path_get_connector(display_path);
	conn_object_id = dal_connector_get_graphics_object_id(connector);

	connector_rsrc = tm_resource_mgr_find_resource(tm_rb->tm_rm,
			conn_object_id);

	/* Update number of paths bound to the link */
	if (link_type == LINK_SERVICE_TYPE_DP_MST) {
		num_of_paths_per_connector =
		dal_adapter_service_get_num_of_path_per_dp_mst_connector(
							tm_rb->as);
	} else {
		/* non-MST always 1 path */
		num_of_paths_per_connector = 1;
	}

	if (num_of_paths_per_connector < 1) {
		TM_ERROR("%s: invalid num_of_paths_per_connector!\n",
				__func__);
		return NULL;
	}

	/* get the HPD RX source */
	irq = dal_adapter_service_obtain_hpd_irq(tm_rb->as,
		GRPH_ID(connector_rsrc) /*conn_object_id*/);
	if (NULL != irq) {
		hpd_rx_irq_src = dal_irq_get_rx_source(irq);
		dal_adapter_service_release_irq(tm_rb->as, irq);
	}

	dal_memset(&link_service_init_data, 0, sizeof(link_service_init_data));

	/* Create a link service */
	link_service_init_data.connector_enum_id =
		GRPH_ID(connector_rsrc).enum_id;
	link_service_init_data.connector_id =
		GRPH_ID(connector_rsrc);
	link_service_init_data.dpcd_access_srv =
			TO_CONNECTOR_INFO(connector_rsrc)->ddc_service;
	link_service_init_data.hwss = tm_rb->hwss;
	/*link_service_init_data.irq_src_dp_sink TODO: ?? */
	link_service_init_data.irq_src_hpd_rx = hpd_rx_irq_src;
	link_service_init_data.link_type = link_type;
	link_service_init_data.num_of_displays = num_of_paths_per_connector;
	link_service_init_data.adapter_service = tm_rb->as;
	link_service_init_data.dal_context = tm_rb->dal_context;
	link_service_init_data.tm = tm_rb->tm;

	link_service = dal_link_service_create(&link_service_init_data);

	if (link_service != NULL) {
		/* Register link service with resource manager and
		 * display path */
		bool rc;

		rc = tm_resource_mgr_add_link_service(tm_rb->tm_rm,
				display_path, link_index, link_service);

		if (false == rc) {
			dal_link_service_destroy(&link_service);

			link_service = NULL;
		}
	}

	return link_service;
}

/**
 * Creating link services for each link on the path.
 *
 * \return TM_RESULT_SUCCESS: all link services were created successfully,
 *	TM_RESULT_FAILURE: an error occurred
 */
static enum tm_result tmrb_create_link_services(
		struct tm_resource_builder *tm_rb,
		struct display_path *display_path)
{
	struct connector *connector;
	struct graphics_object_id conn_object_id;
	uint32_t link_index;
	struct link_service *link_service;
	struct dal_context *dal_context = tm_rb->dal_context;

	connector = dal_display_path_get_connector(display_path);
	conn_object_id = dal_connector_get_graphics_object_id(connector);

	/* Iterate over each link on the path */
	for (link_index = 0;
		link_index < dal_display_path_get_number_of_links(display_path);
		link_index++) {

		switch (dal_display_path_get_query_signal(display_path,
				link_index)) {
		/* For display port path ending with DP connector we create
		 * all 3 types of link services since this is
		 * signal-mutable path. */

		case SIGNAL_TYPE_DISPLAY_PORT:
			if (conn_object_id.id == CONNECTOR_ID_DISPLAY_PORT) {

				tmrb_create_link_service(tm_rb, display_path,
						link_index,
						LINK_SERVICE_TYPE_LEGACY);

				tmrb_create_link_service(tm_rb, display_path,
						link_index,
						LINK_SERVICE_TYPE_DP_MST);
			}

			/* Default link service for DP signal is DP-SST */
			link_service = tmrb_create_link_service(tm_rb,
					display_path, link_index,
					LINK_SERVICE_TYPE_DP_SST);

			dal_display_path_set_link_query_interface(
				display_path, link_index, link_service);
			break;

		case SIGNAL_TYPE_DISPLAY_PORT_MST:
			/* TODO: add MST manager creation here */
			link_service = NULL;
			break;

			/* For EDP case signal is not mutable so we need
			 * only DP-SST link service */
		case SIGNAL_TYPE_EDP:
			link_service = tmrb_create_link_service(tm_rb,
					display_path, link_index,
					LINK_SERVICE_TYPE_DP_SST);

			dal_display_path_set_link_query_interface(
				display_path, link_index, link_service);
			break;

		default:
			/* All the rest signals have only legacy link
			 * service */
			link_service = tmrb_create_link_service(tm_rb,
					display_path, link_index,
					LINK_SERVICE_TYPE_LEGACY);

			dal_display_path_set_link_query_interface(
				display_path, link_index, link_service);
			break;
		}

		/* If we do not have default link service, we consider this
		 * display path as invalid. */
		if (link_service == NULL) {
			TM_ERROR("Failed to create default Link Service!");
			tm_resource_mgr_release_path_link_services(tm_rb->tm_rm,
					display_path);
			return TM_RESULT_FAILURE;
		}
	} /* for ()*/

	return TM_RESULT_SUCCESS;
}

/**
 * Marks given resource as active and belonging to a display path.
 * Resources are marked in TM resource structures - so TM knows whether to
 * power up or not.
 * If resource belongs to more then 1 display path, it considered to
 * be multipath resource.
 *
 * \param [in] go: Graphics Object to activate
 */
static enum tm_result tmrb_activate_display_path_resource(
		struct tm_resource_builder *tm_rb,
		struct graphics_object_id id)
{
	struct tm_resource *tm_resource;
	struct dal_context *dal_context = tm_rb->dal_context;

	tm_resource = tm_resource_mgr_find_resource(tm_rb->tm_rm, id);

	if (NULL == tm_resource) {
		/* This implies a serious error - someone has a GO but there
		 * is no TM Resource which 'knows' about it. Possible scenario
		 * is BIOS parser created an object but returns invalid
		 * GO pointer for it. */
		TM_ERROR("%s: No corresponding TM Resource for a Graphics Object!\n",
				__func__);
		return TM_RESULT_FAILURE;
	}

	/* If resource already marked as "display path resource", it means it
	 * becomes multipath */
	if (tm_resource->flags.display_path_resource)
		tm_resource->funcs->set_multi_path(tm_resource, true);

	tm_resource->flags.resource_active = true;
	tm_resource->flags.display_path_resource = true;

	return TM_RESULT_SUCCESS;
}

/**
 * Creates number of branch MST paths based on given root path
 *
 * \param [in] root_display_path: Root MST display path for which we create
 *			branch MST paths
 *
 * \return true: if MST path was created successfully, false: otherwise
 */
static void tmrb_clone_mst_paths(struct tm_resource_builder *tm_rb,
		struct display_path *root_display_path)
{
	union display_path_properties props;
	uint32_t num_of_path_per_connector;
	uint32_t i;
	struct display_path *branch_display_path;
	bool success;
	uint32_t link_index;
	struct dcs *dcs;
	struct connector *connector;
	struct dal_context *dal_context = tm_rb->dal_context;

	TM_ASSERT(root_display_path != NULL);

	props = dal_display_path_get_properties(root_display_path);

	TM_ASSERT(props.bits.IS_ROOT_DP_MST_PATH);

	/* Total number (including root path) of MST paths to create */
	num_of_path_per_connector =
		dal_adapter_service_get_num_of_path_per_dp_mst_connector(
						tm_rb->as);

	connector = dal_display_path_get_connector(root_display_path);

	/* We start looping from 1 since root display path already exists */
	for (i = 1; i < num_of_path_per_connector; i++) {
		/* Create display path */
		branch_display_path = dal_display_path_clone(root_display_path,
				false);

		if (NULL == branch_display_path) {
			TM_ERROR("%s: Failed to clone DisplayPath", __func__);
			continue;
		}

		TM_BUILD_DSP_PATH("  Creating branch-MST display path from [0x%p]. New Display path is [0x%p]\n",
				root_display_path, branch_display_path);

		/* copy signal from source to new display path */
		success =  dal_display_path_set_sink_signal(
				branch_display_path,
				SIGNAL_TYPE_DISPLAY_PORT_MST);

		if (false == success)
			TM_ERROR("%s: Failed to copy sink signal", __func__);

		/* Create DCS */
		if (success) {
			/* Each display path need a DCS to manage the sink
			 * capability, this apply for the duplicated
			 * MST path as well. DDC service in such path is NULL
			 * because MST only path shouldn't have access to
			 * the DDC lines. */
			struct dcs_init_data dcs_init_data;

			dal_memset(&dcs_init_data, 0, sizeof(dcs_init_data));

			dcs_init_data.as = tm_rb->as;
			dcs_init_data.grph_obj_id =
				dal_connector_get_graphics_object_id(connector);
			dcs_init_data.interface_type =
				dal_tm_utils_signal_type_to_interface_type(
						SIGNAL_TYPE_DISPLAY_PORT_MST);
			dcs_init_data.ts = tm_rb->timing_srvc;

			dcs = dal_dcs_create(&dcs_init_data);

			if (NULL != dcs)
				dal_display_path_set_dcs(branch_display_path,
						dcs);
			/* TODO: when DCS is ready, may need to check
			 * return code here and set 'success' accordingly. */
			else {
				TM_BUILD_DSP_PATH("  x Failed to create DCS\n");
				success = false;
			}

			TM_ERROR("%s: Failed to create DCS", __func__);
		}

		/* Update properties and display index */
		if (success) {
			union display_path_properties props;

			props = dal_display_path_get_properties(
					branch_display_path);

			props.bits.IS_BRANCH_DP_MST_PATH = 1;
			props.bits.IS_ROOT_DP_MST_PATH = 0;

			dal_display_path_set_properties(branch_display_path,
					props);

			/* Setup initial display index (might change during
			 * sort later) */
			dal_display_path_set_display_index(branch_display_path,
					tm_rb->num_of_display_paths);
		}

		/* Assign Link Services (branch MST path need *only*
		 * MST Link Services) */
		if (success) {
			struct link_service *link_service;

			for (link_index = 0;
				link_index
					< dal_display_path_get_number_of_links(
							branch_display_path);
				link_index++) {

				link_service = tm_resource_mgr_get_link_service(
					tm_rb->tm_rm,
					root_display_path,
					link_index,
					SIGNAL_TYPE_DISPLAY_PORT_MST);

				if (NULL == link_service) {
					success = false;
					TM_ERROR("%s: Failed to GET Root Link Service for link index:%d!\n",
							__func__,
						link_index);
					break;
				}

				if (TM_RESULT_FAILURE ==
					tm_resource_mgr_add_link_service(
						tm_rb->tm_rm,
						branch_display_path,
						link_index,
						link_service)) {
					success = false;
					TM_ERROR("%s: Failed to add Root Link Service for branch link index:%d!\n",
							__func__,
						link_index);
					break;
				}

				dal_display_path_set_link_query_interface(
						branch_display_path,
						link_index, link_service);
			} /* for () */
		} /* if (success) */

		/* Finally we can add the path to TM array */
		if (success) {

			tm_rb->root_display_paths[tm_rb->num_of_display_paths] =
					root_display_path;

			tm_rb->display_paths[tm_rb->num_of_display_paths] =
					branch_display_path;

			tm_rb->num_of_display_paths++;
		} else {

			TM_BUILD_DSP_PATH("  Destroying unsuccessful branch-MST of display path [0x%p].\n",
					branch_display_path);

			dcs = dal_display_path_get_dcs(branch_display_path);

			if (NULL != dcs)
				dal_dcs_destroy(&dcs);

			dal_display_path_destroy(&branch_display_path);
		}
	} /* for () */
}

/**
 * Once link chain is complete we create and initialise display path.
 *
 * \param [in] init_data: Display Path Init Data which includes link chain
 */
static void tmrb_create_display_path(
		struct tm_resource_builder *tm_rb,
		const struct tm_display_path_init_data *init_data)
{
	struct dal_context *dal_context = tm_rb->dal_context;
	bool success = true;
	struct display_path *display_path;
	uint32_t i;

	TM_ASSERT(init_data != NULL);

	if (init_data->num_of_encoders < 1) {
		TM_ERROR("%s: Invalid number of Encoders:%d!\n", __func__,
				init_data->num_of_encoders);
		return;
	}

	if (NULL == init_data->connector) {
		TM_ERROR("%s: No connector!\n", __func__);
		return;
	}

	/* Create empty display path */
	display_path = dal_display_path_create();
	if (display_path == NULL) {
		TM_ERROR("%s: Failed to allocate display path!\n", __func__);
		return;
	}

	/* Add connector */
	success = dal_display_path_add_connector(display_path,
			init_data->connector);

	if (false == success)
		TM_ERROR("%s: Failed to add connector!\n", __func__);

	if (true == success) {
		/* Add links for each encoder. Order is GPU --> Connector,
		 * which is reversed to how encoders are discovered (starting
		 * at Connector). */

		/* This is the LAST one discovered, so it is the closest
		 * to GPU. */
		i = init_data->num_of_encoders;
		do {
			enum tm_result tm_result;

			i--; /* "index" is less-by-one than "number" */

			tm_result = tmrb_add_link(tm_rb, display_path,
					init_data->encoders[i]);

			if (TM_RESULT_SUCCESS != tm_result) {
				success = false;
				TM_ERROR("%s: Failed to add link!\n",
						__func__);
				break;
			}

		} while (i != 0);
	}

	/* Setup properties and validate display path - it will put
	 * display path in valid state including applying active signal */
	if (true == success) {
		/* Setup properties */
		tmrb_set_display_path_properties(tm_rb, init_data,
				display_path);

		/* Setup initial display index (might change during
		 * sort later) */
		dal_display_path_set_display_index(display_path,
				tm_rb->num_of_display_paths);

		if (false == dal_display_path_validate(display_path,
						init_data->sink_signal)) {
			TM_ERROR("%s: x Failed to validate display path\n",
					__func__);
			success = false;
		}
	}

	/* Set device tag */
	if (true == success) {
		if (TM_RESULT_SUCCESS != tmrb_update_device_tag(tm_rb,
				init_data, display_path)) {
			TM_ERROR("%s: x Failed to update device tag\n",
					__func__);
			success = false;
		}
	}

	/* Create and assign DCS */
	if (success) {
		struct dcs *dcs;
		struct dcs_init_data dcs_init_data;
		struct tm_resource *tm_resource_connector;

		dal_memset(&dcs_init_data, 0, sizeof(dcs_init_data));

		tm_resource_connector =
			tm_resource_mgr_find_resource(
				tm_rb->tm_rm,
				dal_connector_get_graphics_object_id(
					init_data->connector));

		dcs_init_data.as = tm_rb->as;
		dcs_init_data.grph_obj_id = GRPH_ID(tm_resource_connector);
		dcs_init_data.interface_type =
			dal_tm_utils_signal_type_to_interface_type(
					init_data->sink_signal);
		dcs_init_data.ts = tm_rb->timing_srvc;
		dcs_init_data.dal = tm_rb->dal_context;

		dcs = dal_dcs_create(&dcs_init_data);

		if (NULL != dcs) {
			struct ddc_service *ddc_service;

			ddc_service =
				TO_CONNECTOR_INFO(tm_resource_connector)->
				ddc_service;

			if (ddc_service) {

				dal_dcs_update_ddc(dcs, ddc_service);

				dal_display_path_set_dcs(display_path, dcs);
			} else {
				TM_ERROR("%s: DDC service is not initialised!\n",
						__func__);
				success = false;
			}

		} else {
			TM_ERROR("%s: x Failed to create DCS\n", __func__);
			success = false;
		}
	} /* if (success) */

	/* Create link services for each link */
	if (success)
		success = tmrb_create_link_services(tm_rb, display_path);

	/* Mark all resources as active */
	if (success) {
		if (TM_RESULT_SUCCESS != tmrb_activate_display_path_resource(
				tm_rb,
				dal_connector_get_graphics_object_id(
					dal_display_path_get_connector(
						display_path)))) {
			success = false;
			TM_ERROR("%s:Failed to activate Connector resource!\n",
					__func__);
		}
	}

	if (true == success) {
		struct encoder *encoder;
		struct audio *audio;

		/* Walk all links on path and activate all non-NULL
		 * (optional) GOs. */
		for (i = 0;
			i < dal_display_path_get_number_of_links(display_path);
			i++) {

			encoder = dal_display_path_get_upstream_object(
					display_path, i);

			if (encoder &&
				tmrb_activate_display_path_resource(
					tm_rb,
					dal_encoder_get_graphics_object_id(
						encoder)) != TM_RESULT_SUCCESS){
				success = false;
				TM_ERROR("%s:Failed to activate Encoder resource for link:%d!\n",
					__func__, i);
				break;
			}

			audio = dal_display_path_get_audio_object(
				display_path, i);
			if (audio && tmrb_activate_display_path_resource(
				tm_rb,
				dal_audio_get_graphics_object_id(audio)) !=
					TM_RESULT_SUCCESS){
				success = false;
				TM_ERROR("%s:Failed to activate Audio resource for link:%d!\n",
					__func__, i);
				break;
			}
		} /* for() */
	} /* if (success) */

	if (true == success) {
		struct connector_device_tag_info *current_device_tag;

		/* Update DDI channel mapping */
		union ddi_channel_mapping mapping;

		mapping.raw = 0;

		/* TODO: get mapping from AS */
		dal_display_path_set_ddi_channel_mapping(display_path, mapping);

		/* this is not an MST 'root' path (at this point) */
		tm_rb->root_display_paths[tm_rb->num_of_display_paths] = NULL;

		/* Add display path to repository */
		tm_rb->display_paths[tm_rb->num_of_display_paths] =
				display_path;

		tm_rb->num_of_display_paths++;

		current_device_tag = dal_display_path_get_device_tag(
				display_path);

		if (current_device_tag->dev_id.device_type == DEVICE_TYPE_CF)
			tm_rb->num_of_cf_paths++;

	} /* if (success) */

	if (success) {
		/* Set MST Root property and clone MST paths */
		if (NULL != tm_resource_mgr_find_link_service(tm_rb->tm_rm,
				display_path, SIGNAL_TYPE_DISPLAY_PORT_MST)){

			union display_path_properties props;

			props = dal_display_path_get_properties(display_path);

			props.bits.IS_ROOT_DP_MST_PATH = 1;

			dal_display_path_set_properties(display_path, props);

			tmrb_clone_mst_paths(tm_rb, display_path);
		}
	} /* if (success) */

	if (success) {
		TM_BUILD_DSP_PATH("  Display path [0x%p] at index %u was successfully created.\n",
			display_path, tm_rb->num_of_display_paths - 1);
	} else {
		struct dcs *dcs;

		TM_BUILD_DSP_PATH("  Destroying unsuccessful path [0x%p].\n",
				display_path);

		dcs = dal_display_path_get_dcs(display_path);

		if (NULL != dcs)
			dal_dcs_destroy(&dcs);

		dal_display_path_destroy(&display_path);
	}
}

/**
 * Recursively adds link HW components (typically encoder) to display path.
 * Each loop over all sources (from VBIOS) of last added objectId.
 * For every source we try to branch and create new link chain.
 * If no sources exists for this display path (typically when we reached GPU),
 * we create display path and return.
 *
 * \param [in] pInit: Display Path Initialisation Data which includes
 *			"previous" link chain object.
 */

static void tmrb_build_link_chain(struct tm_resource_builder *tm_rb,
		struct tm_display_path_init_data *init_data)
{
	uint32_t num_of_sources;
	uint32_t orig_num_of_encoders = init_data->num_of_encoders;
	uint32_t source;
	struct graphics_object_id src_object_id;
	struct adapter_service *as = tm_rb->as;
	struct tm_resource *tm_resource_input;
	struct tm_resource *tm_resource_src;
	struct graphics_object_id this_object_id;
	struct dal_context *dal_context = tm_rb->dal_context;

	if (init_data->num_of_encoders > MAX_NUM_OF_LINKS_PER_PATH) {
		/* We don't support more Links than MAX_NUM_OF_LINKS_PER_PATH.
		 * Most likely the VBIOS image is corrupted. */
		TM_ERROR("%s: Number of Links=%d exceeds Maximum=%d!\n",
				__func__,
				init_data->num_of_encoders,
				MAX_NUM_OF_LINKS_PER_PATH);
		return;
	}

	if (init_data->num_of_encoders) {
		/* we already discovered at least one encoder */
		tm_resource_input = tm_resource_mgr_find_resource(
			tm_rb->tm_rm,
			dal_encoder_get_graphics_object_id(
				init_data->
				encoders[init_data->num_of_encoders - 1]));
	} else {
		/* no encoders yet, we start at connector */
		tm_resource_input = tm_resource_mgr_find_resource(
			tm_rb->tm_rm,
			dal_connector_get_graphics_object_id(
				init_data->connector));
	}

	this_object_id = GRPH_ID(tm_resource_input);

	num_of_sources = dal_adapter_service_get_src_num(as, this_object_id);

	TM_BUILD_DSP_PATH("%s: Current Object: %s has num_of_sources: %d\n",
		__func__, tm_utils_go_type_to_str(this_object_id),
		num_of_sources);

	if (!num_of_sources) {
		/* Base of recursion - no more sources.
		 * What that really means is that we reached a dead-end.
		 * Since we didn't reach GPU we can't build a path,
		 * so simply return. */
		TM_BUILD_DSP_PATH("%s: No more sources for Object Type: %d\n",
			__func__, this_object_id.type);
		return;
	}

	for (source = 0; source < num_of_sources; source++) {

		src_object_id = dal_adapter_service_get_src_obj(as,
				this_object_id, source);

		if (false == dal_graphics_object_id_is_valid(src_object_id)) {
			TM_ERROR("%s: dal_adapter_service_get_src_obj() returned invalid src!\n",
					__func__);
		}

		/* For each 'source' object (at this level) the starting
		 * number of encoders must be the same - the original. */
		init_data->num_of_encoders = orig_num_of_encoders;

		switch (src_object_id.type) {
		case OBJECT_TYPE_ENCODER: {
			struct encoder *enc;

			tm_resource_src = tm_resource_mgr_find_resource(
					tm_rb->tm_rm, src_object_id);

			if (tm_resource_src == NULL) {
				/* TM resource for this encoder was not
				 * created yet. */
				struct encoder_init_data enc_init_data;

				dal_memset(&enc_init_data, 0,
						sizeof(enc_init_data));

				enc_init_data.adapter_service = as;
				enc_init_data.encoder = src_object_id;
				enc_init_data.ctx = dal_context;

				enc = dal_encoder_create(&enc_init_data);

				if (!enc) {
					TM_ERROR("%s: Failed to create Encoder",
						__func__);
					break;
				}

				tm_resource_src =
					dal_tm_resource_mgr_add_resource(
						tm_rb->tm_rm,
						dal_tm_resource_encoder_create(
							enc));
			} else {
				enc = TO_ENCODER(tm_resource_src);
			}

			if (NULL == tm_resource_src) {
				TM_ERROR("%s: Failed to add Encoder Resource to display path",
						__func__);
			} else {
				init_data->
					encoders[init_data->num_of_encoders] =
						enc;

				init_data->num_of_encoders++;

				/* recursive call only for Encoder */
				tmrb_build_link_chain(tm_rb, init_data);
			}
			break;
		}
		case OBJECT_TYPE_GPU:
			TM_BUILD_DSP_PATH("%s: reached GPU. Building path...\n",
				__func__);

			tmrb_create_display_path(tm_rb, init_data);
			break;

		default:
			TM_ERROR("%s: Unknown graphics object!", __func__);
			break;

		} /* switch ()*/
	} /* for () */
}

static void tmrb_build_single_display_path(struct tm_resource_builder *tm_rb,
		uint8_t connector_index)
{
	struct dal_context *dal_context = tm_rb->dal_context;
	struct graphics_object_id connector_obj_id;
	struct connector *connector;
	struct tm_resource *tm_resource;
	struct connector_signals default_signals;
	uint32_t signal;
	struct tm_display_path_init_data path_init_data;
	struct ddc_service_init_data ddc_init_data;

	connector_obj_id = dal_adapter_service_get_connector_obj_id(tm_rb->as,
			connector_index);

	if (connector_obj_id.type != OBJECT_TYPE_CONNECTOR) {
		TM_WARNING("%s: Invalid Connector ObjectID from Adapter Service for connector index:%d!\n",
				__func__, connector_index);
		return;
	}

	/* Note that 'connector' will be deallocated when 'tm_rb->tm_rm' is
	 * destroyed, so Resource Builder should not worry about it. */
	connector = dal_connector_create(tm_rb->as, connector_obj_id);
	if (NULL == connector) {
		TM_WARNING("%s: Failed to create connector object!\n",
				__func__);
		return;
	}

	tm_resource = dal_tm_resource_mgr_add_resource(tm_rb->tm_rm,
		dal_tm_resource_connector_create(connector));

	if (NULL == tm_resource) {
		TM_WARNING("TM_RB: failed to add connector resource for connector index: %d\n",
				connector_index);
		return;
	}

	dal_memset(&path_init_data, 0, sizeof(path_init_data));

	ddc_init_data.as = tm_rb->as;
	ddc_init_data.id = connector_obj_id;
	ddc_init_data.ctx = dal_context;
	/* Note that DDC Service is freed by Resource Manager via call to
	 * dal_ddc_service_destroy(). */
	TO_CONNECTOR_INFO(tm_resource)->ddc_service =
		dal_ddc_service_create(&ddc_init_data);

	if (NULL == TO_CONNECTOR_INFO(tm_resource)->ddc_service) {
		TM_WARNING("TM_RB: failed to create DDC service for connector index:%d!\n",
				connector_index);
		return;
	}

	path_init_data.ddc_service =
		TO_CONNECTOR_INFO(tm_resource)->ddc_service;
	path_init_data.connector = TO_CONNECTOR_INFO(tm_resource)->connector;

	/* This is a real path so set fake path device type as unknown. */
	path_init_data.faked_path_device_id.device_type = DEVICE_TYPE_UNKNOWN;

	default_signals = dal_connector_get_default_signals(connector);

	TM_BUILD_DSP_PATH(
		"TM_RB: connector_index:%d: num_of_default_signals:%d\n",
			connector_index, default_signals.number_of_signals);

	/* Loop over all default signals of the connector. */
	for (signal = 0; signal < default_signals.number_of_signals; signal++) {

		path_init_data.num_of_encoders = 0;
		path_init_data.sink_signal = default_signals.signal[signal];

		tmrb_build_link_chain(tm_rb, &path_init_data);
	}

	TM_BUILD_DSP_PATH("Finished building display paths for connector index: %d.\n",
			connector_index);
}

/**
 * Builds display paths. Goes over all connectors expanding connector
 * default signals.
 * For every such signal tries to create display path which starts at
 * current connector.
 *
 * \param tm_rb A pointer to TM Resource Builder.
 *
 * \return true - no error. false - error.
 */
enum tm_result tm_resource_builder_build_display_paths(
		struct tm_resource_builder *tm_rb)
{
	uint8_t connectors_num;
	uint8_t connector_index;
	struct dal_context *dal_context = tm_rb->dal_context;

	TM_IFACE_TRACE();

	connectors_num = dal_adapter_service_get_connectors_num(tm_rb->as);

	if (connectors_num == 0 || connectors_num > ENUM_ID_COUNT) {
		TM_ERROR("%s: Invalid Number of Connectors: %d!\n",
				__func__, connectors_num);
		return TM_RESULT_FAILURE;
	}

	/* As a first thing we need to allocate storage for link services.
	 * We don't know how much storage is needed, so start from
	 * some default. */
	if (TM_RESULT_FAILURE == tm_resource_mgr_setup_link_storage(
			tm_rb->tm_rm, TM_RB_MAX_NUM_OF_DISPLAY_PATHS)) {
		TM_ERROR("%s: tm_resource_mgr_setup_link_storage() failed!\n",
				__func__);
		return TM_RESULT_FAILURE;
	}

	/* loop over all connectors */
	for (connector_index = 0;
		connector_index < connectors_num;
		connector_index++) {

		tmrb_build_single_display_path(tm_rb, connector_index);
	}

	return TM_RESULT_SUCCESS;
}


enum tm_result tm_resource_builder_add_fake_display_paths(
		struct tm_resource_builder *tm_rb)
{
	struct dal_context *dal_context = tm_rb->dal_context;

	TM_NOT_IMPLEMENTED();
	return TM_RESULT_SUCCESS;
}

/**
 * \brief
 *	Creates and adds resources required for audio
 *
 * \return
 *	returns Number of successfully added audio resources
 */
static uint32_t add_audio_resources(struct tm_resource_builder *tm_rb)
{
	uint32_t index = 0;
	struct graphics_object_id obj_id;
	struct tm_resource *tm_resource = NULL;
	struct audio *audio = NULL;
	struct audio_init_data init_data;
	struct dal_context *dal_context = tm_rb->dal_context;

	do {
		obj_id = dal_adapter_service_enum_audio_object(tm_rb->as,
				index);

		if (false == dal_graphics_object_id_is_valid(obj_id)) {
			/* no more valid audio objects */
			break;
		}

		dal_memset(&init_data, 0, sizeof(init_data));

		init_data.as = tm_rb->as;
		init_data.audio_stream_id = obj_id;
		init_data.dal_context = tm_rb->dal_context;

		audio = dal_audio_create(&init_data);
		if (NULL == audio) {
			TM_ERROR("%s: dal_audio_create() failed!\n", __func__);
			break;
		}

		tm_resource = dal_tm_resource_mgr_add_resource(
			tm_rb->tm_rm,
			dal_tm_resource_audio_create(audio));
		if (NULL == tm_resource) {
			TM_ERROR("%s: failed add audio resource!\n", __func__);
			dal_audio_destroy(&audio);
			break;
		}

		tm_resource->flags.resource_active = true;
		index++;

	} while (1);

	TM_RESOURCES("Audio resource count:%d\n", index);

	return index; /* this is the count of added audio resources */
}

/* Add feature such as: Audio, Stereo... */
enum tm_result tm_resource_builder_add_feature_resources(
		struct tm_resource_builder *tm_rb)
{
	if (!tm_rb->num_of_display_paths) {
		/* If there are no paths then we can't add features. */
		return TM_RESULT_FAILURE;
	}

	add_audio_resources(tm_rb);

	return TM_RESULT_SUCCESS;
}

/**
 * Swaps entries of two displays, including corresponding link services.
 *
 * \param [in] index1: display 1 to swap
 * \param [in] index2: display 2 to swap
 */
static void tmrb_swap_entries(struct tm_resource_builder *tm_rb,
		unsigned int index1, unsigned int index2) {
	struct display_path *display_path;

	display_path = tm_rb->display_paths[index1];
	tm_rb->display_paths[index1] = tm_rb->display_paths[index2];
	tm_rb->display_paths[index2] = display_path;

	display_path = tm_rb->root_display_paths[index1];
	tm_rb->root_display_paths[index1] = tm_rb->root_display_paths[index2];
	tm_rb->root_display_paths[index2] = display_path;

	dal_display_path_set_display_index(tm_rb->display_paths[index1],
			index1);

	dal_display_path_set_display_index(tm_rb->display_paths[index2],
			index2);

	tm_resource_mgr_swap_link_services(tm_rb->tm_rm, index1, index2);
}

static void tm_resource_builder_sort_display_paths_by_dev_priority(
		struct tm_resource_builder *tm_rb)
{
	uint32_t num_of_sorted_paths = 0;
	uint32_t type;
	uint32_t i;
	struct connector_device_tag_info *connector_device_tag_info;

	/* Sort by device type priority */
	for (type = 0; type < tmrb_num_of_devices_in_order_enumeration;
			type++) {
		for (i = num_of_sorted_paths;
				i < tm_rb->num_of_display_paths
					&& num_of_sorted_paths
						< tm_rb->num_of_display_paths;
				i++) {

			connector_device_tag_info =
				dal_display_path_get_device_tag(
					tm_rb->display_paths[i]);

			if (connector_device_tag_info->dev_id.device_type ==
					tmrb_device_enumeration_order[type]){

				tmrb_swap_entries(tm_rb,
					num_of_sorted_paths, i);
				num_of_sorted_paths++;
			}
		} /* for () */
	} /* for () */
}

static void tm_resource_builder_sort_display_paths_by_dev_enum(
		struct tm_resource_builder *tm_rb)
{
	bool flipped;
	uint32_t i;
	struct connector_device_tag_info *current_device_tag_info;
	struct connector_device_tag_info *next_device_tag_info;

	flipped = true;
	while (flipped) {
		flipped = false;
		for (i = 0; i < tm_rb->num_of_display_paths - 1; i++) {

			current_device_tag_info =
				dal_display_path_get_device_tag(
					tm_rb->display_paths[i]);

			next_device_tag_info =
				dal_display_path_get_device_tag(
					tm_rb->display_paths[i + 1]);

			if (current_device_tag_info->dev_id.device_type !=
				DEVICE_TYPE_UNKNOWN &&
				(current_device_tag_info->dev_id.device_type ==
				next_device_tag_info->dev_id.device_type) &&
				(current_device_tag_info->dev_id.enum_id >
				next_device_tag_info->dev_id.enum_id)) {

				tmrb_swap_entries(tm_rb, i, i + 1);
				flipped = true;

			}
		} /* for () */
	} /* while () */
}

/* Move root MST paths before branch MST paths */
static void tmrb_move_root_mst_paths_before_branch_mst_paths(
		struct tm_resource_builder *tm_rb)
{
	uint32_t i;
	uint32_t j;

	for (i = 0; i < tm_rb->num_of_display_paths - 1; i++) {

		if (tm_rb->root_display_paths[i] != NULL) {

			for (j = i + 1; j < tm_rb->num_of_display_paths; j++) {

				if (tm_rb->display_paths[j] ==
						tm_rb->root_display_paths[i]) {
					tmrb_swap_entries(tm_rb, i, j);
					break;
				}
			} /* for () */
		} /* if () */
	} /* for () */
}

/**
 * Reads default display type from persistent storage.
 *
 * \return TM Display type matching default display stored in
 *		persistent storage.
 */
enum tm_display_type tmrb_get_default_display_type(
		struct tm_resource_builder *tm_rb)
{
	struct dal_context *dal_context = tm_rb->dal_context;

	TM_NOT_IMPLEMENTED();
	return TM_DISPLAY_TYPE_UNK;
}

static void tmrb_put_default_display_on_top_of_the_list(
		struct tm_resource_builder *tm_rb)
{
	uint32_t num_of_sorted_paths = 0;
	uint32_t i;
	enum tm_display_type default_display_type =
			tmrb_get_default_display_type(tm_rb);
	struct connector_device_tag_info *device_tag_info;

	if (TM_DISPLAY_TYPE_UNK == default_display_type)
		return;

	num_of_sorted_paths = 0;
	for (i = num_of_sorted_paths;
		i < tm_rb->num_of_display_paths
			&& num_of_sorted_paths < tm_rb->num_of_display_paths;
		i++) {

		device_tag_info = dal_display_path_get_device_tag(
				tm_rb->display_paths[i]);

		if (default_display_type ==
			tm_utils_device_id_to_tm_display_type(
				device_tag_info->dev_id)) {

			tmrb_swap_entries(tm_rb, num_of_sorted_paths, i);
			num_of_sorted_paths++;
		}
	}
}

void tm_resource_builder_sort_display_paths(struct tm_resource_builder *tm_rb)
{
	struct dal_context *dal_context = tm_rb->dal_context;

	TM_ASSERT(tm_rb->num_of_display_paths > 0);

	if (tm_rb->num_of_display_paths <= 0)
		return;

	/* Sort by device type priority */
	tm_resource_builder_sort_display_paths_by_dev_priority(tm_rb);

	/* Sort by device enum (within same device type) */
	tm_resource_builder_sort_display_paths_by_dev_enum(tm_rb);

	/* Move root MST paths before branch MST paths */
	tmrb_move_root_mst_paths_before_branch_mst_paths(tm_rb);

	/* Feature 8464. Put default display on top of the list */
	tmrb_put_default_display_on_top_of_the_list(tm_rb);
}

uint32_t tm_resource_builder_get_num_of_paths(
		struct tm_resource_builder *tm_rb)
{
	return tm_rb->num_of_display_paths;
}

struct display_path *tm_resource_builder_get_path_at(
		struct tm_resource_builder *tm_rb,
		uint32_t index)
{
	struct dal_context *dal_context = tm_rb->dal_context;

	TM_ASSERT(index < tm_rb->num_of_display_paths);

	if (index < tm_rb->num_of_display_paths)
		return tm_rb->display_paths[index];

	return NULL;
}
