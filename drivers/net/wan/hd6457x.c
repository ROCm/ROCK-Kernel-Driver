/*
 * Hitachi SCA HD64570 and HD64572 common driver for Linux
 *
 * Copyright (C) 1998-2000 Krzysztof Halasa <khc@pm.waw.pl>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Sources of information:
 *    Hitachi HD64570 SCA User's Manual
 *    Hitachi HD64572 SCA-II User's Manual
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/in.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/ioport.h>

#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#include <linux/netdevice.h>
#include <linux/skbuff.h>

#include <linux/hdlc.h>

#if (!defined (__HD64570_H) && !defined (__HD64572_H)) || \
    (defined (__HD64570_H) && defined (__HD64572_H))
#error Either hd64570.h or hd64572.h must be included
#endif


static card_t *first_card;
static card_t **new_card = &first_card;


/* Maximum events to handle at each interrupt - should I increase it? */
#define INTR_WORK 4

#define get_msci(port)	  (phy_node(port) ?   MSCI1_OFFSET :   MSCI0_OFFSET)
#define get_dmac_rx(port) (phy_node(port) ? DMAC1RX_OFFSET : DMAC0RX_OFFSET)
#define get_dmac_tx(port) (phy_node(port) ? DMAC1TX_OFFSET : DMAC0TX_OFFSET)

#define SCA_INTR_MSCI(node)    (node ? 0x10 : 0x01)
#define SCA_INTR_DMAC_RX(node) (node ? 0x20 : 0x02)
#define SCA_INTR_DMAC_TX(node) (node ? 0x40 : 0x04)

#ifdef __HD64570_H /* HD64570 */
#define sca_outa(value, reg, card)	sca_outw(value, reg, card)
#define sca_ina(reg, card)		sca_inw(reg, card)
#define writea(value, ptr)		writew(value, ptr)

static inline int sca_intr_status(card_t *card)
{
	u8 isr0 = sca_in(ISR0, card);
	u8 isr1 = sca_in(ISR1, card);
	u8 result = 0;

	if (isr1 & 0x03) result |= SCA_INTR_DMAC_RX(0);
	if (isr1 & 0x0C) result |= SCA_INTR_DMAC_TX(0);
	if (isr1 & 0x30) result |= SCA_INTR_DMAC_RX(1);
	if (isr1 & 0xC0) result |= SCA_INTR_DMAC_TX(1);
	if (isr0 & 0x0F) result |= SCA_INTR_MSCI(0);
	if (isr0 & 0xF0) result |= SCA_INTR_MSCI(1);

	return result;
}

#else /* HD64572 */
#define sca_outa(value, reg, card)	sca_outl(value, reg, card)
#define sca_ina(reg, card)		sca_inl(reg, card)
#define writea(value, ptr)		writel(value, ptr)


static inline int sca_intr_status(card_t *card)
{
	u32 isr0 = sca_inl(ISR0, card);
	u8 result = 0;

	if (isr0 & 0x0000000F) result |= SCA_INTR_DMAC_RX(0);
	if (isr0 & 0x000000F0) result |= SCA_INTR_DMAC_TX(0);
	if (isr0 & 0x00000F00) result |= SCA_INTR_DMAC_RX(1);
	if (isr0 & 0x0000F000) result |= SCA_INTR_DMAC_TX(1);
	if (isr0 & 0x003E0000) result |= SCA_INTR_MSCI(0);
	if (isr0 & 0x3E000000) result |= SCA_INTR_MSCI(1);

	return result;
}

#endif /* HD64570 vs HD64572 */




static inline port_t* hdlc_to_port(hdlc_device *hdlc)
{
	return (port_t*)hdlc;
}



static inline port_t* dev_to_port(struct net_device *dev)
{
	return hdlc_to_port(dev_to_hdlc(dev));
}



static inline u8 next_desc(port_t *port, u8 desc)
{
	return (desc + 1) % port_to_card(port)->ring_buffers;
}



