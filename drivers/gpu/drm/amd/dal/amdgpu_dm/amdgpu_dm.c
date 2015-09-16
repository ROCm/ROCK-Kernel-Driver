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

#include "dal_services_types.h"
#include "include/dal_interface.h"
#include "include/mode_query_interface.h"

#include "vid.h"
#include "amdgpu.h"
#include "atom.h"
#include "amdgpu_dm.h"
#include "amdgpu_dm_types.h"

#include "amd_shared.h"
#include "amdgpu_dm_irq.h"

#include "dce/dce_11_0_d.h"
#include "dce/dce_11_0_sh_mask.h"
#include "dce/dce_11_0_enum.h"

#include "oss/oss_3_0_d.h"
#include "oss/oss_3_0_sh_mask.h"
#include "gmc/gmc_8_1_d.h"
#include "gmc/gmc_8_1_sh_mask.h"



#include <linux/module.h>
#include <linux/moduleparam.h>


/* Define variables here
 * These values will be passed to DAL for feature enable purpose
 * Disable ALL for HDMI light up
 * TODO: follow up if need this mechanism*/
struct dal_override_parameters display_param = {
	.bool_param_enable_mask = 0,
	.bool_param_values = 0,
	.int_param_values[DAL_PARAM_MAX_COFUNC_NON_DP_DISPLAYS] = DAL_PARAM_INVALID_INT,
	.int_param_values[DAL_PARAM_DRR_SUPPORT] = DAL_PARAM_INVALID_INT,
};

