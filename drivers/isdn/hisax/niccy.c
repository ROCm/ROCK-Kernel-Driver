/* $Id: niccy.c,v 1.15.6.6 2001/10/20 22:08:24 kai Exp $
 *
 * low level stuff for Dr. Neuhaus NICCY PnP and NICCY PCI and
 * compatible (SAGEM cybermodem)
 *
 * Author       Karsten Keil
 * Copyright    by Karsten Keil      <keil@isdn4linux.de>
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 * 
 * Thanks to Dr. Neuhaus and SAGEM for information
 *
 */


#include <linux/config.h>
#include <linux/init.h>
#include "hisax.h"
#include "isac.h"
#include "hscx.h"
#include "isdnl1.h"
#include <linux/pci.h>
#include <linux/isapnp.h>

extern const char *CardType[];
const char *niccy_revision = "$Revision: 1.15.6.6 $";
static spinlock_t niccy_lock = SPIN_LOCK_UNLOCKED;

#define byteout(addr,val) outb(val,addr)
#define bytein(addr) inb(addr)

#define ISAC_PCI_DATA	0
#define HSCX_PCI_DATA	1
#define ISAC_PCI_ADDR	2
#define HSCX_PCI_ADDR	3
#define ISAC_PNP	0
#define HSCX_PNP	1

/* SUB Types */
#define NICCY_PNP	1
#define NICCY_PCI	2

/* PCI stuff */
#define PCI_IRQ_CTRL_REG	0x38
#define PCI_IRQ_ENABLE		0x1f00
#define PCI_IRQ_DISABLE		0xff0000
#define PCI_IRQ_ASSERT		0x800000

static inline u8
readreg(unsigned int ale, unsigned int adr, u8 off)
{
	u8 ret;
	unsigned long flags;

	spin_lock_irqsave(&niccy_lock, flags);
	byteout(ale, off);
	ret = bytein(adr);
	spin_unlock_irqrestore(&niccy_lock, flags);
	return ret;
}

static inline void
writereg(unsigned int ale, unsigned int adr, u8 off, u8 data)
{
	unsigned long flags;

	spin_lock_irqsave(&niccy_lock, flags);
	byteout(ale, off);
	byteout(adr, data);
	spin_unlock_irqrestore(&niccy_lock, flags);
}

static inline void
readfifo(unsigned int ale, unsigned int adr, u8 off, u8 * data, int size)
{
	byteout(ale, off);
	insb(adr, data, size);
}

static inline void
writefifo(unsigned int ale, unsigned int adr, u8 off, u8 * data, int size)
{
	byteout(ale, off);
	outsb(adr, data, size);
}

static u8
isac_read(struct IsdnCardState *cs, u8 offset)
{
	return readreg(cs->hw.niccy.isac_ale, cs->hw.niccy.isac, offset);
}

static void
isac_write(struct IsdnCardState *cs, u8 offset, u8 value)
{
	writereg(cs->hw.niccy.isac_ale, cs->hw.niccy.isac, offset, value);
}

static void
isac_read_fifo(struct IsdnCardState *cs, u8 *data, int size)
{
	readfifo(cs->hw.niccy.isac_ale, cs->hw.niccy.isac, 0, data, size);
}

static void
isac_write_fifo(struct IsdnCardState *cs, u8 *data, int size)
{
	writefifo(cs->hw.niccy.isac_ale, cs->hw.niccy.isac, 0, data, size);
}

static struct dc_hw_ops isac_ops = {
	.read_reg   = isac_read,
	.write_reg  = isac_write,
	.read_fifo  = isac_read_fifo,
	.write_fifo = isac_write_fifo,
};

static u8
hscx_read(struct IsdnCardState *cs, int hscx, u8 offset)
{
	return readreg(cs->hw.niccy.hscx_ale,
		       cs->hw.niccy.hscx, offset + (hscx ? 0x40 : 0));
}

