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

#ifndef HAVE_DRM_DRM_GEM_FRAMEBUFFER_HELPER_H
struct drm_gem_object *drm_gem_fb_get_obj(struct drm_framebuffer *fb,
					  unsigned int plane)
{
	struct amdgpu_framebuffer *afb = to_amdgpu_framebuffer(fb);
	(void)plane; /* for compile un-used warning */
	if (afb)
		return afb->obj;
	else
		return NULL;
}

void drm_gem_fb_destroy(struct drm_framebuffer *fb)
{
	struct amdgpu_framebuffer *amdgpu_fb = to_amdgpu_framebuffer(fb);

	drm_gem_object_put_unlocked(amdgpu_fb->obj);

	drm_framebuffer_cleanup(fb);
	kfree(fb);
}

int drm_gem_fb_create_handle(struct drm_framebuffer *fb, struct drm_file *file,
			     unsigned int *handle)
{
	struct amdgpu_framebuffer *amdgpu_fb = to_amdgpu_framebuffer(fb);

	return drm_gem_handle_create(file, amdgpu_fb->obj, handle);
}
#endif
