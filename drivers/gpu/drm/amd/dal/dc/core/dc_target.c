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
#include "dc_services.h"
#include "core_types.h"
#include "hw_sequencer.h"
#include "resource.h"

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
		dc_service_free(protected->ctx, target);
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

	target = dc_service_alloc(stream->ctx, sizeof(struct target));

	if (NULL == target)
		goto target_alloc_fail;

	construct(&target->protected, stream->ctx, dc_streams, stream_count);

	dc_target_retain(&target->protected.public);

	return &target->protected.public;


target_alloc_fail:
	return NULL;
}

static void build_gamma_params(
		enum pixel_format pixel_format,
		struct gamma_parameters *gamma_param)
{
	uint32_t i;

	/* translate parameters */
	gamma_param->surface_pixel_format = pixel_format;

	gamma_param->regamma_adjust_type = GRAPHICS_REGAMMA_ADJUST_SW;
	gamma_param->degamma_adjust_type = GRAPHICS_REGAMMA_ADJUST_SW;

	gamma_param->selected_gamma_lut = GRAPHICS_GAMMA_LUT_REGAMMA;

	/* TODO support non-legacy gamma */
	gamma_param->disable_adjustments = false;
	gamma_param->flag.bits.config_is_changed = 0;
	gamma_param->flag.bits.regamma_update = 1;
	gamma_param->flag.bits.gamma_update = 1;

	/* Set regamma */
	gamma_param->regamma.features.bits.GRAPHICS_DEGAMMA_SRGB = 1;
	gamma_param->regamma.features.bits.OVERLAY_DEGAMMA_SRGB = 1;
	gamma_param->regamma.features.bits.GAMMA_RAMP_ARRAY = 0;
	gamma_param->regamma.features.bits.APPLY_DEGAMMA = 0;

	for (i = 0; i < COEFF_RANGE; i++) {
		gamma_param->regamma.gamma_coeff.a0[i] = REGAMMA_COEFF_A0;
		gamma_param->regamma.gamma_coeff.a1[i] = REGAMMA_COEFF_A1;
		gamma_param->regamma.gamma_coeff.a2[i] = REGAMMA_COEFF_A2;
		gamma_param->regamma.gamma_coeff.a3[i] = REGAMMA_COEFF_A3;
		gamma_param->regamma.gamma_coeff.gamma[i] = REGAMMA_COEFF_GAMMA;
	}
}


static bool program_gamma(
		struct dc_context *ctx,
		struct dc_surface *surface,
		struct input_pixel_processor *ipp,
		struct output_pixel_processor *opp)
{
	struct gamma_parameters *gamma_param;
	bool result= false;

	gamma_param = dc_service_alloc(ctx, sizeof(struct gamma_parameters));

	if (!gamma_param)
		goto gamma_param_fail;

	build_gamma_params(surface->format, gamma_param);

	result = ctx->dc->hwss.set_gamma_ramp(ipp, opp,
			&surface->gamma_correction,
			gamma_param);

	dc_service_free(ctx, gamma_param);

gamma_param_fail:
	return result;
}

bool dc_commit_surfaces_to_target(
		struct dc *dc,
		struct dc_surface *new_surfaces[],
		uint8_t new_surface_count,
		struct dc_target *dc_target)

{
	uint8_t i, j;
	uint32_t prev_disp_clk = dc->current_context.bw_results.dispclk_khz;
	struct core_target *target = DC_TARGET_TO_CORE(dc_target);

	bool current_enabled_surface_count = 0;
	bool new_enabled_surface_count = 0;

	for (i = 0; i < target->status.surface_count; i++)
		if (target->status.surfaces[i]->visible)
			current_enabled_surface_count++;

	for (i = 0; i < new_surface_count; i++)
		if (new_surfaces[i]->visible)
			new_enabled_surface_count++;

