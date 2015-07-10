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

/**
 *****************************************************************************
 * tm_resource_mgr responsible to manage all display
 * HW resources (excluding GPU).
 * It is also responsible for acquiring/releasing resources.
 *
 *   Resources are managed in sorted vector. The order of resources
 *   determined by (from MSB to LSB):
 *     1. Resource type     (encoder, audio, etc.)
 *     2. Resource priority (within type it allows to have internal
 *		logic to sort resources)
 *     3. Resource id       (DAC, DVO, Uniphy, etc.)
 *     4. Resource enum     (every object with same ID can have
 *		multiple instances - enums)
 *
 *
 *****************************************************************************
 */

#ifndef __DAL_TM_RESOURCE_MGR_H__
#define __DAL_TM_RESOURCE_MGR_H__

/* External includes */
#include "include/adapter_service_interface.h"
#include "include/topology_mgr_interface.h"
#include "include/display_path_interface.h"
#include "include/gpu_interface.h"
#include "include/link_service_interface.h"
#include "include/clock_source_interface.h"

/* Internal includes */
#include "tm_internal_types.h"
#include "tm_resource.h"


/* Forward declarations */
struct tm_resource_mgr;
struct dal_context;

/** TM Resource Manager initialisation data */
struct tm_resource_mgr_init_data {
	struct dal_context *dal_context;
	struct adapter_service *as;
};

/**************************************************
 *  Public data structures and macro definitions.
 **************************************************/

#define RESOURCE_INVALID_INDEX ((uint32_t)(-1))

struct tm_resource_range {
	uint32_t start;
	uint32_t end;
};

/****************************
  Public interface functions
*****************************/

/** Call to create the TM Resource Manager */
struct tm_resource_mgr*
tm_resource_mgr_create(struct tm_resource_mgr_init_data *init_data);

/** Call to destroy the TM Resource Manager */
void tm_resource_mgr_destroy(struct tm_resource_mgr **tm_rm);

void tm_resource_mgr_release_hw(struct tm_resource_mgr *tm_rm);

struct tm_resource_mgr *tm_resource_mgr_clone(
		struct tm_resource_mgr *tm_rm);

/* Try to add an object, and if successful, return pointer to tm_resource,
 * where the object was stored. */
struct tm_resource *dal_tm_resource_mgr_add_resource(
	struct tm_resource_mgr *tm_rm,
	struct tm_resource *tm_resource_input);

struct tm_resource*
tm_resource_mgr_add_engine(
		struct tm_resource_mgr *tm_rm,
		enum engine_id engine);

/** Sort the resource list - for faster search. */
void tm_resource_mgr_reindex(struct tm_resource_mgr *tm_rm);

void tm_resource_mgr_relink_encoders(struct tm_resource_mgr *tm_rm);

struct tm_resource*
tm_resource_mgr_find_resource(
		struct tm_resource_mgr *tm_rm,
		struct graphics_object_id obj);

struct tm_resource*
tm_resource_mgr_get_resource(
		struct tm_resource_mgr *tm_rm,
		enum object_type obj_type,
		uint32_t index);

struct tm_resource*
tm_resource_mgr_enum_resource(
		struct tm_resource_mgr *tm_rm,
		uint32_t index);

/* Get number of resources of a certain type. */
uint32_t tm_resource_mgr_get_resources_num(
		struct tm_resource_mgr *tm_rm,
		enum object_type obj_type);

/* Get total number of resources. */
uint32_t tm_resource_mgr_get_total_resources_num(
		struct tm_resource_mgr *tm_rm);

/* Acquire resources which are in the display_path */
enum tm_result tm_resource_mgr_acquire_resources(
		struct tm_resource_mgr *tm_rm,
		struct display_path *path,
		enum tm_acquire_method method);

void tm_resource_mgr_release_resources(
		struct tm_resource_mgr *tm_rm,
		struct display_path *path,
		enum tm_acquire_method method);

enum tm_result tm_resource_mgr_acquire_alternative_clock_source(
		struct tm_resource_mgr *tm_rm,
		struct display_path *path);
void tm_resource_mgr_release_alternative_clock_source(
		struct tm_resource_mgr *tm_rm,
		struct display_path *path);
bool tm_resource_mgr_is_alternative_clk_src_available(
		struct tm_resource_mgr *tm_rm,
		struct display_path *path);
