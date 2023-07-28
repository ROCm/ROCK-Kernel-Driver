/* SPDX-License-Identifier: MIT */
#ifndef _KCL_HEADER_DISPLAY_DRM_DSC_H_H_
#define _KCL_HEADER_DISPLAY_DRM_DSC_H_H_


#if defined(HAVE_DRM_DISPLAY_DRM_DSC_HELPER_H)
#include <drm/display/drm_dsc_helper.h>
#endif

#if defined(HAVE_DRM_DISPLAY_DRM_DSC_H)
#include <drm/display/drm_dsc.h>
#else
#include_next <drm/drm_dsc.h>
#endif

#endif

