/* $Id: isurf.c,v 1.10.6.2 2001/09/23 22:24:49 kai Exp $
 *
 * low level stuff for Siemens I-Surf/I-Talk cards
 *
 * Author       Karsten Keil
 * Copyright    by Karsten Keil      <keil@isdn4linux.de>
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include <linux/init.h>
#include "hisax.h"
#include "isac.h"
#include "isar.h"
#include "isdnl1.h"
#include <linux/isapnp.h>

extern const char *CardType[];

static const char *ISurf_revision = "$Revision: 1.10.6.2 $";

#define byteout(addr,val) outb(val,addr)
#define bytein(addr) inb(addr)

#define ISURF_ISAR_RESET	1
#define ISURF_ISAC_RESET	2
#define ISURF_ISAR_EA		4
#define ISURF_ARCOFI_RESET	8
#define ISURF_RESET (ISURF_ISAR_RESET | ISURF_ISAC_RESET | ISURF_ARCOFI_RESET)

#define ISURF_ISAR_OFFSET	0
#define ISURF_ISAC_OFFSET	0x100
#define ISURF_IOMEM_SIZE	0x400
/* Interface functions */

static u8
ReadISAC(struct IsdnCardState *cs, u8 offset)
{
	return (readb(cs->hw.isurf.isac + offset));
}

static void
WriteISAC(struct IsdnCardState *cs, u8 offset, u8 value)
{
	writeb(value, cs->hw.isurf.isac + offset); mb();
}

static void
ReadISACfifo(struct IsdnCardState *cs, u8 * data, int size)
{
	register int i;
	for (i = 0; i < size; i++)
		data[i] = readb(cs->hw.isurf.isac);
}

static void
WriteISACfifo(struct IsdnCardState *cs, u8 * data, int size)
{
	register int i;
	for (i = 0; i < size; i++){
		writeb(data[i], cs->hw.isurf.isac);mb();
	}
}

static struct dc_hw_ops isac_ops = {
	.read_reg   = ReadISAC,
	.write_reg  = WriteISAC,
	.read_fifo  = ReadISACfifo,
	.write_fifo = WriteISACfifo,
};

/* ISAR access routines
 * mode = 0 access with IRQ on
 * mode = 1 access with IRQ off
 * mode = 2 access with IRQ off and using last offset
 */
  
static u8
ReadISAR(struct IsdnCardState *cs, int mode, u8 offset)
{	
	return(readb(cs->hw.isurf.isar + offset));
}

static void
WriteISAR(struct IsdnCardState *cs, int mode, u8 offset, u8 value)
{
	writeb(value, cs->hw.isurf.isar + offset);mb();
}

static struct bc_hw_ops isar_ops = {
	.read_reg  = ReadISAR,
	.write_reg = WriteISAR,
};

static irqreturn_t
isurf_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
	struct IsdnCardState *cs = dev_id;
	u8 val;
	int cnt = 5;

	spin_lock(&cs->lock);
	val = readb(cs->hw.isurf.isar + ISAR_IRQBIT);
      Start_ISAR:
	if (val & ISAR_IRQSTA)
		isar_int_main(cs);
	val = readb(cs->hw.isurf.isac + ISAC_ISTA);
      Start_ISAC:
	if (val)
		isac_interrupt(cs, val);
	val = readb(cs->hw.isurf.isar + ISAR_IRQBIT);
	if ((val & ISAR_IRQSTA) && --cnt) {
		if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "ISAR IntStat after IntRoutine");
		goto Start_ISAR;
	}
	val = readb(cs->hw.isurf.isac + ISAC_ISTA);
	if (val && --cnt) {
		if (cs->debug & L1_DEB_ISAC)
			debugl1(cs, "ISAC IntStat after IntRoutine");
		goto Start_ISAC;
	}
	if (!cnt)
		printk(KERN_WARNING "ISurf IRQ LOOP\n");

	writeb(0, cs->hw.isurf.isar + ISAR_IRQBIT); mb();
	writeb(0xFF, cs->hw.isurf.isac + ISAC_MASK);mb();
	writeb(0, cs->hw.isurf.isac + ISAC_MASK);mb();
	writeb(ISAR_IRQMSK, cs->hw.isurf.isar + ISAR_IRQBIT); mb();
	spin_unlock(&cs->lock);
	return IRQ_HANDLED;
}

static void
reset_isurf(struct IsdnCardState *cs, u8 chips)
{
	printk(KERN_INFO "ISurf: resetting card\n");

	byteout(cs->hw.isurf.reset, chips); /* Reset On */
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout((10*HZ)/1000);
	byteout(cs->hw.isurf.reset, ISURF_ISAR_EA); /* Reset Off */
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout((10*HZ)/1000);
}

