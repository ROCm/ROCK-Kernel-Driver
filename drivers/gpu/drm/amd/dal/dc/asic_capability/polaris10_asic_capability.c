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

#include "include/asic_capability_interface.h"
#include "include/asic_capability_types.h"

#include "polaris10_asic_capability.h"

#include "atom.h"
#include "dce/dce_11_2_d.h"
#include "dce/dce_11_2_sh_mask.h"
#include "dal_asic_id.h"

#define ixVCE_HARVEST_FUSE_MACRO__ADDRESS 0xC0014074

/*
 * carrizo_asic_capability_create
 *
 * Create and initiate Carrizo capability.
 */
void polaris10_asic_capability_create(struct asic_capability *cap,
	struct hw_asic_id *init)
{
	uint32_t e_fuse_setting;
	/* ASIC data */
	if (ASIC_REV_IS_POLARIS11_M(init->hw_internal_rev)) {
		cap->data[ASIC_DATA_CONTROLLERS_NUM] = 5;
		cap->data[ASIC_DATA_FUNCTIONAL_CONTROLLERS_NUM] = 5;
		cap->data[ASIC_DATA_LINEBUFFER_NUM] = 5;
		cap->data[ASIC_DATA_DIGFE_NUM] = 5;
		cap->data[ASIC_DATA_CLOCKSOURCES_NUM] = 7;
		cap->data[ASIC_DATA_MAX_COFUNC_NONDP_DISPLAYS] = 5;
		cap->data[ASIC_DATA_SUPPORTED_HDMI_CONNECTION_NUM] = 5;
	} else {
		cap->data[ASIC_DATA_CONTROLLERS_NUM] = 6;
		cap->data[ASIC_DATA_FUNCTIONAL_CONTROLLERS_NUM] = 6;
		cap->data[ASIC_DATA_LINEBUFFER_NUM] = 6;
		cap->data[ASIC_DATA_DIGFE_NUM] = 6;
		cap->data[ASIC_DATA_CLOCKSOURCES_NUM] = 8;
		cap->data[ASIC_DATA_MAX_COFUNC_NONDP_DISPLAYS] = 6;
		cap->data[ASIC_DATA_SUPPORTED_HDMI_CONNECTION_NUM] = 6;
	}

	cap->data[ASIC_DATA_PATH_NUM_PER_DPMST_CONNECTOR] = 4;
	cap->data[ASIC_DATA_DCE_VERSION] = 0x112; /* DCE 11 */
	cap->data[ASIC_DATA_LINEBUFFER_SIZE] = 5124 * 144;
	cap->data[ASIC_DATA_DRAM_BANDWIDTH_EFFICIENCY] = 70;

	cap->data[ASIC_DATA_MC_LATENCY] = 3000;
	cap->data[ASIC_DATA_STUTTERMODE] = 0x200A;
	cap->data[ASIC_DATA_VIEWPORT_PIXEL_GRANULARITY] = 2;

	cap->data[ASIC_DATA_MEMORYTYPE_MULTIPLIER] = 4;
	cap->data[ASIC_DATA_DEFAULT_I2C_SPEED_IN_KHZ] = 100;
	cap->data[ASIC_DATA_NUM_OF_VIDEO_PLANES] = 0;

	cap->data[ASIC_DATA_MIN_DISPCLK_FOR_UNDERSCAN] = 300000;

	/* ASIC basic capability */
	cap->caps.IS_FUSION = true;
	cap->caps.DP_MST_SUPPORTED = true;
	cap->caps.PANEL_SELF_REFRESH_SUPPORTED = true;
	cap->caps.MIRABILIS_SUPPORTED = true;
	cap->caps.NO_VCC_OFF_HPD_POLLING = true;
	cap->caps.VCE_SUPPORTED = true;
	cap->caps.HPD_CHECK_FOR_EDID = true;
	cap->caps.DFSBYPASS_DYNAMIC_SUPPORT = true;
	cap->caps.SUPPORT_8BPP = false;

	/* ASIC stereo 3d capability */
	cap->stereo_3d_caps.DISPLAY_BASED_ON_WS = true;
	cap->stereo_3d_caps.HDMI_FRAME_PACK = true;
	cap->stereo_3d_caps.INTERLACE_FRAME_PACK = true;
	cap->stereo_3d_caps.DISPLAYPORT_FRAME_PACK = true;
	cap->stereo_3d_caps.DISPLAYPORT_FRAME_ALT = true;
	cap->stereo_3d_caps.INTERLEAVE = true;

	e_fuse_setting = dm_read_index_reg(cap->ctx,CGS_IND_REG__SMC, ixVCE_HARVEST_FUSE_MACRO__ADDRESS);

	/* Bits [28:27]*/
	switch ((e_fuse_setting >> 27) & 0x3) {
	case 0:
		/*both VCE engine are working*/
		cap->caps.VCE_SUPPORTED = true;
		cap->caps.WIRELESS_TIMING_ADJUSTMENT = false;
		/*TODO:
		cap->caps.wirelessLowVCEPerformance = false;
		m_AsicCaps.vceInstance0Enabled = true;
		m_AsicCaps.vceInstance1Enabled = true;*/
		cap->caps.NEED_MC_TUNING = true;
		break;

	case 1:
		cap->caps.VCE_SUPPORTED = true;
		cap->caps.WIRELESS_TIMING_ADJUSTMENT = true;
		/*TODO:
		m_AsicCaps.wirelessLowVCEPerformance = false;
		m_AsicCaps.vceInstance1Enabled = true;*/
		cap->caps.NEED_MC_TUNING = true;
		break;

	case 2:
		cap->caps.VCE_SUPPORTED = true;
		cap->caps.WIRELESS_TIMING_ADJUSTMENT = true;
		/*TODO:
		m_AsicCaps.wirelessLowVCEPerformance = false;
		m_AsicCaps.vceInstance0Enabled = true;*/
		cap->caps.NEED_MC_TUNING = true;
		break;

	case 3:
		/* VCE_DISABLE = 0x3  - both VCE
		 * instances are in harvesting,
		 * no VCE supported any more.
		 */
		cap->caps.VCE_SUPPORTED = false;
		break;

	default:
		break;
	}
}