/* Debug facilities */
#define AMDGPU_DM_NOT_IMPL(fmt, ...) \
	DRM_INFO("DM_NOT_IMPL: " fmt, ##__VA_ARGS__)

static u32 dm_vblank_get_counter(struct amdgpu_device *adev, int crtc)
{
	if (crtc >= adev->mode_info.num_crtc)
		return 0;
	else
		return dal_get_vblank_counter(adev->dm.dal, crtc);
}

static int dm_crtc_get_scanoutpos(struct amdgpu_device *adev, int crtc,
					u32 *vbl, u32 *position)
{
	if ((crtc < 0) || (crtc >= adev->mode_info.num_crtc))
		return -EINVAL;

	dal_get_crtc_scanoutpos(adev->dm.dal, crtc, vbl, position);

	return 0;
}

static u32 dm_hpd_get_gpio_reg(struct amdgpu_device *adev)
{
	return mmDC_GPIO_HPD_A;
}


static bool dm_is_display_hung(struct amdgpu_device *adev)
{
	u32 crtc_hung = 0;
	u32 i, j, tmp;

	crtc_hung = dal_get_connected_targets_vector(adev->dm.dal);

	for (j = 0; j < 10; j++) {
		for (i = 0; i < adev->mode_info.num_crtc; i++) {
			if (crtc_hung & (1 << i)) {
				int32_t vpos1, hpos1;
				int32_t vpos2, hpos2;

				tmp = dal_get_crtc_scanoutpos(
					adev->dm.dal,
					i,
					&vpos1,
					&hpos1);
				udelay(10);
				tmp = dal_get_crtc_scanoutpos(
					adev->dm.dal,
					i,
					&vpos2,
					&hpos2);

				if (hpos1 != hpos2 && vpos1 != vpos2)
					crtc_hung &= ~(1 << i);
			}
		}

		if (crtc_hung == 0)
			return false;
	}

	return true;
}


static bool dm_is_idle(void *handle)
{
	/* XXX todo */
	return true;
}

static int dm_wait_for_idle(void *handle)
{
	/* XXX todo */
	return 0;
}

static void dm_print_status(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	dev_info(adev->dev, "DCE 10.x registers\n");
	/* XXX todo */
}

static int dm_soft_reset(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	u32 srbm_soft_reset = 0, tmp;

	if (dm_is_display_hung(adev))
		srbm_soft_reset |= SRBM_SOFT_RESET__SOFT_RESET_DC_MASK;

	if (srbm_soft_reset) {
		dm_print_status(adev);

		tmp = RREG32(mmSRBM_SOFT_RESET);
		tmp |= srbm_soft_reset;
		dev_info(adev->dev, "SRBM_SOFT_RESET=0x%08X\n", tmp);
		WREG32(mmSRBM_SOFT_RESET, tmp);
		tmp = RREG32(mmSRBM_SOFT_RESET);

		udelay(50);

		tmp &= ~srbm_soft_reset;
		WREG32(mmSRBM_SOFT_RESET, tmp);
		tmp = RREG32(mmSRBM_SOFT_RESET);

		/* Wait a little for things to settle down */
		udelay(50);
		dm_print_status(adev);
	}
	return 0;
}

static void amdgpu_dm_pflip_high_irq(void *interrupt_params)
{
	struct common_irq_params *irq_params = interrupt_params;
	struct amdgpu_device *adev = irq_params->adev;
	enum dal_irq_source src = irq_params->irq_src;
	unsigned long flags;
	uint32_t display_index =
		dal_get_display_index_from_int_src(adev->dm.dal, src);
	struct amdgpu_crtc *amdgpu_crtc = adev->mode_info.crtcs[display_index];
	struct amdgpu_flip_work *works;

	/* IRQ could occur when in initial stage */
	if(amdgpu_crtc == NULL)
		return;

	spin_lock_irqsave(&adev->ddev->event_lock, flags);
	works = amdgpu_crtc->pflip_works;
	if (amdgpu_crtc->pflip_status != AMDGPU_FLIP_SUBMITTED){
		DRM_DEBUG_DRIVER("amdgpu_crtc->pflip_status = %d != "
						 "AMDGPU_FLIP_SUBMITTED(%d)\n",
						 amdgpu_crtc->pflip_status,
						 AMDGPU_FLIP_SUBMITTED);
		spin_unlock_irqrestore(&adev->ddev->event_lock, flags);
		return;
	}

	/* page flip completed. clean up */
	amdgpu_crtc->pflip_status = AMDGPU_FLIP_NONE;
	amdgpu_crtc->pflip_works = NULL;

	/* wakeup usersapce */
	if(works->event)
		drm_send_vblank_event(
			adev->ddev,
			amdgpu_crtc->crtc_id,
			works->event);

	spin_unlock_irqrestore(&adev->ddev->event_lock, flags);

	drm_vblank_put(adev->ddev, amdgpu_crtc->crtc_id);
	amdgpu_irq_put(adev, &adev->pageflip_irq, amdgpu_crtc->crtc_id);
	queue_work(amdgpu_crtc->pflip_queue, &works->unpin_work);
}

static void amdgpu_dm_crtc_high_irq(void *interrupt_params)
{
	struct common_irq_params *irq_params = interrupt_params;
	struct amdgpu_device *adev = irq_params->adev;
	enum dal_irq_source src = irq_params->irq_src;

	uint32_t display_index =
		dal_get_display_index_from_int_src(adev->dm.dal, src);

	drm_handle_vblank(adev->ddev, display_index);
}

static void hpd_low_irq_helper_func(
	void *param,
	const struct path_mode *pm)
{
	uint32_t *display_index = param;

	*display_index = pm->display_path_index;
}

static inline struct amdgpu_connector *find_connector_by_display_index(
	struct drm_device *dev,
	uint32_t display_index)
{
	struct drm_connector *connector = NULL;
	struct amdgpu_connector *aconnector = NULL;

	list_for_each_entry(
		connector,
		&dev->mode_config.connector_list,
		head) {
		aconnector = to_amdgpu_connector(connector);

		/*aconnector->connector_id means display_index*/
		if (aconnector->connector_id == display_index)
			break;
	}

	return aconnector;
}

static void amdgpu_dm_hpd_low_irq(void *interrupt_params)
{
	struct amdgpu_device *adev = interrupt_params;
	struct dal *dal = adev->dm.dal;
	struct drm_device *dev = adev->ddev;
	uint32_t connected_displays;
	struct amdgpu_connector *aconnector = NULL;
	bool trigger_drm_hpd_event = false;

	/* This function runs after dal_notify_hotplug().
	 * That means the user-mode may already called DAL with a Set/Reset
	 * mode, that means this function must acquire the dal_mutex
	 * *before* calling into DAL.
	 * The vice-versa sequence may also happen - this function is
	 * calling into DAL and preempted by a call from user-mode. */
	mutex_lock(&adev->dm.dal_mutex);

	connected_displays = dal_get_connected_targets_vector(dal);

	if (connected_displays == 0) {
		uint32_t display_index = INVALID_DISPLAY_INDEX;

		dal_pin_active_path_modes(
			dal,
			&display_index,
			INVALID_DISPLAY_INDEX,
			hpd_low_irq_helper_func);

		mutex_unlock(&adev->dm.dal_mutex);

		adev->dm.fake_display_index = display_index;

		aconnector =
			find_connector_by_display_index(dev, display_index);

		if (!aconnector)
			return;

		/*
		 * force connected status on fake display connector
		 */
		aconnector->base.status = connector_status_connected;

		/* we need to force user-space notification on changed modes */
		trigger_drm_hpd_event = true;

	} else if (adev->dm.fake_display_index != INVALID_DISPLAY_INDEX) {
		/* we assume only one display is connected */
		uint32_t connected_display_index = 0;
		struct drm_crtc *crtc;

		mutex_unlock(&adev->dm.dal_mutex);

		/* identify first connected display index */
		while (connected_displays) {
			if (1 & connected_displays)
				break;

			++connected_display_index;
			connected_displays >>= 1;
		}

		aconnector =
			find_connector_by_display_index(
				dev,
				adev->dm.fake_display_index);

		if (!aconnector)
			return;

		/*
		 * if there is display on another connector get connected
		 * we need to clean-up connection status on fake display
		 */
		if (connected_display_index != adev->dm.fake_display_index) {
			/* reset connected status on fake display connector */
			aconnector->base.status = connector_status_disconnected;
		} else {
			crtc = aconnector->base.encoder->crtc;

			DRM_DEBUG_KMS("Setting connector DPMS state to off\n");
			DRM_DEBUG_KMS("\t[CONNECTOR:%d] set DPMS off\n",
					aconnector->base.base.id);
			aconnector->base.funcs->dpms(
					&aconnector->base, DRM_MODE_DPMS_OFF);

			amdgpu_dm_mode_reset(crtc);

			/*
			 * as mode reset is done for fake display, we should
			 * unreference drm fb and assign NULL pointer to the
			 * primary drm frame, so we will receive full set mode
			 * sequence later
			 */

			drm_framebuffer_unreference(crtc->primary->fb);

			crtc->primary->fb = NULL;
		}

		adev->dm.fake_display_index = INVALID_DISPLAY_INDEX;

		trigger_drm_hpd_event = true;
	} else
		mutex_unlock(&adev->dm.dal_mutex);

	if (true == trigger_drm_hpd_event)
		drm_kms_helper_hotplug_event(dev);
}

static int dm_set_clockgating_state(void *handle,
		  enum amd_clockgating_state state)
{
	return 0;
}

static int dm_set_powergating_state(void *handle,
		  enum amd_powergating_state state)
{
	return 0;
}

/* Prototypes of private functions */
static int dm_early_init(void* handle);



/* Init display KMS
 *
 * Returns 0 on success
 */
int amdgpu_dm_init(struct amdgpu_device *adev)
{
	struct dal_init_data init_data;
	struct drm_device *ddev = adev->ddev;
	adev->dm.ddev = adev->ddev;
	adev->dm.adev = adev;
	adev->dm.fake_display_index = INVALID_DISPLAY_INDEX;

	/* Zero all the fields */
	memset(&init_data, 0, sizeof(init_data));


	/* initialize DAL's lock (for SYNC context use) */
	spin_lock_init(&adev->dm.dal_lock);

	/* initialize DAL's mutex */
	mutex_init(&adev->dm.dal_mutex);

	if(amdgpu_dm_irq_init(adev)) {
		DRM_ERROR("amdgpu: failed to initialize DM IRQ support.\n");
		goto error;
	}

	if (ddev->pdev) {
		init_data.bdf_info.DEVICE_NUMBER = PCI_SLOT(ddev->pdev->devfn);
		init_data.bdf_info.FUNCTION_NUMBER =
			PCI_FUNC(ddev->pdev->devfn);
		if (ddev->pdev->bus)
			init_data.bdf_info.BUS_NUMBER = ddev->pdev->bus->number;
	}

	init_data.display_param = display_param;

	init_data.asic_id.chip_family = adev->family;

	init_data.asic_id.chip_id = adev->rev_id;
	init_data.asic_id.hw_internal_rev = adev->external_rev_id;

	init_data.asic_id.vram_width = adev->mc.vram_width;
	/* TODO: initialize init_data.asic_id.vram_type here!!!! */
	init_data.asic_id.atombios_base_address =
		adev->mode_info.atom_context->bios;
	init_data.asic_id.runtime_flags.bits.SKIP_POWER_DOWN_ON_RESUME = 1;

	if (adev->asic_type == CHIP_CARRIZO)
		init_data.asic_id.runtime_flags.bits.GNB_WAKEUP_SUPPORTED = 1;

	init_data.driver = adev;

	adev->dm.cgs_device = amdgpu_cgs_create_device(adev);

	if (!adev->dm.cgs_device) {
		DRM_ERROR("amdgpu: failed to create cgs device.\n");
		goto error;
	}

	init_data.cgs_device = adev->dm.cgs_device;

	adev->dm.dal = NULL;

	/* enable gpu scaling in DAL */
	init_data.display_param.bool_param_enable_mask |=
		1 << DAL_PARAM_ENABLE_GPU_SCALING;
	init_data.display_param.bool_param_values |=
		1 << DAL_PARAM_ENABLE_GPU_SCALING;

	adev->dm.dal = dal_create(&init_data);

	if (!adev->dm.dal) {
		DRM_ERROR(
		"amdgpu: failed to initialize hw for display support.\n");
		/* Do not fail and cleanup, try to run without display */
	}

	if (amdgpu_dm_initialize_drm_device(&adev->dm)) {
		DRM_ERROR(
		"amdgpu: failed to initialize sw for display support.\n");
		goto error;
	}

	/* Update the actual used number of crtc */
	adev->mode_info.num_crtc = adev->dm.display_indexes_num;

	/* TODO: Add_display_info? */

	/* TODO use dynamic cursor width */
	adev->ddev->mode_config.cursor_width = 128;
	adev->ddev->mode_config.cursor_height = 128;

	if (drm_vblank_init(adev->ddev, adev->dm.display_indexes_num)) {
		DRM_ERROR(
		"amdgpu: failed to initialize sw for display support.\n");
		goto error;
	}

	DRM_INFO("KMS initialized.\n");

	return 0;
error:
	amdgpu_dm_fini(adev);

	return -1;
}

void amdgpu_dm_fini(struct amdgpu_device *adev)
{
	/*
	 * TODO: pageflip, vlank interrupt
	 *
	 * amdgpu_dm_destroy_drm_device(&adev->dm);
	 * amdgpu_dm_irq_fini(adev);
	 */

	if (adev->dm.cgs_device) {
		amdgpu_cgs_destroy_device(adev->dm.cgs_device);
		adev->dm.cgs_device = NULL;
	}

	dal_destroy(&adev->dm.dal);
	return;
}

/* moved from amdgpu_dm_kms.c */
void amdgpu_dm_destroy()
{
}

/*
 * amdgpu_dm_get_vblank_counter
 *
 * @brief
 * Get counter for number of vertical blanks
 *
 * @param
 * struct amdgpu_device *adev - [in] desired amdgpu device
 * int disp_idx - [in] which CRTC to get the counter from
 *
 * @return
 * Counter for vertical blanks
 */
u32 amdgpu_dm_get_vblank_counter(struct amdgpu_device *adev, int disp_idx)
{
	return dal_get_vblank_counter(adev->dm.dal, disp_idx);
}

static int dm_sw_init(void *handle)
{
	return 0;
}

static int dm_sw_fini(void *handle)
{
	return 0;
}

static int dm_hw_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	/* Create DAL display manager */
	amdgpu_dm_init(adev);

	amdgpu_dm_hpd_init(adev);

	return 0;
}

