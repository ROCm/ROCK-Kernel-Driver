/*
 * Driver for AVM Fritz!classic (ISA) ISDN card
 *
 * Author       Kai Germaschewski
 * Copyright    2001 by Kai Germaschewski  <kai.germaschewski@gmx.de>
 *              2001 by Karsten Keil       <keil@isdn4linux.de>
 * 
 * based upon Karsten Keil's original avm_a1.c driver
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */


#include <linux/version.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/isapnp.h>
#include <linux/kmod.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include "hisax_fcclassic.h"

// debugging cruft
#define __debug_variable debug
#include "hisax_debug.h"

#ifdef CONFIG_HISAX_DEBUG
static int debug = 0;
MODULE_PARM(debug, "i");
#endif

MODULE_AUTHOR("Kai Germaschewski <kai.germaschewski@gmx.de>/Karsten Keil <kkeil@suse.de>");
MODULE_DESCRIPTION("AVM Fritz!Card classic ISDN driver");

static int protocol = 2;       /* EURO-ISDN Default */
MODULE_PARM(protocol, "i");

// ----------------------------------------------------------------------

#define	 AVM_A1_STAT_ISAC	0x01
#define	 AVM_A1_STAT_HSCX	0x02
#define	 AVM_A1_STAT_TIMER	0x04

// ----------------------------------------------------------------------

static unsigned char
fcclassic_read_isac(struct isac *isac, unsigned char offset)
{
	struct fritz_adapter *adapter = isac->priv;
	unsigned char val;

	val = inb(adapter->isac_base + offset);
	DBG(0x1000, " port %#x, value %#x",
	    offset, val);
	return val;
}

static void
fcclassic_write_isac(struct isac *isac, unsigned char offset,
		     unsigned char value)
{
	struct fritz_adapter *adapter = isac->priv;

	DBG(0x1000, " port %#x, value %#x",
	    offset, value);
	outb(value, adapter->isac_base + offset);
}

static void
fcclassic_read_isac_fifo(struct isac *isac, unsigned char * data, int size)
{
	struct fritz_adapter *adapter = isac->priv;

	insb(adapter->isac_fifo, data, size);
}

static void
fcclassic_write_isac_fifo(struct isac *isac, unsigned char * data, int size)
{
	struct fritz_adapter *adapter = isac->priv;

	outsb(adapter->isac_fifo, data, size);
}

static u8
fcclassic_read_hscx(struct hscx *hscx, u8 offset)
{
	struct fritz_adapter *adapter = hscx->priv;

	return inb(adapter->hscx_base[hscx->channel] + offset);
}

static void
fcclassic_write_hscx(struct hscx *hscx, u8 offset, u8 value)
{
	struct fritz_adapter *adapter = hscx->priv;

	outb(value, adapter->hscx_base[hscx->channel] + offset);
}

static void
fcclassic_read_hscx_fifo(struct hscx *hscx, unsigned char * data, int size)
{
	struct fritz_adapter *adapter = hscx->priv;

	insb(adapter->hscx_fifo[hscx->channel], data, size);
}

static void
fcclassic_write_hscx_fifo(struct hscx *hscx, unsigned char * data, int size)
{
	struct fritz_adapter *adapter = hscx->priv;

	outsb(adapter->hscx_fifo[hscx->channel], data, size);
}

// ----------------------------------------------------------------------

static irqreturn_t
fcclassic_irq(int intno, void *dev, struct pt_regs *regs)
{
	struct fritz_adapter *adapter = dev;
	unsigned char sval;

	DBG(2, "");
	while ((sval = inb(adapter->cfg_reg) & 0xf) != 0x7) {
		DBG(2, "sval %#x", sval);
		if (!(sval & AVM_A1_STAT_TIMER)) {
			outb(0x1e, adapter->cfg_reg);
		}
		if (!(sval & AVM_A1_STAT_HSCX)) {
			hscx_irq(adapter->hscx);
		}
		if (!(sval & AVM_A1_STAT_ISAC)) {
			isac_irq(&adapter->isac);
		}
	}
	return IRQ_HANDLED;
}

// ----------------------------------------------------------------------

