/* $Id: gazel.c,v 2.11.6.7 2001/09/23 22:24:47 kai Exp $
 *
 * low level stuff for Gazel isdn cards
 *
 * Author       BeWan Systems
 *              based on source code from Karsten Keil
 * Copyright    by BeWan Systems
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include <linux/config.h>
#include <linux/init.h>
#include "hisax.h"
#include "isac.h"
#include "hscx.h"
#include "isdnl1.h"
#include "ipac.h"
#include <linux/pci.h>

extern const char *CardType[];
const char *gazel_revision = "$Revision: 2.11.6.7 $";
static spinlock_t gazel_lock = SPIN_LOCK_UNLOCKED;

#define R647      1
#define R685      2
#define R753      3
#define R742      4

#define PLX_CNTRL    0x50	/* registre de controle PLX */
#define RESET_GAZEL  0x4
#define RESET_9050   0x40000000
#define PLX_INCSR    0x4C	/* registre d'IT du 9050 */
#define INT_ISAC_EN  0x8	/* 1 = enable IT isac */
#define INT_ISAC     0x20	/* 1 = IT isac en cours */
#define INT_HSCX_EN  0x1	/* 1 = enable IT hscx */
#define INT_HSCX     0x4	/* 1 = IT hscx en cours */
#define INT_PCI_EN   0x40	/* 1 = enable IT PCI */
#define INT_IPAC_EN  0x3	/* enable IT ipac */


#define byteout(addr,val) outb(val,addr)
#define bytein(addr) inb(addr)

static inline u8
readreg(unsigned int adr, u_short off)
{
	return bytein(adr + off);
}

static inline void
writereg(unsigned int adr, u_short off, u8 data)
{
	byteout(adr + off, data);
}


static inline void
read_fifo(unsigned int adr, u8 * data, int size)
{
	insb(adr, data, size);
}

static void
write_fifo(unsigned int adr, u8 * data, int size)
{
	outsb(adr, data, size);
}

static u8
r685_isac_read(struct IsdnCardState *cs, u8 off)
{
	return readreg(cs->hw.gazel.isac, off);
}

static u8
r647_isac_read(struct IsdnCardState *cs, u8 off)
{
	return readreg(cs->hw.gazel.isac, (off << 8 & 0xf000) | (off & 0xf));
}

static void
r685_isac_write(struct IsdnCardState *cs, u8 off, u8 value)
{
	writereg(cs->hw.gazel.isac, off, value);
}

static void
r647_isac_write(struct IsdnCardState *cs, u8 off, u8 value)
{
	writereg(cs->hw.gazel.isac, (off << 8 & 0xf000) | (off & 0xf), value);
}

static void
isac_read_fifo(struct IsdnCardState *cs, u8 * data, int size)
{
	read_fifo(cs->hw.gazel.isacfifo, data, size);
}

static void
isac_write_fifo(struct IsdnCardState *cs, u8 * data, int size)
{
	write_fifo(cs->hw.gazel.isacfifo, data, size);
}

static struct dc_hw_ops r685_isac_ops = {
	.read_reg   = r685_isac_read,
	.write_reg  = r685_isac_write,
	.read_fifo  = isac_read_fifo,
	.write_fifo = isac_write_fifo,
};

static struct dc_hw_ops r647_isac_ops = {
	.read_reg   = r647_isac_read,
	.write_reg  = r647_isac_write,
	.read_fifo  = isac_read_fifo,
	.write_fifo = isac_write_fifo,
};
  
static u8
r685_hscx_read(struct IsdnCardState *cs, int hscx, u8 off)
{
	return readreg(cs->hw.gazel.hscx[hscx], off);
}

static u8
r647_hscx_read(struct IsdnCardState *cs, int hscx, u8 off)
{
	return readreg(cs->hw.gazel.hscx[hscx],
		       (off << 8 & 0xf000) | (off & 0xf));
}

static void
r685_hscx_write(struct IsdnCardState *cs, int hscx, u8 off, u8 value)
{
	writereg(cs->hw.gazel.hscx[hscx], off, value);
}

