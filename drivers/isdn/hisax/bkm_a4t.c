/* $Id: bkm_a4t.c,v 1.13.6.6 2001/09/23 22:24:46 kai Exp $
 *
 * low level stuff for T-Berkom A4T
 *
 * Author       Roland Klabunde
 * Copyright    by Roland Klabunde   <R.Klabunde@Berkom.de>
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include <linux/config.h>
#include <linux/init.h>
#include "hisax.h"
#include "isac.h"
#include "hscx.h"
#include "jade.h"
#include "isdnl1.h"
#include <linux/pci.h>
#include "bkm_ax.h"

extern const char *CardType[];
// FIXME needs per card lock
static spinlock_t bkm_a4t_lock = SPIN_LOCK_UNLOCKED;

const char *bkm_a4t_revision = "$Revision: 1.13.6.6 $";

static inline u8
readreg(unsigned int ale, unsigned long adr, u8 off)
{
	u_int ret;
	unsigned long flags;
	unsigned int *po = (unsigned int *) adr;	/* Postoffice */
	spin_lock_irqsave(&bkm_a4t_lock, flags);
	*po = (GCS_2 | PO_WRITE | off);
	__WAITI20__(po);
	*po = (ale | PO_READ);
	__WAITI20__(po);
	ret = *po;
	spin_unlock_irqrestore(&bkm_a4t_lock, flags);
	return ((unsigned char) ret);
}

static inline void
writereg(unsigned int ale, unsigned long adr, u8 off, u8 data)
{
	unsigned long flags;
	unsigned int *po = (unsigned int *) adr;	/* Postoffice */
	spin_lock_irqsave(&bkm_a4t_lock, flags);
	*po = (GCS_2 | PO_WRITE | off);
	__WAITI20__(po);
	*po = (ale | PO_WRITE | data);
	__WAITI20__(po);
	spin_unlock_irqrestore(&bkm_a4t_lock, flags);
}

static inline void
readfifo(unsigned int ale, unsigned long adr, u8 off, u8 * data, int size)
{
	int i;

	for (i = 0; i < size; i++)
		*data++ = readreg(ale, adr, off);
}

static inline void
writefifo(unsigned int ale, unsigned long adr, u8 off, u8 * data, int size)
{
	int i;

	for (i = 0; i < size; i++)
		writereg(ale, adr, off, *data++);
}

static u8
isac_read(struct IsdnCardState *cs, u8 offset)
{
	return (readreg(cs->hw.ax.isac_ale, cs->hw.ax.isac_adr, offset));
}

static void
isac_write(struct IsdnCardState *cs, u8 offset, u8 value)
{
	writereg(cs->hw.ax.isac_ale, cs->hw.ax.isac_adr, offset, value);
}

static void
isac_read_fifo(struct IsdnCardState *cs, u8 * data, int size)
{
	readfifo(cs->hw.ax.isac_ale, cs->hw.ax.isac_adr, 0, data, size);
}

static void
isac_write_fifo(struct IsdnCardState *cs, u8 * data, int size)
{
	writefifo(cs->hw.ax.isac_ale, cs->hw.ax.isac_adr, 0, data, size);
}

static struct dc_hw_ops isac_ops = {
	.read_reg   = isac_read,
	.write_reg  = isac_write,
	.read_fifo  = isac_read_fifo,
	.write_fifo = isac_write_fifo,
};

static u8
jade_read(struct IsdnCardState *cs, int jade, u8 offset)
{
	return readreg(cs->hw.ax.jade_ale, cs->hw.ax.jade_adr, offset + (jade == -1 ? 0 : (jade ? 0xC0 : 0x80)));
}

static void
jade_write(struct IsdnCardState *cs, int jade, u8 offset, u8 value)
{
	writereg(cs->hw.ax.jade_ale, cs->hw.ax.jade_adr, offset + (jade == -1 ? 0 : (jade ? 0xC0 : 0x80)), value);
}

static void
jade_read_fifo(struct IsdnCardState *cs, int hscx, u8 *data, int size)
{
	readfifo(cs->hw.ax.jade_ale, cs->hw.ax.jade_adr,
		 (hscx == -1 ? 0 : (hscx ? 0xc0 : 0x80)), data, size);
}

static void
jade_write_fifo(struct IsdnCardState *cs, int hscx, u8 *data, int size)
{
	writefifo(cs->hw.ax.jade_ale, cs->hw.ax.jade_adr,
		  (hscx == -1 ? 0 : (hscx ? 0xc0 : 0x80)), data, size);
}

static struct bc_hw_ops jade_ops = {
	.read_reg   = jade_read,
	.write_reg  = jade_write,
	.read_fifo  = jade_read_fifo,
	.write_fifo = jade_write_fifo,
};

static irqreturn_t
bkm_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
	struct IsdnCardState *cs = dev_id;
	u8 val = 0;
	I20_REGISTER_FILE *pI20_Regs;
	int handled = 0;

	spin_lock(&cs->lock);
	pI20_Regs = (I20_REGISTER_FILE *) (cs->hw.ax.base);

	/* ISDN interrupt pending? */
	if (pI20_Regs->i20IntStatus & intISDN) {
		handled = 1;
		/* Reset the ISDN interrupt     */
		pI20_Regs->i20IntStatus = intISDN;
		/* Disable ISDN interrupt */
		pI20_Regs->i20IntCtrl &= ~intISDN;
		/* Channel A first */
		val = readreg(cs->hw.ax.jade_ale, cs->hw.ax.jade_adr, jade_HDLC_ISR + 0x80);
		if (val) {
			jade_int_main(cs, val, 0);
		}
		/* Channel B  */
		val = readreg(cs->hw.ax.jade_ale, cs->hw.ax.jade_adr, jade_HDLC_ISR + 0xC0);
		if (val) {
			jade_int_main(cs, val, 1);
		}
		/* D-Channel */
		val = readreg(cs->hw.ax.isac_ale, cs->hw.ax.isac_adr, ISAC_ISTA);
		if (val) {
			isac_interrupt(cs, val);
		}
		/* Reenable ISDN interrupt */
		pI20_Regs->i20IntCtrl |= intISDN;
	}
	spin_unlock(&cs->lock);
	return IRQ_RETVAL(handled);
}

