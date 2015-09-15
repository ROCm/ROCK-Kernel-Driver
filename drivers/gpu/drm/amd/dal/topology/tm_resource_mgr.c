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

#include "include/connector_interface.h"
#include "include/encoder_interface.h"
#include "include/controller_interface.h"
#include "include/audio_interface.h"
#include "include/controller_interface.h"
#include "include/dcs_interface.h"
#include "include/ddc_service_interface.h"
#include "include/vector.h"
#include "include/flat_set.h"

#include "tm_resource_mgr.h"
#include "tm_internal_types.h"
#include "tm_utils.h"

/*****************************************************************************
 *	private data structures
 ***************************************************************************/

struct tm_resource_mgr {
	struct dal_context *dal_context;
	struct adapter_service *as;

	struct vector *link_services;
	/* This is the number of paths as set by
	 * tm_resource_mgr_setup_link_storage(). */
	uint32_t link_services_number_of_paths;

	struct gpu *gpu_interface;

	bool prioritize_controllers;

	struct flat_set *resources;

	struct tm_resource_range resources_range[OBJECT_TYPE_COUNT];

	/* This lookup will be used to translate IRQ source to Display Index.
	 * The translation will occur on every Vblank interrupt. */
	uint32_t controller_to_display_path_lookup[CONTROLLER_ID_MAX + 1];

	/* If true - this RM (Resource Manager) was cloned from the
	 * one-and-only original RM which was created during TM creation.
	 * A cloned RM should not destroy objects which it copied
	 * from the original RM because the original RM continues to
	 * reference them. */
	bool is_cloned;

	bool pipe_power_gating_enabled;
};

/* local macro definitions */
/* TODO: check if this is too much/not enough */
#define TM_RM_MAX_NUM_OF_RESOURCES 100

DAL_VECTOR_AT_INDEX(link_services, struct link_service **)
DAL_VECTOR_SET_AT_INDEX(link_services, struct link_service **)

DAL_FLAT_SET_AT_INDEX(tmrm_resources, struct tm_resource **)
DAL_FLAT_SET_INSERT(tmrm_resources, struct tm_resource **)

/*****************************************************************************
 *	function prototypes
 ***************************************************************************/

static void tmrm_release_clock_source(
		struct tm_resource_mgr *tm_rm,
		struct display_path *display_path,
		struct clock_source *clock_source,
		enum tm_acquire_method method);


/*****************************************************************************
 *	private functions
 ***************************************************************************/
static uint32_t tm_resource_ref_counter_increment(
	const struct tm_resource_mgr *tm_rm,
	struct tm_resource *tm_resource)
{
	struct dal_context *dal_context = tm_rm->dal_context;
	uint32_t current_count = TM_RES_REF_CNT_GET(tm_resource);

	if (current_count != 0  &&
		!tm_resource->funcs->is_sharable(tm_resource)) {
		/* Ideally, we should allow only 0-->1 transition.
		 *
		 * But, in theory a resource could be sharable, this is
		 * why we treat it as a Warning.
		 *
		 * In practice, we get here if resource usage is unbalanced!
		 *
		 * Do NOT remove this warning unless you are absolutely sure
		 * that it should be removed! */
		TM_WARNING("%s: increment a non-zero count: %d?\n",
				__func__, current_count);
		ASSERT(false);
	}

	TM_RES_REF_CNT_INCREMENT(tm_resource);

	return TM_RES_REF_CNT_GET(tm_resource);
}

uint32_t tm_resource_mgr_ref_counter_decrement(
	const struct tm_resource_mgr *tm_rm,
	struct tm_resource *tm_resource)
{
	struct dal_context *dal_context = tm_rm->dal_context;
	uint32_t current_count = TM_RES_REF_CNT_GET(tm_resource);

	if (current_count != 1 &&
		!tm_resource->funcs->is_sharable(tm_resource)) {
		/* Ideally, we should allow only 1-->0 transition.
		 *
		 * But, in theory a resource could be sharable, this is
		 * why we treat it as a Warning.
		 *
		 * In practice, we get here if resource usage is unbalanced!
		 *
		 * Do NOT remove this warning unless you are absolutely sure
		 * that it should be removed! */
		TM_WARNING("%s: decrement a non-one count: %d?\n",
				__func__, current_count);
		ASSERT(false);
	}

	TM_RES_REF_CNT_DECREMENT(tm_resource);

	return TM_RES_REF_CNT_GET(tm_resource);
}


static bool is_resource_available(const struct tm_resource *tm_resource)
{
	if (TM_RES_REF_CNT_GET(tm_resource) == 0)
		return true;

	return false;
}

/**
 * Returns true if during acquire we need to change HW state
 * in display path context.
 *
 * It is very important that we don't change HW State during
 * cofunctional validation!
 *
 * Note: even if this function returns 'true', the action may still depend
 * on the value of "resource reference count".
 * Most important cases is the transition of "reference count" from 0 to 1 and
 * from 1 to 0.
 *
 * \param [in] method: How to acquire resources/How resources were acquired
 *
 * \return true: if during acquire we need to activate resources,
 *	false: otherwise
 */
static inline bool update_hw_state_needed(enum tm_acquire_method method)
{
	return (method == TM_ACQUIRE_METHOD_HW);
}

static bool tm_rm_less_than(
	const void *tgt_item,
	const void *ref_item);

static enum tm_result tm_resource_mgr_construct(struct tm_resource_mgr *tm_rm)
{
	struct dal_context *dal_context = tm_rm->dal_context;
	struct flat_set_init_data init;

	init.capacity = TM_RM_MAX_NUM_OF_RESOURCES;
	init.funcs.less_than = tm_rm_less_than;
	init.struct_size = sizeof(struct tm_resource *);
	/* Note that Resource array will store pointers to structures,
	 * not structures. */
	tm_rm->resources = dal_flat_set_create(&init);

	if (NULL == tm_rm->resources) {
		TM_ERROR("%s: failed to create 'resources' vector!\n",
				__func__);
		return TM_RESULT_FAILURE;
	}

	dal_memset(tm_rm->controller_to_display_path_lookup,
			INVALID_DISPLAY_INDEX,
			sizeof(tm_rm->controller_to_display_path_lookup));

	/* This flag is for "Tiled Pipes power gating" feature. */
	tm_rm->prioritize_controllers = true;

	tm_rm->pipe_power_gating_enabled =
			dal_adapter_service_is_feature_supported(
					FEATURE_POWER_GATING_PIPE_IN_TILE);

	TM_PWR_GATING("Display Pipe Power Gating option: %s\n",
		(tm_rm->pipe_power_gating_enabled == true ? "Enabled" :
				"Disabled"));

	return TM_RESULT_SUCCESS;
}

/** create the TM Resource Manager */
struct tm_resource_mgr*
tm_resource_mgr_create(struct tm_resource_mgr_init_data *init_data)
{
	struct tm_resource_mgr *tm_rm;
	struct dal_context *dal_context = init_data->dal_context;

	TM_IFACE_TRACE();

	tm_rm = dal_alloc(sizeof(*tm_rm));

	if (!tm_rm) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	tm_rm->dal_context = init_data->dal_context;
	tm_rm->as = init_data->as;

	if (TM_RESULT_FAILURE == tm_resource_mgr_construct(tm_rm)) {
		dal_free(tm_rm);
		return NULL;
	}

	return tm_rm;
}

static struct tm_resource *tmrm_display_path_find_connector_resource(
		struct tm_resource_mgr *tm_rm,
		struct display_path *display_path)
{
	struct tm_resource *tm_resource;
	struct connector *connector;
	struct graphics_object_id id;
	struct dal_context *dal_context = tm_rm->dal_context;

	connector = dal_display_path_get_connector(display_path);

	TM_ASSERT(connector != NULL);

	if (NULL == connector) {
		/* can't proceed */
		return NULL;
	}

	id = dal_connector_get_graphics_object_id(connector);

	tm_resource = tm_resource_mgr_find_resource(tm_rm, id);

	TM_ASSERT(tm_resource != NULL);

	return tm_resource;
}

static struct tm_resource *tmrm_display_path_find_upstream_resource(
		struct tm_resource_mgr *tm_rm,
		struct display_path *display_path,
		uint32_t link_idx)
{
	struct tm_resource *tm_resource;
	struct encoder *encoder;
	struct graphics_object_id id;
	struct dal_context *dal_context = tm_rm->dal_context;

	encoder = dal_display_path_get_upstream_object(display_path,
			link_idx);

	if (NULL == encoder) {
		/* not necessarily an error */
		return NULL;
	}

	id = dal_encoder_get_graphics_object_id(encoder);

	tm_resource = tm_resource_mgr_find_resource(tm_rm, id);

	TM_ASSERT(tm_resource != NULL);

	return tm_resource;
}

/**
 * Find TM resource which contains pointer to Audio object currently set
 * on a path.
 *
 * \return NULL: 1. Path has no Audio set on it 2. Audio is set on path,
 *		but no matching TM Resource found (should never happen).
 *
 *	tm_resource pointer: the TM resource for audio object
 */
static struct tm_resource *tmrm_display_path_find_audio_resource(
		struct tm_resource_mgr *tm_rm,
		struct display_path *display_path,
		uint32_t link_idx)
{
	struct tm_resource *tm_resource;
	struct audio *audio;
	struct graphics_object_id id;
	struct dal_context *dal_context = tm_rm->dal_context;

	audio = dal_display_path_get_audio_object(display_path,	link_idx);

	if (NULL == audio) {
		/* not necessarily an error */
		return NULL;
	}

	id = dal_audio_get_graphics_object_id(audio);

	tm_resource = tm_resource_mgr_find_resource(tm_rm, id);

	TM_ASSERT(tm_resource != NULL);

	return tm_resource;
}

static struct tm_resource *tmrm_find_clock_source_resource(
		struct tm_resource_mgr *tm_rm,
		const struct clock_source *clock_source)
{
	struct tm_resource *tm_resource;
	struct graphics_object_id id;
	struct dal_context *dal_context = tm_rm->dal_context;

	id = dal_clock_source_get_graphics_object_id(clock_source);

	tm_resource = tm_resource_mgr_find_resource(tm_rm, id);

	TM_ASSERT(tm_resource != NULL);

	return tm_resource;
}

static struct tm_resource *tmrm_display_path_find_alternative_clock_resource(
		struct tm_resource_mgr *tm_rm,
		struct display_path *display_path)
{
	struct tm_resource *tm_resource;
	struct clock_source *clock_source;
	struct dal_context *dal_context = tm_rm->dal_context;

	clock_source = dal_display_path_get_alt_clock_source(display_path);

	if (NULL == clock_source) {
		/* not necessarily an error */
		return NULL;
	}

	tm_resource = tmrm_find_clock_source_resource(tm_rm, clock_source);

	/* should not happen */
	TM_ASSERT(tm_resource != NULL);

	return tm_resource;
}

static struct link_service *tmrm_get_ls_at_index(struct tm_resource_mgr *tm_rm,
		uint32_t index)
{
	struct link_service **link_service_item;
	struct link_service *link_service;
	struct dal_context *dal_context = tm_rm->dal_context;

	link_service_item = link_services_vector_at_index(
			tm_rm->link_services, index);

	if (NULL == link_service_item) {
		TM_ERROR("%s: no item at index:%d!\n", __func__, index);
		return NULL;
	}

	link_service = *link_service_item;

	return link_service;
}

static void tmrm_set_ls_at_index(struct tm_resource_mgr *tm_rm, uint32_t index,
		struct link_service *link_service)
{
	link_services_vector_set_at_index(tm_rm->link_services,
			&link_service, index);
}

static void tm_resource_mgr_destruct(struct tm_resource_mgr *tm_rm)
{
	uint32_t count;
	uint32_t index;
	struct tm_resource *resource;
	/* Only the original RM should delete these objects. */
	if (false == tm_rm->is_cloned) {
		tm_resource_mgr_release_all_link_services(tm_rm);
	}

	/* Go over all entries in tm_rm->resource_vector
	 * and destroy 'struct tm_resource->go_interface' */
	count = tm_resource_mgr_get_total_resources_num(tm_rm);

	for (index = 0; index < count; index++) {
		resource = tm_resource_mgr_enum_resource(tm_rm, index);
		resource->funcs->destroy(&resource);
	}

	/* Delete vectors which stored the objects, if any. */
	if (tm_rm->resources != NULL)
		dal_flat_set_destroy(&tm_rm->resources);

	if (tm_rm->link_services != NULL)
		dal_vector_destroy(&tm_rm->link_services);

}