static int dm_hw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	amdgpu_dm_hpd_fini(adev);

	amdgpu_dm_irq_fini(adev);

	return 0;
}

static int dm_suspend(void *handle)
{
	struct amdgpu_device *adev = handle;
	struct amdgpu_display_manager *dm = &adev->dm;

	dal_set_power_state(
		dm->dal,
		DAL_ACPI_CM_POWER_STATE_D3,
		DAL_VIDEO_POWER_SUSPEND);

	amdgpu_dm_irq_suspend(adev);

	return 0;
}

static int dm_resume(void *handle)
{
	uint32_t connected_displays_vector;
	uint32_t prev_connected_displays_vector;
	uint32_t supported_disp = 0; /* vector of supported displays */
	uint32_t displays_number;
	uint32_t current_display_index;
	struct amdgpu_device *adev = handle;
	struct amdgpu_display_manager *dm = &adev->dm;
	uint32_t displays_vector[MAX_COFUNC_PATH];

	dal_set_power_state(
		dm->dal,
		DAL_ACPI_CM_POWER_STATE_D0,
		DAL_VIDEO_POWER_ON);

	prev_connected_displays_vector =
		dal_get_connected_targets_vector(dm->dal);
	supported_disp = dal_get_supported_displays_vector(dm->dal);

	/* save previous connected display to reset mode correctly */
	connected_displays_vector = prev_connected_displays_vector;

	amdgpu_dm_irq_resume(adev);

	dal_resume(dm->dal);

	for (displays_number = 0, current_display_index = 0;
			connected_displays_vector != 0;
			connected_displays_vector >>= 1,
			current_display_index++) {
		if ((connected_displays_vector & 1) == 1) {
			struct amdgpu_crtc *crtc =
				adev->mode_info.crtcs[current_display_index];

			displays_vector[displays_number] =
				current_display_index;

			memset(&crtc->base.mode, 0, sizeof(crtc->base.mode));

			++displays_number;
		}
	}

	mutex_lock(&adev->dm.dal_mutex);
	dal_reset_path_mode(dm->dal, displays_number, displays_vector);
	mutex_unlock(&adev->dm.dal_mutex);

	return 0;
}
const struct amd_ip_funcs amdgpu_dm_funcs = {
	.early_init = dm_early_init,
	.late_init = NULL,
	.sw_init = dm_sw_init,
	.sw_fini = dm_sw_fini,
	.hw_init = dm_hw_init,
	.hw_fini = dm_hw_fini,
	.suspend = dm_suspend,
	.resume = dm_resume,
	.is_idle = dm_is_idle,
	.wait_for_idle = dm_wait_for_idle,
	.soft_reset = dm_soft_reset,
	.print_status = dm_print_status,
	.set_clockgating_state = dm_set_clockgating_state,
	.set_powergating_state = dm_set_powergating_state,
};

