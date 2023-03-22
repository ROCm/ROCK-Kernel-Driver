/* SPDX-License-Identifier: MIT */
#ifndef KCL_BACKPORT_KCL_DRM_CRTC_H
#define KCL_BACKPORT_KCL_DRM_CRTC_H

#include <drm/drm_crtc.h>
#include <kcl/kcl_drm_crtc.h>

#if DRM_VERSION_CODE == DRM_VERSION(4, 10, 0) && defined(OS_NAME_RHEL_7_4)
#define AMDKCL_WORKAROUND_DRM_4_10_0_RHEL_7_4
#endif

#endif
