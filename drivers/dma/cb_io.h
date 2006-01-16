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
**   cb_io.h                                                         **
**                                                                   **
** Abstract:                                                         **
**                                                                   **
**********************************************************************/

#ifndef CB_IO_H
#define CB_IO_H

#include <asm/io.h>

/*
 * device and per-channel MMIO register read and write functions
 * this is a lot of anoying inline functions, but it's typesafe
 */

static inline u8 read_reg8(struct cb_device *device, unsigned int offset)
{
	return readb(device->reg_base + offset);
}

static inline u16 read_reg16(struct cb_device *device, unsigned int offset)
{
	return readw(device->reg_base + offset);
}

static inline u32 read_reg32(struct cb_device *device, unsigned int offset)
{
	return readl(device->reg_base + offset);
}

static inline void write_reg8(struct cb_device *device, unsigned int offset, u8 value)
{
	writeb(value, device->reg_base + offset);
}

static inline void write_reg16(struct cb_device *device, unsigned int offset, u16 value)
{
	writew(value, device->reg_base + offset);
}

static inline void write_reg32(struct cb_device *device, unsigned int offset, u32 value)
{
	writel(value, device->reg_base + offset);
}

static inline u8 chan_read_reg8(struct cb_dma_chan *chan, unsigned int offset)
{
	return readb(chan->reg_base + offset);
}

static inline u16 chan_read_reg16(struct cb_dma_chan *chan, unsigned int offset)
{
	return readw(chan->reg_base + offset);
}

static inline u32 chan_read_reg32(struct cb_dma_chan *chan, unsigned int offset)
{
	return readl(chan->reg_base + offset);
}

static inline void chan_write_reg8(struct cb_dma_chan *chan, unsigned int offset, u8 value)
{
	writeb(value, chan->reg_base + offset);
}

static inline void chan_write_reg16(struct cb_dma_chan *chan, unsigned int offset, u16 value)
{
	writew(value, chan->reg_base + offset);
}

static inline void chan_write_reg32(struct cb_dma_chan *chan, unsigned int offset, u32 value)
{
	writel(value, chan->reg_base + offset);
}

#if (BITS_PER_LONG == 64)
static inline u64 chan_read_reg64(struct cb_dma_chan *chan, unsigned int offset)
{
	return readq(chan->reg_base + offset);
}

static inline void chan_write_reg64(struct cb_dma_chan *chan, unsigned int offset, u64 value)
{
	writeq(value, chan->reg_base + offset);
}
#endif

#endif /* CB_IO_H */

