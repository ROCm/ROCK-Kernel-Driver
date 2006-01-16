/**********************************************************************
**                                                                   **
**                I N T E L   P R O P R I E T A R Y                  **
**                                                                   **
**   COPYRIGHT (c) 2004 - 2005  BY  INTEL  CORPORATION.  ALL         **
**   RIGHTS RESERVED.   NO PART OF THIS PROGRAM OR PUBLICATION MAY   **
**   BE  REPRODUCED,   TRANSMITTED,   TRANSCRIBED,   STORED  IN  A   **
**   RETRIEVAL SYSTEM, OR TRANSLATED INTO ANY LANGUAGE OR COMPUTER   **
**   LANGUAGE IN ANY FORM OR BY ANY MEANS, ELECTRONIC, MECHANICAL,   **
**   MAGNETIC,  OPTICAL,  CHEMICAL, MANUAL, OR OTHERWISE,  WITHOUT   **
**   THE PRIOR WRITTEN PERMISSION OF :                               **
**                                                                   **
**                      INTEL  CORPORATION                           **
**                                                                   **
**                2200 MISSION COLLEGE BOULEVARD                     **
**                                                                   **
**             SANTA  CLARA,  CALIFORNIA  95052-8119                 **
**                                                                   **
**********************************************************************/

/**********************************************************************
**                                                                   **
** INTEL CORPORATION PROPRIETARY INFORMATION                         **
** This software is supplied under the terms of a license agreement  **
** with Intel Corporation and may not be copied nor disclosed        **
** except in accordance with the terms of that agreement.            **
**                                                                   **
** Module Name:                                                      **
**   ioatdma.h                                                       **
**                                                                   **
** Abstract:                                                         **
**                                                                   **
**********************************************************************/

#ifndef IOATDMA_H
#define IOATDMA_H

#include <linux/dmaengine.h>
#include "cb_hw.h"
#include <linux/init.h>
#include <linux/dmapool.h>
#include <linux/cache.h>

#define PCI_DEVICE_ID_INTEL_CB		0x1a38

#define CB_LOW_COMPLETION_MASK		0xffffffc0

extern struct list_head dma_device_list;
extern struct list_head dma_client_list;

/**
 * struct cb_device - internal representation of a CB device
 * @pdev: PCI-Express device
 * @reg_base: MMIO register space base address
 * @dma_pool: for allocating DMA descriptors
 * @common: embedded struct dma_device
 * @msi: Message Signaled Interrupt number
 */

struct cb_device {
	struct pci_dev *pdev;
	void *reg_base;
	struct dma_pool *dma_pool;

	struct dma_device common;
	u8 msi;

	struct cb_dma_chan *idx[4];
};

/**
 * struct cb_dma_chan - internal representation of a DMA channel
 * @device:
 * @reg_base:
 * @sw_in_use:
 * @completion:
 * @completion_low:
 * @completion_high:
 * @completed_cookie: last cookie seen completed on cleanup
 * @cookie: value of last cookie given to client
 * @last_completion:
 * @xfercap:
 * @desc_lock:
 * @free_desc:
 * @used_desc:
 * @resource:
 * @device_node:
 */

struct cb_dma_chan {

	void *reg_base;

	dma_cookie_t completed_cookie;
	unsigned long last_completion;

	u32 running;
	u32 xfercap;	/* XFERCAP register value expanded out */

	spinlock_t cleanup_lock;
	spinlock_t desc_lock;
	struct list_head free_desc;
	struct list_head used_desc;

	int pending;

	struct cb_device *device;
	struct dma_chan common;

	union {
		u64 completion; /* HW completion writeback */
		struct {
			u32 completion_low;
			u32 completion_high;
		};
	} ____cacheline_aligned;
	char padding ____cacheline_aligned;
};

/* wrapper around hardware descriptor format + additional software fields */

/**
 * struct cb_desc_sw - wrapper around hardware descriptor
 * @hw: hardware DMA descriptor
 * @node:
 * @cookie:
 * @phys:
 */

struct cb_desc_sw {
	struct cb_dma_descriptor *hw;
	struct list_head node;
	dma_cookie_t cookie;
	dma_addr_t phys;
	/* these should do nothing on the arch we expect to find this device on */
	DECLARE_PCI_UNMAP_ADDR(src)
	DECLARE_PCI_UNMAP_LEN(src_len)
	DECLARE_PCI_UNMAP_ADDR(dst)
	DECLARE_PCI_UNMAP_LEN(dst_len)
};

#endif /* IOATDMA_H */

