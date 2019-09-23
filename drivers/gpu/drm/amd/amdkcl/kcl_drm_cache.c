#include <kcl/kcl_drm_cache.h>
#include "kcl_common.h"

#include <drm/drmP.h>
#include <xen/xen.h>

#if !defined(HAVE_DRM_NEED_SWIOTLB)
bool drm_need_swiotlb(int dma_bits)
{
	struct resource *tmp;
	resource_size_t max_iomem = 0;

	/*
	 * Xen paravirtual hosts require swiotlb regardless of requested dma
	 * transfer size.
	 *
	 * NOTE: Really, what it requires is use of the dma_alloc_coherent
	 *       allocator used in ttm_dma_populate() instead of
	 *       ttm_populate_and_map_pages(), which bounce buffers so much in
	 *       Xen it leads to swiotlb buffer exhaustion.
	 */
	if (xen_pv_domain())
		return true;

	for (tmp = iomem_resource.child; tmp; tmp = tmp->sibling) {
		max_iomem = max(max_iomem,  tmp->end);
	}

	return max_iomem > ((u64)1 << dma_bits);
}
EXPORT_SYMBOL(drm_need_swiotlb);
#endif /* HAVE_DRM_NEED_SWIOTLB */