/** destroy the TM Resource Manager */
void tm_resource_mgr_destroy(struct tm_resource_mgr **tm_rm)
{
	if (!tm_rm || !*tm_rm) {
		BREAK_TO_DEBUGGER();
		return;
	}
	tm_resource_mgr_destruct(*tm_rm);
	dal_free(*tm_rm);
	*tm_rm = NULL;
}

static void tmrm_resource_release_hw(struct tm_resource_mgr *tm_rm,
		struct tm_resource *tm_resource)
{
	struct dal_context *dal_context = tm_rm->dal_context;

	if (tm_resource == NULL) {
		TM_ERROR("%s: invalid input!\n", __func__);
		return;
	}

	tm_resource->funcs->release_hw(tm_resource);
}

void tm_resource_mgr_release_hw(struct tm_resource_mgr *tm_rm)
{
	uint32_t i;
	struct link_service *link_service;
	struct tm_resource *tm_resource;

	if (tm_rm->link_services != NULL) {

		/* 1. Call Link Services to release HW access */
		for (i = 0;
			i < dal_vector_get_count(tm_rm->link_services);
			i++) {

			link_service = tmrm_get_ls_at_index(tm_rm, i);

			if (link_service != NULL)
				dal_ls_release_hw(link_service);
		}
	}

	/* 2. Call GPU and all GPU sub-components to release HW access */
	if (tm_rm->gpu_interface != NULL)
		dal_gpu_release_hw(tm_rm->gpu_interface);

	/* 3. Release HW access on all graphics objects */
	for (i = 0; i < tm_resource_mgr_get_total_resources_num(tm_rm); i++) {
		tm_resource = tm_resource_mgr_enum_resource(tm_rm, i);
		tmrm_resource_release_hw(tm_rm, tm_resource);
	}
}

/**
 * Clones Resource Manager and resets usage counters.
 *
 * Cloned resources should have exactly same functionality, except the fact,
 * that during acquire/release it should not update DCS state (this is
 * temporary, ideally DCS should not be touched at all).
 *
 * \return Pointer to cloned Resource Manager
 */
struct tm_resource_mgr *tm_resource_mgr_clone(
		struct tm_resource_mgr *tm_rm_other)
{
	struct dal_context *dal_context = tm_rm_other->dal_context;
	struct tm_resource_mgr *tm_rm_new;
	struct tm_resource_mgr_init_data init_data;
	bool error = false;
	uint32_t i = 0;
	uint32_t count;

	dal_memset(&init_data, 0, sizeof(init_data));

	init_data.as = tm_rm_other->as;
	init_data.dal_context = tm_rm_other->dal_context;

	do {
		tm_rm_new = tm_resource_mgr_create(&init_data);
		if (NULL == tm_rm_new) {
			TM_ERROR("%s: failed to create new TMRM!\n", __func__);
			error = true;
			break;
		}

		/* 1. we should properly deallocate vectors as we will clone
		 * them from original resource manager in point 3 */

		dal_flat_set_destroy(&tm_rm_new->resources);

		/* 2. do the 'shallow' copy */
		dal_memmove(tm_rm_new, tm_rm_other, sizeof(*tm_rm_other));

		/* It doesn't matter if the 'other' is cloned or not,
		 * what is important is that the new one is cloned. */
		tm_rm_new->is_cloned = true;

		/* 3. clone the vectors */
		/********** link services vector **********/
		tm_rm_new->link_services = dal_vector_clone(
				tm_rm_other->link_services);

		if (NULL == tm_rm_new->link_services) {
			TM_ERROR("%s: failed to clone LS vector!\n", __func__);
			error = true;
			break;
		}

		/********* resources set ***********/

		{
			struct flat_set_init_data init_data;

			init_data.capacity = TM_RM_MAX_NUM_OF_RESOURCES;
			init_data.funcs.less_than = tm_rm_less_than;
			init_data.struct_size = sizeof(struct tm_resource *);
			tm_rm_new->resources = dal_flat_set_create(&init_data);
		}
		count = dal_flat_set_get_count(tm_rm_other->resources);
		for (i = 0; i < count; ++i) {
			struct tm_resource *resource =
				*tmrm_resources_set_at_index(
					tm_rm_other->resources,
					i);
			resource = resource->funcs->clone(resource);
			tmrm_resources_set_insert(
				tm_rm_new->resources,
				&resource);
		}

	} while (0);

	tm_resource_mgr_reset_all_usage_counters(tm_rm_new);

	if (true == error) {
		/* Note that all vectors will be automatically
		 * destroyed by tm_resource_mgr_destroy() */
		if (tm_rm_new)
			tm_resource_mgr_destroy(&tm_rm_new);

		return NULL;
	}

	return tm_rm_new;
}

/**
 * Compares two resources. Returns true if tgt_item less then ref_item,
 * false otherwise.
 * Compares resource properties in the following order:
 *  1. Resource type     (encoder, audio, etc.)
 *  2. Resource priority (whithin type it allows to have internal logic
 *			to sort resources)
 *  3. Resource id       (DAC, DVO, Uniphy, etc.)
 *  4. Resource enum     (every object with same ID can have multiple
 *			instances - enums)
 *
 *  \param [in] tgt_item: Object to compare
 *  \param [in] ref_item: Object compare with
 *
 *  \return true if tgt_item less then ref_item, false otherwise
 */
static bool tm_rm_less_than(const void *lhs, const void *rhs)
{
	const struct tm_resource *tgt_item =
		*((const struct tm_resource **)lhs);
	const struct tm_resource *ref_item =
		*((const struct tm_resource **)rhs);
	uint32_t tgt_priority = 0;
	uint32_t ref_priority = 0;
	enum object_type tgt_type;
	enum object_type ref_type;
	uint32_t tgt_id;
	uint32_t ref_id;
	enum object_enum_id tgt_enum;
	enum object_enum_id ref_enum;

	tgt_type = GRPH_ID(tgt_item).type;
	ref_type = GRPH_ID(ref_item).type;

	tgt_id = GRPH_ID(tgt_item).id;
	ref_id = GRPH_ID(ref_item).id;

	tgt_enum = GRPH_ID(tgt_item).enum_id;
	ref_enum = GRPH_ID(ref_item).enum_id;

	/* 1. Compare Resource type (encoder, audio, etc.) */
	if (tgt_type < ref_type)
		return true;
	if (tgt_type > ref_type)
		return false;

	tgt_priority = tgt_item->funcs->get_priority(tgt_item);
	ref_priority = ref_item->funcs->get_priority(ref_item);
	/* 2. Compare Resource priority (within type it allows to have
	 * internal logic to sort resources) */
	if (tgt_priority < ref_priority)
		return true;
	if (tgt_priority > ref_priority)
		return false;

	/* 3. Compare Resource id (DAC, DVO, Uniphy, etc.) */
	if (tgt_id < ref_id)
		return true;
	if (tgt_id > ref_id)
		return false;

	/* 4. Compare Resource enum (every object with same ID can have
	 *  multiple instances - enums) */
	if (tgt_enum < ref_enum)
		return true;
	if (tgt_enum > ref_enum)
		return false;

	return false;
}

struct tm_resource *dal_tm_resource_mgr_add_resource(
	struct tm_resource_mgr *tm_rm,
	struct tm_resource *tm_resource_input)
{
	uint32_t count;
	struct tm_resource **tm_resource_output;
	struct dal_context *dal_context = tm_rm->dal_context;

	if (NULL == tm_resource_input) {
		/* We can get here if memory allocation for resource failed. */
		TM_ERROR("%s: Can not add a NULL resource!\n",	__func__);
		return NULL;
	}

	if (false == dal_graphics_object_id_is_valid(
		GRPH_ID(tm_resource_input))) {
		/* Some objects are "artificial" (not from BIOS parser)
		 * and may be invalid. TODO: add exceptions for such
		 * objects in dal_graphics_object_id_is_valid(). */
		TM_RESOURCES("%s: invalid object id!\n", __func__);
	}

	tm_resource_output =
		tmrm_resources_set_insert(tm_rm->resources, &tm_resource_input);

	if (!tm_resource_output || !*tm_resource_output) {
		TM_ERROR("%s: resource storage reached maximum capacity!\n",
				__func__);
		return NULL;
	}

	count = tm_resource_mgr_get_total_resources_num(tm_rm);

	TM_RESOURCES("Added Resource (total %02d): %s\n",
		count,
		tm_utils_get_tm_resource_str(*tm_resource_output));

	return *tm_resource_output;
}

/**
 * Add engine resource to repository (engine does not have an object, only ID)
 *
 * \param [in] engine: engine object ID
 *
 * \return
 *      Pointer to added resource on success, NULL otherwise
 */
struct tm_resource *tm_resource_mgr_add_engine(
	struct tm_resource_mgr *tm_rm,
	enum engine_id engine)
{
	struct graphics_object_id id;

	if (engine >= ENGINE_ID_COUNT)
		return NULL;

	id = dal_graphics_object_id_init(engine, ENUM_ID_1, OBJECT_TYPE_ENGINE);

	return dal_tm_resource_mgr_add_resource(tm_rm,
		dal_tm_resource_engine_create(id));
}

/**
 * Links encoder resources. Our HW unites encoders into pairs which may have
 * implicit dependency.
 * This dependency takes impact (depends on signal as well) when calculating
 * cofunctional paths.
 * That's why we need to know for each encoder what is the paired one
 */
void tm_resource_mgr_relink_encoders(struct tm_resource_mgr *tm_rm)
{
	uint32_t i;
	uint32_t pair;
	struct tm_resource *encoder_rsrc;
	struct tm_resource *paired_encoder_rsrc;
	enum transmitter paired_transmitter_id;
	struct encoder *encoder;
	struct encoder *paired_encoder;

	const struct tm_resource_range *range =
		dal_tmrm_get_resource_range_by_type(tm_rm, OBJECT_TYPE_ENCODER);

	for (i = range->start; i < range->end; i++) {

		encoder_rsrc = tm_resource_mgr_enum_resource(tm_rm, i);

		encoder = TO_ENCODER(encoder_rsrc);

		paired_transmitter_id = dal_encoder_get_paired_transmitter(
				encoder);

		TO_ENCODER_INFO(encoder_rsrc)->paired_encoder_index =
				RESOURCE_INVALID_INDEX;

		if (paired_transmitter_id == TRANSMITTER_UNKNOWN ||
				paired_transmitter_id >= TRANSMITTER_COUNT) {
			/* there is no paired transmitter, so
			 * nothing to pair with */
			continue;
		}

		for (pair = range->start; pair < range->end; pair++) {

			paired_encoder_rsrc = tm_resource_mgr_enum_resource(
					tm_rm, pair);

			paired_encoder = TO_ENCODER(paired_encoder_rsrc);

			if (dal_encoder_get_transmitter(paired_encoder)
					== paired_transmitter_id) {
				/* both have the same transmitter - found it */
				TO_ENCODER_INFO(encoder_rsrc)->
					paired_encoder_index = pair;
				break;
			}
		} /* for () */
	} /* for () */
}

/**
 * Returns true if this path requires stereo mixer controller attached.
 *
 * \param [in] display_path: display path to be acquired
 *
 * \return true: if this path require stereo mixer controller attached,
 *	false: otherwise
 */
#if 0
static bool tmrm_need_stereo_mixer_controller(
		const struct display_path *display_path)
{
	struct dcs *dcs;
	struct dcs_stereo_3d_features row_interleave;
	struct dcs_stereo_3d_features column_interleave;
	struct dcs_stereo_3d_features pixel_interleave;

	dcs = dal_display_path_get_dcs(display_path);

	if (NULL == dcs)
		return false;

	row_interleave = dal_dcs_get_stereo_3d_features(dcs,
			TIMING_3D_FORMAT_ROW_INTERLEAVE);

	column_interleave =  dal_dcs_get_stereo_3d_features(dcs,
			TIMING_3D_FORMAT_COLUMN_INTERLEAVE);

	pixel_interleave  = dal_dcs_get_stereo_3d_features(dcs,
			TIMING_3D_FORMAT_PIXEL_INTERLEAVE);

	if (row_interleave.flags.SUPPORTED ||
		column_interleave.flags.SUPPORTED ||
		pixel_interleave.flags.SUPPORTED) {

		return true;
	}

	return false;
}
#endif

/**
 * Lookup for resources identified by objectID.
 *
 * \param [in] object: object for which to look for TM resource.
 *
 * \return Pointer to requested resource on success, NULL otherwise.
 */
struct tm_resource*
tm_resource_mgr_find_resource(
		struct tm_resource_mgr *tm_rm,
		struct graphics_object_id object)
{
	uint32_t i;
	struct tm_resource *tm_resource;

