/*
 *  IPE405 (IBM IAP 405 chip evaluation board) Debug Support Board
 *    Ehernet Driver
 *  (C) Copyright 2001 by S.nishino (jl04348@jp.ibm.com)  IBM-Japan
 *
 * ---------- Strategy ----------
 *
 *  This NIC is RTL8019AS, simply connected to External Bus Controller
 *  of IAP 405 chip. As many folks of 8390 based NIC, 8390 core driver
 *  is usable.  luckily, the following driver is already available for
 *  Amiga zorro bus (however I don't know this architecture beyond
 *  below), this is modified based on this driver (ariadne2).
 *
 * ---------- original header ----------
 *  Amiga Linux/m68k Ariadne II Ethernet Driver
 *
 *  (C) Copyright 1998 by some Elitist 680x0 Users(TM)
 *
 *  ---------------------------------------------------------------------------
 *
 *  This program is based on all the other NE2000 drivers for Linux
 *
 *  ---------------------------------------------------------------------------
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of the Linux
 *  distribution for more details.  */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/irq.h>


#include <asm/system.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/ppc4xx_pic.h>
#if defined(CONFIG_ARCTIC2)
#include <platforms/4xx/arctic2.h>
#else
#error The driver only works on Arctic
#endif

#include "8390.h"


#define ARCTIC_ENET_BASE8	(ARCTIC2_FPGA8_PADDR + 256*1024)
#define ARCTIC_ENET_BASE16	(ARCTIC2_FPGA16_PADDR + 0)

#define ARCTIC_ENET_IOBASE	0x0300	/* io base offset from NIC region */

#define ARCTIC_ENET_IRQ		29	/* irq number in UIC */
#define ARCTIC_ENET_IRQ_MASK	(0x80000000 >> ARCTIC_ENET_IRQ)

#define NE_BASE         (ARCTIC_ENET_BASE8 + ARCTIC_ENET_IOBASE)
#define NE_BASE16       (ARCTIC_ENET_BASE16 + ARCTIC_ENET_IOBASE)

/* 8390 register address */
#define NE_CMD          (0x00)
#define NE_DATAPORT     (0x10)	/* NatSemi-defined port window offset. */
#define NE_DATAPORT16	(NE_DATAPORT / sizeof(u16))
#define NE_RESET        (0x1f)	/* Issue a read to reset, a write to clear. */
#define NE_IO_EXTENT    (0x20)	/* region extent */

#define NE_EN0_ISR      (0x07)
#define NE_EN0_DCFG     (0x0e)

#define NE_EN0_RSARLO   (0x08)
#define NE_EN0_RSARHI   (0x09)
#define NE_EN0_RCNTLO   (0x0a)
#define NE_EN0_RXCR     (0x0c)
#define NE_EN0_TXCR     (0x0d)
#define NE_EN0_RCNTHI   (0x0b)
#define NE_EN0_IMR      (0x0f)

/* 8390 packet buffer page number */
#define NESM_START_PG   0x40	/* First page of TX buffer */
#define NESM_STOP_PG    0x80	/* Last page +1 of RX ring */

static u8 *iobase8;
static u16 *iobase16;

static int arctic_enet_probe(struct net_device *dev);
static int arctic_enet_init(struct net_device *dev);

static int arctic_enet_open(struct net_device *dev);
static int arctic_enet_close(struct net_device *dev);

static void arctic_enet_reset_8390(struct net_device *dev);
static void arctic_enet_get_8390_hdr(struct net_device *dev,
				    struct e8390_pkt_hdr *hdr,
				    int ring_page);
static void arctic_enet_block_input(struct net_device *dev, int count,
				   struct sk_buff *skb, int ring_offset);
static void arctic_enet_block_output(struct net_device *dev,
				    const int count,
				    const unsigned char *buf,
				    const int start_page);

/* These macros will do something on Arctic-I if we ever add support
 * for it back in */
#define switch_16bit_bank()	do { } while (0)
#define switch_8bit_bank()	do { } while (0)

void p_dump(unsigned char *p, int sz)
{
	int i;
	unsigned char *wp;
	
	wp = p;
	
	printk("------ PACKET START :  %d Bytes  ------ \n", sz);
	
	for (i = 0; i < sz; i++) {
		if (i % 16 == 0) {
			printk("\n %04X: %02X ", i, *wp);
		} else if (i % 16 == 15) {
			printk("%02X", *wp);
		} else {
			printk("%02X ", *wp);
		}
		wp++;
	}

	printk("------ PACKET END   ------ \n");
}

/* Code for reading the MAC address from the Arctic ethernet based on
 * similar code in PIBS */

