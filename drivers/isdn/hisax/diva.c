/* $Id: diva.c,v 1.25.6.5 2001/09/23 22:24:47 kai Exp $
 *
 * low level stuff for Eicon.Diehl Diva Family ISDN cards
 *
 * Author       Karsten Keil
 * Copyright    by Karsten Keil      <keil@isdn4linux.de>
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * For changes and modifications please read
 * ../../../Documentation/isdn/HiSax.cert
 *
 * Thanks to Eicon Technology for documents and information
 *
 */

#include <linux/init.h>
#include <linux/config.h>
#include "hisax.h"
#include "isac.h"
#include "hscx.h"
#include "ipac.h"
#include "ipacx.h"
#include "isdnl1.h"
#include <linux/pci.h>
#include <linux/isapnp.h>

extern const char *CardType[];

const char *Diva_revision = "$Revision: 1.25.6.5 $";
static spinlock_t diva_lock = SPIN_LOCK_UNLOCKED;

#define byteout(addr,val) outb(val,addr)
#define bytein(addr) inb(addr)

#define DIVA_HSCX_DATA		0
#define DIVA_HSCX_ADR		4
#define DIVA_ISA_ISAC_DATA	2
#define DIVA_ISA_ISAC_ADR	6
#define DIVA_ISA_CTRL		7
#define DIVA_IPAC_ADR		0
#define DIVA_IPAC_DATA		1

#define DIVA_PCI_ISAC_DATA	8
#define DIVA_PCI_ISAC_ADR	0xc
#define DIVA_PCI_CTRL		0x10

/* SUB Types */
#define DIVA_ISA	1
#define DIVA_PCI	2
#define DIVA_IPAC_ISA	3
#define DIVA_IPAC_PCI	4
#define DIVA_IPACX_PCI	5

/* CTRL (Read) */
#define DIVA_IRQ_STAT	0x01
#define DIVA_EEPROM_SDA	0x02

/* CTRL (Write) */
#define DIVA_IRQ_REQ	0x01
#define DIVA_RESET	0x08
#define DIVA_EEPROM_CLK	0x40
#define DIVA_PCI_LED_A	0x10
#define DIVA_PCI_LED_B	0x20
#define DIVA_ISA_LED_A	0x20
#define DIVA_ISA_LED_B	0x40
#define DIVA_IRQ_CLR	0x80

/* Siemens PITA */
#define PITA_MISC_REG		0x1c
#ifdef __BIG_ENDIAN
#define PITA_PARA_SOFTRESET	0x00000001
#define PITA_SER_SOFTRESET	0x00000002
#define PITA_PARA_MPX_MODE	0x00000004
#define PITA_INT0_ENABLE	0x00000200
#else
#define PITA_PARA_SOFTRESET	0x01000000
#define PITA_SER_SOFTRESET	0x02000000
#define PITA_PARA_MPX_MODE	0x04000000
#define PITA_INT0_ENABLE	0x00020000
#endif
#define PITA_INT0_STATUS	0x02

static inline u8
readreg(unsigned int ale, unsigned int adr, u8 off)
{
	u8 ret;
	unsigned long flags;

	spin_lock_irqsave(&diva_lock, flags);
	byteout(ale, off);
	ret = bytein(adr);
	spin_unlock_irqrestore(&diva_lock, flags);
	return ret;
}

static inline void
writereg(unsigned int ale, unsigned int adr, u8 off, u8 data)
{
	unsigned long flags;

	spin_lock_irqsave(&diva_lock, flags);
	byteout(ale, off);
	byteout(adr, data);
	spin_unlock_irqrestore(&diva_lock, flags);
}

static inline void
readfifo(unsigned int ale, unsigned int adr, u8 off, u8 * data, int size)
{
	byteout(ale, off);
	insb(adr, data, size);
}

static inline void
writefifo(unsigned int ale, unsigned int adr, u8 off, u8 *data, int size)
{
	byteout(ale, off);
	outsb(adr, data, size);
}

static inline u8
memreadreg(unsigned long adr, u8 off)
{
	return readb(((unsigned int *)adr) + off);
}

static inline void
memwritereg(unsigned long adr, u8 off, u8 data)
{
	writeb(data, ((unsigned int *)adr) + off);
}

static u8
isac_read(struct IsdnCardState *cs, u8 offset)
{
	return readreg(cs->hw.diva.isac_adr, cs->hw.diva.isac, offset);
}

