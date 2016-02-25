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

#include "link_encoder.h"
#include "stream_encoder.h"

#include "resource.h"
#include "include/irq_service_interface.h"
#include "../virtual/virtual_stream_encoder.h"
#include "dce110/dce110_timing_generator.h"
#include "dce110/dce110_mem_input.h"
#include "dce110/dce110_resource.h"
#include "dce80/dce80_timing_generator.h"
#include "dce80/dce80_link_encoder.h"
#include "dce110/dce110_link_encoder.h"
#include "dce80/dce80_mem_input.h"
#include "dce80/dce80_ipp.h"
#include "dce80/dce80_transform.h"
#include "dce110/dce110_stream_encoder.h"
#include "dce80/dce80_stream_encoder.h"
#include "dce80/dce80_opp.h"
#include "dce110/dce110_ipp.h"
#include "dce110/dce110_clock_source.h"

#include "dce/dce_8_0_d.h"

/* TODO remove this include */

#ifndef mmDP_DPHY_INTERNAL_CTRL
#define mmDP_DPHY_INTERNAL_CTRL                         0x1CDE
#define mmDP0_DP_DPHY_INTERNAL_CTRL                     0x1CDE
#define mmDP1_DP_DPHY_INTERNAL_CTRL                     0x1FDE
#define mmDP2_DP_DPHY_INTERNAL_CTRL                     0x42DE
#define mmDP3_DP_DPHY_INTERNAL_CTRL                     0x45DE
#define mmDP4_DP_DPHY_INTERNAL_CTRL                     0x48DE
#define mmDP5_DP_DPHY_INTERNAL_CTRL                     0x4BDE
#define mmDP6_DP_DPHY_INTERNAL_CTRL                     0x4EDE
#endif

enum dce80_clk_src_array_id {
	DCE80_CLK_SRC0 = 0,
	DCE80_CLK_SRC1,
	DCE80_CLK_SRC2,

	DCE80_CLK_SRC_TOTAL
};

#define DCE11_DIG_FE_CNTL 0x4a00
#define DCE11_DIG_BE_CNTL 0x4a47
#define DCE11_DP_SEC 0x4ac3

static const struct dce110_timing_generator_offsets dce80_tg_offsets[] = {
		{
			.crtc = (mmCRTC0_CRTC_CONTROL - mmCRTC_CONTROL),
			.dcp =  (mmGRPH_CONTROL - mmGRPH_CONTROL),
			.dmif = (mmDMIF_PG0_DPG_WATERMARK_MASK_CONTROL
					- mmDPG_WATERMARK_MASK_CONTROL),
		},
		{
			.crtc = (mmCRTC1_CRTC_CONTROL - mmCRTC_CONTROL),
			.dcp = (mmDCP1_GRPH_CONTROL - mmGRPH_CONTROL),
			.dmif = (mmDMIF_PG1_DPG_WATERMARK_MASK_CONTROL
					- mmDPG_WATERMARK_MASK_CONTROL),
		},
		{
			.crtc = (mmCRTC2_CRTC_CONTROL - mmCRTC_CONTROL),
			.dcp = (mmDCP2_GRPH_CONTROL - mmGRPH_CONTROL),
			.dmif = (mmDMIF_PG2_DPG_WATERMARK_MASK_CONTROL
					- mmDPG_WATERMARK_MASK_CONTROL),
		},
		{
			.crtc = (mmCRTC3_CRTC_CONTROL - mmCRTC_CONTROL),
			.dcp = (mmDCP3_GRPH_CONTROL - mmGRPH_CONTROL),
			.dmif = (mmDMIF_PG3_DPG_WATERMARK_MASK_CONTROL
					- mmDPG_WATERMARK_MASK_CONTROL),
		},
		{
			.crtc = (mmCRTC4_CRTC_CONTROL - mmCRTC_CONTROL),
			.dcp = (mmDCP4_GRPH_CONTROL - mmGRPH_CONTROL),
			.dmif = (mmDMIF_PG4_DPG_WATERMARK_MASK_CONTROL
					- mmDPG_WATERMARK_MASK_CONTROL),
		},
		{
			.crtc = (mmCRTC5_CRTC_CONTROL - mmCRTC_CONTROL),
			.dcp = (mmDCP5_GRPH_CONTROL - mmGRPH_CONTROL),
			.dmif = (mmDMIF_PG5_DPG_WATERMARK_MASK_CONTROL
					- mmDPG_WATERMARK_MASK_CONTROL),
		}
};

