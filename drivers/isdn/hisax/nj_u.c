/* $Id: nj_u.c,v 2.8.6.6 2001/09/23 22:24:50 kai Exp $ 
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include <linux/config.h>
#include <linux/init.h>
#include "hisax.h"
#include "icc.h"
#include "isdnl1.h"
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/ppp_defs.h>
#include "netjet.h"

const char *NETjet_U_revision = "$Revision: 2.8.6.6 $";

static void
nj_u_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
	struct IsdnCardState *cs = dev_id;
	u8 val, sval;

	spin_lock(&cs->lock);
	if (!((sval = bytein(cs->hw.njet.base + NETJET_IRQSTAT1)) &
		NETJET_ISACIRQ)) {
		val = NETjet_ReadIC(cs, ICC_ISTA);
		if (cs->debug & L1_DEB_ISAC)
			debugl1(cs, "tiger: i1 %x %x", sval, val);
		if (val) {
			icc_interrupt(cs, val);
			NETjet_WriteIC(cs, ICC_MASK, 0xFF);
			NETjet_WriteIC(cs, ICC_MASK, 0x0);
		}
	}
	/* start new code 13/07/00 GE */
	/* set bits in sval to indicate which page is free */
	if (inl(cs->hw.njet.base + NETJET_DMA_WRITE_ADR) <
		inl(cs->hw.njet.base + NETJET_DMA_WRITE_IRQ))
		/* the 2nd write page is free */
		sval = 0x08;
	else	/* the 1st write page is free */
		sval = 0x04;	
	if (inl(cs->hw.njet.base + NETJET_DMA_READ_ADR) <
		inl(cs->hw.njet.base + NETJET_DMA_READ_IRQ))
		/* the 2nd read page is free */
		sval = sval | 0x02;
	else	/* the 1st read page is free */
		sval = sval | 0x01;	
	if (sval != cs->hw.njet.last_is0) /* we have a DMA interrupt */
	{
		cs->hw.njet.irqstat0 = sval;
		if ((cs->hw.njet.irqstat0 & NETJET_IRQM0_READ) != 
			(cs->hw.njet.last_is0 & NETJET_IRQM0_READ))
			/* we have a read dma int */
			read_tiger(cs);
		if ((cs->hw.njet.irqstat0 & NETJET_IRQM0_WRITE) !=
			(cs->hw.njet.last_is0 & NETJET_IRQM0_WRITE))
			/* we have a write dma int */
			write_tiger(cs);
		/* end new code 13/07/00 GE */
	}
/*	if (!testcnt--) {
		cs->hw.njet.dmactrl = 0;
		byteout(cs->hw.njet.base + NETJET_DMACTRL,
			cs->hw.njet.dmactrl);
		byteout(cs->hw.njet.base + NETJET_IRQMASK0, 0);
	}
*/
	spin_unlock(&cs->lock);
}

static int
nj_u_reset(struct IsdnCardState *cs)
{
	cs->hw.njet.ctrl_reg = 0xff;  /* Reset On */
	byteout(cs->hw.njet.base + NETJET_CTRL, cs->hw.njet.ctrl_reg);
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout((10*HZ)/1000);	/* Timeout 10ms */
	cs->hw.njet.ctrl_reg = 0x40;  /* Reset Off and status read clear */
	/* now edge triggered for TJ320 GE 13/07/00 */
	byteout(cs->hw.njet.base + NETJET_CTRL, cs->hw.njet.ctrl_reg);
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout((10*HZ)/1000);	/* Timeout 10ms */

	cs->hw.njet.auxd = 0xC0;
	cs->hw.njet.dmactrl = 0;
	byteout(cs->hw.njet.auxa, 0);
	byteout(cs->hw.njet.base + NETJET_AUXCTRL, ~NETJET_ISACIRQ);
	byteout(cs->hw.njet.base + NETJET_IRQMASK1, NETJET_ISACIRQ);
	byteout(cs->hw.njet.auxa, cs->hw.njet.auxd);
	return 0;
}

static int
NETjet_U_card_msg(struct IsdnCardState *cs, int mt, void *arg)
{
	return(0);
}

static void
nj_u_init(struct IsdnCardState *cs)
{
	inittiger(cs);
	initicc(cs);
	/* Reenable all IRQ */
	NETjet_WriteIC(cs, ICC_MASK, 0);
}

