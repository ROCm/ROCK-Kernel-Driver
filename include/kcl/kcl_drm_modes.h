/* SPDX-License-Identifier: MIT */
#ifndef KCL_KCL_DRM_MODES_H
#define KCL_KCL_DRM_MODES_H

#include <drm/drm_modes.h>

#ifndef HAVE_DRM_MODE_GET_HV_TIMING
#define drm_mode_get_hv_timing drm_crtc_get_hv_timing
#endif
#endif
