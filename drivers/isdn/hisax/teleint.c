/* $Id: teleint.c,v 1.14.6.2 2001/09/23 22:24:52 kai Exp $
 *
 * low level stuff for TeleInt isdn cards
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
#include "hfc_2bs0.h"
#include "isdnl1.h"

extern const char *CardType[];

const char *TeleInt_revision = "$Revision: 1.14.6.2 $";
static spinlock_t teleint_lock = SPIN_LOCK_UNLOCKED;

#define byteout(addr,val) outb(val,addr)
#define bytein(addr) inb(addr)

static inline u8
readreg(unsigned int ale, unsigned int adr, u8 off)
{
	register u8 ret;
	int max_delay = 2000;
	unsigned long flags;

	spin_lock_irqsave(&teleint_lock, flags);
	byteout(ale, off);
	ret = HFC_BUSY & bytein(ale);
	while (ret && --max_delay)
		ret = HFC_BUSY & bytein(ale);
	if (!max_delay) {
		printk(KERN_WARNING "TeleInt Busy not inactive\n");
		spin_unlock_irqrestore(&teleint_lock, flags);
		return (0);
	}
	ret = bytein(adr);
	spin_unlock_irqrestore(&teleint_lock, flags);
	return (ret);
}

static inline void
readfifo(unsigned int ale, unsigned int adr, u8 off, u8 * data, int size)
{
	register u8 ret;
	register int max_delay = 20000;
	register int i;
	
	byteout(ale, off);
	for (i = 0; i<size; i++) {
		ret = HFC_BUSY & bytein(ale);
		while (ret && --max_delay)
			ret = HFC_BUSY & bytein(ale);
		if (!max_delay) {
			printk(KERN_WARNING "TeleInt Busy not inactive\n");
			return;
		}
		data[i] = bytein(adr);
	}
}


static inline void
writereg(unsigned int ale, unsigned int adr, u8 off, u8 data)
{
	register u8 ret;
	int max_delay = 2000;
	unsigned long flags;

	spin_lock_irqsave(&teleint_lock, flags);
	byteout(ale, off);
	ret = HFC_BUSY & bytein(ale);
	while (ret && --max_delay)
		ret = HFC_BUSY & bytein(ale);
	if (!max_delay) {
		printk(KERN_WARNING "TeleInt Busy not inactive\n");
		spin_unlock_irqrestore(&teleint_lock, flags);
		return;
	}
	byteout(adr, data);
	spin_unlock_irqrestore(&teleint_lock, flags);
}

static inline void
writefifo(unsigned int ale, unsigned int adr, u8 off, u8 * data, int size)
{
	register u8 ret;
	register int max_delay = 20000;
	register int i;
	
	/* fifo write without cli because it's already done  */
	byteout(ale, off);
	for (i = 0; i<size; i++) {
		ret = HFC_BUSY & bytein(ale);
		while (ret && --max_delay)
			ret = HFC_BUSY & bytein(ale);
		if (!max_delay) {
			printk(KERN_WARNING "TeleInt Busy not inactive\n");
			return;
		}
		byteout(adr, data[i]);
	}
}

/* Interface functions */

static u8
ReadISAC(struct IsdnCardState *cs, u8 offset)
{
	cs->hw.hfc.cip = offset;
	return (readreg(cs->hw.hfc.addr | 1, cs->hw.hfc.addr, offset));
}

static void
WriteISAC(struct IsdnCardState *cs, u8 offset, u8 value)
{
	cs->hw.hfc.cip = offset;
	writereg(cs->hw.hfc.addr | 1, cs->hw.hfc.addr, offset, value);
}

static void
ReadISACfifo(struct IsdnCardState *cs, u8 * data, int size)
{
	cs->hw.hfc.cip = 0;
	readfifo(cs->hw.hfc.addr | 1, cs->hw.hfc.addr, 0, data, size);
}

static void
WriteISACfifo(struct IsdnCardState *cs, u8 * data, int size)
{
	cs->hw.hfc.cip = 0;
	writefifo(cs->hw.hfc.addr | 1, cs->hw.hfc.addr, 0, data, size);
}

static struct dc_hw_ops isac_ops = {
	.read_reg   = ReadISAC,
	.write_reg  = WriteISAC,
	.read_fifo  = ReadISACfifo,
	.write_fifo = WriteISACfifo,
};

static u8
ReadHFC(struct IsdnCardState *cs, int data, u8 reg)
{
	register u8 ret;

	if (data) {
		cs->hw.hfc.cip = reg;
		byteout(cs->hw.hfc.addr | 1, reg);
		ret = bytein(cs->hw.hfc.addr);
		if (cs->debug & L1_DEB_HSCX_FIFO && (data != 2))
			debugl1(cs, "hfc RD %02x %02x", reg, ret);
	} else
		ret = bytein(cs->hw.hfc.addr | 1);
	return (ret);
}

static void
WriteHFC(struct IsdnCardState *cs, int data, u8 reg, u8 value)
{
	byteout(cs->hw.hfc.addr | 1, reg);
	cs->hw.hfc.cip = reg;
	if (data)
		byteout(cs->hw.hfc.addr, value);
	if (cs->debug & L1_DEB_HSCX_FIFO && (data != 2))
		debugl1(cs, "hfc W%c %02x %02x", data ? 'D' : 'C', reg, value);
}

static struct bc_hw_ops hfc_ops = {
	.read_reg  = ReadHFC,
	.write_reg = WriteHFC,
};