static void __init writereg_9346(volatile u8 *iobase, u8 value)
{
	/* Switch to register page 3 */
	writeb(readb(iobase + NE_CMD) | 0xc0, iobase + NE_CMD);
	writeb(value, iobase + 0x01);
}

static u8 __init readreg_9346(volatile u8 *iobase)
{
	/* Switch to register page 3 */
	writeb(readb(iobase + NE_CMD) | 0xc0, iobase + NE_CMD);
	return readb(iobase + 0x01);
}

static void __init write_bit_9346(volatile u8 *iobase, u8 bit)
{
	u8 mask = ~0x06;
	
	writereg_9346(iobase, (readreg_9346(iobase) & mask) | bit);
	udelay(1000);
	writereg_9346(iobase, (readreg_9346(iobase) & mask) | bit | 0x04);
	udelay(1000);
}

static u8 __init read_bit_9346(volatile u8 *iobase)
{
	u8 bit;
	u8 mask = ~0x05;
	
	mask = ~0x05;
	writereg_9346(iobase, readreg_9346(iobase) & mask);
	udelay(1000);
	writereg_9346(iobase, (readreg_9346(iobase) & mask) | 0x04);
	bit = readreg_9346(iobase) & 0x01;
	udelay(1000);

	return bit;
}

static u16 __init arctic_read_9346(volatile u8 *iobase, unsigned long addr)
{
	unsigned long flags;
	int i;
	u16 data;

	local_irq_save(flags);

	/* Put the chip into 8390 programming mode */
	writereg_9346(iobase, (readreg_9346(iobase) & ~0xc0) | 0x80);
	udelay(1000);

	/* Send command (read 16-bit value) to EEPROM */
	/* Bring CS Low */
	writereg_9346(iobase, readreg_9346(iobase) & ~0x0f);
	udelay(1000);
	/* Bring CS High */
	writereg_9346(iobase, (readreg_9346(iobase) & ~0x0f) | 0x08);
	udelay(1000);

	/* Send a 1 */
	write_bit_9346(iobase, 0x02);
	/* Send opcode 0b10 */
	write_bit_9346(iobase, 0x02);
	write_bit_9346(iobase, 0x00);
	/* Send address to read */
	for (i = 0; i < 6; i++) {
		if (addr & 0x20)
			write_bit_9346(iobase, 0x02);
		else
			write_bit_9346(iobase, 0x00);
		addr <<= 1;
	}

	/* Read the value back, bit by bit */
	data = 0;
	for (i = 0; i < 16; i++) {
		data <<= 1;
		if (read_bit_9346(iobase))
			data |= 0x1;
	}

	/* Bring CS Low */
	writereg_9346(iobase, readreg_9346(iobase) & ~0x0f);
	udelay(1000);
	/* Bring the chip out of 8390 programming mode */
	writereg_9346(iobase, readreg_9346(iobase) & ~0xc0);
	udelay(1000);

	/* Return to register page 0 */
	writeb(readb(iobase + NE_CMD) & ~0xc0, iobase + NE_CMD);
	udelay(1000);

	local_irq_restore(flags);
	
	return data;
}

static void __init arctic_get_macaddr(struct net_device *dev)
{
	u16 t0, t1, t2, v0, v1;

	t0 = arctic_read_9346(iobase8, 0);
	t1 = arctic_read_9346(iobase8, 2);
	t2 = arctic_read_9346(iobase8, 4);
	v0 = arctic_read_9346(iobase8, 6);
	v1 = arctic_read_9346(iobase8, 8);

	if ( (v0 != 0x4d50) || (v1 != 0x5400) ) {
		printk(KERN_WARNING "%s: MAC address is not set in EEPROM\n", dev->name);
		return;
	}

	dev->dev_addr[0] = t0 >> 8;
	dev->dev_addr[1] = t0 & 0xff;
	dev->dev_addr[2] = t1 >> 8;
	dev->dev_addr[3] = t1 & 0xff;
	dev->dev_addr[4] = t2 >> 8;
	dev->dev_addr[5] = t2 & 0xff;
}

