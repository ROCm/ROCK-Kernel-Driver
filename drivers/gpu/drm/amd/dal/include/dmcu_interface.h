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

#ifndef __DAL_DMCU_INTERFACE_H__
#define __DAL_DMCU_INTERFACE_H__

#include "grph_object_defs.h"
#include "dmcu_types.h"

/* Interface functions */

/* DMCU setup related interface functions */
struct dmcu *dal_dmcu_create(
	struct dmcu_init_data *init_data);
void dal_dmcu_destroy(struct dmcu **dmcu);
void dal_dmcu_release_hw(struct dmcu *dmcu);

void dal_dmcu_power_up(struct dmcu *dmcu);
void dal_dmcu_power_down(struct dmcu *dmcu);

void dal_dmcu_configure_wait_loop(
		struct dmcu *dmcu,
		uint32_t display_clock);

/* PSR feature related interface functions */
void dal_dmcu_psr_setup(
		struct dmcu *dmcu,
		struct dmcu_context *dmcu_context);
void dal_dmcu_psr_enable(struct dmcu *dmcu);
void dal_dmcu_psr_disable(struct dmcu *dmcu);
void dal_dmcu_psr_block(struct dmcu *dmcu, bool block_psr);
bool dal_dmcu_psr_is_blocked(struct dmcu *dmcu);
void dal_dmcu_psr_set_level(
		struct dmcu *dmcu,
		union dmcu_psr_level psr_level);
void dal_dmcu_psr_allow_power_down_crtc(
		struct dmcu *dmcu,
		bool should_allow_crtc_power_down);
bool dal_dmcu_psr_submit_command(
		struct dmcu *dmcu,
		struct dmcu_context *dmcu_context,
		struct dmcu_config_data *config_data);
void dal_dmcu_psr_get_config_data(
		struct dmcu *dmcu,
		uint32_t v_total,
		struct dmcu_config_data *config_data);

/* ABM feature related interface functions */
void dal_dmcu_abm_enable(
		struct dmcu *dmcu,
		enum controller_id controller_id,
		uint32_t vsync_rate_hz);
void dal_dmcu_abm_disable(struct dmcu *dmcu);
bool dal_dmcu_abm_enable_smooth_brightness(struct dmcu *dmcu);
bool dal_dmcu_abm_disable_smooth_brightness(struct dmcu *dmcu);
void dal_dmcu_abm_varibright_control(
		struct dmcu *dmcu,
		const struct varibright_control *varibright_control);
bool dal_dmcu_abm_set_backlight_level(
		struct dmcu *dmcu,
		uint8_t backlight_8_bit);
uint8_t dal_dmcu_abm_get_user_backlight_level(struct dmcu *dmcu);
uint8_t dal_dmcu_abm_get_current_backlight_level(struct dmcu *dmcu);

#endif /* __DAL_DMCU_INTERFACE_H__ */
