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

#ifndef __DAL_PATH_MODE_SET_INTERFACE_H__
#define __DAL_PATH_MODE_SET_INTERFACE_H__

/* Set of path modes */
struct path_mode_set {
	union control_flags {
		struct {
			uint32_t KEEP_DISPLAY_POWERED_OFF:1;
			uint32_t UNBLANCK_SOURCE_AFTER_SETMODE:1;
			uint32_t NODE_FAULT_UNDERSCAN:1;
		} bits;

		uint32_t all;
	} control_flags;

	struct path_mode path_mode_set[MAX_COFUNC_PATH];
	uint32_t count;
};

/* Create path mode set */
struct path_mode_set *dal_pms_create(void);

/* Deallocate path mode set */
void dal_pms_destroy(
	struct path_mode_set **pms);

/* Create a copy of given path mode set */
struct path_mode_set *dal_pms_copy(
	const struct path_mode_set *copy);

/* Constructor for path mode set */
bool dal_pms_construct(
	struct path_mode_set *set);

/* Add a path mode into the set */
bool dal_pms_add_path_mode(
	struct path_mode_set *set,
	const struct path_mode *path_mode);

/* Get number of path modes in the set */
uint32_t dal_pms_get_path_mode_num(
	const struct path_mode_set *set);

/* Return the path mode at the index */
const struct path_mode *dal_pms_get_path_mode_at_index(
	const struct path_mode_set *set,
	uint32_t index);

/* Return the path mode for the given display index */
const struct path_mode *dal_pms_get_path_mode_for_display_index(
	const struct path_mode_set *set,
	uint32_t index);

/* Remove the path mode at index */
bool dal_pms_remove_path_mode_at_index(
	struct path_mode_set *set,
	uint32_t index);

/* Remove the given path mode if it is found in the set */
bool dal_pms_remove_path_mode(
	struct path_mode_set *set,
	struct path_mode *mode);

/* Add control flag to keep display powered off */
void dal_pms_keep_display_powered_off(
	struct path_mode_set *set,
	bool keep);

/* Return control flag if display needs to be kept powered off */
bool dal_pms_is_display_power_off_required(
	const struct path_mode_set *set);

/* Add control flag to not use default underscan*/
void dal_pms_fallback_remove_default_underscan(
	struct path_mode_set *set,
	bool lean);

/* Return control flag if default underscan is not used */
bool dal_pms_is_fallback_no_default_underscan_enabled(
	struct path_mode_set *set);

#endif