static const struct dce110_mem_input_reg_offsets dce80_mi_reg_offsets[] = {
	{
		.dcp = (mmGRPH_CONTROL - mmGRPH_CONTROL),
		.dmif = (mmDMIF_PG0_DPG_WATERMARK_MASK_CONTROL
				- mmDPG_WATERMARK_MASK_CONTROL),
		.pipe = (mmPIPE0_DMIF_BUFFER_CONTROL
				- mmPIPE0_DMIF_BUFFER_CONTROL),
	},
	{
		.dcp = (mmDCP1_GRPH_CONTROL - mmGRPH_CONTROL),
		.dmif = (mmDMIF_PG1_DPG_WATERMARK_MASK_CONTROL
				- mmDPG_WATERMARK_MASK_CONTROL),
		.pipe = (mmPIPE1_DMIF_BUFFER_CONTROL
				- mmPIPE0_DMIF_BUFFER_CONTROL),
	},
	{
		.dcp = (mmDCP2_GRPH_CONTROL - mmGRPH_CONTROL),
		.dmif = (mmDMIF_PG2_DPG_WATERMARK_MASK_CONTROL
				- mmDPG_WATERMARK_MASK_CONTROL),
		.pipe = (mmPIPE2_DMIF_BUFFER_CONTROL
				- mmPIPE0_DMIF_BUFFER_CONTROL),
	},
	{
		.dcp = (mmDCP3_GRPH_CONTROL - mmGRPH_CONTROL),
		.dmif = (mmDMIF_PG3_DPG_WATERMARK_MASK_CONTROL
				- mmDPG_WATERMARK_MASK_CONTROL),
		.pipe = (mmPIPE3_DMIF_BUFFER_CONTROL
				- mmPIPE0_DMIF_BUFFER_CONTROL),
	},
	{
		.dcp = (mmDCP4_GRPH_CONTROL - mmGRPH_CONTROL),
		.dmif = (mmDMIF_PG4_DPG_WATERMARK_MASK_CONTROL
				- mmDPG_WATERMARK_MASK_CONTROL),
		.pipe = (mmPIPE4_DMIF_BUFFER_CONTROL
				- mmPIPE0_DMIF_BUFFER_CONTROL),
	},
	{
		.dcp = (mmDCP5_GRPH_CONTROL - mmGRPH_CONTROL),
		.dmif = (mmDMIF_PG5_DPG_WATERMARK_MASK_CONTROL
				- mmDPG_WATERMARK_MASK_CONTROL),
		.pipe = (mmPIPE5_DMIF_BUFFER_CONTROL
				- mmPIPE0_DMIF_BUFFER_CONTROL),
	}
};

static const struct dce80_transform_reg_offsets dce80_xfm_offsets[] = {
{
	.scl_offset = (mmSCL_CONTROL - mmSCL_CONTROL),
	.crtc_offset = (mmDCFE_MEM_LIGHT_SLEEP_CNTL -
					mmDCFE_MEM_LIGHT_SLEEP_CNTL),
	.dcp_offset = (mmGRPH_CONTROL - mmGRPH_CONTROL),
	.lb_offset = (mmLB_DATA_FORMAT - mmLB_DATA_FORMAT),
},
{	.scl_offset = (mmSCL1_SCL_CONTROL - mmSCL_CONTROL),
	.crtc_offset = (mmCRTC1_DCFE_MEM_LIGHT_SLEEP_CNTL -
					mmDCFE_MEM_LIGHT_SLEEP_CNTL),
	.dcp_offset = (mmDCP1_GRPH_CONTROL - mmGRPH_CONTROL),
	.lb_offset = (mmLB1_LB_DATA_FORMAT - mmLB_DATA_FORMAT),
},
{	.scl_offset = (mmSCL2_SCL_CONTROL - mmSCL_CONTROL),
	.crtc_offset = (mmCRTC2_DCFE_MEM_LIGHT_SLEEP_CNTL -
					mmDCFE_MEM_LIGHT_SLEEP_CNTL),
	.dcp_offset = (mmDCP2_GRPH_CONTROL - mmGRPH_CONTROL),
	.lb_offset = (mmLB2_LB_DATA_FORMAT - mmLB_DATA_FORMAT),
},
{
	.scl_offset = (mmSCL3_SCL_CONTROL - mmSCL_CONTROL),
	.crtc_offset = (mmCRTC3_DCFE_MEM_LIGHT_SLEEP_CNTL -
					mmDCFE_MEM_LIGHT_SLEEP_CNTL),
	.dcp_offset = (mmDCP3_GRPH_CONTROL - mmGRPH_CONTROL),
	.lb_offset = (mmLB3_LB_DATA_FORMAT - mmLB_DATA_FORMAT),
},
{
	.scl_offset = (mmSCL4_SCL_CONTROL - mmSCL_CONTROL),
	.crtc_offset = (mmCRTC4_DCFE_MEM_LIGHT_SLEEP_CNTL -
					mmDCFE_MEM_LIGHT_SLEEP_CNTL),
	.dcp_offset = (mmDCP4_GRPH_CONTROL - mmGRPH_CONTROL),
	.lb_offset = (mmLB4_LB_DATA_FORMAT - mmLB_DATA_FORMAT),
},
{
	.scl_offset = (mmSCL5_SCL_CONTROL - mmSCL_CONTROL),
	.crtc_offset = (mmCRTC5_DCFE_MEM_LIGHT_SLEEP_CNTL -
					mmDCFE_MEM_LIGHT_SLEEP_CNTL),
	.dcp_offset = (mmDCP5_GRPH_CONTROL - mmGRPH_CONTROL),
	.lb_offset = (mmLB5_LB_DATA_FORMAT - mmLB_DATA_FORMAT),
}
};

static const struct dce110_ipp_reg_offsets ipp_reg_offsets[] = {
{
	.dcp_offset = (mmDCP0_CUR_CONTROL - mmDCP0_CUR_CONTROL),
},
{
	.dcp_offset = (mmDCP1_CUR_CONTROL - mmDCP0_CUR_CONTROL),
},
{
	.dcp_offset = (mmDCP2_CUR_CONTROL - mmDCP0_CUR_CONTROL),
},
{
	.dcp_offset = (mmDCP3_CUR_CONTROL - mmDCP0_CUR_CONTROL),
},
{
	.dcp_offset = (mmDCP4_CUR_CONTROL - mmDCP0_CUR_CONTROL),
},
{
	.dcp_offset = (mmDCP5_CUR_CONTROL - mmDCP0_CUR_CONTROL),
}
};

