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

#include "link_encoder.h"
#include "stream_encoder.h"

#include "resource.h"
#include "dce_base/dce_base_resource.h"
#include "include/irq_service_interface.h"
#include "include/timing_generator_interface.h"

#include "dce110/dce110_timing_generator.h"
#include "dce110/dce110_link_encoder.h"
#include "dce110/dce110_mem_input.h"
#include "dce110/dce110_ipp.h"
#include "dce110/dce110_transform.h"
#include "dce110/dce110_stream_encoder.h"
#include "dce110/dce110_opp.h"

#include "dce/dce_11_0_d.h"

enum dce110_clk_src_array_id {
	DCE110_CLK_SRC_PLL0 = 0,
	DCE110_CLK_SRC_PLL1,
	DCE110_CLK_SRC_EXT,

	DCE110_CLK_SRC_TOTAL
};

static const struct dce110_timing_generator_offsets dce110_tg_offsets[] = {
	{
		.crtc = (mmCRTC0_CRTC_CONTROL - mmCRTC_CONTROL),
		.dcp =  (mmDCP0_GRPH_CONTROL - mmGRPH_CONTROL),
	},
	{
		.crtc = (mmCRTC1_CRTC_CONTROL - mmCRTC_CONTROL),
		.dcp = (mmDCP1_GRPH_CONTROL - mmGRPH_CONTROL),
	},
	{
		.crtc = (mmCRTC2_CRTC_CONTROL - mmCRTC_CONTROL),
		.dcp = (mmDCP2_GRPH_CONTROL - mmGRPH_CONTROL),
	}
};

static const struct dce110_stream_enc_offsets dce110_str_enc_offsets[] = {
	{
		.dig = (mmDIG0_DIG_FE_CNTL - mmDIG_FE_CNTL),
		.dp  = (mmDP0_DP_SEC_CNTL - mmDP_SEC_CNTL)
	},
	{
		.dig = (mmDIG1_DIG_FE_CNTL - mmDIG_FE_CNTL),
		.dp  = (mmDP1_DP_SEC_CNTL - mmDP_SEC_CNTL)
	},
	{
		.dig = (mmDIG2_DIG_FE_CNTL - mmDIG_FE_CNTL),
		.dp  = (mmDP2_DP_SEC_CNTL - mmDP_SEC_CNTL)
	}
};


static struct timing_generator *dce110_timing_generator_create(
		struct adapter_service *as,
		struct dc_context *ctx,
		uint32_t instance,
		const struct dce110_timing_generator_offsets *offsets)
{
	struct dce110_timing_generator *tg110 =
		dc_service_alloc(ctx, sizeof(struct dce110_timing_generator));

	if (!tg110)
		return NULL;

	if (dce110_timing_generator_construct(tg110, as, ctx, instance, offsets))
		return &tg110->base;

	BREAK_TO_DEBUGGER();
	dc_service_free(ctx, tg110);
	return NULL;
}

static struct stream_encoder *dce110_stream_encoder_create(
	enum engine_id eng_id,
	struct dc_context *ctx,
	struct bios_parser *bp,
	const struct dce110_stream_enc_offsets *offsets)
{
	struct dce110_stream_encoder *enc110 =
		dc_service_alloc(ctx, sizeof(struct dce110_stream_encoder));

	if (!enc110)
		return NULL;

	if (dce110_stream_encoder_construct(enc110, ctx, bp, eng_id, offsets))
		return &enc110->base;

	BREAK_TO_DEBUGGER();
	dc_service_free(ctx, enc110);
	return NULL;
}

bool dce110_construct_resource_pool(
	struct adapter_service *adapter_serv,
	struct dc *dc,
	struct resource_pool *pool)
{
	unsigned int i;
	struct clock_source_init_data clk_src_init_data = { 0 };
	struct audio_init_data audio_init_data = { 0 };
	struct dc_context *ctx = dc->ctx;
	pool->adapter_srv = adapter_serv;

	pool->stream_engines.engine.ENGINE_ID_DIGA = 1;
	pool->stream_engines.engine.ENGINE_ID_DIGB = 1;
	pool->stream_engines.engine.ENGINE_ID_DIGC = 1;

