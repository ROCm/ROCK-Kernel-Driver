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
#include "include/display_path_interface.h"
#include "include/dcs_interface.h"
#include "include/fixed31_32.h"

#include "adjustment_container.h"

/*no used for now
static struct adj_container *get_adj_container_for_path(void)
{
	return NULL;
}
*/
void dal_adj_container_update_display_cap(
	struct adj_container *container,
	struct display_path *display_path)
{
	struct vendor_product_id_info vendor_info = {0};
	struct cea861_support cea861_support = { 0 };
	union cea_video_capability_data_block video_cap = { {0} };
	struct dcs *dcs = dal_display_path_get_dcs(display_path);

	dal_dcs_get_vendor_product_id_info(dcs, &vendor_info);
	if (!((container->ctx.edid_signature.manufacturer_id ==
		vendor_info.manufacturer_id) && (container->ctx.
		edid_signature.product_id == vendor_info.product_id))) {
		/*new edid,update required */
		container->ctx.update_required = true;
		container->ctx.edid_signature = vendor_info;
		dal_dcs_get_display_characteristics(
			dcs,
			&container->ctx.display_characteristics);
	/* CEA861 support block from edid */

		if (dal_dcs_get_cea861_support(dcs, &cea861_support)) {
			container->ctx.cea861_support = cea861_support;
			container->ctx.valid.CEA861_SUPPORT = true;
		}
		/* get CEA861 vcblock from edid*/
		if (dal_dcs_get_cea_video_capability_data_block(
			dcs, &video_cap)) {
			container->ctx.vcblock = video_cap;
			container->ctx.valid.VCBLOCK = true;
		}
	}
}

bool dal_adj_container_get_default_underscan_allow(
	struct adj_container *container)
{
	return container->ctx.flags.NO_DEFAULT_UNDERSCAN;
}

void dal_adj_container_set_default_underscan_allow(
	struct adj_container *container,
	bool restricted)
{
	container->ctx.flags.NO_DEFAULT_UNDERSCAN = restricted;
}

struct adj_container *dal_adj_container_create()
{

	struct adj_container *container = dal_alloc(sizeof(*container));

