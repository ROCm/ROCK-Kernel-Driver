/* $Id: hfcscard.c,v 1.8.6.2 2001/09/23 22:24:48 kai Exp $
 *
 * low level stuff for hfcs based cards (Teles3c, ACER P10)
 *
 * Author       Karsten Keil
 * Copyright    by Karsten Keil      <keil@isdn4linux.de>
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include <linux/init.h>
#include <linux/isapnp.h>
#include "hisax.h"
#include "hfc_2bds0.h"
#include "isdnl1.h"

extern const char *CardType[];

static const char *hfcs_revision = "$Revision: 1.8.6.2 $";

static inline u8
hfcs_read_reg(struct IsdnCardState *cs, int data, u8 reg)
{
	return cs->bc_hw_ops->read_reg(cs, data, reg);
}

static inline void
hfcs_write_reg(struct IsdnCardState *cs, int data, u8 reg, u8 val)
{
	cs->bc_hw_ops->write_reg(cs, data, reg, val);
}

static irqreturn_t
hfcs_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
	struct IsdnCardState *cs = dev_id;
	u8 val, stat;

	if (!cs) {
		printk(KERN_WARNING "HFCS: Spurious interrupt!\n");
		return IRQ_NONE;
	}
	if ((HFCD_ANYINT | HFCD_BUSY_NBUSY) & 
		(stat = hfcs_read_reg(cs, HFCD_DATA, HFCD_STAT))) {
		val = hfcs_read_reg(cs, HFCD_DATA, HFCD_INT_S1);
		if (cs->debug & L1_DEB_ISAC)
			debugl1(cs, "HFCS: stat(%02x) s1(%02x)", stat, val);
		hfc2bds0_interrupt(cs, val);
	} else {
		if (cs->debug & L1_DEB_ISAC)
			debugl1(cs, "HFCS: irq_no_irq stat(%02x)", stat);
	}
	return IRQ_HANDLED;
}

static void
hfcs_Timer(struct IsdnCardState *cs)
{
	cs->hw.hfcD.timer.expires = jiffies + 75;
	/* WD RESET */
/*	WriteReg(cs, HFCD_DATA, HFCD_CTMT, cs->hw.hfcD.ctmt | 0x80);
	add_timer(&cs->hw.hfcD.timer);
*/
}

static void
hfcs_release(struct IsdnCardState *cs)
{
	release2bds0(cs);
	del_timer(&cs->hw.hfcD.timer);
	hisax_release_resources(cs);
}

static int
hfcs_reset(struct IsdnCardState *cs)
{
	printk(KERN_INFO "HFCS: resetting card\n");
	cs->hw.hfcD.cirm = HFCD_RESET;
	if (cs->typ == ISDN_CTYPE_TELES3C)
		cs->hw.hfcD.cirm |= HFCD_MEM8K;
	hfcs_write_reg(cs, HFCD_DATA, HFCD_CIRM, cs->hw.hfcD.cirm);	/* Reset On */
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout((30*HZ)/1000);
	cs->hw.hfcD.cirm = 0;
	if (cs->typ == ISDN_CTYPE_TELES3C)
		cs->hw.hfcD.cirm |= HFCD_MEM8K;
	hfcs_write_reg(cs, HFCD_DATA, HFCD_CIRM, cs->hw.hfcD.cirm);	/* Reset Off */
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout((10*HZ)/1000);
	if (cs->typ == ISDN_CTYPE_TELES3C)
		cs->hw.hfcD.cirm |= HFCD_INTB;
	else if (cs->typ == ISDN_CTYPE_ACERP10)
		cs->hw.hfcD.cirm |= HFCD_INTA;
	hfcs_write_reg(cs, HFCD_DATA, HFCD_CIRM, cs->hw.hfcD.cirm);
	hfcs_write_reg(cs, HFCD_DATA, HFCD_CLKDEL, 0x0e);
	hfcs_write_reg(cs, HFCD_DATA, HFCD_TEST, HFCD_AUTO_AWAKE); /* S/T Auto awake */
	cs->hw.hfcD.ctmt = HFCD_TIM25 | HFCD_AUTO_TIMER;
	hfcs_write_reg(cs, HFCD_DATA, HFCD_CTMT, cs->hw.hfcD.ctmt);
	cs->hw.hfcD.int_m2 = HFCD_IRQ_ENABLE;
	cs->hw.hfcD.int_m1 = HFCD_INTS_B1TRANS | HFCD_INTS_B2TRANS |
		HFCD_INTS_DTRANS | HFCD_INTS_B1REC | HFCD_INTS_B2REC |
		HFCD_INTS_DREC | HFCD_INTS_L1STATE;
	hfcs_write_reg(cs, HFCD_DATA, HFCD_INT_M1, cs->hw.hfcD.int_m1);
	hfcs_write_reg(cs, HFCD_DATA, HFCD_INT_M2, cs->hw.hfcD.int_m2);
	hfcs_write_reg(cs, HFCD_DATA, HFCD_STATES, HFCD_LOAD_STATE | 2); /* HFC ST 2 */
	udelay(10);
	hfcs_write_reg(cs, HFCD_DATA, HFCD_STATES, 2); /* HFC ST 2 */
	cs->hw.hfcD.mst_m = HFCD_MASTER;
	hfcs_write_reg(cs, HFCD_DATA, HFCD_MST_MODE, cs->hw.hfcD.mst_m); /* HFC Master */
	cs->hw.hfcD.sctrl = 0;
	hfcs_write_reg(cs, HFCD_DATA, HFCD_SCTRL, cs->hw.hfcD.sctrl);
	return 0;
}