	for (i = 0; i < tm_resource_mgr_get_total_resources_num(tm_rm); i++) {

		tm_resource = tm_resource_mgr_enum_resource(tm_rm, i);

		if (true == dal_graphics_object_id_is_equal(
				GRPH_ID(tm_resource), object))
			return tm_resource;
	}

	/* If we got here, resource not found. */
	return NULL;
}

/**
 * Returns requested resource for given index - this index is global and may
 * address any type of resource.
 * Normally this method used when one wants to iterate over all objects.
 *
 * \param [in] index: Index in resource database
 *
 * \return Pointer to requested resource on success, NULL otherwise
 */
struct tm_resource*
tm_resource_mgr_enum_resource(
		struct tm_resource_mgr *tm_rm,
		uint32_t index)
{
	struct dal_context *dal_context = tm_rm->dal_context;

	if (index >= tm_resource_mgr_get_total_resources_num(tm_rm)) {
		TM_ERROR("%s: index out of boundary: %d\n", __func__, index);
		return NULL;
	}

	return *tmrm_resources_set_at_index(tm_rm->resources, index);
}

/**
 *
 * Returns total number of resources (regardless of type)
 *
 * \return  Total number of resources
 */
uint32_t tm_resource_mgr_get_total_resources_num(
		struct tm_resource_mgr *tm_rm)
{
	struct dal_context *dal_context = tm_rm->dal_context;

	if (NULL == tm_rm->resources) {
		TM_ERROR("%s: resources list is NULL!\n", __func__);
		return 0;
	}

	return dal_flat_set_get_count(tm_rm->resources);
}

static
struct tm_resource*
tm_resource_mgr_find_engine_resource(
		struct tm_resource_mgr *tm_rm,
		enum engine_id engine_id)
{
	struct dal_context *dal_context = tm_rm->dal_context;
	uint32_t i;
	struct tm_resource *tm_resource;
	const struct tm_resource_range *engines =
		dal_tmrm_get_resource_range_by_type(tm_rm, OBJECT_TYPE_ENGINE);

	for (i = engines->start; i < engines->end; i++) {

		tm_resource = tm_resource_mgr_enum_resource(tm_rm, i);

		if (GRPH_ID(tm_resource).id == engine_id) {
			/* We can ignore 'id.enum' for engine
			 * because it is not used. */
			return tm_resource;
		}
	}

	/* If we got here, resource not found. */
	TM_WARNING("%s: Engine '%d' not found!\n", __func__, engine_id);

	return NULL;
}

/**
 * Verifies permanent resources required for given display path are available.
 *
 * \param [in] display_path: Display path for which we verify resource
 *	availability.
 *
 * \return	true: if resources are available,
 *		false: otherwise
 */
static bool tmrm_resources_available(struct tm_resource_mgr *tm_rm,
		struct display_path *display_path)
{
	struct tm_resource *tm_resource;
	struct tm_resource *tm_paired_resource;
	bool is_dual_link_signal;
	uint32_t i;
	struct dal_context *dal_context = tm_rm->dal_context;

	if (display_path == NULL) {
		TM_ERROR("%s: invalid state or input data!\n", __func__);
		return false;
	}

	is_dual_link_signal = dal_is_dual_link_signal(
			dal_display_path_get_query_signal(display_path,
					SINK_LINK_INDEX));

	/* Check for connector availability */
	tm_resource = tmrm_display_path_find_connector_resource(tm_rm,
			display_path);

	if (tm_resource == NULL) {
		TM_ERROR("%s: Failed to fetch Connector resource!\n",
				__func__);
		return false;
	}

	if (TM_RES_REF_CNT_GET(tm_resource) > 0
			&& !tm_resource->flags.mst_resource) {

		TM_WARNING("%s: Connector resource NOT available! ref_count:%d\n",
				__func__,
				TM_RES_REF_CNT_GET(tm_resource));
		return false;
	}

	/* Check for encoder availability */
	for (i = 0; i < dal_display_path_get_number_of_links(display_path);
			i++) {

		tm_resource = tmrm_display_path_find_upstream_resource(tm_rm,
				display_path, i);

		if (tm_resource == NULL) {
			TM_ERROR("%s: Failed to fetch Resource!\n", __func__);
			return false;
		}

		/* Primary resource is busy */
		if (TM_RES_REF_CNT_GET(tm_resource) > 0 &&
				!tm_resource->flags.mst_resource) {
			TM_WARNING("%s: Encoder resource NOT available! ref_count:%d, Link Index:%d\n",
				__func__,
				TM_RES_REF_CNT_GET(tm_resource),
				i);
			return false;
		}

		/* Get secondary/paired resource */
		tm_paired_resource = NULL;

		if (is_dual_link_signal
			&& TO_ENCODER_INFO(tm_resource)->paired_encoder_index !=
					RESOURCE_INVALID_INDEX) {

			TM_ASSERT(!tm_resource->flags.mst_resource);

			tm_paired_resource = tm_resource_mgr_enum_resource(
				tm_rm,
				TO_ENCODER_INFO(tm_resource)->
				paired_encoder_index);
		}

		if (tm_paired_resource != NULL &&
			TM_RES_REF_CNT_GET(tm_paired_resource) > 0) {
			/* Paired resource required, but is busy */
			TM_WARNING("%s: Paired resource is busy! Link Index:%d\n",
					__func__, i);
			return false;
		}
	} /* for() */

	/* Stereosync encoder will be present only on already acquired path */
	tm_resource = tm_resource_mgr_get_stereo_sync_resource(tm_rm,
			display_path);

	if (tm_resource != NULL && TM_RES_REF_CNT_GET(tm_resource) > 0) {
		TM_WARNING("%s: Stereosync encoder resource is busy!\n",
				__func__);
		return false;
	}

	/* Sync-output encoder will be present only on already acquired path */
	tm_resource = tm_resource_mgr_get_sync_output_resource(tm_rm,
			display_path);

	if (tm_resource != NULL && TM_RES_REF_CNT_GET(tm_resource) > 0) {
		TM_WARNING("%s: Sync-output encoder resource is busy!\n",
				__func__);
		return false;
	}

	/* No need to check GLSync Connector resources - it is never
	 * acquired within display path and never intersects with other
	 * resources */

	return true;
}


/**
 * Obtains resource index of available controller.
 *
 * \param [in] display_path: Display path for which we look available controller
 * \param [in] exclude_mask:  which controllers should be excluded
 *
 * \return index if available controller, RESOURCE_INVALID_INDEX otherwise
 */
static uint32_t dal_tmrm_find_controller_for_display_path(
		struct tm_resource_mgr *tm_rm,
		uint32_t exclude_mask)
{
	struct dal_context *dal_context = tm_rm->dal_context;
	uint32_t controller_res_ind = RESOURCE_INVALID_INDEX;
	uint32_t i;
	struct tm_resource *tm_resource;
	const struct tm_resource_range *controllers =
		dal_tmrm_get_resource_range_by_type(
			tm_rm,
			OBJECT_TYPE_CONTROLLER);

	for (i = controllers->start; i < controllers->end; i++) {

		tm_resource = tm_resource_mgr_enum_resource(tm_rm, i);

		if (tm_utils_test_bit(&exclude_mask, GRPH_ID(tm_resource).id))
			continue;

		if (TM_RES_REF_CNT_GET(tm_resource) > 0) {
			/* already acquired */
			continue;
		}

		/* found a free Pirmary (non-underlay) controller */
		controller_res_ind = i;
		break;
	} /* for() */

	if (controller_res_ind == RESOURCE_INVALID_INDEX) {
		/* That means we ran out of controllers. */
		TM_WARNING("%s:Failed to find a free Controller!\n", __func__);
	}

	return controller_res_ind;
}

/**
 * Obtains Resource index of available clock source for given display path
 *
 * \param [in] display_path: Display path for which we look available
 *				clock source
 * \param [in] method:       How to acquire resources
 *
 * \return index if available clock source, RESOURCE_INVALID_INDEX otherwise
 */
static uint32_t tmrm_get_available_clock_source(
		struct tm_resource_mgr *tm_rm,
		struct display_path *display_path,
		enum tm_acquire_method method)
{
	struct dal_context *dal_context = tm_rm->dal_context;
	struct encoder *encoder;
	uint32_t i;
	struct tm_resource *tm_resource;
	struct clock_source *clock_source;
	enum clock_source_id clock_source_id;
	enum clock_sharing_group clock_sharing_group;
	enum signal_type signal;
	enum clock_sharing_level clock_sharing_level;
	const struct tm_resource_range *clock_sources;

	TM_ASSERT(display_path != NULL);

	/* Obtain encoder most closest to GPU (we need to know if this encoder
	 * supports selected clock source) */
	encoder = dal_display_path_get_upstream_object(display_path,
			ASIC_LINK_INDEX);

	if (encoder == NULL)
		return RESOURCE_INVALID_INDEX;

	clock_sharing_group = dal_display_path_get_clock_sharing_group(
			display_path);

	clock_sources =
		dal_tmrm_get_resource_range_by_type(
			tm_rm,
			OBJECT_TYPE_CLOCK_SOURCE);

	/* Round 1: Try to find already used (in shared mode) Clock Source */
	if (clock_sharing_group != CLOCK_SHARING_GROUP_EXCLUSIVE) {

		for (i = clock_sources->start; i < clock_sources->end; i++) {

			tm_resource = tm_resource_mgr_enum_resource(tm_rm, i);

			clock_source = TO_CLOCK_SOURCE(tm_resource);

			clock_source_id = dal_clock_source_get_id(clock_source);

			if (!dal_encoder_is_clock_source_supported(encoder,
					clock_source_id)) {
				/* not for this encoder */
				continue;
			}

			if (clock_sharing_group ==
				TO_CLOCK_SOURCE_INFO(tm_resource)->
				clk_sharing_group) {
				/* resource is found */
				return i;
			}

		} /* for() */
	} /* if() */

	/* Round 2: If shared Clock Source was not found - try to find
	 *	available Clock Source */
	for (i = clock_sources->start; i < clock_sources->end; i++) {

		tm_resource = tm_resource_mgr_enum_resource(tm_rm, i);

		if (false == is_resource_available(tm_resource))
			continue;

		clock_source = TO_CLOCK_SOURCE(tm_resource);

		signal = dal_display_path_get_query_signal(display_path,
				ASIC_LINK_INDEX);

		if (!dal_clock_source_is_output_signal_supported(clock_source,
				signal))
			continue;

		clock_sharing_level = dal_clock_souce_get_clk_sharing_lvl(
				clock_source);

		if (tm_utils_is_clock_sharing_mismatch(clock_sharing_level,
				clock_sharing_group))
			continue;

		clock_source_id = dal_clock_source_get_id(clock_source);

		if (!dal_encoder_is_clock_source_supported(encoder,
				clock_source_id))
			continue;

		/* Finally we passed all verifications, this clock source
		 * is valid for use */
		return i;
	} /* for () */

	/* not found */
	TM_WARNING("%s: no clk src found!\n", __func__);
	return RESOURCE_INVALID_INDEX;
}

/**
 * Obtains Engine ID of available engine for given display path
 *
 * \param [in] display_path: Display path for which we look available engine
 * \param [in] method:       How to acquire resources
 *
 * \return ID of engine, if available. ENGINE_ID_UNKNOWN otherwise.
 */
enum engine_id tmrm_get_available_stream_engine(
		struct tm_resource_mgr *tm_rm,
		struct display_path *display_path,
		enum tm_acquire_method method)
{
	struct tm_resource *tm_resource = NULL;
	enum tm_engine_priority restricted_priority;
	enum tm_engine_priority best_priority;
	struct encoder *encoder;
	enum engine_id preferred_engine_id = ENGINE_ID_UNKNOWN;
	enum engine_id available_engine_id = ENGINE_ID_UNKNOWN;
	union supported_stream_engines supported_stream_engines;
	uint32_t i;
	struct dal_context *dal_context = tm_rm->dal_context;
	const struct tm_resource_range *engines =
		dal_tmrm_get_resource_range_by_type(tm_rm, OBJECT_TYPE_ENGINE);

	TM_ASSERT(display_path != NULL);

	/* Define restricted priority (which cannot be used for this path,
	 * but all higher priorities are good). */
	if (dal_display_path_get_query_signal(display_path, SINK_LINK_INDEX)
			== SIGNAL_TYPE_DISPLAY_PORT_MST) {
		/* restricted to be MST capable */
		restricted_priority = TM_ENGINE_PRIORITY_NON_MST_CAPABLE;
	} else {
		/* no real restriction */
		restricted_priority = TM_ENGINE_PRIORITY_UNKNOWN;
	}