static inline u16 desc_offset(port_t *port, u8 desc, u8 transmit)
{
	/* Descriptor offset always fits in 16 bytes */
	u8 buffs = port_to_card(port)->ring_buffers;
	return ((log_node(port) * 2 + transmit) * buffs + (desc % buffs)) *
		sizeof(pkt_desc);
}



static inline pkt_desc* desc_address(port_t *port, u8 desc, u8 transmit)
{
#ifdef PAGE0_ALWAYS_MAPPED
	return (pkt_desc*)(win0base(port_to_card(port))
			   + desc_offset(port, desc, transmit));
#else
	return (pkt_desc*)(winbase(port_to_card(port))
			   + desc_offset(port, desc, transmit));
#endif
}



static inline u32 buffer_offset(port_t *port, u8 desc, u8 transmit)
{
	u8 buffs = port_to_card(port)->ring_buffers;
	return port_to_card(port)->buff_offset +
		((log_node(port) * 2 + transmit) * buffs + (desc % buffs)) *
		(u32)HDLC_MAX_MRU;
}



static void sca_init_sync_port(port_t *port)
{
	card_t *card = port_to_card(port);
	u8 transmit, i;
	u16 dmac, buffs = card->ring_buffers;

	port->rxin = 0;
	port->txin = 0;
	port->txlast = 0;

#if !defined(PAGE0_ALWAYS_MAPPED) && !defined(ALL_PAGES_ALWAYS_MAPPED)
	openwin(card, 0);
#endif

	for (transmit = 0; transmit < 2; transmit++) {
		for (i = 0; i < buffs; i++) {
			pkt_desc* desc = desc_address(port, i, transmit);
			u16 chain_off = desc_offset(port, i + 1, transmit);
			u32 buff_off = buffer_offset(port, i, transmit);

			writea(chain_off, &desc->cp);
			writel(buff_off, &desc->bp);
			writew(0, &desc->len);
			writeb(0, &desc->stat);
		}

		dmac = transmit ? get_dmac_tx(port) : get_dmac_rx(port);
		/* DMA disable - to halt state */
		sca_out(0, transmit ? DSR_TX(phy_node(port)) :
			DSR_RX(phy_node(port)), card);
		/* software ABORT - to initial state */
		sca_out(DCR_ABORT, transmit ? DCR_TX(phy_node(port)) :
			DCR_RX(phy_node(port)), card);

#ifdef __HD64570_H
		sca_out(0, dmac + CPB, card); /* pointer base */
#endif
		/* current desc addr */
		sca_outa(desc_offset(port, 0, transmit), dmac + CDAL, card);
		if (!transmit)
			sca_outa(desc_offset(port, buffs - 1, transmit),
				 dmac + EDAL, card);
		else
			sca_outa(desc_offset(port, 0, transmit), dmac + EDAL,
				 card);

		/* clear frame end interrupt counter */
		sca_out(DCR_CLEAR_EOF, transmit ? DCR_TX(phy_node(port)) :
			DCR_RX(phy_node(port)), card);

		if (!transmit) { /* Receive */
			/* set buffer length */
			sca_outw(HDLC_MAX_MRU, dmac + BFLL, card);
			/* Chain mode, Multi-frame */
			sca_out(0x14, DMR_RX(phy_node(port)), card);
			sca_out(DIR_EOME | DIR_BOFE, DIR_RX(phy_node(port)),
				card);
			/* DMA enable */
			sca_out(DSR_DE, DSR_RX(phy_node(port)), card);
		} else {	/* Transmit */
			/* Chain mode, Multi-frame */
			sca_out(0x14, DMR_TX(phy_node(port)), card);
			/* enable underflow interrupts */
			sca_out(DIR_BOFE, DIR_TX(phy_node(port)), card);
		}
	}
}



