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
readfifo(struct IsdnCardState *cs, unsigned int adr, u8 off, u8 * data, int size)
{
	unsigned long flags;

	spin_lock_irqsave(&ix1_micro_lock, flags);
	byteout(cs->hw.ix1.isac_ale, off);
	insb(adr, data, size);
	spin_unlock_irqrestore(&ix1_micro_lock, flags);
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
writefifo(struct IsdnCardState *cs, unsigned int adr, u8 off, u8 * data, int size)
{
	unsigned long flags;

	spin_lock_irqsave(&ix1_micro_lock, flags);
	byteout(cs->hw.ix1.isac_ale, off);
	outsb(adr, data, size);
	spin_unlock_irqrestore(&ix1_micro_lock, flags);
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

#define READHSCX(cs, nr, reg) readreg(cs, \
		cs->hw.ix1.hscx, reg + (nr ? 0x40 : 0))
#define WRITEHSCX(cs, nr, reg, data) writereg(cs, \
		cs->hw.ix1.hscx, reg + (nr ? 0x40 : 0), data)

#define READHSCXFIFO(cs, nr, ptr, cnt) readfifo(cs, \
		cs->hw.ix1.hscx, (nr ? 0x40 : 0), ptr, cnt)

#define WRITEHSCXFIFO(cs, nr, ptr, cnt) writefifo(cs, \
		cs->hw.ix1.hscx, (nr ? 0x40 : 0), ptr, cnt)

#include "hscx_irq.c"

static void
ix1micro_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
	struct IsdnCardState *cs = dev_id;
	u8 val;

	spin_lock(&cs->lock);
	val = readreg(cs, cs->hw.ix1.hscx, HSCX_ISTA + 0x40);
      Start_HSCX:
	if (val)
		hscx_int_main(cs, val);
	val = readreg(cs, cs->hw.ix1.isac, ISAC_ISTA);
      Start_ISAC:
	if (val)
		isac_interrupt(cs, val);
	val = readreg(cs, cs->hw.ix1.hscx, HSCX_ISTA + 0x40);
	if (val) {
		if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "HSCX IntStat after IntRoutine");
		goto Start_HSCX;
	}
	val = readreg(cs, cs->hw.ix1.isac, ISAC_ISTA);
	if (val) {
		if (cs->debug & L1_DEB_ISAC)
			debugl1(cs, "ISAC IntStat after IntRoutine");
		goto Start_ISAC;
	}
	writereg(cs, cs->hw.ix1.hscx, HSCX_MASK, 0xFF);
	writereg(cs, cs->hw.ix1.hscx, HSCX_MASK + 0x40, 0xFF);
	writereg(cs, cs->hw.ix1.isac, ISAC_MASK, 0xFF);
	writereg(cs, cs->hw.ix1.isac, ISAC_MASK, 0);
	writereg(cs, cs->hw.ix1.hscx, HSCX_MASK, 0);
	writereg(cs, cs->hw.ix1.hscx, HSCX_MASK + 0x40, 0);
	spin_unlock(&cs->lock);
}

void
release_io_ix1micro(struct IsdnCardState *cs)
{
	if (cs->hw.ix1.cfg_reg)
		release_region(cs->hw.ix1.cfg_reg, 4);
}

static void
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
}

static int
ix1_card_msg(struct IsdnCardState *cs, int mt, void *arg)
{
	switch (mt) {
		case CARD_RESET:
			ix1_reset(cs);
			return(0);
		case CARD_RELEASE:
			release_io_ix1micro(cs);
			return(0);
		case CARD_INIT:
			inithscxisac(cs);
			return(0);
		case CARD_TEST:
			return(0);
	}
	return(0);
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
static struct pci_bus *pnp_c __devinitdata = NULL;
#endif


int __init
setup_ix1micro(struct IsdnCard *card)
{
	struct IsdnCardState *cs = card->cs;
	char tmp[64];

	strcpy(tmp, ix1_revision);
	printk(KERN_INFO "HiSax: ITK IX1 driver Rev. %s\n", HiSax_getrev(tmp));
	if (cs->typ != ISDN_CTYPE_IX1MICROR2)
		return (0);

#ifdef __ISAPNP__
	if (!card->para[1] && isapnp_present()) {
		struct pci_bus *pb;
		struct pci_dev *pd;

		while(idev->card_vendor) {
			if ((pb = isapnp_find_card(idev->card_vendor,
				idev->card_device, pnp_c))) {
				pnp_c = pb;
				pd = NULL;
				if ((pd = isapnp_find_dev(pnp_c,
					idev->vendor, idev->function, pd))) {
					printk(KERN_INFO "HiSax: %s detected\n",
						(char *)idev->driver_data);
					pd->prepare(pd);
					pd->deactivate(pd);
					pd->activate(pd);
					card->para[1] = pd->resource[0].start;
					card->para[0] = pd->irq_resource[0].start;
					if (!card->para[0] || !card->para[1]) {
						printk(KERN_ERR "ITK PnP:some resources are missing %ld/%lx\n",
						card->para[0], card->para[1]);
						pd->deactivate(pd);
						return(0);
					}
					break;
				} else {
					printk(KERN_ERR "ITK PnP: PnP error card found, no device\n");
				}
			}
			idev++;
			pnp_c=NULL;
		} 
		if (!idev->card_vendor) {
			printk(KERN_INFO "ITK PnP: no ISAPnP card found\n");
			return(0);
		}
	}
#endif
	/* IO-Ports */
	cs->hw.ix1.isac_ale = card->para[1] + ISAC_COMMAND_OFFSET;
	cs->hw.ix1.isac = card->para[1] + ISAC_DATA_OFFSET;
	cs->hw.ix1.hscx = card->para[1] + HSCX_DATA_OFFSET;
	cs->hw.ix1.cfg_reg = card->para[1];
	cs->irq = card->para[0];
	if (cs->hw.ix1.cfg_reg) {
		if (check_region((cs->hw.ix1.cfg_reg), 4)) {
			printk(KERN_WARNING
			  "HiSax: %s config port %x-%x already in use\n",
			       CardType[card->typ],
			       cs->hw.ix1.cfg_reg,
			       cs->hw.ix1.cfg_reg + 4);
			return (0);
		} else
			request_region(cs->hw.ix1.cfg_reg, 4, "ix1micro cfg");
	}
	printk(KERN_INFO
	       "HiSax: %s config irq:%d io:0x%X\n",
	       CardType[cs->typ], cs->irq,
	       cs->hw.ix1.cfg_reg);
	ix1_reset(cs);
	cs->dc_hw_ops = &isac_ops;
	cs->bc_hw_ops = &hscx_ops;
	cs->BC_Send_Data = &hscx_fill_fifo;
	cs->cardmsg = &ix1_card_msg;
	cs->irq_func = &ix1micro_interrupt;
	ISACVersion(cs, "ix1-Micro:");
	if (HscxVersion(cs, "ix1-Micro:")) {
		printk(KERN_WARNING
		    "ix1-Micro: wrong HSCX versions check IO address\n");
		release_io_ix1micro(cs);
		return (0);
	}
	return (1);
}
