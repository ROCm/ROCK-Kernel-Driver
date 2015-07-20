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

#ifndef __DAL_LINK_SERVICE_H__
#define __DAL_LINK_SERVICE_H__

#include "include/link_service_interface.h"

struct ddc_service;
struct link_service;
struct display_path;
struct hw_path_mode;
struct adapter_service;

struct link_service_funcs {

	void (*destroy)(struct link_service **);

	/* return true if given ModeTiming can be driven to
	 * the sink mapped to given display index.
	 */
	bool (*validate_mode_timing)(struct link_service *ls,
			uint32_t display_index,
			const struct hw_crtc_timing *timing,
			struct link_validation_flags flags);

	/* report properties associated to the sink device
	 *  mapped to the given display indx
	 */
	bool (*get_mst_sink_info)(struct link_service *ls,
			uint32_t display_index,
			struct mst_sink_info *sink_info);

	/*get GTC sync status*/
	bool (*get_gtc_sync_status)(struct link_service *ls);

	/* move stream in/out 'Enabled' state
	 * from 'Disable' state
	 */
	bool (*enable_stream)(struct link_service *ls,
			uint32_t display_index,
			struct hw_path_mode *path_mode);

	bool (*disable_stream)(struct link_service *ls,
			uint32_t display_index,
			struct hw_path_mode *path_mode);

	/* to do: may have to change "display_path"
	 * to "hw_display_path_interface"
	 */
	bool (*optimized_enable_stream)(struct link_service *ls,
			uint32_t display_index,
			struct display_path *display_path);

	/* move stream in/out of 'Acitve' (VID + AUD EN)
	 * from 'Enabled' state
	 */
	bool (*blank_stream)(struct link_service *ls,
			uint32_t display_index,
			struct hw_path_mode *path_mode);

	bool (*unblank_stream)(struct link_service *ls,
			uint32_t display_index,
			struct hw_path_mode *path_mode);

	/* move stream in/out of 'Mode Change Safe'
	 * state from 'Enabled' state
	 */
	bool (*pre_mode_change)(struct link_service *ls,
			uint32_t display_index,
			struct hw_path_mode *path_mode);

	bool (*post_mode_change)(struct link_service *ls,
			uint32_t display_index,
			struct hw_path_mode *path_mode);

	/* move stream in/out of 'Power Save"
	 * state from 'Enabled' state
	 */
	bool (*power_on_stream)(struct link_service *ls,
			uint32_t display_index,
			struct hw_path_mode *path_mode);

	bool (*power_off_stream)(struct link_service *ls,
			uint32_t display_index,
			struct hw_path_mode *path_mode);

	/* Notify LS to connect the link, and retrieve
	 * link capabilities for MST, it starts
	 * topology discovery process and calls
	 * MstTopologyChangeCallback when
	 * discovery finishes.
	 */
	void (*connect_link)(struct link_service *ls,
			const struct display_path *display_path,
			bool initial_detection);

	/* Notify LS to clean up connection states
	 * for MST, MstTopologyChangeCallback is
	 * called to notify changes on a display index.
	 */
	void (*disconnect_link)(struct link_service *ls);

	/* return if MST Network is present*/
	bool (*is_mst_network_present)(struct link_service *ls);

	/* invalidate downstream devices*/
	void (*invalidate_downstream_devices)(struct link_service *ls);

	/* validates the given display indices in the
	 * array 'arrayDisplayIndex' of size 'len' for
	 * cofunctionality. If all channels mapped to
	 * the display indices could be enabled at the
	 * same time, the function return true, otherwise return false.
	 */
	bool (*are_mst_displays_cofunctional)(
			struct link_service *ls,
			const uint32_t *array_display_index,
			uint32_t len);

	/**
	 * @brief interfaces to program / query sink device
	 * mapped a display index
	 */
	/* return true if a sink is assigned at given display index.*/
	bool (*is_sink_present_at_display_index)(
			struct link_service *ls,
			uint32_t display_index);

	/* returns a interface to query the DDC channel to
	 * the sink device mapped to the given display index
	 * his interface would allows for retrieving of cached
	 * EDID and device capability, as well as I2C or AUX
	 * requests to the sink device on the channel.
	 * @note after obtain MstMgr will not modify the cached
	 * data until MstDdcService is released
	 */
	struct ddc_service *(*obtain_mst_ddc_service)(
							struct link_service *ls,
							uint32_t display_index);

	void (*release_mst_ddc_service)(
			struct link_service *ls,
			struct ddc_service *ddc_srv);

	/* function to destroy Mst Manager*/
	void (*destroy_link_service)(
			struct link_service *ls);

	/* Release HW access and possibly restore some HW
	 * registers to its default state
	 */
	void (*release_hw)(struct link_service *ls);

	/* Assign the given display index to the MST Manager
	 * to manage, along with the corresponding display path.
	 * Function fail if the number of display index to manage
	 * exceed numOfDisplay given by CreateMstManager.
	 * All display index which this MST manager will manage
	 * should be added during initialization, before any
	 * other interfaces are called
	 */
	bool (*associate_link)(struct link_service *ls,
			uint32_t display_index, uint32_t link_index,
			bool is_internal_link);