static int amdgpu_dm_mode_config_init(struct amdgpu_device *adev)
{
	int r;
	int i;

	/* Register IRQ sources and initialize high IRQ callbacks */
	struct common_irq_params *c_irq_params;
	struct dal_interrupt_params int_params = {0};
	int_params.requested_polarity = INTERRUPT_POLARITY_DEFAULT;
	int_params.current_polarity = INTERRUPT_POLARITY_DEFAULT;
	int_params.no_mutex_wait = false;
	int_params.one_shot = false;

	for (i = 7; i < 19; i += 2) {
		r = amdgpu_irq_add_id(adev, i, &adev->crtc_irq);

		/* High IRQ callback for crtc irq */
		int_params.int_context = INTERRUPT_HIGH_IRQ_CONTEXT;
		int_params.irq_source =
			dal_interrupt_to_irq_source(adev->dm.dal, i, 0);

		c_irq_params = &adev->dm.vsync_params[int_params.irq_source - DAL_IRQ_SOURCE_CRTC1VSYNC];

		c_irq_params->adev = adev;
		c_irq_params->irq_src = int_params.irq_source;

		amdgpu_dm_irq_register_interrupt(adev, &int_params,
				amdgpu_dm_crtc_high_irq, c_irq_params);

		if (r)
			return r;
	}

	for (i = 8; i < 20; i += 2) {
		r = amdgpu_irq_add_id(adev, i, &adev->pageflip_irq);

		/* High IRQ callback for pflip irq */
		int_params.int_context = INTERRUPT_HIGH_IRQ_CONTEXT;
		int_params.irq_source =
			dal_interrupt_to_irq_source(adev->dm.dal, i, 0);

		c_irq_params = &adev->dm.pflip_params[int_params.irq_source - DAL_IRQ_SOURCE_PFLIP_FIRST];

		c_irq_params->adev = adev;
		c_irq_params->irq_src = int_params.irq_source;

		amdgpu_dm_irq_register_interrupt(adev, &int_params,
				amdgpu_dm_pflip_high_irq, c_irq_params);

		if (r)
			return r;
	}

	/* HPD hotplug */
	r = amdgpu_irq_add_id(adev, 42, &adev->hpd_irq);

	for (i = 0; i < adev->mode_info.num_crtc; i++) {
		/* High IRQ callback for hpd irq */
		int_params.int_context = INTERRUPT_LOW_IRQ_CONTEXT;
		int_params.irq_source =
			dal_interrupt_to_irq_source(adev->dm.dal, 42, i);
		amdgpu_dm_irq_register_interrupt(adev, &int_params,
			amdgpu_dm_hpd_low_irq, adev);
	}

	if (r)
		return r;

	adev->mode_info.mode_config_initialized = true;

	adev->ddev->mode_config.funcs = (void *)&amdgpu_mode_funcs;

	adev->ddev->mode_config.max_width = 16384;
	adev->ddev->mode_config.max_height = 16384;

	adev->ddev->mode_config.preferred_depth = 24;
	adev->ddev->mode_config.prefer_shadow = 1;

	adev->ddev->mode_config.fb_base = adev->mc.aper_base;

	r = amdgpu_modeset_create_props(adev);
	if (r)
		return r;

	/* this is a part of HPD initialization  */
	drm_kms_helper_poll_init(adev->ddev);

	return r;
}

