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

#include "include/adapter_service_interface.h"
#include "include/logger_interface.h"
#include "include/timing_service_interface.h"
#include "include/default_mode_list_interface.h"
#include "include/mode_timing_list_interface.h"
#include "include/mode_manager_interface.h"

#include "mode_query.h"
#include "mode_query_allow_pan.h"
#include "mode_query_no_pan.h"
#include "view_solution.h"
#include "best_view.h"
#include "mode_query.h"
#include "cofunctional_mode_query_validator.h"

static struct view_info guaranteed_view_info[] = {
	{ { 640, 480 }, { ADAPTER_VIEW_IMPORTANCE_GUARANTEED } },
	{ { 800, 600 }, { ADAPTER_VIEW_IMPORTANCE_GUARANTEED } },
	{ { 1024, 768 }, { ADAPTER_VIEW_IMPORTANCE_GUARANTEED } },
	{ { 1280, 720 }, { ADAPTER_VIEW_IMPORTANCE_GUARANTEED_16x9 } },
	{ { 1280, 800 }, { ADAPTER_VIEW_IMPORTANCE_GUARANTEED_16x10 } },
	{ { 1280, 1024 }, { ADAPTER_VIEW_IMPORTANCE_GUARANTEED } },
	{ { 1600, 900 }, { ADAPTER_VIEW_IMPORTANCE_GUARANTEED_16x9 } },
	{ { 1600, 1200 }, { ADAPTER_VIEW_IMPORTANCE_GUARANTEED } },
	{ { 1680, 1050 }, { ADAPTER_VIEW_IMPORTANCE_GUARANTEED_16x10 } }, };

struct mode_manager {
	struct ds_dispatch *ds_dispatch;
	struct adapter_service *as;

	struct view_info_list master_view_list;
	struct vector solution_container_list;
	uint32_t supported_pixel_format;
	uint32_t pixel_granularity;
	struct dal_context *ctx;
};

DAL_VECTOR_AT_INDEX(solution_container,
	struct display_view_solution_container **);
DAL_VECTOR_APPEND(solution_container,
	struct display_view_solution_container **);

static void destruct(struct mode_manager *mm)
{
	uint32_t i;

	dal_view_info_list_destruct(&mm->master_view_list);

	for (i = 0; i < dal_vector_get_count(&mm->solution_container_list);
		++i) {
		struct display_view_solution_container *dvsc =
			*(solution_container_vector_at_index(
				&mm->solution_container_list,
				i));
		dal_dvsc_destroy(&dvsc);
	}

	dal_vector_destruct(&mm->solution_container_list);
}

void dal_mode_manager_destroy(struct mode_manager **mm)
{
	if (mm == NULL || *mm == NULL)
		return;
	destruct(*mm);

	dal_free(*mm);
	*mm = NULL;
}

/**
 * patches view to be SLS-Compatible view
 * for every non-4-divisible mode we want to have closest lower 8-divisible mode
 */
static bool patch_view_for_sls_compatibility(
	struct mode_manager *mm,
	struct view_info *vi)
{
	if (vi->view.width % mm->pixel_granularity != 0) {
		vi->view.width -= vi->view.width % 8;
		return true;
	}

	return false;
}

