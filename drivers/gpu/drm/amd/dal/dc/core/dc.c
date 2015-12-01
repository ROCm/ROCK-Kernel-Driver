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

#include "dc_services.h"

#include "dc.h"

#include "core_status.h"
#include "core_types.h"
#include "hw_sequencer.h"

#include "resource.h"

#include "adapter_service_interface.h"
#include "clock_source_interface.h"

#include "include/irq_service_interface.h"
#include "bandwidth_calcs.h"
#include "include/irq_service_interface.h"

#include "link_hwss.h"

/*******************************************************************************
 * Private structures
 ******************************************************************************/

struct dc_target_sync_report {
	uint32_t h_count;
	uint32_t v_count;
};

struct dc_sync_report {
	uint32_t targets_num;
	struct dc_target_sync_report trg_reports[MAX_TARGET_NUM];
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

	dc->link_count = 0;

	connectors_num = dal_bios_parser_get_connectors_number(
			dal_adapter_service_get_bios_parser(
					init_params->adapter_srv));

	if (0 == connectors_num || connectors_num > ENUM_ID_COUNT) {
		dal_error("DC: Invalid number of connectors!\n");
		return false;
	}

	dal_output_to_console("%s: connectors_num:%d\n", __func__,
			connectors_num);

	dc->links = dc_service_alloc(
		init_params->ctx, connectors_num * sizeof(struct core_link *));

	if (NULL == dc->links) {
		dal_error("DC: failed to allocate 'links' storage!\n");
		goto allocate_dc_links_storage_fail;
	}

	for (i = 0; i < connectors_num; i++) {
		struct link_init_data link_init_params = {0};
		struct core_link *link;

		link_init_params.ctx = init_params->ctx;
		link_init_params.adapter_srv = init_params->adapter_srv;
		link_init_params.connector_index = i;
		link_init_params.link_index = dc->link_count;
		link_init_params.dc = dc;
		link =  link_create(&link_init_params);

		if (link) {
			dc->links[dc->link_count] = link;
			link->dc = dc;
			++dc->link_count;
		}
		else {
			dal_error("DC: failed to create link!\n");
		}
	}

	if (!dc->link_count) {
		dal_error("DC: no 'links' were created!\n");
		goto allocate_dc_links_storage_fail;
	}

	return true;

allocate_dc_links_storage_fail:
	return false;
}

static void init_hw(struct dc *dc)
{
	int i;
	struct bios_parser *bp;
	struct transform *xfm;

	bp = dal_adapter_service_get_bios_parser(dc->res_pool.adapter_srv);
	for(i = 0; i < dc->res_pool.controller_count; i++) {
		xfm = dc->res_pool.transforms[i];

		dc->hwss.enable_display_power_gating(
				dc->ctx, i, bp,
				PIPE_GATING_CONTROL_INIT);
		dc->hwss.enable_display_power_gating(
				dc->ctx, i, bp,
				PIPE_GATING_CONTROL_DISABLE);

		dc->hwss.transform_power_up(xfm);
		dc->hwss.enable_display_pipe_clock_gating(
			dc->ctx,
			true);
	}

	dc->hwss.clock_gating_power_up(dc->ctx, false);
	dal_bios_parser_power_up(bp);
	/***************************************/

	for (i = 0; i < dc->link_count; i++) {
		/****************************************/
		/* Power up AND update implementation according to the
		 * required signal (which may be different from the
		 * default signal on connector). */
		struct core_link *link = dc->links[i];
		if (dc->hwss.encoder_power_up(link->link_enc) != ENCODER_RESULT_OK) {
			dal_error("Failed link encoder power up!\n");
			return;
		}
	}

	dal_bios_parser_set_scratch_acc_mode_change(bp);

	for(i = 0; i < dc->res_pool.controller_count; i++) {
		struct timing_generator *tg = dc->res_pool.timing_generators[i];

		dc->hwss.disable_vga(tg);

		/* Blank controller using driver code instead of
		 * command table. */
		dc->hwss.disable_memory_requests(tg);
	}

	for(i = 0; i < dc->res_pool.audio_count; i++) {
		struct audio *audio = dc->res_pool.audios[i];

		if (dal_audio_power_up(audio) != AUDIO_RESULT_OK)
			dal_error("Failed audio power up!\n");
	}

}