static struct card_ops nj_u_ops = {
	.init     = nj_u_init,
	.reset    = nj_u_reset,
	.release  = netjet_release,
	.irq_func = nj_u_interrupt,
};

static struct pci_dev *dev_netjet __initdata = NULL;

int __init
setup_netjet_u(struct IsdnCard *card)
{
	struct IsdnCardState *cs = card->cs;
	char tmp[64];
#if CONFIG_PCI
#endif
#ifdef __BIG_ENDIAN
#error "not running on big endian machines now"
#endif
	strcpy(tmp, NETjet_U_revision);
	printk(KERN_INFO "HiSax: Traverse Tech. NETspider-U driver Rev. %s\n", HiSax_getrev(tmp));
	if (cs->typ != ISDN_CTYPE_NETJET_U)
		return(0);

#if CONFIG_PCI

	for ( ;; )
	{
		if (!pci_present()) {
			printk(KERN_ERR "Netjet: no PCI bus present\n");
			return(0);
		}
		if ((dev_netjet = pci_find_device(PCI_VENDOR_ID_TIGERJET,
			PCI_DEVICE_ID_TIGERJET_300,  dev_netjet))) {
			if (pci_enable_device(dev_netjet))
				return(0);
			pci_set_master(dev_netjet);
			cs->irq = dev_netjet->irq;
			if (!cs->irq) {
				printk(KERN_WARNING "NETspider-U: No IRQ for PCI card found\n");
				return(0);
			}
			cs->hw.njet.base = pci_resource_start(dev_netjet, 0);
			if (!cs->hw.njet.base) {
				printk(KERN_WARNING "NETspider-U: No IO-Adr for PCI card found\n");
				return(0);
			}
			cs->hw.njet.pdev = dev_netjet;
		} else {
			printk(KERN_WARNING "NETspider-U: No PCI card found\n");
			return(0);
		}

		cs->hw.njet.auxa = cs->hw.njet.base + NETJET_AUXDATA;
		cs->hw.njet.isac = cs->hw.njet.base | NETJET_ISAC_OFF;

		cs->hw.njet.ctrl_reg = 0xff;  /* Reset On */
		byteout(cs->hw.njet.base + NETJET_CTRL, cs->hw.njet.ctrl_reg);

		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout((10*HZ)/1000);	/* Timeout 10ms */

		cs->hw.njet.ctrl_reg = 0x00;  /* Reset Off and status read clear */
		byteout(cs->hw.njet.base + NETJET_CTRL, cs->hw.njet.ctrl_reg);

		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout((10*HZ)/1000);	/* Timeout 10ms */

		cs->hw.njet.auxd = 0xC0;
		cs->hw.njet.dmactrl = 0;

		byteout(cs->hw.njet.auxa, 0);
		byteout(cs->hw.njet.base + NETJET_AUXCTRL, ~NETJET_ISACIRQ);
		byteout(cs->hw.njet.base + NETJET_IRQMASK1, NETJET_ISACIRQ);
		byteout(cs->hw.njet.auxa, cs->hw.njet.auxd);

		switch ( ( ( NETjet_ReadIC( cs, ICC_RBCH ) >> 5 ) & 3 ) )
		{
			case 3 :
				break;

			case 0 :
				printk( KERN_WARNING "NETspider-U: NETjet-S PCI card found\n" );
				continue;

			default :
				printk( KERN_WARNING "NETspider-U: No PCI card found\n" );
				return 0;
                }
                break;
	}
#else

	printk(KERN_WARNING "NETspider-U: NO_PCI_BIOS\n");
	printk(KERN_WARNING "NETspider-U: unable to config NETspider-U PCI\n");
	return (0);

#endif /* CONFIG_PCI */

	printk(KERN_INFO
		"NETspider-U: PCI card configured at %#lx IRQ %d\n",
		cs->hw.njet.base, cs->irq);
	if (!request_io(&cs->rs, cs->hw.njet.base, 0x100, "netjet-s isdn"))
		return 0;
	
	nj_u_reset(cs);
	cs->dc_hw_ops = &netjet_dc_ops;
	cs->cardmsg = &NETjet_U_card_msg;
	cs->irq_flags |= SA_SHIRQ;
	cs->card_ops = &nj_u_ops;
	ICCVersion(cs, "NETspider-U:");
	return 1;
}
