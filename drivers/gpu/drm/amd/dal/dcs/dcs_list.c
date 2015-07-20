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
#include "include/dcs_interface.h"
#include "include/timing_service_types.h"
#include "include/flat_set.h"
#include "include/vector.h"

struct dcs_mode_timing_list *dal_dcs_mode_timing_list_create(uint32_t list_size)
{
	struct dcs_mode_timing_list *list;
	struct flat_set_init_data init_data = {
			.capacity = list_size,
			.struct_size = sizeof(struct mode_timing),
			.funcs = { .less_than = dal_mode_timing_less_than } };

	list = dal_alloc(sizeof(struct dcs_mode_timing_list));

	if (!list)
		return NULL;

	if (dal_flat_set_construct(&list->list, &init_data))
		return list;

	dal_free(list);
	return NULL;
}

void dal_dcs_mode_timing_list_destroy(
	struct dcs_mode_timing_list **list)
{
	if (!list || !*list)
		return;

	dal_flat_set_destruct(&(*list)->list);
	dal_free(*list);
	*list = NULL;
}

bool dal_dcs_mode_timing_list_append(
	struct dcs_mode_timing_list *list,
	const struct mode_timing *mode_timing)
{
	return dal_flat_set_insert(&list->list, mode_timing);
}
uint32_t dal_dcs_mode_timing_list_get_count(
	const struct dcs_mode_timing_list *list)
{
	return dal_flat_set_get_count(&list->list);
}

void dal_dcs_mode_timing_list_remove_at_index(
	struct dcs_mode_timing_list *list,
	uint32_t index)
{
	dal_flat_set_remove_at_index(&list->list, index);
}

struct mode_timing *dal_dcs_mode_timing_list_at_index(
	struct dcs_mode_timing_list *list,
	uint32_t index)
{
	return dal_flat_set_at_index(&list->list, index);
}

void dal_dcs_mode_timing_list_clear(struct dcs_mode_timing_list *list)
{
	dal_flat_set_clear(&list->list);
}

struct dcs_cea_audio_mode_list {
	struct vector list;
};

struct dcs_cea_audio_mode_list *dal_dcs_cea_audio_mode_list_create(
	uint32_t list_size)
{
	struct dcs_cea_audio_mode_list *list;

	list = dal_alloc(sizeof(struct dcs_cea_audio_mode_list));

	if (!list)
		return NULL;

	if (dal_vector_construct(
		&list->list, list_size, sizeof(struct cea_audio_mode)))
		return list;

	dal_free(list);
	return NULL;
}

void dal_dcs_cea_audio_mode_list_destroy(
	struct dcs_cea_audio_mode_list **list)
{
	if (!list || !*list)
		return;

	dal_vector_destruct(&(*list)->list);
	dal_free(*list);
	*list = NULL;
}

bool dal_dcs_cea_audio_mode_list_append(
	struct dcs_cea_audio_mode_list *list,
	struct cea_audio_mode *cea_audio_mode)
{
	return dal_vector_append(&list->list, cea_audio_mode);
}
uint32_t dal_dcs_cea_audio_mode_list_get_count(
	const struct dcs_cea_audio_mode_list *list)
{
	return dal_vector_get_count(&list->list);
}

void dal_dcs_cea_audio_mode_list_clear(
	struct dcs_cea_audio_mode_list *list)
{
	list->list.count = 0;
}

struct cea_audio_mode *dal_dcs_cea_audio_mode_list_at_index(
	const struct dcs_cea_audio_mode_list *list,
	uint32_t index)
{
	return dal_vector_at_index(&list->list, index);
}

struct dcs_customized_mode_list {
	struct flat_set list;
};

struct dcs_customized_mode_list *dal_dcs_customized_mode_list_create(
	uint32_t list_size)
{
	/*TODO: add implementation*/
	return NULL;
}

bool dal_dcs_customized_mode_list_append(
	struct dcs_customized_mode_list *list,
	struct dcs_customized_mode *customized_mode)
{
	return dal_flat_set_insert(&list->list, customized_mode);
}
uint32_t dal_dcs_customized_mode_list_get_count(
	struct dcs_customized_mode_list *list)
{
	return dal_flat_set_get_count(&list->list);
}

struct dcs_customized_mode *dal_dcs_customized_mode_list_at_index(
	struct dcs_customized_mode_list *list,
	uint32_t index)
{
	return dal_flat_set_at_index(&list->list, index);
}

