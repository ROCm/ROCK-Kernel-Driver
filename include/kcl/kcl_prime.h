#ifndef AMDKCL_PRIME_H
#define AMDKCL_PRIME_H

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0)
#include <drm/drm_prime.h>
#else
#include <drm/drmP.h>
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 16, 0) && defined(BUILD_AS_DKMS)
extern int _kcl_drm_prime_sg_to_page_addr_arrays(struct sg_table *sgt, struct page **pages,
				     dma_addr_t *addrs, int max_entries);
#endif

static inline int
kcl_drm_prime_sg_to_page_addr_arrays(struct sg_table *sgt, struct page **pages,
				     dma_addr_t *addrs, int max_entries)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 16, 0) && defined(BUILD_AS_DKMS)
	return  _kcl_drm_prime_sg_to_page_addr_arrays(sgt, pages, addrs, max_entries);
#else
	return  drm_prime_sg_to_page_addr_arrays(sgt, pages, addrs, max_entries);
#endif
}

#endif /* AMDKCL_PRIME_H */
