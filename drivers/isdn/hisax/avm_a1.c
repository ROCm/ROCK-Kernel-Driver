/* $Id: avm_a1.c,v 2.13.6.2 2001/09/23 22:24:46 kai Exp $
 *
 * low level stuff for AVM A1 (Fritz) isdn cards
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
#include "hscx.h"
#include "isdnl1.h"

extern const char *CardType[];
static const char *avm_revision = "$Revision: 2.13.6.2 $";

#define	 AVM_A1_STAT_ISAC	0x01
#define	 AVM_A1_STAT_HSCX	0x02
#define	 AVM_A1_STAT_TIMER	0x04

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
	return readreg(cs->hw.avm.isac, offset);
}

static void
isac_write(struct IsdnCardState *cs, u8 offset, u8 value)
{
	writereg(cs->hw.avm.isac, offset, value);
}

static void
isac_read_fifo(struct IsdnCardState *cs, u8 * data, int size)
{
	read_fifo(cs->hw.avm.isacfifo, data, size);
}

static void
isac_write_fifo(struct IsdnCardState *cs, u8 * data, int size)
{
	write_fifo(cs->hw.avm.isacfifo, data, size);
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
	return (readreg(cs->hw.avm.hscx[hscx], offset));
}

static void
hscx_write(struct IsdnCardState *cs, int hscx, u8 offset, u8 value)
{
	writereg(cs->hw.avm.hscx[hscx], offset, value);
}

static void
hscx_read_fifo(struct IsdnCardState *cs, int hscx, u8 *data, int size)
{
	read_fifo(cs->hw.avm.hscxfifo[hscx], data, size);
}

static void
hscx_write_fifo(struct IsdnCardState *cs, int hscx, u8 *data, int size)
{
	write_fifo(cs->hw.avm.hscxfifo[hscx], data, size);
}

static struct bc_hw_ops hscx_ops = {
	.read_reg   = hscx_read,
	.write_reg  = hscx_write,
	.read_fifo  = hscx_read_fifo,
	.write_fifo = hscx_write_fifo,
};

static irqreturn_t
avm_a1_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
	struct IsdnCardState *cs = dev_id;
	u8 val, sval;
	int handled = 0;

	spin_lock(&cs->lock);
	while (((sval = bytein(cs->hw.avm.cfg_reg)) & 0xf) != 0x7) {
		handled = 1;
		if (!(sval & AVM_A1_STAT_TIMER)) {
			byteout(cs->hw.avm.cfg_reg, 0x1E);
			sval = bytein(cs->hw.avm.cfg_reg);
		} else if (cs->debug & L1_DEB_INTSTAT)
			debugl1(cs, "avm IntStatus %x", sval);
		if (!(sval & AVM_A1_STAT_HSCX)) {
			val = hscx_read(cs, 1, HSCX_ISTA);
			if (val)
				hscx_int_main(cs, val);
		}
		if (!(sval & AVM_A1_STAT_ISAC)) {
			val = isac_read(cs, ISAC_ISTA);
			if (val)
				isac_interrupt(cs, val);
		}
	}
	hscx_write(cs, 0, HSCX_MASK, 0xFF);
	hscx_write(cs, 1, HSCX_MASK, 0xFF);
	isac_write(cs, ISAC_MASK, 0xFF);
	isac_write(cs, ISAC_MASK, 0x0);
	hscx_write(cs, 0, HSCX_MASK, 0x0);
	hscx_write(cs, 1, HSCX_MASK, 0x0);
	spin_unlock(&cs->lock);
	return IRQ_RETVAL(handled);
}

static void
avm_a1_init(struct IsdnCardState *cs)
{
	byteout(cs->hw.avm.cfg_reg, 0x16);
	byteout(cs->hw.avm.cfg_reg, 0x1E);
	inithscxisac(cs);
}

static struct card_ops avm_a1_ops = {
	.init     = avm_a1_init,
	.release  = hisax_release_resources,
	.irq_func = avm_a1_interrupt,
};

static int __init
avm_a1_probe(struct IsdnCardState *cs, struct IsdnCard *card)
{
	int rc;
	u8 val;

	printk(KERN_INFO "AVM A1: defined at %#lx IRQ %lu\n",
	       card->para[1], card->para[0]);

	rc = -EBUSY;
	cs->hw.avm.cfg_reg     = request_io(&cs->rs, card->para[1] + 0x1800,  8, "avm cfg");
	if (!cs->hw.avm.cfg_reg) goto err;
	cs->hw.avm.isac        = request_io(&cs->rs, card->para[1] + 0x1400, 32, "HiSax isac");
	if (!cs->hw.avm.isac) goto err;
	cs->hw.avm.isacfifo    = request_io(&cs->rs, card->para[1] + 0x1000,  1, "HiSax isac fifo");
	if (!cs->hw.avm.isacfifo) goto err;
	cs->hw.avm.hscx[0]     = request_io(&cs->rs, card->para[1] + 0x400,  32, "HiSax hscx A");
	if (!cs->hw.avm.hscx[0]) goto err;
	cs->hw.avm.hscxfifo[0] = request_io(&cs->rs, card->para[1],           1, "HiSax hscx A fifo");
	if (!cs->hw.avm.hscxfifo[0]) goto err;
	cs->hw.avm.hscx[1]     = request_io(&cs->rs, card->para[1] + 0xc00,  32, "HiSax hscx B");
	if (!cs->hw.avm.hscx[1]) goto err;
	cs->hw.avm.hscxfifo[1] = request_io(&cs->rs, card->para[1] + 0x800,   1, "HiSax hscx B fifo");
	if (!cs->hw.avm.hscxfifo[1]) goto err;
	cs->hw.avm.isac    -= 0x20;
	cs->hw.avm.hscx[0] -= 0x20;
	cs->hw.avm.hscx[1] -= 0x20;
	cs->irq = card->para[0];

	byteout(cs->hw.avm.cfg_reg, 0x0);
	HZDELAY(HZ / 5 + 1);
	byteout(cs->hw.avm.cfg_reg, 0x1);
	HZDELAY(HZ / 5 + 1);
	byteout(cs->hw.avm.cfg_reg, 0x0);
	HZDELAY(HZ / 5 + 1);
	val = cs->irq;
	if (val == 9)
		val = 2;
	byteout(cs->hw.avm.cfg_reg + 1, val);
	HZDELAY(HZ / 5 + 1);
	byteout(cs->hw.avm.cfg_reg, 0x0);
	HZDELAY(HZ / 5 + 1);

	val = bytein(cs->hw.avm.cfg_reg);
	printk(KERN_INFO "AVM A1: Byte at %x is %x\n",
	       cs->hw.avm.cfg_reg, val);
	val = bytein(cs->hw.avm.cfg_reg + 3);
	printk(KERN_INFO "AVM A1: Byte at %x is %x\n",
	       cs->hw.avm.cfg_reg + 3, val);
	val = bytein(cs->hw.avm.cfg_reg + 2);
	printk(KERN_INFO "AVM A1: Byte at %x is %x\n",
	       cs->hw.avm.cfg_reg + 2, val);
	val = bytein(cs->hw.avm.cfg_reg);
	printk(KERN_INFO "AVM A1: Byte at %x is %x\n",
	       cs->hw.avm.cfg_reg, val);

	cs->card_ops = &avm_a1_ops;
	if (hscxisac_setup(cs, &isac_ops, &hscx_ops))
		goto err;
	return 0;
 err:
	hisax_release_resources(cs);
	return rc;
}

int __init
setup_avm_a1(struct IsdnCard *card)
{
	char tmp[64];

	strcpy(tmp, avm_revision);
	printk(KERN_INFO "HiSax: AVM driver Rev. %s\n", HiSax_getrev(tmp));

	if (avm_a1_probe(card->cs, card) < 0)
		return 0;
	return 1;
}
