// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/kernel.h>

#include <drm/drm_gem.h>
#include <drm/drm_device.h>
#include <drm/ttm/ttm_bo_api.h>
#include <drm/ttm/ttm_bo_driver.h>
#include <kcl/kcl_dma-buf-map.h>
#include <drm/drm_gem_ttm_helper.h>

#ifndef drm_gem_ttm_of_gem
#define drm_gem_ttm_of_gem(gem_obj) \
	container_of(gem_obj, struct ttm_buffer_object, base)
#endif

#ifndef HAVE_DRM_GEM_TTM_VMAP
void *_kcl_drm_gem_ttm_vmap(struct drm_gem_object *obj)
{
	struct ttm_buffer_object *bo = drm_gem_ttm_of_gem(obj);
	struct dma_buf_map map;

	ttm_bo_vmap(bo, &map);
	return map.vaddr;
}
EXPORT_SYMBOL(_kcl_drm_gem_ttm_vmap);

void _kcl_drm_gem_ttm_vunmap(struct drm_gem_object *gem,
			void *vaddr)
{
	struct ttm_buffer_object *bo = drm_gem_ttm_of_gem(gem);
	struct dma_buf_map map;

	map.vaddr = vaddr;
	map.is_iomem = bo->resource->bus.is_iomem;

	ttm_bo_vunmap(bo, &map);
}
EXPORT_SYMBOL(_kcl_drm_gem_ttm_vunmap);
#endif

#ifndef HAVE_STRUCT_DRM_DRV_GEM_OPEN_OBJECT_CALLBACK
int _kcl_drm_gem_ttm_mmap(struct drm_gem_object *gem,
                     struct vm_area_struct *vma) {

        struct ttm_buffer_object *bo = drm_gem_ttm_of_gem(gem);
        int ret;

        ret = ttm_bo_mmap_obj(vma, bo);
        if (ret < 0)
                return ret;

        /*
         * ttm has its own object refcounting, so drop gem reference
         * to avoid double accounting counting.
         */
        drm_gem_object_put(gem);

        return 0;
}
EXPORT_SYMBOL(_kcl_drm_gem_ttm_mmap);
#endif
