/* SPDX-License-Identifier: MIT */
#ifndef _KCL_HEADER_DISPLAY_DRM_DP_HELPER_H_H_
#define _KCL_HEADER_DISPLAY_DRM_DP_HELPER_H_H_

#if defined(HAVE_DRM_DISPLAY_DRM_DP_HELPER_H)
#include <drm/display/drm_dp_helper.h>
#elif defined(HAVE_DRM_DP_DRM_DP_HELPER_H)
#include <drm/dp/drm_dp_helper.h>
#else
#include_next <drm/drm_dp_helper.h>
#endif

#endif

