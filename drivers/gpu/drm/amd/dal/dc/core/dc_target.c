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

#include "dm_services.h"
#include "core_types.h"
#include "hw_sequencer.h"
#include "resource.h"
#include "ipp.h"
#include "timing_generator.h"

#define COEFF_RANGE	3
#define REGAMMA_COEFF_A0	31308
#define REGAMMA_COEFF_A1	12920
#define REGAMMA_COEFF_A2	55
#define REGAMMA_COEFF_A3	55
#define REGAMMA_COEFF_GAMMA	2400

struct target {
	struct core_target protected;
	int ref_count;
};

#define DC_TARGET_TO_TARGET(dc_target) \
	container_of(dc_target, struct target, protected.public)
#define CORE_TARGET_TO_TARGET(core_target) \
	container_of(core_target, struct target, protected)

static void construct(
	struct core_target *target,
	struct dc_context *ctx,
	struct dc_stream *dc_streams[],
	uint8_t stream_count)
{
	uint8_t i;
	for (i = 0; i < stream_count; i++) {
		target->public.streams[i] = dc_streams[i];
		dc_stream_retain(dc_streams[i]);
	}

	target->ctx = ctx;
	target->public.stream_count = stream_count;
}

static void destruct(struct core_target *core_target)
{
	int i;

	for (i = 0; i < core_target->public.stream_count; i++) {
		dc_stream_release(
			(struct dc_stream *)core_target->public.streams[i]);
		core_target->public.streams[i] = NULL;
	}
}

void dc_target_retain(struct dc_target *dc_target)
{
	struct target *target = DC_TARGET_TO_TARGET(dc_target);

	target->ref_count++;
}

void dc_target_release(struct dc_target *dc_target)
{
	struct target *target = DC_TARGET_TO_TARGET(dc_target);
	struct core_target *protected = DC_TARGET_TO_CORE(dc_target);

	ASSERT(target->ref_count > 0);
	target->ref_count--;
	if (target->ref_count == 0) {
		destruct(protected);
		dm_free(protected->ctx, target);
	}
}

const struct dc_target_status *dc_target_get_status(
					const struct dc_target* dc_target)
{
	uint8_t i;
	struct core_target* target = DC_TARGET_TO_CORE(dc_target);
	struct dc *dc = target->ctx->dc;

	for (i = 0; i < dc->current_context.target_count; i++)
		if (target == dc->current_context.targets[i])
			return &dc->current_context.target_status[i];

	return NULL;
}

struct dc_target *dc_create_target_for_streams(
		struct dc_stream *dc_streams[],
		uint8_t stream_count)
{
	struct core_stream *stream;
	struct target *target;

	if (0 == stream_count)
		goto target_alloc_fail;

	stream = DC_STREAM_TO_CORE(dc_streams[0]);

	target = dm_alloc(stream->ctx, sizeof(struct target));

	if (NULL == target)
		goto target_alloc_fail;

	construct(&target->protected, stream->ctx, dc_streams, stream_count);

	dc_target_retain(&target->protected.public);

	return &target->protected.public;


target_alloc_fail:
	return NULL;
}

