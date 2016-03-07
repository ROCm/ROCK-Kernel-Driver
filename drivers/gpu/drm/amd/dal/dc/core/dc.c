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
 */

#include "dm_services.h"

#include "dc.h"

#include "core_status.h"
#include "core_types.h"
#include "hw_sequencer.h"

#include "resource.h"

#include "adapter_service_interface.h"
#include "clock_source.h"
#include "dc_bios_types.h"

#include "bandwidth_calcs.h"
#include "include/irq_service_interface.h"
#include "transform.h"
#include "timing_generator.h"
#include "virtual/virtual_link_encoder.h"

#include "link_hwss.h"
#include "link_encoder.h"

#include "dc_link_ddc.h"

/*******************************************************************************
 * Private structures
 ******************************************************************************/

struct dc_target_sync_report {
	uint32_t h_count;
	uint32_t v_count;
};

/*******************************************************************************
 * Private functions
 ******************************************************************************/
static void destroy_links(struct core_dc *dc)
{
	uint32_t i;

	for (i = 0; i < dc->link_count; i++) {
		if (NULL != dc->links[i])
			link_destroy(&dc->links[i]);
	}
}

static bool create_links(
		struct core_dc *dc,
		struct adapter_service *as,
		uint32_t num_virtual_links)
{
	int i;
	int connectors_num;
	struct dc_bios *dcb;

	dc->link_count = 0;

	dcb = dc->ctx->dc_bios;

	connectors_num = dcb->funcs->get_connectors_number(dcb);

	if (connectors_num > ENUM_ID_COUNT) {
		dm_error(
			"DC: Number of connectors %d exceeds maximum of %d!\n",
			connectors_num,
			ENUM_ID_COUNT);
		return false;
	}

	if (connectors_num == 0 && num_virtual_links == 0) {
		dm_error("DC: Number of connectors can not be zero!\n");
		return false;
	}

	dm_output_to_console(
		"DC: %s: connectors_num: physical:%d, virtual:%d\n",
		__func__,
		connectors_num,
		num_virtual_links);

	for (i = 0; i < connectors_num; i++) {
		struct link_init_data link_init_params = {0};
		struct core_link *link;

		link_init_params.ctx = dc->ctx;
		link_init_params.adapter_srv = as;
		link_init_params.connector_index = i;
		link_init_params.link_index = dc->link_count;
		link_init_params.dc = dc;
		link = link_create(&link_init_params);

		if (link) {
			dc->links[dc->link_count] = link;
			link->dc = dc;
			++dc->link_count;
		} else {
			dm_error("DC: failed to create link!\n");
		}
	}

	for (i = 0; i < num_virtual_links; i++) {
		struct core_link *link = dm_alloc(sizeof(*link));
		struct encoder_init_data enc_init = {0};

		if (link == NULL) {
			BREAK_TO_DEBUGGER();
			goto failed_alloc;
		}

		link->adapter_srv = as;
		link->ctx = dc->ctx;
		link->dc = dc;
		link->public.connector_signal = SIGNAL_TYPE_VIRTUAL;
		link->link_id.type = OBJECT_TYPE_CONNECTOR;
		link->link_id.id = CONNECTOR_ID_VIRTUAL;
		link->link_id.enum_id = ENUM_ID_1;
		link->link_enc = dm_alloc(sizeof(*link->link_enc));

		enc_init.adapter_service = as;
		enc_init.ctx = dc->ctx;
		enc_init.channel = CHANNEL_ID_UNKNOWN;
		enc_init.hpd_source = HPD_SOURCEID_UNKNOWN;
		enc_init.transmitter = TRANSMITTER_UNKNOWN;
		enc_init.connector = link->link_id;
		enc_init.encoder.type = OBJECT_TYPE_ENCODER;
		enc_init.encoder.id = ENCODER_ID_INTERNAL_VIRTUAL;
		enc_init.encoder.enum_id = ENUM_ID_1;
		virtual_link_encoder_construct(link->link_enc, &enc_init);

		link->public.link_index = dc->link_count;
		dc->links[dc->link_count] = link;
		dc->link_count++;
	}

	return true;

failed_alloc:
	return false;
}



static struct adapter_service *create_as(
		const struct dc_init_data *init,
		struct dc_context *dc_ctx)
{
	struct adapter_service *as = NULL;
	struct as_init_data init_data;

	memset(&init_data, 0, sizeof(init_data));

	init_data.ctx = dc_ctx;

