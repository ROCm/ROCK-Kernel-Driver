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

#ifndef __DAL_ADJUSTMENT_CONTAINER_H__
#define __DAL_ADJUSTMENT_CONTAINER_H__

#include "include/adjustment_types.h"
#include "include/dcs_types.h"
#include "include/timing_service_types.h"
#include "include/set_mode_types.h"
#include "include/signal_types.h"
#include "display_service/adjustment_types_internal.h"

#define ADJUST_DIVIDER_1	1000
#define ADJUST_DIVIDER_2	10000000
#define REGAMMA_CONSTANT_1	256
#define REGAMMA_CONSTANT_2	128
#define REGAMMA_CONSTANT_3	64

struct adj_info_set {
	struct adjustment_info adj_info_array[MAX_ADJUSTMENT_NUM];
};

struct adj_container {
	/* to-do,not 100% be ported */
	struct adj_info_set adj_info_set;

	struct {
		bool update_required;
		struct mode_info mode_info;
		struct view view;
		struct cea861_support cea861_support;
		struct vendor_product_id_info edid_signature;
		struct display_characteristics display_characteristics;
		union cea_video_capability_data_block vcblock;
		enum ds_color_space color_space;
		enum signal_type signal_type;
		enum scanning_type scan_type;
		union display_content_support disp_content_support;
		struct gamut_data gamut_src_grph;
		struct gamut_data gamut_src_ovl;
		struct gamut_data gamut_dst;
		struct ds_regamma_lut regamma;

		struct {
			bool DISPLAY_CHARACTERISTICS:1;
			bool COLOR_SPACE:1;
			bool VCBLOCK:1;
			bool CEA861_SUPPORT:1;
			bool SIGNAL_TYPE:1;
			bool SCAN_TYPE:1;
			bool GAMUT_SRC_GRPH:1;
			bool GAMUT_SRC_OVL:1;
			bool GAMUT_DST:1;
			bool REGAMMA:1;
			bool DISP_CONTENT_SUPPORT:1;
		} valid;
		struct {
			bool NO_DEFAULT_UNDERSCAN:1;
		} flags;
	} ctx;
};

union display_content_support;

bool dal_adj_container_get_default_underscan_allow(
	struct adj_container *container);

void dal_adj_container_update_display_cap(
	struct adj_container *container,
	struct display_path *display_path);

void dal_adj_container_set_default_underscan_allow(
	struct adj_container *container,
	bool restricted);

void dal_adj_container_destroy(struct adj_container **container);

const struct display_characteristics *dal_adj_container_get_disp_character(
	struct adj_container *container);

enum ds_color_space dal_adj_container_get_color_space(
	const struct adj_container *container);

enum signal_type dal_adj_container_get_signal_type(
	struct adj_container *container);

void dal_adj_container_update_color_space(
	struct adj_container *container,
	enum ds_color_space color_space);

void dal_adj_container_update_signal_type(
	struct adj_container *container,
	enum signal_type signal_type);

bool dal_adj_container_get_cea861_support(
	const struct adj_container *container,
	struct cea861_support *cea861_support);

bool dal_adj_container_get_cea_video_cap_data_block(
	const struct adj_container *container,
	union cea_video_capability_data_block *vcblock);

const struct mode_info *dal_adj_container_get_mode_info(
	struct adj_container *container);

const struct view *dal_adj_container_get_view(
	struct adj_container *container);

bool dal_adj_container_is_adjustment_committed(
	struct adj_container *container,
	enum adjustment_id adj_id);

bool dal_adj_container_commit_adj(
	struct adj_container *container,
	enum adjustment_id adj_id);

void dal_adj_container_updated(struct adj_container *adj_container);

struct adj_container *dal_adj_container_create(void);

struct adjustment_info *dal_adj_info_set_get_adj_info(
	struct adj_info_set *adj_info_set,
	enum adjustment_id adj_id);

bool dal_adj_container_get_scan_type(
	const struct adj_container *adj_container,
	enum scanning_type *scanning_type);

/* TODO: implementation of adjustment */
bool dal_adj_container_get_display_content_capability(
	const struct adj_container *adj_container,
	union display_content_support *support);

/* TODO: implementation of adjustment */
bool dal_adj_container_get_adjustment_val(
	const struct adj_container *adj_container,
	enum adjustment_id adj_id,
	uint32_t *val);

bool dal_adj_info_set_update_cur_value(
	struct adj_info_set *adj_info_set,
	enum adjustment_id adj_id,
	int32_t val);

bool dal_adj_container_is_update_required(
	struct adj_container *adj_container);

void dal_adj_info_set_add_adj_info(
	struct adj_info_set *adj_info_set,
	struct adjustment_info *adj_info);

void dal_adj_info_set_clear(struct adj_info_set *adj_info_set);

void dal_adj_container_update_timing_mode(
	struct adj_container *container,
	const struct mode_info *mode_info,
	const struct view *view);

bool dal_adj_container_set_regamma(
	struct adj_container *adj_container,
	const struct ds_regamma_lut *regamma);

bool dal_adj_container_get_gamut(
	struct adj_container *adj_container,
	enum adjustment_id adj_id,
	struct gamut_data *data);

const struct ds_regamma_lut *dal_adj_container_get_regamma(
	struct adj_container *adj_container);

bool dal_adj_container_validate_gamut(
	struct adj_container *adj_container,
	struct gamut_data *data);

bool dal_adj_container_update_gamut(
	struct adj_container *adj_container,
	enum adjustment_id adj_id,
	struct gamut_data *data);

bool dal_adj_container_get_regamma_copy(
	struct adj_container *adj_container,
	struct ds_regamma_lut *regamma);

#endif /* __DAL_ADJUSTMENT_CONTAINER_H__ */
