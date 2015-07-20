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

#ifndef __DAL_ADJUSTMENT_INTERFACE_H__
#define __DAL_ADJUSTMENT_INTERFACE_H__

#include "include/display_service_types.h"
#include "include/adjustment_types.h"
#include "include/overlay_types.h"
#include "include/display_path_interface.h"

struct ds_underscan_desc;
struct adj_container;
struct info_frame;
struct ds_dispatch;
struct hw_adjustment_set;
struct path_mode;
struct hw_path_mode;

enum build_path_set_reason;

bool dal_ds_dispatch_is_adjustment_supported(
	struct ds_dispatch *ds,
	uint32_t display_index,
	enum adjustment_id adjust_id);

enum ds_return dal_ds_dispatch_get_type(
	struct ds_dispatch *adj,
	enum adjustment_id adjust_id,
	enum adjustment_data_type *type);

enum ds_return dal_ds_dispatch_get_property(
	struct ds_dispatch *adj,
	uint32_t display_index,
	enum adjustment_id adjust_id,
	union adjustment_property *property);

enum ds_return dal_ds_dispatch_set_adjustment(
	struct ds_dispatch *ds,
	const uint32_t display_index,
	enum adjustment_id adjust_id,
	int32_t value);

enum ds_return dal_ds_dispatch_get_adjustment_current_value(
	struct ds_dispatch *ds,
	struct adj_container *container,
	struct adjustment_info *info,
	enum adjustment_id id,
	bool fall_back_to_default);

enum ds_return dal_ds_dispatch_get_adjustment_value(
	struct ds_dispatch *ds,
	struct display_path *disp_path,
	enum adjustment_id adj_id,
	bool fall_back_to_default,
	int32_t *value);

const struct raw_gamma_ramp *dal_ds_dispatch_get_current_gamma(
	struct ds_dispatch *ds,
	uint32_t display_index,
	enum adjustment_id adjust_id);

const struct raw_gamma_ramp *dal_ds_dispatch_get_default_gamma(
	struct ds_dispatch *ds,
	uint32_t display_index,
	enum adjustment_id adjust_id);

enum ds_return dal_ds_dispatch_set_current_gamma(
	struct ds_dispatch *ds,
	uint32_t display_index,
	enum adjustment_id adjust_id,
	const struct raw_gamma_ramp *gamma);

enum ds_return dal_ds_dispatch_set_gamma(
	struct ds_dispatch *ds,
	uint32_t display_index,
	enum adjustment_id adjust_id,
	const struct raw_gamma_ramp *gamma);

bool dal_ds_dispatch_get_underscan_info(
	struct ds_dispatch *ds,
	uint32_t display_index,
	struct ds_underscan_info *info);

bool dal_ds_dispatch_get_underscan_mode(
	struct ds_dispatch *ds,
	uint32_t display_index,
	struct ds_underscan_desc *desc);

bool dal_ds_dispatch_set_underscan_mode(
	struct ds_dispatch *ds,
	uint32_t display_index,
	struct ds_underscan_desc *desc);

bool dal_ds_dispatch_setup_overlay(
	struct ds_dispatch *adj,
	uint32_t display_index,
	struct overlay_data *data);

struct adj_container *dal_ds_dispatch_get_adj_container_for_path(
	const struct ds_dispatch *ds,
	uint32_t display_index);

void dal_ds_dispatch_set_applicable_adj(
	struct ds_dispatch *adj,
	uint32_t display_index,
	const struct adj_container *applicable);

enum ds_return dal_ds_dispatch_set_color_gamut(
	struct ds_dispatch *adj,
	uint32_t display_index,
	const struct ds_set_gamut_data *data);

enum ds_return dal_ds_dispatch_get_color_gamut(
	struct ds_dispatch *adj,
	uint32_t display_index,
	const struct ds_gamut_reference_data *ref,
	struct ds_get_gamut_data *data);

enum ds_return dal_ds_dispatch_get_color_gamut_info(
	struct ds_dispatch *adj,
	uint32_t display_index,
	const struct ds_gamut_reference_data *ref,
	struct ds_gamut_info *data);

enum ds_return dal_ds_dispatch_get_regamma_lut(
	struct ds_dispatch *adj,
	uint32_t display_index,
	struct ds_regamma_lut *data);

enum ds_return dal_ds_dispatch_set_regamma_lut(
	struct ds_dispatch *adj,
	uint32_t display_index,
	struct ds_regamma_lut *data);

enum ds_return dal_ds_dispatch_set_info_packets(
	struct ds_dispatch *adj,
	uint32_t display_index,
	const struct info_frame *info_frames);

enum ds_return dal_ds_dispatch_get_info_packets(
	struct ds_dispatch *adj,
	uint32_t display_index,
	struct info_frame *info_frames);

bool dal_ds_dispatch_initialize_adjustment(struct ds_dispatch *ds);

void dal_ds_dispatch_cleanup_adjustment(struct ds_dispatch *ds);

bool dal_ds_dispatch_build_post_set_mode_adj(
	struct ds_dispatch *ds,
	const struct path_mode *mode,
	struct display_path *display_path,
	struct hw_adjustment_set *set);

bool dal_ds_dispatch_build_color_control_adj(
	struct ds_dispatch *ds,
	const struct path_mode *mode,
	struct display_path *display_path,
	struct hw_adjustment_set *set);

bool dal_ds_dispatch_build_include_adj(
	struct ds_dispatch *ds,
	const struct path_mode *mode,
	struct display_path *display_path,
	struct hw_path_mode *hw_mode,
	struct hw_adjustment_set *set);

bool dal_ds_dispatch_apply_scaling(
	struct ds_dispatch *ds,
	const struct path_mode *mode,
	struct adj_container *adj_container,
	enum build_path_set_reason reason,
	struct hw_path_mode *hw_mode);

void dal_ds_dispatch_update_adj_container_for_path_with_mode_info(
	struct ds_dispatch *ds,
	struct display_path *display_path,
	const struct path_mode *path_mode);

enum ds_return dal_ds_dispatch_get_adjustment_info(
	struct ds_dispatch *ds,
	uint32_t display_index,
	enum adjustment_id adjust_id,
	struct adjustment_info *adj_info);

bool dal_ds_dispatch_include_adjustment(
	struct ds_dispatch *ds,
	struct display_path *disp_path,
	struct ds_adj_id_value adj,
	struct hw_adjustment_set *set);

enum ds_return dal_ds_dispatch_set_gamma_adjustment(
	struct ds_dispatch *ds,
	uint32_t display_index,
	enum adjustment_id ad_id,
	const struct raw_gamma_ramp *gamma);

void dal_ds_dispatch_update_adj_container_for_path_with_color_space(
	struct ds_dispatch *ds,
	uint32_t display_index,
	enum ds_color_space color_space);

void dal_ds_dispatch_setup_default_regamma(
	struct ds_dispatch *ds,
	struct ds_regamma_lut *regamma);

#endif /* __DAL_ADJUSTMENT_INTERFACE_H__ */