static const struct dce110_link_enc_bl_registers link_enc_bl_regs = {
		.BL_PWM_CNTL = mmBL_PWM_CNTL,
		.BL_PWM_GRP1_REG_LOCK = mmBL_PWM_GRP1_REG_LOCK,
		.BL_PWM_PERIOD_CNTL = mmBL_PWM_PERIOD_CNTL,
		.LVTMA_PWRSEQ_CNTL = mmLVTMA_PWRSEQ_CNTL,
		.LVTMA_PWRSEQ_STATE = mmLVTMA_PWRSEQ_STATE
};

#define aux_regs(id)\
[id] = {\
	.AUX_CONTROL = mmDP_AUX ## id ## _AUX_CONTROL,\
	.AUX_DPHY_RX_CONTROL0 = mmDP_AUX ## id ## _AUX_DPHY_RX_CONTROL0\
}

static const struct dce110_link_enc_aux_registers link_enc_aux_regs[] = {
	aux_regs(0),
	aux_regs(1),
	aux_regs(2),
	aux_regs(3),
	aux_regs(4),
	aux_regs(5)
};

#define link_regs(id)\
[id] = {\
	.DIG_BE_CNTL = mmDIG ## id ## _DIG_BE_CNTL,\
	.DIG_BE_EN_CNTL = mmDIG ## id ## _DIG_BE_EN_CNTL,\
	.DP_CONFIG = mmDP ## id ## _DP_CONFIG,\
	.DP_DPHY_CNTL = mmDP ## id ## _DP_DPHY_CNTL,\
	.DP_DPHY_INTERNAL_CTRL = mmDP ## id ## _DP_DPHY_INTERNAL_CTRL,\
	.DP_DPHY_PRBS_CNTL = mmDP ## id ## _DP_DPHY_PRBS_CNTL,\
	.DP_DPHY_SYM0 = mmDP ## id ## _DP_DPHY_SYM0,\
	.DP_DPHY_SYM1 = mmDP ## id ## _DP_DPHY_SYM1,\
	.DP_DPHY_SYM2 = mmDP ## id ## _DP_DPHY_SYM2,\
	.DP_DPHY_TRAINING_PATTERN_SEL = mmDP ## id ## _DP_DPHY_TRAINING_PATTERN_SEL,\
	.DP_LINK_CNTL = mmDP ## id ## _DP_LINK_CNTL,\
	.DP_LINK_FRAMING_CNTL = mmDP ## id ## _DP_LINK_FRAMING_CNTL,\
	.DP_MSE_SAT0 = mmDP ## id ## _DP_MSE_SAT0,\
	.DP_MSE_SAT1 = mmDP ## id ## _DP_MSE_SAT1,\
	.DP_MSE_SAT2 = mmDP ## id ## _DP_MSE_SAT2,\
	.DP_MSE_SAT_UPDATE = mmDP ## id ## _DP_MSE_SAT_UPDATE,\
	.DP_SEC_CNTL = mmDP ## id ## _DP_SEC_CNTL,\
	.DP_VID_STREAM_CNTL = mmDP ## id ## _DP_VID_STREAM_CNTL\
}

static const struct dce110_link_enc_registers link_enc_regs[] = {
	link_regs(0),
	link_regs(1),
	link_regs(2),
	link_regs(3),
	link_regs(4),
	link_regs(5)
};

#define stream_enc_regs(id)\
[id] = {\
	.AFMT_AVI_INFO0 = mmDIG ## id ## _AFMT_AVI_INFO0,\
	.AFMT_AVI_INFO1 = mmDIG ## id ## _AFMT_AVI_INFO1,\
	.AFMT_AVI_INFO2 = mmDIG ## id ## _AFMT_AVI_INFO2,\
	.AFMT_AVI_INFO3 = mmDIG ## id ## _AFMT_AVI_INFO3,\
	.AFMT_GENERIC_0 = mmDIG ## id ## _AFMT_GENERIC_0,\
	.AFMT_GENERIC_7 = mmDIG ## id ## _AFMT_GENERIC_7,\
	.AFMT_GENERIC_HDR = mmDIG ## id ## _AFMT_GENERIC_HDR,\
	.AFMT_INFOFRAME_CONTROL0 = mmDIG ## id ## _AFMT_INFOFRAME_CONTROL0,\
	.AFMT_VBI_PACKET_CONTROL = mmDIG ## id ## _AFMT_VBI_PACKET_CONTROL,\
	.DIG_FE_CNTL = mmDIG ## id ## _DIG_FE_CNTL,\
	.DP_MSE_RATE_CNTL = mmDP ## id ## _DP_MSE_RATE_CNTL,\
	.DP_MSE_RATE_UPDATE = mmDP ## id ## _DP_MSE_RATE_UPDATE,\
	.DP_PIXEL_FORMAT = mmDP ## id ## _DP_PIXEL_FORMAT,\
	.DP_SEC_CNTL = mmDP ## id ## _DP_SEC_CNTL,\
	.DP_STEER_FIFO = mmDP ## id ## _DP_STEER_FIFO,\
	.DP_VID_M = mmDP ## id ## _DP_VID_M,\
	.DP_VID_N = mmDP ## id ## _DP_VID_N,\
	.DP_VID_STREAM_CNTL = mmDP ## id ## _DP_VID_STREAM_CNTL,\
	.DP_VID_TIMING = mmDP ## id ## _DP_VID_TIMING,\
	.HDMI_CONTROL = mmDIG ## id ## _HDMI_CONTROL,\
	.HDMI_GC = mmDIG ## id ## _HDMI_GC,\
	.HDMI_GENERIC_PACKET_CONTROL0 = mmDIG ## id ## _HDMI_GENERIC_PACKET_CONTROL0,\
	.HDMI_GENERIC_PACKET_CONTROL1 = mmDIG ## id ## _HDMI_GENERIC_PACKET_CONTROL1,\
	.HDMI_INFOFRAME_CONTROL0 = mmDIG ## id ## _HDMI_INFOFRAME_CONTROL0,\
	.HDMI_INFOFRAME_CONTROL1 = mmDIG ## id ## _HDMI_INFOFRAME_CONTROL1,\
	.HDMI_VBI_PACKET_CONTROL = mmDIG ## id ## _HDMI_VBI_PACKET_CONTROL,\
	.TMDS_CNTL = mmDIG ## id ## _TMDS_CNTL\
}

