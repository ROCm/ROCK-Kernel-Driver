/* $Id: ix1_micro.c,v 2.10.6.2 2001/09/23 22:24:49 kai Exp $
 *
 * low level stuff for ITK ix1-micro Rev.2 isdn cards
 * derived from the original file teles3.c from Karsten Keil
 *
 * Author       Klaus-Peter Nischke
 * Copyright    by Klaus-Peter Nischke, ITK AG
 *                                   <klaus@nischke.do.eunet.de>
 *              by Karsten Keil      <keil@isdn4linux.de>
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * Klaus-Peter Nischke
 * Deusener Str. 287
 * 44369 Dortmund
 * Germany
 */

#include <linux/init.h>
#include <linux/isapnp.h>
#include "hisax.h"
#include "isac.h"
#include "hscx.h"
#include "isdnl1.h"

extern const char *CardType[];
const char *ix1_revision = "$Revision: 2.10.6.2 $";
static spinlock_t ix1_micro_lock = SPIN_LOCK_UNLOCKED;

#define byteout(addr,val) outb(val,addr)
#define bytein(addr) inb(addr)

#define SPECIAL_PORT_OFFSET 3

#define ISAC_COMMAND_OFFSET 2
#define ISAC_DATA_OFFSET 0
#define HSCX_COMMAND_OFFSET 2
#define HSCX_DATA_OFFSET 1

#define TIMEOUT 50

static inline u8
readreg(struct IsdnCardState *cs, unsigned int adr, u8 off)
{
	u8 ret;
	unsigned long flags;

	spin_lock_irqsave(&ix1_micro_lock, flags);
	byteout(cs->hw.ix1.isac_ale, off);
	ret = bytein(adr);
	spin_unlock_irqrestore(&ix1_micro_lock, flags);
	return (ret);
}

static inline void
writereg(struct IsdnCardState *cs, unsigned int adr, u8 off, u8 data)
{
	unsigned long flags;

	spin_lock_irqsave(&ix1_micro_lock, flags);
	byteout(cs->hw.ix1.isac_ale, off);
	byteout(adr, data);
	spin_unlock_irqrestore(&ix1_micro_lock, flags);
}

static inline void
readfifo(struct IsdnCardState *cs, unsigned int adr, u8 off, u8 * data, int size)
{
	byteout(cs->hw.ix1.isac_ale, off);
	insb(adr, data, size);
}

static inline void
writefifo(struct IsdnCardState *cs, unsigned int adr, u8 off, u8 * data, int size)
{
	byteout(cs->hw.ix1.isac_ale, off);
	outsb(adr, data, size);
}

static u8
isac_read(struct IsdnCardState *cs, u8 offset)
{
	return readreg(cs, cs->hw.ix1.isac, offset);
}

static void
isac_write(struct IsdnCardState *cs, u8 offset, u8 value)
{
	writereg(cs, cs->hw.ix1.isac, offset, value);
}

static void
isac_read_fifo(struct IsdnCardState *cs, u8 * data, int size)
{
	readfifo(cs, cs->hw.ix1.isac, 0, data, size);
}

static void
isac_write_fifo(struct IsdnCardState *cs, u8 * data, int size)
{
	writefifo(cs, cs->hw.ix1.isac, 0, data, size);
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
	return readreg(cs, cs->hw.ix1.hscx, offset + (hscx ? 0x40 : 0));
}

static void
hscx_write(struct IsdnCardState *cs, int hscx, u8 offset, u8 value)
{
	writereg(cs, cs->hw.ix1.hscx, offset + (hscx ? 0x40 : 0), value);
}

static void
hscx_read_fifo(struct IsdnCardState *cs, int hscx, u8 *data, int size)
{
	readfifo(cs, cs->hw.ix1.hscx, hscx ? 0x40 : 0, data, size);
}

static void
hscx_write_fifo(struct IsdnCardState *cs, int hscx, u8 *data, int size)
{
	writefifo(cs, cs->hw.ix1.hscx, hscx ? 0x40 : 0, data, size);
}

static struct bc_hw_ops hscx_ops = {
	.read_reg   = hscx_read,
	.write_reg  = hscx_write,
	.read_fifo  = hscx_read_fifo,
	.write_fifo = hscx_write_fifo,
};