/* MSCI interrupt service */
static inline void sca_msci_intr(port_t *port)
{
	u16 msci = get_msci(port);
	card_t* card = port_to_card(port);
	u8 stat = sca_in(msci + ST1, card); /* read MSCI ST1 status */

	/* printk(KERN_DEBUG "MSCI INT: ST1=%02X ILAR=%02X\n",
	   stat, sca_in(ILAR, card)); */

	/* Reset MSCI TX underrun status bit */
	sca_out(stat & ST1_UDRN, msci + ST1, card);

	if (stat & ST1_UDRN) {
		port->hdlc.stats.tx_errors++; /* TX Underrun error detected */
		port->hdlc.stats.tx_fifo_errors++;
	}
}



static inline void sca_rx(card_t *card, port_t *port, pkt_desc *desc,
			      u8 rxin)
{
	struct sk_buff *skb;
	u16 len;
	u32 buff;
#ifndef ALL_PAGES_ALWAYS_MAPPED
	u32 maxlen;
	u8 page;
#endif

	len = readw(&desc->len);
	skb = dev_alloc_skb(len);
	if (!skb) {
		port->hdlc.stats.rx_dropped++;
		return;
	}

	buff = buffer_offset(port, rxin, 0);
#ifndef ALL_PAGES_ALWAYS_MAPPED
	page = buff / winsize(card);
	buff = buff % winsize(card);
	maxlen = winsize(card) - buff;

	openwin(card, page);

	if (len > maxlen) {
		memcpy_fromio(skb->data, winbase(card) + buff, maxlen);
		openwin(card, page + 1);
		memcpy_fromio(skb->data + maxlen, winbase(card), len - maxlen);
	} else
#endif
	memcpy_fromio(skb->data, winbase(card) + buff, len);

#if !defined(PAGE0_ALWAYS_MAPPED) && !defined(ALL_PAGES_ALWAYS_MAPPED)
	/* select pkt_desc table page back */
	openwin(card, 0);
#endif
	skb_put(skb, len);
#ifdef DEBUG_PKT
	printk(KERN_DEBUG "%s RX(%i):", hdlc_to_name(&port->hdlc), skb->len);
	debug_frame(skb);
#endif
	port->hdlc.stats.rx_packets++;
	port->hdlc.stats.rx_bytes += skb->len;
	hdlc_netif_rx(&port->hdlc, skb);
}



/* Receive DMA interrupt service */
static inline void sca_rx_intr(port_t *port)
{
	u16 dmac = get_dmac_rx(port);
	card_t *card = port_to_card(port);
	u8 stat = sca_in(DSR_RX(phy_node(port)), card); /* read DMA Status */
	struct net_device_stats *stats = &port->hdlc.stats;

	/* Reset DSR status bits */
	sca_out((stat & (DSR_EOT | DSR_EOM | DSR_BOF | DSR_COF)) | DSR_DWE,
		DSR_RX(phy_node(port)), card);

	if (stat & DSR_BOF)
		stats->rx_over_errors++; /* Dropped one or more frames */

	while (1) {
		u32 desc_off = desc_offset(port, port->rxin, 0);
		pkt_desc *desc;
		u32 cda = sca_ina(dmac + CDAL, card);

		if (cda == desc_off)
			break;	/* No frame received */

#ifdef __HD64572_H
		if (cda == desc_off + 8)
			break;	/* SCA-II updates CDA in 2 steps */
#endif

		desc = desc_address(port, port->rxin, 0);
		stat = readb(&desc->stat);
		if (!(stat & ST_RX_EOM))
			port->rxpart = 1; /* partial frame received */
		else if ((stat & ST_ERROR_MASK) || port->rxpart) {
			stats->rx_errors++;
			if (stat & ST_RX_OVERRUN) stats->rx_fifo_errors++;
			else if ((stat & (ST_RX_SHORT | ST_RX_ABORT |
					  ST_RX_RESBIT)) || port->rxpart)
				stats->rx_frame_errors++;
			else if (stat & ST_RX_CRC) stats->rx_crc_errors++;
			if (stat & ST_RX_EOM)
				port->rxpart = 0; /* received last fragment */
		} else
			sca_rx(card, port, desc, port->rxin);

		/* Set new error descriptor address */
		sca_outa(desc_off, dmac + EDAL, card);
		port->rxin = next_desc(port, port->rxin);
	}

	/* make sure RX DMA is enabled */
	sca_out(DSR_DE, DSR_RX(phy_node(port)), card);
}