static const struct dce110_stream_enc_registers stream_enc_regs[] = {
	stream_enc_regs(0),
	stream_enc_regs(1),
	stream_enc_regs(2),
	stream_enc_regs(3),
	stream_enc_regs(4),
	stream_enc_regs(5)
};

static const struct dce110_clk_src_reg_offsets dce80_clk_src_reg_offsets[] = {
	{
		.pll_cntl = mmDCCG_PLL0_PLL_CNTL,
		.pixclk_resync_cntl  = mmPIXCLK0_RESYNC_CNTL
	},
	{
		.pll_cntl = mmDCCG_PLL1_PLL_CNTL,
		.pixclk_resync_cntl  = mmPIXCLK1_RESYNC_CNTL
	},
	{
		.pll_cntl = mmDCCG_PLL2_PLL_CNTL,
		.pixclk_resync_cntl  = mmPIXCLK2_RESYNC_CNTL
	}
};

static struct timing_generator *dce80_timing_generator_create(
		struct adapter_service *as,
		struct dc_context *ctx,
		uint32_t instance,
		const struct dce110_timing_generator_offsets *offsets)
{
	struct dce110_timing_generator *tg110 =
		dm_alloc(sizeof(struct dce110_timing_generator));

	if (!tg110)
		return NULL;

	if (dce80_timing_generator_construct(tg110, as, ctx, instance, offsets))
		return &tg110->base;

	BREAK_TO_DEBUGGER();
	dm_free(tg110);
	return NULL;
}

static struct stream_encoder *dce80_stream_encoder_create(
	enum engine_id eng_id,
	struct dc_context *ctx,
	struct dc_bios *dcb,
	const struct dce110_stream_enc_registers *regs)
{
	struct dce110_stream_encoder *enc110 =
		dm_alloc(sizeof(struct dce110_stream_encoder));

	if (!enc110)
		return NULL;

	if (dce80_stream_encoder_construct(enc110, ctx, dcb, eng_id, regs))
		return &enc110->base;

	BREAK_TO_DEBUGGER();
	dm_free(enc110);
	return NULL;
}

static struct mem_input *dce80_mem_input_create(
	struct dc_context *ctx,
	uint32_t inst,
	const struct dce110_mem_input_reg_offsets *offsets)
{
	struct dce110_mem_input *mem_input80 =
		dm_alloc(sizeof(struct dce110_mem_input));

	if (!mem_input80)
		return NULL;

	if (dce80_mem_input_construct(mem_input80,
			ctx, inst, offsets))
		return &mem_input80->base;

	BREAK_TO_DEBUGGER();
	dm_free(mem_input80);
	return NULL;
}

static void dce80_transform_destroy(struct transform **xfm)
{
	dm_free(TO_DCE80_TRANSFORM(*xfm));
	*xfm = NULL;
}

static struct transform *dce80_transform_create(
	struct dc_context *ctx,
	uint32_t inst,
	const struct dce80_transform_reg_offsets *offsets)
{
	struct dce80_transform *transform =
		dm_alloc(sizeof(struct dce80_transform));

	if (!transform)
		return NULL;

	if (dce80_transform_construct(transform, ctx, inst, offsets))
		return &transform->base;

	BREAK_TO_DEBUGGER();
	dm_free(transform);
	return NULL;
}

static struct input_pixel_processor *dce80_ipp_create(
	struct dc_context *ctx,
	uint32_t inst,
	const struct dce110_ipp_reg_offsets *offset)
{
	struct dce110_ipp *ipp =
		dm_alloc(sizeof(struct dce110_ipp));

	if (!ipp)
		return NULL;

	if (dce80_ipp_construct(ipp, ctx, inst, offset))
		return &ipp->base;

	BREAK_TO_DEBUGGER();
	dm_free(ipp);
	return NULL;
}