#if defined(CONFIG_BACKLIGHT_CLASS_DEVICE) ||\
	defined(CONFIG_BACKLIGHT_CLASS_DEVICE_MODULE)
static int amdgpu_dm_backlight_update_status(struct backlight_device *bd)
{
	struct amdgpu_display_manager *dm = bl_get_data(bd);
	uint32_t current_display_index = 0;
	uint32_t connected_displays_vector;
	uint32_t total_supported_displays_vector;

	if (!dm->dal)
		return 0;

	connected_displays_vector =
		dal_get_connected_targets_vector(dm->dal);
	total_supported_displays_vector =
		dal_get_supported_displays_vector(dm->dal);

	/* loops over all the connected displays*/
	for (; total_supported_displays_vector != 0;
		total_supported_displays_vector >>= 1,
		connected_displays_vector >>= 1,
		++current_display_index) {
		enum signal_type st;

		if (!(connected_displays_vector & 1))
			continue;

		st = dal_get_display_signal(dm->dal, current_display_index);

		if (dal_is_embedded_signal(st))
			break;
	}

	if (dal_set_backlight_level(
		dm->dal,
		current_display_index,
		bd->props.brightness))
		return 0;
	else
		return 1;
}

static int amdgpu_dm_backlight_get_brightness(struct backlight_device *bd)
{
	return bd->props.brightness;
}

