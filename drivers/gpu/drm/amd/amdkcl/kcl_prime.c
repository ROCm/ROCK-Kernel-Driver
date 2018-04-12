#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0)
#include <drm/drm_prime.h>
#else
#include <drm/drmP.h>
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 16, 0) && \
													defined(BUILD_AS_DKMS)
/**
 * drm_prime_sg_to_page_addr_arrays - convert an sg table into a page array
 * @sgt: scatter-gather table to convert
 * @pages: optional array of page pointers to store the page array in
 * @addrs: optional array to store the dma bus address of each page
 * @max_entries: size of both the passed-in arrays
 *
 * Exports an sg table into an array of pages and addresses. This is currently
 * required by the TTM driver in order to do correct fault handling.
 */
int _kcl_drm_prime_sg_to_page_addr_arrays(struct sg_table *sgt, struct page **pages,
				     dma_addr_t *addrs, int max_entries)
{
	unsigned count;
	struct scatterlist *sg;
	struct page *page;
	u32 len, index;
	dma_addr_t addr;

	index = 0;
	for_each_sg(sgt->sgl, sg, sgt->nents, count) {
		len = sg->length;
		page = sg_page(sg);
		addr = sg_dma_address(sg);

		while (len > 0) {
			if (WARN_ON(index >= max_entries))
				return -1;
			if (pages)
				pages[index] = page;
			if (addrs)
				addrs[index] = addr;

			page++;
			addr += PAGE_SIZE;
			len -= PAGE_SIZE;
			index++;
		}
	}
	return 0;
}
EXPORT_SYMBOL(_kcl_drm_prime_sg_to_page_addr_arrays);

#endif