	/* BIOS parser init data */
	init_data.bp_init_data.ctx = dc_ctx;
	init_data.bp_init_data.bios = init->asic_id.atombios_base_address;

	/* HW init data */
	init_data.hw_init_data.chip_id = init->asic_id.chip_id;
	init_data.hw_init_data.chip_family = init->asic_id.chip_family;
	init_data.hw_init_data.pci_revision_id = init->asic_id.pci_revision_id;
	init_data.hw_init_data.fake_paths_num = init->asic_id.fake_paths_num;
	init_data.hw_init_data.feature_flags = init->asic_id.feature_flags;
	init_data.hw_init_data.hw_internal_rev = init->asic_id.hw_internal_rev;
	init_data.hw_init_data.runtime_flags = init->asic_id.runtime_flags;
	init_data.hw_init_data.vram_width = init->asic_id.vram_width;
	init_data.hw_init_data.vram_type = init->asic_id.vram_type;

	init_data.display_param = &init->display_param;
	init_data.vbios_override = init->vbios_override;
	init_data.dce_environment = init->dce_environment;

	as = dal_adapter_service_create(&init_data);

	return as;
}

static void bw_calcs_data_update_from_pplib(struct core_dc *dc)
{
	struct dm_pp_clock_levels clks = {0};

	/*do system clock*/
	dm_pp_get_clock_levels_by_type(
			dc->ctx,
			DM_PP_CLOCK_TYPE_ENGINE_CLK,
			&clks);
	/* convert all the clock fro kHz to fix point mHz */
	dc->bw_vbios.high_sclk = bw_frc_to_fixed(
			clks.clocks_in_khz[clks.num_levels-1], 1000);
	dc->bw_vbios.mid_sclk  = bw_frc_to_fixed(
			clks.clocks_in_khz[clks.num_levels>>1], 1000);
	dc->bw_vbios.low_sclk  = bw_frc_to_fixed(
			clks.clocks_in_khz[0], 1000);

	/*do display clock*/
	dm_pp_get_clock_levels_by_type(
			dc->ctx,
			DM_PP_CLOCK_TYPE_DISPLAY_CLK,
			&clks);

	dc->bw_vbios.high_voltage_max_dispclk = bw_frc_to_fixed(
			clks.clocks_in_khz[clks.num_levels-1], 1000);
	dc->bw_vbios.mid_voltage_max_dispclk  = bw_frc_to_fixed(
			clks.clocks_in_khz[clks.num_levels>>1], 1000);
	dc->bw_vbios.low_voltage_max_dispclk  = bw_frc_to_fixed(
			clks.clocks_in_khz[0], 1000);

	/*do memory clock*/
	dm_pp_get_clock_levels_by_type(
			dc->ctx,
			DM_PP_CLOCK_TYPE_MEMORY_CLK,
			&clks);

	dc->bw_vbios.low_yclk = bw_frc_to_fixed(
		clks.clocks_in_khz[0] * MEMORY_TYPE_MULTIPLIER, 1000);
	dc->bw_vbios.mid_yclk = bw_frc_to_fixed(
		clks.clocks_in_khz[clks.num_levels>>1] * MEMORY_TYPE_MULTIPLIER,
		1000);
	dc->bw_vbios.high_yclk = bw_frc_to_fixed(
		clks.clocks_in_khz[clks.num_levels-1] * MEMORY_TYPE_MULTIPLIER,
		1000);
}

