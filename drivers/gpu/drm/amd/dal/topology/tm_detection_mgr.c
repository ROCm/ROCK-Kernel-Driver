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
#include "tm_utils.h"
#include "tm_detection_mgr.h"
#include "tm_internal_types.h"
#include "topology.h"
#include "include/i2caux_interface.h"
#include "include/connector_interface.h"
#include "include/dcs_interface.h"
#include "include/encoder_interface.h"
#include "include/flat_set.h"


/*****************************************************************************
 *	private data structures
 ***************************************************************************/

struct display_state {
	struct display_sink_capability sink_cap;
	uint32_t edid_len;
	uint8_t edid[512];
	bool audio_cap;
};

/*************************************
 * Structure for registration for IRQ.
 *************************************/
#define MAX_NUM_OF_PATHS_PER_CONNECTOR	2

enum tmdm_timer_irq_state {
	TMDM_TMR_STATE_NOT_REGISTERED,
	TMDM_TMR_STATE_REGISTERED,/* registered, currently in timer queue */
	TMDM_TMR_STATE_CANCELLED /* registered, but we don't want it anymore */
};

#define TMDM_DECODE_TIMER_STATE(state) \
	(state == TMDM_TMR_STATE_NOT_REGISTERED ? \
			"TMDM_TMR_STATE_NOT_REGISTERED" : \
	state == TMDM_TMR_STATE_REGISTERED ? \
			"TMDM_TMR_STATE_REGISTERED" : \
	state == TMDM_TMR_STATE_CANCELLED ? \
			"TMDM_TMR_STATE_CANCELLED" : "Invalid")

struct tmdm_irq_entry {
	enum dal_irq_source irq_src;
	irq_handler_idx handler_index_ref;
	/* This flag protects against nesting of timers for the same
	 * connector.
	 * If we allow nesting of timers we have no way of knowing which timer
	 * was fired when the handler runs.
	 * All access to 'timer_state' must be done *only* via
	 * tmdm_set_timer_state() and tmdm_get_timer_state()!*/
	enum tmdm_timer_irq_state timer_state;
};

struct tmdm_hpd_flags {
	/* Controls use of SW Timer for de-bounce of HPD interrupts. */
	bool SW_HPD_FILTERING:1;
	/* HW Capability to filter (de-bounce) HPD interrupts. */
	bool HW_HPD_FILTERING:1;
};

/* This structure is allocated dynamically for each Connector. */
struct tmdm_irq_registration {
	struct graphics_object_id connector_id;
	struct connector *connector;

	/* Access to 'hpd_flags' only via sw_hpd_filter_??? functions! */
	struct tmdm_hpd_flags hpd_flags;

	/* Number of times detection was continuously rescheduled */
	uint32_t resched_count;

	struct display_path *connected_display;
	/* A DP connector can have a passive dongle plugged, so it can be
	 * registered for more than one Display Path. One registration is for
	 * DP and another is for HDMI. */
	struct display_path *displays[MAX_NUM_OF_PATHS_PER_CONNECTOR];
	uint32_t displays_num;

	/* A connector may register for both HPD and Timer interrupt.
	 * 'entries' stores information related to each type of interrupt. */
	struct tmdm_irq_entry entries[TM_INTERRUPT_TYPE_COUNT];

	/* Pointer back to Detection Manager. We will use this pointer in
	 * interrupt handlers, because 'struct tmdm_irq_registration'
	 * will be passed in as interrupt context. */
	struct tm_detection_mgr *detection_mgr;
};

/* irq entry accessor macro definitions */
#define IRQ_ENTRY(irq_reg, type) (&irq_reg->entries[type])
#define IRQ_ENTRY_HPD(irq_reg) IRQ_ENTRY(irq_reg, TM_INTERRUPT_TYPE_HOTPLUG)
#define IRQ_ENTRY_TIMER(irq_reg) IRQ_ENTRY(irq_reg, TM_INTERRUPT_TYPE_TIMER)


struct tm_detection_mgr {
	struct dal_context *dal_context;
	struct adapter_service *as;
	struct hw_sequencer *hwss;
	struct tm_resource_mgr *resource_mgr;
	struct topology_mgr *tm_hpd_callback;

	/*detection options, irq source, irq handler of connectors*/
	struct tmdm_irq_registration *connector_irq_regsitrations;
	/* One irq registration for each connector, so it is the same
	 * as number of connectors. */
	uint8_t irq_registrations_num;

	bool is_blocking_detection;
	bool is_blocking_interrupts;
};

enum {
	DELAY_ON_CONNECT_IN_MS = 500,
	DELAY_ON_DISCONNECT_IN_MS = 100,
	DP_PASSIVE_DONGLE_INTERVAL_IN_MS = 500,
	RESCHED_TIMER_INTERVAL_IN_MS = 3000,
	NUM_OF_DETECTION_RETRIES = 1,
	NUM_OF_LOCK_RETRIES = 50,
	LOCK_RETRY_INTERVAL_IN_MS = 1,
	MICROSECONDS_IN_MILLISECOND = 1000
};

/*****************************************************************************
 *	prototypes of static functions
 *****************************************************************************/

static void tmdm_handle_hpd_interrupt(
		void *interrupt_params);

static void tmdm_handle_timer_interrupt(
		void *interrupt_params);

static void tmdm_set_timer_state(struct tm_detection_mgr *tm_dm,
		struct tmdm_irq_entry *irq_entry,
		enum tmdm_timer_irq_state new_timer_state);

/* HPD Filter - related */
static void sw_hpd_filter_set(struct tmdm_irq_registration *connector_irq,
		bool new_val);
static void hw_hpd_filter_set(struct tmdm_irq_registration *connector_irq,
		bool new_val);

static bool sw_hpd_filter_get(struct tmdm_irq_registration *irq_reg);
static bool hw_hpd_filter_get(struct tmdm_irq_registration *irq_reg);

/*****************************************************************************
 *	static functions
 *****************************************************************************/
static bool construct(
		struct tm_detection_mgr *tm_dm,
		struct tm_detection_mgr_init_data *init_data)
{
	uint8_t i;
	uint8_t j;
	struct tmdm_irq_registration *irq_regsitration;
	struct tmdm_irq_entry *irq_entry;

	tm_dm->dal_context = init_data->dal_context;
	tm_dm->as = init_data->as;
	tm_dm->hwss = init_data->hwss;
	tm_dm->resource_mgr = init_data->resource_mgr;
	tm_dm->tm_hpd_callback = init_data->tm;
	tm_dm->is_blocking_detection = false;
	tm_dm->is_blocking_interrupts = false;
	tm_dm->irq_registrations_num =
			dal_adapter_service_get_connectors_num(tm_dm->as);

	if (tm_dm->irq_registrations_num == 0)
		return false;

	tm_dm->connector_irq_regsitrations = dal_alloc(
		tm_dm->irq_registrations_num *
			sizeof(struct tmdm_irq_registration));

	/** Reset all entries. Please note, at this point it is impossible
	 *  to initialize entries properly since connector OBJECTS (and their
	 *  properties) do not exist yet.
	 *
	 *  While topology creation, it will register display for detection
	 *  register_display. connector will be filled with connector from
	 *  display path. After this, display_path->connector->go_base.id =
	 *  tm_dm->connector_irq_regsitrations[i].id.
	 */
	for (i = 0; i < tm_dm->irq_registrations_num; ++i) {

		irq_regsitration = &tm_dm->connector_irq_regsitrations[i];

		irq_regsitration->detection_mgr = tm_dm;

		irq_regsitration->connector_id =
				dal_adapter_service_get_connector_obj_id(
						tm_dm->as, i);
		irq_regsitration->displays_num = 0;
		sw_hpd_filter_set(irq_regsitration, false);
		hw_hpd_filter_set(irq_regsitration, false);
		irq_regsitration->resched_count = 0;

		for (j = 0; j < TM_INTERRUPT_TYPE_COUNT; ++j) {
			irq_entry = IRQ_ENTRY(irq_regsitration, j);

			irq_entry->irq_src = DAL_IRQ_SOURCE_INVALID;
			irq_entry->handler_index_ref =
					DAL_INVALID_IRQ_HANDLER_IDX;
			tmdm_set_timer_state(tm_dm, irq_entry,
					TMDM_TMR_STATE_NOT_REGISTERED);
		}
	}

	return true;
}

static void destruct(
		struct tm_detection_mgr *tm_dm)
{
	if (tm_dm->connector_irq_regsitrations != NULL)
		dal_free(tm_dm->connector_irq_regsitrations);
}

static struct graphics_object_id get_connector_obj_id(
		struct display_path *display_path)
{
	return dal_connector_get_graphics_object_id(
		dal_display_path_get_connector(display_path));
}

static bool handle_skipping_detection(
		struct tm_detection_mgr *tm_dm,
		struct display_path *display_path,
		enum tm_detection_method method,
		struct tm_detection_status *detection_status)
{
	union display_path_properties properties;
	struct dal_context *dal_context = tm_dm->dal_context;

	if (method == DETECTION_METHOD_CACHED)
		return true;

	if (method == DETECTION_METHOD_CONNECTED_ONLY &&
		!dal_display_path_is_target_connected(display_path))
		return true;

	TM_ASSERT(display_path != NULL);
	TM_ASSERT(detection_status != NULL);

	properties = dal_display_path_get_properties(display_path);

	/*For MST branch display paths detection is done by MST Manager*/
	if (properties.bits.IS_BRANCH_DP_MST_PATH) {
		detection_status->dp_mst_detection = true;
		return true;
	}

	/**For embedded display (LVDS/eDP) we skip the HW detection
	 * (unless explicitly requested to do detection)
	 */
	if (method != DETECTION_METHOD_DESTRUCTIVE_AND_EMBEDDED &&
		dal_is_embedded_signal(
			dal_display_path_get_query_signal(display_path,
						SINK_LINK_INDEX)))
		return true;

	/**If the display path is faked (for Gemini Slave and Stream computing)
	 * it's not connected
	 */
	if (properties.bits.FAKED_PATH) {
		detection_status->connected = false;
		return true;
	}

