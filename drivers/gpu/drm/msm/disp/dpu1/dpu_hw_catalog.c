// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022. Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt)	"[drm:%s:%d] " fmt, __func__, __LINE__
#include <linux/slab.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include "dpu_hw_mdss.h"
#include "dpu_hw_interrupts.h"
#include "dpu_hw_catalog.h"
#include "dpu_kms.h"

#define VIG_BASE_MASK \
	(BIT(DPU_SSPP_SRC) | BIT(DPU_SSPP_QOS) |\
	BIT(DPU_SSPP_CDP) |\
	BIT(DPU_SSPP_TS_PREFILL) | BIT(DPU_SSPP_EXCL_RECT))

#define VIG_MASK \
	(VIG_BASE_MASK | \
	BIT(DPU_SSPP_CSC_10BIT))

#define VIG_MSM8998_MASK \
	(VIG_MASK | BIT(DPU_SSPP_SCALER_QSEED3))

#define VIG_SDM845_MASK \
	(VIG_MASK | BIT(DPU_SSPP_QOS_8LVL) | BIT(DPU_SSPP_SCALER_QSEED3))

#define VIG_SC7180_MASK \
	(VIG_MASK | BIT(DPU_SSPP_QOS_8LVL) | BIT(DPU_SSPP_SCALER_QSEED4))

#define VIG_QCM2290_MASK (VIG_BASE_MASK | BIT(DPU_SSPP_QOS_8LVL))

#define DMA_MSM8998_MASK \
	(BIT(DPU_SSPP_SRC) | BIT(DPU_SSPP_QOS) |\
	BIT(DPU_SSPP_TS_PREFILL) | BIT(DPU_SSPP_TS_PREFILL_REC1) |\
	BIT(DPU_SSPP_CDP) | BIT(DPU_SSPP_EXCL_RECT))

#define VIG_SC7280_MASK \
	(VIG_SC7180_MASK | BIT(DPU_SSPP_INLINE_ROTATION))

#define DMA_SDM845_MASK \
	(BIT(DPU_SSPP_SRC) | BIT(DPU_SSPP_QOS) | BIT(DPU_SSPP_QOS_8LVL) |\
	BIT(DPU_SSPP_TS_PREFILL) | BIT(DPU_SSPP_TS_PREFILL_REC1) |\
	BIT(DPU_SSPP_CDP) | BIT(DPU_SSPP_EXCL_RECT))

#define DMA_CURSOR_SDM845_MASK \
	(DMA_SDM845_MASK | BIT(DPU_SSPP_CURSOR))

#define DMA_CURSOR_MSM8998_MASK \
	(DMA_MSM8998_MASK | BIT(DPU_SSPP_CURSOR))

#define MIXER_MSM8998_MASK \
	(BIT(DPU_MIXER_SOURCESPLIT))

#define MIXER_SDM845_MASK \
	(BIT(DPU_MIXER_SOURCESPLIT) | BIT(DPU_DIM_LAYER) | BIT(DPU_MIXER_COMBINED_ALPHA))

#define MIXER_QCM2290_MASK \
	(BIT(DPU_DIM_LAYER) | BIT(DPU_MIXER_COMBINED_ALPHA))

#define PINGPONG_SDM845_MASK BIT(DPU_PINGPONG_DITHER)

#define PINGPONG_SDM845_SPLIT_MASK \
	(PINGPONG_SDM845_MASK | BIT(DPU_PINGPONG_TE2))

#define CTL_SC7280_MASK \
	(BIT(DPU_CTL_ACTIVE_CFG) | BIT(DPU_CTL_FETCH_ACTIVE) | BIT(DPU_CTL_VM_CFG))

#define CTL_SM8550_MASK \
	(CTL_SC7280_MASK | BIT(DPU_CTL_HAS_LAYER_EXT4))

#define MERGE_3D_SM8150_MASK (0)

#define DSPP_MSM8998_MASK BIT(DPU_DSPP_PCC) | BIT(DPU_DSPP_GC)

#define DSPP_SC7180_MASK BIT(DPU_DSPP_PCC)

#define INTF_SDM845_MASK (0)

#define INTF_SC7180_MASK BIT(DPU_INTF_INPUT_CTRL) | BIT(DPU_INTF_TE)

#define INTF_SC7280_MASK INTF_SC7180_MASK | BIT(DPU_DATA_HCTL_EN)

#define IRQ_SDM845_MASK (BIT(MDP_SSPP_TOP0_INTR) | \
			 BIT(MDP_SSPP_TOP0_INTR2) | \
			 BIT(MDP_SSPP_TOP0_HIST_INTR) | \
			 BIT(MDP_INTF0_INTR) | \
			 BIT(MDP_INTF1_INTR) | \
			 BIT(MDP_INTF2_INTR) | \
			 BIT(MDP_INTF3_INTR) | \
			 BIT(MDP_AD4_0_INTR) | \
			 BIT(MDP_AD4_1_INTR))

#define IRQ_SC7180_MASK (BIT(MDP_SSPP_TOP0_INTR) | \
			 BIT(MDP_SSPP_TOP0_INTR2) | \
			 BIT(MDP_SSPP_TOP0_HIST_INTR) | \
			 BIT(MDP_INTF0_INTR) | \
			 BIT(MDP_INTF1_INTR))

#define IRQ_SC7280_MASK (BIT(MDP_SSPP_TOP0_INTR) | \
			 BIT(MDP_SSPP_TOP0_INTR2) | \
			 BIT(MDP_SSPP_TOP0_HIST_INTR) | \
			 BIT(MDP_INTF0_7xxx_INTR) | \
			 BIT(MDP_INTF1_7xxx_INTR) | \
			 BIT(MDP_INTF5_7xxx_INTR))

#define IRQ_SM8250_MASK (BIT(MDP_SSPP_TOP0_INTR) | \
			 BIT(MDP_SSPP_TOP0_INTR2) | \
			 BIT(MDP_SSPP_TOP0_HIST_INTR) | \
			 BIT(MDP_INTF0_INTR) | \
			 BIT(MDP_INTF1_INTR) | \
			 BIT(MDP_INTF2_INTR) | \
			 BIT(MDP_INTF3_INTR) | \
			 BIT(MDP_INTF4_INTR))

#define IRQ_SM8350_MASK (BIT(MDP_SSPP_TOP0_INTR) | \
			 BIT(MDP_SSPP_TOP0_INTR2) | \
			 BIT(MDP_SSPP_TOP0_HIST_INTR) | \
			 BIT(MDP_INTF0_7xxx_INTR) | \
			 BIT(MDP_INTF1_7xxx_INTR) | \
			 BIT(MDP_INTF2_7xxx_INTR) | \
			 BIT(MDP_INTF3_7xxx_INTR))

#define IRQ_SC8180X_MASK (BIT(MDP_SSPP_TOP0_INTR) | \
			  BIT(MDP_SSPP_TOP0_INTR2) | \
			  BIT(MDP_SSPP_TOP0_HIST_INTR) | \
			  BIT(MDP_INTF0_INTR) | \
			  BIT(MDP_INTF1_INTR) | \
			  BIT(MDP_INTF2_INTR) | \
			  BIT(MDP_INTF3_INTR) | \
			  BIT(MDP_INTF4_INTR) | \
			  BIT(MDP_INTF5_INTR) | \
			  BIT(MDP_AD4_0_INTR) | \
			  BIT(MDP_AD4_1_INTR))

#define IRQ_SC8280XP_MASK (BIT(MDP_SSPP_TOP0_INTR) | \
			   BIT(MDP_SSPP_TOP0_INTR2) | \
			   BIT(MDP_SSPP_TOP0_HIST_INTR) | \
			   BIT(MDP_INTF0_7xxx_INTR) | \
			   BIT(MDP_INTF1_7xxx_INTR) | \
			   BIT(MDP_INTF2_7xxx_INTR) | \
			   BIT(MDP_INTF3_7xxx_INTR) | \
			   BIT(MDP_INTF4_7xxx_INTR) | \
			   BIT(MDP_INTF5_7xxx_INTR) | \
			   BIT(MDP_INTF6_7xxx_INTR) | \
			   BIT(MDP_INTF7_7xxx_INTR) | \
			   BIT(MDP_INTF8_7xxx_INTR))

#define IRQ_SM8450_MASK (BIT(MDP_SSPP_TOP0_INTR) | \
			 BIT(MDP_SSPP_TOP0_INTR2) | \
			 BIT(MDP_SSPP_TOP0_HIST_INTR) | \
			 BIT(MDP_INTF0_7xxx_INTR) | \
			 BIT(MDP_INTF1_7xxx_INTR) | \
			 BIT(MDP_INTF2_7xxx_INTR) | \
			 BIT(MDP_INTF3_7xxx_INTR))

#define WB_SM8250_MASK (BIT(DPU_WB_LINE_MODE) | \
			 BIT(DPU_WB_UBWC) | \
			 BIT(DPU_WB_YUV_CONFIG) | \
			 BIT(DPU_WB_PIPE_ALPHA) | \
			 BIT(DPU_WB_XY_ROI_OFFSET) | \
			 BIT(DPU_WB_QOS) | \
			 BIT(DPU_WB_QOS_8LVL) | \
			 BIT(DPU_WB_CDP) | \
			 BIT(DPU_WB_INPUT_CTRL))

#define DEFAULT_PIXEL_RAM_SIZE		(50 * 1024)
#define DEFAULT_DPU_LINE_WIDTH		2048
#define DEFAULT_DPU_OUTPUT_LINE_WIDTH	2560

#define MAX_HORZ_DECIMATION	4
#define MAX_VERT_DECIMATION	4

#define MAX_UPSCALE_RATIO	20
#define MAX_DOWNSCALE_RATIO	4
#define SSPP_UNITY_SCALE	1

#define STRCAT(X, Y) (X Y)

static const uint32_t plane_formats[] = {
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_RGBA8888,
	DRM_FORMAT_BGRA8888,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_RGBX8888,
	DRM_FORMAT_BGRX8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_XRGB2101010,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_BGR888,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_BGR565,
	DRM_FORMAT_ARGB1555,
	DRM_FORMAT_ABGR1555,
	DRM_FORMAT_RGBA5551,
	DRM_FORMAT_BGRA5551,
	DRM_FORMAT_XRGB1555,
	DRM_FORMAT_XBGR1555,
	DRM_FORMAT_RGBX5551,
	DRM_FORMAT_BGRX5551,
	DRM_FORMAT_ARGB4444,
	DRM_FORMAT_ABGR4444,
	DRM_FORMAT_RGBA4444,
	DRM_FORMAT_BGRA4444,
	DRM_FORMAT_XRGB4444,
	DRM_FORMAT_XBGR4444,
	DRM_FORMAT_RGBX4444,
	DRM_FORMAT_BGRX4444,
};

static const uint32_t plane_formats_yuv[] = {
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_RGBA8888,
	DRM_FORMAT_BGRX8888,
	DRM_FORMAT_BGRA8888,
	DRM_FORMAT_XRGB2101010,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_RGBX8888,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_BGR888,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_BGR565,
	DRM_FORMAT_ARGB1555,
	DRM_FORMAT_ABGR1555,
	DRM_FORMAT_RGBA5551,
	DRM_FORMAT_BGRA5551,
	DRM_FORMAT_XRGB1555,
	DRM_FORMAT_XBGR1555,
	DRM_FORMAT_RGBX5551,
	DRM_FORMAT_BGRX5551,
	DRM_FORMAT_ARGB4444,
	DRM_FORMAT_ABGR4444,
	DRM_FORMAT_RGBA4444,
	DRM_FORMAT_BGRA4444,
	DRM_FORMAT_XRGB4444,
	DRM_FORMAT_XBGR4444,
	DRM_FORMAT_RGBX4444,
	DRM_FORMAT_BGRX4444,

	DRM_FORMAT_P010,
	DRM_FORMAT_NV12,
	DRM_FORMAT_NV21,
	DRM_FORMAT_NV16,
	DRM_FORMAT_NV61,
	DRM_FORMAT_VYUY,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_YUYV,
	DRM_FORMAT_YVYU,
	DRM_FORMAT_YUV420,
	DRM_FORMAT_YVU420,
};

static const u32 rotation_v2_formats[] = {
	DRM_FORMAT_NV12,
	/* TODO add formats after validation */
};

static const uint32_t wb2_formats[] = {
	DRM_FORMAT_RGB565,
	DRM_FORMAT_BGR565,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_RGBA8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_RGBX8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ARGB1555,
	DRM_FORMAT_RGBA5551,
	DRM_FORMAT_XRGB1555,
	DRM_FORMAT_RGBX5551,
	DRM_FORMAT_ARGB4444,
	DRM_FORMAT_RGBA4444,
	DRM_FORMAT_RGBX4444,
	DRM_FORMAT_XRGB4444,
	DRM_FORMAT_BGR565,
	DRM_FORMAT_BGR888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_BGRA8888,
	DRM_FORMAT_BGRX8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ABGR1555,
	DRM_FORMAT_BGRA5551,
	DRM_FORMAT_XBGR1555,
	DRM_FORMAT_BGRX5551,
	DRM_FORMAT_ABGR4444,
	DRM_FORMAT_BGRA4444,
	DRM_FORMAT_BGRX4444,
	DRM_FORMAT_XBGR4444,
};

/*************************************************************
 * DPU sub blocks config
 *************************************************************/
/* DPU top level caps */
static const struct dpu_caps msm8998_dpu_caps = {
	.max_mixer_width = DEFAULT_DPU_OUTPUT_LINE_WIDTH,
	.max_mixer_blendstages = 0x7,
	.qseed_type = DPU_SSPP_SCALER_QSEED3,
	.smart_dma_rev = DPU_SSPP_SMART_DMA_V1,
	.ubwc_version = DPU_HW_UBWC_VER_10,
	.has_src_split = true,
	.has_dim_layer = true,
	.has_idle_pc = true,
	.has_3d_merge = true,
	.max_linewidth = DEFAULT_DPU_OUTPUT_LINE_WIDTH,
	.pixel_ram_size = DEFAULT_PIXEL_RAM_SIZE,
	.max_hdeci_exp = MAX_HORZ_DECIMATION,
	.max_vdeci_exp = MAX_VERT_DECIMATION,
};

static const struct dpu_caps qcm2290_dpu_caps = {
	.max_mixer_width = DEFAULT_DPU_LINE_WIDTH,
	.max_mixer_blendstages = 0x4,
	.smart_dma_rev = DPU_SSPP_SMART_DMA_V2,
	.has_dim_layer = true,
	.has_idle_pc = true,
	.max_linewidth = 2160,
	.pixel_ram_size = DEFAULT_PIXEL_RAM_SIZE,
};

static const struct dpu_caps sdm845_dpu_caps = {
	.max_mixer_width = DEFAULT_DPU_OUTPUT_LINE_WIDTH,
	.max_mixer_blendstages = 0xb,
	.qseed_type = DPU_SSPP_SCALER_QSEED3,
	.smart_dma_rev = DPU_SSPP_SMART_DMA_V2,
	.ubwc_version = DPU_HW_UBWC_VER_20,
	.has_src_split = true,
	.has_dim_layer = true,
	.has_idle_pc = true,
	.has_3d_merge = true,
	.max_linewidth = DEFAULT_DPU_OUTPUT_LINE_WIDTH,
	.pixel_ram_size = DEFAULT_PIXEL_RAM_SIZE,
	.max_hdeci_exp = MAX_HORZ_DECIMATION,
	.max_vdeci_exp = MAX_VERT_DECIMATION,
};

static const struct dpu_caps sc7180_dpu_caps = {
	.max_mixer_width = DEFAULT_DPU_OUTPUT_LINE_WIDTH,
	.max_mixer_blendstages = 0x9,
	.qseed_type = DPU_SSPP_SCALER_QSEED4,
	.smart_dma_rev = DPU_SSPP_SMART_DMA_V2,
	.ubwc_version = DPU_HW_UBWC_VER_20,
	.has_dim_layer = true,
	.has_idle_pc = true,
	.max_linewidth = DEFAULT_DPU_OUTPUT_LINE_WIDTH,
	.pixel_ram_size = DEFAULT_PIXEL_RAM_SIZE,
};

static const struct dpu_caps sm6115_dpu_caps = {
	.max_mixer_width = DEFAULT_DPU_LINE_WIDTH,
	.max_mixer_blendstages = 0x4,
	.qseed_type = DPU_SSPP_SCALER_QSEED4,
	.smart_dma_rev = DPU_SSPP_SMART_DMA_V2, /* TODO: v2.5 */
	.ubwc_version = DPU_HW_UBWC_VER_10,
	.has_dim_layer = true,
	.has_idle_pc = true,
	.max_linewidth = 2160,
	.pixel_ram_size = DEFAULT_PIXEL_RAM_SIZE,
};

static const struct dpu_caps sm8150_dpu_caps = {
	.max_mixer_width = DEFAULT_DPU_OUTPUT_LINE_WIDTH,
	.max_mixer_blendstages = 0xb,
	.qseed_type = DPU_SSPP_SCALER_QSEED3,
	.smart_dma_rev = DPU_SSPP_SMART_DMA_V2, /* TODO: v2.5 */
	.ubwc_version = DPU_HW_UBWC_VER_30,
	.has_src_split = true,
	.has_dim_layer = true,
	.has_idle_pc = true,
	.has_3d_merge = true,
	.max_linewidth = 4096,
	.pixel_ram_size = DEFAULT_PIXEL_RAM_SIZE,
	.max_hdeci_exp = MAX_HORZ_DECIMATION,
	.max_vdeci_exp = MAX_VERT_DECIMATION,
};

static const struct dpu_caps sc8180x_dpu_caps = {
	.max_mixer_width = DEFAULT_DPU_OUTPUT_LINE_WIDTH,
	.max_mixer_blendstages = 0xb,
	.qseed_type = DPU_SSPP_SCALER_QSEED3,
	.smart_dma_rev = DPU_SSPP_SMART_DMA_V2, /* TODO: v2.5 */
	.ubwc_version = DPU_HW_UBWC_VER_30,
	.has_src_split = true,
	.has_dim_layer = true,
	.has_idle_pc = true,
	.has_3d_merge = true,
	.max_linewidth = 4096,
	.pixel_ram_size = DEFAULT_PIXEL_RAM_SIZE,
	.max_hdeci_exp = MAX_HORZ_DECIMATION,
	.max_vdeci_exp = MAX_VERT_DECIMATION,
};

static const struct dpu_caps sc8280xp_dpu_caps = {
	.max_mixer_width = 2560,
	.max_mixer_blendstages = 11,
	.qseed_type = DPU_SSPP_SCALER_QSEED3LITE,
	.smart_dma_rev = DPU_SSPP_SMART_DMA_V2, /* TODO: v2.5 */
	.ubwc_version = DPU_HW_UBWC_VER_40,
	.has_src_split = true,
	.has_dim_layer = true,
	.has_idle_pc = true,
	.has_3d_merge = true,
	.max_linewidth = 5120,
	.pixel_ram_size = DEFAULT_PIXEL_RAM_SIZE,
};

