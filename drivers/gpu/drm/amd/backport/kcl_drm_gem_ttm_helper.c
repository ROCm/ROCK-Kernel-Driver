// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/kernel.h>

#include <drm/drm_gem.h>
#include <drm/drm_device.h>
#include <drm/ttm/ttm_bo.h>
#include <kcl/kcl_dma-buf-map.h>
#include <kcl/kcl_iosys-map.h>
#include <drm/drm_gem_ttm_helper.h>

#ifndef drm_gem_ttm_of_gem
#define drm_gem_ttm_of_gem(gem_obj) \
	container_of(gem_obj, struct ttm_buffer_object, base)
#endif

#ifndef HAVE_DRM_GEM_OBJECT_FUNCS_VMAP_2ARGS
void *amdgpu_gem_prime_vmap(struct drm_gem_object *obj)
{
	struct ttm_buffer_object *bo = drm_gem_ttm_of_gem(obj);
	struct iosys_map map;

	ttm_bo_vmap(bo, &map);
	return map.vaddr;
}

void amdgpu_gem_prime_vunmap(struct drm_gem_object *gem,
			void *vaddr)
{
	struct ttm_buffer_object *bo = drm_gem_ttm_of_gem(gem);
	struct iosys_map map;

	map.vaddr = vaddr;
	map.is_iomem = bo->resource->bus.is_iomem;

	ttm_bo_vunmap(bo, &map);
}
#elif !defined(HAVE_DRM_GEM_OBJECT_FUNCS_VMAP_HAS_IOSYS_MAP_ARG)

int _kcl_drm_gem_ttm_vmap(struct drm_gem_object *gem,
                     struct dma_buf_map *map)
{
        struct ttm_buffer_object *bo = drm_gem_ttm_of_gem(gem);
	struct iosys_map iosys_map;
	int r;

	iosys_map.vaddr = map->vaddr;
	iosys_map.is_iomem = map->is_iomem;

        r = ttm_bo_vmap(bo, &iosys_map);

	map->vaddr = iosys_map.vaddr;
	map->is_iomem = iosys_map.is_iomem;
	return r;
}

void _kcl_drm_gem_ttm_vunmap(struct drm_gem_object *gem,
                        struct dma_buf_map *map)
{
        struct ttm_buffer_object *bo = drm_gem_ttm_of_gem(gem);
	struct iosys_map iosys_map;

	iosys_map.vaddr = map->vaddr;
	iosys_map.is_iomem = map->is_iomem;

        ttm_bo_vunmap(bo, &iosys_map);

	map->vaddr = iosys_map.vaddr;
	map->is_iomem = iosys_map.is_iomem;
}
#endif