static void
r647_hscx_write(struct IsdnCardState *cs, int hscx, u8 off, u8 value)
{
	writereg(cs->hw.gazel.hscx[hscx],
		 (off << 8 & 0xf000) | (off & 0xf), value);
}

static void
hscx_read_fifo(struct IsdnCardState *cs, int hscx, u8 * data, int size)
{
	read_fifo(cs->hw.gazel.hscxfifo[hscx], data, size);
}

static void
hscx_write_fifo(struct IsdnCardState *cs, int hscx, u8 * data, int size)
{
	write_fifo(cs->hw.gazel.hscxfifo[hscx], data, size);
}

static struct bc_hw_ops r685_hscx_ops = {
	.read_reg   = r685_hscx_read,
	.write_reg  = r685_hscx_write,
	.read_fifo  = hscx_read_fifo,
	.write_fifo = hscx_write_fifo,
};

static struct bc_hw_ops r647_hscx_ops = {
	.read_reg   = r647_hscx_read,
	.write_reg  = r647_hscx_write,
	.read_fifo  = hscx_read_fifo,
	.write_fifo = hscx_write_fifo,
};

static inline u8
ipac_read(struct IsdnCardState *cs, u_short off)
{
	register u8 ret;
	unsigned long flags;

	spin_lock_irqsave(&gazel_lock, flags);
	byteout(cs->hw.gazel.ipac, off);
	ret = bytein(cs->hw.gazel.ipac + 4);
	spin_unlock_irqrestore(&gazel_lock, flags);
	return ret;
}

static inline void
ipac_write(struct IsdnCardState *cs, u_short off, u8 data)
{
	unsigned long flags;

	spin_lock_irqsave(&gazel_lock, flags);
	byteout(cs->hw.gazel.ipac, off);
	byteout(cs->hw.gazel.ipac + 4, data);
	spin_unlock_irqrestore(&gazel_lock, flags);
}


static inline void
ipac_readfifo(struct IsdnCardState *cs, u8 off, u8 * data, int size)
{
	byteout(cs->hw.gazel.ipac, off);
	insb(cs->hw.gazel.ipac + 4, data, size);
}

static inline void
ipac_writefifo(struct IsdnCardState *cs, u8 off, u8 * data, int size)
{
	byteout(cs->hw.gazel.ipac, off);
	outsb(cs->hw.gazel.ipac + 4, data, size);
}

/* This will generate ipac_dc_ops and ipac_bc_ops using the functions
 * above */

BUILD_IPAC_OPS(ipac);

static int
r647_reset(struct IsdnCardState *cs)
{
	writereg(cs->hw.gazel.cfg_reg, 0, 0);
	HZDELAY(10);
	writereg(cs->hw.gazel.cfg_reg, 0, 1);
	HZDELAY(2);
	return 0;
}

static int
r685_reset(struct IsdnCardState *cs)
{
	unsigned long plxcntrl, addr = cs->hw.gazel.cfg_reg;

	plxcntrl = inl(addr + PLX_CNTRL);
	plxcntrl |= (RESET_9050 + RESET_GAZEL);
	outl(plxcntrl, addr + PLX_CNTRL);
	plxcntrl &= ~(RESET_9050 + RESET_GAZEL);
	HZDELAY(4);
	outl(plxcntrl, addr + PLX_CNTRL);
	HZDELAY(10);
	outb(INT_ISAC_EN + INT_HSCX_EN + INT_PCI_EN, addr + PLX_INCSR);
	return 0;
}

static int
r753_reset(struct IsdnCardState *cs)
{
	unsigned long plxcntrl, addr = cs->hw.gazel.cfg_reg;

	if (test_bit(FLG_BUGGY_PLX9050, &cs->HW_Flags))
		/* we can't read, assume the default */
		plxcntrl = 0x18784db6;
	else
		plxcntrl = inl(addr + PLX_CNTRL);
	plxcntrl |= (RESET_9050 + RESET_GAZEL);
	outl(plxcntrl, addr + PLX_CNTRL);
	ipac_write(cs, IPAC_POTA2, 0x20);
	HZDELAY(4);
	plxcntrl &= ~(RESET_9050 + RESET_GAZEL);
	outl(plxcntrl, addr + PLX_CNTRL);
	HZDELAY(10);
	ipac_write(cs, IPAC_POTA2, 0x00);
	ipac_write(cs, IPAC_ACFG, 0xff);
	ipac_write(cs, IPAC_AOE, 0x0);
	ipac_write(cs, IPAC_MASK, 0xff);
	ipac_write(cs, IPAC_CONF, 0x1);
	outb(INT_IPAC_EN + INT_PCI_EN, addr + PLX_INCSR);
	ipac_write(cs, IPAC_MASK, 0xc0);
	return 0;
}

