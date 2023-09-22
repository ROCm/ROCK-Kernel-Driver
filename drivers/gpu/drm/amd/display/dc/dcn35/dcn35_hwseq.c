/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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
 */

#include "dm_services.h"
#include "dm_helpers.h"
#include "core_types.h"
#include "resource.h"
#include "dccg.h"
#include "dce/dce_hwseq.h"
#include "clk_mgr.h"
#include "reg_helper.h"
#include "abm.h"
#include "hubp.h"
#include "dchubbub.h"
#include "timing_generator.h"
#include "opp.h"
#include "ipp.h"
#include "mpc.h"
#include "mcif_wb.h"
#include "dc_dmub_srv.h"
#include "dcn35_hwseq.h"
#include "dcn35/dcn35_dccg.h"
#include "link_hwss.h"
#include "dpcd_defs.h"
#include "dce/dmub_outbox.h"
#include "link.h"
#include "dcn10/dcn10_hw_sequencer.h"
#include "inc/link_enc_cfg.h"
#include "dcn30/dcn30_vpg.h"
#include "dce/dce_i2c_hw.h"
#include "dsc.h"
#include "dcn20/dcn20_optc.h"
#include "dcn30/dcn30_cm_common.h"
#include "dcn31/dcn31_hwseq.h"
#include "dcn20/dcn20_hwseq.h"

#define DC_LOGGER_INIT(logger)

#define CTX \
	hws->ctx
#define REG(reg)\
	hws->regs->reg
#define DC_LOGGER \
		dc->ctx->logger


#undef FN
#define FN(reg_name, field_name) \
	hws->shifts->field_name, hws->masks->field_name
#if 0
static void enable_memory_low_power(struct dc *dc)
{
	struct dce_hwseq *hws = dc->hwseq;
	int i;

	if (dc->debug.enable_mem_low_power.bits.dmcu) {
		// Force ERAM to shutdown if DMCU is not enabled
		if (dc->debug.disable_dmcu || dc->config.disable_dmcu) {
			REG_UPDATE(DMU_MEM_PWR_CNTL, DMCU_ERAM_MEM_PWR_FORCE, 3);
		}
	}
	/*dcn35 has default MEM_PWR enabled, make sure wake them up*/
	// Set default OPTC memory power states
	if (dc->debug.enable_mem_low_power.bits.optc) {
		// Shutdown when unassigned and light sleep in VBLANK
		REG_SET_2(ODM_MEM_PWR_CTRL3, 0, ODM_MEM_UNASSIGNED_PWR_MODE, 3, ODM_MEM_VBLANK_PWR_MODE, 1);
	}

	if (dc->debug.enable_mem_low_power.bits.vga) {
		// Power down VGA memory
		REG_UPDATE(MMHUBBUB_MEM_PWR_CNTL, VGA_MEM_PWR_FORCE, 1);
	}

	if (dc->debug.enable_mem_low_power.bits.mpc &&
		dc->res_pool->mpc->funcs->set_mpc_mem_lp_mode)
		dc->res_pool->mpc->funcs->set_mpc_mem_lp_mode(dc->res_pool->mpc);

	if (dc->debug.enable_mem_low_power.bits.vpg && dc->res_pool->stream_enc[0]->vpg->funcs->vpg_powerdown) {
		// Power down VPGs
		for (i = 0; i < dc->res_pool->stream_enc_count; i++)
			dc->res_pool->stream_enc[i]->vpg->funcs->vpg_powerdown(dc->res_pool->stream_enc[i]->vpg);
#if defined(CONFIG_DRM_AMD_DC_DP2_0)
		for (i = 0; i < dc->res_pool->hpo_dp_stream_enc_count; i++)
			dc->res_pool->hpo_dp_stream_enc[i]->vpg->funcs->vpg_powerdown(dc->res_pool->hpo_dp_stream_enc[i]->vpg);
#endif
	}

}
#endif

void dcn35_set_dmu_fgcg(struct dce_hwseq *hws, bool enable)
{
	REG_UPDATE_3(DMU_CLK_CNTL,
		RBBMIF_FGCG_REP_DIS, !enable,
		IHC_FGCG_REP_DIS, !enable,
		LONO_FGCG_REP_DIS, !enable
	);
}

void dcn35_setup_hpo_hw_control(const struct dce_hwseq *hws, bool enable)
{
	REG_UPDATE(HPO_TOP_HW_CONTROL, HPO_IO_EN, !!enable);
}