static int
ix1_reset(struct IsdnCardState *cs)
{
	int cnt;

	/* reset isac */
	cnt = 3 * (HZ / 10) + 1;
	while (cnt--) {
		byteout(cs->hw.ix1.cfg_reg + SPECIAL_PORT_OFFSET, 1);
		HZDELAY(1);	/* wait >=10 ms */
	}
	byteout(cs->hw.ix1.cfg_reg + SPECIAL_PORT_OFFSET, 0);
	return 0;
}

static struct card_ops ix1_ops = {
	.init     = inithscxisac,
	.reset    = ix1_reset,
	.release  = hisax_release_resources,
	.irq_func = hscxisac_irq,
};

static int __init
ix1_probe(struct IsdnCardState *cs, struct IsdnCard *card)
{
	cs->irq             = card->para[0];
	cs->hw.ix1.isac_ale = card->para[1] + ISAC_COMMAND_OFFSET;
	cs->hw.ix1.isac     = card->para[1] + ISAC_DATA_OFFSET;
	cs->hw.ix1.hscx     = card->para[1] + HSCX_DATA_OFFSET;
	cs->hw.ix1.cfg_reg  = card->para[1];
	if (!request_io(&cs->rs, cs->hw.ix1.cfg_reg, 4, "ix1micro cfg"))
		goto err;
	
	printk(KERN_INFO "HiSax: %s config irq:%d io:0x%X\n",
	       CardType[cs->typ], cs->irq, cs->hw.ix1.cfg_reg);
	ix1_reset(cs);
	cs->card_ops = &ix1_ops;
	if (hscxisac_setup(cs, &isac_ops, &hscx_ops))
		goto err;
	return 0;
 err:
	hisax_release_resources(cs);
	return -EBUSY;
}

#ifdef __ISAPNP__
static struct isapnp_device_id itk_ids[] __initdata = {
	{ ISAPNP_VENDOR('I', 'T', 'K'), ISAPNP_FUNCTION(0x25),
	  ISAPNP_VENDOR('I', 'T', 'K'), ISAPNP_FUNCTION(0x25), 
	  (unsigned long) "ITK micro 2" },
	{ ISAPNP_VENDOR('I', 'T', 'K'), ISAPNP_FUNCTION(0x29),
	  ISAPNP_VENDOR('I', 'T', 'K'), ISAPNP_FUNCTION(0x29), 
	  (unsigned long) "ITK micro 2." },
	{ 0, }
};

static struct isapnp_device_id *idev = &itk_ids[0];
static struct pnp_card *pnp_c __devinitdata = NULL;
#endif


int __init
setup_ix1micro(struct IsdnCard *card)
{
	char tmp[64];

	strcpy(tmp, ix1_revision);
	printk(KERN_INFO "HiSax: ITK IX1 driver Rev. %s\n", HiSax_getrev(tmp));

	if (card->para[1]) {
		if (ix1_probe(card->cs, card))
			return 0;
		return 1;
	}
#ifdef __ISAPNP__
	if (isapnp_present()) {
		struct pnp_card *pb;
		struct pnp_dev *pd;

		while(idev->card_vendor) {
			if ((pb = pnp_find_card(idev->card_vendor,
						idev->card_device,
						pnp_c))) {
				pnp_c = pb;
				pd = NULL;
				if ((pd = pnp_find_dev(pnp_c,
						       idev->vendor,
						       idev->function,
						       pd))) {
					printk(KERN_INFO "HiSax: %s detected\n",
						(char *)idev->driver_data);
					if (pnp_device_attach(pd) < 0) {
						printk(KERN_ERR "ITK PnP: attach failed\n");
						return 0;
					}
					if (pnp_activate_dev(pd) < 0) {
						printk(KERN_ERR "ITK PnP: activate failed\n");
						pnp_device_detach(pd);
						return 0;
					}
					if (!pnp_port_valid(pd, 0) || !pnp_irq_valid(pd, 0)) {
						printk(KERN_ERR "ITK PnP:some resources are missing %ld/%lx\n",
							pnp_irq(pd, 0), pnp_port_start(pd, 0));
						pnp_device_detach(pd);
						return(0);
					}
					card->para[1] = pnp_port_start(pd, 0);
					card->para[0] = pnp_irq(pd, 0);
					if (ix1_probe(card->cs, card))
						return 0;
					return 1;
				} else {
					printk(KERN_ERR "ITK PnP: PnP error card found, no device\n");
				}
			}
			idev++;
			pnp_c=NULL;
		} 
		if (!idev->card_vendor) {
			printk(KERN_INFO "ITK PnP: no ISAPnP card found\n");
		}
	}
#endif
	return 0;
}