static void
hscx_write(struct IsdnCardState *cs, int hscx, u8 offset, u8 value)
{
	writereg(cs->hw.niccy.hscx_ale,
		 cs->hw.niccy.hscx, offset + (hscx ? 0x40 : 0), value);
}

static void
hscx_read_fifo(struct IsdnCardState *cs, int hscx, u8 *data, int size)
{
	readfifo(cs->hw.niccy.hscx_ale, cs->hw.niccy.hscx,
		 hscx ? 0x40 : 0, data, size);
}

static void
hscx_write_fifo(struct IsdnCardState *cs, int hscx, u8 *data, int size)
{
	writefifo(cs->hw.niccy.hscx_ale, cs->hw.niccy.hscx,
		  hscx ? 0x40 : 0, data, size);
}

static struct bc_hw_ops hscx_ops = {
	.read_reg  = hscx_read,
	.write_reg = hscx_write,
	.read_fifo  = hscx_read_fifo,
	.write_fifo = hscx_write_fifo,
};

static irqreturn_t
niccy_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
	struct IsdnCardState *cs = dev_id;
	
	if (cs->subtyp == NICCY_PCI) {
		int ival;
		ival = inl(cs->hw.niccy.cfg_reg + PCI_IRQ_CTRL_REG);
		if (!(ival & PCI_IRQ_ASSERT)) /* IRQ not for us (shared) */
			return IRQ_NONE;
		outl(ival, cs->hw.niccy.cfg_reg + PCI_IRQ_CTRL_REG);
	}
	return hscxisac_irq(intno, dev_id, regs);
}

void
niccy_release(struct IsdnCardState *cs)
{
	if (cs->subtyp == NICCY_PCI) {
		int val;
		
		val = inl(cs->hw.niccy.cfg_reg + PCI_IRQ_CTRL_REG);
		val &= PCI_IRQ_DISABLE;
		outl(val, cs->hw.niccy.cfg_reg + PCI_IRQ_CTRL_REG);
	}
	hisax_release_resources(cs);
}

static int
niccy_reset(struct IsdnCardState *cs)
{
	if (cs->subtyp == NICCY_PCI) {
		int val;

		val = inl(cs->hw.niccy.cfg_reg + PCI_IRQ_CTRL_REG);
		val |= PCI_IRQ_ENABLE;
		outl(val, cs->hw.niccy.cfg_reg + PCI_IRQ_CTRL_REG);
	}
	return 0;
}

static struct card_ops niccy_ops = {
	.init     = inithscxisac,
	.reset    = niccy_reset,
	.release  = niccy_release,
	.irq_func = niccy_interrupt,
};

static int __init
niccy_probe(struct IsdnCardState *cs)
{
	printk(KERN_INFO "HiSax: %s %s config irq:%d data:0x%X ale:0x%X\n",
	       CardType[cs->typ], (cs->subtyp==1) ? "PnP":"PCI",
	       cs->irq, cs->hw.niccy.isac, cs->hw.niccy.isac_ale);
	cs->card_ops = &niccy_ops;
	if (hscxisac_setup(cs, &isac_ops, &hscx_ops))
		return -EBUSY;
	return 0;
}

static int __init
niccy_pnp_probe(struct IsdnCardState *cs, struct IsdnCard *card)
{
	cs->subtyp = NICCY_PNP;
	cs->irq = card->para[0];
	cs->hw.niccy.isac = card->para[1] + ISAC_PNP;
	cs->hw.niccy.hscx = card->para[1] + HSCX_PNP;
	cs->hw.niccy.isac_ale = card->para[2] + ISAC_PNP;
	cs->hw.niccy.hscx_ale = card->para[2] + HSCX_PNP;
	cs->hw.niccy.cfg_reg = 0;

	if (!request_io(&cs->rs, cs->hw.niccy.isac, 2, "niccy data"))
		goto err;
	if (!request_io(&cs->rs, cs->hw.niccy.isac_ale, 2, "niccy addr"))
		goto err;
	if (niccy_probe(cs) < 0)
		goto err;
	return 0;
 err:
	hisax_release_resources(cs);
	return -EBUSY;
}

