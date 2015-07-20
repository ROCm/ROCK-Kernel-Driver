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
#include "mode_timing_source.h"
#include "include/fixed32_32.h"

#define CELL_GRAN_RND 8
/*ClockStep(25,100),*/
#define C_PRIME 30
#define H_SYNC_PER 8
#define MIN_V_BPORCH 6
#define MIN_V_PORCH_RND 3
#define MIN_V_SYNC_BP 550
#define M_PRIME 300
#define RB_H_BLANK 160
#define RB_H_SYNC 32
#define RB_MIN_VBLANK 460
#define RB_V_FPORCH 3
#define PIXEL_CLOCK_MULTIPLIER 1000

struct cvt_common_params {
	uint32_t v_field_rate_rqd;
	uint32_t h_pixels_rnd;
	uint32_t total_addressable_pixels;
	uint32_t v_lines_rnd;
	struct fixed32_32 interlace;
	uint32_t v_sync_rnd;
};

static uint32_t calc_v_sync_width(uint32_t w, uint32_t h)
{
	if ((h * 4) == (w * 3)) {
		/* For 4:3 Aspect, vertical sync width is 4 */
		return 4;
	} else if ((h * 16) == (w * 9)) {
		/* For 16:9 Aspect, vertical sync width is 5 */
		return 5;
	} else if ((h * 16) == (w * 10)) {
		/* For 16:10 Aspect, vertical sync width is 6 */
		return 6;
	} else if (((h * 5) == (w * 4))
		|| ((h * 15) == (w * 9))) {
		/* For 5:4 or 15:9 Aspect, vertical sync width is 7 */
		return 7;
	} else {
		/* For other 'non standard' aspect ratios,
		 vertical sync width is 10 */
		return 10;
	}
}

static void calc_common_params(
	const struct mode_info *mode,
	struct cvt_common_params *params)
{
	/* STAGE 1 */

	/* Step 1 */
	params->v_field_rate_rqd = mode->field_rate;

	if (mode->flags.INTERLACE) {
		/* Step 5*/
		params->v_lines_rnd = mode->pixel_height / 2;
		/* Step 7*/
		params->interlace = dal_fixed32_32_from_fraction(1, 2);
	} else {
		/* Step 5*/
		params->v_lines_rnd = mode->pixel_height;
		/* Step 7t*/
		params->interlace = dal_fixed32_32_from_int(0);
	}

	/* Step 2 */
	params->h_pixels_rnd =
		(mode->pixel_width / CELL_GRAN_RND) * CELL_GRAN_RND;

	/* Step 3 - Skip: Left Margin, Right Margin*/

	/* Step 4*/
	params->total_addressable_pixels = params->h_pixels_rnd;

	/* Step 6 - Skip: Top Margin, Bottom Margin*/

	/* Miscellaneous common step*/
	params->v_sync_rnd = calc_v_sync_width(mode->pixel_width,
						mode->pixel_height);
}

static bool calc_normal_timing(
	struct crtc_timing *crtc_timing,
	struct cvt_common_params *params)
{

	struct crtc_timing timing = { 0 };
	struct fixed32_32 h_period_est;
	struct fixed32_32 total_v_lines;
	struct fixed32_32 pixel_clock;
	uint32_t v_sync_bp;
	uint32_t h_sync_rnd;
	uint32_t h_blank;
	uint32_t total_pixels;
	uint32_t h_front_porch;
	uint32_t v_addr_lines;


	if (crtc_timing == NULL)
		return false;

	if (params->v_field_rate_rqd == 0)
		return false;

	/* STAGE 2 */
	/* Step 8 */
	h_period_est = dal_fixed32_32_sub(
		dal_fixed32_32_from_fraction(1000000,
			params->v_field_rate_rqd),
				dal_fixed32_32_from_int(MIN_V_SYNC_BP));
	h_period_est = dal_fixed32_32_div(h_period_est,
		dal_fixed32_32_add_int(params->interlace,
			params->v_lines_rnd + MIN_V_PORCH_RND));