	/* Start with invalid priority */
	best_priority = restricted_priority;

	/* We assign engine only to first encoder - most close to GPU */
	encoder = dal_display_path_get_upstream_object(display_path,
			ASIC_LINK_INDEX);

	if (encoder == NULL) {
		TM_ERROR("%s: no Encoder!\n", __func__);
		return RESOURCE_INVALID_INDEX;
	}

	/* First try preferred engine */
	preferred_engine_id = dal_encoder_get_preferred_stream_engine(encoder);

	if (preferred_engine_id != ENGINE_ID_UNKNOWN) {

		/* Use preferred engine as available engine for now */
		available_engine_id = preferred_engine_id;

		for (i = engines->start; i < engines->end; i++) {

			tm_resource = tm_resource_mgr_enum_resource(tm_rm, i);

			if (GRPH_ID(tm_resource).id == preferred_engine_id)
				break;
		}

		if (is_resource_available(tm_resource) &&
				TO_ENGINE_INFO(tm_resource)->priority <
				best_priority) {
			/* found a free resource with a higher priority */
			best_priority = TO_ENGINE_INFO(tm_resource)->priority;
		}
	}

	/* If preferred engine not available - pick supported
	 * engine with highest priority */
	if (best_priority >= restricted_priority) {

		supported_stream_engines =
				dal_encoder_get_supported_stream_engines(
							encoder);

		for (i = engines->start; i < engines->end; i++) {
			tm_resource = tm_resource_mgr_enum_resource(tm_rm, i);

			if (false == tm_utils_is_supported_engine(
					supported_stream_engines,
					GRPH_ID(tm_resource).id))
				continue;

			if (is_resource_available(tm_resource) &&
				TO_ENGINE_INFO(tm_resource)->priority <
				best_priority) {
				/* found a non-preferred engine */
				available_engine_id = GRPH_ID(tm_resource).id;
				best_priority =
					TO_ENGINE_INFO(tm_resource)->priority;
			}

		} /* for() */
	}

	if (best_priority < restricted_priority) {
		/* We picked a valid engine - return it's ID to caller */
		return available_engine_id;
	}

	TM_ERROR("%s: no stream engine found!\n", __func__);
	return ENGINE_ID_UNKNOWN;
}

static void tmrm_acquire_encoder(
		struct tm_resource_mgr *tm_rm,
		struct display_path *display_path,
		uint32_t link_idx)
{
	enum signal_type signal = dal_display_path_get_query_signal(
			display_path, link_idx);
	struct tm_resource *tm_resource = NULL;
	struct tm_resource *tm_paired_resource = NULL;
	struct dal_context *dal_context = tm_rm->dal_context;
	bool is_dual_link_signal;

	tm_resource = tmrm_display_path_find_upstream_resource(tm_rm,
			display_path, link_idx);
	TM_ASSERT(tm_resource != NULL);

	dal_display_path_set_link_active_state(display_path, link_idx, true);

	is_dual_link_signal = dal_is_dual_link_signal(signal);

	tm_resource_ref_counter_increment(tm_rm, tm_resource);

	tm_resource->flags.mst_resource =
				(signal == SIGNAL_TYPE_DISPLAY_PORT_MST);

	/* Paired resource required - acquire it as well.
	 * In current design, we do not program paired resources -
	 * only need to handle confunctional enumeration properly */
	if (is_dual_link_signal &&
			TO_ENCODER_INFO(tm_resource)->paired_encoder_index !=
					RESOURCE_INVALID_INDEX) {

		TM_ASSERT(!tm_resource->flags.mst_resource);

		tm_paired_resource = tm_resource_mgr_enum_resource(
				tm_rm,
				TO_ENCODER_INFO(tm_resource)->
				paired_encoder_index);

		tm_resource_ref_counter_increment(tm_rm, tm_paired_resource);
	}
}

static void tmrm_acquire_audio(
		struct tm_resource_mgr *tm_rm,
		struct display_path *display_path,
		uint32_t link_idx)
{
	struct tm_resource *tm_resource;

	tm_resource = tmrm_display_path_find_audio_resource(tm_rm,
			display_path, link_idx);

	if (tm_resource != NULL) {
		/* Audio reference count already updated when it was attached
		 * to display path - need only to activate */
		dal_display_path_set_audio_active_state(display_path, link_idx,
				true);
	}
}

/**
 * Acquires resources associated with given link
 * Assumes resources are available
 *
 * \param [in] display_path:	Display path for which to acquire resources
 * \param [in] link_idx:	Index of link with which resources associated
 * \param [in] method:		How to acquire resources
 */
static void tmrm_acquire_link(
		struct tm_resource_mgr *tm_rm,
		struct display_path *display_path,
		uint32_t link_idx,
		enum tm_acquire_method method)
{
	tmrm_acquire_encoder(tm_rm, display_path, link_idx);

	tmrm_acquire_audio(tm_rm, display_path, link_idx);
}

/**
 * Does power gating on controller.
 * Returns true if logical power state is updated.
 * The physical  power gating state is updated into controller object through
 * the call PowerGatingEnable.
 *
 * \param [out]  tm_resource: TM resource to be modified. Of type controller.
 * \param [in ]  method:    How to acquire resources
 * \param [in ]  enable:    Boolean parameter. If it is true - enable power
 *				gating, false - disable.
 */
static void tmrm_do_controller_power_gating(
		struct tm_resource_mgr *tm_rm,
		struct tm_resource *tm_resource,
		enum tm_acquire_method method,
		bool enable)
{
	uint32_t ref_counter;
	struct dal_context *dal_context = tm_rm->dal_context;

	TM_ASSERT(GRPH_ID(tm_resource).type == OBJECT_TYPE_CONTROLLER);

	if (false == update_hw_state_needed(method))
		return;

	ref_counter = TM_RES_REF_CNT_GET(tm_resource);

	if (enable == true) {

		if (false == tm_rm->pipe_power_gating_enabled) {
			TM_PWR_GATING("Pipe PG Feature disabled  --> not gating.\n");
			return;
		}

		if (ref_counter != 0) {
			TM_WARNING("%s: Can NOT power gate with non-zero reference counter:%d!\n",
				__func__, ref_counter);
			return;
		}

		/* This is the fist statefull acquisition.
		 * It must have logical state of "not power gated". */
		if (TO_CONTROLLER_INFO(tm_resource)->power_gating_state !=
				TM_POWER_GATE_STATE_OFF) {
			TM_WARNING("%s: Invalid state:%d! (expected TM_POWER_GATE_STATE_OFF)\n",
				__func__,
				TO_CONTROLLER_INFO(tm_resource)->
				power_gating_state);
			return;
		}

		dal_controller_power_gating_enable(TO_CONTROLLER(tm_resource),
				PIPE_GATING_CONTROL_ENABLE);

		TO_CONTROLLER_INFO(tm_resource)->power_gating_state =
				TM_POWER_GATE_STATE_ON;

		TM_PWR_GATING("Gated Controller: %s(%d)\n",
			tm_utils_go_id_to_str(GRPH_ID(tm_resource)),
			dal_graphics_object_id_get_controller_id(
					GRPH_ID(tm_resource)));
		return;
	}

	if (enable == false) {

		if (ref_counter != 1) {
			/* Un-gate only once! */
			TM_WARNING("%s: Can NOT un-gate with reference counter '%d' note equal to one!\n",
				__func__, ref_counter);
			return;
		}

		/* Un-gate the pipe, if NOT un-gated already. */
		if (TO_CONTROLLER_INFO(tm_resource)->power_gating_state !=
				TM_POWER_GATE_STATE_ON) {
			TM_WARNING("%s: Invalid state:%d! (expected TM_POWER_GATE_STATE_ON)\n",
				__func__,
				TO_CONTROLLER_INFO(tm_resource)->
				power_gating_state);
			return;
		}

		dal_controller_power_gating_enable(TO_CONTROLLER(tm_resource),
				PIPE_GATING_CONTROL_DISABLE);

		/* TODO: 'power_gating_state' flag is set in many places, but
		 * it should be set *only* by this function. */
		TO_CONTROLLER_INFO(tm_resource)->power_gating_state =
				TM_POWER_GATE_STATE_OFF;

		TM_PWR_GATING("Un-Gated Controller: %s(%d)\n",
			tm_utils_go_id_to_str(GRPH_ID(tm_resource)),
			dal_graphics_object_id_get_controller_id(
					GRPH_ID(tm_resource)));
	}
}

uint32_t tm_resource_mgr_get_display_path_index_for_controller(
		struct tm_resource_mgr *tm_rm,
		enum controller_id controller_id)
{
	uint32_t display_index;
	struct dal_context *dal_context = tm_rm->dal_context;

	if (controller_id < CONTROLLER_ID_D0
			|| controller_id > CONTROLLER_ID_MAX) {

		TM_ERROR("%s: Invalid controller_id:%d !\n",
				__func__, controller_id);
		return INVALID_DISPLAY_INDEX;
	}

	display_index = tm_rm->controller_to_display_path_lookup[controller_id];

	/*TM_RESOURCES("ctrlr-to-path:%s(%d)->%02d",
			tm_utils_controller_id_to_str(controller_id),
			controller_id,
			display_index);*/

	return display_index;
}

static void tmrm_update_controller_to_path_lookup_table(
		struct tm_resource_mgr *tm_rm,
		struct tm_resource *tm_resource,
		struct display_path *display_path)
{
	enum controller_id controller_id;
	uint32_t display_index;
	struct dal_context *dal_context = tm_rm->dal_context;

	if (GRPH_ID(tm_resource).type != OBJECT_TYPE_CONTROLLER) {
		TM_ERROR("%s: NOT a controller resource!\n", __func__);
		return;
	}

	controller_id = GRPH_ID(tm_resource).id;

	if (controller_id < CONTROLLER_ID_D0 ||
			controller_id > CONTROLLER_ID_MAX) {

		TM_ERROR("%s: Invalid controller_id:%d !\n",
				__func__, controller_id);
		return;
	}

	if (NULL == display_path) {
		/* this is a request to clear the slot */
		tm_rm->controller_to_display_path_lookup[controller_id] =
				INVALID_DISPLAY_INDEX;
		TM_RESOURCES("clearing-ctrlr idx:%s(%d)\n",
				tm_utils_controller_id_to_str(controller_id),
				controller_id);
		return;
	}

	display_index = dal_display_path_get_display_index(display_path);

	tm_rm->controller_to_display_path_lookup[controller_id] = display_index;

	TM_RESOURCES("path-to-ctrlr:%02d->:%s(%d)\n",
			display_index,
			tm_utils_controller_id_to_str(controller_id),
			controller_id);
}

/**
 * Acquires controller to display path. Controller index should be valid
 * (in scope and available for this path)
 *
 * \param [in] display_path: Display path for which we want to attach controller
 * \param [in] controller_idx: Index of controller in resource database
 * \param [in] method:       How to acquire resources
 */
void dal_tmrm_acquire_controller(
		struct tm_resource_mgr *tm_rm,
		struct display_path *display_path,
		uint32_t controller_idx,
		enum tm_acquire_method method)
{
	struct tm_resource *tm_resource;
	uint32_t display_index;
	struct dal_context *dal_context = tm_rm->dal_context;

	TM_ASSERT(display_path != NULL);

	display_index = dal_display_path_get_display_index(display_path);

	tm_resource = tm_resource_mgr_enum_resource(tm_rm, controller_idx);
	if (NULL == tm_resource) {
		TM_ERROR("%s: Path[%02d]:controller resource not found!\n",
				__func__, display_index);
		return;
	}

	if (1 == tm_resource_ref_counter_increment(tm_rm, tm_resource)
			&& update_hw_state_needed(method)) {

		TM_CONTROLLER_ASN("Path[%02d]: Acquired: Controller: %s(%d)\n",
			display_index,
			tm_utils_go_id_to_str(GRPH_ID(tm_resource)),
			dal_graphics_object_id_get_controller_id(
				GRPH_ID(tm_resource)));

		tmrm_update_controller_to_path_lookup_table(tm_rm, tm_resource,
				display_path);

		/* If controller was grabbed for set/reset mode operations,
		 * we disable power gating on this controller. */
		tmrm_do_controller_power_gating(tm_rm, tm_resource, method,
				false);
	}
}


/**
 * Acquires clock source to display path. Clock source index should be valid
 * (in scope and available for this path).
 *
 * \param [in] display_path: Display path for which we want to attach clock
 *				source
 * \param [in] clk_index:  Index or clock source in resource database
 * \param [in] method:     How to acquire resources
 */