static bool construct(struct core_dc *dc, const struct dc_init_data *init_params)
{
	struct dal_logger *logger;
	struct adapter_service *as;
	struct dc_context *dc_ctx = dm_alloc(sizeof(*dc_ctx));
	enum dce_version dc_version = DCE_VERSION_UNKNOWN;

	if (!dc_ctx) {
		dm_error("%s: failed to create ctx\n", __func__);
		goto ctx_fail;
	}

	dc_ctx->cgs_device = init_params->cgs_device;
	dc_ctx->driver_context = init_params->driver;
	dc_ctx->dc = &dc->public;

	/* Create logger */
	logger = dal_logger_create(dc_ctx);

	if (!logger) {
		/* can *not* call logger. call base driver 'print error' */
		dm_error("%s: failed to create Logger!\n", __func__);
		goto logger_fail;
	}
	dc_ctx->logger = logger;
	dc->ctx = dc_ctx;
	dc->ctx->dce_environment = init_params->dce_environment;


	dc_version = resource_parse_asic_id(init_params->asic_id);


	/* TODO: Refactor DCE code to remove AS and asic caps */
	if (dc_version < DCE_VERSION_MAX) {
		/* Create adapter service */
		as = create_as(init_params, dc_ctx);

		if (!as) {
			dm_error("%s: create_as() failed!\n", __func__);
			goto as_fail;
		}

		/* Initialize HW controlled by Adapter Service */
		if (false == dal_adapter_service_initialize_hw_data(
				as)) {
			dm_error("%s: dal_adapter_service_initialize_hw_data()"\
					"  failed!\n", __func__);
			/* Note that AS exist, so have to destroy it.*/
			goto as_fail;
		}

		dc_ctx->dc_bios = dal_adapter_service_get_bios_parser(as);

		if (!dc_construct_resource_pool(
			as, dc, init_params->num_virtual_links, dc_version))
			goto construct_resource_fail;

		if (!create_links(dc, as, init_params->num_virtual_links))
			goto create_links_fail;

		bw_calcs_init(&dc->bw_dceip, &dc->bw_vbios);

		bw_calcs_data_update_from_pplib(dc);
	} else {

		/* Resource should construct all asic specific resources.
		 * This should be the only place where we need to parse the asic id
		 */

		dc_ctx->dc_bios = init_params->vbios_override;
		if (!dc_construct_resource_pool(
			NULL, dc, init_params->num_virtual_links, dc_version))
			goto construct_resource_fail;

		if (!create_links(dc, NULL, init_params->num_virtual_links))
			goto create_links_fail;
	}

	return true;

	/**** error handling here ****/
construct_resource_fail:
create_links_fail:
as_fail:
	dal_logger_destroy(&dc_ctx->logger);
logger_fail:
	dm_free(dc_ctx);
ctx_fail:
	return false;
}

static void destruct(struct core_dc *dc)
{
	resource_validate_ctx_destruct(&dc->current_context);
	destroy_links(dc);
	dc->res_pool.funcs->destruct(&dc->res_pool);
	dal_logger_destroy(&dc->ctx->logger);
	dm_free(dc->ctx);
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
		struct core_stream *stream,
		struct core_dc* core_dc)
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

		core_dc->hwss.enable_display_power_gating(
			core_dc->ctx,
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

/*******************************************************************************
 * Public functions
 ******************************************************************************/

struct dc *dc_create(const struct dc_init_data *init_params)
 {
	struct dc_context ctx = {
		.driver_context = init_params->driver,
		.cgs_device = init_params->cgs_device
	};
	struct core_dc *core_dc = dm_alloc(sizeof(*core_dc));

	if (NULL == core_dc)
		goto alloc_fail;

	ctx.dc = &core_dc->public;
	if (false == construct(core_dc, init_params))
		goto construct_fail;

	/*TODO: separate HW and SW initialization*/
	core_dc->hwss.init_hw(core_dc);

	core_dc->public.caps.max_targets = core_dc->res_pool.pipe_count;
	core_dc->public.caps.max_links = core_dc->link_count;
	core_dc->public.caps.max_audios = core_dc->res_pool.audio_count;

	dal_logger_write(core_dc->ctx->logger,
			LOG_MAJOR_INTERFACE_TRACE,
			LOG_MINOR_COMPONENT_DC,
			"Display Core initialized\n");

	return &core_dc->public;

construct_fail:
	dm_free(core_dc);

alloc_fail:
	return NULL;
}

void dc_destroy(struct dc **dc)
{
	struct core_dc *core_dc = DC_TO_CORE(*dc);
	destruct(core_dc);
	dm_free(core_dc);
	*dc = NULL;
}

bool dc_validate_resources(
		const struct dc *dc,
		const struct dc_validation_set set[],
		uint8_t set_count)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);
	enum dc_status result = DC_ERROR_UNEXPECTED;
	struct validate_context *context;

	context = dm_alloc(sizeof(struct validate_context));
	if(context == NULL)
		goto context_alloc_fail;

	result = core_dc->res_pool.funcs->validate_with_context(
						core_dc, set, set_count, context);

	resource_validate_ctx_destruct(context);
	dm_free(context);

context_alloc_fail:

	return (result == DC_OK);

}

static void program_timing_sync(
		struct core_dc *core_dc,
		struct validate_context *ctx)
{
	uint8_t i;
	uint8_t j;
	uint8_t group_size = 0;
	uint8_t tg_count = ctx->res_ctx.pool.pipe_count;
	struct timing_generator *tg_set[MAX_PIPES];