static void
hfcs_init(struct IsdnCardState *cs)
{
	cs->hw.hfcD.timer.expires = jiffies + 75;
	add_timer(&cs->hw.hfcD.timer);
	init2bds0(cs);
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout((80*HZ)/1000);
	cs->hw.hfcD.ctmt |= HFCD_TIM800;
	hfcs_write_reg(cs, HFCD_DATA, HFCD_CTMT, cs->hw.hfcD.ctmt); 
	hfcs_write_reg(cs, HFCD_DATA, HFCD_MST_MODE, cs->hw.hfcD.mst_m);
}

static struct card_ops hfcs_ops = {
	.init     = hfcs_init,
	.reset    = hfcs_reset,
	.release  = hfcs_release,
	.irq_func = hfcs_interrupt,
};

static int __init
hfcs_probe(struct IsdnCardState *cs, struct IsdnCard *card)
{
	cs->irq = card->para[0];
	cs->hw.hfcD.addr = card->para[1];

	if (!request_io(&cs->rs, cs->hw.hfcD.addr, 2, "HFCS isdn"))
		goto err;

	printk(KERN_INFO "HFCS: defined at 0x%x IRQ %d\n",
	       cs->hw.hfcD.addr, cs->irq);

	cs->hw.hfcD.cip = 0;
	cs->hw.hfcD.int_s1 = 0;
	cs->hw.hfcD.send = NULL;
	cs->bcs[0].hw.hfc.send = NULL;
	cs->bcs[1].hw.hfc.send = NULL;
	cs->hw.hfcD.dfifosize = 512;
	cs->dc.hfcd.ph_state = 0;
	cs->hw.hfcD.fifo = 255;

	if (cs->typ == ISDN_CTYPE_TELES3C) {
		cs->hw.hfcD.bfifosize = 1024 + 512;
		/* Teles 16.3c IO ADR is 0x200 | YY0U (YY Bit 15/14 address) */
		outb(0x00, cs->hw.hfcD.addr);
		outb(0x56, cs->hw.hfcD.addr | 1);
	} else if (cs->typ == ISDN_CTYPE_ACERP10) {
		cs->hw.hfcD.bfifosize = 7*1024 + 512;
		/* Acer P10 IO ADR is 0x300 */
		outb(0x00, cs->hw.hfcD.addr);
		outb(0x57, cs->hw.hfcD.addr | 1);
	}
	set_cs_func(cs);
	init_timer(&cs->hw.hfcD.timer);
	cs->hw.hfcD.timer.function = (void *) hfcs_Timer;
	cs->hw.hfcD.timer.data = (long) cs;
	hfcs_reset(cs);
	cs->card_ops = &hfcs_ops;
	return 0;
 err:
	hisax_release_resources(cs);
	return -EBUSY;
}