static void tmrm_acquire_clock_source(
		struct tm_resource_mgr *tm_rm,
		struct display_path *display_path,
		uint32_t clk_index)
{
	struct tm_resource *tm_resource;
	struct dal_context *dal_context = tm_rm->dal_context;

	TM_ASSERT(display_path != NULL);

	tm_resource = tm_resource_mgr_enum_resource(tm_rm, clk_index);

	if (GRPH_ID(tm_resource).type != OBJECT_TYPE_CLOCK_SOURCE) {
		TM_ERROR("%s: invalid resource type at index:%d!\n", __func__,
			clk_index);
		return;
	}

	dal_display_path_set_clock_source(display_path,
			TO_CLOCK_SOURCE(tm_resource));

	TO_CLOCK_SOURCE_INFO(tm_resource)->clk_sharing_group =
			dal_display_path_get_clock_sharing_group(display_path);

	tm_resource_ref_counter_increment(tm_rm, tm_resource);
}


/**
 * Acquires engine to display path. Engine index should be valid (in scope and
 * available for this path)
 *
 * \param [in] display_path: Display path for which we want to attach engine
 * \param [in] engine_id:    ID of engine to acquire
 * \param [in] method:       How to acquire resources
 */
static void tmrm_acquire_stream_engine(
		struct tm_resource_mgr *tm_rm,
		struct display_path *display_path,
		enum engine_id engine_id)
{
	struct dal_context *dal_context = tm_rm->dal_context;
	struct tm_resource *tm_rsrc;
	struct tm_resource *tm_rsrc_upstream;

	TM_ASSERT(display_path != NULL);

	/* We assign engine only to first active encoder - most close to GPU */
	tm_rsrc_upstream = tmrm_display_path_find_upstream_resource(tm_rm,
			display_path, ASIC_LINK_INDEX);

	if (NULL == tm_rsrc_upstream) {
		TM_WARNING("%s: no engine resource!\n", __func__);
		return;
	}

	if (GRPH_ID(tm_rsrc_upstream).type != OBJECT_TYPE_ENCODER) {
		TM_ERROR("%s: upstream resource is NOT an encoder!\n",
				__func__);
		return;
	}

	TM_ASSERT(engine_id >= ENGINE_ID_DIGA);
	TM_ASSERT(engine_id <= ENGINE_ID_COUNT);

	tm_rsrc = tm_resource_mgr_find_engine_resource(tm_rm, engine_id);
	if (NULL == tm_rsrc) {
		TM_ERROR("%s: failed to find engine (0x%X) resource!\n",
				__func__, engine_id);
		return;
	}

	tm_resource_ref_counter_increment(tm_rm, tm_rsrc);

	dal_display_path_set_stream_engine(display_path, ASIC_LINK_INDEX,
			engine_id);

	TM_ENG_ASN("Path[%02d]: Acquired StreamEngine=%s(%u) Transmitter=%s\n",
		dal_display_path_get_display_index(display_path),
		tm_utils_engine_id_to_str(engine_id),
		engine_id,
		tm_utils_transmitter_id_to_str(GRPH_ID(tm_rsrc_upstream)));
}

/**
 * Releases engine if such was acquired on display path.
 *
 * \param [in] display_path: Display path from which we want to detach engine
 * \param [in] method:       How resources were ACQUIRED
 */
static void tmrm_release_stream_engine(
		struct tm_resource_mgr *tm_rm,
		struct display_path *display_path)
{
	enum engine_id engine_id;
	struct tm_resource *tm_resource;
	struct dal_context *dal_context = tm_rm->dal_context;

	engine_id = dal_display_path_get_stream_engine(display_path,
				ASIC_LINK_INDEX);
	if (ENGINE_ID_UNKNOWN == engine_id) {
		/* Most likely tmrm_acquire_stream_engine() was not called. */
		TM_ERROR("%s: engine NOT set!\n", __func__);
		return;
	}

	tm_resource = tm_resource_mgr_find_engine_resource(tm_rm, engine_id);
	if (NULL == tm_resource) {
		TM_ERROR("%s: failed to find engine (0x%X) resource!\n",
				__func__, engine_id);
		return;
	}

	dal_display_path_set_stream_engine(display_path, ASIC_LINK_INDEX,
			ENGINE_ID_UNKNOWN);

	tm_resource_mgr_ref_counter_decrement(tm_rm, tm_resource);

	TM_ENG_ASN("Path[%02d]: Released StreamEngine=%s(%u)\n",
		dal_display_path_get_display_index(display_path),
		tm_utils_engine_id_to_str(engine_id),
		engine_id);
}

void dal_tmrm_release_non_root_controllers(
		struct tm_resource_mgr *tm_rm,
		struct display_path *display_path,
		enum tm_acquire_method method)
{
	struct dal_context *dal_context = tm_rm->dal_context;
	struct display_path_plane *path_plane;
	uint8_t planes_no;
	uint8_t i;

	planes_no = dal_display_path_get_number_of_planes(display_path);

	/* in this situation i can become 255, which will output strange
	 * message. This case need to be debugged */
	if (planes_no == 0)
		return;
	/*
	 * iterate over non-root >1 controllers
	 */

	for (i = planes_no-1; i > 0; i--) {

		/*
		 * release controller (and resources)
		 */

		path_plane = dal_display_path_get_plane_at_index(
				display_path, i);

		if (path_plane == NULL) {
			TM_ERROR("%s: Plane at %d is not set!\n", __func__, i);
			return;
		}

		dal_tmrm_release_controller(tm_rm, display_path,
				method,
				path_plane->controller);
	}
}

/**
 * Releases controller, if such was acquired on display path.
 *
 * \param [in] display_path: Display path from which we want to detach
 *				controller
 * \param [in] method:       How resources were ACQUIRED
 */
void dal_tmrm_release_controller(
		struct tm_resource_mgr *tm_rm,
		struct display_path *display_path,
		enum tm_acquire_method method,
		struct controller *controller)
{
	struct dal_context *dal_context = tm_rm->dal_context;
	struct tm_resource *tm_rsrc;
	struct graphics_object_id id;
	uint32_t display_index;

	display_index = dal_display_path_get_display_index(display_path);

	id = dal_controller_get_graphics_object_id(controller);

	tm_rsrc = tm_resource_mgr_find_resource(tm_rm, id);
	if (tm_rsrc == NULL) {
		TM_ERROR("%s: no resource for controller!\n", __func__);
		return;
	}

	if (tm_resource_mgr_ref_counter_decrement(tm_rm, tm_rsrc) == 0) {

		if (update_hw_state_needed(method)) {

			tmrm_do_controller_power_gating(
					tm_rm,
					tm_rsrc,
					method,
					true);

			tmrm_update_controller_to_path_lookup_table(
					tm_rm,
					tm_rsrc,
					NULL);

			TM_CONTROLLER_ASN("Path[%02d]: Released: Controller: %s(%d)\n",
				display_index,
				tm_utils_go_id_to_str(GRPH_ID(tm_rsrc)),
				dal_graphics_object_id_get_controller_id(
						GRPH_ID(tm_rsrc)));
		}
	}
}

static void tmrm_acquire_connector(
		struct tm_resource_mgr *tm_rm,
		struct display_path *display_path)
{
	struct dal_context *dal_context = tm_rm->dal_context;
	struct tm_resource *tm_resource;

	tm_resource = tmrm_display_path_find_connector_resource(tm_rm,
			display_path);

	TM_ASSERT(tm_resource != NULL);

	tm_resource_ref_counter_increment(tm_rm, tm_resource);

	tm_resource->flags.mst_resource =
			(dal_display_path_get_query_signal(display_path,
				SINK_LINK_INDEX) ==
					SIGNAL_TYPE_DISPLAY_PORT_MST);
}

static void tmrm_acquire_stereo_sync(
		struct tm_resource_mgr *tm_rm,
		struct display_path *display_path)
{
	struct tm_resource *tm_resource;

	tm_resource = tm_resource_mgr_get_stereo_sync_resource(tm_rm,
			display_path);

	if (tm_resource != NULL)
		tm_resource_ref_counter_increment(tm_rm, tm_resource);
}

static void tmrm_acquire_sync_output(
		struct tm_resource_mgr *tm_rm,
		struct display_path *display_path)
{
	struct tm_resource *tm_resource;

	tm_resource = tm_resource_mgr_get_sync_output_resource(tm_rm,
		display_path);

	if (tm_resource != NULL)
		tm_resource_ref_counter_increment(tm_rm, tm_resource);
}

static void tmrm_acquire_alternative_clock(
		struct tm_resource_mgr *tm_rm,
		struct display_path *display_path)
{
	struct tm_resource *tm_resource;

	tm_resource = tmrm_display_path_find_alternative_clock_resource(
			tm_rm, display_path);

	if (tm_resource != NULL) {

		TO_CLOCK_SOURCE_INFO(tm_resource)->clk_sharing_group =
			dal_display_path_get_clock_sharing_group(display_path);
		tm_resource_ref_counter_increment(tm_rm, tm_resource);
	}
}

enum tm_result tmrm_add_root_plane(
		struct tm_resource_mgr *tm_rm,
		struct display_path *display_path,
		uint32_t controller_index)
{
	struct dal_context *dal_context = tm_rm->dal_context;
	struct controller *controller;
	struct display_path_plane plane;
	struct tm_resource *tm_resource;

	if (dal_display_path_get_number_of_planes(display_path) != 0) {
		ASSERT(false);
		TM_ERROR(
		"%s: Path Should NOT have any planes! [Path: %d]\n",
			__func__,
			dal_display_path_get_display_index(
					display_path));
		return TM_RESULT_FAILURE;
	}

	/* TODO: add real 'plane' initialisation here, based on
	 * parameters passed in. */
	dal_memset(&plane, 0, sizeof(plane));

	tm_resource = tm_resource_mgr_enum_resource(tm_rm, controller_index);

	controller = TO_CONTROLLER_INFO(tm_resource)->controller;

	plane.controller = controller;

	/* We checked that path has no planes, it means we are adding the 'root'
	 * plane. */
	if (false == dal_display_path_add_plane(display_path, &plane))
		return TM_RESULT_FAILURE;

	return TM_RESULT_SUCCESS;
}

/**
 * Acquires resources for requested display path
 * Exception - Audio resource acquired/released automatically on
 * connect/disconnect.
 * Here we only activate audio if required and such acquired
 *
 * It is OK to use TM_ACQUIRE_METHOD_SW on a path which is already
 * acquired by TM_ACQUIRE_METHOD_HW (because it is a noop).
 *
 * It is *not* OK to use TM_ACQUIRE_METHOD_HW on a path which is already
 * acquired by TM_ACQUIRE_METHOD_SW because many HW-update actions depend on
 * resource usage counter transitions from 1-to-0 and from 0-to-1.
 * If this is done, then tmrm_resources_available() will fail and this function
 * will fail too.
 *
 * \param [in] display_path:	Display path for which to acquire resources
 * \param [in] method:	How to acquire resources
 *
 * \return	TM_RESULT_SUCCESS: resources were successfully acquired
 *		TM_RESULT_FAILURE: otherwise
 */
enum tm_result tm_resource_mgr_acquire_resources(
		struct tm_resource_mgr *tm_rm,
		struct display_path *display_path,
		enum tm_acquire_method method)
{
	uint32_t controller_index;
	uint32_t clock_source_index;
	enum engine_id engine_id;
	uint32_t i;
	struct dal_context *dal_context = tm_rm->dal_context;

	if (display_path == NULL) {
		TM_ERROR("%s: invalid state or input data!\n", __func__);
		return TM_RESULT_FAILURE;
	}

	if (true == dal_display_path_is_acquired(display_path)) {
		if (update_hw_state_needed(method)) {
			/* If display path is already acquired - increment
			 * reference counter so it could be balanced by
			 * tm_resource_mgr_release_resources() */
			dal_display_path_acquire(display_path);
		} else {
			/* Do nothing because for SW acquire all resources
			 * already there. */
		}

		return TM_RESULT_SUCCESS;
	}

	/* Verify that all resources (which are PERMANENT to display path) are
	 * available. */
	if (false == tmrm_resources_available(tm_rm, display_path))
		return TM_RESULT_FAILURE;

	/* Obtain indexes of available resources which are NOT permanent
	 * to display path. */
	controller_index = dal_tmrm_find_controller_for_display_path(tm_rm, 0);
	if (controller_index == RESOURCE_INVALID_INDEX)
		return TM_RESULT_FAILURE;

