/* $Id: teles3.c,v 2.17.6.2 2001/09/23 22:24:52 kai Exp $
 *
 * low level stuff for Teles 16.3 & PNP isdn cards
 *
 * Author       Karsten Keil
 * Copyright    by Karsten Keil      <keil@isdn4linux.de>
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * Thanks to    Jan den Ouden
 *              Fritz Elfert
 *              Beat Doebeli
 *
 */
#include <linux/init.h>
#include <linux/isapnp.h>
#include "hisax.h"
#include "isac.h"
#include "hscx.h"
#include "isdnl1.h"

extern const char *CardType[];
const char *teles3_revision = "$Revision: 2.17.6.2 $";

#define byteout(addr,val) outb(val,addr)
#define bytein(addr) inb(addr)

static inline u8
readreg(unsigned int adr, u8 off)
{
	return (bytein(adr + off));
}

static inline void
writereg(unsigned int adr, u8 off, u8 data)
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
isac_read(struct IsdnCardState *cs, u8 offset)
{
	return readreg(cs->hw.teles3.isac, offset);
}

static void
isac_write(struct IsdnCardState *cs, u8 offset, u8 value)
{
	writereg(cs->hw.teles3.isac, offset, value);
}

static void
isac_read_fifo(struct IsdnCardState *cs, u8 * data, int size)
{
	read_fifo(cs->hw.teles3.isacfifo, data, size);
}

static void
isac_write_fifo(struct IsdnCardState *cs, u8 * data, int size)
{
	write_fifo(cs->hw.teles3.isacfifo, data, size);
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
	return readreg(cs->hw.teles3.hscx[hscx], offset);
}

static void
hscx_write(struct IsdnCardState *cs, int hscx, u8 offset, u8 value)
{
	writereg(cs->hw.teles3.hscx[hscx], offset, value);
}

static void
hscx_read_fifo(struct IsdnCardState *cs, int hscx, u8 *data, int size)
{
	read_fifo(cs->hw.teles3.hscxfifo[hscx], data, size);
}

static void
hscx_write_fifo(struct IsdnCardState *cs, int hscx, u8 *data, int size)
{
	write_fifo(cs->hw.teles3.hscxfifo[hscx], data, size);
}

static struct bc_hw_ops hscx_ops = {
	.read_reg   = hscx_read,
	.write_reg  = hscx_write,
	.read_fifo  = hscx_read_fifo,
	.write_fifo = hscx_write_fifo,
};

static int
teles3_reset(struct IsdnCardState *cs)
{
	u8 irqcfg;

	if (cs->typ != ISDN_CTYPE_TELESPCMCIA) {
		if ((cs->hw.teles3.cfg_reg) && (cs->typ != ISDN_CTYPE_COMPAQ_ISA)) {
			switch (cs->irq) {
				case 2:
				case 9:
					irqcfg = 0x00;
					break;
				case 3:
					irqcfg = 0x02;
					break;
				case 4:
					irqcfg = 0x04;
					break;
				case 5:
					irqcfg = 0x06;
					break;
				case 10:
					irqcfg = 0x08;
					break;
				case 11:
					irqcfg = 0x0A;
					break;
				case 12:
					irqcfg = 0x0C;
					break;
				case 15:
					irqcfg = 0x0E;
					break;
				default:
					return(1);
			}
			byteout(cs->hw.teles3.cfg_reg + 4, irqcfg);
			HZDELAY(HZ / 10 + 1);
			byteout(cs->hw.teles3.cfg_reg + 4, irqcfg | 1);
			HZDELAY(HZ / 10 + 1);
		} else if (cs->typ == ISDN_CTYPE_COMPAQ_ISA) {
			byteout(cs->hw.teles3.cfg_reg, 0xff);
			HZDELAY(2);
			byteout(cs->hw.teles3.cfg_reg, 0x00);
			HZDELAY(2);
		} else {
			/* Reset off for 16.3 PnP , thanks to Georg Acher */
			byteout(cs->hw.teles3.isac + 0x3c, 0);
			HZDELAY(2);
			byteout(cs->hw.teles3.isac + 0x3c, 1);
			HZDELAY(2);
		}
	}
	return(0);
}

static struct card_ops teles3_ops = {
	.init     = inithscxisac,
	.reset    = teles3_reset,
	.release  = hisax_release_resources,
	.irq_func = hscxisac_irq,
};

static int
teles_hw_init(struct IsdnCardState *cs)
{
	
	printk(KERN_INFO "HiSax: %s config irq:%d isac:0x%X  cfg:0x%X\n",
	       CardType[cs->typ], cs->irq,
	       cs->hw.teles3.isac + 32, cs->hw.teles3.cfg_reg);
	printk(KERN_INFO "HiSax: hscx A:0x%X  hscx B:0x%X\n",
	       cs->hw.teles3.hscx[0] + 32, cs->hw.teles3.hscx[1] + 32);

	if (teles3_reset(cs)) {
		printk(KERN_WARNING "Teles3: wrong IRQ\n");
		return -EBUSY;
	}
	cs->card_ops = &teles3_ops;
	if (hscxisac_setup(cs, &isac_ops, &hscx_ops))
		return -EBUSY;
	return 0;
}

