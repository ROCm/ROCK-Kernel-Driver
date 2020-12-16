/* SPDX-License-Identifier: MIT */
#ifndef AMDKCL_DRM_H
#define AMDKCL_DRM_H

#include <kcl/header/kcl_drmP_h.h>
#include <drm/drm_gem.h>
#include <kcl/header/kcl_drm_device_h.h>
#include <kcl/header/kcl_drm_print_h.h>

#if !defined(HAVE_DRM_GEM_OBJECT_PUT_LOCKED)
#if defined(HAVE_DRM_GEM_OBJECT_PUT_UNLOCKED)
static inline void
_kcl_drm_gem_object_put(struct drm_gem_object *obj)
{
	return drm_gem_object_put_unlocked(obj);
}
#else
static inline void
drm_gem_object_put(struct drm_gem_object *obj)
{
	return drm_gem_object_unreference_unlocked(obj);
}

static inline void
drm_gem_object_get(struct drm_gem_object *obj)
{
	kref_get(&obj->refcount);
}
#endif
#endif

#ifndef HAVE_DRM_DEV_PUT
static inline void drm_dev_get(struct drm_device *dev)
{
	drm_dev_ref(dev);
}

static inline void drm_dev_put(struct drm_device *dev)
{
	return drm_dev_unref(dev);
}
#endif

#endif /* AMDKCL_DRM_H */
