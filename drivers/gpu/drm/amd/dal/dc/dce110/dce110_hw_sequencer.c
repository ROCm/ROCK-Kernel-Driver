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
#include "dc_services.h"
#include "dc.h"
#include "dc_bios_types.h"
#include "core_types.h"
#include "core_status.h"
#include "resource.h"
#include "hw_sequencer.h"
#include "dc_helpers.h"

#include "dce110/dce110_resource.h"
#include "dce110/dce110_timing_generator.h"
#include "dce110/dce110_link_encoder.h"
#include "dce110/dce110_stream_encoder.h"
#include "dce110/dce110_mem_input.h"
#include "dce110/dce110_ipp.h"
#include "dce110/dce110_transform.h"
#include "dce110/dce110_opp.h"
#include "gpu/dce110/dc_clock_gating_dce110.h"

/* include DCE11 register header files */
#include "dce/dce_11_0_d.h"
#include "dce/dce_11_0_sh_mask.h"

struct dce110_hw_seq_reg_offsets {
	uint32_t dcfe;
	uint32_t blnd;
	uint32_t crtc;
	uint32_t dcp;
};

enum crtc_stereo_mixer_mode {
	HW_STEREO_MIXER_MODE_INACTIVE,
	HW_STEREO_MIXER_MODE_ROW_INTERLEAVE,
	HW_STEREO_MIXER_MODE_COLUMN_INTERLEAVE,
	HW_STEREO_MIXER_MODE_PIXEL_INTERLEAVE,
	HW_STEREO_MIXER_MODE_BLENDER
};

struct crtc_mixer_params {
	bool sub_sampling;
	enum crtc_stereo_mixer_mode mode;
};

enum pipe_lock_control {
	PIPE_LOCK_CONTROL_GRAPHICS = 1 << 0,
	PIPE_LOCK_CONTROL_BLENDER = 1 << 1,
	PIPE_LOCK_CONTROL_SCL = 1 << 2,
	PIPE_LOCK_CONTROL_SURFACE = 1 << 3,
	PIPE_LOCK_CONTROL_MODE = 1 << 4
};

enum blender_mode {
	BLENDER_MODE_CURRENT_PIPE = 0,/* Data from current pipe only */
	BLENDER_MODE_OTHER_PIPE, /* Data from other pipe only */
	BLENDER_MODE_BLENDING,/* Alpha blending - blend 'current' and 'other' */
	BLENDER_MODE_STEREO
};

enum blender_type {
	BLENDER_TYPE_NON_SINGLE_PIPE = 0,
	BLENDER_TYPE_SB_SINGLE_PIPE,
	BLENDER_TYPE_TB_SINGLE_PIPE
};

enum dc_memory_sleep_state {
	DC_MEMORY_SLEEP_DISABLE = 0,
	DC_MEMORY_LIGHT_SLEEP,
	DC_MEMORY_DEEP_SLEEP,
	DC_MEMORY_SHUTDOWN
};
enum {
	DCE110_PIPE_UPDATE_PENDING_DELAY = 1000,
	DCE110_PIPE_UPDATE_PENDING_CHECKCOUNT = 5000
};

static const struct dce110_hw_seq_reg_offsets reg_offsets[] = {
{
	.dcfe = (mmDCFE0_DCFE_MEM_PWR_CTRL - mmDCFE_MEM_PWR_CTRL),
	.blnd = (mmBLND0_BLND_CONTROL - mmBLND_CONTROL),
	.crtc = (mmCRTC0_CRTC_GSL_CONTROL - mmCRTC_GSL_CONTROL),
	.dcp = (mmDCP0_DVMM_PTE_CONTROL - mmDVMM_PTE_CONTROL),
},
{
	.dcfe = (mmDCFE1_DCFE_MEM_PWR_CTRL - mmDCFE0_DCFE_MEM_PWR_CTRL),
	.blnd = (mmBLND1_BLND_CONTROL - mmBLND0_BLND_CONTROL),
	.crtc = (mmCRTC1_CRTC_GSL_CONTROL - mmCRTC0_CRTC_GSL_CONTROL),
	.dcp = (mmDCP1_DVMM_PTE_CONTROL - mmDCP0_DVMM_PTE_CONTROL),
},
{
	.dcfe = (mmDCFE2_DCFE_MEM_PWR_CTRL - mmDCFE0_DCFE_MEM_PWR_CTRL),
	.blnd = (mmBLND2_BLND_CONTROL - mmBLND0_BLND_CONTROL),
	.crtc = (mmCRTC2_CRTC_GSL_CONTROL - mmCRTC0_CRTC_GSL_CONTROL),
	.dcp = (mmDCP2_DVMM_PTE_CONTROL - mmDCP0_DVMM_PTE_CONTROL),
}
};

#define HW_REG_DCFE(reg, id)\
	(reg + reg_offsets[id].dcfe)

#define HW_REG_BLND(reg, id)\
	(reg + reg_offsets[id].blnd)

#define HW_REG_CRTC(reg, id)\
	(reg + reg_offsets[id].crtc)

#define HW_REG_DCP(reg, id)\
	(reg + reg_offsets[id].dcp)


static void init_pte(struct dc_context *ctx);

/*******************************************************************************
 * Private definitions
 ******************************************************************************/

static void dce110_enable_display_pipe_clock_gating(
	struct dc_context *ctx,
	bool clock_gating)
{
	/*TODO*/
}

static bool dce110_enable_display_power_gating(
	struct dc_context *ctx,
	uint8_t controller_id,
	struct dc_bios *dcb,
	enum pipe_gating_control power_gating)
{
	enum bp_result bp_result = BP_RESULT_OK;
	enum bp_pipe_control_action cntl;

	if (power_gating == PIPE_GATING_CONTROL_INIT)
		cntl = ASIC_PIPE_INIT;
	else if (power_gating == PIPE_GATING_CONTROL_ENABLE)
		cntl = ASIC_PIPE_ENABLE;
	else
		cntl = ASIC_PIPE_DISABLE;

	if (!(power_gating == PIPE_GATING_CONTROL_INIT &&
					(controller_id + 1) != CONTROLLER_ID_D0))
		bp_result = dcb->funcs->enable_disp_power_gating(
						dcb, controller_id + 1, cntl);

	if (power_gating != PIPE_GATING_CONTROL_ENABLE)
		init_pte(ctx);

	if (bp_result == BP_RESULT_OK)
		return true;
	else
		return false;
}


