/* $Id: asuscom.c,v 1.11.6.3 2001/09/23 22:24:46 kai Exp $
 *
 * low level stuff for ASUSCOM NETWORK INC. ISDNLink cards
 *
 * Author       Karsten Keil
 * Copyright    by Karsten Keil      <keil@isdn4linux.de>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * Thanks to  ASUSCOM NETWORK INC. Taiwan and  Dynalink NL for information
 *
 */

#include <linux/init.h>
#include <linux/isapnp.h>
#include "hisax.h"
#include "isac.h"
#include "ipac.h"
#include "hscx.h"
#include "isdnl1.h"

extern const char *CardType[];

const char *Asuscom_revision = "$Revision: 1.11.6.3 $";

static spinlock_t asuscom_lock = SPIN_LOCK_UNLOCKED;
#define byteout(addr,val) outb(val,addr)
#define bytein(addr) inb(addr)

#define ASUS_ISAC	0
#define ASUS_HSCX	1
#define ASUS_ADR	2
#define ASUS_CTRL_U7	3
#define ASUS_CTRL_POTS	5

#define ASUS_IPAC_ALE	0
#define ASUS_IPAC_DATA	1

#define ASUS_ISACHSCX	1
#define ASUS_IPAC	2

/* CARD_ADR (Write) */
#define ASUS_RESET      0x80	/* Bit 7 Reset-Leitung */

static inline u8
readreg(struct IsdnCardState *cs, unsigned int adr, u8 off)
{
	u8 ret;
	unsigned long flags;

	spin_lock_irqsave(&asuscom_lock, flags);
	byteout(cs->hw.asus.adr, off);
	ret = bytein(adr);
	spin_unlock_irqrestore(&asuscom_lock, flags);
	return ret;
}

static inline void
writereg(struct IsdnCardState *cs, unsigned int adr, u8 off, u8 data)
{
	unsigned long flags;

	spin_lock_irqsave(&asuscom_lock, flags);
	byteout(cs->hw.asus.adr, off);
	byteout(adr, data);
	spin_unlock_irqrestore(&asuscom_lock, flags);
}

static inline void
readfifo(struct IsdnCardState *cs, unsigned int adr, u8 off, u8 * data, int size)
{
	byteout(cs->hw.asus.adr, off);
	insb(adr, data, size);
}


static inline void
writefifo(struct IsdnCardState *cs, unsigned int adr, u8 off, u8 * data, int size)
{
	byteout(cs->hw.asus.adr, off);
	outsb(adr, data, size);
}

static u8
isac_read(struct IsdnCardState *cs, u8 offset)
{
	return readreg(cs, cs->hw.asus.isac, offset);
}

static void
isac_write(struct IsdnCardState *cs, u8 offset, u8 value)
{
	writereg(cs, cs->hw.asus.isac, offset, value);
}

static void
isac_read_fifo(struct IsdnCardState *cs, u8 * data, int size)
{
	readfifo(cs, cs->hw.asus.isac, 0, data, size);
}

static void
isac_write_fifo(struct IsdnCardState *cs, u8 * data, int size)
{
	writefifo(cs, cs->hw.asus.isac, 0, data, size);
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
	return readreg(cs, cs->hw.asus.hscx, offset + (hscx ? 0x40 : 0));
}

static void
hscx_write(struct IsdnCardState *cs, int hscx, u8 offset, u8 value)
{
	writereg(cs, cs->hw.asus.hscx, offset + (hscx ? 0x40 : 0), value);
}

static void
hscx_read_fifo(struct IsdnCardState *cs, int hscx, u8 *data, int size)
{
	readfifo(cs, cs->hw.asus.hscx, hscx ? 0x40 : 0, data, size);
}

static void
hscx_write_fifo(struct IsdnCardState *cs, int hscx, u8 *data, int size)
{
	writefifo(cs, cs->hw.asus.hscx, hscx ? 0x40 : 0, data, size);
}

static struct bc_hw_ops hscx_ops = {
	.read_reg   = hscx_read,
	.write_reg  = hscx_write,
	.read_fifo  = hscx_read_fifo,
	.write_fifo = hscx_write_fifo,
};