	/**If "force connected" state has been set for the display path
	 * then no detection is needed
	 */
	if (properties.bits.ALWAYS_CONNECTED) {
		detection_status->connected = true;
		return true;
	}

	/**If we reached here, detection was not handled as
	 * "skipped" so we probably need to do physical detection
	 */
	return false;
}

/**
 *****************************************************************************
 * Obtains pointer to IRQ registration.
 *
 * \param [in] connector: connector ID which identifies IRQ registration entry
 *
 * \return   Pointer to entry if such found, NULL otherwise
 *****************************************************************************
 */
static struct tmdm_irq_registration *get_irq_entry(
		struct tm_detection_mgr *tm_dm,
		struct graphics_object_id connector)
{
	uint8_t i;

	for (i = 0; i < tm_dm->irq_registrations_num; ++i) {
		if (dal_graphics_object_id_is_equal(
			tm_dm->connector_irq_regsitrations[i].connector_id,
			connector))
			return &tm_dm->connector_irq_regsitrations[i];
	}
	return NULL;
}

/**
 *****************************************************************************
 *  Function: get_irq_source
 *
 *  @brief
 *     Returns IRQSource matching given connector + interrupt type
 *       x Timer interrupt - not associated with connector
 *       x Hotplug interrupt associated with connector HPD line
 *       x DDC polling interrupt associated with  connector DDC line
 *       x Sink status (short pulse) interrupt associated with connector DDC
 *        line
 *          (though interrupt occurs on hpd line)
 *
 *  @param [in] connector: connector ID with HPD and DDC lines
 *  @param [in] interruptType: TM interrupt type
 *
 *  @return
 *     IRQSource matching given connector + interrupt type
 *****************************************************************************
 */
static enum dal_irq_source get_irq_source(
		struct tm_detection_mgr *tm_dm,
		struct graphics_object_id connector,
		enum tm_interrupt_type type)
{
	enum dal_irq_source irq_src = DAL_IRQ_SOURCE_INVALID;

	switch (type) {
	case TM_INTERRUPT_TYPE_TIMER:
		irq_src = DAL_IRQ_SOURCE_TIMER;
		break;

	case TM_INTERRUPT_TYPE_HOTPLUG: {
		struct irq *hpd_gpio;

		hpd_gpio = dal_adapter_service_obtain_hpd_irq(tm_dm->as,
				connector);

		if (hpd_gpio != NULL) {
			irq_src = dal_irq_get_source(hpd_gpio);
			dal_adapter_service_release_irq(tm_dm->as, hpd_gpio);
		}
		break;
	} /* switch() */

	default:
		break;
	}
	return irq_src;
}

static void tmdm_set_timer_state(struct tm_detection_mgr *tm_dm,
		struct tmdm_irq_entry *irq_entry,
		enum tmdm_timer_irq_state new_timer_state)
{
	struct dal_context *dal_context = tm_dm->dal_context;

	TM_HPD_IRQ("%s:from: %s --> to: %s\n", __func__,
			TMDM_DECODE_TIMER_STATE(irq_entry->timer_state),
			TMDM_DECODE_TIMER_STATE(new_timer_state));

	irq_entry->timer_state = new_timer_state;
}

static enum tmdm_timer_irq_state tmdm_get_timer_state(
		struct tmdm_irq_entry *irq_entry)
{
	return irq_entry->timer_state;
}

static void sw_hpd_filter_set(struct tmdm_irq_registration *connector_irq,
		bool new_val)
{
	struct tm_detection_mgr *tm_dm = connector_irq->detection_mgr;
	struct dal_context *dal_context = tm_dm->dal_context;

	TM_HPD_IRQ("irq_src=%d: SW_HPD_FILTERING: %d --> to: %d\n",
			IRQ_ENTRY_HPD(connector_irq)->irq_src,
			connector_irq->hpd_flags.SW_HPD_FILTERING,
			new_val);

	connector_irq->hpd_flags.SW_HPD_FILTERING = new_val;
}

static bool sw_hpd_filter_get(struct tmdm_irq_registration *irq_reg)
{
	return irq_reg->hpd_flags.SW_HPD_FILTERING;
}

static void hw_hpd_filter_set(struct tmdm_irq_registration *connector_irq,
		bool new_val)
{
	struct tm_detection_mgr *tm_dm = connector_irq->detection_mgr;
	struct dal_context *dal_context = tm_dm->dal_context;

	TM_HPD_IRQ("irq_src=%d: HW_HPD_FILTERING: %d --> to: %d\n",
			IRQ_ENTRY_HPD(connector_irq)->irq_src,
			connector_irq->hpd_flags.HW_HPD_FILTERING,
			new_val);

	connector_irq->hpd_flags.HW_HPD_FILTERING = new_val;
}

static bool hw_hpd_filter_get(struct tmdm_irq_registration *irq_reg)
{
	return irq_reg->hpd_flags.HW_HPD_FILTERING;
}

/**
 *****************************************************************************
 *  Function: allow_aux_while_hpd_low
 *
 *  @brief
 *     Configures aux to allow transactions while HPD is low
 *
 *  @param [in]  display_path:     Display path on which to perform operation
 *  @param [in]  allow:         if true allow aux transactions while HPD is low
 *****************************************************************************
 */
static void allow_aux_while_hpd_low(
		struct tm_detection_mgr *tm_dm,
		struct display_path *display_path,
		bool allow)
{
	union aux_config aux_flags;
	struct ddc *ddc;

	aux_flags.raw = 0;
	aux_flags.bits.ALLOW_AUX_WHEN_HPD_LOW = allow;

	ddc = dal_adapter_service_obtain_ddc(tm_dm->as,
			get_connector_obj_id(display_path));
	if (ddc != NULL) {
		dal_i2caux_configure_aux(dal_adapter_service_get_i2caux(
			tm_dm->as),
			ddc, aux_flags);

		dal_adapter_service_release_ddc(tm_dm->as, ddc);
	}
}

/**
 *****************************************************************************
 *  Function: need_handle_connection_status_based_on_sink_count
 *
 *  @brief : check whether we need handle dongle sink count info. only check
 *  sink_count == 0 for some known dongle or if runtime parameter exist.
 *
 *  @return
 *
 *****************************************************************************
 */
bool need_handle_connection_status_based_on_sink_count(
		struct tm_detection_mgr *tm_dm,
		struct tm_detection_status *detection_status)
{
	enum display_dongle_type dongle_type;
	dongle_type = detection_status->sink_capabilities.dongle_type;

	/*limit this sink count to active dongle only.*/
	if (detection_status == NULL)
		return false;

	if (!((detection_status->
		sink_capabilities.downstrm_sink_count_valid == true) ||
		dal_adapter_service_is_feature_supported(
			FEATURE_DONGLE_SINK_COUNT_CHECK)))
		return false;


	if (((dongle_type == DISPLAY_DONGLE_DP_VGA_CONVERTER) ||
		(dongle_type == DISPLAY_DONGLE_DP_DVI_CONVERTER) ||
		(dongle_type == DISPLAY_DONGLE_DP_HDMI_CONVERTER)) &&
		(detection_status->sink_capabilities.downstrm_sink_count == 0))
		return true;

	return false;
}

/**
 *****************************************************************************
 *  Function: apply_load_detection_based_edid_patch
 *
 *  @brief
 *     Applies any needed load detection based on EDID patch presence.
 *
 *  @param [in]  display_path:     Display path on which to perform
 *  detection
 *  @param [int] detection_status: Structure which holds detection-related
 *   info from current detection procedure.
 *
 *  @return
 *     true if patch was applied, false otherwise
 *****************************************************************************
 */
static bool apply_load_detection_based_edid_patch(
		struct tm_detection_mgr *tm_dm,
		struct display_path *display_path,
		const struct tm_detection_status *detection_status)
{
	bool apply_patch = false;
	union dcs_monitor_patch_flags patch_flags;
	struct monitor_patch_info patch_info;
	struct dal_context *dal_context = tm_dm->dal_context;

	patch_info.type = MONITOR_PATCH_TYPE_DUAL_EDID_PANEL;
	patch_info.param = 1;

	patch_flags = dal_dcs_get_monitor_patch_flags(
			dal_display_path_get_dcs(display_path));

	/*Dual EDID display support*/
	if (patch_flags.flags.DUAL_EDID_PANEL) {
		struct display_path *temp_path;
		struct graphics_object_id temp_id;
		struct graphics_object_id path_id;
		enum signal_type signal;

		if (SIGNAL_TYPE_RGB == detection_status->detected_signal) {
			signal = dal_hw_sequencer_detect_load(
				tm_dm->hwss, display_path);
			if (SIGNAL_TYPE_RGB == signal)
				apply_patch = true;
			/** If it is a DP_VGA active dongle, force the edid to
			 * analog,  without the detection check
			 */
		} else if (detection_status->sink_capabilities.dongle_type
				== DISPLAY_DONGLE_DP_VGA_CONVERTER) {
			enum dcs_edid_connector_type conn_type =
				dal_dcs_get_connector_type(
					dal_display_path_get_dcs(display_path));

			if (conn_type != EDID_CONNECTOR_ANALOG)
				apply_patch = true;
			/**if we're currently detecting DVI, we force analog
			 * edid only if there is load detected on the CRT on
			 * the same connector as current DVI
			 */
		} else if (dal_is_dvi_signal(
				detection_status->detected_signal)) {
			uint8_t i;
			struct tmdm_irq_registration *connector_irq;

			connector_irq = get_irq_entry(tm_dm,
					get_connector_obj_id(display_path));

			TM_ASSERT(connector_irq != NULL);

			for (i = 0; i < connector_irq->displays_num; ++i) {
				enum tm_result tm_ret;

				temp_path = connector_irq->displays[i];

				/*if connectors are the same, and the temp
				 * display path's signal type is RGB (CRT).
				 * TODO : what does temp_path !=
				 * display_path mean? debug
				 * if (temp_path != display_path &&
				 *  dal_display_path_get_query_signal
				 * (temp_path, SINK_LINK_INDEX) ==
				 * SIGNAL_TYPE_RGB)
				 */
				temp_id = get_connector_obj_id(temp_path);
				path_id = get_connector_obj_id(display_path);
				signal = dal_display_path_get_query_signal(
						temp_path,
						SINK_LINK_INDEX);

				if (dal_graphics_object_id_is_equal(
					temp_id, path_id) ||
					signal != SIGNAL_TYPE_RGB)
					continue;

				tm_ret =
					tm_resource_mgr_acquire_resources(
						tm_dm->resource_mgr,
						temp_path,
					TM_ACQUIRE_METHOD_SW);
				if (TM_RESULT_SUCCESS == tm_ret) {
					signal =
						dal_hw_sequencer_detect_load(
							tm_dm->hwss,
							temp_path);
					apply_patch =
						(signal == SIGNAL_TYPE_RGB);

					tm_resource_mgr_release_resources(
						tm_dm->resource_mgr,
						temp_path,
					TM_ACQUIRE_METHOD_SW);
				} else
					BREAK_TO_DEBUGGER();
			}
		}
	}

	/* apply the patch if needed. In this context "apply" actually refers
	 * to "setup patch value"
	 */
	if (apply_patch)
		if (!dal_dcs_set_monitor_patch_info(
			dal_display_path_get_dcs(display_path), &patch_info)) {
			BREAK_TO_DEBUGGER();
			apply_patch = false;
		}

	return apply_patch;
}

