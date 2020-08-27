/* SPDX-License-Identifier: MIT */
#ifndef KCL_KCL_DRM_MODESET_LOCK_H
#define KCL_KCL_DRM_MODESET_LOCK_H

#include <kcl/header/kcl_drm_device_h.h>
#include <drm/drm_crtc.h>

#if !defined(HAVE_DRM_MODESET_LOCK_ALL_CTX)
int drm_modeset_lock_all_ctx(struct drm_device *dev,
			     struct drm_modeset_acquire_ctx *ctx);
#endif

#endif
