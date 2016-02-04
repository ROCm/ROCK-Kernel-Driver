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

	for (i = 0; i < core_target->status.surface_count; i++) {
		dc_surface_release(core_target->status.surfaces[i]);
		core_target->status.surfaces[i] = NULL;
	}
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
	struct core_target* target = DC_TARGET_TO_CORE(dc_target);
	return &target->status;
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

static bool validate_surface_address(
		struct dc_plane_address address)
{
	bool is_valid_address = false;

	switch (address.type) {
	case PLN_ADDR_TYPE_GRAPHICS:
		if (address.grph.addr.quad_part != 0)
			is_valid_address = true;
		break;
	case PLN_ADDR_TYPE_GRPH_STEREO:
		if ((address.grph_stereo.left_addr.quad_part != 0) &&
			(address.grph_stereo.right_addr.quad_part != 0)) {
			is_valid_address = true;
		}
		break;
	case PLN_ADDR_TYPE_VIDEO_PROGRESSIVE:
	default:
		/* not supported */
		BREAK_TO_DEBUGGER();
		break;
	}

	return is_valid_address;
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

	int current_enabled_surface_count = 0;
	int new_enabled_surface_count = 0;

	if (!dal_adapter_service_is_in_accelerated_mode(
						dc->res_pool.adapter_srv) ||
			dc->current_context.target_count == 0) {
		return false;
	}

	for (i = 0; i < dc->current_context.target_count; i++)
		if (target == dc->current_context.targets[i])
			break;

	/* Cannot commit surface to a target that is not commited */
	if (i == dc->current_context.target_count)
		return false;

	for (i = 0; i < target->status.surface_count; i++)
		if (target->status.surfaces[i]->visible)
			current_enabled_surface_count++;

	for (i = 0; i < new_surface_count; i++)
		if (new_surfaces[i]->visible)
			new_enabled_surface_count++;

	dal_logger_write(dc->ctx->logger,
				LOG_MAJOR_INTERFACE_TRACE,
				LOG_MINOR_COMPONENT_DC,
				"%s: commit %d surfaces to target 0x%x\n",
				__func__,
				new_surface_count,
				dc_target);


	if (!logical_attach_surfaces_to_target(
						new_surfaces,
						new_surface_count,
						dc_target)) {
		BREAK_TO_DEBUGGER();
		goto unexpected_fail;
	}

	for (i = 0; i < new_surface_count; i++)
		for (j = 0; j < target->public.stream_count; j++)
			build_scaling_params(
				new_surfaces[i],
				DC_STREAM_TO_CORE(target->public.streams[j]));

	if (dc->res_pool.funcs->validate_bandwidth(dc, &dc->current_context)
								!= DC_OK) {
		BREAK_TO_DEBUGGER();
		goto unexpected_fail;
	}

	if (prev_disp_clk < dc->current_context.bw_results.dispclk_khz) {
		dc->hwss.program_bw(dc, &dc->current_context);
		pplib_apply_display_requirements(dc, &dc->current_context,
				&dc->current_context.pp_display_cfg);
	}

	if (current_enabled_surface_count > 0 && new_enabled_surface_count == 0)
		dc_target_disable_memory_requests(dc_target);

	for (i = 0; i < new_surface_count; i++) {
		struct dc_surface *surface = new_surfaces[i];
		struct core_surface *core_surface = DC_SURFACE_TO_CORE(surface);
		struct core_stream *stream =
				DC_STREAM_TO_CORE(target->public.streams[0]);
		bool is_valid_address =
				validate_surface_address(surface->address);


		dal_logger_write(dc->ctx->logger,
					LOG_MAJOR_INTERFACE_TRACE,
					LOG_MINOR_COMPONENT_DC,
					"0x%x:",
					surface);

		if (surface->gamma_correction) {
			struct core_gamma *gamma = DC_GAMMA_TO_CORE(
						surface->gamma_correction);

			dc->hwss.set_gamma_correction(
					stream->ipp,
					stream->opp,
					gamma, core_surface);
		}

		dc->hwss.set_plane_config(dc, core_surface, target);

		if (is_valid_address)
			dc->hwss.update_plane_address(dc, core_surface, target);
	}

	if (current_enabled_surface_count == 0 && new_enabled_surface_count > 0)
		dc_target_enable_memory_requests(dc_target);

	/* Lower display clock if necessary */
	if (prev_disp_clk > dc->current_context.bw_results.dispclk_khz) {
		dc->hwss.program_bw(dc, &dc->current_context);
		pplib_apply_display_requirements(dc, &dc->current_context,
				&dc->current_context.pp_display_cfg);
	}