/*
void ProgramPixelDurationV(unsigned int pixelClockInKHz )
{
	fixed31_32 pixel_duration = Fixed31_32(100000000, pixelClockInKHz) * 10;
	unsigned int pixDurationInPico = round(pixel_duration);

	DPG_PIPE_ARBITRATION_CONTROL1 arb_control;

	arb_control.u32All = ReadReg (mmDPGV0_PIPE_ARBITRATION_CONTROL1);
	arb_control.bits.PIXEL_DURATION = pixDurationInPico;
	WriteReg (mmDPGV0_PIPE_ARBITRATION_CONTROL1, arb_control.u32All);

	arb_control.u32All = ReadReg (mmDPGV1_PIPE_ARBITRATION_CONTROL1);
	arb_control.bits.PIXEL_DURATION = pixDurationInPico;
	WriteReg (mmDPGV1_PIPE_ARBITRATION_CONTROL1, arb_control.u32All);

	WriteReg (mmDPGV0_PIPE_ARBITRATION_CONTROL2, 0x4000800);
	WriteReg (mmDPGV0_REPEATER_PROGRAM, 0x11);

	WriteReg (mmDPGV1_PIPE_ARBITRATION_CONTROL2, 0x4000800);
	WriteReg (mmDPGV1_REPEATER_PROGRAM, 0x11);
}
*/
static int8_t acquire_first_free_underlay(
		struct resource_context *res_ctx,
		struct core_stream *stream)
{
	if (!res_ctx->pipe_ctx[DCE110_UNDERLAY_IDX].stream) {
		struct dc_bios *dcb;
		struct pipe_ctx *pipe_ctx = &res_ctx->pipe_ctx[DCE110_UNDERLAY_IDX];

		pipe_ctx->tg = res_ctx->pool.timing_generators[DCE110_UNDERLAY_IDX];
		pipe_ctx->mi = res_ctx->pool.mis[DCE110_UNDERLAY_IDX];
		/*pipe_ctx->ipp = res_ctx->pool.ipps[DCE110_UNDERLAY_IDX];*/
		pipe_ctx->xfm = res_ctx->pool.transforms[DCE110_UNDERLAY_IDX];
		pipe_ctx->opp = res_ctx->pool.opps[DCE110_UNDERLAY_IDX];
		pipe_ctx->dis_clk = res_ctx->pool.display_clock;
		pipe_ctx->pipe_idx = DCE110_UNDERLAY_IDX;

		dcb = dal_adapter_service_get_bios_parser(
						res_ctx->pool.adapter_srv);

		stream->ctx->dc->hwss.enable_display_power_gating(
			stream->ctx->dc->ctx,
			DCE110_UNDERLAY_IDX,
			dcb, PIPE_GATING_CONTROL_DISABLE);

		if (!pipe_ctx->tg->funcs->set_blank(pipe_ctx->tg, true)) {
			dm_error("DC: failed to blank crtc!\n");
			BREAK_TO_DEBUGGER();
		}

		if (!pipe_ctx->tg->funcs->enable_crtc(pipe_ctx->tg)) {
			BREAK_TO_DEBUGGER();
		}

		pipe_ctx->tg->funcs->set_blank_color(
				pipe_ctx->tg,
				COLOR_SPACE_YCBCR601);/* TODO unhardcode*/

		pipe_ctx->stream = stream;
		return DCE110_UNDERLAY_IDX;
	}
	return -1;
}

bool dc_commit_surfaces_to_target(
		struct dc *dc,
		struct dc_surface *new_surfaces[],
		uint8_t new_surface_count,
		struct dc_target *dc_target)

{
	int i, j;
	uint32_t prev_disp_clk = dc->current_context.bw_results.dispclk_khz;
	struct core_target *target = DC_TARGET_TO_CORE(dc_target);
	struct dc_target_status *target_status = NULL;
	struct validate_context *context;
	int current_enabled_surface_count = 0;
	int new_enabled_surface_count = 0;
	bool is_mpo_turning_on = false;

	context = dm_alloc(dc->ctx, sizeof(struct validate_context));

	val_ctx_copy_construct(&dc->current_context, context);

	/* Cannot commit surface to a target that is not commited */
	for (i = 0; i < context->target_count; i++)
		if (target == context->targets[i])
			break;

	target_status = &context->target_status[i];

	if (!dal_adapter_service_is_in_accelerated_mode(
						dc->res_pool.adapter_srv)
		|| i == context->target_count) {
		BREAK_TO_DEBUGGER();
		goto unexpected_fail;
	}

	for (i = 0; i < target_status->surface_count; i++)
		if (target_status->surfaces[i]->visible)
			current_enabled_surface_count++;

	for (i = 0; i < new_surface_count; i++)
		if (new_surfaces[i]->visible)
			new_enabled_surface_count++;

