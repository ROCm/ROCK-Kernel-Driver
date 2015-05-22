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

#include "amdgpu.h"
#include "atom.h"
#include "amdgpu_acp.h"
#include "amd_gnb_bus.h"
#include "acp_gfx_if.h"

static int acp_early_init(void *handle)
{
	return 0;
}

static int acp_sw_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	adev->acp.parent = adev->dev;

	adev->acp.cgs_device =
		amdgpu_cgs_create_device(adev);
	if (!adev->acp.cgs_device)
		return -EINVAL;

	return 0;
}

static int acp_sw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (adev->acp.cgs_device)
		amdgpu_cgs_destroy_device(adev->acp.cgs_device);

	return 0;
}

/**
 * acp_hw_init - start and test UVD block
 *
 * @adev: amdgpu_device pointer
 *
 */
static int acp_hw_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int r;
	const struct amdgpu_ip_block_version *ip_version =
		amdgpu_get_ip_block(adev, AMD_IP_BLOCK_TYPE_ACP);

	if (!ip_version)
		return -EINVAL;

	r = amd_acp_hw_init(adev->acp.cgs_device,
			    ip_version->major, ip_version->minor,
			    &adev->acp.private);
	/* -ENODEV means board uses AZ rather than ACP */
	if (r == -ENODEV)
		return 0;
	else if (r)
		return r;
	r = amd_gnb_bus_device_init(&adev->acp.acp_pcm_dev,
				    AMD_GNB_IP_ACP_PCM,
				    "acp_pcm_dev",
				    adev->acp.private,
				    adev->acp.parent);
	if (r) {
		amd_acp_hw_fini(adev->acp.private);
		return r;
	}

	return 0;
}

/**
 * acp_hw_fini - stop the hardware block
 *
 * @adev: amdgpu_device pointer
 *
 */
static int acp_hw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (adev->acp.private) {
		amd_acp_hw_fini(adev->acp.private);
		amd_gnb_bus_unregister_device(&adev->acp.acp_pcm_dev);
	}

	return 0;
}

static int acp_suspend(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (adev->acp.private)
		amd_acp_suspend(adev->acp.private);

	return 0;
}

static int acp_resume(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (adev->acp.private)
		amd_acp_resume(adev->acp.private);

	return 0;
}

static bool acp_is_idle(void *handle)
{
	return true;
}

static int acp_wait_for_idle(void *handle)
{
	return 0;
}

static int acp_soft_reset(void *handle)
{
	return 0;
}

static void acp_print_status(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	dev_info(adev->dev, "ACP STATUS\n");
}

static int acp_set_clockgating_state(void *handle,
				     enum amd_clockgating_state state)
{
	return 0;
}

static int acp_set_powergating_state(void *handle,
				     enum amd_powergating_state state)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	/* This doesn't actually powergate the ACP block.
	 * That's done in the dpm code via the SMC.  This
	 * just re-inits the block as necessary.  The actual
	 * gating still happens in the dpm code.  We should
	 * revisit this when there is a cleaner line between
	 * the smc and the hw blocks
	 */
	if (state == AMD_PG_STATE_GATE) {
		if (adev->acp.private)
			amd_acp_suspend(adev->acp.private);
	} else {
		if (adev->acp.private)
			amd_acp_resume(adev->acp.private);
	}
	return 0;
}

const struct amd_ip_funcs acp_ip_funcs = {
	.early_init = acp_early_init,
	.late_init = NULL,
	.sw_init = acp_sw_init,
	.sw_fini = acp_sw_fini,
	.hw_init = acp_hw_init,
	.hw_fini = acp_hw_fini,
	.suspend = acp_suspend,
	.resume = acp_resume,
	.is_idle = acp_is_idle,
	.wait_for_idle = acp_wait_for_idle,
	.soft_reset = acp_soft_reset,
	.print_status = acp_print_status,
	.set_clockgating_state = acp_set_clockgating_state,
	.set_powergating_state = acp_set_powergating_state,
};
