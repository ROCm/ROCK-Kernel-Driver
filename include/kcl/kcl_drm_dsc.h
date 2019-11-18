#ifndef AMDKCL_DRM_DSC_H
#define AMDKCL_DRM_DSC_H

#include <drm/drm_dsc.h>

#if !defined(HAVE_DRM_DSC_PPS_PAYLOAD_PACK)
void kcl_drm_dsc_pps_payload_pack(struct drm_dsc_picture_parameter_set *pps_sdp,
				const struct drm_dsc_config *dsc_cfg);
#endif

#endif	/* AMDKCL_DRM_DSC_H */