void dcn35_init_hw(struct dc *dc)
{
	struct abm **abms = dc->res_pool->multiple_abms;
	struct dce_hwseq *hws = dc->hwseq;
	struct dc_bios *dcb = dc->ctx->dc_bios;
	struct resource_pool *res_pool = dc->res_pool;
	uint32_t backlight = MAX_BACKLIGHT_LEVEL;
	int i;

	if (dc->clk_mgr && dc->clk_mgr->funcs->init_clocks)
		dc->clk_mgr->funcs->init_clocks(dc->clk_mgr);

	REG_WRITE(DCCG_GATE_DISABLE_CNTL, 0);
	REG_WRITE(DCCG_GATE_DISABLE_CNTL2, 0x3F000000);
	REG_WRITE(DCCG_GATE_DISABLE_CNTL5, 0x1f7c3fcf);

	//dcn35_set_dmu_fgcg(hws, dc->debug.enable_fine_grain_clock_gating.bits.dmu);

	if (!dcb->funcs->is_accelerated_mode(dcb)) {
		/*this calls into dmubfw to do the init*/
		hws->funcs.bios_golden_init(dc);
	}
	// Initialize the dccg
	if (res_pool->dccg->funcs->dccg_init)
		res_pool->dccg->funcs->dccg_init(res_pool->dccg);

	//enable_memory_low_power(dc);

	if (dc->ctx->dc_bios->fw_info_valid) {
		res_pool->ref_clocks.xtalin_clock_inKhz =
				dc->ctx->dc_bios->fw_info.pll_info.crystal_frequency;

		if (res_pool->dccg && res_pool->hubbub) {

			(res_pool->dccg->funcs->get_dccg_ref_freq)(res_pool->dccg,
				dc->ctx->dc_bios->fw_info.pll_info.crystal_frequency,
				&res_pool->ref_clocks.dccg_ref_clock_inKhz);

			(res_pool->hubbub->funcs->get_dchub_ref_freq)(res_pool->hubbub,
				res_pool->ref_clocks.dccg_ref_clock_inKhz,
				&res_pool->ref_clocks.dchub_ref_clock_inKhz);
		} else {
			// Not all ASICs have DCCG sw component
			res_pool->ref_clocks.dccg_ref_clock_inKhz =
				res_pool->ref_clocks.xtalin_clock_inKhz;
			res_pool->ref_clocks.dchub_ref_clock_inKhz =
				res_pool->ref_clocks.xtalin_clock_inKhz;
		}
	} else
		ASSERT_CRITICAL(false);

	for (i = 0; i < dc->link_count; i++) {
		/* Power up AND update implementation according to the
		 * required signal (which may be different from the
		 * default signal on connector).
		 */
		struct dc_link *link = dc->links[i];

		if (link->ep_type != DISPLAY_ENDPOINT_PHY)
			continue;

		link->link_enc->funcs->hw_init(link->link_enc);

		/* Check for enabled DIG to identify enabled display */
		if (link->link_enc->funcs->is_dig_enabled &&
			link->link_enc->funcs->is_dig_enabled(link->link_enc)) {
			link->link_status.link_active = true;
			if (link->link_enc->funcs->fec_is_active &&
					link->link_enc->funcs->fec_is_active(link->link_enc))
				link->fec_state = dc_link_fec_enabled;
		}
	}

	/* we want to turn off all dp displays before doing detection */
	dc->link_srv->blank_all_dp_displays(dc);
/*
	if (hws->funcs.enable_power_gating_plane)
		hws->funcs.enable_power_gating_plane(dc->hwseq, true);
*/
	if (res_pool->hubbub->funcs->dchubbub_init)
		res_pool->hubbub->funcs->dchubbub_init(dc->res_pool->hubbub);
	/* If taking control over from VBIOS, we may want to optimize our first
	 * mode set, so we need to skip powering down pipes until we know which
	 * pipes we want to use.
	 * Otherwise, if taking control is not possible, we need to power
	 * everything down.
	 */
	if (dcb->funcs->is_accelerated_mode(dcb) || !dc->config.seamless_boot_edp_requested) {

		// we want to turn off edp displays if odm is enabled and no seamless boot
		if (!dc->caps.seamless_odm) {
			for (i = 0; i < dc->res_pool->timing_generator_count; i++) {
				struct timing_generator *tg = dc->res_pool->timing_generators[i];
				uint32_t num_opps, opp_id_src0, opp_id_src1;

				num_opps = 1;
				if (tg) {
					if (tg->funcs->is_tg_enabled(tg) && tg->funcs->get_optc_source) {
						tg->funcs->get_optc_source(tg, &num_opps,
								&opp_id_src0, &opp_id_src1);
					}
				}

				if (num_opps > 1) {
					dc->link_srv->blank_all_edp_displays(dc);
					break;
				}
			}
		}

		hws->funcs.init_pipes(dc, dc->current_state);
		if (dc->res_pool->hubbub->funcs->allow_self_refresh_control)
			dc->res_pool->hubbub->funcs->allow_self_refresh_control(dc->res_pool->hubbub,
					!dc->res_pool->hubbub->ctx->dc->debug.disable_stutter);
	}

	for (i = 0; i < res_pool->audio_count; i++) {
		struct audio *audio = res_pool->audios[i];

		audio->funcs->hw_init(audio);
	}

	for (i = 0; i < dc->link_count; i++) {
		struct dc_link *link = dc->links[i];

		if (link->panel_cntl)
			backlight = link->panel_cntl->funcs->hw_init(link->panel_cntl);
	}
	if (dc->ctx->dmub_srv) {
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		if (abms[i] != NULL && abms[i]->funcs != NULL)
			abms[i]->funcs->abm_init(abms[i], backlight);
		}
	}

	/* power AFMT HDMI memory TODO: may move to dis/en output save power*/
	REG_WRITE(DIO_MEM_PWR_CTRL, 0);

	// Set i2c to light sleep until engine is setup
	if (dc->debug.enable_mem_low_power.bits.i2c)
		REG_UPDATE(DIO_MEM_PWR_CTRL, I2C_LIGHT_SLEEP_FORCE, 0);

	if (hws->funcs.setup_hpo_hw_control)
		hws->funcs.setup_hpo_hw_control(hws, false);

	if (!dc->debug.disable_clock_gate) {
		/* enable all DCN clock gating */
		REG_WRITE(DCCG_GATE_DISABLE_CNTL, 0);
		REG_WRITE(DCCG_GATE_DISABLE_CNTL2, 0);
		REG_UPDATE(DCFCLK_CNTL, DCFCLK_GATE_DIS, 0);
	}

	if (dc->debug.disable_mem_low_power) {
		REG_UPDATE(DC_MEM_GLOBAL_PWR_REQ_CNTL, DC_MEM_GLOBAL_PWR_REQ_DIS, 1);
	}
	if (!dcb->funcs->is_accelerated_mode(dcb) && dc->res_pool->hubbub->funcs->init_watermarks)
		dc->res_pool->hubbub->funcs->init_watermarks(dc->res_pool->hubbub);

	if (dc->clk_mgr->funcs->notify_wm_ranges)
		dc->clk_mgr->funcs->notify_wm_ranges(dc->clk_mgr);

	if (dc->clk_mgr->funcs->set_hard_max_memclk && !dc->clk_mgr->dc_mode_softmax_enabled)
		dc->clk_mgr->funcs->set_hard_max_memclk(dc->clk_mgr);



	if (dc->res_pool->hubbub->funcs->force_pstate_change_control)
		dc->res_pool->hubbub->funcs->force_pstate_change_control(
				dc->res_pool->hubbub, false, false);

	if (dc->res_pool->hubbub->funcs->init_crb)
		dc->res_pool->hubbub->funcs->init_crb(dc->res_pool->hubbub);

	if (dc->res_pool->hubbub->funcs->set_request_limit && dc->config.sdpif_request_limit_words_per_umc > 0)
		dc->res_pool->hubbub->funcs->set_request_limit(dc->res_pool->hubbub, dc->ctx->dc_bios->vram_info.num_chans, dc->config.sdpif_request_limit_words_per_umc);
	// Get DMCUB capabilities
	if (dc->ctx->dmub_srv) {
		dc_dmub_srv_query_caps_cmd(dc->ctx->dmub_srv);
		dc->caps.dmub_caps.psr = dc->ctx->dmub_srv->dmub->feature_caps.psr;
		dc->caps.dmub_caps.mclk_sw = dc->ctx->dmub_srv->dmub->feature_caps.fw_assisted_mclk_switch;
	}

	if (dc->res_pool->pg_cntl) {
		if (dc->res_pool->pg_cntl->funcs->init_pg_status)
			dc->res_pool->pg_cntl->funcs->init_pg_status(dc->res_pool->pg_cntl);
	}
}

