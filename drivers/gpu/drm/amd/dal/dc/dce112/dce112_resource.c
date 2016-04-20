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
#include "dce112/dce112_mem_input.h"
#include "dce112/dce112_link_encoder.h"
#include "dce110/dce110_link_encoder.h"
#include "dce110/dce110_transform.h"
#include "dce110/dce110_stream_encoder.h"
#include "dce110/dce110_opp.h"
#include "dce110/dce110_ipp.h"
#include "dce112/dce112_clock_source.h"

#include "dce/dce_11_2_d.h"

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

enum dce112_clk_src_array_id {
	DCE112_CLK_SRC_PLL0,
	DCE112_CLK_SRC_PLL1,
	DCE112_CLK_SRC_PLL2,
	DCE112_CLK_SRC_PLL3,
	DCE112_CLK_SRC_PLL4,
	DCE112_CLK_SRC_PLL5,

	DCE112_CLK_SRC_TOTAL
};

static const struct dce110_transform_reg_offsets dce112_xfm_offsets[] = {
{
	.scl_offset = (mmSCL0_SCL_CONTROL - mmSCL_CONTROL),
	.dcfe_offset = (mmDCFE0_DCFE_MEM_PWR_CTRL - mmDCFE_MEM_PWR_CTRL),
	.dcp_offset = (mmDCP0_GRPH_CONTROL - mmGRPH_CONTROL),
	.lb_offset = (mmLB0_LB_DATA_FORMAT - mmLB_DATA_FORMAT),
},
{	.scl_offset = (mmSCL1_SCL_CONTROL - mmSCL_CONTROL),
	.dcfe_offset = (mmDCFE1_DCFE_MEM_PWR_CTRL - mmDCFE_MEM_PWR_CTRL),
	.dcp_offset = (mmDCP1_GRPH_CONTROL - mmGRPH_CONTROL),
	.lb_offset = (mmLB1_LB_DATA_FORMAT - mmLB_DATA_FORMAT),
},
{	.scl_offset = (mmSCL2_SCL_CONTROL - mmSCL_CONTROL),
	.dcfe_offset = (mmDCFE2_DCFE_MEM_PWR_CTRL - mmDCFE_MEM_PWR_CTRL),
	.dcp_offset = (mmDCP2_GRPH_CONTROL - mmGRPH_CONTROL),
	.lb_offset = (mmLB2_LB_DATA_FORMAT - mmLB_DATA_FORMAT),
},
{
	.scl_offset = (mmSCL3_SCL_CONTROL - mmSCL_CONTROL),
	.dcfe_offset = (mmDCFE3_DCFE_MEM_PWR_CTRL - mmDCFE_MEM_PWR_CTRL),
	.dcp_offset = (mmDCP3_GRPH_CONTROL - mmGRPH_CONTROL),
	.lb_offset = (mmLB3_LB_DATA_FORMAT - mmLB_DATA_FORMAT),
},
{	.scl_offset = (mmSCL4_SCL_CONTROL - mmSCL_CONTROL),
	.dcfe_offset = (mmDCFE4_DCFE_MEM_PWR_CTRL - mmDCFE_MEM_PWR_CTRL),
	.dcp_offset = (mmDCP4_GRPH_CONTROL - mmGRPH_CONTROL),
	.lb_offset = (mmLB4_LB_DATA_FORMAT - mmLB_DATA_FORMAT),
},
{	.scl_offset = (mmSCL5_SCL_CONTROL - mmSCL_CONTROL),
	.dcfe_offset = (mmDCFE5_DCFE_MEM_PWR_CTRL - mmDCFE_MEM_PWR_CTRL),
	.dcp_offset = (mmDCP5_GRPH_CONTROL - mmGRPH_CONTROL),
	.lb_offset = (mmLB5_LB_DATA_FORMAT - mmLB_DATA_FORMAT),
}
};

