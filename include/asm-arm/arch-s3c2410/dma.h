/* linux/include/asm-arm/arch-bast/dma.h
 *
 * Copyright (C) 2003 Simtec Electronics
 *  Ben Dooks <ben@simtec.co.uk>
 *
 * Samsung S3C2410X DMA support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Changelog:
 *  ??-May-2003 BJD   Created file
 *  ??-Jun-2003 BJD   Added more dma functionality to go with arch
*/


#ifndef __ASM_ARCH_DMA_H
#define __ASM_ARCH_DMA_H

#include <linux/config.h>
#include "hardware.h"


/*
 * This is the maximum DMA address(physical address) that can be DMAd to.
 *
 */
#define MAX_DMA_ADDRESS		0x20000000
#define MAX_DMA_TRANSFER_SIZE	0x100000 /* Data Unit is half word  */


/* according to the samsung port, we cannot use the regular
 * dma channels... we must therefore provide our own interface
 * for DMA, and allow our drivers to use that.
 */

#define MAX_DMA_CHANNELS	0


/* we have 4 dma channels */
#define S3C2410_DMA_CHANNELS	    (4)


/* dma buffer */

typedef struct s3c2410_dma_buf_s s3c2410_dma_buf_t;

struct s3c2410_dma_buf_s {
	s3c2410_dma_buf_t   *next;
	int		     magic;	   /* magic */
	int		     size;	   /* buffer size in bytes */
	dma_addr_t	     data;	   /* start of DMA data */
	dma_addr_t	     ptr;	   /* where the DMA got to [1] */
	int		     ref;
	void		     *id;	   /* client's id */
	unsigned char	     no_callback;  /* disable callback for buffer */
};

/* [1] is this updated for both recv/send modes? */

typedef struct s3c2410_dma_chan_s s3c2410_dma_chan_t;

typedef void (*s3c2410_dma_cbfn_t)(s3c2410_dma_chan_t *, void *buf, int size);
typedef void (*s3c2410_dma_enfn_t)(s3c2410_dma_chan_t *, int on);
typedef void (*s3c2410_dma_pausefn_t)(s3c2410_dma_chan_t *, int on);

struct s3c2410_dma_chan_s {
	/* channel state flags */
	unsigned char	       number;	      /* number of this dma channel */
	unsigned char	       in_use;	      /* channel allocated */
	unsigned char	       started;	      /* channel has been started */
	unsigned char	       stopped;	      /* channel stopped */
	unsigned char	       sleeping;
	unsigned char	       xfer_unit;     /* size of an transfer */
	unsigned char	       irq_claimed;

	/* channel's hardware position and configuration */
	unsigned long	       regs;	      /* channels registers */
	unsigned int	       irq;	      /* channel irq */
	unsigned long	       addr_reg;      /* data address register for buffs */
	unsigned long	       dcon;	      /* default value of DCON */

	/* driver handlers for channel */
	s3c2410_dma_cbfn_t     callback_fn;   /* callback function for buf-done */
	s3c2410_dma_enfn_t     enable_fn;     /* channel enable function */
	s3c2410_dma_pausefn_t  pause_fn;      /* channel pause function */

	/* buffer list and information */
	s3c2410_dma_buf_t      *curr;	      /* current dma buffer */
	s3c2410_dma_buf_t      *next;	      /* next buffer to load */
	s3c2410_dma_buf_t      *end;	      /* end of queue */

	int		       queue_count;   /* number of items in queue */
	int		       loaded_count;  /* number of loaded buffers */
};

/* note, we don't really use dma_deivce_t at the moment */
typedef unsigned long dma_device_t;

typedef enum s3c2410_dmasrc_e s3c2410_dmasrc_t;

/* these two defines control the source for the dma channel,
 * wether it is from memory or an device
*/

enum s3c2410_dmasrc_e {
  S3C2410_DMASRC_HW,	  /* source is memory */
  S3C2410_DMASRC_MEM	  /* source is hardware */
};

/* dma control routines */

extern int s3c2410_request_dma(dmach_t channel, const char *devid, void *dev);
extern int s3c2410_free_dma(dmach_t channel);
extern int s3c2410_dma_flush_all(dmach_t channel);

extern int s3c2410_dma_stop(dmach_t channel);
extern int s3c2410_dma_resume(dmach_t channel);

extern int s3c2410_dma_queue(dmach_t channel, void *id,
			     dma_addr_t data, int size);

#define s3c2410_dma_queue_buffer s3c2410_dma_queue

/* channel configuration */

extern int s3c2410_dma_config(dmach_t channel, int xferunit, int dcon);

extern int s3c2410_dma_devconfig(int channel, s3c2410_dmasrc_t source,
				 int hwcfg, unsigned long devaddr);

extern int s3c2410_dma_set_enablefn(dmach_t, s3c2410_dma_enfn_t rtn);
extern int s3c2410_dma_set_pausefn(dmach_t, s3c2410_dma_pausefn_t rtn);
extern int s3c2410_dma_set_callbackfn(dmach_t, s3c2410_dma_cbfn_t rtn);

#define s3c2410_dma_set_callback s3c2410_dma_set_callbackfn

#define S3C2410_DMA_DISRC	(0x00)
#define S3C2410_DMA_DISRCC	(0x04)
#define S3C2410_DMA_DIDST	(0x08)
#define S3C2410_DMA_DIDSTC	(0x0C)
#define S3C2410_DMA_DCON	(0x10)
#define S3C2410_DMA_DSTAT	(0x14)
#define S3C2410_DMA_DCSRC	(0x18)
#define S3C2410_DMA_DCDST	(0x1C)
#define S3C2410_DMA_DMASKTRIG	(0x20)

#define S3C2410_DMASKTRIG_STOP	 (1<<2)
#define S3C2410_DMASKTRIG_ON	 (1<<1)
#define S3C2410_DMASKTRIG_SWTRIG (1<<0)

#define S3C2410_DCOM_DEMAND	(0<<31)
#define S3C2410_DCON_HANDSHAKE  (1<<31)
#define S3C2410_DCON_SYNC_PCLK  (0<<30)
#define S3C2410_DCON_SYNC_HCLK  (1<<30)

#define S3C2410_DCON_INTREQ	(1<<29)

#define S3C2410_DCON_SRCSHIFT	(24)

#define S3C2410_DCON_BYTE	(0<<20)
#define S3C2410_DCON_HALFWORD	(1<<20)
#define S3C2410_DCON_WORD	(2<<20)

#define S3C2410_DCON_AUTORELOAD (0<<22)
#define S3C2410_DCON_HWTRIG	(1<<23)

#endif /* __ASM_ARCH_DMA_H */
