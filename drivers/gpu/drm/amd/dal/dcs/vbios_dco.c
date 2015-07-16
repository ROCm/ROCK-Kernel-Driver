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
#include "vbios_dco.h"
#include "include/dcs_interface.h"
#include "include/grph_object_ctrl_defs.h"
#include "include/adapter_service_interface.h"
#include "include/timing_service_interface.h"

struct vbios_dco {
	struct adapter_service *as;
	struct embedded_panel_info panel_info;
	uint8_t *edid_data;
	uint32_t edid_len;
};

static const struct lcd_resolution lcd_low_resolution[] = {
	{ 320, 200 },
	{ 320, 240 },
	{ 400, 300 },
	{ 512, 384 },
	{ 640, 480 },
	{ 800, 600 },
	{ 1024, 768 },
	{ 1152, 864 },
	{ 1280, 1024 },
	{ 1600, 1200 },
	{ 1792, 1344 },
	{ 1800, 1440 },
	{ 1920, 1440 },
	{ 2048, 1536 }
};

bool dal_vbios_dco_construct(
	struct vbios_dco *dco,
	struct adapter_service *as)
{
	bool ret = false;

	if (!as)
		return ret;

	dco->as = as;

	if (dal_adapter_service_get_embedded_panel_info(as, &dco->panel_info)) {
		ret = true;

		if (dal_adapter_service_get_faked_edid_len(
		as, &dco->edid_len)) {

			dco->edid_data = dal_alloc(dco->edid_len);

			if (!dco->edid_data)
				return false;

			if (!dal_adapter_service_get_faked_edid_buf(
				as, dco->edid_data, dco->edid_len)) {

				dal_free(dco->edid_data);
				ret = false;
			}
		}
	}
	return ret;
}

struct vbios_dco *dal_vbios_dco_create(
	struct adapter_service *as)
{
	struct vbios_dco *dco;

	dco = dal_alloc(sizeof(struct vbios_dco));

	if (!dco)
		return NULL;

	if (dal_vbios_dco_construct(dco, as))
		return dco;

	dal_free(dco);
	return NULL;
}

void dal_vbios_dco_destruct(
	struct vbios_dco *dco)
{
	if (dco->edid_data)
		dal_free(dco->edid_data);
}

void dal_vbios_dco_destroy(
	struct vbios_dco **dco)
{
	if (!dco || !*dco)
		return;

	dal_vbios_dco_destruct(*dco);
	dal_free(*dco);
	*dco = NULL;
}

static void vbios_timing_to_crtc_timing(
	struct device_timing *device_timing,
	struct crtc_timing *crtc_timing)
{
	ASSERT(device_timing != NULL);
	ASSERT(crtc_timing != NULL);

	crtc_timing->pix_clk_khz = device_timing->pixel_clk;

	crtc_timing->h_addressable = device_timing->horizontal_addressable;
	crtc_timing->h_total = device_timing->horizontal_addressable +
		device_timing->horizontal_blanking_time;
	crtc_timing->h_front_porch = device_timing->horizontal_sync_offset;
	crtc_timing->h_sync_width = device_timing->horizontal_sync_width;
	crtc_timing->h_border_left = device_timing->horizontal_border;
	crtc_timing->h_border_right = device_timing->horizontal_border;

	crtc_timing->v_addressable = device_timing->vertical_addressable;
	crtc_timing->v_total = device_timing->vertical_addressable +
		device_timing->vertical_blanking_time;
	crtc_timing->v_front_porch = device_timing->vertical_sync_offset;
	crtc_timing->v_sync_width = device_timing->vertical_sync_width;
	crtc_timing->v_border_top = device_timing->vertical_border;
	crtc_timing->v_border_bottom = device_timing->vertical_border;

	crtc_timing->timing_standard = TIMING_STANDARD_EXPLICIT;
	crtc_timing->pixel_encoding = PIXEL_ENCODING_RGB;
	crtc_timing->display_color_depth = device_timing->misc_info.RGB888 ?
		DISPLAY_COLOR_DEPTH_888 : DISPLAY_COLOR_DEPTH_666;

	crtc_timing->flags.INTERLACE = device_timing->misc_info.INTERLACE;
	crtc_timing->flags.HSYNC_POSITIVE_POLARITY =
		device_timing->misc_info.H_SYNC_POLARITY;
	crtc_timing->flags.VSYNC_POSITIVE_POLARITY =
		device_timing->misc_info.V_SYNC_POLARITY;

	if (device_timing->misc_info.H_REPLICATION_BY2)
		crtc_timing->flags.PIXEL_REPETITION = 2;
}

static bool get_vbios_native_mode_timing(
	struct vbios_dco *dco,
	struct mode_timing *mode_timing,
	bool *preferred_mode_found)
{

	if (dco->panel_info.lcd_timing.pixel_clk == 0)
		return false;

	vbios_timing_to_crtc_timing(
		&dco->panel_info.lcd_timing,
		&mode_timing->crtc_timing);

	dal_timing_service_create_mode_info_from_timing(
		&mode_timing->crtc_timing, &mode_timing->mode_info);

	mode_timing->mode_info.timing_standard =
		mode_timing->crtc_timing.timing_standard;
	mode_timing->mode_info.timing_source = TIMING_SOURCE_VBIOS;

	/*If preferred mode yet not found -
	 * select native bios mode/timing as preferred*/
	if (!(*preferred_mode_found)) {
		mode_timing->mode_info.flags.PREFERRED = 1;
		*preferred_mode_found = true;
	}

	return true;
}

