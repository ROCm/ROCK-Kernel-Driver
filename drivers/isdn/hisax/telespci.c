/* $Id: telespci.c,v 2.16.6.5 2001/09/23 22:24:52 kai Exp $
 *
 * low level stuff for Teles PCI isdn cards
 *
 * Author       Ton van Rosmalen
 *              Karsten Keil
 * Copyright    by Ton van Rosmalen
 *              by Karsten Keil      <keil@isdn4linux.de>
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include <linux/init.h>
#include <linux/config.h>
#include "hisax.h"
#include "isac.h"
#include "hscx.h"
#include "isdnl1.h"
#include <linux/pci.h>

extern const char *CardType[];
const char *telespci_revision = "$Revision: 2.16.6.5 $";

#define ZORAN_PO_RQ_PEN	0x02000000
#define ZORAN_PO_WR	0x00800000
#define ZORAN_PO_GID0	0x00000000
#define ZORAN_PO_GID1	0x00100000
#define ZORAN_PO_GREG0	0x00000000
#define ZORAN_PO_GREG1	0x00010000
#define ZORAN_PO_DMASK	0xFF

#define WRITE_ADDR_ISAC	(ZORAN_PO_WR | ZORAN_PO_GID0 | ZORAN_PO_GREG0)
#define READ_DATA_ISAC	(ZORAN_PO_GID0 | ZORAN_PO_GREG1)
#define WRITE_DATA_ISAC	(ZORAN_PO_WR | ZORAN_PO_GID0 | ZORAN_PO_GREG1)
#define WRITE_ADDR_HSCX	(ZORAN_PO_WR | ZORAN_PO_GID1 | ZORAN_PO_GREG0)
#define READ_DATA_HSCX	(ZORAN_PO_GID1 | ZORAN_PO_GREG1)
#define WRITE_DATA_HSCX	(ZORAN_PO_WR | ZORAN_PO_GID1 | ZORAN_PO_GREG1)

#define ZORAN_WAIT_NOBUSY	do { \
					portdata = readl(adr); \
				} while (portdata & ZORAN_PO_RQ_PEN)

static u8
isac_read(struct IsdnCardState *cs, u8 off)
{
	void *adr = cs->hw.teles0.membase + 0x200;
	unsigned int portdata;

	ZORAN_WAIT_NOBUSY;
	
	/* set address for ISAC */
	writel(WRITE_ADDR_ISAC | off, adr);
	ZORAN_WAIT_NOBUSY;
	
	/* read data from ISAC */
	writel(READ_DATA_ISAC, adr);
	ZORAN_WAIT_NOBUSY;
	return((u8)(portdata & ZORAN_PO_DMASK));
}

static void
isac_write(struct IsdnCardState *cs, u8 off, u8 data)
{
	void *adr = cs->hw.teles0.membase + 0x200;
	unsigned int portdata;

	ZORAN_WAIT_NOBUSY;
	
	/* set address for ISAC */
	writel(WRITE_ADDR_ISAC | off, adr);
	ZORAN_WAIT_NOBUSY;

	/* write data to ISAC */
	writel(WRITE_DATA_ISAC | data, adr);
	ZORAN_WAIT_NOBUSY;
}

static void
isac_read_fifo(struct IsdnCardState *cs, u8 *data, int size)
{
	void *adr = cs->hw.teles0.membase + 0x200;
	unsigned int portdata;
	int i;

	ZORAN_WAIT_NOBUSY;
	/* read data from ISAC */
	for (i = 0; i < size; i++) {
		/* set address for ISAC fifo */
		writel(WRITE_ADDR_ISAC | 0x1E, adr);
		ZORAN_WAIT_NOBUSY;
		writel(READ_DATA_ISAC, adr);
		ZORAN_WAIT_NOBUSY;
		data[i] = (u8)(portdata & ZORAN_PO_DMASK);
	}
}

static void
isac_write_fifo(struct IsdnCardState *cs, u8 *data, int size)
{
	void *adr = cs->hw.teles0.membase + 0x200;
	unsigned int portdata;
	int i;

	ZORAN_WAIT_NOBUSY;
	/* write data to ISAC */
	for (i = 0; i < size; i++) {
		/* set address for ISAC fifo */
		writel(WRITE_ADDR_ISAC | 0x1E, adr);
		ZORAN_WAIT_NOBUSY;
		writel(WRITE_DATA_ISAC | data[i], adr);
		ZORAN_WAIT_NOBUSY;
	}
}

static struct dc_hw_ops isac_ops = {
	.read_reg   = isac_read,
	.write_reg  = isac_write,
	.read_fifo  = isac_read_fifo,
	.write_fifo = isac_write_fifo,
};

static u8
hscx_read(struct IsdnCardState *cs, int hscx, u8 off)
{
	void *adr = cs->hw.teles0.membase + 0x200;
	unsigned int portdata;

	ZORAN_WAIT_NOBUSY;
	/* set address for HSCX */
	writel(WRITE_ADDR_HSCX | ((hscx ? 0x40:0) + off), adr);
	ZORAN_WAIT_NOBUSY;
	
	/* read data from HSCX */
	writel(READ_DATA_HSCX, adr);
	ZORAN_WAIT_NOBUSY;
	return ((u8)(portdata & ZORAN_PO_DMASK));
}

static void
hscx_write(struct IsdnCardState *cs, int hscx, u8 off, u8 data)
{
	void *adr = cs->hw.teles0.membase + 0x200;
	unsigned int portdata;

	ZORAN_WAIT_NOBUSY;
	/* set address for HSCX */
	writel(WRITE_ADDR_HSCX | ((hscx ? 0x40:0) + off), adr);
	ZORAN_WAIT_NOBUSY;

	/* write data to HSCX */
	writel(WRITE_DATA_HSCX | data, adr);
	ZORAN_WAIT_NOBUSY;
}