/* Transmit DMA interrupt service */
static inline void sca_tx_intr(port_t *port)
{
	u16 dmac = get_dmac_tx(port);
	card_t* card = port_to_card(port);
	u8 stat;

	spin_lock(&port->lock);

	stat = sca_in(DSR_TX(phy_node(port)), card); /* read DMA Status */

	/* Reset DSR status bits */
	sca_out((stat & (DSR_EOT | DSR_EOM | DSR_BOF | DSR_COF)) | DSR_DWE,
		DSR_TX(phy_node(port)), card);

	while (1) {
		u32 desc_off = desc_offset(port, port->txlast, 1);
		pkt_desc *desc;
		u16 len;

		if (sca_ina(dmac + CDAL, card) == desc_off)
			break;	/* Transmitter is/will_be sending this frame */

		desc = desc_address(port, port->txlast, 1);
		len = readw(&desc->len);

		port->hdlc.stats.tx_packets++;
		port->hdlc.stats.tx_bytes += len;
		writeb(0, &desc->stat);	/* Free descriptor */

		port->txlast = (port->txlast + 1) %
			port_to_card(port)->ring_buffers;
	}

	netif_wake_queue(hdlc_to_dev(&port->hdlc));
	spin_unlock(&port->lock);
}



static void sca_intr(int irq, void* dev_id, struct pt_regs *regs)
{
	card_t *card = dev_id;
	int boguscnt = INTR_WORK;
	int i;
	u8 stat;

#ifndef ALL_PAGES_ALWAYS_MAPPED
	u8 page = sca_get_page(card);
#endif

	while((stat = sca_intr_status(card)) != 0) {
		for (i = 0; i < 2; i++) {
			port_t *port = get_port(card, i);
			if (port) {
				if (stat & SCA_INTR_MSCI(i))
					sca_msci_intr(port);

				if (stat & SCA_INTR_DMAC_RX(i))
					sca_rx_intr(port);

				if (stat & SCA_INTR_DMAC_TX(i))
					sca_tx_intr(port);
			}

			if (--boguscnt < 0) {
				printk(KERN_ERR "%s: too much work at "
				       "interrupt\n",
				       hdlc_to_name(&port->hdlc));
				goto exit;
			}
		}
	}

 exit:
#ifndef ALL_PAGES_ALWAYS_MAPPED
	openwin(card, page);		/* Restore original page */
#endif
}



static inline int sca_set_loopback(port_t *port, int line)
{
	card_t* card = port_to_card(port);
	u8 msci = get_msci(port);
	u8 md2 = sca_in(msci + MD2, card);

	switch(line) {
	case LINE_DEFAULT:
		md2 &= ~MD2_LOOPBACK;
		port->line &= ~LINE_LOOPBACK;
		break;

	case LINE_LOOPBACK:
		md2 |= MD2_LOOPBACK;
		port->line |= LINE_LOOPBACK;
		break;

	default:
		return -EINVAL;
	}

	sca_out(md2, msci + MD2, card);
	return 0;
}



