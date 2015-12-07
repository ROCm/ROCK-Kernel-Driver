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

#include <linux/string.h>
#include <linux/acpi.h>

#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <drm/amdgpu_drm.h>
#include "amdgpu.h"
#include "dal_services.h"
#include "amdgpu_dm.h"
#include "amdgpu_dm_irq.h"
#include "amdgpu_dm_types.h"
#include "amdgpu_pm.h"

/*
#include "logger_interface.h"
#include "acpimethod_atif.h"
#include "amdgpu_powerplay.h"
#include "amdgpu_notifications.h"
*/

/* if the pointer is not NULL, the allocated memory is zeroed */
void *dc_service_alloc(struct dc_context *ctx, uint32_t size)
{
	return kzalloc(size, GFP_KERNEL);
}

/* Reallocate memory. The contents will remain unchanged.*/
void *dc_service_realloc(struct dc_context *ctx, const void *ptr, uint32_t size)
{
	return krealloc(ptr, size, GFP_KERNEL);
}

void dc_service_memmove(void *dst, const void *src, uint32_t size)
{
	memmove(dst, src, size);
}

void dc_service_free(struct dc_context *ctx, void *p)
{
	kfree(p);
}

void dc_service_memset(void *p, int32_t c, uint32_t count)
{
	memset(p, c, count);
}

int32_t dal_memcmp(const void *p1, const void *p2, uint32_t count)
{
	return memcmp(p1, p2, count);
}

int32_t dal_strncmp(const int8_t *p1, const int8_t *p2, uint32_t count)
{
	return strncmp(p1, p2, count);
}

void dc_service_sleep_in_milliseconds(struct dc_context *ctx, uint32_t milliseconds)
{
	if (milliseconds >= 20)
		msleep(milliseconds);
	else
		usleep_range(milliseconds*1000, milliseconds*1000+1);
}

void dal_delay_in_nanoseconds(uint32_t nanoseconds)
{
	ndelay(nanoseconds);
}

void dc_service_delay_in_microseconds(struct dc_context *ctx, uint32_t microseconds)
{
	udelay(microseconds);
}

/******************************************************************************
 * IRQ Interfaces.
 *****************************************************************************/

void dal_register_timer_interrupt(
	struct dc_context *ctx,
	struct dc_timer_interrupt_params *int_params,
	interrupt_handler ih,
	void *args)
{
	struct amdgpu_device *adev = ctx->driver_context;

	if (!adev || !int_params) {
		DRM_ERROR("DM_IRQ: invalid input!\n");
		return;
	}

	if (int_params->int_context != INTERRUPT_LOW_IRQ_CONTEXT) {
		/* only low irq ctx is supported. */
		DRM_ERROR("DM_IRQ: invalid context: %d!\n",
				int_params->int_context);
		return;
	}

	amdgpu_dm_irq_register_timer(adev, int_params, ih, args);
}

void dal_isr_acquire_lock(struct dc_context *ctx)
{
	/*TODO*/
}

void dal_isr_release_lock(struct dc_context *ctx)
{
	/*TODO*/
}

/******************************************************************************
 * End-of-IRQ Interfaces.
 *****************************************************************************/

bool dal_get_platform_info(struct dc_context *ctx,
			struct platform_info_params *params)
{
	/*TODO*/
	return false;
}

/**** power component interfaces ****/

bool dc_service_pp_pre_dce_clock_change(
		struct dc_context *ctx,
		struct dal_to_power_info *input,
		struct power_to_dal_info *output)
{
	/*TODO*/
	return false;
}

bool dc_service_pp_apply_display_requirements(
		const struct dc_context *ctx,
		const struct dc_pp_display_configuration *pp_display_cfg)
{
#ifdef CONFIG_DRM_AMD_POWERPLAY
	struct amdgpu_device *adev = ctx->driver_context;

	if (adev->pm.dpm_enabled) {

		memset(&adev->pm.pm_display_cfg, 0,
				sizeof(adev->pm.pm_display_cfg));

		adev->pm.pm_display_cfg.cpu_cc6_disable =
			pp_display_cfg->cpu_cc6_disable;

		adev->pm.pm_display_cfg.cpu_pstate_disable =
			pp_display_cfg->cpu_pstate_disable;

		adev->pm.pm_display_cfg.cpu_pstate_separation_time =
			pp_display_cfg->cpu_pstate_separation_time;

		adev->pm.pm_display_cfg.nb_pstate_switch_disable =
			pp_display_cfg->nb_pstate_switch_disable;

		/* TODO: complete implementation of
		 * amd_powerplay_display_configuration_change().
		 * Follow example of:
		 * PHM_StoreDALConfigurationData - powerplay\hwmgr\hardwaremanager.c
		 * PP_IRI_DisplayConfigurationChange - powerplay\eventmgr\iri.c */
		amd_powerplay_display_configuration_change(
				adev->powerplay.pp_handle,
				&adev->pm.pm_display_cfg);

		/* TODO: replace by a separate call to 'apply display cfg'? */
		amdgpu_pm_compute_clocks(adev);
	}

	return true;
#else
	return false;
#endif
}