static const struct dce110_timing_generator_offsets dce112_tg_offsets[] = {
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
		.dcp = (mmDCP3_GRPH_CONTROL - mmGRPH_CONTROL),
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

static const struct dce110_mem_input_reg_offsets dce112_mi_reg_offsets[] = {
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

static const struct dce110_ipp_reg_offsets ipp_reg_offsets[] = {
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

static const struct dce110_opp_reg_offsets dce112_opp_reg_offsets[] = {
{
	.fmt_offset = (mmFMT0_FMT_CONTROL - mmFMT0_FMT_CONTROL),
	.dcfe_offset = (mmDCFE0_DCFE_MEM_PWR_CTRL - mmDCFE0_DCFE_MEM_PWR_CTRL),
	.dcp_offset = (mmDCP0_GRPH_CONTROL - mmDCP0_GRPH_CONTROL),
},
{	.fmt_offset = (mmFMT1_FMT_CONTROL - mmFMT0_FMT_CONTROL),
	.dcfe_offset = (mmDCFE1_DCFE_MEM_PWR_CTRL - mmDCFE0_DCFE_MEM_PWR_CTRL),
	.dcp_offset = (mmDCP1_GRPH_CONTROL - mmDCP0_GRPH_CONTROL),
},
{	.fmt_offset = (mmFMT2_FMT_CONTROL - mmFMT0_FMT_CONTROL),
	.dcfe_offset = (mmDCFE2_DCFE_MEM_PWR_CTRL - mmDCFE0_DCFE_MEM_PWR_CTRL),
	.dcp_offset = (mmDCP2_GRPH_CONTROL - mmDCP0_GRPH_CONTROL),
},
{
	.fmt_offset = (mmFMT3_FMT_CONTROL - mmFMT0_FMT_CONTROL),
	.dcfe_offset = (mmDCFE3_DCFE_MEM_PWR_CTRL - mmDCFE0_DCFE_MEM_PWR_CTRL),
	.dcp_offset = (mmDCP3_GRPH_CONTROL - mmDCP0_GRPH_CONTROL),
},
{	.fmt_offset = (mmFMT4_FMT_CONTROL - mmFMT0_FMT_CONTROL),
	.dcfe_offset = (mmDCFE4_DCFE_MEM_PWR_CTRL - mmDCFE0_DCFE_MEM_PWR_CTRL),
	.dcp_offset = (mmDCP4_GRPH_CONTROL - mmDCP0_GRPH_CONTROL),
},
{	.fmt_offset = (mmFMT5_FMT_CONTROL - mmFMT0_FMT_CONTROL),
	.dcfe_offset = (mmDCFE5_DCFE_MEM_PWR_CTRL - mmDCFE0_DCFE_MEM_PWR_CTRL),
	.dcp_offset = (mmDCP5_GRPH_CONTROL - mmDCP0_GRPH_CONTROL),
}
};

static const struct dce112_clk_src_reg_offsets dce112_clk_src_reg_offsets[] = {
	{
		.pixclk_resync_cntl  = mmPHYPLLA_PIXCLK_RESYNC_CNTL
	},
	{
		.pixclk_resync_cntl  = mmPHYPLLB_PIXCLK_RESYNC_CNTL
	},
	{
		.pixclk_resync_cntl  = mmPHYPLLC_PIXCLK_RESYNC_CNTL
	},
	{
		.pixclk_resync_cntl  = mmPHYPLLD_PIXCLK_RESYNC_CNTL
	},
	{
		.pixclk_resync_cntl  = mmPHYPLLE_PIXCLK_RESYNC_CNTL
	},
	{
		.pixclk_resync_cntl  = mmPHYPLLF_PIXCLK_RESYNC_CNTL
	}
};

static struct timing_generator *dce112_timing_generator_create(
		struct adapter_service *as,
		struct dc_context *ctx,
		uint32_t instance,
		const struct dce110_timing_generator_offsets *offsets)
{
	struct dce110_timing_generator *tg110 =
		dm_alloc(sizeof(struct dce110_timing_generator));

	if (!tg110)
		return NULL;

	if (dce110_timing_generator_construct(tg110, as, ctx, instance, offsets))
		return &tg110->base;

	BREAK_TO_DEBUGGER();
	dm_free(tg110);
	return NULL;
}

static struct stream_encoder *dce112_stream_encoder_create(
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

static struct mem_input *dce112_mem_input_create(
	struct dc_context *ctx,
	uint32_t inst,
	const struct dce110_mem_input_reg_offsets *offset)
{
	struct dce110_mem_input *mem_input110 =
		dm_alloc(sizeof(struct dce110_mem_input));

	if (!mem_input110)
		return NULL;

	if (dce112_mem_input_construct(mem_input110,
			ctx, inst, offset))
		return &mem_input110->base;

	BREAK_TO_DEBUGGER();
	dm_free(mem_input110);
	return NULL;
}

static void dce112_transform_destroy(struct transform **xfm)
{
	dm_free(TO_DCE110_TRANSFORM(*xfm));
	*xfm = NULL;
}

static struct transform *dce112_transform_create(
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
struct link_encoder *dce112_link_encoder_create(
	const struct encoder_init_data *enc_init_data)
{
	struct dce110_link_encoder *enc110 =
		dm_alloc(sizeof(struct dce110_link_encoder));

	if (!enc110)
		return NULL;

	if (dce112_link_encoder_construct(
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

struct input_pixel_processor *dce112_ipp_create(
	struct dc_context *ctx,
	uint32_t inst,
	const struct dce110_ipp_reg_offsets *offset)
{
	struct dce110_ipp *ipp =
		dm_alloc(sizeof(struct dce110_ipp));

	if (!ipp)
		return NULL;

	if (dce110_ipp_construct(ipp, ctx, inst, offset))
			return &ipp->base;

	BREAK_TO_DEBUGGER();
	dm_free(ipp);
	return NULL;
}

void dce112_ipp_destroy(struct input_pixel_processor **ipp)
{
	dm_free(TO_DCE110_IPP(*ipp));
	*ipp = NULL;
}

struct output_pixel_processor *dce112_opp_create(
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

void dce112_opp_destroy(struct output_pixel_processor **opp)
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

struct clock_source *dce112_clock_source_create(
	struct dc_context *ctx,
	struct dc_bios *bios,
	enum clock_source_id id,
	const struct dce112_clk_src_reg_offsets *offsets)
{
	struct dce112_clk_src *clk_src =
		dm_alloc(sizeof(struct dce112_clk_src));

	if (!clk_src)
		return NULL;

	if (dce112_clk_src_construct(clk_src, ctx, bios, id, offsets))
		return &clk_src->base;

	BREAK_TO_DEBUGGER();
	return NULL;
}

void dce112_clock_source_destroy(struct clock_source **clk_src)
{
	dm_free(TO_DCE112_CLK_SRC(*clk_src));
	*clk_src = NULL;
}

void dce112_destruct_resource_pool(struct resource_pool *pool)
{
	unsigned int i;

	for (i = 0; i < pool->pipe_count; i++) {
		if (pool->opps[i] != NULL)
			dce112_opp_destroy(&pool->opps[i]);

		if (pool->transforms[i] != NULL)
			dce112_transform_destroy(&pool->transforms[i]);

		if (pool->ipps[i] != NULL)
			dce112_ipp_destroy(&pool->ipps[i]);

		if (pool->mis[i] != NULL) {
			dm_free(TO_DCE110_MEM_INPUT(pool->mis[i]));
			pool->mis[i] = NULL;
		}

		if (pool->timing_generators[i] != NULL) {
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
			dce112_clock_source_destroy(&pool->clock_sources[i]);
		}
	}

	if (pool->dp_clock_source != NULL)
		dce112_clock_source_destroy(&pool->dp_clock_source);

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

static struct clock_source *find_matching_pll(struct resource_context *res_ctx,
		const struct core_stream *const stream)
{
	switch (stream->sink->link->link_enc->transmitter) {
	case TRANSMITTER_UNIPHY_A:
		return res_ctx->pool.clock_sources[DCE112_CLK_SRC_PLL0];
	case TRANSMITTER_UNIPHY_B:
		return res_ctx->pool.clock_sources[DCE112_CLK_SRC_PLL1];
	case TRANSMITTER_UNIPHY_C:
		return res_ctx->pool.clock_sources[DCE112_CLK_SRC_PLL2];
	case TRANSMITTER_UNIPHY_D:
		return res_ctx->pool.clock_sources[DCE112_CLK_SRC_PLL3];
	case TRANSMITTER_UNIPHY_E:
		return res_ctx->pool.clock_sources[DCE112_CLK_SRC_PLL4];
	case TRANSMITTER_UNIPHY_F:
		return res_ctx->pool.clock_sources[DCE112_CLK_SRC_PLL5];
	default:
		return NULL;
	};

	return 0;
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

enum dc_status dce112_validate_bandwidth(
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
	context->target_flags[target_idx].unchanged = true;
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
					pipe_ctx->clock_source =
						context->res_ctx.pool.dp_clock_source;
				else
					pipe_ctx->clock_source =
							find_matching_pll(&context->res_ctx,
									stream);

				if (pipe_ctx->clock_source == NULL)
					return DC_NO_CLOCK_SOURCE_RESOURCE;

				resource_reference_clock_source(
						&context->res_ctx,
						pipe_ctx->clock_source);

				/* only one cs per stream regardless of mpo */
				break;
			}
		}
	}

	return DC_OK;
}

enum dc_status dce112_validate_with_context(
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
		result = map_clock_resources(dc, context);

	if (result == DC_OK)
		result = validate_mapped_resource(dc, context);

	if (result == DC_OK)
		resource_build_scaling_params_for_context(dc, context);

	if (result == DC_OK)
		result = dce112_validate_bandwidth(dc, context);

	return result;
}

static struct resource_funcs dce112_res_pool_funcs = {
	.destruct = dce112_destruct_resource_pool,
	.link_enc_create = dce112_link_encoder_create,
	.validate_with_context = dce112_validate_with_context,
	.validate_bandwidth = dce112_validate_bandwidth
};

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


bool dce112_construct_resource_pool(
	struct adapter_service *adapter_serv,
	uint8_t num_virtual_links,
	struct core_dc *dc,
	struct resource_pool *pool)
{
	unsigned int i;
	struct audio_init_data audio_init_data = { 0 };
	struct dc_context *ctx = dc->ctx;

	pool->adapter_srv = adapter_serv;
	pool->funcs = &dce112_res_pool_funcs;

	pool->stream_engines.engine.ENGINE_ID_DIGA = 1;
	pool->stream_engines.engine.ENGINE_ID_DIGB = 1;
	pool->stream_engines.engine.ENGINE_ID_DIGC = 1;
	pool->stream_engines.engine.ENGINE_ID_DIGD = 1;
	pool->stream_engines.engine.ENGINE_ID_DIGE = 1;
	pool->stream_engines.engine.ENGINE_ID_DIGF = 1;

	pool->clock_sources[DCE112_CLK_SRC_PLL0] = dce112_clock_source_create(
		ctx, dal_adapter_service_get_bios_parser(adapter_serv),
		CLOCK_SOURCE_COMBO_PHY_PLL0, &dce112_clk_src_reg_offsets[0]);
	pool->clock_sources[DCE112_CLK_SRC_PLL1] = dce112_clock_source_create(
		ctx, dal_adapter_service_get_bios_parser(adapter_serv),
		CLOCK_SOURCE_COMBO_PHY_PLL1, &dce112_clk_src_reg_offsets[1]);
	pool->clock_sources[DCE112_CLK_SRC_PLL2] = dce112_clock_source_create(
		ctx, dal_adapter_service_get_bios_parser(adapter_serv),
		CLOCK_SOURCE_COMBO_PHY_PLL2, &dce112_clk_src_reg_offsets[2]);
	pool->clock_sources[DCE112_CLK_SRC_PLL3] = dce112_clock_source_create(
		ctx, dal_adapter_service_get_bios_parser(adapter_serv),
		CLOCK_SOURCE_COMBO_PHY_PLL3, &dce112_clk_src_reg_offsets[3]);
	pool->clock_sources[DCE112_CLK_SRC_PLL4] = dce112_clock_source_create(
		ctx, dal_adapter_service_get_bios_parser(adapter_serv),
		CLOCK_SOURCE_COMBO_PHY_PLL4, &dce112_clk_src_reg_offsets[4]);
	pool->clock_sources[DCE112_CLK_SRC_PLL5] = dce112_clock_source_create(
		ctx, dal_adapter_service_get_bios_parser(adapter_serv),
		CLOCK_SOURCE_COMBO_PHY_PLL5, &dce112_clk_src_reg_offsets[5]);
	pool->clk_src_count = DCE112_CLK_SRC_TOTAL;

	pool->dp_clock_source =  dce112_clock_source_create(
		ctx, dal_adapter_service_get_bios_parser(adapter_serv),
		CLOCK_SOURCE_ID_DP_DTO, &dce112_clk_src_reg_offsets[0]);

	for (i = 0; i < pool->clk_src_count; i++) {
		if (pool->clock_sources[i] == NULL) {
			dm_error("DC: failed to create clock sources!\n");
			BREAK_TO_DEBUGGER();
			goto clk_src_create_fail;
		}
	}

	pool->display_clock = dal_display_clock_dce112_create(ctx, adapter_serv);
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

	pool->pipe_count =
		dal_adapter_service_get_func_controllers_num(adapter_serv);
	pool->stream_enc_count = dal_adapter_service_get_stream_engines_num(adapter_serv);
	pool->scaler_filter = dal_scaler_filter_create(ctx);
	if (pool->scaler_filter == NULL) {
		BREAK_TO_DEBUGGER();
		dm_error("DC: failed to create filter!\n");
		goto filter_create_fail;
	}

	for (i = 0; i < pool->pipe_count; i++) {
		pool->timing_generators[i] = dce112_timing_generator_create(
				adapter_serv,
				ctx,
				i,
				&dce112_tg_offsets[i]);
		if (pool->timing_generators[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error("DC: failed to create tg!\n");
			goto controller_create_fail;
		}

		pool->mis[i] = dce112_mem_input_create(
			ctx,
			i,
			&dce112_mi_reg_offsets[i]);
		if (pool->mis[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC: failed to create memory input!\n");
			goto controller_create_fail;
		}

		pool->ipps[i] = dce112_ipp_create(
			ctx,
			i,
			&ipp_reg_offsets[i]);
		if (pool->ipps[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC: failed to create input pixel processor!\n");
			goto controller_create_fail;
		}

		pool->transforms[i] = dce112_transform_create(
				ctx,
				i,
				&dce112_xfm_offsets[i]);
		if (pool->transforms[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC: failed to create transform!\n");
			goto controller_create_fail;
		}
		pool->transforms[i]->funcs->transform_set_scaler_filter(
				pool->transforms[i],
				pool->scaler_filter);

		pool->opps[i] = dce112_opp_create(
			ctx,
			i,
			&dce112_opp_reg_offsets[i]);
		if (pool->opps[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC: failed to create output pixel processor!\n");
			goto controller_create_fail;
		}
	}

	audio_init_data.as = adapter_serv;
	audio_init_data.ctx = ctx;
	pool->audio_count = 0;
	for (i = 0; i < pool->pipe_count; i++) {
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
			dm_error("DC: failed to create DPPs!\n");
			goto audio_create_fail;
		}
		pool->audio_count++;
	}

	for (i = 0; i < pool->stream_enc_count; i++) {
		/* TODO: rework fragile code*/
		if (pool->stream_engines.u_all & 1 << i) {
			pool->stream_enc[i] = dce112_stream_encoder_create(
				i, dc->ctx,
				dal_adapter_service_get_bios_parser(
					adapter_serv),
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
								adapter_serv));
		if (pool->stream_enc[pool->stream_enc_count] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error("DC: failed to create stream_encoder!\n");
			goto stream_enc_create_fail;
		}
		pool->stream_enc_count++;
	}

	/* Create hardware sequencer */
	if (!dc_construct_hw_sequencer(adapter_serv, dc))
		goto stream_enc_create_fail;

	bw_calcs_init(&dc->bw_dceip, &dc->bw_vbios, BW_CALCS_VERSION_BAFFIN);

	bw_calcs_data_update_from_pplib(dc);

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
			dce112_opp_destroy(&pool->opps[i]);

		if (pool->transforms[i] != NULL)
			dce112_transform_destroy(&pool->transforms[i]);

		if (pool->ipps[i] != NULL)
			dce112_ipp_destroy(&pool->ipps[i]);

		if (pool->mis[i] != NULL) {
			dm_free(TO_DCE110_MEM_INPUT(pool->mis[i]));
			pool->mis[i] = NULL;
		}

		if (pool->timing_generators[i] != NULL) {
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
			dce112_clock_source_destroy(&pool->clock_sources[i]);
	}

	return false;
}
