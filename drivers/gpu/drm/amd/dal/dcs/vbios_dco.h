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

#ifndef __DAL_VBIOS_DCO__
#define __DAL_VBIOS_DCO__

struct lcd_resolution {
	uint32_t width;
	uint32_t height;
};

struct vbios_dco;
union panel_misc_info;
struct adapter_service;
struct dcs_mode_timing_list;

struct vbios_dco *dal_vbios_dco_create(
	struct adapter_service *as);

void dal_vbios_dco_destroy(
	struct vbios_dco **dco);


bool dal_vbios_dco_construct(
	struct vbios_dco *dco,
	struct adapter_service *as);

void dal_vbios_dco_destruct(
	struct vbios_dco *dco);

bool dal_vbios_dco_add_mode_timing(
	struct vbios_dco *dco,
	struct dcs_mode_timing_list *list,
	bool *preffered_mode_found);

bool dal_vbios_dco_get_panel_misc_info(
	struct vbios_dco *dco,
	union panel_misc_info *panel_info);

bool dal_vbios_dco_is_pixel_clk_ss_supported(
	struct vbios_dco *dco);

uint32_t dal_vbios_dco_get_edid_buff_len(
	struct vbios_dco *dco);

uint8_t *dal_vbios_dco_get_edid_buff(
	struct vbios_dco *dco);

uint32_t dal_vbios_dco_get_pixel_clk_for_drr_khz(
	struct vbios_dco *dco);

uint32_t dal_vbios_dco_get_min_fps_for_drr(
	struct vbios_dco *dco);

#endif /* __DAL_VBIOS_DCO__ */
