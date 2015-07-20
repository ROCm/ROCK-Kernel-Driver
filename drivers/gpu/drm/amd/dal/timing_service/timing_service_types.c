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
#include "include/timing_service_types.h"

bool dal_mode_info_less_than(
		const void *lhs_address,
		const void *rhs_address)
{
	const struct mode_info *lhs = lhs_address;
	const struct mode_info *rhs = rhs_address;

	/* order from highest to lowest priority */
	if (lhs->pixel_width < rhs->pixel_width)
		return true;
	if (lhs->pixel_width > rhs->pixel_width)
		return false;

	if (lhs->pixel_height < rhs->pixel_height)
		return true;
	if (lhs->pixel_height > rhs->pixel_height)
		return false;

	if (lhs->field_rate < rhs->field_rate)
		return true;
	if (lhs->field_rate > rhs->field_rate)
		return false;

	/* interlaced < progressive */
	if (lhs->flags.INTERLACE > rhs->flags.INTERLACE)
		return true;
	if (lhs->flags.INTERLACE < rhs->flags.INTERLACE)
		return false;

	/* VIDEO_OPTIMIZED_RATE (refreshrate/1.001) <
	 *			non VIDEO_OPTIMIZED_RATE (refreshrate/1.000) */
	if (lhs->flags.VIDEO_OPTIMIZED_RATE > rhs->flags.VIDEO_OPTIMIZED_RATE)
		return true;
	if (lhs->flags.VIDEO_OPTIMIZED_RATE < rhs->flags.VIDEO_OPTIMIZED_RATE)
		return false;

	/* normal blanking < reduced blanking */
	if (lhs->flags.REDUCED_BLANKING < rhs->flags.REDUCED_BLANKING)
		return true;
	if (lhs->flags.REDUCED_BLANKING > rhs->flags.REDUCED_BLANKING)
		return false;

	/* preferred < non preferred */
	if (lhs->flags.PREFERRED > rhs->flags.PREFERRED)
		return true;
	if (lhs->flags.PREFERRED < rhs->flags.PREFERRED)
		return false;

	/* native < non native */
	if (lhs->flags.NATIVE > rhs->flags.NATIVE)
		return true;
	if (lhs->flags.NATIVE < rhs->flags.NATIVE)
		return false;

	/* TIMING_SOURCE_UNDEFINED considered lowest priority
	 * (should be referred as highest value in enumeration) */
	return (lhs->timing_source != TIMING_SOURCE_UNDEFINED &&
			lhs->timing_source < rhs->timing_source);
}

bool dal_mode_info_is_equal(
		const struct mode_info *lhs,
		const struct mode_info *rhs)
{
	if (lhs->pixel_height != rhs->pixel_height)
		return false;
	if (lhs->pixel_width != rhs->pixel_width)
		return false;
	if (lhs->flags.INTERLACE != rhs->flags.INTERLACE)
		return false;
	if (lhs->flags.VIDEO_OPTIMIZED_RATE != rhs->flags.VIDEO_OPTIMIZED_RATE)
		return false;
	if (lhs->flags.REDUCED_BLANKING != rhs->flags.REDUCED_BLANKING)
		return false;
	if (lhs->field_rate != rhs->field_rate)
		return false;
	if (lhs->flags.PREFERRED != rhs->flags.PREFERRED)
		return false;
	if (lhs->flags.NATIVE != rhs->flags.NATIVE)
		return false;

	/* TIMING_STANDARD_UNDEFINED implies wildcard */
	if (lhs->timing_standard != TIMING_STANDARD_UNDEFINED &&
			rhs->timing_standard != TIMING_STANDARD_UNDEFINED &&
			lhs->timing_standard != rhs->timing_standard)
		return false;

	return true;
}

static bool is_encoding_priority_higher(
		enum pixel_encoding lhs,
		enum pixel_encoding rhs)
{
	static const uint32_t pix_enc_priority[] = {
			3,/* PIXEL_ENCODING_UNDEFINED*/
			2,/* PIXEL_ENCODING_RGB*/
			1,/* PIXEL_ENCODING_YCbCr422*/
			0 /* PIXEL_ENCODING_YCbCr444*/};

