/*
 * Copyright 2019 Advanced Micro Devices, Inc.
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

#include "dc.h"
#include "dc_dmub_srv.h"
#include "../dmub/dmub_srv.h"
#include "dm_helpers.h"
#include "dc_hw_types.h"
#include "core_types.h"
#include "../basics/conversion.h"

#define CTX dc_dmub_srv->ctx
#define DC_LOGGER CTX->logger

static void dc_dmub_srv_construct(struct dc_dmub_srv *dc_srv, struct dc *dc,
				  struct dmub_srv *dmub)
{
	dc_srv->dmub = dmub;
	dc_srv->ctx = dc->ctx;
}

struct dc_dmub_srv *dc_dmub_srv_create(struct dc *dc, struct dmub_srv *dmub)
{
	struct dc_dmub_srv *dc_srv =
		kzalloc(sizeof(struct dc_dmub_srv), GFP_KERNEL);

	if (dc_srv == NULL) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	dc_dmub_srv_construct(dc_srv, dc, dmub);

	return dc_srv;
}

void dc_dmub_srv_destroy(struct dc_dmub_srv **dmub_srv)
{
	if (*dmub_srv) {
		kfree(*dmub_srv);
		*dmub_srv = NULL;
	}
}

void dc_dmub_srv_cmd_queue(struct dc_dmub_srv *dc_dmub_srv,
			   union dmub_rb_cmd *cmd)
{
	struct dmub_srv *dmub = dc_dmub_srv->dmub;
	struct dc_context *dc_ctx = dc_dmub_srv->ctx;
	enum dmub_status status;

	status = dmub_srv_cmd_queue(dmub, cmd);
	if (status == DMUB_STATUS_OK)
		return;

	if (status != DMUB_STATUS_QUEUE_FULL)
		goto error;

	/* Execute and wait for queue to become empty again. */
	dc_dmub_srv_cmd_execute(dc_dmub_srv);
	dc_dmub_srv_wait_idle(dc_dmub_srv);

	/* Requeue the command. */
	status = dmub_srv_cmd_queue(dmub, cmd);
	if (status == DMUB_STATUS_OK)
		return;

error:
	DC_ERROR("Error queuing DMUB command: status=%d\n", status);
	dc_dmub_srv_log_diagnostic_data(dc_dmub_srv);
}

void dc_dmub_srv_cmd_execute(struct dc_dmub_srv *dc_dmub_srv)
{
	struct dmub_srv *dmub = dc_dmub_srv->dmub;
	struct dc_context *dc_ctx = dc_dmub_srv->ctx;
	enum dmub_status status;

	status = dmub_srv_cmd_execute(dmub);
	if (status != DMUB_STATUS_OK) {
		DC_ERROR("Error starting DMUB execution: status=%d\n", status);
		dc_dmub_srv_log_diagnostic_data(dc_dmub_srv);
	}
}

void dc_dmub_srv_wait_idle(struct dc_dmub_srv *dc_dmub_srv)
{
	struct dmub_srv *dmub = dc_dmub_srv->dmub;
	struct dc_context *dc_ctx = dc_dmub_srv->ctx;
	enum dmub_status status;

	status = dmub_srv_wait_for_idle(dmub, 100000);
	if (status != DMUB_STATUS_OK) {
		DC_ERROR("Error waiting for DMUB idle: status=%d\n", status);
		dc_dmub_srv_log_diagnostic_data(dc_dmub_srv);
	}
}

void dc_dmub_srv_clear_inbox0_ack(struct dc_dmub_srv *dmub_srv)
{
	struct dmub_srv *dmub = dmub_srv->dmub;
	struct dc_context *dc_ctx = dmub_srv->ctx;
	enum dmub_status status = DMUB_STATUS_OK;

	status = dmub_srv_clear_inbox0_ack(dmub);
	if (status != DMUB_STATUS_OK) {
		DC_ERROR("Error clearing INBOX0 ack: status=%d\n", status);
		dc_dmub_srv_log_diagnostic_data(dmub_srv);
	}
}

void dc_dmub_srv_wait_for_inbox0_ack(struct dc_dmub_srv *dmub_srv)
{
	struct dmub_srv *dmub = dmub_srv->dmub;
	struct dc_context *dc_ctx = dmub_srv->ctx;
	enum dmub_status status = DMUB_STATUS_OK;

	status = dmub_srv_wait_for_inbox0_ack(dmub, 100000);
	if (status != DMUB_STATUS_OK) {
		DC_ERROR("Error waiting for INBOX0 HW Lock Ack\n");
		dc_dmub_srv_log_diagnostic_data(dmub_srv);
	}
}

void dc_dmub_srv_send_inbox0_cmd(struct dc_dmub_srv *dmub_srv,
		union dmub_inbox0_data_register data)
{
	struct dmub_srv *dmub = dmub_srv->dmub;
	struct dc_context *dc_ctx = dmub_srv->ctx;
	enum dmub_status status = DMUB_STATUS_OK;