/**
 *****************************************************************************
 *  Function: apply_detection_status_patches
 *
 *  @brief
 *     Apply EDID-based workarounds to sink capabilities.
 *
 *  @param [in]  display_path:     Display path on which to perform detection
 *  @param [int] detection_status: Structure which holds detection-related info
 *  from current detection procedure.
 *****************************************************************************
 */
static void apply_detection_status_patches(
		struct tm_detection_mgr *tm_dm,
		struct display_path *display_path,
		struct tm_detection_status *detection_status)
{
	union dcs_monitor_patch_flags patch_flags =
			dal_dcs_get_monitor_patch_flags(
				dal_display_path_get_dcs(display_path));

	/*allow aux transactions when hpd low for
	 * AllowAuxWhenHpdLow monitor patch*/
	if ((patch_flags.flags.ALLOW_AUX_WHEN_HPD_LOW)
		&& (detection_status->detected_signal ==
				SIGNAL_TYPE_DISPLAY_PORT))
		allow_aux_while_hpd_low(tm_dm, display_path, true);

	  /* Add other patches here */
}

/**
 *****************************************************************************
 *  Function: read_edid
 *
 *  @brief
 *     Atomic Edid retrieval
 *     Reads Edid data, setups monitor patches and updates Edid
 *     internals if needed. After we read EDID we can know whether
 *     in addition load detection needed to
 *     be performed
 *
 *  @param [in]     display_path:     Display path on which to perform
 *   detection
 *  @param [in]     destructive:      true if detection method is
 *   destructive,
 *   false if non-destructive
 *  @param [in/out] detection_status: Structure to hold detection-
 *  related info
 *   from current detection procedure.
 *
 *  @return
 *     True if load detection needed to complete display detection,
 *      false otherwise
 *****************************************************************************
 */
static bool read_edid(
		struct tm_detection_mgr *tm_dm,
		struct display_path *display_path,
		bool destructive,
		struct tm_detection_status *detection_status)
{
	bool load_detect_need = false;
	bool connected = false;
	struct vendor_product_id_info old_vendor_info;
	bool old_vendor_info_retrieved;
	enum edid_retrieve_status status;

	/**Get old vendor product info - we need this for later on
	 * to figure out if the monitor changed or not...
	 */
	old_vendor_info_retrieved = dal_dcs_get_vendor_product_id_info(
		dal_display_path_get_dcs(display_path), &old_vendor_info);

	/*TODO: android does not support 2 encoder for now,
	 * preDDC and postDDC are not needed for now
	 * configure display path before I2C over AUX case
	 * m_pHWSS->PreDDC(display_path);
	 *
	 * Read Edid data from monitor into local buffer.
	 * It will also create list of applicable monitor patches
	 */
	status = dal_dcs_retrieve_raw_edid(
			dal_display_path_get_dcs(display_path));

	/**TODO
	 * restore configure display path after I2C over AUX case
	 * m_pHWSS->PostDDC(display_path);
	 */

	/**Only apply Dual-EDID patch and update return "status"
	 * when new EDID is retrieved
	 */
	if (status == EDID_RETRIEVE_SUCCESS)
		/**Handle any load detection based edid patching. In case
		 * it was succefully applied - treat Edid as new
		 */
		apply_load_detection_based_edid_patch(tm_dm, display_path,
				detection_status);

	/**Update Edid internals if new Edid was retrieved. In case
	 * of update failure we consider no Edid detected
	 */
	if (status == EDID_RETRIEVE_SUCCESS)
		status = dal_dcs_update_edid_from_last_retrieved(
				dal_display_path_get_dcs(display_path));

	/*On successful Edid read we need to update detection status*/
	if (status == EDID_RETRIEVE_SUCCESS ||
		status == EDID_RETRIEVE_SAME_EDID) {
		enum dcs_edid_connector_type connector_type;
		enum display_dongle_type dongle;
		enum signal_type signal;
		/*apply EDID-based sink capability patches*/
		apply_detection_status_patches(
				tm_dm, display_path, detection_status);

		connector_type = dal_dcs_get_connector_type(
				dal_display_path_get_dcs(display_path));

		signal = detection_status->detected_signal;

		dongle = detection_status->sink_capabilities.dongle_type;

		/*check whether EDID connector is right for the signal*/
		if (tm_utils_is_edid_connector_type_valid_with_signal_type(
				dongle,
				connector_type, signal)) {
			/*downgrade signal in case it has
			 *  changed but EDID hasn't*/
			detection_status->detected_signal =
				tm_utils_get_downgraded_signal_type(
						signal,
						connector_type);

			/* report connected since edid successfully detected*/
			connected = true;
		}

		/* TODO m_isPersistenceEmulationIsOn is not implemented in
		 * As yet always return false for now
		 * m_pAdapterService->GetEdidPersistenceMode() = false
		 */

		/**Check whether monitor ID changed - this flag only
		 * matters if the connected flag is set
		 */
		if (connected && old_vendor_info_retrieved) {
			/**Get new vendor product info - we only need to
			 * test this if we were able to retrieve the old
			 * vendor information
			 */
			struct vendor_product_id_info new_vendor_info;
			bool new_vendor_info_retrieved;

			new_vendor_info_retrieved =
				dal_dcs_get_vendor_product_id_info(
					dal_display_path_get_dcs(
						display_path),
						&new_vendor_info);

			if (new_vendor_info_retrieved) {
				if (old_vendor_info.manufacturer_id !=
					new_vendor_info.manufacturer_id ||
					old_vendor_info.product_id !=
					new_vendor_info.product_id ||
					old_vendor_info.serial_id !=
					new_vendor_info.serial_id)
					detection_status->monitor_changed =
							true;
				/*else if(m_pAdapterService->
				 * GetEdidPersistenceMode())
				 * {
				 *   status = EDID_RETRIEVE_SAME_EDID;
				 * }
				 */
			}
		}
	}

	/**By DisplayPort spec, if sink is present and Edid is not available
	 * DP display should support 640x480, therefore we can report
	 *  DisplayPort
	 * as connected if HPD high or Edid present (one indication enough)
	 */
	if (dal_is_dp_signal(detection_status->detected_signal))
		connected = (detection_status->connected || connected);

	/* Handling the case of unplugging of a VGA monitor with
	 * DDC polling. In this case we keep track of previous EDID read status
	 * so we can detect a change and detect the unplug return so that we do
	 * not try and detect load as this is non destructive and we want to
	 * update connected to false
	 */
	if (SIGNAL_TYPE_RGB == detection_status->detected_signal &&
			!destructive
			&& status == EDID_RETRIEVE_FAIL_WITH_PREVIOUS_SUCCESS)
		connected = false;
	else if (!connected && dal_is_analog_signal(
			detection_status->detected_signal))
		load_detect_need = true;

	/**For embedded display its connection state just depends on lid state.
	 * Edid retrieving state should not affect it.
	 */
	if (!dal_is_embedded_signal(detection_status->detected_signal))
		detection_status->connected = connected;

	return load_detect_need;
}

/**
 *****************************************************************************
 *  Function: is_sink_present
 *
 *  @brief
 *     Checks whether the sink is present
 *
 *  @param [in]  display_path:     Display path on which to perform detection
 *  @param [out] detection_status: Structure to hold detection-related info
 *  from current detection procedure.
 *****************************************************************************
 */
static bool is_sink_present(
		struct tm_detection_mgr *tm_dm,
		struct display_path *display_path)
{
	bool connected = dal_hw_sequencer_is_sink_present(tm_dm->hwss,
			display_path);
	uint32_t display_index = dal_display_path_get_display_index(
			display_path);
	struct dal_context *dal_context = tm_dm->dal_context;

	TM_DISPLAY_DETECT("HPD of DisplayIndex:%d is: %s\n",
			display_index, (connected == true ? "High" : "Low"));

	if (!connected && dal_dcs_get_monitor_patch_flags(
		dal_display_path_get_dcs(display_path)).
			flags.ALLOW_AUX_WHEN_HPD_LOW) {
		uint8_t value = 0;
		enum ddc_result result = dal_dcs_dpcd_read(
				dal_display_path_get_dcs(display_path),
				DPCD_ADDRESS_DPCD_REV, &value, 1);

		if ((result == DDC_RESULT_SUCESSFULL) && (value > 0))
			connected = true;
		else
			/*reset the aux control to disallow aux
			 *  transactions when hpd low*/
			allow_aux_while_hpd_low(tm_dm, display_path, false);
	}

	return connected;
}

/**
 *****************************************************************************
 *  Function: detect_sink_caps
 *
 *  @brief
 *     Actual does sink detection with retrievng all related capabilities.
 *     XXX: Besides updating output structure DCS state updated as well.
 *     Needed to be called before first Edid read to setup transaction mode.
 *
 *  @param [in]  display_path:     Display path on which to perform detection
 *  @param [out] detection_status: Structure to hold detection-related info
 *   from current detection procedure.
 *****************************************************************************
 */