	for (i = 0; i < tg_count; i++) {
		if (!ctx->res_ctx.pipe_ctx[i].stream)
			continue;

		tg_set[0] = ctx->res_ctx.pool.timing_generators[i];
		group_size = 1;

		/* Add tg to the set, search rest of the tg's for ones with
		 * same timing, add all tgs with same timing to the group
		 */
		for (j = i + 1; j < tg_count; j++) {
			if (!ctx->res_ctx.pipe_ctx[j].stream)
				continue;

			if (resource_is_same_timing(
				&ctx->res_ctx.pipe_ctx[j].stream->public.timing,
				&ctx->res_ctx.pipe_ctx[i].stream->public
								.timing)) {
				tg_set[group_size] =
					ctx->res_ctx.pool.timing_generators[j];
				group_size++;
			}
		}

		/* Right now we limit to one timing sync group so if one is
		 * found we break. A group has to be more than one tg.*/
		if (group_size > 1)
			break;
	}

	if(group_size > 1) {
		core_dc->hwss.enable_timing_synchronization(core_dc->ctx, group_size, tg_set);
	}
}

static bool targets_changed(
		struct core_dc *dc,
		struct dc_target *targets[],
		uint8_t target_count)
{
	uint8_t i;

	if (target_count != dc->current_context.target_count)
		return true;

	for (i = 0; i < dc->current_context.target_count; i++) {
		if (&dc->current_context.targets[i]->public != targets[i])
			return true;
	}

	return false;
}

static void target_enable_memory_requests(struct dc_target *dc_target,
		struct resource_context *res_ctx)
{
	uint8_t i, j;
	struct core_target *target = DC_TARGET_TO_CORE(dc_target);

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

static void target_disable_memory_requests(struct dc_target *dc_target,
		struct resource_context *res_ctx)
{
	uint8_t i, j;
	struct core_target *target = DC_TARGET_TO_CORE(dc_target);

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

void pplib_apply_safe_state(
	const struct core_dc *dc)
{
	dm_pp_apply_safe_state(dc->ctx);
}

static void fill_display_configs(
	const struct validate_context *context,
	struct dm_pp_display_configuration *pp_display_cfg)
{
	uint8_t i, j, k;
	uint8_t num_cfgs = 0;

	for (i = 0; i < context->target_count; i++) {
		const struct core_target *target = context->targets[i];

		for (j = 0; j < target->public.stream_count; j++) {
			const struct core_stream *stream =
				DC_STREAM_TO_CORE(target->public.streams[j]);
			struct dm_pp_single_disp_config *cfg =
					&pp_display_cfg->disp_configs[num_cfgs];
			const struct pipe_ctx *pipe_ctx = NULL;

			for (k = 0; k < MAX_PIPES; k++)
				if (stream ==
					context->res_ctx.pipe_ctx[k].stream) {
					pipe_ctx = &context->res_ctx.pipe_ctx[k];
					break;
				}

			num_cfgs++;
			cfg->signal = pipe_ctx->signal;
			cfg->pipe_idx = pipe_ctx->pipe_idx;
			cfg->src_height = stream->public.src.height;
			cfg->src_width = stream->public.src.width;
			cfg->ddi_channel_mapping =
				stream->sink->link->ddi_channel_mapping.raw;
			cfg->transmitter =
				stream->sink->link->link_enc->transmitter;
			cfg->link_settings.lane_count = stream->sink->link->public.cur_link_settings.lane_count;
			cfg->link_settings.link_rate = stream->sink->link->public.cur_link_settings.link_rate;
			cfg->link_settings.link_spread = stream->sink->link->public.cur_link_settings.link_spread;
			cfg->sym_clock = stream->public.timing.pix_clk_khz;
			switch (stream->public.timing.display_color_depth) {
			case COLOR_DEPTH_101010:
				cfg->sym_clock = (cfg->sym_clock * 30) / 24;
				break;
			case COLOR_DEPTH_121212:
				cfg->sym_clock = (cfg->sym_clock * 36) / 24;
				break;
			case COLOR_DEPTH_161616:
				cfg->sym_clock = (cfg->sym_clock * 48) / 24;
				break;
			default:
				break;
			}
			/* TODO: unhardcode*/
			cfg->v_refresh = 60;
		}
	}
	pp_display_cfg->display_count = num_cfgs;
}

static uint32_t get_min_vblank_time_us(const struct validate_context *context)
{
	uint8_t i, j;
	uint32_t min_vertical_blank_time = -1;

	for (i = 0; i < context->target_count; i++) {
		const struct core_target *target = context->targets[i];

		for (j = 0; j < target->public.stream_count; j++) {
			const struct dc_stream *stream =
						target->public.streams[j];
			uint32_t vertical_blank_in_pixels = 0;
			uint32_t vertical_blank_time = 0;

			vertical_blank_in_pixels = stream->timing.h_total *
				(stream->timing.v_total
					- stream->timing.v_addressable);
			vertical_blank_time = vertical_blank_in_pixels
				* 1000 / stream->timing.pix_clk_khz;
			if (min_vertical_blank_time > vertical_blank_time)
				min_vertical_blank_time = vertical_blank_time;
		}
	}
	return min_vertical_blank_time;
}

void pplib_apply_display_requirements(
	const struct core_dc *dc,
	const struct validate_context *context,
	struct dm_pp_display_configuration *pp_display_cfg)
{
	pp_display_cfg->all_displays_in_sync =
		context->bw_results.all_displays_in_sync;
	pp_display_cfg->nb_pstate_switch_disable =
			context->bw_results.nbp_state_change_enable == false;
	pp_display_cfg->cpu_cc6_disable =
			context->bw_results.cpuc_state_change_enable == false;
	pp_display_cfg->cpu_pstate_disable =
			context->bw_results.cpup_state_change_enable == false;
	pp_display_cfg->cpu_pstate_separation_time =
			context->bw_results.required_blackout_duration_us;

