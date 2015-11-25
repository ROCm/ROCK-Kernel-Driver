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

#ifndef __DISPLAY_SERVICE_INTERFACE_H__
#define __DISPLAY_SERVICE_INTERFACE_H__

#include "include/display_service_types.h"
#include "include/display_path_types.h"
#include "include/grph_object_ctrl_defs.h"

struct display_service;
struct ds_overlay;
struct ds_dispatch;
struct ds_synchronization;
struct path_mode_set;

struct display_service *dal_display_service_create(
	struct ds_init_data *data);

void dal_display_service_destroy(
	struct display_service **ds);

struct ds_dispatch *dal_display_service_get_adjustment_interface(
	struct display_service *ds);

struct ds_overlay *dal_display_service_get_overlay_interface(
	struct display_service *ds);

struct ds_dispatch *dal_display_service_get_set_mode_interface(
	struct display_service *ds);

struct ds_dispatch *dal_display_service_get_reset_mode_interface(
	struct display_service *ds);

struct ds_synchronization *dal_display_service_get_synchronization_interface(
	struct display_service *ds);

enum ds_return dal_display_service_notify_v_sync_int_state(
	struct display_service *ds,
	uint32_t display_index,
	bool maintain_v_sync_phase);

enum ds_return dal_display_service_target_power_control(
	struct display_service *ds,
	uint32_t display_index,
	bool power_on);

enum ds_return dal_display_service_power_down_active_hw(
	struct display_service *ds,
	enum dc_video_power_state state);

enum ds_return dal_display_service_mem_request_control(
	struct display_service *ds,
	uint32_t display_index,
	bool enable);

enum ds_return dal_display_service_set_multimedia_pass_through_mode(
	struct display_service *ds,
	uint32_t display_index,
	bool passThrough);

enum ds_return dal_display_service_set_palette(
	struct display_service *ds,
	uint32_t display_index,
	const struct ds_devclut *palette,
	const uint32_t start,
	const uint32_t length);

enum ds_return dal_display_service_apply_pix_clk_range(
	struct display_service *ds,
	uint32_t display_index,
	struct pixel_clock_safe_range *range);

enum ds_return dal_display_service_get_safe_pix_clk(
	struct display_service *ds,
	uint32_t display_index,
	uint32_t *pix_clk_khz);

enum ds_return dal_display_service_apply_refreshrate_adjustment(
	struct display_service *ds,
	uint32_t display_index,
	enum ds_refreshrate_adjust_action action,
	struct ds_refreshrate *refreshrate);

enum ds_return dal_display_service_pre_ddc(
	struct display_service *ds,
	uint32_t display_index);

enum ds_return dal_display_service_post_ddc(
	struct display_service *ds,
	uint32_t display_index);

enum ds_return dal_display_service_backlight_control(
	struct display_service *ds,
	uint32_t display_index,
	bool enable);

enum ds_return dal_display_service_get_backlight_user_level(
	struct display_service *ds,
	uint32_t display_index,
	uint32_t *level);

enum ds_return dal_display_service_get_backlight_effective_level(
	struct display_service *ds,
	uint32_t display_index,
	uint32_t *level);

enum ds_return dal_display_service_enable_hpd(
	struct display_service *ds,
	uint32_t display_index);

enum ds_return dal_display_service_disable_hpd(
	struct display_service *ds,
	uint32_t display_index);

enum ds_return dal_display_service_get_min_mem_channels(
	struct display_service *ds,
	const struct path_mode_set *path_mode_set,
	uint32_t mem_channels_num,
	uint32_t *min_mem_channels_num);

enum ds_return dal_display_service_enable_advanced_request(
	struct display_service *ds,
	bool enable);

/*Audio related*/
enum ds_return dal_display_service_enable_audio_endpoint(
	struct display_service *ds,
	uint32_t display_index,
	bool enable);

enum ds_return dal_display_service_mute_audio_endpoint(
	struct display_service *ds,
	uint32_t display_index,
	bool mute);

bool dal_display_service_calc_view_port_for_wide_display(
	struct display_service *ds,
	uint32_t display_index,
	const struct ds_view_port *set_view_port,
	struct ds_get_view_port *get_view_port);

#endif /* __DISPLAY_SERVICE_INTERFACE_H__ */