static void add_patch_mode_timing(
	struct vbios_dco *dco,
	struct crtc_timing *timing,
	struct dcs_mode_timing_list *list)
{
	struct embedded_panel_patch_mode patch_mode;
	uint32_t index = 0;
	uint32_t refresh_rate =
		(timing->pix_clk_khz * PIXEL_CLOCK_MULTIPLIER) /
		(timing->h_total * timing->v_total);

	while (dal_adapter_service_enum_embedded_panel_patch_mode(
		dco->as, index++, &patch_mode)) {

		struct mode_timing mode_timing = { { 0 } };

		if (!patch_mode.width)
			continue;

		mode_timing.crtc_timing = *timing;
		mode_timing.mode_info.pixel_width = patch_mode.width;
		mode_timing.mode_info.pixel_height = patch_mode.height;
		mode_timing.mode_info.field_rate = refresh_rate;
		mode_timing.mode_info.timing_standard =
			TIMING_STANDARD_EXPLICIT;
		mode_timing.mode_info.timing_source = TIMING_SOURCE_VBIOS;

		dal_dcs_mode_timing_list_append(list, &mode_timing);
	}
}

bool dal_vbios_dco_add_mode_timing(
	struct vbios_dco *dco,
	struct dcs_mode_timing_list *list,
	bool *preffered_mode_found)
{
	struct mode_timing mode_timing = { { 0 } };

	ASSERT(list != NULL);

	if (!get_vbios_native_mode_timing(
		dco, &mode_timing, preffered_mode_found))
		return false;

	dal_dcs_mode_timing_list_append(list, &mode_timing);

	add_patch_mode_timing(dco, &mode_timing.crtc_timing, list);

	return true;
}

bool dal_vbios_dco_get_panel_misc_info(
	struct vbios_dco *dco, union panel_misc_info *panel_info)
{
	if (!panel_info)
		return false;

	panel_info->bits.API_ENABLED =
		dco->panel_info.lcd_timing.misc_info.API_ENABLED;

	panel_info->bits.COMPOSITE_SYNC =
		dco->panel_info.lcd_timing.misc_info.COMPOSITE_SYNC;

	panel_info->bits.DOUBLE_CLOCK =
		dco->panel_info.lcd_timing.misc_info.DOUBLE_CLOCK;

	panel_info->bits.GREY_LEVEL =
		dco->panel_info.lcd_timing.misc_info.GREY_LEVEL;

	panel_info->bits.H_CUT_OFF =
		dco->panel_info.lcd_timing.misc_info.HORIZONTAL_CUT_OFF;

	panel_info->bits.H_REPLICATION_BY_2 =
		dco->panel_info.lcd_timing.misc_info.H_REPLICATION_BY2;

	panel_info->bits.H_SYNC_POLARITY =
		dco->panel_info.lcd_timing.misc_info.H_SYNC_POLARITY;

	panel_info->bits.INTERLACE =
		dco->panel_info.lcd_timing.misc_info.INTERLACE;

	panel_info->bits.RGB888 =
		dco->panel_info.lcd_timing.misc_info.RGB888;

	panel_info->bits.SPATIAL =
		dco->panel_info.lcd_timing.misc_info.SPATIAL;

	panel_info->bits.TEMPORAL =
		dco->panel_info.lcd_timing.misc_info.TEMPORAL;

	panel_info->bits.V_CUT_OFF =
		dco->panel_info.lcd_timing.misc_info.VERTICAL_CUT_OFF;

	panel_info->bits.V_REPLICATION_BY_2 =
			dco->panel_info.lcd_timing.misc_info.V_REPLICATION_BY2;

	panel_info->bits.V_SYNC_POLARITY =
			dco->panel_info.lcd_timing.misc_info.V_SYNC_POLARITY;
	return true;

}

bool dal_vbios_dco_is_pixel_clk_ss_supported(
	struct vbios_dco *dco)
{
	return dco->panel_info.ss_id != 0;
}

uint32_t dal_vbios_dco_get_edid_buff_len(
	struct vbios_dco *dco)
{
	return dco->edid_len;
}

uint8_t *dal_vbios_dco_get_edid_buff(
	struct vbios_dco *dco)
{
	return dco->edid_data;
}

uint32_t dal_vbios_dco_get_pixel_clk_for_drr_khz(
	struct vbios_dco *dco)
{
	return dco->panel_info.drr_enabled ?
		dco->panel_info.lcd_timing.pixel_clk : 0;
}

uint32_t dal_vbios_dco_get_min_fps_for_drr(
	struct vbios_dco *dco)
{
	return dco->panel_info.drr_enabled ?
		dco->panel_info.min_drr_refresh_rate : 0;
}

