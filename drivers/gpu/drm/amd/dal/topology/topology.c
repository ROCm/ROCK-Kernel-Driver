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

#include "include/logger_interface.h"
#include "include/topology_mgr_interface.h"
#include "include/display_path_interface.h"
#include "include/connector_interface.h"
#include "include/encoder_interface.h"
#include "include/controller_interface.h"
#include "include/display_path_interface.h"
#include "include/audio_interface.h"
#include "include/dcs_interface.h"
#include "include/vector.h"
#include "include/display_path_set_interface.h"
#include "include/adapter_service_interface.h"
#include "include/mode_manager_interface.h"

#include "topology.h"
#include "tm_internal_types.h"
#include "tm_resource.h"
#include "tm_resource_mgr.h"
#include "tm_detection_mgr.h"
#include "tm_resource_builder.h"

#include "tm_utils.h"
#include "tm_subsets_cache.h"

/* file-level globals */

/* file-level structures */
struct topology_mgr {
	struct dal_context *dal_context;
	struct adapter_service *adapter_srv;
	struct timing_service *timing_srv;
	struct hw_sequencer *hwss_srvr;
	struct mode_manager *mm;

	struct tm_resource_mgr *tm_rm;
	struct tm_detection_mgr *tm_dm;
	struct tm_resource_builder *tm_rb;

	struct vector *display_paths;

	struct tm_subsets_cache *display_subsets;

	uint32_t max_num_of_non_dp_paths;
	/* A bitmap of signals which support 'single selected timing'
	 * feature. */
	uint32_t single_selected_timing_signals;

	uint32_t max_num_of_cofunctional_paths;
	uint32_t max_num_of_cofunctional_targets;

	uint32_t max_num_of_supported_hdmi;

	bool valid_cofunc_sets;

	enum dal_video_power_state current_power_state;
	enum dal_video_power_state previous_power_state;

	enum clock_sharing_level clock_sharing_level;

	uint32_t display_detection_mask;
	bool report_detection_changes;

	/* This flag indicates that if DAL is in process to power down HW.*/
	bool hw_power_down_required;

	uint32_t attached_hdmi_num;
};


/******************************************************************************
 *	Prototypes of private functions.
 *****************************************************************************/

static enum tm_result tm_init_during_construct(struct topology_mgr *tm);
static enum tm_result create_gpu_resources(struct topology_mgr *tm);
static enum tm_result create_real_display_paths(struct topology_mgr *tm);
static enum tm_result tm_update_encoder_implementations(
		struct topology_mgr *tm);

static enum tm_result add_fake_crt_vga_dvi_paths(struct topology_mgr *tm);
static enum tm_result miscellaneous_init(struct topology_mgr *tm);
static enum tm_result transfer_paths_from_resource_builder_to_tm(
			struct topology_mgr *tm);

static enum tm_result allocate_storage_for_link_services(
		struct topology_mgr *tm);
static void associate_link_services_with_display_paths(
			struct topology_mgr *tm);

static void tm_init_features(struct topology_mgr *tm);
static enum tm_result tm_update_internal_database(struct topology_mgr *tm);

static enum tm_result tm_handle_detection_register_display(
		struct topology_mgr *tm);

static bool tm_is_display_index_valid(struct topology_mgr *tm,
		uint32_t display_index, const char *caller_func);

static void tm_update_stream_engine_priorities(
		struct topology_mgr *tm);

static bool tm_create_initial_cofunc_display_subsets(
		struct topology_mgr *tm);

static enum clock_sharing_group tm_get_default_clock_sharing_group(
		struct topology_mgr *tm,
		enum signal_type signal,
		bool allow_per_timing_sharing);

static bool tm_check_num_of_cofunc_displays(
		struct topology_mgr *tm,
		uint32_t max_value,
		uint32_t max_subset_size);

static bool tm_can_display_paths_be_enabled_at_the_same_time(
		struct topology_mgr *tm,
		struct tm_resource_mgr *tm_rm_clone,
		const uint32_t *displays,
		uint32_t array_size);

static void handle_signal_downgrade(struct topology_mgr *tm,
		struct display_path *display_path,
		enum signal_type new_signal);

static void tm_update_on_connection_change(struct topology_mgr *tm,
		struct display_path *display_path,
		struct tm_detection_status *detection_status);

static bool is_display_index_array_valid(
		struct topology_mgr *tm,
		const uint32_t display_index_array[],
		uint32_t array_size);

static struct controller *get_controller_for_plane_index(
		struct topology_mgr *tm,
		struct display_path *path,
		uint32_t plane_index,
		const struct plane_config *plcfg,
		uint32_t *controller_index_out);

/******************************************************************************
 * type-safe macro definitions for vector handling
 *****************************************************************************/
DAL_VECTOR_AT_INDEX(display_paths, struct display_path **);
DAL_VECTOR_INSERT_AT(display_paths, struct display_path **);

/******************************************************************************
 *	Implementation of private functions.
 *****************************************************************************/

static bool construct(struct topology_mgr_init_data *init_data,
		struct topology_mgr *tm)
{
	bool init_err = false;
	struct tm_resource_mgr *tm_rm = NULL;
	struct tm_resource_mgr_init_data tm_rm_init_data;
	struct tm_detection_mgr *tm_dm = NULL;
	struct tm_detection_mgr_init_data tm_dm_init_data;
	struct tm_resource_builder *tm_rb = NULL;
	struct tm_resource_builder_init_data tm_rb_init_data;
	struct dal_context *dal_context = init_data->dal_context;

	TM_IFACE_TRACE();

	tm->dal_context = init_data->dal_context;
	tm->mm = init_data->mm;
	tm->adapter_srv = init_data->adapter_srv;
	tm->hwss_srvr = init_data->hwss_srvr;
	tm->timing_srv = init_data->timing_srv;

	tm->current_power_state = DAL_VIDEO_POWER_UNSPECIFIED;
	tm->previous_power_state = DAL_VIDEO_POWER_UNSPECIFIED;

	tm->hw_power_down_required = true;
	tm->clock_sharing_level = CLOCK_SHARING_LEVEL_NOT_SHAREABLE;

	dal_memset(&tm_rm_init_data, 0, sizeof(tm_rm_init_data));
	dal_memset(&tm_dm_init_data, 0, sizeof(tm_dm_init_data));
	dal_memset(&tm_rb_init_data, 0, sizeof(tm_rb_init_data));

	do {
		/* create/initialise Resource Manager */
		tm_rm_init_data.dal_context = init_data->dal_context;
		tm_rm_init_data.as = init_data->adapter_srv;

		tm_rm = tm_resource_mgr_create(&tm_rm_init_data);
		if (!tm_rm) {
			init_err = true;
			TM_ERROR("tm_resource_mgr_create() failed!\n");
			break;
		}

		tm->tm_rm = tm_rm;

		/* create/initialise Detection Manager */
		tm_dm_init_data.dal_context = init_data->dal_context;
		tm_dm_init_data.as = init_data->adapter_srv;
		tm_dm_init_data.hwss = init_data->hwss_srvr;
		tm_dm_init_data.resource_mgr = tm_rm;
		tm_dm_init_data.tm = tm;

		tm_dm = dal_tm_detection_mgr_create(&tm_dm_init_data);
		if (!tm_dm) {
			TM_ERROR("dal_tm_detection_mgr_create() failed!\n");
			init_err = true;
			break;
		}

		tm->tm_dm = tm_dm;

		/* create/initialise Resource Builder */
		tm_rb_init_data.dal_context = tm->dal_context;
		tm_rb_init_data.adapter_service = init_data->adapter_srv;
		tm_rb_init_data.timing_service = init_data->timing_srv;
		/* TODO: Possibly remove irq_manager */
		/* tm_rb_init_data.irq_manager = init_data->irq_manager; */
		tm_rb_init_data.hwss = init_data->hwss_srvr;
		tm_rb_init_data.resource_mgr = tm->tm_rm;
		tm_rb_init_data.tm = tm;

		tm_rb = tm_resource_builder_create(&tm_rb_init_data);
		if (!tm_rb) {
			TM_ERROR("tm_resource_builder_create() failed!\n");
			init_err = true;
			break;
		}

		tm->tm_rb = tm_rb;

	} while (0);

	if (false == init_err)
		init_err = (tm_init_during_construct(tm) != TM_RESULT_SUCCESS);

	if (true == init_err) {
		/* Clean-up.
		 * Note: Do NOT call dal_tm_destroy()! */
		if (tm->display_paths)
			dal_vector_destroy(&tm->display_paths);

		if (tm->tm_rm)
			tm_resource_mgr_destroy(&tm->tm_rm);

		if (tm->tm_dm)
			dal_tm_detection_mgr_destroy(&tm->tm_dm);

		if (tm->tm_rb)
			tm_resource_builder_destroy(&tm->tm_rb);

		return false;
	}

	/* All O.K. */
	return true;
}

static struct display_path *tm_get_display_path_at_index(
		struct topology_mgr *tm,
		uint32_t index)
{
	struct display_path **display_path_item;
	struct display_path *display_path;
	struct dal_context *dal_context = tm->dal_context;

	if (NULL == tm->display_paths) {
		/* We may get here if OS ignores error returned by
		 * dal_tm_create(). */
		TM_ERROR("%s: No display path were created!\n", __func__);
		return NULL;
	}

	display_path_item = display_paths_vector_at_index(
			tm->display_paths, index);

	if (NULL == display_path_item) {
		TM_ERROR("%s: no item at index:%d!\n", __func__, index);
		return NULL;
	}

	display_path = *display_path_item;

	return display_path;
}

static uint32_t tm_get_display_path_count(struct topology_mgr *tm)
{
	struct dal_context *dal_context = tm->dal_context;

	if (NULL == tm->display_paths) {
		/* We may get here if OS ignores error returned by
		 * dal_tm_create(). */
		TM_ERROR("%s: No display path were created!\n", __func__);
		return 0;
	}

	return dal_vector_get_count(tm->display_paths);
}

static void destruct(struct topology_mgr *tm)
{
	struct gpu *gpu = NULL;

	/* TODO: call dal_tm_unregister_from_display_detection_interrupt() */

	gpu = tm_resource_mgr_get_gpu_interface(tm->tm_rm);

	if (tm->display_paths) {
		uint32_t i;

		for (i = 0; i < tm_get_display_path_count(tm); i++) {
			struct display_path *display_path =
				tm_get_display_path_at_index(tm, i);
			struct dcs *dcs = dal_display_path_get_dcs(
				display_path);
			dal_dcs_destroy(&dcs);
			dal_display_path_destroy(&display_path);
		}

		dal_vector_destroy(&tm->display_paths);
	}

	if (gpu != NULL)
		dal_gpu_destroy(&gpu);

	if (tm->tm_rm)
		tm_resource_mgr_destroy(&tm->tm_rm);

	if (tm->tm_dm)
		dal_tm_detection_mgr_destroy(&tm->tm_dm);

	if (tm->tm_rb)
		tm_resource_builder_destroy(&tm->tm_rb);

	if (tm->display_subsets)
		dal_tm_subsets_cache_destroy(&tm->display_subsets);
}

/******************************************************************************
 *	Implementation of public functions.
 *****************************************************************************/

struct topology_mgr *dal_tm_create(struct topology_mgr_init_data *init_data)
{
	struct topology_mgr *tm = NULL;
	struct dal_context *dal_context = init_data->dal_context;

	tm = dal_alloc(sizeof(*tm));

	if (!tm) {
		TM_ERROR("dal_alloc() failed!\n");
		return NULL;
	}

	if (!construct(init_data, tm) == true) {
		dal_free(tm);
		return NULL;
	}

	return tm;
}

void dal_tm_destroy(struct topology_mgr **tm)
{
	if (!tm || !(*tm))
		return;

	/***************************************
	 * deallocate all subcomponents of TM
	 ***************************************/
	destruct(*tm);

	/***************************************
	 * deallocate TM itself
	 ***************************************/
	dal_free(*tm);
	*tm = NULL;
}

/**********************************
 Per-Display Path handlers/queries
***********************************/

/**
 * Acquires display path and all mandatory resources which belong to it.
 *
 * \param [in] display_index: Index of display path which should be acquired
 *
 * \return
 *     TM_RESULT_SUCCESS: if display path was successfully acquired
 *     TM_RESULT_FAILURE: otherwise
 */
enum tm_result dal_tm_acquire_display_path(struct topology_mgr *tm,
		uint32_t display_index)
{
	struct display_path *display_path;
	struct dal_context *dal_context = tm->dal_context;

	TM_IFACE_TRACE();

	if (!tm_is_display_index_valid(tm, display_index, __func__))
		return TM_RESULT_FAILURE;

	display_path = tm_get_display_path_at_index(tm, display_index);

	if (dal_display_path_is_acquired(display_path) == true) {
		TM_ERROR("%s: path 0x%p (index: %d) already acquired!\n",
			__func__, display_path, display_index);
		return TM_RESULT_FAILURE;
	}

	if (TM_RESULT_SUCCESS != tm_resource_mgr_acquire_resources(
			tm->tm_rm,
			display_path,
			TM_ACQUIRE_METHOD_HW)) {
		TM_ERROR("%s: path 0x%p (index: %d) : error in TMRM!\n",
			__func__, display_path, display_index);
		return TM_RESULT_FAILURE;
	}

	return TM_RESULT_SUCCESS;
}

/**
 * Releases display path and all resources (including optional) which
 * belong to it
 *
 * \param [in] display_index: Index of display path which should be released
 */
void dal_tm_release_display_path(struct topology_mgr *tm,
		uint32_t display_index)
{
	struct display_path *display_path;
	struct dal_context *dal_context = tm->dal_context;

	TM_IFACE_TRACE();

	if (!tm_is_display_index_valid(tm, display_index, __func__))
		return;

	display_path = tm_get_display_path_at_index(tm, display_index);

	if (dal_display_path_is_acquired(display_path) == false) {
		TM_ERROR("%s: path 0x%p (index: %d) NOT acquired!\n",
			__func__, display_path, display_index);
		return;
	}

	/* Release optional objects which should be detached explicitly
	 * from display path. */
	dal_tm_detach_stereo_sync_from_display_path(tm, display_index);

	dal_tm_detach_sync_output_from_display_path(tm, display_index);

	tm_resource_mgr_release_resources(tm->tm_rm, display_path,
		TM_ACQUIRE_METHOD_HW);
}

/**
 * Releases display path enabled by vBIOS.
 *
 * \param [in] display_index: Index of display path which should be released
 */
void dal_tm_release_vbios_enabled_display_path(struct topology_mgr *tm,
		uint32_t display_index)
{
	struct display_path *display_path;
	struct dal_context *dal_context = tm->dal_context;

	TM_IFACE_TRACE();

	if (!tm_is_display_index_valid(tm, display_index, __func__))
		return;

	display_path = tm_get_display_path_at_index(tm, display_index);

	if (dal_display_path_is_acquired(display_path) == false) {
		TM_ERROR("%s: path 0x%p (index: %d) NOT acquired!\n",
			__func__, display_path, display_index);
		return;
	}

	tm_resource_mgr_release_resources(tm->tm_rm, display_path,
			TM_ACQUIRE_METHOD_HW);
}

/**
 * Allocates, duplicates and acquires single path.
 * The caller is responsible to destroy this display path
 * This function is used when HW and display path context need to be
 * accessed in reentrant mode.
 *
 *  @param [in] display_index: Index of display path to duplicate
 *
 *  @return
 *     Pointer to allocated display path if succeeded, NULL otherwise
 */
struct display_path *dal_tm_create_resource_context_for_display_index(
		struct topology_mgr *tm,
		uint32_t display_index)
{
	struct dal_context *dal_context = tm->dal_context;
	struct display_path *src_display_path;
	struct display_path *dst_display_path;
	bool is_dst_path_acquired;

	if (!tm_is_display_index_valid(tm, display_index, __func__))
		return NULL;

	src_display_path = tm_get_display_path_at_index(tm, display_index);

	/* We are cloning CURRENT state - this is why the 'true' flag.
	 * Note that validation code works only with a COPY of the path,
	 * and when it is done, the copy is destroyed.
	 * The idea is NOT to change SW or HW state of the ORIGINAL path.
	 * However, the Resources are NOT copies, that means all resources
	 * which are acquired by this function must be released by calling
	 * dal_tm_destroy_resource_context_for_display_path() */
	dst_display_path = dal_display_path_clone(src_display_path, true);

	if (dst_display_path == NULL) {
		TM_ERROR("%s: failed to clone Path:%d!\n", __func__,
				display_index);
		return NULL;
	}

	is_dst_path_acquired = dal_display_path_is_acquired(dst_display_path);

	/* Re-acquire links and signals on already active path or
	 * acquire resources on inactive path. */
	if (is_dst_path_acquired)
		dal_display_path_acquire_links(dst_display_path);
	else {
		enum tm_result tm_result = tm_resource_mgr_acquire_resources(
				tm->tm_rm, dst_display_path,
				/* Validation only - no need to change
				 * HW state. */
				TM_ACQUIRE_METHOD_SW);

		if (tm_result != TM_RESULT_SUCCESS) {
			dal_display_path_destroy(&dst_display_path);
			dst_display_path = NULL;
		}
	}

	return dst_display_path;
}

void dal_tm_destroy_resource_context_for_display_path(
		struct topology_mgr *tm_mgr,
		struct display_path *display_path)
{
	tm_resource_mgr_release_resources(
			tm_mgr->tm_rm,
			display_path,
			/* Validation only - no need to change
			 * HW state. */
			TM_ACQUIRE_METHOD_SW);

	dal_display_path_destroy(&display_path);
}


/** Acquire stereo-sync object on display path (the display path itself
 *  should be already acquired) */
enum tm_result dal_tm_attach_stereo_synch_to_display_path(
		struct topology_mgr *tm,
		uint32_t display_index)
{
	struct dal_context *dal_context = tm->dal_context;

	TM_NOT_IMPLEMENTED();
	return TM_RESULT_FAILURE;
}

/**
 * Detaches stereo-sync object from display path (the display path itself
 * expected to be already acquired).
 * If stereo-sync object is not attached to display path, then this
 * function does nothing.
 * If stereo-sync object not used anymore, we will power it down.
 *
 * \param [in] display_index: Index of display path from which stereo-sync
 *	object should be detached
 */
void dal_tm_detach_stereo_sync_from_display_path(struct topology_mgr *tm,
		uint32_t display_index)
{
	struct display_path *display_path;
	struct encoder *stereo_sync;
	bool recache_needed = false;
	struct tm_resource *stereo_resource;
	struct dal_context *dal_context = tm->dal_context;

	TM_IFACE_TRACE();

	if (!tm_is_display_index_valid(tm, display_index, __func__))
		return;

	display_path = tm_get_display_path_at_index(tm, display_index);

	stereo_sync = dal_display_path_get_stereo_sync_object(display_path);

	if (stereo_sync == NULL)
		return;

	/* Find encoder RESOURCE which matches stereosync object in the
	 * display path. */
	stereo_resource = tm_resource_mgr_find_resource(tm->tm_rm,
		dal_encoder_get_graphics_object_id(stereo_sync));

	if (stereo_resource != NULL) {

		tm_resource_mgr_ref_counter_decrement(tm->tm_rm,
				stereo_resource);

		/* Optimisation - if refCount > 0 then stereosync
		 * encoder points to encoder on the acquired
		 * display path.
		 * In this case no cofunctional paths changed. */
		recache_needed =
				(stereo_resource->flags.display_path_resource
				&&
				!TM_RES_REF_CNT_GET(stereo_resource));

		/* Once reference count falls to 0 - we need to
		 * power down the object. */
		if (TM_RES_REF_CNT_GET(stereo_resource) == 0)
			dal_encoder_disable_sync_output(stereo_sync);
	}

	/* Remove stereosync object from display path (need to be done before
	 * we recache cofunctional paths, but after we disable
	 * stereo in HWSS) */
	dal_display_path_set_stereo_sync_object(display_path, NULL);

	/* Recalculate cofunctional sets the next time it is required
	 * (need to be done after we remove stereo object from the path). */
	if (recache_needed)
		tm->valid_cofunc_sets = false;
}

/** Returns stereo ranking (higher value = higher priority) for given
 * display path */
uint32_t dal_tm_get_display_path_stereo_priority(
		struct topology_mgr *tm,
		uint32_t display_index,
		bool display_stereo_active)
{
	struct dal_context *dal_context = tm->dal_context;

	TM_NOT_IMPLEMENTED();
	return 0;
}

/** Acquire sync-output resources on display path (the display path itself
 * should be already acquired) */
