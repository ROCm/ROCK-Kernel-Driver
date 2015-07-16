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
#include "include/adjustment_interface.h"
#include "include/display_path_interface.h"
#include "display_service/adjustment_types_internal.h"

#include "adjustment_api.h"

static const struct range_adjustment_api default_adj_range_dfp_table[] = {
	{ ADJ_ID_SATURATION, 100, 0, 200, 1, {0} },
	{ ADJ_ID_UNDERSCAN, 0, 0, 15, 1, {0} },
	{ ADJ_ID_BIT_DEPTH_REDUCTION, 1, 0, 23, 1, {0} },
	{ ADJ_ID_BACKLIGHT, 255, 0, 255, 1, {0} },
	{ ADJ_ID_BRIGHTNESS, 0, -100, 100, 1, {0} },
	{ ADJ_ID_CONTRAST, 100, 0, 200, 1, {0} },
	{ ADJ_ID_HUE, 0, -30, 30, 1, {0} },
	{ ADJ_ID_TEMPERATURE, 6500, 4000, 10000, 100, {0} },
	{ ADJ_ID_TEMPERATURE_SOURCE, 2, 1, 2, 1, {0} },
	{ ADJ_ID_NOMINAL_RANGE_RGB_LIMITED, 0, 0, 1, 1, {0} },
};

static const struct range_adjustment_api default_adj_range_crt_table[] = {
	{ ADJ_ID_SATURATION, 100, 0, 200, 1, {0} },
	{ ADJ_ID_BRIGHTNESS, 0, -100, 100, 1, {0} },
	{ ADJ_ID_CONTRAST, 100, 0, 200, 1, {0} },
	{ ADJ_ID_HUE, 0, -30, 30, 1, {0} },
	{ ADJ_ID_TEMPERATURE, 6500, 4000, 10000, 100, {0} },
	{ ADJ_ID_TEMPERATURE_SOURCE, 2, 1, 2, 1, {0} },
	{ ADJ_ID_NOMINAL_RANGE_RGB_LIMITED, 0, 0, 1, 1, {0} },
	{ ADJ_ID_BACKLIGHT, 255, 0, 255, 1, {0} },
};

static const struct range_adjustment_api default_adj_range_lcd_table[] = {
	{ ADJ_ID_SATURATION, 100, 0, 200, 1, {0} },
	{ ADJ_ID_UNDERSCAN, 0, 0, 10, 1, {0} },
	{ ADJ_ID_BIT_DEPTH_REDUCTION, 1, 0, 23, 1, {0} },
	{ ADJ_ID_BACKLIGHT, 255, 0, 255, 1, {0} },
	{ ADJ_ID_BRIGHTNESS, 0, -100, 100, 1, {0} },
	{ ADJ_ID_CONTRAST, 100, 0, 200, 1, {0} },
	{ ADJ_ID_HUE, 0, -30, 30, 1, {0} },
	{ ADJ_ID_TEMPERATURE, 6500, 4000, 10000, 100, {0} },
	{ ADJ_ID_TEMPERATURE_SOURCE, 2, 1, 2, 1, {0} },
	{ ADJ_ID_NOMINAL_RANGE_RGB_LIMITED, 0, 0, 1, 1, {0} },



};

static void destruct(struct adjustment_api *adj_api)
{
	if (adj_api->range_table) {
		dal_free(adj_api->range_table);
		adj_api->range_table = NULL;
	}
}

static bool build_default_adj_table(struct adjustment_api *adj_api)
{
	uint32_t i;
	uint32_t range_alloc_size = 0;
	const struct range_adjustment_api *tmp_range_table = NULL;

	switch (adj_api->adj_category) {
	case CAT_CRT:
		adj_api->range_table_size = sizeof(default_adj_range_crt_table)
		/ sizeof(default_adj_range_crt_table[0]);
		tmp_range_table = default_adj_range_crt_table;
		break;
	case CAT_DFP:
		adj_api->range_table_size = sizeof(default_adj_range_dfp_table)
		/ sizeof(default_adj_range_dfp_table[0]);
		tmp_range_table = default_adj_range_dfp_table;
		break;
	case CAT_LCD:
		adj_api->range_table_size = sizeof(default_adj_range_lcd_table)
		/ sizeof(default_adj_range_lcd_table[0]);
		tmp_range_table = default_adj_range_lcd_table;
		break;
	default:
		break;
	}
	if (adj_api->range_table_size > 0)
		range_alloc_size = adj_api->range_table_size *
			sizeof(struct range_adjustment_api);
	if (range_alloc_size > 0 && tmp_range_table != NULL) {
		adj_api->range_table = dal_alloc(range_alloc_size);
		if (adj_api->range_table) {
			for (i = 0; i < adj_api->range_table_size; i++) {
				dal_memmove(
					&adj_api->range_table[i],
					&tmp_range_table[i],
					sizeof(struct range_adjustment_api));
				adj_api->range_table[i].flag.bits.
				def_from_driver = 1;
			/* not to do read config from reg key*/
			}
		}
		if (adj_api->range_table == NULL) {
			destruct(adj_api);
			return false;
		}

		return true;
	}
	/* to do vector table in furture*/
	return false;
}