static int calc_mpc_flow_ctrl_cnt(const struct dc_stream_state *stream,
		int opp_cnt)
{
	bool hblank_halved = optc2_is_two_pixels_per_containter(&stream->timing);
	int flow_ctrl_cnt;

	if (opp_cnt >= 2)
		hblank_halved = true;

	flow_ctrl_cnt = stream->timing.h_total - stream->timing.h_addressable -
			stream->timing.h_border_left -
			stream->timing.h_border_right;

	if (hblank_halved)
		flow_ctrl_cnt /= 2;

	/* ODM combine 4:1 case */
	if (opp_cnt == 4)
		flow_ctrl_cnt /= 2;

	return flow_ctrl_cnt;
}

static void update_dsc_on_stream(struct pipe_ctx *pipe_ctx, bool enable)
{
	struct display_stream_compressor *dsc = pipe_ctx->stream_res.dsc;
	struct dc_stream_state *stream = pipe_ctx->stream;
	struct pipe_ctx *odm_pipe;
	int opp_cnt = 1;

	ASSERT(dsc);
	for (odm_pipe = pipe_ctx->next_odm_pipe; odm_pipe; odm_pipe = odm_pipe->next_odm_pipe)
		opp_cnt++;

	if (enable) {
		struct dsc_config dsc_cfg;
		struct dsc_optc_config dsc_optc_cfg;
		enum optc_dsc_mode optc_dsc_mode;

		/* Enable DSC hw block */
		dsc_cfg.pic_width = (stream->timing.h_addressable + stream->timing.h_border_left + stream->timing.h_border_right) / opp_cnt;
		dsc_cfg.pic_height = stream->timing.v_addressable + stream->timing.v_border_top + stream->timing.v_border_bottom;
		dsc_cfg.pixel_encoding = stream->timing.pixel_encoding;
		dsc_cfg.color_depth = stream->timing.display_color_depth;
		dsc_cfg.is_odm = pipe_ctx->next_odm_pipe ? true : false;
		dsc_cfg.dc_dsc_cfg = stream->timing.dsc_cfg;
		ASSERT(dsc_cfg.dc_dsc_cfg.num_slices_h % opp_cnt == 0);
		dsc_cfg.dc_dsc_cfg.num_slices_h /= opp_cnt;

		dsc->funcs->dsc_set_config(dsc, &dsc_cfg, &dsc_optc_cfg);
		dsc->funcs->dsc_enable(dsc, pipe_ctx->stream_res.opp->inst);
		for (odm_pipe = pipe_ctx->next_odm_pipe; odm_pipe; odm_pipe = odm_pipe->next_odm_pipe) {
			struct display_stream_compressor *odm_dsc = odm_pipe->stream_res.dsc;

			ASSERT(odm_dsc);
			odm_dsc->funcs->dsc_set_config(odm_dsc, &dsc_cfg, &dsc_optc_cfg);
			odm_dsc->funcs->dsc_enable(odm_dsc, odm_pipe->stream_res.opp->inst);
		}
		dsc_cfg.dc_dsc_cfg.num_slices_h *= opp_cnt;
		dsc_cfg.pic_width *= opp_cnt;

		optc_dsc_mode = dsc_optc_cfg.is_pixel_format_444 ? OPTC_DSC_ENABLED_444 : OPTC_DSC_ENABLED_NATIVE_SUBSAMPLED;

		/* Enable DSC in OPTC */
		DC_LOG_DSC("Setting optc DSC config for tg instance %d:", pipe_ctx->stream_res.tg->inst);
		pipe_ctx->stream_res.tg->funcs->set_dsc_config(pipe_ctx->stream_res.tg,
							optc_dsc_mode,
							dsc_optc_cfg.bytes_per_pixel,
							dsc_optc_cfg.slice_width);
	} else {
		/* disable DSC in OPTC */
		pipe_ctx->stream_res.tg->funcs->set_dsc_config(
				pipe_ctx->stream_res.tg,
				OPTC_DSC_DISABLED, 0, 0);

		/* disable DSC block */
		dsc->funcs->dsc_disable(pipe_ctx->stream_res.dsc);
		for (odm_pipe = pipe_ctx->next_odm_pipe; odm_pipe; odm_pipe = odm_pipe->next_odm_pipe) {
			ASSERT(odm_pipe->stream_res.dsc);
			odm_pipe->stream_res.dsc->funcs->dsc_disable(odm_pipe->stream_res.dsc);
		}
	}
}

// Given any pipe_ctx, return the total ODM combine factor, and optionally return
// the OPPids which are used
static unsigned int get_odm_config(struct pipe_ctx *pipe_ctx, unsigned int *opp_instances)
{
	unsigned int opp_count = 1;
	struct pipe_ctx *odm_pipe;

	// First get to the top pipe
	for (odm_pipe = pipe_ctx; odm_pipe->prev_odm_pipe; odm_pipe = odm_pipe->prev_odm_pipe)
		;

	// First pipe is always used
	if (opp_instances)
		opp_instances[0] = odm_pipe->stream_res.opp->inst;

	// Find and count odm pipes, if any
	for (odm_pipe = odm_pipe->next_odm_pipe; odm_pipe; odm_pipe = odm_pipe->next_odm_pipe) {
		if (opp_instances)
			opp_instances[opp_count] = odm_pipe->stream_res.opp->inst;
		opp_count++;
	}

	return opp_count;
}