	if (!container) {
		dal_free(container);
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	return container;
}

void dal_adj_container_destroy(struct adj_container **container)
{
	if (!container || !*container) {
		BREAK_TO_DEBUGGER();
		return;
	}
	dal_free(*container);
	*container = NULL;
}

bool dal_adj_container_get_scan_type(
		const struct adj_container *container,
		enum scanning_type *scanning_type)
{
	if (container->ctx.valid.SCAN_TYPE) {
		*scanning_type = container->ctx.scan_type;
		return true;
	}
	return false;
}

bool dal_adj_container_get_display_content_capability(
	const struct adj_container *container,
	union display_content_support *support)
{
	if (container->ctx.valid.DISP_CONTENT_SUPPORT) {
		*support = container->ctx.disp_content_support;
		return true;
	}
	return false;
}

bool dal_adj_container_get_adjustment_val(
		const struct adj_container *adj_container,
		enum adjustment_id adj_id,
		uint32_t *val)
{
	/* TODO: implementation of adjustment */
	return false;
}


void dal_adj_info_set_clear(struct adj_info_set *adj_info_set)
{
	dal_memset(
		adj_info_set->adj_info_array,
		0,
		MAX_ADJUSTMENT_NUM * sizeof(struct adjustment_info));
}

void dal_adj_info_set_add_adj_info(
	struct adj_info_set *adj_info_set,
	struct adjustment_info *adj_info)
{
	adj_info_set->adj_info_array[adj_info->adj_id] = *adj_info;
	adj_info_set->adj_info_array[adj_info->adj_id].adj_state =
		ADJUSTMENT_STATE_VALID;
}

static void copy_contents_from(
	struct adj_info_set *adj_info_set,
	const struct adj_info_set *adj_info_set_src)
{
	uint32_t i;

	for (i = 0; i < MAX_ADJUSTMENT_NUM; i++)
		adj_info_set->adj_info_array[i] =
			adj_info_set_src->adj_info_array[i];
}

struct adjustment_info *dal_adj_info_set_get_adj_info(
	struct adj_info_set *adj_info_set,
	enum adjustment_id adj_id)
{
	if (adj_id <= ADJ_ID_END && adj_id != ADJ_ID_INVALID) {
		if (adj_info_set->adj_info_array[adj_id].adj_state !=
			ADJUSTMENT_STATE_INVALID)
			return &adj_info_set->adj_info_array[adj_id];
	}
	return NULL;
}

const struct mode_info *dal_adj_container_get_mode_info(
	struct adj_container *container)
{
	return &container->ctx.mode_info;
}

const struct view *dal_adj_container_get_view(
	struct adj_container *container)
{
	return &container->ctx.view;
}

void dal_adj_container_update_timing_mode(
	struct adj_container *container,
	const struct mode_info *mode_info,
	const struct view *view)
{
	if (mode_info && ((container->ctx.mode_info.field_rate != mode_info->
		field_rate) || (container->ctx.mode_info.pixel_width !=
			mode_info->pixel_width) || (container->ctx.mode_info.
				pixel_height != mode_info->pixel_height) ||
				(container->ctx.view.height != view->height) ||
					(container->ctx.view.width !=
						view->width))) {

		container->ctx.view = *view;
		container->ctx.mode_info = *mode_info;
		container->ctx.update_required = true;
	}
}

bool dal_adj_container_get_cea861_support(
	const struct adj_container *container,
	struct cea861_support *cea861_support)
{
	if (container->ctx.cea861_support.revision) {
		*cea861_support = container->ctx.cea861_support;
		return true;
	}
	return false;
}

bool dal_adj_container_get_cea_video_cap_data_block(
	const struct adj_container *container,
	union cea_video_capability_data_block *vcblock)
{
	if (container->ctx.valid.VCBLOCK) {
		*vcblock = container->ctx.vcblock;
		return true;
	}
	return false;
}

void dal_adj_container_update_color_space(
	struct adj_container *container,
	enum ds_color_space color_space)
{
	container->ctx.color_space = color_space;
	container->ctx.valid.COLOR_SPACE = true;
}

void dal_adj_container_update_signal_type(
	struct adj_container *container,
	enum signal_type signal_type)
{
	container->ctx.signal_type = signal_type;
	container->ctx.valid.SIGNAL_TYPE = true;
}

enum signal_type dal_adj_container_get_signal_type(
	struct adj_container *container)
{
	if (container->ctx.valid.SIGNAL_TYPE)
		return container->ctx.signal_type;
	return SIGNAL_TYPE_NONE;
}

enum ds_color_space dal_adj_container_get_color_space(
	const struct adj_container *container)
{
	if (container->ctx.valid.COLOR_SPACE)
		return container->ctx.color_space;
	return DS_COLOR_SPACE_UNKNOWN;
}

const struct display_characteristics *dal_adj_container_get_disp_character(
	struct adj_container *container)
{
	if (container->ctx.valid.DISPLAY_CHARACTERISTICS)
		return &container->ctx.display_characteristics;

	return NULL;
}

void dal_adj_container_copy_contents_from(
	struct adj_container *container,
	struct adj_container *container_src)
{
	copy_contents_from(
		&container->adj_info_set,
		&container_src->adj_info_set);
	container->ctx = container_src->ctx;
}

bool dal_adj_container_commit_adj(
	struct adj_container *container,
	enum adjustment_id adj_id)
{
	struct adjustment_info *info = dal_adj_info_set_get_adj_info(
		&container->adj_info_set, adj_id);
	if (info) {
		info->adj_state = ADJUSTMENT_STATE_COMMITTED_TO_HW;
		return true;
	}
	return false;
}

bool dal_adj_container_is_adjustment_committed(
	struct adj_container *container,
	enum adjustment_id adj_id)
{
	struct adjustment_info *info = dal_adj_info_set_get_adj_info(
		&container->adj_info_set, adj_id);
	if (info)
		return (info->adj_state == ADJUSTMENT_STATE_COMMITTED_TO_HW);

