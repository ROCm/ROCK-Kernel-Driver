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

#ifndef __DAL_BIOS_PARSER_HELPER_H__
#define __DAL_BIOS_PARSER_HELPER_H__

#if defined(CONFIG_DRM_AMD_DAL_DCE11_0)
#include "dce110/bios_parser_helper_dce110.h"
#endif

struct bios_parser;

struct vbios_helper_data {
	uint32_t active;
	uint32_t requested;
};

struct bios_parser_helper {
	enum signal_type (*detect_sink)(
		struct dc_context *ctx,
		struct graphics_object_id encoder,
		struct graphics_object_id connector,
		enum signal_type signal);
	bool (*is_lid_open)(
		struct dc_context *ctx);
	bool (*is_lid_status_changed)(
		struct dc_context *ctx);
	bool (*is_display_config_changed)(
		struct dc_context *ctx);
	void (*set_scratch_acc_mode_change)(
		struct dc_context *ctx);
	bool (*is_accelerated_mode)(
		struct dc_context *ctx);
	void (*set_scratch_critical_state)(
		struct dc_context *ctx,
		bool state);
	void (*prepare_scratch_active_and_requested)(
		struct dc_context *ctx,
		struct vbios_helper_data *data,
		enum controller_id id, enum signal_type s,
		const struct connector_device_tag_info *dev_tag);
	void (*set_scratch_active_and_requested)(
		struct dc_context *ctx,
		struct vbios_helper_data *d);
	void (*set_scratch_connected)(
		struct dc_context *ctx,
		struct graphics_object_id id,
		bool connected,
		const struct connector_device_tag_info *device_tag);
	void (*set_scratch_lcd_scale)(
		struct dc_context *ctx,
		enum lcd_scale lcd_scale_request);
	enum lcd_scale (*get_scratch_lcd_scale)(
		struct dc_context *ctx);
	uint32_t (*fmt_control)(
		struct dc_context *ctx,
		enum controller_id id, uint32_t *value);
	uint32_t (*fmt_bit_depth_control)(
		struct dc_context *ctx,
		enum controller_id id,
		uint32_t *value);
	void (*get_bios_event_info)(
		struct dc_context *ctx,
		struct bios_event_info *info);
	void (*take_backlight_control)(
		struct dc_context *ctx, bool control);
	uint32_t (*get_requested_backlight_level)(
		struct dc_context *ctx);
	void (*update_requested_backlight_level)(
		struct dc_context *ctx,
		uint32_t backlight_8bit);
	bool (*is_active_display)(
		struct dc_context *ctx,
		enum signal_type signal,
		const struct connector_device_tag_info *dev_tag);
	enum controller_id (*get_embedded_display_controller_id)(
		struct dc_context *ctx);
	uint32_t (*get_embedded_display_refresh_rate)(
		struct dc_context *ctx);
};

bool dal_bios_parser_init_bios_helper(
	struct bios_parser *bp,
	enum dce_version ver);

#endif