struct link_encoder *dce80_link_encoder_create(
	const struct encoder_init_data *enc_init_data)
{
	struct dce110_link_encoder *enc110 =
		dm_alloc(sizeof(struct dce110_link_encoder));

	if (!enc110)
		return NULL;

	if (dce80_link_encoder_construct(
			enc110,
			enc_init_data,
			&link_enc_regs[enc_init_data->transmitter],
			&link_enc_aux_regs[enc_init_data->channel - 1],
			&link_enc_bl_regs))
		return &enc110->base;

	BREAK_TO_DEBUGGER();
	dm_free(enc110);
	return NULL;
}

struct clock_source *dce80_clock_source_create(
	struct dc_context *ctx,
	struct dc_bios *bios,
	enum clock_source_id id,
	const struct dce110_clk_src_reg_offsets *offsets)
{
	struct dce110_clk_src *clk_src =
		dm_alloc(sizeof(struct dce110_clk_src));

	if (!clk_src)
		return NULL;

	if (dce110_clk_src_construct(clk_src, ctx, bios, id, offsets))
		return &clk_src->base;

	BREAK_TO_DEBUGGER();
	return NULL;
}

void dce80_clock_source_destroy(struct clock_source **clk_src)
{
	dm_free(TO_DCE110_CLK_SRC(*clk_src));
	*clk_src = NULL;
}

void dce80_destruct_resource_pool(struct resource_pool *pool)
{
	unsigned int i;

	for (i = 0; i < pool->pipe_count; i++) {
		if (pool->opps[i] != NULL)
			dce80_opp_destroy(&pool->opps[i]);

		if (pool->transforms[i] != NULL)
			dce80_transform_destroy(&pool->transforms[i]);

		if (pool->ipps[i] != NULL)
			dce80_ipp_destroy(&pool->ipps[i]);

		if (pool->mis[i] != NULL) {
			dm_free(TO_DCE110_MEM_INPUT(pool->mis[i]));
			pool->mis[i] = NULL;
		}

		if (pool->timing_generators[i] != NULL)	{
			dm_free(DCE110TG_FROM_TG(pool->timing_generators[i]));
			pool->timing_generators[i] = NULL;
		}
	}

	for (i = 0; i < pool->stream_enc_count; i++) {
		if (pool->stream_enc[i] != NULL)
			dm_free(DCE110STRENC_FROM_STRENC(pool->stream_enc[i]));
	}

	for (i = 0; i < pool->clk_src_count; i++) {
		if (pool->clock_sources[i] != NULL) {
			dce80_clock_source_destroy(&pool->clock_sources[i]);
		}
	}

	if (pool->dp_clock_source != NULL)
		dce80_clock_source_destroy(&pool->dp_clock_source);

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
	const struct pipe_ctx *pipe_ctx,
	struct audio_output *audio_output)
{
	const struct core_stream *stream = pipe_ctx->stream;
	audio_output->engine_id = pipe_ctx->stream_enc->id;

	audio_output->signal = pipe_ctx->signal;

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
			pipe_ctx->pix_clk_params.requested_pix_clk;

	/* TODO - Investigate why calculated pixel clk has to be
	 * requested pixel clk */
	audio_output->crtc_info.calculated_pixel_clock =
			pipe_ctx->pix_clk_params.requested_pix_clk;

	if (pipe_ctx->signal == SIGNAL_TYPE_DISPLAY_PORT ||
			pipe_ctx->signal == SIGNAL_TYPE_DISPLAY_PORT_MST) {
		audio_output->pll_info.dp_dto_source_clock_in_khz =
			dal_display_clock_get_dp_ref_clk_frequency(
				pipe_ctx->dis_clk);
	}

	audio_output->pll_info.feed_back_divider =
			pipe_ctx->pll_settings.feedback_divider;

	audio_output->pll_info.dto_source =
		translate_to_dto_source(
			pipe_ctx->pipe_idx + 1);

	/* TODO hard code to enable for now. Need get from stream */
	audio_output->pll_info.ss_enabled = true;

	audio_output->pll_info.ss_percentage =
			pipe_ctx->pll_settings.ss_percentage;
}

static void get_pixel_clock_parameters(
	const struct pipe_ctx *pipe_ctx,
	struct pixel_clk_params *pixel_clk_params)
{
	const struct core_stream *stream = pipe_ctx->stream;
	pixel_clk_params->requested_pix_clk = stream->public.timing.pix_clk_khz;
	pixel_clk_params->encoder_object_id = stream->sink->link->link_enc->id;
	pixel_clk_params->signal_type = stream->sink->public.sink_signal;
	pixel_clk_params->controller_id = pipe_ctx->pipe_idx + 1;
	/* TODO: un-hardcode*/
	pixel_clk_params->requested_sym_clk = LINK_RATE_LOW *
		LINK_RATE_REF_FREQ_IN_KHZ;
	pixel_clk_params->flags.ENABLE_SS = 0;
	pixel_clk_params->color_depth =
		stream->public.timing.display_color_depth;
	pixel_clk_params->flags.DISPLAY_BLANKED = 1;
}

static enum dc_status build_pipe_hw_param(struct pipe_ctx *pipe_ctx)
{
	/*TODO: unhardcode*/
	pipe_ctx->max_tmds_clk_from_edid_in_mhz = 0;
	pipe_ctx->max_hdmi_deep_color = COLOR_DEPTH_121212;
	pipe_ctx->max_hdmi_pixel_clock = 600000;