static int
r742_reset(struct IsdnCardState *cs)
{
	ipac_write(cs, IPAC_POTA2, 0x20);
	HZDELAY(4);
	ipac_write(cs, IPAC_POTA2, 0x00);
	ipac_write(cs, IPAC_ACFG, 0xff);
	ipac_write(cs, IPAC_AOE, 0x0);
	ipac_write(cs, IPAC_MASK, 0xff);
	ipac_write(cs, IPAC_CONF, 0x1);
	ipac_write(cs, IPAC_MASK, 0xc0);
	return 0;
}

static int
Gazel_card_msg(struct IsdnCardState *cs, int mt, void *arg)
{
	return (0);
}

static void
gazel_init(struct IsdnCardState *cs)
{
	int i;

	for (i = 0; i < 2; i++) {
		cs->bcs[i].hw.hscx.tsaxr0 = 0x1f;
		cs->bcs[i].hw.hscx.tsaxr1 = 0x23;
	}
	inithscxisac(cs);
}

static int
r647_reserve_regions(struct IsdnCardState *cs)
{
	int i, base = cs->hw.gazel.hscx[0];

	for (i = 0; i < 0xc000; i += 0x1000) {
		if (!request_io(&cs->rs, i + base, 16, "gazel"))
			goto err;
	}
	if (!request_io(&cs->rs, 0xc000 + base, 1, "gazel"))
		goto err;
	return 0;
 err:
	hisax_release_resources(cs);
	return -EBUSY;
}

static int
r685_reserve_regions(struct IsdnCardState *cs)
{
	if (!request_io(&cs->rs, cs->hw.gazel.hscx[0], 0x100, "gazel"))
		goto err;
	if (!request_io(&cs->rs, cs->hw.gazel.cfg_reg, 0x80, "gazel"))
		goto err;
	return 0;
 err:
	hisax_release_resources(cs);
	return -EBUSY;
}

static int
r742_reserve_regions(struct IsdnCardState *cs)
{
	if (!request_io(&cs->rs, cs->hw.gazel.ipac, 0x8, "gazel"))
		goto err;
	return 0;
 err:
	hisax_release_resources(cs);
	return -EBUSY;
}

static int
r753_reserve_regions(struct IsdnCardState *cs)
{
	if (!request_io(&cs->rs, cs->hw.gazel.ipac, 0x8, "gazel"))
		goto err;
	if (!request_io(&cs->rs, cs->hw.gazel.cfg_reg, 0x80, "gazel"))
		goto err;
	return 0;
 err:
	hisax_release_resources(cs);
	return -EBUSY;
}

static struct card_ops r647_ops = {
	.init     = gazel_init,
	.reset    = r647_reset,
	.release  = hisax_release_resources,
	.irq_func = hscxisac_irq,
};

static struct card_ops r685_ops = {
	.init     = gazel_init,
	.reset    = r685_reset,
	.release  = hisax_release_resources,
	.irq_func = hscxisac_irq,
};

static struct card_ops r742_ops = {
	.init     = ipac_init,
	.reset    = r742_reset,
	.release  = hisax_release_resources,
	.irq_func = ipac_irq,
};

static struct card_ops r753_ops = {
	.init     = ipac_init,
	.reset    = r753_reset,
	.release  = hisax_release_resources,
	.irq_func = ipac_irq,
};

