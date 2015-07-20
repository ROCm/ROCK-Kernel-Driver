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

#ifndef __DAL_TOPOLOGY_MGR_INTERFACE_H__
#define __DAL_TOPOLOGY_MGR_INTERFACE_H__

#include "grph_object_defs.h"
#include "grph_object_ctrl_defs.h"
#include "signal_types.h"
#include "adapter_service_interface.h"
#include "display_path_interface.h"
#include "ddc_interface.h"
#include "hw_sequencer_interface.h"
#include "timing_service_interface.h"

#include "plane_types.h"

/****** forward declarations ******/
struct topology_mgr;
struct display_path_set;
struct display_controller_pair;
struct gpu_clock_interface;
struct ddc;
struct vector;

enum tm_detection_method {
	DETECTION_METHOD_CACHED = 1,
	DETECTION_METHOD_NON_DESTRUCTIVE,
	DETECTION_METHOD_DESTRUCTIVE,
	DETECTION_METHOD_DESTRUCTIVE_AND_EMBEDDED,
	DETECTION_METHOD_CONNECTED_ONLY,
	DETECTION_METHOD_HOTPLUG
};

enum tm_reenum_modes_reason {
	RMR_FORCED_CAPABILITY_CHANGE,
	RMR_TV_FORMAT_CHANGE,
	RMR_CAP_CHANGE_DO_NOT_REFRESH_MODE_IF_SUPPORTED,
	/* Cap. change when detect EDID changing or sink capability changing */
	RMR_DETECTED_CAPABILITY_CHANGE
};


/* Topology Manager initialisation data. */
struct topology_mgr_init_data {
	struct dal_context *dal_context;
	struct adapter_service *adapter_srv;
	struct timing_service *timing_srv;
	struct hw_sequencer *hwss_srvr;
	struct mode_manager *mm;
};

/****** return codes ******/
enum tm_result {
	TM_RESULT_FAILURE,
	TM_RESULT_SUCCESS,
	TM_RESULT_DISPLAY_CONNECTED, /* HPD high */
	TM_RESULT_DISPLAY_DISCONNECTED /* HPD low */
};

/****** interface functions ******/

/** Call to create the Topology Manager */
struct topology_mgr *dal_tm_create(struct topology_mgr_init_data *init_data);
/** Call to destroy the Topology Manager */
void dal_tm_destroy(struct topology_mgr **tm);

/**********************************
 Per-Display Path handlers/queries
***********************************/
/** Acquire the display path - mark it as used */
enum tm_result dal_tm_acquire_display_path(struct topology_mgr *tm_mgr,
		uint32_t display_index);

/** Release the display path - mark it as not used */
void dal_tm_release_display_path(struct topology_mgr *tm_mgr,
		uint32_t display_index);

/** Release the display path enabled by vBIOS */
void dal_tm_release_vbios_enabled_display_path(struct topology_mgr *tm_mgr,
		uint32_t display_index);

/** Create active duplicate display path */
struct display_path *dal_tm_create_resource_context_for_display_index(
		struct topology_mgr *tm_mgr,
		uint32_t display_index);

/** Reverse actions of dal_tm_create_resource_context_for_display_index() */
void dal_tm_destroy_resource_context_for_display_path(
		struct topology_mgr *tm_mgr,
		struct display_path *display_path);

/** Acquire stereo-sync object on display path (the display path itself
 *  should be already acquired) */
enum tm_result dal_tm_attach_stereo_synch_to_display_path(
		struct topology_mgr *tm_mgr,
		uint32_t display_index);

/** Release stereo-sync object on display path (the display path itself
 * should be already acquired) */
void dal_tm_detach_stereo_sync_from_display_path(struct topology_mgr *tm_mgr,
		uint32_t display_index);

/** Returns stereo ranking (higher value = higher priority) for given
 * display path */
uint32_t dal_tm_get_display_path_stereo_priority(struct topology_mgr *tm_mgr,
		uint32_t display_index,
		bool display_stereo_active);

/** Acquire sync-output resources on display path (the display path itself
 * should be already acquired) */
enum tm_result dal_tm_attach_sync_output_to_display_path(
		struct topology_mgr *tm_mgr,
		uint32_t display_index,
		enum sync_source sync_output);

/** Release sync-output resources on display path (the display path itself
 *  should be already acquired) */
void dal_tm_detach_sync_output_from_display_path(struct topology_mgr *tm_mgr,
		uint32_t display_index);

/** Moves sync-output resources from one display path to other */
enum tm_result dal_tm_move_sync_output_object(
		struct topology_mgr *tm_mgr,
		uint32_t src_display_index,
		uint32_t tgt_display_index);

/**  Convert DisplayPathInterface to display index */
uint32_t dal_tm_display_path_to_display_index(struct topology_mgr *tm_mgr,
		struct display_path *p_display_path);

/**  Convert display index to DisplayPathInterface*/
struct display_path *dal_tm_display_index_to_display_path(
		struct topology_mgr *tm_mgr,
		uint32_t display_index);

/**	 Iterates and returns the first found wireless display index*/
uint32_t dal_tm_get_wireless_display_index(
	struct topology_mgr *tm);