static irqreturn_t
teleint_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
	struct IsdnCardState *cs = dev_id;
	u8 val;

	spin_lock(&cs->lock);
	val = readreg(cs->hw.hfc.addr | 1, cs->hw.hfc.addr, ISAC_ISTA);
      Start_ISAC:
	if (val)
		isac_interrupt(cs, val);
	val = readreg(cs->hw.hfc.addr | 1, cs->hw.hfc.addr, ISAC_ISTA);
	if (val) {
		if (cs->debug & L1_DEB_ISAC)
			debugl1(cs, "ISAC IntStat after IntRoutine");
		goto Start_ISAC;
	}
	writereg(cs->hw.hfc.addr | 1, cs->hw.hfc.addr, ISAC_MASK, 0xFF);
	writereg(cs->hw.hfc.addr | 1, cs->hw.hfc.addr, ISAC_MASK, 0x0);
	spin_unlock(&cs->lock);
	return IRQ_HANDLED;
}

static void
TeleInt_Timer(struct IsdnCardState *cs)
{
	int stat = 0;

	if (cs->bcs[0].mode) {
		stat |= 1;
		main_irq_hfc(&cs->bcs[0]);
	}
	if (cs->bcs[1].mode) {
		stat |= 2;
		main_irq_hfc(&cs->bcs[1]);
	}
	cs->hw.hfc.timer.expires = jiffies + 1;
	add_timer(&cs->hw.hfc.timer);
}

static void
teleint_release(struct IsdnCardState *cs)
{
	del_timer(&cs->hw.hfc.timer);
	releasehfc(cs);
	hisax_release_resources(cs);
}

static int
teleint_reset(struct IsdnCardState *cs)
{
	printk(KERN_INFO "TeleInt: resetting card\n");
	cs->hw.hfc.cirm |= HFC_RESET;
	byteout(cs->hw.hfc.addr | 1, cs->hw.hfc.cirm);	/* Reset On */
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout((30*HZ)/1000);
	cs->hw.hfc.cirm &= ~HFC_RESET;
	byteout(cs->hw.hfc.addr | 1, cs->hw.hfc.cirm);	/* Reset Off */
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout((10*HZ)/1000);
	return 0;
}

static void
teleint_init(struct IsdnCardState *cs)
{
	inithfc(cs);
	initisac(cs);
	cs->hw.hfc.timer.expires = jiffies + 1;
	add_timer(&cs->hw.hfc.timer);
}

static struct card_ops teleint_ops = {
	.init     = teleint_init,
	.reset    = teleint_reset,
	.release  = teleint_release,
	.irq_func = teleint_interrupt,
};

static int __init
teleint_probe(struct IsdnCardState *cs, struct IsdnCard *card)
{
	cs->hw.hfc.addr = card->para[1] & 0x3fe;
	cs->irq = card->para[0];
	cs->hw.hfc.cirm = HFC_CIRM;
	cs->hw.hfc.isac_spcr = 0x00;
	cs->hw.hfc.cip = 0;
	cs->hw.hfc.ctmt = HFC_CTMT | HFC_CLTIMER;
	cs->bcs[0].hw.hfc.send = NULL;
	cs->bcs[1].hw.hfc.send = NULL;
	cs->hw.hfc.fifosize = 7 * 1024 + 512;
	cs->hw.hfc.timer.function = (void *) TeleInt_Timer;
	cs->hw.hfc.timer.data = (long) cs;
	init_timer(&cs->hw.hfc.timer);
	if (!request_io(&cs->rs, cs->hw.hfc.addr, 2, "TeleInt isdn"))
		goto err;
	
	/* HW IO = IO */
	byteout(cs->hw.hfc.addr, cs->hw.hfc.addr & 0xff);
	byteout(cs->hw.hfc.addr | 1, ((cs->hw.hfc.addr & 0x300) >> 8) | 0x54);
	switch (cs->irq) {
	case 3:
		cs->hw.hfc.cirm |= HFC_INTA;
		break;
	case 4:
		cs->hw.hfc.cirm |= HFC_INTB;
		break;
	case 5:
		cs->hw.hfc.cirm |= HFC_INTC;
		break;
	case 7:
		cs->hw.hfc.cirm |= HFC_INTD;
		break;
	case 10:
		cs->hw.hfc.cirm |= HFC_INTE;
		break;
	case 11:
		cs->hw.hfc.cirm |= HFC_INTF;
		break;
	default:
		printk(KERN_WARNING "TeleInt: wrong IRQ\n");
		goto err;
	}
	byteout(cs->hw.hfc.addr | 1, cs->hw.hfc.cirm);
	byteout(cs->hw.hfc.addr | 1, cs->hw.hfc.ctmt);

	printk(KERN_INFO "TeleInt: defined at 0x%x IRQ %d\n",
	       cs->hw.hfc.addr, cs->irq);

	cs->card_ops = &teleint_ops;
	teleint_reset(cs);
	isac_setup(cs, &isac_ops);
	hfc_setup(cs, &hfc_ops);
	return 0;

 err:
	hisax_release_resources(cs);
	return -EBUSY;
}

int __init
setup_TeleInt(struct IsdnCard *card)
{
	char tmp[64];

	strcpy(tmp, TeleInt_revision);
	printk(KERN_INFO "HiSax: TeleInt driver Rev. %s\n", HiSax_getrev(tmp));

	if (teleint_probe(card->cs, card) < 0)
		return 0;
	return 1;
}
