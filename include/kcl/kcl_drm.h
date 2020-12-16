/* SPDX-License-Identifier: MIT */
#ifndef AMDKCL_DRM_H
#define AMDKCL_DRM_H

#include <kcl/header/kcl_drmP_h.h>
#include <drm/drm_gem.h>
#include <kcl/header/kcl_drm_device_h.h>
#include <kcl/header/kcl_drm_print_h.h>


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
