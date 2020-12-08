/* SPDX-License-Identifier: GPL-2.0 */
#include <drm/drm_drv.h>
#include "amdgpu.h"

#ifdef AMDKCL_DEVM_DRM_DEV_ALLOC
void * __devm_drm_dev_alloc(struct device *parent, struct drm_driver *driver,
															size_t size, size_t offset)
{
	void *container;
	struct drm_device *drm;
	int ret;

	container = kzalloc(size, GFP_KERNEL);
	if (!container)
		return ERR_PTR(-ENOMEM);

#ifdef HAVE_DRM_DRIVER_RELEASE
	drm = container + offset;
	ret = drm_dev_init(drm, driver, parent);
	if (ret) {
		drm_dev_put(drm);
		return ret;
	}
#ifdef HAVE_DRM_DRM_MANAGED_H
	drmm_add_final_kfree(ddev, adev);
#endif
#else
	drm = drm_dev_alloc(driver, parent);
	if (IS_ERR(drm))
		return PTR_ERR(drm);
	((struct amdgpu_device*)container)->ddev = drm;
#endif
	return container;
}
#endif
