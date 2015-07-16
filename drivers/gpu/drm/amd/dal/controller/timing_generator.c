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

#include "timing_generator.h"
#include "crtc_overscan_color.h"

enum black_color_format {
	BLACK_COLOR_FORMAT_RGB_FULLRANGE = 0,	/* used as index in array */
	BLACK_COLOR_FORMAT_RGB_LIMITED,
	BLACK_COLOR_FORMAT_YUV_TV,
	BLACK_COLOR_FORMAT_YUV_CV,
	BLACK_COLOR_FORMAT_YUV_SUPER_AA,

	BLACK_COLOR_FORMAT_COUNT
};

bool dal_timing_generator_construct(
		struct timing_generator *tg,
		enum controller_id id)
{
	tg->controller_id = id;
	return true;
}

#define NUMBER_OF_FRAME_TO_WAIT_ON_TRIGGERED_RESET 10

/**
 *****************************************************************************
 *  Function: force_triggered_reset_now
 *
 *  @brief
 *     Complete operation for CRTC triggered reset:
 *        1. Enables TimingGenerator triggered reset
 *        2. Wait until reset occurs
 *        3. Disables TimingGenerator triggered reset
 *
 *  @param [in] pTriggerParams: triggering data (IO source + trigger edge)
 *
 *  @return
 *     true if CRTC was succefully reset, false otherwise
 *****************************************************************************
 */

bool dal_timing_generator_force_triggered_reset_now(
	struct timing_generator *tg,
	const struct trigger_params *trigger_params)
{
	bool success = false;
	uint32_t i;

	/* Enable CRTC triggered reset */
	if (!tg->funcs->enable_reset_trigger(tg, trigger_params))
		return success;

	/* To avoid endless loop we wait at most <x> frames for the reset to
	 * occur */
	for (i = 0; i < NUMBER_OF_FRAME_TO_WAIT_ON_TRIGGERED_RESET; i++) {
		/* Make sure CRTC is running */
		if (!tg->funcs->is_counter_moving(tg))
			break;

		if (tg->funcs->did_triggered_reset_occur(tg)) {
			success = true;
			break;
		}

		/* Wait one frame */
		tg->funcs->wait_for_vactive(tg);
		tg->funcs->wait_for_vblank(tg);
	}

	ASSERT(success);

	/* We are done (reset occured or failed to reset) - need to disable
	 * reset trigger
	 */
	tg->funcs->disable_reset_trigger(tg);
	return success;
}

/**
* Wait till we are in VActive (anywhere in VActive)
* Note: for now use use quick easy polling.
*/
void dal_timing_generator_wait_for_vactive(struct timing_generator *tg)
{
	/* To prevent infinite loop for checking IsInVerticalBlank, we will
	 * check IsCounterMoving for every 100 call to IsVerticalBlank and break
	 * if counter are stopped
	 */
	uint32_t i = 0;

	while (tg->funcs->is_in_vertical_blank(tg))
		if (i++ % 100 == 0)
			if (!tg->funcs->is_counter_moving(tg))
				break;
}

/**
* Wait till we are in VBlank
* Note: for now use use quick easy polling.
*/
void dal_timing_generator_wait_for_vblank(struct timing_generator *tg)
{
	/* To prevent infinite loop for checking IsInVerticalBlank, we will
	 * check IsCounterMoving for every 100 call to IsVerticalBlank and break
	 * if counter are stopped
	 */
	uint32_t i = 0;

	/* We want to catch beginning of VBlank here, so if the first try are
	 * in VBlank, we might be very close to Active, in this case wait for
	 * another frame
	 */
	while (tg->funcs->is_in_vertical_blank(tg))
		if (i++ % 100 == 0)
			if (!tg->funcs->is_counter_moving(tg))
				break;

	while (!tg->funcs->is_in_vertical_blank(tg))
		if (i++ % 100 == 0)
			if (!tg->funcs->is_counter_moving(tg))
				break;
}

static const struct crtc_black_color black_color_format[] = {
	/* BlackColorFormat_RGB_FullRange */
	{0, 0, 0},
	/* BlackColorFormat_RGB_Limited */
	{CRTC_OVERSCAN_COLOR_BLACK_COLOR_B_RGB_LIMITED_RANGE,
		CRTC_OVERSCAN_COLOR_BLACK_COLOR_G_RGB_LIMITED_RANGE,
		CRTC_OVERSCAN_COLOR_BLACK_COLOR_R_RGB_LIMITED_RANGE},
	/* BlackColorFormat_YUV_TV */
	{CRTC_OVERSCAN_COLOR_BLACK_COLOR_B_CB_YUV_4TV,
		CRTC_OVERSCAN_COLOR_BLACK_COLOR_G_Y_YUV_4TV,
		CRTC_OVERSCAN_COLOR_BLACK_COLOR_R_CR_YUV_4TV},
	/* BlackColorFormat_YUV_CV */
	{CRTC_OVERSCAN_COLOR_BLACK_COLOR_B_CB_YUV_4CV,
		CRTC_OVERSCAN_COLOR_BLACK_COLOR_G_Y_YUV_4CV,
		CRTC_OVERSCAN_COLOR_BLACK_COLOR_R_CR_YUV_4CV},
	/* BlackColorFormat_YUV_SuperAA */
	{CRTC_OVERSCAN_COLOR_BLACK_COLOR_B_CB_YUV_4SUPERAA,
		CRTC_OVERSCAN_COLOR_BLACK_COLOR_G_Y_YUV_4SUPERAA,
		CRTC_OVERSCAN_COLOR_BLACK_COLOR_R_CR_YUV_4SUPERAA}
};