void dcn35_update_odm(struct dc *dc, struct dc_state *context, struct pipe_ctx *pipe_ctx)
{
	struct pipe_ctx *odm_pipe;
	int opp_cnt = 0;
	int opp_inst[MAX_PIPES] = {0};
	bool rate_control_2x_pclk = (pipe_ctx->stream->timing.flags.INTERLACE || optc2_is_two_pixels_per_containter(&pipe_ctx->stream->timing));
	struct mpc_dwb_flow_control flow_control;
	struct mpc *mpc = dc->res_pool->mpc;
	int i;

	opp_cnt = get_odm_config(pipe_ctx, opp_inst);

	if (opp_cnt > 1)
		pipe_ctx->stream_res.tg->funcs->set_odm_combine(
				pipe_ctx->stream_res.tg,
				opp_inst, opp_cnt,
				&pipe_ctx->stream->timing);
	else
		pipe_ctx->stream_res.tg->funcs->set_odm_bypass(
				pipe_ctx->stream_res.tg, &pipe_ctx->stream->timing);

	rate_control_2x_pclk = rate_control_2x_pclk || opp_cnt > 1;
	flow_control.flow_ctrl_mode = 0;
	flow_control.flow_ctrl_cnt0 = 0x80;
	flow_control.flow_ctrl_cnt1 = calc_mpc_flow_ctrl_cnt(pipe_ctx->stream, opp_cnt);
	if (mpc->funcs->set_out_rate_control) {
		for (i = 0; i < opp_cnt; ++i) {
			mpc->funcs->set_out_rate_control(
					mpc, opp_inst[i],
					true,
					rate_control_2x_pclk,
					&flow_control);
		}
	}

	for (odm_pipe = pipe_ctx->next_odm_pipe; odm_pipe; odm_pipe = odm_pipe->next_odm_pipe) {
		odm_pipe->stream_res.opp->funcs->opp_pipe_clock_control(
				odm_pipe->stream_res.opp,
				true);
	}

	if (pipe_ctx->stream_res.dsc) {
		struct pipe_ctx *current_pipe_ctx = &dc->current_state->res_ctx.pipe_ctx[pipe_ctx->pipe_idx];

		update_dsc_on_stream(pipe_ctx, pipe_ctx->stream->timing.flags.DSC);

		/* Check if no longer using pipe for ODM, then need to disconnect DSC for that pipe */
		if (!pipe_ctx->next_odm_pipe && current_pipe_ctx->next_odm_pipe &&
				current_pipe_ctx->next_odm_pipe->stream_res.dsc) {
			struct display_stream_compressor *dsc = current_pipe_ctx->next_odm_pipe->stream_res.dsc;
			/* disconnect DSC block from stream */
			dsc->funcs->dsc_disconnect(dsc);
		}
	}
}

void dcn35_dpp_root_clock_control(struct dce_hwseq *hws, unsigned int dpp_inst, bool clock_on)
{
	if (!hws->ctx->dc->debug.root_clock_optimization.bits.dpp)
		return;

	if (hws->ctx->dc->res_pool->dccg->funcs->dpp_root_clock_control) {
		hws->ctx->dc->res_pool->dccg->funcs->dpp_root_clock_control(
			hws->ctx->dc->res_pool->dccg, dpp_inst, clock_on);
	}
}