	status = dmub_srv_send_inbox0_cmd(dmub, data);
	if (status != DMUB_STATUS_OK) {
		DC_ERROR("Error sending INBOX0 cmd\n");
		dc_dmub_srv_log_diagnostic_data(dmub_srv);
	}
}

bool dc_dmub_srv_cmd_with_reply_data(struct dc_dmub_srv *dc_dmub_srv, union dmub_rb_cmd *cmd)
{
	struct dmub_srv *dmub;
	enum dmub_status status;

	if (!dc_dmub_srv || !dc_dmub_srv->dmub)
		return false;

	dmub = dc_dmub_srv->dmub;

	status = dmub_srv_cmd_with_reply_data(dmub, cmd);
	if (status != DMUB_STATUS_OK) {
		DC_LOG_DEBUG("No reply for DMUB command: status=%d\n", status);
		return false;
	}

	return true;
}

void dc_dmub_srv_wait_phy_init(struct dc_dmub_srv *dc_dmub_srv)
{
	struct dmub_srv *dmub = dc_dmub_srv->dmub;
	struct dc_context *dc_ctx = dc_dmub_srv->ctx;
	enum dmub_status status;

	for (;;) {
		/* Wait up to a second for PHY init. */
		status = dmub_srv_wait_for_phy_init(dmub, 1000000);
		if (status == DMUB_STATUS_OK)
			/* Initialization OK */
			break;

		DC_ERROR("DMCUB PHY init failed: status=%d\n", status);
		ASSERT(0);

		if (status != DMUB_STATUS_TIMEOUT)
			/*
			 * Server likely initialized or we don't have
			 * DMCUB HW support - this won't end.
			 */
			break;

		/* Continue spinning so we don't hang the ASIC. */
	}
}

bool dc_dmub_srv_notify_stream_mask(struct dc_dmub_srv *dc_dmub_srv,
				    unsigned int stream_mask)
{
	struct dmub_srv *dmub;
	const uint32_t timeout = 30;

	if (!dc_dmub_srv || !dc_dmub_srv->dmub)
		return false;

	dmub = dc_dmub_srv->dmub;

	return dmub_srv_send_gpint_command(
		       dmub, DMUB_GPINT__IDLE_OPT_NOTIFY_STREAM_MASK,
		       stream_mask, timeout) == DMUB_STATUS_OK;
}

bool dc_dmub_srv_is_restore_required(struct dc_dmub_srv *dc_dmub_srv)
{
	struct dmub_srv *dmub;
	struct dc_context *dc_ctx;
	union dmub_fw_boot_status boot_status;
	enum dmub_status status;

	if (!dc_dmub_srv || !dc_dmub_srv->dmub)
		return false;

	dmub = dc_dmub_srv->dmub;
	dc_ctx = dc_dmub_srv->ctx;

	status = dmub_srv_get_fw_boot_status(dmub, &boot_status);
	if (status != DMUB_STATUS_OK) {
		DC_ERROR("Error querying DMUB boot status: error=%d\n", status);
		return false;
	}

	return boot_status.bits.restore_required;
}

bool dc_dmub_srv_get_dmub_outbox0_msg(const struct dc *dc, struct dmcub_trace_buf_entry *entry)
{
	struct dmub_srv *dmub = dc->ctx->dmub_srv->dmub;
	return dmub_srv_get_outbox0_msg(dmub, entry);
}

void dc_dmub_trace_event_control(struct dc *dc, bool enable)
{
	dm_helpers_dmub_outbox_interrupt_control(dc->ctx, enable);
}

void dc_dmub_srv_drr_update_cmd(struct dc *dc, uint32_t tg_inst, uint32_t vtotal_min, uint32_t vtotal_max)
{
	union dmub_rb_cmd cmd = { 0 };

	cmd.drr_update.header.type = DMUB_CMD__FW_ASSISTED_MCLK_SWITCH;
	cmd.drr_update.header.sub_type = DMUB_CMD__FAMS_DRR_UPDATE;
	cmd.drr_update.dmub_optc_state_req.v_total_max = vtotal_max;
	cmd.drr_update.dmub_optc_state_req.v_total_min = vtotal_min;
	cmd.drr_update.dmub_optc_state_req.tg_inst = tg_inst;

	cmd.drr_update.header.payload_bytes = sizeof(cmd.drr_update) - sizeof(cmd.drr_update.header);

	// Send the command to the DMCUB.
	dc_dmub_srv_cmd_queue(dc->ctx->dmub_srv, &cmd);
	dc_dmub_srv_cmd_execute(dc->ctx->dmub_srv);
	dc_dmub_srv_wait_idle(dc->ctx->dmub_srv);
}