static int
isurf_auxcmd(struct IsdnCardState *cs, isdn_ctrl *ic) {
	int ret;

	if ((ic->command == ISDN_CMD_IOCTL) && (ic->arg == 9)) {
		ret = isar_auxcmd(cs, ic);
		if (!ret) {
			reset_isurf(cs, ISURF_ISAR_EA | ISURF_ISAC_RESET |
				ISURF_ARCOFI_RESET);
			initisac(cs);
		}
		return(ret);
	}
	return(isar_auxcmd(cs, ic));
}

static void
isurf_init(struct IsdnCardState *cs)
{
	writeb(0, cs->hw.isurf.isar + ISAR_IRQBIT);
	initisac(cs);
	initisar(cs);
}

static int
isurf_reset(struct IsdnCardState *cs)
{
	reset_isurf(cs, ISURF_RESET);
	return 0;
}

static struct card_ops isurf_ops = {
	.init     = isurf_init,
	.reset    = isurf_reset,
	.release  = hisax_release_resources,
	.irq_func = isurf_interrupt,
};

#ifdef __ISAPNP__
static struct pnp_card *pnp_surf __devinitdata = NULL;
#endif

static int __init
isurf_probe(struct IsdnCardState *cs, struct IsdnCard *card)
{
	unsigned long phymem;

	phymem = card->para[2];
	cs->hw.isurf.reset = card->para[1];
	cs->irq = card->para[0];

	if (!request_io(&cs->rs, cs->hw.isurf.reset, 1, "isurf isdn"))
		goto err;

	cs->hw.isurf.isar = request_mmio(&cs->rs, phymem, ISURF_IOMEM_SIZE,
					 "isurf iomem");
	if (!cs->hw.isurf.isar)
		goto err;

	cs->hw.isurf.isac = cs->hw.isurf.isar + ISURF_ISAC_OFFSET;
	printk(KERN_INFO "ISurf: defined at 0x%x 0x%lx IRQ %d\n",
	       cs->hw.isurf.reset, phymem, cs->irq);

	cs->auxcmd = &isurf_auxcmd;
	cs->card_ops = &isurf_ops;
	cs->bcs[0].hw.isar.reg = &cs->hw.isurf.isar_r;
	cs->bcs[1].hw.isar.reg = &cs->hw.isurf.isar_r;
	reset_isurf(cs, ISURF_RESET);
	__set_bit(HW_ISAR, &cs->HW_Flags);
	isac_setup(cs, &isac_ops);
	if (isar_setup(cs, &isar_ops))
		goto err;
	return 0;
 err:
	hisax_release_resources(cs);
	return -EBUSY;
}

int __init
setup_isurf(struct IsdnCard *card)
{
	char tmp[64];

	strcpy(tmp, ISurf_revision);
	printk(KERN_INFO "HiSax: ISurf driver Rev. %s\n", HiSax_getrev(tmp));
	
#ifdef __ISAPNP__
	if (!card->para[1] || !card->para[2]) {
		struct pnp_card *pb;
		struct pnp_dev *pd;
	
		card->cs->subtyp = 0;
		if ((pb = pnp_find_card(
			     ISAPNP_VENDOR('S', 'I', 'E'),
			     ISAPNP_FUNCTION(0x0010), pnp_surf))) {
			pnp_surf = pb;
			pd = NULL;
			if (!(pd = pnp_find_dev(pnp_surf,
						ISAPNP_VENDOR('S', 'I', 'E'),
						ISAPNP_FUNCTION(0x0010), pd))) {
				printk(KERN_ERR "ISurfPnP: PnP error card found, no device\n");
				return (0);
			}
			if (pnp_device_attach(pd) < 0) {
				printk(KERN_ERR "ISurfPnP: attach failed\n");
				return 0;
			}
			if (pnp_activate_dev(pd) < 0) {
				printk(KERN_ERR "ISurfPnP: activate failed\n");
				pnp_device_detach(pd);
				return 0;
			}
			if (!pnp_irq_valid(pd, 0) || !pnp_port_valid(pd, 0) || !pnp_port_valid(pd, 1)) {
				printk(KERN_ERR "ISurfPnP:some resources are missing %ld/%lx/%lx\n",
				       pnp_irq(pd, 0), pnp_port_start(pd, 0), pnp_port_start(pd, 1));
				pnp_device_detach(pd);
				return(0);
			}
			card->para[1] = pnp_port_start(pd, 0);
			card->para[2] = pnp_port_start(pd, 1);
			card->para[0] = pnp_irq(pd, 0);
		} else {
			printk(KERN_INFO "ISurfPnP: no ISAPnP card found\n");
			return 0;
		}
	}
#endif
	if (isurf_probe(card->cs, card) < 0)
		return 0;
	return 1;
}