static void __init
teles_setup_io(struct IsdnCardState *cs, struct IsdnCard *card)
{
	cs->irq = card->para[0];
	cs->hw.teles3.isacfifo = cs->hw.teles3.isac + 0x3e;
	cs->hw.teles3.hscxfifo[0] = cs->hw.teles3.hscx[0] + 0x3e;
	cs->hw.teles3.hscxfifo[1] = cs->hw.teles3.hscx[1] + 0x3e;
}

static int __init
telespcmcia_probe(struct IsdnCardState *cs, struct IsdnCard *card)
{
	cs->hw.teles3.cfg_reg = 0;
	cs->hw.teles3.hscx[0] = card->para[1] - 0x20;
	cs->hw.teles3.hscx[1] = card->para[1];
	cs->hw.teles3.isac = card->para[1] + 0x20;
	teles_setup_io(cs, card);
	if (!request_io(&cs->rs, cs->hw.teles3.hscx[1], 96, 
			"HiSax Teles PCMCIA"))
		goto err;
	if (teles_hw_init(cs) < 0)
		goto err;
	return 0;
 err:
	hisax_release_resources(cs);
	return -EBUSY;
}

static int __init
teles_request_io(struct IsdnCardState *cs)
{
	if (!request_io(&cs->rs, cs->hw.teles3.isac + 32, 32, "HiSax isac"))
		return -EBUSY;
	if (!request_io(&cs->rs, cs->hw.teles3.hscx[0]+32, 32, "HiSax hscx A"))
		return -EBUSY;
	if (!request_io(&cs->rs, cs->hw.teles3.hscx[1]+32, 32, "HiSax hscx B"))
		return -EBUSY;
	return 0;
}

static int __init
teles16_3_probe(struct IsdnCardState *cs, struct IsdnCard *card)
{
	u8 val;

	cs->hw.teles3.cfg_reg = card->para[1];
	switch (cs->hw.teles3.cfg_reg) {
	case 0x180:
	case 0x280:
	case 0x380:
		cs->hw.teles3.cfg_reg |= 0xc00;
		break;
	}
	cs->hw.teles3.isac = cs->hw.teles3.cfg_reg - 0x420;
	cs->hw.teles3.hscx[0] = cs->hw.teles3.cfg_reg - 0xc20;
	cs->hw.teles3.hscx[1] = cs->hw.teles3.cfg_reg - 0x820;
	teles_setup_io(cs, card);
	if (!request_io(&cs->rs, cs->hw.teles3.cfg_reg, 8, "teles3 cfg"))
		goto err;
	if (teles_request_io(cs) < 0)
		goto err;
	if ((val = bytein(cs->hw.teles3.cfg_reg + 0)) != 0x51) {
		printk(KERN_WARNING "Teles: 16.3 Byte at %x is %x\n",
		       cs->hw.teles3.cfg_reg + 0, val);
		goto err;
	}
	if ((val = bytein(cs->hw.teles3.cfg_reg + 1)) != 0x93) {
		printk(KERN_WARNING "Teles: 16.3 Byte at %x is %x\n",
		       cs->hw.teles3.cfg_reg + 1, val);
		goto err;
	}
	/* 0x1e without AB, 0x1f with AB,  0x1c 16.3 ???, 
	 * 0x39 16.3 1.1,   0x38 16.3 1.3, 0x46 16.3 with AB + Video */
	val = bytein(cs->hw.teles3.cfg_reg + 2);
	if (val != 0x46 && val != 0x39 && val != 0x38 && 
	    val != 0x1c && val != 0x1e && val != 0x1f) {
		printk(KERN_WARNING "Teles: 16.3 Byte at %x is %x\n",
		       cs->hw.teles3.cfg_reg + 2, val);
		goto err;
	}
	if (teles_hw_init(cs) < 0)
		goto err;
	return 0;
 err:
	hisax_release_resources(cs);
	return -EBUSY;
}

static int __init
compaq_probe(struct IsdnCardState *cs, struct IsdnCard *card)
{
	cs->hw.teles3.cfg_reg = card->para[3];
	cs->hw.teles3.isac = card->para[2] - 32;
	cs->hw.teles3.hscx[0] = card->para[1] - 32;
	cs->hw.teles3.hscx[1] = card->para[1];
	teles_setup_io(cs, card);
	if (!request_io(&cs->rs, cs->hw.teles3.cfg_reg, 1, "teles3 cfg"))
		goto err;
	if (teles_request_io(cs) < 0)
		goto err;
	if (teles_hw_init(cs) < 0)
		goto err;
	return 0;
 err:
	hisax_release_resources(cs);
	return -EBUSY;
}