static inline u8
ipac_read(struct IsdnCardState *cs, u8 off)
{
	u8 ret;
	unsigned long flags;

	spin_lock_irqsave(&asuscom_lock, flags);
	byteout(cs->hw.asus.adr, off);
	ret = bytein(cs->hw.asus.isac);
	spin_unlock_irqrestore(&asuscom_lock, flags);
	return ret;
}

static inline void
ipac_write(struct IsdnCardState *cs, u8 off, u8 data)
{
	unsigned long flags;

	spin_lock_irqsave(&asuscom_lock, flags);
	byteout(cs->hw.asus.adr, off);
	byteout(cs->hw.asus.isac, data);
	spin_unlock_irqrestore(&asuscom_lock, flags);
}

static inline void
ipac_readfifo(struct IsdnCardState *cs, u8 off, u8 * data, int size)
{
	byteout(cs->hw.asus.adr, off);
	insb(cs->hw.asus.isac, data, size);
}


static inline void
ipac_writefifo(struct IsdnCardState *cs, u8 off, u8 * data, int size)
{
	byteout(cs->hw.asus.adr, off);
	outsb(cs->hw.asus.isac, data, size);
}

/* This will generate ipac_dc_ops and ipac_bc_ops using the functions
 * above */

BUILD_IPAC_OPS(ipac);

static int
asuscom_reset(struct IsdnCardState *cs)
{
	byteout(cs->hw.asus.adr, ASUS_RESET);	/* Reset On */
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout((10*HZ)/1000);
	byteout(cs->hw.asus.adr, 0);	/* Reset Off */
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout((10*HZ)/1000);
	return 0;
}

static int
asuscom_ipac_reset(struct IsdnCardState *cs)
{
	writereg(cs, cs->hw.asus.isac, IPAC_POTA2, 0x20);
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout((10*HZ)/1000);
	writereg(cs, cs->hw.asus.isac, IPAC_POTA2, 0x0);
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout((10*HZ)/1000);
	writereg(cs, cs->hw.asus.isac, IPAC_CONF, 0x0);
	writereg(cs, cs->hw.asus.isac, IPAC_ACFG, 0xff);
	writereg(cs, cs->hw.asus.isac, IPAC_AOE, 0x0);
	writereg(cs, cs->hw.asus.isac, IPAC_MASK, 0xc0);
	writereg(cs, cs->hw.asus.isac, IPAC_PCFG, 0x12);
	return 0;
}

static struct card_ops asuscom_ops = {
	.init     = inithscxisac,
	.reset    = asuscom_reset,
	.release  = hisax_release_resources,
	.irq_func = hscxisac_irq,
};

static struct card_ops asuscom_ipac_ops = {
	.init     = ipac_init,
	.reset    = asuscom_ipac_reset,
	.release  = hisax_release_resources,
	.irq_func = ipac_irq,
};

static int __init
asuscom_probe(struct IsdnCardState *cs, struct IsdnCard *card)
{
	int rc;
	u8 val;

	printk(KERN_INFO "ISDNLink: defined at %#lx IRQ %lu\n",
	       card->para[1], card->para[0]);

	cs->hw.asus.cfg_reg = card->para[1];
	cs->irq = card->para[0];

	rc = -EBUSY;
	if (!request_io(&cs->rs, cs->hw.asus.cfg_reg, 8, "asuscom isdn"))
		goto err;

	rc = -ENODEV;
	cs->hw.asus.adr = cs->hw.asus.cfg_reg + ASUS_IPAC_ALE;
	val = readreg(cs, cs->hw.asus.cfg_reg + ASUS_IPAC_DATA, IPAC_ID);
	if ((val == 1) || (val == 2)) {
		cs->subtyp = ASUS_IPAC;
		cs->card_ops = &asuscom_ipac_ops;
		cs->hw.asus.isac = cs->hw.asus.cfg_reg + ASUS_IPAC_DATA;
		if (ipac_setup(cs, &ipac_dc_ops, &ipac_bc_ops))
			goto err;
	} else {
		cs->subtyp = ASUS_ISACHSCX;
		cs->card_ops = &asuscom_ops;
		cs->hw.asus.adr = cs->hw.asus.cfg_reg + ASUS_ADR;
		cs->hw.asus.isac = cs->hw.asus.cfg_reg + ASUS_ISAC;
		cs->hw.asus.hscx = cs->hw.asus.cfg_reg + ASUS_HSCX;
		cs->hw.asus.u7 = cs->hw.asus.cfg_reg + ASUS_CTRL_U7;
		cs->hw.asus.pots = cs->hw.asus.cfg_reg + ASUS_CTRL_POTS;
		if (hscxisac_setup(cs, &isac_ops, &hscx_ops))
			goto err;
	}
	printk(KERN_INFO "ISDNLink: resetting card\n");
	cs->card_ops->reset(cs);
	return 0;

 err:
	hisax_release_resources(cs);
	return rc;
}