int __init arctic_enet_probe(struct net_device *dev)
{
	unsigned long reset_start_time;

	switch_8bit_bank();
	/* Reset card. Who knows what dain-bramaged state it was left in. */
	reset_start_time = jiffies;
	
	writeb(readb(iobase8 + NE_RESET), iobase8 + NE_RESET);

	while ((readb(iobase8 + NE_EN0_ISR) & ENISR_RESET) == 0)
		if (jiffies - reset_start_time > 2 * HZ / 100) {
			printk("arctic_enet: not found (no reset ack).\n");
			return -ENODEV;
		}
	
	writeb(0xff, iobase8 + NE_EN0_ISR);	/* Ack all intr. */

	arctic_get_macaddr(dev);

	printk("arctic_enet: found at 0x%08x/0x%08x, MAC address "
	       "%02x:%02x:%02x:%02x:%02x:%02x\n",
	       NE_BASE, NE_BASE16,
	       dev->dev_addr[0], dev->dev_addr[1], dev->dev_addr[2],
	       dev->dev_addr[3], dev->dev_addr[4], dev->dev_addr[5]);

	/* Hack to let 8390.c work properly - it assumes IO space
	 * addresses */
	dev->base_addr = (unsigned long)iobase8 - _IO_BASE;
	dev->irq = ARCTIC_ENET_IRQ;

	return 0;
}

static int __init arctic_enet_init(struct net_device *dev)
{
	static u32 arctic_enet_offsets[16] = {
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
		0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
	};

	/* Since this irq is connected to uic as edge interrupt, its pending must be cleared. */
	/* FIXME: it would be nice to get rid of the direct reference
	 * to the 4xx irq structure */
	ppc4xx_pic->ack(dev->irq);

	/* Install the Interrupt handler */
	if (request_irq(dev->irq, ei_interrupt, SA_SHIRQ, dev->name, dev))
		return -EAGAIN;

	/* Allocate dev->priv and fill in 8390 specific dev fields. */
	if (ethdev_init(dev)) {
		printk(" Unable to get memory for dev->priv.\n");
		return -ENOMEM;
	}
	
	/*
	 * Fill 8390 specific member for 8390 core driver
	 */
	ei_status.name = "RTL8019AS";
	ei_status.tx_start_page = NESM_START_PG;
	ei_status.stop_page = NESM_STOP_PG;
	ei_status.word16 = 1;
	ei_status.rx_start_page = NESM_START_PG + TX_PAGES;
	
	ei_status.reset_8390 = &arctic_enet_reset_8390;
	ei_status.block_input = &arctic_enet_block_input;
	ei_status.block_output = &arctic_enet_block_output;
	ei_status.get_8390_hdr = &arctic_enet_get_8390_hdr;
	ei_status.reg_offset = arctic_enet_offsets;

	NS8390_init(dev, 0);
	return 0;
}

static int arctic_enet_open(struct net_device *dev)
{
	int err;
	err = ei_open(dev);
	if (err)
		return err;

	MOD_INC_USE_COUNT;
	return 0;
}

static int arctic_enet_close(struct net_device *dev)
{
	int err;

	err = ei_close(dev);
	if (err)
		return err;

	MOD_DEC_USE_COUNT;
	return 0;
}

/* Hard reset the card.  This used to pause for the same period that a
   8390 reset command required, but that shouldn't be necessary. */
static void arctic_enet_reset_8390(struct net_device *dev)
{
	unsigned long reset_start_time = jiffies;

	if (ei_debug > 1)
		printk("resetting the 8390 t=%ld...", jiffies);

	writeb(readb(iobase8 + NE_RESET), iobase8 + NE_RESET);

	ei_status.txing = 0;
	ei_status.dmaing = 0;

	/* This check _should_not_ be necessary, omit eventually. */
	while ((readb(iobase8 + NE_EN0_ISR) & ENISR_RESET) == 0)
		if (jiffies - reset_start_time > 2 * HZ / 100) {
			printk("%s: ne_reset_8390() did not complete.\n",
			       dev->name);
			break;
		}
	writeb(ENISR_RESET, iobase8 + NE_EN0_ISR);	/* Ack intr. */
}

/* Grab the 8390 specific header. Similar to the block_input routine, but
   we don't need to be concerned with ring wrap as the header will be at
   the start of a page, so we optimize accordingly. */

