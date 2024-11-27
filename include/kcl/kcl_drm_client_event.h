/* SPDX-License-Identifier: MIT */
#ifndef KCL_KCL_DRM_CLIENT_EVENT_H
#define KCL_KCL_DRM_CLIENT_EVENT_H

#include <drm/drm_fb_helper.h>

#ifndef HAVE_DRM_CLIENT_DEV_RESUME
void drm_client_dev_suspend(struct drm_device *dev, bool holds_console_lock);
void drm_client_dev_resume(struct drm_device *dev, bool holds_console_lock);
#endif

#endif
