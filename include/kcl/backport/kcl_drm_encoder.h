/* SPDX-License-Identifier: MIT */
#ifndef KCL_BACKPORT_KCL_DRM_ENCODER_H
#define KCL_BACKPORT_KCL_DRM_ENCODER_H

#include <kcl/header/kcl_drm_encoder_h.h>
#include <drm/drm_edid.h>

#if !defined(HAVE_DRM_ENCODER_FIND_VALID_WITH_FILE)
static inline struct drm_encoder *_kcl_drm_encoder_find(struct drm_device *dev,
						   struct drm_file *file_priv,
						   uint32_t id)
{
	return drm_encoder_find(dev, id);
}
#define drm_encoder_find _kcl_drm_encoder_find
#endif

#if defined(HAVE_DRM_EDID_TO_ELD)
static inline
int _kcl_drm_add_edid_modes(struct drm_connector *connector, struct edid *edid)
{
	int ret;

	ret = drm_add_edid_modes(connector, edid);

	if (drm_edid_is_valid(edid))
		drm_edid_to_eld(connector, edid);

	return ret;
}
#define drm_add_edid_modes _kcl_drm_add_edid_modes
#endif

#endif
