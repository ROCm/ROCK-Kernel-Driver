/*
 * Moxa C101 synchronous serial card driver for Linux
 *
 * Copyright (C) 2000-2002 Krzysztof Halasa <khc@pm.waw.pl>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * For information see http://hq.pm.waw.pl/hdlc/
 *
 * Sources of information:
 *    Hitachi HD64570 SCA User's Manual
 *    Moxa C101 User's Manual
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/hdlc.h>
#include <linux/delay.h>
#include <asm/io.h>

#include "hd64570.h"


static const char* version = "Moxa C101 driver version: 1.10";
static const char* devname = "C101";

#define C101_PAGE 0x1D00
#define C101_DTR 0x1E00
#define C101_SCA 0x1F00
#define C101_WINDOW_SIZE 0x2000
#define C101_MAPPED_RAM_SIZE 0x4000

#define RAM_SIZE (256 * 1024)
#define CLOCK_BASE 9830400	/* 9.8304 MHz */
#define PAGE0_ALWAYS_MAPPED

static char *hw;		/* pointer to hw=xxx command line string */


typedef struct card_s {
	hdlc_device hdlc;	/* HDLC device struct - must be first */
	spinlock_t lock;	/* TX lock */
	u8 *win0base;		/* ISA window base address */
	u32 phy_winbase;	/* ISA physical base address */
	u16 buff_offset;	/* offset of first buffer of first channel */
	sync_serial_settings settings;
	unsigned short encoding;
	unsigned short parity;
	u8 rxs, txs, tmc;	/* SCA registers */
	u8 irq;			/* IRQ (3-15) */
	u8 ring_buffers;	/* number of buffers in a ring */
	u8 page;

	u8 rxin;		/* rx ring buffer 'in' pointer */
	u8 txin;		/* tx ring buffer 'in' and 'last' pointers */
	u8 txlast;
	u8 rxpart;		/* partial frame received, next frame invalid*/

	struct card_s *next_card;
}card_t;

typedef card_t port_t;

static card_t *first_card;
static card_t **new_card = &first_card;


#define sca_in(reg, card)	   readb((card)->win0base + C101_SCA + (reg))
#define sca_out(value, reg, card)  writeb(value, (card)->win0base + C101_SCA + (reg))
#define sca_inw(reg, card)	   readw((card)->win0base + C101_SCA + (reg))
#define sca_outw(value, reg, card) writew(value, (card)->win0base + C101_SCA + (reg))

#define port_to_card(port)	   (port)
#define log_node(port)		   (0)
#define phy_node(port)		   (0)
#define winsize(card)		   (C101_WINDOW_SIZE)
#define win0base(card)		   ((card)->win0base)
#define winbase(card)      	   ((card)->win0base + 0x2000)
#define get_port(card, port)	   ((port) == 0 ? (card) : NULL)


static inline u8 sca_get_page(card_t *card)
{
	return card->page;
}

static inline void openwin(card_t *card, u8 page)
{
	card->page = page;
	writeb(page, card->win0base + C101_PAGE);
}


#define close_windows(card) {} /* no hardware support */


#include "hd6457x.c"


static void c101_set_iface(port_t *port)
{
	u8 msci = get_msci(port);
	u8 rxs = port->rxs & CLK_BRG_MASK;
	u8 txs = port->txs & CLK_BRG_MASK;

	switch(port->settings.clock_type) {
	case CLOCK_INT:
		rxs |= CLK_BRG_RX; /* TX clock */
		txs |= CLK_RXCLK_TX; /* BRG output */
		break;

	case CLOCK_TXINT:
		rxs |= CLK_LINE_RX; /* RXC input */
		txs |= CLK_BRG_TX; /* BRG output */
		break;

	case CLOCK_TXFROMRX:
		rxs |= CLK_LINE_RX; /* RXC input */
		txs |= CLK_RXCLK_TX; /* RX clock */
		break;

	default:	/* EXTernal clock */
		rxs |= CLK_LINE_RX; /* RXC input */
		txs |= CLK_LINE_TX; /* TXC input */
	}

	port->rxs = rxs;
	port->txs = txs;
	sca_out(rxs, msci + RXS, port);
	sca_out(txs, msci + TXS, port);
	sca_set_port(port);
}