void dc_dmub_srv_set_drr_manual_trigger_cmd(struct dc *dc, uint32_t tg_inst)
{
	union dmub_rb_cmd cmd = { 0 };

	cmd.drr_update.header.type = DMUB_CMD__FW_ASSISTED_MCLK_SWITCH;
	cmd.drr_update.header.sub_type = DMUB_CMD__FAMS_SET_MANUAL_TRIGGER;
	cmd.drr_update.dmub_optc_state_req.tg_inst = tg_inst;

	cmd.drr_update.header.payload_bytes = sizeof(cmd.drr_update) - sizeof(cmd.drr_update.header);

	// Send the command to the DMCUB.
	dc_dmub_srv_cmd_queue(dc->ctx->dmub_srv, &cmd);
	dc_dmub_srv_cmd_execute(dc->ctx->dmub_srv);
	dc_dmub_srv_wait_idle(dc->ctx->dmub_srv);
}

static uint8_t dc_dmub_srv_get_pipes_for_stream(struct dc *dc, struct dc_stream_state *stream)
{
	uint8_t pipes = 0;
	int i = 0;

	for (i = 0; i < MAX_PIPES; i++) {
		struct pipe_ctx *pipe = &dc->current_state->res_ctx.pipe_ctx[i];

		if (pipe->stream == stream && pipe->stream_res.tg)
			pipes = i;
	}
	return pipes;
}

static int dc_dmub_srv_get_timing_generator_offset(struct dc *dc, struct dc_stream_state *stream)
{
	int  tg_inst = 0;
	int i = 0;

	for (i = 0; i < MAX_PIPES; i++) {
		struct pipe_ctx *pipe = &dc->current_state->res_ctx.pipe_ctx[i];

		if (pipe->stream == stream && pipe->stream_res.tg) {
			tg_inst = pipe->stream_res.tg->inst;
			break;
		}
	}
	return tg_inst;
}

bool dc_dmub_srv_p_state_delegate(struct dc *dc, bool should_manage_pstate, struct dc_state *context)
{
	union dmub_rb_cmd cmd = { 0 };
	struct dmub_cmd_fw_assisted_mclk_switch_config *config_data = &cmd.fw_assisted_mclk_switch.config_data;
	int i = 0;
	int ramp_up_num_steps = 1; // TODO: Ramp is currently disabled. Reenable it.
	uint8_t visual_confirm_enabled = dc->debug.visual_confirm == VISUAL_CONFIRM_FAMS;

	if (dc == NULL)
		return false;

	// Format command.
	cmd.fw_assisted_mclk_switch.header.type = DMUB_CMD__FW_ASSISTED_MCLK_SWITCH;
	cmd.fw_assisted_mclk_switch.header.sub_type = DMUB_CMD__FAMS_SETUP_FW_CTRL;
	cmd.fw_assisted_mclk_switch.config_data.fams_enabled = should_manage_pstate;
	cmd.fw_assisted_mclk_switch.config_data.visual_confirm_enabled = visual_confirm_enabled;

	for (i = 0; context && i < context->stream_count; i++) {
		struct dc_stream_state *stream = context->streams[i];
		uint8_t min_refresh_in_hz = (stream->timing.min_refresh_in_uhz + 999999) / 1000000;
		int  tg_inst = dc_dmub_srv_get_timing_generator_offset(dc, stream);

		config_data->pipe_data[tg_inst].pix_clk_100hz = stream->timing.pix_clk_100hz;
		config_data->pipe_data[tg_inst].min_refresh_in_hz = min_refresh_in_hz;
		config_data->pipe_data[tg_inst].max_ramp_step = ramp_up_num_steps;
		config_data->pipe_data[tg_inst].pipes = dc_dmub_srv_get_pipes_for_stream(dc, stream);
	}

	cmd.fw_assisted_mclk_switch.header.payload_bytes =
		sizeof(cmd.fw_assisted_mclk_switch) - sizeof(cmd.fw_assisted_mclk_switch.header);

	// Send the command to the DMCUB.
	dc_dmub_srv_cmd_queue(dc->ctx->dmub_srv, &cmd);
	dc_dmub_srv_cmd_execute(dc->ctx->dmub_srv);
	dc_dmub_srv_wait_idle(dc->ctx->dmub_srv);

	return true;
}

void dc_dmub_srv_query_caps_cmd(struct dmub_srv *dmub)
{
	union dmub_rb_cmd cmd = { 0 };
	enum dmub_status status;

	if (!dmub) {
		return;
	}

	memset(&cmd, 0, sizeof(cmd));

	/* Prepare fw command */
	cmd.query_feature_caps.header.type = DMUB_CMD__QUERY_FEATURE_CAPS;
	cmd.query_feature_caps.header.sub_type = 0;
	cmd.query_feature_caps.header.ret_status = 1;
	cmd.query_feature_caps.header.payload_bytes = sizeof(struct dmub_cmd_query_feature_caps_data);

	/* Send command to fw */
	status = dmub_srv_cmd_with_reply_data(dmub, &cmd);

	ASSERT(status == DMUB_STATUS_OK);

	/* If command was processed, copy feature caps to dmub srv */
	if (status == DMUB_STATUS_OK &&
	    cmd.query_feature_caps.header.ret_status == 0) {
		memcpy(&dmub->feature_caps,
		       &cmd.query_feature_caps.query_feature_caps_data,
		       sizeof(struct dmub_feature_caps));
	}
}