static int __init
fcclassic_setup(struct fritz_adapter *adapter)
{
	u32 val = 0;
	int i;
	int retval;

	DBG(1,"");

	isac_init(&adapter->isac); // FIXME is this okay now

	adapter->cfg_reg      = adapter->io + 0x1800;
	adapter->isac_base    = adapter->io + 0x1400 - 0x20;
	adapter->isac_fifo    = adapter->io + 0x1000;
	adapter->hscx_base[0] = adapter->io + 0x0400 - 0x20;
	adapter->hscx_fifo[0] = adapter->io;
	adapter->hscx_base[1] = adapter->io + 0x0c00 - 0x20;
	adapter->hscx_fifo[1] = adapter->io + 0x0800;

	retval = -EBUSY;
	if (!request_region(adapter->cfg_reg            ,  8,
			    "fcclassic cfg"))
		goto err;
	if (!request_region(adapter->isac_base + 0x20   , 32,
			    "fcclassic isac"))
		goto err_cfg_reg;
	if (!request_region(adapter->isac_fifo          ,  1,
			    "fcclassic isac fifo"))
		goto err_isac_base;
	if (!request_region(adapter->hscx_base[0] + 0x20, 32,
			    "fcclassic hscx"))
		goto err_isac_fifo;
	if (!request_region(adapter->hscx_fifo[0]       ,  1,
			    "fcclassic hscx fifo"))
		goto err_hscx_base_0;
	if (!request_region(adapter->hscx_base[1] + 0x20, 32,
			    "fcclassic hscx"))
		goto err_hscx_fifo_0;
	if (!request_region(adapter->hscx_fifo[1]       ,  1,
			    "fcclassic hscx fifo"))
		goto err_hscx_base_1;
	retval = request_irq(adapter->irq, fcclassic_irq,  0,
			     "fcclassic", adapter);
	if (retval)
		goto err_hscx_fifo_1;

	// Reset
	outb(0x00, adapter->cfg_reg);
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout(200 * HZ / 1000); // 200 msec
	outb(0x01, adapter->cfg_reg);
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout(200 * HZ / 1000); // 200 msec
	outb(0x00, adapter->cfg_reg);
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout(200 * HZ / 1000); // 200 msec

	val = adapter->irq;
	if (val == 9)
		val = 2;
	outb(val, adapter->cfg_reg + 1);
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout(200 * HZ / 1000); // 200 msec
	outb(0x00, adapter->cfg_reg);
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout(200 * HZ / 1000); // 200 msec

	val = inb(adapter->cfg_reg);
	printk(KERN_INFO "AVM A1: Byte at %x is %x\n",
	       adapter->cfg_reg, val);
	val = inb(adapter->cfg_reg + 3);
	printk(KERN_INFO "AVM A1: Byte at %x is %x\n",
	       adapter->cfg_reg + 3, val);
	val = inb(adapter->cfg_reg + 2);
	printk(KERN_INFO "AVM A1: Byte at %x is %x\n",
	       adapter->cfg_reg + 2, val);
	val = inb(adapter->cfg_reg);
	printk(KERN_INFO "AVM A1: Byte at %x is %x\n",
	       adapter->cfg_reg, val);

	outb(0x16, adapter->cfg_reg);
	outb(0x1e, adapter->cfg_reg);

	adapter->isac.priv            = adapter;
	adapter->isac.read_isac       = &fcclassic_read_isac;
	adapter->isac.write_isac      = &fcclassic_write_isac;
	adapter->isac.read_isac_fifo  = &fcclassic_read_isac_fifo;
	adapter->isac.write_isac_fifo = &fcclassic_write_isac_fifo;
	hisax_isac_setup(&adapter->isac);
	for (i = 0; i < 2; i++) {
		hscx_init(&adapter->hscx[i]);
		adapter->hscx[i].priv            = adapter;
		adapter->hscx[i].read_hscx       = &fcclassic_read_hscx;
		adapter->hscx[i].write_hscx      = &fcclassic_write_hscx;
		adapter->hscx[i].read_hscx_fifo  = &fcclassic_read_hscx_fifo;
		adapter->hscx[i].write_hscx_fifo = &fcclassic_write_hscx_fifo;
		hscx_setup(&adapter->hscx[i]);
	}

	return 0;

 err_hscx_fifo_1:
	release_region(adapter->hscx_fifo[1]       ,  1);
 err_hscx_base_1:
	release_region(adapter->hscx_base[1] + 0x20, 32);
 err_hscx_fifo_0:
	release_region(adapter->hscx_fifo[0]       ,  1);
 err_hscx_base_0:
	release_region(adapter->hscx_base[0] + 0x20, 32);
 err_isac_fifo:
	release_region(adapter->isac_fifo          ,  1);
 err_isac_base:
	release_region(adapter->isac_base    + 0x20, 32);
 err_cfg_reg:
	release_region(adapter->cfg_reg            ,  8);
 err:
	return retval;
}