void tm_resource_mgr_reset_all_usage_counters(
		struct tm_resource_mgr *tm_rm);

struct tm_resource*
tm_resource_mgr_get_stereo_sync_resource(
		struct tm_resource_mgr *tm_rm,
		struct display_path *path);

struct tm_resource*
tm_resource_mgr_get_sync_output_resource(
		struct tm_resource_mgr *tm_rm,
		struct display_path *path);

struct tm_resource*
tm_resource_mgr_get_available_sync_output_for_display_path(
		struct tm_resource_mgr *tm_rm,
		struct display_path *path,
		enum sync_source sync_output);

uint32_t tm_resource_mgr_get_crtc_index_for_display_path(
		struct tm_resource_mgr *tm_rm,
		struct display_path *path,
		uint32_t exclude_mask);

void tm_resource_mgr_set_gpu_interface(
		struct tm_resource_mgr *tm_rm,
		struct gpu *gpu);
struct gpu *tm_resource_mgr_get_gpu_interface(
		struct tm_resource_mgr *tm_rm);

enum tm_result tm_resource_mgr_attach_audio_to_display_path(
		struct tm_resource_mgr *tm_rm,
		struct display_path *display_path,
		enum signal_type sig_type);
void tm_resource_mgr_detach_audio_from_display_path(
		struct tm_resource_mgr *tm_rm,
		struct display_path *display_path);
uint32_t tm_resource_mgr_get_active_audio_resources_num(
		struct tm_resource_mgr *tm_rm);


enum tm_result tm_resource_mgr_setup_link_storage(
		struct tm_resource_mgr *tm_rm,
		uint32_t number_of_paths);
void tm_resource_mgr_invalidate_link_services(
		struct tm_resource_mgr *tm_rm);
void tm_resource_mgr_release_all_link_services(
		struct tm_resource_mgr *tm_rm);
void tm_resource_mgr_release_path_link_services(
		struct tm_resource_mgr *tm_rm,
		struct display_path *path);

enum tm_result tm_resource_mgr_add_link_service(
		struct tm_resource_mgr *tm_rm,
		struct display_path *path,
		uint32_t link_index,
		struct link_service *ls_interface);

struct link_service *tm_resource_mgr_get_link_service(
		struct tm_resource_mgr *tm_rm,
		struct display_path *path,
		uint32_t link_index,
		enum signal_type sig_type);

struct link_service *tm_resource_mgr_find_link_service(
		struct tm_resource_mgr *tm_rm,
		struct display_path *path,
		enum signal_type sig_type);

void tm_resource_mgr_swap_link_services(
		struct tm_resource_mgr *tm_rm,
		uint32_t display_index1,
		uint32_t display_index2);

void tm_resource_mgr_associate_link_services(
		struct tm_resource_mgr *tm_rm,
		struct display_path *path);

void dal_tmrm_dump(struct tm_resource_mgr *tm_rm);

uint32_t tm_resource_mgr_get_display_path_index_for_controller(
		struct tm_resource_mgr *tm_rm,
		enum controller_id controller_id);

uint32_t tm_resource_mgr_ref_counter_decrement(
	const struct tm_resource_mgr *tm_rm,
	struct tm_resource *tm_resource);

struct controller *dal_tmrm_get_free_controller(
		struct tm_resource_mgr *tm_rm,
		uint32_t *controller_index_out,
		uint32_t exclude_mask);

void dal_tmrm_acquire_controller(struct tm_resource_mgr *tm_rm,
		struct display_path *display_path,
		uint32_t controller_idx,
		enum tm_acquire_method method);

void dal_tmrm_release_controller(
		struct tm_resource_mgr *tm_rm,
		struct display_path *display_path,
		enum tm_acquire_method method,
		struct controller *controller);

void dal_tmrm_release_non_root_controllers(
		struct tm_resource_mgr *tm_rm,
		struct display_path *display_path,
		enum tm_acquire_method method);

void dal_tmrm_set_resources_range_by_type(struct tm_resource_mgr *tm_rm);

const struct tm_resource_range *dal_tmrm_get_resource_range_by_type(
	struct tm_resource_mgr *tm_rm,
	enum object_type type);

#endif /* __DAL_TM_RESOURCE_MGR_H__ */
