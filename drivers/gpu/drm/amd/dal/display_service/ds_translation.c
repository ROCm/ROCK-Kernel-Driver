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
#include "include/hw_sequencer_types.h"

#include "ds_translation.h"
#include "adjustment_types_internal.h"

/*
 * ds_translation_translate_content_type
 *
 * Translate adjustment value into supported display content type
 *
 * uint32_t val - adjustment value
 * union display_content_type *support - supported display content type
 *
 * return void
 */
void dal_ds_translation_translate_content_type(uint32_t val,
		enum display_content_type *support)
{
	switch (val) {
	case DISPLAY_CONTENT_TYPE_GRAPHICS:
		*support = DISPLAY_CONTENT_TYPE_GRAPHICS;
		break;

	case DISPLAY_CONTENT_TYPE_PHOTO:
		*support = DISPLAY_CONTENT_TYPE_PHOTO;
		break;

	case DISPLAY_CONTENT_TYPE_CINEMA:
		*support = DISPLAY_CONTENT_TYPE_CINEMA;
		break;

	case DISPLAY_CONTENT_TYPE_GAME:
		*support = DISPLAY_CONTENT_TYPE_GAME;
		break;

	default:
		*support = DISPLAY_CONTENT_TYPE_NO_DATA;
	}
}

/* Translate 3D format */
enum timing_3d_format dal_ds_translation_get_active_timing_3d_format(
	enum timing_3d_format timing_format,
	enum view_3d_format view_format)
{
	enum view_3d_format target_format =
		dal_ds_translation_3d_format_timing_to_view(
			timing_format);

	if (target_format != view_format)
		return TIMING_3D_FORMAT_NONE;

	return timing_format;
}

/* Translate timing 3D format to view 3D format*/
enum view_3d_format dal_ds_translation_3d_format_timing_to_view(
		enum timing_3d_format timing_format)
{
	enum view_3d_format view_format = VIEW_3D_FORMAT_NONE;

	switch (timing_format) {
	case TIMING_3D_FORMAT_SIDEBAND_FA:
	case TIMING_3D_FORMAT_INBAND_FA:
	case TIMING_3D_FORMAT_FRAME_ALTERNATE:
	case TIMING_3D_FORMAT_HW_FRAME_PACKING:
	case TIMING_3D_FORMAT_SW_FRAME_PACKING:
	case TIMING_3D_FORMAT_ROW_INTERLEAVE:
	case TIMING_3D_FORMAT_COLUMN_INTERLEAVE:
	case TIMING_3D_FORMAT_PIXEL_INTERLEAVE:
		view_format = VIEW_3D_FORMAT_FRAME_SEQUENTIAL;
		break;

	case TIMING_3D_FORMAT_SBS_SW_PACKED:
		view_format = VIEW_3D_FORMAT_SIDE_BY_SIDE;
		break;

	case TIMING_3D_FORMAT_TB_SW_PACKED:
		view_format = VIEW_3D_FORMAT_TOP_AND_BOTTOM;
		break;

	default:
		break;
	}

	return view_format;
}

/*
 * dal_ds_translation_patch_hw_view_for_3d
 *
 * Adjust HW view for 3D Frame Packing format (according to HDMI spec).
 * This is relevant only 3D timing packed by SW.
 * Also assumes if 3D timing packed by SW, then no scaling available.
 *  1. NewVTotal = OldVTotal * 2
 *  2. NewVBlank = OldVBlank (reminder: VBlank includes borders)
 *  3. NewView   = NewVTotal - NewVBlank
 *
 */
void dal_ds_translation_patch_hw_view_for_3d(
	struct view *view,
	const struct crtc_timing *timing,
	enum view_3d_format view_3d_format)
{

	/* Adjust HW view for 3D Frame Packing format (according to HDMI spec)
	 * 1. NewVTotal = OldVTotal * 2
	 * 2. NewVBlank = OldVBlank (reminder: VBlank includes borders)
	 * 3. NewView   = NewVTotal - NewVBlank
	 */
	if (dal_ds_translation_get_active_timing_3d_format(
		timing->timing_3d_format,
		view_3d_format) == TIMING_3D_FORMAT_SW_FRAME_PACKING) {
		uint32_t blank_region = timing->v_total -
			timing->v_addressable;
		ASSERT(view->height == timing->v_addressable);
		view->height = (timing->v_total * 2) - blank_region;
	}
}

#define DVI_10_BIT_TRESHOLD_RATE_IN_KHZ 50000

/*
 * dal_ds_translation_hw_crtc_timing_from_crtc_timing
 *
 * Converts SW layer timing structure into HW layer timing structure.
 * additional input parameters maybe present for proper conversion.
 *
 */
