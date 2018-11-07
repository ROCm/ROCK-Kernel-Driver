#ifndef AMDKCL_DRM_CONNECTOR_H
#define AMDKCL_DRM_CONNECTOR_H

#include <drm/drm_crtc.h>

#if !defined(HAVE_DRM_CONNECTOR_PUT)
static inline void drm_connector_put(struct drm_connector *connector)
{
#if defined(HAVE_FREE_CB_IN_STRUCT_DRM_MODE_OBJECT)
	struct drm_mode_object *obj = &connector->base;
	if (obj->free_cb) {
		DRM_DEBUG("OBJ ID: %d (%d)\n", obj->id, kref_read(&obj->refcount));
		kref_put(&obj->refcount, obj->free_cb);
	}
#endif
}

#endif

#endif /* AMDKCL_DRM_CONNECTOR_H */