enum tm_result dal_tm_attach_sync_output_to_display_path(
		struct topology_mgr *tm,
		uint32_t display_index,
		enum sync_source sync_output)
{
	struct dal_context *dal_context = tm->dal_context;

	TM_NOT_IMPLEMENTED();
	return TM_RESULT_FAILURE;
}

/**
 * Detaches sync-output resources from display path (the display path itself
 * expected to be already acquired).
 * If sync-output resources is not attached to display path, then this
 * function does nothing.
 * If sync-output resource not used anymore, we will power it down.
 *
 * \param [in] display_index: Index of display path from which sync-output
 *			resource should be detached.
 */
void dal_tm_detach_sync_output_from_display_path(
		struct topology_mgr *tm,
		uint32_t display_index)
{
	struct display_path *display_path;
	struct encoder *sync_output_object;
	enum sync_source sync_output;
	bool turn_off_sync_output = false;
	bool recache_needed = false;
	struct tm_resource *sync_output_rsrc;
	struct dal_context *dal_context = tm->dal_context;

	TM_IFACE_TRACE();

	if (!tm_is_display_index_valid(tm, display_index, __func__))
		return;

	display_path = tm_get_display_path_at_index(tm, display_index);

	sync_output_object = dal_display_path_get_sync_output_object(
			display_path);

	sync_output = dal_display_path_get_sync_output_source(display_path);

	if (sync_output >= SYNC_SOURCE_GSL_IO_FIRST &&
			sync_output <= SYNC_SOURCE_GSL_IO_LAST)
		turn_off_sync_output = true;

	if (sync_output_object != NULL) {
		/* Find encoder RESOURCE which matches stereosync object in the
		 * display path. */
		sync_output_rsrc = tm_resource_mgr_find_resource(tm->tm_rm,
			dal_encoder_get_graphics_object_id(sync_output_object));

		if (sync_output_rsrc != NULL) {

			tm_resource_mgr_ref_counter_decrement(tm->tm_rm,
					sync_output_rsrc);

			/* Optimisation - if refCount > 0 then
			 * syncoutput encoder points to encoder on the
			 * acquired display path.
			 * In this case no cofunctional paths
			 * changed. */
			recache_needed =
					(sync_output_rsrc->
						flags.display_path_resource &&
						TM_RES_REF_CNT_GET(
							sync_output_rsrc) == 0);


			/* Once reference count falls to 0 - we need to power
			 * down the object. */
			if (TM_RES_REF_CNT_GET(sync_output_rsrc) == 0)
				turn_off_sync_output = true;
		}
	}

	/* Turn off sync-output resources */
	if (turn_off_sync_output)
		dal_hw_sequencer_disable_sync_output(tm->hwss_srvr,
				display_path);

	/* Remove sync-output object from display path (need to be done
	 * before we re-cache co-functional paths, but after we disable
	 * sync-output in HWSS). */
	dal_display_path_set_sync_output_object(display_path,
			SYNC_SOURCE_NONE, NULL);

	/* Recalculate cofunctional sets the next time it is required
	 * (need to be done after we remove sync-output object from the path)*/
	if (recache_needed)
		tm->valid_cofunc_sets = false;
}

/** Moves sync-output resources from one display path to other */
enum tm_result dal_tm_move_sync_output_object(struct topology_mgr *tm,
		uint32_t src_display_index,
		uint32_t tgt_display_index)
{
	struct dal_context *dal_context = tm->dal_context;

	TM_NOT_IMPLEMENTED();
	return TM_RESULT_FAILURE;
}

/**  Convert Display Path Interface to display index */
uint32_t dal_tm_display_path_to_display_index(
		struct topology_mgr *tm,
		struct display_path *display_path)
{
	uint32_t ind;
	uint32_t display_paths_num = tm_get_display_path_count(tm);
	struct dal_context *dal_context = tm->dal_context;

	TM_IFACE_TRACE();

	for (ind = 0; ind < display_paths_num; ind++) {
		if (display_path == tm_get_display_path_at_index(tm, ind)) {
			/* found it */
			return ind;
		}
	}

	return INVALID_DISPLAY_INDEX;
}

/**  Convert display index to DisplayPathInterface*/
struct display_path *dal_tm_display_index_to_display_path(
		struct topology_mgr *tm,
		uint32_t display_index)
{
	return tm_get_display_path_at_index(tm, display_index);
}

uint32_t dal_tm_get_wireless_display_index(
	struct topology_mgr *tm)
{
	uint32_t i;
	struct display_path *disp_path = NULL;
	enum signal_type signal;
	uint num_of_display_path = dal_tm_get_num_display_paths(tm, false);

	for (i = 0; i < num_of_display_path; i++) {
		disp_path = dal_tm_display_index_to_display_path(tm, i);
		signal = dal_display_path_get_query_signal(
					disp_path, SINK_LINK_INDEX);
		if (signal == SIGNAL_TYPE_WIRELESS)
			return i;
	}

	return INVALID_DISPLAY_INDEX;
}


/************************************
Display combinations handlers/queries
*************************************/
/** Gets the number of available display paths */
uint32_t dal_tm_get_num_display_paths(struct topology_mgr *tm,
		bool display_targets_only)
{
	struct dal_context *dal_context = tm->dal_context;

	/* TODO: add code for 'targets only' */

	TM_IFACE_TRACE();

	return tm_get_display_path_count(tm);
}

/**  Query the max number of display paths (excluding CF paths) that can be
 *  enabled simultaneously */
uint32_t dal_tm_max_num_cofunctional_targets(struct topology_mgr *tm)
{
	struct dal_context *dal_context = tm->dal_context;

	/* TODO: implement multidisplay */

	TM_IFACE_TRACE();

	return 1; /* currently only one display is supported */
}

/** Queries the number of connected displays that support audio */
uint32_t dal_tm_get_num_connected_audio_displays(struct topology_mgr *tm)
{
	struct dal_context *dal_context = tm->dal_context;

	TM_NOT_IMPLEMENTED();
	return 0;
}

static bool is_display_index_array_valid(
		struct topology_mgr *tm,
		const uint32_t display_index_array[],
		uint32_t array_size)
{
	struct dal_context *dal_context = tm->dal_context;
	uint32_t i;

	if (array_size > tm_get_display_path_count(tm)) {
		TM_ERROR("%s: array_size=%d greater than maximum=%d!\n",
				__func__, array_size,
				tm_get_display_path_count(tm));
		return false;
	}

	for (i = 0; i < array_size; i++) {
		if (!tm_get_display_path_at_index(tm, display_index_array[i])) {
			TM_ERROR(
			"%s: display_index_array contains invalid index=%d!\n",
					__func__, display_index_array[i]);
			return false;
		}
	}

	/* array is valid */
	return true;
}

/** Request if the specified array of DisplayPaths can be enabled
 * simultaneously */
bool dal_tm_can_display_paths_be_enabled_at_the_same_time(
		struct topology_mgr *tm,
		const uint32_t display_index_array[],
		uint32_t array_size)
{
	bool ret = true;
	enum cache_query_result query_result;
	struct tm_resource_mgr *resource_mgr;
	struct dal_context *dal_context = tm->dal_context;

	if (!is_display_index_array_valid(tm, display_index_array, array_size))
		return false;

	/*if cache of co-functional sets are invalid, recalculate.*/
	if (!tm->valid_cofunc_sets) {
		dal_invalidate_subsets_cache(tm->display_subsets, true);
		tm->valid_cofunc_sets = true;
	}

	query_result = dal_is_subset_supported(
			tm->display_subsets,
			display_index_array,
			array_size);

	if (query_result == CQR_SUPPORTED)
		ret = true;
	else if (query_result == CQR_NOT_SUPPORTED)
		ret = false;
	else {
		/*Allocate temporary resources*/
		resource_mgr = tm_resource_mgr_clone(tm->tm_rm);
		if (resource_mgr == NULL) {

			TM_ERROR("%s: Failed to clone resources", __func__);
			/* KK: no way to inform the caller that
			 * there was an internal error, false
			 * is meaningless here!
			 */
			return false;
		}

		ret = tm_can_display_paths_be_enabled_at_the_same_time(
				tm,
				resource_mgr,
				display_index_array,
				array_size);

		if (query_result != CQR_DP_MAPPING_NOT_VALID)
			dal_set_subset_supported(
				tm->display_subsets,
				display_index_array,
				array_size, ret);


		tm_resource_mgr_destroy(&resource_mgr);
	}

	return ret;
}

/** Return an array of display indexes sorted according to display
 * selection priority */
const uint32_t *dal_tm_get_display_selection_priority_array(
		struct topology_mgr *tm)
{
	struct dal_context *dal_context = tm->dal_context;

	TM_NOT_IMPLEMENTED();
	return NULL;
}

/**
 * Allocates and duplicates a subset of display paths.
 * Duplicated display paths are acquired (so all resources within display path
 * will be available).
 * The caller is responsible to deallocate the subset.
 * This function used when HW and display path context need to be
 * accessed in reentrant mode.
 *
 * \param [in] pDisplayIndexes: indexes of displays in requested subset
 * \param [in] arraySize:       size of the above array
 *
 * \return Pointer to subset of display paths if succeeded, NULL otherwise
 */
struct display_path_set *dal_tm_create_resource_context_for_display_indices(
	struct topology_mgr *tm,
	const uint32_t display_index_array[],
	uint32_t array_size)
{
	struct display_path_set *display_path_set;
	struct tm_resource_mgr *resource_mgr;
	struct display_path *display_path;
	uint32_t num_of_display_path;
	uint32_t i;
	struct display_path_set_init_data dps_init_data;
	struct dal_context *dal_context = tm->dal_context;

	if (!is_display_index_array_valid(tm, display_index_array, array_size))
		return NULL;

	resource_mgr = tm_resource_mgr_clone(tm->tm_rm);

	if (resource_mgr == NULL) {
		TM_ERROR("%s: Failed to clone resources", __func__);
		return NULL;
	}

	/* Validate requested displays are confunctional*/
	if (tm->valid_cofunc_sets) {
		if (!dal_tm_can_display_paths_be_enabled_at_the_same_time(
				tm,
				display_index_array,
				array_size))
			goto release_rm;
	} else {
		if (!tm_can_display_paths_be_enabled_at_the_same_time(
				tm,
				resource_mgr,
				display_index_array,
				array_size))
			goto release_rm;
		tm_resource_mgr_reset_all_usage_counters(resource_mgr);
	}

	dps_init_data.dal_context = tm->dal_context;
	dps_init_data.display_path_num = array_size;
	display_path_set = dal_display_path_set_create(&dps_init_data);

	if (!display_path_set)
		goto release_rm;

	/* Copy display paths*/
	num_of_display_path = tm_get_display_path_count(tm);
	for (i = 0; i < array_size; i++) {

		display_path = tm_get_display_path_at_index(
			tm,
			display_index_array[i]);

		if (display_index_array[i] >= num_of_display_path) {
			TM_ERROR("%s: Invalid display index", __func__);
			goto release_dps;
		}

		if (!dal_display_path_set_add_path(
			display_path_set, display_path)) {
			TM_ERROR("%s: AddDisplayPath failed", __func__);
			goto release_dps;
		}
	}

	/* Acquire resources on display paths. Once
	 * acquired (or failed to acquired) we do
	 * not need these resources anymore - it means we can delete
	 * the temporary TM Resource Manager.
	 */
	for (i = 0; i < array_size; i++) {
		if (!tm_resource_mgr_acquire_resources(
			resource_mgr,
			dal_display_path_set_path_at_index(
				display_path_set, i),
				/* Validation of views etc. No need to
				 * change HW state. */
				TM_ACQUIRE_METHOD_SW)) {

			TM_ERROR("%s: Failed to acquire resources", __func__);
			goto release_dps;
		}
	}

	/* Release temporary resources*/
	tm_resource_mgr_destroy(&resource_mgr);

	return display_path_set;
release_dps:
	dal_display_path_set_destroy(&display_path_set);
release_rm:
	/* Release temporary resources*/
	tm_resource_mgr_destroy(&resource_mgr);
	return NULL;
}

void dal_tm_display_path_set_destroy(
		struct topology_mgr *tm,
		struct display_path_set **display_path_set)
{
	struct dal_context *dal_context = tm->dal_context;

	/* TODO: call display_path_set_destroy() */
	TM_NOT_IMPLEMENTED();
}


/** return a bit vector of controllers mapped to given array of display
 * path indexes */
enum tm_result dal_tm_get_controller_mapping(struct topology_mgr *tm,
		const uint32_t display_index_array[],
		uint32_t array_size,
		bool use_current_mapping,
		struct display_controller_pair *pairs)
{
	struct dal_context *dal_context = tm->dal_context;

	if (!is_display_index_array_valid(tm, display_index_array, array_size))
		return TM_RESULT_FAILURE;

	TM_NOT_IMPLEMENTED();
	return TM_RESULT_FAILURE;
}


/*******************
Display Path lookup
********************/
/** Finds a display path given encoder, connector and signal type */
struct display_path *dal_tm_find_display_path(
		struct topology_mgr *tm,
		struct graphics_object_id encoder_id,
		struct graphics_object_id connector_id,
		enum signal_type sig_type)
{
	struct dal_context *dal_context = tm->dal_context;

	TM_NOT_IMPLEMENTED();
	return NULL;
}

/** Find display path according to device type */
struct display_path *dal_tm_find_display_path_with_device_type(
		struct topology_mgr *tm,
		enum dal_device_type dev_type)
{
	struct dal_context *dal_context = tm->dal_context;

	TM_NOT_IMPLEMENTED();
	return NULL;
}

/** Returns an active display index given a controller handle */
uint32_t dal_tm_find_display_path_with_controller(
		struct topology_mgr *tm,
		uint32_t controller_handle)
{
	struct dal_context *dal_context = tm->dal_context;

	TM_NOT_IMPLEMENTED();
	return 0;
}

/* get number of audios which allowed over a dongle */
static uint32_t get_dvi_audio_number(struct dal_context *dal_context,
		uint32_t max_num_of_supported_hdmi,
		uint32_t hdmi_connectors_num,
		uint32_t dvi_connectors_num)
{
	uint32_t dvi_audio_num;

	if (max_num_of_supported_hdmi - hdmi_connectors_num  >
							dvi_connectors_num)
		dvi_audio_num = dvi_connectors_num;
	else
		dvi_audio_num = max_num_of_supported_hdmi - hdmi_connectors_num;

	TM_RESOURCES("dvi_audio_num:%d\n", dvi_audio_num);

	return dvi_audio_num;
}

/**
 * co_func_audio_endpoint =  MINIMUM(	NumberOfPhysicalAudioEnds,
 *					NumberofDisplayPipe,
 *					NumberOfAudioCapableConnectors).
 *
 * Here NumberOfAudioCapableConnectors = number of HDMI connectors +
 *  NumberofDPConnectors*4  (in MST mode each DP can drive up to 4 streams) +
 *  (if (Wireless capable) ? 1 : 0).
 */
static uint32_t get_number_of_audio_capable_display_paths(
		struct topology_mgr *tm)
{
	uint32_t audio_capable_path_num = 0;
	uint32_t hdmi_connectors_num = 0;
	uint32_t dvi_connectors_num = 0;
	uint32_t paths_per_mst_connector;
	uint32_t i;
	struct tm_resource *tm_resource;
	union audio_support audio_support;
	struct dal_context *dal_context = tm->dal_context;
	const struct tm_resource_range *connectors;

	paths_per_mst_connector =
		dal_adapter_service_get_num_of_path_per_dp_mst_connector(
				tm->adapter_srv);

	if (paths_per_mst_connector == 0) {
		/* has to me at least one path - for SST mode */
		paths_per_mst_connector = 1;
	}

	connectors =
		dal_tmrm_get_resource_range_by_type(
			tm->tm_rm,
			OBJECT_TYPE_CONNECTOR);

	for (i = connectors->start; i < connectors->end; i++) {
		struct graphics_object_id object_id;

		tm_resource = tm_resource_mgr_enum_resource(tm->tm_rm, i);

		object_id = GRPH_ID(tm_resource);

		switch (object_id.id) {
		case CONNECTOR_ID_HDMI_TYPE_A:
			audio_capable_path_num++;
			hdmi_connectors_num++;
			break;
		case CONNECTOR_ID_DISPLAY_PORT:
			/* consider DP connector as DP MST connector */
			audio_capable_path_num += paths_per_mst_connector;
			break;
		case CONNECTOR_ID_DUAL_LINK_DVII:
		case CONNECTOR_ID_DUAL_LINK_DVID:
		case CONNECTOR_ID_SINGLE_LINK_DVID:
		case CONNECTOR_ID_SINGLE_LINK_DVII:
			dvi_connectors_num++;
			break;
		default:
			/* irrelevant from audio point of view */
			break;
		}
	} /* for () */

	audio_support = dal_adapter_service_get_audio_support(
			tm->adapter_srv);

	/* If discrete ASIC, allow DVI-HDMI dongle.
	 * Note: APU does not support DVI-HDMI dongle. */
	if (dal_adapter_service_is_fusion(tm->adapter_srv)) {
		/* Check feature flag for wireless, here can not use ASIC CAP.
		 * currently wireless only enabled for APU. */

		if (true == dal_adapter_service_is_feature_supported(
				FEATURE_WIRELESS_ENABLE)) {
			/* value exist and is set to true */
			audio_capable_path_num++;
		}
	} else if (audio_support.bits.HDMI_AUDIO_ON_DONGLE == 1) {

		uint32_t max_num_of_supported_hdmi = 1;
		uint32_t dvi_audio_num;/* this is "audio over dongle" number */

		if (true == dal_adapter_service_get_feature_value(
				FEATURE_SUPPORTED_HDMI_CONNECTION_NUM,
				&max_num_of_supported_hdmi,
				sizeof(max_num_of_supported_hdmi))) {

			if (max_num_of_supported_hdmi > hdmi_connectors_num) {
				/* We support DVI->HDMI dongle if strapping is
				 * set.
				 * Strapping allows DP/HDMI audio over dongle
				 * is the same bit.
				 */
				dvi_audio_num = get_dvi_audio_number(
						tm->dal_context,
						max_num_of_supported_hdmi,
						hdmi_connectors_num,
						dvi_connectors_num);
				audio_capable_path_num += dvi_audio_num;
			}
		}
	}

	TM_RESOURCES("audio_capable_path_num:%d\n", audio_capable_path_num);

	return audio_capable_path_num;
}

static void tm_update_audio_connectivity(struct topology_mgr *tm)
{
	uint32_t audio_capable_path_num =
			get_number_of_audio_capable_display_paths(tm);

	dal_adapter_service_update_audio_connectivity(tm->adapter_srv,
		audio_capable_path_num);
}

/**
 * For each Controller:
 *  - disable power gating and save power gating state,
 *  - power-up Controller,
 *  - enable clock gating.
 */
static void tm_reset_controllers(struct topology_mgr *tm)
{
	uint32_t i;
	uint32_t controller_index = 0;
	struct tm_resource *tm_resource;
	struct tm_resource_controller_info *controller_info;
	struct controller *controller;
	struct dal_asic_runtime_flags asic_runtime_flags;
	const struct tm_resource_range *controllers;

	asic_runtime_flags = dal_adapter_service_get_asic_runtime_flags(
		tm->adapter_srv);

	controllers =
		dal_tmrm_get_resource_range_by_type(
			tm->tm_rm,
			OBJECT_TYPE_CONTROLLER);

	for (i = controllers->start; i < controllers->end; i++) {

		tm_resource = tm_resource_mgr_enum_resource(tm->tm_rm, i);

		controller_info = TO_CONTROLLER_INFO(tm_resource);

		controller = controller_info->controller;

		if (controller_index == 0 &&
			asic_runtime_flags.bits.GNB_WAKEUP_SUPPORTED == 1)
			dal_controller_power_gating_enable(controller,
					PIPE_GATING_CONTROL_INIT);

		dal_controller_power_gating_enable(controller,
				PIPE_GATING_CONTROL_DISABLE);

		controller_info->power_gating_state = TM_POWER_GATE_STATE_OFF;

		dal_controller_power_up(controller);

		dal_controller_enable_display_pipe_clock_gating(controller,
				true);

		++controller_index;
	}
}

/**
 * Initialises encoder context structure with connector information and
 * other related information.
 * The 'context' is for encoder, which is an 'upstream' object
 * relative to 'link_index'.
 *
 * \param [in] display_path: Display path to which encoder belongs
 * \param [in] link_idx: index of link of encoder which context to initialise
 * \param [out] encoder_context: Encoder context to initialise
 */
