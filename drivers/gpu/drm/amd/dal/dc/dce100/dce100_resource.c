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
#include "dce110/dce110_resource.h"
#include "dce110/dce110_timing_generator.h"
#include "dce110/dce110_link_encoder.h"
#include "dce110/dce110_mem_input.h"
#include "dce110/dce110_mem_input_v.h"
#include "dce110/dce110_ipp.h"
#include "dce110/dce110_transform.h"
#include "dce100/dce100_link_encoder.h"
#include "dce110/dce110_stream_encoder.h"
#include "dce110/dce110_opp.h"
#include "dce110/dce110_clock_source.h"

#include "dce/dce_10_0_d.h"

#ifndef mmDP_DPHY_INTERNAL_CTRL
	#define mmDP_DPHY_INTERNAL_CTRL 0x4aa7
	#define mmDP0_DP_DPHY_INTERNAL_CTRL 0x4aa7
	#define mmDP1_DP_DPHY_INTERNAL_CTRL 0x4ba7
	#define mmDP2_DP_DPHY_INTERNAL_CTRL 0x4ca7
	#define mmDP3_DP_DPHY_INTERNAL_CTRL 0x4da7
	#define mmDP4_DP_DPHY_INTERNAL_CTRL 0x4ea7
	#define mmDP5_DP_DPHY_INTERNAL_CTRL 0x4fa7
	#define mmDP6_DP_DPHY_INTERNAL_CTRL 0x54a7
	#define mmDP7_DP_DPHY_INTERNAL_CTRL 0x56a7
	#define mmDP8_DP_DPHY_INTERNAL_CTRL 0x57a7
#endif

static const struct dce110_timing_generator_offsets dce100_tg_offsets[] = {
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
	},
	{
		.crtc = (mmCRTC3_CRTC_CONTROL - mmCRTC_CONTROL),
		.dcp =  (mmDCP3_GRPH_CONTROL - mmGRPH_CONTROL),
	},
	{
		.crtc = (mmCRTC4_CRTC_CONTROL - mmCRTC_CONTROL),
		.dcp = (mmDCP4_GRPH_CONTROL - mmGRPH_CONTROL),
	},
	{
		.crtc = (mmCRTC5_CRTC_CONTROL - mmCRTC_CONTROL),
		.dcp = (mmDCP5_GRPH_CONTROL - mmGRPH_CONTROL),
	}
};