static bool set_gamma_ramp(
	struct input_pixel_processor *ipp,
	struct output_pixel_processor *opp,
	const struct gamma_ramp *ramp,
	const struct gamma_parameters *params)
{
	/*Power on LUT memory*/
	dce110_opp_power_on_regamma_lut(opp, true);

	if (params->surface_pixel_format == PIXEL_FORMAT_INDEX8 ||
		params->selected_gamma_lut == GRAPHICS_GAMMA_LUT_LEGACY) {
		/* do legacy DCP for 256 colors if we are requested to do so */
		ipp->funcs->ipp_set_legacy_input_gamma_ramp(
			ipp, ramp, params);

		ipp->funcs->ipp_set_legacy_input_gamma_mode(ipp, true);

		/* set bypass */
		ipp->funcs->ipp_program_prescale(ipp, PIXEL_FORMAT_UNINITIALIZED);

		ipp->funcs->ipp_set_degamma(ipp, params, true);

		dce110_opp_set_regamma(opp, ramp, params, true);
	} else if (params->selected_gamma_lut ==
			GRAPHICS_GAMMA_LUT_LEGACY_AND_REGAMMA) {
		if (!dce110_opp_map_legacy_and_regamma_hw_to_x_user(
			opp, ramp, params)) {
			BREAK_TO_DEBUGGER();
			/* invalid parameters or bug */
			return false;
		}

		/* do legacy DCP for 256 colors if we are requested to do so */
		ipp->funcs->ipp_set_legacy_input_gamma_ramp(
			ipp, ramp, params);

		ipp->funcs->ipp_set_legacy_input_gamma_mode(ipp, true);

		/* set bypass */
		ipp->funcs->ipp_program_prescale(ipp, PIXEL_FORMAT_UNINITIALIZED);
	} else {
		ipp->funcs->ipp_set_legacy_input_gamma_mode(ipp, false);

		ipp->funcs->ipp_program_prescale(ipp, params->surface_pixel_format);

		/* Do degamma step : remove the given gamma value from FB.
		 * For FP16 or no degamma do by pass */
		ipp->funcs->ipp_set_degamma(ipp, params, false);

		dce110_opp_set_regamma(opp, ramp, params, false);
	}

	/*re-enable low power mode for LUT memory*/
	dce110_opp_power_on_regamma_lut(opp, false);

	return true;
}

static enum dc_status bios_parser_crtc_source_select(
		struct core_stream *stream)
{
	struct dc_bios *dcb;
	/* call VBIOS table to set CRTC source for the HW
	 * encoder block
	 * note: video bios clears all FMT setting here. */
	struct bp_crtc_source_select crtc_source_select = {0};
	const struct core_sink *sink = stream->sink;

	crtc_source_select.engine_id = stream->stream_enc->id;
	crtc_source_select.controller_id = stream->controller_idx + 1;
	/*TODO: Need to un-hardcode color depth, dp_audio and account for
	 * the case where signal and sink signal is different (translator
	 * encoder)*/
	crtc_source_select.signal = sink->public.sink_signal;
	crtc_source_select.enable_dp_audio = false;
	crtc_source_select.sink_signal = sink->public.sink_signal;
	crtc_source_select.display_output_bit_depth = PANEL_8BIT_COLOR;

	dcb = dal_adapter_service_get_bios_parser(sink->link->adapter_srv);

	if (BP_RESULT_OK != dcb->funcs->crtc_source_select(
		dcb,
		&crtc_source_select)) {
		return DC_ERROR_UNEXPECTED;
	}

	return DC_OK;
}

static enum color_space surface_color_to_color_space(
	struct plane_colorimetry *colorimetry)
{
	enum color_space color_space = COLOR_SPACE_UNKNOWN;

	switch (colorimetry->color_space) {
	case SURFACE_COLOR_SPACE_SRGB:
	case SURFACE_COLOR_SPACE_XRRGB:
		if (colorimetry->limited_range)
			color_space = COLOR_SPACE_SRGB_LIMITED_RANGE;
		else
			color_space = COLOR_SPACE_SRGB_FULL_RANGE;
		break;
	case SURFACE_COLOR_SPACE_BT601:
	case SURFACE_COLOR_SPACE_XVYCC_BT601:
		color_space = COLOR_SPACE_YCBCR601;
		break;
	case SURFACE_COLOR_SPACE_BT709:
	case SURFACE_COLOR_SPACE_XVYCC_BT709:
		color_space = COLOR_SPACE_YCBCR709;
		break;
	}

	return color_space;
}

/*******************************FMT**************************************/
static void program_fmt(
		struct output_pixel_processor *opp,
		struct bit_depth_reduction_params *fmt_bit_depth,
		struct clamping_and_pixel_encoding_params *clamping)
{
	/* dithering is affected by <CrtcSourceSelect>, hence should be
	 * programmed afterwards */

	dce110_opp_program_bit_depth_reduction(
		opp,
		fmt_bit_depth);

	dce110_opp_program_clamping_and_pixel_encoding(
		opp,
		clamping);

	return;
}

/***************************PIPE_CONTROL***********************************/
static void enable_fe_clock(
	struct dc_context *ctx, uint8_t controller_id, bool enable)
{
	uint32_t value = 0;
	uint32_t addr;

	/*TODO: proper offset*/
	addr = HW_REG_DCFE(mmDCFE_CLOCK_CONTROL, controller_id);

	value = dal_read_reg(ctx, addr);

	set_reg_field_value(
		value,
		enable,
		DCFE_CLOCK_CONTROL,
		DCFE_CLOCK_ENABLE);

	dal_write_reg(ctx, addr, value);
}
/*
static void enable_stereo_mixer(
	struct dc_context *ctx,
	const struct crtc_mixer_params *params)
{
	TODO
}
*/
static void disable_stereo_mixer(
	struct dc_context *ctx)
{
	/*TODO*/
}

static void init_pte(struct dc_context *ctx)
{
	uint32_t addr;
	uint32_t value = 0;
	uint32_t chunk_int = 0;
	uint32_t chunk_mul = 0;

	addr = mmUNP_DVMM_PTE_CONTROL;
	value = dal_read_reg(ctx, addr);

	set_reg_field_value(
		value,
		0,
		DVMM_PTE_CONTROL,
		DVMM_USE_SINGLE_PTE);

	set_reg_field_value(
		value,
		1,
		DVMM_PTE_CONTROL,
		DVMM_PTE_BUFFER_MODE0);

	set_reg_field_value(
		value,
		1,
		DVMM_PTE_CONTROL,
		DVMM_PTE_BUFFER_MODE1);

	dal_write_reg(ctx, addr, value);

	addr = mmDVMM_PTE_REQ;
	value = dal_read_reg(ctx, addr);

	chunk_int = get_reg_field_value(
		value,
		DVMM_PTE_REQ,
		HFLIP_PTEREQ_PER_CHUNK_INT);

	chunk_mul = get_reg_field_value(
		value,
		DVMM_PTE_REQ,
		HFLIP_PTEREQ_PER_CHUNK_MULTIPLIER);

	if (chunk_int != 0x4 || chunk_mul != 0x4) {

		set_reg_field_value(
			value,
			255,
			DVMM_PTE_REQ,
			MAX_PTEREQ_TO_ISSUE);

		set_reg_field_value(
			value,
			4,
			DVMM_PTE_REQ,
			HFLIP_PTEREQ_PER_CHUNK_INT);

		set_reg_field_value(
			value,
			4,
			DVMM_PTE_REQ,
			HFLIP_PTEREQ_PER_CHUNK_MULTIPLIER);

		dal_write_reg(ctx, addr, value);
	}
}

/**
 *****************************************************************************
 *  Function: enable_disp_power_gating
 *
 *  @brief
 *     enable or disable power gating
 *
 *  @param [in] enum pipe_gating_control power_gating true - power down,
 *  false - power up
 *****************************************************************************
 */