	get_pixel_clock_parameters(pipe_ctx, &pipe_ctx->pix_clk_params);
	pipe_ctx->clock_source->funcs->get_pix_clk_dividers(
		pipe_ctx->clock_source,
		&pipe_ctx->pix_clk_params,
		&pipe_ctx->pll_settings);

	build_audio_output(pipe_ctx, &pipe_ctx->audio_output);

	return DC_OK;
}

static enum dc_status validate_mapped_resource(
		const struct core_dc *dc,
		struct validate_context *context)
{
	enum dc_status status = DC_OK;
	uint8_t i, j, k;

	for (i = 0; i < context->target_count; i++) {
		struct core_target *target = context->targets[i];
		if (context->target_flags[i].unchanged)
			continue;
		for (j = 0; j < target->public.stream_count; j++) {
			struct core_stream *stream =
				DC_STREAM_TO_CORE(target->public.streams[j]);
			struct core_link *link = stream->sink->link;

			for (k = 0; k < MAX_PIPES; k++) {
				struct pipe_ctx *pipe_ctx =
					&context->res_ctx.pipe_ctx[k];

				if (context->res_ctx.pipe_ctx[k].stream != stream)
					continue;

				if (!pipe_ctx->tg->funcs->validate_timing(
						pipe_ctx->tg, &stream->public.timing))
					return DC_FAIL_CONTROLLER_VALIDATE;

				status = build_pipe_hw_param(pipe_ctx);

				if (status != DC_OK)
					return status;

				if (!link->link_enc->funcs->validate_output_with_stream(
						link->link_enc,
						pipe_ctx))
					return DC_FAIL_ENC_VALIDATE;

				/* TODO: validate audio ASIC caps, encoder */

				status = dc_link_validate_mode_timing(stream->sink,
						link,
						&stream->public.timing);

				if (status != DC_OK)
					return status;

				build_info_frame(pipe_ctx);

				/* do not need to validate non root pipes */
				break;
			}
		}
	}

	return DC_OK;
}

enum dc_status dce80_validate_bandwidth(
	const struct core_dc *dc,
	struct validate_context *context)
{
	uint8_t i;
	enum dc_status result = DC_ERROR_UNEXPECTED;
	uint8_t number_of_displays = 0;
	uint8_t max_htaps = 1;
	uint8_t max_vtaps = 1;
	bool all_displays_in_sync = true;
	struct dc_crtc_timing prev_timing;

	memset(&context->bw_mode_data, 0, sizeof(context->bw_mode_data));

	for (i = 0; i < MAX_PIPES; i++) {
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];
		struct bw_calcs_input_single_display *disp = &context->
			bw_mode_data.displays_data[number_of_displays];

		if (pipe_ctx->stream == NULL)
			continue;

		if (pipe_ctx->scl_data.ratios.vert.value == 0) {
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
					pipe_ctx->scl_data.ratios.vert.value);
			disp->graphics_h_taps = pipe_ctx->scl_data.taps.h_taps;
			disp->graphics_v_taps = pipe_ctx->scl_data.taps.v_taps;

