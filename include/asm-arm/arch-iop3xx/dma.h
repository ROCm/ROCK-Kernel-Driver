/*
 * linux/include/asm-arm/arch-iop3xx/dma.h
 *
 *  Copyright (C) 2004 Intel Corp.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _IOP3XX_DMA_H_P
#define _IOP3XX_DMA_H_P

/* 80310 not supported */
#define MAX_IOP3XX_DMA_CHANNEL   2
#define MAX_DMA_DESC        	64	/*128 */

#define DMA_FREE         0x0
#define DMA_ACTIVE       0x1
#define DMA_COMPLETE     0x2
#define DMA_ERROR        0x4

/*
 * Make the generic DMA bits go away since we don't use it
 */
#define MAX_DMA_CHANNELS	0

#define MAX_DMA_ADDRESS		0xffffffff

#define DMA_POLL         0x0
#define DMA_INTERRUPT    0x1

#define DMA_DCR_MTM     0x00000040  /* memory to memory transfer */
#define DMA_DCR_DAC		0x00000020	/* Dual Addr Cycle Enab */
#define DMA_DCR_IE		0x00000010	/* Interrupt Enable */
#define DMA_DCR_PCI_IOR		0x00000002	/* I/O Read */
#define DMA_DCR_PCI_IOW		0x00000003	/* I/O Write */
#define DMA_DCR_PCI_MR		0x00000006	/* Memory Read */
#define DMA_DCR_PCI_MW		0x00000007	/* Memory Write */
#define DMA_DCR_PCI_CR		0x0000000A	/* Configuration Read */
#define DMA_DCR_PCI_CW		0x0000000B	/* Configuration Write */
#define DMA_DCR_PCI_MRM		0x0000000C	/* Memory Read Multiple */
#define DMA_DCR_PCI_MRL		0x0000000E	/* Memory Read Line */
#define DMA_DCR_PCI_MWI		0x0000000F	/* Mem Write and Inval */


//extern iop3xx_dma_t dma_chan[2];
/* function prototypes */
#ifdef CONFIG_IOP3XX_DMACOPY
extern int iop_memcpy;
void * dma_memcpy(void * to, const void* from, __kernel_size_t n);
#endif

#endif /* _ASM_ARCH_DMA_H_P */
