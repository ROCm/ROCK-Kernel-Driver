#ifndef AMDKCL_DRM_CONNECTOR_H
#define AMDKCL_DRM_CONNECTOR_H


#if DRM_VERSION_CODE < DRM_VERSION(4, 12, 0)
#include <drm/drm_crtc.h>
#include <drm/drmP.h>

static inline void drm_connector_put(struct drm_connector *connector)
{
#if DRM_VERSION_CODE >= DRM_VERSION(4, 7, 0)
	struct drm_mode_object *obj = &connector->base;
	if (obj->free_cb) {
		DRM_DEBUG("OBJ ID: %d (%d)\n", obj->id, kref_read(&obj->refcount));
		kref_put(&obj->refcount, obj->free_cb);
	}
#endif
}

#endif

#endif /* AMDKCL_DRM_CONNECTOR_H */


