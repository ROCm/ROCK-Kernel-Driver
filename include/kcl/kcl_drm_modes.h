/* SPDX-License-Identifier: MIT */
#ifndef KCL_KCL_DRM_MODES_H
#define KCL_KCL_DRM_MODES_H

#include <drm/drm_modes.h>

#ifndef HAVE_DRM_MODE_GET_HV_TIMING
#define drm_mode_get_hv_timing drm_crtc_get_hv_timing
#endif

#ifndef HAVE_DRM_CONNECTOR_SET_PATH_PROPERTY
#define drm_connector_list_update drm_mode_connector_list_update
#endif

#ifndef HAVE_DRM_MODE_IS_420_XXX
bool drm_mode_is_420_only(const struct drm_display_info *display,
		const struct drm_display_mode *mode);
bool drm_mode_is_420_also(const struct drm_display_info *display,
		const struct drm_display_mode *mode);
#endif

#endif