/* this is a workaround for hw bug - it is a trigger on r/w */

static void trigger_write_crtc_h_blank_start_end(
	struct dc_context *ctx,
	uint8_t controller_id)
{
	uint32_t value;
	uint32_t addr;

	addr =  HW_REG_CRTC(mmCRTC_H_BLANK_START_END, controller_id);
	value = dal_read_reg(ctx, addr);
	dal_write_reg(ctx, addr, value);
}

static bool pipe_control_lock(
	struct dc_context *ctx,
	uint8_t controller_idx,
	uint32_t control_mask,
	bool lock)
{
	uint32_t addr = HW_REG_BLND(mmBLND_V_UPDATE_LOCK, controller_idx);
	uint32_t value = dal_read_reg(ctx, addr);
	bool need_to_wait = false;

	if (control_mask & PIPE_LOCK_CONTROL_GRAPHICS)
		set_reg_field_value(
			value,
			lock,
			BLND_V_UPDATE_LOCK,
			BLND_DCP_GRPH_V_UPDATE_LOCK);

	if (control_mask & PIPE_LOCK_CONTROL_SCL)
		set_reg_field_value(
			value,
			lock,
			BLND_V_UPDATE_LOCK,
			BLND_SCL_V_UPDATE_LOCK);

	if (control_mask & PIPE_LOCK_CONTROL_SURFACE)
		set_reg_field_value(
			value,
			lock,
			BLND_V_UPDATE_LOCK,
			BLND_DCP_GRPH_SURF_V_UPDATE_LOCK);

	if (control_mask & PIPE_LOCK_CONTROL_BLENDER) {
		set_reg_field_value(
			value,
			lock,
			BLND_V_UPDATE_LOCK,
			BLND_BLND_V_UPDATE_LOCK);
		need_to_wait = true;
	}

	if (control_mask & PIPE_LOCK_CONTROL_MODE)
		set_reg_field_value(
			value,
			lock,
			BLND_V_UPDATE_LOCK,
			BLND_V_UPDATE_LOCK_MODE);

	dal_write_reg(ctx, addr, value);

	if (!lock && need_to_wait) {
		uint8_t counter = 0;
		const uint8_t counter_limit = 100;
		const uint16_t delay_us = 1000;

		uint8_t pipe_pending;

		addr = HW_REG_BLND(mmBLND_REG_UPDATE_STATUS,
				controller_idx);

		while (counter < counter_limit) {
			value = dal_read_reg(ctx, addr);

			pipe_pending = 0;

			if (control_mask & PIPE_LOCK_CONTROL_BLENDER) {
				pipe_pending |=
					get_reg_field_value(
						value,
						BLND_REG_UPDATE_STATUS,
						BLND_BLNDC_UPDATE_PENDING);
				pipe_pending |= get_reg_field_value(
					value,
					BLND_REG_UPDATE_STATUS,
					BLND_BLNDO_UPDATE_PENDING);
			}

			if (control_mask & PIPE_LOCK_CONTROL_SCL) {
				pipe_pending |=
					get_reg_field_value(
						value,
						BLND_REG_UPDATE_STATUS,
						SCL_BLNDC_UPDATE_PENDING);
				pipe_pending |=
					get_reg_field_value(
						value,
						BLND_REG_UPDATE_STATUS,
						SCL_BLNDO_UPDATE_PENDING);
			}
			if (control_mask & PIPE_LOCK_CONTROL_GRAPHICS) {
				pipe_pending |=
					get_reg_field_value(
						value,
						BLND_REG_UPDATE_STATUS,
						DCP_BLNDC_GRPH_UPDATE_PENDING);
				pipe_pending |=
					get_reg_field_value(
						value,
						BLND_REG_UPDATE_STATUS,
						DCP_BLNDO_GRPH_UPDATE_PENDING);
			}
			if (control_mask & PIPE_LOCK_CONTROL_SURFACE) {
				pipe_pending |= get_reg_field_value(
					value,
					BLND_REG_UPDATE_STATUS,
					DCP_BLNDC_GRPH_SURF_UPDATE_PENDING);
				pipe_pending |= get_reg_field_value(
					value,
					BLND_REG_UPDATE_STATUS,
					DCP_BLNDO_GRPH_SURF_UPDATE_PENDING);
			}

			if (pipe_pending == 0)
				break;

			counter++;
			dc_service_delay_in_microseconds(ctx, delay_us);
		}

		if (counter == counter_limit) {
			dal_logger_write(
				ctx->logger,
				LOG_MAJOR_WARNING,
				LOG_MINOR_COMPONENT_CONTROLLER,
				"%s: wait for update exceeded (wait %d us)\n",
				__func__,
				counter * delay_us);
			dal_logger_write(
				ctx->logger,
				LOG_MAJOR_WARNING,
				LOG_MINOR_COMPONENT_CONTROLLER,
				"%s: control %d, remain value %x\n",
				__func__,
				control_mask,
				value);
		} else {
			/* OK. */
		}
	}

	if (!lock && (control_mask & PIPE_LOCK_CONTROL_BLENDER))
		trigger_write_crtc_h_blank_start_end(ctx, controller_idx);

	return true;
}

static void set_blender_mode(
	struct dc_context *ctx,
	uint8_t controller_id,
	enum blender_mode mode)
{
	uint32_t value;
	uint32_t addr = HW_REG_BLND(mmBLND_CONTROL, controller_id);
	uint32_t blnd_mode;
	uint32_t feedthrough = 0;

	switch (mode) {
	case BLENDER_MODE_OTHER_PIPE:
		feedthrough = 0;
		blnd_mode = 1;
		break;
	case BLENDER_MODE_BLENDING:
		feedthrough = 0;
		blnd_mode = 2;
		break;
	case BLENDER_MODE_CURRENT_PIPE:
	default:
		feedthrough = 1;
		blnd_mode = 0;
		break;
	}

	value = dal_read_reg(ctx, addr);

	set_reg_field_value(
		value,
		feedthrough,
		BLND_CONTROL,
		BLND_FEEDTHROUGH_EN);

	set_reg_field_value(
		value,
		blnd_mode,
		BLND_CONTROL,
		BLND_MODE);

	dal_write_reg(ctx, addr, value);
}
/**************************************************************************/
static void update_bios_scratch_critical_state(struct adapter_service *as,
		bool state)
{
	struct dc_bios *dcb = dal_adapter_service_get_bios_parser(as);

	dcb->funcs->set_scratch_critical_state(dcb, state);
}

static void update_info_frame(struct core_stream *stream)
{
	if (dc_is_hdmi_signal(stream->signal))
		stream->stream_enc->funcs->update_hdmi_info_packets(
			stream->stream_enc,
			&stream->encoder_info_frame);
	else if (dc_is_dp_signal(stream->signal))
		stream->stream_enc->funcs->update_dp_info_packets(
			stream->stream_enc,
			&stream->encoder_info_frame);
}


