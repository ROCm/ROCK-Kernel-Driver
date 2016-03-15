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

#include "include/logger_interface.h"

#include "include/asic_capability_interface.h"
#include "include/asic_capability_types.h"
#include "include/dal_types.h"
#include "include/dal_asic_id.h"

#if defined(CONFIG_DRM_AMD_DAL_DCE8_0)
#include "hawaii_asic_capability.h"
#endif

#if defined(CONFIG_DRM_AMD_DAL_DCE10_0)
#include "tonga_asic_capability.h"
#endif

#if defined(CONFIG_DRM_AMD_DAL_DCE11_0)
#include "carrizo_asic_capability.h"
#endif

#if defined(CONFIG_DRM_AMD_DAL_DCE11_2)
#include "polaris10_asic_capability.h"
#endif

/*
 * Initializes asic_capability instance.
 */
static bool construct(
	struct asic_capability *cap,
	struct hw_asic_id *init,
	struct dc_context *ctx)
{
	bool asic_supported = false;

	cap->ctx = ctx;
	memset(cap->data, 0, sizeof(cap->data));

	/* ASIC data */
	cap->data[ASIC_DATA_VRAM_TYPE] = init->vram_type;
	cap->data[ASIC_DATA_VRAM_BITWIDTH] = init->vram_width;
	cap->data[ASIC_DATA_FEATURE_FLAGS] = init->feature_flags;
	cap->runtime_flags = init->runtime_flags;
	cap->data[ASIC_DATA_REVISION_ID] = init->hw_internal_rev;
	cap->data[ASIC_DATA_MAX_UNDERSCAN_PERCENTAGE] = 10;
	cap->data[ASIC_DATA_VIEWPORT_PIXEL_GRANULARITY] = 4;
	cap->data[ASIC_DATA_SUPPORTED_HDMI_CONNECTION_NUM] = 1;
	cap->data[ASIC_DATA_NUM_OF_VIDEO_PLANES] = 0;
	cap->data[ASIC_DATA_DEFAULT_I2C_SPEED_IN_KHZ] = 25;

	/* ASIC basic capability */
	cap->caps.UNDERSCAN_FOR_HDMI_ONLY = true;
	cap->caps.SUPPORT_CEA861E_FINAL = true;
	cap->caps.MIRABILIS_SUPPORTED = false;
	cap->caps.MIRABILIS_ENABLED_BY_DEFAULT = false;
	cap->caps.WIRELESS_LIMIT_TO_720P = false;
	cap->caps.WIRELESS_FULL_TIMING_ADJUSTMENT = false;
	cap->caps.WIRELESS_TIMING_ADJUSTMENT = true;
	cap->caps.WIRELESS_COMPRESSED_AUDIO = false;
	cap->caps.VCE_SUPPORTED = false;
	cap->caps.HPD_CHECK_FOR_EDID = false;
	cap->caps.NO_VCC_OFF_HPD_POLLING = false;
	cap->caps.NEED_MC_TUNING = false;
	cap->caps.SUPPORT_8BPP = true;

	/* ASIC stereo 3D capability */
	cap->stereo_3d_caps.SUPPORTED = true;

	switch (init->chip_family) {
	case FAMILY_CI:
#if defined(CONFIG_DRM_AMD_DAL_DCE8_0)
		dal_hawaii_asic_capability_create(cap, init);
		asic_supported = true;
#endif
		break;

	case FAMILY_KV:
		if (ASIC_REV_IS_KALINDI(init->hw_internal_rev) ||
			ASIC_REV_IS_BHAVANI(init->hw_internal_rev)) {
		} else {
		}
		break;

	case FAMILY_CZ:
#if defined(CONFIG_DRM_AMD_DAL_DCE11_0)
		carrizo_asic_capability_create(cap, init);
		asic_supported = true;
#endif
		break;

	case FAMILY_VI:
#if defined(CONFIG_DRM_AMD_DAL_DCE10_0)
		if (ASIC_REV_IS_TONGA_P(init->hw_internal_rev) ||
				ASIC_REV_IS_FIJI_P(init->hw_internal_rev)) {
			tonga_asic_capability_create(cap, init);
			asic_supported = true;
			break;
		}
#endif
#if defined(CONFIG_DRM_AMD_DAL_DCE11_2)
		if (ASIC_REV_IS_POLARIS10_P(init->hw_internal_rev) ||
				ASIC_REV_IS_POLARIS11_M(init->hw_internal_rev)) {
			polaris10_asic_capability_create(cap, init);
			asic_supported = true;
		}
#endif
		break;

	default:
		/* unsupported "chip_family" */
		break;
	}

	if (false == asic_supported) {
		dal_logger_write(ctx->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_MASK_ALL,
			"%s: ASIC not supported!\n", __func__);
	}

	return asic_supported;
}

static void destruct(
	struct asic_capability *cap)
{
	/* nothing to do (yet?) */
}

/*
 * dal_asic_capability_create
 *
 * Creates asic capability based on DCE version.
 */
struct asic_capability *dal_asic_capability_create(
	struct hw_asic_id *init,
	struct dc_context *ctx)
{
	struct asic_capability *cap;

	if (!init) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	cap = dm_alloc(sizeof(struct asic_capability));

	if (!cap) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	if (construct(cap, init, ctx))
		return cap;

	BREAK_TO_DEBUGGER();

	dm_free(cap);

	return NULL;
}

/*
 * dal_asic_capability_destroy
 *
 * Destroy allocated memory.
 */
void dal_asic_capability_destroy(
	struct asic_capability **cap)
{
	if (!cap) {
		BREAK_TO_DEBUGGER();
		return;
	}

	if (!*cap) {
		BREAK_TO_DEBUGGER();
		return;
	}

	destruct(*cap);

	dm_free(*cap);

	*cap = NULL;
}