	clock_source_index = tmrm_get_available_clock_source(tm_rm,
			display_path, method);
	if (clock_source_index == RESOURCE_INVALID_INDEX)
		return TM_RESULT_FAILURE;

	engine_id = tmrm_get_available_stream_engine(tm_rm, display_path,
			method);
	if (engine_id == ENGINE_ID_UNKNOWN)
		return TM_RESULT_FAILURE;

	/*****************************************************************
	 * At this point we know that all required resources are available
	 * and we know IDs/indexes of these.
	 * Some of the resources are already in display_path (for example
	 * the connector), but from Resources point of view the acquisition
	 * was not done yet, and this is what will be done.
	 *****************************************************************/

	tmrm_acquire_connector(tm_rm, display_path);

	/* Acquire links (encoder, audio) */
	for (i = 0;
		i < dal_display_path_get_number_of_links(display_path);
		i++) {
		tmrm_acquire_link(tm_rm, display_path, i, method);
	}

	tmrm_acquire_stereo_sync(tm_rm, display_path);

	tmrm_acquire_sync_output(tm_rm, display_path);

	/* NOTE: GLSync Connector resources never acquired within
	 * display path and never intersects with other resources */

	/* In the context of checking co-func set, if the alternative
	 * clock source is attached to display path, there is a need
	 * to acquire its resource. */
	tmrm_acquire_alternative_clock(tm_rm, display_path);

	/* Acquire temporary, but mandatory components - CAN NOT fail,
	 * since we confirmed availability of resources at the beginning of
	 * this function. */
	dal_tmrm_acquire_controller(tm_rm, display_path, controller_index,
			method);

	tmrm_acquire_clock_source(tm_rm, display_path, clock_source_index);

	tmrm_acquire_stream_engine(tm_rm, display_path, engine_id);

	if (TM_RESULT_SUCCESS != tmrm_add_root_plane(tm_rm, display_path,
			controller_index)) {
		TM_ERROR("%s: failed to add 'root' plane!\n", __func__);
		return TM_RESULT_FAILURE;
	}

	dal_display_path_acquire_links(display_path);

	if (update_hw_state_needed(method)) {
		dal_display_path_acquire(display_path);
	}

	return TM_RESULT_SUCCESS;
}

/**
 * Releases a resource and its pair.
 *
 * \param [in] resource:       Resource to release
 * \param [in] pPairedResource: Paired resource to release
 */
static void tmrm_release_resource(struct tm_resource_mgr *tm_rm,
		struct tm_resource *resource,
		struct tm_resource *paired_resource)
{
	/* Release Resource and clear MST flag for main resource. */
	if (resource != NULL &&
		tm_resource_mgr_ref_counter_decrement(tm_rm,
					resource) == 0) {
		/* it was the last reference */
		resource->flags.mst_resource = false;
	}

	/* Release Resource and clear MST flag for paired resource. */
	if (paired_resource != NULL &&
		tm_resource_mgr_ref_counter_decrement(tm_rm,
					paired_resource) == 0) {
		/* it was the last reference */
		paired_resource->flags.mst_resource = false;
	}
}

static void tmrm_release_stereo_sync_resource(
	struct tm_resource_mgr *tm_rm,
	struct display_path *display_path)
{
	struct tm_resource *resource = NULL;

	/* Stereosync encoder will be present only on
	 * already acquired path. */
	resource = tm_resource_mgr_get_stereo_sync_resource(tm_rm,
			display_path);

	tmrm_release_resource(tm_rm, resource, NULL);
}

static void tmrm_release_sync_output_resource(
	struct tm_resource_mgr *tm_rm,
	struct display_path *display_path)
{
	struct tm_resource *resource = NULL;

	/* Sync-output encoder will be present only on already acquired path */
	resource = tm_resource_mgr_get_sync_output_resource(tm_rm,
			display_path);

	tmrm_release_resource(tm_rm, resource, NULL);
}

static void tmrm_release_connector_resource(
	struct tm_resource_mgr *tm_rm,
	struct display_path *display_path)
{
	struct tm_resource *resource;
	struct connector *connector;

	connector = dal_display_path_get_connector(display_path);

	resource = tm_resource_mgr_find_resource(tm_rm,
		dal_connector_get_graphics_object_id(connector));

	tmrm_release_resource(tm_rm, resource, NULL);
}

static void tmrm_release_encoder_resource(struct tm_resource_mgr *tm_rm,
		struct display_path *display_path,
		uint32_t link_idx)
{
	struct tm_resource *resource = NULL;
	struct tm_resource *paired_resource = NULL;
	struct encoder *encoder;
	bool is_dual_link_signal;
	enum signal_type sink_signal;
	uint32_t paired_encoder_index;
	uint32_t display_index;
	struct dal_context *dal_context = tm_rm->dal_context;

	display_index = dal_display_path_get_display_index(display_path);

	TM_RESOURCES("%s: display_index:%d, link_idx:%d\n", __func__,
			display_index, link_idx);

	sink_signal = dal_display_path_get_query_signal(display_path,
			SINK_LINK_INDEX);

	is_dual_link_signal = dal_is_dual_link_signal(sink_signal);

	encoder = dal_display_path_get_upstream_object(display_path, link_idx);

	if (encoder) {
		resource = tm_resource_mgr_find_resource(tm_rm,
			dal_encoder_get_graphics_object_id(encoder));

		paired_encoder_index =
			TO_ENCODER_INFO(resource)->paired_encoder_index;

		if (is_dual_link_signal && resource != NULL
				&& paired_encoder_index
						!= RESOURCE_INVALID_INDEX) {

			TM_ASSERT(!resource->flags.mst_resource);

			paired_resource = tm_resource_mgr_enum_resource(tm_rm,
					paired_encoder_index);
		}

		tmrm_release_resource(tm_rm, resource, paired_resource);
	}
}

static void tmrm_release_link_service_resources(struct tm_resource_mgr *tm_rm,
		struct display_path *display_path)
{
	uint32_t i;
	uint32_t links_num;

	links_num = dal_display_path_get_number_of_links(display_path);

	/* Release links (encoder) Audio release on
	 * connect/disconnected. */
	for (i = 0; i < links_num; i++) {
		tmrm_release_encoder_resource(tm_rm, display_path, i);
	}
}

/**
 * Releases resources which were acquired for given display path.
 * Exception - Audio resource acquired/released automatically
 *		on connect/disconnect.
 *
 * \param [in] display_path: Display path on which to release resources
 * \param [in] method:       How resources were ACQUIRED
 */
void tm_resource_mgr_release_resources(
		struct tm_resource_mgr *tm_rm,
		struct display_path *display_path,
		enum tm_acquire_method method)
{
	struct dal_context *dal_context = tm_rm->dal_context;

	if (display_path == NULL) {
		TM_ERROR("%s: invalid state or input data!\n", __func__);
		return;
	}

	if (true == dal_display_path_is_acquired(display_path)) {
		if (update_hw_state_needed(method)) {
			if (dal_display_path_get_ref_counter(
					display_path) > 1) {
				/* We get here when handling HPD-Disconnect.
				 * It is ok because Path is double-acquired:
				 * 1st acquire - to drive the display.
				 * 2nd acquire - to detect the display.
				 *
				 * Decrement reference counter of the path. */
				dal_display_path_release(display_path);
				/* We can NOT release path-resources because
				 * someone is still using it.*/
				return;
			}
		} else {
			/* Path is "in-use" at HW level. We should NOT change
			 * its state. */
			return;
		}
	}

	tmrm_release_stream_engine(tm_rm, display_path);

	/* Clock source should be released before controller. */
	tmrm_release_clock_source(tm_rm, display_path,
			dal_display_path_get_clock_source(display_path),
			method);

	tmrm_release_clock_source(tm_rm, display_path,
			dal_display_path_get_alt_clock_source(display_path),
			method);

	tmrm_release_stereo_sync_resource(tm_rm, display_path);

	tmrm_release_sync_output_resource(tm_rm, display_path);

	tmrm_release_connector_resource(tm_rm, display_path);

	tmrm_release_link_service_resources(tm_rm, display_path);

	/* Deactivate all resources */
	if (update_hw_state_needed(method)) {

		/* Release ALL Planes, including the "root" one.
		 * This is different from "dal_tm_release_plane_resources()"
		 * where we release only NON-ROOT planes. */

		/* Non-root MUST be released BEFORE root because
		 * dal_tmrm_release_non_root_controllers() will NOT release
		 * the 1st controller in the vector.
		 * If we don't do it in this order will "leak" a controller.
		 * (because it is falsely considered a 'root') */
		dal_tmrm_release_non_root_controllers(
				tm_rm,
				display_path,
				method);

		dal_display_path_release(display_path);
	}

	/* this will release 'root' controller */
	dal_tmrm_release_controller(
			tm_rm,
			display_path,
			method,
			dal_display_path_get_controller(display_path));

	dal_display_path_release_resources(display_path);
}

/**
 * Acquire alternative ClockSource on display path with appropriate group
 * sharing level.
 *
 * \param [in] display_path: Display path to which ClockSource will be attached
 *
 * \return	TM_RESULT_SUCCESS: if additional ClockSource was successfully
 *			attached to requested display path
 *		TM_RESULT_FAILURE: otherwise
 */
enum tm_result tm_resource_mgr_acquire_alternative_clock_source(
		struct tm_resource_mgr *tm_rm,
		struct display_path *display_path)
{
	uint32_t clock_source_index;
	struct tm_resource *tm_resource;
	struct dal_context *dal_context = tm_rm->dal_context;

	TM_ASSERT(display_path != NULL);

	/* Find an appropriate clock source. */
	clock_source_index = tmrm_get_available_clock_source(tm_rm,
			display_path, TM_ACQUIRE_METHOD_HW);
	if (clock_source_index == RESOURCE_INVALID_INDEX)
		return TM_RESULT_FAILURE;

	/* the actual acquiring */
	tm_resource = tm_resource_mgr_enum_resource(tm_rm, clock_source_index);

	TO_CLOCK_SOURCE_INFO(tm_resource)->clk_sharing_group =
		dal_display_path_get_clock_sharing_group(display_path);

	tm_resource_ref_counter_increment(tm_rm, tm_resource);

	dal_display_path_set_alt_clock_source(display_path,
			TO_CLOCK_SOURCE(tm_resource));

	return TM_RESULT_SUCCESS;
}

/**
 * Releases clock source if such was acquired on display path.
 *
 * \param [in] display_path: Display path from which we want to detach
 *				clock source
 * \param [in] clock_source: clock source to release
 * \param [in] method:	How resources were acquired
 */
static void tmrm_release_clock_source(
		struct tm_resource_mgr *tm_rm,
		struct display_path *display_path,
		struct clock_source *clock_source,
		enum tm_acquire_method method)
{
	struct tm_resource *tm_resource;
	struct controller *controller;
	struct dal_context *dal_context = tm_rm->dal_context;

	TM_ASSERT(display_path != NULL);

	if (clock_source == NULL) {
		/* that means there is no alternative clock source */
		return;
	}

	tm_resource = tmrm_find_clock_source_resource(tm_rm, clock_source);
	if (tm_resource == NULL)
		return;

	if (tm_resource_mgr_ref_counter_decrement(tm_rm, tm_resource) == 0) {
		/* Once nobody uses this Clock Source - restore default
		 * sharing group. */
		TO_CLOCK_SOURCE_INFO(tm_resource)->clk_sharing_group =
				CLOCK_SHARING_GROUP_EXCLUSIVE;

		if (update_hw_state_needed(method)) {

			/* HWSS cannot not power off PLL due to sharing of
			 * resources (because it doesn't know if anyone else is
			 * still using it).
			 * Do it now when last reference removed. */

			controller = dal_display_path_get_controller(
					display_path);

			TM_ASSERT(controller != NULL);

			dal_clock_source_power_down_pll(clock_source,
					dal_controller_get_id(controller));
		}
	}

	if (dal_display_path_get_alt_clock_source(display_path) == clock_source)
		dal_display_path_set_alt_clock_source(display_path, NULL);
	else
		dal_display_path_set_clock_source(display_path, NULL);
}

/**
 * Queries if an alternative ClockSource resource can be found.
 *
 * \param [in] display_path: Display path for which the ClockSource resource
 *	is searched for
 */
bool tm_resource_mgr_is_alternative_clk_src_available(
		struct tm_resource_mgr *tm_rm,
		struct display_path *display_path)
{
	uint32_t clock_source_index;
	struct dal_context *dal_context = tm_rm->dal_context;

	TM_ASSERT(display_path != NULL);

