#ifndef AMDKCL_DRM_CONNECTOR_H
#define AMDKCL_DRM_CONNECTOR_H

#include <drm/drm_crtc.h>
#include <drm/drm_connector.h>

#ifndef HAVE_DRM_CONNECTOR_UPDATE_EDID_PROPERTY
#define drm_connector_update_edid_property drm_mode_connector_update_edid_property
#endif
#ifndef HAVE_DRM_CONNECTOR_ATTACH_ENCODER
#define drm_connector_attach_encoder drm_mode_connector_attach_encoder
#endif
#if DRM_VERSION_CODE < DRM_VERSION(4, 19, 0) && !defined(OS_NAME_SUSE_15_1)
#define drm_connector_set_path_property drm_mode_connector_set_path_property
#endif

/**
 * drm_connector_for_each_possible_encoder - iterate connector's possible encoders
 * @connector: &struct drm_connector pointer
 * @encoder: &struct drm_encoder pointer used as cursor
 * @__i: int iteration cursor, for macro-internal use
 */
#ifndef drm_connector_for_each_possible_encoder
#define drm_connector_for_each_possible_encoder(connector, encoder, __i) \
	for ((__i) = 0; (__i) < ARRAY_SIZE((connector)->encoder_ids) && \
		     (connector)->encoder_ids[(__i)] != 0; (__i)++) \
		for_each_if((encoder) = \
			    drm_encoder_find((connector)->dev, NULL, \
					     (connector)->encoder_ids[(__i)])) \

#endif

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
