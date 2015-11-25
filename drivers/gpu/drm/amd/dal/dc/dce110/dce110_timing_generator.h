/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 *  and/or sell copies of the Software, and to permit persons to whom the
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

#ifndef __DC_TIMING_GENERATOR_DCE110_H__
#define __DC_TIMING_GENERATOR_DCE110_H__


#include "../include/timing_generator_types.h"
#include "../include/grph_object_id.h"

/* overscan in blank for YUV color space. For RGB, it is zero for black. */
#define CRTC_OVERSCAN_COLOR_BLACK_COLOR_B_CB_YUV_4CV 0x1f4
#define CRTC_OVERSCAN_COLOR_BLACK_COLOR_G_Y_YUV_4CV 0x40
#define CRTC_OVERSCAN_COLOR_BLACK_COLOR_R_CR_YUV_4CV 0x1f4

#define CRTC_OVERSCAN_COLOR_BLACK_COLOR_B_CB_YUV_4TV 0x200
#define CRTC_OVERSCAN_COLOR_BLACK_COLOR_G_Y_YUV_4TV 0x40
#define CRTC_OVERSCAN_COLOR_BLACK_COLOR_R_CR_YUV_4TV 0x200

/* overscan in blank for YUV color space when in SuperAA crossfire mode */
#define CRTC_OVERSCAN_COLOR_BLACK_COLOR_B_CB_YUV_4SUPERAA 0x1a2
#define CRTC_OVERSCAN_COLOR_BLACK_COLOR_G_Y_YUV_4SUPERAA 0x20
#define CRTC_OVERSCAN_COLOR_BLACK_COLOR_R_CR_YUV_4SUPERAA 0x1a2

/* OVERSCAN COLOR FOR RGB LIMITED RANGE
 * (16~253) 16*4 (Multiple over 256 code leve) =64 (0x40) */
#define CRTC_OVERSCAN_COLOR_BLACK_COLOR_B_RGB_LIMITED_RANGE 0x40
#define CRTC_OVERSCAN_COLOR_BLACK_COLOR_G_RGB_LIMITED_RANGE 0x40
#define CRTC_OVERSCAN_COLOR_BLACK_COLOR_R_RGB_LIMITED_RANGE 0X40

struct dce110_timing_generator {
	struct timing_generator base;
	enum sync_source cached_gsl_group;
	bool advanced_request_enable;
};

struct timing_generator *dce110_timing_generator_create(
	struct adapter_service *as,
	struct dc_context *ctx,
	enum controller_id id);

void dce110_timing_generator_destroy(struct timing_generator **tg);

bool dce110_timing_generator_construct(
	struct timing_generator *tg,
	enum controller_id id);

void dce110_timing_generator_program_blank_color(
	struct timing_generator *tg,
	enum color_space color_space);

bool dce110_timing_generator_blank_crtc(struct timing_generator *tg);

bool dce110_timing_generator_enable_crtc(struct timing_generator *tg);

bool dce110_timing_generator_disable_crtc(struct timing_generator *tg);

bool dce110_timing_generator_is_in_vertical_blank(struct timing_generator *tg);

void dce110_timing_generator_program_blanking(
	struct timing_generator *tg,
	const struct dc_crtc_timing *timing);

bool dce110_timing_generator_program_timing_generator(
	struct timing_generator *tg,
	struct dc_crtc_timing *dc_crtc_timing);

void dce110_timing_generator_set_early_control(
		struct timing_generator *tg,
		uint32_t early_cntl);

bool dce110_timing_generator_unblank_crtc(struct timing_generator *tg);

bool dce110_timing_generator_validate_timing(
	struct timing_generator *tg,
	const struct dc_crtc_timing *timing,
	enum signal_type signal);

void dce110_timing_generator_wait_for_vblank(struct timing_generator *tg);

void dce110_timing_generator_wait_for_vactive(struct timing_generator *tg);

void dce110_timing_generator_set_test_pattern(
	struct timing_generator *tg,
	/* TODO: replace 'controller_dp_test_pattern' by 'test_pattern_mode'
	 * because this is not DP-specific (which is probably somewhere in DP
	 * encoder) */
	enum controller_dp_test_pattern test_pattern,
	enum dc_color_depth color_depth);

void dce110_timing_generator_program_drr(
	struct timing_generator *tg,
	const struct hw_ranged_timing *timing);

uint32_t dce110_timing_generator_get_crtc_scanoutpos(
	struct timing_generator *tg,
	int32_t *vbl,
	int32_t *position);

uint32_t dce110_timing_generator_get_vblank_counter(struct timing_generator *tg);

void dce110_timing_generator_color_space_to_black_color(
		enum color_space colorspace,
		struct crtc_black_color *black_color);
void dce110_timing_generator_apply_front_porch_workaround(
	struct timing_generator *tg,
	struct dc_crtc_timing *timing);
int32_t dce110_timing_generator_get_vsynch_and_front_porch_size(
	const struct dc_crtc_timing *timing);

void dce110_timing_generator_get_crtc_positions(
	struct timing_generator *tg,
	int32_t *h_position,
	int32_t *v_position);


/* TODO: Figure out if we need these functions*/
bool dce110_timing_generator_is_counter_moving(struct timing_generator *tg);

void dce110_timing_generator_enable_advanced_request(
	struct timing_generator *tg,
	bool enable,
	const struct dc_crtc_timing *timing);

void dce110_timing_generator_set_lock_master(struct timing_generator *tg,
		bool lock);

void dce110_timing_generator_set_overscan_color_black(
	struct timing_generator *tg,
	enum color_space black_color);


/**** Sync-related interfaces ****/
void dce110_timing_generator_setup_global_swap_lock(
	struct timing_generator *tg,
	const struct dcp_gsl_params *gsl_params);
void dce110_timing_generator_tear_down_global_swap_lock(
	struct timing_generator *tg);


void dce110_timing_generator_enable_reset_trigger(
	struct timing_generator *tg,
	const struct trigger_params *trigger_params);

void dce110_timing_generator_disable_reset_trigger(
	struct timing_generator *tg);

bool dce110_timing_generator_did_triggered_reset_occur(
	struct timing_generator *tg);

void dce110_timing_generator_disable_vga(
	struct timing_generator *tg);

/**** End-of-Sync-related interfaces ****/

#endif /* __DC_TIMING_GENERATOR_DCE110_H__ */