static void __exit fcclassic_release(struct fritz_adapter *adapter)
{
	DBG(1,"");

//	outb(0, adapter->io + AVM_STATUS0);
	free_irq(adapter->irq, adapter);
	release_region(adapter->hscx_fifo[1]       ,  1);
	release_region(adapter->hscx_base[1] + 0x20, 32);
	release_region(adapter->hscx_fifo[0]       ,  1);
	release_region(adapter->hscx_base[0] + 0x20, 32);
	release_region(adapter->isac_fifo          ,  1);
	release_region(adapter->isac_base    + 0x20, 32);
	release_region(adapter->cfg_reg            ,  8);
}

// ----------------------------------------------------------------------

static struct fritz_adapter * __init 
new_adapter(struct pci_dev *pdev)
{
	struct fritz_adapter *adapter;
	struct hisax_b_if *b_if[2];
	int i;

	adapter = kmalloc(sizeof(struct fritz_adapter), GFP_KERNEL);
	if (!adapter)
		return NULL;

	memset(adapter, 0, sizeof(struct fritz_adapter));

	adapter->isac.hisax_d_if.owner = THIS_MODULE;
	adapter->isac.hisax_d_if.ifc.priv = &adapter->isac;
	adapter->isac.hisax_d_if.ifc.l2l1 = isac_d_l2l1;

	for (i = 0; i < 2; i++) {
	  //		adapter->hscx[i].adapter = adapter;
		adapter->hscx[i].channel = i;
		adapter->hscx[i].b_if.ifc.priv = &adapter->hscx[i];
		adapter->hscx[i].b_if.ifc.l2l1 = hscx_b_l2l1;
	}
	pci_set_drvdata(pdev, adapter);

	for (i = 0; i < 2; i++)
		b_if[i] = &adapter->hscx[i].b_if;

	hisax_register(&adapter->isac.hisax_d_if, b_if, "fcclassic", protocol);

	return adapter;
}

static void
delete_adapter(struct fritz_adapter *adapter)
{
	hisax_unregister(&adapter->isac.hisax_d_if);
	kfree(adapter);
}

static int __init
fcclassic_probe(struct pci_dev *pdev, const struct isapnp_device_id *ent)
{
	struct fritz_adapter *adapter;
	int retval;

	retval = -ENOMEM;
	adapter = new_adapter(pdev);
	if (!adapter)
		goto err;

	adapter->io = pdev->resource[0].start;
	adapter->irq = pdev->irq_resource[0].start;

	printk(KERN_INFO "hisax_fcclassic: found Fritz!Card classic at IO %#x irq %d\n",
	       adapter->io, adapter->irq);

	retval = fcclassic_setup(adapter);
	if (retval)
		goto err_free;

	return 0;
	
 err_free:
	delete_adapter(adapter);
 err:
	return retval;
}

static int __exit 
fcclassic_remove(struct pci_dev *pdev)
{
	struct fritz_adapter *adapter = pci_get_drvdata(pdev);

	fcclassic_release(adapter);
	delete_adapter(adapter);

	return 0;
}

static struct pci_dev isa_dev[4];

static int __init
hisax_fcclassic_init(void)
{
	printk(KERN_INFO "hisax_fcclassic: Fritz!Card classic ISDN driver v0.0.1\n");

	isa_dev[0].resource[0].start = 0x300;
	isa_dev[0].irq_resource[0].start = 7;

	fcclassic_probe(isa_dev, NULL);

	return 0;
}

static void __exit
hisax_fcclassic_exit(void)
{
	fcclassic_remove(isa_dev);
}

module_init(hisax_fcclassic_init);
module_exit(hisax_fcclassic_exit);
