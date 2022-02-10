/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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

#ifndef __DC_LINK_HWSS_H__
#define __DC_LINK_HWSS_H__

struct gpio *get_hpd_gpio(struct dc_bios *dcb,
		struct graphics_object_id link_id,
		struct gpio_service *gpio_service);

void dp_enable_link_phy(
	struct dc_link *link,
	const struct link_resource *link_res,
	enum signal_type signal,
	enum clock_source_id clock_source,
	const struct dc_link_settings *link_settings);

void dp_receiver_power_ctrl(struct dc_link *link, bool on);
void dp_source_sequence_trace(struct dc_link *link, uint8_t dp_test_mode);
void edp_add_delay_for_T9(struct dc_link *link);
bool edp_receiver_ready_T9(struct dc_link *link);
bool edp_receiver_ready_T7(struct dc_link *link);

void dp_disable_link_phy(struct dc_link *link, const struct link_resource *link_res,
		enum signal_type signal);

void dp_disable_link_phy_mst(struct dc_link *link, const struct link_resource *link_res,
		enum signal_type signal);

bool dp_set_hw_training_pattern(
	struct dc_link *link,
	const struct link_resource *link_res,
	enum dc_dp_training_pattern pattern,
	uint32_t offset);

void dp_set_hw_lane_settings(
	struct dc_link *link,
	const struct link_resource *link_res,
	const struct link_training_settings *link_settings,
	uint32_t offset);

void dp_set_hw_test_pattern(
	struct dc_link *link,
	const struct link_resource *link_res,
	enum dp_test_pattern test_pattern,
	uint8_t *custom_pattern,
	uint32_t custom_pattern_size);

void dp_retrain_link_dp_test(struct dc_link *link,
		struct dc_link_settings *link_setting,
		bool skip_video_pattern);

struct dc_link;
struct link_resource;
struct fixed31_32;
struct pipe_ctx;

struct link_hwss {
	/* you must define a dummy implementation and assign the function to
	 * dummy_link_hwss if you don't want to check for NULL pointer
	 */
	void (*set_throttled_vcp_size)(struct pipe_ctx *pipe_ctx,
			struct fixed31_32 throttled_vcp_size);

	/* function pointers below this point require check for NULL
	 * *********************************************************************
	 */
	void (*set_hblank_min_symbol_width)(struct pipe_ctx *pipe_ctx,
			const struct dc_link_settings *link_settings,
			struct fixed31_32 throttled_vcp_size);
};

const struct link_hwss *dc_link_hwss_get(const struct dc_link *link, const struct link_resource *link_res);

#endif /* __DC_LINK_HWSS_H__ */