void dal_ds_translation_hw_crtc_timing_from_crtc_timing(
	struct hw_crtc_timing *hw_timing,
	const struct crtc_timing *timing,
	enum view_3d_format view_3d_format,
	enum signal_type signal)
{
	enum timing_3d_format timing_3d_format;
	uint32_t pixel_repetition =
		timing->flags.PIXEL_REPETITION == 0 ?
			1 : timing->flags.PIXEL_REPETITION;

	/* HW expects FrontPorch - 1 for interlaced modes */
	uint32_t vsync_offset = timing->v_border_bottom +
		(timing->v_front_porch - timing->flags.INTERLACE);
	uint32_t hsync_offset = timing->h_border_right +
		timing->h_front_porch;

	hw_timing->h_total = timing->h_total / pixel_repetition;
	hw_timing->h_addressable = timing->h_addressable / pixel_repetition;
	hw_timing->h_overscan_left = timing->h_border_left / pixel_repetition;
	hw_timing->h_overscan_right = timing->h_border_right / pixel_repetition;
	hw_timing->h_sync_start = (timing->h_addressable + hsync_offset) /
		pixel_repetition;
	hw_timing->h_sync_width = timing->h_sync_width / pixel_repetition;

	hw_timing->v_total = timing->v_total;
	hw_timing->v_addressable = timing->v_addressable;
	hw_timing->v_overscan_top = timing->v_border_top;
	hw_timing->v_overscan_bottom = timing->v_border_bottom;
	hw_timing->v_sync_width = timing->v_sync_width;
	hw_timing->v_sync_start = timing->v_addressable + vsync_offset;

	hw_timing->pixel_clock = timing->pix_clk_khz;

	/* flags */
	hw_timing->flags.INTERLACED = timing->flags.INTERLACE;
	hw_timing->flags.DOUBLESCAN = timing->flags.DOUBLESCAN;
	hw_timing->flags.PIXEL_REPETITION = pixel_repetition;
	hw_timing->flags.HSYNC_POSITIVE_POLARITY =
		timing->flags.HSYNC_POSITIVE_POLARITY;
	hw_timing->flags.VSYNC_POSITIVE_POLARITY =
		timing->flags.VSYNC_POSITIVE_POLARITY;
	hw_timing->flags.RIGHT_EYE_3D_POLARITY =
		timing->flags.RIGHT_EYE_3D_POLARITY;
	hw_timing->flags.PACK_3D_FRAME = false;
	hw_timing->flags.HIGH_COLOR_DL_MODE = false;
	hw_timing->flags.Y_ONLY = timing->flags.YONLY;

	/* below work only because HW def is clone of TS def.
	 * need translation to make this robust */
	hw_timing->flags.COLOR_DEPTH =
		(enum hw_color_depth) timing->display_color_depth;
	hw_timing->flags.PIXEL_ENCODING =
		(enum hw_pixel_encoding) timing->pixel_encoding;
	hw_timing->timing_standard =
		(enum hw_timing_standard) timing->timing_standard;

	/* Adjust HW timing for DVI DualLink 10bit. For low clocks we do not
	 * double pixel rate */
	if (signal == SIGNAL_TYPE_DVI_DUAL_LINK &&
		timing->display_color_depth >= DISPLAY_COLOR_DEPTH_101010) {
		if (hw_timing->pixel_clock > DVI_10_BIT_TRESHOLD_RATE_IN_KHZ)
			hw_timing->pixel_clock *= 2;

		hw_timing->flags.HIGH_COLOR_DL_MODE = true;
	}

	/* Adjust HW timing for 3D Frame Packing format (according to HDMI spec)
	 * 1. 3D pixel clock frequency is x2 of 2D pixel clock frequency
	 * 2. 3D vertical total line is x2 of 2D vertical total line
	 * 3. 3D horizontal total pixel is equal to 2D horizontal total pixel
	 */
	timing_3d_format = dal_ds_translation_get_active_timing_3d_format(
		timing->timing_3d_format,
		view_3d_format);
	switch (timing_3d_format) {
	/* HW will adjust image size. Here we need only adjust pixel clock.
	 * Otherwise we need to do it as following: */
	case TIMING_3D_FORMAT_HW_FRAME_PACKING:
		hw_timing->pixel_clock *= 2;
		hw_timing->flags.PACK_3D_FRAME = true;
		break;

		/* HW does not support frame packing. Need to adjust image and
		 * pixel clock
		 * 1. NewVTotal = OldVTotal * 2
		 * 2. NewVBlank = OldVBlank (reminder: VBlank includes borders)
		 * 2. NewVSyncOffset  = OldVSyncOffset
		 * 4. NewVAddressable = NewVTotal - NewVBlank
		 * 5. NewVSyncStart   = NewVAddressable - NewVSyncOffset
		 */
	case TIMING_3D_FORMAT_SW_FRAME_PACKING: {
		uint32_t blank_region = hw_timing->v_total -
			hw_timing->v_addressable;
		hw_timing->v_total *= 2;
		hw_timing->v_addressable = hw_timing->v_total -
			blank_region;
		hw_timing->v_sync_start = hw_timing->v_addressable +
			vsync_offset;
		hw_timing->pixel_clock *= 2;

		break;
	}
	case TIMING_3D_FORMAT_DP_HDMI_INBAND_FA:
		/* When we try to set frame packing with a HDMI display that is
		 * connected via active DP-HDMI dongle, we want to use HDMI
		 * frame packing, so the pixel clock is doubled */
		hw_timing->pixel_clock *= 2;
		break;

	default:
		break;
	}
}

