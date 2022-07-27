/* SPDX-License-Identifier: MIT */

#ifndef _KCL_KCL_DRM_DSC_HELPER_H
#define _KCL_KCL_DRM_DSC_HELPER_H

#ifdef CONFIG_DRM_AMD_DC_DSC_SUPPORT

#include <drm/display/drm_dsc_helper.h>
#include <drm/display/drm_dsc.h>

#ifndef HAVE_DRM_DSC_PPS_PAYLOAD_PACK
void drm_dsc_pps_payload_pack(struct drm_dsc_picture_parameter_set *pps_sdp,
                              const struct drm_dsc_config *dsc_cfg);
#endif

#endif /* CONFIG_DRM_AMD_DC_DSC_SUPPORT */
#endif /* _KCL_KCL_DRM_DSC_HELPER_H */

