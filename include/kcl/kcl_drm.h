#ifndef AMDKCL_DRM_H
#define AMDKCL_DRM_H

#include <linux/version.h>
#include <drm/drmP.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_atomic.h>
#include <drm/drm_gem.h>
#include <drm/drm_atomic_helper.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0)  && \
	!defined(OS_NAME_UBUNTU)
extern int drm_pcie_get_max_link_width(struct drm_device *dev, u32 *mlw);
#endif

#endif /* AMDKCL_DRM_H */