/*
* dal_ds_translation_setup_hw_stereo_mixer_params
*
* Setups Stereo Mixer parameters for HW sequencer
*
*/
void dal_ds_translation_setup_hw_stereo_mixer_params(
	struct hw_mode_info *hw_mode,
	const struct crtc_timing *timing,
	enum view_3d_format view_3d_format)
{
	enum timing_3d_format timing_3d_format =
		dal_ds_translation_get_active_timing_3d_format(
			timing->timing_3d_format, view_3d_format);

	switch (timing_3d_format) {
	case TIMING_3D_FORMAT_ROW_INTERLEAVE:
		hw_mode->stereo_format = HW_STEREO_FORMAT_ROW_INTERLEAVED;
		hw_mode->stereo_mixer_params.sub_sampling =
			timing->flags.SUB_SAMPLE_3D;
		break;

	case TIMING_3D_FORMAT_COLUMN_INTERLEAVE:
		hw_mode->stereo_format = HW_STEREO_FORMAT_COLUMN_INTERLEAVED;
		hw_mode->stereo_mixer_params.sub_sampling =
			timing->flags.SUB_SAMPLE_3D;
		break;

	case TIMING_3D_FORMAT_PIXEL_INTERLEAVE:
		hw_mode->stereo_format = HW_STEREO_FORMAT_CHECKER_BOARD;
		hw_mode->stereo_mixer_params.sub_sampling =
			timing->flags.SUB_SAMPLE_3D;
		break;

	default:
		hw_mode->stereo_format = HW_STEREO_FORMAT_NONE;
		break;
	}
}

enum hw_color_space dal_ds_translation_hw_color_space_from_color_space(
	enum ds_color_space color_space)
{
	enum hw_color_space hw_color_space;

	switch (color_space) {
	case DS_COLOR_SPACE_SRGB_FULLRANGE:
		hw_color_space = HW_COLOR_SPACE_SRGB_FULL_RANGE;
		break;
	case DS_COLOR_SPACE_SRGB_LIMITEDRANGE:
		hw_color_space = HW_COLOR_SPACE_SRGB_LIMITED_RANGE;
		break;
	case DS_COLOR_SPACE_YPBPR601:
		hw_color_space = HW_COLOR_SPACE_YPBPR601;
		break;
	case DS_COLOR_SPACE_YPBPR709:
		hw_color_space = HW_COLOR_SPACE_YPBPR709;
		break;
	case DS_COLOR_SPACE_YCBCR601:
		hw_color_space = HW_COLOR_SPACE_YCBCR601;
		break;
	case DS_COLOR_SPACE_YCBCR709:
		hw_color_space = HW_COLOR_SPACE_YCBCR709;
		break;
	case DS_COLOR_SPACE_NMVPU_SUPERAA:
		hw_color_space = HW_COLOR_SPACE_NMVPU_SUPERAA;
		break;
	default:
		hw_color_space = HW_COLOR_SPACE_UNKNOWN;
		break;
	}
	return hw_color_space;
}

enum ds_color_space dal_ds_translation_color_space_from_hw_color_space(
	enum hw_color_space hw_color_space)
{
	enum ds_color_space color_space;

