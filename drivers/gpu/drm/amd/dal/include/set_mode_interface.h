/* Copyright 2012-15 Advanced Micro Devices, Inc.
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
#ifndef __DAL_SET_MODE_INTERFACE_H__
#define __DAL_SET_MODE_INTERFACE_H__

#include "include/set_mode_types.h"
#include "include/display_service_types.h"
#include "include/timing_service_types.h"
#include "include/hw_path_mode_set_interface.h"
#include "include/path_mode_set_interface.h"
#include "include/set_mode_params_interface.h"
#include "include/plane_types.h"

struct mode_timing;
struct crtc_timing;
struct ds_dispatch;
struct hw_path_mode;
enum build_path_set_reason;

/* Set mode on a set of display paths */
enum ds_return dal_ds_dispatch_set_mode(
	struct ds_dispatch *ds_dispatch,
	const struct path_mode_set *path_mode_set);

bool dal_ds_dispatch_is_valid_mode_timing_for_display(
		const struct ds_dispatch *ds_dispatch,
		uint32_t display_path_index,
		enum validation_method method,
		const struct mode_timing *mode_timing);

/* Surface management interface */
enum ds_return dal_ds_dispatch_setup_plane_configurations(
	struct ds_dispatch *ds_dispatch,
	uint32_t display_index,
	uint32_t planes_num,
	const struct plane_config *configs);

bool dal_ds_dispatch_validate_plane_configurations(
	struct ds_dispatch *ds,
	uint32_t num_planes,
	const struct plane_config *pl_configs,
	bool *supported);

/*
 * Create HW path mode specifically for adjustment or generic control request on
 * an active display
 */
bool dal_ds_dispatch_build_hw_path_mode_for_adjustment(
		struct ds_dispatch *ds_dispatch,
		struct hw_path_mode *mode,
		uint32_t disp_index,
		struct adjustment_params *params);

bool dal_ds_dispatch_build_hw_path_set_for_adjustment(
		struct ds_dispatch *ds,
		struct hw_path_mode_set *hw_pms,
		struct adjustment_params *params);

/* Build a set of HW path modes from the given paths */
bool dal_ds_dispatch_build_hw_path_set(
	struct ds_dispatch *ds,
	const uint32_t num,
	const struct path_mode *const mode,
	struct hw_path_mode_set *const hw_mode_set,
	enum build_path_set_reason reason,
	struct adjustment_params *params);

/* Destroy a set of HW path */
void dal_ds_dispatch_destroy_hw_path_set(
	struct hw_path_mode_set *hw_mode_set);

/*
* Reset mode on a set of display paths to make the paths to be properly
* released
*/
enum ds_return dal_ds_dispatch_reset_mode(
		struct ds_dispatch *ds_dispatch,
		const uint32_t displays_num,
		const uint32_t *display_indexes);

/* Change clock to safe values when coming from PPLib */
enum ds_return dal_ds_dispatch_pre_adapter_clock_change(
		struct ds_dispatch *ds);

/* Change clock to values specified from PPLib */
enum ds_return dal_ds_dispatch_post_adapter_clock_change(
		struct ds_dispatch *ds);

struct mode_query;
void dal_ds_dispatch_pin_active_path_modes(
	struct ds_dispatch *ds_dispatch,
	void *param,
	uint32_t display_index,
	void (*func)(void *, const struct path_mode *));

struct set_mode_params *dal_ds_dispatch_create_resource_context(
		const struct ds_dispatch *ds_dispatch,
		const uint32_t display_idx[],
		uint32_t idx_num);

#endif /*__DAL_SET_MODE_INTERFACE_H__*/