static void tm_build_encoder_context(
		struct dal_context *dal_context,
		struct display_path *display_path,
		uint32_t link_idx,
		struct encoder_context *encoder_context)
{
	struct connector_feature_support cfs;
	struct encoder *dwn_strm_encoder;
	struct connector *connector;
	struct graphics_object_id connector_obj_id;

	TM_ASSERT(display_path != NULL);
	TM_ASSERT(encoder_context != NULL);

	connector = dal_display_path_get_connector(display_path);

	dal_connector_get_features(connector, &cfs);

	connector_obj_id = dal_connector_get_graphics_object_id(connector);

	encoder_context->connector = connector_obj_id;

	encoder_context->hpd_source = cfs.hpd_line;
	encoder_context->channel = cfs.ddc_line;
	encoder_context->signal = dal_display_path_get_query_signal(
			display_path, link_idx);

	encoder_context->engine =  dal_display_path_get_stream_engine(
					display_path, link_idx);

	dwn_strm_encoder = dal_display_path_get_downstream_object(display_path,
			link_idx);

	if (dwn_strm_encoder != NULL) {
		/* get the id of downstream encoder */
		encoder_context->downstream =
			dal_encoder_get_graphics_object_id(dwn_strm_encoder);
	} else {
		/* downstream object is connector */
		encoder_context->downstream = connector_obj_id;
	}

	/* If this encoder doesn't have engine set (because not active),
	 * we try its native engine. */
	if (encoder_context->engine == ENGINE_ID_UNKNOWN) {
		struct encoder *this_encoder =
			dal_display_path_get_upstream_object(display_path,
				link_idx);

		encoder_context->engine =
			dal_encoder_get_preferred_stream_engine(this_encoder);
	}
}

/**
 * The function will return the priority of display path from encoder context
 * perspective.
 *
 * \param [in] display_path: Display path which contains  requested encoder
 *
 * \return Encoder context priority
 */
static enum tm_encoder_ctx_priority tm_get_encoder_ctx_priority(
		struct display_path *display_path)
{
	enum tm_encoder_ctx_priority priority = TM_ENCODER_CTX_PRIORITY_DEFAULT;
	bool acquried = dal_display_path_is_acquired(display_path);
	bool connected = dal_display_path_is_target_connected(display_path);

	if (acquried) {
		if (connected)
			priority = TM_ENCODER_CTX_PRIORITY_ACQUIRED_CONNECTED;
		else
			priority = TM_ENCODER_CTX_PRIORITY_ACQUIRED;
	} else {
		if (connected)
			priority = TM_ENCODER_CTX_PRIORITY_CONNECTED;
	}

	return priority;
}

/**
 * Power up an encoder.
 * If the encoder is on AN active display path, update (or setup) encoder
 * implementation for the display path.
 * The implementation will be updated only for "highest priority" context.
 * In the end we need to setup current implementation
 *
 * \param [in] tm_resource: resource of encoder which is to be powered up
 */
static void tm_power_up_encoder(struct topology_mgr *tm,
		struct encoder *enc_input)
{
	struct encoder *enc_upstrm;
	struct display_path *active_display_path = NULL;
	struct display_path *display_path;
	uint32_t active_link_idx = 0;
	enum tm_encoder_ctx_priority best_priority =
			TM_ENCODER_CTX_PRIORITY_INVALID;
	struct graphics_object_id enc_input_obj_id =
		dal_encoder_get_graphics_object_id(enc_input);
	struct graphics_object_id enc_upstrm_obj_id;
	uint32_t i;
	uint32_t link_idx;
	uint32_t num_of_links;
	uint32_t display_paths_num = tm_get_display_path_count(tm);
	struct encoder_context context;
	enum tm_encoder_ctx_priority priority;
	uint32_t display_path_index;
	struct dal_context *dal_context = tm->dal_context;

	/* Find all path in which "enc_input" is active. */
	for (i = 0; i < display_paths_num; i++) {

		display_path = tm_get_display_path_at_index(tm, i);
		num_of_links = dal_display_path_get_number_of_links(
				display_path);
		display_path_index = dal_display_path_get_display_index(
				display_path);

		/* Check if "enc_input" is in on link of a display path. */
		for (link_idx = 0; link_idx < num_of_links; link_idx++) {

			enc_upstrm = dal_display_path_get_upstream_object(
					display_path, link_idx);

			enc_upstrm_obj_id =
				dal_encoder_get_graphics_object_id(enc_upstrm);

			if (false == dal_graphics_object_id_is_equal(
					enc_upstrm_obj_id,
					enc_input_obj_id)) {
				/* go to next link */
				continue;
			}

			/* Found Link Index for "enc_input". */
			dal_memset(&context, 0, sizeof(context));
			context.engine = ENGINE_ID_UNKNOWN;

			priority = tm_get_encoder_ctx_priority(display_path);

			if (priority > best_priority) {
				best_priority = priority;
				active_display_path = display_path;
				active_link_idx = link_idx;
			}

			tm_build_encoder_context(tm->dal_context,
					display_path, link_idx, &context);

			if (ENCODER_RESULT_OK != dal_encoder_power_up(
						enc_input, &context)) {
				/* TODO: should we return error to caller? */
				TM_ERROR("%s: failed encoder power up!\n",
						__func__);
			}

			TM_ENCODER_CTL("%s:[PowerUp]: %s, Path=%u,"\
				" Link=%u, Engine=%s, Signal=%s",
				__func__,
				tm_utils_transmitter_id_to_str(
					dal_encoder_get_graphics_object_id(
							enc_input)),
				display_path_index,
				link_idx,
				tm_utils_engine_id_to_str(context.engine),
				tm_utils_signal_type_to_str(context.signal));

		} /* for() */
	} /* for() */

	/* Update encoder implementation on ACTIVE display path */
	if (NULL != active_display_path &&
		best_priority > TM_ENCODER_CTX_PRIORITY_DEFAULT) {

		dal_memset(&context, 0, sizeof(context));
		context.engine = ENGINE_ID_UNKNOWN;

		tm_build_encoder_context(tm->dal_context, active_display_path,
				active_link_idx, &context);

		if (ENCODER_RESULT_OK != dal_encoder_update_implementation(
				enc_input, &context)) {
			/* TODO: should we return error to caller? */
			TM_ERROR("%s: failed to update encoder implementation!\n",
					__func__);
		}

		TM_ENCODER_CTL("%s:[UpdateImpl]: %s, on Active Path=%u, Link=%u, Engine=%s, Signal=%s",
			__func__,
			tm_utils_transmitter_id_to_str(
				dal_encoder_get_graphics_object_id(
						enc_input)),
			dal_display_path_get_display_index(active_display_path),
			active_link_idx,
			tm_utils_engine_id_to_str(context.engine),
			tm_utils_signal_type_to_str(context.signal));
	}
}

static void tm_power_up_encoders(struct topology_mgr *tm)
{
	uint32_t i;
	struct tm_resource *tm_resource;
	struct encoder_context context;
	struct encoder *encoder;
	struct dal_context *dal_context = tm->dal_context;
	const struct tm_resource_range *encoders =
		dal_tmrm_get_resource_range_by_type(
			tm->tm_rm,
			OBJECT_TYPE_ENCODER);

	/* Power Up encoders.
	 * Order is important - from first (internal) to last (external).
	 * The order is enforced by resource list being sorted according to
	 * priorities (in tm_rm_add_tm_resource()). */
	for (i = encoders->start; i < encoders->end; i++) {
		tm_resource = tm_resource_mgr_enum_resource(tm->tm_rm, i);

		if (!tm_resource->flags.resource_active)
			continue;

		encoder = TO_ENCODER(tm_resource);

		if (tm_resource->flags.display_path_resource) {
			/* power up AND update implementation */
			tm_power_up_encoder(tm, encoder);
		} else {
			/* only power up */
			dal_memset(&context, 0, sizeof(context));
			context.engine = ENGINE_ID_UNKNOWN;

			if (ENCODER_RESULT_OK != dal_encoder_power_up(
					encoder, &context)) {
				/* TODO: should we return error to caller? */
				TM_ERROR("%s: failed encoder power up!\n",
						__func__);
			}
		}
	} /* for () */
}

/**
 * This is called for all Encoders, for Shutdown and Standby.
 * We need to do powerDown once per PHY, but we need to find best encoder
 * context for this.
 * In case we could not fetch engine from the context, we will try to
 * power down all encoder with all supported engines.
 *
 * \param [in] tm_resource: resource of encoder which is to be powered down
 */
static void tm_power_down_encoder(struct topology_mgr *tm,
		struct encoder *enc_input,
		bool turn_off_vcc)
{
	struct encoder *enc_upstrm;
	struct display_path *active_display_path = NULL;
	struct display_path *display_path;
	uint32_t active_link_idx = 0;
	enum tm_encoder_ctx_priority best_priority =
			TM_ENCODER_CTX_PRIORITY_INVALID;
	struct graphics_object_id enc_input_obj_id =
		dal_encoder_get_graphics_object_id(enc_input);
	struct graphics_object_id enc_upstrm_obj_id;
	uint32_t i;
	uint32_t link_idx;
	uint32_t num_of_links;
	uint32_t display_paths_num = tm_get_display_path_count(tm);
	enum tm_encoder_ctx_priority priority;
	bool powered_down = false;
	struct encoder_output enc_output;
	union supported_stream_engines engines;
	struct dcs *dcs;
	const struct monitor_patch_info *mon_patch_info;
	struct dal_context *dal_context = tm->dal_context;

	/* Find all path in which "enc_input" is active. */
	for (i = 0; i < display_paths_num; i++) {

		display_path = tm_get_display_path_at_index(tm, i);

		num_of_links = dal_display_path_get_number_of_links(
				display_path);

		/* Check if "enc_input" is on a link of a display path. */
		for (link_idx = 0; link_idx < num_of_links; link_idx++) {

			enc_upstrm = dal_display_path_get_upstream_object(
					display_path, link_idx);

			enc_upstrm_obj_id =
				dal_encoder_get_graphics_object_id(enc_upstrm);

			if (false == dal_graphics_object_id_is_equal(
					enc_upstrm_obj_id,
					enc_input_obj_id)) {
				/* go to next link */
				continue;
			}

			/* Found Link Index for "enc_input". */
			priority = tm_get_encoder_ctx_priority(display_path);

			if (priority > best_priority) {
				best_priority = priority;
				active_display_path = display_path;
				active_link_idx = link_idx;
			}

			if (priority == TM_ENCODER_CTX_PRIORITY_HIGHEST) {
				/* No need to continue search because found
				 * the highest priority. */
				break;
			}

		} /* for() */
	} /* for() */

	if (NULL == active_display_path) {
		/* If not on an active path, no need to power down. */
		return;
	}

	dal_memset(&enc_output, 0, sizeof(enc_output));
	engines.u_all = 0;

	/* Build context for power down */
	tm_build_encoder_context(tm->dal_context, active_display_path,
			active_link_idx, &enc_output.ctx);

	dcs = dal_display_path_get_dcs(active_display_path);

	mon_patch_info = dal_dcs_get_monitor_patch_info(dcs,
			MONITOR_PATCH_TYPE_KEEP_DP_RECEIVER_POWERED);

	if (NULL != mon_patch_info)
		enc_output.flags.bits.KEEP_RECEIVER_POWERED =
				mon_patch_info->param;
	else
		enc_output.flags.bits.KEEP_RECEIVER_POWERED = false;

	mon_patch_info = dal_dcs_get_monitor_patch_info(dcs,
			MONITOR_PATCH_TYPE_VID_STREAM_DIFFER_TO_SYNC);

	if (NULL != mon_patch_info)
		enc_output.flags.bits.VID_STREAM_DIFFER_TO_SYNC =
				mon_patch_info->param;
	else
		enc_output.flags.bits.VID_STREAM_DIFFER_TO_SYNC = false;

	enc_output.flags.bits.TURN_OFF_VCC = turn_off_vcc;
	enc_output.flags.bits.NO_WAIT_FOR_HPD_LOW = false;

	/*************************/
	/* get supported engines */
	engines = dal_encoder_get_supported_stream_engines(enc_input);

	/* In case we could not fetch engine from context, but we know this
	 * encoder supports at least one engine. */
	if (enc_output.ctx.engine == ENGINE_ID_UNKNOWN && engines.u_all > 0) {

		enum engine_id first_valid_engine = ENGINE_ID_UNKNOWN;

		/* Try to power down encoder with all supported engines
		 * which were not yet powered down. */
		for (i = 0; i < ENGINE_ID_COUNT; i++) {

			if (!tm_utils_is_supported_engine(engines, i)) {
				/* not a supported engine */
				continue;
			}

			if (first_valid_engine == ENGINE_ID_UNKNOWN)
				first_valid_engine = i;

			enc_output.ctx.engine = i;

			if (ENCODER_RESULT_OK != dal_encoder_power_down(
					enc_input, &enc_output))
				TM_ERROR("%s: encoder power down failed (1)!\n",
						__func__);

			TM_ENCODER_CTL(
				"TM Encoder PowerDown [Supported Engine]: %s, Active Path=%u, Link=%u, Engine=%s, Signal=%s",
				tm_utils_transmitter_id_to_str(
						enc_input_obj_id),
				dal_display_path_get_display_index(
						active_display_path),
				active_link_idx,
				tm_utils_engine_id_to_str(
						enc_output.ctx.engine),
				tm_utils_signal_type_to_str(
						enc_output.ctx.signal));

			powered_down = true;
		} /* for () */

		/* If we did NOT powered down encoder at all, means all
		 * supported engines already powered down and we need to care
		 * about transmitter only.
		 * Means we can use any engine (first one is good enough). */
		if (!powered_down && first_valid_engine != ENGINE_ID_UNKNOWN) {

			enc_output.ctx.engine = first_valid_engine;

			if (ENCODER_RESULT_OK != dal_encoder_power_down(
					enc_input, &enc_output))
				TM_ERROR("%s: encoder power down failed (2)!\n",
						__func__);

			TM_ENCODER_CTL(
				"TM Encoder PowerDown [1st Valid Engine]: %s, Active Path=%u, Link=%u, Engine=%s, Signal=%s",
				tm_utils_transmitter_id_to_str(
						enc_input_obj_id),
				dal_display_path_get_display_index(
						active_display_path),
				active_link_idx,
				tm_utils_engine_id_to_str(
						enc_output.ctx.engine),
				tm_utils_signal_type_to_str(
						enc_output.ctx.signal));

			powered_down = true;
		}
	} /* if() */

	/* Either engine was initially valid (it had a real context), or
	 * no engine required/supported by this encoder. */
	if (!powered_down) {

		if (ENCODER_RESULT_OK != dal_encoder_power_down(
				enc_input, &enc_output))
			TM_ERROR("%s: encoder power down failed (3)!\n",
					__func__);

		TM_ENCODER_CTL(
			"TM Encoder PowerDown [Input Engine]: %s, Active Path=%u, Link=%u, Engine=%s, Signal=%s",
			tm_utils_transmitter_id_to_str(
					enc_input_obj_id),
			dal_display_path_get_display_index(
					active_display_path),
			active_link_idx,
			tm_utils_engine_id_to_str(
					enc_output.ctx.engine),
			tm_utils_signal_type_to_str(
					enc_output.ctx.signal));
	}
}

static void tm_power_down_encoders(struct topology_mgr *tm)
{
	uint32_t i;
	struct tm_resource *tm_resource;
	bool turn_off_vcc = true;

	const struct tm_resource_range *encoders =
		dal_tmrm_get_resource_range_by_type(
			tm->tm_rm,
			OBJECT_TYPE_ENCODER);

	/* Power Down encoders.
	 * Order is important - from last (external) to first (internal).
	 * The order is enforced by resource list being sorted according to
	 * priorities (in tm_rm_add_tm_resource()). */
	i = encoders->end;

	do {
		i--;

		tm_resource = tm_resource_mgr_enum_resource(tm->tm_rm, i);

		if (!tm_resource->flags.resource_active)
			continue;

		/*TODO: do_not_turn_off_vcc will be updated properly after
		* implementing the optimization code
		*/
		tm_power_down_encoder(
			tm,
			TO_ENCODER(tm_resource),
			turn_off_vcc);

	} while (i != encoders->start);
}

static void tm_power_down_controller(struct topology_mgr *tm,
		struct tm_resource *tm_resource)
{
	struct dal_context *dal_context = tm->dal_context;
	struct tm_resource_controller_info *info =
		TO_CONTROLLER_INFO(tm_resource);

	if (info->power_gating_state != TM_POWER_GATE_STATE_ON) {
		/* No power gating means power is on and it is OK to
		 * access the controller. */
		dal_controller_power_down(info->controller);
	} else {
		/* Resource is power gated and we could not
		 * access it to PowerDown(). */
		TM_PWR_GATING("Could not PowerDown Controller Id:%d because it is power gated.",
			dal_controller_get_graphics_object_id(
				info->controller));
	}
}

/* We should not power down all controllers because we could not do
 * this with power gated tiles. */
static void tm_power_down_controllers(struct topology_mgr *tm)
{
	uint32_t i;
	struct tm_resource *tm_resource;

	const struct tm_resource_range *controllers =
		dal_tmrm_get_resource_range_by_type(
			tm->tm_rm,
			OBJECT_TYPE_CONTROLLER);

	for (i = controllers->start; i < controllers->end; i++) {

		tm_resource = tm_resource_mgr_enum_resource(tm->tm_rm, i);

		tm_power_down_controller(tm, tm_resource);

	} /* for () */
}

static void tm_power_down_clock_sources(struct topology_mgr *tm)
{
	uint32_t i;
	struct tm_resource *tm_resource;
	enum controller_id first_controller_id = CONTROLLER_ID_UNDEFINED;
	const struct tm_resource_range *resources =
		dal_tmrm_get_resource_range_by_type(
			tm->tm_rm,
			OBJECT_TYPE_CONTROLLER);

	tm_resource =
		tm_resource_mgr_enum_resource(tm->tm_rm, resources->start);
	first_controller_id = GRPH_ID(tm_resource).id;

	resources =
		dal_tmrm_get_resource_range_by_type(
			tm->tm_rm,
			OBJECT_TYPE_CLOCK_SOURCE);

	for (i = resources->start; i < resources->end; i++) {

		tm_resource = tm_resource_mgr_enum_resource(tm->tm_rm, i);

		dal_clock_source_power_down_pll(
			TO_CLOCK_SOURCE(tm_resource),
			first_controller_id);
	}
}

static void tm_power_down_update_all_display_path_logical_power_state(
		struct topology_mgr *tm)
{
	uint32_t i;
	uint32_t display_paths_num = tm_get_display_path_count(tm);
	struct display_path *display_path;

	for (i = 0; i < display_paths_num; i++) {

		display_path = tm_get_display_path_at_index(tm, i);

		/* When we power down the all the HW blocks, we must update
		 * the states to unknown.
		 * This ensures the correct HW programming sequence is
		 * performed upon resuming from FS DOS, resume from sleep, or
		 * resume from hibernate. */
		dal_display_path_set_target_powered_on(display_path,
				DISPLAY_TRI_STATE_UNKNOWN);

		dal_display_path_set_target_blanked(display_path,
				DISPLAY_TRI_STATE_UNKNOWN);

		dal_display_path_set_source_blanked(display_path,
				DISPLAY_TRI_STATE_UNKNOWN);
	}
}

/*
 * tm_can_optimize_resume_sequence
 *
 * @brief Check if TM can optimize S3/S4 resume sequence
 *
 * @param
 * feature: for which purpose we want to optimize resume sequence
 *
 * @return
 * true if sequence can be optimized, false otherwise
 */
static bool tm_can_optimize_resume_sequence(
		struct topology_mgr *tm,
		enum optimization_feature feature)
{

	if (tm->previous_power_state != DAL_VIDEO_POWER_SUSPEND)
		return false;

	if (!dal_adapter_service_should_optimize(tm->adapter_srv, feature))
		return false;

	return true;

}

static void power_up_audio_objects(struct topology_mgr *tm)
{
	uint32_t i;
	struct tm_resource *tm_resource;
	struct audio *audio;
	struct dal_context *dal_context = tm->dal_context;
	const struct tm_resource_range *audios =
		dal_tmrm_get_resource_range_by_type(
			tm->tm_rm,
			OBJECT_TYPE_AUDIO);

	for (i = audios->start; i < audios->end; i++) {

		tm_resource = tm_resource_mgr_enum_resource(tm->tm_rm, i);

		if (!tm_resource->flags.resource_active)
			continue;

		audio = TO_AUDIO(tm_resource);

		if (AUDIO_RESULT_OK != dal_audio_power_up(audio)) {
			/* TODO: should we return error to caller? */
			TM_ERROR("%s: failed audio power up!\n", __func__);
		}
	} /* for () */
}

