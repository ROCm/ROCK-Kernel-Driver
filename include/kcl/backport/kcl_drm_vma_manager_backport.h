/* SPDX-License-Identifier: MIT */
#ifndef AMDKCL_DRM_VMA_MANAGER_H
#define AMDKCL_DRM_VMA_MANAGER_H

#include <drm/drm_vma_manager.h>
#include <kcl/header/kcl_drmP_h.h>

#ifndef HAVE_DRM_VMA_NODE_VERIFY_ACCESS_HAS_DRM_FILE
static inline int _kcl_drm_vma_node_verify_access(struct drm_vma_offset_node *node,
					     struct drm_file *tag)
{
	return drm_vma_node_verify_access(node, tag->filp);
}
#define drm_vma_node_verify_access _kcl_drm_vma_node_verify_access
#endif
#endif