static void arctic_enet_get_8390_hdr(struct net_device *dev,
				    struct e8390_pkt_hdr *hdr,
				    int ring_page)
{
	int cnt;
	u16 *ptrs;
	unsigned char *ptrc;

	/* This *shouldn't* happen. If it does, it's the last thing you'll see */
	if (ei_status.dmaing) {
		printk("%s: DMAing conflict in ne_get_8390_hdr "
		       "[DMAstat:%d][irqlock:%d].\n", dev->name,
		       ei_status.dmaing, ei_status.irqlock);
		return;
	}

	ei_status.dmaing |= 0x01;
	writeb(E8390_NODMA + E8390_PAGE0 + E8390_START, iobase8 + NE_CMD);
	writeb(ENISR_RDC, iobase8 + NE_EN0_ISR);
	writeb(sizeof(struct e8390_pkt_hdr), iobase8 + NE_EN0_RCNTLO);
	writeb(0, iobase8 + NE_EN0_RCNTHI);
	writeb(0, iobase8 + NE_EN0_RSARLO);	/* On page boundary */
	writeb(ring_page, iobase8 + NE_EN0_RSARHI);
	writeb(E8390_RREAD + E8390_START, iobase8 + NE_CMD);

	if (ei_status.word16) {
		switch_16bit_bank();
		ptrs = (u16 *) hdr;
		for (cnt = 0; cnt < (sizeof(struct e8390_pkt_hdr) >> 1);
		     cnt++)
			*ptrs++ = in_be16((u16 *) (iobase16 + NE_DATAPORT16));
		switch_8bit_bank();
	} else {

		ptrc = (unsigned char *) hdr;
		for (cnt = 0; cnt < sizeof(struct e8390_pkt_hdr); cnt++)
			*ptrc++ = readb(iobase8 + NE_DATAPORT);
	}


	writeb(ENISR_RDC, iobase8 + NE_EN0_ISR);	/* Ack intr. */

	/* I am Big Endian, but received byte count is Little Endian. */
	hdr->count = le16_to_cpu(hdr->count);

	ei_status.dmaing &= ~0x01;
}

/* Block input and output, similar to the Crynwr packet driver.  If you
   are porting to a new ethercard, look at the packet driver source for hints.
   The NEx000 doesn't share the on-board packet memory -- you have to put
   the packet out through the "remote DMA" dataport using writeb. */

static void arctic_enet_block_input(struct net_device *dev, int count,
				   struct sk_buff *skb, int ring_offset)
{
	char *buf = skb->data;
	u16 *ptrs;
	unsigned char *ptrc;

	int cnt;

	/* This *shouldn't* happen. If it does, it's the last thing you'll see */
	if (ei_status.dmaing) {
		printk("%s: DMAing conflict in ne_block_input "
		       "[DMAstat:%d][irqlock:%d].\n",
		       dev->name, ei_status.dmaing, ei_status.irqlock);
		return;
	}
	ei_status.dmaing |= 0x01;
	writeb(E8390_NODMA + E8390_PAGE0 + E8390_START, iobase8 + NE_CMD);
	writeb(ENISR_RDC, iobase8 + NE_EN0_ISR);
	writeb(count & 0xff, iobase8 + NE_EN0_RCNTLO);
	writeb(count >> 8, iobase8 + NE_EN0_RCNTHI);
	writeb(ring_offset & 0xff, iobase8 + NE_EN0_RSARLO);
	writeb(ring_offset >> 8, iobase8 + NE_EN0_RSARHI);
	writeb(E8390_RREAD + E8390_START, iobase8 + NE_CMD);


	if (ei_status.word16) {

		switch_16bit_bank();

		ptrs = (u16 *) buf;
		for (cnt = 0; cnt < (count >> 1); cnt++)
			/* At 16 bits mode, bus acts as Little Endian mode
			   That's swap is needed ??? */
			*ptrs++ = in_be16((u16 *) (iobase16 + NE_DATAPORT16));
		switch_8bit_bank();

		if (count & 0x01)
			buf[count - 1] = readb(iobase8 + NE_DATAPORT);

	} else {


		ptrc = (unsigned char *) buf;
		for (cnt = 0; cnt < count; cnt++)
			*ptrc++ = readb(iobase8 + NE_DATAPORT);
	}

	writeb(ENISR_RDC, iobase8 + NE_EN0_ISR);	/* Ack intr. */
	ei_status.dmaing &= ~0x01;
}

