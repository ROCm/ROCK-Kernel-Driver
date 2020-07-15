/* SPDX-License-Identifier: MIT */
#ifndef AMDKCL_DRM_CONNECTOR_H
#define AMDKCL_DRM_CONNECTOR_H

#include <drm/drm_crtc.h>
#include <kcl/header/kcl_drm_connector_h.h>
#include <kcl/kcl_kref.h>
#include <kcl/kcl_drm.h>

#ifndef HAVE_DRM_CONNECTOR_UPDATE_EDID_PROPERTY
#define drm_connector_update_edid_property drm_mode_connector_update_edid_property
#endif
#ifndef HAVE_DRM_CONNECTOR_ATTACH_ENCODER
#define drm_connector_attach_encoder drm_mode_connector_attach_encoder
#endif
#ifndef HAVE_DRM_CONNECTOR_SET_PATH_PROPERTY
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

#ifndef HAVE_DRM_CONNECTOR_INIT_WITH_DDC
int _kcl_drm_connector_init_with_ddc(struct drm_device *dev,
				struct drm_connector *connector,
				const struct drm_connector_funcs *funcs,
				int connector_type,
				struct i2c_adapter *ddc);
static inline
int drm_connector_init_with_ddc(struct drm_device *dev,
				struct drm_connector *connector,
				const struct drm_connector_funcs *funcs,
				int connector_type,
				struct i2c_adapter *ddc)
{
	return _kcl_drm_connector_init_with_ddc(dev, connector, funcs, connector_type, ddc);
}
#endif
#endif /* AMDKCL_DRM_CONNECTOR_H */