	return false;
}

bool dal_adj_info_set_get_adj_val(
	struct adj_info_set *adj_info_set,
	enum adjustment_id adj_id,
	int32_t *val)
{
	struct adjustment_info *info = dal_adj_info_set_get_adj_info(
		adj_info_set, adj_id);
	if (info && val) {
		*val = info->adj_data.ranged.cur;
		return true;
	}
	return false;
}

bool dal_adj_info_set_update_cur_value(
	struct adj_info_set *adj_info_set,
	enum adjustment_id adj_id,
	int32_t val)
{
	struct adjustment_info *info = dal_adj_info_set_get_adj_info(
		adj_info_set, adj_id);
	if (info) {
		info->adj_data.ranged.cur = val;
		info->adj_state = ADJUSTMENT_STATE_REQUESTED;
		return true;
	}
	return false;
}

bool dal_adj_container_is_update_required(struct adj_container *adj_container)
{
	return adj_container->ctx.update_required;
}

void dal_adj_container_updated(struct adj_container *adj_container)
{
	adj_container->ctx.update_required = false;
}

bool dal_adj_container_validate_regamma(
	struct adj_container *adj_container,
	const struct ds_regamma_lut *data)
{
	uint32_t i;

	struct fixed31_32 a0;
	struct fixed31_32 a1;
	struct fixed31_32 a2;
	struct fixed31_32 a3;
	struct fixed31_32 gamma;
	struct fixed31_32 divide;
	struct fixed31_32 discrete_increment;
	struct fixed31_32 linear_end;
	struct fixed31_32 exp_start;
	bool ret = true;

	if (data->flags.bits.GAMMA_RAMP_ARRAY == 0) {
		for (i = 0; i < COEFF_RANGE && ret ; i++) {
			if (data->coeff.coeff_a0[i] == 0 ||
				data->coeff.coeff_a1[i] == 0 ||
				data->coeff.coeff_a2[i] == 0 ||
				data->coeff.coeff_a3[i] == 0 ||
				data->coeff.gamma[i] == 0) {
				ret = false;
				break;
			}

			a0 = dal_fixed31_32_from_int(
					(int64_t)data->coeff.coeff_a0[i]);
			a1 = dal_fixed31_32_from_int(
					(int64_t)data->coeff.coeff_a1[i]);
			a2 = dal_fixed31_32_from_int(
					(int64_t)data->coeff.coeff_a2[i]);
			a3 = dal_fixed31_32_from_int(
					(int64_t)data->coeff.coeff_a3[i]);
			gamma = dal_fixed31_32_from_int(
					(int64_t)data->coeff.gamma[i]);

			a0 = dal_fixed31_32_div_int(
					a0,	ADJUST_DIVIDER_2);
			a1 = dal_fixed31_32_div_int(
					a1, ADJUST_DIVIDER_1);
			a2 = dal_fixed31_32_div_int(
					a2, ADJUST_DIVIDER_1);
			a3 = dal_fixed31_32_div_int(
					a3,	ADJUST_DIVIDER_1);
			gamma = dal_fixed31_32_div_int(
					gamma, ADJUST_DIVIDER_1);

			divide = dal_fixed31_32_one;
			discrete_increment = dal_fixed31_32_one;

			if (dal_fixed31_32_lt(
				dal_fixed31_32_from_int(
						(int64_t)REGAMMA_CONSTANT_1),
				dal_fixed31_32_div(divide, a0)))
				discrete_increment = dal_fixed31_32_div_int(
						discrete_increment, 8192);
			else if (dal_fixed31_32_lt(
				dal_fixed31_32_from_int(
						(int64_t)REGAMMA_CONSTANT_2),
				dal_fixed31_32_div(divide, a0)))
				discrete_increment = dal_fixed31_32_div_int(
						discrete_increment, 4096);
			else if (dal_fixed31_32_lt(
				dal_fixed31_32_from_int(
						(int64_t)REGAMMA_CONSTANT_3),
				dal_fixed31_32_div(divide, a0)))
				discrete_increment = dal_fixed31_32_div_int(
						discrete_increment, 2048);
			else
				discrete_increment = dal_fixed31_32_div_int(
						discrete_increment, 1024);

			linear_end = dal_fixed31_32_mul(
					dal_fixed31_32_sub(
						a0, discrete_increment), a1);

			discrete_increment = dal_fixed31_32_one;
			exp_start = dal_fixed31_32_div(
					discrete_increment, gamma);
			exp_start = dal_fixed31_32_pow(a0, exp_start);

			exp_start = dal_fixed31_32_mul(
					(dal_fixed31_32_add(
						discrete_increment, a3)),
						exp_start);

			exp_start = dal_fixed31_32_sub(
						exp_start, a2);

			if (dal_fixed31_32_lt(
					exp_start, linear_end)) {
				ret = false;
				break;
			}
		}
	} else {
		for (i = 1; i < REGAMMA_VALUE ; i++) {
			if (data->gamma.gamma[i] < data->gamma.gamma[i-1]) {
				ret = false;
				break;
			}
			if (data->gamma.gamma[i+256] <
					data->gamma.gamma[i-1+256]) {
				ret = false;
				break;
			}
			if (data->gamma.gamma[i+512] <
					data->gamma.gamma[i-1+512]) {
				ret = false;
				break;
			}
		}
	}
	return ret;
}

bool dal_adj_container_set_regamma(
	struct adj_container *adj_container,
	const struct ds_regamma_lut *regamma)
{
	if (!dal_adj_container_validate_regamma(
			adj_container, regamma))
		return false;

	adj_container->ctx.valid.REGAMMA = true;
	adj_container->ctx.regamma = *regamma;

	return true;
}

const struct ds_regamma_lut *dal_adj_container_get_regamma(
	struct adj_container *adj_container)
{