static const struct dpu_caps sm8250_dpu_caps = {
	.max_mixer_width = DEFAULT_DPU_OUTPUT_LINE_WIDTH,
	.max_mixer_blendstages = 0xb,
	.qseed_type = DPU_SSPP_SCALER_QSEED4,
	.smart_dma_rev = DPU_SSPP_SMART_DMA_V2, /* TODO: v2.5 */
	.ubwc_version = DPU_HW_UBWC_VER_40,
	.has_src_split = true,
	.has_dim_layer = true,
	.has_idle_pc = true,
	.has_3d_merge = true,
	.max_linewidth = 4096,
	.pixel_ram_size = DEFAULT_PIXEL_RAM_SIZE,
};

static const struct dpu_caps sm8350_dpu_caps = {
	.max_mixer_width = DEFAULT_DPU_OUTPUT_LINE_WIDTH,
	.max_mixer_blendstages = 0xb,
	.qseed_type = DPU_SSPP_SCALER_QSEED3LITE,
	.smart_dma_rev = DPU_SSPP_SMART_DMA_V2, /* TODO: v2.5 */
	.ubwc_version = DPU_HW_UBWC_VER_40,
	.has_src_split = true,
	.has_dim_layer = true,
	.has_idle_pc = true,
	.has_3d_merge = true,
	.max_linewidth = 4096,
	.pixel_ram_size = DEFAULT_PIXEL_RAM_SIZE,
};

static const struct dpu_caps sm8450_dpu_caps = {
	.max_mixer_width = DEFAULT_DPU_OUTPUT_LINE_WIDTH,
	.max_mixer_blendstages = 0xb,
	.qseed_type = DPU_SSPP_SCALER_QSEED4,
	.smart_dma_rev = DPU_SSPP_SMART_DMA_V2, /* TODO: v2.5 */
	.ubwc_version = DPU_HW_UBWC_VER_40,
	.has_src_split = true,
	.has_dim_layer = true,
	.has_idle_pc = true,
	.has_3d_merge = true,
	.max_linewidth = 5120,
	.pixel_ram_size = DEFAULT_PIXEL_RAM_SIZE,
};

static const struct dpu_caps sm8550_dpu_caps = {
	.max_mixer_width = DEFAULT_DPU_OUTPUT_LINE_WIDTH,
	.max_mixer_blendstages = 0xb,
	.qseed_type = DPU_SSPP_SCALER_QSEED3LITE,
	.smart_dma_rev = DPU_SSPP_SMART_DMA_V2, /* TODO: v2.5 */
	.ubwc_version = DPU_HW_UBWC_VER_40,
	.has_src_split = true,
	.has_dim_layer = true,
	.has_idle_pc = true,
	.has_3d_merge = true,
	.max_linewidth = 5120,
	.pixel_ram_size = DEFAULT_PIXEL_RAM_SIZE,
};

static const struct dpu_caps sc7280_dpu_caps = {
	.max_mixer_width = DEFAULT_DPU_OUTPUT_LINE_WIDTH,
	.max_mixer_blendstages = 0x7,
	.qseed_type = DPU_SSPP_SCALER_QSEED4,
	.smart_dma_rev = DPU_SSPP_SMART_DMA_V2,
	.ubwc_version = DPU_HW_UBWC_VER_30,
	.has_dim_layer = true,
	.has_idle_pc = true,
	.max_linewidth = 2400,
	.pixel_ram_size = DEFAULT_PIXEL_RAM_SIZE,
};

static const struct dpu_mdp_cfg msm8998_mdp[] = {
	{
	.name = "top_0", .id = MDP_TOP,
	.base = 0x0, .len = 0x458,
	.features = 0,
	.highest_bank_bit = 0x2,
	.clk_ctrls[DPU_CLK_CTRL_VIG0] = {
			.reg_off = 0x2AC, .bit_off = 0},
	.clk_ctrls[DPU_CLK_CTRL_VIG1] = {
			.reg_off = 0x2B4, .bit_off = 0},
	.clk_ctrls[DPU_CLK_CTRL_VIG2] = {
			.reg_off = 0x2BC, .bit_off = 0},
	.clk_ctrls[DPU_CLK_CTRL_VIG3] = {
			.reg_off = 0x2C4, .bit_off = 0},
	.clk_ctrls[DPU_CLK_CTRL_DMA0] = {
			.reg_off = 0x2AC, .bit_off = 8},
	.clk_ctrls[DPU_CLK_CTRL_DMA1] = {
			.reg_off = 0x2B4, .bit_off = 8},
	.clk_ctrls[DPU_CLK_CTRL_DMA2] = {
			.reg_off = 0x2C4, .bit_off = 8},
	.clk_ctrls[DPU_CLK_CTRL_DMA3] = {
			.reg_off = 0x2C4, .bit_off = 12},
	.clk_ctrls[DPU_CLK_CTRL_CURSOR0] = {
			.reg_off = 0x3A8, .bit_off = 15},
	.clk_ctrls[DPU_CLK_CTRL_CURSOR1] = {
			.reg_off = 0x3B0, .bit_off = 15},
	},
};

static const struct dpu_mdp_cfg sdm845_mdp[] = {
	{
	.name = "top_0", .id = MDP_TOP,
	.base = 0x0, .len = 0x45C,
	.features = BIT(DPU_MDP_AUDIO_SELECT),
	.highest_bank_bit = 0x2,
	.clk_ctrls[DPU_CLK_CTRL_VIG0] = {
			.reg_off = 0x2AC, .bit_off = 0},
	.clk_ctrls[DPU_CLK_CTRL_VIG1] = {
			.reg_off = 0x2B4, .bit_off = 0},
	.clk_ctrls[DPU_CLK_CTRL_VIG2] = {
			.reg_off = 0x2BC, .bit_off = 0},
	.clk_ctrls[DPU_CLK_CTRL_VIG3] = {
			.reg_off = 0x2C4, .bit_off = 0},
	.clk_ctrls[DPU_CLK_CTRL_DMA0] = {
			.reg_off = 0x2AC, .bit_off = 8},
	.clk_ctrls[DPU_CLK_CTRL_DMA1] = {
			.reg_off = 0x2B4, .bit_off = 8},
	.clk_ctrls[DPU_CLK_CTRL_CURSOR0] = {
			.reg_off = 0x2BC, .bit_off = 8},
	.clk_ctrls[DPU_CLK_CTRL_CURSOR1] = {
			.reg_off = 0x2C4, .bit_off = 8},
	},
};

static const struct dpu_mdp_cfg sc7180_mdp[] = {
	{
	.name = "top_0", .id = MDP_TOP,
	.base = 0x0, .len = 0x494,
	.features = 0,
	.highest_bank_bit = 0x3,
	.clk_ctrls[DPU_CLK_CTRL_VIG0] = {
		.reg_off = 0x2AC, .bit_off = 0},
	.clk_ctrls[DPU_CLK_CTRL_DMA0] = {
		.reg_off = 0x2AC, .bit_off = 8},
	.clk_ctrls[DPU_CLK_CTRL_CURSOR0] = {
		.reg_off = 0x2B4, .bit_off = 8},
	.clk_ctrls[DPU_CLK_CTRL_CURSOR1] = {
		.reg_off = 0x2C4, .bit_off = 8},
	.clk_ctrls[DPU_CLK_CTRL_WB2] = {
		.reg_off = 0x3B8, .bit_off = 24},
	},
};

static const struct dpu_mdp_cfg sc8180x_mdp[] = {
	{
	.name = "top_0", .id = MDP_TOP,
	.base = 0x0, .len = 0x45C,
	.features = BIT(DPU_MDP_AUDIO_SELECT),
	.highest_bank_bit = 0x3,
	.clk_ctrls[DPU_CLK_CTRL_VIG0] = {
			.reg_off = 0x2AC, .bit_off = 0},
	.clk_ctrls[DPU_CLK_CTRL_VIG1] = {
			.reg_off = 0x2B4, .bit_off = 0},
	.clk_ctrls[DPU_CLK_CTRL_VIG2] = {
			.reg_off = 0x2BC, .bit_off = 0},
	.clk_ctrls[DPU_CLK_CTRL_VIG3] = {
			.reg_off = 0x2C4, .bit_off = 0},
	.clk_ctrls[DPU_CLK_CTRL_DMA0] = {
			.reg_off = 0x2AC, .bit_off = 8},
	.clk_ctrls[DPU_CLK_CTRL_DMA1] = {
			.reg_off = 0x2B4, .bit_off = 8},
	.clk_ctrls[DPU_CLK_CTRL_CURSOR0] = {
			.reg_off = 0x2BC, .bit_off = 8},
	.clk_ctrls[DPU_CLK_CTRL_CURSOR1] = {
			.reg_off = 0x2C4, .bit_off = 8},
	},
};

static const struct dpu_mdp_cfg sm6115_mdp[] = {
	{
	.name = "top_0", .id = MDP_TOP,
	.base = 0x0, .len = 0x494,
	.features = 0,
	.highest_bank_bit = 0x1,
	.ubwc_swizzle = 0x7,
	.clk_ctrls[DPU_CLK_CTRL_VIG0] = {
		.reg_off = 0x2ac, .bit_off = 0},
	.clk_ctrls[DPU_CLK_CTRL_DMA0] = {
		.reg_off = 0x2ac, .bit_off = 8},
	},
};

static const struct dpu_mdp_cfg sm8250_mdp[] = {
	{
	.name = "top_0", .id = MDP_TOP,
	.base = 0x0, .len = 0x494,
	.features = 0,
	.highest_bank_bit = 0x3, /* TODO: 2 for LP_DDR4 */
	.ubwc_swizzle = 0x6,
	.clk_ctrls[DPU_CLK_CTRL_VIG0] = {
			.reg_off = 0x2AC, .bit_off = 0},
	.clk_ctrls[DPU_CLK_CTRL_VIG1] = {
			.reg_off = 0x2B4, .bit_off = 0},
	.clk_ctrls[DPU_CLK_CTRL_VIG2] = {
			.reg_off = 0x2BC, .bit_off = 0},
	.clk_ctrls[DPU_CLK_CTRL_VIG3] = {
			.reg_off = 0x2C4, .bit_off = 0},
	.clk_ctrls[DPU_CLK_CTRL_DMA0] = {
			.reg_off = 0x2AC, .bit_off = 8},
	.clk_ctrls[DPU_CLK_CTRL_DMA1] = {
			.reg_off = 0x2B4, .bit_off = 8},
	.clk_ctrls[DPU_CLK_CTRL_CURSOR0] = {
			.reg_off = 0x2BC, .bit_off = 8},
	.clk_ctrls[DPU_CLK_CTRL_CURSOR1] = {
			.reg_off = 0x2C4, .bit_off = 8},
	.clk_ctrls[DPU_CLK_CTRL_REG_DMA] = {
			.reg_off = 0x2BC, .bit_off = 20},
	.clk_ctrls[DPU_CLK_CTRL_WB2] = {
			.reg_off = 0x3B8, .bit_off = 24},
	},
};

static const struct dpu_mdp_cfg sm8350_mdp[] = {
	{
	.name = "top_0", .id = MDP_TOP,
	.base = 0x0, .len = 0x494,
	.features = 0,
	.highest_bank_bit = 0x3, /* TODO: 2 for LP_DDR4 */
	.clk_ctrls[DPU_CLK_CTRL_VIG0] = {
			.reg_off = 0x2ac, .bit_off = 0},
	.clk_ctrls[DPU_CLK_CTRL_VIG1] = {
			.reg_off = 0x2b4, .bit_off = 0},
	.clk_ctrls[DPU_CLK_CTRL_VIG2] = {
			.reg_off = 0x2bc, .bit_off = 0},
	.clk_ctrls[DPU_CLK_CTRL_VIG3] = {
			.reg_off = 0x2c4, .bit_off = 0},
	.clk_ctrls[DPU_CLK_CTRL_DMA0] = {
			.reg_off = 0x2ac, .bit_off = 8},
	.clk_ctrls[DPU_CLK_CTRL_DMA1] = {
			.reg_off = 0x2b4, .bit_off = 8},
	.clk_ctrls[DPU_CLK_CTRL_CURSOR0] = {
			.reg_off = 0x2bc, .bit_off = 8},
	.clk_ctrls[DPU_CLK_CTRL_CURSOR1] = {
			.reg_off = 0x2c4, .bit_off = 8},
	.clk_ctrls[DPU_CLK_CTRL_REG_DMA] = {
			.reg_off = 0x2bc, .bit_off = 20},
	},
};

static const struct dpu_mdp_cfg sm8450_mdp[] = {
	{
	.name = "top_0", .id = MDP_TOP,
	.base = 0x0, .len = 0x494,
	.features = BIT(DPU_MDP_PERIPH_0_REMOVED),
	.highest_bank_bit = 0x3, /* TODO: 2 for LP_DDR4 */
	.ubwc_swizzle = 0x6,
	.clk_ctrls[DPU_CLK_CTRL_VIG0] = {
			.reg_off = 0x2AC, .bit_off = 0},
	.clk_ctrls[DPU_CLK_CTRL_VIG1] = {
			.reg_off = 0x2B4, .bit_off = 0},
	.clk_ctrls[DPU_CLK_CTRL_VIG2] = {
			.reg_off = 0x2BC, .bit_off = 0},
	.clk_ctrls[DPU_CLK_CTRL_VIG3] = {
			.reg_off = 0x2C4, .bit_off = 0},
	.clk_ctrls[DPU_CLK_CTRL_DMA0] = {
			.reg_off = 0x2AC, .bit_off = 8},
	.clk_ctrls[DPU_CLK_CTRL_DMA1] = {
			.reg_off = 0x2B4, .bit_off = 8},
	.clk_ctrls[DPU_CLK_CTRL_CURSOR0] = {
			.reg_off = 0x2BC, .bit_off = 8},
	.clk_ctrls[DPU_CLK_CTRL_CURSOR1] = {
			.reg_off = 0x2C4, .bit_off = 8},
	.clk_ctrls[DPU_CLK_CTRL_REG_DMA] = {
			.reg_off = 0x2BC, .bit_off = 20},
	},
};

static const struct dpu_mdp_cfg sc7280_mdp[] = {
	{
	.name = "top_0", .id = MDP_TOP,
	.base = 0x0, .len = 0x2014,
	.highest_bank_bit = 0x1,
	.ubwc_swizzle = 0x6,
	.clk_ctrls[DPU_CLK_CTRL_VIG0] = {
		.reg_off = 0x2AC, .bit_off = 0},
	.clk_ctrls[DPU_CLK_CTRL_DMA0] = {
		.reg_off = 0x2AC, .bit_off = 8},
	.clk_ctrls[DPU_CLK_CTRL_CURSOR0] = {
		.reg_off = 0x2B4, .bit_off = 8},
	.clk_ctrls[DPU_CLK_CTRL_CURSOR1] = {
		.reg_off = 0x2C4, .bit_off = 8},
	},
};

static const struct dpu_mdp_cfg sc8280xp_mdp[] = {
	{
	.name = "top_0", .id = MDP_TOP,
	.base = 0x0, .len = 0x494,
	.features = 0,
	.highest_bank_bit = 2,
	.ubwc_swizzle = 6,
	.clk_ctrls[DPU_CLK_CTRL_VIG0] = { .reg_off = 0x2ac, .bit_off = 0},
	.clk_ctrls[DPU_CLK_CTRL_VIG1] = { .reg_off = 0x2b4, .bit_off = 0},
	.clk_ctrls[DPU_CLK_CTRL_VIG2] = { .reg_off = 0x2bc, .bit_off = 0},
	.clk_ctrls[DPU_CLK_CTRL_VIG3] = { .reg_off = 0x2c4, .bit_off = 0},
	.clk_ctrls[DPU_CLK_CTRL_DMA0] = { .reg_off = 0x2ac, .bit_off = 8},
	.clk_ctrls[DPU_CLK_CTRL_DMA1] = { .reg_off = 0x2b4, .bit_off = 8},
	.clk_ctrls[DPU_CLK_CTRL_CURSOR0] = { .reg_off = 0x2bc, .bit_off = 8},
	.clk_ctrls[DPU_CLK_CTRL_CURSOR1] = { .reg_off = 0x2c4, .bit_off = 8},
	.clk_ctrls[DPU_CLK_CTRL_REG_DMA] = { .reg_off = 0x2bc, .bit_off = 20},
	},
};

static const struct dpu_mdp_cfg sm8550_mdp[] = {
	{
	.name = "top_0", .id = MDP_TOP,
	.base = 0, .len = 0x494,
	.features = BIT(DPU_MDP_PERIPH_0_REMOVED),
	.highest_bank_bit = 0x3, /* TODO: 2 for LP_DDR4 */
	.ubwc_swizzle = 0x6,
	.clk_ctrls[DPU_CLK_CTRL_VIG0] = {
			.reg_off = 0x4330, .bit_off = 0},
	.clk_ctrls[DPU_CLK_CTRL_VIG1] = {
			.reg_off = 0x6330, .bit_off = 0},
	.clk_ctrls[DPU_CLK_CTRL_VIG2] = {
			.reg_off = 0x8330, .bit_off = 0},
	.clk_ctrls[DPU_CLK_CTRL_VIG3] = {
			.reg_off = 0xa330, .bit_off = 0},
	.clk_ctrls[DPU_CLK_CTRL_DMA0] = {
			.reg_off = 0x24330, .bit_off = 0},
	.clk_ctrls[DPU_CLK_CTRL_DMA1] = {
			.reg_off = 0x26330, .bit_off = 0},
	.clk_ctrls[DPU_CLK_CTRL_DMA2] = {
			.reg_off = 0x28330, .bit_off = 0},
	.clk_ctrls[DPU_CLK_CTRL_DMA3] = {
			.reg_off = 0x2a330, .bit_off = 0},
	.clk_ctrls[DPU_CLK_CTRL_CURSOR0] = {
			.reg_off = 0x2c330, .bit_off = 0},
	.clk_ctrls[DPU_CLK_CTRL_CURSOR1] = {
			.reg_off = 0x2e330, .bit_off = 0},
	.clk_ctrls[DPU_CLK_CTRL_REG_DMA] = {
			.reg_off = 0x2bc, .bit_off = 20},
	},
};