/************************************
Display combinations handlers/queries
*************************************/
/** Gets the number of available display paths */
uint32_t dal_tm_get_num_display_paths(struct topology_mgr *tm_mgr,
		bool display_targets_only);

/**  Query the max number of display paths (excluding CF paths) that can be
 *  enabled simultaneously */
uint32_t dal_tm_max_num_cofunctional_targets(struct topology_mgr *tm_mgr);

/** Queries the number of connected displays that support audio */
uint32_t dal_tm_get_num_connected_audio_displays(struct topology_mgr *tm_mgr);

/** Request if the specified array of DisplayPaths can be enabled
 * simultaneously */
bool dal_tm_can_display_paths_be_enabled_at_the_same_time(
		struct topology_mgr *tm_mgr,
		const uint32_t display_index_array[],
		uint32_t array_size);

/** Return an array of display indexes sorted according to display
 * selection priority */
const uint32_t *dal_tm_get_display_selection_priority_array(
		struct topology_mgr *tm_mgr);

/** Allocates cofunctional set of activated display paths */
struct display_path_set*
dal_tm_create_resource_context_for_display_indices(
		struct topology_mgr *tm_mgr,
		const uint32_t display_index_array[],
		uint32_t array_size);

/** Destroys a set of display paths */
void dal_tm_display_path_set_destroy(
		struct topology_mgr *tm_mgr,
		struct display_path_set **display_path_set);

/** return a bit vector of controllers mapped to given array of display
 * path indexes */
enum tm_result dal_tm_get_controller_mapping(
		struct topology_mgr *tm_mgr,
		const uint32_t display_index_array[],
		uint32_t array_size,
		bool use_current_mapping,
		struct display_controller_pair *pairs);


/*******************
Display Path lookup
********************/
/** Finds a display path given encoder, connector and signal type */
struct display_path *dal_tm_find_display_path(
		struct topology_mgr *tm_mgr,
		struct graphics_object_id encoder_id,
		struct graphics_object_id connector_id,
		enum signal_type sig_type);

/** Find display path according to device type */
struct display_path *dal_tm_find_display_path_with_device_type(
		struct topology_mgr *tm_mgr,
		enum dal_device_type dev_type);

/** Returns an active display index given a controller handle */
uint32_t dal_tm_find_display_path_with_controller(
		struct topology_mgr *tm_mgr,
		uint32_t controller_handle);

/** Returns pointer to Controller assigned to Path, or NULL if Path is not
 * acquired. */
struct controller *dal_tm_get_controller_from_display_path(
		struct topology_mgr *tm_mgr,
		struct display_path *display_path);

/********************
Programming sequences
*********************/
/** Initialise all HW blocks at boot/resume/tdr, needed for detection
 * prior set mode. */
enum tm_result dal_tm_init_hw(struct topology_mgr *tm_mgr);

/** power down all HW blocks before ACPI non-D0 state */
enum tm_result dal_tm_power_down_hw(struct topology_mgr *tm_mgr);

/** power down all HW blocks before ACPI non-D0 state */
enum tm_result dal_tm_power_down_hw_active(struct topology_mgr *tm_mgr,
		const uint32_t display_index_array[],
		uint32_t array_size);

/** powerdown all the objects that compose the display paths */
enum tm_result dal_tm_power_down_path_elements(struct topology_mgr *tm_mgr);

/** reset logical state for controllers */
void dal_tm_reset_vbios_controllers(struct topology_mgr *tm_mgr);
/*ResetControllersForFSDOSToWindows()*/

/*****************
Display Detection
*****************/
/** does detection on all display paths and assigns audio resources
 * based on priority */
void dal_tm_do_initial_detection(struct topology_mgr *tm_mgr);

/** does detection on specific connector */
void dal_tm_do_detection_for_connector(struct topology_mgr *tm_mgr,
		uint32_t connector_index);

/** does detection on all display paths in certain order to make sure
 * resources allocated properly */
uint32_t dal_tm_do_complete_detection(struct topology_mgr *tm_mgr,
		enum tm_detection_method method,
		bool emulate_connectivity_change);

/** Does detection in separate thread in order not to delay the
 * calling thread. Used during S3->S0 transition. */
enum tm_result dal_tm_do_asynchronous_detection(struct topology_mgr *tm_mgr);

/** Register with base driver to receive Interrupt notification for Display
 * Connect/Disconnect Events. The interrupts are triggered by HPD pin. */
enum tm_result dal_tm_register_for_display_detection_interrupt(
		struct topology_mgr *tm_mgr);

/** Revert actions of dal_tm_register_for_display_detection_interrupt(). */
enum tm_result dal_tm_unregister_from_display_detection_interrupt(
		struct topology_mgr *tm_mgr);

/*************
Power related
**************/

/** enables or disables the Base Light Sleep */
void dal_tm_toggle_hw_base_light_sleep(struct topology_mgr *tm_mgr,
		bool enable);

/** handle transition from VBIOS/VGA mode to driver/accelerated mode */
void dal_tm_enable_accelerated_mode(struct topology_mgr *tm_mgr);

