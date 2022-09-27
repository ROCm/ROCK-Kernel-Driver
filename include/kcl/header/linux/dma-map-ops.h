/* SPDX-License-Identifier: MIT */
#ifndef _KCL_HEADER__LINUX_DMA_MAP_OPS_H_H_
#define _KCL_HEADER__LINUX_DMA_MAP_OPS_H_H_

#if defined(HAVE_LINUX_DMA_MAP_OPS_H)
#include_next <linux/dma-map-ops.h>
#else
#include <linux/dma-mapping.h>
#endif

#endif