static void
isac_write(struct IsdnCardState *cs, u8 offset, u8 value)
{
	writereg(cs->hw.diva.isac_adr, cs->hw.diva.isac, offset, value);
}

static void
isac_read_fifo(struct IsdnCardState *cs, u8 *data, int size)
{
	readfifo(cs->hw.diva.isac_adr, cs->hw.diva.isac, 0, data, size);
}

static void
isac_write_fifo(struct IsdnCardState *cs, u8 *data, int size)
{
	writefifo(cs->hw.diva.isac_adr, cs->hw.diva.isac, 0, data, size);
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
	return readreg(cs->hw.diva.hscx_adr, cs->hw.diva.hscx, 
		       offset + (hscx ? 0x40 : 0));
}

static void
hscx_write(struct IsdnCardState *cs, int hscx, u8 offset, u8 value)
{
	writereg(cs->hw.diva.hscx_adr, cs->hw.diva.hscx,
		 offset + (hscx ? 0x40 : 0), value);
}

static void
hscx_read_fifo(struct IsdnCardState *cs, int hscx, u8 *data, int size)
{
	readfifo(cs->hw.diva.hscx_adr, cs->hw.diva.hscx, hscx ? 0x40 : 0, data, size);
}

static void
hscx_write_fifo(struct IsdnCardState *cs, int hscx, u8 *data, int size)
{
	writefifo(cs->hw.diva.hscx_adr, cs->hw.diva.hscx, hscx ? 0x40 : 0, data, size);
}

static struct bc_hw_ops hscx_ops = {
	.read_reg  = hscx_read,
	.write_reg = hscx_write,
	.read_fifo  = hscx_read_fifo,
	.write_fifo = hscx_write_fifo,
};

static inline u8
ipac_read(struct IsdnCardState *cs, u8 offset)
{
	return readreg(cs->hw.diva.isac_adr, cs->hw.diva.isac, offset);
}

static inline void
ipac_write(struct IsdnCardState *cs, u8 offset, u8 value)
{
	writereg(cs->hw.diva.isac_adr, cs->hw.diva.isac, offset, value);
}

static inline void
ipac_readfifo(struct IsdnCardState *cs, u8 offset, u8 *data, int size)
{
	readfifo(cs->hw.diva.isac_adr, cs->hw.diva.isac, offset, data, size);
}

static inline void
ipac_writefifo(struct IsdnCardState *cs, u8 offset, u8 *data, int size)
{
	writefifo(cs->hw.diva.isac_adr, cs->hw.diva.isac, offset, data, size);
}

/* This will generate ipac_dc_ops and ipac_bc_ops using the functions
 * above */

BUILD_IPAC_OPS(ipac);

static inline u8
mem_ipac_read(struct IsdnCardState *cs, u8 offset)
{
	return memreadreg(cs->hw.diva.cfg_reg, offset);
}

static inline void
mem_ipac_write(struct IsdnCardState *cs, u8 offset, u8 value)
{
	memwritereg(cs->hw.diva.cfg_reg, offset, value);
}

static inline void
mem_ipac_readfifo(struct IsdnCardState *cs, u8 offset, u8 *data, int size)
{
	while(size--)
		*data++ = memreadreg(cs->hw.diva.cfg_reg, offset);
}

static inline void
mem_ipac_writefifo(struct IsdnCardState *cs, u8 offset, u8 *data, int size)
{
	while(size--)
		memwritereg(cs->hw.diva.cfg_reg, offset, *data++);
}

/* This will generate mem_ipac_dc_ops and mem_ipac_bc_ops using the functions
 * above */

BUILD_IPAC_OPS(mem_ipac);

/* IO-Functions for IPACX type cards */
static u8
ipacx_dc_read(struct IsdnCardState *cs, u8 offset)
{
	return memreadreg(cs->hw.diva.cfg_reg, offset);
}

static void
ipacx_dc_write(struct IsdnCardState *cs, u8 offset, u8 value)
{
	memwritereg(cs->hw.diva.cfg_reg, offset, value);
}

static void
ipacx_dc_read_fifo(struct IsdnCardState *cs, u8 *data, int size)
{
	while(size--)
		*data++ = memreadreg(cs->hw.diva.cfg_reg, 0);
}

