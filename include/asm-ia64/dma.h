#ifndef _ASM_IA64_DMA_H
#define _ASM_IA64_DMA_H

/*
 * Copyright (C) 1998, 1999 Hewlett-Packard Co
 * Copyright (C) 1998, 1999 David Mosberger-Tang <davidm@hpl.hp.com>
 */

#include <linux/config.h>
#include <linux/spinlock.h>	/* And spinlocks */
#include <linux/delay.h>

#include <asm/io.h>		/* need byte IO */

#ifdef HAVE_REALLY_SLOW_DMA_CONTROLLER
#define dma_outb	outb_p
#else
#define dma_outb	outb
#endif

#define dma_inb		inb

#define MAX_DMA_CHANNELS	8
#define MAX_DMA_ADDRESS		0xffffffffUL

extern spinlock_t  dma_spin_lock;

/* From PCI */

#ifdef CONFIG_PCI
extern int isa_dma_bridge_buggy;
#else
#define isa_dma_bridge_buggy 	(0)
#endif

#endif /* _ASM_IA64_DMA_H */