static void detect_sink_caps(
		struct tm_detection_mgr *tm_dm,
		struct display_path *display_path,
		struct tm_detection_status *detection_status)
{
	enum signal_type detected_signal;
	struct dcs *dcs;
	struct display_sink_capability cur_sink_cap;
	struct dal_context *dal_context = tm_dm->dal_context;

	TM_ASSERT(detection_status != NULL);
	TM_ASSERT(dal_display_path_get_dcs(display_path) != NULL);

	dcs = dal_display_path_get_dcs(display_path);

	/*Obtain signal and connectivity state*/
	detection_status->connected = is_sink_present(tm_dm, display_path);
	detection_status->detected_signal = dal_hw_sequencer_detect_sink(
			tm_dm->hwss, display_path);

	/*Detect MST signal (This function should be only called
	 *  for MST Master path)*/
	if (detection_status->connected &&
		detection_status->detected_signal == SIGNAL_TYPE_DISPLAY_PORT) {
		struct link_service *link_service;

		link_service = tm_resource_mgr_find_link_service(
				tm_dm->resource_mgr,
				display_path,
				SIGNAL_TYPE_DISPLAY_PORT_MST);

		if (link_service != NULL &&
				dal_ls_is_mst_network_present(link_service))
			detection_status->detected_signal =
					SIGNAL_TYPE_DISPLAY_PORT_MST;
	}

	/*update DCS with corresponding DDC type*/
	detected_signal = detection_status->detected_signal;

	/**if we have passive dongle dp_to _hdmi and edid emulator presence,
	 * we could not detect sink. we force it connected and do not allow
	 * to change
	 */

	dal_dcs_set_transaction_type(dcs,
		tm_utils_get_ddc_transaction_type(detected_signal,
			dal_display_path_sink_signal_to_asic_signal(
					display_path,
					detected_signal)));

	/**MST Display Path detection will be done by MST Manager.
	 * Setting this flag will skip further detection steps
	 */
	if ((detection_status->connected &&
		(detection_status->detected_signal
			== SIGNAL_TYPE_DISPLAY_PORT_MST)) ||
			(!detection_status->connected &&
			dal_display_path_get_query_signal(
				display_path,
				SINK_LINK_INDEX) ==
					SIGNAL_TYPE_DISPLAY_PORT_MST)) {

		detection_status->dp_mst_detection = true;
		if (!detection_status->connected)
			dal_dcs_reset_sink_capability(dcs);
		return;
	}

	/**query output sink capability when connected
	 * or if the signal is embedded
	 */
	dal_memset(&cur_sink_cap, 0, sizeof(cur_sink_cap));
	if (detection_status->connected
		|| dal_is_embedded_signal(detection_status->detected_signal))
		/**update DCS with the latest sink capability
		 * this (DCS) should go directly to the real DDC
		 * to retrieve the current sink capabilities
		 */
		dal_dcs_query_sink_capability(
				dcs,
				&detection_status->sink_capabilities,
				detection_status->connected);

	/**we are here only when passive dongle and edid emulator
	 * TODO: emulator is not implemented yet
	 *
	 * else if (dcs->QueryEdidEmulatorCapability(&cur_sink_cap))
	 * {
	 *   detection_status->sink_capabilities = cur_sink_cap;
	 * }
	 */
	else
		/*clear (reported) capabilities*/
		dal_dcs_reset_sink_capability(dcs);
}

/**
 *****************************************************************************
 *  Function: do_target_detection
 *
 *  @brief
 *     Does physical detection of display on given display path.
 *     Detection composed from 3 steps:
 *       1. Sink detection (not so relevant for analog displays)
 *       2. Edid read
 *       3. Load detection (relevant only for analog displays)
 *     Cached method not handled in this function
 *
 *  @param [in]  display_path:     Display path on which to perform detection
 *  @param [in]  destructive:      true if detection method is destructive,
 *   false if non-destructive.
 *  @param [out] detection_status: Structure to hold detection-related info
 *   from current detection procedure.
 *****************************************************************************
 */
static void do_target_detection(
		struct tm_detection_mgr *tm_dm,
		struct display_path *display_path,
		bool destructive,
		struct tm_detection_status *detection_status)
{
	struct tm_resource *connector_rsrc;
	struct dal_context *dal_context = tm_dm->dal_context;

	TM_ASSERT(detection_status != NULL);

	/** Step 1. Set appropriate DDC. Set the DDC for the real device
	 *  as we're doing real detection at this point
	 */
	connector_rsrc = tm_resource_mgr_find_resource(
			tm_dm->resource_mgr,
			get_connector_obj_id(display_path));

	if ((connector_rsrc != NULL) &&
		(TO_CONNECTOR_INFO(connector_rsrc)->ddc_service != NULL)) {

		/** if the obtained Ddc service is different than the real and
		 *  the emulated DDCs that we have stored with resource manager
		 *  then the old DDC might be from MST in which case we want to
		 *  leave things alone as the MST DDC comes from a different
		 *  place and needs to be cleaned up differently...
		 */
		/**TODO: ddcServiceWithEmulation is not implemented yet.
		struct ddc_service *ddc_old_dev;
		ddc_old_dev = dal_dcs_update_ddc(
			dal_display_path_get_dcs(display_path),
			TO_CONNECTOR_INFO(connector_rsrc)->ddc_service);
		if ((ddc_old_dev != connector_rsrc->connector.ddc_service) &&
		    (ddc_old_dev != connector_rsrc->connector.
		    ddcServiceWithEmulation))
		{
		    display_path->GetDCS()->UpdateDdcService(ddc_old_dev);
		}
		 */
	}

	/** Step 2. Sink detection Update all sink capabilities
	 * - signal, dongle, sink presence, etc.
	 */
	detect_sink_caps(tm_dm, display_path, detection_status);

	/** Step 3. Handle MST detection. MST discovery done by MST Manager
	 *  which will eventually notify TM about connectivity change.
	 */
	if (detection_status->dp_mst_detection)
		return;

	/** Step 4. Handle the case where sink presence not detected
	 * yet (HPD line  is low)
	 */
	if (!detection_status->connected) {
		switch (detection_status->detected_signal) {
		/**Display Port requires HPD high. So we force skip
		 * further steps. However eDP we want to read EDID
		 * always (eDP not connected =
		 * LID closed, not related to HPD)
		 */
		case SIGNAL_TYPE_DISPLAY_PORT:
		case SIGNAL_TYPE_DISPLAY_PORT_MST:
			return;

			/** DVI/HDMI does not require HPD high, but in hotplug
			 * context we might want to reschedule detection
			 */
		case SIGNAL_TYPE_DVI_SINGLE_LINK:
		case SIGNAL_TYPE_DVI_SINGLE_LINK1:
		case SIGNAL_TYPE_DVI_DUAL_LINK:
		case SIGNAL_TYPE_HDMI_TYPE_A:
			detection_status->hpd_pin_failure = true;
			break;

			/*All the rest- continue to EDID read/load detection*/
		default:
			break;
		}
	}

	/** Step 5. Edid read  - updates detection status
	 *  only read edid, if connected (HPD sensebit check), or
	 *  runtime parameters not set, or set but value is 0
	 */
	if (detection_status->connected
			|| !dal_adapter_service_is_feature_supported(
					FEATURE_DETECT_REQUIRE_HPD_HIGH)) {
		bool load_detect_need;
		/*not read Edid if HPD sense bit low.*/
		load_detect_need = read_edid(tm_dm, display_path, destructive,
				detection_status);

		/**Step 6. Load detection (relevant only for non-DDC analog
		 * displays if analog display marked as connected, it means
		 * it has EDID)
		 */
		if (load_detect_need) {
			if (destructive) {
				enum signal_type signal;
				/*Update signal if detected. Update connection
				 * status always*/
				signal = dal_hw_sequencer_detect_load(
						tm_dm->hwss,
						display_path);

				if (signal != SIGNAL_TYPE_NONE)
					detection_status->detected_signal =
							signal;

				detection_status->connected =
						(signal != SIGNAL_TYPE_NONE);
			} else {
				/**In non-destructive case, connectivity
				 * does not change.so we can return
				 * cached value
				 */
				detection_status->connected =
					dal_display_path_is_target_connected(
						display_path);
			}
		}
	}
}

/**
 *****************************************************************************
 *  Function: do_target_pre_processing
 *
 *  @brief
 *     Before detection and emulation (if required), retrieve the current
 *     display information which we will need to compare against in
 *     do_target_post_processing
 *
 *  @param [in]       display_path:     Display path on which to perform
 *   detection
 *  @param [in]       detection_status: Structure to hold detection-related
 *   info from current detection procedure.
 *  @param [int/out]  display_state:    the previous display information
 *   before target detection and emulation.
 *****************************************************************************
 */
static void do_target_pre_processing(
		struct tm_detection_mgr *tm_dm,
		struct display_path *display_path,
		struct display_state *display_state)
{
	const uint8_t *orig_edid;

	dal_memset(display_state, 0, sizeof(struct display_state));

	/*retrieve the sink capabilities*/
	dal_dcs_get_sink_capability(dal_display_path_get_dcs(display_path),
			&display_state->sink_cap);

	/*retrieve the old Edid information*/
	orig_edid = dal_dcs_get_edid_raw_data(
			dal_display_path_get_dcs(display_path),
			&display_state->edid_len);

	dal_memmove(display_state->edid, orig_edid, display_state->edid_len);

	/*Get old audio capabilities*/
	display_state->audio_cap = dal_dcs_is_audio_supported(
			dal_display_path_get_dcs(display_path));
}

/**
 *****************************************************************************
 *  Function: do_target_post_processing
 *
 *  @brief
 *     After detection and emulation (if required), updates DCS and the
 *     detection status
 *     Expects do_target_detection and doTargetEmulation to be called first
 *
 *  @param [in]  display_path:     Display path on which to perform detection
 *  @param [out] detection_status: Structure to hold detection-related info
 *   from current detection procedure.
 *  @param [in]  display_state:    the previous display information before
 *   target detection and emulation.
 *****************************************************************************
 */
