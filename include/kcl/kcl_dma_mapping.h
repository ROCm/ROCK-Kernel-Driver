#ifndef AMDKCL_DMA_MAPPING_H
#define AMDKCL_DMA_MAPPING_H

#include <linux/dma-mapping.h>
#include <linux/version.h>

/*
 * commit v4.8-11962-ga9a62c938441
 * dma-mapping: introduce the DMA_ATTR_NO_WARN attribute
 */
#ifndef DMA_ATTR_NO_WARN
#define DMA_ATTR_NO_WARN (0UL)
#endif

/*
* commit v5.3-rc1-57-g06532750010e
* dma-mapping: use dma_get_mask in dma_addressing_limited
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)
#define AMDKCL_DMA_ADDRESSING_LIMITED_WORKAROUND
#endif
#endif
