/*
 * arch/sh/pci/dma-dreamcast.c
 *
 * PCI DMA support for the Sega Dreamcast
 *
 * Copyright (C) 2001, 2002  M. R. Brown
 * Copyright (C) 2002, 2003  Paul Mundt
 *
 * This file originally bore the message (with enclosed-$):
 *	Id: pci.c,v 1.3 2003/05/04 19:29:46 lethal Exp
 *	Dreamcast PCI: Supports SEGA Broadband Adaptor only.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/pci.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mach/pci.h>

static int gapspci_dma_used = 0;

void *__pci_alloc_consistent(struct pci_dev *hwdev, size_t size,
			   dma_addr_t * dma_handle)
{
	unsigned long buf;

	if (gapspci_dma_used+size > GAPSPCI_DMA_SIZE)
		return NULL;

	buf = GAPSPCI_DMA_BASE+gapspci_dma_used;

	gapspci_dma_used = PAGE_ALIGN(gapspci_dma_used+size);
	
	*dma_handle = (dma_addr_t)buf;

	buf = P2SEGADDR(buf);

	/* Flush the dcache before we hand off the buffer */
	dma_cache_wback_inv((void *)buf, size);

	return (void *)buf;
}

void __pci_free_consistent(struct pci_dev *hwdev, size_t size,
			 void *vaddr, dma_addr_t dma_handle)
{
	/* XXX */
	gapspci_dma_used = 0;
}