static struct adapter_service *create_as(
		struct dc_init_data *dc_init_data,
		const struct dal_init_data *init)
{
	struct adapter_service *as = NULL;
	struct as_init_data init_data;

	dc_service_memset(&init_data, 0, sizeof(init_data));

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

	as = dal_adapter_service_create(&init_data);

	return as;
}

static void bw_calcs_data_update_from_pplib(struct dc *dc)
{
	struct dal_system_clock_range clk_range = { 0 };

	dc_service_get_system_clocks_range(dc->ctx, &clk_range);

	/* on CZ Gardenia from PPLib we get:
	 * clk_range.max_mclk:80000
	 * clk_range.min_mclk:80000
	 * clk_range.max_sclk:80000
	 * clk_range.min_sclk:30000 */

	/* The values for calcs are stored in units of MHz, so for example
	 * 80000 will be stored as 800. */
	dc->bw_vbios.high_sclk = frc_to_fixed(clk_range.max_sclk, 100);
	dc->bw_vbios.low_sclk = frc_to_fixed(clk_range.min_sclk, 100);

	dc->bw_vbios.high_yclk = frc_to_fixed(clk_range.max_mclk, 100);
	dc->bw_vbios.low_yclk = frc_to_fixed(clk_range.min_mclk, 100);
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

	dc_init_data.ctx = dc_service_alloc(&ctx, sizeof(*dc_init_data.ctx));
	if (!dc_init_data.ctx) {
		dal_error("%s: failed to create ctx\n", __func__);
		goto ctx_fail;
	}
	dc_init_data.ctx->driver_context = init_params->driver;
	dc_init_data.ctx->cgs_device = init_params->cgs_device;
	dc_init_data.ctx->dc = dc;

	/* Create logger */
	logger = dal_logger_create(dc_init_data.ctx);

	if (!logger) {
		/* can *not* call logger. call base driver 'print error' */
		dal_error("%s: failed to create Logger!\n", __func__);
		goto logger_fail;
	}
	dc_init_data.ctx->logger = logger;

	/* Create adapter service */
	dc_init_data.adapter_srv = create_as(&dc_init_data, init_params);

	if (!dc_init_data.adapter_srv) {
		dal_error("%s: create_as() failed!\n", __func__);
		goto as_fail;
	}

	/* Initialize HW controlled by Adapter Service */
	if (false == dal_adapter_service_initialize_hw_data(
			dc_init_data.adapter_srv)) {
		dal_error("%s: dal_adapter_service_initialize_hw_data()"\
				"  failed!\n", __func__);
		/* Note that AS exist, so have to destroy it.*/
		goto as_fail;
	}

	dc->ctx = dc_init_data.ctx;

	/* Create hardware sequencer */
	if (!dc_construct_hw_sequencer(dc_init_data.adapter_srv, dc))
		goto hwss_fail;


	/* TODO: create all the sub-objects of DC. */
	if (false == create_links(dc, &dc_init_data))
		goto create_links_fail;

	if (!dc->hwss.construct_resource_pool(
			dc_init_data.adapter_srv,
			dc,
			&dc->res_pool))
		goto construct_resource_fail;


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
	dc_service_free(&ctx, dc_init_data.ctx);
ctx_fail:
	return false;
}

