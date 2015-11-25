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

#ifndef __DAL_HW_DVO_H__
#define __DAL_HW_DVO_H__

#define BUNDLE_A_SHIFT 12L
#define BUNDLE_B_SHIFT 0L

struct hw_dvo {
	struct hw_gpio_pin base;
	/* Register indices are represented by member variables,
	 * are to be filled in by derived classes.
	 * These members permit the use of common code
	 * for programming registers where the sequence is the same
	 * but the register sets are different */
	struct {
		uint32_t DC_GPIO_DVODATA_MASK;
		uint32_t DC_GPIO_DVODATA_EN;
		uint32_t DC_GPIO_DVODATA_A;
		uint32_t DC_GPIO_DVODATA_Y;
		uint32_t DVO_STRENGTH_CONTROL;
		uint32_t D1CRTC_MVP_CONTROL1;
	} addr;

	/* Mask and shift differentiates between Bundle A and Bundle B */
	uint32_t dvo_mask;
	uint32_t dvo_shift;
	uint32_t dvo_strength_mask;
	uint32_t mvp_termination_mask;

	uint32_t dvo_strength;

	struct {
		uint32_t dvo_mask;
		uint32_t dvo_en;
		uint32_t dvo_data_a;
		bool mvp_terminator_state;
	} store;
};

bool dal_hw_dvo_construct(
	struct hw_dvo *pin,
	enum gpio_id id,
	uint32_t en,
	struct dc_context *ctx);

void dal_hw_dvo_destruct(
	struct hw_dvo *pin);

bool dal_hw_dvo_open(
	struct hw_gpio_pin *ptr,
	enum gpio_mode mode,
	void *options);

enum gpio_result dal_hw_dvo_get_value(
	const struct hw_gpio_pin *ptr,
	uint32_t *value);

enum gpio_result dal_hw_dvo_set_value(
	const struct hw_gpio_pin *ptr,
	uint32_t value);

void dal_hw_dvo_close(
	struct hw_gpio_pin *ptr);

#endif
