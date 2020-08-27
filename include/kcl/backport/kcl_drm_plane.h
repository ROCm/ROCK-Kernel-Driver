/* SPDX-License-Identifier: MIT */
#ifndef KCL_BACKPORT_KCL_DRM_PLANE_H
#define KCL_BACKPORT_KCL_DRM_PLANE_H

#include <kcl/header/kcl_drm_plane_h.h>

#ifndef HAVE_DRM_UNIVERSAL_PLANE_INIT_9ARGS
static inline int _kcl_drm_universal_plane_init(struct drm_device *dev, struct drm_plane *plane,
			     unsigned long possible_crtcs,
			     const struct drm_plane_funcs *funcs,
			     const uint32_t *formats, unsigned int format_count,
			     const uint64_t *format_modifiers,
			     enum drm_plane_type type,
			     const char *name, ...)
{
#if defined(HAVE_DRM_UNIVERSAL_PLANE_INIT_8ARGS)
	return drm_universal_plane_init(dev, plane, possible_crtcs, funcs,
			 formats, format_count, type, name);
#else
	return drm_universal_plane_init(dev, plane, possible_crtcs, funcs,
			 formats, format_count, type);
#endif
}
#define drm_universal_plane_init _kcl_drm_universal_plane_init
#endif

#endif