void dcn35_dsc_pg_control(
		struct dce_hwseq *hws,
		unsigned int dsc_inst,
		bool power_on)
{
	uint32_t power_gate = power_on ? 0 : 1;
	uint32_t pwr_status = power_on ? 0 : 2;
	uint32_t org_ip_request_cntl = 0;

	if (hws->ctx->dc->debug.disable_dsc_power_gate)
		return;
	if (hws->ctx->dc->debug.ignore_pg)
		return;
	REG_GET(DC_IP_REQUEST_CNTL, IP_REQUEST_EN, &org_ip_request_cntl);
	if (org_ip_request_cntl == 0)
		REG_SET(DC_IP_REQUEST_CNTL, 0, IP_REQUEST_EN, 1);

	switch (dsc_inst) {
	case 0: /* DSC0 */
		REG_UPDATE(DOMAIN16_PG_CONFIG,
				DOMAIN_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN16_PG_STATUS,
				DOMAIN_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	case 1: /* DSC1 */
		REG_UPDATE(DOMAIN17_PG_CONFIG,
				DOMAIN_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN17_PG_STATUS,
				DOMAIN_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	case 2: /* DSC2 */
		REG_UPDATE(DOMAIN18_PG_CONFIG,
				DOMAIN_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN18_PG_STATUS,
				DOMAIN_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	case 3: /* DSC3 */
		REG_UPDATE(DOMAIN19_PG_CONFIG,
				DOMAIN_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN19_PG_STATUS,
				DOMAIN_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	default:
		BREAK_TO_DEBUGGER();
		break;
	}

	if (org_ip_request_cntl == 0)
		REG_SET(DC_IP_REQUEST_CNTL, 0, IP_REQUEST_EN, 0);
}

void dcn35_enable_power_gating_plane(struct dce_hwseq *hws, bool enable)
{
	bool force_on = true; /* disable power gating */
	uint32_t org_ip_request_cntl = 0;

	if (hws->ctx->dc->debug.disable_hubp_power_gate)
		return;
	if (hws->ctx->dc->debug.ignore_pg)
		return;
	REG_GET(DC_IP_REQUEST_CNTL, IP_REQUEST_EN, &org_ip_request_cntl);
	if (org_ip_request_cntl == 0)
		REG_SET(DC_IP_REQUEST_CNTL, 0, IP_REQUEST_EN, 1);
	/* DCHUBP0/1/2/3/4/5 */
	REG_UPDATE(DOMAIN0_PG_CONFIG, DOMAIN_POWER_FORCEON, force_on);
	REG_UPDATE(DOMAIN2_PG_CONFIG, DOMAIN_POWER_FORCEON, force_on);
	/* DPP0/1/2/3/4/5 */
	REG_UPDATE(DOMAIN1_PG_CONFIG, DOMAIN_POWER_FORCEON, force_on);
	REG_UPDATE(DOMAIN3_PG_CONFIG, DOMAIN_POWER_FORCEON, force_on);

	force_on = true; /* disable power gating */
	if (enable && !hws->ctx->dc->debug.disable_dsc_power_gate)
		force_on = false;

	/* DCS0/1/2/3/4 */
	REG_UPDATE(DOMAIN16_PG_CONFIG, DOMAIN_POWER_FORCEON, force_on);
	REG_UPDATE(DOMAIN17_PG_CONFIG, DOMAIN_POWER_FORCEON, force_on);
	REG_UPDATE(DOMAIN18_PG_CONFIG, DOMAIN_POWER_FORCEON, force_on);
	REG_UPDATE(DOMAIN19_PG_CONFIG, DOMAIN_POWER_FORCEON, force_on);


}

/* In headless boot cases, DIG may be turned
 * on which causes HW/SW discrepancies.
 * To avoid this, power down hardware on boot
 * if DIG is turned on
 */
void dcn35_power_down_on_boot(struct dc *dc)
{
	struct dc_link *edp_links[MAX_NUM_EDP];
	struct dc_link *edp_link = NULL;
	int edp_num;
	int i = 0;

	dc_get_edp_links(dc, edp_links, &edp_num);
	if (edp_num)
		edp_link = edp_links[0];

	if (edp_link && edp_link->link_enc->funcs->is_dig_enabled &&
			edp_link->link_enc->funcs->is_dig_enabled(edp_link->link_enc) &&
			dc->hwseq->funcs.edp_backlight_control &&
			dc->hwss.power_down &&
			dc->hwss.edp_power_control) {
		dc->hwseq->funcs.edp_backlight_control(edp_link, false);
		dc->hwss.power_down(dc);
		dc->hwss.edp_power_control(edp_link, false);
	} else {
		for (i = 0; i < dc->link_count; i++) {
			struct dc_link *link = dc->links[i];

			if (link->link_enc && link->link_enc->funcs->is_dig_enabled &&
					link->link_enc->funcs->is_dig_enabled(link->link_enc) &&
					dc->hwss.power_down) {
				dc->hwss.power_down(dc);
				break;
			}

		}
	}

	/*
	 * Call update_clocks with empty context
	 * to send DISPLAY_OFF
	 * Otherwise DISPLAY_OFF may not be asserted
	 */
	if (dc->clk_mgr->funcs->set_low_power_state)
		dc->clk_mgr->funcs->set_low_power_state(dc->clk_mgr);

	if (dc->clk_mgr->clks.pwr_state == DCN_PWR_STATE_LOW_POWER) {
		if (!dc->idle_optimizations_allowed) {
			dc_dmub_srv_notify_idle(dc, true);
			dc->idle_optimizations_allowed = true;
		}
	}
}

bool dcn35_apply_idle_power_optimizations(struct dc *dc, bool enable)
{
	struct dc_link *edp_links[MAX_NUM_EDP];
	int edp_num;
	if (dc->debug.dmcub_emulation)
		return true;

	if (enable) {
		dc_get_edp_links(dc, edp_links, &edp_num);
		if (edp_num == 0 || edp_num > 1)
			return false;
	}

	// TODO: review other cases when idle optimization is allowed

	if (!enable) {
		// Tell PMFW to exit low power state
		if (dc->clk_mgr->funcs->exit_low_power_state)
			dc->clk_mgr->funcs->exit_low_power_state(dc->clk_mgr);

		dc_dmub_srv_is_hw_pwr_up(dc->ctx->dmub_srv, true);
	}

	dc_dmub_srv_notify_idle(dc, enable);

	if (!enable)
		dc_dmub_srv_is_hw_pwr_up(dc->ctx->dmub_srv, true);

	return true;
}

void dcn35_z10_restore(const struct dc *dc)
{
	if (dc->debug.disable_z10)
		return;

	dc_dmub_srv_exit_low_power_state(dc);

	dcn31_z10_restore(dc);
}

void dcn35_init_pipes(struct dc *dc, struct dc_state *context)
{
	int i;
	struct dce_hwseq *hws = dc->hwseq;
	struct hubbub *hubbub = dc->res_pool->hubbub;
	struct pg_cntl *pg_cntl = dc->res_pool->pg_cntl;
	bool can_apply_seamless_boot = false;

	for (i = 0; i < context->stream_count; i++) {
		if (context->streams[i]->apply_seamless_boot_optimization) {
			can_apply_seamless_boot = true;
			break;
		}
	}

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct timing_generator *tg = dc->res_pool->timing_generators[i];
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

		/* There is assumption that pipe_ctx is not mapping irregularly
		 * to non-preferred front end. If pipe_ctx->stream is not NULL,
		 * we will use the pipe, so don't disable
		 */
		if (pipe_ctx->stream != NULL && can_apply_seamless_boot)
			continue;

		/* Blank controller using driver code instead of
		 * command table.
		 */
		if (tg->funcs->is_tg_enabled(tg)) {
			if (hws->funcs.init_blank != NULL) {
				hws->funcs.init_blank(dc, tg);
				tg->funcs->lock(tg);
			} else {
				tg->funcs->lock(tg);
				tg->funcs->set_blank(tg, true);
				hwss_wait_for_blank_complete(tg);
			}
		}
	}

	/* Reset det size */
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];
		struct hubp *hubp = dc->res_pool->hubps[i];

		/* Do not need to reset for seamless boot */
		if (pipe_ctx->stream != NULL && can_apply_seamless_boot)
			continue;

		if (hubbub && hubp) {
			if (hubbub->funcs->program_det_size)
				hubbub->funcs->program_det_size(hubbub, hubp->inst, 0);
		}
	}

	/* num_opp will be equal to number of mpcc */
	for (i = 0; i < dc->res_pool->res_cap->num_opp; i++) {
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

		/* Cannot reset the MPC mux if seamless boot */
		if (pipe_ctx->stream != NULL && can_apply_seamless_boot)
			continue;

		dc->res_pool->mpc->funcs->mpc_init_single_inst(
				dc->res_pool->mpc, i);
	}

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct timing_generator *tg = dc->res_pool->timing_generators[i];
		struct hubp *hubp = dc->res_pool->hubps[i];
		struct dpp *dpp = dc->res_pool->dpps[i];
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

		/* There is assumption that pipe_ctx is not mapping irregularly
		 * to non-preferred front end. If pipe_ctx->stream is not NULL,
		 * we will use the pipe, so don't disable
		 */
		if (can_apply_seamless_boot &&
			pipe_ctx->stream != NULL &&
			pipe_ctx->stream_res.tg->funcs->is_tg_enabled(
				pipe_ctx->stream_res.tg)) {
			// Enable double buffering for OTG_BLANK no matter if
			// seamless boot is enabled or not to suppress global sync
			// signals when OTG blanked. This is to prevent pipe from
			// requesting data while in PSR.
			tg->funcs->tg_init(tg);
			hubp->power_gated = true;
			continue;
		}

		/* Disable on the current state so the new one isn't cleared. */
		pipe_ctx = &dc->current_state->res_ctx.pipe_ctx[i];

		dpp->funcs->dpp_reset(dpp);

		pipe_ctx->stream_res.tg = tg;
		pipe_ctx->pipe_idx = i;

		pipe_ctx->plane_res.hubp = hubp;
		pipe_ctx->plane_res.dpp = dpp;
		pipe_ctx->plane_res.mpcc_inst = dpp->inst;
		hubp->mpcc_id = dpp->inst;
		hubp->opp_id = OPP_ID_INVALID;
		hubp->power_gated = false;

		dc->res_pool->opps[i]->mpc_tree_params.opp_id = dc->res_pool->opps[i]->inst;
		dc->res_pool->opps[i]->mpc_tree_params.opp_list = NULL;
		dc->res_pool->opps[i]->mpcc_disconnect_pending[pipe_ctx->plane_res.mpcc_inst] = true;
		pipe_ctx->stream_res.opp = dc->res_pool->opps[i];

		hws->funcs.plane_atomic_disconnect(dc, pipe_ctx);

		if (tg->funcs->is_tg_enabled(tg))
			tg->funcs->unlock(tg);

		dc->hwss.disable_plane(dc, pipe_ctx);

		pipe_ctx->stream_res.tg = NULL;
		pipe_ctx->plane_res.hubp = NULL;

		if (tg->funcs->is_tg_enabled(tg)) {
			if (tg->funcs->init_odm)
				tg->funcs->init_odm(tg);
		}

		tg->funcs->tg_init(tg);
	}

	if (pg_cntl != NULL) {
		if (pg_cntl->funcs->dsc_pg_control != NULL) {
			uint32_t num_opps = 0;
			uint32_t opp_id_src0 = OPP_ID_INVALID;
			uint32_t opp_id_src1 = OPP_ID_INVALID;

			// Step 1: To find out which OPTC is running & OPTC DSC is ON
			// We can't use res_pool->res_cap->num_timing_generator to check
			// Because it records display pipes default setting built in driver,
			// not display pipes of the current chip.
			// Some ASICs would be fused display pipes less than the default setting.
			// In dcnxx_resource_construct function, driver would obatin real information.
			for (i = 0; i < dc->res_pool->timing_generator_count; i++) {
				uint32_t optc_dsc_state = 0;
				struct timing_generator *tg = dc->res_pool->timing_generators[i];

				if (tg->funcs->is_tg_enabled(tg)) {
					if (tg->funcs->get_dsc_status)
						tg->funcs->get_dsc_status(tg, &optc_dsc_state);
					// Only one OPTC with DSC is ON, so if we got one result,
					// we would exit this block. non-zero value is DSC enabled
					if (optc_dsc_state != 0) {
						tg->funcs->get_optc_source(tg, &num_opps, &opp_id_src0, &opp_id_src1);
						break;
					}
				}
			}

			// Step 2: To power down DSC but skip DSC  of running OPTC
			for (i = 0; i < dc->res_pool->res_cap->num_dsc; i++) {
				struct dcn_dsc_state s  = {0};

				dc->res_pool->dscs[i]->funcs->dsc_read_state(dc->res_pool->dscs[i], &s);

				if ((s.dsc_opp_source == opp_id_src0 || s.dsc_opp_source == opp_id_src1) &&
					s.dsc_clock_en && s.dsc_fw_en)
					continue;

				pg_cntl->funcs->dsc_pg_control(pg_cntl, dc->res_pool->dscs[i]->inst, false);
			}
		}
	}
}