	/* TODO unhack mpo */
	if (new_surface_count == 2 && target_status->surface_count < 2) {
		acquire_first_free_underlay(&context->res_ctx,
				DC_STREAM_TO_CORE(dc_target->streams[0]));
		is_mpo_turning_on = true;
	} else if (new_surface_count < 2 && target_status->surface_count == 2) {
		context->res_ctx.pipe_ctx[DCE110_UNDERLAY_IDX].stream = NULL;
		context->res_ctx.pipe_ctx[DCE110_UNDERLAY_IDX].surface = NULL;
	}

	dal_logger_write(dc->ctx->logger,
				LOG_MAJOR_INTERFACE_TRACE,
				LOG_MINOR_COMPONENT_DC,
				"%s: commit %d surfaces to target 0x%x\n",
				__func__,
				new_surface_count,
				dc_target);


	if (!attach_surfaces_to_context(
			new_surfaces, new_surface_count, dc_target, context)) {
		BREAK_TO_DEBUGGER();
		goto unexpected_fail;
	}

	for (i = 0; i < new_surface_count; i++)
		for (j = 0; j < MAX_PIPES; j++) {
			if (context->res_ctx.pipe_ctx[j].surface !=
					DC_SURFACE_TO_CORE(new_surfaces[i]))
				continue;

			build_scaling_params(
				new_surfaces[i], &context->res_ctx.pipe_ctx[j]);
		}

	if (dc->res_pool.funcs->validate_bandwidth(dc, context) != DC_OK) {
		BREAK_TO_DEBUGGER();
		goto unexpected_fail;
	}

	if (prev_disp_clk < context->bw_results.dispclk_khz ||
		(is_mpo_turning_on &&
			prev_disp_clk == context->bw_results.dispclk_khz)) {
		dc->hwss.program_bw(dc, context);
		pplib_apply_display_requirements(dc, context,
						&context->pp_display_cfg);
	}

	if (current_enabled_surface_count > 0 && new_enabled_surface_count == 0)
		dc_target_disable_memory_requests(dc_target);

	for (i = 0; i < new_surface_count; i++)
		for (j = 0; j < MAX_PIPES; j++) {
			struct dc_surface *dc_surface = new_surfaces[i];
			struct core_surface *surface =
						DC_SURFACE_TO_CORE(dc_surface);
			struct pipe_ctx *pipe_ctx =
						&context->res_ctx.pipe_ctx[j];
			struct core_gamma *gamma = NULL;

			if (pipe_ctx->surface !=
					DC_SURFACE_TO_CORE(new_surfaces[i]))
				continue;

			dal_logger_write(dc->ctx->logger,
						LOG_MAJOR_INTERFACE_TRACE,
						LOG_MINOR_COMPONENT_DC,
						"Pipe:%d 0x%x: src: %d, %d, %d,"
						" %d; dst: %d, %d, %d, %d;\n",
						pipe_ctx->pipe_idx,
						dc_surface,
						dc_surface->src_rect.x,
						dc_surface->src_rect.y,
						dc_surface->src_rect.width,
						dc_surface->src_rect.height,
						dc_surface->dst_rect.x,
						dc_surface->dst_rect.y,
						dc_surface->dst_rect.width,
						dc_surface->dst_rect.height);

			if (surface->public.gamma_correction)
				gamma = DC_GAMMA_TO_CORE(
					surface->public.gamma_correction);

			dc->hwss.set_gamma_correction(
					pipe_ctx->ipp,
					pipe_ctx->opp,
					gamma, surface);

			dc->hwss.set_plane_config(
				dc, pipe_ctx, &context->res_ctx);
		}

	dc->hwss.update_plane_addrs(dc, &context->res_ctx);

	/* Lower display clock if necessary */
	if (prev_disp_clk > context->bw_results.dispclk_khz) {
		dc->hwss.program_bw(dc, context);
		pplib_apply_display_requirements(dc, context,
						&context->pp_display_cfg);
	}