static void sca_set_clock(port_t *port)
{
	card_t *card = port_to_card(port);
	u8 msci = get_msci(port);
	unsigned int tmc, br = 10, brv = 1024;

	if (port->clkrate > 0) {
		/* Try lower br for better accuracy*/
		do {
			br--;
			brv >>= 1; /* brv = 2^9 = 512 max in specs */

			/* Baud Rate = CLOCK_BASE / TMC / 2^BR */
			tmc = CLOCK_BASE / (brv * port->clkrate);
		}while(br > 1 && tmc <= 128);

		if (tmc < 1) {
			tmc = 1;
			br = 0;	/* For baud=CLOCK_BASE we use tmc=1 br=0 */
			brv = 1;
		} else if (tmc > 255)
			tmc = 256; /* tmc=0 means 256 - low baud rates */

		port->clkrate = CLOCK_BASE / (brv * tmc);
	} else {
		br = 9; /* Minimum clock rate */
		tmc = 256;	/* 8bit = 0 */
		port->clkrate = CLOCK_BASE / (256 * 512);
	}

	port->rxs = (port->rxs & ~CLK_BRG_MASK) | br;
	port->txs = (port->txs & ~CLK_BRG_MASK) | br;
	port->tmc = tmc;

	/* baud divisor - time constant*/
#ifdef __HD64570_H
	sca_out(port->tmc, msci + TMC, card);
#else
	sca_out(port->tmc, msci + TMCR, card);
	sca_out(port->tmc, msci + TMCT, card);
#endif

	/* Set BRG bits */
	sca_out(port->rxs, msci + RXS, card);
	sca_out(port->txs, msci + TXS, card);
}



static void sca_set_hdlc_mode(port_t *port, u8 idle, u8 crc, u8 nrzi)
{
	card_t* card = port_to_card(port);
	u8 msci = get_msci(port);
	u8 md2 = (nrzi ? MD2_NRZI : 0) |
		((port->line & LINE_LOOPBACK) ? MD2_LOOPBACK : 0);
	u8 ctl = (idle ? CTL_IDLE : 0);
#ifdef __HD64572_H
	ctl |= CTL_URCT | CTL_URSKP; /* Skip the rest of underrun frame */
#endif

	sca_out(CMD_RESET, msci + CMD, card);
	sca_out(MD0_HDLC | crc, msci + MD0, card);
	sca_out(0x00, msci + MD1, card); /* no address field check */
	sca_out(md2, msci + MD2, card);
	sca_out(0x7E, msci + IDL, card); /* flag character 0x7E */
	sca_out(ctl, msci + CTL, card);

#ifdef __HD64570_H
	/* Allow at least 8 bytes before requesting RX DMA operation */
	/* TX with higher priority and possibly with shorter transfers */
	sca_out(0x07, msci + RRC, card); /* +1=RXRDY/DMA activation condition*/
	sca_out(0x10, msci + TRC0, card); /* = TXRDY/DMA activation condition*/
	sca_out(0x14, msci + TRC1, card); /* +1=TXRDY/DMA deactiv condition */
#else
	sca_out(0x0F, msci + RNR, card); /* +1=RX DMA activation condition */
	/* Setting than to larger value may cause Illegal Access */
	sca_out(0x20, msci + TNR0, card); /* =TX DMA activation condition */
	sca_out(0x30, msci + TNR1, card); /* +1=TX DMA deactivation condition*/
	sca_out(0x04, msci + TCR, card); /* =Critical TX DMA activ condition */
#endif


#ifdef __HD64570_H
	/* MSCI TX INT IRQ enable */
	sca_out(IE0_TXINT, msci + IE0, card);
	sca_out(IE1_UDRN, msci + IE1, card); /* TX underrun IRQ */
	sca_out(sca_in(IER0, card) | (phy_node(port) ? 0x80 : 0x08),
		IER0, card);
	/* DMA IRQ enable */
	sca_out(sca_in(IER1, card) | (phy_node(port) ? 0xF0 : 0x0F),
		IER1, card);
#else
	/* MSCI TX INT and underrrun IRQ enable */
	sca_outl(IE0_TXINT | IE0_UDRN, msci + IE0, card);
	/* DMA & MSCI IRQ enable */
	sca_outl(sca_in(IER0, card) |
		 (phy_node(port) ? 0x02006600 : 0x00020066), IER0, card);
#endif

#ifdef __HD64570_H
	sca_out(port->tmc, msci + TMC, card); /* Restore registers */
#else
	sca_out(port->tmc, msci + TMCR, card);
	sca_out(port->tmc, msci + TMCT, card);
#endif
	sca_out(port->rxs, msci + RXS, card);
	sca_out(port->txs, msci + TXS, card);
	sca_out(CMD_TX_ENABLE, msci + CMD, card);
	sca_out(CMD_RX_ENABLE, msci + CMD, card);
}



