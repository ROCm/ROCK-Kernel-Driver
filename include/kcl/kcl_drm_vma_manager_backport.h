#ifndef AMDKCL_DRM_VMA_MANAGER_H
#define AMDKCL_DRM_VMA_MANAGER_H

/* We make up offsets for buffer objects so we can recognize them at
 * mmap time. pgoff in mmap is an unsigned long, so we need to make sure
 * that the faked up offset will fit
 */
#include <drm/drm_vma_manager.h>
#include <kcl/kcl_drmP_h.h>

#if (BITS_PER_LONG == 64) && \
	(!defined(DRM_FILE_PAGE_OFFSET_SIZE) || \
	(DRM_FILE_PAGE_OFFSET_SIZE == ((0xFFFFFFFUL >> PAGE_SHIFT) * 16)))
#ifdef DRM_FILE_PAGE_OFFSET_START
#undef DRM_FILE_PAGE_OFFSET_START
#endif
#ifdef DRM_FILE_PAGE_OFFSET_SIZE
#undef DRM_FILE_PAGE_OFFSET_SIZE
#endif

#define DRM_FILE_PAGE_OFFSET_START ((0xFFFFFFFFUL >> PAGE_SHIFT) + 1)
#define DRM_FILE_PAGE_OFFSET_SIZE ((0xFFFFFFFFUL >> PAGE_SHIFT) * 256)

static inline void
kcl_drm_vma_offset_manager_init(struct drm_vma_offset_manager *mgr)
{
	drm_vma_offset_manager_destroy(mgr);
	drm_vma_offset_manager_init(mgr,
		DRM_FILE_PAGE_OFFSET_START,
		DRM_FILE_PAGE_OFFSET_SIZE);
}
#else
static inline void
kcl_drm_vma_offset_manager_init(struct drm_vma_offset_manager *mgr)
{
}
#endif

#ifndef HAVE_DRM_VMA_NODE_VERIFY_ACCESS_HAS_DRM_FILE
static inline int _kcl_drm_vma_node_verify_access(struct drm_vma_offset_node *node,
					     struct drm_file *tag)
{
	return drm_vma_node_verify_access(node, tag->filp);
}
#define drm_vma_node_verify_access _kcl_drm_vma_node_verify_access
#endif
#endif