	return pix_enc_priority[lhs] < pix_enc_priority[rhs];
}

bool dal_crtc_timing_less_than(
		const struct crtc_timing *lhs,
		const struct crtc_timing *rhs)
{
	if (lhs->h_total < rhs->h_total)
		return true;
	if (lhs->h_total > rhs->h_total)
		return false;

	if (lhs->v_total < rhs->v_total)
		return true;
	if (lhs->v_total > rhs->v_total)
		return false;

	if (lhs->pix_clk_khz < rhs->pix_clk_khz)
		return true;
	if (lhs->pix_clk_khz > rhs->pix_clk_khz)
		return false;

	if (lhs->h_addressable < rhs->h_addressable)
		return true;
	if (lhs->h_addressable > rhs->h_addressable)
		return false;

	if (lhs->v_addressable < rhs->v_addressable)
		return true;
	if (lhs->v_addressable > rhs->v_addressable)
		return false;

	if (lhs->flags.INTERLACE < rhs->flags.INTERLACE)
		return true;
	if (lhs->flags.INTERLACE > rhs->flags.INTERLACE)
		return false;

	if (lhs->flags.DOUBLESCAN < rhs->flags.DOUBLESCAN)
		return true;
	if (lhs->flags.DOUBLESCAN > rhs->flags.DOUBLESCAN)
		return false;

	if (lhs->flags.PIXEL_REPETITION < rhs->flags.PIXEL_REPETITION)
		return true;
	if (lhs->flags.PIXEL_REPETITION > rhs->flags.PIXEL_REPETITION)
		return false;

	if (lhs->flags.HSYNC_POSITIVE_POLARITY <
			rhs->flags.HSYNC_POSITIVE_POLARITY)
		return true;

	if (lhs->flags.HSYNC_POSITIVE_POLARITY >
			rhs->flags.HSYNC_POSITIVE_POLARITY)
		return false;

	if (lhs->flags.VSYNC_POSITIVE_POLARITY <
			rhs->flags.VSYNC_POSITIVE_POLARITY)
		return true;

	if (lhs->flags.VSYNC_POSITIVE_POLARITY >
			rhs->flags.VSYNC_POSITIVE_POLARITY)
		return false;

/* higher color depth timing are higher in priority,
 * thus should be placed ahead of
 * lower color depth timing when sorting ModeTimingList.
 * therefore the comparison here is flipped */
	if (lhs->display_color_depth > rhs->display_color_depth)
		return true;
	if (lhs->display_color_depth < rhs->display_color_depth)
		return false;

	if (is_encoding_priority_higher(
			lhs->pixel_encoding,
			rhs->pixel_encoding))
		return true;
	if (is_encoding_priority_higher(
			rhs->pixel_encoding,
			lhs->pixel_encoding))
		return false;

	if (lhs->h_border_left < rhs->h_border_left)
		return true;
	if (lhs->h_border_left > rhs->h_border_left)
		return false;

	if (lhs->h_border_right < rhs->h_border_right)
		return true;
	if (lhs->h_border_right > rhs->h_border_right)
		return false;

	if (lhs->h_front_porch < rhs->h_front_porch)
		return true;
	if (lhs->h_front_porch > rhs->h_front_porch)
		return false;

	if (lhs->h_sync_width < rhs->h_sync_width)
		return true;
	if (lhs->h_sync_width > rhs->h_sync_width)
		return false;

	if (lhs->v_border_top < rhs->v_border_top)
		return true;
	if (lhs->v_border_top > rhs->v_border_top)
		return false;

	if (lhs->v_border_bottom < rhs->v_border_bottom)
		return true;
	if (lhs->v_border_bottom > rhs->v_border_bottom)
		return false;

	if (lhs->v_front_porch < rhs->v_front_porch)
		return true;
	if (lhs->v_front_porch > rhs->v_front_porch)
		return false;

	if (lhs->v_sync_width < rhs->v_sync_width)
		return true;
	if (lhs->v_sync_width > rhs->v_sync_width)
		return false;

	if (lhs->timing_3d_format < rhs->timing_3d_format)
		return true;
	if (lhs->timing_3d_format > rhs->timing_3d_format)
		return false;

	return false;
}

