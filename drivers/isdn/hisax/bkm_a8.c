/* $Id: bkm_a8.c,v 1.14.6.7 2001/09/23 22:24:46 kai Exp $
 *
 * low level stuff for Scitel Quadro (4*S0, passive)
 *
 * Author       Roland Klabunde
 * Copyright    by Roland Klabunde   <R.Klabunde@Berkom.de>
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include <linux/config.h>
#include <linux/init.h>
#include "hisax.h"
#include "isac.h"
#include "ipac.h"
#include "hscx.h"
#include "isdnl1.h"
#include <linux/pci.h>
#include "bkm_ax.h"

#define	ATTEMPT_PCI_REMAPPING	/* Required for PLX rev 1 */

extern const char *CardType[];
static spinlock_t bkm_a8_lock = SPIN_LOCK_UNLOCKED;
const char sct_quadro_revision[] = "$Revision: 1.14.6.7 $";

static const char *sct_quadro_subtypes[] =
{
	"",
	"#1",
	"#2",
	"#3",
	"#4"
};


#define wordout(addr,val) outw(val,addr)
#define wordin(addr) inw(addr)

static inline u8
ipac_read(struct IsdnCardState *cs, u8 off)
{
	u8 ret;
	unsigned long flags;

	spin_lock_irqsave(&bkm_a8_lock, flags);
	wordout(cs->hw.ax.base, off);
	ret = wordin(cs->hw.ax.data_adr) & 0xFF;
	spin_unlock_irqrestore(&bkm_a8_lock, flags);
	return ret;
}

static inline void
ipac_write(struct IsdnCardState *cs, u8 off, u8 data)
{
	unsigned long flags;

	spin_lock_irqsave(&bkm_a8_lock, flags);
	wordout(cs->hw.ax.base, off);
	wordout(cs->hw.ax.data_adr, data);
	spin_unlock_irqrestore(&bkm_a8_lock, flags);
}

static inline void
ipac_readfifo(struct IsdnCardState *cs, u8 off, u8 *data, int size)
{
	int i;

	wordout(cs->hw.ax.base, off);
	for (i = 0; i < size; i++)
		data[i] = wordin(cs->hw.ax.data_adr) & 0xFF;
}

static inline void
ipac_writefifo(struct IsdnCardState *cs, u8 off, u8 *data, int size)
{
	int i;

	wordout(cs->hw.ax.base, off);
	for (i = 0; i < size; i++)
		wordout(cs->hw.ax.data_adr, data[i]);
}

/* This will generate ipac_dc_ops and ipac_bc_ops using the functions
 * above */

BUILD_IPAC_OPS(ipac);
  
/* Set the specific ipac to active */
static void
set_ipac_active(struct IsdnCardState *cs, u_int active)
{
	/* set irq mask */
	ipac_write(cs, IPAC_MASK, active ? 0xc0 : 0xff);
}

static void
enable_bkm_int(struct IsdnCardState *cs, unsigned bEnable)
{
	if (bEnable)
		wordout(cs->hw.ax.plx_adr + 0x4C, (wordin(cs->hw.ax.plx_adr + 0x4C) | 0x41));
	else
		wordout(cs->hw.ax.plx_adr + 0x4C, (wordin(cs->hw.ax.plx_adr + 0x4C) & ~0x41));
}

static void
reset_bkm(struct IsdnCardState *cs)
{
	if (cs->subtyp == SCT_1) {
		wordout(cs->hw.ax.plx_adr + 0x50, (wordin(cs->hw.ax.plx_adr + 0x50) & ~4));
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout((10 * HZ) / 1000);
		/* Remove the soft reset */
		wordout(cs->hw.ax.plx_adr + 0x50, (wordin(cs->hw.ax.plx_adr + 0x50) | 4));
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout((10 * HZ) / 1000);
	}
}

static void
bkm_a8_init(struct IsdnCardState *cs)
{
	cs->debug |= L1_DEB_IPAC;
	set_ipac_active(cs, 1);
	ipac_init(cs);
	/* Enable ints */
	enable_bkm_int(cs, 1);
}