static void enable_stream(struct core_stream *stream)
{
	enum lane_count lane_count =
			stream->sink->link->cur_link_settings.lane_count;

	struct dc_crtc_timing *timing = &stream->public.timing;
	struct core_link *link = stream->sink->link;

	/* 1. update AVI info frame (HDMI, DP)
	 * we always need to update info frame
	*/
	uint32_t active_total_with_borders;
	uint32_t early_control = 0;
	struct timing_generator *tg = stream->tg;

	update_info_frame(stream);
	/* enable early control to avoid corruption on DP monitor*/
	active_total_with_borders =
			timing->h_addressable
				+ timing->h_border_left
				+ timing->h_border_right;

	if (lane_count != 0)
		early_control = active_total_with_borders % lane_count;

	if (early_control == 0)
		early_control = lane_count;

	tg->funcs->set_early_control(tg, early_control);

	/* enable audio only within mode set */
	if (stream->audio != NULL) {
		dal_audio_enable_output(
			stream->audio,
			stream->stream_enc->id,
			stream->signal);
	}

	/* For MST, there are multiply stream go to only one link.
	 * connect DIG back_end to front_end while enable_stream and
	 * disconnect them during disable_stream
	 * BY this, it is logic clean to separate stream and link */
	 link->link_enc->funcs->connect_dig_be_to_fe(link->link_enc,
			stream->stream_enc->id, true);

}

static void disable_stream(struct core_stream *stream)
{
	struct core_link *link = stream->sink->link;

	if (dc_is_hdmi_signal(stream->signal))
		stream->stream_enc->funcs->stop_hdmi_info_packets(
			stream->stream_enc);

	if (dc_is_dp_signal(stream->signal))
		stream->stream_enc->funcs->stop_dp_info_packets(
			stream->stream_enc);

	if (stream->audio) {
		/* mute audio */
		dal_audio_mute(stream->audio, stream->stream_enc->id,
				stream->signal);

		/* TODO: notify audio driver for if audio modes list changed
		 * add audio mode list change flag */
		/* dal_audio_disable_azalia_audio_jack_presence(stream->audio,
		 * stream->stream_engine_id);
		 */
	}

	/* blank at encoder level */
	if (dc_is_dp_signal(stream->signal))
		stream->stream_enc->funcs->dp_blank(stream->stream_enc);

	link->link_enc->funcs->connect_dig_be_to_fe(
			link->link_enc,
			stream->stream_enc->id,
			false);

}

static void unblank_stream(struct core_stream *stream,
		struct link_settings *link_settings)
{
	struct encoder_unblank_param params = { { 0 } };

	/* only 3 items below are used by unblank */
	params.crtc_timing.pixel_clock =
		stream->public.timing.pix_clk_khz;
	params.link_settings.link_rate = link_settings->link_rate;
	stream->stream_enc->funcs->dp_unblank(
		stream->stream_enc, &params);
}

static enum color_space get_output_color_space(
				const struct dc_crtc_timing *dc_crtc_timing)
{
	enum color_space color_space = COLOR_SPACE_SRGB_FULL_RANGE;

	switch (dc_crtc_timing->pixel_encoding)	{
	case PIXEL_ENCODING_YCBCR422:
	case PIXEL_ENCODING_YCBCR444:
	case PIXEL_ENCODING_YCBCR420:
	{
		if ((dc_crtc_timing->timing_standard ==
			TIMING_STANDARD_CEA770) ||
			(dc_crtc_timing->timing_standard ==
				TIMING_STANDARD_CEA861)) {
			if (dc_crtc_timing->pix_clk_khz > 27030) {
				if (dc_crtc_timing->flags.Y_ONLY)
					color_space =
						COLOR_SPACE_YCBCR709_YONLY;
				else
					color_space = COLOR_SPACE_YCBCR709;
			} else {
				if (dc_crtc_timing->flags.Y_ONLY)
					color_space =
						COLOR_SPACE_YCBCR601_YONLY;
				else
					color_space = COLOR_SPACE_YCBCR601;
			}
		}
	}
	break;

	default:
		break;
	}

	return color_space;
}

static enum dc_status apply_single_controller_ctx_to_hw(uint8_t controller_idx,
		struct validate_context *context,
		const struct dc *dc)
{
	struct core_stream *stream =
			context->res_ctx.controller_ctx[controller_idx].stream;
	struct output_pixel_processor *opp =
		context->res_ctx.pool.opps[controller_idx];
	bool timing_changed = context->res_ctx.controller_ctx[controller_idx]
			.flags.timing_changed;
	enum color_space color_space;
	struct dc_bios *dcb;

	dcb = dal_adapter_service_get_bios_parser(
			context->res_ctx.pool.adapter_srv);

	if (timing_changed) {
		dce110_enable_display_power_gating(
				stream->ctx, controller_idx, dcb,
				PIPE_GATING_CONTROL_DISABLE);

		/* Must blank CRTC after disabling power gating and before any
		 * programming, otherwise CRTC will be hung in bad state
		 */
		stream->tg->funcs->set_blank(stream->tg, true);

		core_link_disable_stream(stream->sink->link, stream);

		/*TODO: AUTO check if timing changed*/
		if (false == dal_clock_source_program_pix_clk(
				stream->clock_source,
				&stream->pix_clk_params,
				&stream->pll_settings)) {
			BREAK_TO_DEBUGGER();
			return DC_ERROR_UNEXPECTED;
		}

		stream->tg->funcs->program_timing(
				stream->tg,
				&stream->public.timing,
				true);
	}

	/*TODO: mst support - use total stream count*/
	stream->mi->funcs->mem_input_allocate_dmif_buffer(
					stream->mi,
					&stream->public.timing,
					context->target_count);

	if (timing_changed) {
		if (false == stream->tg->funcs->enable_crtc(
				stream->tg)) {
			BREAK_TO_DEBUGGER();
			return DC_ERROR_UNEXPECTED;
		}
	}

	/* TODO: move to stream encoder */
	if (stream->signal != SIGNAL_TYPE_VIRTUAL)
		if (DC_OK != bios_parser_crtc_source_select(stream)) {
			BREAK_TO_DEBUGGER();
			return DC_ERROR_UNEXPECTED;
		}

	dce110_opp_set_dyn_expansion(
			opp,
			COLOR_SPACE_YCBCR601,
			stream->public.timing.display_color_depth,
			stream->sink->public.sink_signal);

	program_fmt(opp, &stream->fmt_bit_depth, &stream->clamping);

	stream->sink->link->link_enc->funcs->setup(
		stream->sink->link->link_enc,
		stream->signal);

	if (dc_is_dp_signal(stream->signal))
		stream->stream_enc->funcs->dp_set_stream_attribute(
			stream->stream_enc,
			&stream->public.timing);

	if (dc_is_hdmi_signal(stream->signal))
		stream->stream_enc->funcs->hdmi_set_stream_attribute(
			stream->stream_enc,
			&stream->public.timing,
			stream->audio != NULL);

	if (dc_is_dvi_signal(stream->signal))
		stream->stream_enc->funcs->dvi_set_stream_attribute(
			stream->stream_enc,
			&stream->public.timing,
			(stream->signal == SIGNAL_TYPE_DVI_DUAL_LINK) ?
			true : false);

	if (stream->audio != NULL) {
		if (AUDIO_RESULT_OK != dal_audio_setup(
				stream->audio,
				&stream->audio_output,
				&stream->public.audio_info)) {
			BREAK_TO_DEBUGGER();
			return DC_ERROR_UNEXPECTED;
		}
	}

	/* Setup audio rate clock source */
	if (stream->audio != NULL)
		dal_audio_setup_audio_wall_dto(
				stream->audio,
				stream->signal,
				&stream->audio_output.crtc_info,
				&stream->audio_output.pll_info);

	/* program blank color */
	color_space = get_output_color_space(&stream->public.timing);
	stream->tg->funcs->set_blank_color(
			context->res_ctx.pool.timing_generators[controller_idx],
			color_space);

	if (timing_changed)
		core_link_enable_stream(stream->sink->link, stream);

	if (dc_is_dp_signal(stream->signal))
		unblank_stream(stream, &stream->sink->link->cur_link_settings);

	return DC_OK;
}


