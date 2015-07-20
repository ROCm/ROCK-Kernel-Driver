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

#include "include/mode_timing_list_interface.h"
#include "include/logger_interface.h"

#include "display_view_solution_container.h"
#include "solution.h"

DAL_VECTOR_AT_INDEX(solution_key, struct solution_key *);
DAL_VECTOR_INSERT_AT(solution_key, struct solution_key *);
DAL_VECTOR_APPEND(solution_store, struct solution *);

struct view_solution dal_dvsc_get_view_solution_at_index(
	const struct display_view_solution_container *dvsc,
	uint32_t index)
{
	struct view_solution vw_sol;

	view_solution_construct(
		&vw_sol,
		&dal_view_info_list_at_index(
			dvsc->master_view_list,
			index)->view,
		dvsc->store,
		solution_key_vector_at_index(dvsc->keys, index));

	dal_logger_write(
		dvsc->ctx->logger,
		LOG_MAJOR_MODE_ENUM,
		LOG_MINOR_MODE_ENUM_VIEW_SOLUTION,
		"%s: %i %ix%i start_index=%i count=%i importance=%i\n",
		__func__,
		index,
		vw_sol.view->width,
		vw_sol.view->height,
		vw_sol.key->start_index,
		vw_sol.key->count,
		vw_sol.key->importance);

	return vw_sol;
}

bool dal_dvsc_grow(
	struct display_view_solution_container *dvsc,
	uint32_t count)
{
	if (!dal_vector_reserve(dvsc->keys, count))
		return false;
	if (!dal_vector_reserve(dvsc->store, count))
		return false;
	return true;
}

static const struct bestview_options HDMI_BEST_VIEW_DEFAULT_OPTION = {
	TIMING_SELECT_DEFAULT, /* base_timing_select */
	DISPLAY_COLOR_DEPTH_101010, /* prefered_color_depth */
	PIXEL_ENCODING_YCBCR444, /* prefered_pixel_encoding */
	false, /* RESTRICT_HD_TIMING */
	false, /* DISALLOW_CUSTOM_MODES_AS_BASE */
	false, /* MAINTAIN_ASPECT_RATIO */
	true /* ENABLE_SCALING */
};

static const struct bestview_options BEST_VIEW_DEFAULT_OPTION = {
	TIMING_SELECT_DEFAULT, /* base_timing_select */
	DISPLAY_COLOR_DEPTH_101010, /* prefered_color_depth */
	PIXEL_ENCODING_RGB, /* prefered_pixel_encoding */
	false, /* RESTRICT_HD_TIMING */
	false, /* DISALLOW_CUSTOM_MODES_AS_BASE */
	false, /* MAINTAIN_ASPECT_RATIO */
	true /* ENABLE_SCALING */
};

static void restore_best_view_option(
		struct display_view_solution_container *dvsc)
{
	/* TODO: temporary disable bestview restore until final logic
	 * identified */
	return;
	dvsc->best_view_options = BEST_VIEW_DEFAULT_OPTION;
	dvsc->hdmi_default_options = HDMI_BEST_VIEW_DEFAULT_OPTION;
	dvsc->hdmi_options = HDMI_BEST_VIEW_DEFAULT_OPTION;

	if (dvsc->set_mode_params != NULL &&
		dal_set_mode_params_is_multiple_pixel_encoding_supported(
			dvsc->set_mode_params, dvsc->display_index)) {
		/* default setting in not persistent data. */

		if (dal_set_mode_params_get_default_pixel_format_preference(
			dvsc->set_mode_params, dvsc->display_index))
			/* if FID9204 is applicable then we do RGB */
			dvsc->hdmi_default_options.prefered_pixel_encoding =
				PIXEL_ENCODING_RGB;
	}
}