	{
		/* Step 9 */
		struct fixed32_32 fx_v_sync_bp = dal_fixed32_32_div(
			dal_fixed32_32_from_int(MIN_V_SYNC_BP),
			h_period_est);
		v_sync_bp = dal_fixed32_32_floor(fx_v_sync_bp) + 1;
	}

	if (v_sync_bp < (params->v_sync_rnd + MIN_V_BPORCH))
		v_sync_bp = params->v_sync_rnd + MIN_V_BPORCH;


	/* Step 10*/

	/* Step 11 */
	total_v_lines = dal_fixed32_32_add_int(params->interlace,
		params->v_lines_rnd + v_sync_bp + MIN_V_PORCH_RND);

	{
		/* Step 12 */
		struct fixed32_32 fx_c_prime =
			dal_fixed32_32_from_int(C_PRIME);
		struct fixed32_32 fx_m_prime = dal_fixed32_32_div_int(
			dal_fixed32_32_mul_int(h_period_est, M_PRIME),
								1000);
		struct fixed32_32 ideal_duty_cycle = dal_fixed32_32_sub(
			fx_c_prime, fx_m_prime);

		/* Step 13 */
		struct fixed32_32 fx_h_blank;

		if (dal_fixed32_32_lt_int(ideal_duty_cycle, 20))
			ideal_duty_cycle = dal_fixed32_32_from_int(20);

		fx_h_blank = dal_fixed32_32_mul_int(
					ideal_duty_cycle,
					params->total_addressable_pixels);
		fx_h_blank = dal_fixed32_32_div(fx_h_blank,
			dal_fixed32_32_sub(
					dal_fixed32_32_from_int(100),
					ideal_duty_cycle));
		fx_h_blank = dal_fixed32_32_div_int(fx_h_blank,
			CELL_GRAN_RND * 2);
		h_blank = dal_fixed32_32_floor(fx_h_blank) * CELL_GRAN_RND * 2;
	}


	/* Step 14*/
	total_pixels = params->total_addressable_pixels + h_blank;

	{
		/* Step 15*/
		struct fixed32_32 act_h_freq;
		struct fixed32_32 act_field_rate;
		struct fixed32_32 clock_step =
			dal_fixed32_32_from_fraction(25, 100);
		struct fixed32_32 act_pix_freq = dal_fixed32_32_div(
			dal_fixed32_32_div(
				dal_fixed32_32_from_int(total_pixels),
				h_period_est),
						clock_step);
		/* Step 16 */
		act_pix_freq = dal_fixed32_32_mul(
			dal_fixed32_32_from_int(
				dal_fixed32_32_floor(act_pix_freq)),
						clock_step);

		act_h_freq = dal_fixed32_32_div_int(
			dal_fixed32_32_mul_int(act_pix_freq, 1000),
							total_pixels);

		/* Step 17*/
		act_field_rate = dal_fixed32_32_div(
				dal_fixed32_32_mul_int(act_h_freq, 1000),
								total_v_lines);
		/* Step 18 Skip*/
		/* Calculate final pixel clock in kHz*/
		pixel_clock = dal_fixed32_32_mul(act_pix_freq,
			dal_fixed32_32_from_fraction(
				1000000, PIXEL_CLOCK_MULTIPLIER));
	}

	/* MISC Calculations  */
	/* Calculate horizontal sync*/
	{
		struct fixed32_32 fx_h_sync = dal_fixed32_32_from_fraction(
			H_SYNC_PER * total_pixels, 100 * CELL_GRAN_RND);
		h_sync_rnd = dal_fixed32_32_floor(fx_h_sync) * CELL_GRAN_RND;
	}
	/* Calculate horizontal front porch*/
	h_front_porch = h_blank - (h_blank / 2) - h_sync_rnd;

	/* Calculate vertical total and addressable lines*/