	/* Check if alternative clock source can be found. */
	clock_source_index = tmrm_get_available_clock_source(tm_rm,
			display_path, TM_ACQUIRE_METHOD_HW);

	if (clock_source_index == RESOURCE_INVALID_INDEX) {
		/* not found means not available */
		return false;
	}

	/* available */
	return true;
}

/**
 * Reset usage counter for ALL resources.
 */
void tm_resource_mgr_reset_all_usage_counters(
		struct tm_resource_mgr *tm_rm)
{
	uint32_t i;
	struct tm_resource *tm_resource;

	for (i = 0; i < tm_resource_mgr_get_total_resources_num(tm_rm); i++) {
		tm_resource = tm_resource_mgr_enum_resource(tm_rm, i);
		TM_RES_REF_CNT_RESET(tm_resource);
	}
}

/**
 * Obtains stereo-sync resource currently assigned to display path
 *
 * \param [in] display_path: Display path for which we are
 *	looking stereo-sync resources.
 *
 * \return Pointer to stereo-sync resource if such found, NULL otherwise
 */
struct tm_resource*
tm_resource_mgr_get_stereo_sync_resource(
		struct tm_resource_mgr *tm_rm,
		struct display_path *display_path)
{
	struct encoder *encoder;
	struct graphics_object_id id;

	if (display_path == NULL)
		return NULL;

	encoder = dal_display_path_get_stereo_sync_object(display_path);

	if (encoder == NULL)
		return NULL;

	id = dal_encoder_get_graphics_object_id(encoder);

	return tm_resource_mgr_find_resource(tm_rm, id);
}

struct tm_resource *tm_resource_mgr_get_sync_output_resource(
	struct tm_resource_mgr *tm_rm,
	struct display_path *display_path)
{
	struct encoder *encoder;
	struct graphics_object_id id;

	if (display_path == NULL)
		return NULL;

	encoder = dal_display_path_get_sync_output_object(display_path);

	if (encoder == NULL)
		return NULL;

	id = dal_encoder_get_graphics_object_id(encoder);

	return tm_resource_mgr_find_resource(tm_rm, id);
}

/**
 * Finds available sync-output resources that can be attached to display path.
 * Sync-output object can be encoder or.. encoder
 *
 * \param [in] display_path: Display path for which sync-output resource
 *				requested
 * \param [in] sync_output: Identification of sync-output resource
 *
 * \return The Sync-output resource for given display path if such resource
 *	were found, NULL otherwise.
 */
struct tm_resource*
tm_resource_mgr_get_available_sync_output_for_display_path(
		struct tm_resource_mgr *tm_rm,
		struct display_path *display_path,
		enum sync_source sync_output)
{
	struct dal_context *dal_context = tm_rm->dal_context;
	struct tm_resource *sync_output_rsrc = NULL;
	struct tm_resource *encoder_rsrc = NULL;
	enum sync_source current_sync_source;
	bool path_contains_obj;
	uint32_t i;
	const struct tm_resource_range *encoders =
		dal_tmrm_get_resource_range_by_type(tm_rm, OBJECT_TYPE_ENCODER);

	if (display_path == NULL ||
		!dal_display_path_is_acquired(display_path)) {
		TM_WARNING("%s: invalid input or path not acquired!\n",
				__func__);
		return NULL;
	}

	/* Loop over all encoders */
	for (i = encoders->start; i < encoders->end; i++) {
		encoder_rsrc = tm_resource_mgr_enum_resource(tm_rm, i);

		/* We are looking for an encoder with the requested sync-output
		 * capabilities, that satisfies one of these:
		 * 1. Encoder acquired on issued display path (as encoder or
		 *	optional object) - best case, we can stop looping
		 * 2. Encoder is not acquired on any display path - we remember
		 *	such encoder, but continue to loop to find encoder
		 *	that match first requirement
		 */
		current_sync_source = dal_encoder_get_vsync_output_source(
				TO_ENCODER(encoder_rsrc));

		if (current_sync_source == sync_output) {

			path_contains_obj = dal_display_path_contains_object(
				display_path,
				GRPH_ID(encoder_rsrc));

			if (path_contains_obj) {
				sync_output_rsrc = encoder_rsrc;
				break;
			} else if (!TM_RES_REF_CNT_GET(encoder_rsrc)) {
				/* For now this is the one, but keep
				 * searching.*/
				sync_output_rsrc = encoder_rsrc;
			}
		}
	}

	return sync_output_rsrc;
}

void tm_resource_mgr_set_gpu_interface(
		struct tm_resource_mgr *tm_rm,
		struct gpu *gpu)
{
	tm_rm->gpu_interface = gpu;
}

struct gpu *tm_resource_mgr_get_gpu_interface(
		struct tm_resource_mgr *tm_rm)
{
	return tm_rm->gpu_interface;
}


/**
 * Attaches an audio to display path if available for the specified
 * signal type, and increments the reference count.
 *
 * \param [in] display_path:     Display path on which connect event occurred
 * \param [in] signal:           signal type
 */
enum tm_result tm_resource_mgr_attach_audio_to_display_path(
		struct tm_resource_mgr *tm_rm,
		struct display_path *display_path,
		enum signal_type sig_type)
{
	struct dal_context *dal_context = tm_rm->dal_context;
	union display_path_properties path_props;
	uint32_t i;
	struct tm_resource *tm_audio_resource = NULL;
	const struct tm_resource_range *audios;

	/* First we check if the display path already has an audio assigned,
	 * and if so, we print a warning (should never happen) and
	 * return TM_RESULT_SUCCESS. */
	if (dal_display_path_get_audio_object(display_path,
			ASIC_LINK_INDEX) != NULL) {
		TM_WARNING("%s: audio already attached!\n ", __func__);
		return TM_RESULT_SUCCESS;
	}

	/* If DP signal but DP audio not supported in the display path,
	 * or HDMI signal but HDMI audio not supported in the display path,
	 * we return TM_RESULT_FAILURE. */
	path_props = dal_display_path_get_properties(display_path);

	if ((dal_is_dp_signal(sig_type) &&
			!path_props.bits.IS_DP_AUDIO_SUPPORTED) ||
	(dal_is_hdmi_signal(sig_type) &&
			!path_props.bits.IS_HDMI_AUDIO_SUPPORTED)) {
		TM_WARNING("%s: can't attach audio - no audio support on path!\n ",
				__func__);
		return TM_RESULT_FAILURE;
	}

	audios = dal_tmrm_get_resource_range_by_type(tm_rm, OBJECT_TYPE_AUDIO);

	/* Loop over all audio resources, and assign the first free audio
	 * which supports the signal. */
	for (i = audios->start; i < audios->end; i++) {

		tm_audio_resource = tm_resource_mgr_enum_resource(tm_rm, i);

		/* Allow at most ONE display path to use an audio resource. */
		if (is_resource_available(tm_audio_resource) == false) {
			/* This audio is in-use, continue the search. */
			continue;
		}

		if (!dal_audio_is_output_signal_supported(
				TO_AUDIO(tm_audio_resource), sig_type)) {
			/* Signal is not supported by the audio
			 * resource, continue. */
			continue;
		}

		/* Available audio found.
		 * Set audio on display path, increment the reference count and
		 * return TM_RESULT_SUCCESS. */
		dal_display_path_set_audio(display_path, ASIC_LINK_INDEX,
				TO_AUDIO_INFO(tm_audio_resource)->audio);

		tm_resource_ref_counter_increment(tm_rm, tm_audio_resource);

		return TM_RESULT_SUCCESS;
	}

	/* If we got here, we didn't find a free audio resource. */
	return TM_RESULT_FAILURE;
}

/**
 * Release (decrement counter) for audio resource assigned to specified
 * display path.
 *
 * \param [in] display_path: Display path on which to detach audio from
 */
void tm_resource_mgr_detach_audio_from_display_path(
		struct tm_resource_mgr *tm_rm,
		struct display_path *display_path)
{
	struct tm_resource *tm_audio_resource = NULL;

	/* decrement the reference count and reset audio on display path */
	tm_audio_resource = tmrm_display_path_find_audio_resource(tm_rm,
			display_path, ASIC_LINK_INDEX);

	if (NULL == tm_audio_resource) {
		/* nothing to do without the resource */
		return;
	}

	tm_resource_mgr_ref_counter_decrement(tm_rm, tm_audio_resource);

	dal_display_path_set_audio_active_state(
		display_path,
		ASIC_LINK_INDEX,
		false);

	dal_display_path_set_audio(display_path, ASIC_LINK_INDEX, NULL);
}

/**
 * Allocates or Re-allocates memory to store pointers to link services.
 * In case of re-allocation, moves existing link services to new
 * allocated space.
 * We allocate up to LINK_SERVICE_TYPE_MAX link services for each link,
 * for each display path. Each Type of link service will be used when
 * switching between SST/MST/Legacy on the fly.
 * TODO: consider hiding this switching logic inside of a link service,
 * so TM simply will command when switching of type is needed. After it is
 * done LINK_SERVICE_TYPE_MAX should be removed from the calculation.
 *
 * \param [in] number_of_paths: number of created display paths
 *
 * \return TM_RESULT_SUCCESS: if memory was successfully allocated
 *	TM_RESULT_FAILURE: otherwise
 */
enum tm_result tm_resource_mgr_setup_link_storage(
		struct tm_resource_mgr *tm_rm,
		uint32_t number_of_paths)
{
	struct vector *link_services = NULL;
	uint32_t num_of_cells_to_copy = 0;
	uint32_t requested_num_of_cells = 0;
	uint32_t i;
	struct dal_context *dal_context = tm_rm->dal_context;

	/* Calculate new allocation size and how many cells we need to copy
	 * to new memory segment. */
	if (NULL != tm_rm->link_services) {

		num_of_cells_to_copy =
			dal_vector_get_count(tm_rm->link_services) *
			LINK_SERVICE_TYPE_MAX *
			MAX_NUM_OF_LINKS_PER_PATH;
	}

	requested_num_of_cells = number_of_paths *
			LINK_SERVICE_TYPE_MAX * MAX_NUM_OF_LINKS_PER_PATH;

	if (num_of_cells_to_copy > requested_num_of_cells) {
		/* New storage requirement is smaller than old one,
		 * therefore we'll copy only some of the old link services. */
		num_of_cells_to_copy = requested_num_of_cells;
	}

	/* Allocate new array. It will store POINTERS. */
	if (requested_num_of_cells > 0) {

		link_services = dal_vector_presized_create(
				requested_num_of_cells,
				NULL,/* no initial value - leave all zeros */
				sizeof(struct link_service *));
	}

	/* Transfer existing link services to the new vector. */
	if (link_services != NULL) {
		struct link_service *current_ls;

		for (i = 0; i < num_of_cells_to_copy; i++) {

			current_ls = tmrm_get_ls_at_index(tm_rm, i);

			if (current_ls) {
				link_services_vector_set_at_index(
					link_services, &current_ls, i);
			}
		}
	}

	/* Release old vector and reassign data member. */
	if (tm_rm->link_services != NULL)
		dal_vector_destroy(&tm_rm->link_services);

	tm_rm->link_services = link_services;

	if (tm_rm->link_services == NULL || requested_num_of_cells == 0) {
		tm_rm->link_services_number_of_paths = 0;
		TM_ERROR("%s: no link services were allocated!\n", __func__);
		return TM_RESULT_FAILURE;
	}

	tm_rm->link_services_number_of_paths = number_of_paths;
	return TM_RESULT_SUCCESS;
}

/**
 * Notify all LinkService (either in used or not) that it is invalidated.
 */
void tm_resource_mgr_invalidate_link_services(
		struct tm_resource_mgr *tm_rm)
{
	uint32_t i;
	struct link_service *link_service;

	for (i = 0; i < dal_vector_get_count(tm_rm->link_services); i++) {

		link_service = tmrm_get_ls_at_index(tm_rm, i);

		/* Note here the MST shared link will be notified multiple
		 * times. Its okay for now because this call just sets a flag
		 * and real work is done on link_service->connect_link().
		 * TODO: code this properly so each link get notified once. */
		if (link_service != NULL)
			dal_ls_invalidate_down_stream_devices(link_service);
	}
}

/**
 * Release all link services.
 */
void tm_resource_mgr_release_all_link_services(
		struct tm_resource_mgr *tm_rm)
{
	uint32_t i;
	struct link_service *link_service;

	if (tm_rm->link_services == NULL)
		return;

