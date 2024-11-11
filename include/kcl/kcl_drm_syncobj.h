/* SPDX-License-Identifier: GPL-2.0 OR MIT */

#ifndef AMDKCL_DRM_SYNCOBJ_H
#define AMDKCL_DRM_SYNCOBJ_H

#include <drm/drm_syncobj.h>
#include <kcl/kcl_dma_fence.h>
#include <kcl/kcl_dma_fence_chain.h>

#ifndef HAVE_DRM_SYNCOBJ_ADD_POINT
void _kcl_drm_syncobj_add_point(struct drm_syncobj *syncobj,
			   struct dma_fence_chain *chain,
			   struct dma_fence *fence,
			   uint64_t point);

#define drm_syncobj_add_point _kcl_drm_syncobj_add_point
#endif

#endif