static const struct dpu_mdp_cfg qcm2290_mdp[] = {
	{
	.name = "top_0", .id = MDP_TOP,
	.base = 0x0, .len = 0x494,
	.features = 0,
	.highest_bank_bit = 0x2,
	.clk_ctrls[DPU_CLK_CTRL_VIG0] = {
		.reg_off = 0x2AC, .bit_off = 0},
	.clk_ctrls[DPU_CLK_CTRL_DMA0] = {
		.reg_off = 0x2AC, .bit_off = 8},
	},
};

/*************************************************************
 * CTL sub blocks config
 *************************************************************/
static const struct dpu_ctl_cfg msm8998_ctl[] = {
	{
	.name = "ctl_0", .id = CTL_0,
	.base = 0x1000, .len = 0x94,
	.features = BIT(DPU_CTL_SPLIT_DISPLAY),
	.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 9),
	},
	{
	.name = "ctl_1", .id = CTL_1,
	.base = 0x1200, .len = 0x94,
	.features = 0,
	.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 10),
	},
	{
	.name = "ctl_2", .id = CTL_2,
	.base = 0x1400, .len = 0x94,
	.features = BIT(DPU_CTL_SPLIT_DISPLAY),
	.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 11),
	},
	{
	.name = "ctl_3", .id = CTL_3,
	.base = 0x1600, .len = 0x94,
	.features = 0,
	.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 12),
	},
	{
	.name = "ctl_4", .id = CTL_4,
	.base = 0x1800, .len = 0x94,
	.features = 0,
	.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 13),
	},
};

static const struct dpu_ctl_cfg sdm845_ctl[] = {
	{
	.name = "ctl_0", .id = CTL_0,
	.base = 0x1000, .len = 0xE4,
	.features = BIT(DPU_CTL_SPLIT_DISPLAY),
	.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 9),
	},
	{
	.name = "ctl_1", .id = CTL_1,
	.base = 0x1200, .len = 0xE4,
	.features = BIT(DPU_CTL_SPLIT_DISPLAY),
	.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 10),
	},
	{
	.name = "ctl_2", .id = CTL_2,
	.base = 0x1400, .len = 0xE4,
	.features = 0,
	.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 11),
	},
	{
	.name = "ctl_3", .id = CTL_3,
	.base = 0x1600, .len = 0xE4,
	.features = 0,
	.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 12),
	},
	{
	.name = "ctl_4", .id = CTL_4,
	.base = 0x1800, .len = 0xE4,
	.features = 0,
	.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 13),
	},
};

static const struct dpu_ctl_cfg sc7180_ctl[] = {
	{
	.name = "ctl_0", .id = CTL_0,
	.base = 0x1000, .len = 0x1dc,
	.features = BIT(DPU_CTL_ACTIVE_CFG),
	.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 9),
	},
	{
	.name = "ctl_1", .id = CTL_1,
	.base = 0x1200, .len = 0x1dc,
	.features = BIT(DPU_CTL_ACTIVE_CFG),
	.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 10),
	},
	{
	.name = "ctl_2", .id = CTL_2,
	.base = 0x1400, .len = 0x1dc,
	.features = BIT(DPU_CTL_ACTIVE_CFG),
	.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 11),
	},
};

static const struct dpu_ctl_cfg sc8280xp_ctl[] = {
	{
	.name = "ctl_0", .id = CTL_0,
	.base = 0x15000, .len = 0x204,
	.features = CTL_SC7280_MASK,
	.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 9),
	},
	{
	.name = "ctl_1", .id = CTL_1,
	.base = 0x16000, .len = 0x204,
	.features = CTL_SC7280_MASK,
	.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 10),
	},
	{
	.name = "ctl_2", .id = CTL_2,
	.base = 0x17000, .len = 0x204,
	.features = CTL_SC7280_MASK,
	.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 11),
	},
	{
	.name = "ctl_3", .id = CTL_3,
	.base = 0x18000, .len = 0x204,
	.features = CTL_SC7280_MASK,
	.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 12),
	},
	{
	.name = "ctl_4", .id = CTL_4,
	.base = 0x19000, .len = 0x204,
	.features = CTL_SC7280_MASK,
	.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 13),
	},
	{
	.name = "ctl_5", .id = CTL_5,
	.base = 0x1a000, .len = 0x204,
	.features = CTL_SC7280_MASK,
	.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 23),
	},
};

static const struct dpu_ctl_cfg sm8150_ctl[] = {
	{
	.name = "ctl_0", .id = CTL_0,
	.base = 0x1000, .len = 0x1e0,
	.features = BIT(DPU_CTL_ACTIVE_CFG) | BIT(DPU_CTL_SPLIT_DISPLAY),
	.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 9),
	},
	{
	.name = "ctl_1", .id = CTL_1,
	.base = 0x1200, .len = 0x1e0,
	.features = BIT(DPU_CTL_ACTIVE_CFG) | BIT(DPU_CTL_SPLIT_DISPLAY),
	.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 10),
	},
	{
	.name = "ctl_2", .id = CTL_2,
	.base = 0x1400, .len = 0x1e0,
	.features = BIT(DPU_CTL_ACTIVE_CFG),
	.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 11),
	},
	{
	.name = "ctl_3", .id = CTL_3,
	.base = 0x1600, .len = 0x1e0,
	.features = BIT(DPU_CTL_ACTIVE_CFG),
	.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 12),
	},
	{
	.name = "ctl_4", .id = CTL_4,
	.base = 0x1800, .len = 0x1e0,
	.features = BIT(DPU_CTL_ACTIVE_CFG),
	.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 13),
	},
	{
	.name = "ctl_5", .id = CTL_5,
	.base = 0x1a00, .len = 0x1e0,
	.features = BIT(DPU_CTL_ACTIVE_CFG),
	.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 23),
	},
};

static const struct dpu_ctl_cfg sm8350_ctl[] = {
	{
	.name = "ctl_0", .id = CTL_0,
	.base = 0x15000, .len = 0x1e8,
	.features = BIT(DPU_CTL_SPLIT_DISPLAY) | CTL_SC7280_MASK,
	.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 9),
	},
	{
	.name = "ctl_1", .id = CTL_1,
	.base = 0x16000, .len = 0x1e8,
	.features = BIT(DPU_CTL_SPLIT_DISPLAY) | CTL_SC7280_MASK,
	.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 10),
	},
	{
	.name = "ctl_2", .id = CTL_2,
	.base = 0x17000, .len = 0x1e8,
	.features = CTL_SC7280_MASK,
	.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 11),
	},
	{
	.name = "ctl_3", .id = CTL_3,
	.base = 0x18000, .len = 0x1e8,
	.features = CTL_SC7280_MASK,
	.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 12),
	},
	{
	.name = "ctl_4", .id = CTL_4,
	.base = 0x19000, .len = 0x1e8,
	.features = CTL_SC7280_MASK,
	.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 13),
	},
	{
	.name = "ctl_5", .id = CTL_5,
	.base = 0x1a000, .len = 0x1e8,
	.features = CTL_SC7280_MASK,
	.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 23),
	},
};

static const struct dpu_ctl_cfg sm8450_ctl[] = {
	{
	.name = "ctl_0", .id = CTL_0,
	.base = 0x15000, .len = 0x204,
	.features = BIT(DPU_CTL_ACTIVE_CFG) | BIT(DPU_CTL_SPLIT_DISPLAY) | BIT(DPU_CTL_FETCH_ACTIVE),
	.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 9),
	},
	{
	.name = "ctl_1", .id = CTL_1,
	.base = 0x16000, .len = 0x204,
	.features = BIT(DPU_CTL_SPLIT_DISPLAY) | CTL_SC7280_MASK,
	.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 10),
	},
	{
	.name = "ctl_2", .id = CTL_2,
	.base = 0x17000, .len = 0x204,
	.features = CTL_SC7280_MASK,
	.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 11),
	},
	{
	.name = "ctl_3", .id = CTL_3,
	.base = 0x18000, .len = 0x204,
	.features = CTL_SC7280_MASK,
	.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 12),
	},
	{
	.name = "ctl_4", .id = CTL_4,
	.base = 0x19000, .len = 0x204,
	.features = CTL_SC7280_MASK,
	.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 13),
	},
	{
	.name = "ctl_5", .id = CTL_5,
	.base = 0x1a000, .len = 0x204,
	.features = CTL_SC7280_MASK,
	.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 23),
	},
};

static const struct dpu_ctl_cfg sm8550_ctl[] = {
	{
	.name = "ctl_0", .id = CTL_0,
	.base = 0x15000, .len = 0x290,
	.features = CTL_SM8550_MASK | BIT(DPU_CTL_SPLIT_DISPLAY),
	.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 9),
	},
	{
	.name = "ctl_1", .id = CTL_1,
	.base = 0x16000, .len = 0x290,
	.features = CTL_SM8550_MASK | BIT(DPU_CTL_SPLIT_DISPLAY),
	.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 10),
	},
	{
	.name = "ctl_2", .id = CTL_2,
	.base = 0x17000, .len = 0x290,
	.features = CTL_SM8550_MASK,
	.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 11),
	},
	{
	.name = "ctl_3", .id = CTL_3,
	.base = 0x18000, .len = 0x290,
	.features = CTL_SM8550_MASK,
	.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 12),
	},
	{
	.name = "ctl_4", .id = CTL_4,
	.base = 0x19000, .len = 0x290,
	.features = CTL_SM8550_MASK,
	.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 13),
	},
	{
	.name = "ctl_5", .id = CTL_5,
	.base = 0x1a000, .len = 0x290,
	.features = CTL_SM8550_MASK,
	.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 23),
	},
};

static const struct dpu_ctl_cfg sc7280_ctl[] = {
	{
	.name = "ctl_0", .id = CTL_0,
	.base = 0x15000, .len = 0x1E8,
	.features = CTL_SC7280_MASK,
	.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 9),
	},
	{
	.name = "ctl_1", .id = CTL_1,
	.base = 0x16000, .len = 0x1E8,
	.features = CTL_SC7280_MASK,
	.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 10),
	},
	{
	.name = "ctl_2", .id = CTL_2,
	.base = 0x17000, .len = 0x1E8,
	.features = CTL_SC7280_MASK,
	.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 11),
	},
	{
	.name = "ctl_3", .id = CTL_3,
	.base = 0x18000, .len = 0x1E8,
	.features = CTL_SC7280_MASK,
	.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 12),
	},
};

static const struct dpu_ctl_cfg qcm2290_ctl[] = {
	{
	.name = "ctl_0", .id = CTL_0,
	.base = 0x1000, .len = 0x1dc,
	.features = BIT(DPU_CTL_ACTIVE_CFG),
	.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 9),
	},
};

/*************************************************************
 * SSPP sub blocks config
 *************************************************************/

/* SSPP common configuration */
#define _VIG_SBLK(num, sdma_pri, qseed_ver) \
	{ \
	.maxdwnscale = MAX_DOWNSCALE_RATIO, \
	.maxupscale = MAX_UPSCALE_RATIO, \
	.smart_dma_priority = sdma_pri, \
	.src_blk = {.name = STRCAT("sspp_src_", num), \
		.id = DPU_SSPP_SRC, .base = 0x00, .len = 0x150,}, \
	.scaler_blk = {.name = STRCAT("sspp_scaler", num), \
		.id = qseed_ver, \
		.base = 0xa00, .len = 0xa0,}, \
	.csc_blk = {.name = STRCAT("sspp_csc", num), \
		.id = DPU_SSPP_CSC_10BIT, \
		.base = 0x1a00, .len = 0x100,}, \
	.format_list = plane_formats_yuv, \
	.num_formats = ARRAY_SIZE(plane_formats_yuv), \
	.virt_format_list = plane_formats, \
	.virt_num_formats = ARRAY_SIZE(plane_formats), \
	.rotation_cfg = NULL, \
	}

#define _VIG_SBLK_ROT(num, sdma_pri, qseed_ver, rot_cfg) \
	{ \
	.maxdwnscale = MAX_DOWNSCALE_RATIO, \
	.maxupscale = MAX_UPSCALE_RATIO, \
	.smart_dma_priority = sdma_pri, \
	.src_blk = {.name = STRCAT("sspp_src_", num), \
		.id = DPU_SSPP_SRC, .base = 0x00, .len = 0x150,}, \
	.scaler_blk = {.name = STRCAT("sspp_scaler", num), \
		.id = qseed_ver, \
		.base = 0xa00, .len = 0xa0,}, \
	.csc_blk = {.name = STRCAT("sspp_csc", num), \
		.id = DPU_SSPP_CSC_10BIT, \
		.base = 0x1a00, .len = 0x100,}, \
	.format_list = plane_formats_yuv, \
	.num_formats = ARRAY_SIZE(plane_formats_yuv), \
	.virt_format_list = plane_formats, \
	.virt_num_formats = ARRAY_SIZE(plane_formats), \
	.rotation_cfg = rot_cfg, \
	}

#define _DMA_SBLK(num, sdma_pri) \
	{ \
	.maxdwnscale = SSPP_UNITY_SCALE, \
	.maxupscale = SSPP_UNITY_SCALE, \
	.smart_dma_priority = sdma_pri, \
	.src_blk = {.name = STRCAT("sspp_src_", num), \
		.id = DPU_SSPP_SRC, .base = 0x00, .len = 0x150,}, \
	.format_list = plane_formats, \
	.num_formats = ARRAY_SIZE(plane_formats), \
	.virt_format_list = plane_formats, \
	.virt_num_formats = ARRAY_SIZE(plane_formats), \
	}

static const struct dpu_sspp_sub_blks msm8998_vig_sblk_0 =
				_VIG_SBLK("0", 0, DPU_SSPP_SCALER_QSEED3);
static const struct dpu_sspp_sub_blks msm8998_vig_sblk_1 =
				_VIG_SBLK("1", 0, DPU_SSPP_SCALER_QSEED3);
static const struct dpu_sspp_sub_blks msm8998_vig_sblk_2 =
				_VIG_SBLK("2", 0, DPU_SSPP_SCALER_QSEED3);
static const struct dpu_sspp_sub_blks msm8998_vig_sblk_3 =
				_VIG_SBLK("3", 0, DPU_SSPP_SCALER_QSEED3);

static const struct dpu_rotation_cfg dpu_rot_sc7280_cfg_v2 = {
	.rot_maxheight = 1088,
	.rot_num_formats = ARRAY_SIZE(rotation_v2_formats),
	.rot_format_list = rotation_v2_formats,
};

static const struct dpu_sspp_sub_blks sdm845_vig_sblk_0 =
				_VIG_SBLK("0", 5, DPU_SSPP_SCALER_QSEED3);
static const struct dpu_sspp_sub_blks sdm845_vig_sblk_1 =
				_VIG_SBLK("1", 6, DPU_SSPP_SCALER_QSEED3);
static const struct dpu_sspp_sub_blks sdm845_vig_sblk_2 =
				_VIG_SBLK("2", 7, DPU_SSPP_SCALER_QSEED3);
static const struct dpu_sspp_sub_blks sdm845_vig_sblk_3 =
				_VIG_SBLK("3", 8, DPU_SSPP_SCALER_QSEED3);

static const struct dpu_sspp_sub_blks sdm845_dma_sblk_0 = _DMA_SBLK("8", 1);
static const struct dpu_sspp_sub_blks sdm845_dma_sblk_1 = _DMA_SBLK("9", 2);
static const struct dpu_sspp_sub_blks sdm845_dma_sblk_2 = _DMA_SBLK("10", 3);
static const struct dpu_sspp_sub_blks sdm845_dma_sblk_3 = _DMA_SBLK("11", 4);

#define SSPP_BLK(_name, _id, _base, _features, \
		_sblk, _xinid, _type, _clkctrl) \
	{ \
	.name = _name, .id = _id, \
	.base = _base, .len = 0x1c8, \
	.features = _features, \
	.sblk = &_sblk, \
	.xin_id = _xinid, \
	.type = _type, \
	.clk_ctrl = _clkctrl \
	}

static const struct dpu_sspp_cfg msm8998_sspp[] = {
	SSPP_BLK("sspp_0", SSPP_VIG0, 0x4000, VIG_MSM8998_MASK,
		msm8998_vig_sblk_0, 0,  SSPP_TYPE_VIG, DPU_CLK_CTRL_VIG0),
	SSPP_BLK("sspp_1", SSPP_VIG1, 0x6000, VIG_MSM8998_MASK,
		msm8998_vig_sblk_1, 4,  SSPP_TYPE_VIG, DPU_CLK_CTRL_VIG1),
	SSPP_BLK("sspp_2", SSPP_VIG2, 0x8000, VIG_MSM8998_MASK,
		msm8998_vig_sblk_2, 8, SSPP_TYPE_VIG, DPU_CLK_CTRL_VIG2),
	SSPP_BLK("sspp_3", SSPP_VIG3, 0xa000, VIG_MSM8998_MASK,
		msm8998_vig_sblk_3, 12,  SSPP_TYPE_VIG, DPU_CLK_CTRL_VIG3),
	SSPP_BLK("sspp_8", SSPP_DMA0, 0x24000,  DMA_MSM8998_MASK,
		sdm845_dma_sblk_0, 1, SSPP_TYPE_DMA, DPU_CLK_CTRL_DMA0),
	SSPP_BLK("sspp_9", SSPP_DMA1, 0x26000,  DMA_MSM8998_MASK,
		sdm845_dma_sblk_1, 5, SSPP_TYPE_DMA, DPU_CLK_CTRL_DMA1),
	SSPP_BLK("sspp_10", SSPP_DMA2, 0x28000,  DMA_CURSOR_MSM8998_MASK,
		sdm845_dma_sblk_2, 9, SSPP_TYPE_DMA, DPU_CLK_CTRL_DMA2),
	SSPP_BLK("sspp_11", SSPP_DMA3, 0x2a000,  DMA_CURSOR_MSM8998_MASK,
		sdm845_dma_sblk_3, 13, SSPP_TYPE_DMA, DPU_CLK_CTRL_DMA3),
};

