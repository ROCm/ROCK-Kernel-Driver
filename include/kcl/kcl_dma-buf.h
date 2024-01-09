/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Pointer to dma-buf-mapped memory, plus helpers.
 * Copied from include/kcl/dma-buf.h
 */
#ifndef _KCL_KCL__DMA_BUF_H__H__
#define _KCL_KCL__DMA_BUF_H__H__

#include <linux/dma-buf.h>

#ifndef HAVE_DMA_BUF_IS_DYNAMIC
static inline bool dma_buf_is_dynamic(struct dma_buf *dmabuf)
{
	return false;
}
#endif

#endif