bool dc_service_get_system_clocks_range(
		const struct dc_context *ctx,
		struct dal_system_clock_range *sys_clks)
{
#ifdef CONFIG_DRM_AMD_POWERPLAY
	struct amdgpu_device *adev = ctx->driver_context;
#endif

	/* Default values, in case PPLib is not compiled-in. */
	sys_clks->max_mclk = 80000;
	sys_clks->min_mclk = 80000;

	sys_clks->max_sclk = 60000;
	sys_clks->min_sclk = 30000;

#ifdef CONFIG_DRM_AMD_POWERPLAY
	if (adev->pm.dpm_enabled) {
		sys_clks->max_mclk = amdgpu_dpm_get_mclk(adev, false);
		sys_clks->min_mclk = amdgpu_dpm_get_mclk(adev, true);

		sys_clks->max_sclk = amdgpu_dpm_get_sclk(adev, false);
		sys_clks->min_sclk = amdgpu_dpm_get_sclk(adev, true);
	}
#endif

	return true;
}


bool dc_service_pp_get_clock_levels_by_type(
		const struct dc_context *ctx,
		enum dc_pp_clock_type clk_type,
		struct dc_pp_clock_levels *clks)
{
/*
 #ifdef CONFIG_DRM_AMD_POWERPLAY
	if(amd_powerplay_get_clocks_by_type(adev->powerplay.pp_handle,
		(int)clk_type, (void *)clks) == fail
		PPlib return clocks in tens of kHz
		divide by 10 & push to the clks which kHz
#endif
*/
	uint32_t disp_clks_in_khz[8] = {
	300000, 411430, 480000, 533340, 626090, 626090, 626090, 626090 };
	uint32_t sclks_in_khz[8] = {
	200000, 266670, 342860, 411430, 450000, 514290, 576000, 626090 };
	uint32_t mclks_in_khz[8] = { 333000, 800000 };

	switch (clk_type) {
	case DC_PP_CLOCK_TYPE_DISPLAY_CLK:
		clks->num_levels = 8;
		dc_service_memmove(clks->clocks_in_khz, disp_clks_in_khz,
				sizeof(disp_clks_in_khz));
		break;
	case DC_PP_CLOCK_TYPE_ENGINE_CLK:
		clks->num_levels = 8;
		dc_service_memmove(clks->clocks_in_khz, sclks_in_khz,
				sizeof(sclks_in_khz));
		break;
	case DC_PP_CLOCK_TYPE_MEMORY_CLK:
		clks->num_levels = 2;
		dc_service_memmove(clks->clocks_in_khz, mclks_in_khz,
				sizeof(mclks_in_khz));
		break;
	default:
		return false;
	}

#ifdef CONFIG_DRM_AMD_POWERPLAY
	if (clk_type ==  DC_PP_CLOCK_TYPE_ENGINE_CLK ||
		clk_type ==  DC_PP_CLOCK_TYPE_DISPLAY_CLK) {

		struct amdgpu_device *adev = ctx->driver_context;
		struct amd_pp_dal_clock_info info = {0};

		if (0 == amd_powerplay_get_display_power_level(
				adev->powerplay.pp_handle, &info) &&
				info.level < clks->num_levels) {
		/*if the max possible power level less then use smaller*/
			clks->num_levels = info.level;
		}
	}
#endif
	return true;
}
/**** end of power component interfaces ****/


/* Calls to notification */

void dal_notify_setmode_complete(struct dc_context *ctx,
	uint32_t h_total,
	uint32_t v_total,
	uint32_t h_active,
	uint32_t v_active,
	uint32_t pix_clk_in_khz)
{
	/*TODO*/
}
/* End of calls to notification */

long dal_get_pid(void)
{
	return current->pid;
}

long dal_get_tgid(void)
{
	return current->tgid;
}