static const struct backlight_ops amdgpu_dm_backlight_ops = {
	.get_brightness = amdgpu_dm_backlight_get_brightness,
	.update_status	= amdgpu_dm_backlight_update_status,
};
#endif

/* In this architecture, the association
 * connector -> encoder -> crtc
 * id not really requried. The crtc and connector will hold the
 * display_index as an abstraction to use with DAL component
 *
 * Returns 0 on success
 */
int amdgpu_dm_initialize_drm_device(struct amdgpu_display_manager *dm)
{
	int current_display_index = 0;
	struct amdgpu_connector *aconnector;
	struct amdgpu_encoder *aencoder;
	struct amdgpu_crtc *acrtc;

	uint32_t connected_displays_vector =
		dal_get_connected_targets_vector(dm->dal);
	uint32_t total_supported_displays_vector =
		dal_get_supported_displays_vector(dm->dal);


	if (amdgpu_dm_mode_config_init(dm->adev)) {
		DRM_ERROR("KMS: Failed to initialize mode config\n");
		return -1;
	}

#if defined(CONFIG_BACKLIGHT_CLASS_DEVICE) ||\
	defined(CONFIG_BACKLIGHT_CLASS_DEVICE_MODULE)
	{
		struct backlight_device *bd;
		char bl_name[16];
		struct backlight_properties props;

		memset(&props, 0, sizeof(props));
		props.max_brightness = AMDGPU_MAX_BL_LEVEL;
		props.type = BACKLIGHT_RAW;
		snprintf(bl_name, sizeof(bl_name),
			"amdgpu_bl%d", dm->adev->ddev->primary->index);
		bd = backlight_device_register(
			bl_name,
			dm->adev->ddev->dev,
			dm,
			&amdgpu_dm_backlight_ops,
			&props);
		if (!bd) {
			DRM_ERROR("Backlight registration failed\n");
			goto fail_backlight_dev;
		}
		dm->backlight_dev = bd;
	}
#endif

	/* loops over all the connected displays*/
	for (; total_supported_displays_vector != 0;
				total_supported_displays_vector >>= 1,
				connected_displays_vector >>= 1,
				++current_display_index) {

		if (current_display_index > AMDGPU_DM_MAX_DISPLAY_INDEX) {
			DRM_ERROR(
				"KMS: Cannot support more than %d display indeces\n",
					AMDGPU_DM_MAX_DISPLAY_INDEX);
			continue;
		}

		aconnector = kzalloc(sizeof(*aconnector), GFP_KERNEL);
		if (!aconnector)
			goto fail_connector;

		aencoder = kzalloc(sizeof(*aencoder), GFP_KERNEL);
		if (!aencoder)
			goto fail_encoder;

		acrtc = kzalloc(sizeof(struct amdgpu_crtc), GFP_KERNEL);
		if (!acrtc)
			goto fail_crtc;

		if (amdgpu_dm_crtc_init(
			dm,
			acrtc,
			current_display_index)) {
			DRM_ERROR("KMS: Failed to initialize crtc\n");
			goto fail;
		}

		if (amdgpu_dm_encoder_init(
			dm->ddev,
			aencoder,
			current_display_index,
			acrtc)) {
			DRM_ERROR("KMS: Failed to initialize encoder\n");
			goto fail;
		}

		if (amdgpu_dm_connector_init(
			dm,
			aconnector,
			current_display_index,
			(connected_displays_vector & 1) == 1,
			aencoder)) {
			DRM_ERROR("KMS: Failed to initialize connector\n");
			goto fail;
		}
	}

	dm->display_indexes_num = current_display_index;
	dm->mode_query_option = QUERY_OPTION_NO_PAN;

	return 0;

fail:
	/* clean any dongling drm structure for the last (corrupted)
	display target */
	amdgpu_dm_crtc_destroy(&acrtc->base);
fail_crtc:
	amdgpu_dm_encoder_destroy(&aencoder->base);
fail_encoder:
	amdgpu_dm_connector_destroy(&aconnector->base);
fail_connector:
	backlight_device_unregister(dm->backlight_dev);
fail_backlight_dev:
	return -1;
}