#ifdef DEBUG_RINGS
static void sca_dump_rings(hdlc_device *hdlc)
{
	port_t *port = hdlc_to_port(hdlc);
	card_t *card = port_to_card(port);
	u16 cnt;
#if !defined(PAGE0_ALWAYS_MAPPED) && !defined(ALL_PAGES_ALWAYS_MAPPED)
	u8 page;
#endif

#if !defined(PAGE0_ALWAYS_MAPPED) && !defined(ALL_PAGES_ALWAYS_MAPPED)
	page = sca_get_page(card);
	openwin(card, 0);
#endif

	printk(KERN_ERR "RX ring: CDA=%u EDA=%u DSR=%02X in=%u "
	       "%sactive",
	       sca_ina(get_dmac_rx(port) + CDAL, card),
	       sca_ina(get_dmac_rx(port) + EDAL, card),
	       sca_in(DSR_RX(phy_node(port)), card),
	       port->rxin,
	       sca_in(DSR_RX(phy_node(port)), card) & DSR_DE?"":"in");
	for (cnt = 0; cnt<port_to_card(port)->ring_buffers; cnt++)
		printk(" %02X",
		       readb(&(desc_address(port, cnt, 0)->stat)));

	printk("\n" KERN_ERR "TX ring: CDA=%u EDA=%u DSR=%02X in=%u "
	       "last=%u %sactive",
	       sca_ina(get_dmac_tx(port) + CDAL, card),
	       sca_ina(get_dmac_tx(port) + EDAL, card),
	       sca_in(DSR_TX(phy_node(port)), card), port->txin,
	       port->txlast,
	       sca_in(DSR_TX(phy_node(port)), card) & DSR_DE ? "" : "in");

	for (cnt = 0; cnt<port_to_card(port)->ring_buffers; cnt++)
		printk(" %02X",
		       readb(&(desc_address(port, cnt, 1)->stat)));
	printk("\n");

	printk(KERN_ERR "MSCI: MD: %02x %02x %02x, "
	       "ST: %02x %02x %02x %02x"
#ifdef __HD64572_H
	       " %02x"
#endif
	       ", FST: %02x CST: %02x %02x\n",
	       sca_in(get_msci(port) + MD0, card),
	       sca_in(get_msci(port) + MD1, card),
	       sca_in(get_msci(port) + MD2, card),
	       sca_in(get_msci(port) + ST0, card),
	       sca_in(get_msci(port) + ST1, card),
	       sca_in(get_msci(port) + ST2, card),
	       sca_in(get_msci(port) + ST3, card),
#ifdef __HD64572_H
	       sca_in(get_msci(port) + ST4, card),
#endif
	       sca_in(get_msci(port) + FST, card),
	       sca_in(get_msci(port) + CST0, card),
	       sca_in(get_msci(port) + CST1, card));

#ifdef __HD64572_H
	printk(KERN_ERR "ILAR: %02x\n", sca_in(ILAR, card));
#endif

#if !defined(PAGE0_ALWAYS_MAPPED) && !defined(ALL_PAGES_ALWAYS_MAPPED)
	openwin(card, page); /* Restore original page */
#endif
}
#endif /* DEBUG_RINGS */