static bool construct(
	struct display_view_solution_container *dvsc,
	const struct display_view_solution_container_init_data *dvsc_init_data)
{
	uint32_t display_index_array[] = { dvsc_init_data->display_index };

	dvsc->display_index = dvsc_init_data->display_index;
	dvsc->master_view_list = dvsc_init_data->master_view_list;
	dvsc->bv_flags = dvsc_init_data->bv_flags;
	if (dvsc_init_data->ds_dispatch == NULL)
		return false;

	dvsc->keys = dal_vector_create(
			dvsc_init_data->capacity,
			sizeof(struct solution_key));

	if (dvsc->keys == NULL)
		return false;

	dvsc->ctx = dvsc_init_data->dal_context;
	dvsc->ds_dispatch = dvsc_init_data->ds_dispatch;
	dvsc->best_view = NULL;
	dvsc->is_valid = false;
	dvsc->store = dal_vector_create(
			dvsc_init_data->capacity,
			sizeof(struct solution));

	if (dvsc->store == NULL)
		goto out_destruct_key_list;

	dvsc->set_mode_params = dal_ds_dispatch_create_resource_context(
			dvsc_init_data->ds_dispatch,
			display_index_array,
			1);

	if (dvsc->set_mode_params == NULL)
		goto out_destruct_solution_store;

	if (!dal_solution_set_construct(&dvsc->output_mode_list))
		goto out_destruct_set_mode_params;

	restore_best_view_option(dvsc);
	return true;
out_destruct_set_mode_params:
	dal_set_mode_params_destroy(&dvsc->set_mode_params);
out_destruct_solution_store:
	dal_vector_destroy(&dvsc->store);
out_destruct_key_list:
	dal_vector_destroy(&dvsc->keys);
	return false;
}

struct display_view_solution_container *dal_dvsc_create(
	const struct display_view_solution_container_init_data *dvsc_init_data)
{
	struct display_view_solution_container *dvsc = dal_alloc(
			sizeof(struct display_view_solution_container));

	if (dvsc == NULL)
		return NULL;

	if (construct(dvsc, dvsc_init_data))
		return dvsc;

	dal_free(dvsc);
	return NULL;
}

static void destruct(struct display_view_solution_container *dvsc)
{
	dal_vector_destroy(&dvsc->keys);
	dal_vector_destroy(&dvsc->store);
	dal_best_view_destroy(&dvsc->best_view);
	dal_set_mode_params_destroy(&dvsc->set_mode_params);
	solution_set_destruct(&dvsc->output_mode_list);
}

void dal_dvsc_destroy(struct display_view_solution_container **dvsc)
{
	if (dvsc == NULL || *dvsc == NULL)
		return;
	destruct(*dvsc);
	dal_free(*dvsc);
	*dvsc = NULL;
}

void dal_dvsc_save_bestview_options(
	struct display_view_solution_container *dvsc,
	const struct bestview_options *opts)
{
	if (!opts)
		return;

	if (dvsc->set_mode_params &&
		dal_set_mode_params_is_multiple_pixel_encoding_supported(
			dvsc->set_mode_params,
			dvsc->display_index))
		dvsc->hdmi_options = *opts;
	else
		dvsc->best_view_options = *opts;
}

struct bestview_options dal_dvsc_get_default_bestview_options(
	struct display_view_solution_container *dvsc)
{
	if (dvsc->set_mode_params &&
		dal_set_mode_params_is_multiple_pixel_encoding_supported(
			dvsc->set_mode_params,
			dvsc->display_index))
		return dvsc->hdmi_default_options;
	else
		return BEST_VIEW_DEFAULT_OPTION;
}


struct bestview_options dal_dvsc_get_bestview_options(
	struct display_view_solution_container *dvsc)
{
	if (dvsc->set_mode_params &&
		dal_set_mode_params_is_multiple_pixel_encoding_supported(
			dvsc->set_mode_params,
			dvsc->display_index))
		return dvsc->hdmi_options;
	return dvsc->best_view_options;
}

void dal_dvsc_update_view_importance(
	struct display_view_solution_container *dvsc,
	uint32_t index,
	enum display_view_importance importance)
{
	struct solution_key *key = solution_key_vector_at_index(
		dvsc->keys, index);

	if (importance < key->importance)
		key->importance = importance;
}

static const enum scaling_transformation SCALING_ENUM_ORDER_PAR[] = {
	SCALING_TRANSFORMATION_IDENTITY,
	SCALING_TRANSFORMATION_PRESERVE_ASPECT_RATIO_SCALE,
	SCALING_TRANSFORMATION_FULL_SCREEN_SCALE,
	SCALING_TRANSFORMATION_CENTER_TIMING,
	SCALING_TRANSFORMATION_INVALID };