			/* TODO: remove when bw formula accepts taps per
			 * display
			 */
			if (max_vtaps < pipe_ctx->scl_data.taps.v_taps)
				max_vtaps = pipe_ctx->scl_data.taps.v_taps;
			if (max_htaps < pipe_ctx->scl_data.taps.h_taps)
				max_htaps = pipe_ctx->scl_data.taps.h_taps;
		}

		disp->graphics_src_width =
			pipe_ctx->stream->public.timing.h_addressable;
		disp->graphics_src_height =
			pipe_ctx->stream->public.timing.v_addressable;
		disp->h_total = pipe_ctx->stream->public.timing.h_total;
		disp->pixel_rate = bw_frc_to_fixed(
			pipe_ctx->stream->public.timing.pix_clk_khz, 1000);

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
			if (memcmp(&prev_timing,
				&pipe_ctx->stream->public.timing,
				sizeof(struct dc_crtc_timing)) != 0)
				all_displays_in_sync = false;
		if (number_of_displays == 0)
			prev_timing = pipe_ctx->stream->public.timing;

		number_of_displays++;
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

	if (memcmp(&dc->current_context.bw_results,
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
	uint8_t i, j;
	struct core_target *target = context->targets[target_idx];
	context->target_flags[target_idx].unchanged = true;
	for (i = 0; i < target->public.stream_count; i++) {
		struct core_stream *stream =
			DC_STREAM_TO_CORE(target->public.streams[i]);
		for (j = 0; j < MAX_PIPES; j++) {
			if (context->res_ctx.pipe_ctx[j].stream == stream)
				context->res_ctx.pipe_ctx[j].flags.unchanged =
									true;
		}
	}
}

static enum dc_status map_clock_resources(
		const struct core_dc *dc,
		struct validate_context *context)
{
	uint8_t i, j, k;

	/* acquire new resources */
	for (i = 0; i < context->target_count; i++) {
		struct core_target *target = context->targets[i];

		if (context->target_flags[i].unchanged)
			continue;

		for (j = 0; j < target->public.stream_count; j++) {
			struct core_stream *stream =
				DC_STREAM_TO_CORE(target->public.streams[j]);

			for (k = 0; k < MAX_PIPES; k++) {
				struct pipe_ctx *pipe_ctx =
					&context->res_ctx.pipe_ctx[k];

				if (context->res_ctx.pipe_ctx[k].stream != stream)
					continue;

				if (dc_is_dp_signal(pipe_ctx->signal)
					|| pipe_ctx->signal == SIGNAL_TYPE_VIRTUAL)
					pipe_ctx->clock_source = context->res_ctx.pool.dp_clock_source;
				else {
					pipe_ctx->clock_source =
						find_used_clk_src_for_sharing(
							&context->res_ctx, pipe_ctx);

					if (pipe_ctx->clock_source == NULL)
						pipe_ctx->clock_source =
							dc_resource_find_first_free_pll(&context->res_ctx);
				}

				if (pipe_ctx->clock_source == NULL)
					return DC_NO_CLOCK_SOURCE_RESOURCE;

				reference_clock_source(
						&context->res_ctx,
						pipe_ctx->clock_source);

				/* only one cs per stream regardless of mpo */
				break;
			}
		}
	}

	return DC_OK;
}

enum dc_status dce80_validate_with_context(
		const struct core_dc *dc,
		const struct dc_validation_set set[],
		uint8_t set_count,
		struct validate_context *context)
{
	enum dc_status result = DC_ERROR_UNEXPECTED;
	uint8_t i, j;
	struct dc_context *dc_ctx = dc->ctx;

	for (i = 0; i < set_count; i++) {
		bool unchanged = false;

		context->targets[i] = DC_TARGET_TO_CORE(set[i].target);
		dc_target_retain(&context->targets[i]->public);
		context->target_count++;

		for (j = 0; j < dc->current_context.target_count; j++)
			if (dc->current_context.targets[j]
						== context->targets[i]) {
				unchanged = true;
				set_target_unchanged(context, i);
				attach_surfaces_to_context(
					(struct dc_surface **)dc->current_context.
						target_status[j].surfaces,
					dc->current_context.target_status[j].surface_count,
					&context->targets[i]->public,
					context);
				context->target_status[i] =
					dc->current_context.target_status[j];
			}
		if (!unchanged || set[i].surface_count != 0)
			if (!attach_surfaces_to_context(
					(struct dc_surface **)set[i].surfaces,
					set[i].surface_count,
					&context->targets[i]->public,
					context)) {
				DC_ERROR("Failed to attach surface to target!\n");
				return DC_FAIL_ATTACH_SURFACES;
			}
	}

	context->res_ctx.pool = dc->res_pool;

	result = map_resources(dc, context);

	if (result == DC_OK)
		result = map_clock_resources(dc, context);

	if (result == DC_OK)
		result = validate_mapped_resource(dc, context);

	if (result == DC_OK)
		build_scaling_params_for_context(dc, context);

	if (result == DC_OK)
		result = dce80_validate_bandwidth(dc, context);

	return result;
}

static struct resource_funcs dce80_res_pool_funcs = {
	.destruct = dce80_destruct_resource_pool,
	.link_enc_create = dce80_link_encoder_create,
	.link_enc_destroy = dce110_link_encoder_destroy,
	.validate_with_context = dce80_validate_with_context,
	.validate_bandwidth = dce80_validate_bandwidth
};

bool dce80_construct_resource_pool(
	struct adapter_service *as,
	uint8_t num_virtual_links,
	struct core_dc *dc,
	struct resource_pool *pool)
{
	unsigned int i;
	struct audio_init_data audio_init_data = { 0 };
	struct dc_context *ctx = dc->ctx;
	struct firmware_info info;
	struct dc_bios *bp;
	int regular_pll_offset = 0;

	pool->adapter_srv = as;
	pool->funcs = &dce80_res_pool_funcs;

	pool->stream_engines.engine.ENGINE_ID_DIGA = 1;
	pool->stream_engines.engine.ENGINE_ID_DIGB = 1;
	pool->stream_engines.engine.ENGINE_ID_DIGC = 1;
	pool->stream_engines.engine.ENGINE_ID_DIGD = 1;
	pool->stream_engines.engine.ENGINE_ID_DIGE = 1;
	pool->stream_engines.engine.ENGINE_ID_DIGF = 1;

	bp = dal_adapter_service_get_bios_parser(as);

	if (dal_adapter_service_get_firmware_info(as, &info) &&
		info.external_clock_source_frequency_for_dp != 0) {
		pool->dp_clock_source =
			dce80_clock_source_create(
				ctx,
				bp,
				CLOCK_SOURCE_ID_EXTERNAL,
				NULL);
	} else {
		pool->dp_clock_source =
			dce80_clock_source_create(
				ctx,
				bp,
				CLOCK_SOURCE_ID_PLL0,
				&dce80_clk_src_reg_offsets[0]);
		regular_pll_offset = 1;
	}

	pool->clk_src_count = DCE80_CLK_SRC_TOTAL - regular_pll_offset;

	for (i = 0; i < DCE80_CLK_SRC_TOTAL; ++i, ++regular_pll_offset)
		pool->clock_sources[DCE80_CLK_SRC0 + i] =
			dce80_clock_source_create(
				ctx,
				bp,
				CLOCK_SOURCE_ID_PLL0 + regular_pll_offset,
				&dce80_clk_src_reg_offsets[regular_pll_offset]);

	pool->clock_sources[DCE80_CLK_SRC1] =
		dce80_clock_source_create(
			ctx,
			bp,
			CLOCK_SOURCE_ID_PLL1,
			&dce80_clk_src_reg_offsets[1]);

	pool->clock_sources[DCE80_CLK_SRC2] =
		dce80_clock_source_create(
			ctx,
			bp,
			CLOCK_SOURCE_ID_PLL2,
			&dce80_clk_src_reg_offsets[2]);

	for (i = 0; i < pool->clk_src_count; i++) {
		if (pool->clock_sources[i] == NULL) {
			dm_error("DC: failed to create clock sources!\n");
			BREAK_TO_DEBUGGER();
			goto clk_src_create_fail;
		}
	}

	pool->display_clock = dal_display_clock_dce80_create(ctx, as);
	if (pool->display_clock == NULL) {
		dm_error("DC: failed to create display clock!\n");
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

	pool->pipe_count = dal_adapter_service_get_func_controllers_num(as);
	pool->stream_enc_count = dal_adapter_service_get_stream_engines_num(as);
	pool->scaler_filter = dal_scaler_filter_create(ctx);
	if (pool->scaler_filter == NULL) {
		BREAK_TO_DEBUGGER();
		dm_error("DC: failed to create filter!\n");
		goto filter_create_fail;
	}

	for (i = 0; i < pool->pipe_count; i++) {
		pool->timing_generators[i] = dce80_timing_generator_create(
				as, ctx, i, &dce80_tg_offsets[i]);
		if (pool->timing_generators[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error("DC: failed to create tg!\n");
			goto controller_create_fail;
		}

		pool->mis[i] = dce80_mem_input_create(ctx, i,
				&dce80_mi_reg_offsets[i]);
		if (pool->mis[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error("DC: failed to create memory input!\n");
			goto controller_create_fail;
		}

		pool->ipps[i] = dce80_ipp_create(ctx, i, &ipp_reg_offsets[i]);
		if (pool->ipps[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error("DC: failed to create input pixel processor!\n");
			goto controller_create_fail;
		}

		pool->transforms[i] = dce80_transform_create(
						ctx, i, &dce80_xfm_offsets[i]);
		if (pool->transforms[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error("DC: failed to create transform!\n");
			goto controller_create_fail;
		}
		pool->transforms[i]->funcs->transform_set_scaler_filter(
				pool->transforms[i],
				pool->scaler_filter);

		pool->opps[i] = dce80_opp_create(ctx, i);
		if (pool->opps[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error("DC: failed to create output pixel processor!\n");
			goto controller_create_fail;
		}
	}

	audio_init_data.as = as;
	audio_init_data.ctx = ctx;
	pool->audio_count = 0;
	for (i = 0; i < pool->pipe_count; i++) {
		struct graphics_object_id obj_id;

		obj_id = dal_adapter_service_enum_audio_object(as, i);
		if (false == dal_graphics_object_id_is_valid(obj_id)) {
			/* no more valid audio objects */
			break;
		}

		audio_init_data.audio_stream_id = obj_id;
		pool->audios[i] = dal_audio_create(&audio_init_data);
		if (pool->audios[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error("DC: failed to create DPPs!\n");
			goto audio_create_fail;
		}
		pool->audio_count++;
	}

	for (i = 0; i < pool->stream_enc_count; i++) {
		if (pool->stream_engines.u_all & 1 << i) {
			pool->stream_enc[i] = dce80_stream_encoder_create(
					i, dc->ctx,
					dal_adapter_service_get_bios_parser(
						as),
					&stream_enc_regs[i]);

			if (pool->stream_enc[i] == NULL) {
				BREAK_TO_DEBUGGER();
				dm_error("DC: failed to create stream_encoder!\n");
				goto stream_enc_create_fail;
			}
		}
	}

	for (i = 0; i < num_virtual_links; i++) {
		pool->stream_enc[pool->stream_enc_count] =
			virtual_stream_encoder_create(
				dc->ctx, dal_adapter_service_get_bios_parser(
								as));
		if (pool->stream_enc[pool->stream_enc_count] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error("DC: failed to create stream_encoder!\n");
			goto stream_enc_create_fail;
		}
		pool->stream_enc_count++;
	}

	return true;

stream_enc_create_fail:
	for (i = 0; i < pool->stream_enc_count; i++) {
		if (pool->stream_enc[i] != NULL)
			dm_free(DCE110STRENC_FROM_STRENC(pool->stream_enc[i]));
	}

audio_create_fail:
	for (i = 0; i < pool->pipe_count; i++) {
		if (pool->audios[i] != NULL)
			dal_audio_destroy(&pool->audios[i]);
	}

controller_create_fail:
	for (i = 0; i < pool->pipe_count; i++) {
		if (pool->opps[i] != NULL)
			dce80_opp_destroy(&pool->opps[i]);

		if (pool->transforms[i] != NULL)
			dce80_transform_destroy(&pool->transforms[i]);

		if (pool->ipps[i] != NULL)
			dce80_ipp_destroy(&pool->ipps[i]);

		if (pool->mis[i] != NULL) {
			dm_free(TO_DCE110_MEM_INPUT(pool->mis[i]));
			pool->mis[i] = NULL;
		}
		if (pool->timing_generators[i] != NULL)	{
			dm_free(DCE110TG_FROM_TG(pool->timing_generators[i]));
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
			dce80_clock_source_destroy(&pool->clock_sources[i]);
	}

	return false;
}