	if (adj_container->ctx.valid.REGAMMA == true)
		return &adj_container->ctx.regamma;
	return NULL;
}

bool dal_adj_container_get_gamut(
	struct adj_container *adj_container,
	enum adjustment_id adj_id,
	struct gamut_data *data)
{
	bool ret = false;

	switch (adj_id) {
	case ADJ_ID_GAMUT_SOURCE_GRPH:
		if (adj_container->ctx.valid.GAMUT_SRC_GRPH == true) {
			*data = adj_container->ctx.gamut_src_grph;
			ret = true;
		}
		break;
	case ADJ_ID_GAMUT_SOURCE_OVL:
		if (adj_container->ctx.valid.GAMUT_SRC_OVL == true) {
			*data = adj_container->ctx.gamut_src_ovl;
			ret = true;
		}
		break;
	case ADJ_ID_GAMUT_DESTINATION:
		if (adj_container->ctx.valid.GAMUT_DST == true) {
			*data = adj_container->ctx.gamut_dst;
			ret = true;
		}
		break;
	default:
		break;
	}
	return ret;
}

bool dal_adj_container_validate_gamut(
	struct adj_container *adj_container,
	struct gamut_data *data)
{
	if (data->option.bits.CUSTOM_GAMUT_SPACE == 1) {
		if (data->gamut.custom.red_x == 0 ||
			data->gamut.custom.red_y == 0 ||
			data->gamut.custom.green_x == 0 ||
			data->gamut.custom.green_y == 0 ||
			data->gamut.custom.blue_x == 0 ||
			data->gamut.custom.blue_y == 0)
			return false;
	} else
		if (data->gamut.predefined.u32all == 0)
			return false;

	if (data->option.bits.CUSTOM_WHITE_POINT == 1) {
		if (data->white_point.custom.white_x == 0 ||
			data->white_point.custom.white_y == 0)
			return false;
	} else
		if (data->white_point.predefined.u32all == 0)
			return false;

	return true;
}

bool dal_adj_container_update_gamut(
	struct adj_container *adj_container,
	enum adjustment_id adj_id,
	struct gamut_data *data)
{
	if (!dal_adj_container_validate_gamut(
			adj_container, data))
		return false;

	switch (adj_id) {
	case ADJ_ID_GAMUT_SOURCE_GRPH:
		adj_container->ctx.gamut_src_grph = *data;
		adj_container->ctx.valid.GAMUT_SRC_GRPH = true;
		break;
	case ADJ_ID_GAMUT_SOURCE_OVL:
		adj_container->ctx.gamut_src_ovl = *data;
		adj_container->ctx.valid.GAMUT_SRC_OVL = true;
		break;
	case ADJ_ID_GAMUT_DESTINATION:
		adj_container->ctx.gamut_dst = *data;
		adj_container->ctx.valid.GAMUT_DST = true;
		break;
	default:
		adj_container->ctx.valid.GAMUT_SRC_GRPH = false;
		adj_container->ctx.valid.GAMUT_SRC_OVL = false;
		adj_container->ctx.valid.GAMUT_DST = false;
		break;
	}
	return true;
}

bool dal_adj_container_get_regamma_copy(
	struct adj_container *adj_container,
	struct ds_regamma_lut *regamma)
{
	if (adj_container->ctx.valid.REGAMMA == true) {
		*regamma = adj_container->ctx.regamma;
		return true;
	}
	return false;
}
