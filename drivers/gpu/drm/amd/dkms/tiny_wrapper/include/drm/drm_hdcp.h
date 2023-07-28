/* SPDX-License-Identifier: MIT */
#ifndef _KCL_HEADER_DRM_DISPLAY_HDCP_H_INCLUDED_H_
#define _KCL_HEADER_DRM_DISPLAY_HDCP_H_INCLUDED_H_

#ifdef HAVE_DRM_DISPLAY_DRM_HDCP_H
#include <drm/display/drm_hdcp_helper.h>
#include <drm/display/drm_hdcp.h>
#else
#include_next <drm/drm_hdcp.h>
#endif

#endif