#ifdef __ISAPNP__
static struct isapnp_device_id asus_ids[] __initdata = {
	{ ISAPNP_VENDOR('A', 'S', 'U'), ISAPNP_FUNCTION(0x1688),
	  ISAPNP_VENDOR('A', 'S', 'U'), ISAPNP_FUNCTION(0x1688), 
	  (unsigned long) "Asus1688 PnP" },
	{ ISAPNP_VENDOR('A', 'S', 'U'), ISAPNP_FUNCTION(0x1690),
	  ISAPNP_VENDOR('A', 'S', 'U'), ISAPNP_FUNCTION(0x1690), 
	  (unsigned long) "Asus1690 PnP" },
	{ ISAPNP_VENDOR('S', 'I', 'E'), ISAPNP_FUNCTION(0x0020),
	  ISAPNP_VENDOR('S', 'I', 'E'), ISAPNP_FUNCTION(0x0020), 
	  (unsigned long) "Isurf2 PnP" },
	{ ISAPNP_VENDOR('E', 'L', 'F'), ISAPNP_FUNCTION(0x0000),
	  ISAPNP_VENDOR('E', 'L', 'F'), ISAPNP_FUNCTION(0x0000), 
	  (unsigned long) "Iscas TE320" },
	{ 0, }
};

static struct isapnp_device_id *adev = &asus_ids[0];
static struct pnp_card *pnp_c __devinitdata = NULL;
#endif

int __init
setup_asuscom(struct IsdnCard *card)
{
	char tmp[64];

	strcpy(tmp, Asuscom_revision);
	printk(KERN_INFO "HiSax: Asuscom ISDNLink driver Rev. %s\n", HiSax_getrev(tmp));
#ifdef __ISAPNP__
	if (!card->para[1] && isapnp_present()) {
		struct pnp_card *pb;
		struct pnp_dev *pd;

		while(adev->card_vendor) {
			if ((pb = pnp_find_card(adev->card_vendor,
						adev->card_device,
						pnp_c))) {
				pnp_c = pb;
				pd = NULL;
				if ((pd = pnp_find_dev(pnp_c,
						       adev->vendor,
						       adev->function,
						       pd))) {
					printk(KERN_INFO "HiSax: %s detected\n",
						(char *)adev->driver_data);
					if (pnp_device_attach(pd) < 0) {
						printk(KERN_ERR "AsusPnP: attach failed\n");
						return 0;
					}
					if (pnp_activate_dev(pd) < 0) {
						printk(KERN_ERR "AsusPnP: activate failed\n");
						pnp_device_detach(pd);
						return 0;
					}
					if (!pnp_irq_valid(pd, 0) || !pnp_port_valid(pd, 0)) {
						printk(KERN_ERR "AsusPnP:some resources are missing %ld/%lx\n",
							pnp_irq(pd, 0), pnp_port_start(pd, 0));
						pnp_device_detach(pd);
						return(0);
					}
					card->para[1] = pnp_port_start(pd, 0);
					card->para[0] = pnp_irq(pd, 0);
					break;
				} else {
					printk(KERN_ERR "AsusPnP: PnP error card found, no device\n");
				}
			}
			adev++;
			pnp_c=NULL;
		} 
		if (!adev->card_vendor) {
			printk(KERN_INFO "AsusPnP: no ISAPnP card found\n");
			return(0);
		}
	}
#endif
	if (asuscom_probe(card->cs, card) < 0)
		return 0;
	return 1;
}