static const struct dpu_sspp_cfg sdm845_sspp[] = {
	SSPP_BLK("sspp_0", SSPP_VIG0, 0x4000, VIG_SDM845_MASK,
		sdm845_vig_sblk_0, 0,  SSPP_TYPE_VIG, DPU_CLK_CTRL_VIG0),
	SSPP_BLK("sspp_1", SSPP_VIG1, 0x6000, VIG_SDM845_MASK,
		sdm845_vig_sblk_1, 4,  SSPP_TYPE_VIG, DPU_CLK_CTRL_VIG1),
	SSPP_BLK("sspp_2", SSPP_VIG2, 0x8000, VIG_SDM845_MASK,
		sdm845_vig_sblk_2, 8, SSPP_TYPE_VIG, DPU_CLK_CTRL_VIG2),
	SSPP_BLK("sspp_3", SSPP_VIG3, 0xa000, VIG_SDM845_MASK,
		sdm845_vig_sblk_3, 12,  SSPP_TYPE_VIG, DPU_CLK_CTRL_VIG3),
	SSPP_BLK("sspp_8", SSPP_DMA0, 0x24000,  DMA_SDM845_MASK,
		sdm845_dma_sblk_0, 1, SSPP_TYPE_DMA, DPU_CLK_CTRL_DMA0),
	SSPP_BLK("sspp_9", SSPP_DMA1, 0x26000,  DMA_SDM845_MASK,
		sdm845_dma_sblk_1, 5, SSPP_TYPE_DMA, DPU_CLK_CTRL_DMA1),
	SSPP_BLK("sspp_10", SSPP_DMA2, 0x28000,  DMA_CURSOR_SDM845_MASK,
		sdm845_dma_sblk_2, 9, SSPP_TYPE_DMA, DPU_CLK_CTRL_CURSOR0),
	SSPP_BLK("sspp_11", SSPP_DMA3, 0x2a000,  DMA_CURSOR_SDM845_MASK,
		sdm845_dma_sblk_3, 13, SSPP_TYPE_DMA, DPU_CLK_CTRL_CURSOR1),
};

static const struct dpu_sspp_sub_blks sc7180_vig_sblk_0 =
				_VIG_SBLK("0", 4, DPU_SSPP_SCALER_QSEED4);

static const struct dpu_sspp_sub_blks sc7280_vig_sblk_0 =
			_VIG_SBLK_ROT("0", 4, DPU_SSPP_SCALER_QSEED4, &dpu_rot_sc7280_cfg_v2);

static const struct dpu_sspp_cfg sc7180_sspp[] = {
	SSPP_BLK("sspp_0", SSPP_VIG0, 0x4000, VIG_SC7180_MASK,
		sc7180_vig_sblk_0, 0,  SSPP_TYPE_VIG, DPU_CLK_CTRL_VIG0),
	SSPP_BLK("sspp_8", SSPP_DMA0, 0x24000,  DMA_SDM845_MASK,
		sdm845_dma_sblk_0, 1, SSPP_TYPE_DMA, DPU_CLK_CTRL_DMA0),
	SSPP_BLK("sspp_9", SSPP_DMA1, 0x26000,  DMA_CURSOR_SDM845_MASK,
		sdm845_dma_sblk_1, 5, SSPP_TYPE_DMA, DPU_CLK_CTRL_CURSOR0),
	SSPP_BLK("sspp_10", SSPP_DMA2, 0x28000,  DMA_CURSOR_SDM845_MASK,
		sdm845_dma_sblk_2, 9, SSPP_TYPE_DMA, DPU_CLK_CTRL_CURSOR1),
};

static const struct dpu_sspp_sub_blks sm6115_vig_sblk_0 =
				_VIG_SBLK("0", 2, DPU_SSPP_SCALER_QSEED4);

static const struct dpu_sspp_cfg sm6115_sspp[] = {
	SSPP_BLK("sspp_0", SSPP_VIG0, 0x4000, VIG_SC7180_MASK,
		sm6115_vig_sblk_0, 0, SSPP_TYPE_VIG, DPU_CLK_CTRL_VIG0),
	SSPP_BLK("sspp_8", SSPP_DMA0, 0x24000,  DMA_SDM845_MASK,
		sdm845_dma_sblk_0, 1, SSPP_TYPE_DMA, DPU_CLK_CTRL_DMA0),
};

static const struct dpu_sspp_sub_blks sm8250_vig_sblk_0 =
				_VIG_SBLK("0", 5, DPU_SSPP_SCALER_QSEED4);
static const struct dpu_sspp_sub_blks sm8250_vig_sblk_1 =
				_VIG_SBLK("1", 6, DPU_SSPP_SCALER_QSEED4);
static const struct dpu_sspp_sub_blks sm8250_vig_sblk_2 =
				_VIG_SBLK("2", 7, DPU_SSPP_SCALER_QSEED4);
static const struct dpu_sspp_sub_blks sm8250_vig_sblk_3 =
				_VIG_SBLK("3", 8, DPU_SSPP_SCALER_QSEED4);

static const struct dpu_sspp_cfg sm8250_sspp[] = {
	SSPP_BLK("sspp_0", SSPP_VIG0, 0x4000, VIG_SC7180_MASK,
		sm8250_vig_sblk_0, 0,  SSPP_TYPE_VIG, DPU_CLK_CTRL_VIG0),
	SSPP_BLK("sspp_1", SSPP_VIG1, 0x6000, VIG_SC7180_MASK,
		sm8250_vig_sblk_1, 4,  SSPP_TYPE_VIG, DPU_CLK_CTRL_VIG1),
	SSPP_BLK("sspp_2", SSPP_VIG2, 0x8000, VIG_SC7180_MASK,
		sm8250_vig_sblk_2, 8, SSPP_TYPE_VIG, DPU_CLK_CTRL_VIG2),
	SSPP_BLK("sspp_3", SSPP_VIG3, 0xa000, VIG_SC7180_MASK,
		sm8250_vig_sblk_3, 12,  SSPP_TYPE_VIG, DPU_CLK_CTRL_VIG3),
	SSPP_BLK("sspp_8", SSPP_DMA0, 0x24000,  DMA_SDM845_MASK,
		sdm845_dma_sblk_0, 1, SSPP_TYPE_DMA, DPU_CLK_CTRL_DMA0),
	SSPP_BLK("sspp_9", SSPP_DMA1, 0x26000,  DMA_SDM845_MASK,
		sdm845_dma_sblk_1, 5, SSPP_TYPE_DMA, DPU_CLK_CTRL_DMA1),
	SSPP_BLK("sspp_10", SSPP_DMA2, 0x28000,  DMA_CURSOR_SDM845_MASK,
		sdm845_dma_sblk_2, 9, SSPP_TYPE_DMA, DPU_CLK_CTRL_CURSOR0),
	SSPP_BLK("sspp_11", SSPP_DMA3, 0x2a000,  DMA_CURSOR_SDM845_MASK,
		sdm845_dma_sblk_3, 13, SSPP_TYPE_DMA, DPU_CLK_CTRL_CURSOR1),
};

static const struct dpu_sspp_sub_blks sm8450_vig_sblk_0 =
				_VIG_SBLK("0", 5, DPU_SSPP_SCALER_QSEED3LITE);
static const struct dpu_sspp_sub_blks sm8450_vig_sblk_1 =
				_VIG_SBLK("1", 6, DPU_SSPP_SCALER_QSEED3LITE);
static const struct dpu_sspp_sub_blks sm8450_vig_sblk_2 =
				_VIG_SBLK("2", 7, DPU_SSPP_SCALER_QSEED3LITE);
static const struct dpu_sspp_sub_blks sm8450_vig_sblk_3 =
				_VIG_SBLK("3", 8, DPU_SSPP_SCALER_QSEED3LITE);

static const struct dpu_sspp_cfg sm8450_sspp[] = {
	SSPP_BLK("sspp_0", SSPP_VIG0, 0x4000, VIG_SC7180_MASK,
		sm8450_vig_sblk_0, 0,  SSPP_TYPE_VIG, DPU_CLK_CTRL_VIG0),
	SSPP_BLK("sspp_1", SSPP_VIG1, 0x6000, VIG_SC7180_MASK,
		sm8450_vig_sblk_1, 4,  SSPP_TYPE_VIG, DPU_CLK_CTRL_VIG1),
	SSPP_BLK("sspp_2", SSPP_VIG2, 0x8000, VIG_SC7180_MASK,
		sm8450_vig_sblk_2, 8, SSPP_TYPE_VIG, DPU_CLK_CTRL_VIG2),
	SSPP_BLK("sspp_3", SSPP_VIG3, 0xa000, VIG_SC7180_MASK,
		sm8450_vig_sblk_3, 12,  SSPP_TYPE_VIG, DPU_CLK_CTRL_VIG3),
	SSPP_BLK("sspp_8", SSPP_DMA0, 0x24000,  DMA_SDM845_MASK,
		sdm845_dma_sblk_0, 1, SSPP_TYPE_DMA, DPU_CLK_CTRL_DMA0),
	SSPP_BLK("sspp_9", SSPP_DMA1, 0x26000,  DMA_SDM845_MASK,
		sdm845_dma_sblk_1, 5, SSPP_TYPE_DMA, DPU_CLK_CTRL_DMA1),
	SSPP_BLK("sspp_10", SSPP_DMA2, 0x28000,  DMA_CURSOR_SDM845_MASK,
		sdm845_dma_sblk_2, 9, SSPP_TYPE_DMA, DPU_CLK_CTRL_CURSOR0),
	SSPP_BLK("sspp_11", SSPP_DMA3, 0x2a000,  DMA_CURSOR_SDM845_MASK,
		sdm845_dma_sblk_3, 13, SSPP_TYPE_DMA, DPU_CLK_CTRL_CURSOR1),
};

static const struct dpu_sspp_sub_blks sm8550_vig_sblk_0 =
				_VIG_SBLK("0", 7, DPU_SSPP_SCALER_QSEED3LITE);
static const struct dpu_sspp_sub_blks sm8550_vig_sblk_1 =
				_VIG_SBLK("1", 8, DPU_SSPP_SCALER_QSEED3LITE);
static const struct dpu_sspp_sub_blks sm8550_vig_sblk_2 =
				_VIG_SBLK("2", 9, DPU_SSPP_SCALER_QSEED3LITE);
static const struct dpu_sspp_sub_blks sm8550_vig_sblk_3 =
				_VIG_SBLK("3", 10, DPU_SSPP_SCALER_QSEED3LITE);
static const struct dpu_sspp_sub_blks sm8550_dma_sblk_4 = _DMA_SBLK("12", 5);
static const struct dpu_sspp_sub_blks sd8550_dma_sblk_5 = _DMA_SBLK("13", 6);

static const struct dpu_sspp_cfg sm8550_sspp[] = {
	SSPP_BLK("sspp_0", SSPP_VIG0, 0x4000, VIG_SC7180_MASK,
		sm8550_vig_sblk_0, 0,  SSPP_TYPE_VIG, DPU_CLK_CTRL_VIG0),
	SSPP_BLK("sspp_1", SSPP_VIG1, 0x6000, VIG_SC7180_MASK,
		sm8550_vig_sblk_1, 4,  SSPP_TYPE_VIG, DPU_CLK_CTRL_VIG1),
	SSPP_BLK("sspp_2", SSPP_VIG2, 0x8000, VIG_SC7180_MASK,
		sm8550_vig_sblk_2, 8, SSPP_TYPE_VIG, DPU_CLK_CTRL_VIG2),
	SSPP_BLK("sspp_3", SSPP_VIG3, 0xa000, VIG_SC7180_MASK,
		sm8550_vig_sblk_3, 12,  SSPP_TYPE_VIG, DPU_CLK_CTRL_VIG3),
	SSPP_BLK("sspp_8", SSPP_DMA0, 0x24000,  DMA_SDM845_MASK,
		sdm845_dma_sblk_0, 1, SSPP_TYPE_DMA, DPU_CLK_CTRL_DMA0),
	SSPP_BLK("sspp_9", SSPP_DMA1, 0x26000,  DMA_SDM845_MASK,
		sdm845_dma_sblk_1, 5, SSPP_TYPE_DMA, DPU_CLK_CTRL_DMA1),
	SSPP_BLK("sspp_10", SSPP_DMA2, 0x28000,  DMA_SDM845_MASK,
		sdm845_dma_sblk_2, 9, SSPP_TYPE_DMA, DPU_CLK_CTRL_DMA2),
	SSPP_BLK("sspp_11", SSPP_DMA3, 0x2a000,  DMA_SDM845_MASK,
		sdm845_dma_sblk_3, 13, SSPP_TYPE_DMA, DPU_CLK_CTRL_DMA3),
	SSPP_BLK("sspp_12", SSPP_DMA4, 0x2c000,  DMA_CURSOR_SDM845_MASK,
		sm8550_dma_sblk_4, 14, SSPP_TYPE_DMA, DPU_CLK_CTRL_CURSOR0),
	SSPP_BLK("sspp_13", SSPP_DMA5, 0x2e000,  DMA_CURSOR_SDM845_MASK,
		sd8550_dma_sblk_5, 15, SSPP_TYPE_DMA, DPU_CLK_CTRL_CURSOR1),
};

static const struct dpu_sspp_cfg sc7280_sspp[] = {
	SSPP_BLK("sspp_0", SSPP_VIG0, 0x4000, VIG_SC7280_MASK,
		sc7280_vig_sblk_0, 0,  SSPP_TYPE_VIG, DPU_CLK_CTRL_VIG0),
	SSPP_BLK("sspp_8", SSPP_DMA0, 0x24000,  DMA_SDM845_MASK,
		sdm845_dma_sblk_0, 1, SSPP_TYPE_DMA, DPU_CLK_CTRL_DMA0),
	SSPP_BLK("sspp_9", SSPP_DMA1, 0x26000,  DMA_CURSOR_SDM845_MASK,
		sdm845_dma_sblk_1, 5, SSPP_TYPE_DMA, DPU_CLK_CTRL_CURSOR0),
	SSPP_BLK("sspp_10", SSPP_DMA2, 0x28000,  DMA_CURSOR_SDM845_MASK,
		sdm845_dma_sblk_2, 9, SSPP_TYPE_DMA, DPU_CLK_CTRL_CURSOR1),
};

static const struct dpu_sspp_sub_blks sc8280xp_vig_sblk_0 =
				_VIG_SBLK("0", 5, DPU_SSPP_SCALER_QSEED3LITE);
static const struct dpu_sspp_sub_blks sc8280xp_vig_sblk_1 =
				_VIG_SBLK("1", 6, DPU_SSPP_SCALER_QSEED3LITE);
static const struct dpu_sspp_sub_blks sc8280xp_vig_sblk_2 =
				_VIG_SBLK("2", 7, DPU_SSPP_SCALER_QSEED3LITE);
static const struct dpu_sspp_sub_blks sc8280xp_vig_sblk_3 =
				_VIG_SBLK("3", 8, DPU_SSPP_SCALER_QSEED3LITE);

static const struct dpu_sspp_cfg sc8280xp_sspp[] = {
	SSPP_BLK("sspp_0", SSPP_VIG0, 0x4000, VIG_SM8250_MASK,
		 sc8280xp_vig_sblk_0, 0,  SSPP_TYPE_VIG, DPU_CLK_CTRL_VIG0),
	SSPP_BLK("sspp_1", SSPP_VIG1, 0x6000, VIG_SM8250_MASK,
		 sc8280xp_vig_sblk_1, 4,  SSPP_TYPE_VIG, DPU_CLK_CTRL_VIG1),
	SSPP_BLK("sspp_2", SSPP_VIG2, 0x8000, VIG_SM8250_MASK,
		 sc8280xp_vig_sblk_2, 8, SSPP_TYPE_VIG, DPU_CLK_CTRL_VIG2),
	SSPP_BLK("sspp_3", SSPP_VIG3, 0xa000, VIG_SM8250_MASK,
		 sc8280xp_vig_sblk_3, 12,  SSPP_TYPE_VIG, DPU_CLK_CTRL_VIG3),
	SSPP_BLK("sspp_8", SSPP_DMA0, 0x24000, DMA_SDM845_MASK,
		 sdm845_dma_sblk_0, 1, SSPP_TYPE_DMA, DPU_CLK_CTRL_DMA0),
	SSPP_BLK("sspp_9", SSPP_DMA1, 0x26000, DMA_SDM845_MASK,
		 sdm845_dma_sblk_1, 5, SSPP_TYPE_DMA, DPU_CLK_CTRL_DMA1),
	SSPP_BLK("sspp_10", SSPP_DMA2, 0x28000, DMA_CURSOR_SDM845_MASK,
		 sdm845_dma_sblk_2, 9, SSPP_TYPE_DMA, DPU_CLK_CTRL_CURSOR0),
	SSPP_BLK("sspp_11", SSPP_DMA3, 0x2a000, DMA_CURSOR_SDM845_MASK,
		 sdm845_dma_sblk_3, 13, SSPP_TYPE_DMA, DPU_CLK_CTRL_CURSOR1),
};

#define _VIG_SBLK_NOSCALE(num, sdma_pri) \
	{ \
	.maxdwnscale = SSPP_UNITY_SCALE, \
	.maxupscale = SSPP_UNITY_SCALE, \
	.smart_dma_priority = sdma_pri, \
	.src_blk = {.name = STRCAT("sspp_src_", num), \
		.id = DPU_SSPP_SRC, .base = 0x00, .len = 0x150,}, \
	.format_list = plane_formats_yuv, \
	.num_formats = ARRAY_SIZE(plane_formats_yuv), \
	.virt_format_list = plane_formats, \
	.virt_num_formats = ARRAY_SIZE(plane_formats), \
	}

static const struct dpu_sspp_sub_blks qcm2290_vig_sblk_0 = _VIG_SBLK_NOSCALE("0", 2);
static const struct dpu_sspp_sub_blks qcm2290_dma_sblk_0 = _DMA_SBLK("8", 1);

static const struct dpu_sspp_cfg qcm2290_sspp[] = {
	SSPP_BLK("sspp_0", SSPP_VIG0, 0x4000, VIG_QCM2290_MASK,
		 qcm2290_vig_sblk_0, 0, SSPP_TYPE_VIG, DPU_CLK_CTRL_VIG0),
	SSPP_BLK("sspp_8", SSPP_DMA0, 0x24000,  DMA_SDM845_MASK,
		 qcm2290_dma_sblk_0, 1, SSPP_TYPE_DMA, DPU_CLK_CTRL_DMA0),
};

/*************************************************************
 * MIXER sub blocks config
 *************************************************************/

#define LM_BLK(_name, _id, _base, _fmask, _sblk, _pp, _lmpair, _dspp) \
	{ \
	.name = _name, .id = _id, \
	.base = _base, .len = 0x320, \
	.features = _fmask, \
	.sblk = _sblk, \
	.pingpong = _pp, \
	.lm_pair_mask = (1 << _lmpair), \
	.dspp = _dspp \
	}

/* MSM8998 */

static const struct dpu_lm_sub_blks msm8998_lm_sblk = {
	.maxwidth = DEFAULT_DPU_OUTPUT_LINE_WIDTH,
	.maxblendstages = 7, /* excluding base layer */
	.blendstage_base = { /* offsets relative to mixer base */
		0x20, 0x50, 0x80, 0xb0, 0x230,
		0x260, 0x290
	},
};