static void do_target_post_processing(
		struct tm_detection_mgr *tm_dm,
		struct display_path *display_path,
		struct tm_detection_status *detection_status,
		const struct display_state *display_state)
{
	uint32_t cur_edid_len;
	uint32_t i;
	const uint8_t *cur_edid;

	/*NOTE: do we need to check if the signal type has changed?*/

	/*Report capability change if sink capabilities are different*/
	if (display_state->sink_cap.dp_link_lane_count
		!= detection_status->sink_capabilities.dp_link_lane_count
		|| display_state->sink_cap.dp_link_rate
		!= detection_status->sink_capabilities.dp_link_rate)
		detection_status->capability_changed = true;

	/**report capability change if Edid has changed
	 * NOTE: if we already noticed the capability changed due
	 * to link settings modified, no need to waste CPU
	 * clocks to see if the Edid changed just so we can
	 * set the same flag...
	 */
	if (!detection_status->capability_changed) {
		/**retrieve the current EDID information that DCS has and see
		 * if it's been modified during the detect/emulate process
		 */
		cur_edid_len = 0;
		cur_edid = dal_dcs_get_edid_raw_data(
			dal_display_path_get_dcs(display_path), &cur_edid_len);

		/**NOTE: the comparison of the Edid should be placed
		 * in a separate function that's available to everyone,
		 * instead of just having this all over the code -
		 * TO BE DONE LATER
		 * NOTE: since the EDID contains a check-sum, it might
		 * be faster to just compare the checksum instead of
		 * going through each byte...
		 * NOTE: we need to compare the Edid if the starting
		 * pointers are different (obviously if they start at
		 * the same address, it's the same buffer) or the lengths
		 * are different...
		 */
		if (cur_edid && (cur_edid_len == display_state->edid_len)) {
			for (i = 0; i < cur_edid_len; ++i) {
				if (cur_edid[i] != display_state->edid[i]) {
					detection_status->capability_changed =
						true;
					break;
				}
			}
		} else if (cur_edid_len != display_state->edid_len) {
			detection_status->capability_changed = true;
		}
	}

	/*report if audio capabilities changed*/
	detection_status->audio_cap_changed =
		dal_dcs_is_audio_supported(
				dal_display_path_get_dcs(display_path))
				!= display_state->audio_cap;

	/*Update signal based on dongle*/
	switch (detection_status->sink_capabilities.dongle_type) {
	/**Upgrade DVI --> HDMI signal if HDMI dongle present and
	 * hdmi audio is supported on display path
	 */
	case DISPLAY_DONGLE_DP_HDMI_DONGLE:
		if (dal_display_path_get_properties(
			display_path).bits.IS_HDMI_AUDIO_SUPPORTED &&
			dal_is_dvi_signal(detection_status->detected_signal) &&
			dal_dcs_get_connector_type(
				dal_display_path_get_dcs(display_path)) ==
					EDID_CONNECTOR_HDMIA)
			detection_status->detected_signal =
					SIGNAL_TYPE_HDMI_TYPE_A;
		break;

	case DISPLAY_DONGLE_DP_DVI_DONGLE:
		detection_status->detected_signal = SIGNAL_TYPE_DVI_SINGLE_LINK;
		break;

	default:
		break;
	}
}

/**
 *****************************************************************************
 * Detects signal which goes out of ASIC.
 *
 *  @param [in]  display_path: Display path on which to perform detection
 *
 *  @return
 *	Detected signal if sink connected, SIGNAL_TYPE_NONE otherwise
 *****************************************************************************
 */
static enum signal_type detect_asic_signal(
		struct tm_detection_mgr *tm_dm,
		struct display_path *display_path)
{
	struct encoder *encoder;
	struct graphics_object_id down_stream_id;
	enum signal_type signal = SIGNAL_TYPE_NONE;
	struct dal_context *dal_context = tm_dm->dal_context;

	TM_ASSERT(display_path != NULL);

	encoder = dal_display_path_get_upstream_object(display_path,
			ASIC_LINK_INDEX);

	down_stream_id = get_connector_obj_id(display_path);

	/*TODO: connector?*/
	if (dal_display_path_get_downstream_object(display_path,
			ASIC_LINK_INDEX) != NULL)
		down_stream_id = dal_encoder_get_graphics_object_id(
			dal_display_path_get_downstream_object(display_path,
				ASIC_LINK_INDEX));
	if (encoder != NULL)
		if (dal_encoder_is_sink_present(encoder, down_stream_id))
			signal = dal_encoder_detect_sink(encoder,
					down_stream_id);

	return signal;
}

/**
 *****************************************************************************
 *  Function: reconnect_link_services
 *
 *  @brief
 *     Connect/Disconnect LinkServices associated to the given display path
 *     based on detection status
 *
 *  @param [in] display_path:     connect/disconnect link services associated
 *  to this Display path
 *  @param [in] detection_status: connect/disconnect based on detection status
 *****************************************************************************
 */
static void reconnect_link_services(
		struct tm_detection_mgr *tm_dm,
		struct display_path *display_path,
		struct tm_detection_status *detection_status)
{
	uint8_t i;
	uint8_t link_count;
	enum signal_type connect_signal;
	enum signal_type dis_connect_signal;
	struct link_service *link_service;
	struct dal_context *dal_context = tm_dm->dal_context;

	link_count = dal_display_path_get_number_of_links(display_path);
	connect_signal = SIGNAL_TYPE_NONE;
	dis_connect_signal = SIGNAL_TYPE_NONE;

	if (dal_is_embedded_signal(detection_status->detected_signal)) {
		/**For embedded displays we need to call Link Service to
		 * retrieve link capabilities regardless of lid state
		 */
		connect_signal = detection_status->detected_signal;
		dis_connect_signal = SIGNAL_TYPE_NONE;
	} else {
		/**Disconnect Link service on signal change
		 * (and of course if not connected)
		 */
		if (!detection_status->connected
			|| dal_display_path_get_query_signal(display_path,
			SINK_LINK_INDEX) != detection_status->detected_signal)
			dis_connect_signal = dal_display_path_get_query_signal(
				display_path,
				SINK_LINK_INDEX);

		if (detection_status->connected)
			connect_signal = detection_status->detected_signal;
	}

	/*only disconnect the last link, and connect all others*/
	if (dis_connect_signal != SIGNAL_TYPE_NONE) {
		/*disconnect link service*/
		/* TODO: check if dead loop*/
		for (i = link_count; i > 0; i--) {
			link_service = tm_resource_mgr_get_link_service(
					tm_dm->resource_mgr,
					display_path,
					i - 1,
					dis_connect_signal);

			TM_ASSERT(link_service != NULL);

			if (i < link_count) {
				dal_ls_connect_link(link_service, display_path,
						tm_dm->is_blocking_detection);

			if (need_handle_connection_status_based_on_sink_count(
				tm_dm,
				detection_status))
				/*we need this to remove the optimiztation in
				 *  connectlink logic*/
				dal_ls_invalidate_down_stream_devices(
					link_service);
			} else
				dal_ls_disconnect_link(link_service);

			dis_connect_signal =
				dal_display_path_downstream_to_upstream_signal(
					display_path,
					dis_connect_signal,
					i - 1);
		}
	}

	if (connect_signal != SIGNAL_TYPE_NONE) {
		/*connect link service*/
		for (i = 0; i < link_count; ++i) {
			enum signal_type current_signal;

			current_signal =
				dal_display_path_sink_signal_to_link_signal(
					display_path, connect_signal, i);

			link_service = tm_resource_mgr_get_link_service(
					tm_dm->resource_mgr,
					display_path,
					i,
					current_signal);

			TM_ASSERT(link_service != NULL);

			dal_ls_connect_link(link_service, display_path,
					tm_dm->is_blocking_detection);
		}
	}
}

/* Interrupt related handlers */

/**
 *****************************************************************************
 *  Function: register_irq_source
 *
 *  @brief
 *     Registers irq source for requested interrupt type within given irq
 *      registration entry
 *     Does nothing if such irq source is already registered
 *
 *  @param [in] interruptType: TM interrupt type
 *  @param [in] irq_entry: Entry within which to register irq source
 *****************************************************************************
 */
static void register_irq_source(
		struct tm_detection_mgr *tm_dm,
		enum tm_interrupt_type type,
		struct tmdm_irq_registration *connector_irq)
{
	struct dal_context *dal_context = tm_dm->dal_context;
	struct tmdm_irq_entry *irq_entry;

	TM_ASSERT(type < TM_INTERRUPT_TYPE_COUNT);
	TM_ASSERT(connector_irq != NULL);

	irq_entry = IRQ_ENTRY(connector_irq, type);

	if (irq_entry->irq_src == DAL_IRQ_SOURCE_INVALID)
		irq_entry->irq_src = get_irq_source(tm_dm,
				connector_irq->connector_id, type);

	/**This TM_ASSERT may hit for connectors which do not have ddc/hpd line
	 * associated. Though in these cases it is valid, we still want to
	 * catch such configuration (very rare config)
	 */
	TM_ASSERT(irq_entry->irq_src != DAL_IRQ_SOURCE_INVALID);
}

/**
 *****************************************************************************
 *  Function: init_irq_entry
 *
 *  @brief
 *     Initializes irq entry. It includes few things:
 *       1. Cache connector inetrface (till now we had only connector
 *        object ID)
 *       2. Register irq sources for relevant interrupts
 *       3. Initialize interrupt features (like hpd filtering and hw
 *        ddc polling)
 *     This fucntion typically called when first dispay registered within
 *      irq registration entry
 *
 *  @param [in]  display_path: Registered display path
 *  @param [in]  irq_entry:       IRQ registration entry corresponding to
 *   this display path
 *
 *  @return
 *     true if entry successfully initialized, false otherwise
 *****************************************************************************
 */
static bool init_irq_entry(
		struct tm_detection_mgr *tm_dm,
		struct display_path *display_path,
		struct tmdm_irq_registration *connector_irq)
{
	struct connector_feature_support features = {0};
	struct dal_context *dal_context = tm_dm->dal_context;

	TM_ASSERT(display_path != NULL);
	TM_ASSERT(connector_irq != NULL);
	TM_ASSERT(connector_irq->connector == NULL);