/********************
Programming sequences
*********************/
/** Initialise all HW blocks at boot/resume/tdr, needed for detection
 * prior set mode. */
enum tm_result dal_tm_init_hw(struct topology_mgr *tm)
{
	struct bios_parser *bp;
	struct dal_context *dal_context = tm->dal_context;

	TM_IFACE_TRACE();

	/* 0. PowerUp controllers, reset front pipe*/
	tm_reset_controllers(tm);

	/* 1. PowerUp GPU */
	dal_gpu_power_up(tm_resource_mgr_get_gpu_interface(tm->tm_rm));

	/* 2. Reinitialise VBIOS wrapper. */
	bp = dal_adapter_service_get_bios_parser(tm->adapter_srv);
	dal_bios_parser_power_up(bp);

	/* 3. Updates audio connectivity based on connector,
	 * number of pipes and wireless etc. */
	tm_update_audio_connectivity(tm);

	/* 4. TODO PowerUp DMCU - This should be done before Encoder */

	/* 5. PowerUp controllers :move to step0 to make sure init front pipe*/

	/* 6. PowerUp encoders. */
	tm_power_up_encoders(tm);

	/* 7. PowerUp connectors */
	/* TODO: implement when/if connector powerup() interface is ready. */

	/* 8. PowerUp audios */
	power_up_audio_objects(tm);

	/* 9. Initialise detection-related HW */
	dal_tm_detection_mgr_init_hw(tm->tm_dm);

	/* 10. notify all LS that HW has been init'ed so LS can act accordingly
	 * if required. */
	tm_resource_mgr_invalidate_link_services(tm->tm_rm);

	dal_bios_parser_set_scratch_acc_mode_change(bp);

	/* */
	tm->hw_power_down_required =
			!tm_can_optimize_resume_sequence(
					tm,
					OF_SKIP_RESET_OF_ALL_HW_ON_S3RESUME);

	return TM_RESULT_SUCCESS;
}

/** power down all HW blocks before ACPI non-D0 state */
/**
 *	dal_tm_power_down_hw
 *
 *	Powers down all HW blocks in the following order:
 *		1. GLSyncConnectors
 *		2. All remaining HW blocks except GPU
 *		3. VBIOS
 *		3. GPU
 */

enum tm_result dal_tm_power_down_hw(struct topology_mgr *tm)
{
	uint32_t i;
	struct tm_resource *tm_resource = NULL;
	struct display_path *display_path = NULL;
	struct dal_context *dal_context = tm->dal_context;
	uint32_t display_paths_num = tm_get_display_path_count(tm);
	struct bios_parser *bp = NULL;
	const struct tm_resource_range *controllers =
		dal_tmrm_get_resource_range_by_type(
				tm->tm_rm,
				OBJECT_TYPE_CONTROLLER);

	enum dal_video_power_state power_state =
		dal_tm_get_current_power_state(tm);

	/*1. PowerDown GLSync Connectors*/
	/*TODO: Power Down GLSyncConnector*/
	TM_NOT_IMPLEMENTED();

	/*2. TODO PowerDown DMCU*/

	/* 3.0 If we are going to S4 or BACO,
	 * then we only need to invalidate states
	 */
	if (power_state == DAL_VIDEO_POWER_HIBERNATE ||
		power_state == DAL_VIDEO_POWER_ULPS) {

		for (i = controllers->start; i < controllers->end; i++) {

			tm_resource =
				tm_resource_mgr_enum_resource(
					tm->tm_rm, i);

			TO_CONTROLLER_INFO(tm_resource)->power_gating_state =
				TM_POWER_GATE_STATE_ON;
		}

		/* Update display logical power state*/
		for (i = 0; i < display_paths_num; i++) {
			/* When we power down the all the HW blocks,
			 * we must update the states to unknown.
			 * This ensures the correct HW programming
			 * sequence is performed upon resuming from
			 * FS DOS, resume from sleep, or resume
			 * from hibernate.
			 */
			display_path = tm_get_display_path_at_index(tm, i);
			dal_display_path_set_target_powered_on(
				display_path, DISPLAY_TRI_STATE_UNKNOWN);
			dal_display_path_set_target_blanked(
				display_path, DISPLAY_TRI_STATE_UNKNOWN);
			dal_display_path_set_source_blanked(
				display_path, DISPLAY_TRI_STATE_UNKNOWN);
		}

		tm->hw_power_down_required = false;

	} else { /* 3.1 Otherwise we need to do full powerdown.*/

		/* 3.1.1 PowerDown all displays paths*/
		/* use driver code instead of using command table.*/
		dal_tm_power_down_path_elements(tm);

		/* 3.1.2 Power gating enable for all controllers
		 * We could move this into GPU object
		 */
		for (i = controllers->start; i < controllers->end; i++) {

			struct controller *controller = NULL;

			tm_resource =
				tm_resource_mgr_enum_resource(
					tm->tm_rm, i);

			controller =
				TO_CONTROLLER_INFO(tm_resource)->controller;
			dal_controller_enable_display_pipe_clock_gating(
				controller, false);

			/* if already power gated we do nothing*/
			if (TO_CONTROLLER_INFO(
				tm_resource)->power_gating_state !=
				TM_POWER_GATE_STATE_ON) {
				dal_controller_power_gating_enable(
					controller, PIPE_GATING_CONTROL_ENABLE);
				TO_CONTROLLER_INFO(tm_resource)->
					power_gating_state =
						TM_POWER_GATE_STATE_ON;
			} else
				TM_WARNING("Controller %d already power gated\n",
					dal_controller_get_id(controller));
		}
	}

	bp = dal_adapter_service_get_bios_parser(tm->adapter_srv);
	dal_bios_parser_power_down(bp);

	dal_gpu_power_down(
		tm_resource_mgr_get_gpu_interface(tm->tm_rm),
		power_state);

	return true;

}

/** power down all HW blocks before ACPI non-D0 state */
enum tm_result dal_tm_power_down_hw_active(struct topology_mgr *tm,
		const uint32_t display_index_array[],
		uint32_t array_size)
{
	struct dal_context *dal_context = tm->dal_context;

	if (!is_display_index_array_valid(tm, display_index_array, array_size))
		return TM_RESULT_FAILURE;

	TM_NOT_IMPLEMENTED();
	return TM_RESULT_FAILURE;
}


/**
 * Powers down all HW blocks that compose display paths in the following
 * order (reverse order from PowerUp):
 *	1. Audios
 *	2. Connectors
 *	3. Routers
 *	4. Encoders
 *	5. Controllers
 *	6. Clock Sources
 *
 * \return	TM_RESULT_SUCCESS: no error
 *		TM_RESULT_FAILURE: error
 */
enum tm_result dal_tm_power_down_path_elements(struct topology_mgr *tm)
{
	struct dal_context *dal_context = tm->dal_context;

	TM_IFACE_TRACE();

	/* 1. Power Down audios */
	/* TODO: implement when/if audio power_down() interface is ready. */

	/* 2. Power Down connectors */
	/* TODO: implement when/if connector power_down() interface is ready. */

	/* 3. Power Down encoders. */
	tm_power_down_encoders(tm);

	/* 4. PowerDown controllers */
	tm_power_down_controllers(tm);

	/* 5. PowerDown clock sources */
	tm_power_down_clock_sources(tm);

	tm_power_down_update_all_display_path_logical_power_state(tm);

	/* HW is powered down, update state */
	tm->hw_power_down_required = false;

	return TM_RESULT_SUCCESS;
}

/** reset logical state for controllers */
void dal_tm_reset_vbios_controllers(struct topology_mgr *tm)
/*ResetControllersForFSDOSToWindows()*/
{
	struct dal_context *dal_context = tm->dal_context;

	TM_NOT_IMPLEMENTED();
}

/**
 * "Locks" the path to allow exclusive detection.
 * This lock is not 100% safe, since no OS mutex/lock used
 * However it is guaranteed no deadlock occur
 * It returns the "safe detection method" (i.e. if lock failed, it is safe
 * to do only cached detection).
 *
 * \param [in] display_path: Display path to lock
 * \param [in] method:       Purpose of lock
 *
 * \return	Original (requested) method if lock succeeded.
 *		DetectionMethod_Cached otherwise.
 */
static enum tm_detection_method tm_lock_path(
		struct topology_mgr *tm,
		struct display_path *display_path,
		enum tm_detection_method method)
{
	uint32_t display_index;
	struct dal_context *dal_context = tm->dal_context;

	TM_ASSERT(display_path != NULL);

	if (method == DETECTION_METHOD_CACHED) {
		/* always safe to do cached detection */
		return method;
	}

	display_index = dal_display_path_get_display_index(display_path);

	if (!tm_utils_test_bit(&tm->display_detection_mask, display_index)) {

		tm_utils_set_bit(&tm->display_detection_mask, display_index);
		return method;
	}

	return DETECTION_METHOD_CACHED;
}


/**
 * Unlocks the path which was locked to allow exclusive detection.
 *
 * \param [in] display_path: Display path to unlock
 * \param [in] method:       Purpose of previous lock
 */
static void tm_unlock_path(struct topology_mgr *tm,
		struct display_path *display_path,
		enum tm_detection_method method)
{
	uint32_t display_index;
	struct dal_context *dal_context = tm->dal_context;

	TM_ASSERT(display_path != NULL);

	if (method == DETECTION_METHOD_CACHED)
		return;

	display_index = dal_display_path_get_display_index(display_path);

	tm_utils_clear_bit(&tm->display_detection_mask, display_index);
}

/**
 * Checks if current path is locked for detection
 *
 * \param [in] display_path: Display path to check for lock
 *
 * \return	true if path locked, false otherwise
 */
static bool tm_is_path_locked(struct topology_mgr *tm,
		struct display_path *display_path)
{
	uint32_t display_index;
	struct dal_context *dal_context = tm->dal_context;

	TM_ASSERT(display_path != NULL);

	display_index = dal_display_path_get_display_index(display_path);

	return tm_utils_test_bit(&tm->display_detection_mask, display_index);
}

/*****************************************************************************
 * Audio-related code.
 ****************************************************************************/

/**
 * This function handles arbitration of audio resources when display was
 * just connected.
 *
 * According to "Output Device Management" spec, audio resource is assigned on
 * device arrival (if display requests audio) and released on device removal.
 *
 * Signal can be downgraded only on connected display path.
 *
 * So in this function we do:
 *  1. Start audio device on enabled connected display path. We need to
 *  do it every time we connect enabled display, to allow OS to switch to
 *  new audio device, so sound can to be heard.
 *
 *  2. Call AttachAudioToDisplayPath to set the audio to the display path
 *
 * \param [in]  display_path: Display path which got connected.
 */
static void arbitrate_audio_on_connect(struct topology_mgr *tm,
		struct display_path *display_path)
{
	enum signal_type sink_signal;
	enum signal_type new_signal;
	struct dcs *dcs;
	struct dal_context *dal_context = tm->dal_context;

	sink_signal = dal_display_path_get_query_signal(display_path,
			SINK_LINK_INDEX);
	new_signal = sink_signal;

	if (dal_is_hdmi_signal(sink_signal) &&
			tm->attached_hdmi_num >=
				tm->max_num_of_supported_hdmi) {
		/* we ran out of HDMI audios */
		new_signal = tm_utils_downgrade_to_no_audio_signal(sink_signal);
	}

	dcs = dal_display_path_get_dcs(display_path);
	if (!dcs) {
		TM_ERROR("%s: no DCS on the Path!\n", __func__);
		return;
	}

	if (dal_is_audio_capable_signal(new_signal) &&
			dal_dcs_is_audio_supported(dcs)) {
		enum tm_result tm_result;

		tm_result = tm_resource_mgr_attach_audio_to_display_path(
				tm->tm_rm, display_path, sink_signal);

		if (tm_result != TM_RESULT_SUCCESS) {
			/* could not attach audio resource for some reason */
			new_signal = tm_utils_downgrade_to_no_audio_signal(
					sink_signal);
		}
	}

	if (new_signal != sink_signal) {
		/* signal was downgraded for audio-related reasons */
		handle_signal_downgrade(tm, display_path, new_signal);
	}

	if (dal_is_hdmi_signal(new_signal))
		tm->attached_hdmi_num++;
}

/**
 * This function handles arbitration of audio resources when display was
 * just disconnected.
 *
 * According to "Output Device Management" spec, audio resource is assigned on
 * device arrival (if display requests audio) and released on device removal.
 *
 * Signal can be downgraded only on connected display path.
 *
 * So in this function we do:
 *  1. Stop audio device on enabled disconnected display path. We need to
 *  do it every time we disconnect enabled display, to allow OS to switch to
 *  another audio device, so sound will continue to be heard.
 *
 *  2. Call DetachAudioFromDisplayPath to remove the audio from the display path
 *     NOTE: Even though disconnected display path loses it's audio resources,
 *     it's signal is not changed.
 *
 * \param [in]  display_path: Display path which got disconnected.
 */
static void arbitrate_audio_on_disconnect(struct topology_mgr *tm,
		struct display_path *display_path)
{
	struct dal_context *dal_context = tm->dal_context;
	struct audio *audio;
	enum signal_type current_signal;

	if (false == tm_is_path_locked(tm, display_path)) {
		TM_WARNING("%s: Path is NOT locked!\n",
				__func__);
	}

	audio = dal_display_path_get_audio_object(display_path,
			ASIC_LINK_INDEX);

	if (audio != NULL) {
		/* Stop audio device on acquired display path */
		if (dal_display_path_is_acquired(display_path)) {
			dal_hw_sequencer_reset_audio_device(tm->hwss_srvr,
					display_path);
		}

		tm_resource_mgr_detach_audio_from_display_path(tm->tm_rm,
				display_path);
	}

	current_signal = dal_display_path_get_query_signal(display_path,
			SINK_LINK_INDEX);

	if (dal_is_hdmi_signal(current_signal)) {
		if (tm->attached_hdmi_num > 0)
			tm->attached_hdmi_num--;
		else
			TM_ERROR("%s: can NOT reduce attached_hdmi_num below zero!\n",
					__func__);
	}
}

/**
 * This function handles arbitration of audio resources when no
 * connect/disconnect event occurred, but due to detection logic, signal
 * was changed.
 *
 * According to "Output Device Management" spec, audio resource is assigned on
 * device arrival (if display requests audio) and released on device removal.
 *
 * So in this function no audio resources reassignment happens.
 *
 * The only thing we need to do here is to make sure we do not report audio
 * capability for connected display path that does not have audio resources
 * assigned.
 * We ignore signal changes on disconnected display path, since on disconnect,
 * signal is not really updated.
 * So for connected HDMI display which does not have audio resource,
 * we downgrade signal to DVI.
 * For DP we do nothing, since currently we do not report DP audio capability
 * to upper layers. Also, DP remains DP (DP audio capability does not change
 * signal).
 *
 * \param [in]     pDisplayPath:     Display path on which signal change event
 *				occurred
 * \param [in/out] detect_status: Most recent detection status.
 *			detected_signal field in this structure maybe downgraded
 */

static void arbitrate_audio_on_signal_change(struct topology_mgr *tm,
		struct display_path *display_path,
		struct tm_detection_status *detect_status)
{
	enum signal_type sink_signal;

	if (false == detect_status->connected)
		return;

	if (false == dal_is_hdmi_signal(detect_status->detected_signal))
		return;

	sink_signal = dal_display_path_get_query_signal(display_path,
			SINK_LINK_INDEX);

	if (dal_is_dvi_signal(sink_signal)) {
		/* This is an HDMI display, but it doesn't have
		 * audio capability. */
		detect_status->detected_signal =
				tm_utils_downgrade_to_no_audio_signal(
						detect_status->detected_signal);
	}
}

/**
 * This function handles arbitration of audio resources PER PATH.
 *
 * Most of the work is for "disconnect", where resources are freed.
 *
 * For the "connect" only internal state is updated, so basically
 * it prepares everything for assign_audio_by_signal_priority(), where
 * a decision is made which path gets what audio, and then audio is assigned.
 *
 * This function should be called in safe detection context.
 *
 * According to "Output Device Management" spec, audio resource is assigned on
 * device arrival (if display requests audio) and released on device removal.
 *
 * In case of signal change event (if no connectivity change occurred) we just
 * downgrade signal if audio resources not available for this display path.
 *
 * \param [in]     display_path: Display path on which signal/connection
 *					state changed
 * \param [in/out] detect_status: Most recent detection status.
 *				detected_signal field in this structure
 *				maybe downgraded.
 */
static void update_path_audio(struct topology_mgr *tm,
		struct display_path *display_path,
		struct tm_detection_status *detect_status)
{
	struct dal_context *dal_context = tm->dal_context;
	bool target_connected;
	struct dcs *dcs;
	bool connect_event;
	bool disconnect_event;
	bool signal_change_event;
	bool dongle_changed;
	bool monitor_event;
	bool connectivity_changed;
	enum signal_type sink_signal;

	if (false == tm_is_path_locked(tm, display_path)) {
		TM_WARNING("%s: Path is NOT locked!\n",
				__func__);
	}

	dcs = dal_display_path_get_dcs(display_path);
	if (!dcs) {
		TM_ERROR("%s: no DCS on the Path!\n", __func__);
		return;
	}

	target_connected = dal_display_path_is_target_connected(display_path);

	/* Define signal/connection state change events */
	connect_event = (detect_status->connected && !target_connected);

	disconnect_event = (!detect_status->connected && target_connected);

	sink_signal = dal_display_path_get_query_signal(display_path,
			SINK_LINK_INDEX);

	signal_change_event = (detect_status->detected_signal != sink_signal);

	dongle_changed = (dal_dcs_get_dongle_type(dcs) !=
			detect_status->sink_capabilities.dongle_type);

	monitor_event = (detect_status->monitor_changed ||
			detect_status->audio_cap_changed || dongle_changed);

	connectivity_changed = (detect_status->connected != target_connected);

	/* Handle events */
	if (monitor_event && !connectivity_changed) {
		/* This is the case where we missed disconnected event (due to
		 * sleep for example).
		 * i.e. new monitor connected while path state refers to
		 * the old one. */
		/* handle disconnect on the path */
		arbitrate_audio_on_disconnect(tm, display_path);
	} else if (connect_event) {
		arbitrate_audio_on_connect(tm, display_path);
	} else if (disconnect_event) {
		arbitrate_audio_on_disconnect(tm, display_path);
	} else if (signal_change_event) {
		arbitrate_audio_on_signal_change(tm, display_path,
				detect_status);
	}
}

static void handle_signal_downgrade(struct topology_mgr *tm,
		struct display_path *display_path,
		enum signal_type new_signal)
{
	struct dal_context *dal_context = tm->dal_context;
	/* Signal changed - we need to update connection status */
	struct tm_detection_status detection_status;
	struct dcs *dcs;
	enum tm_detection_method safe_method;

	dal_memset(&detection_status, 0, sizeof(detection_status));

	detection_status.detected_signal = new_signal;
	detection_status.capability_changed = true;
	detection_status.connected = dal_display_path_is_target_connected(
			display_path);

	if (false == detection_status.connected) {
		TM_WARNING("%s: downgrading disconnected path?!\n",
				__func__);
	}

	dcs = dal_display_path_get_dcs(display_path);

	dal_dcs_get_sink_capability(dcs, &detection_status.sink_capabilities);

	safe_method = tm_lock_path(tm, display_path,
			DETECTION_METHOD_DESTRUCTIVE_AND_EMBEDDED);

	tm_update_on_connection_change(tm, display_path, &detection_status);

	tm_unlock_path(tm, display_path, safe_method);
}

static void tm_vbios_set_scratch_connection_state(
		struct topology_mgr *tm,
		struct display_path *display_path,
		bool connected)
{
	struct bios_parser *bp;
	struct connector *connector;
	struct graphics_object_id id;
	struct connector_device_tag_info *device_tag;

	bp = dal_adapter_service_get_bios_parser(tm->adapter_srv);

	connector = dal_display_path_get_connector(display_path);

	id = dal_connector_get_graphics_object_id(connector);

	device_tag = dal_display_path_get_device_tag(display_path);

	dal_bios_parser_set_scratch_connected(bp, id, connected, device_tag);
}

/* Get current DRR from DCS and update cached values in DisplayPath. */
static void tm_display_path_set_drr_from_dcs(struct topology_mgr *tm,
		struct display_path *display_path)
{
	struct drr_config drr_from_dcs;
	struct dcs *dcs;

	dcs = dal_display_path_get_dcs(display_path);

	dal_dcs_get_drr_config(dcs, &drr_from_dcs);

	dal_display_path_set_drr_config(display_path, &drr_from_dcs);
}