static const struct dpu_lm_cfg msm8998_lm[] = {
	LM_BLK("lm_0", LM_0, 0x44000, MIXER_MSM8998_MASK,
		&msm8998_lm_sblk, PINGPONG_0, LM_2, DSPP_0),
	LM_BLK("lm_1", LM_1, 0x45000, MIXER_MSM8998_MASK,
		&msm8998_lm_sblk, PINGPONG_1, LM_5, DSPP_1),
	LM_BLK("lm_2", LM_2, 0x46000, MIXER_MSM8998_MASK,
		&msm8998_lm_sblk, PINGPONG_2, LM_0, 0),
	LM_BLK("lm_3", LM_3, 0x47000, MIXER_MSM8998_MASK,
		&msm8998_lm_sblk, PINGPONG_MAX, 0, 0),
	LM_BLK("lm_4", LM_4, 0x48000, MIXER_MSM8998_MASK,
		&msm8998_lm_sblk, PINGPONG_MAX, 0, 0),
	LM_BLK("lm_5", LM_5, 0x49000, MIXER_MSM8998_MASK,
		&msm8998_lm_sblk, PINGPONG_3, LM_1, 0),
};

/* SDM845 */

static const struct dpu_lm_sub_blks sdm845_lm_sblk = {
	.maxwidth = DEFAULT_DPU_OUTPUT_LINE_WIDTH,
	.maxblendstages = 11, /* excluding base layer */
	.blendstage_base = { /* offsets relative to mixer base */
		0x20, 0x38, 0x50, 0x68, 0x80, 0x98,
		0xb0, 0xc8, 0xe0, 0xf8, 0x110
	},
};

static const struct dpu_lm_cfg sdm845_lm[] = {
	LM_BLK("lm_0", LM_0, 0x44000, MIXER_SDM845_MASK,
		&sdm845_lm_sblk, PINGPONG_0, LM_1, 0),
	LM_BLK("lm_1", LM_1, 0x45000, MIXER_SDM845_MASK,
		&sdm845_lm_sblk, PINGPONG_1, LM_0, 0),
	LM_BLK("lm_2", LM_2, 0x46000, MIXER_SDM845_MASK,
		&sdm845_lm_sblk, PINGPONG_2, LM_5, 0),
	LM_BLK("lm_3", LM_3, 0x0, MIXER_SDM845_MASK,
		&sdm845_lm_sblk, PINGPONG_MAX, 0, 0),
	LM_BLK("lm_4", LM_4, 0x0, MIXER_SDM845_MASK,
		&sdm845_lm_sblk, PINGPONG_MAX, 0, 0),
	LM_BLK("lm_5", LM_5, 0x49000, MIXER_SDM845_MASK,
		&sdm845_lm_sblk, PINGPONG_3, LM_2, 0),
};

/* SC7180 */

static const struct dpu_lm_sub_blks sc7180_lm_sblk = {
	.maxwidth = DEFAULT_DPU_OUTPUT_LINE_WIDTH,
	.maxblendstages = 7, /* excluding base layer */
	.blendstage_base = { /* offsets relative to mixer base */
		0x20, 0x38, 0x50, 0x68, 0x80, 0x98, 0xb0
	},
};

static const struct dpu_lm_cfg sc7180_lm[] = {
	LM_BLK("lm_0", LM_0, 0x44000, MIXER_SDM845_MASK,
		&sc7180_lm_sblk, PINGPONG_0, LM_1, DSPP_0),
	LM_BLK("lm_1", LM_1, 0x45000, MIXER_SDM845_MASK,
		&sc7180_lm_sblk, PINGPONG_1, LM_0, 0),
};

/* SC8280XP */

static const struct dpu_lm_cfg sc8280xp_lm[] = {
	LM_BLK("lm_0", LM_0, 0x44000, MIXER_SDM845_MASK, &sdm845_lm_sblk, PINGPONG_0, LM_1, DSPP_0),
	LM_BLK("lm_1", LM_1, 0x45000, MIXER_SDM845_MASK, &sdm845_lm_sblk, PINGPONG_1, LM_0, DSPP_1),
	LM_BLK("lm_2", LM_2, 0x46000, MIXER_SDM845_MASK, &sdm845_lm_sblk, PINGPONG_2, LM_3, DSPP_2),
	LM_BLK("lm_3", LM_3, 0x47000, MIXER_SDM845_MASK, &sdm845_lm_sblk, PINGPONG_3, LM_2, DSPP_3),
	LM_BLK("lm_4", LM_4, 0x48000, MIXER_SDM845_MASK, &sdm845_lm_sblk, PINGPONG_4, LM_5, 0),
	LM_BLK("lm_5", LM_5, 0x49000, MIXER_SDM845_MASK, &sdm845_lm_sblk, PINGPONG_5, LM_4, 0),
};

/* SM8150 */

static const struct dpu_lm_cfg sm8150_lm[] = {
	LM_BLK("lm_0", LM_0, 0x44000, MIXER_SDM845_MASK,
		&sdm845_lm_sblk, PINGPONG_0, LM_1, DSPP_0),
	LM_BLK("lm_1", LM_1, 0x45000, MIXER_SDM845_MASK,
		&sdm845_lm_sblk, PINGPONG_1, LM_0, DSPP_1),
	LM_BLK("lm_2", LM_2, 0x46000, MIXER_SDM845_MASK,
		&sdm845_lm_sblk, PINGPONG_2, LM_3, 0),
	LM_BLK("lm_3", LM_3, 0x47000, MIXER_SDM845_MASK,
		&sdm845_lm_sblk, PINGPONG_3, LM_2, 0),
	LM_BLK("lm_4", LM_4, 0x48000, MIXER_SDM845_MASK,
		&sdm845_lm_sblk, PINGPONG_4, LM_5, 0),
	LM_BLK("lm_5", LM_5, 0x49000, MIXER_SDM845_MASK,
		&sdm845_lm_sblk, PINGPONG_5, LM_4, 0),
};

static const struct dpu_lm_cfg sc7280_lm[] = {
	LM_BLK("lm_0", LM_0, 0x44000, MIXER_SDM845_MASK,
		&sc7180_lm_sblk, PINGPONG_0, 0, DSPP_0),
	LM_BLK("lm_2", LM_2, 0x46000, MIXER_SDM845_MASK,
		&sc7180_lm_sblk, PINGPONG_2, LM_3, 0),
	LM_BLK("lm_3", LM_3, 0x47000, MIXER_SDM845_MASK,
		&sc7180_lm_sblk, PINGPONG_3, LM_2, 0),
};

/* QCM2290 */

static const struct dpu_lm_sub_blks qcm2290_lm_sblk = {
	.maxwidth = DEFAULT_DPU_LINE_WIDTH,
	.maxblendstages = 4, /* excluding base layer */
	.blendstage_base = { /* offsets relative to mixer base */
		0x20, 0x38, 0x50, 0x68
	},
};

static const struct dpu_lm_cfg qcm2290_lm[] = {
	LM_BLK("lm_0", LM_0, 0x44000, MIXER_QCM2290_MASK,
		&qcm2290_lm_sblk, PINGPONG_0, 0, DSPP_0),
};

/*************************************************************
 * DSPP sub blocks config
 *************************************************************/
static const struct dpu_dspp_sub_blks msm8998_dspp_sblk = {
	.pcc = {.id = DPU_DSPP_PCC, .base = 0x1700,
		.len = 0x90, .version = 0x10007},
	.gc = { .id = DPU_DSPP_GC, .base = 0x17c0,
		.len = 0x90, .version = 0x10007},
};

static const struct dpu_dspp_sub_blks sc7180_dspp_sblk = {
	.pcc = {.id = DPU_DSPP_PCC, .base = 0x1700,
		.len = 0x90, .version = 0x10000},
};

static const struct dpu_dspp_sub_blks sm8150_dspp_sblk = {
	.pcc = {.id = DPU_DSPP_PCC, .base = 0x1700,
		.len = 0x90, .version = 0x40000},
};

#define DSPP_BLK(_name, _id, _base, _mask, _sblk) \
		{\
		.name = _name, .id = _id, \
		.base = _base, .len = 0x1800, \
		.features = _mask, \
		.sblk = _sblk \
		}

static const struct dpu_dspp_cfg msm8998_dspp[] = {
	DSPP_BLK("dspp_0", DSPP_0, 0x54000, DSPP_MSM8998_MASK,
		 &msm8998_dspp_sblk),
	DSPP_BLK("dspp_1", DSPP_1, 0x56000, DSPP_MSM8998_MASK,
		 &msm8998_dspp_sblk),
};

static const struct dpu_dspp_cfg sc7180_dspp[] = {
	DSPP_BLK("dspp_0", DSPP_0, 0x54000, DSPP_SC7180_MASK,
		 &sc7180_dspp_sblk),
};

static const struct dpu_dspp_cfg sm8150_dspp[] = {
	DSPP_BLK("dspp_0", DSPP_0, 0x54000, DSPP_SC7180_MASK,
		 &sm8150_dspp_sblk),
	DSPP_BLK("dspp_1", DSPP_1, 0x56000, DSPP_SC7180_MASK,
		 &sm8150_dspp_sblk),
	DSPP_BLK("dspp_2", DSPP_2, 0x58000, DSPP_SC7180_MASK,
		 &sm8150_dspp_sblk),
	DSPP_BLK("dspp_3", DSPP_3, 0x5a000, DSPP_SC7180_MASK,
		 &sm8150_dspp_sblk),
};

static const struct dpu_dspp_cfg qcm2290_dspp[] = {
	DSPP_BLK("dspp_0", DSPP_0, 0x54000, DSPP_SC7180_MASK,
		 &sm8150_dspp_sblk),
};

/*************************************************************
 * PINGPONG sub blocks config
 *************************************************************/
static const struct dpu_pingpong_sub_blks sdm845_pp_sblk_te = {
	.te2 = {.id = DPU_PINGPONG_TE2, .base = 0x2000, .len = 0x0,
		.version = 0x1},
	.dither = {.id = DPU_PINGPONG_DITHER, .base = 0x30e0,
		.len = 0x20, .version = 0x10000},
};

static const struct dpu_pingpong_sub_blks sdm845_pp_sblk = {
	.dither = {.id = DPU_PINGPONG_DITHER, .base = 0x30e0,
		.len = 0x20, .version = 0x10000},
};

static const struct dpu_pingpong_sub_blks sc7280_pp_sblk = {
	.dither = {.id = DPU_PINGPONG_DITHER, .base = 0xe0,
	.len = 0x20, .version = 0x20000},
};

#define PP_BLK_DIPHER(_name, _id, _base, _merge_3d, _sblk, _done, _rdptr) \
	{\
	.name = _name, .id = _id, \
	.base = _base, .len = 0, \
	.features = BIT(DPU_PINGPONG_DITHER), \
	.merge_3d = _merge_3d, \
	.sblk = &_sblk, \
	.intr_done = _done, \
	.intr_rdptr = _rdptr, \
	}
#define PP_BLK_TE(_name, _id, _base, _merge_3d, _sblk, _done, _rdptr) \
	{\
	.name = _name, .id = _id, \
	.base = _base, .len = 0xd4, \
	.features = PINGPONG_SDM845_SPLIT_MASK, \
	.merge_3d = _merge_3d, \
	.sblk = &_sblk, \
	.intr_done = _done, \
	.intr_rdptr = _rdptr, \
	}
#define PP_BLK(_name, _id, _base, _merge_3d, _sblk, _done, _rdptr) \
	{\
	.name = _name, .id = _id, \
	.base = _base, .len = 0xd4, \
	.features = PINGPONG_SDM845_MASK, \
	.merge_3d = _merge_3d, \
	.sblk = &_sblk, \
	.intr_done = _done, \
	.intr_rdptr = _rdptr, \
	}

static const struct dpu_pingpong_cfg sdm845_pp[] = {
	PP_BLK_TE("pingpong_0", PINGPONG_0, 0x70000, 0, sdm845_pp_sblk_te,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 8),
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 12)),
	PP_BLK_TE("pingpong_1", PINGPONG_1, 0x70800, 0, sdm845_pp_sblk_te,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 9),
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 13)),
	PP_BLK("pingpong_2", PINGPONG_2, 0x71000, 0, sdm845_pp_sblk,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 10),
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 14)),
	PP_BLK("pingpong_3", PINGPONG_3, 0x71800, 0, sdm845_pp_sblk,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 11),
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 15)),
};

static struct dpu_pingpong_cfg sc7180_pp[] = {
	PP_BLK_TE("pingpong_0", PINGPONG_0, 0x70000, 0, sdm845_pp_sblk_te, -1, -1),
	PP_BLK_TE("pingpong_1", PINGPONG_1, 0x70800, 0, sdm845_pp_sblk_te, -1, -1),
};

static struct dpu_pingpong_cfg sc8280xp_pp[] = {
	PP_BLK_TE("pingpong_0", PINGPONG_0, 0x69000, MERGE_3D_0, sdm845_pp_sblk_te,
		  DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 8), -1),
	PP_BLK_TE("pingpong_1", PINGPONG_1, 0x6a000, MERGE_3D_0, sdm845_pp_sblk_te,
		  DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 9), -1),
	PP_BLK_TE("pingpong_2", PINGPONG_2, 0x6b000, MERGE_3D_1, sdm845_pp_sblk_te,
		  DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 10), -1),
	PP_BLK_TE("pingpong_3", PINGPONG_3, 0x6c000, MERGE_3D_1, sdm845_pp_sblk_te,
		  DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 11), -1),
	PP_BLK_TE("pingpong_4", PINGPONG_4, 0x6d000, MERGE_3D_2, sdm845_pp_sblk_te,
		  DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 30), -1),
	PP_BLK_TE("pingpong_5", PINGPONG_5, 0x6e000, MERGE_3D_2, sdm845_pp_sblk_te,
		  DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 31), -1),
};

static const struct dpu_pingpong_cfg sm8150_pp[] = {
	PP_BLK_TE("pingpong_0", PINGPONG_0, 0x70000, MERGE_3D_0, sdm845_pp_sblk_te,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 8),
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 12)),
	PP_BLK_TE("pingpong_1", PINGPONG_1, 0x70800, MERGE_3D_0, sdm845_pp_sblk_te,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 9),
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 13)),
	PP_BLK("pingpong_2", PINGPONG_2, 0x71000, MERGE_3D_1, sdm845_pp_sblk,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 10),
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 14)),
	PP_BLK("pingpong_3", PINGPONG_3, 0x71800, MERGE_3D_1, sdm845_pp_sblk,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 11),
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 15)),
	PP_BLK("pingpong_4", PINGPONG_4, 0x72000, MERGE_3D_2, sdm845_pp_sblk,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 30),
			-1),
	PP_BLK("pingpong_5", PINGPONG_5, 0x72800, MERGE_3D_2, sdm845_pp_sblk,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 31),
			-1),
};

static const struct dpu_pingpong_cfg sm8350_pp[] = {
	PP_BLK_TE("pingpong_0", PINGPONG_0, 0x69000, MERGE_3D_0, sdm845_pp_sblk_te,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 8),
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 12)),
	PP_BLK_TE("pingpong_1", PINGPONG_1, 0x6a000, MERGE_3D_0, sdm845_pp_sblk_te,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 9),
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 13)),
	PP_BLK("pingpong_2", PINGPONG_2, 0x6b000, MERGE_3D_1, sdm845_pp_sblk,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 10),
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 14)),
	PP_BLK("pingpong_3", PINGPONG_3, 0x6c000, MERGE_3D_1, sdm845_pp_sblk,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 11),
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 15)),
	PP_BLK("pingpong_4", PINGPONG_4, 0x6d000, MERGE_3D_2, sdm845_pp_sblk,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 30),
			-1),
	PP_BLK("pingpong_5", PINGPONG_5, 0x6e000, MERGE_3D_2, sdm845_pp_sblk,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 31),
			-1),
};

static const struct dpu_pingpong_cfg sc7280_pp[] = {
	PP_BLK("pingpong_0", PINGPONG_0, 0x69000, 0, sc7280_pp_sblk, -1, -1),
	PP_BLK("pingpong_1", PINGPONG_1, 0x6a000, 0, sc7280_pp_sblk, -1, -1),
	PP_BLK("pingpong_2", PINGPONG_2, 0x6b000, 0, sc7280_pp_sblk, -1, -1),
	PP_BLK("pingpong_3", PINGPONG_3, 0x6c000, 0, sc7280_pp_sblk, -1, -1),
};

static struct dpu_pingpong_cfg qcm2290_pp[] = {
	PP_BLK("pingpong_0", PINGPONG_0, 0x70000, 0, sdm845_pp_sblk,
		DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 8),
		DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 12)),
};

/* FIXME: interrupts */
static const struct dpu_pingpong_cfg sm8450_pp[] = {
	PP_BLK_TE("pingpong_0", PINGPONG_0, 0x69000, MERGE_3D_0, sdm845_pp_sblk_te,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 8),
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 12)),
	PP_BLK_TE("pingpong_1", PINGPONG_1, 0x6a000, MERGE_3D_0, sdm845_pp_sblk_te,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 9),
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 13)),
	PP_BLK("pingpong_2", PINGPONG_2, 0x6b000, MERGE_3D_1, sdm845_pp_sblk,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 10),
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 14)),
	PP_BLK("pingpong_3", PINGPONG_3, 0x6c000, MERGE_3D_1, sdm845_pp_sblk,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 11),
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 15)),
	PP_BLK("pingpong_4", PINGPONG_4, 0x6d000, MERGE_3D_2, sdm845_pp_sblk,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 30),
			-1),
	PP_BLK("pingpong_5", PINGPONG_5, 0x6e000, MERGE_3D_2, sdm845_pp_sblk,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 31),
			-1),
	PP_BLK("pingpong_6", PINGPONG_6, 0x65800, MERGE_3D_3, sdm845_pp_sblk,
			-1,
			-1),
	PP_BLK("pingpong_7", PINGPONG_7, 0x65c00, MERGE_3D_3, sdm845_pp_sblk,
			-1,
			-1),
};

static const struct dpu_pingpong_cfg sm8550_pp[] = {
	PP_BLK_DIPHER("pingpong_0", PINGPONG_0, 0x69000, MERGE_3D_0, sc7280_pp_sblk,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 8),
			-1),
	PP_BLK_DIPHER("pingpong_1", PINGPONG_1, 0x6a000, MERGE_3D_0, sc7280_pp_sblk,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 9),
			-1),
	PP_BLK_DIPHER("pingpong_2", PINGPONG_2, 0x6b000, MERGE_3D_1, sc7280_pp_sblk,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 10),
			-1),
	PP_BLK_DIPHER("pingpong_3", PINGPONG_3, 0x6c000, MERGE_3D_1, sc7280_pp_sblk,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 11),
			-1),
	PP_BLK_DIPHER("pingpong_4", PINGPONG_4, 0x6d000, MERGE_3D_2, sc7280_pp_sblk,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 30),
			-1),
	PP_BLK_DIPHER("pingpong_5", PINGPONG_5, 0x6e000, MERGE_3D_2, sc7280_pp_sblk,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 31),
			-1),
	PP_BLK_DIPHER("pingpong_6", PINGPONG_6, 0x66000, MERGE_3D_3, sc7280_pp_sblk,
			-1,
			-1),
	PP_BLK_DIPHER("pingpong_7", PINGPONG_7, 0x66400, MERGE_3D_3, sc7280_pp_sblk,
			-1,
			-1),
};