/******************************************************************************/

static void power_down_encoders(struct dc *dc)
{
	int i;

	for (i = 0; i < dc->link_count; i++) {
		dc->links[i]->link_enc->funcs->disable_output(
				dc->links[i]->link_enc, SIGNAL_TYPE_NONE);
	}
}

static void power_down_controllers(struct dc *dc)
{
	int i;

	for (i = 0; i < dc->res_pool.controller_count; i++) {
		dc->res_pool.timing_generators[i]->funcs->disable_crtc(
				dc->res_pool.timing_generators[i]);
	}
}

static void power_down_clock_sources(struct dc *dc)
{
	int i;

	for (i = 0; i < dc->res_pool.clk_src_count; i++) {
		if (false == dal_clock_source_power_down_pll(
				dc->res_pool.clock_sources[i],
				i+1)) {
			dal_error(
				"Failed to power down pll! (clk src index=%d)\n",
				i);
		}
	}
}

static void power_down_all_hw_blocks(struct dc *dc)
{
	power_down_encoders(dc);

	power_down_controllers(dc);

	power_down_clock_sources(dc);
}

static void disable_vga_and_power_gate_all_controllers(
		struct dc *dc)
{
	int i;
	struct timing_generator *tg;
	struct dc_bios *dcb;
	struct dc_context *ctx;

	dcb = dal_adapter_service_get_bios_parser(
			dc->res_pool.adapter_srv);

	for (i = 0; i < dc->res_pool.controller_count; i++) {
		tg = dc->res_pool.timing_generators[i];
		ctx = dc->ctx;

		tg->funcs->disable_vga(tg);

		/* Enable CLOCK gating for each pipe BEFORE controller
		 * powergating. */
		dce110_enable_display_pipe_clock_gating(ctx,
				true);
		dce110_enable_display_power_gating(ctx, i, dcb,
				PIPE_GATING_CONTROL_ENABLE);
	}
}

/**
 * When ASIC goes from VBIOS/VGA mode to driver/accelerated mode we need:
 *  1. Power down all DC HW blocks
 *  2. Disable VGA engine on all controllers
 *  3. Enable power gating for controller
 *  4. Set acc_mode_change bit (VBIOS will clear this bit when going to FSDOS)
 */
static void enable_accelerated_mode(struct dc *dc)
{
	struct dc_bios *dcb;

	dcb = dal_adapter_service_get_bios_parser(dc->res_pool.adapter_srv);

	power_down_all_hw_blocks(dc);

	disable_vga_and_power_gate_all_controllers(dc);

	dcb->funcs->set_scratch_acc_mode_change(dcb);
}

#if 0
static enum clocks_state get_required_clocks_state(
	struct display_clock *display_clock,
	struct state_dependent_clocks *req_state_dep_clks)
{
	enum clocks_state clocks_required_state;
	enum clocks_state dp_link_required_state;
	enum clocks_state overall_required_state;

	clocks_required_state = dal_display_clock_get_required_clocks_state(
			display_clock, req_state_dep_clks);

	dp_link_required_state = CLOCKS_STATE_ULTRA_LOW;

	/* overall required state is the max of required state for clocks
	 * (pixel, display clock) and the required state for DP link. */
	overall_required_state =
		clocks_required_state > dp_link_required_state ?
			clocks_required_state : dp_link_required_state;

	/* return the min required state */
	return overall_required_state;
}

static bool dc_pre_clock_change(
		struct dc_context *ctx,
		struct minimum_clocks_calculation_result *min_clk_in,
		enum clocks_state required_clocks_state,
		struct power_to_dal_info *output)
{
	struct dal_to_power_info input = {0};

	input.min_deep_sleep_sclk = min_clk_in->min_deep_sleep_sclk;
	input.min_mclk = min_clk_in->min_mclk_khz;
	input.min_sclk = min_clk_in->min_sclk_khz;

	switch (required_clocks_state) {
	case CLOCKS_STATE_ULTRA_LOW:
		input.required_clock = PP_CLOCKS_STATE_ULTRA_LOW;
		break;
	case CLOCKS_STATE_LOW:
		input.required_clock = PP_CLOCKS_STATE_LOW;
		break;
	case CLOCKS_STATE_NOMINAL:
		input.required_clock = PP_CLOCKS_STATE_NOMINAL;
		break;
	case CLOCKS_STATE_PERFORMANCE:
		input.required_clock = PP_CLOCKS_STATE_PERFORMANCE;
		break;
	default:
		input.required_clock = PP_CLOCKS_STATE_NOMINAL;
		break;
	}

	if (!dc_service_pp_pre_dce_clock_change(ctx, &input, output)) {
		dal_error("DC: dc_service_pp_pre_dce_clock_change failed!\n");
		return false;
	}

	return true;
}

static bool dc_set_clocks_and_clock_state (
		struct validate_context *context)
{
	struct power_to_dal_info output = {0};

	struct display_clock *disp_clk = context->res_ctx.pool.display_clock;
	struct dc_context *ctx = context->targets[0]->ctx;


	if (!dc_pre_clock_change(
			ctx,
			&context->res_ctx.min_clocks,
			get_required_clocks_state(
					context->res_ctx.pool.display_clock,
					&context->res_ctx.state_clocks),
			&output)) {
		/* "output" was not updated by PPLib.
		 * DAL will use default values for set mode.
		 *
		 * Do NOT fail this call. */
		return true;
	}

	/* PPLib accepted the "clock state" that we need, that means we
	 * can store it as minimum state because PPLib guarantees not go below
	 * that state.
	 *
	 * Update the clock state here (prior to setting Pixel clock,
	 * or Display clock)
	 **/
	if (!dal_display_clock_set_min_clocks_state(
			disp_clk, context->res_ctx.required_clocks_state)) {
		BREAK_TO_DEBUGGER();
		dal_error("DC: failed to set minimum clock state!\n");
	}


	/*bm_clk_info.max_mclk_khz = output.max_mclk;
	bm_clk_info.min_mclk_khz = output.min_mclk;
	bm_clk_info.max_sclk_khz = output.max_sclk;
	bm_clk_info.min_sclk_khz = output.min_sclk;*/

	/* Now let Bandwidth Manager know about values we got from PPLib. */
	/*dal_bandwidth_manager_set_dynamic_clock_info(bw_mgr, &bm_clk_info);*/