/* Set initial Static Screen detection values in DisplayPath
 * once a display is connected. */
static void tm_initialize_static_screen_events(struct topology_mgr *tm,
	struct display_path *display_path)
{
	/* Initialize static screen events on display connect time. */
	struct static_screen_events events;

	/* Initialize to set no events. */
	events.u_all = 0;

	/* Try to find runtime parameter forced events. */
	dal_adapter_service_get_feature_value(
			FEATURE_FORCE_STATIC_SCREEN_EVENT_TRIGGERS,
			&events.u_all,
			sizeof(events.u_all));

	/* If runtime parameter is not set or set to 0, we should use Driver
	 * defaults, which is defined by the logic below. */
	if (events.u_all == 0) {
		/* Set initial Static Screen trigger events. */
		events.bits.CURSOR_MOVE = 1;
		events.bits.GFX_UPDATE = 1;
		events.bits.OVL_UPDATE = 1;

		/*
		 * On Linux the OS might write directly to the primary
		 * surface. Enable memory trigger events.
		 */
		if (dal_adapter_service_is_feature_supported(
				FEATURE_ALLOW_DIRECT_MEMORY_ACCESS_TRIG)) {

			/* By default, enable all hit regions. Later when
			 * region address range is defined, there should be
			 * a call to set_static_screen_triggers to update
			 * to the updated setting. */
			events.bits.MEM_REGION0_WRITE = 1;
			events.bits.MEM_REGION1_WRITE = 1;
			events.bits.MEM_REGION2_WRITE = 1;
			events.bits.MEM_REGION3_WRITE = 1;
		}
	}

	/* Store the initialized triggers events in the display path.
	 * These settings are usually stored once on display connection.
	 * They may be updated later through a test application, or internal
	 * update of memory hit regions after regions are programmed. */
	dal_display_path_set_static_screen_triggers(display_path, &events);
}

static void tm_update_on_connect_link_services_and_encoder_implementation(
		struct topology_mgr *tm,
		struct display_path *display_path)
{
	uint32_t link_idx;
	struct link_service *link_service;
	struct encoder_context context;
	enum signal_type signal_type;
	struct encoder *encoder;
	struct graphics_object_id id;
	uint32_t display_index;
	struct dal_context *dal_context = tm->dal_context;

	for (link_idx = 0;
		link_idx < dal_display_path_get_number_of_links(display_path);
		link_idx++) {

		/* Set link service according to signal type */
		signal_type = dal_display_path_get_query_signal(display_path,
				link_idx);

		link_service = tm_resource_mgr_get_link_service(tm->tm_rm,
				display_path, link_idx, signal_type);

		dal_display_path_set_link_query_interface(display_path,
				link_idx, link_service);

		/* update encoder implementation according to signal type */
		dal_memset(&context, 0, sizeof(context));

		context.engine = ENGINE_ID_UNKNOWN;

		tm_build_encoder_context(tm->dal_context, display_path,
				link_idx, &context);

		encoder = dal_display_path_get_upstream_object(display_path,
				link_idx);

		if (ENCODER_RESULT_OK != dal_encoder_update_implementation(
				encoder, &context)) {
			/* should never happen */
			TM_ERROR("%s:dal_encoder_update_implementation() failed!\n",
					__func__);
		}

		id = dal_encoder_get_graphics_object_id(encoder);
		display_index = dal_display_path_get_display_index(
				display_path);

		TM_ENCODER_CTL("OnConnect[UpdateImpl]: Transmitter=%s, Path=%u, LinkIdx=%u, Engine=%s, Signal=%s\n",
				tm_utils_transmitter_id_to_str(id),
				display_index,
				link_idx,
				tm_utils_engine_id_to_str(context.engine),
				tm_utils_signal_type_to_str(context.signal));
	} /* for() */
}

/*
 * Delegate to handle update DAL subcomponents when Display Connection changed.
 * This function does the same thing as
 * display_capability_changed_at_display_index()
 * but in ADDITION, it resets the preferred colour depth to 10 bpc.
 *
 * display_index: the display path index that changed
 */
static void display_connection_changed_at_display_index(
	struct topology_mgr *tm,
	uint32_t display_index)
{
	/* update Timing Service with modes from Display Capability Service */
	struct display_path *display_path;
	struct dcs *dcs;
	struct mode_manager *mm = tm->mm;
	struct bestview_options opts =
		dal_mode_manager_get_bestview_options(
			mm,
			display_index);

	struct bestview_options default_opts =
		dal_mode_manager_get_default_bestview_options(
			mm,
			display_index);

	display_path = dal_tm_display_index_to_display_path(tm, display_index);

	dcs = dal_display_path_get_dcs(display_path);

	dal_dcs_update_ts_timing_list_on_display(dcs, display_index);

	opts.prefered_color_depth = default_opts.prefered_color_depth;

	if (dal_adapter_service_is_feature_supported(
		FEATURE_ENABLE_GPU_SCALING)) {
		opts.base_timing_select = TIMING_SELECT_NATIVE_ONLY;
		opts.ENABLE_SCALING = true;
		opts.MAINTAIN_ASPECT_RATIO = true;
	}

	dal_mode_manager_set_bestview_options(
		mm,
		display_index,
		&opts,
		true,
		dal_timing_service_get_mode_timing_list_for_path(
			tm->timing_srv,
			display_index));
}

/* A possible use-case which will run this function is "Change monitor during
 * suspend".
 *
 * It means there were no 'disconnect' event but the monitor was changed,
 * so the capability was changed, without change in connectivity. */
static void display_capability_changed_at_display_index(
	struct topology_mgr *tm,
	uint32_t display_idx)
{
	struct display_path *display_path =
		dal_tm_display_index_to_display_path(tm, display_idx);
	struct dcs *dcs = dal_display_path_get_dcs(display_path);

	dal_dcs_update_ts_timing_list_on_display(
		dcs,
		display_idx);

	dal_mode_manager_update_disp_path_func_view_tbl(
		tm->mm,
		display_idx,
		dal_timing_service_get_mode_timing_list_for_path(
			tm->timing_srv,
			display_idx));
}

/**
 * Updates connectivity state of display path and rebuilds display timing list
 * This function should be called in safe detection context
 *
 * \param [in] display_path:     Display path which connectivity changed
 * \param [in] connected:        New connectivity state of display
 * \param [in] update_timing_list: Whether display timing list should be rebuilt
 */
static void tm_update_connection_state_and_timing(
		struct topology_mgr *tm,
		struct display_path *display_path,
		bool connected,
		bool update_timing_list)
{
	uint32_t display_index;
	struct dal_context *dal_context = tm->dal_context;

	TM_ASSERT(tm_is_path_locked(tm, display_path));

	dal_display_path_set_target_connected(display_path, connected);

	display_index = dal_display_path_get_display_index(display_path);

	/* TODO: Update display path mappings in the cache. */

	/* Update IRQ registrations for this path */
	dal_tm_detection_mgr_update_active_state(tm->tm_dm, display_path);

	/* update timing list */
	if (update_timing_list) {
		/* For this special case (capability change), we want
		 * to reset the preferred colour depth in BestviewOptions. */
		display_connection_changed_at_display_index(tm, display_index);
	}
}

static void tm_update_spread_spectrum_capability_for_display_path(
		struct topology_mgr *tm,
		struct display_path *display_path,
		struct tm_detection_status *detection_status)
{
	enum signal_type signal;
	bool is_dp_force_ss;

	signal = dal_display_path_get_query_signal(display_path,
			ASIC_LINK_INDEX);

	is_dp_force_ss = dal_adapter_service_is_feature_supported(
			FEATURE_DP_DISPLAY_FORCE_SS_ENABLE);

	if (dal_is_dp_signal(signal) && (true == is_dp_force_ss)) {

		/* Feature is set -> set SS enabled in DisplayPath */
		dal_display_path_set_ss_support(display_path, true);

	} else {
		/* Feature is not set -> set SS support in DisplayPath
		 * based on sink capabilities. */
		dal_display_path_set_ss_support(display_path,
			detection_status->sink_capabilities.ss_supported);
	}
}

/**
 * Updates display and TM state when connectivity/capability change
 * This function should be called in safe detection context
 *
 * \param [in] display_path:	Display path on which connectivity/capability
 *				changed
 * \param [in] detection_status: New sink state
 */
static void tm_update_on_connection_change(struct topology_mgr *tm,
		struct display_path *display_path,
		struct tm_detection_status *detection_status)
{
	bool update_timing_list = false;
	uint32_t link_idx;
	struct dal_context *dal_context = tm->dal_context;

	TM_ASSERT(tm_is_path_locked(tm, display_path));

	if (detection_status->connected) {
		/*************************************************
		 * Update sink properties for CONNECTED display.
		 *************************************************/

		dal_display_path_set_sink_signal(display_path,
				detection_status->detected_signal);

		tm_display_path_set_drr_from_dcs(tm, display_path);

		tm_initialize_static_screen_events(tm, display_path);

		tm_update_on_connect_link_services_and_encoder_implementation(
				tm, display_path);
	} else {
		/*************************************************
		 * Handle DISCONNECT.
		 *************************************************/
		struct goc_link_service_data ls_data;

		dal_memset(&ls_data, 0, sizeof(ls_data));

		for (link_idx = 0;
			link_idx < dal_display_path_get_number_of_links(
								display_path);
			link_idx++) {
			/* clear ls data */
			dal_display_path_set_link_service_data(display_path,
					link_idx, &ls_data);
		}

		/* Clear DisplayPath DRR on disconnect */
		dal_display_path_set_drr_config(display_path, NULL);

		/* Clear Static Screen trigger events on disconnect */
		dal_display_path_set_static_screen_triggers(display_path, NULL);
	}

	tm_update_spread_spectrum_capability_for_display_path(tm,
			display_path, detection_status);

	/* Update connection status and timing list - need to be done before
	 * clock arbitration timing list should be updated (for any display
	 * type), only if display connected OR if it's embedded. */
	update_timing_list = (detection_status->connected
			|| dal_is_embedded_signal(
					detection_status->detected_signal));

	tm_update_connection_state_and_timing(tm, display_path,
			detection_status->connected, update_timing_list);

	/* TODO: update clock sharing category */

	if (dal_display_path_is_target_connected(display_path)) {
		/* Recalculate cofunctional sets the next time
		 * it is required. */
		tm->valid_cofunc_sets = false;
	}

	/* Configuration changed - we need to re-prioritise Stream Engines.
	 * Device connection/disconnection causes stream engine priority
	 * to be changed. */
	tm_update_stream_engine_priorities(tm);
}

/**
 * Reset the transmitter on displays which have corruption on power on.
 * This is a workaround for specific displays only (i.e. EDID patch).
 *
 * \param [in] display_path: Display path on which to reset transmitter
 *
 * \return	true: if transmitter has to be reset (and was reset)
 *			on this path,
 *		false: otherwise
 */
static bool reset_transmitter_on_display_power_on(
		struct topology_mgr *tm,
		struct display_path *display_path)
{
	bool apply_patch = false;
	union dcs_monitor_patch_flags patch_flags;
	struct dcs *dcs;

	dcs = dal_display_path_get_dcs(display_path);

	patch_flags = dal_dcs_get_monitor_patch_flags(dcs);

	if (patch_flags.flags.RESET_TX_ON_DISPLAY_POWER_ON) {
		/* TODO: Force set mode to reset transmitter? */
		apply_patch = true;
	}

	return apply_patch;
}

/**
 * Updates internal state and notifies external components
 * This function should be called in safe detection context
 *
 * \param [in] display_path:     Display path on which to perform detection
 * \param [in] method:           Detection method
 * \param [in] detection_status: Detection status
 */
static void tm_post_target_detection(struct topology_mgr *tm,
		struct display_path *display_path,
		enum tm_detection_method method,
		struct tm_detection_status *detect_status)
{
	bool sink_signal_changed = false;
	bool connectivity_changed = false;
	bool fake_hpd = false;
	uint32_t display_index;
	struct dal_context *dal_context = tm->dal_context;

	TM_ASSERT(tm_is_path_locked(tm, display_path));

	display_index = dal_display_path_get_display_index(display_path);

	/* Reassign audio resources when signal/connection state changes
	 * Inside detect_status.detected_signal may be updated. */
	update_path_audio(tm, display_path, detect_status);

	/* Update BIOS scratch registers */
	tm_vbios_set_scratch_connection_state(tm, display_path,
			detect_status->connected);

	TM_DISPLAY_DETECT("%s:[%u]: New detected_signal: %s\n", __func__,
			display_index, tm_utils_signal_type_to_str(
					detect_status->detected_signal));

	TM_DISPLAY_DETECT("%s:[%u]: Old signal at SINK_LINK_INDEX: %s\n",
			__func__, display_index, tm_utils_signal_type_to_str(
				dal_display_path_get_query_signal(display_path,
						SINK_LINK_INDEX)));

	/* Define connectivity/capability change. */
	if (detect_status->detected_signal !=
			dal_display_path_get_query_signal(display_path,
					SINK_LINK_INDEX)) {
		/* signal changed */
		sink_signal_changed = true;
	}

	TM_DISPLAY_DETECT("%s:[%u]: sink_signal_changed: %s\n",
			__func__, display_index,
			(sink_signal_changed == true ? "true" : "false"));

	if (detect_status->connected !=
			dal_display_path_is_target_connected(display_path)) {
		/* transition from one state to another occurred */
		connectivity_changed = true;
	}

	TM_DISPLAY_DETECT("%s:[%u]: connectivity_changed: %s\n",
			__func__, display_index,
			(connectivity_changed == true ? "true" : "false"));

	if (false == connectivity_changed &&
			false == detect_status->capability_changed) {
		/* 1. If connectivity and capability UNchanged, then it is
		 *	a fake HPD.
		 * 2. Some OSs provide "manual" detection UI, so we can get
		 *	here if user clicked on a "Detect" button in UI.
		 */
		fake_hpd = true;
	}

	TM_DISPLAY_DETECT("%s:[%u]: fake_hpd: %s\n", __func__, display_index,
			(fake_hpd == true ? "true" : "false"));

	/* Update connectivity state internally, including timing list
	 * update based on connectivity change, capability change, or
	 * embedded signal. */
	if (dal_is_embedded_signal(detect_status->detected_signal)) {
		/* note that embedded is always connected */
		tm_update_on_connection_change(tm, display_path, detect_status);

	} else if (connectivity_changed || detect_status->capability_changed) {

		tm_update_on_connection_change(tm, display_path, detect_status);

		/* Workaround for monitors which need longer HPD disconnect
		 * delay. */
		dal_tm_detection_mgr_program_hpd_filter(tm->tm_dm,
				display_path);

	} else if (sink_signal_changed && detect_status->connected) {
		/* Signal should not change on connected display path if
		 * connectivity/capability did not. */
		TM_WARNING("%s: Signal changed on connected Path: %d!\n",
					__func__, display_index);
	}

	/* Report connectivity changes */
	if (tm->report_detection_changes) {
		if (connectivity_changed) {
			dal_notify_hotplug(
				tm->dal_context,
				display_index,
				detect_status->connected);
		} else if (detect_status->capability_changed &&
			detect_status->connected) {
			dal_notify_capability_change(
				tm->dal_context,
				display_index);
		}
	}

	/* TODO: Handle MST detection reporting */

	/* Apply workaround for displays that show corruption when
	 * its power is toggled. */
	if (fake_hpd && method == DETECTION_METHOD_HOTPLUG)
		reset_transmitter_on_display_power_on(tm, display_path);

	if (method == DETECTION_METHOD_HOTPLUG &&
		detect_status->hpd_pin_failure &&
		detect_status->connected) {
		/* Reschedule detection if HDP line is low and display
		 * considered connected (inconsistent result). */
		dal_tm_detection_mgr_reschedule_detection(tm->tm_dm,
				display_path, true);

	} else if (tm_utils_is_destructive_method(method)) {
		/* Reset pending detection if this was a destructive method. */
		dal_tm_detection_mgr_reschedule_detection(tm->tm_dm,
				display_path, false);
	}
}

/**
 * Handles MST sink connectivity/capability update
 *
 * \param [in] display_path: Display Path where connection state was changed
 * \param [in] method:           Detection/Update method
 * \param [in] detection_status: Output structure which contains all
 *				new detected info
 *
 * \return	True: if actual update was performed,
 *		False: if detection was skipped for any reason
 *		(not necessarily failure).
 */
static bool tm_process_mst_sink_update(struct topology_mgr *tm,
		struct display_path *display_path,
		enum tm_detection_method method,
		struct tm_detection_status *detection_status)
{
	struct dal_context *dal_context = tm->dal_context;

	TM_NOT_IMPLEMENTED();
	return false;
}


/**
 * Performs display detection, updates internal state and notifies
 * external components.
 *
 * \param [in] display_path: Display path on which to perform detection
 * \param [in] method:       Detection method
 *
 * return	true: if display is connected,
 *		false: otherwise.
 */
static bool tm_detect_display(struct topology_mgr *tm,
		struct display_path *display_path,
		enum tm_detection_method method)
{
	struct dal_context *dal_context = tm->dal_context;
	struct tm_detection_status detection_status;
	enum tm_detection_method safe_method;
	bool detection_performed;
	uint32_t display_index;

	TM_ASSERT(display_path != NULL);

	dal_memset(&detection_status, 0, sizeof(detection_status));

	detection_status.detected_signal = SIGNAL_TYPE_NONE;

	safe_method = tm_lock_path(tm, display_path, method);

	/* Perform detection */
	detection_performed = dal_tm_detection_mgr_detect_display(tm->tm_dm,
			display_path, safe_method, &detection_status);

	display_index = dal_display_path_get_display_index(display_path);

	if (safe_method != method) {
		TM_WARNING("Re-entry during detection for display index:%d!\n",
				display_index);
	}

	if (detection_performed) {
		if (detection_status.connected
				!= dal_display_path_is_target_connected(
						display_path)
			|| tm_utils_is_destructive_method(safe_method)) {

			TM_DISPLAY_DETECT("%s:[%u]: %s\n", __func__,
					display_index,
				(detection_status.connected ? "Connected" :
						"Not connected"));
		}
	} else {
		TM_WARNING("%s:[%u]: No detection done!\n", __func__,
				display_index);
	}

	if (!detection_status.dp_mst_detection) {
		/* Proceed with post detect update for non-MST paths */
		union display_path_properties path_props;

		path_props = dal_display_path_get_properties(display_path);

		if (detection_performed ||
			(path_props.bits.ALWAYS_CONNECTED &&
				safe_method  != DETECTION_METHOD_CACHED)) {

			TM_DISPLAY_DETECT("%s:[%u]: non-MST post-detect...\n",
					__func__, display_index);

			tm_post_target_detection(tm, display_path,
					safe_method, &detection_status);
		}
	} else {
		TM_DISPLAY_DETECT("%s:[%u]: MST post-detect...\n",
				__func__, display_index);

		/* Proceed with post detect update for MST paths
		 * (For non-blocking case, update will be issued
		 * asynchronously by MstMgr) */
		if (dal_tm_detection_mgr_is_blocking_detection(tm->tm_dm)) {

			dal_memset(&detection_status, 0,
					sizeof(detection_status));

			tm_process_mst_sink_update(tm, display_path,
					safe_method, &detection_status);
		}
	}

	tm_unlock_path(tm, display_path, safe_method);

	return detection_status.connected;
}


/** does detection on all display paths and assigns audio resources
 * based on priority */
void dal_tm_do_initial_detection(struct topology_mgr *tm)
{
	uint32_t ind;
	uint32_t display_paths_num = tm_get_display_path_count(tm);
	struct display_path *display_path;
	struct dal_context *dal_context = tm->dal_context;

	TM_IFACE_TRACE();

	dal_tm_detection_mgr_set_blocking_detection(tm->tm_dm, true);

	for (ind = 0; ind < display_paths_num; ind++) {
		display_path = tm_get_display_path_at_index(tm, ind);

		/* TODO: Create EDID emulator??? (TM does not care if it fails)
		 * DCS doesn't have interface to do it. */

		/* Assume not connected.
		 * If display not connected then we basically force creation
		 * of default display timings list. */
		dal_display_path_set_target_connected(display_path, false);

		if (false == tm_detect_display(tm, display_path,
				DETECTION_METHOD_DESTRUCTIVE_AND_EMBEDDED)) {

			/* Assign default timing list to non-connected
			 * displays: */
			display_capability_changed_at_display_index(tm, ind);
		}
	} /* for() */

	/* After initial detection, we can start reporting detection
	 * changes (to base driver). */
	tm->report_detection_changes = true;

	/* After initial detection, we always do asynchronous (non-blocking)
	 * MST link data fetching. */
	dal_tm_detection_mgr_set_blocking_detection(tm->tm_dm, false);
}

