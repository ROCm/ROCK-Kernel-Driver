/* SPDX-License-Identifier: MIT */
#ifndef AMDKCL_DRM_CONNECTOR_H
#define AMDKCL_DRM_CONNECTOR_H

#include <drm/drm_crtc.h>
#include <kcl/header/kcl_drm_connector_h.h>

#ifndef HAVE_DRM_CONNECTOR_UPDATE_EDID_PROPERTY
#define drm_connector_update_edid_property drm_mode_connector_update_edid_property
#endif

#endif /* AMDKCL_DRM_CONNECTOR_H */
