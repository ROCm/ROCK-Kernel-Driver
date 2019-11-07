#ifndef AMDKCL_DMA_MAPPING_H
#define AMDKCL_DMA_MAPPING_H

#include <linux/dma-mapping.h>

/*
 * commit v4.8-11962-ga9a62c938441
 * dma-mapping: introduce the DMA_ATTR_NO_WARN attribute
 */
#ifndef DMA_ATTR_NO_WARN
#define DMA_ATTR_NO_WARN (0UL)
#endif
#endif