static int __init
niccy_pci_probe(struct IsdnCardState *cs, struct pci_dev *pdev)
{
	u32 pci_ioaddr;

	if (pci_enable_device(pdev))
		goto err;

	cs->subtyp = NICCY_PCI;
	cs->irq = pdev->irq;
	cs->irq_flags |= SA_SHIRQ;
	cs->hw.niccy.cfg_reg = pci_resource_start(pdev, 0);
	pci_ioaddr = pci_resource_start(pdev, 1);
	cs->hw.niccy.isac = pci_ioaddr + ISAC_PCI_DATA;
	cs->hw.niccy.isac_ale = pci_ioaddr + ISAC_PCI_ADDR;
	cs->hw.niccy.hscx = pci_ioaddr + HSCX_PCI_DATA;
	cs->hw.niccy.hscx_ale = pci_ioaddr + HSCX_PCI_ADDR;
	if (!request_io(&cs->rs, cs->hw.niccy.isac, 4, "niccy"))
		goto err;
	if (!request_io(&cs->rs, cs->hw.niccy.cfg_reg, 0x40, "niccy pci"))
		goto err;
	if (niccy_probe(cs) < 0)
		goto err;
	return 0;
 err:
	hisax_release_resources(cs);
	return -EBUSY;
}

static struct pci_dev *niccy_dev __initdata = NULL;
#ifdef __ISAPNP__
static struct pnp_card *pnp_c __devinitdata = NULL;
#endif

int __init
setup_niccy(struct IsdnCard *card)
{
	char tmp[64];

	strcpy(tmp, niccy_revision);
	printk(KERN_INFO "HiSax: Niccy driver Rev. %s\n", HiSax_getrev(tmp));
#ifdef __ISAPNP__
	if (!card->para[1] && isapnp_present()) {
		struct pnp_card *pb;
		struct pnp_dev  *pd;

		if ((pb = pnp_find_card(
			ISAPNP_VENDOR('S', 'D', 'A'),
			ISAPNP_FUNCTION(0x0150), pnp_c))) {
			pnp_c = pb;
			pd = NULL;
			if (!(pd = pnp_find_dev(pnp_c,
				ISAPNP_VENDOR('S', 'D', 'A'),
				ISAPNP_FUNCTION(0x0150), pd))) {
				printk(KERN_ERR "NiccyPnP: PnP error card found, no device\n");
				return (0);
			}
			if (pnp_device_attach(pd) < 0) {
				printk(KERN_ERR "NiccyPnP: attach failed\n");
				return 0;
			}
			if (pnp_activate_dev(pd) < 0) {
				printk(KERN_ERR "NiccyPnP: activate failed\n");
				pnp_device_detach(pd);
				return 0;
			}
			if (!pnp_irq_valid(pd, 0) || !pnp_port_valid(pd, 0) || !pnp_port_valid(pd, 1)) {
				printk(KERN_ERR "NiccyPnP:some resources are missing %ld/%lx/%lx\n",
					pnp_irq(pd, 0), pnp_port_start(pd, 0), pnp_port_start(pd, 1));
				pnp_device_detach(pd);
				return(0);
			}
			card->para[1] = pnp_port_start(pd, 0);
			card->para[2] = pnp_port_start(pd, 1);
			card->para[0] = pnp_irq(pd, 0);
		} else {
			printk(KERN_INFO "NiccyPnP: no ISAPnP card found\n");
		}
	}
#endif
	if (card->para[1]) {
		if (niccy_pnp_probe(card->cs, card) < 0)
			return 0;
		return 1;
	} else {
#ifdef CONFIG_PCI
		if ((niccy_dev = pci_find_device(PCI_VENDOR_ID_SATSAGEM,
			PCI_DEVICE_ID_SATSAGEM_NICCY, niccy_dev))) {
			if (niccy_pci_probe(card->cs, niccy_dev) < 0)
				return 0;
			return 1;
		}
#endif /* CONFIG_PCI */
	}
	return 0;
}