static bool construct(
	struct mode_manager *mm,
	const struct mode_manager_init_data *init_data)
{
	struct view min_view_override = { 0 };
	struct view max_view_override = { 0 };
	bool force_min_max_res = false;
	uint32_t i;
	uint32_t count;
	const struct default_mode_list *default_modes;

	if (!init_data)
		return false;
	if (!init_data->as)
		return false;
	if (!init_data->default_modes)
		return false;

	mm->ctx = init_data->dal_context;
	mm->as = init_data->as;
	mm->pixel_granularity =
		dal_adapter_service_get_view_port_pixel_granularity(mm->as);

	if (!dal_view_info_list_construct(&mm->master_view_list,
		min_view_override, max_view_override, force_min_max_res))
		return false;

	for (i = 0; i < ARRAY_SIZE(guaranteed_view_info); i++)
		dal_view_info_list_insert(
			&mm->master_view_list, &guaranteed_view_info[i]);

	/*
	 * populate master_view_list with views from default_mode_list
	 */
	default_modes = init_data->default_modes;
	count = dal_default_mode_list_get_count(default_modes);

	for (i = 0; i < count; i++) {
		const struct mode_info *mi =
			dal_default_mode_list_get_mode_info_at_index(
				default_modes, i);
		struct view view = { mi->pixel_width, mi->pixel_height };
		union adapter_view_importance importance = { 0 };
		struct view_info cur_vi;
		uint32_t index = 0;

		importance.flags.OPTIONAL = mi->timing_source ==
			TIMING_SOURCE_BASICMODE;
		importance.flags.DEFAULT_VIEW = 1;

		cur_vi.importance = importance;
		cur_vi.view = view;

		if (dal_view_info_list_find(
			&mm->master_view_list, &cur_vi, &index))
			/* view is already in the list, update importance */
			dal_view_info_list_at_index(
				&mm->master_view_list, index)->
				importance.value |= importance.value;
		else
			if (!dal_view_info_list_insert(
				&mm->master_view_list, &cur_vi))
				dal_logger_write(mm->ctx->logger,
					LOG_MAJOR_MODE_ENUM,
					LOG_MINOR_MODE_ENUM_MASTER_VIEW_LIST,
					"%s: one view is not inserted %dx%d\n",
					__func__,
					view.width,
					view.height);
	}

	/*
	 * Add modified views for SLS
	 */
	count = dal_view_info_list_get_count(&mm->master_view_list);
	for (i = 0; i < count; ++i) {
		struct view_info *vi_sls_compatible =
			dal_view_info_list_at_index(&mm->master_view_list, i);

		if (patch_view_for_sls_compatibility(mm, vi_sls_compatible))
			dal_view_info_list_insert(
				&mm->master_view_list, vi_sls_compatible);
	}

	dal_logger_write(mm->ctx->logger,
		LOG_MAJOR_MODE_ENUM,
		LOG_MINOR_MODE_ENUM_MASTER_VIEW_LIST,
		"Master View List, %u views:\n",
		dal_view_info_list_get_count(&mm->master_view_list));

	if (!dal_vector_construct(
		&mm->solution_container_list,
		SOLUTION_CONTAINER_LIST_INITIAL_CAPACITY,
		sizeof(struct display_view_solution_container *))) {
		dal_view_info_list_destruct(&mm->master_view_list);
		return false;
	}

	/* Check if Low Bitdepth (8bpp/16bpp) modes are required to be
	 * enumerated */
	if (!dal_adapter_service_get_asic_runtime_flags(mm->as).bits.
		NO_LOW_BPP_MODES) {
		if (dal_adapter_service_is_feature_supported(
			FEATURE_8BPP_SUPPORTED))
			mm->supported_pixel_format |= PIXEL_FORMAT_INDEX8;

		mm->supported_pixel_format |= PIXEL_FORMAT_RGB565;
	}

	mm->supported_pixel_format |= PIXEL_FORMAT_ARGB8888;
	mm->supported_pixel_format |= PIXEL_FORMAT_ARGB2101010;

	if (dal_adapter_service_get_asic_runtime_flags(mm->as).bits.
		SUPPORT_XRBIAS)
		mm->supported_pixel_format |=
			PIXEL_FORMAT_ARGB2101010_XRBIAS;

	mm->supported_pixel_format |= PIXEL_FORMAT_FP16;

	return true;
}

static struct display_view_solution_container *create_association_table(
		struct mode_manager *mm, uint32_t display_index)
{
	struct display_view_solution_container *tbl;
	union best_view_flags flags = {};
	struct display_view_solution_container_init_data dvsc_init_data = {
		.ds_dispatch = mm->ds_dispatch,
		.display_index = display_index,
		.master_view_list = &mm->master_view_list,
		.capacity = dal_flat_set_capacity(&mm->master_view_list.set),
		.dal_context = mm->ctx,
	};
	flags.bits.PREFER_3D_TIMING = dal_adapter_service_is_feature_supported(
			FEATURE_PREFER_3D_TIMING);
	dvsc_init_data.bv_flags = flags;

	tbl = dal_dvsc_create(&dvsc_init_data);

	if (!tbl)
		return NULL;

	if (!solution_container_vector_append(
		&mm->solution_container_list,
		&tbl)) {
		dal_dvsc_destroy(&tbl);
		return NULL;
	}
	return tbl;
}

/**
 * maps display path to view to timing association.
 * if association table already exist, return pointer to association table.
 * otherwise, create a new association table for this display path and added to
 * association master table.
 *
 * pointer to display_view_solution_container of given display_path
 */