	/*Obtain and cache connector*/
	connector_irq->connector = dal_display_path_get_connector(
			display_path);

	if (connector_irq->connector == NULL) {
		TM_ERROR("%s: no Connector on Display Path!\n", __func__);
		return false;
	}

	dal_connector_get_features(connector_irq->connector, &features);

	if (features.hpd_line != HPD_SOURCEID_UNKNOWN) {
		/* Since connector supports HPD, register HPD IRQ source. */
		register_irq_source(tm_dm, TM_INTERRUPT_TYPE_HOTPLUG,
				connector_irq);
	}

	/* Setup Delayed HPD mode: On regular display path we need
	 * either HW HPD filtering or SW timer. But NOT both. */
	if (IRQ_ENTRY_HPD(connector_irq)->irq_src != DAL_IRQ_SOURCE_INVALID) {
		hw_hpd_filter_set(connector_irq, features.HPD_FILTERING);
		sw_hpd_filter_set(connector_irq, !features.HPD_FILTERING);
	}

	register_irq_source(tm_dm, TM_INTERRUPT_TYPE_TIMER, connector_irq);

	return true;
}

/**
 *****************************************************************************
 *  Function: update_irq_on_connect
 *
 *  @brief
 *     Updates interrupt state when display path becomes connected.
 *     This includes reprogramming HW for special interrupt-related features
 *      (like hpd filtering)
 *
 *  @param [in]  irq_entry:       IRQ registration entry corresponding to this
 *   display path
 *****************************************************************************
 */
static void update_irq_on_connect(
		struct tm_detection_mgr *tm_dm,
		struct tmdm_irq_registration *connector_irq)
{
	struct display_path *display_path = connector_irq->connected_display;
	struct dal_context *dal_context = tm_dm->dal_context;

	if (NULL == display_path) {
		TM_ERROR("%s: 'connected_display' is NULL!\n", __func__);
		return;
	}

	/* Setup HPD Filtering flags for next DISCONNECT interrupt. */
	if (tm_utils_is_dp_connector(connector_irq->connector)) {
		if (tm_utils_is_dp_asic_signal(display_path))
			sw_hpd_filter_set(connector_irq, false);
		else
			sw_hpd_filter_set(connector_irq, true);
	}
}

static bool is_active_converter(enum display_dongle_type type)
{
	switch (type) {
	case DISPLAY_DONGLE_DP_VGA_CONVERTER:
	case DISPLAY_DONGLE_DP_DVI_CONVERTER:
	case DISPLAY_DONGLE_DP_HDMI_CONVERTER:
		return true;
	default:
		return false;
	}
}

/**
 *****************************************************************************
 *  Function: register_interrupt
 *
 *  @brief
 *     Registers requested interrupt within IRQ Manager
 *     Does nothing if such interrupt already registered
 *     Therefore this function cannot be used as is to re-register Timer
 *     interrupts
 *
 *  @param [in] interruptType: TM interrupt type
 *  @param [in] irq_entry: Entry which to register for this interrupt
 *****************************************************************************
 */
static void register_interrupt(
		struct tm_detection_mgr *tm_dm,
		struct display_path *display_path,
		enum tm_interrupt_type type,
		struct tmdm_irq_registration *connector_irq,
		uint32_t timeout_ms)
{
	enum dal_irq_source irq_src;
	struct dal_context *dal_context = tm_dm->dal_context;
	struct dal_interrupt_params int_params = {0};
	struct dal_timer_interrupt_params timer_int_params = {0};
	struct tmdm_irq_entry *irq_entry;

	TM_ASSERT(type < TM_INTERRUPT_TYPE_COUNT);
	TM_ASSERT(connector_irq != NULL);

	irq_entry = IRQ_ENTRY(connector_irq, type);

	irq_src = irq_entry->irq_src;

	if (irq_src == DAL_IRQ_SOURCE_INVALID)
		return;

	if (DAL_INVALID_IRQ_HANDLER_IDX != irq_entry->handler_index_ref) {
		/* We don't want to overwrite somebody's IRQ handler!
		 * If we get here, we have inconsistent state - somebody
		 * is still registered for this IRQ and we register again. */
		TM_ERROR("%s: can not overwrite IRQ handler!\n", __func__);
		return;
	}

	switch (type) {
	case TM_INTERRUPT_TYPE_TIMER:

		if (tmdm_get_timer_state(irq_entry) !=
				TMDM_TMR_STATE_NOT_REGISTERED) {
			/* We don't want multiple timers for a connector being
			 * queued at the same time. */
			TM_HPD_IRQ("%s:Can not register timer - another already registered!\n",
					__func__);
			break;
		}

		timer_int_params.int_context = INTERRUPT_LOW_IRQ_CONTEXT;
		timer_int_params.micro_sec_interval =
				timeout_ms * MICROSECONDS_IN_MILLISECOND;
		int_params.no_mutex_wait = false;
		int_params.one_shot = true;/* We only have 'one shot' timers,
				but let's make it explicit for readability.*/

		/* dal_register_timer_interrupt has no return code - we assume
		 * the call is always successful. */
		dal_register_timer_interrupt(dal_context,
				&timer_int_params,
				tmdm_handle_timer_interrupt,
				connector_irq);

		tmdm_set_timer_state(tm_dm, irq_entry,
				TMDM_TMR_STATE_REGISTERED);

		TM_HPD_IRQ("%s: type=%s timeout_ms=%d\n", __func__,
				TM_DECODE_INTERRUPT_TYPE(type),
				timeout_ms);
		break;

	case TM_INTERRUPT_TYPE_HOTPLUG: {
		struct display_sink_capability sink_cap;

		int_params.int_context = INTERRUPT_LOW_IRQ_CONTEXT;
		int_params.irq_source = irq_src;

		/* Trigger an interrupt for both - Connect and Disconnect
		 * cases. */
		int_params.requested_polarity = INTERRUPT_POLARITY_BOTH;

		dal_dcs_get_sink_capability(
			dal_display_path_get_dcs(display_path),
			&sink_cap);

		/* The display path target state is initialised by
		 * dal_tm_do_initial_detection() and updated by HPD Interrupt
		 * handler. */
		if (dal_display_path_is_target_connected(display_path) ||
			is_active_converter(sink_cap.dongle_type))
			int_params.current_polarity = INTERRUPT_POLARITY_HIGH;
		else
			int_params.current_polarity = INTERRUPT_POLARITY_LOW;

		int_params.no_mutex_wait = false;
		int_params.one_shot = false;/* we'll stop it during DAL
						unload. */

		irq_entry->handler_index_ref =
			dal_register_interrupt(dal_context,
					&int_params,
					tmdm_handle_hpd_interrupt,
					connector_irq);

		if (DAL_INVALID_IRQ_HANDLER_IDX !=
				irq_entry->handler_index_ref) {
			TM_HPD_IRQ(
				"%s: type=%s irq_src=%d current_polarity=%s\n",
				__func__, TM_DECODE_INTERRUPT_TYPE(type),
				irq_src,
				DAL_DECODE_INTERRUPT_POLARITY(
						int_params.current_polarity));
		} else {
			TM_ERROR("%s: failed to register: type=%s irq_src=%d\n",
				__func__, TM_DECODE_INTERRUPT_TYPE(type),
				irq_src);
		}
		break;
	}
	default:
		TM_WARNING("%s: request for unknown type of IRQ handler: %d!\n",
				__func__, type);
		break;
	}
}

/**
 *****************************************************************************
 *  Function: unregister_interrupt
 *
 *  @brief
 *     Unregisters requested interrupt within IRQ Manager
 *     Does nothing if such interrupt not registered
 *
 *  @param [in] interruptType: TM interrupt type
 *  @param [in] irq_entry: Entry which to unregister for this interrupt
 *****************************************************************************
 */
static void unregister_interrupt(
		struct tm_detection_mgr *tm_dm,
		enum tm_interrupt_type type,
		struct tmdm_irq_registration *connector_irq)
{
	struct dal_context *dal_context = tm_dm->dal_context;
	struct tmdm_irq_entry *irq_entry = IRQ_ENTRY(connector_irq, type);
	enum dal_irq_source irq_src = irq_entry->irq_src;

	TM_ASSERT(type < TM_INTERRUPT_TYPE_COUNT);

	switch (type) {
	case TM_INTERRUPT_TYPE_TIMER:
		/* Timer interrupt *can not* be unregistered (no API to
		 * do that).
		 * We can only change internal timer state, which will indicate
		 * to tmdm_handle_timer_interrupt() that timer was cancelled
		 * and no work should be done in timer interrupt handler. */
		switch (irq_entry->timer_state) {
		case TMDM_TMR_STATE_REGISTERED:
			tmdm_set_timer_state(tm_dm, irq_entry,
					TMDM_TMR_STATE_CANCELLED);
			break;
		case TMDM_TMR_STATE_NOT_REGISTERED:
			/* do nothing */
			break;
		case TMDM_TMR_STATE_CANCELLED:
			tmdm_set_timer_state(tm_dm, irq_entry,
					TMDM_TMR_STATE_NOT_REGISTERED);
			break;
		default:
			TM_WARNING("%s: invalid timer state!\n", __func__);
			break;
		}
		break;

	case TM_INTERRUPT_TYPE_HOTPLUG:
		if (DAL_INVALID_IRQ_HANDLER_IDX ==
				irq_entry->handler_index_ref) {
			/* we can not unregister if no handler */
			TM_WARNING("%s: HPD Interrupt has no handler!\n",
					__func__);
			break;
		}

		TM_HPD_IRQ("%s: type=%s irq_src=%d\n", __func__,
				TM_DECODE_INTERRUPT_TYPE(type), irq_src);

		dal_unregister_interrupt(dal_context, irq_src,
				irq_entry->handler_index_ref);

		irq_entry->handler_index_ref = DAL_INVALID_IRQ_HANDLER_IDX;
		break;

	default:
		TM_WARNING("%s: unknown type of IRQ: %d!\n", __func__, type);
		break;
	}
}