static int
bkm_a8_reset(struct IsdnCardState *cs)
{
	/* Disable ints */
	set_ipac_active(cs, 0);
	enable_bkm_int(cs, 0);
	reset_bkm(cs);
	return 0;
}

static void
bkm_a8_release(struct IsdnCardState *cs)
{
	set_ipac_active(cs, 0);
	enable_bkm_int(cs, 0);
	hisax_release_resources(cs);
}

static struct card_ops bkm_a8_ops = {
	.init     = bkm_a8_init,
	.reset    = bkm_a8_reset,
	.release  = bkm_a8_release,
	.irq_func = ipac_irq,
};

static struct pci_dev *dev_a8 __initdata = NULL;
static u16  sub_vendor_id __initdata = 0;
static u16  sub_sys_id __initdata = 0;
static u8 pci_irq __initdata = 0;

int __init
setup_sct_quadro(struct IsdnCard *card)
{
	struct IsdnCardState *cs = card->cs;
	char tmp[64];
	u8 pci_rev_id;
	u_int found = 0;
	u_int pci_ioaddr1, pci_ioaddr2, pci_ioaddr3, pci_ioaddr4, pci_ioaddr5;

	strcpy(tmp, sct_quadro_revision);
	printk(KERN_INFO "HiSax: T-Berkom driver Rev. %s\n", HiSax_getrev(tmp));
	/* Identify subtype by para[0] */
	if (card->para[0] >= SCT_1 && card->para[0] <= SCT_4)
		cs->subtyp = card->para[0];
	else {
		printk(KERN_WARNING "HiSax: %s: Invalid subcontroller in configuration, default to 1\n",
			CardType[card->typ]);
		return (0);
	}
	if ((cs->subtyp != SCT_1) && ((sub_sys_id != PCI_DEVICE_ID_BERKOM_SCITEL_QUADRO) ||
		(sub_vendor_id != PCI_VENDOR_ID_BERKOM)))
		return (0);
	if (cs->subtyp == SCT_1) {
		while ((dev_a8 = pci_find_device(PCI_VENDOR_ID_PLX,
			PCI_DEVICE_ID_PLX_9050, dev_a8))) {
			
			sub_vendor_id = dev_a8->subsystem_vendor;
			sub_sys_id = dev_a8->subsystem_device;
			if ((sub_sys_id == PCI_DEVICE_ID_BERKOM_SCITEL_QUADRO) &&
				(sub_vendor_id == PCI_VENDOR_ID_BERKOM)) {
				if (pci_enable_device(dev_a8))
					return(0);
				pci_ioaddr1 = pci_resource_start(dev_a8, 1);
				pci_irq = dev_a8->irq;
				found = 1;
				break;
			}
		}
		if (!found) {
			printk(KERN_WARNING "HiSax: %s (%s): Card not found\n",
				CardType[card->typ],
				sct_quadro_subtypes[cs->subtyp]);
			return (0);
		}
#ifdef ATTEMPT_PCI_REMAPPING
/* HACK: PLX revision 1 bug: PLX address bit 7 must not be set */
		pci_read_config_byte(dev_a8, PCI_REVISION_ID, &pci_rev_id);
		if ((pci_ioaddr1 & 0x80) && (pci_rev_id == 1)) {
			printk(KERN_WARNING "HiSax: %s (%s): PLX rev 1, remapping required!\n",
				CardType[card->typ],
				sct_quadro_subtypes[cs->subtyp]);
			/* Restart PCI negotiation */
			pci_write_config_dword(dev_a8, PCI_BASE_ADDRESS_1, (u_int) - 1);
			/* Move up by 0x80 byte */
			pci_ioaddr1 += 0x80;
			pci_ioaddr1 &= PCI_BASE_ADDRESS_IO_MASK;
			pci_write_config_dword(dev_a8, PCI_BASE_ADDRESS_1, pci_ioaddr1);
			dev_a8->resource[ 1].start = pci_ioaddr1;
		}
#endif /* End HACK */
	}
	if (!pci_irq) {		/* IRQ range check ?? */
		printk(KERN_WARNING "HiSax: %s (%s): No IRQ\n",
		       CardType[card->typ],
		       sct_quadro_subtypes[cs->subtyp]);
		return (0);
	}
	pci_read_config_dword(dev_a8, PCI_BASE_ADDRESS_1, &pci_ioaddr1);
	pci_read_config_dword(dev_a8, PCI_BASE_ADDRESS_2, &pci_ioaddr2);
	pci_read_config_dword(dev_a8, PCI_BASE_ADDRESS_3, &pci_ioaddr3);
	pci_read_config_dword(dev_a8, PCI_BASE_ADDRESS_4, &pci_ioaddr4);
	pci_read_config_dword(dev_a8, PCI_BASE_ADDRESS_5, &pci_ioaddr5);
	if (!pci_ioaddr1 || !pci_ioaddr2 || !pci_ioaddr3 || !pci_ioaddr4 || !pci_ioaddr5) {
		printk(KERN_WARNING "HiSax: %s (%s): No IO base address(es)\n",
		       CardType[card->typ],
		       sct_quadro_subtypes[cs->subtyp]);
		return (0);
	}
	pci_ioaddr1 &= PCI_BASE_ADDRESS_IO_MASK;
	pci_ioaddr2 &= PCI_BASE_ADDRESS_IO_MASK;
	pci_ioaddr3 &= PCI_BASE_ADDRESS_IO_MASK;
	pci_ioaddr4 &= PCI_BASE_ADDRESS_IO_MASK;
	pci_ioaddr5 &= PCI_BASE_ADDRESS_IO_MASK;
	/* Take over */
	cs->irq = pci_irq;
	cs->irq_flags |= SA_SHIRQ;
	/* pci_ioaddr1 is unique to all subdevices */
	/* pci_ioaddr2 is for the fourth subdevice only */
	/* pci_ioaddr3 is for the third subdevice only */
	/* pci_ioaddr4 is for the second subdevice only */
	/* pci_ioaddr5 is for the first subdevice only */
	cs->hw.ax.plx_adr = pci_ioaddr1;
	/* Enter all ipac_base addresses */
	switch(cs->subtyp) {
	case 1:
		cs->hw.ax.base = pci_ioaddr5 + 0x00;
		if (!request_io(&cs->rs, pci_ioaddr1, 128, "scitel"))
			goto err;
		if (!request_io(&cs->rs, pci_ioaddr5, 64, "scitel"))
			goto err;
		break;
	case 2:
		cs->hw.ax.base = pci_ioaddr4 + 0x08;
		if (!request_io(&cs->rs, pci_ioaddr4, 64, "scitel"))
			goto err;
		break;
	case 3:
		cs->hw.ax.base = pci_ioaddr3 + 0x10;
		if (!request_io(&cs->rs, pci_ioaddr3, 64, "scitel"))
			goto err;
		break;
	case 4:
		cs->hw.ax.base = pci_ioaddr2 + 0x20;
		if (!request_io(&cs->rs, pci_ioaddr2, 64, "scitel"))
			goto err;
		break;
	}	
	cs->hw.ax.data_adr = cs->hw.ax.base + 4;
	ipac_write(cs, IPAC_MASK, 0xFF);

	printk(KERN_INFO "HiSax: %s (%s) configured at 0x%.4lX, 0x%.4lX, 0x%.4lX and IRQ %d\n",
	       CardType[card->typ],
	       sct_quadro_subtypes[cs->subtyp],
	       cs->hw.ax.plx_adr,
	       cs->hw.ax.base,
	       cs->hw.ax.data_adr,
	       cs->irq);

	cs->card_ops = &bkm_a8_ops;
	if (ipac_setup(cs, &ipac_dc_ops, &ipac_bc_ops))
		goto err;

	return 1;
 err:
	hisax_release_resources(cs);
	return 0;
}
