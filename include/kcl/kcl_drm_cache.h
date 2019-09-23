#ifndef AMDKCL_DRM_CACHE_H
#define AMDKCL_DRM_CACHE_H
#include <drm/drm_cache.h>

#if !defined(HAVE_DRM_NEED_SWIOTLB)
bool drm_need_swiotlb(int dma_bits);
#endif /* HAVE_DRM_NEED_SWIOTLB */

#endif /* AMDKCL_DRM_CACHE_H */
