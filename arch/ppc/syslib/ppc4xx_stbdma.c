/*
 * arch/ppc/syslib/ppc4xx_stbdma.c
 *
 * BRIEF MODULE DESCRIPTION
 *	IBM PPC4xx STBxxxx DMA Controller Functions
 *
 * Copyright 2002 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *         Armin Kuster <akuster@mvista.com>
 *   
 * Based on ppc4xx_dma.c by
 *         ppopov@mvista.com or source@mvista.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <asm/system.h>
#include <asm/io.h>
#include <linux/mm.h>
#include <linux/miscdevice.h>
#include <linux/init.h>
#include <linux/module.h>

#include <asm/ppc4xx_dma.h>

int
clr_dma_status(unsigned int dmanr)
{
	unsigned int control;
	ppc_dma_ch_t *p_dma_ch = &dma_channels[dmanr];

	p_dma_ch->int_enable = 1;

	control = mfdcr(DCRN_DMASR);
	switch (dmanr) {
	case 0:
		control |= DMA_CS0 & DMA_CH0_ERR & DMA_CT0;	
		break;
	case 1:
		control |= DMA_CS1 & DMA_CH1_ERR & DMA_CT1;
		break;
	case 2:
		control |= DMA_CS2 & DMA_CH2_ERR & DMA_CT2;
		break;
	case 3:
		control |= DMA_CS3 & DMA_CH3_ERR & DMA_CT3;
		break;
	default:
#ifdef DEBUG_4xxDMA
		printk("clr_dma_status: bad channel: %d\n", dmanr);
#endif
		return DMA_STATUS_BAD_CHANNEL;
	}
	mtdcr(DCRN_DMASR, control);
	return DMA_STATUS_GOOD;
}

/*
 * Maps a given port to a one of the dma
 * channels
 */
int
map_dma_port(unsigned int dmanr, unsigned int ocp_dma,short dma_chan)
{
	unsigned int map;
	int connect_port_to_chan, select; 

	ppc_dma_ch_t *p_dma_ch = &dma_channels[dmanr];
	
	connect_port_to_chan = ((ocp_dma & 0x7)*4);
	
	select = ocp_dma >> 3;
	switch (select) {
	case 0:
		map = mfdcr(DCRN_DMAS1);
		map |= (connect_port_to_chan << dma_chan);	/* */
		mtdcr(DCRN_DMAS1, map);
		break;
	case 1:
		map = mfdcr(DCRN_DMAS2);
		map |= (connect_port_to_chan << dma_chan);
		mtdcr(DCRN_DMAS2, map);
		break;
	default:
#ifdef DEBUG_4xxDMA
		printk("map_dma_port: bad channel: %d\n", dmanr);
#endif
		return DMA_STATUS_BAD_CHANNEL;
	}
	return DMA_STATUS_GOOD;
}

int
disable_dma_port(unsigned int dmanr, unsigned int ocp_dma,short dma_chan)
{
	unsigned int map;
	int connect_port_to_chan, select; 

	ppc_dma_ch_t *p_dma_ch = &dma_channels[dmanr];
	
	connect_port_to_chan = ((ocp_dma & 0x7)*4);
	
	select = ocp_dma >> 3;
	switch (select) {
	case 0:
		map = mfdcr(DCRN_DMAS1);
		map &= ~(connect_port_to_chan << dma_chan);	/* */
		mtdcr(DCRN_DMAS1, map);
		break;
	case 1:
		map = mfdcr(DCRN_DMAS2);
		map &= ~(connect_port_to_chan << dma_chan);
		mtdcr(DCRN_DMAS2, map);
		break;
	default:
#ifdef DEBUG_4xxDMA
		printk("disable_dma_port: bad channel: %d\n", dmanr);
#endif
		return DMA_STATUS_BAD_CHANNEL;
	}
	return DMA_STATUS_GOOD;
}

EXPORT_SYMBOL(disable_dma_port);
EXPORT_SYMBOL(map_dma_port);
EXPORT_SYMBOL(clr_dma_status);