static void arctic_enet_block_output(struct net_device *dev, int count,
				    const unsigned char *buf,
				    const int start_page)
{
	unsigned long dma_start;
	u16 *ptrs;
	unsigned char *ptrc;
	int cnt;

	/* Round the count up for word writes.  Do we need to do this?
	   What effect will an odd byte count have on the 8390?
	   I should check someday. */
	if (count & 0x01)
		count++;

	/* This *shouldn't* happen. If it does, it's the last thing you'll see */
	if (ei_status.dmaing) {
		printk("%s: DMAing conflict in ne_block_output."
		       "[DMAstat:%d][irqlock:%d]\n", dev->name,
		       ei_status.dmaing, ei_status.irqlock);
		return;
	}
	ei_status.dmaing |= 0x01;

#if 1 /* FIXME: not sure what this is for -dwg */
	writeb(0x42, iobase8 + EN0_RCNTLO);
	writeb(0x00, iobase8 + EN0_RCNTHI);
	writeb(0x42, iobase8 + EN0_RSARLO);
	writeb(0x00, iobase8 + EN0_RSARHI);
#endif
	/* We should already be in page 0, but to be safe... */
	writeb(E8390_PAGE0 + E8390_START + E8390_NODMA, iobase8 + NE_CMD);

	writeb(ENISR_RDC, iobase8 + NE_EN0_ISR);

	/* Now the normal output. */
	writeb(count & 0xff, iobase8 + NE_EN0_RCNTLO);
	writeb(count >> 8, iobase8 + NE_EN0_RCNTHI);
	writeb(0x00, iobase8 + NE_EN0_RSARLO);
	writeb(start_page, iobase8 + NE_EN0_RSARHI);

	writeb(E8390_RWRITE + E8390_START, iobase8 + NE_CMD);

	if (ei_status.word16) {
		switch_16bit_bank();

		ptrs = (u16 *) buf;
		for (cnt = 0; cnt < count >> 1; cnt++) {
			/* At 16 bits mode, bus acts as Little Endian mode
			   That's swap is needed ??? */
			out_be16((u16 *) (iobase16 + NE_DATAPORT16),
				 *ptrs);
			ptrs++;
		}

		switch_8bit_bank();

	} else {
		ptrc = (unsigned char *) buf;
		for (cnt = 0; cnt < count; cnt++)
			writeb(*ptrc++, iobase8 + NE_DATAPORT);
	}

	dma_start = jiffies;

	while ((readb(iobase8 + NE_EN0_ISR) & ENISR_RDC) == 0)
		if (jiffies - dma_start > 2 * HZ / 100) {	/* 20ms */
			printk("%s: timeout waiting for Tx RDC.\n",
			       dev->name);
			arctic_enet_reset_8390(dev);
			NS8390_init(dev, 1);
			break;
		}

	writeb(ENISR_RDC, iobase8 + NE_EN0_ISR);	/* Ack intr. */
	ei_status.dmaing &= ~0x01;
	return;
}

static struct net_device arctic_enet_dev = {
	.init	= arctic_enet_init,
	.open	= arctic_enet_open,
	.stop	= arctic_enet_close,
};

int init_arctic_enet(void)
{
	struct net_device *dev = &arctic_enet_dev;
	int rsvd8 = 0;
	int rsvd16 = 0;
	int err;

	/* First set up our IO regions */
	if (! request_mem_region(NE_BASE, NE_IO_EXTENT, "arctic_enet"))
		goto fail;
	rsvd8 = 1;

	iobase8 = ioremap(NE_BASE, NE_IO_EXTENT);
	if (! iobase8) {
		err = -EBUSY;
		goto fail;
	}
	
	if (NE_BASE16 != NE_BASE) {
		if (! request_mem_region(NE_BASE16, NE_IO_EXTENT, "arctic_enet"))
			goto fail;
		rsvd16 = 1;
	}

	iobase16 = ioremap(NE_BASE16, NE_IO_EXTENT);
	if (! iobase16) {
		err = -EBUSY;
		goto fail;
	}	

	/* Configure IRQ */
	cli();
	mtdcr(DCRN_UIC0_TR, mfdcr(DCRN_UIC0_TR) | ARCTIC_ENET_IRQ_MASK);
	mtdcr(DCRN_UIC0_PR, mfdcr(DCRN_UIC0_PR) | ARCTIC_ENET_IRQ_MASK);
	mtdcr(DCRN_UIC0_SR, ARCTIC_ENET_IRQ_MASK);
	sti();

	err = arctic_enet_probe(dev);
	if (err) {
		printk(KERN_ERR "arctic_enet: No Arctic ethernet card found.\n");
		goto fail;
	}

	err = register_netdev(dev);
	if (err)
		goto fail;

	return 0;

 fail:
	if (iobase16)
		iounmap(iobase16);
	if (rsvd16)
		release_mem_region(NE_BASE16, NE_IO_EXTENT);
	if (iobase8)
		iounmap(iobase8);
	if (rsvd8)
		release_mem_region(NE_BASE, NE_IO_EXTENT);

	return err;
	
}

void remove_arctic_enet(void)
{
	unregister_netdev(&arctic_enet_dev);
	free_irq(ARCTIC_ENET_IRQ, &arctic_enet_dev);

	if (iobase16) {
		iounmap(iobase16);
		release_mem_region(NE_BASE16, NE_IO_EXTENT);
	}
	if (iobase8) {
		iounmap(iobase8);
		release_mem_region(NE_BASE, NE_IO_EXTENT);
	}
}

module_init(init_arctic_enet);
module_exit(remove_arctic_enet);