/* TODO might write as one return statement */
bool dal_crtc_timing_is_equal(
		const struct crtc_timing *lhs,
		const struct crtc_timing *rhs)
{
	if (lhs->h_total != rhs->h_total)
		return false;
	if (lhs->h_addressable != rhs->h_addressable)
		return false;
	if (lhs->h_border_left != rhs->h_border_left)
		return false;
	if (lhs->h_border_right != rhs->h_border_right)
		return false;
	if (lhs->h_front_porch != rhs->h_front_porch)
		return false;
	if (lhs->h_sync_width != rhs->h_sync_width)
		return false;

	if (lhs->v_total != rhs->v_total)
		return false;
	if (lhs->v_addressable != rhs->v_addressable)
		return false;
	if (lhs->v_border_top != rhs->v_border_top)
		return false;
	if (lhs->v_border_bottom != rhs->v_border_bottom)
		return false;
	if (lhs->v_front_porch != rhs->v_front_porch)
		return false;
	if (lhs->v_sync_width  != rhs->v_sync_width)
		return false;

	if (lhs->flags.INTERLACE != rhs->flags.INTERLACE)
		return false;
	if (lhs->flags.DOUBLESCAN != rhs->flags.DOUBLESCAN)
		return false;
	if (lhs->flags.PIXEL_REPETITION != rhs->flags.PIXEL_REPETITION)
		return false;
	if (lhs->flags.HSYNC_POSITIVE_POLARITY !=
			rhs->flags.HSYNC_POSITIVE_POLARITY)
		return false;
	if (lhs->flags.VSYNC_POSITIVE_POLARITY !=
			rhs->flags.VSYNC_POSITIVE_POLARITY)
		return false;
	if (lhs->flags.EXCLUSIVE_3D != rhs->flags.EXCLUSIVE_3D)
		return false;
	if (lhs->flags.RIGHT_EYE_3D_POLARITY !=
					rhs->flags.RIGHT_EYE_3D_POLARITY)
		return false;
	if (lhs->display_color_depth != rhs->display_color_depth)
		return false;
	if (lhs->pixel_encoding != rhs->pixel_encoding)
		return false;
	if (lhs->timing_3d_format != rhs->timing_3d_format)
		return false;

/* if pixel clock doesn't match exactly, check if it matches +/- 10KHz */
	if (lhs->pix_clk_khz  != rhs->pix_clk_khz &&
		(((lhs->pix_clk_khz  / 10) < ((rhs->pix_clk_khz  / 10) - 1)) ||
			((lhs->pix_clk_khz  / 10) >
					((rhs->pix_clk_khz  / 10) + 1))))
			return false;

	return true;
}