void dal_adj_api_get_api_flag(
	struct adjustment_api *adj_api,
	enum adjustment_id adj_id,
	union adjustment_api_flag *flag)
{
	uint32_t i;

	if (adj_api->range_table) {
		for (i = 0; i < adj_api->range_table_size; i++) {
			if (adj_api->range_table[i].adj_id == adj_id) {
				*flag = adj_api->range_table[i].flag;
				break;
			}
		}
	}
}

bool dal_adj_api_get_range_adj_data(
	struct adjustment_api *adj_api,
	enum adjustment_id adj_id,
	struct adjustment_info *adj_info)
{
	uint32_t i;

	if (adj_api->range_table) {
		for (i = 0; i < adj_api->range_table_size; i++) {
			if (adj_api->range_table[i].adj_id == adj_id) {
				adj_info->adj_data.ranged.def =
					adj_api->range_table[i].def;
				adj_info->adj_data.ranged.min =
					adj_api->range_table[i].min;
				adj_info->adj_data.ranged.max =
					adj_api->range_table[i].max;
				adj_info->adj_data.ranged.step =
					adj_api->range_table[i].step;
				return true;
			}
		}
	}
	return false;
}
static bool construct(
	struct adjustment_api *adj_api,
	enum adjustment_category category)
{
	if (category == CAT_INVALID)
		return false;
	adj_api->adj_category = category;
	adj_api->range_table_size = 0;
	adj_api->bit_vector_table_size = 0;
	adj_api->range_table = NULL;

	return true;
}

static bool parent_api_construct(struct adjustment_parent_api *parent_api)
{
	parent_api->api_crt = NULL;
	parent_api->api_dfp = NULL;
	parent_api->api_lcd = NULL;
	return true;
}

static void parent_api_destruct(struct adjustment_parent_api **parent_api)
{
	dal_adj_api_destroy(&(*parent_api)->api_crt);
	dal_adj_api_destroy(&(*parent_api)->api_dfp);
	dal_adj_api_destroy(&(*parent_api)->api_lcd);
}

struct adjustment_api *dal_adj_api_create(enum adjustment_category category)
{
	struct adjustment_api *adj_api;

	adj_api = dal_alloc(sizeof(*adj_api));

	if (!adj_api)
		return NULL;
	if (construct(adj_api, category))
		return adj_api;

	dal_free(adj_api);
	BREAK_TO_DEBUGGER();
	return NULL;
}

struct adjustment_parent_api *dal_adj_parent_api_create()
{
	struct adjustment_parent_api *parent_api;

	parent_api = dal_alloc(sizeof(*parent_api));
	if (!parent_api)
		return NULL;
	if (parent_api_construct(parent_api))
		return parent_api;

	dal_free(parent_api);
	BREAK_TO_DEBUGGER();
	return NULL;
}

void dal_adj_parent_api_destroy(struct adjustment_parent_api **parent_api)
{
	if (!parent_api || !*parent_api) {
		BREAK_TO_DEBUGGER();
		return;
	}
	parent_api_destruct(parent_api);
	dal_free(*parent_api);
	*parent_api = NULL;
}

void dal_adj_api_destroy(struct adjustment_api **adj_api)
{
	if (!adj_api || !*adj_api) {
		BREAK_TO_DEBUGGER();
		return;
	}
	destruct(*adj_api);
	dal_free(*adj_api);
	*adj_api = NULL;
}

bool dal_adj_parent_api_build_child_objs(struct adjustment_parent_api *adj_api)
{
	adj_api->api_crt = dal_adj_api_create(CAT_CRT);
	if (!adj_api->api_crt || !build_default_adj_table(adj_api->api_crt))
		return false;

	adj_api->api_dfp = dal_adj_api_create(CAT_DFP);
	if (!adj_api->api_dfp || !build_default_adj_table(adj_api->api_dfp))
		return false;

	adj_api->api_lcd = dal_adj_api_create(CAT_LCD);
	if (!adj_api->api_lcd || !build_default_adj_table(adj_api->api_lcd))
		return false;
	return true;
}

struct adjustment_api *dal_adj_parent_api_what_is_the_target_obj(
	struct adjustment_parent_api *adj_api,
	enum signal_type signal)
{
	switch (signal) {
	case SIGNAL_TYPE_DVI_SINGLE_LINK:
	case SIGNAL_TYPE_DVI_SINGLE_LINK1:
	case SIGNAL_TYPE_DVI_DUAL_LINK:
	case SIGNAL_TYPE_HDMI_TYPE_A:
	case SIGNAL_TYPE_DISPLAY_PORT:
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
	case SIGNAL_TYPE_WIRELESS:
		return adj_api->api_dfp;
	case SIGNAL_TYPE_EDP:
		return adj_api->api_lcd;
	case SIGNAL_TYPE_RGB:
		return adj_api->api_crt;
	default:
		return NULL;
	}
}
