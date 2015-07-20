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

/* Include */
#include "dal_services.h"
#include "include/set_mode_interface.h"
#include "include/hw_sequencer_types.h"
#include "ds_dispatch.h"

/* Create path mode set */
struct path_mode_set *dal_pms_create()
{
	struct path_mode_set *set;

	set = dal_alloc(sizeof(struct path_mode_set));

	if (set == NULL)
		return NULL;

	if (dal_pms_construct(set))
		return set;

	dal_free(set);
	BREAK_TO_DEBUGGER();
	return NULL;
}

void dal_pms_destroy(struct path_mode_set **pms)
{
	if (!pms || !*pms) {
		BREAK_TO_DEBUGGER();
		return;
	}

	dal_free(*pms);
	*pms = NULL;
}

/* Create a copy of given path mode set */
struct path_mode_set *dal_pms_copy(const struct path_mode_set *copy)
{
	struct path_mode_set *set = NULL;

	if (copy == NULL) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	set = dal_alloc(sizeof(struct path_mode_set));

	if (set == NULL)
		return NULL;

	if (dal_pms_construct(set)) {
		uint32_t i = 0;

		set->count = copy->count;
		set->control_flags.all = copy->control_flags.all;

		for (i = 0; i < set->count; i++)
			set->path_mode_set[i] = copy->path_mode_set[i];

		return set;
	}

	dal_free(set);
	BREAK_TO_DEBUGGER();
	return NULL;


}

/* Constructor for path mode set */
bool dal_pms_construct(struct path_mode_set *set)
{
	if (set == NULL)
		return false;

	set->count = 0;
	set->control_flags.all = 0;

	return true;
}

/* Add a path mode into the set */
bool dal_pms_add_path_mode(
		struct path_mode_set *set,
		const struct path_mode *path_mode)
{
	if (set->count >= MAX_COFUNC_PATH)
		return false;

	/* Check if display index is already in the set */
	if (dal_pms_get_path_mode_for_display_index(
			set, path_mode->display_path_index) != NULL)
		return false;

	set->path_mode_set[set->count] = *path_mode;
	set->count++;

	return true;
}

/* Get number of path modes in the set */
uint32_t dal_pms_get_path_mode_num(const struct path_mode_set *set)
{
	if (set == NULL) {
		BREAK_TO_DEBUGGER();
		return 0;
	}
	return set->count;
}

/* Return the path mode at the index */
const struct path_mode *dal_pms_get_path_mode_at_index(
		const struct path_mode_set *set,
		uint32_t index)
{
	if (index >= set->count)
		return NULL;
	else
		return &set->path_mode_set[index];
}

/* Return the path mode for the given display index */
const struct path_mode *dal_pms_get_path_mode_for_display_index(
		const struct path_mode_set *set,
		uint32_t index)
{
	uint32_t i;

	for (i = 0; i < set->count; i++) {
		if (set->path_mode_set[i].display_path_index == index)
			return &set->path_mode_set[i];
	}
	return NULL;
}

/* Add control flag to keep display powered off */
void dal_pms_keep_display_powered_off(
		struct path_mode_set *set,
		bool keep)
{
	set->control_flags.bits.KEEP_DISPLAY_POWERED_OFF = keep;
}

/* Return control flag if display needs to be kept powered off */
bool dal_pms_is_display_power_off_required(const struct path_mode_set *set)
{
	return set->control_flags.bits.KEEP_DISPLAY_POWERED_OFF;
}

/* Add control flag to not use default underscan*/
void dal_pms_fallback_remove_default_underscan(
		struct path_mode_set *set,
		bool lean)
{
	/* TODO: implementation */
}

/* Return control flag if default underscan is not used */
bool dal_pms_is_fallback_no_default_underscan_enabled(
		struct path_mode_set *set)
{
	return false;
}

/* Remove path mode at index from the set */
bool dal_pms_remove_path_mode_at_index(
	struct path_mode_set *set,
	uint32_t index)
{
	if (index < set->count) {
		uint32_t i = 0;

		for (i = index; i < set->count; i++)
			set->path_mode_set[i] = set->path_mode_set[i + 1];
		set->count--;
	} else
		return false;

	return true;
}

/* Remove path mode from the set if given mode is found */
bool dal_pms_remove_path_mode(
	struct path_mode_set *set,
	struct path_mode *mode)
{
	uint32_t i;

	for (i = 0; i < set->count; i++) {
		if (&set->path_mode_set[i] == mode)
			return dal_pms_remove_path_mode_at_index(set, i);
	}

	return false;
}