	for (i = 0; i < dal_vector_get_count(tm_rm->link_services); i++) {

		link_service = tmrm_get_ls_at_index(tm_rm, i);

		if (link_service != NULL) {

			dal_link_service_destroy(&link_service);

			link_service = NULL;

			tmrm_set_ls_at_index(tm_rm, i, link_service);
		}
	}
}

/**
 * Releases link services for given display path.
 */
void tm_resource_mgr_release_path_link_services(
		struct tm_resource_mgr *tm_rm,
		struct display_path *display_path)
{
	uint32_t single_path_ls_array_size =
			LINK_SERVICE_TYPE_MAX * MAX_NUM_OF_LINKS_PER_PATH;
	uint32_t display_index;
	uint32_t i;
	uint32_t index;
	struct link_service *link_service;
	struct dal_context *dal_context = tm_rm->dal_context;

	display_index = dal_display_path_get_display_index(display_path);

	if (display_index >= tm_rm->link_services_number_of_paths) {
		TM_ERROR("%s: invalid input/state!\n", __func__);
		return;
	}

	for (i = 0; i < single_path_ls_array_size; i++) {

		index = (display_index * LINK_SERVICE_TYPE_MAX *
				MAX_NUM_OF_LINKS_PER_PATH) + i;

		link_service = tmrm_get_ls_at_index(tm_rm, index);

		if (link_service != NULL) {

			dal_link_service_destroy(&link_service);

			link_service = NULL;

			tmrm_set_ls_at_index(tm_rm, index, link_service);
		}
	}

	/* We removed *all* link services for the path, so count this path
	 * out of our link_services vector. */
	/*tm_rm->link_services_number_of_paths--;*/
}

/**
 * Adds already created link service to the pool.
 * The index is calculated based on:
 * (Display index) x (max num of link per path) x
 *	(max num of link service types per link) +
 * (Link index) x (max num of link service types per link) +
 *	(Link service type)
 *
 * \param [in] display_path: display path associated with link service
 * \param [in] link_index:    link index inside display path associated
 *				with link service
 * \param [in] new_link_service: link service to add
 *
 * \return TM_RESULT_SUCCESS if link service was successfully added,
 *	TM_RESULT_FAILURE otherwise
 */
enum tm_result tm_resource_mgr_add_link_service(
		struct tm_resource_mgr *tm_rm,
		struct display_path *display_path,
		uint32_t link_index,
		struct link_service *new_link_service)
{
	uint32_t display_index;
	uint32_t index;
	struct link_service *old_link_service;
	struct dal_context *dal_context = tm_rm->dal_context;

	TM_ASSERT(display_path != NULL);

	display_index = dal_display_path_get_display_index(display_path);

	if (display_index >= tm_rm->link_services_number_of_paths ||
		link_index >= MAX_NUM_OF_LINKS_PER_PATH ||
		new_link_service == NULL) {
		TM_ERROR("%s: Invalid input!\n", __func__);
		return TM_RESULT_FAILURE;
	}

	index = (display_index * LINK_SERVICE_TYPE_MAX *
			MAX_NUM_OF_LINKS_PER_PATH)
			+ (link_index * LINK_SERVICE_TYPE_MAX)
			+ (dal_ls_get_link_service_type(
					new_link_service));

	old_link_service = tmrm_get_ls_at_index(tm_rm, index);

	if (old_link_service != NULL) {
		TM_ERROR("%s: overwriting an existing LS pointer!\n",
				__func__);
		return TM_RESULT_FAILURE;
	}

	tmrm_set_ls_at_index(tm_rm, index, new_link_service);

	return TM_RESULT_SUCCESS;
}

/**
 * Obtains link service for requested link matching requested signal
 * The index is calculated based on:
 *  (Display index) x (max num of link per path) x
 *	(max num of link service types per link)
 *  (Link index) x (max num of link service types per link)
 *  (Link service type)
 *
 * \param [in] display_path: display path associated with link service
 * \param [in] linkIndex:    link index inside display path associated with
 *			link service
 * \param [in] signal:       signal type matching link service
 *
 * \return Pointer to link service associated with given link,
 *		matching given signal
 */
struct link_service *tm_resource_mgr_get_link_service(
		struct tm_resource_mgr *tm_rm,
		struct display_path *display_path,
		uint32_t link_index,
		enum signal_type sig_type)
{
	enum link_service_type link_type;
	uint32_t display_index;
	uint32_t link_service_count;
	uint32_t index;
	struct link_service *link_service;
	struct dal_context *dal_context = tm_rm->dal_context;

	TM_ASSERT(display_path != NULL);

	link_type = tm_utils_signal_to_link_service_type(sig_type);
	display_index = dal_display_path_get_display_index(display_path);
	link_service_count = dal_display_path_get_number_of_links(display_path);

	TM_LINK_SRV("%s: PathIdx:%d, LinkIdx:%d, Signal:0x%08X(%s)\n",
			__func__, display_index, link_index, sig_type,
			tm_utils_signal_type_to_str(sig_type));

	if (display_index >= tm_rm->link_services_number_of_paths
			|| link_index >= link_service_count
			|| link_type >= LINK_SERVICE_TYPE_MAX) {
		TM_ERROR("%s: Invalid input!\n", __func__);
		return NULL;
	}

	index = (display_index * LINK_SERVICE_TYPE_MAX
			* MAX_NUM_OF_LINKS_PER_PATH)
			+ (link_index * LINK_SERVICE_TYPE_MAX) + (link_type);

	link_service = tmrm_get_ls_at_index(tm_rm, index);

	return link_service;
}

/**
 * Obtains link service for first link that match requested signal
 * The index is calculated based on:
 *  (Display index) x (max num of link per path) x
 *   (max num of link service types per link)
 *  (Link index) x (max num of link service types per link)
 *  (Link service type)
 *
 * \param [in] display_path: display path associated with link service
 * \param [in] signal:       signal type matching link service
 *
 * \return Pointer to link service associated with given signal
 */
struct link_service *tm_resource_mgr_find_link_service(
		struct tm_resource_mgr *tm_rm,
		struct display_path *display_path,
		enum signal_type sig_type)
{
	enum link_service_type link_type;
	uint32_t display_index;
	uint32_t index;
	uint32_t i;
	struct link_service *link_service;
	struct dal_context *dal_context = tm_rm->dal_context;

	TM_ASSERT(display_path != NULL);

	link_type = tm_utils_signal_to_link_service_type(sig_type);
	display_index = dal_display_path_get_display_index(display_path);

	for (i = 0; i < MAX_NUM_OF_LINKS_PER_PATH; i++) {

		index = (display_index * LINK_SERVICE_TYPE_MAX
				* MAX_NUM_OF_LINKS_PER_PATH)
				+ (i * LINK_SERVICE_TYPE_MAX) + (link_type);

		link_service = tmrm_get_ls_at_index(tm_rm, index);

		if (link_service != NULL) {
			/* found the 1st matching one*/
			return link_service;
		}
	}

	return NULL;
}

/**
 * Swaps link services of two displays.
 *
 * \param [in] display_index1: display 1 to swap
 * \param [in] display_index2: display 2 to swap
 */

void tm_resource_mgr_swap_link_services(
		struct tm_resource_mgr *tm_rm,
		uint32_t display_index1,
		uint32_t display_index2)
{
	uint32_t i;
	uint32_t index1;
	uint32_t index2;
	struct link_service *link_service_index1;
	struct link_service *link_service_index2;
	struct dal_context *dal_context = tm_rm->dal_context;

	if (display_index1 >= tm_rm->link_services_number_of_paths
		|| display_index2 >= tm_rm->link_services_number_of_paths) {
		TM_ERROR("%s: Invalid input!\n", __func__);
		return;
	}

	for (i = 0; i < LINK_SERVICE_TYPE_MAX * MAX_NUM_OF_LINKS_PER_PATH;
			i++) {

		index1 = (display_index1 * LINK_SERVICE_TYPE_MAX
				* MAX_NUM_OF_LINKS_PER_PATH) + i;

		index2 = (display_index2 * LINK_SERVICE_TYPE_MAX
				* MAX_NUM_OF_LINKS_PER_PATH) + i;

		link_service_index1 = tmrm_get_ls_at_index(tm_rm, index1);

		link_service_index2 = tmrm_get_ls_at_index(tm_rm, index2);

		tmrm_set_ls_at_index(tm_rm, index1, link_service_index2);

		tmrm_set_ls_at_index(tm_rm, index2, link_service_index1);
	}
}

/**
 * Associates links services to display path and type of link.
 *
 * \param [in] display_path: display path which to associate with link services
 */
void tm_resource_mgr_associate_link_services(
		struct tm_resource_mgr *tm_rm,
		struct display_path *display_path)
{
	uint32_t number_of_links;
	uint32_t display_index;
	enum signal_type sink_signal;
	uint32_t link_idx;
	bool is_internal_link;
	uint32_t link_type;
	uint32_t index;
	struct link_service *link_service;
	struct dal_context *dal_context = tm_rm->dal_context;

	TM_ASSERT(display_path != NULL);

	number_of_links = dal_display_path_get_number_of_links(display_path);
	display_index = dal_display_path_get_display_index(display_path);
	sink_signal = dal_display_path_get_query_signal(display_path,
			SINK_LINK_INDEX);

	if (display_index >= tm_rm->link_services_number_of_paths) {
		TM_ERROR("%s: Invalid input!\n", __func__);
		return;
	}

	for (link_idx = 0; link_idx < number_of_links; link_idx++) {

		is_internal_link = (sink_signal == SIGNAL_TYPE_EDP
			|| link_idx < number_of_links - 1);

		for (link_type = 0; link_type < LINK_SERVICE_TYPE_MAX;
				link_type++) {

			index = (display_index * LINK_SERVICE_TYPE_MAX
					* MAX_NUM_OF_LINKS_PER_PATH)
					+ (link_idx * LINK_SERVICE_TYPE_MAX)
					+ (link_type);

			link_service = tmrm_get_ls_at_index(tm_rm, index);

			if (link_service != NULL) {

				dal_ls_associate_link(link_service,
						display_index,
						link_idx,
						is_internal_link);
			}
		}
	}
}

void dal_tmrm_set_resources_range_by_type(struct tm_resource_mgr *tm_rm)
{
	uint32_t index;
	struct tm_resource *tm_resource;
	uint32_t count = tm_resource_mgr_get_total_resources_num(tm_rm);
	struct tm_resource_range *resources;

	for (index = 0; index < count; index++) {
		tm_resource = tm_resource_mgr_enum_resource(tm_rm, index);
		resources = &tm_rm->resources_range[GRPH_ID(tm_resource).type];

		if (resources->end == 0) {
			resources->end = index;
			resources->start = index;
		}

		resources->end++;
	}
}

const struct tm_resource_range *dal_tmrm_get_resource_range_by_type(
	struct tm_resource_mgr *tm_rm,
	enum object_type type)
{
	if (type <= OBJECT_TYPE_UNKNOWN ||
		type >= OBJECT_TYPE_COUNT)
		return NULL;

	return &tm_rm->resources_range[type];
}


/**
 * Debug output of all resources
 */
void dal_tmrm_dump(struct tm_resource_mgr *tm_rm)
{
	uint32_t index;
	struct tm_resource *tm_resource;
	struct dal_context *dal_context = tm_rm->dal_context;

	TM_RESOURCES("Total number of TM resources = %u. Resource list:\n",
		tm_resource_mgr_get_total_resources_num(tm_rm));

	for (index = 0;
		index < tm_resource_mgr_get_total_resources_num(tm_rm);
		index++) {

		tm_resource = tm_resource_mgr_enum_resource(tm_rm, index);

		TM_RESOURCES("Resource at [%02d]: %s\n",
				index,
				tm_utils_get_tm_resource_str(tm_resource));
	}

	TM_RESOURCES("End of resource list.\n");
}

struct controller *dal_tmrm_get_free_controller(
		struct tm_resource_mgr *tm_rm,
		uint32_t *controller_index_out,
		uint32_t exclude_mask)
{
	struct dal_context *dal_context = tm_rm->dal_context;
	uint32_t res_ind;
	struct tm_resource *tm_resource_tmp = NULL;

	res_ind =
		dal_tmrm_find_controller_for_display_path(
			tm_rm,
			exclude_mask);

	if (RESOURCE_INVALID_INDEX == res_ind) {
		TM_MPO("%s: failed to find controller!\n", __func__);
		return NULL;
	}

	tm_resource_tmp = tm_resource_mgr_enum_resource(tm_rm, res_ind);

	*controller_index_out = res_ind;

	TM_MPO("%s: found controller.\n", __func__);

	return TO_CONTROLLER_INFO(tm_resource_tmp)->controller;
}
