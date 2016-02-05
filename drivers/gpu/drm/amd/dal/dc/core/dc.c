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
static void destroy_links(struct dc *dc)
{
	uint32_t i;

	for (i = 0; i < dc->link_count; i++) {
		if (NULL != dc->links[i])
			link_destroy(&dc->links[i]);
	}
}

static bool create_links(struct dc *dc, const struct dc_init_data *init_params)
{
	int i;
	int connectors_num;
	struct dc_bios *dcb;

	dc->link_count = 0;

	dcb = dal_adapter_service_get_bios_parser(init_params->adapter_srv);

	connectors_num = dcb->funcs->get_connectors_number(dcb);

	if (connectors_num > ENUM_ID_COUNT) {
		dm_error(
			"DC: Number of connectors %d exceeds maximum of %d!\n",
			connectors_num,
			ENUM_ID_COUNT);
		return false;
	}

	if (connectors_num == 0 && init_params->num_virtual_links == 0) {
		dm_error("DC: Number of connectors can not be zero!\n");
		return false;
	}

	dm_output_to_console(
		"DC: %s: connectors_num: physical:%d, virtual:%d\n",
		__func__,
		connectors_num,
		init_params->num_virtual_links);

	for (i = 0; i < connectors_num; i++) {
		struct link_init_data link_init_params = {0};
		struct core_link *link;

		link_init_params.ctx = init_params->ctx;
		link_init_params.adapter_srv = init_params->adapter_srv;
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

	for (i = 0; i < init_params->num_virtual_links; i++) {
		struct core_link *link = dm_alloc(
			dc->ctx,
			sizeof(*link));
		struct encoder_init_data enc_init = {0};

		if (link == NULL) {
			BREAK_TO_DEBUGGER();
			goto failed_alloc;
		}

		link->adapter_srv = init_params->adapter_srv;
		link->ctx = init_params->ctx;
		link->dc = dc;
		link->public.connector_signal = SIGNAL_TYPE_VIRTUAL;
		link->link_id.type = OBJECT_TYPE_CONNECTOR;
		link->link_id.id = CONNECTOR_ID_VIRTUAL;
		link->link_id.enum_id = ENUM_ID_1;
		link->link_enc = dm_alloc(
			dc->ctx,
			sizeof(*link->link_enc));

		enc_init.adapter_service = init_params->adapter_srv;
		enc_init.ctx = init_params->ctx;
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


static void init_hw(struct dc *dc)
{
	int i;
	struct dc_bios *bp;
	struct transform *xfm;

	bp = dal_adapter_service_get_bios_parser(dc->res_pool.adapter_srv);
	for (i = 0; i < dc->res_pool.pipe_count; i++) {
		xfm = dc->res_pool.transforms[i];

		dc->hwss.enable_display_power_gating(
				dc->ctx, i, bp,
				PIPE_GATING_CONTROL_INIT);
		dc->hwss.enable_display_power_gating(
				dc->ctx, i, bp,
				PIPE_GATING_CONTROL_DISABLE);

		xfm->funcs->transform_power_up(xfm);
		dc->hwss.enable_display_pipe_clock_gating(
			dc->ctx,
			true);
	}

	dc->hwss.clock_gating_power_up(dc->ctx, false);
	bp->funcs->power_up(bp);
	/***************************************/

	for (i = 0; i < dc->link_count; i++) {
		/****************************************/
		/* Power up AND update implementation according to the
		 * required signal (which may be different from the
		 * default signal on connector). */
		struct core_link *link = dc->links[i];
		link->link_enc->funcs->hw_init(link->link_enc);
	}

	for (i = 0; i < dc->res_pool.pipe_count; i++) {
		struct timing_generator *tg = dc->res_pool.timing_generators[i];

		tg->funcs->disable_vga(tg);

		/* Blank controller using driver code instead of
		 * command table. */
		tg->funcs->set_blank(tg, true);
	}

	for(i = 0; i < dc->res_pool.audio_count; i++) {
		struct audio *audio = dc->res_pool.audios[i];

		if (dal_audio_power_up(audio) != AUDIO_RESULT_OK)
			dm_error("Failed audio power up!\n");
	}

}

static struct adapter_service *create_as(
		struct dc_init_data *dc_init_data,
		const struct dal_init_data *init)
{
	struct adapter_service *as = NULL;
	struct as_init_data init_data;

	dm_memset(&init_data, 0, sizeof(init_data));

	init_data.ctx = dc_init_data->ctx;

	/* BIOS parser init data */
	init_data.bp_init_data.ctx = dc_init_data->ctx;
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

	/* bdf is BUS,DEVICE,FUNCTION*/
	init_data.bdf_info = init->bdf_info;

	init_data.display_param = &init->display_param;
	init_data.vbios_override = init->vbios_override;
	init_data.dce_environment = init->dce_environment;

	as = dal_adapter_service_create(&init_data);

	return as;
}

static void bw_calcs_data_update_from_pplib(struct dc *dc)
{
	struct dc_pp_clock_levels clks = {0};

	/*do system clock*/
	dm_pp_get_clock_levels_by_type(
			dc->ctx,
			DC_PP_CLOCK_TYPE_ENGINE_CLK,
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
			DC_PP_CLOCK_TYPE_DISPLAY_CLK,
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
			DC_PP_CLOCK_TYPE_MEMORY_CLK,
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

static bool construct(struct dc *dc, const struct dal_init_data *init_params)
{
	struct dal_logger *logger;
	/* Tempory code
	 * TODO: replace dal_init_data with dc_init_data when dal is removed
	 */
	struct dc_init_data dc_init_data = {0};

	/* Create dc context */
	/* A temp dc context is used only to allocate the memory for actual
	 * dc context */
	struct dc_context ctx = {0};
	ctx.cgs_device = init_params->cgs_device;
	ctx.dc = dc;

	dc_init_data.ctx = dm_alloc(&ctx, sizeof(*dc_init_data.ctx));
	if (!dc_init_data.ctx) {
		dm_error("%s: failed to create ctx\n", __func__);
		goto ctx_fail;
	}
	dc_init_data.ctx->driver_context = init_params->driver;
	dc_init_data.ctx->cgs_device = init_params->cgs_device;
	dc_init_data.num_virtual_links = init_params->num_virtual_links;
	dc_init_data.ctx->dc = dc;

	/* Create logger */
	logger = dal_logger_create(dc_init_data.ctx);

	if (!logger) {
		/* can *not* call logger. call base driver 'print error' */
		dm_error("%s: failed to create Logger!\n", __func__);
		goto logger_fail;
	}
	dc_init_data.ctx->logger = logger;

	/* Create adapter service */
	dc_init_data.adapter_srv = create_as(&dc_init_data, init_params);

	if (!dc_init_data.adapter_srv) {
		dm_error("%s: create_as() failed!\n", __func__);
		goto as_fail;
	}

	/* Initialize HW controlled by Adapter Service */
	if (false == dal_adapter_service_initialize_hw_data(
			dc_init_data.adapter_srv)) {
		dm_error("%s: dal_adapter_service_initialize_hw_data()"\
				"  failed!\n", __func__);
		/* Note that AS exist, so have to destroy it.*/
		goto as_fail;
	}

	dc->ctx = dc_init_data.ctx;

	dc->ctx->dce_environment = dal_adapter_service_get_dce_environment(
			dc_init_data.adapter_srv);

	/* Create hardware sequencer */
	if (!dc_construct_hw_sequencer(dc_init_data.adapter_srv, dc))
		goto hwss_fail;

	if (!dc_construct_resource_pool(
		dc_init_data.adapter_srv, dc, dc_init_data.num_virtual_links))
		goto construct_resource_fail;

	if (!create_links(dc, &dc_init_data))
		goto create_links_fail;

	bw_calcs_init(&dc->bw_dceip, &dc->bw_vbios);

	bw_calcs_data_update_from_pplib(dc);

	return true;

	/**** error handling here ****/
construct_resource_fail:
create_links_fail:
as_fail:
	dal_logger_destroy(&dc_init_data.ctx->logger);
logger_fail:
hwss_fail:
	dm_free(&ctx, dc_init_data.ctx);
ctx_fail:
	return false;
}

static void destruct(struct dc *dc)
{
	destroy_links(dc);
	dc->res_pool.funcs->destruct(&dc->res_pool);
	dal_logger_destroy(&dc->ctx->logger);
	dm_free(dc->ctx, dc->ctx);
}

/*******************************************************************************
 * Public functions
 ******************************************************************************/

struct dc *dc_create(const struct dal_init_data *init_params)
 {
	struct dc_context ctx = {
		.driver_context = init_params->driver,
		.cgs_device = init_params->cgs_device
	};
	struct dc *dc = dm_alloc(&ctx, sizeof(*dc));

	if (NULL == dc)
		goto alloc_fail;

	ctx.dc = dc;
	if (false == construct(dc, init_params))
		goto construct_fail;

	/*TODO: separate HW and SW initialization*/
	init_hw(dc);

	return dc;

construct_fail:
	dm_free(&ctx, dc);

alloc_fail:
	return NULL;
}

void dc_destroy(struct dc **dc)
{
	struct dc_context ctx = *(*dc)->ctx;
	destruct(*dc);
	dm_free(&ctx, *dc);
	*dc = NULL;
}

bool dc_validate_resources(
		const struct dc *dc,
		const struct dc_validation_set set[],
		uint8_t set_count)
{
	enum dc_status result = DC_ERROR_UNEXPECTED;
	struct validate_context *context;

	context = dm_alloc(dc->ctx, sizeof(struct validate_context));
	if(context == NULL)
		goto context_alloc_fail;

	result = dc->res_pool.funcs->validate_with_context(
						dc, set, set_count, context);

	dm_free(dc->ctx, context);
context_alloc_fail:

	return (result == DC_OK);

}

static void program_timing_sync(
		struct dc_context *dc_ctx,
		struct validate_context *ctx)
{
	uint8_t i;
	uint8_t j;
	uint8_t group_size = 0;
	uint8_t tg_count = ctx->res_ctx.pool.pipe_count;
	struct timing_generator *tg_set[3];

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

			if (is_same_timing(
				&ctx->res_ctx.pipe_ctx[j].stream->public
								.timing,
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
		dc_ctx->dc->hwss.enable_timing_synchronization(dc_ctx, group_size, tg_set);
	}
}

static bool targets_changed(
		struct dc *dc,
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

bool dc_commit_targets(
	struct dc *dc,
	struct dc_target *targets[],
	uint8_t target_count)
{
	enum dc_status result = DC_ERROR_UNEXPECTED;
	struct validate_context *context;
	struct dc_validation_set set[4];
	uint8_t i;

	if (false == targets_changed(dc, targets, target_count))
		return DC_OK;

	dal_logger_write(dc->ctx->logger,
				LOG_MAJOR_INTERFACE_TRACE,
				LOG_MINOR_COMPONENT_DC,
				"%s: %d targets\n",
				__func__,
				target_count);

	for (i = 0; i < target_count; i++) {
		struct dc_target *target = targets[i];

		dc_target_log(target,
				dc->ctx->logger,
				LOG_MAJOR_INTERFACE_TRACE,
				LOG_MINOR_COMPONENT_DC);

		set[i].target = targets[i];
		set[i].surface_count = 0;

	}

	context = dm_alloc(dc->ctx, sizeof(struct validate_context));
	if (context == NULL)
		goto context_alloc_fail;

	result = dc->res_pool.funcs->validate_with_context(dc, set, target_count, context);
	if (result != DC_OK){
		BREAK_TO_DEBUGGER();
		goto fail;
	}

	pplib_apply_safe_state(dc);

	if (!dal_adapter_service_is_in_accelerated_mode(
						dc->res_pool.adapter_srv)) {
		dc->hwss.enable_accelerated_mode(dc);
	}

	for (i = 0; i < dc->current_context.target_count; i++) {
		/*TODO: optimize this to happen only when necessary*/
		dc_target_disable_memory_requests(
				&dc->current_context.targets[i]->public);
	}

	if (result == DC_OK) {
		dc->hwss.reset_hw_ctx(dc, context);

		if (context->target_count > 0)
			result = dc->hwss.apply_ctx_to_hw(dc, context);
	}

	for (i = 0; i < context->target_count; i++) {
		struct dc_target *dc_target = &context->targets[i]->public;
		if (context->target_status[i].surface_count > 0)
			dc_target_enable_memory_requests(dc_target);
	}

	/* Release old targets */
	for (i = 0; i < dc->current_context.target_count; i++) {
		dc_target_release(
				&dc->current_context.targets[i]->public);
		dc->current_context.targets[i] = NULL;
	}
	/* Retain new targets*/
	for (i = 0; i < context->target_count; i++) {
		dc_target_retain(&context->targets[i]->public);
	}

	dc->current_context = *context;

	program_timing_sync(dc->ctx, context);

	pplib_apply_display_requirements(dc, context, &context->pp_display_cfg);

fail:
	dm_free(dc->ctx, context);

context_alloc_fail:
	return (result == DC_OK);
}

uint8_t dc_get_current_target_count(const struct dc *dc)
{
	return dc->current_context.target_count;
}

struct dc_target *dc_get_target_at_index(const struct dc *dc, uint8_t i)
{
	if (i < dc->current_context.target_count)
		return &dc->current_context.targets[i]->public;
	return NULL;
}

const struct dc_link *dc_get_link_at_index(struct dc *dc, uint32_t link_index)
{
	return &dc->links[link_index]->public;
}

const struct graphics_object_id dc_get_link_id_at_index(
	struct dc *dc, uint32_t link_index)
{
	return dc->links[link_index]->link_id;
}

const struct ddc_service *dc_get_ddc_at_index(
	struct dc *dc, uint32_t link_index)
{
	return dc->links[link_index]->ddc;
}

const enum dc_irq_source dc_get_hpd_irq_source_at_index(
	struct dc *dc, uint32_t link_index)
{
	return dc->links[link_index]->public.irq_source_hpd;
}

const struct audio **dc_get_audios(struct dc *dc)
{
	return (const struct audio **)dc->res_pool.audios;
}

void dc_get_caps(const struct dc *dc, struct dc_caps *caps)
{
	caps->max_targets = dc->res_pool.pipe_count;
	caps->max_links = dc->link_count;
	caps->max_audios = dc->res_pool.audio_count;
}

void dc_flip_surface_addrs(
		struct dc *dc,
		const struct dc_surface *const surfaces[],
		struct dc_flip_addrs flip_addrs[],
		uint32_t count)
{
	uint8_t i;
	for (i = 0; i < count; i++) {
		struct core_surface *surface = DC_SURFACE_TO_CORE(surfaces[i]);
		/*
		 * TODO figure out a good way to keep track of address. Until
		 * then we'll have to awkwardly bypass the "const" surface.
		 */
		surface->public.address = flip_addrs[i].address;
		surface->public.flip_immediate = flip_addrs[i].flip_immediate;

		dc->hwss.update_plane_addrs(
				dc, &dc->current_context.res_ctx, surface);
	}
}

enum dc_irq_source dc_interrupt_to_irq_source(
		struct dc *dc,
		uint32_t src_id,
		uint32_t ext_id)
{
	return dal_irq_service_to_irq_source(dc->res_pool.irqs, src_id, ext_id);
}


void dc_interrupt_set(const struct dc *dc, enum dc_irq_source src, bool enable)
{
	dal_irq_service_set(dc->res_pool.irqs, src, enable);
}

void dc_interrupt_ack(struct dc *dc, enum dc_irq_source src)
{
	dal_irq_service_ack(dc->res_pool.irqs, src);
}

const struct dc_target *dc_get_target_on_irq_source(
		const struct dc *dc,
		enum dc_irq_source src)
{
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

	for (i = 0; i < dc->current_context.target_count; i++) {
		struct core_target *target = dc->current_context.targets[i];
		struct dc_target *dc_target = &target->public;

		for (j = 0; j < target->public.stream_count; j++) {
			const struct core_stream *stream =
				DC_STREAM_TO_CORE(dc_target->streams[j]);

			if (dc->current_context.res_ctx.
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
	dc->previous_power_state = dc->current_power_state;
	dc->current_power_state = video_power_state;

	switch (power_state) {
	case DC_ACPI_CM_POWER_STATE_D0:
		init_hw(dc);
		break;
	default:
		/* NULL means "reset/release all DC targets" */
		dc_commit_targets(dc, NULL, 0);

		dc->hwss.power_down(dc);
		break;
	}

}

void dc_resume(const struct dc *dc)
{
	uint32_t i;

	for (i = 0; i < dc->link_count; i++)
		core_link_resume(dc->links[i]);
}

bool dc_read_dpcd(
		struct dc *dc,
		uint32_t link_index,
		uint32_t address,
		uint8_t *data,
		uint32_t size)
{
	struct core_link *link =
			DC_LINK_TO_LINK(dc_get_link_at_index(dc, link_index));

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
	struct core_link *link =
			DC_LINK_TO_LINK(dc_get_link_at_index(dc, link_index));

	enum ddc_result r = dal_ddc_service_write_dpcd_data(
			link->ddc,
			address,
			data,
			size);
	return r == DDC_RESULT_SUCESSFULL;
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