	dal_logger_write(dc->ctx->logger,
				LOG_MAJOR_INTERFACE_TRACE,
				LOG_MINOR_COMPONENT_DC,
				"%s: commit %d surfaces to target 0x%x",
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

	if (dc->hwss.validate_bandwidth(dc, &dc->current_context) != DC_OK) {
		BREAK_TO_DEBUGGER();
		goto unexpected_fail;
	}

	if (prev_disp_clk < dc->current_context.bw_results.dispclk_khz) {
		dc->hwss.program_bw(dc, &dc->current_context);
		pplib_apply_display_requirements(dc, &dc->current_context);
	}

	if (current_enabled_surface_count > 0 && new_enabled_surface_count == 0)
		dc_target_disable_memory_requests(dc_target);

	for (i = 0; i < new_surface_count; i++) {
		struct dc_surface *surface = new_surfaces[i];
		struct core_surface *core_surface = DC_SURFACE_TO_CORE(surface);

		dal_logger_write(dc->ctx->logger,
					LOG_MAJOR_INTERFACE_TRACE,
					LOG_MINOR_COMPONENT_DC,
					"0x%x:",
					surface);

		program_gamma(dc->ctx, surface,
			DC_STREAM_TO_CORE(target->public.streams[0])->ipp,
			DC_STREAM_TO_CORE(target->public.streams[0])->opp);

		dc->hwss.set_plane_config(
			core_surface,
			target);

		dc->hwss.update_plane_address(core_surface, target);
	}

	if (current_enabled_surface_count == 0 && new_enabled_surface_count > 0)
		dc_target_enable_memory_requests(dc_target);

	/* Lower display clock if necessary */
	if (prev_disp_clk > dc->current_context.bw_results.dispclk_khz) {
		dc->hwss.program_bw(dc, &dc->current_context);
		pplib_apply_display_requirements(dc, &dc->current_context);
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

		if (!core_target->ctx->dc->hwss.enable_memory_requests(tg)) {
			dal_error("DC: failed to unblank crtc!\n");
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
			dal_error("DC: timing generator is NULL!\n");
			BREAK_TO_DEBUGGER();
			continue;
		}

		if (false == core_target->ctx->dc->hwss.disable_memory_requests(tg)) {
			dal_error("DC: failed to blank crtc!\n");
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
		dal_error("DC: dc_target is NULL!\n");
			return false;

	}

	core_target = DC_TARGET_TO_CORE(dc_target);
	ipp = DC_STREAM_TO_CORE(core_target->public.streams[0])->ipp;

	if (NULL == ipp) {
		dal_error("DC: input pixel processor is NULL!\n");
		return false;
	}

	if (true == core_target->ctx->dc->hwss.cursor_set_attributes(ipp, attributes))
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
		dal_error("DC: dc_target is NULL!\n");
		return false;
	}

	if (NULL == position) {
		dal_error("DC: cursor position is NULL!\n");
		return false;
	}

	core_target = DC_TARGET_TO_CORE(dc_target);
	ipp = DC_STREAM_TO_CORE(core_target->public.streams[0])->ipp;

	if (NULL == ipp) {
		dal_error("DC: input pixel processor is NULL!\n");
		return false;
	}


	if (true == core_target->ctx->dc->hwss.cursor_set_position(ipp, position))
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

	return core_target->ctx->dc->hwss.get_vblank_counter(tg);
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
			"core_target 0x%x: surface_count=%d, stream_count=%d",
			core_target,
			core_target->status.surface_count,
			core_target->public.stream_count);

	for (i = 0; i < core_target->public.stream_count; i++) {
		const struct core_stream *core_stream =
			DC_STREAM_TO_CORE(core_target->public.streams[i]);

		dal_logger_write(dal_logger,
			log_major,
			log_minor,
			"core_stream 0x%x: src: %d, %d, %d, %d; dst: %d, %d, %d, %d;",
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
			"\tpix_clk_khz: %d, h_total: %d, v_total: %d",
			core_stream->public.timing.pix_clk_khz,
			core_stream->public.timing.h_total,
			core_stream->public.timing.v_total);
		dal_logger_write(dal_logger,
			log_major,
			log_minor,
			"\tsink name: %s, serial: %d",
			core_stream->sink->public.edid_caps.display_name,
			core_stream->sink->public.edid_caps.serial_number);
		dal_logger_write(dal_logger,
			log_major,
			log_minor,
			"\tconnector: %d",
			core_stream->sink->link->connector_index);
	}
}
