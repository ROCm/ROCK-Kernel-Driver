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

#include "dm_services.h"

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
#if defined(CONFIG_DRM_AMD_DAL_DCE8_0)
	case DCE_VERSION_8_0:
		bp->bios_helper = dal_bios_parser_helper_dce80_get_table();
		return true;
#endif
#if defined(CONFIG_DRM_AMD_DAL_DCE10_0)
	case DCE_VERSION_10_0:
		bp->bios_helper = dal_bios_parser_helper_dce110_get_table();
		return true;

#endif
#if defined(CONFIG_DRM_AMD_DAL_DCE11_0)
	case DCE_VERSION_11_0:
		bp->bios_helper = dal_bios_parser_helper_dce110_get_table();
		return true;

#endif
#if defined(CONFIG_DRM_AMD_DAL_DCE11_2)
	case DCE_VERSION_11_2:
		bp->bios_helper = dal_bios_parser_helper_dce112_get_table();
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