static int __init
setup_gazelisa(struct IsdnCard *card, struct IsdnCardState *cs)
{
	printk(KERN_INFO "Gazel: ISA PnP card automatic recognition\n");
	// we got an irq parameter, assume it is an ISA card
	// R742 decodes address even in not started...
	// R647 returns FF if not present or not started
	// eventually needs improvment
	cs->hw.gazel.ipac = card->para[1];
	if (ipac_read(cs, IPAC_ID) == 1)
		cs->subtyp = R742;
	else
		cs->subtyp = R647;

	cs->hw.gazel.cfg_reg = card->para[1] + 0xC000;
	cs->hw.gazel.isac = card->para[1] + 0x8000;
	cs->hw.gazel.hscx[0] = card->para[1];
	cs->hw.gazel.hscx[1] = card->para[1] + 0x4000;
	cs->irq = card->para[0];
	cs->hw.gazel.isacfifo = cs->hw.gazel.isac;
	cs->hw.gazel.hscxfifo[0] = cs->hw.gazel.hscx[0];
	cs->hw.gazel.hscxfifo[1] = cs->hw.gazel.hscx[1];

	switch (cs->subtyp) {
	case R647:
		printk(KERN_INFO "Gazel: Card ISA R647/R648 found\n");
		cs->dc.isac.adf2 = 0x87;
		printk(KERN_INFO
		       "Gazel: config irq:%d isac:0x%X  cfg:0x%X\n",
		       cs->irq, cs->hw.gazel.isac, cs->hw.gazel.cfg_reg);
		printk(KERN_INFO
		       "Gazel: hscx A:0x%X  hscx B:0x%X\n",
		       cs->hw.gazel.hscx[0], cs->hw.gazel.hscx[1]);
		
		return r647_reserve_regions(cs);
	case R742:
		printk(KERN_INFO "Gazel: Card ISA R742 found\n");
		printk(KERN_INFO
		       "Gazel: config irq:%d ipac:0x%X\n",
		       cs->irq, cs->hw.gazel.ipac);
		return r742_reserve_regions(cs);
	}
	return 0;
}

static struct pci_dev *dev_tel __initdata = NULL;