/** does detection on specific connector */
void dal_tm_do_detection_for_connector(struct topology_mgr *tm,
		uint32_t connector_index)
{
	struct dal_context *dal_context = tm->dal_context;

	TM_NOT_IMPLEMENTED();
}

/** does detection on all display paths in certain order to make sure
 * resources allocated properly */
uint32_t dal_tm_do_complete_detection(
	struct topology_mgr *tm,
	enum tm_detection_method method,
	bool emulate_connectivity_change)
{
	uint32_t connected_num = 0;
	uint32_t detected_displays = 0;
	uint32_t i;

	uint32_t disp_path_number = tm_get_display_path_count(tm);

	if (method == DETECTION_METHOD_CACHED ||
		tm->display_detection_mask != 0) {
		ASSERT(method == DETECTION_METHOD_CACHED);

		for (i = 0; i < disp_path_number; i++) {
			if (dal_display_path_is_target_connected(
				*display_paths_vector_at_index(
					tm->display_paths,
					i)))
				connected_num++;
		}

		return connected_num;
	}

	/* First round - only previously connected displays
	 * TODO: Here should be number of (display paths - number of cf display
	 * paths)
	 */
	for (i = 0; i < disp_path_number; i++) {
		struct display_path *display_path =
			tm_get_display_path_at_index(tm, i);
		if (!tm_utils_test_bit(&detected_displays, i) &&
			dal_display_path_is_target_connected(display_path)) {
			if (tm_detect_display(tm, display_path, method))
				connected_num++;
			tm_utils_set_bit(&detected_displays, i);
		}
	}

	/* Second round - all the rest
	 * TODO: please see round 1 for comment */
	for (i = 0; i < disp_path_number; i++) {
		struct display_path *display_path =
			*display_paths_vector_at_index(tm->display_paths, i);
		if (!tm_utils_test_bit(&detected_displays, i)) {
			if (tm_detect_display(tm, display_path, method))
				connected_num++;
		}
	}

	return connected_num;
}

/** Does detection in separate thread in order not to delay the
 * calling thread. Used during S3->S0 transition. */
enum tm_result dal_tm_do_asynchronous_detection(struct topology_mgr *tm)
{
	struct dal_context *dal_context = tm->dal_context;

	TM_NOT_IMPLEMENTED();
	return TM_RESULT_FAILURE;
}

/** enables or disables the Base Light Sleep */
void dal_tm_toggle_hw_base_light_sleep(struct topology_mgr *tm,
		bool enable)
{
	struct dal_context *dal_context = tm->dal_context;

	TM_NOT_IMPLEMENTED();
}

/**
 * When ASIC goes from VBIOS/VGA mode to driver/accelerated mode we need:
 *  1. Power down all DC HW blocks
 *  2. Disable VGA engine on all controllers
 *  3. Enable power gating for controller
 *  4. Set acc_mode_change bit (VBIOS will clear this bit when going to FSDOS)
 */
void dal_tm_enable_accelerated_mode(struct topology_mgr *tm)
{
	uint32_t i;
	uint32_t resource_num;
	struct tm_resource *tm_resource;
	struct controller *controller;
	struct bios_parser *bp;
	struct dal_context *dal_context = tm->dal_context;
	const struct tm_resource_range *controllers =
		dal_tmrm_get_resource_range_by_type(
			tm->tm_rm,
			OBJECT_TYPE_CONTROLLER);

	TM_IFACE_TRACE();

	/* 1. Power Down all blocks. */
	if (tm->hw_power_down_required || !tm_can_optimize_resume_sequence(
			tm, OF_SKIP_RESET_OF_ALL_HW_ON_S3RESUME))
		dal_tm_power_down_path_elements(tm);

	/* 2. Disable VGA engine on all controllers. */
	resource_num = tm_resource_mgr_get_total_resources_num(tm->tm_rm);

	for (i = controllers->start; i < controllers->end; i++) {

		tm_resource = tm_resource_mgr_enum_resource(tm->tm_rm, i);

		controller = TO_CONTROLLER_INFO(tm_resource)->controller;

		/* we should call DisableVGA for each pipe */
		dal_controller_disable_vga(controller);

		/* enable clock gating for each pipe before
		 * powergating occurs */
		dal_controller_enable_display_pipe_clock_gating(controller,
				true);

		/* Only power gate controllers which not acquired by a
		 * display path.
		 * Controllers acquired by a display path should be released
		 * by ResetMode sequence or reprogrammed. */
		if (TM_RES_REF_CNT_GET(tm_resource) == 0) {
			dal_controller_power_gating_enable(controller,
					PIPE_GATING_CONTROL_ENABLE);

			TO_CONTROLLER_INFO(tm_resource)->power_gating_state =
				TM_POWER_GATE_STATE_ON;
		} else {
			TO_CONTROLLER_INFO(tm_resource)->power_gating_state =
					TM_POWER_GATE_STATE_OFF;
		}
	} /* for () */

	bp = dal_adapter_service_get_bios_parser(tm->adapter_srv);

	dal_bios_parser_set_scratch_acc_mode_change(bp);
}

/** block interrupt if we are under VBIOS (FSDOS) */
void dal_tm_block_interrupts(struct topology_mgr *tm, bool blocking)
{
	struct dal_context *dal_context = tm->dal_context;

	TM_NOT_IMPLEMENTED();
}

/** Acquiring Embedded display based on current HW config */
enum tm_result dal_tm_setup_embedded_display_path(struct topology_mgr *tm)
{
	struct dal_context *dal_context = tm->dal_context;

	TM_NOT_IMPLEMENTED();
	return TM_RESULT_FAILURE;
}

/** Release HW access and possibly restore some HW registers to its
 * default state */
void dal_tm_release_hw(struct topology_mgr *tm)
{
	dal_tm_detection_mgr_release_hw(tm->tm_dm);

	/* TODO: check if needed to add release_hw for resource manager */
}

/**
 * Update previous adapter power state to current
 * Update current adapter power state to new (passed as parameter)
 *
 * \param [in] power_state: New adapter power state
 */

void dal_tm_set_current_power_state(struct topology_mgr *tm,
		enum dal_video_power_state power_state)
{
	tm->previous_power_state = tm->current_power_state;
	tm->current_power_state = power_state;
}

bool dal_tm_update_display_edid(struct topology_mgr *tm,
				uint32_t display_index,
				uint8_t *edid_buffer,
				uint32_t buffer_len)
{
	struct display_path *display_path;
	struct dcs *dcs;
	enum edid_retrieve_status ret = EDID_RETRIEVE_FAIL;
	struct tm_detection_status detection_status;
	enum tm_detection_method safe_method;
	struct dal_context *dal_context = tm->dal_context;

	display_path = tm_get_display_path_at_index(tm, display_index);

	dcs = dal_display_path_get_dcs(display_path);

	ret = dal_dcs_override_raw_edid(dcs, buffer_len, edid_buffer);

	dal_memset(&detection_status, 0, sizeof(detection_status));

	/* For the use case "DSAT Override EDID":
	 * 1. Currently HPD IRQ is not working.
	 * 2. We test by connecting display AFTER system boot.
	 * 3. In order to get the new display to light-up, call
	 *    dal_display_path_set_target_connected(display_path, true)
	 * TODO: remove this comment when HPD IRQ is working. */
	/*dal_display_path_set_target_connected(display_path, true);*/

	detection_status.detected_signal =
			dal_display_path_get_query_signal(display_path,
			SINK_LINK_INDEX);
	detection_status.capability_changed = true;
	detection_status.monitor_changed = true;
	detection_status.connected = dal_display_path_is_target_connected(
			display_path);

	if (false == detection_status.connected) {
		dal_logger_write(dal_context->logger, LOG_MAJOR_DSAT,
			LOG_MINOR_DSAT_EDID_OVERRIDE,
				"%s: updating EDID on disconnected path: %d!\n",
				__func__, display_index);
	}

	dal_dcs_get_sink_capability(dcs, &detection_status.sink_capabilities);

	safe_method = tm_lock_path(tm, display_path,
			DETECTION_METHOD_DESTRUCTIVE_AND_EMBEDDED);

	/* Program Encoder according to new connector state */
	tm_update_on_connection_change(tm, display_path, &detection_status);

	arbitrate_audio_on_signal_change(tm, display_path, &detection_status);

	tm_unlock_path(tm, display_path, safe_method);

	dal_logger_write(dal_context->logger, LOG_MAJOR_DSAT,
		LOG_MINOR_DSAT_EDID_OVERRIDE,
			"%s(): DisplayInd: %d, DCS return code: %s (%d).\n",
			__func__, display_index,
			DCS_DECODE_EDID_RETRIEVE_STATUS(ret), ret);

	return ret == EDID_RETRIEVE_SUCCESS ||
		ret == EDID_RETRIEVE_SAME_EDID;
}

/**************
General queries
***************/
/** check is it coming from VBIOS or driver already made mode set at least
 * once */
bool dal_tm_is_hw_state_valid(struct topology_mgr *tm)
{
	/* Going to power down HW */
	if (tm->hw_power_down_required)
		return false;

	if (!dal_adapter_service_is_in_accelerated_mode(tm->adapter_srv)) {
		/* DAL driver has not taken control of HW from VBIOS yet */
		return false;
	}

	return true;
}

/** Query whether sync output can be attached to display path */
bool dal_tm_is_sync_output_available_for_display_path(
		struct topology_mgr *tm,
		uint32_t display_index,
		enum sync_source sync_output)
{
	struct dal_context *dal_context = tm->dal_context;

	TM_NOT_IMPLEMENTED();
	return false;
}

/**
 * Obtains display index of embedded display path (eDP or LCD)
 *
 * \return	display index of embedded display path if such exists,
 *		INVALID_DISPLAYINDEX otherwise
 */
uint32_t dal_tm_get_embedded_device_index(struct topology_mgr *tm)
{
	uint32_t i;
	uint32_t display_paths_num;
	struct display_path *display_path;
	struct connector *connector;
	struct graphics_object_id id;
	struct dal_context *dal_context = tm->dal_context;

	TM_IFACE_TRACE();

	display_paths_num = tm_get_display_path_count(tm);

	for (i = 0; i < display_paths_num; i++) {

		display_path = tm_get_display_path_at_index(tm, i);

		connector = dal_display_path_get_connector(display_path);

		id = dal_connector_get_graphics_object_id(connector);

		if (id.id == CONNECTOR_ID_LVDS || id.id == CONNECTOR_ID_EDP) {
			/* found it */
			return i;
		}
	}

	return INVALID_DISPLAY_INDEX;
}

/** Get GPU Clock Interface */
struct gpu_clock_interface *dal_tm_get_gpu_clock_interface(
		struct topology_mgr *tm)
{
	struct dal_context *dal_context = tm->dal_context;

	TM_NOT_IMPLEMENTED();
	return NULL;
}

/** Get DPCD Access Service Interface */
struct ddc *dal_tm_get_dpcd_access_interface(
		struct topology_mgr *tm,
		uint32_t display_index)
{
	struct dal_context *dal_context = tm->dal_context;

	TM_NOT_IMPLEMENTED();
	return NULL;
}

/** Get DDC Access Service Interface - by display index */
struct ddc *dal_tm_get_ddc_access_interface_by_index(
		struct topology_mgr *tm,
		uint32_t display_index)
{
	struct dal_context *dal_context = tm->dal_context;

	TM_NOT_IMPLEMENTED();
	return NULL;
}

/** Get DdcAccessServiceInterface - by connector */
struct ddc *dal_tm_get_ddc_access_interface_by_connector(
		struct topology_mgr *tm,
		struct graphics_object_id connector)
{
	struct dal_context *dal_context = tm->dal_context;

	TM_NOT_IMPLEMENTED();
	return NULL;
}

/** Report the number of functional controllers */
uint32_t dal_tm_get_num_functional_controllers(struct topology_mgr *tm)
{
	struct dal_context *dal_context = tm->dal_context;

	TM_NOT_IMPLEMENTED();
	return 0;
}

uint32_t dal_tm_get_display_path_index_for_controller(
		struct topology_mgr *tm,
		enum controller_id controller_id)
{
	return tm_resource_mgr_get_display_path_index_for_controller(
			tm->tm_rm, controller_id);
}

/** Returns current adapter power state */
enum dal_video_power_state dal_tm_get_current_power_state(
		struct topology_mgr *tm)
{
	struct dal_context *dal_context = tm->dal_context;

	TM_IFACE_TRACE();
	return tm->current_power_state;
}

/** Returns previous adapter power state */
enum dal_video_power_state dal_tm_get_previous_power_state(
		struct topology_mgr *tm)
{
	struct dal_context *dal_context = tm->dal_context;

	TM_IFACE_TRACE();
	return tm->previous_power_state;
}

struct gpu *dal_tm_get_gpu(
		struct topology_mgr *tm)
{
	return tm_resource_mgr_get_gpu_interface(tm->tm_rm);
}

/**********************
General functionallity
***********************/
/** update signal type of CrossFire Display Path according to upper
 *  layer's request */
enum tm_result dal_tm_set_signal_type(struct topology_mgr *tm,
		uint32_t display_index,
		enum signal_type signal)
{
	struct dal_context *dal_context = tm->dal_context;

	TM_NOT_IMPLEMENTED();
	return TM_RESULT_FAILURE;
}

/** Sets connectivity state to a display path if it supports "force connect" */
enum tm_result dal_tm_set_force_connected(struct topology_mgr *tm,
		uint32_t display_index, bool connected)
{
	struct display_path *disp_path =
			tm_get_display_path_at_index(tm, display_index);
	enum tm_detection_method detection_method = DETECTION_METHOD_CACHED;
	struct tm_detection_status detection_status = {0};

	/* Update display path property connection state*/
	union display_path_properties props =
			dal_display_path_get_properties(disp_path);

	props.bits.ALWAYS_CONNECTED = connected;
	dal_display_path_set_properties(disp_path, props);

	/* Get detection status and update connection status */
	detection_method = tm_lock_path(
			tm,
			disp_path,
			DETECTION_METHOD_DESTRUCTIVE_AND_EMBEDDED);

	detection_status.detected_signal = dal_display_path_get_query_signal(
			disp_path,
			SINK_LINK_INDEX);

	dal_dcs_query_sink_capability(
			dal_display_path_get_dcs(disp_path),
			&detection_status.sink_capabilities,
			true);
	detection_status.connected = connected;

	/* Arbitrate audio, update connection state and notify external */
	tm_post_target_detection(
			tm, disp_path, detection_method, &detection_status);

	tm_unlock_path(tm, disp_path, detection_method);

	return TM_RESULT_SUCCESS;
}

/**
 * Updates VBIOS with properties (signal, device tag) of current active
 * display paths.
 */
void dal_tm_force_update_scratch_active_and_requested(
		struct topology_mgr *tm)
{
	uint32_t i;
	uint32_t display_paths_num = tm_get_display_path_count(tm);
	struct display_path *display_path;
	struct bios_parser *bp;
	struct connector_device_tag_info *device_tag;
	enum signal_type signal;
	struct controller *controller;
	struct graphics_object_id id;
	struct dal_context *dal_context = tm->dal_context;

	TM_IFACE_TRACE();

	bp = dal_adapter_service_get_bios_parser(tm->adapter_srv);

	for (i = 0; i < display_paths_num; i++) {

		display_path = tm_get_display_path_at_index(tm, i);

		if (false == dal_display_path_is_acquired(display_path))
			continue;

		controller = dal_display_path_get_controller(display_path);

		id = dal_controller_get_graphics_object_id(controller);

		signal = dal_display_path_get_query_signal(display_path,
				SINK_LINK_INDEX);

		device_tag = dal_display_path_get_device_tag(display_path);

		dal_bios_parser_prepare_scratch_active_and_requested(
				bp, id.id, signal, device_tag);
	}

	dal_bios_parser_set_scratch_active_and_requested(bp);
}

/** Perform target connectivity check */
enum tm_result dal_tm_detect_and_notify_target_connection(
		struct topology_mgr *tm,
		uint32_t display_index,
		enum tm_detection_method method)
{
	struct dal_context *dal_context = tm->dal_context;

	TM_NOT_IMPLEMENTED();
	return TM_RESULT_FAILURE;
}

/** External entity notifies TM about the new connectivity (via Escape) */
void dal_tm_detect_notify_connectivity_change(struct topology_mgr *tm,
		uint32_t display_index,
		bool connected)
{
	struct dal_context *dal_context = tm->dal_context;

	TM_NOT_IMPLEMENTED();
}

/** External entity requests to re-enumerate the mode list of a device
 * and notify OS about the change. */
void dal_tm_notify_capability_change(struct topology_mgr *tm,
		uint32_t display_index,
		enum tm_reenum_modes_reason reason)
{
	struct dal_context *dal_context = tm->dal_context;

	TM_NOT_IMPLEMENTED();
}

/**************
Debug interface
***************/
/** Prints the content of a display path*/
void dal_tm_dump_display_path(struct topology_mgr *tm,
		uint32_t display_index)
{
	struct display_path *display_path;
	struct connector_device_tag_info *connector_device_tag;
	uint32_t i;
	uint32_t links_num;
	struct encoder *encoder;
	struct audio *audio;
	struct connector *connector;
	struct graphics_object_id id;
	struct controller *controller;
	/* Internal number, must be equal to the display_index (which is an
	 * index in TM storage for display path objects). */
	uint32_t display_path_index;
	struct dal_context *dal_context = tm->dal_context;

	display_path = tm_get_display_path_at_index(tm, display_index);
	links_num = dal_display_path_get_number_of_links(display_path);
	display_path_index = dal_display_path_get_display_index(display_path);

	if (display_path_index != display_index) {
		TM_ERROR("%s: internal path index (%d) != storage index (%d)\n",
				__func__, display_path_index, display_index);
	}

	TM_INFO("DisplayPath[%02d]: Sink Signal:%s, ASIC Signal:%s\n",
			display_index,
			tm_utils_signal_type_to_str(
					dal_display_path_get_query_signal(
							display_path,
							SINK_LINK_INDEX)),
			tm_utils_signal_type_to_str(
					dal_display_path_get_query_signal(
							display_path,
							ASIC_LINK_INDEX)));

	connector_device_tag = dal_display_path_get_device_tag(display_path);

	if (connector_device_tag != NULL) {
		TM_INFO("  (ACPI Device Tag: %s-%u ACPI=0x%X)\n",
			tm_utils_device_type_to_str(
				connector_device_tag->dev_id.device_type),
			connector_device_tag->dev_id.enum_id,
			connector_device_tag->acpi_device);
	}

	controller = dal_display_path_get_controller(display_path);
	if (controller != NULL) {
		id = dal_controller_get_graphics_object_id(controller);
		TM_INFO("  (%s %s-%s)\n",
				tm_utils_go_type_to_str(id),
				tm_utils_go_id_to_str(id),
				tm_utils_go_enum_to_str(id));
	}

	for (i = 0; i < links_num; i++) {

		encoder = dal_display_path_get_upstream_object(display_path, i);
		if (encoder != NULL) {
			id = dal_encoder_get_graphics_object_id(encoder);

			TM_INFO("  (Link[%u]: %s %s-%s. Transmitter: %s)\n",
				i,
				tm_utils_go_type_to_str(id),
				tm_utils_go_id_to_str(id),
				tm_utils_go_enum_to_str(id),
				tm_utils_transmitter_id_to_str(id));

		}

		audio = dal_display_path_get_audio(display_path, i);
		if (audio != NULL) {
			id = dal_audio_get_graphics_object_id(audio);

			TM_INFO("  (Link[%u]: %s %s-%s)\n",
					i,
					tm_utils_go_type_to_str(id),
					tm_utils_go_id_to_str(id),
					tm_utils_go_enum_to_str(id));
		}

	} /* for() */

	connector = dal_display_path_get_connector(display_path);
	if (connector != NULL) {
		struct connector_feature_support cfs;

		dal_connector_get_features(connector, &cfs);

		id = dal_connector_get_graphics_object_id(connector);

		TM_INFO("  (%s %s-%s [%s] [%s])\n",
				tm_utils_go_type_to_str(id),
				tm_utils_go_id_to_str(id),
				tm_utils_go_enum_to_str(id),
				tm_utils_hpd_line_to_str(cfs.hpd_line),
				tm_utils_ddc_line_to_str(cfs.ddc_line));
	}

	TM_INFO("\n");
}

/** Prints the content of all display paths and some other content*/
void dal_tm_dump(struct topology_mgr *tm)
{
	uint32_t ind;
	uint32_t display_paths_num = tm_get_display_path_count(tm);

	for (ind = 0; ind < display_paths_num; ind++)
		dal_tm_dump_display_path(tm, ind);
}

