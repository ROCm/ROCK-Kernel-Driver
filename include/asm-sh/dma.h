/*
 * include/asm-sh/dma.h
 *
 * Copyright (C) 2003  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef __ASM_SH_DMA_H
#define __ASM_SH_DMA_H

#include <linux/config.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <asm/cpu/dma.h>
#include <asm/semaphore.h>

/* The maximum address that we can perform a DMA transfer to on this platform */
/* Don't define MAX_DMA_ADDRESS; it's useless on the SuperH and any
   occurrence should be flagged as an error.  */
/* But... */
/* XXX: This is not applicable to SuperH, just needed for alloc_bootmem */
#define MAX_DMA_ADDRESS		(PAGE_OFFSET+0x10000000)

#ifdef CONFIG_NR_DMA_CHANNELS
#  define MAX_DMA_CHANNELS	(CONFIG_NR_DMA_CHANNELS)
#else
#  define MAX_DMA_CHANNELS	(CONFIG_NR_ONCHIP_DMA_CHANNELS)
#endif

/* 
 * Read and write modes can mean drastically different things depending on the
 * channel configuration. Consult your DMAC documentation and module
 * implementation for further clues.
 */
#define DMA_MODE_READ		0x00
#define DMA_MODE_WRITE		0x01
#define DMA_MODE_MASK		0x01

extern spinlock_t dma_spin_lock;

struct dma_info;

struct dma_ops {
	const char *name;

	int (*request)(struct dma_info *info);
	void (*free)(struct dma_info *info);

	int (*get_residue)(struct dma_info *info);
	int (*xfer)(struct dma_info *info);
	void (*configure)(struct dma_info *info, unsigned long flags);
};

struct dma_info {
	const char *dev_id;

	unsigned int chan;
	unsigned int mode;
	unsigned int count;
	
	unsigned long sar;
	unsigned long dar;

	unsigned int configured:1;
	unsigned int tei_capable:1;
	atomic_t busy;

	struct semaphore sem;
	wait_queue_head_t wait_queue;
	struct dma_ops *ops;
};

/* arch/sh/drivers/dma/dma-api.c */
extern int dma_xfer(unsigned int chan, unsigned long from,
		    unsigned long to, size_t size, unsigned int mode);

#define dma_write(chan, from, to, size)	\
	dma_xfer(chan, from, to, size, DMA_MODE_WRITE)
#define dma_write_page(chan, from, to)	\
	dma_write(chan, from, to, PAGE_SIZE)

#define dma_read(chan, from, to, size)	\
	dma_xfer(chan, from, to, size, DMA_MODE_READ)
#define dma_read_page(chan, from, to)	\
	dma_read(chan, from, to, PAGE_SIZE)

extern int request_dma(unsigned int chan, const char *dev_id);
extern void free_dma(unsigned int chan);
extern int get_dma_residue(unsigned int chan);
extern struct dma_info *get_dma_info(unsigned int chan);
extern void dma_wait_for_completion(unsigned int chan);
extern void dma_configure_channel(unsigned int chan, unsigned long flags);

extern int register_dmac(struct dma_ops *ops);

extern struct dma_info dma_info[];

#ifdef CONFIG_PCI
extern int isa_dma_bridge_buggy;
#else
#define isa_dma_bridge_buggy 	(0)
#endif

#endif /* __ASM_SH_DMA_H */