static void
ipacx_dc_write_fifo(struct IsdnCardState *cs, u8 *data, int size)
{
	while(size--)
		memwritereg(cs->hw.diva.cfg_reg, 0, *data++);
}

static struct dc_hw_ops ipacx_dc_ops = {
	.read_reg   = ipacx_dc_read,
	.write_reg  = ipacx_dc_write,
	.read_fifo  = ipacx_dc_read_fifo,
	.write_fifo = ipacx_dc_write_fifo,
};

static u8
ipacx_bc_read(struct IsdnCardState *cs, int hscx, u8 offset)
{
	return memreadreg(cs->hw.diva.cfg_reg, offset + 
			  (hscx ? IPACX_OFF_B2 : IPACX_OFF_B1));
}

static void
ipacx_bc_write(struct IsdnCardState *cs, int hscx, u8 offset, u8 value)
{
	memwritereg(cs->hw.diva.cfg_reg, offset + 
              (hscx ? IPACX_OFF_B2 : IPACX_OFF_B1), value);
}

static void
ipacx_bc_read_fifo(struct IsdnCardState *cs, int hscx, u8 *data, int len)
{
	int i;

	for (i = 0; i < len ; i++)
		*data++ = ipacx_bc_read(cs, hscx, IPACX_RFIFOB);
}

static struct bc_hw_ops ipacx_bc_ops = {
	.read_reg   = ipacx_bc_read,
	.write_reg  = ipacx_bc_write,
	.read_fifo  = ipacx_bc_read_fifo,
};

static irqreturn_t
diva_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
	struct IsdnCardState *cs = dev_id;
	u8 sval;
	int cnt=5;

	while (((sval = bytein(cs->hw.diva.ctrl)) & DIVA_IRQ_REQ) && cnt) {
		hscxisac_irq(intno, dev_id, regs);
	}
	if (!cnt)
		printk(KERN_WARNING "Diva: IRQ LOOP\n");
	return IRQ_HANDLED;
}

static irqreturn_t
diva_ipac_pci_irq(int intno, void *dev_id, struct pt_regs *regs)
{
	struct IsdnCardState *cs = dev_id;
	u8 val;

	val = readb(cs->hw.diva.pci_cfg);
	if (!(val & PITA_INT0_STATUS))
		return IRQ_NONE; /* other shared IRQ */
	writeb(PITA_INT0_STATUS, cs->hw.diva.pci_cfg); /* Reset pending INT0 */

	return ipac_irq(intno, dev_id, regs);
}

static irqreturn_t
diva_ipacx_pci_irq(int intno, void *dev_id, struct pt_regs *regs)
{
	struct IsdnCardState *cs = dev_id;
	u8 val;

	val = readb(cs->hw.diva.pci_cfg);
	if (!(val &PITA_INT0_STATUS))
		return IRQ_NONE; // other shared IRQ
	interrupt_ipacx(cs);      // handler for chip
	writeb(PITA_INT0_STATUS, cs->hw.diva.pci_cfg);  // Reset PLX interrupt
	return IRQ_HANDLED;
}

static void
diva_release(struct IsdnCardState *cs)
{
	del_timer_sync(&cs->hw.diva.tl);
	if (cs->hw.diva.cfg_reg)
		byteout(cs->hw.diva.ctrl, 0); /* LED off, Reset */

	hisax_release_resources(cs);
}

static void
diva_ipac_pci_release(struct IsdnCardState *cs)
{
	writel(0, cs->hw.diva.pci_cfg); /* disable INT0/1 */ 
	writel(2, cs->hw.diva.pci_cfg); /* reset pending INT0 */
	hisax_release_resources(cs);
}

static int
diva_ipac_isa_reset(struct IsdnCardState *cs)
{
	writereg(cs->hw.diva.isac_adr, cs->hw.diva.isac, IPAC_POTA2, 0x20);
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout((10*HZ)/1000);
	writereg(cs->hw.diva.isac_adr, cs->hw.diva.isac, IPAC_POTA2, 0x00);
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout((10*HZ)/1000);
	writereg(cs->hw.diva.isac_adr, cs->hw.diva.isac, IPAC_MASK, 0xc0);
	return 0;
}