	clk_src_init_data.as = adapter_serv;
	clk_src_init_data.ctx = ctx;
	clk_src_init_data.clk_src_id.enum_id = ENUM_ID_1;
	clk_src_init_data.clk_src_id.type = OBJECT_TYPE_CLOCK_SOURCE;
	pool->clk_src_count = DCE110_CLK_SRC_TOTAL;

	clk_src_init_data.clk_src_id.id = CLOCK_SOURCE_ID_PLL0;
	pool->clock_sources[DCE110_CLK_SRC_PLL0] = dal_clock_source_create(
							&clk_src_init_data);
	clk_src_init_data.clk_src_id.id = CLOCK_SOURCE_ID_PLL1;
	pool->clock_sources[DCE110_CLK_SRC_PLL1] = dal_clock_source_create(
							&clk_src_init_data);
	clk_src_init_data.clk_src_id.id = CLOCK_SOURCE_ID_EXTERNAL;
	pool->clock_sources[DCE110_CLK_SRC_EXT] = dal_clock_source_create(
							&clk_src_init_data);

	for (i = 0; i < pool->clk_src_count; i++) {
		if (pool->clock_sources[i] == NULL) {
			dal_error("DC: failed to create clock sources!\n");
			BREAK_TO_DEBUGGER();
			goto clk_src_create_fail;
		}
	}

	pool->display_clock = dal_display_clock_dce110_create(ctx, adapter_serv);
	if (pool->display_clock == NULL) {
		dal_error("DC: failed to create display clock!\n");
		BREAK_TO_DEBUGGER();
		goto disp_clk_create_fail;
	}

	{
		struct irq_service_init_data init_data;
		init_data.ctx = dc->ctx;
		pool->irqs = dal_irq_service_create(
				dal_adapter_service_get_dce_version(
					dc->res_pool.adapter_srv),
				&init_data);
		if (!pool->irqs)
			goto irqs_create_fail;

	}

	pool->controller_count =
		dal_adapter_service_get_func_controllers_num(adapter_serv);
	pool->stream_enc_count = 3;
	pool->scaler_filter = dal_scaler_filter_create(ctx);
	if (pool->scaler_filter == NULL) {
		BREAK_TO_DEBUGGER();
		dal_error("DC: failed to create filter!\n");
		goto filter_create_fail;
	}

