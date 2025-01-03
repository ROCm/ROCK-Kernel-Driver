#ifndef __KCL_BACKPORT_KCL_DRM_FBDEV_TTM_H__
#define __KCL_BACKPORT_KCL_DRM_FBDEV_TTM_H__

#include <drm/drm_fbdev_ttm.h>
#include <drm/drm_fb_helper.h>

#ifndef HAVE_DRM_DRM_FBDEV_TTM_H
static inline
void _kcl_drm_fbdev_ttm_setup(struct drm_device *dev, unsigned int preferred_bpp)
{
	return drm_fbdev_generic_setup(dev, preferred_bpp);
}
#define drm_fbdev_ttm_setup _kcl_drm_fbdev_ttm_setup
#endif

#endif