static void destruct(struct dc *dc)
{
	destroy_links(dc);
	dc_service_free(dc->ctx, dc->links);
	dc->hwss.destruct_resource_pool(&dc->res_pool);
	dal_logger_destroy(&dc->ctx->logger);
	dc_service_free(dc->ctx, dc->ctx);
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
	struct dc *dc = dc_service_alloc(&ctx, sizeof(*dc));

	if (NULL == dc)
		goto alloc_fail;

	ctx.dc = dc;
	if (false == construct(dc, init_params))
		goto construct_fail;

	/*TODO: separate HW and SW initialization*/
	init_hw(dc);

	return dc;

construct_fail:
	dc_service_free(&ctx, dc);

alloc_fail:
	return NULL;
}

void dc_destroy(struct dc **dc)
{
	destruct(*dc);
	dc_service_free((*dc)->ctx, *dc);
	*dc = NULL;
}

bool dc_validate_resources(
		const struct dc *dc,
		const struct dc_validation_set set[],
		uint8_t set_count)
{
	enum dc_status result = DC_ERROR_UNEXPECTED;
	struct validate_context *context;

	context = dc_service_alloc(dc->ctx, sizeof(struct validate_context));
	if(context == NULL)
		goto context_alloc_fail;

	result = dc->hwss.validate_with_context(dc, set, set_count, context);

	dc_service_free(dc->ctx, context);
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
	uint8_t tg_count = ctx->res_ctx.pool.controller_count;
	struct timing_generator *tg_set[3];

	for (i = 0; i < tg_count; i++) {
		if (!ctx->res_ctx.controller_ctx[i].stream)
			continue;

		tg_set[0] = ctx->res_ctx.pool.timing_generators[i];
		group_size = 1;

		/* Add tg to the set, search rest of the tg's for ones with
		 * same timing, add all tgs with same timing to the group
		 */
		for (j = i + 1; j < tg_count; j++) {
			if (!ctx->res_ctx.controller_ctx[j].stream)
				continue;

			if (is_same_timing(
				&ctx->res_ctx.controller_ctx[j].stream->public
								.timing,
				&ctx->res_ctx.controller_ctx[i].stream->public
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

static void pplib_post_set_mode(
	struct dc *dc,
	const struct validate_context *context)
{
	struct dc_pp_display_configuration pp_display_cfg = { 0 };

	pp_display_cfg.nb_pstate_switch_disable =
			context->bw_results.nbp_state_change_enable == false;

	pp_display_cfg.cpu_cc6_disable =
			context->bw_results.cpuc_state_change_enable == false;

	pp_display_cfg.cpu_pstate_disable =
			context->bw_results.cpup_state_change_enable == false;

	/* TODO: get cpu_pstate_separation_time from BW Calcs. */
	pp_display_cfg.cpu_pstate_separation_time = 0;

	dc_service_pp_post_dce_clock_change(dc->ctx, &pp_display_cfg);
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
				"%s: %d targets",
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

	context = dc_service_alloc(dc->ctx, sizeof(struct validate_context));
	if (context == NULL)
		goto context_alloc_fail;

	result = dc->hwss.validate_with_context(dc, set, target_count, context);
	if (result != DC_OK){
		BREAK_TO_DEBUGGER();
		goto fail;
	}

	if (!dal_adapter_service_is_in_accelerated_mode(
						dc->res_pool.adapter_srv)) {
		dc->hwss.enable_accelerated_mode(context);
	}

	for (i = 0; i < dc->current_context.target_count; i++) {
		/*TODO: optimize this to happen only when necessary*/
		dc_target_disable_memory_requests(
				&dc->current_context.targets[i]->public);
	}

	if (result == DC_OK) {
		dc->hwss.reset_hw_ctx(dc, context, target_count);

		if (context->target_count > 0)
			result = dc->hwss.apply_ctx_to_hw(dc, context);
	}

	for (i = 0; i < context->target_count; i++) {
		struct dc_target *dc_target = &context->targets[i]->public;
		if (context->targets[i]->status.surface_count > 0)
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

	pplib_post_set_mode(dc, context);

	/* TODO: disable unused plls*/
fail:
	dc_service_free(dc->ctx, context);

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
    caps->max_targets = dal_min(dc->res_pool.controller_count, dc->link_count);
    caps->max_links = dc->link_count;
    caps->max_audios = dc->res_pool.audio_count;
}

void dc_flip_surface_addrs(struct dc* dc,
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
		dc->hwss.update_plane_address(
			surface,
			DC_TARGET_TO_CORE(surface->status.dc_target));
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
		dal_error("%s: invalid irq source: %d\n!",__func__, src);
		return NULL;
	}

	for (i = 0; i < dc->current_context.target_count; i++) {
		struct core_target *target = dc->current_context.targets[i];

		struct dc_target *dc_target;

		if (NULL == target) {
			dal_error("%s: 'dc_target' is NULL for irq source: %d\n!",
					__func__, src);
			continue;
		}

		dc_target = &target->public;

		for (j = 0; j < target->public.stream_count; j++) {
			const struct core_stream *stream =
				DC_STREAM_TO_CORE(dc_target->streams[j]);
			const uint8_t controller_idx = stream->controller_idx;

			if (controller_idx == crtc_idx)
				return dc_target;
		}
	}

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

		dc->hwss.power_down(&dc->current_context);
		break;
	}

}

void dc_resume(const struct dc *dc)
{
	uint32_t i;

	for (i = 0; i < dc->link_count; i++)
		core_link_resume(dc->links[i]);
}

void dc_print_sync_report(
	const struct dc *dc)
{
	uint32_t i;
	const struct core_target *core_target;
	const struct core_stream *core_stream;
	struct dc_context *dc_ctx = dc->ctx;
	struct dc_target_sync_report *target_sync_report;
	struct dc_sync_report sync_report = { 0 };

	if (dc->current_context.target_count > MAX_TARGET_NUM) {
		DC_ERROR("Target count: %d > %d!\n",
			dc->current_context.target_count,
			MAX_TARGET_NUM);
		return;
	}

	sync_report.targets_num = dc->current_context.target_count;

	/* Step 1: get data for sync validation */
	for (i = 0; i < dc->current_context.target_count; i++) {

		core_target = dc->current_context.targets[i];
		target_sync_report = &sync_report.trg_reports[i];
		core_stream = DC_STREAM_TO_CORE(core_target->public.streams[0]);

		dc->hwss.get_crtc_positions(
			core_stream->tg,
			&target_sync_report->h_count,
			&target_sync_report->v_count);

		DC_SYNC_INFO("GSL:target[%d]: h: %d\t v: %d\n",
				i,
				target_sync_report->h_count,
				target_sync_report->v_count);
	}

	/* Step 2: validate that display pipes are synchronized (based on
	 * data from Step 1). */
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
	enum dc_status r = core_link_read_dpcd(link, address, data, size);

	return r == DC_OK;
}

bool dc_write_dpcd(
		struct dc *dc,
		uint32_t link_index,
		uint32_t address,
		uint8_t *data,
		uint32_t size)
{
	struct core_link *link =
			DC_LINK_TO_LINK(dc_get_link_at_index(dc, link_index));
	enum dc_status r = core_link_write_dpcd(link, address, data, size);

	return r == DC_OK;
}

bool dc_link_add_sink(
		struct dc_link *link,
		struct dc_sink *sink)
{
	if (link->sink_count >= MAX_SINKS_PER_LINK) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	link->sink[link->sink_count] = sink;
	link->sink_count++;

	return true;
}


void dc_link_remove_sink(struct dc_link *link, const struct dc_sink *sink)
{
	int i;

	if (!link->sink_count) {
		BREAK_TO_DEBUGGER();
		return;
	}

	for (i = 0; i < link->sink_count; i++) {
		if (link->sink[i] == sink) {
			dc_sink_release(sink);
			link->sink[i] = NULL;

			/* shrink array to remove empty place */
			dc_service_memmove(
				&link->sink[i],
				&link->sink[i + 1],
				(link->sink_count - i - 1) *
				sizeof(link->sink[i]));

			link->sink_count--;
			return;
		}
	}

	BREAK_TO_DEBUGGER();
}
