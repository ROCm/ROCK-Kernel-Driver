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

#ifndef __DC_HW_SEQUENCER_H__
#define __DC_HW_SEQUENCER_H__
#include "core_types.h"

struct gamma_parameters;

enum pipe_gating_control {
	PIPE_GATING_CONTROL_DISABLE = 0,
	PIPE_GATING_CONTROL_ENABLE,
	PIPE_GATING_CONTROL_INIT
};


struct hw_sequencer_funcs {

	enum dc_status (*apply_ctx_to_hw)(
					const struct dc *dc,
					struct validate_context *context);

	void (*reset_hw_ctx)(struct dc *dc,
					struct validate_context *context,
					uint8_t target_count);

	bool (*set_plane_config)(
					struct core_surface *surface,
					struct core_target *target);

	bool (*update_plane_address)(
					const struct core_surface *surface,
					struct core_target *target);

	bool (*enable_memory_requests)(struct timing_generator *tg);

	bool (*disable_memory_requests)(struct timing_generator *tg);

	bool (*transform_power_up)(struct transform *xfm);

	bool (*set_gamma_ramp)(
					struct input_pixel_processor *ipp,
					struct output_pixel_processor *opp,
					const struct gamma_ramp *ramp,
					const struct gamma_parameters *params);

	void (*power_down)(struct dc *dc);

	void (*enable_accelerated_mode)(struct dc *dc);

	void (*get_crtc_positions)(
					struct timing_generator *tg,
					int32_t *h_position,
					int32_t *v_position);

	uint32_t (*get_vblank_counter)(struct timing_generator *tg);

	void (*enable_timing_synchronization)(
					struct dc_context *dc_ctx,
					uint32_t timing_generator_num,
					struct timing_generator *tgs[]);

	void (*disable_vga)(struct timing_generator *tg);



	/* link encoder sequences */
	struct link_encoder *(*encoder_create)(
			const struct encoder_init_data *init);

	void (*encoder_destroy)(struct link_encoder **enc);

	/* backlight control */
	void (*encoder_set_lcd_backlight_level)(struct link_encoder *enc,
					uint32_t level);


	/* power management */
	void (*clock_gating_power_up)(
					struct dc_context *ctx,
					bool enable);

	void (*enable_display_pipe_clock_gating)(
					struct dc_context *ctx,
					bool clock_gating);

	bool (*enable_display_power_gating)(
					struct dc_context *ctx,
					uint8_t controller_id,
					struct dc_bios *dcb,
					enum pipe_gating_control power_gating);

	/* resource management and validation*/
	bool (*construct_resource_pool)(
					struct adapter_service *adapter_serv,
					uint8_t num_virtual_links,
					struct dc *dc,
					struct resource_pool *pool);

	void (*destruct_resource_pool)(struct resource_pool *pool);

	enum dc_status (*validate_with_context)(
					const struct dc *dc,
					const struct dc_validation_set set[],
					uint8_t set_count,
					struct validate_context *context);

	enum dc_status (*validate_bandwidth)(
					const struct dc *dc,
					struct validate_context *context);
	void (*program_bw)(
					struct dc *dc,
					struct validate_context *context);
	void (*enable_stream)(
					struct core_stream *stream);

	void (*disable_stream)(
					struct core_stream *stream);

	void (*set_mst_bandwidth)(
					struct stream_encoder *enc,
					struct fixed31_32 avg_time_slots_per_mtp);
};

bool dc_construct_hw_sequencer(
				struct adapter_service *adapter_serv,
				struct dc *dc);


#endif /* __DC_HW_SEQUENCER_H__ */