/*************************************************************
 * MERGE_3D sub blocks config
 *************************************************************/
#define MERGE_3D_BLK(_name, _id, _base) \
	{\
	.name = _name, .id = _id, \
	.base = _base, .len = 0x100, \
	.features = MERGE_3D_SM8150_MASK, \
	.sblk = NULL \
	}

static const struct dpu_merge_3d_cfg sm8150_merge_3d[] = {
	MERGE_3D_BLK("merge_3d_0", MERGE_3D_0, 0x83000),
	MERGE_3D_BLK("merge_3d_1", MERGE_3D_1, 0x83100),
	MERGE_3D_BLK("merge_3d_2", MERGE_3D_2, 0x83200),
};

static const struct dpu_merge_3d_cfg sm8350_merge_3d[] = {
	MERGE_3D_BLK("merge_3d_0", MERGE_3D_0, 0x4e000),
	MERGE_3D_BLK("merge_3d_1", MERGE_3D_1, 0x4f000),
	MERGE_3D_BLK("merge_3d_2", MERGE_3D_2, 0x50000),
};

static const struct dpu_merge_3d_cfg sm8450_merge_3d[] = {
	MERGE_3D_BLK("merge_3d_0", MERGE_3D_0, 0x4e000),
	MERGE_3D_BLK("merge_3d_1", MERGE_3D_1, 0x4f000),
	MERGE_3D_BLK("merge_3d_2", MERGE_3D_2, 0x50000),
	MERGE_3D_BLK("merge_3d_3", MERGE_3D_3, 0x65f00),
};

static const struct dpu_merge_3d_cfg sm8550_merge_3d[] = {
	MERGE_3D_BLK("merge_3d_0", MERGE_3D_0, 0x4e000),
	MERGE_3D_BLK("merge_3d_1", MERGE_3D_1, 0x4f000),
	MERGE_3D_BLK("merge_3d_2", MERGE_3D_2, 0x50000),
	MERGE_3D_BLK("merge_3d_3", MERGE_3D_3, 0x66700),
};

/*************************************************************
 * DSC sub blocks config
 *************************************************************/
#define DSC_BLK(_name, _id, _base, _features) \
	{\
	.name = _name, .id = _id, \
	.base = _base, .len = 0x140, \
	.features = _features, \
	}

static struct dpu_dsc_cfg sdm845_dsc[] = {
	DSC_BLK("dsc_0", DSC_0, 0x80000, 0),
	DSC_BLK("dsc_1", DSC_1, 0x80400, 0),
	DSC_BLK("dsc_2", DSC_2, 0x80800, 0),
	DSC_BLK("dsc_3", DSC_3, 0x80c00, 0),
};

static struct dpu_dsc_cfg sm8150_dsc[] = {
	DSC_BLK("dsc_0", DSC_0, 0x80000, BIT(DPU_DSC_OUTPUT_CTRL)),
	DSC_BLK("dsc_1", DSC_1, 0x80400, BIT(DPU_DSC_OUTPUT_CTRL)),
	DSC_BLK("dsc_2", DSC_2, 0x80800, BIT(DPU_DSC_OUTPUT_CTRL)),
	DSC_BLK("dsc_3", DSC_3, 0x80c00, BIT(DPU_DSC_OUTPUT_CTRL)),
};

/*************************************************************
 * INTF sub blocks config
 *************************************************************/
#define INTF_BLK(_name, _id, _base, _type, _ctrl_id, _progfetch, _features, _reg, _underrun_bit, _vsync_bit) \
	{\
	.name = _name, .id = _id, \
	.base = _base, .len = 0x280, \
	.features = _features, \
	.type = _type, \
	.controller_id = _ctrl_id, \
	.prog_fetch_lines_worst_case = _progfetch, \
	.intr_underrun = DPU_IRQ_IDX(_reg, _underrun_bit), \
	.intr_vsync = DPU_IRQ_IDX(_reg, _vsync_bit), \
	}

static const struct dpu_intf_cfg msm8998_intf[] = {
	INTF_BLK("intf_0", INTF_0, 0x6A000, INTF_DP, 0, 25, INTF_SDM845_MASK, MDP_SSPP_TOP0_INTR, 24, 25),
	INTF_BLK("intf_1", INTF_1, 0x6A800, INTF_DSI, 0, 25, INTF_SDM845_MASK, MDP_SSPP_TOP0_INTR, 26, 27),
	INTF_BLK("intf_2", INTF_2, 0x6B000, INTF_DSI, 1, 25, INTF_SDM845_MASK, MDP_SSPP_TOP0_INTR, 28, 29),
	INTF_BLK("intf_3", INTF_3, 0x6B800, INTF_HDMI, 0, 25, INTF_SDM845_MASK, MDP_SSPP_TOP0_INTR, 30, 31),
};

static const struct dpu_intf_cfg sdm845_intf[] = {
	INTF_BLK("intf_0", INTF_0, 0x6A000, INTF_DP, 0, 24, INTF_SDM845_MASK, MDP_SSPP_TOP0_INTR, 24, 25),
	INTF_BLK("intf_1", INTF_1, 0x6A800, INTF_DSI, 0, 24, INTF_SDM845_MASK, MDP_SSPP_TOP0_INTR, 26, 27),
	INTF_BLK("intf_2", INTF_2, 0x6B000, INTF_DSI, 1, 24, INTF_SDM845_MASK, MDP_SSPP_TOP0_INTR, 28, 29),
	INTF_BLK("intf_3", INTF_3, 0x6B800, INTF_DP, 1, 24, INTF_SDM845_MASK, MDP_SSPP_TOP0_INTR, 30, 31),
};

static const struct dpu_intf_cfg sc7180_intf[] = {
	INTF_BLK("intf_0", INTF_0, 0x6A000, INTF_DP, MSM_DP_CONTROLLER_0, 24, INTF_SC7180_MASK, MDP_SSPP_TOP0_INTR, 24, 25),
	INTF_BLK("intf_1", INTF_1, 0x6A800, INTF_DSI, 0, 24, INTF_SC7180_MASK, MDP_SSPP_TOP0_INTR, 26, 27),
};

static const struct dpu_intf_cfg sm8150_intf[] = {
	INTF_BLK("intf_0", INTF_0, 0x6A000, INTF_DP, 0, 24, INTF_SC7180_MASK, MDP_SSPP_TOP0_INTR, 24, 25),
	INTF_BLK("intf_1", INTF_1, 0x6A800, INTF_DSI, 0, 24, INTF_SC7180_MASK, MDP_SSPP_TOP0_INTR, 26, 27),
	INTF_BLK("intf_2", INTF_2, 0x6B000, INTF_DSI, 1, 24, INTF_SC7180_MASK, MDP_SSPP_TOP0_INTR, 28, 29),
	INTF_BLK("intf_3", INTF_3, 0x6B800, INTF_DP, 1, 24, INTF_SC7180_MASK, MDP_SSPP_TOP0_INTR, 30, 31),
};

static const struct dpu_intf_cfg sc7280_intf[] = {
	INTF_BLK("intf_0", INTF_0, 0x34000, INTF_DP, MSM_DP_CONTROLLER_0, 24, INTF_SC7280_MASK, MDP_SSPP_TOP0_INTR, 24, 25),
	INTF_BLK("intf_1", INTF_1, 0x35000, INTF_DSI, 0, 24, INTF_SC7280_MASK, MDP_SSPP_TOP0_INTR, 26, 27),
	INTF_BLK("intf_5", INTF_5, 0x39000, INTF_DP, MSM_DP_CONTROLLER_1, 24, INTF_SC7280_MASK, MDP_SSPP_TOP0_INTR, 22, 23),
};

static const struct dpu_intf_cfg sm8350_intf[] = {
	INTF_BLK("intf_0", INTF_0, 0x34000, INTF_DP, MSM_DP_CONTROLLER_0, 24, INTF_SC7280_MASK, MDP_SSPP_TOP0_INTR, 24, 25),
	INTF_BLK("intf_1", INTF_1, 0x35000, INTF_DSI, 0, 24, INTF_SC7280_MASK, MDP_SSPP_TOP0_INTR, 26, 27),
	INTF_BLK("intf_2", INTF_2, 0x36000, INTF_DSI, 1, 24, INTF_SC7280_MASK, MDP_SSPP_TOP0_INTR, 28, 29),
	INTF_BLK("intf_3", INTF_3, 0x37000, INTF_DP, MSM_DP_CONTROLLER_1, 24, INTF_SC7280_MASK, MDP_SSPP_TOP0_INTR, 30, 31),
};

static const struct dpu_intf_cfg sc8180x_intf[] = {
	INTF_BLK("intf_0", INTF_0, 0x6A000, INTF_DP, MSM_DP_CONTROLLER_0, 24, INTF_SC7180_MASK, MDP_SSPP_TOP0_INTR, 24, 25),
	INTF_BLK("intf_1", INTF_1, 0x6A800, INTF_DSI, 0, 24, INTF_SC7180_MASK, MDP_SSPP_TOP0_INTR, 26, 27),
	INTF_BLK("intf_2", INTF_2, 0x6B000, INTF_DSI, 1, 24, INTF_SC7180_MASK, MDP_SSPP_TOP0_INTR, 28, 29),
	/* INTF_3 is for MST, wired to INTF_DP 0 and 1, use dummy index until this is supported */
	INTF_BLK("intf_3", INTF_3, 0x6B800, INTF_DP, 999, 24, INTF_SC7180_MASK, MDP_SSPP_TOP0_INTR, 30, 31),
	INTF_BLK("intf_4", INTF_4, 0x6C000, INTF_DP, MSM_DP_CONTROLLER_1, 24, INTF_SC7180_MASK, MDP_SSPP_TOP0_INTR, 20, 21),
	INTF_BLK("intf_5", INTF_5, 0x6C800, INTF_DP, MSM_DP_CONTROLLER_2, 24, INTF_SC7180_MASK, MDP_SSPP_TOP0_INTR, 22, 23),
};

/* TODO: INTF 3, 8 and 7 are used for MST, marked as INTF_NONE for now */
static const struct dpu_intf_cfg sc8280xp_intf[] = {
	INTF_BLK("intf_0", INTF_0, 0x34000, INTF_DP, MSM_DP_CONTROLLER_0, 24, INTF_SC7280_MASK, MDP_SSPP_TOP0_INTR, 24, 25),
	INTF_BLK("intf_1", INTF_1, 0x35000, INTF_DSI, 0, 24, INTF_SC7280_MASK, MDP_SSPP_TOP0_INTR, 26, 27),
	INTF_BLK("intf_2", INTF_2, 0x36000, INTF_DSI, 1, 24, INTF_SC7280_MASK, MDP_SSPP_TOP0_INTR, 28, 29),
	INTF_BLK("intf_3", INTF_3, 0x37000, INTF_NONE, MSM_DP_CONTROLLER_0, 24, INTF_SC7280_MASK, MDP_SSPP_TOP0_INTR, 30, 31),
	INTF_BLK("intf_4", INTF_4, 0x38000, INTF_DP, MSM_DP_CONTROLLER_1, 24, INTF_SC7280_MASK, MDP_SSPP_TOP0_INTR, 20, 21),
	INTF_BLK("intf_5", INTF_5, 0x39000, INTF_DP, MSM_DP_CONTROLLER_3, 24, INTF_SC7280_MASK, MDP_SSPP_TOP0_INTR, 22, 23),
	INTF_BLK("intf_6", INTF_6, 0x3a000, INTF_DP, MSM_DP_CONTROLLER_2, 24, INTF_SC7280_MASK, MDP_SSPP_TOP0_INTR, 16, 17),
	INTF_BLK("intf_7", INTF_7, 0x3b000, INTF_NONE, MSM_DP_CONTROLLER_2, 24, INTF_SC7280_MASK, MDP_SSPP_TOP0_INTR, 18, 19),
	INTF_BLK("intf_8", INTF_8, 0x3c000, INTF_NONE, MSM_DP_CONTROLLER_1, 24, INTF_SC7280_MASK, MDP_SSPP_TOP0_INTR, 12, 13),
};

static const struct dpu_intf_cfg qcm2290_intf[] = {
	INTF_BLK("intf_0", INTF_0, 0x00000, INTF_NONE, 0, 0, 0, 0, 0, 0),
	INTF_BLK("intf_1", INTF_1, 0x6A800, INTF_DSI, 0, 24, INTF_SC7180_MASK, MDP_SSPP_TOP0_INTR, 26, 27),
};

static const struct dpu_intf_cfg sm8450_intf[] = {
	INTF_BLK("intf_0", INTF_0, 0x34000, INTF_DP, MSM_DP_CONTROLLER_0, 24, INTF_SC7280_MASK, MDP_SSPP_TOP0_INTR, 24, 25),
	INTF_BLK("intf_1", INTF_1, 0x35000, INTF_DSI, 0, 24, INTF_SC7280_MASK, MDP_SSPP_TOP0_INTR, 26, 27),
	INTF_BLK("intf_2", INTF_2, 0x36000, INTF_DSI, 1, 24, INTF_SC7280_MASK, MDP_SSPP_TOP0_INTR, 28, 29),
	INTF_BLK("intf_3", INTF_3, 0x37000, INTF_DP, MSM_DP_CONTROLLER_1, 24, INTF_SC7280_MASK, MDP_SSPP_TOP0_INTR, 30, 31),
};

static const struct dpu_intf_cfg sm8550_intf[] = {
	INTF_BLK("intf_0", INTF_0, 0x34000, INTF_DP, MSM_DP_CONTROLLER_0, 24, INTF_SC7280_MASK, MDP_SSPP_TOP0_INTR, 24, 25),
	/* TODO TE sub-blocks for intf1 & intf2 */
	INTF_BLK("intf_1", INTF_1, 0x35000, INTF_DSI, 0, 24, INTF_SC7280_MASK, MDP_SSPP_TOP0_INTR, 26, 27),
	INTF_BLK("intf_2", INTF_2, 0x36000, INTF_DSI, 1, 24, INTF_SC7280_MASK, MDP_SSPP_TOP0_INTR, 28, 29),
	INTF_BLK("intf_3", INTF_3, 0x37000, INTF_DP, MSM_DP_CONTROLLER_1, 24, INTF_SC7280_MASK, MDP_SSPP_TOP0_INTR, 30, 31),
};

/*************************************************************
 * Writeback blocks config
 *************************************************************/
#define WB_BLK(_name, _id, _base, _features, _clk_ctrl, \
		__xin_id, vbif_id, _reg, _max_linewidth, _wb_done_bit) \
	{ \
	.name = _name, .id = _id, \
	.base = _base, .len = 0x2c8, \
	.features = _features, \
	.format_list = wb2_formats, \
	.num_formats = ARRAY_SIZE(wb2_formats), \
	.clk_ctrl = _clk_ctrl, \
	.xin_id = __xin_id, \
	.vbif_idx = vbif_id, \
	.maxlinewidth = _max_linewidth, \
	.intr_wb_done = DPU_IRQ_IDX(_reg, _wb_done_bit) \
	}

static const struct dpu_wb_cfg sm8250_wb[] = {
	WB_BLK("wb_2", WB_2, 0x65000, WB_SM8250_MASK, DPU_CLK_CTRL_WB2, 6,
			VBIF_RT, MDP_SSPP_TOP0_INTR, 4096, 4),
};

/*************************************************************
 * VBIF sub blocks config
 *************************************************************/
/* VBIF QOS remap */
static const u32 msm8998_rt_pri_lvl[] = {1, 2, 2, 2};
static const u32 msm8998_nrt_pri_lvl[] = {1, 1, 1, 1};
static const u32 sdm845_rt_pri_lvl[] = {3, 3, 4, 4, 5, 5, 6, 6};
static const u32 sdm845_nrt_pri_lvl[] = {3, 3, 3, 3, 3, 3, 3, 3};

static const struct dpu_vbif_dynamic_ot_cfg msm8998_ot_rdwr_cfg[] = {
	{
		.pps = 1088 * 1920 * 30,
		.ot_limit = 2,
	},
	{
		.pps = 1088 * 1920 * 60,
		.ot_limit = 6,
	},
	{
		.pps = 3840 * 2160 * 30,
		.ot_limit = 16,
	},
};

static const struct dpu_vbif_cfg msm8998_vbif[] = {
	{
	.name = "vbif_rt", .id = VBIF_RT,
	.base = 0, .len = 0x1040,
	.default_ot_rd_limit = 32,
	.default_ot_wr_limit = 32,
	.features = BIT(DPU_VBIF_QOS_REMAP) | BIT(DPU_VBIF_QOS_OTLIM),
	.xin_halt_timeout = 0x4000,
	.qos_rp_remap_size = 0x20,
	.dynamic_ot_rd_tbl = {
		.count = ARRAY_SIZE(msm8998_ot_rdwr_cfg),
		.cfg = msm8998_ot_rdwr_cfg,
		},
	.dynamic_ot_wr_tbl = {
		.count = ARRAY_SIZE(msm8998_ot_rdwr_cfg),
		.cfg = msm8998_ot_rdwr_cfg,
		},
	.qos_rt_tbl = {
		.npriority_lvl = ARRAY_SIZE(msm8998_rt_pri_lvl),
		.priority_lvl = msm8998_rt_pri_lvl,
		},
	.qos_nrt_tbl = {
		.npriority_lvl = ARRAY_SIZE(msm8998_nrt_pri_lvl),
		.priority_lvl = msm8998_nrt_pri_lvl,
		},
	.memtype_count = 14,
	.memtype = {2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2},
	},
};

static const struct dpu_vbif_cfg sdm845_vbif[] = {
	{
	.name = "vbif_rt", .id = VBIF_RT,
	.base = 0, .len = 0x1040,
	.features = BIT(DPU_VBIF_QOS_REMAP),
	.xin_halt_timeout = 0x4000,
	.qos_rp_remap_size = 0x40,
	.qos_rt_tbl = {
		.npriority_lvl = ARRAY_SIZE(sdm845_rt_pri_lvl),
		.priority_lvl = sdm845_rt_pri_lvl,
		},
	.qos_nrt_tbl = {
		.npriority_lvl = ARRAY_SIZE(sdm845_nrt_pri_lvl),
		.priority_lvl = sdm845_nrt_pri_lvl,
		},
	.memtype_count = 14,
	.memtype = {3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3},
	},
};

