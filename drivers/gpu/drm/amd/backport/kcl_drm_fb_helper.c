/* SPDX-License-Identifier: MIT */
#include <kcl/kcl_amdgpu_drm_fb_helper.h>
#include "amdgpu.h"

#ifndef HAVE_DRM_FB_HELPER_LASTCLOSE
void drm_fb_helper_lastclose(struct drm_device *dev)
{
	struct amdgpu_device *adev = dev->dev_private;
	struct amdgpu_fbdev *afbdev;
	struct drm_fb_helper *fb_helper;
	int ret;

	if (!adev)
		return;

	afbdev = adev->mode_info.rfbdev;

	if (!afbdev)
		return;

	fb_helper = &afbdev->helper;

	ret = drm_fb_helper_restore_fbdev_mode_unlocked(fb_helper);
	if (ret)
		DRM_DEBUG("failed to restore crtc mode\n");
}

void drm_fb_helper_output_poll_changed(struct drm_device *dev)
{
	struct amdgpu_device *adev = dev->dev_private;

	if (adev->mode_info.rfbdev)
		drm_fb_helper_hotplug_event(&adev->mode_info.rfbdev->helper);
}
#endif