void amdgpu_dm_destroy_drm_device(
				struct amdgpu_display_manager *dm)
{
	drm_mode_config_cleanup(dm->ddev);
	return;
}

/******************************************************************************
 * amdgpu_display_funcs functions
 *****************************************************************************/


static void dm_set_vga_render_state(struct amdgpu_device *adev,
					   bool render)
{
	u32 tmp;

	/* Lockout access through VGA aperture*/
	tmp = RREG32(mmVGA_HDP_CONTROL);
	if (render)
		tmp = REG_SET_FIELD(tmp, VGA_HDP_CONTROL, VGA_MEMORY_DISABLE, 0);
	else
		tmp = REG_SET_FIELD(tmp, VGA_HDP_CONTROL, VGA_MEMORY_DISABLE, 1);
	WREG32(mmVGA_HDP_CONTROL, tmp);

	/* disable VGA render */
	tmp = RREG32(mmVGA_RENDER_CONTROL);
	if (render)
		tmp = REG_SET_FIELD(tmp, VGA_RENDER_CONTROL, VGA_VSTATUS_CNTL, 1);
	else
		tmp = REG_SET_FIELD(tmp, VGA_RENDER_CONTROL, VGA_VSTATUS_CNTL, 0);
	WREG32(mmVGA_RENDER_CONTROL, tmp);
}

/**
 * dm_bandwidth_update - program display watermarks
 *
 * @adev: amdgpu_device pointer
 *
 * Calculate and program the display watermarks and line buffer allocation.
 */
static void dm_bandwidth_update(struct amdgpu_device *adev)
{
	AMDGPU_DM_NOT_IMPL("%s\n", __func__);
}

