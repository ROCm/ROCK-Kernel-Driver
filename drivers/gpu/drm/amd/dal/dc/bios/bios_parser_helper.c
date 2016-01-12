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

#include "atom.h"

#include "include/bios_parser_types.h"
#include "bios_parser_helper.h"
#include "command_table_helper.h"
#include "command_table.h"
#include "bios_parser.h"

bool dal_bios_parser_init_bios_helper(
	struct bios_parser *bp,
	enum dce_version version)
{
	switch (version) {

#if defined(CONFIG_DRM_AMD_DAL_DCE11_0)
	case DCE_VERSION_11_0:
		bp->bios_helper = dal_bios_parser_helper_dce110_get_table();
		return true;

#endif
	default:
		BREAK_TO_DEBUGGER();
		return false;
	}
}

bool dal_bios_parser_is_lid_open(
	struct bios_parser *bp)
{
	const struct graphics_object_id encoder = dal_graphics_object_id_init(
		ENCODER_ID_INTERNAL_UNIPHY,
		ENUM_ID_UNKNOWN,
		OBJECT_TYPE_UNKNOWN);
	const struct graphics_object_id connector = dal_graphics_object_id_init(
		CONNECTOR_ID_LVDS,
		ENUM_ID_UNKNOWN,
		OBJECT_TYPE_UNKNOWN);

	enum signal_type signal;

	/* check if VBIOS reported LCD as connected */
	signal = bp->bios_helper->detect_sink(bp->ctx,
		encoder, connector, SIGNAL_TYPE_LVDS);

	if (signal == SIGNAL_TYPE_NONE)
		return false;

	return bp->bios_helper->is_lid_open(bp->ctx);
}

bool dal_bios_parser_is_lid_status_changed(
	struct bios_parser *bp)
{
	return bp->bios_helper->is_lid_status_changed(
			bp->ctx);
}

bool dal_bios_parser_is_display_config_changed(
	struct bios_parser *bp)
{
	return bp->bios_helper->is_display_config_changed(
			bp->ctx);
}

/**
* dal_bios_parser_set_scratch_lcd_scale
*
* @brief
*  update VBIOS scratch pad registers about LCD scale
*
* @param
*  bool - to set to full panel mode or aspect-ratio mode
*/
void dal_bios_parser_set_scratch_lcd_scale(
	struct bios_parser *bp,
	enum lcd_scale scale)
{
	bp->bios_helper->set_scratch_lcd_scale(
		bp->ctx, scale);
}

/**
* dal_bios_parser_get_scratch_lcd_scale
*
* @brief
*  get LCD Scale Mode from VBIOS scratch register
*
* @param
*  NONE
*/
enum lcd_scale  dal_bios_parser_get_scratch_lcd_scale(
	struct bios_parser *bp)
{
	return bp->bios_helper->get_scratch_lcd_scale(
			bp->ctx);
}

void dal_bios_parser_get_bios_event_info(
	struct bios_parser *bp,
	struct bios_event_info *info)
{
	bp->bios_helper->get_bios_event_info(
		bp->ctx, info);
}

/* ABM related */

void dal_bios_parser_update_requested_backlight_level(
	struct bios_parser *bp,
	uint32_t backlight_8bit)
{
	bp->bios_helper->update_requested_backlight_level(
		bp->ctx,
		backlight_8bit);
}

uint32_t dal_bios_parser_get_requested_backlight_level(
	struct bios_parser *bp)
{
	return bp->bios_helper->get_requested_backlight_level(
			bp->ctx);
}

void dal_bios_parser_take_backlight_control(
	struct bios_parser *bp,
	bool cntl)
{
	bp->bios_helper->take_backlight_control(
		bp->ctx, cntl);
}

/**
 * dal_bios_parser_is_active_display
 *  Check video bios active display.
 */
bool dal_bios_parser_is_active_display(
	struct bios_parser *bp,
	enum signal_type signal,
	const struct connector_device_tag_info *device_tag)
{
	return bp->bios_helper->is_active_display(
			bp->ctx, signal, device_tag);
}

/**
 * dal_bios_parser_get_embedded_display_controller_id
 * Get controller ID for embedded display from scratch registers
 */
enum controller_id dal_bios_parser_get_embedded_display_controller_id(
	struct bios_parser *bp)
{
	return bp->bios_helper->get_embedded_display_controller_id(
			bp->ctx);
}

/**
 * dal_bios_parser_get_embedded_display_refresh_rate
 * Get refresh rate for embedded display from scratch registers
 */
uint32_t dal_bios_parser_get_embedded_display_refresh_rate(
	struct bios_parser *bp)
{
	return bp->bios_helper->get_embedded_display_refresh_rate(
			bp->ctx);
}