static void hpd_notify(struct tmdm_irq_registration *connector_irq)
{
	struct tm_detection_mgr *tm_dm = connector_irq->detection_mgr;
	struct dal_context *dal_context = tm_dm->dal_context;

	if (connector_irq->connected_display != NULL) {
		TM_HPD_IRQ("%s: connected_display != NULL\n", __func__);

		/* should be a Disconnect event on a connected path. */
		tm_handle_hpd_event(tm_dm->tm_hpd_callback,
				connector_irq->connected_display);
	} else {
		uint8_t i;

		TM_HPD_IRQ("%s: connected_display == NULL\n", __func__);

		/* should be a Connect event on disconnected path.  */
		for (i = 0; i < connector_irq->displays_num; ++i) {
			if (TM_RESULT_DISPLAY_CONNECTED == tm_handle_hpd_event(
					tm_dm->tm_hpd_callback,
					connector_irq->displays[i])) {
				/* A path got connected. We can stop, since it
				 * means connectivity changed. */
				break;
			}
		} /*for () */
	}
}

/*****************************************************************************
 * Handles Timer interrupt - notify connectivity changed.
 *
 * If this is connect event on disconnected path
 *  - notification will be sent for all registered display paths until
 *    connected path found
 *
 * If this is disconnected event on connected path
 *  - notification will be sent only for connected path
 *****************************************************************************/
static void tmdm_handle_timer_interrupt(void *interrupt_params)
{
	struct tmdm_irq_registration *connector_irq = interrupt_params;
	struct tmdm_irq_entry *irq_entry = IRQ_ENTRY_TIMER(connector_irq);
	struct tm_detection_mgr *tm_dm = connector_irq->detection_mgr;
	struct dal_context *dal_context = tm_dm->dal_context;

	TM_HPD_IRQ("%s: timer state: %s\n", __func__,
		TMDM_DECODE_TIMER_STATE(tmdm_get_timer_state(irq_entry)));

	if (TMDM_TMR_STATE_REGISTERED != tmdm_get_timer_state(irq_entry)) {
		/* Timer was cancelled while it was in timer queue.
		 * No work should be done by the timer. */
		return;
	}

	hpd_notify(connector_irq);

	/* Note that we allow for next timer only at the very end of
	 * handling of this timer. */
	unregister_interrupt(tm_dm, TM_INTERRUPT_TYPE_TIMER, connector_irq);
}

/* Passive DP-to-HDMI or DP-to-DVI dongles may introduce instability on
 * HPD Pin. */
static bool dp_passive_dongle_hpd_workaround(
		struct tmdm_irq_registration *connector_irq)
{
	struct tm_detection_mgr *tm_dm = connector_irq->detection_mgr;
	struct dal_context *dal_context = tm_dm->dal_context;
	struct display_path *display_path = connector_irq->displays[0];
	bool schedule_timer = sw_hpd_filter_get(connector_irq);

	if (connector_irq->connected_display == NULL
			&& tm_utils_is_dp_connector(connector_irq->connector)) {
		enum signal_type asic_signal;
		/* DP *connector*. Sleep 50ms before detection because of noise
		 * on DDC pin.
		 * Note that MST Long/Short pulse interrupt is handled by
		 * DP Link Service, not here. */
		dal_sleep_in_milliseconds(50);

		/* We are disconnected (because connected_display == NULL).
		 * That means we are handling a connect event.
		 * DP mutable path cannot rely on sw_hpd_filter_get().
		 * Instead we need to detect ASIC signal and decide
		 * based on it. */
		asic_signal = detect_asic_signal(tm_dm, display_path);

		TM_HPD_IRQ("%s: Connector=DP, ASIC signal=%s\n", __func__,
				tm_utils_signal_type_to_str(asic_signal));

		if (dal_is_dp_signal(asic_signal) == false) {
			/* DP Connector but non-DP Signal - Passive dongle
			 * case. */
			schedule_timer = true;
		} else {
			/* Both Connector and Signal are DP. */
			schedule_timer = false;
		}
	}

	TM_HPD_IRQ("%s: schedule de-bouncing timer: %s\n", __func__,
			(schedule_timer == true ? "true" : "false"));

	return schedule_timer;
}

/**
 *****************************************************************************
 * Handles HotPlug interrupt - reschedule timer or notify connectivity
 *      changed
 *
 *  @param [in] connector_irq: Entry corresponding to occurred interrupt
 *****************************************************************************
 */
static void tmdm_handle_hpd_interrupt(void *interrupt_params)
{
	struct tmdm_irq_registration *connector_irq = interrupt_params;
	struct tm_detection_mgr *tm_dm = connector_irq->detection_mgr;
	struct dal_context *dal_context = tm_dm->dal_context;
	struct display_path *display_path = connector_irq->displays[0];

	TM_INFO("HPD interrupt: irq_src=%d\n",
			IRQ_ENTRY_HPD(connector_irq)->irq_src);

	if (dp_passive_dongle_hpd_workaround(connector_irq)) {
		register_interrupt(tm_dm, display_path,
				TM_INTERRUPT_TYPE_TIMER,
				connector_irq,
				DP_PASSIVE_DONGLE_INTERVAL_IN_MS);
	} else {
		/* notify without delay */
		hpd_notify(connector_irq);
	}
}

/*****************************************************************************
 *	public functions
 * **************************************************************************/

struct tm_detection_mgr *dal_tm_detection_mgr_create(
		struct tm_detection_mgr_init_data *init_data)
{
	struct tm_detection_mgr *tm_dm;

	if (init_data->as == NULL || init_data->hwss == NULL
		|| init_data->resource_mgr == NULL
		|| init_data->tm == NULL) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	tm_dm = dal_alloc(sizeof(*tm_dm));

	if (tm_dm == NULL)
		return NULL;

	if (construct(tm_dm, init_data))
		return tm_dm;
	dal_free(tm_dm);
	return NULL;
}

void dal_tm_detection_mgr_destroy(
		struct tm_detection_mgr **tm_dm)
{
	if (!tm_dm || !*tm_dm) {
		BREAK_TO_DEBUGGER();
		return;
	}

	destruct(*tm_dm);

	dal_free(*tm_dm);
	*tm_dm = NULL;
}

void dal_tm_detection_mgr_init_hw(
		struct tm_detection_mgr *tm_dm)
{
	uint8_t i;
	struct tmdm_irq_registration *connector_irq;
	struct dal_context *dal_context = tm_dm->dal_context;

	for (i = 0; i < tm_dm->irq_registrations_num; ++i) {
		connector_irq = &tm_dm->connector_irq_regsitrations[i];
		if (connector_irq->connector == NULL) {
			/* This situation is possible if number of connectors
			 * is greater than number of display path. In other
			 * words, a creation of a display path failed for a
			 * connector. */
			TM_WARNING("%s: 'connector_irq->connector' is NULL!\n",
					__func__);
			continue;
		}

		/*Setup initial state of HW*/
		if (hw_hpd_filter_get(connector_irq)) {
			dal_connector_program_hpd_filter(
					connector_irq->connector,
					DELAY_ON_CONNECT_IN_MS,
					DELAY_ON_DISCONNECT_IN_MS);
		} else {
			dal_connector_program_hpd_filter(
					connector_irq->connector, 0, 0);
		}

		/*Update HW state based on current connectivity state*/
		if (connector_irq->connected_display != NULL)
			update_irq_on_connect(tm_dm, connector_irq);
	}
}

void dal_tm_detection_mgr_release_hw(
		struct tm_detection_mgr *tm_dm)
{
	uint8_t i;

	if (tm_dm->connector_irq_regsitrations == NULL)
		return;

	for (i = 0; i < tm_dm->irq_registrations_num; ++i) {
		uint8_t j;

		for (j = 0; j < TM_INTERRUPT_TYPE_COUNT; ++j) {
			enum tm_interrupt_type irq_type;

			irq_type = TM_INTERRUPT_TYPE_TIMER;
			if (j > 0)
				irq_type = TM_INTERRUPT_TYPE_HOTPLUG;

			unregister_interrupt(tm_dm, irq_type,
					&tm_dm->connector_irq_regsitrations[i]);
		}
	}
}

bool dal_tm_detection_mgr_detect_display(
		struct tm_detection_mgr *tm_dm,
		struct display_path *display_path,
		enum tm_detection_method method,
		struct tm_detection_status *detection_status)
{
	bool destructive;
	bool detect_performed = false;
	struct display_state original_info;

	destructive = tm_utils_is_destructive_method(method);

	if (display_path == NULL || detection_status == NULL)
		return detect_performed;

	detection_status->detected_signal = dal_display_path_get_query_signal(
			display_path, SINK_LINK_INDEX);

	detection_status->connected = dal_display_path_is_target_connected(
			display_path);

	/**Do physical detection or handle the case when no physical detection
	 * should be done. In both cases detection_status will have valid info.
	 * Detection procedure does not change display_path_state
	 * (only can update DCS)
	 */
	if (handle_skipping_detection(tm_dm, display_path, method,
			detection_status)) {
		/* no need to do physical detection */
		return detect_performed;
	}

	if (!tm_resource_mgr_acquire_resources(tm_dm->resource_mgr,
			display_path, TM_ACQUIRE_METHOD_SW))
		return detect_performed;

	/**Step 1: retrieve the current sink capabilities and Edid
	 * we need them for later on to see if after detection,
	 * information changed
	 */
	do_target_pre_processing(tm_dm, display_path, &original_info);

	/*Step 2: perform real detection*/
	do_target_detection(tm_dm, display_path, destructive, detection_status);

	/**Step 3: perform emulation if needed
	 * do_target_emulation(display_path, destructive,
	 * detection_status);
	 */

	/**Step 4: compare to previous information (sink/Edid)
	 * and finalize DCS and detection information
	 */
	do_target_post_processing(tm_dm, display_path, detection_status,
			&original_info);

	/*Final Step: Connect/Disconnect link services*/
	reconnect_link_services(tm_dm, display_path, detection_status);

	detect_performed = true;

	/* After reconnect_link_services(), we need consider downstream
	 * sinkcount is 0 case, i.e. not report connected to OS and not update
	 * internal modelist in postTargetDetection
	 */
	if (need_handle_connection_status_based_on_sink_count(tm_dm,
			detection_status))
		detection_status->connected = false;