static int
diva_ipac_pci_reset(struct IsdnCardState *cs)
{
	unsigned long misc_reg = cs->hw.diva.pci_cfg + PITA_MISC_REG;

	writel(PITA_PARA_SOFTRESET | PITA_PARA_MPX_MODE, misc_reg);
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout((10*HZ)/1000);
	writel(PITA_PARA_MPX_MODE, misc_reg);
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout((10*HZ)/1000);
	memwritereg(cs->hw.diva.cfg_reg, IPAC_MASK, 0xc0);
	return 0;
}

static int
diva_ipacx_pci_reset(struct IsdnCardState *cs)
{
	unsigned long misc_reg = cs->hw.diva.pci_cfg + PITA_MISC_REG;

	writel(PITA_PARA_SOFTRESET | PITA_PARA_MPX_MODE, misc_reg);
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout((10*HZ)/1000);
	writel(PITA_PARA_MPX_MODE | PITA_SER_SOFTRESET, misc_reg);
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout((10*HZ)/1000);
	ipacx_dc_write(cs, IPACX_MASK, 0xff); // Interrupts off
	return 0;
}

static int
diva_reset(struct IsdnCardState *cs)
{
	/* DIVA 2.0 */
	cs->hw.diva.ctrl_reg = 0;        /* Reset On */
	byteout(cs->hw.diva.ctrl, cs->hw.diva.ctrl_reg);
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout((10*HZ)/1000);
	cs->hw.diva.ctrl_reg |= DIVA_RESET;  /* Reset Off */
	byteout(cs->hw.diva.ctrl, cs->hw.diva.ctrl_reg);
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout((10*HZ)/1000);
	if (cs->subtyp == DIVA_ISA) {
		cs->hw.diva.ctrl_reg |= DIVA_ISA_LED_A;
	} else {
		/* Workaround PCI9060 */
		byteout(cs->hw.diva.pci_cfg + 0x69, 9);
		cs->hw.diva.ctrl_reg |= DIVA_PCI_LED_A;
	}
	byteout(cs->hw.diva.ctrl, cs->hw.diva.ctrl_reg);
	return 0;
}

static void
diva_led_handler(struct IsdnCardState *cs)
{
	int blink = 0;

	if (cs->status & 0x0001)
		cs->hw.diva.ctrl_reg |= (DIVA_ISA == cs->subtyp) ?
			DIVA_ISA_LED_A : DIVA_PCI_LED_A;
	else {
		cs->hw.diva.ctrl_reg ^= (DIVA_ISA == cs->subtyp) ?
			DIVA_ISA_LED_A : DIVA_PCI_LED_A;
		blink = 250;
	}
	if (cs->status & 0xf000)
		cs->hw.diva.ctrl_reg |= (DIVA_ISA == cs->subtyp) ?
			DIVA_ISA_LED_B : DIVA_PCI_LED_B;
	else if (cs->status & 0x0f00) {
		cs->hw.diva.ctrl_reg ^= (DIVA_ISA == cs->subtyp) ?
			DIVA_ISA_LED_B : DIVA_PCI_LED_B;
		blink = 500;
	} else
		cs->hw.diva.ctrl_reg &= ~((DIVA_ISA == cs->subtyp) ?
			DIVA_ISA_LED_B : DIVA_PCI_LED_B);

	byteout(cs->hw.diva.ctrl, cs->hw.diva.ctrl_reg);
	if (blink)
		mod_timer(&cs->hw.diva.tl, jiffies + (blink * HZ) / 1000);
}

static void
diva_ipacx_pci_init(struct IsdnCardState *cs)
{
	writel(PITA_INT0_ENABLE, cs->hw.diva.pci_cfg);
	init_ipacx(cs, 3); // init chip and enable interrupts
}

static void
diva_ipac_pci_init(struct IsdnCardState *cs)
{
	writel(PITA_INT0_ENABLE, cs->hw.diva.pci_cfg);
	ipac_init(cs);
}

static struct card_ops diva_ops = {
	.init        = inithscxisac,
	.reset       = diva_reset,
	.release     = diva_release,
	.led_handler = diva_led_handler,
	.irq_func    = diva_interrupt,
};

static struct card_ops diva_ipac_isa_ops = {
	.init        = ipac_init,
	.reset       = diva_ipac_isa_reset,
	.release     = hisax_release_resources,
	.irq_func    = ipac_irq,
};

static struct card_ops diva_ipac_pci_ops = {
	.init        = diva_ipac_pci_init,
	.reset       = diva_ipac_pci_reset,
	.release     = diva_ipac_pci_release,
	.irq_func    = diva_ipac_pci_irq,
};