static const struct dce110_mem_input_reg_offsets dce100_mi_reg_offsets[] = {
	{
		.dcp = (mmDCP0_GRPH_CONTROL - mmGRPH_CONTROL),
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

static const struct dce110_clk_src_reg_offsets dce100_clk_src_reg_offsets[] = {
	{
		.pll_cntl = mmBPHYC_PLL0_PLL_CNTL,
		.pixclk_resync_cntl  = mmPIXCLK0_RESYNC_CNTL
	},
	{
		.pll_cntl = mmBPHYC_PLL1_PLL_CNTL,
		.pixclk_resync_cntl  = mmPIXCLK1_RESYNC_CNTL
	},
	{
		.pll_cntl = mmBPHYC_PLL2_PLL_CNTL,
		.pixclk_resync_cntl  = mmPIXCLK2_RESYNC_CNTL
	}
};

static const struct dce110_transform_reg_offsets dce100_xfm_offsets[] = {
{
	.scl_offset = (mmSCL0_SCL_CONTROL - mmSCL_CONTROL),
	.dcfe_offset = (mmCRTC0_DCFE_MEM_PWR_CTRL - mmDCFE_MEM_PWR_CTRL),
	.dcp_offset = (mmDCP0_GRPH_CONTROL - mmGRPH_CONTROL),
	.lb_offset = (mmLB0_LB_DATA_FORMAT - mmLB_DATA_FORMAT),
},
{	.scl_offset = (mmSCL1_SCL_CONTROL - mmSCL_CONTROL),
	.dcfe_offset = (mmCRTC1_DCFE_MEM_PWR_CTRL - mmDCFE_MEM_PWR_CTRL),
	.dcp_offset = (mmDCP1_GRPH_CONTROL - mmGRPH_CONTROL),
	.lb_offset = (mmLB1_LB_DATA_FORMAT - mmLB_DATA_FORMAT),
},
{	.scl_offset = (mmSCL2_SCL_CONTROL - mmSCL_CONTROL),
	.dcfe_offset = (mmCRTC2_DCFE_MEM_PWR_CTRL - mmDCFE_MEM_PWR_CTRL),
	.dcp_offset = (mmDCP2_GRPH_CONTROL - mmGRPH_CONTROL),
	.lb_offset = (mmLB2_LB_DATA_FORMAT - mmLB_DATA_FORMAT),
},
{
	.scl_offset = (mmSCL3_SCL_CONTROL - mmSCL_CONTROL),
	.dcfe_offset = (mmCRTC3_DCFE_MEM_PWR_CTRL - mmDCFE_MEM_PWR_CTRL),
	.dcp_offset = (mmDCP3_GRPH_CONTROL - mmGRPH_CONTROL),
	.lb_offset = (mmLB3_LB_DATA_FORMAT - mmLB_DATA_FORMAT),
},
{	.scl_offset = (mmSCL4_SCL_CONTROL - mmSCL_CONTROL),
	.dcfe_offset = (mmCRTC4_DCFE_MEM_PWR_CTRL - mmDCFE_MEM_PWR_CTRL),
	.dcp_offset = (mmDCP4_GRPH_CONTROL - mmGRPH_CONTROL),
	.lb_offset = (mmLB4_LB_DATA_FORMAT - mmLB_DATA_FORMAT),
},
{	.scl_offset = (mmSCL5_SCL_CONTROL - mmSCL_CONTROL),
	.dcfe_offset = (mmCRTC5_DCFE_MEM_PWR_CTRL - mmDCFE_MEM_PWR_CTRL),
	.dcp_offset = (mmDCP5_GRPH_CONTROL - mmGRPH_CONTROL),
	.lb_offset = (mmLB5_LB_DATA_FORMAT - mmLB_DATA_FORMAT),
}
};

static const struct dce110_ipp_reg_offsets dce100_ipp_reg_offsets[] = {
{
	.dcp_offset = (mmDCP0_CUR_CONTROL - mmCUR_CONTROL),
},
{
	.dcp_offset = (mmDCP1_CUR_CONTROL - mmCUR_CONTROL),
},
{
	.dcp_offset = (mmDCP2_CUR_CONTROL - mmCUR_CONTROL),
},
{
	.dcp_offset = (mmDCP3_CUR_CONTROL - mmCUR_CONTROL),
},
{
	.dcp_offset = (mmDCP4_CUR_CONTROL - mmCUR_CONTROL),
},
{
	.dcp_offset = (mmDCP5_CUR_CONTROL - mmCUR_CONTROL),
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
	link_regs(5),
	link_regs(6)
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
	stream_enc_regs(5),
	stream_enc_regs(6)
};

#define DCFE_MEM_PWR_CTRL_REG_BASE 0x1b03

static const struct dce110_opp_reg_offsets dce100_opp_reg_offsets[] = {
{
	.fmt_offset = (mmFMT0_FMT_CONTROL - mmFMT_CONTROL),
	.dcfe_offset = (mmCRTC0_DCFE_MEM_PWR_CTRL - DCFE_MEM_PWR_CTRL_REG_BASE),
	.dcp_offset = (mmDCP0_GRPH_CONTROL - mmGRPH_CONTROL),
},
{	.fmt_offset = (mmFMT1_FMT_CONTROL - mmFMT0_FMT_CONTROL),
	.dcfe_offset = (mmCRTC1_DCFE_MEM_PWR_CTRL - DCFE_MEM_PWR_CTRL_REG_BASE),
	.dcp_offset = (mmDCP1_GRPH_CONTROL - mmDCP0_GRPH_CONTROL),
},
{	.fmt_offset = (mmFMT2_FMT_CONTROL - mmFMT0_FMT_CONTROL),
	.dcfe_offset = (mmCRTC2_DCFE_MEM_PWR_CTRL - DCFE_MEM_PWR_CTRL_REG_BASE),
	.dcp_offset = (mmDCP2_GRPH_CONTROL - mmDCP0_GRPH_CONTROL),
},
{
	.fmt_offset = (mmFMT3_FMT_CONTROL - mmFMT0_FMT_CONTROL),
	.dcfe_offset = (mmCRTC3_DCFE_MEM_PWR_CTRL - DCFE_MEM_PWR_CTRL_REG_BASE),
	.dcp_offset = (mmDCP3_GRPH_CONTROL - mmDCP0_GRPH_CONTROL),
},
{	.fmt_offset = (mmFMT4_FMT_CONTROL - mmFMT0_FMT_CONTROL),
	.dcfe_offset = (mmCRTC4_DCFE_MEM_PWR_CTRL - DCFE_MEM_PWR_CTRL_REG_BASE),
	.dcp_offset = (mmDCP4_GRPH_CONTROL - mmDCP0_GRPH_CONTROL),
},
{	.fmt_offset = (mmFMT5_FMT_CONTROL - mmFMT0_FMT_CONTROL),
	.dcfe_offset = (mmCRTC5_DCFE_MEM_PWR_CTRL - DCFE_MEM_PWR_CTRL_REG_BASE),
	.dcp_offset = (mmDCP5_GRPH_CONTROL - mmDCP0_GRPH_CONTROL),
}
};

static struct timing_generator *dce100_timing_generator_create(
		struct adapter_service *as,
		struct dc_context *ctx,
		uint32_t instance,
		const struct dce110_timing_generator_offsets *offsets)
{
	struct dce110_timing_generator *tg110 =
		dm_alloc(sizeof(struct dce110_timing_generator));

	if (!tg110)
		return NULL;

	if (dce110_timing_generator_construct(tg110, as, ctx, instance,
			offsets))
		return &tg110->base;

	BREAK_TO_DEBUGGER();
	dm_free(tg110);
	return NULL;
}

static struct stream_encoder *dce100_stream_encoder_create(
	enum engine_id eng_id,
	struct dc_context *ctx,
	struct dc_bios *bp,
	const struct dce110_stream_enc_registers *regs)
{
	struct dce110_stream_encoder *enc110 =
		dm_alloc(sizeof(struct dce110_stream_encoder));

	if (!enc110)
		return NULL;

	if (dce110_stream_encoder_construct(enc110, ctx, bp, eng_id, regs))
		return &enc110->base;

	BREAK_TO_DEBUGGER();
	dm_free(enc110);
	return NULL;
}

static struct mem_input *dce100_mem_input_create(
	struct dc_context *ctx,
	uint32_t inst,
	const struct dce110_mem_input_reg_offsets *offset)
{
	struct dce110_mem_input *mem_input110 =
		dm_alloc(sizeof(struct dce110_mem_input));

	if (!mem_input110)
		return NULL;

	if (dce110_mem_input_construct(mem_input110,
			ctx, inst, offset))
		return &mem_input110->base;

	BREAK_TO_DEBUGGER();
	dm_free(mem_input110);
	return NULL;
}

static void dce100_transform_destroy(struct transform **xfm)
{
	dm_free(TO_DCE110_TRANSFORM(*xfm));
	*xfm = NULL;
}

static struct transform *dce100_transform_create(
	struct dc_context *ctx,
	uint32_t inst,
	const struct dce110_transform_reg_offsets *offsets)
{
	struct dce110_transform *transform =
		dm_alloc(sizeof(struct dce110_transform));

	if (!transform)
		return NULL;

	if (dce110_transform_construct(transform, ctx, inst, offsets))
		return &transform->base;

	BREAK_TO_DEBUGGER();
	dm_free(transform);
	return NULL;
}

static struct input_pixel_processor *dce100_ipp_create(
	struct dc_context *ctx,
	uint32_t inst,
	const struct dce110_ipp_reg_offsets *offsets)
{
	struct dce110_ipp *ipp =
		dm_alloc(sizeof(struct dce110_ipp));

	if (!ipp)
		return NULL;

	if (dce110_ipp_construct(ipp, ctx, inst, offsets))
		return &ipp->base;

	BREAK_TO_DEBUGGER();
	dm_free(ipp);
	return NULL;
}

struct link_encoder *dce100_link_encoder_create(
	const struct encoder_init_data *enc_init_data)
{
	struct dce110_link_encoder *enc110 =
		dm_alloc(sizeof(struct dce110_link_encoder));

	if (!enc110)
		return NULL;

	if (dce100_link_encoder_construct(
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

struct output_pixel_processor *dce100_opp_create(
	struct dc_context *ctx,
	uint32_t inst,
	const struct dce110_opp_reg_offsets *offset)
{
	struct dce110_opp *opp =
		dm_alloc(sizeof(struct dce110_opp));

	if (!opp)
		return NULL;

	if (dce110_opp_construct(opp,
			ctx, inst, offset))
		return &opp->base;

	BREAK_TO_DEBUGGER();
	dm_free(opp);
	return NULL;
}

void dce100_opp_destroy(struct output_pixel_processor **opp)
{
	dm_free(FROM_DCE11_OPP(*opp)->regamma.coeff128_dx);
	dm_free(FROM_DCE11_OPP(*opp)->regamma.coeff128_oem);
	dm_free(FROM_DCE11_OPP(*opp)->regamma.coeff128);
	dm_free(FROM_DCE11_OPP(*opp)->regamma.axis_x_1025);
	dm_free(FROM_DCE11_OPP(*opp)->regamma.axis_x_256);
	dm_free(FROM_DCE11_OPP(*opp)->regamma.coordinates_x);
	dm_free(FROM_DCE11_OPP(*opp)->regamma.rgb_regamma);
	dm_free(FROM_DCE11_OPP(*opp)->regamma.rgb_resulted);
	dm_free(FROM_DCE11_OPP(*opp)->regamma.rgb_oem);
	dm_free(FROM_DCE11_OPP(*opp)->regamma.rgb_user);
	dm_free(FROM_DCE11_OPP(*opp));
	*opp = NULL;
}

struct clock_source *dce100_clock_source_create(
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

void dce100_clock_source_destroy(struct clock_source **clk_src)
{
	dm_free(TO_DCE110_CLK_SRC(*clk_src));
	*clk_src = NULL;
}

void dce100_destruct_resource_pool(struct resource_pool *pool)
{
	unsigned int i;

	for (i = 0; i < pool->pipe_count; i++) {
		if (pool->opps[i] != NULL)
			dce100_opp_destroy(&pool->opps[i]);

		if (pool->transforms[i] != NULL)
			dce100_transform_destroy(&pool->transforms[i]);

		if (pool->ipps[i] != NULL)
			dce110_ipp_destroy(&pool->ipps[i]);

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
		if (pool->clock_sources[i] != NULL)
			dce100_clock_source_destroy(&pool->clock_sources[i]);
	}

	if (pool->dp_clock_source != NULL)
		dce100_clock_source_destroy(&pool->dp_clock_source);

	for (i = 0; i < pool->audio_count; i++)	{
		if (pool->audios[i] != NULL)
			dal_audio_destroy(&pool->audios[i]);
	}

	if (pool->display_clock != NULL)
		dal_display_clock_destroy(&pool->display_clock);

	if (pool->scaler_filter != NULL)
		dal_scaler_filter_destroy(&pool->scaler_filter);

	if (pool->irqs != NULL)
		dal_irq_service_destroy(&pool->irqs);

	if (pool->adapter_srv != NULL)
		dal_adapter_service_destroy(&pool->adapter_srv);
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
	get_pixel_clock_parameters(pipe_ctx, &pipe_ctx->pix_clk_params);
	pipe_ctx->clock_source->funcs->get_pix_clk_dividers(
		pipe_ctx->clock_source,
		&pipe_ctx->pix_clk_params,
		&pipe_ctx->pll_settings);

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

				resource_build_info_frame(pipe_ctx);

				/* do not need to validate non root pipes */
				break;
			}
		}
	}

	return DC_OK;
}

enum dc_status dce100_validate_bandwidth(
	const struct core_dc *dc,
	struct validate_context *context)
{
	/* TODO implement when needed but for now hardcode max value*/
	context->bw_results.dispclk_khz = 681000;

	return DC_OK;
}

enum dc_status dce100_validate_with_context(
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
				context->target_flags[i].unchanged = true;
				resource_attach_surfaces_to_context(
					(struct dc_surface **)dc->current_context.
						target_status[j].surfaces,
					dc->current_context.target_status[j].surface_count,
					&context->targets[i]->public,
					context);
				context->target_status[i] =
					dc->current_context.target_status[j];
			}
		if (!unchanged || set[i].surface_count != 0)
			if (!resource_attach_surfaces_to_context(
					(struct dc_surface **)set[i].surfaces,
					set[i].surface_count,
					&context->targets[i]->public,
					context)) {
				DC_ERROR("Failed to attach surface to target!\n");
				return DC_FAIL_ATTACH_SURFACES;
			}
	}

	context->res_ctx.pool = dc->res_pool;

	result = resource_map_pool_resources(dc, context);

	if (result == DC_OK)
		result = resource_map_clock_resources(dc, context);

	if (result == DC_OK)
		result = validate_mapped_resource(dc, context);

	if (result == DC_OK)
		resource_build_scaling_params_for_context(dc, context);

	if (result == DC_OK)
		result = dce100_validate_bandwidth(dc, context);

	return result;
}

static struct resource_funcs dce100_res_pool_funcs = {
	.destruct = dce100_destruct_resource_pool,
	.link_enc_create = dce100_link_encoder_create,
	.validate_with_context = dce100_validate_with_context,
	.validate_bandwidth = dce100_validate_bandwidth
};

bool dce100_construct_resource_pool(
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

	pool->adapter_srv = as;
	pool->funcs = &dce100_res_pool_funcs;

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
				dce100_clock_source_create(ctx, bp, CLOCK_SOURCE_ID_EXTERNAL, NULL);

		pool->clock_sources[0] =
				dce100_clock_source_create(ctx, bp, CLOCK_SOURCE_ID_PLL0, &dce100_clk_src_reg_offsets[0]);
		pool->clock_sources[1] =
				dce100_clock_source_create(ctx, bp, CLOCK_SOURCE_ID_PLL1, &dce100_clk_src_reg_offsets[1]);
		pool->clock_sources[2] =
				dce100_clock_source_create(ctx, bp, CLOCK_SOURCE_ID_PLL2, &dce100_clk_src_reg_offsets[2]);
		pool->clk_src_count = 3;

	} else {
		pool->dp_clock_source =
				dce100_clock_source_create(ctx, bp, CLOCK_SOURCE_ID_PLL0, &dce100_clk_src_reg_offsets[0]);
		pool->clock_sources[0] =
				dce100_clock_source_create(ctx, bp, CLOCK_SOURCE_ID_PLL1, &dce100_clk_src_reg_offsets[1]);
		pool->clock_sources[1] =
				dce100_clock_source_create(ctx, bp, CLOCK_SOURCE_ID_PLL2, &dce100_clk_src_reg_offsets[2]);
		pool->clk_src_count = 2;
	}

	if (pool->dp_clock_source == NULL) {
		dm_error("DC: failed to create dp clock source!\n");
		BREAK_TO_DEBUGGER();
		goto clk_src_create_fail;
	}

	for (i = 0; i < pool->clk_src_count; i++) {
		if (pool->clock_sources[i] == NULL) {
			dm_error("DC: failed to create clock sources!\n");
			BREAK_TO_DEBUGGER();
			goto clk_src_create_fail;
		}
	}

	pool->display_clock = dal_display_clock_dce110_create(ctx, as);
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
		pool->timing_generators[i] =
			dce100_timing_generator_create(
				as,
				ctx,
				i,
				&dce100_tg_offsets[i]);
		if (pool->timing_generators[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error("DC: failed to create tg!\n");
			goto controller_create_fail;
		}

		pool->mis[i] = dce100_mem_input_create(ctx, i,
				&dce100_mi_reg_offsets[i]);
		if (pool->mis[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC: failed to create memory input!\n");
			goto controller_create_fail;
		}

		pool->ipps[i] = dce100_ipp_create(ctx, i,
				&dce100_ipp_reg_offsets[i]);
		if (pool->ipps[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC: failed to create input pixel processor!\n");
			goto controller_create_fail;
		}

		pool->transforms[i] = dce100_transform_create(
					ctx, i, &dce100_xfm_offsets[i]);
		if (pool->transforms[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC: failed to create transform!\n");
			goto controller_create_fail;
		}
		pool->transforms[i]->funcs->transform_set_scaler_filter(
				pool->transforms[i],
				pool->scaler_filter);

		pool->opps[i] = dce100_opp_create(ctx, i, &dce100_opp_reg_offsets[i]);
		if (pool->opps[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC: failed to create output pixel processor!\n");
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
		/* TODO: rework fragile code*/
		if (pool->stream_engines.u_all & 1 << i) {
			pool->stream_enc[i] = dce100_stream_encoder_create(
				i, dc->ctx,
				dal_adapter_service_get_bios_parser(as),
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
				dc->ctx,
				dal_adapter_service_get_bios_parser(as));
		if (pool->stream_enc[pool->stream_enc_count] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error("DC: failed to create stream_encoder!\n");
			goto stream_enc_create_fail;
		}
		pool->stream_enc_count++;
	}

	/* Create hardware sequencer */
	if (!dc_construct_hw_sequencer(as, dc))
		goto stream_enc_create_fail;

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
			dce100_opp_destroy(&pool->opps[i]);

		if (pool->transforms[i] != NULL)
			dce100_transform_destroy(&pool->transforms[i]);

		if (pool->ipps[i] != NULL)
			dce110_ipp_destroy(&pool->ipps[i]);

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
			dce100_clock_source_destroy(&pool->clock_sources[i]);
	}

	return false;
}