	return true;

unexpected_fail:
	for (i = 0; i < new_surface_count; i++) {
		target->status.surfaces[i] = NULL;
	}
	target->status.surface_count = 0;

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

void dc_target_enable_memory_requests(struct dc_target *target)
{
	uint8_t i;
	struct core_target *core_target = DC_TARGET_TO_CORE(target);
	for (i = 0; i < core_target->public.stream_count; i++) {
		struct timing_generator *tg =
			DC_STREAM_TO_CORE(core_target->public.streams[i])->tg;

		if (!tg->funcs->set_blank(tg, false)) {
			dm_error("DC: failed to unblank crtc!\n");
			BREAK_TO_DEBUGGER();
		}
	}
}

void dc_target_disable_memory_requests(struct dc_target *target)
{
	uint8_t i;
	struct core_target *core_target = DC_TARGET_TO_CORE(target);
	for (i = 0; i < core_target->public.stream_count; i++) {
	struct timing_generator *tg =
		DC_STREAM_TO_CORE(core_target->public.streams[i])->tg;

		if (NULL == tg) {
			dm_error("DC: timing generator is NULL!\n");
			BREAK_TO_DEBUGGER();
			continue;
		}

		if (false == tg->funcs->set_blank(tg, true)) {
			dm_error("DC: failed to blank crtc!\n");
			BREAK_TO_DEBUGGER();
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
	struct core_target *core_target;
	struct input_pixel_processor *ipp;

	if (NULL == dc_target) {
		dm_error("DC: dc_target is NULL!\n");
			return false;

	}

	core_target = DC_TARGET_TO_CORE(dc_target);
	ipp = DC_STREAM_TO_CORE(core_target->public.streams[0])->ipp;

	if (NULL == ipp) {
		dm_error("DC: input pixel processor is NULL!\n");
		return false;
	}

	if (true == ipp->funcs->ipp_cursor_set_attributes(ipp, attributes))
		return true;

	return false;
}

bool dc_target_set_cursor_position(
	struct dc_target *dc_target,
	const struct dc_cursor_position *position)
{
	struct core_target *core_target;
	struct input_pixel_processor *ipp;

	if (NULL == dc_target) {
		dm_error("DC: dc_target is NULL!\n");
		return false;
	}

	if (NULL == position) {
		dm_error("DC: cursor position is NULL!\n");
		return false;
	}

	core_target = DC_TARGET_TO_CORE(dc_target);
	ipp = DC_STREAM_TO_CORE(core_target->public.streams[0])->ipp;

	if (NULL == ipp) {
		dm_error("DC: input pixel processor is NULL!\n");
		return false;
	}


	if (true == ipp->funcs->ipp_cursor_set_position(ipp, position))
		return true;

	return false;
}

/* TODO: #flip temporary to make flip work */
uint8_t dc_target_get_link_index(const struct dc_target *dc_target)
{
	const struct core_target *target = CONST_DC_TARGET_TO_CORE(dc_target);
	const struct core_sink *sink =
		DC_SINK_TO_CORE(target->public.streams[0]->sink);

	return sink->link->public.link_index;
}

uint32_t dc_target_get_vblank_counter(const struct dc_target *dc_target)
{
	struct core_target *core_target = DC_TARGET_TO_CORE(dc_target);
	struct timing_generator *tg =
		DC_STREAM_TO_CORE(core_target->public.streams[0])->tg;

	return tg->funcs->get_frame_count(tg);
}

enum dc_irq_source dc_target_get_irq_src(
	const struct dc_target *dc_target, const enum irq_type irq_type)
{
	struct core_target *core_target = DC_TARGET_TO_CORE(dc_target);

	/* #TODO - Remove the assumption that the controller is always in the
	 * first stream of a core target */
	struct core_stream *stream =
		DC_STREAM_TO_CORE(core_target->public.streams[0]);
	uint8_t controller_idx = stream->controller_idx;

	/* Get controller id */
	enum controller_id crtc_id = controller_idx + 1;

	/* Calculate controller offset */
	unsigned int offset = crtc_id - CONTROLLER_ID_D0;
	unsigned int base = irq_type;

	/* Calculate irq source */
	enum dc_irq_source src = base + offset;

	return src;
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
			"core_target 0x%x: surface_count=%d, stream_count=%d\n",
			core_target,
			core_target->status.surface_count,
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