static void sca_open(hdlc_device *hdlc)
{
	port_t *port = hdlc_to_port(hdlc);

	sca_set_hdlc_mode(port, 1, MD0_CRC_ITU, 0);
	netif_start_queue(hdlc_to_dev(hdlc));
}


static void sca_close(hdlc_device *hdlc)
{
	port_t *port = hdlc_to_port(hdlc);

	/* reset channel */
	netif_stop_queue(hdlc_to_dev(hdlc));
	sca_out(CMD_RESET, get_msci(port) + CMD, port_to_card(port));
}



static int sca_xmit(hdlc_device *hdlc, struct sk_buff *skb)
{
	port_t *port = hdlc_to_port(hdlc);
	struct net_device *dev = hdlc_to_dev(hdlc);
	card_t *card = port_to_card(port);
	pkt_desc *desc;
	u32 buff, len;
#ifndef ALL_PAGES_ALWAYS_MAPPED
	u8 page;
	u32 maxlen;
#endif

	spin_lock_irq(&port->lock);

	desc = desc_address(port, port->txin + 1, 1);
	if (readb(&desc->stat)) { /* allow 1 packet gap */
		/* should never happen - previous xmit should stop queue */
#ifdef DEBUG_PKT
		printk(KERN_DEBUG "%s: transmitter buffer full\n", dev->name);
#endif
		netif_stop_queue(dev);
		spin_unlock_irq(&port->lock);
		return 1;	/* request packet to be queued */
	}

#ifdef DEBUG_PKT
	printk(KERN_DEBUG "%s TX(%i):", hdlc_to_name(hdlc), skb->len);
	debug_frame(skb);
#endif

	desc = desc_address(port, port->txin, 1);
	buff = buffer_offset(port, port->txin, 1);
	len = skb->len;
#ifndef ALL_PAGES_ALWAYS_MAPPED
	page = buff / winsize(card);
	buff = buff % winsize(card);
	maxlen = winsize(card) - buff;

	openwin(card, page);
	if (len > maxlen) {
		memcpy_toio(winbase(card) + buff, skb->data, maxlen);
		openwin(card, page + 1);
		memcpy_toio(winbase(card), skb->data + maxlen, len - maxlen);
	}
	else
#endif
		memcpy_toio(winbase(card) + buff, skb->data, len);

#if !defined(PAGE0_ALWAYS_MAPPED) && !defined(ALL_PAGES_ALWAYS_MAPPED)
	openwin(card, 0);	/* select pkt_desc table page back */
#endif
	writew(len, &desc->len);
	writeb(ST_TX_EOM, &desc->stat);
	dev->trans_start = jiffies;

	port->txin = next_desc(port, port->txin);
	sca_outa(desc_offset(port, port->txin, 1),
		 get_dmac_tx(port) + EDAL, card);

	sca_out(DSR_DE, DSR_TX(phy_node(port)), card); /* Enable TX DMA */

	desc = desc_address(port, port->txin + 1, 1);
	if (readb(&desc->stat)) /* allow 1 packet gap */
		netif_stop_queue(hdlc_to_dev(&port->hdlc));

	spin_unlock_irq(&port->lock);

	dev_kfree_skb(skb);
	return 0;
}


static void sca_init(card_t *card, int wait_states)
{
	sca_out(wait_states, WCRL, card); /* Wait Control */
	sca_out(wait_states, WCRM, card);
	sca_out(wait_states, WCRH, card);

	sca_out(0, DMER, card);	/* DMA Master disable */
	sca_out(0x03, PCR, card); /* DMA priority */
	sca_out(0, IER1, card);	/* DMA interrupt disable */
	sca_out(0, DSR_RX(0), card); /* DMA disable - to halt state */
	sca_out(0, DSR_TX(0), card);
	sca_out(0, DSR_RX(1), card);
	sca_out(0, DSR_TX(1), card);
	sca_out(DMER_DME, DMER, card); /* DMA Master enable */
}
