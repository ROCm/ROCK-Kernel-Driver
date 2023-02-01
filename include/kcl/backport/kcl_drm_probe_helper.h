#ifndef AMDKCL_BACKPORT_DRM_PROBE_HELPER_H
#define AMDKCL_BACKPORT_DRM_PROBE_HELPER_H

#include <drm/drm_probe_helper.h>

#ifndef HAVE_DRM_KMS_HELPER_CONNECTOR_HOTPLUG_EVENT
static inline void _kcl_drm_kms_helper_connector_hotplug_event(struct drm_connector *connector)
{
	drm_kms_helper_hotplug_event(connector->dev);
}

#define drm_kms_helper_connector_hotplug_event _kcl_drm_kms_helper_connector_hotplug_event


#endif
#endif