	switch (hw_color_space) {
	case HW_COLOR_SPACE_SRGB_FULL_RANGE:
		color_space = DS_COLOR_SPACE_SRGB_FULLRANGE;
		break;
	case HW_COLOR_SPACE_SRGB_LIMITED_RANGE:
		color_space = DS_COLOR_SPACE_SRGB_LIMITEDRANGE;
		break;
	case HW_COLOR_SPACE_YPBPR601:
		color_space = DS_COLOR_SPACE_YPBPR601;
		break;
	case HW_COLOR_SPACE_YPBPR709:
		color_space = DS_COLOR_SPACE_YPBPR709;
		break;
	case HW_COLOR_SPACE_YCBCR601:
		color_space = DS_COLOR_SPACE_YCBCR601;
		break;
	case HW_COLOR_SPACE_YCBCR709:
		color_space = DS_COLOR_SPACE_YCBCR709;
		break;
	case HW_COLOR_SPACE_NMVPU_SUPERAA:
		color_space = DS_COLOR_SPACE_NMVPU_SUPERAA;
		break;
	default:
		color_space = DS_COLOR_SPACE_UNKNOWN;
		break;
	}
	return color_space;
}

bool dal_ds_translate_regamma_to_external(
	const struct ds_regamma_lut *gamma_int,
	struct ds_regamma_lut *gamma_ext)
{
	uint32_t i;

	gamma_ext->flags.u32all = 0;
	gamma_ext->flags.bits.GAMMA_RAMP_ARRAY =
			gamma_int->flags.bits.GAMMA_RAMP_ARRAY;
	gamma_ext->flags.bits.COEFF_FROM_EDID =
			gamma_int->flags.bits.COEFF_FROM_EDID;
	gamma_ext->flags.bits.GAMMA_FROM_EDID_EX =
			gamma_int->flags.bits.GAMMA_FROM_EDID_EX;
	gamma_ext->flags.bits.GAMMA_FROM_USER =
			gamma_int->flags.bits.GAMMA_FROM_USER;

	gamma_ext->flags.bits.COEFF_FROM_USER =
			gamma_int->flags.bits.COEFF_FROM_USER;
	gamma_ext->flags.bits.COEFF_FROM_EDID =
			gamma_int->flags.bits.COEFF_FROM_EDID;

	if (gamma_int->flags.bits.GAMMA_RAMP_ARRAY == 1) {
		gamma_ext->flags.bits.APPLY_DEGAMMA =
				gamma_int->flags.bits.APPLY_DEGAMMA;
		for (i = 0 ; i < REGAMMA_RANGE ; i++)
			gamma_ext->gamma.gamma[i] =
					gamma_int->gamma.gamma[i];
	} else {
		gamma_ext->flags.bits.APPLY_DEGAMMA = 0;
		for (i = 0; i < COEFF_RANGE ; i++) {
			gamma_ext->coeff.coeff_a0[i] =
					gamma_int->coeff.coeff_a0[i];
			gamma_ext->coeff.coeff_a1[i] =
					gamma_int->coeff.coeff_a1[i];
			gamma_ext->coeff.coeff_a2[i] =
					gamma_int->coeff.coeff_a2[i];
			gamma_ext->coeff.coeff_a3[i] =
					gamma_int->coeff.coeff_a3[i];
			gamma_ext->coeff.gamma[i] =
					gamma_int->coeff.gamma[i];
		}
	}
	return true;

}

bool dal_ds_translate_regamma_to_internal(
	const struct ds_regamma_lut *gamma_ext,
	struct ds_regamma_lut *gamma_int)
{
	uint32_t i;

	gamma_int->flags.bits.GAMMA_RAMP_ARRAY =
			gamma_ext->flags.bits.GAMMA_RAMP_ARRAY;
	if (gamma_ext->flags.bits.GAMMA_FROM_EDID == 1 ||
		gamma_ext->flags.bits.GAMMA_FROM_EDID_EX == 1 ||
		gamma_ext->flags.bits.GAMMA_FROM_USER == 1)
		return false;

	gamma_int->flags.bits.COEFF_FROM_EDID =
				gamma_ext->flags.bits.COEFF_FROM_EDID;
	gamma_int->flags.bits.GAMMA_FROM_EDID_EX =
				gamma_ext->flags.bits.GAMMA_FROM_EDID_EX;
	gamma_int->flags.bits.GAMMA_FROM_USER =
				gamma_ext->flags.bits.GAMMA_FROM_USER;

	if (gamma_ext->flags.bits.COEFF_FROM_USER == 1 &&
		gamma_ext->flags.bits.COEFF_FROM_EDID == 1)
		return false;

	gamma_int->flags.bits.COEFF_FROM_USER =
				gamma_ext->flags.bits.COEFF_FROM_USER;
	gamma_int->flags.bits.COEFF_FROM_EDID =
				gamma_ext->flags.bits.COEFF_FROM_EDID;