	v_addr_lines = params->v_lines_rnd;
	if (params->interlace.value) {
		total_v_lines = dal_fixed32_32_mul_int(total_v_lines, 2);
		v_addr_lines *= 2;
	}

	/* Convert all values to CrtcTiming structure*/

	timing.h_total = total_pixels;
	timing.h_border_left = params->total_addressable_pixels;
	timing.h_border_right = 0;
	timing.h_front_porch = h_front_porch;
	timing.h_sync_width = h_sync_rnd;
	timing.v_total = dal_fixed32_32_floor(total_v_lines);
	timing.v_border_bottom = 0;
	timing.v_border_top = 0;
	timing.v_addressable = v_addr_lines;
	timing.v_front_porch = MIN_V_PORCH_RND;
	timing.v_sync_width = params->v_sync_rnd;
	timing.pix_clk_khz = dal_fixed32_32_floor(pixel_clock);
	timing.timing_standard = TIMING_STANDARD_CVT;
	timing.timing_3d_format = TIMING_3D_FORMAT_NONE;
	timing.flags.HSYNC_POSITIVE_POLARITY = false;
	timing.flags.VSYNC_POSITIVE_POLARITY = true;
	timing.flags.DOUBLESCAN = false;
	timing.flags.PIXEL_REPETITION = 0;
	timing.display_color_depth = DISPLAY_COLOR_DEPTH_UNDEFINED;
	timing.pixel_encoding = PIXEL_ENCODING_UNDEFINED;
	timing.flags.INTERLACE = (params->interlace.value) ? true : false;

	/* Return the completed values*/
	*crtc_timing = timing;

	return true;
}

static bool calc_reduced_blanking_timing(
	struct crtc_timing *crtc_timing,
	struct cvt_common_params *params)
{
	struct crtc_timing timing = { 0 };
	struct fixed32_32 h_period_est;
	struct fixed32_32 total_v_lines;
	struct fixed32_32 pixel_clock;
	uint32_t total_pixels;
	uint32_t h_front_porch;
	uint32_t v_addr_lines;

	if (crtc_timing == NULL)
		return false;

	if (params->v_field_rate_rqd == 0 ||
		params->v_lines_rnd == 0)
		return false;

	{
		/* Step 8 */
		struct fixed32_32 fx_r = dal_fixed32_32_sub(
			dal_fixed32_32_from_fraction(1000000,
				params->v_field_rate_rqd),
				dal_fixed32_32_from_int(RB_MIN_VBLANK));

		h_period_est =
			dal_fixed32_32_div_int(fx_r, params->v_lines_rnd);
	}


	{
		/* Step 9*/
		struct fixed32_32 fx_vbi_l = dal_fixed32_32_div(
			dal_fixed32_32_from_int(RB_MIN_VBLANK),
							h_period_est);
		uint32_t vbi_lines = dal_fixed32_32_floor(fx_vbi_l) + 1;

		/* Step 10*/
		uint32_t rb_min_vbi =
			RB_V_FPORCH + params->v_sync_rnd + MIN_V_BPORCH;
		uint32_t act_vbi_lines =
			(vbi_lines < rb_min_vbi) ? rb_min_vbi : vbi_lines;

		/* Step 11 */
		total_v_lines = dal_fixed32_32_add_int(params->interlace,
				act_vbi_lines + params->v_lines_rnd);


	}

	/* Step 12*/
	total_pixels = RB_H_BLANK + params->total_addressable_pixels;

	{
	/* Step 13*/
		struct fixed32_32 clock_step =
			dal_fixed32_32_from_fraction(25, 100);

		struct fixed32_32 act_pix_freq = dal_fixed32_32_mul_int(
			total_v_lines,
			params->v_field_rate_rqd * total_pixels);
		act_pix_freq = dal_fixed32_32_div_int(act_pix_freq, 1000000);
		act_pix_freq = dal_fixed32_32_div(act_pix_freq, clock_step);
		act_pix_freq = dal_fixed32_32_mul(
			dal_fixed32_32_from_int(
				dal_fixed32_32_floor(act_pix_freq)),
					clock_step);

		/* Calculate final pixel clock in kHz*/
		pixel_clock = dal_fixed32_32_mul(
			act_pix_freq,
			dal_fixed32_32_from_fraction(
				1000000, PIXEL_CLOCK_MULTIPLIER));
	}