	val_ctx_destruct(&dc->current_context);
	dc->current_context = *context;
	dm_free(dc->ctx, context);
	return true;

unexpected_fail:

	val_ctx_destruct(context);

	dm_free(dc->ctx, context);
	return false;
}

bool dc_target_is_connected_to_sink(
		const struct dc_target * dc_target,
		const struct dc_sink *dc_sink)
{
	struct core_target *target = DC_TARGET_TO_CORE(dc_target);
	uint8_t i;
	for (i = 0; i < target->public.stream_count; i++) {
		if (target->public.streams[i]->sink == dc_sink)
			return true;
	}
	return false;
}

void dc_target_enable_memory_requests(struct dc_target *dc_target)
{
	uint8_t i, j;
	struct core_target *target = DC_TARGET_TO_CORE(dc_target);
	struct resource_context *res_ctx =
		&target->ctx->dc->current_context.res_ctx;

	for (i = 0; i < target->public.stream_count; i++) {
		for (j = 0; j < MAX_PIPES; j++) {
			struct timing_generator *tg = res_ctx->pipe_ctx[j].tg;

			if (res_ctx->pipe_ctx[j].stream !=
				DC_STREAM_TO_CORE(target->public.streams[i]))
				continue;

			if (!tg->funcs->set_blank(tg, false)) {
				dm_error("DC: failed to unblank crtc!\n");
				BREAK_TO_DEBUGGER();
			}
		}
	}
}

void dc_target_disable_memory_requests(struct dc_target *dc_target)
{
	uint8_t i, j;
	struct core_target *target = DC_TARGET_TO_CORE(dc_target);
	struct resource_context *res_ctx =
		&target->ctx->dc->current_context.res_ctx;

	for (i = 0; i < target->public.stream_count; i++) {
		for (j = 0; j < MAX_PIPES; j++) {
			struct timing_generator *tg = res_ctx->pipe_ctx[j].tg;

			if (res_ctx->pipe_ctx[j].stream !=
				DC_STREAM_TO_CORE(target->public.streams[i]))
				continue;

			if (!tg->funcs->set_blank(tg, true)) {
				dm_error("DC: failed to blank crtc!\n");
				BREAK_TO_DEBUGGER();
			}
		}
	}
}

/**
 * Update the cursor attributes and set cursor surface address
 */
bool dc_target_set_cursor_attributes(
	struct dc_target *dc_target,
	const struct dc_cursor_attributes *attributes)
{
	uint8_t i, j;
	struct core_target *target;
	struct resource_context *res_ctx;

	if (NULL == dc_target) {
		dm_error("DC: dc_target is NULL!\n");
			return false;

	}
	if (NULL == attributes) {
		dm_error("DC: attributes is NULL!\n");
			return false;

	}

	target = DC_TARGET_TO_CORE(dc_target);
	res_ctx = &target->ctx->dc->current_context.res_ctx;

	for (i = 0; i < target->public.stream_count; i++) {
		for (j = 0; j < MAX_PIPES; j++) {
			struct input_pixel_processor *ipp =
						res_ctx->pipe_ctx[j].ipp;

			if (j == DCE110_UNDERLAY_IDX)
				continue;

			if (res_ctx->pipe_ctx[j].stream !=
				DC_STREAM_TO_CORE(target->public.streams[i]))
				continue;

			/* As of writing of this code cursor is on the top
			 * plane so we only need to set it on first pipe we
			 * find. May need to make this code dce specific later.
			 */
			if (ipp->funcs->ipp_cursor_set_attributes(
							ipp, attributes))
				return true;
		}
	}

	return false;
}

