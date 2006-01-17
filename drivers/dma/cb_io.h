/*****************************************************************************
Copyright(c) 2004 - 2006 Intel Corporation. All rights reserved.

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2 of the License, or (at your option)
any later version.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
more details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc., 59
Temple Place - Suite 330, Boston, MA  02111-1307, USA.

The full GNU General Public License is included in this distribution in the
file called LICENSE.
*****************************************************************************/
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