#ifdef CONFIG_DRM_AMD_DC_DCN
/**
 * ***********************************************************************************************
 * populate_subvp_cmd_drr_info: Helper to populate DRR pipe info for the DMCUB subvp command
 *
 * Populate the DMCUB SubVP command with DRR pipe info. All the information required for calculating
 * the SubVP + DRR microschedule is populated here.
 *
 * High level algorithm:
 * 1. Get timing for SubVP pipe, phantom pipe, and DRR pipe
 * 2. Calculate the min and max vtotal which supports SubVP + DRR microschedule
 * 3. Populate the drr_info with the min and max supported vtotal values
 *
 * @param [in] dc: current dc state
 * @param [in] subvp_pipe: pipe_ctx for the SubVP pipe
 * @param [in] vblank_pipe: pipe_ctx for the DRR pipe
 * @param [in] pipe_data: Pipe data which stores the VBLANK/DRR info
 *
 * @return: void
 *
 * ***********************************************************************************************
 */
static void populate_subvp_cmd_drr_info(struct dc *dc,
		struct pipe_ctx *subvp_pipe,
		struct pipe_ctx *vblank_pipe,
		struct dmub_cmd_fw_assisted_mclk_switch_pipe_data_v2 *pipe_data)
{
	struct dc_crtc_timing *main_timing = &subvp_pipe->stream->timing;
	struct dc_crtc_timing *phantom_timing = &subvp_pipe->stream->mall_stream_config.paired_stream->timing;
	struct dc_crtc_timing *drr_timing = &vblank_pipe->stream->timing;
	int16_t drr_frame_us = 0;
	int16_t min_drr_supported_us = 0;
	int16_t max_drr_supported_us = 0;
	int16_t max_drr_vblank_us = 0;
	int16_t max_drr_mallregion_us = 0;
	int16_t mall_region_us = 0;
	int16_t prefetch_us = 0;
	int16_t subvp_active_us = 0;
	int16_t drr_active_us = 0;
	int16_t min_vtotal_supported = 0;
	int16_t max_vtotal_supported = 0;

	pipe_data->pipe_config.vblank_data.drr_info.drr_in_use = true;
	pipe_data->pipe_config.vblank_data.drr_info.use_ramping = false; // for now don't use ramping
	pipe_data->pipe_config.vblank_data.drr_info.drr_window_size_ms = 4; // hardcode 4ms DRR window for now

	drr_frame_us = div64_s64(drr_timing->v_total * drr_timing->h_total,
				 (int64_t)(drr_timing->pix_clk_100hz * 100) * 1000000);
	// P-State allow width and FW delays already included phantom_timing->v_addressable
	mall_region_us = div64_s64(phantom_timing->v_addressable * phantom_timing->h_total,
				   (int64_t)(phantom_timing->pix_clk_100hz * 100) * 1000000);
	min_drr_supported_us = drr_frame_us + mall_region_us + SUBVP_DRR_MARGIN_US;
	min_vtotal_supported = div64_s64(drr_timing->pix_clk_100hz * 100 *
					 (div64_s64((int64_t)min_drr_supported_us, 1000000)),
					 (int64_t)drr_timing->h_total);

	prefetch_us = div64_s64((phantom_timing->v_total - phantom_timing->v_front_porch) * phantom_timing->h_total,
				(int64_t)(phantom_timing->pix_clk_100hz * 100) * 1000000 +
				dc->caps.subvp_prefetch_end_to_mall_start_us);
	subvp_active_us = div64_s64(main_timing->v_addressable * main_timing->h_total,
				    (int64_t)(main_timing->pix_clk_100hz * 100) * 1000000);
	drr_active_us = div64_s64(drr_timing->v_addressable * drr_timing->h_total,
				  (int64_t)(drr_timing->pix_clk_100hz * 100) * 1000000);
	max_drr_vblank_us = div64_s64((int64_t)(subvp_active_us - prefetch_us - drr_active_us), 2) + drr_active_us;
	max_drr_mallregion_us = subvp_active_us - prefetch_us - mall_region_us;
	max_drr_supported_us = max_drr_vblank_us > max_drr_mallregion_us ? max_drr_vblank_us : max_drr_mallregion_us;
	max_vtotal_supported = div64_s64(drr_timing->pix_clk_100hz * 100 * (div64_s64((int64_t)max_drr_supported_us, 1000000)),
					 (int64_t)drr_timing->h_total);