/** block interrupt if we are under VBIOS (FSDOS) */
void dal_tm_block_interrupts(struct topology_mgr *tm_mgr, bool blocking);

/** Acquiring Embedded display based on current HW config */
enum tm_result dal_tm_setup_embedded_display_path(struct topology_mgr *tm_mgr);

/** Release HW access and possibly restore some HW registers to its
 * default state */
void dal_tm_release_hw(struct topology_mgr *tm_mgr);

/** Sets new adapter power state */
void dal_tm_set_current_power_state(struct topology_mgr *tm_mgr,
		enum dal_video_power_state power_state);

bool dal_tm_update_display_edid(struct topology_mgr *tm_mgr,
				uint32_t display_index,
				uint8_t *edid_buffer,
				uint32_t buffer_len);

/**************
General queries
***************/
/** check is it coming from VBIOS or driver already made mode set at least
 * once */
bool dal_tm_is_hw_state_valid(struct topology_mgr *tm_mgr);

/** Query whether sync output can be attached to display path */
bool dal_tm_is_sync_output_available_for_display_path(
		struct topology_mgr *tm_mgr,
		uint32_t display_index,
		enum sync_source sync_output);

/** Query Embedded display index */
uint32_t dal_tm_get_embedded_device_index(struct topology_mgr *tm_mgr);

/** Get GPU Clock Interface */
struct gpu_clock_interface *dal_tm_get_gpu_clock_interface(
		struct topology_mgr *tm_mgr);

/** Get DPCD Access Service Interface (the same as DDC) */
struct ddc *dal_tm_get_dpcd_access_interface(
		struct topology_mgr *tm_mgr,
		uint32_t display_index);

/** Get DDC Access Service Interface - by display index */
struct ddc *dal_tm_get_ddc_access_interface_by_index(
		struct topology_mgr *tm_mgr,
		uint32_t display_index);

/** Get DdcAccessServiceInterface - by connector */
struct ddc *dal_tm_get_ddc_access_interface_by_connector(
		struct topology_mgr *tm_mgr,
		struct graphics_object_id connector);

/** Report the number of functional controllers */
uint32_t dal_tm_get_num_functional_controllers(struct topology_mgr *tm_mgr);

/* If controller_id Controller is acquired on a Path - return Path index.
 * Else - return INVALID_DISPLAY_INDEX. */
uint32_t dal_tm_get_display_path_index_for_controller(
		struct topology_mgr *tm_mgr,
		enum controller_id controller_id);

/** Returns current adapter power state */
enum dal_video_power_state dal_tm_get_current_power_state(
		struct topology_mgr *tm_mgr);

/** Returns previous adapter power state */
enum dal_video_power_state dal_tm_get_previous_power_state(
		struct topology_mgr *tm_mgr);

struct gpu *dal_tm_get_gpu(struct topology_mgr *tm_mgr);

/**********************
General functionality
***********************/
/** update signal type of CrossFire Display Path according to upper
 *  layer's request */
enum tm_result dal_tm_set_signal_type(struct topology_mgr *tm_mgr,
		uint32_t display_index,
		enum signal_type signal);

/** Sets connectivity state to a display path if it supports "force connect" */
enum tm_result dal_tm_set_force_connected(struct topology_mgr *tm_mgr,
		uint32_t display_index,
		bool connected);

/** update BIOS scratch registers about requested and active displays */
void dal_tm_force_update_scratch_active_and_requested(
		struct topology_mgr *tm_mgr);

/** Perform target connectivity check */
enum tm_result dal_tm_detect_and_notify_target_connection(
		struct topology_mgr *tm_mgr,
		uint32_t display_index,
		enum tm_detection_method method);

/** External entity notifies TM about the new connectivity (via Escape) */
void dal_tm_detect_notify_connectivity_change(
		struct topology_mgr *tm_mgr,
		uint32_t display_index,
		bool connected);

/** External entity requests to re-enumerate the mode list of a device
 * and notify OS about the change. */
void dal_tm_notify_capability_change(
		struct topology_mgr *tm_mgr,
		uint32_t display_index,
		enum tm_reenum_modes_reason reason);

/**************
Debug interface
***************/
/** Prints the content of a display path*/
void dal_tm_dump_display_path(struct topology_mgr *tm_mgr,
		uint32_t display_index);

/** Prints the content of all display paths and some other content*/
void dal_tm_dump(struct topology_mgr *tm_mgr);

/** Blank CRTC and disable memory requests */
void dal_tm_disable_all_dcp_pipes(struct topology_mgr *tm_mgr);

/***************
Planes interface
****************/
void dal_tm_acquire_plane_resources(
	struct topology_mgr *tm,
	uint32_t display_index,
	uint32_t num_planes,
	const struct plane_config *configs);

void dal_tm_release_plane_resources(
	struct topology_mgr *tm,
	uint32_t display_index);

/*******************************
 Handles Hotplug/Hotunplug event
 *******************************/
void dal_tm_handle_sink_connectivity_change(
	struct topology_mgr *tm,
	uint32_t display_index);

#endif /* __DAL_TOPOLOGY_MGR_INTERFACE_H__ */