	return true;
}
#endif

/**
 * Call display_engine_clock_dce80 to perform the Dclk programming.
 */
static void set_display_clock(struct validate_context *context)
{
	/* Program the display engine clock.
	 * Check DFS bypass mode support or not. DFSbypass feature is only when
	 * BIOS GPU info table reports support. */

	if (/*dal_adapter_service_is_dfs_bypass_enabled()*/ false) {
		/*TODO: set_display_clock_dfs_bypass(
				hws,
				path_set,
				context->res_ctx.pool.display_clock,
				context->res_ctx.min_clocks.min_dclk_khz);*/
	} else
		dal_display_clock_set_clock(context->res_ctx.pool.display_clock,
				context->bw_results.dispclk_khz);

	/* TODO: When changing display engine clock, DMCU WaitLoop must be
	 * reconfigured in order to maintain the same delays within DMCU
	 * programming sequences. */

	/* TODO: Start GTC counter */
}

static void set_displaymarks(
	const struct dc *dc, struct validate_context *context)
{
	uint8_t i, j;
	uint8_t total_streams = 0;
	uint8_t target_count = context->target_count;

	for (i = 0; i < target_count; i++) {
		struct core_target *target = context->targets[i];

		for (j = 0; j < target->public.stream_count; j++) {
			struct core_stream *stream =
				DC_STREAM_TO_CORE(target->public.streams[j]);

			stream->mi->funcs->mem_input_program_display_marks(
				stream->mi,
				context->bw_results
				.nbp_state_change_wm_ns[total_streams],
				context->bw_results
					.stutter_exit_wm_ns[total_streams],
				context->bw_results
					.urgent_wm_ns[total_streams],
				stream->public.timing.h_total,
				stream->public.timing.pix_clk_khz,
				1000 * dc->bw_vbios.blackout_duration
								.value >> 24);
			total_streams++;
		}
	}
}

static void set_safe_displaymarks(struct validate_context *context)
{
	uint8_t i, j;
	uint8_t target_count = context->target_count;

	for (i = 0; i < target_count; i++) {
		struct core_target *target = context->targets[i];

		for (j = 0; j < target->public.stream_count; j++) {
			struct core_stream *stream =
				DC_STREAM_TO_CORE(target->public.streams[j]);

			stream->mi->funcs->mem_input_program_safe_display_marks(
					stream->mi);
		}
	}
}

static void dce110_program_bw(struct dc *dc, struct validate_context *context)
{
	set_safe_displaymarks(context);
	/*TODO: when pplib works*/
	/*dc_set_clocks_and_clock_state(context);*/

	set_display_clock(context);
	set_displaymarks(dc, context);
}

/*TODO: break out clock sources like timing gen/ encoder*/
static void dce110_switch_dp_clk_src(
	const struct dc_context *ctx,
	const struct core_stream *stream)
{
	uint32_t pixel_rate_cntl_value;
	uint32_t addr;
	enum clock_source_id id = dal_clock_source_get_id(stream->clock_source);

	/*TODO: proper offset*/
	addr = mmCRTC0_PIXEL_RATE_CNTL + stream->controller_idx *
			(mmCRTC1_PIXEL_RATE_CNTL - mmCRTC0_PIXEL_RATE_CNTL);

	pixel_rate_cntl_value = dal_read_reg(ctx, addr);

	if (id == CLOCK_SOURCE_ID_EXTERNAL) {

		if (!get_reg_field_value(pixel_rate_cntl_value,
				CRTC0_PIXEL_RATE_CNTL, DP_DTO0_ENABLE)) {

			set_reg_field_value(pixel_rate_cntl_value, 1,
				CRTC0_PIXEL_RATE_CNTL, DP_DTO0_ENABLE);
		}

	} else {
		set_reg_field_value(pixel_rate_cntl_value,
				0,
				CRTC0_PIXEL_RATE_CNTL,
				DP_DTO0_ENABLE);

		set_reg_field_value(pixel_rate_cntl_value,
				id - 1,
				CRTC0_PIXEL_RATE_CNTL,
				CRTC0_PIXEL_RATE_SOURCE);
	}
	dal_write_reg(ctx, addr, pixel_rate_cntl_value);
}

static void switch_dp_clock_sources(
	const struct dc_context *ctx,
	struct validate_context *val_context)
{
	uint8_t i, j;
	for (i = 0; i < val_context->target_count; i++) {
		struct core_target *target = val_context->targets[i];
		for (j = 0; j < target->public.stream_count; j++) {
			struct core_stream *stream =
				DC_STREAM_TO_CORE(target->public.streams[j]);

			if (dc_is_dp_signal(stream->signal)) {
				struct clock_source *clk_src =
					find_used_clk_src_for_sharing(
							val_context, stream);

				if (clk_src &&
					clk_src != stream->clock_source) {
					unreference_clock_source(
							&val_context->res_ctx,
							stream->clock_source);
					stream->clock_source = clk_src;
					reference_clock_source(
						&val_context->res_ctx, clk_src);
					dce110_switch_dp_clk_src(ctx, stream);
				}
			}
		}
	}
}

/*******************************************************************************
 * Public functions
 ******************************************************************************/

/*TODO: const validate_context*/
static enum dc_status apply_ctx_to_hw(
		const struct dc *dc,
		struct validate_context *context)
{
	enum dc_status status;
	uint8_t i;
	struct resource_pool *pool = &context->res_ctx.pool;

	update_bios_scratch_critical_state(context->res_ctx.pool.adapter_srv,
			true);
	set_safe_displaymarks(context);
	/*TODO: when pplib works*/
	/*dc_set_clocks_and_clock_state(context);*/

	if (context->bw_results.dispclk_khz
		> dc->current_context.bw_results.dispclk_khz)
		set_display_clock(context);

	for (i = 0; i < pool->controller_count; i++) {
		struct controller_ctx *ctlr_ctx
			= &context->res_ctx.controller_ctx[i];
		if (ctlr_ctx->flags.unchanged || !ctlr_ctx->stream)
			continue;

		status = apply_single_controller_ctx_to_hw(
				i,
				context,
				dc);

		if (DC_OK != status)
			return status;
	}
	set_displaymarks(dc, context);

	update_bios_scratch_critical_state(context->res_ctx.pool.adapter_srv,
			false);

	switch_dp_clock_sources(dc->ctx, context);

	return DC_OK;
}


/*******************************************************************************
 * Front End programming
 ******************************************************************************/

static bool setup_line_buffer_pixel_depth(
	const struct core_stream *stream,
	enum lb_pixel_depth depth,
	bool blank)
{
	enum lb_pixel_depth current_depth;

	struct timing_generator *tg = stream->tg;
	struct transform *xfm = stream->xfm;

	if (!xfm->funcs->transform_get_current_pixel_storage_depth(
		xfm,
		&current_depth))
		return false;

	if (current_depth != depth) {
		if (blank)
			tg->funcs->wait_for_state(tg, CRTC_STATE_VBLANK);

		return xfm->funcs->transform_set_pixel_storage_depth(xfm, depth);
	}

	return false;
}