static const struct dpu_reg_dma_cfg sc8280xp_regdma = {
	.base = 0x0,
	.version = 0x00020000,
	.trigger_sel_off = 0x119c,
	.xin_id = 7,
	.clk_ctrl = DPU_CLK_CTRL_REG_DMA,
};

static const struct dpu_reg_dma_cfg sdm845_regdma = {
	.base = 0x0, .version = 0x1, .trigger_sel_off = 0x119c
};

static const struct dpu_reg_dma_cfg sm8150_regdma = {
	.base = 0x0, .version = 0x00010001, .trigger_sel_off = 0x119c
};

static const struct dpu_reg_dma_cfg sm8250_regdma = {
	.base = 0x0,
	.version = 0x00010002,
	.trigger_sel_off = 0x119c,
	.xin_id = 7,
	.clk_ctrl = DPU_CLK_CTRL_REG_DMA,
};

static const struct dpu_reg_dma_cfg sm8350_regdma = {
	.base = 0x400,
	.version = 0x00020000,
	.trigger_sel_off = 0x119c,
	.xin_id = 7,
	.clk_ctrl = DPU_CLK_CTRL_REG_DMA,
};

static const struct dpu_reg_dma_cfg sm8450_regdma = {
	.base = 0x0,
	.version = 0x00020000,
	.trigger_sel_off = 0x119c,
	.xin_id = 7,
	.clk_ctrl = DPU_CLK_CTRL_REG_DMA,
};

/*************************************************************
 * PERF data config
 *************************************************************/

/* SSPP QOS LUTs */
static const struct dpu_qos_lut_entry msm8998_qos_linear[] = {
	{.fl = 4,  .lut = 0x1b},
	{.fl = 5,  .lut = 0x5b},
	{.fl = 6,  .lut = 0x15b},
	{.fl = 7,  .lut = 0x55b},
	{.fl = 8,  .lut = 0x155b},
	{.fl = 9,  .lut = 0x555b},
	{.fl = 10, .lut = 0x1555b},
	{.fl = 11, .lut = 0x5555b},
	{.fl = 12, .lut = 0x15555b},
	{.fl = 13, .lut = 0x55555b},
	{.fl = 14, .lut = 0},
	{.fl = 1,  .lut = 0x1b},
	{.fl = 0,  .lut = 0}
};

static const struct dpu_qos_lut_entry sdm845_qos_linear[] = {
	{.fl = 4, .lut = 0x357},
	{.fl = 5, .lut = 0x3357},
	{.fl = 6, .lut = 0x23357},
	{.fl = 7, .lut = 0x223357},
	{.fl = 8, .lut = 0x2223357},
	{.fl = 9, .lut = 0x22223357},
	{.fl = 10, .lut = 0x222223357},
	{.fl = 11, .lut = 0x2222223357},
	{.fl = 12, .lut = 0x22222223357},
	{.fl = 13, .lut = 0x222222223357},
	{.fl = 14, .lut = 0x1222222223357},
	{.fl = 0, .lut = 0x11222222223357}
};

static const struct dpu_qos_lut_entry msm8998_qos_macrotile[] = {
	{.fl = 10, .lut = 0x1aaff},
	{.fl = 11, .lut = 0x5aaff},
	{.fl = 12, .lut = 0x15aaff},
	{.fl = 13, .lut = 0x55aaff},
	{.fl = 1,  .lut = 0x1aaff},
	{.fl = 0,  .lut = 0},
};

static const struct dpu_qos_lut_entry sc7180_qos_linear[] = {
	{.fl = 0, .lut = 0x0011222222335777},
};

static const struct dpu_qos_lut_entry sm8150_qos_linear[] = {
	{.fl = 0, .lut = 0x0011222222223357 },
};

static const struct dpu_qos_lut_entry sc8180x_qos_linear[] = {
	{.fl = 4, .lut = 0x0000000000000357 },
};

static const struct dpu_qos_lut_entry qcm2290_qos_linear[] = {
	{.fl = 0, .lut = 0x0011222222335777},
};

static const struct dpu_qos_lut_entry sdm845_qos_macrotile[] = {
	{.fl = 10, .lut = 0x344556677},
	{.fl = 11, .lut = 0x3344556677},
	{.fl = 12, .lut = 0x23344556677},
	{.fl = 13, .lut = 0x223344556677},
	{.fl = 14, .lut = 0x1223344556677},
	{.fl = 0, .lut = 0x112233344556677},
};

static const struct dpu_qos_lut_entry sc7180_qos_macrotile[] = {
	{.fl = 0, .lut = 0x0011223344556677},
};

static const struct dpu_qos_lut_entry sc8180x_qos_macrotile[] = {
	{.fl = 10, .lut = 0x0000000344556677},
};

static const struct dpu_qos_lut_entry msm8998_qos_nrt[] = {
	{.fl = 0, .lut = 0x0},
};

static const struct dpu_qos_lut_entry sdm845_qos_nrt[] = {
	{.fl = 0, .lut = 0x0},
};

static const struct dpu_qos_lut_entry sc7180_qos_nrt[] = {
	{.fl = 0, .lut = 0x0},
};

static const struct dpu_perf_cfg msm8998_perf_data = {
	.max_bw_low = 6700000,
	.max_bw_high = 6700000,
	.min_core_ib = 2400000,
	.min_llcc_ib = 800000,
	.min_dram_ib = 800000,
	.undersized_prefill_lines = 2,
	.xtra_prefill_lines = 2,
	.dest_scale_prefill_lines = 3,
	.macrotile_prefill_lines = 4,
	.yuv_nv12_prefill_lines = 8,
	.linear_prefill_lines = 1,
	.downscaling_prefill_lines = 1,
	.amortizable_threshold = 25,
	.min_prefill_lines = 25,
	.danger_lut_tbl = {0xf, 0xffff, 0x0},
	.safe_lut_tbl = {0xfffc, 0xff00, 0xffff},
	.qos_lut_tbl = {
		{.nentry = ARRAY_SIZE(msm8998_qos_linear),
		.entries = msm8998_qos_linear
		},
		{.nentry = ARRAY_SIZE(msm8998_qos_macrotile),
		.entries = msm8998_qos_macrotile
		},
		{.nentry = ARRAY_SIZE(msm8998_qos_nrt),
		.entries = msm8998_qos_nrt
		},
	},
	.cdp_cfg = {
		{.rd_enable = 1, .wr_enable = 1},
		{.rd_enable = 1, .wr_enable = 0}
	},
	.clk_inefficiency_factor = 200,
	.bw_inefficiency_factor = 120,
};

static const struct dpu_perf_cfg sdm845_perf_data = {
	.max_bw_low = 6800000,
	.max_bw_high = 6800000,
	.min_core_ib = 2400000,
	.min_llcc_ib = 800000,
	.min_dram_ib = 800000,
	.undersized_prefill_lines = 2,
	.xtra_prefill_lines = 2,
	.dest_scale_prefill_lines = 3,
	.macrotile_prefill_lines = 4,
	.yuv_nv12_prefill_lines = 8,
	.linear_prefill_lines = 1,
	.downscaling_prefill_lines = 1,
	.amortizable_threshold = 25,
	.min_prefill_lines = 24,
	.danger_lut_tbl = {0xf, 0xffff, 0x0},
	.safe_lut_tbl = {0xfff0, 0xf000, 0xffff},
	.qos_lut_tbl = {
		{.nentry = ARRAY_SIZE(sdm845_qos_linear),
		.entries = sdm845_qos_linear
		},
		{.nentry = ARRAY_SIZE(sdm845_qos_macrotile),
		.entries = sdm845_qos_macrotile
		},
		{.nentry = ARRAY_SIZE(sdm845_qos_nrt),
		.entries = sdm845_qos_nrt
		},
	},
	.cdp_cfg = {
		{.rd_enable = 1, .wr_enable = 1},
		{.rd_enable = 1, .wr_enable = 0}
	},
	.clk_inefficiency_factor = 105,
	.bw_inefficiency_factor = 120,
};

static const struct dpu_perf_cfg sc7180_perf_data = {
	.max_bw_low = 6800000,
	.max_bw_high = 6800000,
	.min_core_ib = 2400000,
	.min_llcc_ib = 800000,
	.min_dram_ib = 1600000,
	.min_prefill_lines = 24,
	.danger_lut_tbl = {0xff, 0xffff, 0x0},
	.safe_lut_tbl = {0xfff0, 0xff00, 0xffff},
	.qos_lut_tbl = {
		{.nentry = ARRAY_SIZE(sc7180_qos_linear),
		.entries = sc7180_qos_linear
		},
		{.nentry = ARRAY_SIZE(sc7180_qos_macrotile),
		.entries = sc7180_qos_macrotile
		},
		{.nentry = ARRAY_SIZE(sc7180_qos_nrt),
		.entries = sc7180_qos_nrt
		},
	},
	.cdp_cfg = {
		{.rd_enable = 1, .wr_enable = 1},
		{.rd_enable = 1, .wr_enable = 0}
	},
	.clk_inefficiency_factor = 105,
	.bw_inefficiency_factor = 120,
};

static const struct dpu_perf_cfg sm6115_perf_data = {
	.max_bw_low = 3100000,
	.max_bw_high = 4000000,
	.min_core_ib = 2400000,
	.min_llcc_ib = 800000,
	.min_dram_ib = 800000,
	.min_prefill_lines = 24,
	.danger_lut_tbl = {0xff, 0xffff, 0x0},
	.safe_lut_tbl = {0xfff0, 0xff00, 0xffff},
	.qos_lut_tbl = {
		{.nentry = ARRAY_SIZE(sc7180_qos_linear),
		.entries = sc7180_qos_linear
		},
		{.nentry = ARRAY_SIZE(sc7180_qos_macrotile),
		.entries = sc7180_qos_macrotile
		},
		{.nentry = ARRAY_SIZE(sc7180_qos_nrt),
		.entries = sc7180_qos_nrt
		},
		/* TODO: macrotile-qseed is different from macrotile */
	},
	.cdp_cfg = {
		{.rd_enable = 1, .wr_enable = 1},
		{.rd_enable = 1, .wr_enable = 0}
	},
	.clk_inefficiency_factor = 105,
	.bw_inefficiency_factor = 120,
};

static const struct dpu_perf_cfg sm8150_perf_data = {
	.max_bw_low = 12800000,
	.max_bw_high = 12800000,
	.min_core_ib = 2400000,
	.min_llcc_ib = 800000,
	.min_dram_ib = 800000,
	.min_prefill_lines = 24,
	.danger_lut_tbl = {0xf, 0xffff, 0x0},
	.safe_lut_tbl = {0xfff8, 0xf000, 0xffff},
	.qos_lut_tbl = {
		{.nentry = ARRAY_SIZE(sm8150_qos_linear),
		.entries = sm8150_qos_linear
		},
		{.nentry = ARRAY_SIZE(sc7180_qos_macrotile),
		.entries = sc7180_qos_macrotile
		},
		{.nentry = ARRAY_SIZE(sc7180_qos_nrt),
		.entries = sc7180_qos_nrt
		},
		/* TODO: macrotile-qseed is different from macrotile */
	},
	.cdp_cfg = {
		{.rd_enable = 1, .wr_enable = 1},
		{.rd_enable = 1, .wr_enable = 0}
	},
	.clk_inefficiency_factor = 105,
	.bw_inefficiency_factor = 120,
};

static const struct dpu_perf_cfg sc8180x_perf_data = {
	.max_bw_low = 9600000,
	.max_bw_high = 9600000,
	.min_core_ib = 2400000,
	.min_llcc_ib = 800000,
	.min_dram_ib = 800000,
	.danger_lut_tbl = {0xf, 0xffff, 0x0},
	.qos_lut_tbl = {
		{.nentry = ARRAY_SIZE(sc7180_qos_linear),
		.entries = sc7180_qos_linear
		},
		{.nentry = ARRAY_SIZE(sc7180_qos_macrotile),
		.entries = sc7180_qos_macrotile
		},
		{.nentry = ARRAY_SIZE(sc7180_qos_nrt),
		.entries = sc7180_qos_nrt
		},
		/* TODO: macrotile-qseed is different from macrotile */
	},
	.cdp_cfg = {
		{.rd_enable = 1, .wr_enable = 1},
		{.rd_enable = 1, .wr_enable = 0}
	},
	.clk_inefficiency_factor = 105,
	.bw_inefficiency_factor = 120,
};

static const struct dpu_perf_cfg sc8280xp_perf_data = {
	.max_bw_low = 13600000,
	.max_bw_high = 18200000,
	.min_core_ib = 2500000,
	.min_llcc_ib = 0,
	.min_dram_ib = 800000,
	.danger_lut_tbl = {0xf, 0xffff, 0x0},
	.qos_lut_tbl = {
		{.nentry = ARRAY_SIZE(sc8180x_qos_linear),
		.entries = sc8180x_qos_linear
		},
		{.nentry = ARRAY_SIZE(sc8180x_qos_macrotile),
		.entries = sc8180x_qos_macrotile
		},
		{.nentry = ARRAY_SIZE(sc7180_qos_nrt),
		.entries = sc7180_qos_nrt
		},
		/* TODO: macrotile-qseed is different from macrotile */
	},
	.cdp_cfg = {
		{.rd_enable = 1, .wr_enable = 1},
		{.rd_enable = 1, .wr_enable = 0}
	},
	.clk_inefficiency_factor = 105,
	.bw_inefficiency_factor = 120,
};

static const struct dpu_perf_cfg sm8250_perf_data = {
	.max_bw_low = 13700000,
	.max_bw_high = 16600000,
	.min_core_ib = 4800000,
	.min_llcc_ib = 0,
	.min_dram_ib = 800000,
	.min_prefill_lines = 35,
	.danger_lut_tbl = {0xf, 0xffff, 0x0},
	.safe_lut_tbl = {0xfff0, 0xff00, 0xffff},
	.qos_lut_tbl = {
		{.nentry = ARRAY_SIZE(sc7180_qos_linear),
		.entries = sc7180_qos_linear
		},
		{.nentry = ARRAY_SIZE(sc7180_qos_macrotile),
		.entries = sc7180_qos_macrotile
		},
		{.nentry = ARRAY_SIZE(sc7180_qos_nrt),
		.entries = sc7180_qos_nrt
		},
		/* TODO: macrotile-qseed is different from macrotile */
	},
	.cdp_cfg = {
		{.rd_enable = 1, .wr_enable = 1},
		{.rd_enable = 1, .wr_enable = 0}
	},
	.clk_inefficiency_factor = 105,
	.bw_inefficiency_factor = 120,
};

static const struct dpu_perf_cfg sm8450_perf_data = {
	.max_bw_low = 13600000,
	.max_bw_high = 18200000,
	.min_core_ib = 2500000,
	.min_llcc_ib = 0,
	.min_dram_ib = 800000,
	.min_prefill_lines = 35,
	/* FIXME: lut tables */
	.danger_lut_tbl = {0x3ffff, 0x3ffff, 0x0},
	.safe_lut_tbl = {0xfe00, 0xfe00, 0xffff},
	.qos_lut_tbl = {
		{.nentry = ARRAY_SIZE(sc7180_qos_linear),
		.entries = sc7180_qos_linear
		},
		{.nentry = ARRAY_SIZE(sc7180_qos_macrotile),
		.entries = sc7180_qos_macrotile
		},
		{.nentry = ARRAY_SIZE(sc7180_qos_nrt),
		.entries = sc7180_qos_nrt
		},
		/* TODO: macrotile-qseed is different from macrotile */
	},
	.cdp_cfg = {
		{.rd_enable = 1, .wr_enable = 1},
		{.rd_enable = 1, .wr_enable = 0}
	},
	.clk_inefficiency_factor = 105,
	.bw_inefficiency_factor = 120,
};

static const struct dpu_perf_cfg sc7280_perf_data = {
	.max_bw_low = 4700000,
	.max_bw_high = 8800000,
	.min_core_ib = 2500000,
	.min_llcc_ib = 0,
	.min_dram_ib = 1600000,
	.min_prefill_lines = 24,
	.danger_lut_tbl = {0xffff, 0xffff, 0x0},
	.safe_lut_tbl = {0xff00, 0xff00, 0xffff},
	.qos_lut_tbl = {
		{.nentry = ARRAY_SIZE(sc7180_qos_macrotile),
		.entries = sc7180_qos_macrotile
		},
		{.nentry = ARRAY_SIZE(sc7180_qos_macrotile),
		.entries = sc7180_qos_macrotile
		},
		{.nentry = ARRAY_SIZE(sc7180_qos_nrt),
		.entries = sc7180_qos_nrt
		},
	},
	.cdp_cfg = {
		{.rd_enable = 1, .wr_enable = 1},
		{.rd_enable = 1, .wr_enable = 0}
	},
	.clk_inefficiency_factor = 105,
	.bw_inefficiency_factor = 120,
};

static const struct dpu_perf_cfg sm8350_perf_data = {
	.max_bw_low = 11800000,
	.max_bw_high = 15500000,
	.min_core_ib = 2500000,
	.min_llcc_ib = 0,
	.min_dram_ib = 800000,
	.min_prefill_lines = 40,
	/* FIXME: lut tables */
	.danger_lut_tbl = {0x3ffff, 0x3ffff, 0x0},
	.safe_lut_tbl = {0xfe00, 0xfe00, 0xffff},
	.qos_lut_tbl = {
		{.nentry = ARRAY_SIZE(sc7180_qos_linear),
		.entries = sc7180_qos_linear
		},
		{.nentry = ARRAY_SIZE(sc7180_qos_macrotile),
		.entries = sc7180_qos_macrotile
		},
		{.nentry = ARRAY_SIZE(sc7180_qos_nrt),
		.entries = sc7180_qos_nrt
		},
		/* TODO: macrotile-qseed is different from macrotile */
	},
	.cdp_cfg = {
		{.rd_enable = 1, .wr_enable = 1},
		{.rd_enable = 1, .wr_enable = 0}
	},
	.clk_inefficiency_factor = 105,
	.bw_inefficiency_factor = 120,
};

static const struct dpu_perf_cfg qcm2290_perf_data = {
	.max_bw_low = 2700000,
	.max_bw_high = 2700000,
	.min_core_ib = 1300000,
	.min_llcc_ib = 0,
	.min_dram_ib = 1600000,
	.min_prefill_lines = 24,
	.danger_lut_tbl = {0xff, 0x0, 0x0},
	.safe_lut_tbl = {0xfff0, 0x0, 0x0},
	.qos_lut_tbl = {
		{.nentry = ARRAY_SIZE(qcm2290_qos_linear),
		.entries = qcm2290_qos_linear
		},
	},
	.cdp_cfg = {
		{.rd_enable = 1, .wr_enable = 1},
		{.rd_enable = 1, .wr_enable = 0}
	},
	.clk_inefficiency_factor = 105,
	.bw_inefficiency_factor = 120,
};
/*************************************************************
 * Hardware catalog
 *************************************************************/

