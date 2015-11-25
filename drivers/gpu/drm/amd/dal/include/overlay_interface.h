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

#ifndef __DAL_OVERLAY_INTERFACE_H__
#define __DAL_OVERLAY_INTERFACE_H__

#include "include/overlay_types.h"
#include "include/display_service_types.h"

struct ds_overlay;
struct path_mode_set;
struct path_mode;
struct view;

bool dal_ds_overlay_is_active(
	struct ds_overlay *ovl,
	uint32_t display_index);

uint32_t dal_ds_overlay_get_controller_handle(
	struct ds_overlay *ovl,
	uint32_t display_index);

enum ds_return dal_ds_overlay_alloc(
	struct ds_overlay *ovl,
	struct path_mode_set *path_mode_set,
	uint32_t display_index,
	struct view *view,
	struct overlay_data *data);

enum ds_return dal_ds_overlay_validate(
	struct ds_overlay *ovl,
	struct path_mode_set *path_mode_set,
	uint32_t display_index,
	struct view *view,
	struct overlay_data *data);

enum ds_return dal_ds_overlay_free(
	struct ds_overlay *ovl,
	struct path_mode_set *path_mode_set,
	uint32_t display_index);

enum ds_return dal_ds_overlay_get_info(
	struct ds_overlay *ovl,
	uint32_t display_index,
	enum overlay_color_space *color_space,
	enum overlay_backend_bpp *backend_bpp,
	enum overlay_alloc_option *alloc_option,
	enum overlay_format *surface_format);

enum ds_return dal_ds_overlay_set_otm(
	struct ds_overlay *ovl,
	uint32_t display_index,
	const struct path_mode *current_path_mode);

enum ds_return dal_ds_overlay_reset_otm(
	struct ds_overlay *ovl,
	uint32_t display_index,
	struct path_mode **saved_path_mode);

/**is in overlay theater mode*/
bool dal_ds_overlay_is_in_otm(
	struct ds_overlay *ovl,
	uint32_t display_index);

void dal_ds_overlay_set_matrix(
	struct ds_overlay *ovl,
	uint32_t display_index,
	const struct overlay_color_matrix *matrix);

void dal_ds_overlay_reset_matrix(
	struct ds_overlay *ovl,
	uint32_t display_index,
	enum overlay_csc_matrix_type type);

const struct overlay_color_matrix *dal_ds_overlay_get_matrix(
	struct ds_overlay *ovl,
	uint32_t display_index,
	enum overlay_csc_matrix_type type);

bool dal_ds_overlay_set_color_space(
	struct ds_overlay *ovl,
	uint32_t display_index,
	enum overlay_color_space space);

bool dal_ds_overlay_get_display_pixel_encoding(
	struct ds_overlay *ovl,
	uint32_t display_index,
	enum display_pixel_encoding *pixel_encoding);

bool dal_ds_overlay_set_display_pixel_encoding(
	struct ds_overlay *ovl,
	uint32_t display_index,
	enum display_pixel_encoding pixel_encoding);

bool dal_ds_overlay_reset_display_pixel_encoding(
	struct ds_overlay *ovl,
	uint32_t display_index);

/*After Set Overlay Theatre Mode (OTM) on a display path,
 *  saving the passed setting of Gpu scaling option for later restore*/
enum ds_return dal_ds_overlay_save_gpu_scaling_before_otm(
	struct ds_overlay *ovl,
	uint32_t display_index,
	int32_t timing_sel_before_otm);

/* After reset Overlay Theatre Mode (OTM) on a display path,
 * returning the previous Gpu scaling option by SetOverlayTheatreMode*/
enum ds_return dal_ds_overlay_get_gpu_scaling_before_otm(
	struct ds_overlay *ovl,
	uint32_t display_index,
	int32_t *timing_sel_before_otm);

uint32_t dal_ds_overlay_get_num_of_allowed(struct ds_overlay *ovl);

#endif /* __DAL_OVERLAY_INTERFACE_H__ */