	pipe_data->pipe_config.vblank_data.drr_info.min_vtotal_supported = min_vtotal_supported;
	pipe_data->pipe_config.vblank_data.drr_info.max_vtotal_supported = max_vtotal_supported;
}

/**
 * ***********************************************************************************************
 * populate_subvp_cmd_vblank_pipe_info: Helper to populate VBLANK pipe info for the DMUB subvp command
 *
 * Populate the DMCUB SubVP command with VBLANK pipe info. All the information required to calculate
 * the microschedule for SubVP + VBLANK case is stored in the pipe_data (subvp_data and vblank_data).
 * Also check if the VBLANK pipe is a DRR display -- if it is make a call to populate drr_info.
 *
 * @param [in] dc: current dc state
 * @param [in] context: new dc state
 * @param [in] cmd: DMUB cmd to be populated with SubVP info
 * @param [in] vblank_pipe: pipe_ctx for the VBLANK pipe
 * @param [in] cmd_pipe_index: index for the pipe array in DMCUB SubVP cmd
 *
 * @return: void
 *
 * ***********************************************************************************************
 */
static void populate_subvp_cmd_vblank_pipe_info(struct dc *dc,
		struct dc_state *context,
		union dmub_rb_cmd *cmd,
		struct pipe_ctx *vblank_pipe,
		uint8_t cmd_pipe_index)
{
	uint32_t i;
	struct pipe_ctx *pipe = NULL;
	struct dmub_cmd_fw_assisted_mclk_switch_pipe_data_v2 *pipe_data =
			&cmd->fw_assisted_mclk_switch_v2.config_data.pipe_data[cmd_pipe_index];

	// Find the SubVP pipe
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		pipe = &context->res_ctx.pipe_ctx[i];

		// We check for master pipe, but it shouldn't matter since we only need
		// the pipe for timing info (stream should be same for any pipe splits)
		if (!pipe->stream || !pipe->plane_state || pipe->top_pipe || pipe->prev_odm_pipe)
			continue;

		// Find the SubVP pipe
		if (pipe->stream->mall_stream_config.type == SUBVP_MAIN)
			break;
	}

	pipe_data->mode = VBLANK;
	pipe_data->pipe_config.vblank_data.pix_clk_100hz = vblank_pipe->stream->timing.pix_clk_100hz;
	pipe_data->pipe_config.vblank_data.vblank_start = vblank_pipe->stream->timing.v_total -
							vblank_pipe->stream->timing.v_front_porch;
	pipe_data->pipe_config.vblank_data.vtotal = vblank_pipe->stream->timing.v_total;
	pipe_data->pipe_config.vblank_data.htotal = vblank_pipe->stream->timing.h_total;
	pipe_data->pipe_config.vblank_data.vblank_pipe_index = vblank_pipe->pipe_idx;
	pipe_data->pipe_config.vblank_data.vstartup_start = vblank_pipe->pipe_dlg_param.vstartup_start;
	pipe_data->pipe_config.vblank_data.vblank_end =
			vblank_pipe->stream->timing.v_total - vblank_pipe->stream->timing.v_front_porch - vblank_pipe->stream->timing.v_addressable;

	if (vblank_pipe->stream->ignore_msa_timing_param)
		populate_subvp_cmd_drr_info(dc, pipe, vblank_pipe, pipe_data);
}

/**
 * ***********************************************************************************************
 * update_subvp_prefetch_end_to_mall_start: Helper for SubVP + SubVP case
 *
 * For SubVP + SubVP, we use a single vertical interrupt to start the microschedule for both
 * SubVP pipes. In order for this to work correctly, the MALL REGION of both SubVP pipes must
 * start at the same time. This function lengthens the prefetch end to mall start delay of the
 * SubVP pipe that has the shorter prefetch so that both MALL REGION's will start at the same time.
 *
 * @param [in] dc: current dc state
 * @param [in] context: new dc state
 * @param [in] cmd: DMUB cmd to be populated with SubVP info
 * @param [in] subvp_pipes: Array of SubVP pipes (should always be length 2)
 *
 * @return: void
 *
 * ***********************************************************************************************
 */
static void update_subvp_prefetch_end_to_mall_start(struct dc *dc,
		struct dc_state *context,
		union dmub_rb_cmd *cmd,
		struct pipe_ctx *subvp_pipes[])
{
	uint32_t subvp0_prefetch_us = 0;
	uint32_t subvp1_prefetch_us = 0;
	uint32_t prefetch_delta_us = 0;
	struct dc_crtc_timing *phantom_timing0 = &subvp_pipes[0]->stream->mall_stream_config.paired_stream->timing;
	struct dc_crtc_timing *phantom_timing1 = &subvp_pipes[1]->stream->mall_stream_config.paired_stream->timing;
	struct dmub_cmd_fw_assisted_mclk_switch_pipe_data_v2 *pipe_data = NULL;