static int c101_open(struct net_device *dev)
{
	hdlc_device *hdlc = dev_to_hdlc(dev);
	port_t *port = hdlc_to_port(hdlc);
	int result = hdlc_open(hdlc);
	if (result)
		return result;

	MOD_INC_USE_COUNT;
	writeb(1, port->win0base + C101_DTR);
	sca_out(0, MSCI1_OFFSET + CTL, port); /* RTS uses ch#2 output */
	sca_open(hdlc);
	c101_set_iface(port);
	return 0;
}


static int c101_close(struct net_device *dev)
{
	hdlc_device *hdlc = dev_to_hdlc(dev);
	port_t *port = hdlc_to_port(hdlc);

	sca_close(hdlc);
	writeb(0, port->win0base + C101_DTR);
	sca_out(CTL_NORTS, MSCI1_OFFSET + CTL, port);
	hdlc_close(hdlc);
	MOD_DEC_USE_COUNT;
	return 0;
}


static int c101_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	union line_settings *line = &ifr->ifr_settings->ifs_line;
	const size_t size = sizeof(sync_serial_settings);
	sync_serial_settings new_line;
	hdlc_device *hdlc = dev_to_hdlc(dev);
	port_t *port = hdlc_to_port(hdlc);

#ifdef CONFIG_HDLC_DEBUG_RINGS
	if (cmd == SIOCDEVPRIVATE) {
		sca_dump_rings(hdlc);
		return 0;
	}
#endif
	if (cmd != SIOCWANDEV)
		return hdlc_ioctl(dev, ifr, cmd);

	switch(ifr->ifr_settings->type) {
	case IF_GET_IFACE:
		ifr->ifr_settings->type = IF_IFACE_SYNC_SERIAL;
		if (copy_to_user(&line->sync, &port->settings, size))
			return -EFAULT;
		return 0;

	case IF_IFACE_SYNC_SERIAL:
		if(!capable(CAP_NET_ADMIN))
			return -EPERM;

		if (copy_from_user(&new_line, &line->sync, size))
			return -EFAULT;

		if (new_line.clock_type != CLOCK_EXT &&
		    new_line.clock_type != CLOCK_TXFROMRX &&
		    new_line.clock_type != CLOCK_INT &&
		    new_line.clock_type != CLOCK_TXINT)
		return -EINVAL;	/* No such clock setting */

		if (new_line.loopback != 0 && new_line.loopback != 1)
			return -EINVAL;

		memcpy(&port->settings, &new_line, size); /* Update settings */
		c101_set_iface(port);
		return 0;

	default:
		return hdlc_ioctl(dev, ifr, cmd);
	}
}



static void c101_destroy_card(card_t *card)
{
	if (card->irq)
		free_irq(card->irq, card);

	if (card->win0base) {
		iounmap(card->win0base);
		release_mem_region(card->phy_winbase, C101_MAPPED_RAM_SIZE);
	}

	kfree(card);
}



