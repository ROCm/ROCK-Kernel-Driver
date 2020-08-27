/* SPDX-License-Identifier: MIT */
#include <drm/drm_crtc.h>
#include <drm/drm_mode.h>
#include <kcl/kcl_drm_modes.h>
#include "kcl_common.h"

#ifndef HAVE_DRM_MODE_IS_420_XXX
amdkcl_dummy_symbol(drm_mode_is_420_only, bool, return false,
				  const struct drm_display_info *display, const struct drm_display_mode *mode)
amdkcl_dummy_symbol(drm_mode_is_420_also, bool, return false,
			 const struct drm_display_info *display, const struct drm_display_mode *mode)
#endif
