/*
 * Definitions shared between dma-sa1100.c and dma-sa1111.c
 * (C) 2000 Nicolas Pitre <nico@cam.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */


/*
 * DMA channel structure.
 */

typedef struct dma_buf_s dma_buf_t;

typedef struct {
	unsigned int lock;      /* Device is allocated */
	const char *device_id;  /* Device name */
	dma_buf_t *head;        /* where to insert buffers */
	dma_buf_t *tail;        /* where to remove buffers */
	dma_buf_t *curr;        /* buffer currently DMA'ed */
	int ready;              /* 1 if DMA can occur */
	int active;             /* 1 if DMA is actually processing data */
	dma_regs_t *regs;       /* points to appropriate DMA registers */
	int irq;                /* IRQ used by the channel */
	dma_callback_t callback;        /* ... to call when buffers are done */
	int spin_size;          /* > 0 when DMA should spin when no more buffer */
	dma_addr_t spin_addr;   /* DMA address to spin onto */
	int spin_ref;           /* number of spinning references */
#ifdef CONFIG_SA1111
	int dma_a, dma_b, last_dma;	/* SA-1111 specific */
#endif
} sa1100_dma_t;

extern sa1100_dma_t dma_chan[MAX_SA1100_DMA_CHANNELS];


int start_sa1111_sac_dma(sa1100_dma_t *dma, dma_addr_t dma_ptr, size_t size);
int sa1111_dma_get_current(dmach_t channel, void **buf_id, dma_addr_t *addr);
int sa1111_dma_stop(dmach_t channel);
int sa1111_dma_resume(dmach_t channel);
void sa1111_reset_sac_dma(dmach_t channel);
void sa1111_cleanup_sac_dma(dmach_t channel);

void sa1100_dma_done (sa1100_dma_t *dma);


#ifdef CONFIG_SA1111
#define channel_is_sa1111_sac(ch) \
	((ch) >= SA1111_SAC_DMA_BASE && \
	 (ch) <  SA1111_SAC_DMA_BASE + SA1111_SAC_DMA_CHANNELS)
#else
#define channel_is_sa1111_sac(ch) (0)
#endif