static int __init
telespnp_probe(struct IsdnCardState *cs, struct IsdnCard *card)
{
	cs->hw.teles3.cfg_reg = 0;
	cs->hw.teles3.isac = card->para[1] - 32;
	cs->hw.teles3.hscx[0] = card->para[2] - 32;
	cs->hw.teles3.hscx[1] = card->para[2];
	teles_setup_io(cs, card);
	if (teles_request_io(cs) < 0)
		goto err;
	if (teles_hw_init(cs) < 0)
		goto err;
	return 0;
 err:
	hisax_release_resources(cs);
	return -EBUSY;
}

#ifdef __ISAPNP__
static struct isapnp_device_id teles_ids[] __initdata = {
	{ ISAPNP_VENDOR('T', 'A', 'G'), ISAPNP_FUNCTION(0x2110),
	  ISAPNP_VENDOR('T', 'A', 'G'), ISAPNP_FUNCTION(0x2110), 
	  (unsigned long) "Teles 16.3 PnP" },
	{ ISAPNP_VENDOR('C', 'T', 'X'), ISAPNP_FUNCTION(0x0),
	  ISAPNP_VENDOR('C', 'T', 'X'), ISAPNP_FUNCTION(0x0), 
	  (unsigned long) "Creatix 16.3 PnP" },
	{ ISAPNP_VENDOR('C', 'P', 'Q'), ISAPNP_FUNCTION(0x1002),
	  ISAPNP_VENDOR('C', 'P', 'Q'), ISAPNP_FUNCTION(0x1002), 
	  (unsigned long) "Compaq ISDN S0" },
	{ 0, }
};

static struct isapnp_device_id *tdev = &teles_ids[0];
static struct pnp_card *pnp_c __devinitdata = NULL;
#endif

int __devinit
setup_teles3(struct IsdnCard *card)
{
	char tmp[64];

	strcpy(tmp, teles3_revision);
	printk(KERN_INFO "HiSax: Teles IO driver Rev. %s\n", HiSax_getrev(tmp));
#ifdef __ISAPNP__
	if (!card->para[1] && isapnp_present()) {
		struct pnp_card *pnp_card;
		struct pnp_dev *pnp_dev;

		while(tdev->card_vendor) {
			if ((pnp_card = pnp_find_card(tdev->card_vendor,
						      tdev->card_device, pnp_c))) {
				pnp_c = pnp_card;
				pnp_dev = NULL;
				if ((pnp_dev = pnp_find_dev(pnp_card,
							    tdev->vendor,
							    tdev->function,
							    pnp_dev))) {
					printk(KERN_INFO "HiSax: %s detected\n",
						(char *)tdev->driver_data);
					if (pnp_device_attach(pnp_dev) < 0) {
						printk(KERN_ERR "Teles PnP: attach failed\n");
						return 0;
					}
					if (pnp_activate_dev(pnp_dev) < 0) {
						printk(KERN_ERR "Teles PnP: activate failed\n");
						pnp_device_detach(pnp_dev);
						return 0;
					}
					if (!pnp_irq_valid(pnp_dev, 0) ||
					    !pnp_port_valid(pnp_dev, 0) ||
					    !pnp_port_valid(pnp_dev, 1)) {
						printk(KERN_ERR "Teles PnP: some resources are missing %ld/%lx/%lx\n",
							pnp_irq(pnp_dev, 0), pnp_port_start(pnp_dev, 0), pnp_port_start(pnp_dev, 1));
						pnp_device_detach(pnp_dev);
						return 0;
					}
					card->para[3] = pnp_port_start(pnp_dev, 2);
					card->para[2] = pnp_port_start(pnp_dev, 1);
					card->para[1] = pnp_port_start(pnp_dev, 0);
					card->para[0] = pnp_irq(pnp_dev, 0);
					break;
				} else {
					printk(KERN_ERR "Teles PnP: PnP error card found, no device\n");
				}
			}
			tdev++;
			pnp_c=NULL;
		} 
		if (!tdev->card_vendor) {
			printk(KERN_INFO "Teles PnP: no ISAPnP card found\n");
			return(0);
		}
	}
#endif
	if (card->cs->typ == ISDN_CTYPE_16_3) {
		if (teles16_3_probe(card->cs, card) < 0)
			return 0;
	} else if (card->cs->typ == ISDN_CTYPE_TELESPCMCIA) {
		if (telespcmcia_probe(card->cs, card) < 0)
			return 0;
	} else if (card->cs->typ == ISDN_CTYPE_COMPAQ_ISA) {
		if (compaq_probe(card->cs, card) < 0)
			return 0;
	} else {	/* PNP */
		if (telespnp_probe(card->cs, card) < 0)
			return 0;
	}
	return 1;
}
