/* $Id: s0box.c,v 2.4.6.2 2001/09/23 22:24:51 kai Exp $
 *
 * low level stuff for Creatix S0BOX
 *
 * Author       Enrik Berkhan
 * Copyright    by Enrik Berkhan <enrik@starfleet.inka.de>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include <linux/init.h>
#include "hisax.h"
#include "isac.h"
#include "hscx.h"
#include "isdnl1.h"

extern const char *CardType[];
const char *s0box_revision = "$Revision: 2.4.6.2 $";
static spinlock_t s0box_lock = SPIN_LOCK_UNLOCKED;

static inline void
writereg(struct IsdnCardState *cs, int addr, u8 off, u8 val)
{
	unsigned long flags;
	unsigned long padr = cs->hw.teles3.cfg_reg;

	spin_lock_irqsave(&s0box_lock, flags);
	outb_p(0x1c,padr+2);
	outb_p(0x14,padr+2);
	outb_p((addr+off)&0x7f,padr);
	outb_p(0x16,padr+2);
	outb_p(val,padr);
	outb_p(0x17,padr+2);
	outb_p(0x14,padr+2);
	outb_p(0x1c,padr+2);
	spin_unlock_irqrestore(&s0box_lock, flags);
}

static u8 nibtab[] = { 1, 9, 5, 0xd, 3, 0xb, 7, 0xf,
			 0, 0, 0, 0, 0, 0, 0, 0,
			 0, 8, 4, 0xc, 2, 0xa, 6, 0xe } ;

static inline u8
readreg(struct IsdnCardState *cs, int addr, u8 off)
{
	u8 n1, n2;
	unsigned long flags;
	unsigned long padr = cs->hw.teles3.cfg_reg;

	spin_lock_irqsave(&s0box_lock, flags);
	outb_p(0x1c,padr+2);
	outb_p(0x14,padr+2);
	outb_p((addr+off)|0x80,padr);
	outb_p(0x16,padr+2);
	outb_p(0x17,padr+2);
	n1 = (inb_p(padr+1) >> 3) & 0x17;
	outb_p(0x16,padr+2);
	n2 = (inb_p(padr+1) >> 3) & 0x17;
	outb_p(0x14,padr+2);
	outb_p(0x1c,padr+2);
	spin_unlock_irqrestore(&s0box_lock, flags);
	return nibtab[n1] | (nibtab[n2] << 4);
}

static inline void
read_fifo(struct IsdnCardState *cs, signed int adr, u8 * data, int size)
{
	int i;
	u8 n1, n2;
	unsigned long padr = cs->hw.teles3.cfg_reg;
	
	outb_p(0x1c, padr+2);
	outb_p(0x14, padr+2);
	outb_p(adr|0x80, padr);
	outb_p(0x16, padr+2);
	for (i=0; i<size; i++) {
		outb_p(0x17, padr+2);
		n1 = (inb_p(padr+1) >> 3) & 0x17;
		outb_p(0x16,padr+2);
		n2 = (inb_p(padr+1) >> 3) & 0x17;
		*(data++)=nibtab[n1] | (nibtab[n2] << 4);
	}
	outb_p(0x14,padr+2);
	outb_p(0x1c,padr+2);
}

static inline void
write_fifo(struct IsdnCardState *cs, signed int adr, u8 * data, int size)
{
	int i;
	unsigned long padr = cs->hw.teles3.cfg_reg;

	outb_p(0x1c, padr+2);
	outb_p(0x14, padr+2);
	outb_p(adr&0x7f, padr);
	for (i=0; i<size; i++) {
		outb_p(0x16, padr+2);
		outb_p(*(data++), padr);
		outb_p(0x17, padr+2);
	}
	outb_p(0x14,padr+2);
	outb_p(0x1c,padr+2);
}

static u8
isac_read(struct IsdnCardState *cs, u8 offset)
{
	return readreg(cs, cs->hw.teles3.isac, offset);
}

static void
isac_write(struct IsdnCardState *cs, u8 offset, u8 value)
{
	writereg(cs, cs->hw.teles3.isac, offset, value);
}

static void
isac_read_fifo(struct IsdnCardState *cs, u8 * data, int size)
{
	read_fifo(cs, cs->hw.teles3.isacfifo, data, size);
}

static void
isac_write_fifo(struct IsdnCardState *cs, u8 * data, int size)
{
	write_fifo(cs, cs->hw.teles3.isacfifo, data, size);
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
	return readreg(cs, cs->hw.teles3.hscx[hscx], offset);
}

static void
hscx_write(struct IsdnCardState *cs, int hscx, u8 offset, u8 value)
{
	writereg(cs, cs->hw.teles3.hscx[hscx], offset, value);
}

static void
hscx_read_fifo(struct IsdnCardState *cs, int hscx, u8 *data, int size)
{
	read_fifo(cs, cs->hw.teles3.hscxfifo[hscx], data, size);
}

static void
hscx_write_fifo(struct IsdnCardState *cs, int hscx, u8 *data, int size)
{
	write_fifo(cs, cs->hw.teles3.hscxfifo[hscx], data, size);
}

static struct bc_hw_ops hscx_ops = {
	.read_reg   = hscx_read,
	.write_reg  = hscx_write,
	.read_fifo  = hscx_read_fifo,
	.write_fifo = hscx_write_fifo,
};
 
static struct card_ops s0box_ops = {
	.init     = inithscxisac,
	.release  = hisax_release_resources,
	.irq_func = hscxisac_irq,
};

static int __init
s0box_probe(struct IsdnCardState *cs, struct IsdnCard *card)
{
	cs->hw.teles3.cfg_reg = card->para[1];
	cs->hw.teles3.hscx[0] = -0x20;
	cs->hw.teles3.hscx[1] = 0x0;
	cs->hw.teles3.isac = 0x20;
	cs->hw.teles3.isacfifo = cs->hw.teles3.isac + 0x3e;
	cs->hw.teles3.hscxfifo[0] = cs->hw.teles3.hscx[0] + 0x3e;
	cs->hw.teles3.hscxfifo[1] = cs->hw.teles3.hscx[1] + 0x3e;
	cs->irq = card->para[0];
	if (!request_io(&cs->rs, cs->hw.teles3.cfg_reg, 8, "S0Box parallel I/O"))
		goto err;
	printk(KERN_INFO
	       "HiSax: %s config irq:%d isac:0x%x  cfg:0x%x\n",
	       CardType[cs->typ], cs->irq,
	       cs->hw.teles3.isac, cs->hw.teles3.cfg_reg);
	printk(KERN_INFO
	       "HiSax: hscx A:0x%x  hscx B:0x%x\n",
	       cs->hw.teles3.hscx[0], cs->hw.teles3.hscx[1]);
	cs->card_ops = &s0box_ops;
	if (hscxisac_setup(cs, &isac_ops, &hscx_ops))
		goto err;
	return 0;
 err:
	hisax_release_resources(cs);
	return -EBUSY;
}

int __init
setup_s0box(struct IsdnCard *card)
{
	char tmp[64];

	strcpy(tmp, s0box_revision);
	printk(KERN_INFO "HiSax: S0Box IO driver Rev. %s\n", HiSax_getrev(tmp));
	if (s0box_probe(card->cs, card))
		return 0;
	return 1;
}