void dcn35_enable_plane(struct dc *dc, struct pipe_ctx *pipe_ctx,
			       struct dc_state *context)
{
	/* enable DCFCLK current DCHUB */
	pipe_ctx->plane_res.hubp->funcs->hubp_clk_cntl(pipe_ctx->plane_res.hubp, true);

	/* initialize HUBP on power up */
	pipe_ctx->plane_res.hubp->funcs->hubp_init(pipe_ctx->plane_res.hubp);

	/* make sure OPP_PIPE_CLOCK_EN = 1 */
	pipe_ctx->stream_res.opp->funcs->opp_pipe_clock_control(
			pipe_ctx->stream_res.opp,
			true);
	/*to do: insert PG here*/
	if (dc->vm_pa_config.valid) {
		struct vm_system_aperture_param apt;

		apt.sys_default.quad_part = 0;

		apt.sys_low.quad_part = dc->vm_pa_config.system_aperture.start_addr;
		apt.sys_high.quad_part = dc->vm_pa_config.system_aperture.end_addr;

		// Program system aperture settings
		pipe_ctx->plane_res.hubp->funcs->hubp_set_vm_system_aperture_settings(pipe_ctx->plane_res.hubp, &apt);
	}

	if (!pipe_ctx->top_pipe
		&& pipe_ctx->plane_state
		&& pipe_ctx->plane_state->flip_int_enabled
		&& pipe_ctx->plane_res.hubp->funcs->hubp_set_flip_int)
		pipe_ctx->plane_res.hubp->funcs->hubp_set_flip_int(pipe_ctx->plane_res.hubp);
}

/* disable HW used by plane.
 * note:  cannot disable until disconnect is complete
 */
