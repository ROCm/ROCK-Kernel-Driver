#ifndef AMDKCL_DRM_PLANE_HELPER_H
#define AMDKCL_DRM_PLANE_HELPER_H

#include <drm/drm_plane_helper.h>

#ifndef HAVE_DRM_PLANE_HELPER_DESTROY
static inline void kcl_drm_plane_helper_destroy(struct drm_plane *plane)
{
	drm_plane_cleanup(plane);
	kfree(plane);
}
#define drm_plane_helper_destroy kcl_drm_plane_helper_destroy
#endif
#endif