static int __init
setup_gazelpci(struct IsdnCardState *cs)
{
	u_int pci_ioaddr0 = 0, pci_ioaddr1 = 0;
	u8 pci_irq = 0, found;
	u_int nbseek, seekcard;
	u8 pci_rev;

	printk(KERN_WARNING "Gazel: PCI card automatic recognition\n");

	found = 0;
	if (!pci_present()) {
		printk(KERN_WARNING "Gazel: No PCI bus present\n");
		return 1;
	}
	seekcard = PCI_DEVICE_ID_PLX_R685;
	for (nbseek = 0; nbseek < 3; nbseek++) {
		if ((dev_tel = pci_find_device(PCI_VENDOR_ID_PLX, seekcard, dev_tel))) {
			if (pci_enable_device(dev_tel))
				return 1;
			pci_irq = dev_tel->irq;
			pci_ioaddr0 = pci_resource_start(dev_tel, 1);
			pci_ioaddr1 = pci_resource_start(dev_tel, 2);
			found = 1;
		}
		if (found)
			break;
		else {
			switch (seekcard) {
			case PCI_DEVICE_ID_PLX_R685:
				seekcard = PCI_DEVICE_ID_PLX_R753;
				break;
			case PCI_DEVICE_ID_PLX_R753:
				seekcard = PCI_DEVICE_ID_PLX_DJINN_ITOO;
				break;
			}
		}
	}
	if (!found) {
		printk(KERN_WARNING "Gazel: No PCI card found\n");
		return -ENODEV;
	}
	if (!pci_irq) {
		printk(KERN_WARNING "Gazel: No IRQ for PCI card found\n");
		return -ENODEV;
	}
	cs->hw.gazel.pciaddr[0] = pci_ioaddr0;
	cs->hw.gazel.pciaddr[1] = pci_ioaddr1;

	pci_ioaddr1 &= 0xfffe;
	cs->hw.gazel.cfg_reg = pci_ioaddr0 & 0xfffe;
	cs->hw.gazel.ipac = pci_ioaddr1;
	cs->hw.gazel.isac = pci_ioaddr1 + 0x80;
	cs->hw.gazel.hscx[0] = pci_ioaddr1;
	cs->hw.gazel.hscx[1] = pci_ioaddr1 + 0x40;
	cs->hw.gazel.isacfifo = cs->hw.gazel.isac;
	cs->hw.gazel.hscxfifo[0] = cs->hw.gazel.hscx[0];
	cs->hw.gazel.hscxfifo[1] = cs->hw.gazel.hscx[1];
	cs->irq = pci_irq;
	cs->irq_flags |= SA_SHIRQ;

	switch (seekcard) {
	case PCI_DEVICE_ID_PLX_R685:
		printk(KERN_INFO "Gazel: Card PCI R685 found\n");
		cs->subtyp = R685;
		cs->dc.isac.adf2 = 0x87;
		printk(KERN_INFO
		       "Gazel: config irq:%d isac:0x%X  cfg:0x%X\n",
		       cs->irq, cs->hw.gazel.isac, cs->hw.gazel.cfg_reg);
		printk(KERN_INFO
		       "Gazel: hscx A:0x%X  hscx B:0x%X\n",
		       cs->hw.gazel.hscx[0], cs->hw.gazel.hscx[1]);
		return r685_reserve_regions(cs);
	case PCI_DEVICE_ID_PLX_R753:
	case PCI_DEVICE_ID_PLX_DJINN_ITOO:
		printk(KERN_INFO "Gazel: Card PCI R753 found\n");
		cs->subtyp = R753;
		printk(KERN_INFO
		       "Gazel: config irq:%d ipac:0x%X  cfg:0x%X\n",
		       cs->irq, cs->hw.gazel.ipac, cs->hw.gazel.cfg_reg);
		/* 
		 * Erratum for PLX9050, revision 1:
		 * If bit 7 of BAR 0/1 is set, local config registers
		 * can not be read (write is okay)
		 */
		if (cs->hw.gazel.cfg_reg & 0x80) {
			pci_read_config_byte(dev_tel, PCI_REVISION_ID, &pci_rev);
			if (pci_rev == 1) {
				printk(KERN_INFO "Gazel: PLX9050 rev1 workaround activated\n");
				set_bit(FLG_BUGGY_PLX9050, &cs->HW_Flags);
			}
		}
		return r753_reserve_regions(cs);
	}
	return 0;
}

int __init
setup_gazel(struct IsdnCard *card)
{
	struct IsdnCardState *cs = card->cs;
	char tmp[64];
	u8 val;

	strcpy(tmp, gazel_revision);
	printk(KERN_INFO "Gazel: Driver Revision %s\n", HiSax_getrev(tmp));

	if (cs->typ != ISDN_CTYPE_GAZEL)
		return (0);

	if (card->para[0]) {
		if (setup_gazelisa(card, cs))
			return (0);
	} else {
		if (setup_gazelpci(cs))
			return (0);
	}

	cs->cardmsg = &Gazel_card_msg;

	switch (cs->subtyp) {
	case R647:
	case R685:
		if (cs->subtyp == R647) {
			cs->dc_hw_ops = &r647_isac_ops;
			cs->bc_hw_ops = &r647_hscx_ops;
			cs->card_ops  = &r647_ops;
		} else {
			cs->dc_hw_ops = &r685_isac_ops;
			cs->bc_hw_ops = &r685_hscx_ops;
			cs->card_ops  = &r685_ops;
		}
		cs->card_ops->reset(cs);
		ISACVersion(cs, "Gazel:");
		if (HscxVersion(cs, "Gazel:")) {
			printk(KERN_WARNING
			       "Gazel: wrong HSCX versions check IO address\n");
			cs->card_ops->release(cs);
				return (0);
		}
		break;
	case R742:
	case R753:
		if (cs->subtyp == R742) {
			cs->card_ops = &r742_ops;
		} else {
			cs->card_ops = &r753_ops;
		}
		cs->dc_hw_ops = &ipac_dc_ops;
		cs->bc_hw_ops = &ipac_bc_ops;
		cs->card_ops->reset(cs);
		val = ipac_read(cs, IPAC_ID);
		printk(KERN_INFO "Gazel: IPAC version %x\n", val);
		break;
	}

	return 1;
}