	pp_display_cfg->min_memory_clock_khz = context->bw_results.required_yclk
		/ MEMORY_TYPE_MULTIPLIER;
	pp_display_cfg->min_engine_clock_khz = context->bw_results.required_sclk;
	pp_display_cfg->min_engine_clock_deep_sleep_khz
			= context->bw_results.required_sclk_deep_sleep;

	pp_display_cfg->avail_mclk_switch_time_us =
						get_min_vblank_time_us(context);
	/* TODO: dce11.2*/
	pp_display_cfg->avail_mclk_switch_time_in_disp_active_us = 0;

	pp_display_cfg->disp_clk_khz = context->bw_results.dispclk_khz;

	fill_display_configs(context, pp_display_cfg);

	/* TODO: is this still applicable?*/
	if (pp_display_cfg->display_count == 1) {
		const struct dc_crtc_timing *timing =
			&context->targets[0]->public.streams[0]->timing;

		pp_display_cfg->crtc_index =
			pp_display_cfg->disp_configs[0].pipe_idx;
		pp_display_cfg->line_time_in_us = timing->h_total * 1000
							/ timing->pix_clk_khz;
	}

	dm_pp_apply_display_requirements(dc->ctx, pp_display_cfg);
}

bool dc_commit_targets(
	struct dc *dc,
	struct dc_target *targets[],
	uint8_t target_count)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);
	struct dc_bios *dcb = core_dc->ctx->dc_bios;
	enum dc_status result = DC_ERROR_UNEXPECTED;
	struct validate_context *context;
	struct dc_validation_set set[4];
	uint8_t i;

	if (false == targets_changed(core_dc, targets, target_count))
		return DC_OK;

	dal_logger_write(core_dc->ctx->logger,
				LOG_MAJOR_INTERFACE_TRACE,
				LOG_MINOR_COMPONENT_DC,
				"%s: %d targets\n",
				__func__,
				target_count);

	for (i = 0; i < target_count; i++) {
		struct dc_target *target = targets[i];

		dc_target_log(target,
				core_dc->ctx->logger,
				LOG_MAJOR_INTERFACE_TRACE,
				LOG_MINOR_COMPONENT_DC);

		set[i].target = targets[i];
		set[i].surface_count = 0;

	}

	context = dm_alloc(sizeof(struct validate_context));
	if (context == NULL)
		goto context_alloc_fail;

	result = core_dc->res_pool.funcs->validate_with_context(core_dc, set, target_count, context);
	if (result != DC_OK){
		BREAK_TO_DEBUGGER();
		resource_validate_ctx_destruct(context);
		goto fail;
	}

	pplib_apply_safe_state(core_dc);

	if (!dcb->funcs->is_accelerated_mode(dcb)) {
		core_dc->hwss.enable_accelerated_mode(core_dc);
	}

