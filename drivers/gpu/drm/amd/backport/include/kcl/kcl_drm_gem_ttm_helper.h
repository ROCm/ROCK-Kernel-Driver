/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copied from include/drm/drm_gem_ttm_helper.h */

#ifndef _KCL_KCL_DRM_GEM_TTM_HELPER_H_H
#define _KCL_KCL_DRM_GEM_TTM_HELPER_H_H

#include <drm/drm_gem.h>

#ifndef HAVE_DRM_GEM_TTM_VMAP
void _kcl_drm_gem_ttm_vunmap(struct drm_gem_object *gem,
			void *vaddr);

void *_kcl_drm_gem_ttm_vmap(struct drm_gem_object *obj);

static inline
void drm_gem_ttm_vunmap(struct drm_gem_object *gem,
			void *vaddr)
{
	_kcl_drm_gem_ttm_vunmap(gem, vaddr);
}

static inline
void *drm_gem_ttm_vmap(struct drm_gem_object *obj)
{
	return _kcl_drm_gem_ttm_vmap(obj);
}
#endif

#ifdef HAVE_STRUCT_DRM_DRV_GEM_OPEN_OBJECT_CALLBACK
void amdgpu_gem_object_free(struct drm_gem_object *obj);
int amdgpu_gem_object_open(struct drm_gem_object *obj,
				struct drm_file *file_priv);
void amdgpu_gem_object_close(struct drm_gem_object *obj,
				struct drm_file *file_priv);
#endif

#endif