	for (i = 0; i < pool->controller_count; i++) {
		pool->timing_generators[i] = dce110_timing_generator_create(
				adapter_serv, ctx, i, &dce110_tg_offsets[i]);
		if (pool->timing_generators[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dal_error("DC: failed to create tg!\n");
			goto controller_create_fail;
		}

		pool->mis[i] = dce110_mem_input_create(ctx, i);
		if (pool->mis[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dal_error(
				"DC: failed to create memory input!\n");
			goto controller_create_fail;
		}

		pool->ipps[i] = dce110_ipp_create(ctx, i);
		if (pool->ipps[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dal_error(
				"DC: failed to create input pixel processor!\n");
			goto controller_create_fail;
		}

		pool->transforms[i] = dce110_transform_create(ctx, i);
		if (pool->transforms[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dal_error(
				"DC: failed to create transform!\n");
			goto controller_create_fail;
		}
		dce110_transform_set_scaler_filter(
				pool->transforms[i],
				pool->scaler_filter);

		pool->opps[i] = dce110_opp_create(ctx, i);
		if (pool->opps[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dal_error(
				"DC: failed to create output pixel processor!\n");
			goto controller_create_fail;
		}
	}

	audio_init_data.as = adapter_serv;
	audio_init_data.ctx = ctx;
	pool->audio_count = 0;
	for (i = 0; i < pool->controller_count; i++) {
		struct graphics_object_id obj_id;

		obj_id = dal_adapter_service_enum_audio_object(adapter_serv, i);
		if (false == dal_graphics_object_id_is_valid(obj_id)) {
			/* no more valid audio objects */
			break;
		}

		audio_init_data.audio_stream_id = obj_id;
		pool->audios[i] = dal_audio_create(&audio_init_data);
		if (pool->audios[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dal_error("DC: failed to create DPPs!\n");
			goto audio_create_fail;
		}
		pool->audio_count++;
	}

	for (i = 0; i < pool->stream_enc_count; i++) {
		/* TODO: rework fragile code*/
		if (pool->stream_engines.u_all & 1 << i) {
			pool->stream_enc[i] = dce110_stream_encoder_create(
				i, dc->ctx,
				dal_adapter_service_get_bios_parser(
					adapter_serv),
				&dce110_str_enc_offsets[i]);
			if (pool->stream_enc[i] == NULL) {
				BREAK_TO_DEBUGGER();
				dal_error("DC: failed to create stream_encoder!\n");
				goto stream_enc_create_fail;
			}
		}
	}

	return true;

stream_enc_create_fail:
	for (i = 0; i < pool->stream_enc_count; i++) {
		if (pool->stream_enc[i] != NULL)
			dc_service_free(pool->stream_enc[i]->ctx,
				DCE110STRENC_FROM_STRENC(pool->stream_enc[i]));
	}

audio_create_fail:
	for (i = 0; i < pool->controller_count; i++) {
		if (pool->audios[i] != NULL)
			dal_audio_destroy(&pool->audios[i]);
	}

controller_create_fail:
	for (i = 0; i < pool->controller_count; i++) {
		if (pool->opps[i] != NULL)
			dce110_opp_destroy(&pool->opps[i]);

		if (pool->transforms[i] != NULL)
			dce110_transform_destroy(&pool->transforms[i]);

		if (pool->ipps[i] != NULL)
			dce110_ipp_destroy(&pool->ipps[i]);

		if (pool->mis[i] != NULL)
			dce110_mem_input_destroy(&pool->mis[i]);

		if (pool->timing_generators[i] != NULL)	{
			dc_service_free(pool->timing_generators[i]->ctx, DCE110TG_FROM_TG(pool->timing_generators[i]));
			pool->timing_generators[i] = NULL;
		}
	}

filter_create_fail:
	dal_irq_service_destroy(&pool->irqs);

irqs_create_fail:
	dal_display_clock_destroy(&pool->display_clock);

disp_clk_create_fail:
clk_src_create_fail:
	for (i = 0; i < pool->clk_src_count; i++) {
		if (pool->clock_sources[i] != NULL)
			dal_clock_source_destroy(&pool->clock_sources[i]);
	}
	return false;
}

void dce110_destruct_resource_pool(struct resource_pool *pool)
{
	unsigned int i;

	for (i = 0; i < pool->controller_count; i++) {
		if (pool->opps[i] != NULL)
			dce110_opp_destroy(&pool->opps[i]);

		if (pool->transforms[i] != NULL)
			dce110_transform_destroy(&pool->transforms[i]);

		if (pool->ipps[i] != NULL)
			dce110_ipp_destroy(&pool->ipps[i]);

		if (pool->mis[i] != NULL)
			dce110_mem_input_destroy(&pool->mis[i]);

		if (pool->timing_generators[i] != NULL)	{
			dc_service_free(pool->timing_generators[i]->ctx, DCE110TG_FROM_TG(pool->timing_generators[i]));
			pool->timing_generators[i] = NULL;
		}
	}

	for (i = 0; i < pool->stream_enc_count; i++) {
		if (pool->stream_enc[i] != NULL)
			dc_service_free(pool->stream_enc[i]->ctx,
				DCE110STRENC_FROM_STRENC(pool->stream_enc[i]));
	}

	for (i = 0; i < pool->clk_src_count; i++) {
		if (pool->clock_sources[i] != NULL) {
			dal_clock_source_destroy(&pool->clock_sources[i]);
		}
	}

	for (i = 0; i < pool->audio_count; i++)	{
		if (pool->audios[i] != NULL) {
			dal_audio_destroy(&pool->audios[i]);
		}
	}
	if (pool->display_clock != NULL) {
		dal_display_clock_destroy(&pool->display_clock);
	}

	if (pool->scaler_filter != NULL) {
		dal_scaler_filter_destroy(&pool->scaler_filter);
	}
	if (pool->irqs != NULL) {
		dal_irq_service_destroy(&pool->irqs);
	}

	if (pool->adapter_srv != NULL) {
		dal_adapter_service_destroy(&pool->adapter_srv);
	}
}

static struct clock_source *find_first_free_pll(
		struct resource_context *res_ctx)
{
	if (res_ctx->clock_source_ref_count[DCE110_CLK_SRC_PLL0] == 0) {
		return res_ctx->pool.clock_sources[DCE110_CLK_SRC_PLL0];
	}
	if (res_ctx->clock_source_ref_count[DCE110_CLK_SRC_PLL1] == 0) {
		return res_ctx->pool.clock_sources[DCE110_CLK_SRC_PLL1];
	}

	return 0;
}

static enum audio_dto_source translate_to_dto_source(enum controller_id crtc_id)
{
	switch (crtc_id) {
	case CONTROLLER_ID_D0:
		return DTO_SOURCE_ID0;
	case CONTROLLER_ID_D1:
		return DTO_SOURCE_ID1;
	case CONTROLLER_ID_D2:
		return DTO_SOURCE_ID2;
	case CONTROLLER_ID_D3:
		return DTO_SOURCE_ID3;
	case CONTROLLER_ID_D4:
		return DTO_SOURCE_ID4;
	case CONTROLLER_ID_D5:
		return DTO_SOURCE_ID5;
	default:
		return DTO_SOURCE_UNKNOWN;
	}
}

static void build_audio_output(
	const struct core_stream *stream,
	struct audio_output *audio_output)
{
	audio_output->engine_id = stream->stream_enc->id;

	audio_output->signal = stream->signal;

	/* audio_crtc_info  */

	audio_output->crtc_info.h_total =
		stream->public.timing.h_total;

	/* Audio packets are sent during actual CRTC blank physical signal, we
	 * need to specify actual active signal portion */
	audio_output->crtc_info.h_active =
			stream->public.timing.h_addressable
			+ stream->public.timing.h_border_left
			+ stream->public.timing.h_border_right;

	audio_output->crtc_info.v_active =
			stream->public.timing.v_addressable
			+ stream->public.timing.v_border_top
			+ stream->public.timing.v_border_bottom;

	audio_output->crtc_info.pixel_repetition = 1;

	audio_output->crtc_info.interlaced =
			stream->public.timing.flags.INTERLACE;

	audio_output->crtc_info.refresh_rate =
		(stream->public.timing.pix_clk_khz*1000)/
		(stream->public.timing.h_total*stream->public.timing.v_total);

	audio_output->crtc_info.color_depth =
		stream->public.timing.display_color_depth;

	audio_output->crtc_info.requested_pixel_clock =
			stream->pix_clk_params.requested_pix_clk;

	/* TODO - Investigate why calculated pixel clk has to be
	 * requested pixel clk */
	audio_output->crtc_info.calculated_pixel_clock =
			stream->pix_clk_params.requested_pix_clk;

	if (stream->signal == SIGNAL_TYPE_DISPLAY_PORT ||
			stream->signal == SIGNAL_TYPE_DISPLAY_PORT_MST) {
		audio_output->pll_info.dp_dto_source_clock_in_khz =
			dal_display_clock_get_dp_ref_clk_frequency(
				stream->dis_clk);
	}

	audio_output->pll_info.feed_back_divider =
			stream->pll_settings.feedback_divider;

	audio_output->pll_info.dto_source =
		translate_to_dto_source(
			stream->controller_idx + 1);

	/* TODO hard code to enable for now. Need get from stream */
	audio_output->pll_info.ss_enabled = true;

	audio_output->pll_info.ss_percentage =
			stream->pll_settings.ss_percentage;
}

static void get_pixel_clock_parameters(
	const struct core_stream *stream,
	struct pixel_clk_params *pixel_clk_params)
{
	pixel_clk_params->requested_pix_clk = stream->public.timing.pix_clk_khz;
	pixel_clk_params->encoder_object_id = stream->sink->link->link_enc->id;
	pixel_clk_params->signal_type = stream->sink->public.sink_signal;
	pixel_clk_params->controller_id = stream->controller_idx + 1;
	/* TODO: un-hardcode*/
	pixel_clk_params->requested_sym_clk = LINK_RATE_LOW *
		LINK_RATE_REF_FREQ_IN_KHZ;
	pixel_clk_params->flags.ENABLE_SS = 0;
	pixel_clk_params->color_depth =
		stream->public.timing.display_color_depth;
	pixel_clk_params->flags.DISPLAY_BLANKED = 1;
}

static enum dc_status build_stream_hw_param(struct core_stream *stream)
{
	/*TODO: unhardcode*/
	stream->max_tmds_clk_from_edid_in_mhz = 0;
	stream->max_hdmi_deep_color = COLOR_DEPTH_121212;
	stream->max_hdmi_pixel_clock = 600000;

	get_pixel_clock_parameters(stream, &stream->pix_clk_params);
	dal_clock_source_get_pix_clk_dividers(
		stream->clock_source,
		&stream->pix_clk_params,
		&stream->pll_settings);

	build_audio_output(stream, &stream->audio_output);

	return DC_OK;
}

static enum dc_status validate_mapped_resource(
		const struct dc *dc,
		struct validate_context *context)
{
	enum dc_status status = DC_OK;
	uint8_t i, j;

	for (i = 0; i < context->target_count; i++) {
		struct core_target *target = context->targets[i];
		if (context->target_flags[i].unchanged)
			continue;
		for (j = 0; j < target->public.stream_count; j++) {
			struct core_stream *stream =
				DC_STREAM_TO_CORE(target->public.streams[j]);
			struct core_link *link = stream->sink->link;

			if (!stream->tg->funcs->validate_timing(
					stream->tg, &stream->public.timing))
				return DC_FAIL_CONTROLLER_VALIDATE;

			if (stream->signal == SIGNAL_TYPE_VIRTUAL)
				return status;

			status = build_stream_hw_param(stream);

			if (status != DC_OK)
				return status;

			if (!dce110_link_encoder_validate_output_with_stream(
					link->link_enc,
					stream))
				return DC_FAIL_ENC_VALIDATE;

			/* TODO: validate audio ASIC caps, encoder */

			status = dc_link_validate_mode_timing(stream->sink,
					link,
					&stream->public.timing);

			if (status != DC_OK)
				return status;

			dce_base_build_info_frame(stream);
		}
	}

	return DC_OK;
}

enum dc_status dce110_validate_bandwidth(
	const struct dc *dc,
	struct validate_context *context)
{
	uint8_t i, j;
	enum dc_status result = DC_ERROR_UNEXPECTED;
	uint8_t number_of_displays = 0;
	uint8_t max_htaps = 1;
	uint8_t max_vtaps = 1;
	bool all_displays_in_sync = true;
	struct dc_crtc_timing prev_timing;

	memset(&context->bw_mode_data, 0, sizeof(context->bw_mode_data));

	for (i = 0; i < context->target_count; i++) {
		struct core_target *target = context->targets[i];
		for (j = 0; j < target->public.stream_count; j++) {
			struct core_stream *stream =
				DC_STREAM_TO_CORE(target->public.streams[j]);
			struct bw_calcs_input_single_display *disp = &context->
				bw_mode_data.displays_data[number_of_displays];

			if (target->status.surface_count == 0) {
				disp->graphics_scale_ratio = bw_int_to_fixed(1);
				disp->graphics_h_taps = 2;
				disp->graphics_v_taps = 2;

				/* TODO: remove when bw formula accepts taps per
				 * display
				 */
				if (max_vtaps < 2)
					max_vtaps = 2;
				if (max_htaps < 2)
					max_htaps = 2;

			} else {
				disp->graphics_scale_ratio =
					fixed31_32_to_bw_fixed(
						stream->ratios.vert.value);
				disp->graphics_h_taps = stream->taps.h_taps;
				disp->graphics_v_taps = stream->taps.v_taps;

				/* TODO: remove when bw formula accepts taps per
				 * display
				 */
				if (max_vtaps < stream->taps.v_taps)
					max_vtaps = stream->taps.v_taps;
				if (max_htaps < stream->taps.h_taps)
					max_htaps = stream->taps.h_taps;
			}

			disp->graphics_src_width =
					stream->public.timing.h_addressable;
			disp->graphics_src_height =
					stream->public.timing.v_addressable;
			disp->h_total = stream->public.timing.h_total;
			disp->pixel_rate = bw_frc_to_fixed(
				stream->public.timing.pix_clk_khz, 1000);

			/*TODO: get from surface*/
			disp->graphics_bytes_per_pixel = 4;
			disp->graphics_tiling_mode = bw_def_tiled;

			/* DCE11 defaults*/
			disp->graphics_lb_bpc = 10;
			disp->graphics_interlace_mode = false;
			disp->fbc_enable = false;
			disp->lpt_enable = false;
			disp->graphics_stereo_mode = bw_def_mono;
			disp->underlay_mode = bw_def_none;

			/*All displays will be synchronized if timings are all
			 * the same
			 */
			if (number_of_displays != 0 && all_displays_in_sync)
				if (dal_memcmp(&prev_timing,
					&stream->public.timing,
					sizeof(struct dc_crtc_timing))!= 0)
					all_displays_in_sync = false;
			if (number_of_displays == 0)
				prev_timing = stream->public.timing;

			number_of_displays++;
		}
	}

	/* TODO: remove when bw formula accepts taps per
	 * display
	 */
	context->bw_mode_data.displays_data[0].graphics_v_taps = max_vtaps;
	context->bw_mode_data.displays_data[0].graphics_h_taps = max_htaps;

	context->bw_mode_data.number_of_displays = number_of_displays;
	context->bw_mode_data.display_synchronization_enabled =
							all_displays_in_sync;

	dal_logger_write(
		dc->ctx->logger,
		LOG_MAJOR_BWM,
		LOG_MINOR_BWM_REQUIRED_BANDWIDTH_CALCS,
		"%s: start",
		__func__);

	if (!bw_calcs(
			dc->ctx,
			&dc->bw_dceip,
			&dc->bw_vbios,
			&context->bw_mode_data,
			&context->bw_results))
		result =  DC_FAIL_BANDWIDTH_VALIDATE;
	else
		result =  DC_OK;

	if (result == DC_FAIL_BANDWIDTH_VALIDATE)
		dal_logger_write(dc->ctx->logger,
			LOG_MAJOR_BWM,
			LOG_MINOR_BWM_MODE_VALIDATION,
			"%s: Bandwidth validation failed!",
			__func__);

	if (dal_memcmp(&dc->current_context.bw_results,
			&context->bw_results, sizeof(context->bw_results))) {
		struct log_entry log_entry;
		dal_logger_open(
			dc->ctx->logger,
			&log_entry,
			LOG_MAJOR_BWM,
			LOG_MINOR_BWM_REQUIRED_BANDWIDTH_CALCS);
		dal_logger_append(&log_entry, "%s: finish, numDisplays: %d\n"
			"nbpMark_b: %d nbpMark_a: %d urgentMark_b: %d urgentMark_a: %d\n"
			"stutMark_b: %d stutMark_a: %d\n",
			__func__, number_of_displays,
			context->bw_results.nbp_state_change_wm_ns[0].b_mark,
			context->bw_results.nbp_state_change_wm_ns[0].a_mark,
			context->bw_results.urgent_wm_ns[0].b_mark,
			context->bw_results.urgent_wm_ns[0].a_mark,
			context->bw_results.stutter_exit_wm_ns[0].b_mark,
			context->bw_results.stutter_exit_wm_ns[0].a_mark);
		dal_logger_append(&log_entry,
			"nbpMark_b: %d nbpMark_a: %d urgentMark_b: %d urgentMark_a: %d\n"
			"stutMark_b: %d stutMark_a: %d\n",
			context->bw_results.nbp_state_change_wm_ns[1].b_mark,
			context->bw_results.nbp_state_change_wm_ns[1].a_mark,
			context->bw_results.urgent_wm_ns[1].b_mark,
			context->bw_results.urgent_wm_ns[1].a_mark,
			context->bw_results.stutter_exit_wm_ns[1].b_mark,
			context->bw_results.stutter_exit_wm_ns[1].a_mark);
		dal_logger_append(&log_entry,
			"nbpMark_b: %d nbpMark_a: %d urgentMark_b: %d urgentMark_a: %d\n"
			"stutMark_b: %d stutMark_a: %d stutter_mode_enable: %d\n",
			context->bw_results.nbp_state_change_wm_ns[2].b_mark,
			context->bw_results.nbp_state_change_wm_ns[2].a_mark,
			context->bw_results.urgent_wm_ns[2].b_mark,
			context->bw_results.urgent_wm_ns[2].a_mark,
			context->bw_results.stutter_exit_wm_ns[2].b_mark,
			context->bw_results.stutter_exit_wm_ns[2].a_mark,
			context->bw_results.stutter_mode_enable);
		dal_logger_append(&log_entry,
			"cstate: %d pstate: %d nbpstate: %d sync: %d dispclk: %d\n"
			"sclk: %d sclk_sleep: %d yclk: %d blackout_duration: %d\n",
			context->bw_results.cpuc_state_change_enable,
			context->bw_results.cpup_state_change_enable,
			context->bw_results.nbp_state_change_enable,
			context->bw_results.all_displays_in_sync,
			context->bw_results.dispclk_khz,
			context->bw_results.required_sclk,
			context->bw_results.required_sclk_deep_sleep,
			context->bw_results.required_yclk,
			context->bw_results.required_blackout_duration_us);
		dal_logger_close(&log_entry);
	}
	return result;
}

static void set_target_unchanged(
		struct validate_context *context,
		uint8_t target_idx)
{
	uint8_t i;
	struct core_target *target = context->targets[target_idx];
	context->target_flags[target_idx].unchanged = true;
	for (i = 0; i < target->public.stream_count; i++) {
		struct core_stream *core_stream =
			DC_STREAM_TO_CORE(target->public.streams[i]);
		uint8_t index = core_stream->controller_idx;
		context->res_ctx.controller_ctx[index].flags.unchanged = true;
	}
}

static enum dc_status map_clock_resources(
		const struct dc *dc,
		struct validate_context *context)
{
	uint8_t i, j;

	/* mark resources used for targets that are already active */
	for (i = 0; i < context->target_count; i++) {
		struct core_target *target = context->targets[i];

		if (!context->target_flags[i].unchanged)
			continue;

		for (j = 0; j < target->public.stream_count; j++) {
			struct core_stream *stream =
				DC_STREAM_TO_CORE(target->public.streams[j]);

			reference_clock_source(
				&context->res_ctx,
				stream->clock_source);
		}
	}

	/* acquire new resources */
	for (i = 0; i < context->target_count; i++) {
		struct core_target *target = context->targets[i];

		if (context->target_flags[i].unchanged)
			continue;

		for (j = 0; j < target->public.stream_count; j++) {
			struct core_stream *stream =
				DC_STREAM_TO_CORE(target->public.streams[j]);

			if (dc_is_dp_signal(stream->signal)
				|| stream->signal == SIGNAL_TYPE_VIRTUAL)
				stream->clock_source = context->res_ctx.
					pool.clock_sources[DCE110_CLK_SRC_EXT];
			else
				stream->clock_source =
					find_used_clk_src_for_sharing(
							context, stream);
			if (stream->clock_source == NULL)
				stream->clock_source =
					find_first_free_pll(&context->res_ctx);

			if (stream->clock_source == NULL)
				return DC_NO_CLOCK_SOURCE_RESOURCE;

			reference_clock_source(
					&context->res_ctx,
					stream->clock_source);
		}
	}

	return DC_OK;
}

enum dc_status dce110_validate_with_context(
		const struct dc *dc,
		const struct dc_validation_set set[],
		uint8_t set_count,
		struct validate_context *context)
{
	enum dc_status result = DC_ERROR_UNEXPECTED;
	uint8_t i, j;
	struct dc_context *dc_ctx = dc->ctx;

	for (i = 0; i < set_count; i++) {
		context->targets[i] = DC_TARGET_TO_CORE(set[i].target);

		for (j = 0; j < dc->current_context.target_count; j++)
			if (dc->current_context.targets[j] == context->targets[i])
				set_target_unchanged(context, i);

		if (!context->target_flags[i].unchanged)
			if (!logical_attach_surfaces_to_target(
							(struct dc_surface **)set[i].surfaces,
							set[i].surface_count,
							&context->targets[i]->public)) {
				DC_ERROR("Failed to attach surface to target!\n");
				return DC_FAIL_ATTACH_SURFACES;
			}
	}

	context->target_count = set_count;

	context->res_ctx.pool = dc->res_pool;

	result = dce_base_map_resources(dc, context);

	if (result == DC_OK)
		result = map_clock_resources(dc, context);

	if (result == DC_OK)
		result = validate_mapped_resource(dc, context);

	if (result == DC_OK)
		build_scaling_params_for_context(dc, context);

	if (result == DC_OK)
		result = dce110_validate_bandwidth(dc, context);

	return result;
}