	for (i = 0; i < core_dc->current_context.target_count; i++) {
		/*TODO: optimize this to happen only when necessary*/
		target_disable_memory_requests(
				&core_dc->current_context.targets[i]->public, &core_dc->current_context.res_ctx);
	}

	if (result == DC_OK) {
		core_dc->hwss.reset_hw_ctx(core_dc, context);

		if (context->target_count > 0)
			result = core_dc->hwss.apply_ctx_to_hw(core_dc, context);
	}

	for (i = 0; i < context->target_count; i++) {
		struct dc_target *dc_target = &context->targets[i]->public;
		if (context->target_status[i].surface_count > 0)
			target_enable_memory_requests(dc_target, &core_dc->current_context.res_ctx);
	}

	program_timing_sync(core_dc, context);

	pplib_apply_display_requirements(core_dc, context, &context->pp_display_cfg);

	resource_validate_ctx_destruct(&core_dc->current_context);

	core_dc->current_context = *context;

fail:
	dm_free(context);

context_alloc_fail:
	return (result == DC_OK);
}

bool dc_commit_surfaces_to_target(
		struct dc *dc,
		struct dc_surface *new_surfaces[],
		uint8_t new_surface_count,
		struct dc_target *dc_target)

{
	struct core_dc *core_dc = DC_TO_CORE(dc);
	struct dc_bios *dcb = core_dc->ctx->dc_bios;

	int i, j;
	uint32_t prev_disp_clk = core_dc->current_context.bw_results.dispclk_khz;
	struct core_target *target = DC_TARGET_TO_CORE(dc_target);
	struct dc_target_status *target_status = NULL;
	struct validate_context *context;
	int current_enabled_surface_count = 0;
	int new_enabled_surface_count = 0;
	bool is_mpo_turning_on = false;

	context = dm_alloc(sizeof(struct validate_context));

	resource_validate_ctx_copy_construct(&core_dc->current_context, context);

	/* Cannot commit surface to a target that is not commited */
	for (i = 0; i < context->target_count; i++)
		if (target == context->targets[i])
			break;

	target_status = &context->target_status[i];

	if (!dcb->funcs->is_accelerated_mode(dcb)
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
				DC_STREAM_TO_CORE(dc_target->streams[0]), core_dc);
		is_mpo_turning_on = true;
	} else if (new_surface_count < 2 && target_status->surface_count == 2) {
		context->res_ctx.pipe_ctx[DCE110_UNDERLAY_IDX].stream = NULL;
		context->res_ctx.pipe_ctx[DCE110_UNDERLAY_IDX].surface = NULL;
	}

	dal_logger_write(core_dc->ctx->logger,
				LOG_MAJOR_INTERFACE_TRACE,
				LOG_MINOR_COMPONENT_DC,
				"%s: commit %d surfaces to target 0x%x\n",
				__func__,
				new_surface_count,
				dc_target);

	if (!resource_attach_surfaces_to_context(
			new_surfaces, new_surface_count, dc_target, context)) {
		BREAK_TO_DEBUGGER();
		goto unexpected_fail;
	}

	for (i = 0; i < new_surface_count; i++)
		for (j = 0; j < MAX_PIPES; j++) {
			if (context->res_ctx.pipe_ctx[j].surface !=
					DC_SURFACE_TO_CORE(new_surfaces[i]))
				continue;

			resource_build_scaling_params(
				new_surfaces[i], &context->res_ctx.pipe_ctx[j]);
		}

	if (core_dc->res_pool.funcs->validate_bandwidth(core_dc, context) != DC_OK) {
		BREAK_TO_DEBUGGER();
		goto unexpected_fail;
	}

	if (prev_disp_clk < context->bw_results.dispclk_khz ||
		(is_mpo_turning_on &&
			prev_disp_clk == context->bw_results.dispclk_khz)) {
		core_dc->hwss.program_bw(core_dc, context);
		pplib_apply_display_requirements(core_dc, context,
						&context->pp_display_cfg);
	}

	if (current_enabled_surface_count > 0 && new_enabled_surface_count == 0)
		target_disable_memory_requests(dc_target, &core_dc->current_context.res_ctx);

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

			dal_logger_write(core_dc->ctx->logger,
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

			core_dc->hwss.set_gamma_correction(
					pipe_ctx->ipp,
					pipe_ctx->opp,
					gamma, surface);

			core_dc->hwss.set_plane_config(
				core_dc, pipe_ctx, &context->res_ctx);
		}

	core_dc->hwss.update_plane_addrs(core_dc, &context->res_ctx);

	/* Lower display clock if necessary */
	if (prev_disp_clk > context->bw_results.dispclk_khz) {
		core_dc->hwss.program_bw(core_dc, context);
		pplib_apply_display_requirements(core_dc, context,
						&context->pp_display_cfg);
	}

	resource_validate_ctx_destruct(&(core_dc->current_context));
	core_dc->current_context = *context;
	dm_free(context);
	return true;