	/* MISC Calculations */

	/* Calculate horizontal front porch*/
	if ((RB_H_BLANK - (RB_H_BLANK / 2)) < RB_H_SYNC)
		return false;

	h_front_porch = RB_H_BLANK - (RB_H_BLANK / 2) - RB_H_SYNC;

	/* Calculate vertical total and addressable lines*/
	v_addr_lines = params->v_lines_rnd;
	if (params->interlace.value) {
		total_v_lines = dal_fixed32_32_mul_int(total_v_lines, 2);
		v_addr_lines *= 2;
	}

	/* Convert all values to CrtcTiming structure*/

	timing.h_total = total_pixels;
	timing.h_border_left = 0;
	timing.h_border_right = 0;
	timing.h_addressable = params->total_addressable_pixels;
	timing.h_front_porch = h_front_porch;
	timing.h_sync_width = RB_H_SYNC;
	timing.v_total = dal_fixed32_32_floor(total_v_lines);
	timing.v_border_bottom = 0;
	timing.v_border_top = 0;
	timing.v_addressable = v_addr_lines;
	timing.v_front_porch = RB_V_FPORCH;
	timing.v_sync_width = params->v_sync_rnd;
	timing.pix_clk_khz = dal_fixed32_32_floor(pixel_clock);
	timing.timing_standard = TIMING_STANDARD_CVT_RB;
	timing.timing_3d_format = TIMING_3D_FORMAT_NONE;
	timing.flags.HSYNC_POSITIVE_POLARITY = false;
	timing.flags.VSYNC_POSITIVE_POLARITY = true;
	timing.flags.DOUBLESCAN = false;
	timing.display_color_depth = DISPLAY_COLOR_DEPTH_UNDEFINED;
	timing.pixel_encoding = PIXEL_ENCODING_UNDEFINED;
	timing.flags.PIXEL_REPETITION = 0;
	timing.flags.INTERLACE = (params->interlace.value) ? true : false;

	/* Return the completed values*/
	*crtc_timing = timing;
	return true;
}


static uint32_t get_mode_timing_count(void)
{
	return 0;
}

static const struct mode_timing *get_mode_timing_by_index(uint32_t index)
{
	return NULL;
}

static bool get_timing_for_mode(
		const struct mode_info *mode,
		struct crtc_timing *crtc_timing)
{
	/* Calculate the CVT common parameters*/
	struct cvt_common_params params;

	if (crtc_timing == NULL)
		return false;

	calc_common_params(mode, &params);

	/* check if we need to use reduced blanking algorithm or not*/
	if (mode->timing_standard == TIMING_STANDARD_CVT_RB)
		return calc_reduced_blanking_timing(crtc_timing, &params);
	else
		return calc_normal_timing(crtc_timing, &params);
}

static bool is_timing_in_standard(const struct crtc_timing *crtc_timing)
{
	return true;
}

struct mode_timing_source_funcs *dal_mode_timing_source_cvt_create(void)
{
	struct mode_timing_source_funcs *mts = dal_alloc(
			sizeof(struct mode_timing_source_funcs));
	if (mts == NULL)
		return NULL;
	if (!dal_mode_timing_source_construct(mts))
		goto out_free;
	mts->is_timing_in_standard = is_timing_in_standard;
	mts->get_timing_for_mode = get_timing_for_mode;
	mts->get_mode_timing_by_index = get_mode_timing_by_index;
	mts->get_mode_timing_count = get_mode_timing_count;

	return mts;
out_free:
	dal_free(mts);
	return NULL;
}