static const enum scaling_transformation SCALING_ENUM_ORDER_FS[] = {
	SCALING_TRANSFORMATION_IDENTITY,
	SCALING_TRANSFORMATION_FULL_SCREEN_SCALE,
	SCALING_TRANSFORMATION_PRESERVE_ASPECT_RATIO_SCALE,
	SCALING_TRANSFORMATION_CENTER_TIMING,
	SCALING_TRANSFORMATION_INVALID };

static const enum scaling_transformation SCALING_ENUM_ORDER_CENTER[] = {
	SCALING_TRANSFORMATION_IDENTITY,
	SCALING_TRANSFORMATION_CENTER_TIMING,
	SCALING_TRANSFORMATION_PRESERVE_ASPECT_RATIO_SCALE,
	SCALING_TRANSFORMATION_FULL_SCREEN_SCALE,
	SCALING_TRANSFORMATION_INVALID };

enum scaling_transformation dal_dvsc_get_preferred_scaling(
	struct display_view_solution_container *dvsc)
{
	/* The order is important. If aspect_ratio set, we assume that scaler
	 * enabled and report aspect_ratio
	 * If aspect_ratio not set, but scaler enabled - we scale to full screen
	 * If both unset - we report Centered Timing
	 * Generally we have one more state - aspect_ratio set, but scaler
	 * disabled. Currently we report aspect_ratio, but this state could be
	 * used for some special case
	 */
	struct bestview_options opts = dal_dvsc_get_bestview_options(dvsc);

	if (!opts.ENABLE_SCALING)
		return SCALING_TRANSFORMATION_CENTER_TIMING;

	if (opts.MAINTAIN_ASPECT_RATIO)
		return SCALING_TRANSFORMATION_PRESERVE_ASPECT_RATIO_SCALE;
	else
		return SCALING_TRANSFORMATION_FULL_SCREEN_SCALE;
}

static enum mode_aspect_ratio get_aspect_ratio_for_mode(
	const struct mode_info *mi)
{
	if (mi->pixel_width * 3 == mi->pixel_height * 4)
		return MODE_ASPECT_RATIO_4X3;
	else if (mi->pixel_width * 4 == mi->pixel_height * 5)
		return MODE_ASPECT_RATIO_5X4;
	else if (mi->pixel_width * 9 == mi->pixel_height * 16)
		return MODE_ASPECT_RATIO_16X9;
	else if (mi->pixel_width * 10 == mi->pixel_height * 16)
		return MODE_ASPECT_RATIO_16X10;

	return MODE_ASPECT_RATIO_UNKNOWN;
}

static void update_display_aspect_ratio(
	struct display_view_solution_container *dvsc)
{
	enum mode_aspect_ratio aspect = MODE_ASPECT_RATIO_UNKNOWN;
	uint32_t i;

	for (i = dal_mode_timing_list_get_count(dvsc->mtl); i > 0; --i) {
		const struct mode_info *mi =
			&(dal_mode_timing_list_get_timing_at_index(
				dvsc->mtl,
				i - 1)->mode_info);

		if (mi->flags.PREFERRED || mi->flags.NATIVE) {
			aspect = get_aspect_ratio_for_mode(mi);

			if (aspect != MODE_ASPECT_RATIO_UNKNOWN)
				break;
		}
	}

	dvsc->mode_aspect_ratio = aspect;

}