/** Blank CRTC and disable memory requests */
void dal_tm_disable_all_dcp_pipes(struct topology_mgr *tm)
{
	uint32_t i;
	struct tm_resource *tm_resource;
	struct controller *controller;
	struct dal_context *dal_context = tm->dal_context;
	const struct tm_resource_range *controllers =
		dal_tmrm_get_resource_range_by_type(
			tm->tm_rm,
			OBJECT_TYPE_CONTROLLER);

	TM_IFACE_TRACE();

	for (i = controllers->start; i < controllers->end; i++) {

		tm_resource = tm_resource_mgr_enum_resource(tm->tm_rm, i);

		controller = TO_CONTROLLER_INFO(tm_resource)->controller;

		dal_controller_disable_vga(controller);

		/* Blank controller using driver code instead of
		 * command table. */
		dal_controller_blank_crtc(controller,
				COLOR_SPACE_SRGB_FULL_RANGE);
	}
}

/* Callback interface - a way for tm_detection_mgr to notify
 * TM about hotplug event */
enum tm_result tm_handle_hpd_event(struct topology_mgr *tm,
		struct display_path *display_path)
{
	struct dal_context *dal_context = tm->dal_context;
	uint32_t display_index = dal_display_path_get_display_index(
			display_path);
	struct tm_detection_status detection_status;

	dal_memset(&detection_status, 0, sizeof(detection_status));

	if (NULL == display_path) {
		TM_ERROR("%s: No display_path found for display_index:%d!\n",
				__func__, display_index);
		return TM_RESULT_FAILURE;
	}

	detection_status.connected = tm_detect_display(tm, display_path,
			DETECTION_METHOD_HOTPLUG);

	TM_INFO("Display Path %d is now %s.\n", display_index,
		(dal_display_path_is_target_connected(display_path) == true ?
				"Connected" : "Disconnected"));

	if (true == detection_status.connected)
		return TM_RESULT_DISPLAY_CONNECTED;
	else
		return TM_RESULT_DISPLAY_DISCONNECTED;
}

struct controller *dal_tm_get_controller_from_display_path(
		struct topology_mgr *tm,
		struct display_path *display_path)
{
	struct controller *controller;
	struct dal_context *dal_context = tm->dal_context;

	if (NULL == display_path) {
		TM_ERROR("%s: Path pointer is NULL!\n", __func__);
		return NULL;
	}

	if (false == dal_display_path_is_acquired(display_path)) {
		TM_RESOURCES("%s: No Controller: Path[%02d] is not acquired!\n",
				__func__,
				dal_display_path_get_display_index(
						display_path));
		return NULL;
	}

	controller = dal_display_path_get_controller(display_path);

	if (NULL == controller) {
		TM_RESOURCES("%s: Path[%02d] acquired but No Controller!\n",
				__func__,
				dal_display_path_get_display_index(
						display_path));
		return NULL;
	}

	return controller;
}

/********************************
 *
 * Private functions.
 *
 *********************************/

static enum tm_result tm_init_during_construct(struct topology_mgr *tm)
{
	enum tm_result rc = TM_RESULT_SUCCESS;
	struct dal_context *dal_context = tm->dal_context;

	/******************************************************
	Create Resources and display paths - ORDER IS IMPORTANT!
	*******************************************************/
	do {
		rc = create_gpu_resources(tm);
		if (TM_RESULT_FAILURE == rc)
			break;

		rc = create_real_display_paths(tm);
		if (TM_RESULT_FAILURE == rc)
			break;

		rc = add_fake_crt_vga_dvi_paths(tm);
		if (TM_RESULT_FAILURE == rc)
			break;

		rc = miscellaneous_init(tm);
		if (TM_RESULT_FAILURE == rc)
			break;

		rc = transfer_paths_from_resource_builder_to_tm(tm);
		if (TM_RESULT_FAILURE == rc)
			break;

		rc = allocate_storage_for_link_services(tm);
		if (TM_RESULT_FAILURE == rc)
			break;

		associate_link_services_with_display_paths(tm);

		dal_tmrm_set_resources_range_by_type(tm->tm_rm);

		tm_init_features(tm);

		rc = tm_update_internal_database(tm);
		if (TM_RESULT_FAILURE == rc)
			break;

		rc = tm_handle_detection_register_display(tm);
		if (TM_RESULT_FAILURE == rc)
			break;

		/* destroy temporary objects */
		tm_resource_builder_destroy(&tm->tm_rb);

		TM_INFO("Number of Display Paths:         %u\n",
				tm_get_display_path_count(tm));

		TM_INFO("Number of Confunctional Paths:   %u\n",
				tm->max_num_of_cofunctional_paths);

		TM_INFO("Number of Confunctional Targets: %u\n",
				tm->max_num_of_cofunctional_targets);

		TM_INFO("Display Paths:\n");

		dal_tm_dump(tm);

		dal_tmrm_dump(tm->tm_rm);

	} while (0);

	return rc;
}

static enum tm_result create_gpu_resources(struct topology_mgr *tm)
{
	enum tm_result rc;
	struct dal_context *dal_context = tm->dal_context;

	/* Step 1. Create GPU resources */
	rc = tm_resource_builder_create_gpu_resources(tm->tm_rb);

	if (TM_RESULT_FAILURE == rc) {
		TM_ERROR("%s: tm_resource_builder_create_gpu_resources() failed!\n",
				__func__);
	}

	return rc;
}

static enum tm_result create_real_display_paths(struct topology_mgr *tm)
{
	enum tm_result rc;

	/* Step 2. Create real display path (i.e. reported by VBIOS) */
	rc = tm_resource_builder_build_display_paths(tm->tm_rb);
	if (TM_RESULT_FAILURE == rc)
		return rc;

	/* Step 3. Add resources for various features (like stereo, audio) */
	return tm_resource_builder_add_feature_resources(tm->tm_rb);
}

static enum tm_result add_fake_crt_vga_dvi_paths(struct topology_mgr *tm)
{
	/* Step 4. Add Fake CRT/VGA/DVI paths */
	return tm_resource_builder_add_fake_display_paths(tm->tm_rb);
}

static enum tm_result miscellaneous_init(struct topology_mgr *tm)
{
	enum tm_result rc = TM_RESULT_SUCCESS;
	struct dal_context *dal_context = tm->dal_context;

	/* Step 5. Sort Displays */
	tm_resource_builder_sort_display_paths(tm->tm_rb);

	/* Step 6. Assign display path specific resources */

	/* Step 7. Check number of paths */
	if (!tm_resource_builder_get_num_of_paths(tm->tm_rb)) {
		TM_ERROR("%s: No Display Paths were built!\n", __func__);
		rc = TM_RESULT_FAILURE;
	}

	return rc;
}

/**
 * Step 8. Transfer paths from the temporary Resource Builder object
 * to TM data member.
 * Note that paths are expected to be sorted in Resource Builder,
 * which is done by tm_resource_builder_sort_display_paths() call in Step 5.
 */
static enum tm_result transfer_paths_from_resource_builder_to_tm(
		struct topology_mgr *tm)
{
	uint32_t ind;
	uint32_t display_paths_num;
	struct display_path *display_path;
	uint32_t path_internal_index;
	struct dal_context *dal_context = tm->dal_context;

	display_paths_num = tm_resource_builder_get_num_of_paths(tm->tm_rb);

	tm->display_paths = dal_vector_create(display_paths_num,
			sizeof(struct display_path *));

	if (NULL == tm->display_paths) {
		TM_ERROR("%s:tm_resource_mgr_get_gpu_interface() failed!\n",
				__func__);
		return TM_RESULT_FAILURE;
	}

	for (ind = 0; ind < display_paths_num; ind++) {

		display_path = tm_resource_builder_get_path_at(tm->tm_rb,
				ind);
		if (NULL == display_path) {
			TM_ERROR("%s: NULL == display_path!\n", __func__);
			return TM_RESULT_FAILURE;
		}

		path_internal_index = dal_display_path_get_display_index(
				display_path);

		/* Insert each Path into TM storage at index which matches
		 * internal index of Path. It is important because internal
		 * index of Path dictates priority of Path, and we want
		 * the the two indexes to match.
		 * See tmrb_swap_entries() for details about path priorities.*/
		if (false == display_paths_vector_insert_at(
				tm->display_paths,
				&display_path,
				path_internal_index)) {
			TM_ERROR("%s: failed to add path!\n", __func__);
			return TM_RESULT_FAILURE;
		}
	}

	return TM_RESULT_SUCCESS;
}

/**
 * Step 9. Allocate storage for link services according to the actual number
 * of paths.
 */
static enum tm_result allocate_storage_for_link_services(
		struct topology_mgr *tm)
{
	return tm_resource_mgr_setup_link_storage(tm->tm_rm,
			tm_get_display_path_count(tm));
}

/**
 * Step 10. Once display paths are built and all resources allocated
 * we can create resource index and associate link services with
 * display paths.
 */
static void associate_link_services_with_display_paths(
		struct topology_mgr *tm)
{
	struct tm_resource_mgr *tm_rm = tm->tm_rm;
	uint32_t ind;
	uint32_t display_paths_num;

	tm_resource_mgr_relink_encoders(tm_rm);

	display_paths_num = tm_get_display_path_count(tm);

	for (ind = 0; ind < display_paths_num; ind++) {

		tm_resource_mgr_associate_link_services(tm_rm,
			tm_get_display_path_at_index(tm, ind));
	}
}

/**
 * Initialise Topology Manager features.
 * Get Features from Adapter Service and set internal flags accordingly.
 */
static void tm_init_features(struct topology_mgr *tm)
{
	uint32_t i = 0;
	uint32_t resource_num;
	struct tm_resource *tm_resource;
	struct clock_source *clock_source;
	enum clock_sharing_level clock_sharing_level;
	const struct tm_resource_range *clock_sources =
		dal_tmrm_get_resource_range_by_type(
			tm->tm_rm,
			OBJECT_TYPE_CLOCK_SOURCE);

	/* TODO: is there a 'force-connect' or 'always connected' Display
	 * Path feature, as in DAL2? */

	tm->max_num_of_non_dp_paths =
		dal_adapter_service_get_max_cofunc_non_dp_displays();

	tm->single_selected_timing_signals =
		dal_adapter_service_get_single_selected_timing_signals();

	dal_adapter_service_get_feature_value(
		FEATURE_SUPPORTED_HDMI_CONNECTION_NUM,
		&tm->max_num_of_supported_hdmi,
		sizeof(tm->max_num_of_supported_hdmi));

	resource_num = tm_resource_mgr_get_total_resources_num(tm->tm_rm);

	for (i = clock_sources->start; i < clock_sources->end; i++) {

		tm_resource = tm_resource_mgr_enum_resource(tm->tm_rm, i);

		clock_source = TO_CLOCK_SOURCE_INFO(tm_resource)->clock_source;

		clock_sharing_level = dal_clock_souce_get_clk_sharing_lvl(
			clock_source);

		if (clock_sharing_level > tm->clock_sharing_level)
			tm->clock_sharing_level = clock_sharing_level;
	}

}

static enum tm_result tm_update_single_encoder_implementation(
		struct topology_mgr *tm,
		struct display_path *display_path,
		uint32_t link_index)
{
	struct encoder_context ctx;
	struct encoder *this_encoder;
	struct graphics_object_id obj_id;
	const char *transmitter_str;
	uint32_t dsp_index;
	struct dal_context *dal_context = tm->dal_context;

	dal_memset(&ctx, 0, sizeof(ctx));
	ctx.engine = ENGINE_ID_UNKNOWN;

	this_encoder = dal_display_path_get_upstream_object(
			display_path, link_index);

	if (NULL == this_encoder) {
		TM_ERROR("%s: Encoder is NULL for link index:%d!\n",
				__func__, link_index);
		return TM_RESULT_FAILURE;
	}

	tm_build_encoder_context(tm->dal_context, display_path, link_index,
			&ctx);

	if (ENCODER_RESULT_OK != dal_encoder_update_implementation(
			this_encoder, &ctx)) {
		TM_ERROR("%s:dal_encoder_update_implementation() failed!\n",
				__func__);
		return TM_RESULT_FAILURE;
	}

	obj_id = dal_encoder_get_graphics_object_id(this_encoder);

	transmitter_str = tm_utils_transmitter_id_to_str(obj_id);

	dsp_index = dal_display_path_get_display_index(display_path);

	TM_ENCODER_CTL("Encoder Update Impl: %s, Path=%u, Link=%u, Engine=%s, Signal=%s\n",
			transmitter_str,
			dsp_index,
			link_index,
			tm_utils_engine_id_to_str(ctx.engine),
			tm_utils_signal_type_to_str(ctx.signal));

	return TM_RESULT_SUCCESS;

}

static enum tm_result tm_update_encoder_implementations(
		struct topology_mgr *tm)
{
	uint32_t dsp_index;
	uint32_t paths_num;
	uint32_t link_index;
	uint32_t num_links;
	struct display_path *display_path;

	paths_num = tm_get_display_path_count(tm);

	for (dsp_index = 0; dsp_index < paths_num; dsp_index++) {

		display_path = tm_get_display_path_at_index(tm,
				dsp_index);

		num_links = dal_display_path_get_number_of_links(display_path);

		for (link_index = 0; link_index < num_links; link_index++) {

			if (TM_RESULT_SUCCESS !=
				tm_update_single_encoder_implementation(tm,
					display_path, link_index)){
				/* should never happen */
				return TM_RESULT_FAILURE;
			}
		}
	}

	return TM_RESULT_SUCCESS;
}

/**
 * Check if 'this encoder' can use more than one Engine.
 * This function is mainly written for eDP Resource sharing feature
 *
 * \return true: yes, more than one Engine can be used.
 *	false: no, only a single Engine can be used.
 */
static bool tm_are_alternative_engines_supported(
		const struct display_path *display_path)
{
	uint32_t engine_count = 0;
	uint32_t ind;
	struct encoder *encoder = dal_display_path_get_upstream_object(
			display_path, ASIC_LINK_INDEX);
	union supported_stream_engines supported_stream_engines =
			dal_encoder_get_supported_stream_engines(encoder);

	for (ind = 0; ind < ENGINE_ID_COUNT; ind++) {

		if (tm_utils_is_supported_engine(supported_stream_engines, ind))
			engine_count++;
	}

	return (engine_count > 1 ? true : false);
}

/**
 * Get stream engines priority based on sink signal type and connectivity state.
 * Entry with highest priority will be acquired first by MST Display Path
 *
 * \param [in] display_path:      Display path to which engine somehow related
 * \param [in] is_preferred_engine: True if this engine is preferred for
 *				given display path
 *
 * \return
 *     Stream Engine priority
 */
static enum tm_engine_priority tm_get_stream_engine_priority(
		struct display_path *display_path, bool is_preferred_engine)
{
	enum signal_type signal = dal_display_path_get_query_signal(
			display_path, ASIC_LINK_INDEX);
	bool connected = dal_display_path_is_target_connected(display_path);
	enum tm_engine_priority requested_priority = TM_ENGINE_PRIORITY_UNKNOWN;
	bool is_embedded_signal;
	bool is_alternative_engines_supported;

	/* For embedded panels(LVDS only), we want to reserve one stream engine
	 * resource to guarantee the embedded panel can be used.
	 * We don't reserve for eDP (controlled by runtime parameter) because on
	 * some ASICs for e.g. Kabini where there are only 2 DIGs and if we
	 * reserve one for eDP then we can drive only one MST monitor even if
	 * the user disables eDP, by not reserving for eDP, the user will have
	 * the option to disable eDP and then be able to drive 2 MST monitors.
	 * To avoid reserving all stream engines for embedded use, reserve only
	 * the preferred engine. */

	is_embedded_signal = dal_is_embedded_signal(
			dal_display_path_get_query_signal(display_path,
							SINK_LINK_INDEX));

	is_alternative_engines_supported =
			tm_are_alternative_engines_supported(display_path);

	if (is_preferred_engine &&
		is_embedded_signal &&
		is_alternative_engines_supported) {
		/* This Engine can NOT be used by MST */
		return TM_ENGINE_PRIORITY_NON_MST_CAPABLE;
	}

	switch (signal) {
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
	case SIGNAL_TYPE_DISPLAY_PORT:
	case SIGNAL_TYPE_EDP:
		if (connected && is_preferred_engine) {
			requested_priority =
					TM_ENGINE_PRIORITY_MST_DP_CONNECTED;
		} else {
			requested_priority = TM_ENGINE_PRIORITY_MST_DP_MST_ONLY;
		}
		break;

	case SIGNAL_TYPE_DVI_SINGLE_LINK:
	case SIGNAL_TYPE_DVI_SINGLE_LINK1:
	case SIGNAL_TYPE_DVI_DUAL_LINK:
		if (connected) {
			requested_priority =
					TM_ENGINE_PRIORITY_MST_DVI_CONNECTED;
		} else {
			requested_priority = TM_ENGINE_PRIORITY_MST_DVI;
		}
		break;

	case SIGNAL_TYPE_HDMI_TYPE_A:
		if (connected) {
			requested_priority =
					TM_ENGINE_PRIORITY_MST_HDMI_CONNECTED;
		} else {
			requested_priority = TM_ENGINE_PRIORITY_MST_HDMI;
		}
		break;

	default:
		requested_priority = TM_ENGINE_PRIORITY_NON_MST_CAPABLE;
		break;

	}

	return requested_priority;
}

static void tm_update_stream_engine_priorities_for_path(
		struct topology_mgr *tm,
		struct display_path *display_path)
{
	uint32_t i;
	struct tm_resource *tm_resource;
	struct tm_resource_engine_info *engine_info;
	struct encoder *encoder;
	union supported_stream_engines supported_stream_engines;
	enum engine_id preferred_engine_id;
	bool is_preferred_engine;
	enum tm_engine_priority priority;
	uint32_t engine_id;
	struct tm_resource_mgr *tm_rm = tm->tm_rm;
	struct dal_context *dal_context = tm->dal_context;
	const struct tm_resource_range *engines =
		dal_tmrm_get_resource_range_by_type(
			tm_rm,
			OBJECT_TYPE_ENGINE);

	/* We check engine only to first encoder - most close to GPU. */
	encoder = dal_display_path_get_upstream_object(display_path,
			ASIC_LINK_INDEX);

	if (NULL == encoder) {
		TM_ERROR("%s: No encoder after GPU!?\n", __func__);
		return;
	}

	supported_stream_engines = dal_encoder_get_supported_stream_engines(
			encoder);

	preferred_engine_id = dal_encoder_get_preferred_stream_engine(encoder);

	for (i = engines->start; i < engines->end; i++) {

		tm_resource = tm_resource_mgr_enum_resource(tm_rm, i);

		engine_id = GRPH_ID(tm_resource).id;

		if (!tm_utils_is_supported_engine(supported_stream_engines,
				engine_id)) {
			/* not a supported engine */
			continue;
		}

		engine_info = TO_ENGINE_INFO(tm_resource);

		if (preferred_engine_id ==
			dal_graphics_object_id_get_engine_id(
				GRPH_ID(tm_resource))) {
			/* This engine is the same as the one
			 * preferred by Encoder. */
			is_preferred_engine = true;
		} else {
			/* This engine is different from the one
			 * preferred by Encoder. */
			is_preferred_engine = false;
		}

		priority = tm_get_stream_engine_priority(display_path,
				is_preferred_engine);

		if (engine_info->priority == TM_ENGINE_PRIORITY_UNKNOWN
				|| engine_info->priority < priority) {

			engine_info->priority = priority;

			TM_ENG_ASN("  New Engine Priority[%s]=%s(%u).\n",
					tm_utils_engine_id_to_str(engine_id),
					tm_utils_engine_priority_to_str(
							priority),
					priority);
		} /* if() */
	} /* for() */
}

/**
 * Sets stream engines priority.
 * Entry with highest priority (lowest value) will be acquired first
 * by MST Display Path.
 */
static void tm_update_stream_engine_priorities(struct topology_mgr *tm)
{
	struct tm_resource_mgr *tm_rm = tm->tm_rm;
	struct tm_resource *tm_resource;
	uint32_t i;
	uint32_t paths_num;
	struct dal_context *dal_context = tm->dal_context;
	const struct tm_resource_range *engines =
		dal_tmrm_get_resource_range_by_type(
			tm_rm,
			OBJECT_TYPE_ENGINE);

	TM_ENG_ASN("%s() - Start\n", __func__);

	for (i = engines->start; i < engines->end; i++) {

		tm_resource = tm_resource_mgr_enum_resource(tm_rm, i);

		TO_ENGINE_INFO(tm_resource)->priority =
			TM_ENGINE_PRIORITY_UNKNOWN;
	}

	/* Update Stream Engine Priorities based on engine mapping of
	 * each path.
	 * We choose lowest priority (highest value) among priorities
	 * reported by all paths which MAY use this engine.
	 */

	paths_num = tm_get_display_path_count(tm);

	for (i = 0; i < paths_num; i++) {

		tm_update_stream_engine_priorities_for_path(
			tm,
			tm_get_display_path_at_index(tm, i));
	}

	TM_ENG_ASN("%s() - End\n", __func__);
}