static int c101_run(unsigned long irq, unsigned long winbase)
{
	struct net_device *dev;
	card_t *card;
	int result;

	if (irq<3 || irq>15 || irq == 6) /* FIXME */ {
		printk(KERN_ERR "c101: invalid IRQ value\n");
		return -ENODEV;
	}

	if (winbase < 0xC0000 || winbase > 0xDFFFF || (winbase & 0x3FFF) !=0) {
		printk(KERN_ERR "c101: invalid RAM value\n");
		return -ENODEV;
	}

	card = kmalloc(sizeof(card_t), GFP_KERNEL);
	if (card == NULL) {
		printk(KERN_ERR "c101: unable to allocate memory\n");
		return -ENOBUFS;
	}
	memset(card, 0, sizeof(card_t));

	if (request_irq(irq, sca_intr, 0, devname, card)) {
		printk(KERN_ERR "c101: could not allocate IRQ\n");
		c101_destroy_card(card);
		return(-EBUSY);
	}
	card->irq = irq;

	if (!request_mem_region(winbase, C101_MAPPED_RAM_SIZE, devname)) {
		printk(KERN_ERR "c101: could not request RAM window\n");
		c101_destroy_card(card);
		return(-EBUSY);
	}
	card->phy_winbase = winbase;
	card->win0base = ioremap(winbase, C101_MAPPED_RAM_SIZE);
	if (!card->win0base) {
		printk(KERN_ERR "c101: could not map I/O address\n");
		c101_destroy_card(card);
		return -EBUSY;
	}

	/* 2 rings required for 1 port */
	card->ring_buffers = (RAM_SIZE -C101_WINDOW_SIZE) / (2 * HDLC_MAX_MRU);
	printk(KERN_DEBUG "c101: using %u packets rings\n",card->ring_buffers);

	card->buff_offset = C101_WINDOW_SIZE; /* Bytes 1D00-1FFF reserved */

	readb(card->win0base + C101_PAGE); /* Resets SCA? */
	udelay(100);
	writeb(0, card->win0base + C101_PAGE);
	writeb(0, card->win0base + C101_DTR); /* Power-up for RAM? */

	sca_init(card, 0);

	dev = hdlc_to_dev(&card->hdlc);

	spin_lock_init(&card->lock);
	dev->irq = irq;
	dev->mem_start = winbase;
	dev->mem_end = winbase + C101_MAPPED_RAM_SIZE - 1;
	dev->tx_queue_len = 50;
	dev->do_ioctl = c101_ioctl;
	dev->open = c101_open;
	dev->stop = c101_close;
	card->hdlc.attach = sca_attach;
	card->hdlc.xmit = sca_xmit;
	card->settings.clock_type = CLOCK_EXT;

	result = register_hdlc_device(&card->hdlc);
	if (result) {
		printk(KERN_WARNING "c101: unable to register hdlc device\n");
		c101_destroy_card(card);
		return result;
	}

	sca_init_sync_port(card); /* Set up C101 memory */

	*new_card = card;
	new_card = &card->next_card;
	return 0;
}



static int __init c101_init(void)
{
	if (hw == NULL) {
#ifdef MODULE
		printk(KERN_INFO "c101: no card initialized\n");
#endif
		return -ENOSYS;	/* no parameters specified, abort */
	}

	printk(KERN_INFO "%s (SCA-%s)\n", version, sca_version);

	do {
		unsigned long irq, ram;

		irq = simple_strtoul(hw, &hw, 0);

		if (*hw++ != ',')
			break;
		ram = simple_strtoul(hw, &hw, 0);

		if (*hw == ':' || *hw == '\x0')
			c101_run(irq, ram);

		if (*hw == '\x0')
			return 0;
	}while(*hw++ == ':');

	printk(KERN_ERR "c101: invalid hardware parameters\n");
	return first_card ? 0 : -ENOSYS;
}


#ifndef MODULE
static int __init c101_setup(char *str)
{
	hw = str;
	return 1;
}

__setup("c101=", c101_setup);
#endif


static void __exit c101_cleanup(void)
{
	card_t *card = first_card;

	while (card) {
		card_t *ptr = card;
		card = card->next_card;
		unregister_hdlc_device(&ptr->hdlc);
		c101_destroy_card(ptr);
	}
}


module_init(c101_init);
module_exit(c101_cleanup);

MODULE_AUTHOR("Krzysztof Halasa <khc@pm.waw.pl>");
MODULE_DESCRIPTION("Moxa C101 serial port driver");
MODULE_LICENSE("GPL");
MODULE_PARM(hw, "s");		/* hw=irq,ram:irq,... */