static struct card_ops diva_ipacx_pci_ops = {
	.init        = diva_ipacx_pci_init,
	.reset       = diva_ipacx_pci_reset,
	.release     = diva_ipac_pci_release,
	.irq_func    = diva_ipacx_pci_irq,
};

static int __init
diva_ipac_probe(struct IsdnCardState *cs)
{
	u8 val;

	// request_io
	val = readreg(cs->hw.diva.cfg_reg + DIVA_IPAC_ADR,
		      cs->hw.diva.cfg_reg + DIVA_IPAC_DATA, IPAC_ID);
	printk(KERN_INFO "Diva: IPAC version %x\n", val);
	return (val == 1 || val == 2);
}

static int __init
diva_ipac_isa_probe(struct IsdnCardState *cs, struct IsdnCard *card)
{
	cs->subtyp = DIVA_IPAC_ISA;
	cs->irq = card->para[0];
	cs->hw.diva.cfg_reg  = card->para[1];
	cs->hw.diva.isac     = card->para[1] + DIVA_IPAC_DATA;
	cs->hw.diva.isac_adr = card->para[1] + DIVA_IPAC_ADR;
	printk(KERN_INFO "Diva: %s card configured at %#lx IRQ %d\n",
	       "IPAC ISA", cs->hw.diva.cfg_reg, cs->irq);
	if (!request_io(&cs->rs, cs->hw.diva.cfg_reg, 8, "diva isdn"))
		goto err;
	diva_ipac_isa_reset(cs);
	cs->card_ops = &diva_ipac_isa_ops;
	if (ipac_setup(cs, &ipac_dc_ops, &ipac_bc_ops))
		goto err;
	return 0;
 err:
	hisax_release_resources(cs);
	return -EBUSY;
}

static int __init
diva_isac_isa_probe(struct IsdnCardState *cs, struct IsdnCard *card)
{
	cs->subtyp = DIVA_ISA;
	cs->irq = card->para[0];
	cs->hw.diva.cfg_reg  = card->para[1];
	cs->hw.diva.ctrl     = card->para[1] + DIVA_ISA_CTRL;
	cs->hw.diva.isac     = card->para[1] + DIVA_ISA_ISAC_DATA;
	cs->hw.diva.hscx     = card->para[1] + DIVA_HSCX_DATA;
	cs->hw.diva.isac_adr = card->para[1] + DIVA_ISA_ISAC_ADR;
	cs->hw.diva.hscx_adr = card->para[1] + DIVA_HSCX_ADR;
	printk(KERN_INFO "Diva: %s card configured at %#lx IRQ %d\n",
	       "ISA", cs->hw.diva.cfg_reg, cs->irq);
	if (!request_io(&cs->rs, cs->hw.diva.cfg_reg, 8, "diva isdn"))
		goto err;
	diva_reset(cs);
	init_timer(&cs->hw.diva.tl);
	cs->hw.diva.tl.function = (void *) diva_led_handler;
	cs->hw.diva.tl.data = (long) cs;
	cs->card_ops = &diva_ops;
	if (hscxisac_setup(cs, &isac_ops, &hscx_ops))
		goto err;
	return 0;
 err:
	hisax_release_resources(cs);
	return -EBUSY;
}

static int __init
diva_isa_probe(struct IsdnCardState *cs, struct IsdnCard *card)
{
	int is_ipac;
	cs->hw.diva.cfg_reg  = card->para[1];
	if (!request_io(&cs->rs, cs->hw.diva.cfg_reg, 8, "diva isdn"))
		return -EBUSY;

	is_ipac = diva_ipac_probe(cs);
	hisax_release_resources(cs);

	if (is_ipac)
		return diva_ipac_isa_probe(cs, card);
	else
		return diva_isac_isa_probe(cs, card);
}