	subvp0_prefetch_us = div64_s64((phantom_timing0->v_total - phantom_timing0->v_front_porch) * phantom_timing0->h_total,
				       (int64_t)(phantom_timing0->pix_clk_100hz * 100) * 1000000 + dc->caps.subvp_prefetch_end_to_mall_start_us);
	subvp1_prefetch_us = div64_s64((phantom_timing1->v_total - phantom_timing1->v_front_porch) * phantom_timing1->h_total,
				       (int64_t)(phantom_timing1->pix_clk_100hz * 100) * 1000000 + dc->caps.subvp_prefetch_end_to_mall_start_us);

	// Whichever SubVP PIPE has the smaller prefetch (including the prefetch end to mall start time)
	// should increase it's prefetch time to match the other
	if (subvp0_prefetch_us > subvp1_prefetch_us) {
		pipe_data = &cmd->fw_assisted_mclk_switch_v2.config_data.pipe_data[1];
		prefetch_delta_us = subvp0_prefetch_us - subvp1_prefetch_us;
		pipe_data->pipe_config.subvp_data.prefetch_to_mall_start_lines =
			div64_s64(((div64_s64((int64_t)(dc->caps.subvp_prefetch_end_to_mall_start_us + prefetch_delta_us), 1000000)) *
				   (phantom_timing1->pix_clk_100hz * 100) + phantom_timing1->h_total - 1),
				  (int64_t)phantom_timing1->h_total);
	} else if (subvp1_prefetch_us >  subvp0_prefetch_us) {
		pipe_data = &cmd->fw_assisted_mclk_switch_v2.config_data.pipe_data[0];
		prefetch_delta_us = subvp1_prefetch_us - subvp0_prefetch_us;
		pipe_data->pipe_config.subvp_data.prefetch_to_mall_start_lines =
			div64_s64(((div64_s64((int64_t)(dc->caps.subvp_prefetch_end_to_mall_start_us + prefetch_delta_us), 1000000)) *
				   (phantom_timing0->pix_clk_100hz * 100) + phantom_timing0->h_total - 1),
				  (int64_t)phantom_timing0->h_total);
	}
}

/**
 * ***************************************************************************************
 * setup_subvp_dmub_command: Helper to populate the SubVP pipe info for the DMUB subvp command
 *
 * Populate the DMCUB SubVP command with SubVP pipe info. All the information required to
 * calculate the microschedule for the SubVP pipe is stored in the pipe_data of the DMCUB
 * SubVP command.
 *
 * @param [in] dc: current dc state
 * @param [in] context: new dc state
 * @param [in] cmd: DMUB cmd to be populated with SubVP info
 * @param [in] subvp_pipe: pipe_ctx for the SubVP pipe
 * @param [in] cmd_pipe_index: index for the pipe array in DMCUB SubVP cmd
 *
 * @return: void
 *
 * ***************************************************************************************
 */