void dal_timing_generator_color_space_to_black_color(
		enum color_space colorspace,
	struct crtc_black_color *black_color)
{
	switch (colorspace) {
	case COLOR_SPACE_YPBPR601:
		*black_color = black_color_format[BLACK_COLOR_FORMAT_YUV_TV];
		break;

	case COLOR_SPACE_YPBPR709:
	case COLOR_SPACE_YCBCR601:
	case COLOR_SPACE_YCBCR709:
		*black_color = black_color_format[BLACK_COLOR_FORMAT_YUV_CV];
		break;

	case COLOR_SPACE_N_MVPU_SUPER_AA:
		/* In crossfire SuperAA mode, the slave overscan data is forced
		 * to 0 in the pixel mixer on the master.  As a result, we need
		 * to adjust the blank color so that after blending the
		 * master+slave, it will appear black
		 */
		*black_color =
			black_color_format[BLACK_COLOR_FORMAT_YUV_SUPER_AA];
		break;

	case COLOR_SPACE_SRGB_LIMITED_RANGE:
		*black_color =
			black_color_format[BLACK_COLOR_FORMAT_RGB_LIMITED];
		break;

	default:
		/* fefault is sRGB black (full range). */
		*black_color =
			black_color_format[BLACK_COLOR_FORMAT_RGB_FULLRANGE];
		/* default is sRGB black 0. */
		break;
	}
}

/**
* apply_front_porch_workaround
*
* This is a workaround for a bug that has existed since R5xx and has not been
* fixed keep Front porch at minimum 2 for Interlaced mode or 1 for progressive.
*/
void dal_timing_generator_apply_front_porch_workaround(
	struct timing_generator *tg,
	struct hw_crtc_timing *timing)
{
	if (timing->flags.INTERLACED == 1) {
		if ((timing->v_sync_start - timing->v_addressable) < 2)
			timing->v_sync_start = timing->v_addressable + 2;
	} else {
		if ((timing->v_sync_start - timing->v_addressable) < 1)
			timing->v_sync_start = timing->v_addressable + 1;
	}
}

int32_t dal_timing_generator_get_vsynch_and_front_porch_size(
	const struct hw_crtc_timing *timing)
{
	int32_t front_porch = timing->v_sync_start - timing->v_addressable -
		timing->v_overscan_bottom + timing->flags.INTERLACED;

	int32_t synch_width = timing->v_sync_width;

	return synch_width + front_porch;
}

/**
* dal_timing_generator_validate_timing
* The timing generators support a maximum display size of is 8192 x 8192 pixels,
* including both active display and blanking periods. Check H Total and V Total.
*/
bool dal_timing_generator_validate_timing(
	struct timing_generator *tg,
	const struct hw_crtc_timing *hw_crtc_timing,
	enum signal_type signal)
{
	uint32_t h_blank;
	uint32_t h_front_porch;
	uint32_t h_back_porch;

	ASSERT(hw_crtc_timing != NULL);

	if (!hw_crtc_timing)
		return false;

	/* Check maximum number of pixels supported by Timing Generator
	 * (Currently will never fail, in order to fail needs display which
	 * needs more than 8192 horizontal and
	 * more than 8192 vertical total pixels)
	 */
	if (hw_crtc_timing->h_total > tg->max_h_total ||
		hw_crtc_timing->v_total > tg->max_v_total)
		return false;

	h_blank = (hw_crtc_timing->h_total - hw_crtc_timing->h_addressable -
		hw_crtc_timing->h_overscan_right -
		hw_crtc_timing->h_overscan_left) *
			hw_crtc_timing->flags.PIXEL_REPETITION;

	if (h_blank < tg->min_h_blank)
		return false;

	h_front_porch = (hw_crtc_timing->h_sync_start -
		hw_crtc_timing->h_addressable -
		hw_crtc_timing->h_overscan_right) *
			hw_crtc_timing->flags.PIXEL_REPETITION;

	if (h_front_porch < tg->min_h_front_porch)
		return false;

	h_back_porch = h_blank - (hw_crtc_timing->h_sync_start -
		hw_crtc_timing->h_addressable -
		hw_crtc_timing->h_overscan_right -
		hw_crtc_timing->h_sync_width) *
			hw_crtc_timing->flags.PIXEL_REPETITION;

	if (h_back_porch < tg->min_h_back_porch)
		return false;

	if (hw_crtc_timing->flags.PIXEL_REPETITION > 1) {
		if (signal != SIGNAL_TYPE_HDMI_TYPE_A)
			/* Pixel Repetition is ONLY allowed on HDMI */
			return false;
	}

	return true;
}