unexpected_fail:

	resource_validate_ctx_destruct(context);

	dm_free(context);
	return false;
}

uint8_t dc_get_current_target_count(const struct dc *dc)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);
	return core_dc->current_context.target_count;
}

struct dc_target *dc_get_target_at_index(const struct dc *dc, uint8_t i)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);
	if (i < core_dc->current_context.target_count)
		return &(core_dc->current_context.targets[i]->public);
	return NULL;
}

const struct dc_link *dc_get_link_at_index(struct dc *dc, uint32_t link_index)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);
	return &core_dc->links[link_index]->public;
}

const struct graphics_object_id dc_get_link_id_at_index(
	struct dc *dc, uint32_t link_index)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);
	return core_dc->links[link_index]->link_id;
}

const struct ddc_service *dc_get_ddc_at_index(
	struct dc *dc, uint32_t link_index)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);
	return core_dc->links[link_index]->ddc;
}

const enum dc_irq_source dc_get_hpd_irq_source_at_index(
	struct dc *dc, uint32_t link_index)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);
	return core_dc->links[link_index]->public.irq_source_hpd;
}

const struct audio **dc_get_audios(struct dc *dc)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);
	return (const struct audio **)core_dc->res_pool.audios;
}

void dc_flip_surface_addrs(
		struct dc *dc,
		const struct dc_surface *const surfaces[],
		struct dc_flip_addrs flip_addrs[],
		uint32_t count)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);
	int i, j;

	for (i = 0; i < count; i++) {
		for (j = 0; j < MAX_PIPES; j++) {
			struct core_surface *ctx_surface =
				core_dc->current_context.res_ctx.pipe_ctx[j].surface;
			if (DC_SURFACE_TO_CORE(surfaces[i]) == ctx_surface) {
				ctx_surface->public.address = flip_addrs[i].address;
				ctx_surface->public.flip_immediate = flip_addrs[i].flip_immediate;
				break;
			}
		}
	}

	core_dc->hwss.update_plane_addrs(core_dc, &core_dc->current_context.res_ctx);
}

enum dc_irq_source dc_interrupt_to_irq_source(
		struct dc *dc,
		uint32_t src_id,
		uint32_t ext_id)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);
	return dal_irq_service_to_irq_source(core_dc->res_pool.irqs, src_id, ext_id);
}

void dc_interrupt_set(const struct dc *dc, enum dc_irq_source src, bool enable)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);
	dal_irq_service_set(core_dc->res_pool.irqs, src, enable);
}

void dc_interrupt_ack(struct dc *dc, enum dc_irq_source src)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);
	dal_irq_service_ack(core_dc->res_pool.irqs, src);
}

const struct dc_target *dc_get_target_on_irq_source(
		const struct dc *dc,
		enum dc_irq_source src)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);

	uint8_t i, j;
	uint8_t crtc_idx;

	switch (src) {
	case DC_IRQ_SOURCE_VUPDATE1:
	case DC_IRQ_SOURCE_VUPDATE2:
	case DC_IRQ_SOURCE_VUPDATE3:
	case DC_IRQ_SOURCE_VUPDATE4:
	case DC_IRQ_SOURCE_VUPDATE5:
	case DC_IRQ_SOURCE_VUPDATE6:
		crtc_idx = src - DC_IRQ_SOURCE_VUPDATE1;
		break;
	case DC_IRQ_SOURCE_PFLIP1:
	case DC_IRQ_SOURCE_PFLIP2:
	case DC_IRQ_SOURCE_PFLIP3:
	case DC_IRQ_SOURCE_PFLIP4:
	case DC_IRQ_SOURCE_PFLIP5:
	case DC_IRQ_SOURCE_PFLIP6:
	case DC_IRQ_SOURCE_PFLIP_UNDERLAY0:
		crtc_idx = src - DC_IRQ_SOURCE_PFLIP1;
		break;
	default:
		dm_error("%s: invalid irq source: %d\n!" ,__func__, src);
		return NULL;
	}

	for (i = 0; i < core_dc->current_context.target_count; i++) {
		struct core_target *target = core_dc->current_context.targets[i];
		struct dc_target *dc_target = &target->public;

		for (j = 0; j < target->public.stream_count; j++) {
			const struct core_stream *stream =
				DC_STREAM_TO_CORE(dc_target->streams[j]);

			if (core_dc->current_context.res_ctx.
					pipe_ctx[crtc_idx].stream == stream)
				return dc_target;
		}
	}

	dm_error("%s: 'dc_target' is NULL for irq source: %d\n!", __func__, src);
	return NULL;
}