static struct display_view_solution_container *get_association_table(
		struct mode_manager *mm, uint32_t display_index)
{
	uint32_t i;
	uint32_t scl_size = dal_vector_get_count(&mm->solution_container_list);

	dal_logger_write(mm->ctx->logger,
		LOG_MAJOR_MODE_ENUM,
		LOG_MINOR_MODE_ENUM_MASTER_VIEW_LIST,
		"%s: display_index:%d, solution container size: %d\n",
		__func__, display_index, scl_size);

	for (i = 0; i < scl_size; i++) {
		struct display_view_solution_container *item =
				*(solution_container_vector_at_index(
						&mm->solution_container_list,
						i));

		if (item->display_index == display_index)
			return item;
	}

	return create_association_table(mm, display_index);
}

struct mode_query *dal_mode_manager_create_mode_query(
		struct mode_manager *mm,
		const struct topology *topology,
		const enum query_option option)
{
	struct mode_query *mq;
	struct mode_query_init_data mq_init_data = {0};
	struct mode_query_set_init_data mqs_init_data = {0};

	uint32_t i;

	if (topology == NULL) {
		dal_logger_write(mm->ctx->logger,
			LOG_MAJOR_MODE_ENUM,
			LOG_MINOR_MODE_ENUM_MASTER_VIEW_LIST,
			"%s - Invalid parameter\n", __func__);
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	mqs_init_data.ctx = mm->ctx;
	mqs_init_data.supported_pixel_format = mm->supported_pixel_format;
	mqs_init_data.master_view_list = &mm->master_view_list;

	mq_init_data.ctx =  mm->ctx;
	mq_init_data.ds_dispatch = mm->ds_dispatch;
	mq_init_data.query_set = dal_mode_query_set_create(&mqs_init_data);

	if (!mq_init_data.query_set) {
		dal_logger_write(mm->ctx->logger,
			LOG_MAJOR_MODE_ENUM,
			LOG_MINOR_MODE_ENUM_MASTER_VIEW_LIST,
			"%s - Mode query set was not created\n", __func__);
		return NULL;
	}

	for (i = 0; i < topology->disp_path_num; i++) {
		if (!(dal_mode_query_set_add_solution_container(
				mq_init_data.query_set,
				get_association_table(
						mm,
						topology->display_index[i])))) {
			dal_logger_write(mm->ctx->logger,
				LOG_MAJOR_MODE_ENUM,
				LOG_MINOR_MODE_ENUM_MASTER_VIEW_LIST,
				" %s:%d: Container is invalid\n",
				__func__, __LINE__);
			goto free_mode_query_set;
		}
	}

	switch (option) {
	case QUERY_OPTION_NO_PAN:
		if (topology->disp_path_num > 2)
			mq = dal_mode_query_wide_topology_create(&mq_init_data);
		else
			mq = dal_mode_query_no_pan_create(&mq_init_data);
		break;

	case QUERY_OPTION_NO_PAN_NO_DISPLAY_VIEW_RESTRICTION:
		mq = dal_mode_query_no_pan_no_display_view_restriction_create(
				&mq_init_data);
		break;

	case QUERY_OPTION_3D_LIMITED_CANDIDATES:
		if (topology->disp_path_num > 2)
			mq =
			dal_mode_query_3d_limited_cand_wide_topology_create(
					&mq_init_data);
		else
			mq = dal_mode_query_3d_limited_candidates_create(
					&mq_init_data);
		break;

	case QUERY_OPTION_PAN_ON_LIMITED_RESOLUTION_DISP_PATH:
		mq = dal_mode_query_pan_on_limited_create(&mq_init_data);
		break;

	case QUERY_OPTION_ALLOW_PAN:
		mq = dal_mode_query_allow_pan_create(&mq_init_data);
		break;
	case QUERY_OPTION_ALLOW_PAN_NO_VIEW_RESTRICTION:
		mq = dal_mode_query_allow_pan_no_view_restriction_create(
				&mq_init_data);
		break;

	case QUERY_OPTION_TILED_DISPLAY_PREFERRED:
		mq = dal_mode_query_tiled_display_preferred_create(
				&mq_init_data);
		break;
	default:
		goto free_mode_query_set;
	}

	if (mq == NULL)
		goto free_mode_query_set;

	if (option == QUERY_OPTION_PAN_ON_LIMITED_RESOLUTION_DISP_PATH ||
			option == QUERY_OPTION_ALLOW_PAN ||
			option == QUERY_OPTION_ALLOW_PAN_NO_VIEW_RESTRICTION)

			dal_mode_query_allow_pan_post_initialize(mq);

	return mq;
free_mode_query_set:
	dal_mode_query_set_destroy(&mq_init_data.query_set);
	return NULL;
}

bool dal_mode_manager_retreive_path_mode_set(
		struct mode_manager *mm,
		struct path_mode_set *path_mode_set,
		struct render_mode *render_mode,
		struct refresh_rate *refresh_rate,
		const struct topology *top,
		enum query_option option,
		bool allow_fallback)
{
	struct mode_query *mode_query = dal_mode_manager_create_mode_query(
			mm,
			top,
			option);
	dal_mode_query_destroy(&mode_query);
	return false;
	/* TODO to be implemented */
}

struct mode_manager *dal_mode_manager_create(
	const struct mode_manager_init_data *init_data)
{
	struct mode_manager *mm = dal_alloc(sizeof(struct mode_manager));

	if (!mm)
		return NULL;

	if (construct(mm, init_data))
		return mm;

	BREAK_TO_DEBUGGER();
	dal_free(mm);

	return NULL;
}

uint32_t dal_mode_manager_get_supported_pixel_format(
	const struct mode_manager *mm)
{
	return mm->supported_pixel_format;
}

void dal_mode_manager_set_supported_pixel_format(
	struct mode_manager *mm,
	uint32_t supported_pixel_format_mask)
{
	mm->supported_pixel_format = supported_pixel_format_mask;
}

struct bestview_options dal_mode_manager_get_bestview_options(
	struct mode_manager *mm,
	uint32_t display_index)
{
	struct bestview_options options;

	struct display_view_solution_container *tbl =
		get_association_table(mm, display_index);
	if (tbl)
		options = dal_dvsc_get_bestview_options(tbl);

	return options;
}

static enum display_view_importance determine_display_view_importance(
	struct dal_context *ctx,
	enum timing_source timing_source, bool default_view)
{
	switch (timing_source) {
	case TIMING_SOURCE_USER_FORCED:
	case TIMING_SOURCE_DALINTERFACE_EXPLICIT:
	case TIMING_SOURCE_EDID_DETAILED:
	case TIMING_SOURCE_CV:
	case TIMING_SOURCE_TV:
	case TIMING_SOURCE_VBIOS:
	case TIMING_SOURCE_USER_OVERRIDE:
	case TIMING_SOURCE_EDID_CEA_SVD_3D:
	case TIMING_SOURCE_HDMI_VIC:
		return DISPLAY_VIEW_IMPORTANCE_GUARANTEED;
	case TIMING_SOURCE_EDID_ESTABLISHED:
	case TIMING_SOURCE_EDID_STANDARD:
	case TIMING_SOURCE_EDID_CEA_SVD:
	case TIMING_SOURCE_EDID_CVT_3BYTE:
	case TIMING_SOURCE_EDID_4BYTE:
		return default_view ?
			DISPLAY_VIEW_IMPORTANCE_OPTIONAL :
			DISPLAY_VIEW_IMPORTANCE_NON_GUARANTEED;
	case TIMING_SOURCE_OS_FORCED:
	case TIMING_SOURCE_DALINTERFACE_IMPLICIT:
	case TIMING_SOURCE_DEFAULT:
	case TIMING_SOURCE_CUSTOM:
	case TIMING_SOURCE_CUSTOM_BASE:
		return DISPLAY_VIEW_IMPORTANCE_RESTRICTED;
	case TIMING_SOURCE_RANGELIMIT:
		return DISPLAY_VIEW_IMPORTANCE_NON_GUARANTEED;
	default:
		/* MM doesn't handle this timing source!!! */
		dal_logger_write(ctx->logger,
			LOG_MAJOR_MODE_ENUM,
			LOG_MINOR_MODE_ENUM_MASTER_VIEW_LIST,
			"%s: timing source is incorrect: %d\n",
			__func__,
			timing_source);
		return DISPLAY_VIEW_IMPORTANCE_NON_GUARANTEED;
	}
}

#define ALLOC_RESERVE_CNT 16

static bool grow_all_tables(struct mode_manager *mm)
{
	uint32_t capacity = dal_view_info_list_get_count(&mm->master_view_list)
		+ ALLOC_RESERVE_CNT;
	uint32_t i;
	uint32_t scl_count = dal_vector_get_count(&mm->solution_container_list);

	for (i = 0; i < scl_count; ++i) {
		struct display_view_solution_container *dvsc =
			*(solution_container_vector_at_index(
				&mm->solution_container_list, i));
		if (!dvsc) {
			dal_logger_write(mm->ctx->logger,
				LOG_MAJOR_MODE_ENUM,
				LOG_MINOR_MODE_ENUM_MASTER_VIEW_LIST,
				" %s:%d: dvsc is NULL\n", __func__, __LINE__);
			return false;
		}
		if (!dal_dvsc_grow(dvsc, capacity))
			return false;
	}

	if (!dal_view_info_list_reserve(&mm->master_view_list, capacity))
		return false;
	return true;
}

static bool insert_view_and_update_solutions(
	struct mode_manager *mm,
	const struct view_info *vi,
	uint32_t *index)
{
	uint32_t j;
	uint32_t solution_container_count;

	if (dal_view_info_list_get_count(&mm->master_view_list) >=
		dal_view_info_list_capacity(&mm->master_view_list))
		if (!grow_all_tables(mm))
			return false;

	/* this is newly added view, insert the View to master view list, and
	 * insert association to all association tables. */
	if (!dal_view_info_list_insert(&mm->master_view_list, vi))
		/*
		 * failed to insert the view due to pruning against
		 * runtime parameters
		 */
		return false;

	solution_container_count =
		dal_vector_get_count(&mm->solution_container_list);

	for (j = 0; j < solution_container_count; ++j) {
		/* set dispView flag on this display */
		struct display_view_solution_container *dvsc =
			*(solution_container_vector_at_index(
				&mm->solution_container_list, j));
		dal_dvsc_notify_newly_inserted_view_at_index(
			dvsc, *index);
	}

	return true;
}

/**
 * Adds view to master table or verifies it already exists there.
 * In any case the view will be marked as display view
 */
static void add_display_view(
	struct mode_manager *mm,
	struct display_view_solution_container *tbl,
	struct view_info *vi,
	enum timing_source source)
{
	uint32_t index = 0;

	if (dal_view_info_list_find(&mm->master_view_list, vi, &index) ||
		insert_view_and_update_solutions(mm, vi, &index)) {
		enum display_view_importance importance;
		bool default_view =
			dal_view_info_list_at_index(
				&mm->master_view_list,
				index)->
				importance.flags.DEFAULT_VIEW;
		dal_view_info_list_at_index(&mm->master_view_list, index)->
			importance.value |= vi->importance.value;
		importance = determine_display_view_importance(mm->ctx,
			source, default_view);

		dal_dvsc_update_view_importance(
			tbl, index, importance);
	}
}

static void process_display_views(
	struct mode_manager *mm,
	struct display_view_solution_container *association_tbl,
	const struct mode_timing_list *mode_timing_list)
{
	uint32_t i;
	uint32_t mtl_count = dal_mode_timing_list_get_count(mode_timing_list);

	for (i = 0; i < mtl_count; ++i) {
		const struct mode_timing *mt =
			dal_mode_timing_list_get_timing_at_index(
				mode_timing_list, i);
		struct view_info vi = {
			{ mt->mode_info.pixel_width,
			mt->mode_info.pixel_height },
			{ 0 } };
		enum timing_source timing_source = mt->mode_info.timing_source;
		/* Sometimes DAL will not enumerate a mode on a particular
		 * display unless it is part of a clone configuration. It will
		 * cause issue in some corner case when OS try to keep DFP
		 * single in the mode that supported in clone configuration. We
		 * add a W/A that if flag PREFERRED_VIEW is set for a mode,
		 * other displays will support it always */
		if (mt->mode_info.flags.PREFERRED_VIEW)
			vi.importance.flags.PREFERRED_VIEW = 1;

		/* Add original view */
		add_display_view(mm, association_tbl, &vi, timing_source);

		/* Add modified views */

		{
			struct view_info vi_sls_compatible = vi;

			if (patch_view_for_sls_compatibility(
				mm, &vi_sls_compatible))
				add_display_view(
					mm,
					association_tbl,
					&vi_sls_compatible,
					timing_source);
		}
	}
}

/**
* validate the given set of ModeQuery for cofunctionality
*
* the mode_query may or may not have RenderMode/RefreshRate/Scaling selected.
* If it's not selected then ModeMgr will try select the least resource consuming
* configuration and use it to do validation
*/
bool dal_mode_mgr_are_mode_queries_cofunctional(
	struct mode_manager *mm,
	struct mode_query **mode_queries,
	uint32_t count)
{
	/*
	 * set up ModeQueryValidator.
	 */
	uint32_t i;
	bool result = false;
	struct mode_query *mode_query;
	struct cofunctional_mode_query_validator *validator =
		dal_cmqv_create(mm->ds_dispatch);

	for (i = 0; i < count; ++i) {
		mode_query = mode_queries[i];
		if (!dal_cmqv_add_mode_query(validator, mode_query))
			goto free_validator;
	}

	/* if mode query has something unpinned (unselected), select something
	 * and try validate. Below assumed that select min resource for auto
	 * selected is able to select path mode combination that uses the least
	 * resource for unpinned item,
	 * as well as there is only 1 combination. This may not be the case, as
	 * resource is not an absolute number, and there maybe multiple
	 * combination that below code need to select and try (i.e. higher pixel
	 * clock but no scaling vs lower pixel clock with scaling). This code is
	 * in a separate loop so we can address the problem of selecting and
	 * trying different combination if required.
	 */
	for (i = 0; i < count; ++i) {
		mode_query = mode_queries[i];

		if (!dal_mode_query_select_min_resources_for_autoselect(
			mode_query))
			goto free_validator;

		/* update the validator with the above selected combination */
		dal_cmqv_update_mode_query(
			validator, mode_query);
	}

	result = dal_cmqv_is_cofunctional(validator);
free_validator:
	dal_cmqv_destroy(&validator);
	return result;
}

/*
 * Updates (or create) association from master view list to associated timing &
 * scaling. Method does the following
 * 1. Update TS mode timing list on this disp index according to the DCS on this
 * path
 */
bool dal_mode_manager_update_disp_path_func_view_tbl(
	struct mode_manager *mm,
	uint32_t display_index,
	struct mode_timing_list *mtl)
{
	/* gets the association table of the given display path */
	struct display_view_solution_container *tbl =
		get_association_table(
			mm,
			display_index);

	/* create new associate table if it doesn't exist already */
	if (!tbl) {
		dal_logger_write(mm->ctx->logger, LOG_MAJOR_MODE_ENUM,
			LOG_MINOR_MODE_ENUM_TS_LIST_BUILD,
		"%s: PathInd:%d: Display View Solution Container is NULL!\n",
			__func__,
			display_index);
		return false;
	}

	if (!mtl) {
		dal_logger_write(mm->ctx->logger, LOG_MAJOR_MODE_ENUM,
			LOG_MINOR_MODE_ENUM_TS_LIST_BUILD,
			"TS::ModeTimingList(%d) not initialized yet.\n",
			display_index);
		return false;
	}

	/* update given container with given best view option with views in
	 * master view list */
	if (!dal_dvsc_update(tbl, mtl)) {
		dal_logger_write(mm->ctx->logger, LOG_MAJOR_MODE_ENUM,
			LOG_MINOR_MODE_ENUM_TS_LIST_BUILD,
		"%s: PathInd:%d: failed to update container with best view!\n",
			__func__,
			display_index);
		return false;
	}

	/* add views explicitly supported by this display but not in
	 * master view list and update associations for all displays on these
	 * newly added views. Mark views explicitly supported by this display in
	 * association table
	 */
	process_display_views(mm, tbl, mtl);

	return true;
}

struct bestview_options dal_mode_manager_get_default_bestview_options(
	struct mode_manager *mm,
	uint32_t display_index)
{
	struct bestview_options options = { 0 };

	struct display_view_solution_container *tbl = get_association_table(
		mm, display_index);

	if (tbl)
		options = dal_dvsc_get_default_bestview_options(tbl);

	return options;
}

void dal_mode_manager_set_bestview_options(
	struct mode_manager *mm,
	uint32_t display_index,
	const struct bestview_options *opts,
	bool rebuild_bestview,
	struct mode_timing_list *mtl)
{
	struct display_view_solution_container *tbl;

	if (!opts)
		return;

	tbl = get_association_table(mm, display_index);

	if (!tbl)
		return;

	dal_dvsc_save_bestview_options(tbl, opts);

	if (rebuild_bestview)
		dal_mode_manager_update_disp_path_func_view_tbl(
			mm, display_index, mtl);
}

void dal_mode_manager_set_ds_dispatch(
	struct mode_manager *mm,
	struct ds_dispatch *ds_dispatch)
{
	mm->ds_dispatch = ds_dispatch;
}
