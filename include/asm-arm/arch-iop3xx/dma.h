/*
 * linux/include/asm-arm/arch-iop80310/dma.h
 *
 *  Copyright (C) 2001 MontaVista Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _IOP310_DMA_H_
#define _IOP310_DMA_H_

/* 2 DMA on primary PCI and 1 on secondary for 80310 */
#define MAX_IOP310_DMA_CHANNEL   3
#define MAX_DMA_DESC        	64	/*128 */

/*
 * Make the generic DMA bits go away since we don't use it
 */
#define MAX_DMA_CHANNELS	0

#define MAX_DMA_ADDRESS		0xffffffff

#define IOP310_DMA_P0      	0
#define IOP310_DMA_P1		1
#define IOP310_DMA_S0      	2

#define DMA_MOD_READ        	0x0001
#define DMA_MOD_WRITE		0x0002
#define DMA_MOD_CACHED		0x0004
#define DMA_MOD_NONCACHED	0x0008


#define DMA_DESC_DONE   	0x0010
#define DMA_INCOMPLETE  	0x0020
#define DMA_HOLD		0x0040
#define DMA_END_CHAIN		0x0080
#define DMA_COMPLETE		0x0100
#define DMA_NOTIFY		0x0200
#define DMA_NEW_HEAD		0x0400

#define DMA_USER_MASK	(DMA_NOTIFY | DMA_INCOMPLETE | \
						 DMA_HOLD | DMA_COMPLETE)

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

#define DMA_USER_CMD_IE		0x00000001	/* user request int */
#define DMA_USER_END_CHAIN	0x00000002	/* end of sgl chain flag */

/* ATU defines */
#define     IOP310_ATUCR_PRIM_OUT_ENAB  /* Configuration   */      0x00000002
#define     IOP310_ATUCR_DIR_ADDR_ENAB  /* Configuration   */      0x00000080


typedef void (*dma_callback_t) (void *buf_context);
/*
 * DMA Descriptor
 */
typedef struct _dma_desc
{
	u32 NDAR;					/* next descriptor address */
	u32 PDAR;					/* PCI address */
	u32 PUADR;					/* upper PCI address */
	u32 LADR;					/* local address */
	u32 BC;						/* byte count */
	u32 DC;						/* descriptor control */
} dma_desc_t;

typedef struct _dma_sgl
{
	dma_desc_t dma_desc;		/* DMA descriptor pointer */
	u32 status;					/* descriptor status */
	void *data;					/* local virt */
	struct _dma_sgl *next;		/* next descriptor */
} dma_sgl_t;

/* dma sgl head */
typedef struct _dma_head
{
	u32 total;					/* total elements in SGL */
	u32 status;					/* status of sgl */
	u32 mode;					/* read or write mode */
	dma_sgl_t *list;			/* pointer to list */
	dma_callback_t callback;	/* callback function */
} dma_head_t;

/* function prototypes */
int dma_request(dmach_t, const char *);
int dma_queue_buffer(dmach_t, dma_head_t *);
int dma_suspend(dmach_t);
int dma_resume(dmach_t);
int dma_flush_all(dmach_t);
void dma_free(dmach_t);
void dma_set_irq_threshold(dmach_t, int);
dma_sgl_t *dma_get_buffer(dmach_t, int);
void dma_return_buffer(dmach_t, dma_sgl_t *);

#endif /* _ASM_ARCH_DMA_H */