#ifdef __ISAPNP__
static struct isapnp_device_id hfc_ids[] __initdata = {
	{ ISAPNP_VENDOR('A', 'N', 'X'), ISAPNP_FUNCTION(0x1114),
	  ISAPNP_VENDOR('A', 'N', 'X'), ISAPNP_FUNCTION(0x1114), 
	  (unsigned long) "Acer P10" },
	{ ISAPNP_VENDOR('B', 'I', 'L'), ISAPNP_FUNCTION(0x0002),
	  ISAPNP_VENDOR('B', 'I', 'L'), ISAPNP_FUNCTION(0x0002), 
	  (unsigned long) "Billion 2" },
	{ ISAPNP_VENDOR('B', 'I', 'L'), ISAPNP_FUNCTION(0x0001),
	  ISAPNP_VENDOR('B', 'I', 'L'), ISAPNP_FUNCTION(0x0001), 
	  (unsigned long) "Billion 1" },
	{ ISAPNP_VENDOR('T', 'A', 'G'), ISAPNP_FUNCTION(0x7410),
	  ISAPNP_VENDOR('T', 'A', 'G'), ISAPNP_FUNCTION(0x7410), 
	  (unsigned long) "IStar PnP" },
	{ ISAPNP_VENDOR('T', 'A', 'G'), ISAPNP_FUNCTION(0x2610),
	  ISAPNP_VENDOR('T', 'A', 'G'), ISAPNP_FUNCTION(0x2610), 
	  (unsigned long) "Teles 16.3c" },
	{ ISAPNP_VENDOR('S', 'F', 'M'), ISAPNP_FUNCTION(0x0001),
	  ISAPNP_VENDOR('S', 'F', 'M'), ISAPNP_FUNCTION(0x0001), 
	  (unsigned long) "Tornado Tipa C" },
	{ ISAPNP_VENDOR('K', 'Y', 'E'), ISAPNP_FUNCTION(0x0001),
	  ISAPNP_VENDOR('K', 'Y', 'E'), ISAPNP_FUNCTION(0x0001), 
	  (unsigned long) "Genius Speed Surfer" },
	{ 0, }
};

static struct isapnp_device_id *hdev = &hfc_ids[0];
static struct pnp_card *pnp_c __devinitdata = NULL;
#endif

int __init
setup_hfcs(struct IsdnCard *card)
{
	char tmp[64];

	strcpy(tmp, hfcs_revision);
	printk(KERN_INFO "HiSax: HFC-S driver Rev. %s\n", HiSax_getrev(tmp));

#ifdef __ISAPNP__
	if (!card->para[1] && isapnp_present()) {
		struct pnp_card *pb;
		struct pnp_dev *pd;

		while(hdev->card_vendor) {
			if ((pb = pnp_find_card(hdev->card_vendor,
						hdev->card_device,
						pnp_c))) {
				pnp_c = pb;
				pd = NULL;
				if ((pd = pnp_find_dev(pnp_c,
						       hdev->vendor,
						       hdev->function,
						       pd))) {
					printk(KERN_INFO "HiSax: %s detected\n",
						(char *)hdev->driver_data);
					if (pnp_device_attach(pd) < 0) {
						printk(KERN_ERR "HFC PnP: attach failed\n");
						return 0;
					}
					if (pnp_activate_dev(pd) < 0) {
						printk(KERN_ERR "HFC PnP: activate failed\n");
						pnp_device_detach(pd);
						return 0;
					}
					if (!pnp_irq_valid(pd, 0) || !pnp_port_valid(pd, 0)) {
						printk(KERN_ERR "HFC PnP:some resources are missing %ld/%lx\n",
							pnp_irq(pd, 0), pnp_port_start(pd, 0));
						pnp_device_detach(pd);
						return(0);
					}
					card->para[1] = pnp_port_start(pd, 0);
					card->para[0] = pnp_irq(pd, 0);
					break;
				} else {
					printk(KERN_ERR "HFC PnP: PnP error card found, no device\n");
				}
			}
			hdev++;
			pnp_c=NULL;
		} 
		if (!hdev->card_vendor) {
			printk(KERN_INFO "HFC PnP: no ISAPnP card found\n");
			return(0);
		}
	}
#endif
	if (hfcs_probe(card->cs, card) < 0)
		return 0;
	return 1;
	
}