	void (*decide_link_settings)(struct link_service *ls,
			const struct hw_path_mode *path_mode,
			struct link_settings *link_setting);

	bool (*should_send_notification)(struct link_service *ls);
	uint32_t (*get_notification_display_idx)(struct link_service *ls);
	void (*retrain_link)(struct link_service *ls,
					struct hw_path_mode_set *path_set);

	void (*update_stream_features)(struct link_service *ls,
			const struct hw_path_mode *path_mode);

	bool (*is_stream_drr_supported)(struct link_service *ls);

	bool (*is_link_psr_supported)(struct link_service *ls);

	void (*set_link_psr_capabilities)(struct link_service *ls,
			struct psr_caps *psr_caps);

	void (*get_link_psr_capabilities)(struct link_service *ls,
			struct psr_caps *psr_caps);
};

enum stream_state {
	STREAM_STATE_DISABLED = 0,
	/* Link+stream enabled but features
	 * (dmcu) still disabled*/
	STREAM_STATE_OPTIMIZED_READY,
	STREAM_STATE_ENABLED, /* blanked, ready*/
	STREAM_STATE_ACTIVE,  /* VID + AUD EN*/
	STREAM_STATE_MODE_CHANGE_SAFE,
	STREAM_STATE_POWER_SAVE/* DPMS off*/
};

struct link_property {
	uint32_t INTERNAL:1; /* not physical cable, wires on PCB*/
};

struct link_state {
	uint32_t CONNECTED:1;
	uint32_t INVALIDATED:1;
	uint32_t RESET_REQUESTED:1;
	uint32_t SINK_CAP_CONSTANT:1;
	uint32_t FAKE_CONNECTED:1;
};

struct link_service {
	const struct link_service_funcs *funcs;

	/* dependencies*/
	struct hw_sequencer *hwss;

	/* property*/
	enum link_service_type link_srv_type;
	uint32_t link_idx;
	/* For link status change notifications. Use
	 * get_notification_display_idx() to access this index.*/
	uint32_t link_notification_display_index;

	/* states*/
	uint32_t   encrypted_stream_count;
	enum stream_state strm_state;
	struct link_state link_st;
	struct link_property link_prop;
	struct link_settings cur_link_setting;
	struct graphics_object_id connector_id;
	struct adapter_service *adapter_service;
	struct dal_context *dal_context;
	struct topology_mgr *tm;
	uint32_t connector_enum_id;

};

bool dal_link_service_construct(struct link_service *link_service,
			struct link_service_init_data *init_data);

bool dal_ls_try_enable_link_base(
		struct link_service *ls,
		const struct hw_path_mode *path_mode,
		const struct link_settings *link_setting);

void dal_ls_try_enable_stream(
	struct link_service *ls,
	const struct hw_path_mode *path_mode,
	const struct link_settings *link_setting);

void dal_ls_disable_link(struct link_service *ls,
	const struct hw_path_mode *path_mode);

bool dal_ls_disable_stream_base(struct link_service *ls,
	uint32_t display_index,
	struct hw_path_mode *path_mode);

bool dal_link_service_blank_stream(struct link_service *ls,
				uint32_t display_index,
				struct hw_path_mode *path_mode);

bool dal_link_service_unblank_stream(struct link_service *ls,
			uint32_t display_index,
			struct hw_path_mode *path_mode);

bool dal_link_service_pre_mode_change(struct link_service *ls,
			uint32_t display_index,
			struct hw_path_mode *path_mode);

bool dal_link_service_post_mode_change(struct link_service *ls,
			uint32_t display_index,
			struct hw_path_mode *path_mode);

bool dal_link_service_power_off_stream(struct link_service *ls,
			uint32_t display_index,
			struct hw_path_mode *path_mode);

bool dal_link_service_power_on_stream(struct link_service *ls,
			uint32_t display_index,
			struct hw_path_mode *path_mode);

uint32_t dal_link_service_get_notification_display_idx(
		struct link_service *ls);

bool dal_link_service_is_mst_network_present(
		struct link_service *ls);

bool dal_link_service_is_sink_present_at_display_index(
				struct link_service *ls,
				uint32_t display_index);

struct ddc_service *dal_link_service_obtain_mst_ddc_service(
				struct link_service *ls,
				uint32_t display_index);

void dal_link_service_release_mst_ddc_service(
				struct link_service *ls,
				struct ddc_service *ddc_service);

void dal_link_service_invalidate_downstream_devices(
		struct link_service *ls);

bool dal_link_service_are_mst_displays_cofunctional(
				struct link_service *ls,
				const uint32_t *array_display_index,
				uint32_t len);

bool dal_link_service_get_mst_sink_info(
		struct link_service *ls,
		uint32_t display_index,
		struct mst_sink_info *sink_info);

#endif /*__DAL_LINK_SERVICE_H__*/