static void hw_sequencer_build_scaler_parameter_plane(
		const struct core_stream *stream,
		struct scaler_data *scaler_data)
{
	/*TODO: per pipe not per stream*/
	/*TODO: get from feature from adapterservice*/
	scaler_data->flags.bits.SHOW_COLOURED_BORDER = false;

	scaler_data->flags.bits.SHOULD_PROGRAM_ALPHA = 1;

	scaler_data->flags.bits.SHOULD_PROGRAM_VIEWPORT = 0;

	scaler_data->flags.bits.SHOULD_UNLOCK = 0;

	scaler_data->flags.bits.INTERLACED = 0;

	scaler_data->dal_pixel_format = stream->format;

	scaler_data->taps = stream->taps;

	scaler_data->viewport = stream->viewport;

	scaler_data->overscan = stream->overscan;

	scaler_data->ratios = &stream->ratios;

	/*TODO rotation and adjustment */
	scaler_data->h_sharpness = 0;
	scaler_data->v_sharpness = 0;

}

static void set_default_colors(
				struct input_pixel_processor *ipp,
				struct output_pixel_processor *opp,
				enum pixel_format format,
				enum color_space input_color_space,
				enum color_space output_color_space,
				enum dc_color_depth color_depth)
{
	struct default_adjustment default_adjust = { 0 };

	default_adjust.force_hw_default = false;
	default_adjust.color_space = output_color_space;
	default_adjust.csc_adjust_type = GRAPHICS_CSC_ADJUST_TYPE_SW;
	default_adjust.surface_pixel_format = format;

	/* display color depth */
	default_adjust.color_depth = color_depth;

	/* Lb color depth */
	default_adjust.lb_color_depth = LB_PIXEL_DEPTH_24BPP;
	/*dal_hw_sequencer_translate_to_lb_color_depth(
			build_params->
			line_buffer_params[path_id][plane_id].depth);*/

	dce110_opp_set_csc_default(opp, &default_adjust);
}

static void program_scaler(
	uint8_t controller_idx,
	struct timing_generator *tg,
	struct transform *xfm,
	const struct core_surface *surface,
	const struct core_stream *stream)
{
	struct scaler_data scaler_data = { { 0 } };

	hw_sequencer_build_scaler_parameter_plane(
		stream,
		&scaler_data);

	setup_line_buffer_pixel_depth(
		stream,
		LB_PIXEL_DEPTH_24BPP,
		false);

	tg->funcs->set_overscan_blank_color(tg, surface->public.colorimetry.color_space);

	xfm->funcs->transform_set_scaler(xfm, &scaler_data);

	xfm->funcs->transform_update_viewport(
			xfm,
			&scaler_data.viewport,
			false);
}

static void configure_locking(struct dc_context *ctx, uint8_t controller_id)
{
	/* main controller should be in mode 0 (master pipe) */
	pipe_control_lock(
			ctx,
			controller_id,
			PIPE_LOCK_CONTROL_MODE,
			false);

	/* TODO: for MPO finish the non-root controllers */
}

/**
 * Program the Front End of the Pipe.
 * The Back End was already programmed by Set Mode.
 */
static bool set_plane_config(
	struct core_surface *surface,
	struct core_target *target)
{
	const struct core_stream *core_stream =
		DC_STREAM_TO_CORE(target->public.streams[0]);
	const struct dc_crtc_timing *dc_crtc_timing =
			&target->public.streams[0]->timing;
	struct mem_input *mi = core_stream->mi;
	struct input_pixel_processor *ipp = core_stream->ipp;
	struct timing_generator *tg = core_stream->tg;
	struct transform *xfm = core_stream->xfm;
	struct output_pixel_processor *opp = core_stream->opp;
	struct dc_context *ctx = core_stream->ctx;
	uint8_t controller_idx = core_stream->controller_idx;

	/* TODO: Clean up change, possibly change to use same type */
	enum color_space input_color_space =
					surface_color_to_color_space(&(surface->public.colorimetry));

	configure_locking(ctx, controller_idx);

	/* While a non-root controller is programmed we
	 * have to lock the root controller. */
	pipe_control_lock(
			ctx,
			controller_idx,
			PIPE_LOCK_CONTROL_GRAPHICS |
			PIPE_LOCK_CONTROL_SCL |
			PIPE_LOCK_CONTROL_BLENDER |
			PIPE_LOCK_CONTROL_SURFACE,
			true);

	tg->funcs->program_timing(tg, dc_crtc_timing, false);

	enable_fe_clock(ctx, controller_idx, true);

	set_default_colors(
			ipp,
			opp,
			core_stream->format,
			input_color_space,
			get_output_color_space(dc_crtc_timing),
			dc_crtc_timing->display_color_depth);

	/* program Scaler */
	program_scaler(
		controller_idx, tg, xfm, surface, core_stream);

	set_blender_mode(
			ctx,
			controller_idx,
			BLENDER_MODE_CURRENT_PIPE);

	mi->funcs->mem_input_program_surface_config(
			mi,
			surface->public.format,
			&surface->public.tiling_info,
			&surface->public.plane_size,
			surface->public.rotation);

	pipe_control_lock(
			ctx,
			controller_idx,
			PIPE_LOCK_CONTROL_GRAPHICS |
			PIPE_LOCK_CONTROL_SCL |
			PIPE_LOCK_CONTROL_BLENDER |
			PIPE_LOCK_CONTROL_SURFACE,
			false);

	return true;
}

static bool update_plane_address(
	const struct core_surface *surface,
	struct core_target *target)
{
	const struct core_stream *core_stream =
		DC_STREAM_TO_CORE(target->public.streams[0]);
	struct dc_context *ctx = core_stream->ctx;
	struct mem_input *mi = core_stream->mi;
	uint8_t controller_id = core_stream->controller_idx;

	/* TODO: crtc should be per surface, NOT per-target */
	pipe_control_lock(
		ctx,
		controller_id,
		PIPE_LOCK_CONTROL_SURFACE,
		true);

	if (false ==
		core_stream->mi->funcs->mem_input_program_surface_flip_and_addr(
		mi, &surface->public.address, surface->public.flip_immediate))
		return false;

	pipe_control_lock(
		ctx,
		controller_id,
		PIPE_LOCK_CONTROL_SURFACE,
		false);

	return true;
}

static void reset_single_stream_hw_ctx(struct core_stream *stream,
		struct validate_context *context)
{
	struct dc_bios *dcb;

	dcb = dal_adapter_service_get_bios_parser(
			context->res_ctx.pool.adapter_srv);
	if (stream->audio) {
		dal_audio_disable_output(stream->audio,
				stream->stream_enc->id,
				stream->signal);
		stream->audio = NULL;
	}

	core_link_disable_stream(stream->sink->link, stream);

	stream->tg->funcs->set_blank(stream->tg, true);
	stream->tg->funcs->disable_crtc(stream->tg);
	stream->mi->funcs->mem_input_deallocate_dmif_buffer(
			stream->mi, context->target_count);
	stream->xfm->funcs->transform_set_scaler_bypass(stream->xfm);
	disable_stereo_mixer(stream->ctx);
	unreference_clock_source(&context->res_ctx, stream->clock_source);
	dce110_enable_display_power_gating(
			stream->ctx, stream->controller_idx, dcb,
			PIPE_GATING_CONTROL_ENABLE);
}

