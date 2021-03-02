/* SPDX-License-Identifier: MIT */
#ifndef AMDGPU_BACKPORT_KCL_AMDGPU_TTM_H
#define AMDGPU_BACKPORT_KCL_AMDGPU_TTM_H
#include <drm/ttm/ttm_bo_api.h>
#include <drm/ttm/ttm_bo_driver.h>
#include <drm/ttm/ttm_placement.h>
#include "amdgpu.h"
#include "amdgpu_ttm.h"

#if !defined(HAVE_DRM_MM_PRINT)
extern struct drm_mm *kcl_ttm_range_res_manager_to_drm_mm(struct ttm_resource_manager *man);

static inline struct drm_mm *kcl_ttm_get_drm_mm_by_mem_type(struct amdgpu_device *adev, unsigned char ttm_pl)
{
	if (ttm_pl == TTM_PL_TT) {
		return &(adev->mman.gtt_mgr.mm);
	} else if (ttm_pl == TTM_PL_VRAM) {
		return &(adev->mman.vram_mgr.mm);
	} else {
		struct ttm_resource_manager *man = ttm_manager_type(&adev->mman.bdev, ttm_pl);
		return kcl_ttm_range_res_manager_to_drm_mm(man);
	}
}

static inline int kcl_amdgpu_mm_dump_table(struct seq_file *m, struct amdgpu_device *adev, unsigned char ttm_pl)
{
	struct drm_mm *mm = kcl_ttm_get_drm_mm_by_mem_type(adev, ttm_pl);
	struct ttm_bo_global *glob = &ttm_bo_glob;
	int ret;

	spin_lock(&glob->lru_lock);
	ret = drm_mm_dump_table(m, mm);
	spin_unlock(&glob->lru_lock);
	return ret;
}
#endif

#endif /* AMDGPU_BACKPORT_KCL_AMDGPU_TTM_H */