static int __init
diva_pci_probe(struct IsdnCardState *cs, struct pci_dev *pdev)
{
	if (pci_enable_device(pdev))
		goto err;

	cs->subtyp = DIVA_PCI;
	cs->irq = pdev->irq;
	cs->irq_flags |= SA_SHIRQ;
	cs->hw.diva.cfg_reg  = pci_resource_start(pdev, 2);
	cs->hw.diva.ctrl     = cs->hw.diva.cfg_reg + DIVA_PCI_CTRL;
	cs->hw.diva.isac     = cs->hw.diva.cfg_reg + DIVA_PCI_ISAC_DATA;
	cs->hw.diva.hscx     = cs->hw.diva.cfg_reg + DIVA_HSCX_DATA;
	cs->hw.diva.isac_adr = cs->hw.diva.cfg_reg + DIVA_PCI_ISAC_ADR;
	cs->hw.diva.hscx_adr = cs->hw.diva.cfg_reg + DIVA_HSCX_ADR;
	printk(KERN_INFO "Diva: %s card configured at %#lx IRQ %d\n",
	       "PCI", cs->hw.diva.cfg_reg, cs->irq);
	printk(KERN_INFO "Diva: %s space at %#lx\n",
	       "PCI", cs->hw.diva.pci_cfg);
	if (!request_io(&cs->rs, cs->hw.diva.cfg_reg, 32, "diva isdn"))
		goto err;
	return 0;
 err:
	hisax_release_resources(cs);
	return -EBUSY;
}

static int __init
diva_ipac_pci_probe(struct IsdnCardState *cs, struct pci_dev *pdev)
{
	if (pci_enable_device(pdev))
		goto err;

	cs->subtyp = DIVA_IPAC_PCI;
	cs->irq = pdev->irq;
	cs->irq_flags |= SA_SHIRQ;
	cs->hw.diva.pci_cfg = (unsigned long)request_mmio(
		&cs->rs, pci_resource_start(pdev, 0), 4096, "diva");
	cs->hw.diva.cfg_reg = (unsigned long)request_mmio(
		&cs->rs, pci_resource_start(pdev, 1), 4096, "diva");
	printk(KERN_INFO "Diva: %s card configured at %#lx IRQ %d\n",
	       "IPAC PCI", cs->hw.diva.cfg_reg, cs->irq);
	printk(KERN_INFO "Diva: %s space at %#lx\n",
	       "IPAC PCI", cs->hw.diva.pci_cfg);
	diva_ipac_pci_reset(cs);
	cs->card_ops = &diva_ipac_pci_ops;
	if (ipac_setup(cs, &mem_ipac_dc_ops, &mem_ipac_bc_ops))
		goto err;
	return 0;
 err:
	hisax_release_resources(cs);
	return -EBUSY;
}

static int __init
diva_ipacx_pci_probe(struct IsdnCardState *cs, struct pci_dev *pdev)
{
	if (pci_enable_device(pdev))
		goto err;

	cs->subtyp = DIVA_IPACX_PCI;
	cs->irq = pdev->irq;
	cs->irq_flags |= SA_SHIRQ;
	printk(KERN_INFO "Diva: %s card configured at %#lx IRQ %d\n",
	       "IPACX PCI", cs->hw.diva.cfg_reg, cs->irq);
	printk(KERN_INFO "Diva: %s space at %#lx\n",
	       "IPACX PCI", cs->hw.diva.pci_cfg);
	diva_ipacx_pci_reset(cs);
	cs->card_ops = &diva_ipacx_pci_ops;
	if (ipacx_setup(cs, &ipacx_dc_ops, &ipacx_bc_ops))
		goto err;
	return 0;
 err:
	hisax_release_resources(cs);
	return -EBUSY;
}

static struct pci_dev *dev_diva __initdata = NULL;
static struct pci_dev *dev_diva_u __initdata = NULL;
static struct pci_dev *dev_diva201 __initdata = NULL;
#ifdef __ISAPNP__
static struct isapnp_device_id diva_ids[] __initdata = {
	{ ISAPNP_VENDOR('G', 'D', 'I'), ISAPNP_FUNCTION(0x51),
	  ISAPNP_VENDOR('G', 'D', 'I'), ISAPNP_FUNCTION(0x51), 
	  (unsigned long) "Diva picola" },
	{ ISAPNP_VENDOR('G', 'D', 'I'), ISAPNP_FUNCTION(0x51),
	  ISAPNP_VENDOR('E', 'I', 'C'), ISAPNP_FUNCTION(0x51), 
	  (unsigned long) "Diva picola" },
	{ ISAPNP_VENDOR('G', 'D', 'I'), ISAPNP_FUNCTION(0x71),
	  ISAPNP_VENDOR('G', 'D', 'I'), ISAPNP_FUNCTION(0x71), 
	  (unsigned long) "Diva 2.0" },
	{ ISAPNP_VENDOR('G', 'D', 'I'), ISAPNP_FUNCTION(0x71),
	  ISAPNP_VENDOR('E', 'I', 'C'), ISAPNP_FUNCTION(0x71), 
	  (unsigned long) "Diva 2.0" },
	{ ISAPNP_VENDOR('G', 'D', 'I'), ISAPNP_FUNCTION(0xA1),
	  ISAPNP_VENDOR('G', 'D', 'I'), ISAPNP_FUNCTION(0xA1), 
	  (unsigned long) "Diva 2.01" },
	{ ISAPNP_VENDOR('G', 'D', 'I'), ISAPNP_FUNCTION(0xA1),
	  ISAPNP_VENDOR('E', 'I', 'C'), ISAPNP_FUNCTION(0xA1), 
	  (unsigned long) "Diva 2.01" },
	{ 0, }
};