static void populate_subvp_cmd_pipe_info(struct dc *dc,
		struct dc_state *context,
		union dmub_rb_cmd *cmd,
		struct pipe_ctx *subvp_pipe,
		uint8_t cmd_pipe_index)
{
	uint32_t j;
	struct dmub_cmd_fw_assisted_mclk_switch_pipe_data_v2 *pipe_data =
			&cmd->fw_assisted_mclk_switch_v2.config_data.pipe_data[cmd_pipe_index];
	struct dc_crtc_timing *main_timing = &subvp_pipe->stream->timing;
	struct dc_crtc_timing *phantom_timing = &subvp_pipe->stream->mall_stream_config.paired_stream->timing;
	uint32_t out_num, out_den;

	pipe_data->mode = SUBVP;
	pipe_data->pipe_config.subvp_data.pix_clk_100hz = subvp_pipe->stream->timing.pix_clk_100hz;
	pipe_data->pipe_config.subvp_data.htotal = subvp_pipe->stream->timing.h_total;
	pipe_data->pipe_config.subvp_data.vtotal = subvp_pipe->stream->timing.v_total;
	pipe_data->pipe_config.subvp_data.main_vblank_start =
			main_timing->v_total - main_timing->v_front_porch;
	pipe_data->pipe_config.subvp_data.main_vblank_end =
			main_timing->v_total - main_timing->v_front_porch - main_timing->v_addressable;
	pipe_data->pipe_config.subvp_data.mall_region_lines = phantom_timing->v_addressable;
	pipe_data->pipe_config.subvp_data.main_pipe_index = subvp_pipe->pipe_idx;
	pipe_data->pipe_config.subvp_data.is_drr = subvp_pipe->stream->ignore_msa_timing_param;

	/* Calculate the scaling factor from the src and dst height.
	 * e.g. If 3840x2160 being downscaled to 1920x1080, the scaling factor is 1/2.
	 * Reduce the fraction 1080/2160 = 1/2 for the "scaling factor"
	 */
	reduce_fraction(subvp_pipe->stream->src.height, subvp_pipe->stream->dst.height, &out_num, &out_den);
	// TODO: Uncomment below lines once DMCUB include headers are promoted
	//pipe_data->pipe_config.subvp_data.scale_factor_numerator = out_num;
	//pipe_data->pipe_config.subvp_data.scale_factor_denominator = out_den;

	// Prefetch lines is equal to VACTIVE + BP + VSYNC
	pipe_data->pipe_config.subvp_data.prefetch_lines =
			phantom_timing->v_total - phantom_timing->v_front_porch;

	// Round up
	pipe_data->pipe_config.subvp_data.prefetch_to_mall_start_lines =
		div64_s64(((div64_s64((int64_t)dc->caps.subvp_prefetch_end_to_mall_start_us, 1000000)) *
			   (phantom_timing->pix_clk_100hz * 100) + phantom_timing->h_total - 1),
			  (int64_t)phantom_timing->h_total);
	pipe_data->pipe_config.subvp_data.processing_delay_lines =
		div64_s64(((div64_s64((int64_t)dc->caps.subvp_fw_processing_delay_us, 1000000)) *
			   (phantom_timing->pix_clk_100hz * 100) + phantom_timing->h_total - 1),
			  (int64_t)phantom_timing->h_total);
	// Find phantom pipe index based on phantom stream
	for (j = 0; j < dc->res_pool->pipe_count; j++) {
		struct pipe_ctx *phantom_pipe = &context->res_ctx.pipe_ctx[j];

		if (phantom_pipe->stream == subvp_pipe->stream->mall_stream_config.paired_stream) {
			pipe_data->pipe_config.subvp_data.phantom_pipe_index = phantom_pipe->pipe_idx;
			break;
		}
	}
}

/**
 * ***************************************************************************************
 * dc_dmub_setup_subvp_dmub_command: Populate the DMCUB SubVP command
 *
 * This function loops through each pipe and populates the DMUB
 * SubVP CMD info based on the pipe (e.g. SubVP, VBLANK).
 *
 * @param [in] dc: current dc state
 * @param [in] context: new dc state
 * @param [in] cmd: DMUB cmd to be populated with SubVP info
 *
 * @return: void
 *
 * ***************************************************************************************
 */
void dc_dmub_setup_subvp_dmub_command(struct dc *dc,
		struct dc_state *context,
		bool enable)
{
	uint8_t cmd_pipe_index = 0;
	uint32_t i, pipe_idx;
	uint8_t subvp_count = 0;
	union dmub_rb_cmd cmd;
	struct pipe_ctx *subvp_pipes[2];
	uint32_t wm_val_refclk = 0;

	memset(&cmd, 0, sizeof(cmd));
	// FW command for SUBVP
	cmd.fw_assisted_mclk_switch_v2.header.type = DMUB_CMD__FW_ASSISTED_MCLK_SWITCH;
	cmd.fw_assisted_mclk_switch_v2.header.sub_type = DMUB_CMD__HANDLE_SUBVP_CMD;
	cmd.fw_assisted_mclk_switch_v2.header.payload_bytes =
			sizeof(cmd.fw_assisted_mclk_switch_v2) - sizeof(cmd.fw_assisted_mclk_switch_v2.header);

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

		if (!pipe->stream)
			continue;

		if (pipe->plane_state && !pipe->top_pipe &&
				pipe->stream->mall_stream_config.type == SUBVP_MAIN)
			subvp_pipes[subvp_count++] = pipe;
	}

	if (enable) {
		// For each pipe that is a "main" SUBVP pipe, fill in pipe data for DMUB SUBVP cmd
		for (i = 0, pipe_idx = 0; i < dc->res_pool->pipe_count; i++) {
			struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

			if (!pipe->stream)
				continue;

			if (pipe->plane_state && pipe->stream->mall_stream_config.paired_stream &&
					pipe->stream->mall_stream_config.type == SUBVP_MAIN) {
				populate_subvp_cmd_pipe_info(dc, context, &cmd, pipe, cmd_pipe_index++);
			} else if (pipe->plane_state && pipe->stream->mall_stream_config.type == SUBVP_NONE) {
				// Don't need to check for ActiveDRAMClockChangeMargin < 0, not valid in cases where
				// we run through DML without calculating "natural" P-state support
				populate_subvp_cmd_vblank_pipe_info(dc, context, &cmd, pipe, cmd_pipe_index++);

			}
			pipe_idx++;
		}
		if (subvp_count == 2) {
			update_subvp_prefetch_end_to_mall_start(dc, context, &cmd, subvp_pipes);
		}
		cmd.fw_assisted_mclk_switch_v2.config_data.pstate_allow_width_us = dc->caps.subvp_pstate_allow_width_us;
		cmd.fw_assisted_mclk_switch_v2.config_data.vertical_int_margin_us = dc->caps.subvp_vertical_int_margin_us;

		// Store the original watermark value for this SubVP config so we can lower it when the
		// MCLK switch starts
		wm_val_refclk = context->bw_ctx.bw.dcn.watermarks.a.cstate_pstate.pstate_change_ns *
				dc->res_pool->ref_clocks.dchub_ref_clock_inKhz / 1000 / 1000;

		cmd.fw_assisted_mclk_switch_v2.config_data.watermark_a_cache = wm_val_refclk < 0xFFFF ? wm_val_refclk : 0xFFFF;
	}
	dc_dmub_srv_cmd_queue(dc->ctx->dmub_srv, &cmd);
	dc_dmub_srv_cmd_execute(dc->ctx->dmub_srv);
	dc_dmub_srv_wait_idle(dc->ctx->dmub_srv);
}
#endif

