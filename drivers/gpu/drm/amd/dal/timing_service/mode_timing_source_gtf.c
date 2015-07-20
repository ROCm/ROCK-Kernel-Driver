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

#define CELL_GRANULARITY 8
#define MIN_VSYNC_BACK_PORCH 550
#define MIN_PORCH 1
#define VSYNC_REQUESTED 3
#define C_PRIME 30
#define M_PRIME 300
#define H_SYNC_PERCENT 8
#define PIXEL_CLOCK_MULTIPLIER 1000

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
	struct crtc_timing timing = { 0 };
	/* STAGE 1 */
	/* Step 1 */
	uint32_t h_pixels_rnd = (mode->pixel_width / CELL_GRANULARITY)*
							CELL_GRANULARITY;
	uint32_t v_field_rate_req = mode->field_rate;
	uint32_t v_lines_rnd;
	uint32_t v_sync_bp;
	struct fixed32_32 interlace;
	struct fixed32_32 h_period_est;
	uint32_t total_pixels;
	struct fixed32_32 h_period;
	uint32_t addr_lines_per_frame;
	struct fixed32_32 total_lines_per_frame;
	uint32_t h_sync_pixels;
	uint32_t h_blank_pixels;
	uint32_t h_front_porch;
	struct fixed32_32 v_front_porch;
	struct fixed32_32 pixel_freq;

	if (crtc_timing == 0)
		return false;

	if (v_field_rate_req == 0)
		return false;

	/* Step 3*/


	if (mode->flags.INTERLACE) {
		/* Step 2*/
		v_lines_rnd = (mode->pixel_height + 1) / 2;
		/* Step 4 - Skip: Top Margins*/
		/* Step 5 - Skip: Bottom Margins*/

		/* Step 6*/
		interlace = dal_fixed32_32_from_fraction(1, 2);
	} else {
		/* Step 2*/
		v_lines_rnd = mode->pixel_height;

		/* Step 4 - Skip: Top Margins*/
		/* Step 5 - Skip: Bottom Margins*/

		/* Step 6*/
		interlace = dal_fixed32_32_from_int(0u);
	}

	/* Step 7*/
	h_period_est = dal_fixed32_32_sub(
		dal_fixed32_32_from_fraction(1000000u, v_field_rate_req),
		dal_fixed32_32_from_int(MIN_VSYNC_BACK_PORCH));
	h_period_est = dal_fixed32_32_div(h_period_est,
		dal_fixed32_32_add(interlace,
			dal_fixed32_32_from_int(v_lines_rnd + MIN_PORCH)));

	/* Step 8*/
	{
		struct fixed32_32 fx_vsync_bp = dal_fixed32_32_div(
			dal_fixed32_32_from_int(MIN_VSYNC_BACK_PORCH),
			h_period_est);
		v_sync_bp = dal_fixed32_32_round(fx_vsync_bp);
	}

	/* Step 9*/
	if (v_sync_bp < VSYNC_REQUESTED)
		return false;
	{
		/* Step 10*/
		struct fixed32_32 total_v_lines = dal_fixed32_32_add(
			interlace,
			dal_fixed32_32_from_int(
				v_lines_rnd + v_sync_bp + MIN_PORCH));

		/* Step 11*/
		struct fixed32_32 v_field_rate_est = dal_fixed32_32_div(
			dal_fixed32_32_div(dal_fixed32_32_from_int(1000000),
								h_period_est),
							total_v_lines);

		/* Step 12*/
		h_period = dal_fixed32_32_div_int(
			dal_fixed32_32_mul(h_period_est, v_field_rate_est),
						v_field_rate_req);
	}

	/* Step 13 - Skip*/
	/* Step 14 - Skip*/
	/* Step 15 - Skip: Left Margins*/
	/* Step 16 - Skip: Right Margins*/

	{
		/* Step 17*/
		uint32_t total_addr = h_pixels_rnd;
		/* Step 18*/
		struct fixed32_32 fx_c_prime =
			dal_fixed32_32_from_int(C_PRIME);
		struct fixed32_32 fx_m_prime = dal_fixed32_32_div_int(
			dal_fixed32_32_mul_int(h_period, M_PRIME), 1000);
		struct fixed32_32 ideal_duty_cycle = dal_fixed32_32_sub(
			fx_c_prime, fx_m_prime);
		/*Step 19*/
		struct fixed32_32 fx_h_blank = dal_fixed32_32_mul_int(
			ideal_duty_cycle, total_addr);
		fx_h_blank = dal_fixed32_32_div(fx_h_blank,
			dal_fixed32_32_sub(dal_fixed32_32_from_int(100),
				ideal_duty_cycle));
		fx_h_blank = dal_fixed32_32_div_int(fx_h_blank,
			CELL_GRANULARITY * 2);

		h_blank_pixels = dal_fixed32_32_round(fx_h_blank)*
							CELL_GRANULARITY * 2;
		/*Step 20*/
		total_pixels = total_addr + h_blank_pixels;
	}

	/* Step 21 (and convert to kHz from MHz)*/
	pixel_freq = dal_fixed32_32_div(
		dal_fixed32_32_from_int(
			total_pixels * (1000000 / PIXEL_CLOCK_MULTIPLIER)),
			h_period);

	/* Step 22*/
	/*uQ32_32 HFreq = uQ32_32(1000u) / HPeriod;*/

	/* STAGE 2 */
	/* Step 1 */
	addr_lines_per_frame = v_lines_rnd;
	/* Step 3*/
	total_lines_per_frame = dal_fixed32_32_add(interlace,
		dal_fixed32_32_from_int(v_lines_rnd + v_sync_bp + MIN_PORCH));

	if (mode->flags.INTERLACE) {
		addr_lines_per_frame *= 2;
		total_lines_per_frame = dal_fixed32_32_mul_int(
			total_lines_per_frame, 2);
	}

	{
	/* Step 17*/
		struct fixed32_32 fx_h_sync = dal_fixed32_32_from_int(
					H_SYNC_PERCENT * total_pixels);
		fx_h_sync = dal_fixed32_32_div_int(
			fx_h_sync, CELL_GRANULARITY * 100);
		h_sync_pixels = dal_fixed32_32_round(fx_h_sync) *
							CELL_GRANULARITY;


	}

	/* Step 18*/
	if ((h_blank_pixels / 2) < h_sync_pixels)
		return false;

	h_front_porch = (h_blank_pixels / 2) - h_sync_pixels;

	/* Step 36*/
	v_front_porch = dal_fixed32_32_add(
		dal_fixed32_32_from_int(MIN_PORCH), interlace);

	/* Step 37*/
	if (mode->flags.INTERLACE)
		v_front_porch = dal_fixed32_32_add_int(
			v_front_porch, MIN_PORCH);

	/* Pack the timing variables into our CrtcTiming structure*/
	timing.h_total = total_pixels;
	timing.h_border_left = 0;
	timing.h_border_right = 0;
	timing.h_addressable = h_pixels_rnd;

	timing.h_front_porch = h_front_porch;
	timing.h_sync_width = h_sync_pixels;
	timing.v_total = dal_fixed32_32_floor(total_lines_per_frame);
	timing.v_border_bottom = 0;
	timing.v_border_top = 0;
	timing.v_addressable = addr_lines_per_frame;
	timing.v_front_porch = dal_fixed32_32_round(v_front_porch);
	timing.v_sync_width = VSYNC_REQUESTED;
	timing.pix_clk_khz = dal_fixed32_32_floor(pixel_freq);
	timing.timing_standard = TIMING_STANDARD_GTF;
	timing.timing_3d_format = TIMING_3D_FORMAT_NONE;
	timing.flags.HSYNC_POSITIVE_POLARITY = false;
	timing.flags.VSYNC_POSITIVE_POLARITY = true;
	timing.flags.DOUBLESCAN = false;
	timing.display_color_depth = DISPLAY_COLOR_DEPTH_UNDEFINED;
	timing.pixel_encoding = PIXEL_ENCODING_UNDEFINED;
	timing.flags.PIXEL_REPETITION = 0;
	timing.flags.INTERLACE = (interlace.value) ? true : false;

	*crtc_timing = timing;

	return false;
}

static bool is_timing_in_standard(const struct crtc_timing *crtc_timing)
{
	return true;
}

struct mode_timing_source_funcs *dal_mode_timing_source_gtf_create(void)
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
