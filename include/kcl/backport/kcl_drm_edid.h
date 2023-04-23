#ifndef AMDKCL_BACKPORT_DRM_EDID_H
#define AMDKCL_BACKPORT_DRM_EDID_H

#include <drm/drm_edid.h>

#if !defined(HAVE_DRM_EDID_OVERRIDE_CONNECTOR_UPDATE)
#ifdef HAVE_DRM_ADD_OVERRIDE_EDID_MODES
static inline int _kcl_drm_edid_override_connector_update(struct drm_connector *connector)
{
	int ret;

	ret = drm_add_override_edid_modes(connector);
	return ret;
}

#define drm_edid_override_connector_update _kcl_drm_edid_override_connector_update
#endif
#endif

#endif