static void
enable_bkm_int(struct IsdnCardState *cs, unsigned bEnable)
{
	I20_REGISTER_FILE *pI20_Regs = (I20_REGISTER_FILE *) (cs->hw.ax.base);
	if (bEnable)
		pI20_Regs->i20IntCtrl |= (intISDN | intPCI);
	else
		/* CAUTION: This disables the video capture driver too */
		pI20_Regs->i20IntCtrl &= ~(intISDN | intPCI);
}

static void
reset_bkm(struct IsdnCardState *cs)
{
	I20_REGISTER_FILE *pI20_Regs = (I20_REGISTER_FILE *) (cs->hw.ax.base);
	/* Issue the I20 soft reset     */
	pI20_Regs->i20SysControl = 0xFF;	/* all in */
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout((10 * HZ) / 1000);
	/* Remove the soft reset */
	pI20_Regs->i20SysControl = sysRESET | 0xFF;
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout((10 * HZ) / 1000);
	/* Set our configuration */
	pI20_Regs->i20SysControl = sysRESET | sysCFG;
	/* Issue ISDN reset     */
	pI20_Regs->i20GuestControl = guestWAIT_CFG |
		g_A4T_JADE_RES |
		g_A4T_ISAR_RES |
		g_A4T_ISAC_RES |
		g_A4T_JADE_BOOTR |
		g_A4T_ISAR_BOOTR;
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout((10 * HZ) / 1000);

	/* Remove RESET state from ISDN */
	pI20_Regs->i20GuestControl &= ~(g_A4T_ISAC_RES |
					g_A4T_JADE_RES |
					g_A4T_ISAR_RES);
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout((10 * HZ) / 1000);
}

static void
bkm_a4t_init(struct IsdnCardState *cs)
{
	initisac(cs);
	initjade(cs);
	/* Enable ints */
	enable_bkm_int(cs, 1);
}

static int
bkm_a4t_reset(struct IsdnCardState *cs)
{
	/* Disable ints */
	enable_bkm_int(cs, 0);
	reset_bkm(cs);
	return 0;
}

static void
bkm_a4t_release(struct IsdnCardState *cs)
{
	reset_bkm(cs);
	hisax_release_resources(cs);
}

static struct card_ops bkm_a4t_ops = {
	.init     = bkm_a4t_init,
	.reset    = bkm_a4t_reset,
	.release  = bkm_a4t_release,
	.irq_func = bkm_interrupt,
};

static int __init
bkm_a4t_probe(struct IsdnCardState *cs, struct pci_dev *pdev)
{
	I20_REGISTER_FILE *pI20_Regs;
	int rc;

	printk(KERN_INFO "BKM A4T: defined at %#lx IRQ %u\n",
	       pci_resource_start(pdev, 0), pdev->irq);
	
	rc = -EBUSY;
	if (pci_enable_device(pdev))
		goto err;
			
	cs->irq = pdev->irq;
	cs->irq_flags |= SA_SHIRQ;
	cs->hw.avm.cfg_reg = pci_resource_start(pdev, 1);

	cs->hw.ax.base = (unsigned long)request_mmio(&cs->rs, pci_resource_start(pdev, 0), 4096, "Telekom A4T");
	if (!cs->hw.ax.base)
		goto err;
	
	/* Check suspicious address */
	// FIXME needs to use read[bl]
	pI20_Regs = (I20_REGISTER_FILE *) (cs->hw.ax.base);
	if ((pI20_Regs->i20IntStatus & 0x8EFFFFFF) != 0) {
		printk(KERN_WARNING "HiSax: address %lx suspicious\n",
		       cs->hw.ax.base);
		goto err;
	}
	cs->hw.ax.isac_adr = cs->hw.ax.base + PO_OFFSET;
	cs->hw.ax.jade_adr = cs->hw.ax.base + PO_OFFSET;
	cs->hw.ax.isac_ale = GCS_1;
	cs->hw.ax.jade_ale = GCS_3;

	reset_bkm(cs);
	cs->card_ops = &bkm_a4t_ops;
	isac_setup(cs, &isac_ops);
	jade_setup(cs, &jade_ops);
	return 0;

 err:
	hisax_release_resources(cs);
	return rc;
}

static struct pci_dev *dev_a4t __initdata = NULL;

int __init
setup_bkm_a4t(struct IsdnCard *card)
{
	char tmp[64];

	strcpy(tmp, bkm_a4t_revision);
	printk(KERN_INFO "HiSax: T-Berkom driver Rev. %s\n", HiSax_getrev(tmp));
	while ((dev_a4t = pci_find_device(PCI_VENDOR_ID_ZORAN,
		PCI_DEVICE_ID_ZORAN_36120, dev_a4t))) {
		u16 sub_sys;
		u16 sub_vendor;

		sub_vendor = dev_a4t->subsystem_vendor;
		sub_sys = dev_a4t->subsystem_device;
		if (sub_sys == PCI_DEVICE_ID_BERKOM_A4T && 
		    sub_vendor == PCI_VENDOR_ID_BERKOM) {
			if (bkm_a4t_probe(card->cs, dev_a4t))
				return 0;
			return 1;
		}
	}
	printk(KERN_WARNING "HiSax: %s: Card not found\n", CardType[card->typ]);
	return 0;
}
