/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Enterprise Fibre Channel Host Bus Adapters.                     *
 * Refer to the README file included with this package for         *
 * driver version and adapter support.                             *
 * Copyright (C) 2004 Emulex Corporation.                          *
 * www.emulex.com                                                  *
 *                                                                 *
 * This program is free software; you can redistribute it and/or   *
 * modify it under the terms of the GNU General Public License     *
 * as published by the Free Software Foundation; either version 2  *
 * of the License, or (at your option) any later version.          *
 *                                                                 *
 * This program is distributed in the hope that it will be useful, *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of  *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the   *
 * GNU General Public License for more details, a copy of which    *
 * can be found in the file COPYING included with this package.    *
 *******************************************************************/

#ifndef _H_ELX_MEM
#define _H_ELX_MEM

/*
 * This structure is used when allocating a buffer pool.
 */
struct mbuf_info {
	uint32_t size;		/* Specifies the number of bytes to allocate. */
	uint32_t align;		/* The desired address boundary. */

	uint32_t flags;
#define ELX_MBUF_VIRT       0x0	/* virtual memory (unmapped) */
#define ELX_MBUF_DMA        0x1	/* blocks are for DMA */
#define ELX_MBUF_PHYSONLY   0x2	/* For malloc - map a given virtual address
				 * to physical (skip the malloc). For free -
				 * just unmap the given physical address
				 * (skip the free).
				 */
#define ELX_MBUF_MASK       0x3	/* Mask for flags */
#define ELX_MBUF_SLEEP      0x8	/* sleep - no sleep */

	void *virt;		/* specifies the virtual buffer pointer */
	elx_dma_addr_t phys;	/* specifies the physical buffer pointer */
	elx_acc_handle_t data_handle;
	elx_dma_handle_t dma_handle;
};
typedef struct mbuf_info MBUF_INFO_t;

#define  ELX_MEM_DBUF     1	/* Use DMABUF_t for referencing DMAable memory */
#define  ELX_MEM_DBUFEXT  2	/* Use DMABUFEXT_t for referencing DMAable memory */

struct elx_dmabuf {
	struct elx_dmabuf *next;
	void *virt;		/* virtual address ptr */
	elx_dma_addr_t phys;	/* mapped address */
	elx_acc_handle_t data_handle;
	elx_dma_handle_t dma_handle;
};
typedef struct elx_dmabuf DMABUF_t;

struct elx_dmabufext {
	DMABUF_t dma;
	uint32_t size;
	uint32_t flag;
};
typedef struct elx_dmabufext DMABUFEXT_t;

struct elx_dmabufip {
	DMABUF_t dma;
	void *ipbuf;
};
typedef struct elx_dmabufip DMABUFIP_t;

#define MEM_BUF          0	/* memory seg to hold buffer data   */
#define MEM_BPL          0	/* and to hold buffer ptr lists - SLI2   */
#define MEM_MBOX         1	/* memory seg to hold mailbox cmds  */
#define MEM_IOCB         2	/* memory seg to hold iocb commands */
#define MEM_CLOCK        3	/* memory seg to hold clock blocks */
#define MEM_SCSI_BUF     4	/* memory seg for scsi buffer for each I/O */

#define MEM_FCP_CMND_BUF 5

#define MEM_NLP          6	/* memory seg to hold node list entries */
#define MEM_BIND         7	/* memory seg to hold bind list entries */
#define MEM_IP_BUF       8	/* memory seg for ip buffer for each I/O */
#define MEM_IP_RCV_BUF   9	/* memory seg for ip rcv buffers */
#define MEM_IP_MAP       10	/* memory seg for ip net->phys/dma buffers. */
#define MEM_SCSI_DMA_EXT 11	/* and to hold SCSI fcp_cmnd, fcp_rsp, bpl */
#define MEM_IP_DMA_EXT   12	/* and to hold IP network header and bpl */
#define ELX_MAX_SEG      13

#define MEM_SEG_MASK    0xff	/* mask used to mask off the priority bit */
#define MEM_PRI         0x100	/* Priority bit: set to exceed low water */

struct elx_memseg {
	ELX_SLINK_t mem_hdr;
	uint16_t elx_memsize;	/* size of memory blocks */
	uint16_t elx_memflag;	/* what to do when list is exhausted */
	uint16_t elx_lowmem;	/* low water mark, used w/MEM_PRI flag */
	uint16_t elx_himem;	/* high water mark */
};
typedef struct elx_memseg MEMSEG_t;

#define ELX_MEM_ERR          0x1	/* return error memflag */
#define ELX_MEM_GETMORE      0x2	/* get more memory memflag */
#define ELX_MEM_DMA          0x4	/* blocks are for DMA */
#define ELX_MEM_LOWHIT       0x8	/* low water mark was hit */
#define ELX_MEMPAD           0x10	/* offset used for a FC_MEM_DMA buffer */
#define ELX_MEM_ATTACH_IPBUF 0x20	/* attach a system IP buffer */
#define ELX_MEM_BOUND        0x40	/* has a upper bound */

#define ELX_MIN_POOL_GROWTH  32

#endif				/* _H_ELX_MEM */
