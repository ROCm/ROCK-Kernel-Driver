/* SPDX-License-Identifier: MIT */
#ifndef AMDKCL_DRM_ATOMIC_HELPER_H
#define AMDKCL_DRM_ATOMIC_HELPER_H

#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic.h>
#include <drm/drm_plane_helper.h>
#include <kcl/kcl_drm_crtc.h>

#if !defined(HAVE_DRM_ATOMIC_HELPER_DISABLE_ALL)
int drm_atomic_helper_disable_all(struct drm_device *dev,
				  struct drm_modeset_acquire_ctx *ctx);
#endif

#if !defined(HAVE_DRM_ATOMIC_HELPER_DUPLICATE_STATE)
struct drm_atomic_state *
drm_atomic_helper_duplicate_state(struct drm_device *dev,
				  struct drm_modeset_acquire_ctx *ctx);
#endif

#endif