static void
hscx_read_fifo(struct IsdnCardState *cs, int hscx, u8 * data, int size)
{
	void *adr = cs->hw.teles0.membase + 0x200;
	unsigned int portdata;
	int i;

	ZORAN_WAIT_NOBUSY;
	/* read data from HSCX */
	for (i = 0; i < size; i++) {
		/* set address for HSCX fifo */
		writel(WRITE_ADDR_HSCX |(hscx ? 0x5F:0x1F), adr);
		ZORAN_WAIT_NOBUSY;
		writel(READ_DATA_HSCX, adr);
		ZORAN_WAIT_NOBUSY;
		data[i] = (u8) (portdata & ZORAN_PO_DMASK);
	}
}

static void
hscx_write_fifo(struct IsdnCardState *cs, int hscx, u8 * data, int size)
{
	void *adr = cs->hw.teles0.membase + 0x200;
	unsigned int portdata;
	int i;

	ZORAN_WAIT_NOBUSY;
	/* write data to HSCX */
	for (i = 0; i < size; i++) {
		/* set address for HSCX fifo */
		writel(WRITE_ADDR_HSCX |(hscx ? 0x5F:0x1F), adr);
		ZORAN_WAIT_NOBUSY;
		writel(WRITE_DATA_HSCX | data[i], adr);
		ZORAN_WAIT_NOBUSY;
		udelay(10);
	}
}

static struct bc_hw_ops hscx_ops = {
	.read_reg   = hscx_read,
	.write_reg  = hscx_write,
	.read_fifo  = hscx_read_fifo,
	.write_fifo = hscx_write_fifo,
};

static void
telespci_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
#define MAXCOUNT 20
	struct IsdnCardState *cs = dev_id;
	u8 val;

	spin_lock(&cs->lock);
	val = hscx_read(cs, 1, HSCX_ISTA);
	if (val)
		hscx_int_main(cs, val);
	val = isac_read(cs, ISAC_ISTA);
	if (val)
		isac_interrupt(cs, val);
	/* Clear interrupt register for Zoran PCI controller */
	writel(0x70000000, cs->hw.teles0.membase + 0x3C);

	hscx_write(cs, 0, HSCX_MASK, 0xFF);
	hscx_write(cs, 1, HSCX_MASK, 0xFF);
	isac_write(cs, ISAC_MASK, 0xFF);
	isac_write(cs, ISAC_MASK, 0x0);
	hscx_write(cs, 0, HSCX_MASK, 0x0);
	hscx_write(cs, 1, HSCX_MASK, 0x0);
	spin_unlock(&cs->lock);
}

static int
TelesPCI_card_msg(struct IsdnCardState *cs, int mt, void *arg)
{
	return(0);
}

static struct card_ops telespci_ops = {
	.init     = inithscxisac,
	.release  = hisax_release_resources,
	.irq_func = telespci_interrupt,
};

static struct pci_dev *dev_tel __initdata = NULL;

int __init
setup_telespci(struct IsdnCard *card)
{
	struct IsdnCardState *cs = card->cs;
	char tmp[64];

#ifdef __BIG_ENDIAN
#error "not running on big endian machines now"
#endif
	strcpy(tmp, telespci_revision);
	printk(KERN_INFO "HiSax: Teles/PCI driver Rev. %s\n", HiSax_getrev(tmp));
	if (cs->typ != ISDN_CTYPE_TELESPCI)
		return (0);
	if ((dev_tel = pci_find_device (PCI_VENDOR_ID_ZORAN, PCI_DEVICE_ID_ZORAN_36120, dev_tel))) {
		if (pci_enable_device(dev_tel))
			return(0);
		cs->irq = dev_tel->irq;
		if (!cs->irq) {
			printk(KERN_WARNING "Teles: No IRQ for PCI card found\n");
			return(0);
		}
		cs->hw.teles0.membase = request_mmio(&cs->rs, pci_resource_start(dev_tel, 0), 4096, "telespci");
		if (!cs->hw.teles0.membase)
			goto err;
		printk(KERN_INFO "Found: Zoran, base-address: 0x%lx, irq: 0x%x\n",
			pci_resource_start(dev_tel, 0), dev_tel->irq);
	} else {
		printk(KERN_WARNING "TelesPCI: No PCI card found\n");
		return(0);
	}

	/* Initialize Zoran PCI controller */
	writel(0x00000000, cs->hw.teles0.membase + 0x28);
	writel(0x01000000, cs->hw.teles0.membase + 0x28);
	writel(0x01000000, cs->hw.teles0.membase + 0x28);
	writel(0x7BFFFFFF, cs->hw.teles0.membase + 0x2C);
	writel(0x70000000, cs->hw.teles0.membase + 0x3C);
	writel(0x61000000, cs->hw.teles0.membase + 0x40);
	/* writel(0x00800000, cs->hw.teles0.membase + 0x200); */
	printk(KERN_INFO
	       "HiSax: %s config irq:%d mem:%p\n",
	       CardType[cs->typ], cs->irq,
	       cs->hw.teles0.membase);

	cs->dc_hw_ops = &isac_ops;
	cs->bc_hw_ops = &hscx_ops;
	cs->cardmsg = &TelesPCI_card_msg;
	cs->irq_flags |= SA_SHIRQ;
	cs->card_ops = &telespci_ops;
	ISACVersion(cs, "TelesPCI:");
	if (HscxVersion(cs, "TelesPCI:")) {
		printk(KERN_WARNING
		 "TelesPCI: wrong HSCX versions check IO/MEM addresses\n");
		goto err;
	}
	return 1;
 err:
	hisax_release_resources(cs);
	return 0;
}