bool dal_dvsc_update(
	struct display_view_solution_container *dvsc,
	const struct mode_timing_list *mtl)
{
	uint32_t i;
	uint32_t count;
	struct bestview_options opts;
	/* container is valid only if we update properly */
	dvsc->is_valid = false;

	/* process rebuild */
	dvsc->mtl = mtl;
	update_display_aspect_ratio(dvsc);

	/* recreate resource validation context */
	if (dvsc->set_mode_params != NULL)
		dal_set_mode_params_destroy(&dvsc->set_mode_params);

	dvsc->set_mode_params =
		dal_ds_dispatch_create_resource_context(
			dvsc->ds_dispatch,
			&dvsc->display_index,
			1);

	if (dvsc->set_mode_params == NULL) {
		dal_logger_write(dvsc->ctx->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_MODE_ENUM_VIEW_SOLUTION,
			"%s - Out of Memory\n", __func__);
		return false;
	}

	/* add restore_best_view_option here to make a chance to update the hdmi
	 * best view option for the passive dongle case */
	restore_best_view_option(dvsc);

	opts = dal_dvsc_get_bestview_options(dvsc);

	/* Update Scaling Enum Order according to input options */
	switch (dal_dvsc_get_preferred_scaling(dvsc)) {
	case SCALING_TRANSFORMATION_PRESERVE_ASPECT_RATIO_SCALE:
		dvsc->scl_enum_order_list = SCALING_ENUM_ORDER_PAR;
		break;
	case SCALING_TRANSFORMATION_CENTER_TIMING:
		dvsc->scl_enum_order_list = SCALING_ENUM_ORDER_CENTER;
		break;
	case SCALING_TRANSFORMATION_FULL_SCREEN_SCALE:
	default:
		dvsc->scl_enum_order_list = SCALING_ENUM_ORDER_FS;
		break;
	}

	/* Update the best view object to be used to build view to timing
	 * association */
	if (dvsc->best_view)
		dal_best_view_destroy(&dvsc->best_view);

	{
		struct best_view_init_data data = {
			dvsc->set_mode_params,
			dvsc->display_index,
			opts,
			dvsc->bv_flags,
			mtl
		};
		dvsc->best_view = dal_best_view_create(dvsc->ctx, &data);
	}

	if (dvsc->best_view == NULL) {
		dal_logger_write(dvsc->ctx->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_MODE_ENUM_VIEW_SOLUTION,
			"%s - Out of Memory\n", __func__);
		return false;
	}

	/* clear current associations */
	dal_vector_clear(dvsc->keys);
	dal_vector_clear(dvsc->store);

	/* build timing associations for all views currently in master view list
	 */
	count = dal_view_info_list_get_count(dvsc->master_view_list);
	for (i = 0; i < count; ++i)
		dal_dvsc_notify_newly_inserted_view_at_index(dvsc, i);

	dal_best_view_dump_statistics(dvsc->best_view);
	dvsc->is_valid = true;

	return true;
}

bool dal_dvsc_notify_newly_inserted_view_at_index(
	struct display_view_solution_container *dvsc, uint32_t index)
{
	const struct view *view;
	struct solution_key key = {0};
	/* TODO this should never happen. Investigate why dvsc->best_view is
	 * ever NULL! */
	if (dvsc->best_view == NULL) {
		dal_logger_write(dvsc->ctx->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_MODE_ENUM_VIEW_SOLUTION,
			" %s:%d: best_view is NULL\n", __func__, __LINE__);
		return false;
	}

	view = &(dal_view_info_list_at_index(
		dvsc->master_view_list, index)->view);

	solution_set_clear(&dvsc->output_mode_list);

	key.importance = dvsc->best_view->funcs->get_view_importance_override(
		dvsc->best_view, view);

	if (dal_best_view_match_view_to_timing(
		dvsc->best_view, view, &dvsc->output_mode_list)) {
		uint32_t i;
		uint32_t count =
			solution_set_get_count(&dvsc->output_mode_list);
		key.start_index = dal_vector_get_count(dvsc->store);

		for (i = 0; i < count; ++i) {
			if (!solution_store_vector_append(
				dvsc->store,
				solution_set_at_index(
					&dvsc->output_mode_list,
					i)))
				dal_logger_write(dvsc->ctx->logger,
					LOG_MAJOR_ERROR,
					LOG_MINOR_MODE_ENUM_VIEW_SOLUTION,
					"%s - Out of Memory\n", __func__);
		}

		key.count = dal_vector_get_count(dvsc->store) -
			key.start_index;
	} else
		key.count = 0;

	if (!solution_key_vector_insert_at(dvsc->keys, &key, index))
		dal_logger_write(dvsc->ctx->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_MODE_ENUM_VIEW_SOLUTION,
			"%s - Out of Memory\n", __func__);

	dal_logger_write(
		dvsc->ctx->logger,
		LOG_MAJOR_MODE_ENUM,
		LOG_MINOR_MODE_ENUM_VIEW_SOLUTION,
		"%s: display_path=%i, view_index=%i %ix%i start_index=%i count=%i importance=%i\n",
		__func__,
		dvsc->display_index,
		index,
		view->width,
		view->height,
		key.start_index,
		key.count,
		key.importance);

	return true;
}
