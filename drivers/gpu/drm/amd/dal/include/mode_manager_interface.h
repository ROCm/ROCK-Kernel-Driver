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

#ifndef __DAL_MODE_MANAGER_INTEFACE_H__
#define __DAL_MODE_MANAGER_INTEFACE_H__

#include "mode_query_interface.h"
#include "mode_manager_types.h"

struct mode_timing_list;

struct mode_manager;
struct mode_manager_init_data {
	const struct default_mode_list *default_modes;
	struct adapter_service *as;
	struct dal_context *dal_context;
};

struct mode_manager *dal_mode_manager_create(
	const struct mode_manager_init_data *init_data);

void dal_mode_manager_destroy(struct mode_manager **mm);

uint32_t dal_mode_manager_get_supported_pixel_format(
	const struct mode_manager *mm);

void dal_mode_manager_set_supported_pixel_format(
	struct mode_manager *mm,
	uint32_t mask);

struct bestview_options dal_mode_manager_get_default_bestview_options(
	struct mode_manager *mm,
	uint32_t display_index);

struct bestview_options dal_mode_manager_get_bestview_options(
	struct mode_manager *mode_mgr,
	uint32_t display_index);

void dal_mode_manager_set_bestview_options(
	struct mode_manager *mode_mgr,
	uint32_t display_index,
	const struct bestview_options *bv_options,
	bool rebuild_best_view,
	struct mode_timing_list *mtl);

/* Updates the cached render view table
 * associated with given DisplayPath accordingly
 */
bool dal_mode_manager_update_disp_path_func_view_tbl(
	struct mode_manager *mode_mgr,
	uint32_t display_index,
	struct mode_timing_list *mtl);

/* generate all supported render modes
 * and the corresponding cofunctional output mode set
 * according to the given mode query parameter*/
struct topology;
struct mode_query *dal_mode_manager_create_mode_query(
	struct mode_manager *mm,
	const struct topology *topology,
	const enum query_option query_option);

/* Fills passed PathModeSet with the pathModes
 * returned from querying specific mode
 * Fallback options in this order:
 * 1. Try interlaced refresh rate (if interlaced wasn't requested)
 * 2. Try any refresh rate
 * Returns true if PathModeSet for requested mode was found, false otherwise
 * Updates renderMode + refreshRate if due to fallback found mode is different*/
bool dal_mode_manager_retreive_path_mode_set(
	struct mode_manager *mode_mgr,
	struct path_mode_set *path_mode_set,
	struct render_mode *render_mode,
	struct refresh_rate *refresh_rate,
	const struct topology *topology,
	enum query_option query_option,
	bool allow_fallback);

bool dal_mode_manager_are_mode_queries_cofunctional(
	const struct mode_manager *mode_mgr,
	struct mode_query *ap_mode_queries,
	uint32_t count);

void dal_mode_manager_set_ds_dispatch(
	struct mode_manager *mm,
	struct ds_dispatch *ds_dispatch);

#endif /* __DAL_MODE_MANAGER_INTEFACE_H__ */