bool dal_mode_timing_less_than(
		const void *lhs_address,
		const void *rhs_address)
{
	const struct mode_timing *lhs = lhs_address;
	const struct mode_timing *rhs = rhs_address;
/* order from highest to lowest priority */
	if (lhs->mode_info.pixel_width < rhs->mode_info.pixel_width)
		return true;
	if (lhs->mode_info.pixel_width > rhs->mode_info.pixel_width)
		return false;

	if (lhs->mode_info.pixel_height < rhs->mode_info.pixel_height)
		return true;
	if (lhs->mode_info.pixel_height > rhs->mode_info.pixel_height)
		return false;

	if (lhs->mode_info.field_rate < rhs->mode_info.field_rate)
		return true;
	if (lhs->mode_info.field_rate > rhs->mode_info.field_rate)
		return false;

/* interlaced < progressive */
	if (lhs->mode_info.flags.INTERLACE > rhs->mode_info.flags.INTERLACE)
		return true;
	if (lhs->mode_info.flags.INTERLACE < rhs->mode_info.flags.INTERLACE)
		return false;

/* VIDEO_OPTIMIZED_RATE (refresh_rate/1.001) <
 *				non VIDEO_OPTIMIZED_RATE (refresh_rate/1.000) */
	if (lhs->mode_info.flags.VIDEO_OPTIMIZED_RATE
			> rhs->mode_info.flags.VIDEO_OPTIMIZED_RATE)
		return true;
	if (lhs->mode_info.flags.VIDEO_OPTIMIZED_RATE
			< rhs->mode_info.flags.VIDEO_OPTIMIZED_RATE)
		return false;

/* normal blanking < reduced blanking */
	if (lhs->mode_info.flags.REDUCED_BLANKING
			< rhs->mode_info.flags.REDUCED_BLANKING)
		return true;
	if (lhs->mode_info.flags.REDUCED_BLANKING
			> rhs->mode_info.flags.REDUCED_BLANKING)
		return false;

/* Preferred < non Preferred */
	if (lhs->mode_info.flags.PREFERRED > rhs->mode_info.flags.PREFERRED)
		return true;
	if (lhs->mode_info.flags.PREFERRED < rhs->mode_info.flags.PREFERRED)
		return false;

/* NATIVE < non NATIVE */
	if (lhs->mode_info.flags.NATIVE > rhs->mode_info.flags.NATIVE)
		return true;
	if (lhs->mode_info.flags.NATIVE < rhs->mode_info.flags.NATIVE)
		return false;

/* 3D format (2D is smaller then 3D)
 * Need to check 3D format before timing source
 *	and color depth and pixel encoding */
	if (lhs->crtc_timing.timing_3d_format <
					rhs->crtc_timing.timing_3d_format)
		return true;
	if (lhs->crtc_timing.timing_3d_format >
					rhs->crtc_timing.timing_3d_format)
		return false;

/* Timing Source (TIMING_SOURCE_UNDEFINED is lower priority - bigger) */
	if (lhs->mode_info.timing_source != TIMING_SOURCE_UNDEFINED
			&& lhs->mode_info.timing_source
					< rhs->mode_info.timing_source)
		return true;
	if (rhs->mode_info.timing_source != TIMING_SOURCE_UNDEFINED
			&& lhs->mode_info.timing_source
					> rhs->mode_info.timing_source)
		return false;

	if (lhs->crtc_timing.h_total < rhs->crtc_timing.h_total)
		return true;
	if (lhs->crtc_timing.h_total > rhs->crtc_timing.h_total)
		return false;

	if (lhs->crtc_timing.v_total < rhs->crtc_timing.v_total)
		return true;
	if (lhs->crtc_timing.v_total > rhs->crtc_timing.v_total)
		return false;

	if (lhs->crtc_timing.pix_clk_khz  < rhs->crtc_timing.pix_clk_khz)
		return true;
	if (lhs->crtc_timing.pix_clk_khz  > rhs->crtc_timing.pix_clk_khz)
		return false;

	if (lhs->crtc_timing.h_addressable
			< rhs->crtc_timing.h_addressable)
		return true;
	if (lhs->crtc_timing.h_addressable
			> rhs->crtc_timing.h_addressable)
		return false;

	if (lhs->crtc_timing.v_addressable
			< rhs->crtc_timing.v_addressable)
		return true;
	if (lhs->crtc_timing.v_addressable
			> rhs->crtc_timing.v_addressable)
		return false;

	if (lhs->crtc_timing.flags.INTERLACE < rhs->crtc_timing.flags.INTERLACE)
		return true;
	if (lhs->crtc_timing.flags.INTERLACE > rhs->crtc_timing.flags.INTERLACE)
		return false;

	if (lhs->crtc_timing.flags.DOUBLESCAN <
				rhs->crtc_timing.flags.DOUBLESCAN)
		return true;
	if (lhs->crtc_timing.flags.DOUBLESCAN >
				rhs->crtc_timing.flags.DOUBLESCAN)
		return false;

	if (lhs->crtc_timing.flags.PIXEL_REPETITION
			< rhs->crtc_timing.flags.PIXEL_REPETITION)
		return true;
	if (lhs->crtc_timing.flags.PIXEL_REPETITION
			> rhs->crtc_timing.flags.PIXEL_REPETITION)
		return false;

	if (lhs->crtc_timing.flags.HSYNC_POSITIVE_POLARITY
			< rhs->crtc_timing.flags.HSYNC_POSITIVE_POLARITY)
		return true;
	if (lhs->crtc_timing.flags.HSYNC_POSITIVE_POLARITY
			> rhs->crtc_timing.flags.HSYNC_POSITIVE_POLARITY)
		return false;

	if (lhs->crtc_timing.flags.VSYNC_POSITIVE_POLARITY
			< rhs->crtc_timing.flags.VSYNC_POSITIVE_POLARITY)
		return true;
	if (lhs->crtc_timing.flags.VSYNC_POSITIVE_POLARITY
			> rhs->crtc_timing.flags.VSYNC_POSITIVE_POLARITY)
		return false;

/* higher color depth timing are higher in priority,
 * thus should be placed ahead of
 * lower color depth timing when sorting ModeTimingList.
 * therefore the comparison here is flipped */
	if (lhs->crtc_timing.display_color_depth >
					rhs->crtc_timing.display_color_depth)
		return true;
	if (lhs->crtc_timing.display_color_depth <
					rhs->crtc_timing.display_color_depth)
		return false;

	if (is_encoding_priority_higher(lhs->crtc_timing.pixel_encoding,
			rhs->crtc_timing.pixel_encoding))
		return true;
	if (is_encoding_priority_higher(rhs->crtc_timing.pixel_encoding,
			lhs->crtc_timing.pixel_encoding))
		return false;

	if (lhs->crtc_timing.h_border_left
			< rhs->crtc_timing.h_border_left)
		return true;
	if (lhs->crtc_timing.h_border_left
			> rhs->crtc_timing.h_border_left)
		return false;

	if (lhs->crtc_timing.h_border_right
			< rhs->crtc_timing.h_border_right)
		return true;
	if (lhs->crtc_timing.h_border_right
			> rhs->crtc_timing.h_border_right)
		return false;

	if (lhs->crtc_timing.h_front_porch
			< rhs->crtc_timing.h_front_porch)
		return true;
	if (lhs->crtc_timing.h_front_porch
			> rhs->crtc_timing.h_front_porch)
		return false;

	if (lhs->crtc_timing.h_sync_width
			< rhs->crtc_timing.h_sync_width)
		return true;
	if (lhs->crtc_timing.h_sync_width
			> rhs->crtc_timing.h_sync_width)
		return false;

	if (lhs->crtc_timing.v_border_top < rhs->crtc_timing.v_border_top)
		return true;
	if (lhs->crtc_timing.v_border_top > rhs->crtc_timing.v_border_top)
		return false;

	if (lhs->crtc_timing.v_border_bottom
			< rhs->crtc_timing.v_border_bottom)
		return true;
	if (lhs->crtc_timing.v_border_bottom
			> rhs->crtc_timing.v_border_bottom)
		return false;

	if (lhs->crtc_timing.v_front_porch
			< rhs->crtc_timing.v_front_porch)
		return true;
	if (lhs->crtc_timing.v_front_porch
			> rhs->crtc_timing.v_front_porch)
		return false;

	if (lhs->crtc_timing.v_sync_width  < rhs->crtc_timing.v_sync_width)
		return true;
	if (lhs->crtc_timing.v_sync_width  > rhs->crtc_timing.v_sync_width)
		return false;

	return false;
}

bool dal_mode_timing_is_equal(
		const struct mode_timing *lhs,
		const struct mode_timing *rhs)
{
	return dal_mode_info_is_equal(&lhs->mode_info, &rhs->mode_info) &&
		dal_crtc_timing_is_equal(&lhs->crtc_timing, &rhs->crtc_timing);
}