void dc_set_power_state(
	struct dc *dc,
	enum dc_acpi_cm_power_state power_state,
	enum dc_video_power_state video_power_state)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);

	core_dc->previous_power_state = core_dc->current_power_state;
	core_dc->current_power_state = video_power_state;

	switch (power_state) {
	case DC_ACPI_CM_POWER_STATE_D0:
		core_dc->hwss.init_hw(core_dc);
		break;
	default:
		/* NULL means "reset/release all DC targets" */
		dc_commit_targets(dc, NULL, 0);

		core_dc->hwss.power_down(core_dc);
		break;
	}

}

void dc_resume(const struct dc *dc)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);

	uint32_t i;

	for (i = 0; i < core_dc->link_count; i++)
		core_link_resume(core_dc->links[i]);
}

bool dc_read_dpcd(
		struct dc *dc,
		uint32_t link_index,
		uint32_t address,
		uint8_t *data,
		uint32_t size)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);

	struct core_link *link = core_dc->links[link_index];
	enum ddc_result r = dal_ddc_service_read_dpcd_data(
			link->ddc,
			address,
			data,
			size);
	return r == DDC_RESULT_SUCESSFULL;
}

bool dc_write_dpcd(
		struct dc *dc,
		uint32_t link_index,
		uint32_t address,
		const uint8_t *data,
		uint32_t size)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);

	struct core_link *link = core_dc->links[link_index];

	enum ddc_result r = dal_ddc_service_write_dpcd_data(
			link->ddc,
			address,
			data,
			size);
	return r == DDC_RESULT_SUCESSFULL;
}

bool dc_submit_i2c(
		struct dc *dc,
		uint32_t link_index,
		struct i2c_command *cmd)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);

	struct core_link *link = core_dc->links[link_index];
	struct ddc_service *ddc = link->ddc;

	return dal_i2caux_submit_i2c_command(
		dal_adapter_service_get_i2caux(ddc->as),
		ddc->ddc_pin,
		cmd);
}

bool dc_link_add_remote_sink(const struct dc_link *link, struct dc_sink *sink)
{
	struct core_link *core_link = DC_LINK_TO_LINK(link);
	struct dc_link *dc_link = &core_link->public;

	if (dc_link->sink_count >= MAX_SINKS_PER_LINK) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	dc_link->remote_sinks[link->sink_count] = sink;
	dc_link->sink_count++;

	return true;
}

void dc_link_set_sink(const struct dc_link *link, struct dc_sink *sink)
{
	struct core_link *core_link = DC_LINK_TO_LINK(link);
	struct dc_link *dc_link = &core_link->public;

	dc_link->local_sink = sink;

	if (sink == NULL) {
		dc_link->sink_count = 0;
		dc_link->type = dc_connection_none;
	} else {
		dc_link->sink_count = 1;
		dc_link->type = dc_connection_single;
	}
}

void dc_link_remove_remote_sink(const struct dc_link *link, const struct dc_sink *sink)
{
	int i;
	struct core_link *core_link = DC_LINK_TO_LINK(link);
	struct dc_link *dc_link = &core_link->public;

	if (!link->sink_count) {
		BREAK_TO_DEBUGGER();
		return;
	}

	for (i = 0; i < dc_link->sink_count; i++) {
		if (dc_link->remote_sinks[i] == sink) {
			dc_sink_release(sink);
			dc_link->remote_sinks[i] = NULL;

			/* shrink array to remove empty place */
			while (i < dc_link->sink_count - 1) {
				dc_link->remote_sinks[i] = dc_link->remote_sinks[i+1];
				i++;
			}

			dc_link->sink_count--;
			return;
		}
	}
}

const struct dc_stream_status *dc_stream_get_status(
	const struct dc_stream *dc_stream)
{
	struct core_stream *stream = DC_STREAM_TO_CORE(dc_stream);

	return &stream->status;
}