void dcn35_plane_atomic_disable(struct dc *dc, struct pipe_ctx *pipe_ctx)
{
	struct hubp *hubp = pipe_ctx->plane_res.hubp;
	struct dpp *dpp = pipe_ctx->plane_res.dpp;

	dc->hwss.wait_for_mpcc_disconnect(dc, dc->res_pool, pipe_ctx);

	/* In flip immediate with pipe splitting case GSL is used for
	 * synchronization so we must disable it when the plane is disabled.
	 */
	if (pipe_ctx->stream_res.gsl_group != 0)
		dcn20_setup_gsl_group_as_lock(dc, pipe_ctx, false);
/*
	if (hubp->funcs->hubp_update_mall_sel)
		hubp->funcs->hubp_update_mall_sel(hubp, 0, false);
*/
	dc->hwss.set_flip_control_gsl(pipe_ctx, false);

	hubp->funcs->hubp_clk_cntl(hubp, false);

	dpp->funcs->dpp_dppclk_control(dpp, false, false);
/*to do, need to support both case*/
	hubp->power_gated = true;

	dpp->funcs->dpp_reset(dpp);

	pipe_ctx->stream = NULL;
	memset(&pipe_ctx->stream_res, 0, sizeof(pipe_ctx->stream_res));
	memset(&pipe_ctx->plane_res, 0, sizeof(pipe_ctx->plane_res));
	pipe_ctx->top_pipe = NULL;
	pipe_ctx->bottom_pipe = NULL;
	pipe_ctx->plane_state = NULL;
}

void dcn35_disable_plane(struct dc *dc, struct pipe_ctx *pipe_ctx)
{
	struct dce_hwseq *hws = dc->hwseq;
	bool is_phantom = pipe_ctx->plane_state && pipe_ctx->plane_state->is_phantom;
	struct timing_generator *tg = is_phantom ? pipe_ctx->stream_res.tg : NULL;

	DC_LOGGER_INIT(dc->ctx->logger);

	if (!pipe_ctx->plane_res.hubp || pipe_ctx->plane_res.hubp->power_gated)
		return;

	if (hws->funcs.plane_atomic_disable)
		hws->funcs.plane_atomic_disable(dc, pipe_ctx);

	/* Turn back off the phantom OTG after the phantom plane is fully disabled
	 */
	if (is_phantom)
		if (tg && tg->funcs->disable_phantom_crtc)
			tg->funcs->disable_phantom_crtc(tg);

	DC_LOG_DC("Power down front end %d\n",
					pipe_ctx->pipe_idx);
}

void dcn35_calc_blocks_to_gate(struct dc *dc, struct dc_state *context,
	struct pg_block_update *update_state)
{
	bool hpo_frl_stream_enc_acquired = false;
	bool hpo_dp_stream_enc_acquired = false;
	int i = 0, j = 0;

	memset(update_state, 0, sizeof(struct pg_block_update));

	for (i = 0; i < dc->res_pool->hpo_dp_stream_enc_count; i++) {
		if (context->res_ctx.is_hpo_dp_stream_enc_acquired[i] &&
				dc->res_pool->hpo_dp_stream_enc[i]) {
			hpo_dp_stream_enc_acquired = true;
			break;
		}
	}

	if (!hpo_frl_stream_enc_acquired && !hpo_dp_stream_enc_acquired)
		update_state->pg_res_update[PG_HPO] = true;

	update_state->pg_res_update[PG_DWB] = true;

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

		for (j = 0; j < PG_HW_PIPE_RESOURCES_NUM_ELEMENT; j++)
			update_state->pg_pipe_res_update[j][i] = true;

		if (!pipe_ctx)
			continue;

		if (pipe_ctx->plane_res.hubp)
			update_state->pg_pipe_res_update[PG_HUBP][pipe_ctx->plane_res.hubp->inst] = false;

		if (pipe_ctx->plane_res.dpp)
			update_state->pg_pipe_res_update[PG_DPP][pipe_ctx->plane_res.hubp->inst] = false;

		if ((pipe_ctx->plane_res.dpp || pipe_ctx->stream_res.opp) &&
			pipe_ctx->plane_res.mpcc_inst >= 0)
			update_state->pg_pipe_res_update[PG_MPCC][pipe_ctx->plane_res.mpcc_inst] = false;

		if (pipe_ctx->stream_res.dsc)
			update_state->pg_pipe_res_update[PG_DSC][pipe_ctx->stream_res.dsc->inst] = false;

		if (pipe_ctx->stream_res.opp)
			update_state->pg_pipe_res_update[PG_OPP][pipe_ctx->stream_res.opp->inst] = false;

		if (pipe_ctx->stream_res.tg)
			update_state->pg_pipe_res_update[PG_OPTC][pipe_ctx->stream_res.tg->inst] = false;
	}
}

void dcn35_calc_blocks_to_ungate(struct dc *dc, struct dc_state *context,
	struct pg_block_update *update_state)
{
	bool hpo_frl_stream_enc_acquired = false;
	bool hpo_dp_stream_enc_acquired = false;
	int i = 0, j = 0;

	memset(update_state, 0, sizeof(struct pg_block_update));

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *cur_pipe = &dc->current_state->res_ctx.pipe_ctx[i];
		struct pipe_ctx *new_pipe = &context->res_ctx.pipe_ctx[i];

		if (cur_pipe == NULL || new_pipe == NULL)
			continue;