static const struct dpu_mdss_cfg msm8998_dpu_cfg = {
	.caps = &msm8998_dpu_caps,
	.mdp_count = ARRAY_SIZE(msm8998_mdp),
	.mdp = msm8998_mdp,
	.ctl_count = ARRAY_SIZE(msm8998_ctl),
	.ctl = msm8998_ctl,
	.sspp_count = ARRAY_SIZE(msm8998_sspp),
	.sspp = msm8998_sspp,
	.mixer_count = ARRAY_SIZE(msm8998_lm),
	.mixer = msm8998_lm,
	.dspp_count = ARRAY_SIZE(msm8998_dspp),
	.dspp = msm8998_dspp,
	.pingpong_count = ARRAY_SIZE(sdm845_pp),
	.pingpong = sdm845_pp,
	.intf_count = ARRAY_SIZE(msm8998_intf),
	.intf = msm8998_intf,
	.vbif_count = ARRAY_SIZE(msm8998_vbif),
	.vbif = msm8998_vbif,
	.reg_dma_count = 0,
	.perf = &msm8998_perf_data,
	.mdss_irqs = IRQ_SM8250_MASK,
};

static const struct dpu_mdss_cfg sdm845_dpu_cfg = {
	.caps = &sdm845_dpu_caps,
	.mdp_count = ARRAY_SIZE(sdm845_mdp),
	.mdp = sdm845_mdp,
	.ctl_count = ARRAY_SIZE(sdm845_ctl),
	.ctl = sdm845_ctl,
	.sspp_count = ARRAY_SIZE(sdm845_sspp),
	.sspp = sdm845_sspp,
	.mixer_count = ARRAY_SIZE(sdm845_lm),
	.mixer = sdm845_lm,
	.pingpong_count = ARRAY_SIZE(sdm845_pp),
	.pingpong = sdm845_pp,
	.dsc_count = ARRAY_SIZE(sdm845_dsc),
	.dsc = sdm845_dsc,
	.intf_count = ARRAY_SIZE(sdm845_intf),
	.intf = sdm845_intf,
	.vbif_count = ARRAY_SIZE(sdm845_vbif),
	.vbif = sdm845_vbif,
	.reg_dma_count = 1,
	.dma_cfg = &sdm845_regdma,
	.perf = &sdm845_perf_data,
	.mdss_irqs = IRQ_SDM845_MASK,
};

static const struct dpu_mdss_cfg sc7180_dpu_cfg = {
	.caps = &sc7180_dpu_caps,
	.mdp_count = ARRAY_SIZE(sc7180_mdp),
	.mdp = sc7180_mdp,
	.ctl_count = ARRAY_SIZE(sc7180_ctl),
	.ctl = sc7180_ctl,
	.sspp_count = ARRAY_SIZE(sc7180_sspp),
	.sspp = sc7180_sspp,
	.mixer_count = ARRAY_SIZE(sc7180_lm),
	.mixer = sc7180_lm,
	.dspp_count = ARRAY_SIZE(sc7180_dspp),
	.dspp = sc7180_dspp,
	.pingpong_count = ARRAY_SIZE(sc7180_pp),
	.pingpong = sc7180_pp,
	.intf_count = ARRAY_SIZE(sc7180_intf),
	.intf = sc7180_intf,
	.wb_count = ARRAY_SIZE(sm8250_wb),
	.wb = sm8250_wb,
	.vbif_count = ARRAY_SIZE(sdm845_vbif),
	.vbif = sdm845_vbif,
	.reg_dma_count = 1,
	.dma_cfg = &sdm845_regdma,
	.perf = &sc7180_perf_data,
	.mdss_irqs = IRQ_SC7180_MASK,
};

static const struct dpu_mdss_cfg sm6115_dpu_cfg = {
	.caps = &sm6115_dpu_caps,
	.mdp_count = ARRAY_SIZE(sm6115_mdp),
	.mdp = sm6115_mdp,
	.ctl_count = ARRAY_SIZE(qcm2290_ctl),
	.ctl = qcm2290_ctl,
	.sspp_count = ARRAY_SIZE(sm6115_sspp),
	.sspp = sm6115_sspp,
	.mixer_count = ARRAY_SIZE(qcm2290_lm),
	.mixer = qcm2290_lm,
	.dspp_count = ARRAY_SIZE(qcm2290_dspp),
	.dspp = qcm2290_dspp,
	.pingpong_count = ARRAY_SIZE(qcm2290_pp),
	.pingpong = qcm2290_pp,
	.intf_count = ARRAY_SIZE(qcm2290_intf),
	.intf = qcm2290_intf,
	.vbif_count = ARRAY_SIZE(sdm845_vbif),
	.vbif = sdm845_vbif,
	.perf = &sm6115_perf_data,
	.mdss_irqs = IRQ_SC7180_MASK,
};

static const struct dpu_mdss_cfg sm8150_dpu_cfg = {
	.caps = &sm8150_dpu_caps,
	.mdp_count = ARRAY_SIZE(sdm845_mdp),
	.mdp = sdm845_mdp,
	.ctl_count = ARRAY_SIZE(sm8150_ctl),
	.ctl = sm8150_ctl,
	.sspp_count = ARRAY_SIZE(sdm845_sspp),
	.sspp = sdm845_sspp,
	.mixer_count = ARRAY_SIZE(sm8150_lm),
	.mixer = sm8150_lm,
	.dspp_count = ARRAY_SIZE(sm8150_dspp),
	.dspp = sm8150_dspp,
	.dsc_count = ARRAY_SIZE(sm8150_dsc),
	.dsc = sm8150_dsc,
	.pingpong_count = ARRAY_SIZE(sm8150_pp),
	.pingpong = sm8150_pp,
	.merge_3d_count = ARRAY_SIZE(sm8150_merge_3d),
	.merge_3d = sm8150_merge_3d,
	.intf_count = ARRAY_SIZE(sm8150_intf),
	.intf = sm8150_intf,
	.vbif_count = ARRAY_SIZE(sdm845_vbif),
	.vbif = sdm845_vbif,
	.reg_dma_count = 1,
	.dma_cfg = &sm8150_regdma,
	.perf = &sm8150_perf_data,
	.mdss_irqs = IRQ_SDM845_MASK,
};

static const struct dpu_mdss_cfg sc8180x_dpu_cfg = {
	.caps = &sc8180x_dpu_caps,
	.mdp_count = ARRAY_SIZE(sc8180x_mdp),
	.mdp = sc8180x_mdp,
	.ctl_count = ARRAY_SIZE(sm8150_ctl),
	.ctl = sm8150_ctl,
	.sspp_count = ARRAY_SIZE(sdm845_sspp),
	.sspp = sdm845_sspp,
	.mixer_count = ARRAY_SIZE(sm8150_lm),
	.mixer = sm8150_lm,
	.pingpong_count = ARRAY_SIZE(sm8150_pp),
	.pingpong = sm8150_pp,
	.merge_3d_count = ARRAY_SIZE(sm8150_merge_3d),
	.merge_3d = sm8150_merge_3d,
	.intf_count = ARRAY_SIZE(sc8180x_intf),
	.intf = sc8180x_intf,
	.vbif_count = ARRAY_SIZE(sdm845_vbif),
	.vbif = sdm845_vbif,
	.reg_dma_count = 1,
	.dma_cfg = &sm8150_regdma,
	.perf = &sc8180x_perf_data,
	.mdss_irqs = IRQ_SC8180X_MASK,
};

static const struct dpu_mdss_cfg sc8280xp_dpu_cfg = {
	.caps = &sc8280xp_dpu_caps,
	.mdp_count = ARRAY_SIZE(sc8280xp_mdp),
	.mdp = sc8280xp_mdp,
	.ctl_count = ARRAY_SIZE(sc8280xp_ctl),
	.ctl = sc8280xp_ctl,
	.sspp_count = ARRAY_SIZE(sc8280xp_sspp),
	.sspp = sc8280xp_sspp,
	.mixer_count = ARRAY_SIZE(sc8280xp_lm),
	.mixer = sc8280xp_lm,
	.dspp_count = ARRAY_SIZE(sm8150_dspp),
	.dspp = sm8150_dspp,
	.pingpong_count = ARRAY_SIZE(sc8280xp_pp),
	.pingpong = sc8280xp_pp,
	.merge_3d_count = ARRAY_SIZE(sm8350_merge_3d),
	.merge_3d = sm8350_merge_3d,
	.intf_count = ARRAY_SIZE(sc8280xp_intf),
	.intf = sc8280xp_intf,
	.vbif_count = ARRAY_SIZE(sdm845_vbif),
	.vbif = sdm845_vbif,
	.reg_dma_count = 1,
	.dma_cfg = &sc8280xp_regdma,
	.perf = &sc8280xp_perf_data,
	.mdss_irqs = IRQ_SC8280XP_MASK,
};

static const struct dpu_mdss_cfg sm8250_dpu_cfg = {
	.caps = &sm8250_dpu_caps,
	.mdp_count = ARRAY_SIZE(sm8250_mdp),
	.mdp = sm8250_mdp,
	.ctl_count = ARRAY_SIZE(sm8150_ctl),
	.ctl = sm8150_ctl,
	.sspp_count = ARRAY_SIZE(sm8250_sspp),
	.sspp = sm8250_sspp,
	.mixer_count = ARRAY_SIZE(sm8150_lm),
	.mixer = sm8150_lm,
	.dspp_count = ARRAY_SIZE(sm8150_dspp),
	.dspp = sm8150_dspp,
	.dsc_count = ARRAY_SIZE(sm8150_dsc),
	.dsc = sm8150_dsc,
	.pingpong_count = ARRAY_SIZE(sm8150_pp),
	.pingpong = sm8150_pp,
	.merge_3d_count = ARRAY_SIZE(sm8150_merge_3d),
	.merge_3d = sm8150_merge_3d,
	.intf_count = ARRAY_SIZE(sm8150_intf),
	.intf = sm8150_intf,
	.vbif_count = ARRAY_SIZE(sdm845_vbif),
	.vbif = sdm845_vbif,
	.wb_count = ARRAY_SIZE(sm8250_wb),
	.wb = sm8250_wb,
	.reg_dma_count = 1,
	.dma_cfg = &sm8250_regdma,
	.perf = &sm8250_perf_data,
	.mdss_irqs = IRQ_SM8250_MASK,
};

static const struct dpu_mdss_cfg sm8350_dpu_cfg = {
	.caps = &sm8350_dpu_caps,
	.mdp_count = ARRAY_SIZE(sm8350_mdp),
	.mdp = sm8350_mdp,
	.ctl_count = ARRAY_SIZE(sm8350_ctl),
	.ctl = sm8350_ctl,
	.sspp_count = ARRAY_SIZE(sm8250_sspp),
	.sspp = sm8250_sspp,
	.mixer_count = ARRAY_SIZE(sm8150_lm),
	.mixer = sm8150_lm,
	.dspp_count = ARRAY_SIZE(sm8150_dspp),
	.dspp = sm8150_dspp,
	.pingpong_count = ARRAY_SIZE(sm8350_pp),
	.pingpong = sm8350_pp,
	.merge_3d_count = ARRAY_SIZE(sm8350_merge_3d),
	.merge_3d = sm8350_merge_3d,
	.intf_count = ARRAY_SIZE(sm8350_intf),
	.intf = sm8350_intf,
	.vbif_count = ARRAY_SIZE(sdm845_vbif),
	.vbif = sdm845_vbif,
	.reg_dma_count = 1,
	.dma_cfg = &sm8350_regdma,
	.perf = &sm8350_perf_data,
	.mdss_irqs = IRQ_SM8350_MASK,
};

static const struct dpu_mdss_cfg sm8450_dpu_cfg = {
	.caps = &sm8450_dpu_caps,
	.mdp_count = ARRAY_SIZE(sm8450_mdp),
	.mdp = sm8450_mdp,
	.ctl_count = ARRAY_SIZE(sm8450_ctl),
	.ctl = sm8450_ctl,
	.sspp_count = ARRAY_SIZE(sm8450_sspp),
	.sspp = sm8450_sspp,
	.mixer_count = ARRAY_SIZE(sm8150_lm),
	.mixer = sm8150_lm,
	.dspp_count = ARRAY_SIZE(sm8150_dspp),
	.dspp = sm8150_dspp,
	.pingpong_count = ARRAY_SIZE(sm8450_pp),
	.pingpong = sm8450_pp,
	.merge_3d_count = ARRAY_SIZE(sm8450_merge_3d),
	.merge_3d = sm8450_merge_3d,
	.intf_count = ARRAY_SIZE(sm8450_intf),
	.intf = sm8450_intf,
	.vbif_count = ARRAY_SIZE(sdm845_vbif),
	.vbif = sdm845_vbif,
	.reg_dma_count = 1,
	.dma_cfg = &sm8450_regdma,
	.perf = &sm8450_perf_data,
	.mdss_irqs = IRQ_SM8450_MASK,
};

static const struct dpu_mdss_cfg sm8550_dpu_cfg = {
	.caps = &sm8550_dpu_caps,
	.mdp_count = ARRAY_SIZE(sm8550_mdp),
	.mdp = sm8550_mdp,
	.ctl_count = ARRAY_SIZE(sm8550_ctl),
	.ctl = sm8550_ctl,
	.sspp_count = ARRAY_SIZE(sm8550_sspp),
	.sspp = sm8550_sspp,
	.mixer_count = ARRAY_SIZE(sm8150_lm),
	.mixer = sm8150_lm,
	.dspp_count = ARRAY_SIZE(sm8150_dspp),
	.dspp = sm8150_dspp,
	.pingpong_count = ARRAY_SIZE(sm8550_pp),
	.pingpong = sm8550_pp,
	.merge_3d_count = ARRAY_SIZE(sm8550_merge_3d),
	.merge_3d = sm8550_merge_3d,
	.intf_count = ARRAY_SIZE(sm8550_intf),
	.intf = sm8550_intf,
	.vbif_count = ARRAY_SIZE(sdm845_vbif),
	.vbif = sdm845_vbif,
	.reg_dma_count = 1,
	.dma_cfg = &sm8450_regdma,
	.perf = &sm8450_perf_data,
	.mdss_irqs = IRQ_SM8450_MASK,
};

static const struct dpu_mdss_cfg sc7280_dpu_cfg = {
	.caps = &sc7280_dpu_caps,
	.mdp_count = ARRAY_SIZE(sc7280_mdp),
	.mdp = sc7280_mdp,
	.ctl_count = ARRAY_SIZE(sc7280_ctl),
	.ctl = sc7280_ctl,
	.sspp_count = ARRAY_SIZE(sc7280_sspp),
	.sspp = sc7280_sspp,
	.dspp_count = ARRAY_SIZE(sc7180_dspp),
	.dspp = sc7180_dspp,
	.mixer_count = ARRAY_SIZE(sc7280_lm),
	.mixer = sc7280_lm,
	.pingpong_count = ARRAY_SIZE(sc7280_pp),
	.pingpong = sc7280_pp,
	.intf_count = ARRAY_SIZE(sc7280_intf),
	.intf = sc7280_intf,
	.vbif_count = ARRAY_SIZE(sdm845_vbif),
	.vbif = sdm845_vbif,
	.perf = &sc7280_perf_data,
	.mdss_irqs = IRQ_SC7280_MASK,
};

static const struct dpu_mdss_cfg qcm2290_dpu_cfg = {
	.caps = &qcm2290_dpu_caps,
	.mdp_count = ARRAY_SIZE(qcm2290_mdp),
	.mdp = qcm2290_mdp,
	.ctl_count = ARRAY_SIZE(qcm2290_ctl),
	.ctl = qcm2290_ctl,
	.sspp_count = ARRAY_SIZE(qcm2290_sspp),
	.sspp = qcm2290_sspp,
	.mixer_count = ARRAY_SIZE(qcm2290_lm),
	.mixer = qcm2290_lm,
	.dspp_count = ARRAY_SIZE(qcm2290_dspp),
	.dspp = qcm2290_dspp,
	.pingpong_count = ARRAY_SIZE(qcm2290_pp),
	.pingpong = qcm2290_pp,
	.intf_count = ARRAY_SIZE(qcm2290_intf),
	.intf = qcm2290_intf,
	.vbif_count = ARRAY_SIZE(sdm845_vbif),
	.vbif = sdm845_vbif,
	.perf = &qcm2290_perf_data,
	.mdss_irqs = IRQ_SC7180_MASK,
};

static const struct dpu_mdss_hw_cfg_handler cfg_handler[] = {
	{ .hw_rev = DPU_HW_VER_300, .dpu_cfg = &msm8998_dpu_cfg},
	{ .hw_rev = DPU_HW_VER_301, .dpu_cfg = &msm8998_dpu_cfg},
	{ .hw_rev = DPU_HW_VER_400, .dpu_cfg = &sdm845_dpu_cfg},
	{ .hw_rev = DPU_HW_VER_401, .dpu_cfg = &sdm845_dpu_cfg},
	{ .hw_rev = DPU_HW_VER_500, .dpu_cfg = &sm8150_dpu_cfg},
	{ .hw_rev = DPU_HW_VER_501, .dpu_cfg = &sm8150_dpu_cfg},
	{ .hw_rev = DPU_HW_VER_510, .dpu_cfg = &sc8180x_dpu_cfg},
	{ .hw_rev = DPU_HW_VER_600, .dpu_cfg = &sm8250_dpu_cfg},
	{ .hw_rev = DPU_HW_VER_620, .dpu_cfg = &sc7180_dpu_cfg},
	{ .hw_rev = DPU_HW_VER_630, .dpu_cfg = &sm6115_dpu_cfg},
	{ .hw_rev = DPU_HW_VER_650, .dpu_cfg = &qcm2290_dpu_cfg},
	{ .hw_rev = DPU_HW_VER_700, .dpu_cfg = &sm8350_dpu_cfg},
	{ .hw_rev = DPU_HW_VER_720, .dpu_cfg = &sc7280_dpu_cfg},
	{ .hw_rev = DPU_HW_VER_800, .dpu_cfg = &sc8280xp_dpu_cfg},
	{ .hw_rev = DPU_HW_VER_810, .dpu_cfg = &sm8450_dpu_cfg},
	{ .hw_rev = DPU_HW_VER_900, .dpu_cfg = &sm8550_dpu_cfg},
};

const struct dpu_mdss_cfg *dpu_hw_catalog_init(u32 hw_rev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cfg_handler); i++) {
		if (cfg_handler[i].hw_rev == hw_rev)
			return cfg_handler[i].dpu_cfg;
	}

	DPU_ERROR("unsupported chipset id:%X\n", hw_rev);

	return ERR_PTR(-ENODEV);
}

