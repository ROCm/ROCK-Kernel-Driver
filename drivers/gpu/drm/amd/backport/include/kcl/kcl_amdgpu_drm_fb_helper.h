/* SPDX-License-Identifier: MIT */
#ifndef AMDGPU_BACKPORT_KCL_AMDGPU_DRM_FB_HELPER_H
#define AMDGPU_BACKPORT_KCL_AMDGPU_DRM_FB_HELPER_H

#include <drm/drm_fb_helper.h>

#ifndef HAVE_DRM_FB_HELPER_LASTCLOSE
void drm_fb_helper_lastclose(struct drm_device *dev);
void drm_fb_helper_output_poll_changed(struct drm_device *dev);
#endif
#endif
