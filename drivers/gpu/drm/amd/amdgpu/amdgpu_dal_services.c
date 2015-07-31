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
#include "include/dal_interface.h"

/*
#include "logger_interface.h"
#include "acpimethod_atif.h"
#include "amdgpu_powerplay.h"
#include "amdgpu_notifications.h"
*/

/******************************************************************************
 * IRQ Interfaces.
 *****************************************************************************/

void dal_register_timer_interrupt(
	struct dal_context *context,
	struct dal_timer_interrupt_params *int_params,
	interrupt_handler ih,
	void *args)
{
	struct amdgpu_device *adev = context->driver_context;

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

irq_handler_idx dal_register_interrupt(
	struct dal_context *context,
	struct dal_interrupt_params *int_params,
	interrupt_handler ih,
	void *handler_args)
{
	struct amdgpu_device *adev = context->driver_context;

	if (NULL == int_params || NULL == ih) {
		DRM_ERROR("DM_IRQ: invalid input!\n");
		return DAL_INVALID_IRQ_HANDLER_IDX;
	}

	if (int_params->int_context >= INTERRUPT_CONTEXT_NUMBER) {
		DRM_ERROR("DM_IRQ: invalid context: %d!\n",
				int_params->int_context);
		return DAL_INVALID_IRQ_HANDLER_IDX;
	}

	if (!DAL_VALID_IRQ_SRC_NUM(int_params->irq_source)) {
		DRM_ERROR("DM_IRQ: invalid irq_source: %d!\n",
				int_params->irq_source);
		return DAL_INVALID_IRQ_HANDLER_IDX;
	}

	return amdgpu_dm_irq_register_interrupt(adev, int_params, ih,
			handler_args);
}

void dal_unregister_interrupt(
	struct dal_context *context,
	enum dal_irq_source irq_source,
	irq_handler_idx handler_idx)
{
	struct amdgpu_device *adev = context->driver_context;

	if (DAL_INVALID_IRQ_HANDLER_IDX == handler_idx) {
		DRM_ERROR("DM_IRQ: invalid handler_idx==NULL!\n");
		return;
	}

	if (!DAL_VALID_IRQ_SRC_NUM(irq_source)) {
		DRM_ERROR("DM_IRQ: invalid irq_source:%d!\n", irq_source);
		return;
	}

	amdgpu_dm_irq_unregister_interrupt(adev, irq_source, handler_idx);
}


void dal_isr_acquire_lock(struct dal_context *context)
{
	/*TODO*/
}

void dal_isr_release_lock(struct dal_context *context)
{
	/*TODO*/
}

/******************************************************************************
 * End-of-IRQ Interfaces.
 *****************************************************************************/

bool dal_get_platform_info(struct dal_context *dal_context,
			struct platform_info_params *params)
{
	/*TODO*/
	return false;
}

/* Next calls are to power component */
bool dal_pp_pre_dce_clock_change(struct dal_context *ctx,
				struct dal_to_power_info *input,
				struct power_to_dal_info *output)
{
	/*TODO*/
	return false;
}

bool dal_pp_post_dce_clock_change(struct dal_context *ctx)
{
	/*TODO*/
	return false;
}

bool dal_get_system_clocks_range(struct dal_context *ctx,
				struct dal_system_clock_range *sys_clks)
{
	/*TODO*/
	return false;
}


bool dal_pp_set_display_clock(struct dal_context *ctx,
			     struct dal_to_power_dclk *dclk)
{
	/* TODO: need power component to provide appropriate interface */
	return false;
}

/* end of calls to power component */

/* Calls to notification */

/* dal_notify_hotplug
 *
 * Notify display manager for hotplug event
 *
 * @param
 * struct dal_context *dal_context - [in] pointer to specific DAL context
 *
 * @return
 * void
 * */
void dal_notify_hotplug(
	struct dal_context *ctx,
	uint32_t display_index,
	bool is_connected)
{
	struct amdgpu_device *adev = ctx->driver_context;
	struct drm_device *dev = adev->ddev;
	struct drm_connector *connector = NULL;
	struct amdgpu_connector *aconnector = NULL;

	/* 1. Update status of drm connectors
	 * 2. Send a uevent and let userspace tell us what to do */
	list_for_each_entry(connector,
		&dev->mode_config.connector_list, head) {
		aconnector = to_amdgpu_connector(connector);

		/*aconnector->connector_id means display_index*/
		if (aconnector->connector_id != display_index)
			continue;

		if (is_connected) {
			drm_mode_connector_update_edid_property(
				connector,
				(struct edid *)
				dal_get_display_edid(
					adev->dm.dal,
					display_index,
					NULL));
		} else
			drm_mode_connector_update_edid_property(
				connector, NULL);

		break;
	}

	drm_helper_hpd_irq_event(dev);
}

void dal_notify_capability_change(
	struct dal_context *ctx,
	uint32_t display_index)
{
	struct amdgpu_device *adev = ctx->driver_context;
	struct drm_device *dev = adev->ddev;
	struct drm_connector *connector = NULL;
	struct amdgpu_connector *aconnector = NULL;

	/* 1. Update status of drm connectors
	 * 2. Send a uevent and let userspace tell us what to do */

	list_for_each_entry(connector,
		&dev->mode_config.connector_list, head) {
		aconnector = to_amdgpu_connector(connector);

		/*aconnector->connector_id means display_index*/
		if (aconnector->connector_id == display_index) {
			drm_mode_connector_update_edid_property(
				connector,
				(struct edid *)
				dal_get_display_edid(
					adev->dm.dal,
					display_index,
					NULL));
		}
	}

	drm_kms_helper_hotplug_event(dev);
}

void dal_notify_setmode_complete(struct dal_context *ctx,
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