bool dc_target_set_cursor_position(
	struct dc_target *dc_target,
	const struct dc_cursor_position *position)
{
	uint8_t i, j;
	struct core_target *target;
	struct resource_context *res_ctx;

	if (NULL == dc_target) {
		dm_error("DC: dc_target is NULL!\n");
		return false;
	}

	if (NULL == position) {
		dm_error("DC: cursor position is NULL!\n");
		return false;
	}

	target = DC_TARGET_TO_CORE(dc_target);
	res_ctx = &target->ctx->dc->current_context.res_ctx;

	for (i = 0; i < target->public.stream_count; i++) {
		for (j = 0; j < MAX_PIPES; j++) {
			struct input_pixel_processor *ipp =
						res_ctx->pipe_ctx[j].ipp;

			if (j == DCE110_UNDERLAY_IDX)
				continue;

			if (res_ctx->pipe_ctx[j].stream !=
				DC_STREAM_TO_CORE(target->public.streams[i]))
				continue;

			/* As of writing of this code cursor is on the top
			 * plane so we only need to set it on first pipe we
			 * find. May need to make this code dce specific later.
			 */
			ipp->funcs->ipp_cursor_set_position(ipp, position);
			return true;
		}
	}

	return false;
}

uint32_t dc_target_get_vblank_counter(const struct dc_target *dc_target)
{
	uint8_t i, j;
	struct core_target *target = DC_TARGET_TO_CORE(dc_target);
	struct resource_context *res_ctx =
		&target->ctx->dc->current_context.res_ctx;

	for (i = 0; i < target->public.stream_count; i++) {
		for (j = 0; j < MAX_PIPES; j++) {
			struct timing_generator *tg = res_ctx->pipe_ctx[j].tg;

			if (res_ctx->pipe_ctx[j].stream !=
				DC_STREAM_TO_CORE(target->public.streams[i]))
				continue;

			return tg->funcs->get_frame_count(tg);
		}
	}

	return 0;
}

enum dc_irq_source dc_target_get_irq_src(
	const struct dc *dc,
	const struct dc_target *dc_target,
	const enum irq_type irq_type)
{
	uint8_t i;
	struct core_target *core_target = DC_TARGET_TO_CORE(dc_target);
	struct core_stream *stream =
			DC_STREAM_TO_CORE(core_target->public.streams[0]);

	for (i = 0; i < MAX_PIPES; i++)
		if (dc->current_context.res_ctx.pipe_ctx[i].stream == stream)
			return irq_type + i;

	return irq_type;
}

void dc_target_log(
	const struct dc_target *dc_target,
	struct dal_logger *dal_logger,
	enum log_major log_major,
	enum log_minor log_minor)
{
	int i;

	const struct core_target *core_target =
			CONST_DC_TARGET_TO_CORE(dc_target);

	dal_logger_write(dal_logger,
			log_major,
			log_minor,
			"core_target 0x%x: stream_count=%d\n",
			core_target,
			core_target->public.stream_count);

	for (i = 0; i < core_target->public.stream_count; i++) {
		const struct core_stream *core_stream =
			DC_STREAM_TO_CORE(core_target->public.streams[i]);

		dal_logger_write(dal_logger,
			log_major,
			log_minor,
			"core_stream 0x%x: src: %d, %d, %d, %d; dst: %d, %d, %d, %d;\n",
			core_stream,
			core_stream->public.src.x,
			core_stream->public.src.y,
			core_stream->public.src.width,
			core_stream->public.src.height,
			core_stream->public.dst.x,
			core_stream->public.dst.y,
			core_stream->public.dst.width,
			core_stream->public.dst.height);
		dal_logger_write(dal_logger,
			log_major,
			log_minor,
			"\tpix_clk_khz: %d, h_total: %d, v_total: %d\n",
			core_stream->public.timing.pix_clk_khz,
			core_stream->public.timing.h_total,
			core_stream->public.timing.v_total);
		dal_logger_write(dal_logger,
			log_major,
			log_minor,
			"\tsink name: %s, serial: %d\n",
			core_stream->sink->public.edid_caps.display_name,
			core_stream->sink->public.edid_caps.serial_number);
		dal_logger_write(dal_logger,
			log_major,
			log_minor,
			"\tlink: %d\n",
			core_stream->sink->link->public.link_index);
	}
}