static void dm_set_backlight_level(struct amdgpu_encoder *amdgpu_encoder,
				     u8 level)
{
	/* TODO: translate amdgpu_encoder to display_index and call DAL */
	AMDGPU_DM_NOT_IMPL("%s\n", __func__);
}

static u8 dm_get_backlight_level(struct amdgpu_encoder *amdgpu_encoder)
{
	/* TODO: translate amdgpu_encoder to display_index and call DAL */
	AMDGPU_DM_NOT_IMPL("%s\n", __func__);
	return 0;
}

/******************************************************************************
 * Page Flip functions
 ******************************************************************************/
/**
 * dm_page_flip - called by amdgpu_flip_work_func(), which is triggered
 * 			via DRM IOCTL, by user mode.
 *
 * @adev: amdgpu_device pointer
 * @crtc_id: crtc to cleanup pageflip on
 * @crtc_base: new address of the crtc (GPU MC address)
 *
 * Does the actual pageflip (surface address update).
 */
static void dm_page_flip(struct amdgpu_device *adev,
			      int crtc_id, u64 crtc_base)
{
	struct amdgpu_crtc *amdgpu_crtc = adev->mode_info.crtcs[crtc_id];
	struct plane_addr_flip_info flip_info;
	const unsigned int num_of_planes = 1;

	memset(&flip_info, 0, sizeof(flip_info));

	flip_info.display_index = amdgpu_crtc->crtc_id;
	flip_info.address_info.address.type = PLN_ADDR_TYPE_GRAPHICS;
	flip_info.address_info.layer_index = LAYER_INDEX_PRIMARY;
	flip_info.address_info.flags.bits.ENABLE = 1;

	flip_info.address_info.address.grph.addr.low_part =
			       lower_32_bits(crtc_base);

	flip_info.address_info.address.grph.addr.high_part =
			       upper_32_bits(crtc_base);

	dal_update_plane_addresses(adev->dm.dal, num_of_planes, &flip_info);
}

static const struct amdgpu_display_funcs display_funcs = {
	.set_vga_render_state = dm_set_vga_render_state,
	.bandwidth_update = dm_bandwidth_update, /* called unconditionally */
	.vblank_get_counter = dm_vblank_get_counter,/* called unconditionally */
	.vblank_wait = NULL, /* not called anywhere */
	.is_display_hung = dm_is_display_hung,/* called unconditionally */
	.backlight_set_level =
		dm_set_backlight_level,/* called unconditionally */
	.backlight_get_level =
		dm_get_backlight_level,/* called unconditionally */
	.hpd_sense = NULL,/* called unconditionally */
	.hpd_set_polarity = NULL, /* called unconditionally */
	.hpd_get_gpio_reg = dm_hpd_get_gpio_reg,/* called unconditionally */
	.page_flip = dm_page_flip, /* called unconditionally */
	.page_flip_get_scanoutpos =
		dm_crtc_get_scanoutpos,/* called unconditionally */
	.add_encoder = NULL, /* VBIOS parsing. DAL does it. */
	.add_connector = NULL, /* VBIOS parsing. DAL does it. */
	.stop_mc_access = dal_stop_mc_access, /* called unconditionally */
	.resume_mc_access = dal_resume_mc_access, /* called unconditionally */
};

static void set_display_funcs(struct amdgpu_device *adev)
{
	if (adev->mode_info.funcs == NULL)
		adev->mode_info.funcs = &display_funcs;
}

static int dm_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	set_display_funcs(adev);
	amdgpu_dm_set_irq_funcs(adev);

	switch (adev->asic_type) {
	case CHIP_CARRIZO:
		adev->mode_info.num_crtc = 3;
		adev->mode_info.num_hpd = 6;
		adev->mode_info.num_dig = 9;
		break;
	default:
		DRM_ERROR("Usupported ASIC type: 0x%X\n", adev->asic_type);
		return -EINVAL;
	}

	/* Note: Do NOT change adev->audio_endpt_rreg and
	 * adev->audio_endpt_wreg because they are initialised in
	 * amdgpu_device_init() */



	return 0;
}


bool amdgpu_dm_acquire_dal_lock(struct amdgpu_display_manager *dm)
{
	/* TODO */
	return true;
}

bool amdgpu_dm_release_dal_lock(struct amdgpu_display_manager *dm)
{
	/* TODO */
	return true;
}