/**
 * Creates initial cache of cofunctional displays and
 * calculates maximal number of cofunctional displays
 * Should be called only once at bootup
 */

static bool tm_create_initial_cofunc_display_subsets(
	struct topology_mgr *tm)
{
	uint32_t num_func_controllers;
	uint32_t i;
	uint32_t display_paths_num;
	bool allow_sharing = false;
	struct gpu *gpu;
	enum signal_type signal;
	struct display_path *display_path = NULL;
	enum clock_sharing_group clock_sharing_grp;
	union display_path_properties properties;
	struct dal_context *dal_context = tm->dal_context;

	display_paths_num = tm_get_display_path_count(tm);

	gpu = tm_resource_mgr_get_gpu_interface(tm->tm_rm);
	num_func_controllers = dal_gpu_get_num_of_functional_controllers(gpu);

	ASSERT(tm->display_subsets == NULL);

	/* Calculate max number of cofunc displays.
	 * We do it in 4 steps when clock sharing supported
	 * If clock sharing not supported, we keep clock_sharing_group
	 * of displaypath as exclusive and can skip the first and last
	 * steps and can skip first and last steps
	 */

	/* 1. Force settings on display paths whenever possible to reduce
	 * limitations when we calculate cofunc displays
	 */

	for (i = 0; i < display_paths_num; i++) {

		/* Force sharing on display paths whenever possible
		* This will reduce clock resource limitation when we
		* calculate cofunc displays. However crossfire path we still
		* still want to share clock_source exclusively
		*/

		display_path = tm_get_display_path_at_index(tm, i);
		signal = dal_display_path_get_query_signal(
			display_path,
			ASIC_LINK_INDEX);

		/*TODO: Add reportSingleSelectedTiming check*/
		TM_NOT_IMPLEMENTED();

		clock_sharing_grp = tm_get_default_clock_sharing_group(
			tm,
			signal,
			allow_sharing);

		dal_display_path_set_clock_sharing_group(
			display_path,
			clock_sharing_grp);

		/* For root DP MST Display Path, we need to set
		* SignalType_DisplayPort_MST to correctly calculate
		* the maximum number of cofunctional targets.
		* After calculating the cofunctional targets, we will
		* revert the default signal back to DP.
		*/
		properties = dal_display_path_get_properties(display_path);
		if (properties.bits.IS_ROOT_DP_MST_PATH)
			dal_display_path_set_sink_signal(
				display_path,
				SIGNAL_TYPE_DISPLAY_PORT_MST);
	}

	/* 2. Figure out the greatest confunctional display paths subset.
	 *	We start with total number of display paths and reduce the size
	 *	until successful confunctional subset allocation
	 */
	tm->max_num_of_cofunctional_paths = display_paths_num <
		num_func_controllers ? display_paths_num :
				num_func_controllers;

	while (tm->max_num_of_cofunctional_paths > 0) {

		if (tm_check_num_of_cofunc_displays(tm,
			display_paths_num,
			tm->max_num_of_cofunctional_paths))
			break;

		tm->max_num_of_cofunctional_paths--;

	}

	/* 3. Figure out the greatest confunctional targets subset.
	 *	We start with total max number of confunctional display
	 *	paths and reduce the size until successful
	 *	confunctional subset allocation
	 */
	tm->max_num_of_cofunctional_targets =
		tm->max_num_of_cofunctional_paths;

	if (tm_resource_builder_get_num_of_paths(tm->tm_rb) !=
			display_paths_num) {

		while (tm->max_num_of_cofunctional_targets > 0) {
			if (tm_check_num_of_cofunc_displays(tm,
				display_paths_num,
				tm->max_num_of_cofunctional_targets))
				break;

			tm->max_num_of_cofunctional_targets--;
		}
	}

	/* 4. Setup defaults on all display paths. */
	for (i = 0; i < display_paths_num; i++) {

		display_path = tm_get_display_path_at_index(tm, i);

		/*Setup default clock sharing group on all display paths.*/
		/* Same here - we skip CF path*/
		signal = dal_display_path_get_query_signal(
					display_path,
					ASIC_LINK_INDEX);

		clock_sharing_grp = tm_get_default_clock_sharing_group(
						tm,
						signal,
						false);

		dal_display_path_set_clock_sharing_group(
			display_path,
			clock_sharing_grp);

		/* We finished calculating the maximum number
		 * of cofunctional targets. Now we revert the
		 * default signal back to DP.
		 */
		properties = dal_display_path_get_properties(display_path);
		if (properties.bits.IS_ROOT_DP_MST_PATH)
			dal_display_path_set_sink_signal(
				display_path,
				SIGNAL_TYPE_DISPLAY_PORT);

	}

	ASSERT(tm->max_num_of_cofunctional_paths > 0);
	ASSERT(tm->max_num_of_cofunctional_targets > 0);

	/* If if successfully calculated maximum
	 * number of cofunctional displays we can
	 * proceed to next step - init relevant
	 * data members and allocate cache of
	 * confunctional subsets
	 */
	if (tm->max_num_of_cofunctional_paths > 0) {

		tm->display_subsets = dal_tm_subsets_cache_create(
				tm->dal_context,
				display_paths_num,
				tm->max_num_of_cofunctional_paths,
				num_func_controllers);
	}

	return (tm->display_subsets != NULL);

}

static bool tm_check_num_of_cofunc_displays(
		struct topology_mgr *tm,
		uint32_t max_value,
		uint32_t max_subset_size)
{
	bool calc_result;
	bool ret_value = false;
	struct tm_resource_mgr *resource_mgr;
	struct tm_calc_subset *calc_subset;
	struct dal_context *dal_context = tm->dal_context;

	/* Allocate Temporary resources*/
	resource_mgr = tm_resource_mgr_clone(tm->tm_rm);
	if (resource_mgr == NULL) {
		TM_ERROR("%s: Failed to clone resources", __func__);
		return false;
	}

	TM_COFUNC_PATH(
		"%s Max size of subset: %u. Display index range 0-%u.\n",
		__func__, max_subset_size, max_value-1);

	calc_subset = dal_tm_calc_subset_create();
	calc_result = dal_tm_calc_subset_start(
					calc_subset,
					max_value,
					max_subset_size);

	while (calc_result) {

		if (calc_subset->subset_size == max_subset_size) {

			/*TODO:dumpSubset(count++, &calc_subset);*/

			if (tm_can_display_paths_be_enabled_at_the_same_time(
				tm,
				resource_mgr,
				calc_subset->buffer,
				calc_subset->subset_size)) {

				ret_value = true;
				break;
			}

			TM_COFUNC_PATH(
				"Subset not valid. Continue to iterate...\n");
		}
		calc_result = dal_tm_calc_subset_step(calc_subset);
	}

	tm_resource_mgr_destroy(&resource_mgr);
	dal_tm_calc_subset_destroy(calc_subset);
	return ret_value;
}

static bool tm_can_display_paths_be_enabled_at_the_same_time(
		struct topology_mgr *tm,
		struct tm_resource_mgr *tm_rm_clone,
		const uint32_t *displays,
		uint32_t array_size)
{
	bool success = true;
	uint32_t num_of_non_dp_paths = 0;
	struct display_path *display_path;
	struct link_service *last_mst_link_service = NULL;
	struct link_service *mst_link_service = NULL;
	uint32_t i;

	ASSERT(tm->tm_rm != NULL);
	ASSERT(displays != NULL);
	ASSERT(array_size >  0);

	tm_resource_mgr_reset_all_usage_counters(tm_rm_clone);

	/* Try to acquire resources temporarily*/
	for (i = 0; i < array_size; i++) {

		display_path = tm_get_display_path_at_index(
				tm,
				displays[i]);

		if (!tm_resource_mgr_acquire_resources(
			tm_rm_clone,
			display_path,
			/* Validation doesn't require change of HW state! */
			TM_ACQUIRE_METHOD_SW)) {

			success = false;
			break;
		}

		if (!dal_is_dp_signal(
				dal_display_path_get_query_signal(
						display_path,
						ASIC_LINK_INDEX))) {

			num_of_non_dp_paths++;

			/* make sure we do not exceed
			 * limitations on number of non-DP paths
			 */
			if (num_of_non_dp_paths >
				tm->max_num_of_non_dp_paths) {
				success = false;
				break;
			}
		}
	}

	/* Release acquired resources*/
	for (i = 0; i < array_size; i++) {

		display_path = tm_get_display_path_at_index(
			tm,
			displays[i]);

		tm_resource_mgr_release_resources(
			tm_rm_clone,
			display_path,
			TM_ACQUIRE_METHOD_SW);
	}

	/* validate against MST bandwidth*/
	for (i = 0; i < array_size; i++) {

		if (!success)
			break;

		display_path = tm_get_display_path_at_index(
				tm,
				displays[i]);

		mst_link_service =
			dal_display_path_get_mst_link_service(display_path);

		/* only need to call each MST Link
		 * Service once with all display indices
		 */
		if (mst_link_service != NULL &&
			mst_link_service != last_mst_link_service) {

			success = dal_ls_are_mst_displays_cofunctional(
					mst_link_service,
					displays,
					array_size);
			last_mst_link_service = mst_link_service;
		}
	}

	return success;

}

/**
 * Returns the default clock sharing group based on signal
 */
static enum clock_sharing_group tm_get_default_clock_sharing_group(
	struct topology_mgr *tm,
	enum signal_type signal,
	bool allow_per_timing_sharing)
{
	enum clock_sharing_group clk_sharing_grp =
		CLOCK_SHARING_GROUP_EXCLUSIVE;

	switch (signal) {
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
		if (tm->clock_sharing_level >=
			CLOCK_SHARING_LEVEL_DISPLAY_PORT_SHAREABLE)
			clk_sharing_grp = CLOCK_SHARING_GROUP_DISPLAY_PORT;
		else if (tm->clock_sharing_level >=
			CLOCK_SHARING_LEVEL_DP_MST_SHAREABLE)
			clk_sharing_grp = CLOCK_SHARING_GROUP_DP_MST;
		else if (allow_per_timing_sharing)
			clk_sharing_grp = CLOCK_SHARING_GROUP_GROUP1;

		break;
	case SIGNAL_TYPE_EDP:
	case SIGNAL_TYPE_DISPLAY_PORT:
		if (tm->clock_sharing_level >=
			CLOCK_SHARING_LEVEL_DISPLAY_PORT_SHAREABLE)
			clk_sharing_grp = CLOCK_SHARING_GROUP_DISPLAY_PORT;
		else if (allow_per_timing_sharing)
			clk_sharing_grp = CLOCK_SHARING_GROUP_GROUP1;

		break;
	case SIGNAL_TYPE_DVI_SINGLE_LINK:
	case SIGNAL_TYPE_DVI_SINGLE_LINK1:
	case SIGNAL_TYPE_DVI_DUAL_LINK:
	case SIGNAL_TYPE_HDMI_TYPE_A:
		if (allow_per_timing_sharing)
			clk_sharing_grp = CLOCK_SHARING_GROUP_GROUP1;
		break;
	default:
		break;
	}

	return clk_sharing_grp;
}

/**
 * Update Topology Manager internal database
 */
static enum tm_result tm_update_internal_database(struct topology_mgr *tm)
{
	struct dal_context *dal_context = tm->dal_context;

	/* Update encoder implementation PRIOR to call
	 * tm_update_stream_engine_priorities(). */
	if (TM_RESULT_FAILURE == tm_update_encoder_implementations(tm))
		return TM_RESULT_FAILURE;

	tm_update_stream_engine_priorities(tm);

	if (!tm_create_initial_cofunc_display_subsets(tm)) {
		TM_ERROR("%s: Failed to create cofunctional subsets",
			__func__);
		return TM_RESULT_FAILURE;
	}

	return TM_RESULT_SUCCESS;
}

static bool tm_is_display_index_valid(struct topology_mgr *tm,
		uint32_t display_index, const char *caller_func)
{
	uint32_t path_count;
	struct dal_context *dal_context = tm->dal_context;

	path_count = tm_get_display_path_count(tm);

	if (display_index >= path_count) {
		TM_ERROR("%s: display_index '%d' greater than maximum of %d!\n",
			caller_func, display_index, path_count);
		return false;
	}

	return true;
}

/* Register to recieve HPD interrupt. */
enum tm_result dal_tm_register_for_display_detection_interrupt(
		struct topology_mgr *tm)
{
	enum tm_result rc = TM_RESULT_SUCCESS;
	uint32_t i;
	struct display_path *path;
	union display_path_properties props;

	for (i = 0; i < tm_get_display_path_count(tm); i++) {

		path = tm_get_display_path_at_index(tm, i);

		props = dal_display_path_get_properties(path);

		if (props.bits.FAKED_PATH || props.bits.IS_BRANCH_DP_MST_PATH)
			continue;

		if (!dal_tm_detection_mgr_register_hpd_irq(tm->tm_dm, path)) {
			rc = TM_RESULT_FAILURE;
			break;
		}
	}

	return rc;
}

/* Register IRQ Sources. */
static enum tm_result tm_handle_detection_register_display(
		struct topology_mgr *tm)
{
	enum tm_result rc = TM_RESULT_SUCCESS;
	uint32_t i;
	struct display_path *path;
	union display_path_properties props;

	for (i = 0; i < tm_get_display_path_count(tm); i++) {

		path = tm_get_display_path_at_index(tm, i);

		props = dal_display_path_get_properties(path);

		if (props.bits.FAKED_PATH || props.bits.IS_BRANCH_DP_MST_PATH)
			continue;

		if (!dal_tm_detection_mgr_register_display(tm->tm_dm, path)) {
			rc = TM_RESULT_FAILURE;
			break;
		}
	}

	return rc;
}

/****************************
 * Plane-related interfaces.
 ****************************/
/**
 * Acquire resources for the set of Planes.
 * For each acquired resource, set configuration options which will be used
 * for HW programming.
 *
 * NOTE: it is assumed that SetMode was already called and acquired the
 * "root" controller for the "main" plain.
 * Because of this assumption this function will acquire resources for planes
 * AFTER the 1st one. But, the configuration of "root" plane will be changed.
 *
 * \param [in] num_planes: number of planes.
 *
 * \param [in] configs: array of Plane Configuration structures.
 *
 * \return : NO return code! It is assumed that caller validated the
 *	configuration *before* setting it.
 *	The advantage is that even for an incorrect configuration
 *	we will see something on the screen (no all planes), instead
 *	of a black screen.
 *	If we run out of resources we print an error message into
 *	the logger.
 */
void dal_tm_acquire_plane_resources(
	struct topology_mgr *tm,
	uint32_t display_index,
	uint32_t num_planes,
	const struct plane_config *configs)
{
	struct display_path *path;
	struct dal_context *dal_context = tm->dal_context;
	uint32_t plane_ind;
	struct display_path_plane plane;
	const struct plane_config *curr_config;
	struct controller *controller;
	uint32_t controller_index;
	struct display_path_plane *root_plane;
	uint32_t dp_planes_num;

	path = tm_get_display_path_at_index(tm, display_index);
	if (NULL == path) {
		TM_ERROR("%s: invalid display_index:%d!\n", __func__,
				display_index);
		return;
	}

	root_plane = dal_display_path_get_plane_at_index(path, 0);
	if (NULL == root_plane) {
		TM_ERROR("%s: Invalid State! Was the Mode Set? [Path: %d]\n",
				__func__, display_index);
		return;

	}

	/* handle the 'root' plane 1st */
	plane_ind = 0;
	curr_config = &configs[plane_ind];
	if (!dal_controller_is_surface_supported(root_plane->controller,
			curr_config)) {
		TM_MPO("%s: Surface is NOT supported on 'root'! Path:%d\n",
				__func__, display_index);
		/* This is (most likely) the case of Full Screen Video which
		 * we want to display via Underlay.
		 * In this case OS supplied us a single surface.
		 * And we can power-gate parts of root-pipe FE. */
		root_plane->disabled = true;
		root_plane->blnd_mode = BLENDER_MODE_OTHER_PIPE;
		/* Note that the loop below will still start from config[0]
		 * and will add a new plane on top of the root one. */
		if (num_planes != 1) {
			TM_WARNING(
			"%s: Number of Planes NOT equals one! [Path: %d]\n",
					__func__, display_index);
		}
	} else {
		/* TODO: add real root 'plane' initialisation here,
		 * based on parameters passed in */
		root_plane->disabled = false;
		root_plane->blnd_mode = BLENDER_MODE_BLENDING;
		/* the loop below will skip the search for 'root' controller
		 * because the one it has now supports the surface. */
		plane_ind++;
	}

	for (; plane_ind < num_planes; plane_ind++) {

		curr_config = &configs[plane_ind];
		controller = get_controller_for_plane_index(tm, path,
				plane_ind, curr_config, &controller_index);

		if (controller) {
			dal_memset(&plane, 0, sizeof(plane));
			/* TODO: add real 'plane' initialisation here,
			 * based on parameters passed in */
			plane.controller = controller;
			plane.disabled = false;
			plane.blnd_mode = BLENDER_MODE_BLENDING;
			/* found free controller -> add the plane to the path */
			dal_display_path_add_plane(path, &plane);

			dal_tmrm_acquire_controller(tm->tm_rm, path,
					controller_index,
					TM_ACQUIRE_METHOD_HW);
		}
	} /* for() */

	dp_planes_num = dal_display_path_get_number_of_planes(path);

	{
		struct display_path_plane *plane =
			dal_display_path_get_plane_at_index(
				path,
				dp_planes_num - 1);

		if (!plane->disabled)
			plane->blnd_mode = BLENDER_MODE_CURRENT_PIPE;
	}

	TM_MPO("%s: acquired resources for %d planes out of %d.\n",
		__func__,
		dp_planes_num,
		num_planes);
}

static struct controller *get_controller_for_plane_index(
		struct topology_mgr *tm,
		struct display_path *path,
		uint32_t plane_index,
		const struct plane_config *plcfg,
		uint32_t *controller_index_out)
{
	struct dal_context *dal_context = tm->dal_context;
	uint32_t display_index = dal_display_path_get_display_index(path);
	uint32_t controller_mask = 0;
	struct controller *controller;

	while (true) {
		*controller_index_out = RESOURCE_INVALID_INDEX;

		controller =
			dal_tmrm_get_free_controller(
				tm->tm_rm,
				controller_index_out,
				controller_mask);

		/* if we fail to acquire underlay try to get another controller
		 */
		if (NULL == controller) {
			TM_ERROR("%s: Failed to get controller! Path:%d, Plane:%d\n",
				__func__,
				display_index,
				plane_index);
			break;
		}

		if (dal_controller_is_surface_supported(controller, plcfg))
			break;

		controller_mask |=
			1 <<
			dal_controller_get_graphics_object_id(controller).id;
	}

	return controller;
}

/* Release resource acquired by dal_tm_acquire_plane_resources() */
void dal_tm_release_plane_resources(
		struct topology_mgr *tm,
		uint32_t display_index)
{
	struct dal_context *dal_context = tm->dal_context;
	struct display_path *display_path;

	display_path = tm_get_display_path_at_index(tm, display_index);
	if (NULL == display_path) {
		TM_ERROR("%s: invalid display_index:%d!\n", __func__,
				display_index);
		return;
	}

	dal_tmrm_release_non_root_controllers(tm->tm_rm, display_path,
			TM_ACQUIRE_METHOD_HW);

	dal_display_path_release_non_root_planes(display_path);
}

/***********************************
 * End-of-Plane-related interfaces.
 ***********************************/

/* Handles hotplug/hotunplug event -just performs
 * detection on requested display */
void dal_tm_handle_sink_connectivity_change(
		struct topology_mgr *tm,
		uint32_t display_index)
{
	struct display_path *display_path;
	struct dal_context *dal_context = tm->dal_context;

	display_path = tm_get_display_path_at_index(tm, display_index);

	if (NULL == display_path) {
		TM_ERROR("%s: invalid display_index:%d!\n", __func__,
				display_index);
		return;
	}

	tm_detect_display(tm, display_path, DETECTION_METHOD_HOTPLUG);
}