static void reset_hw_ctx(struct dc *dc,
		struct validate_context *context,
		uint8_t target_count)
{
	uint8_t i;
	/* look up the targets that have been removed since last commit */
	for (i = 0; i < dc->current_context.target_count; i++) {
		const struct core_target *core_target =
			dc->current_context.targets[i];
		struct core_stream *core_stream =
			DC_STREAM_TO_CORE(core_target->public.streams[0]);
		uint8_t controller_idx = core_stream->controller_idx;

		if (context->res_ctx.controller_ctx[controller_idx].stream &&
				!context->res_ctx.controller_ctx[controller_idx]
				.flags.timing_changed)
			continue;

		reset_single_stream_hw_ctx(core_stream, &dc->current_context);
	}
}

static void power_down(struct dc *dc)
{
	power_down_all_hw_blocks(dc);
	disable_vga_and_power_gate_all_controllers(dc);

}

static bool wait_for_reset_trigger_to_occur(
	struct dc_context *dc_ctx,
	struct timing_generator *tg)
{
	bool rc = false;

	/* To avoid endless loop we wait at most
	 * frames_to_wait_on_triggered_reset frames for the reset to occur. */
	const uint32_t frames_to_wait_on_triggered_reset = 10;
	uint32_t i;

	for (i = 0; i < frames_to_wait_on_triggered_reset; i++) {

		if (!tg->funcs->is_counter_moving(tg)) {
			DC_ERROR("TG counter is not moving!\n");
			break;
		}

		if (tg->funcs->did_triggered_reset_occur(tg)) {
			rc = true;
			/* usually occurs at i=1 */
			DC_SYNC_INFO("GSL: reset occurred at wait count: %d\n",
					i);
			break;
		}

		/* Wait for one frame. */
		tg->funcs->wait_for_state(tg, CRTC_STATE_VACTIVE);
		tg->funcs->wait_for_state(tg, CRTC_STATE_VBLANK);
	}

	if (false == rc)
		DC_ERROR("GSL: Timeout on reset trigger!\n");

	return rc;
}

/* Enable timing synchronization for a group of Timing Generators. */
static void enable_timing_synchronization(
	struct dc_context *dc_ctx,
	uint32_t timing_generator_num,
	struct timing_generator *tgs[])
{
	struct dcp_gsl_params gsl_params = { 0 };
	struct trigger_params trigger_params;
	uint32_t i;

	DC_SYNC_INFO("GSL: Setting-up...\n");

	gsl_params.gsl_group = SYNC_SOURCE_GSL_GROUP0;
	gsl_params.gsl_purpose = DCP_GSL_PURPOSE_SURFACE_FLIP;

	for (i = 0; i < timing_generator_num; i++) {
		/* Designate a single TG in the group as a master.
		 * Since HW doesn't care which one, we always assign
		 * the 1st one in the group. */
		gsl_params.timing_server = (0 == i ? true : false);

		tgs[i]->funcs->setup_global_swap_lock(tgs[i], &gsl_params);
	}

	/* Reset slave controllers on master VSync */
	DC_SYNC_INFO("GSL: enabling trigger-reset\n");
	dc_service_memset(&trigger_params, 0, sizeof(trigger_params));

	trigger_params.edge = TRIGGER_EDGE_DEFAULT;
	trigger_params.source = SYNC_SOURCE_GSL_GROUP0;

	for (i = 1 /* skip the master */; i < timing_generator_num; i++) {
		tgs[i]->funcs->enable_reset_trigger(tgs[i], &trigger_params);

		DC_SYNC_INFO("GSL: waiting for reset to occur.\n");
		wait_for_reset_trigger_to_occur(dc_ctx, tgs[i]);

		/* Regardless of success of the wait above, remove the reset or
		 * the driver will start timing out on Display requests. */
		DC_SYNC_INFO("GSL: disabling trigger-reset.\n");
		tgs[i]->funcs->disable_reset_trigger(tgs[i]);
	}

	/* GSL Vblank synchronization is a one time sync mechanism, assumption
	 * is that the sync'ed displays will not drift out of sync over time*/
	DC_SYNC_INFO("GSL: Restoring register states.\n");
	for (i = 0; i < timing_generator_num; i++)
		tgs[i]->funcs->tear_down_global_swap_lock(tgs[i]);

	DC_SYNC_INFO("GSL: Set-up complete.\n");
}

static void get_crtc_positions(struct timing_generator *tg,
		int32_t *h_position, int32_t *v_position)
{
	tg->funcs->get_position(tg, h_position, v_position);
}

static bool enable_memory_request(struct timing_generator *tg)
{
	return tg->funcs->set_blank(tg, false);
}

static bool disable_memory_requests(struct timing_generator *tg)
{
	return tg->funcs->set_blank(tg, true);
}

static uint32_t get_vblank_counter(struct timing_generator *tg)
{
	return tg->funcs->get_frame_count(tg);
}

static void disable_vga(struct timing_generator *tg)
{
	tg->funcs->disable_vga(tg);
}

static void set_mst_bandwidth(struct stream_encoder *stream_enc,
	struct fixed31_32 avg_time_slots_per_mtp)
{
	stream_enc->funcs->set_mst_bandwidth(stream_enc,
		avg_time_slots_per_mtp);
}


static const struct hw_sequencer_funcs dce110_funcs = {
	.apply_ctx_to_hw = apply_ctx_to_hw,
	.reset_hw_ctx = reset_hw_ctx,
	.set_plane_config = set_plane_config,
	.update_plane_address = update_plane_address,
	.enable_memory_requests = enable_memory_request,
	.disable_memory_requests = disable_memory_requests,
	.set_gamma_ramp = set_gamma_ramp,
	.power_down = power_down,
	.enable_accelerated_mode = enable_accelerated_mode,
	.get_crtc_positions = get_crtc_positions,
	.get_vblank_counter = get_vblank_counter,
	.enable_timing_synchronization = enable_timing_synchronization,
	.disable_vga = disable_vga,
	.encoder_create = dce110_link_encoder_create,
	.encoder_destroy = dce110_link_encoder_destroy,
	.clock_gating_power_up = dal_dc_clock_gating_dce110_power_up,
	.transform_power_up = dce110_transform_power_up,
	.construct_resource_pool = dce110_construct_resource_pool,
	.destruct_resource_pool = dce110_destruct_resource_pool,
	.validate_with_context = dce110_validate_with_context,
	.validate_bandwidth = dce110_validate_bandwidth,
	.enable_display_pipe_clock_gating =
		dce110_enable_display_pipe_clock_gating,
	.enable_display_power_gating = dce110_enable_display_power_gating,
	.program_bw = dce110_program_bw,
	.enable_stream = enable_stream,
	.disable_stream = disable_stream,
	.set_mst_bandwidth = set_mst_bandwidth,
};

bool dce110_hw_sequencer_construct(struct dc *dc)
{
	dc->hwss = dce110_funcs;

	return true;
}