static struct isapnp_device_id *pdev = &diva_ids[0];
static struct pnp_card *pnp_c __devinitdata = NULL;
#endif

int __init
setup_diva(struct IsdnCard *card)
{
	char tmp[64];

	strcpy(tmp, Diva_revision);
	printk(KERN_INFO "HiSax: Eicon.Diehl Diva driver Rev. %s\n", HiSax_getrev(tmp));
	if (card->para[1]) {
		if (diva_isa_probe(card->cs, card) < 0)
			return 0;
		return 1;
		
	}
#ifdef __ISAPNP__
	if (isapnp_present()) {
		struct pnp_card *pb;
		struct pnp_dev *pd;
		
		while(pdev->card_vendor) {
			if ((pb = pnp_find_card(pdev->card_vendor,
						pdev->card_device, pnp_c))) {
				pnp_c = pb;
				pd = NULL;
				if ((pd = pnp_find_dev(pnp_c,
						       pdev->vendor,
						       pdev->function,
						       pd))) {
					printk(KERN_INFO "HiSax: %s detected\n",
					       (char *)pdev->driver_data);
					if (pnp_device_attach(pd) < 0) {
						printk(KERN_ERR "Diva PnP: attach failed\n");
						return 0;
					}
					if (pnp_activate_dev(pd) < 0) {
						printk(KERN_ERR "Diva PnP: activate failed\n");
						pnp_device_detach(pd);
						return 0;
					}
					if (!pnp_irq_valid(pd, 0) || !pnp_port_valid(pd, 0)) {
						printk(KERN_ERR "Diva PnP:some resources are missing %ld/%lx\n",
						       pnp_irq(pd, 0), pnp_port_start(pd, 0));
						pnp_device_detach(pd);
						return(0);
					}
					card->para[1] = pnp_port_start(pd, 0);
					card->para[0] = pnp_irq(pd, 0);
					if (pdev->function == ISAPNP_FUNCTION(0xA1)) {
						if (diva_ipac_isa_probe(card->cs, card))
							return 0;
						return 1;
					} else {
						if (diva_isac_isa_probe(card->cs, card))
							return 0;
						return 1;
					}
				} else {
					printk(KERN_ERR "Diva PnP: PnP error card found, no device\n");
					return(0);
				}
				pdev++;
				pnp_c=NULL;
			} 
			if (!pdev->card_vendor) {
				printk(KERN_INFO "Diva PnP: no ISAPnP card found\n");
			}
		}
	}
#endif
#ifdef CONFIG_PCI
	if ((dev_diva = pci_find_device(PCI_VENDOR_ID_EICON,
					PCI_DEVICE_ID_EICON_DIVA20,
					dev_diva))) {
		if (diva_pci_probe(card->cs, dev_diva))
			return 0;
		return 1;
	} else if ((dev_diva_u = pci_find_device(PCI_VENDOR_ID_EICON,
						 PCI_DEVICE_ID_EICON_DIVA20_U,
						 dev_diva_u))) {
		if (diva_pci_probe(card->cs, dev_diva_u))
			return 0;
		return 1;
	} else if ((dev_diva201 = pci_find_device(PCI_VENDOR_ID_EICON,
						  PCI_DEVICE_ID_EICON_DIVA201,
						  dev_diva201))) {
		if (diva_ipac_pci_probe(card->cs, dev_diva201))
			return 0;
		return 1;
	}
	printk(KERN_WARNING "Diva: No PCI card found\n");
#endif /* CONFIG_PCI */
	return 0;
}