		if ((!cur_pipe->plane_state && new_pipe->plane_state) ||
			(!cur_pipe->stream && new_pipe->stream)) {
			// New pipe addition
			for (j = 0; j < PG_HW_PIPE_RESOURCES_NUM_ELEMENT; j++) {
				if (j == PG_HUBP && new_pipe->plane_res.hubp)
					update_state->pg_pipe_res_update[j][new_pipe->plane_res.hubp->inst] = true;

				if (j == PG_DPP && new_pipe->plane_res.dpp)
					update_state->pg_pipe_res_update[j][new_pipe->plane_res.dpp->inst] = true;

				if (j == PG_MPCC && new_pipe->plane_res.dpp)
					update_state->pg_pipe_res_update[j][new_pipe->plane_res.mpcc_inst] = true;

				if (j == PG_DSC && new_pipe->stream_res.dsc)
					update_state->pg_pipe_res_update[j][new_pipe->stream_res.dsc->inst] = true;

				if (j == PG_OPP && new_pipe->stream_res.opp)
					update_state->pg_pipe_res_update[j][new_pipe->stream_res.opp->inst] = true;

				if (j == PG_OPTC && new_pipe->stream_res.tg)
					update_state->pg_pipe_res_update[j][new_pipe->stream_res.tg->inst] = true;
			}
		} else if (cur_pipe->plane_state == new_pipe->plane_state ||
				cur_pipe == new_pipe) {
			//unchanged pipes
			for (j = 0; j < PG_HW_PIPE_RESOURCES_NUM_ELEMENT; j++) {
				if (j == PG_HUBP &&
					cur_pipe->plane_res.hubp != new_pipe->plane_res.hubp &&
					new_pipe->plane_res.hubp)
					update_state->pg_pipe_res_update[j][new_pipe->plane_res.hubp->inst] = true;

				if (j == PG_DPP &&
					cur_pipe->plane_res.dpp != new_pipe->plane_res.dpp &&
					new_pipe->plane_res.dpp)
					update_state->pg_pipe_res_update[j][new_pipe->plane_res.dpp->inst] = true;

				if (j == PG_OPP &&
					cur_pipe->stream_res.opp != new_pipe->stream_res.opp &&
					new_pipe->stream_res.opp)
					update_state->pg_pipe_res_update[j][new_pipe->stream_res.opp->inst] = true;

				if (j == PG_DSC &&
					cur_pipe->stream_res.dsc != new_pipe->stream_res.dsc &&
					new_pipe->stream_res.dsc)
					update_state->pg_pipe_res_update[j][new_pipe->stream_res.dsc->inst] = true;

				if (j == PG_OPTC &&
					cur_pipe->stream_res.tg != new_pipe->stream_res.tg &&
					new_pipe->stream_res.tg)
					update_state->pg_pipe_res_update[j][new_pipe->stream_res.tg->inst] = true;
			}
		}
	}

	for (i = 0; i < dc->res_pool->hpo_dp_stream_enc_count; i++) {
		if (context->res_ctx.is_hpo_dp_stream_enc_acquired[i] &&
				dc->res_pool->hpo_dp_stream_enc[i]) {
			hpo_dp_stream_enc_acquired = true;
			break;
		}
	}

	if (hpo_frl_stream_enc_acquired || hpo_dp_stream_enc_acquired)
		update_state->pg_res_update[PG_HPO] = true;

}

void dcn35_block_power_control(struct dc *dc,
	struct pg_block_update *update_state, bool power_on)
{
	int i = 0;
	struct pg_cntl *pg_cntl = dc->res_pool->pg_cntl;

	if (!pg_cntl)
		return;
	if (dc->debug.ignore_pg)
		return;
	if (update_state->pg_res_update[PG_HPO]) {
		if (pg_cntl->funcs->hpo_pg_control)
			pg_cntl->funcs->hpo_pg_control(pg_cntl, power_on);
	}

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		if (update_state->pg_pipe_res_update[PG_HUBP][i] &&
			update_state->pg_pipe_res_update[PG_DPP][i]) {
			if (pg_cntl->funcs->hubp_dpp_pg_control)
				pg_cntl->funcs->hubp_dpp_pg_control(pg_cntl, i, power_on);
		}

		if (update_state->pg_pipe_res_update[PG_DSC][i]) {
			if (pg_cntl->funcs->dsc_pg_control)
				pg_cntl->funcs->dsc_pg_control(pg_cntl, i, power_on);
		}

		if (update_state->pg_pipe_res_update[PG_MPCC][i]) {
			if (pg_cntl->funcs->mpcc_pg_control)
				pg_cntl->funcs->mpcc_pg_control(pg_cntl, i, power_on);
		}

		if (update_state->pg_pipe_res_update[PG_OPP][i]) {
			if (pg_cntl->funcs->opp_pg_control)
				pg_cntl->funcs->opp_pg_control(pg_cntl, i, power_on);
		}

		if (update_state->pg_pipe_res_update[PG_OPTC][i]) {
			if (pg_cntl->funcs->optc_pg_control)
				pg_cntl->funcs->optc_pg_control(pg_cntl, i, power_on);
		}
	}

	if (update_state->pg_res_update[PG_DWB]) {
		if (pg_cntl->funcs->dwb_pg_control)
			pg_cntl->funcs->dwb_pg_control(pg_cntl, power_on);
	}

	if (pg_cntl->funcs->plane_otg_pg_control)
		pg_cntl->funcs->plane_otg_pg_control(pg_cntl, power_on);
}

void dcn35_root_clock_control(struct dc *dc,
	struct pg_block_update *update_state, bool power_on)
{
	int i = 0;
	struct pg_cntl *pg_cntl = dc->res_pool->pg_cntl;

	if (!pg_cntl)
		return;

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		if (update_state->pg_pipe_res_update[PG_HUBP][i] &&
			update_state->pg_pipe_res_update[PG_DPP][i]) {
			if (dc->hwseq->funcs.dpp_root_clock_control)
				dc->hwseq->funcs.dpp_root_clock_control(dc->hwseq, i, power_on);
		}

		if (update_state->pg_pipe_res_update[PG_DSC][i]) {
			if (power_on) {
				if (dc->res_pool->dccg->funcs->enable_dsc)
					dc->res_pool->dccg->funcs->enable_dsc(dc->res_pool->dccg, i);
			} else {
				if (dc->res_pool->dccg->funcs->disable_dsc)
					dc->res_pool->dccg->funcs->disable_dsc(dc->res_pool->dccg, i);
			}
		}
	}
}

void dcn35_prepare_bandwidth(
		struct dc *dc,
		struct dc_state *context)
{
	struct pg_block_update pg_update_state;

	if (dc->hwss.calc_blocks_to_ungate) {
		dc->hwss.calc_blocks_to_ungate(dc, context, &pg_update_state);

		if (dc->hwss.root_clock_control)
			dc->hwss.root_clock_control(dc, &pg_update_state, true);

		if (dc->hwss.block_power_control)
			dc->hwss.block_power_control(dc, &pg_update_state, true);
	}

	dcn20_prepare_bandwidth(dc, context);
}

void dcn35_optimize_bandwidth(
		struct dc *dc,
		struct dc_state *context)
{
	struct pg_block_update pg_update_state;

	dcn20_optimize_bandwidth(dc, context);

	if (dc->hwss.calc_blocks_to_gate) {
		dc->hwss.calc_blocks_to_gate(dc, context, &pg_update_state);

		if (dc->hwss.block_power_control)
			dc->hwss.block_power_control(dc, &pg_update_state, false);

		if (dc->hwss.root_clock_control)
			dc->hwss.root_clock_control(dc, &pg_update_state, false);
	}
}