	/*Revert to cached state if this is MST detection*/
	if (detection_status->dp_mst_detection) {
		detection_status->detected_signal =
				dal_display_path_get_query_signal(display_path,
					SINK_LINK_INDEX);
		detection_status->connected =
					dal_display_path_is_target_connected(
					display_path);
	}

	tm_resource_mgr_release_resources(tm_dm->resource_mgr, display_path,
			TM_ACQUIRE_METHOD_SW);

	return detect_performed;
}

bool dal_tm_detection_mgr_retreive_sink_info(
		struct tm_detection_mgr *tm_dm,
		struct display_path *display_path,
		enum tm_detection_method method,
		struct tm_detection_status *detection_status)
{
	struct display_state original_info;
	bool destructive = tm_utils_is_destructive_method(method);
	bool detect_performed = false;

	if (display_path == NULL || detection_status == NULL)
		return false;

	if (method != DETECTION_METHOD_CACHED) {
		/**Retrieve the current sink capabilities and Edid
		 * we need the Edid information to compare it with
		 * the Edid after being read see if anything changed
		 * NOTE: the extra work with
		 * do_target_pre_processing/do_target_post_processing
		 * is because we currently don't have an Edid
		 * class that can provide comparing capability
		 * NOTE: do_target_pre_processing/do_target_post_processing
		 * do a bit more checking other than comparing
		 * the Edid, but the other information should
		 * not change, so it shouldn't come into play
		 */

		do_target_pre_processing(tm_dm, display_path, &original_info);

		dal_dcs_query_sink_capability(
				dal_display_path_get_dcs(display_path),
				&detection_status->sink_capabilities, 0);

		read_edid(tm_dm, display_path, destructive, detection_status);

		/**compare to previous information (sink/Edid) - in this
		 * case we're only interested in the Edid
		 */
		do_target_post_processing(
				tm_dm,
				display_path,
				detection_status,
				&original_info);

		detect_performed = true;
	} else
		dal_dcs_get_sink_capability(
				dal_display_path_get_dcs(display_path),
				&detection_status->sink_capabilities);

	return detect_performed;
}

void dal_tm_detection_mgr_reschedule_detection(
		struct tm_detection_mgr *tm_dm,
		struct display_path *display_path,
		bool reschedule)
{
	struct tmdm_irq_registration *connector_irq;

	if (display_path == NULL)
		return;

	connector_irq = get_irq_entry(tm_dm,
			get_connector_obj_id(display_path));

	if (connector_irq == NULL || connector_irq->displays_num == 0)
		return;

	unregister_interrupt(tm_dm, TM_INTERRUPT_TYPE_TIMER, connector_irq);

	if (!reschedule) {
		connector_irq->resched_count = 0;
		return;
	}

	if (connector_irq->resched_count >= NUM_OF_DETECTION_RETRIES) {
		connector_irq->resched_count = 0;
		return;
	}

	register_interrupt(tm_dm, display_path, TM_INTERRUPT_TYPE_TIMER,
			connector_irq, RESCHED_TIMER_INTERVAL_IN_MS);

	connector_irq->resched_count++;
}

/* IRQ Source registration and state update methods.
 *
 * This function will *not* register for HPD Interrupt.
 * To register for HPD Interrupt call dal_tm_detection_mgr_register_hpd_irq().
 */
bool dal_tm_detection_mgr_register_display(
		struct tm_detection_mgr *tm_dm,
		struct display_path *display_path)
{
	struct tmdm_irq_registration *connector_irq;
	struct dal_context *dal_context = tm_dm->dal_context;

	if (display_path == NULL) {
		TM_ERROR("%s: 'display_path' is NULL!\n", __func__);
		return false;
	}

	/* Obtain irq registration entry*/
	connector_irq = get_irq_entry(tm_dm,
			get_connector_obj_id(display_path));

	if (connector_irq == NULL) {
		TM_ERROR("%s: 'connector_irq' was not found!\n", __func__);
		return false;
	}

	/* First registration connector object is not cached - need to
	 * initialize entry */
	if (connector_irq->connector == NULL)
		if (!init_irq_entry(tm_dm, display_path, connector_irq))
			return false;

	/*We exceeded maximum allowed display registrations per entry*/
	if (connector_irq->displays_num >= MAX_NUM_OF_PATHS_PER_CONNECTOR) {
		TM_WARNING("%s: exceeded maximum registrations!\n", __func__);
		return false;
	}
	/* Update SW delay request in processing of HPD interrupt.
	 *
	 * Order of evaluation is important: 1st evaluate DP *connector*, as
	 * one with mutable DisplayPort. */
	if (IRQ_ENTRY_HPD(connector_irq)->irq_src != DAL_IRQ_SOURCE_INVALID) {

		if (tm_utils_is_dp_connector(connector_irq->connector)) {
			/* Case 1: mutable Display Port path.
			 *
			 * By mutable we mean possibility of having a passive
			 * DP->HDMI dongle.
			 * Start with no delay */

			sw_hpd_filter_set(connector_irq, false);
			/* Note: we START with disconnected path so
			 * schedule_hpd_timer can't be valid for this option */

		} else if (tm_utils_is_dp_asic_signal(display_path)) {
			/* Case 2: unmutable Display Port path.
			 *
			 * Make sure it is not overriding existing settings
			 * with higher priority.
			 * By unmutable we mean that ASIC signal will
			 * *always* be DP because there is an Active Converter
			 * on the board, which delivers the correct signal
			 * to the non-DP Connector. */
			sw_hpd_filter_set(connector_irq, false);
			hw_hpd_filter_set(connector_irq, false);
		}
	}

	/* Add display to notification list */
	connector_irq->displays[connector_irq->displays_num] = display_path;
	connector_irq->displays_num++;

	return true;
}

bool dal_tm_detection_mgr_register_hpd_irq(
		struct tm_detection_mgr *tm_dm,
		struct display_path *display_path)
{
	struct tmdm_irq_registration *connector_irq;
	struct dal_context *dal_context = tm_dm->dal_context;

	if (display_path == NULL) {
		TM_ERROR("%s: 'display_path' is NULL!\n", __func__);
		return false;
	}

	connector_irq = get_irq_entry(tm_dm,
			get_connector_obj_id(display_path));

	if (connector_irq == NULL) {
		TM_ERROR("%s: 'connector_irq' was not found!\n", __func__);
		return false;
	}

	if (connector_irq->connector == NULL) {
		TM_ERROR("%s: 'connector' is NULL!\n", __func__);
		return false;
	}

	register_interrupt(tm_dm, display_path, TM_INTERRUPT_TYPE_HOTPLUG,
			connector_irq, 0);

	return true;
}

void dal_tm_detection_mgr_update_active_state(
		struct tm_detection_mgr *tm_dm,
		struct display_path *display_path)
{
	struct tmdm_irq_registration *connector_irq;
	bool display_registered = false;
	uint8_t i;

	if (display_path == NULL)
		return;

	connector_irq = get_irq_entry(tm_dm,
			get_connector_obj_id(display_path));

	if (connector_irq == NULL || connector_irq->displays_num == 0)
		return;

	/*here only handle registered display path*/
	for (i = 0; i < connector_irq->displays_num; ++i) {
		/* TODO: check connector id only. */
		if (dal_graphics_object_id_is_equal(
				get_connector_obj_id(
						connector_irq->displays[i]),
				get_connector_obj_id(display_path)))
			display_registered = true;
		break;
	}

	if (!display_registered)
		/*skip hanlding because the display path is not registered*/
		return;

	if (dal_display_path_is_target_connected(display_path)) {
		connector_irq->connected_display = display_path;
		update_irq_on_connect(tm_dm, connector_irq);
	} else {
		connector_irq->connected_display = NULL;
	}
}

void dal_tm_detection_mgr_set_blocking_detection(
		struct tm_detection_mgr *tm_dm,
		bool blocking)
{
	tm_dm->is_blocking_detection = blocking;
}

bool dal_tm_detection_mgr_is_blocking_detection(
		struct tm_detection_mgr *tm_dm)
{
	return tm_dm->is_blocking_detection;
}

void dal_tm_detection_mgr_set_blocking_interrupts(
		struct tm_detection_mgr *tm_dm,
		bool blocking)
{
	tm_dm->is_blocking_interrupts = blocking;
}

bool dal_tm_detection_mgr_is_blocking_interrupts(
		struct tm_detection_mgr *tm_dm)
{
	return tm_dm->is_blocking_interrupts;
}

/******************************************************************************
 * Check if the monitor patch flag is set.
 * If yes, then we program HPD filter with the delay that is defined in
 * the patch.
 * If no, then we program HPD filter to default value.
 *
 * TODO: check whether we should handle active converter sinkcount info?
 *****************************************************************************/
void dal_tm_detection_mgr_program_hpd_filter(
		struct tm_detection_mgr *tm_dm,
		struct display_path *display_path)
{
	struct connector *connector;
	struct dcs *dcs;
	uint32_t delay_on_disconnect_in_ms = DELAY_ON_DISCONNECT_IN_MS;
	const struct monitor_patch_info *mon_patch_info;
	struct dal_context *dal_context = tm_dm->dal_context;
	uint32_t display_ind;

	connector = dal_display_path_get_connector(display_path);
	dcs = dal_display_path_get_dcs(display_path);
	display_ind = dal_display_path_get_display_index(display_path);

	/* Get HPD disconnect delay value from the monitor patch. */
	mon_patch_info = dal_dcs_get_monitor_patch_info(dcs,
			MONITOR_PATCH_TYPE_EXTRA_DELAY_ON_DISCONNECT);

	if (NULL != mon_patch_info) {
		delay_on_disconnect_in_ms = mon_patch_info->param;

		TM_HPD_IRQ(
			"%s: Path[%d]: 'patch delay' on disconnect: %d Ms\n",
			__func__, display_ind, delay_on_disconnect_in_ms);
	} else {
		TM_HPD_IRQ(
			"%s: Path[%d]: 'default delay' on disconnect: %d Ms\n",
			__func__, display_ind, delay_on_disconnect_in_ms);
	}

	dal_connector_program_hpd_filter(connector, DELAY_ON_CONNECT_IN_MS,
			delay_on_disconnect_in_ms);
}