bool dc_dmub_srv_get_diagnostic_data(struct dc_dmub_srv *dc_dmub_srv, struct dmub_diagnostic_data *diag_data)
{
	if (!dc_dmub_srv || !dc_dmub_srv->dmub || !diag_data)
		return false;
	return dmub_srv_get_diagnostic_data(dc_dmub_srv->dmub, diag_data);
}

void dc_dmub_srv_log_diagnostic_data(struct dc_dmub_srv *dc_dmub_srv)
{
	struct dmub_diagnostic_data diag_data = {0};

	if (!dc_dmub_srv || !dc_dmub_srv->dmub) {
		DC_LOG_ERROR("%s: invalid parameters.", __func__);
		return;
	}

	if (!dc_dmub_srv_get_diagnostic_data(dc_dmub_srv, &diag_data)) {
		DC_LOG_ERROR("%s: dc_dmub_srv_get_diagnostic_data failed.", __func__);
		return;
	}

	DC_LOG_DEBUG(
		"DMCUB STATE\n"
		"    dmcub_version      : %08x\n"
		"    scratch  [0]       : %08x\n"
		"    scratch  [1]       : %08x\n"
		"    scratch  [2]       : %08x\n"
		"    scratch  [3]       : %08x\n"
		"    scratch  [4]       : %08x\n"
		"    scratch  [5]       : %08x\n"
		"    scratch  [6]       : %08x\n"
		"    scratch  [7]       : %08x\n"
		"    scratch  [8]       : %08x\n"
		"    scratch  [9]       : %08x\n"
		"    scratch [10]       : %08x\n"
		"    scratch [11]       : %08x\n"
		"    scratch [12]       : %08x\n"
		"    scratch [13]       : %08x\n"
		"    scratch [14]       : %08x\n"
		"    scratch [15]       : %08x\n"
		"    pc                 : %08x\n"
		"    unk_fault_addr     : %08x\n"
		"    inst_fault_addr    : %08x\n"
		"    data_fault_addr    : %08x\n"
		"    inbox1_rptr        : %08x\n"
		"    inbox1_wptr        : %08x\n"
		"    inbox1_size        : %08x\n"
		"    inbox0_rptr        : %08x\n"
		"    inbox0_wptr        : %08x\n"
		"    inbox0_size        : %08x\n"
		"    is_enabled         : %d\n"
		"    is_soft_reset      : %d\n"
		"    is_secure_reset    : %d\n"
		"    is_traceport_en    : %d\n"
		"    is_cw0_en          : %d\n"
		"    is_cw6_en          : %d\n",
		diag_data.dmcub_version,
		diag_data.scratch[0],
		diag_data.scratch[1],
		diag_data.scratch[2],
		diag_data.scratch[3],
		diag_data.scratch[4],
		diag_data.scratch[5],
		diag_data.scratch[6],
		diag_data.scratch[7],
		diag_data.scratch[8],
		diag_data.scratch[9],
		diag_data.scratch[10],
		diag_data.scratch[11],
		diag_data.scratch[12],
		diag_data.scratch[13],
		diag_data.scratch[14],
		diag_data.scratch[15],
		diag_data.pc,
		diag_data.undefined_address_fault_addr,
		diag_data.inst_fetch_fault_addr,
		diag_data.data_write_fault_addr,
		diag_data.inbox1_rptr,
		diag_data.inbox1_wptr,
		diag_data.inbox1_size,
		diag_data.inbox0_rptr,
		diag_data.inbox0_wptr,
		diag_data.inbox0_size,
		diag_data.is_dmcub_enabled,
		diag_data.is_dmcub_soft_reset,
		diag_data.is_dmcub_secure_reset,
		diag_data.is_traceport_en,
		diag_data.is_cw0_enabled,
		diag_data.is_cw6_enabled);
}
