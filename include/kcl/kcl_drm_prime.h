/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef __KCL_DRM_PRIME_H__
#define __KCL_DRM_PRIME_H__

#include <linux/mutex.h>
#include <linux/rbtree.h>
#include <linux/scatterlist.h>

#ifndef HAVE_DRM_PRIME_SG_TO_DMA_ADDR_ARRAY
static inline
int drm_prime_sg_to_dma_addr_array(struct sg_table *sgt, dma_addr_t *addrs,
				   int max_entries)
{
	return drm_prime_sg_to_page_addr_arrays(sgt, NULL, addrs, max_entries);

}
#endif
#endif