	if (gamma_ext->flags.bits.GAMMA_RAMP_ARRAY == 1) {
		gamma_int->flags.bits.APPLY_DEGAMMA =
				gamma_ext->flags.bits.APPLY_DEGAMMA;
				for (i = 0 ; i < REGAMMA_RANGE ; i++)
					gamma_int->gamma.gamma[i] =
						gamma_ext->gamma.gamma[i];
	} else {
		gamma_int->flags.bits.APPLY_DEGAMMA = 0;
		for (i = 0; i < COEFF_RANGE ; i++) {
			gamma_int->coeff.coeff_a0[i] =
					gamma_ext->coeff.coeff_a0[i];
			gamma_int->coeff.coeff_a1[i] =
					gamma_ext->coeff.coeff_a1[i];
			gamma_int->coeff.coeff_a2[i] =
					gamma_ext->coeff.coeff_a2[i];
			gamma_int->coeff.coeff_a3[i] =
					gamma_ext->coeff.coeff_a3[i];
			gamma_int->coeff.gamma[i] =
					gamma_ext->coeff.gamma[i];
		}
	}
	return true;
}

bool dal_ds_translate_regamma_to_hw(
		const struct ds_regamma_lut *regumma_lut,
		struct hw_regamma_lut *regamma_lut_hw)
{
	bool ret = true;
	uint32_t i = 0;

	regamma_lut_hw->flags.bits.gamma_ramp_array =
			regumma_lut->flags.bits.GAMMA_RAMP_ARRAY;
	regamma_lut_hw->flags.bits.graphics_degamma_srgb =
			regumma_lut->flags.bits.GRAPHICS_DEGAMMA_SRGB;
	regamma_lut_hw->flags.bits.overlay_degamma_srgb  =
			regumma_lut->flags.bits.OVERLAY_DEGAMMA_SRGB;

	if (regumma_lut->flags.bits.GAMMA_RAMP_ARRAY == 1) {
		regamma_lut_hw->flags.bits.apply_degamma =
				regumma_lut->flags.bits.APPLY_DEGAMMA;

		for (i = 0; i < 256 * 3; i++)
			regamma_lut_hw->gamma.gamma[i] = regumma_lut->gamma.gamma[i];
	} else {
		regamma_lut_hw->flags.bits.apply_degamma = 0;

		for (i = 0; i < 3; i++) {
			regamma_lut_hw->coeff.a0[i] =
					regumma_lut->coeff.coeff_a0[i];
			regamma_lut_hw->coeff.a1[i] =
					regumma_lut->coeff.coeff_a1[i];
			regamma_lut_hw->coeff.a2[i] =
					regumma_lut->coeff.coeff_a2[i];
			regamma_lut_hw->coeff.a3[i] =
					regumma_lut->coeff.coeff_a3[i];
			regamma_lut_hw->coeff.gamma[i] =
					regumma_lut->coeff.gamma[i];
		}
	}
	return ret;
}

bool dal_ds_translate_internal_gamut_to_external_parameter(
	const struct gamut_data *gamut,
	struct ds_gamut_data *data)
{
	if (gamut->option.bits.CUSTOM_GAMUT_SPACE == 0)
		data->gamut.predefined = gamut->gamut.predefined.u32all;
	else {
		data->feature.bits.CUSTOM_GAMUT_SPACE = 0;
		data->gamut.custom.red_x = gamut->gamut.custom.red_x;
		data->gamut.custom.red_y = gamut->gamut.custom.red_y;

		data->gamut.custom.green_x = gamut->gamut.custom.green_x;
		data->gamut.custom.green_y = gamut->gamut.custom.green_y;

		data->gamut.custom.blue_x = gamut->gamut.custom.blue_x;
		data->gamut.custom.blue_y = gamut->gamut.custom.blue_y;
	}
	if (gamut->option.bits.CUSTOM_WHITE_POINT == 0)
		data->white_point.predefined =
				gamut->white_point.predefined.u32all;
	else {
		data->feature.bits.CUSTOM_WHITE_POINT = 1;
		data->white_point.custom.white_x =
				gamut->white_point.custom.white_x;
		data->white_point.custom.white_y =
				gamut->white_point.custom.white_y;
	}
	return true;
}

bool dal_ds_translate_gamut_reference(
	const struct ds_gamut_reference_data *ref,
	enum adjustment_id *adj_id)
{
	if (ref->gamut_ref == DS_GAMUT_REFERENCE_DESTINATION)
		*adj_id = ADJ_ID_GAMUT_DESTINATION;
	else {
		if (ref->gamut_content == DS_GAMUT_CONTENT_GRAPHICS)
			*adj_id = ADJ_ID_GAMUT_SOURCE_GRPH;
		else
			*adj_id = ADJ_ID_GAMUT_SOURCE_OVL;
	}
	return true;
}
