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

/*
 * Includes
 */

#include "dm_services.h"

#include "include/asic_capability_interface.h"
#include "include/asic_capability_types.h"
#include "include/dal_types.h"
#include "include/dal_asic_id.h"
#include "include/logger_interface.h"
#include "hawaii_asic_capability.h"

#include "atom.h"

#include "dce/dce_8_0_d.h"
#include "gmc/gmc_7_1_d.h"

/*
 * Sea Islands (CI) ASIC capability.
 *
 * dal_hawaii_asic_capability_create
 *
 * Create and initiate hawaii capability.
 */
void dal_hawaii_asic_capability_create(struct asic_capability *cap,
		struct hw_asic_id *init)
{
	uint32_t mc_seq_misc0;

	/* ASIC data */
	cap->data[ASIC_DATA_CONTROLLERS_NUM] = 6;
	cap->data[ASIC_DATA_FUNCTIONAL_CONTROLLERS_NUM] = 6;
	cap->data[ASIC_DATA_DIGFE_NUM] = 6;
	cap->data[ASIC_DATA_LINEBUFFER_NUM] = 6;
	cap->data[ASIC_DATA_MAX_COFUNC_NONDP_DISPLAYS] = 2;
	cap->data[ASIC_DATA_MIN_DISPCLK_FOR_UNDERSCAN] = 300000;

	cap->data[ASIC_DATA_DCE_VERSION] = 0x80; /* DCE 8.0 */

	/* Pixel RAM is 1712 entries of 144 bits each or
	 * in other words 246528 bits. */
	cap->data[ASIC_DATA_LINEBUFFER_SIZE] = 1712 * 144;
	cap->data[ASIC_DATA_DRAM_BANDWIDTH_EFFICIENCY] = 70;
	cap->data[ASIC_DATA_CLOCKSOURCES_NUM] = 3;
	cap->data[ASIC_DATA_MC_LATENCY] = 5000; /* units of ns */

	/* StutterModeEnhanced; Quad DMIF Buffer */
	cap->data[ASIC_DATA_STUTTERMODE] = 0x2002;
	cap->data[ASIC_DATA_PATH_NUM_PER_DPMST_CONNECTOR] = 4;
	cap->data[ASIC_DATA_VIEWPORT_PIXEL_GRANULARITY] = 2;

	/* 3 HDMI support by default */
	cap->data[ASIC_DATA_SUPPORTED_HDMI_CONNECTION_NUM] = 3;

	cap->data[ASIC_DATA_DEFAULT_I2C_SPEED_IN_KHZ] = 40;

	mc_seq_misc0 = dm_read_reg(cap->ctx, mmMC_SEQ_MISC0);

	switch (mc_seq_misc0 & MC_MISC0__MEMORY_TYPE_MASK) {
	case MC_MISC0__MEMORY_TYPE__GDDR1:
	case MC_MISC0__MEMORY_TYPE__DDR2:
	case MC_MISC0__MEMORY_TYPE__DDR3:
	case MC_MISC0__MEMORY_TYPE__GDDR3:
	case MC_MISC0__MEMORY_TYPE__GDDR4:
		cap->data[ASIC_DATA_MEMORYTYPE_MULTIPLIER] = 2;
		break;
	case MC_MISC0__MEMORY_TYPE__GDDR5:
		cap->data[ASIC_DATA_MEMORYTYPE_MULTIPLIER] = 4;
		break;
	default:
		dal_logger_write(cap->ctx->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_MASK_ALL,
			"%s:Unrecognized memory type!", __func__);
		cap->data[ASIC_DATA_MEMORYTYPE_MULTIPLIER] = 2;
		break;
	}

	/* ASIC stereo 3D capability */
	cap->stereo_3d_caps.INTERLEAVE = true;
	cap->stereo_3d_caps.HDMI_FRAME_PACK = true;
	cap->stereo_3d_caps.INTERLACE_FRAME_PACK = true;
	cap->stereo_3d_caps.DISPLAYPORT_FRAME_PACK = true;
	cap->stereo_3d_caps.DISPLAYPORT_FRAME_ALT = true;
	cap->stereo_3d_caps.DISPLAY_BASED_ON_WS = true;

	/* ASIC basic capability */
	cap->caps.DP_MST_SUPPORTED = true;
	cap->caps.PANEL_SELF_REFRESH_SUPPORTED = true;

	cap->caps.MIRABILIS_SUPPORTED = true;
	cap->caps.MIRABILIS_ENABLED_BY_DEFAULT = true;

	/* Remap device tag IDs when patching VBIOS. */
	cap->caps.DEVICE_TAG_REMAP_SUPPORTED = true;

	/* Report headless if no OPM attached (with MXM connectors present). */
	cap->caps.HEADLESS_NO_OPM_SUPPORTED = true;

	cap->caps.HPD_CHECK_FOR_EDID = true;
	cap->caps.NO_VCC_OFF_HPD_POLLING = true;

	/* true will hang the system! */
	cap->caps.DFSBYPASS_DYNAMIC_SUPPORT = false;

	/* Do w/a on CI A0 by default */
	if (init->hw_internal_rev == CI_BONAIRE_M_A0)
		cap->bugs.LB_WA_IS_SUPPORTED = true;

	/* Apply MC Tuning for Hawaii */
	if (ASIC_REV_IS_HAWAII_P(init->hw_internal_rev))
		cap->caps.NEED_MC_TUNING = true;

	/* DCE6.0 and DCE8.0 has a HW issue when accessing registers
	 * from ROM block. When there is a W access following R or W access
	 * right after (no more than couple of cycles)  the first W access
	 * sometimes is not executed (in rate of about once per 100K tries).
	 * It creates problems in different scenarios of FL setup. */
	cap->bugs.ROM_REGISTER_ACCESS = true;

	/* VCE is supported */
	cap->caps.VCE_SUPPORTED = true;
}
