/* $Id: t1pci.c,v 1.1.4.1.2.1 2001/12/21 15:00:17 kai Exp $
 * 
 * Module for AVM T1 PCI-card.
 * 
 * Copyright 1999 by Carsten Paeth <calle@calle.de>
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/capi.h>
#include <linux/init.h>
#include <asm/io.h>
#include <linux/isdn/capicmd.h>
#include <linux/isdn/capiutil.h>
#include <linux/isdn/capilli.h>
#include "avmcard.h"

static char *revision = "$Revision: 1.1.4.1.2.1 $";

#undef CONFIG_T1PCI_DEBUG
#undef CONFIG_T1PCI_POLLDEBUG

/* ------------------------------------------------------------- */

static struct pci_device_id t1pci_pci_tbl[] __devinitdata = {
	{ PCI_VENDOR_ID_AVM, PCI_DEVICE_ID_AVM_T1, PCI_ANY_ID, PCI_ANY_ID },
	{ }				/* Terminating entry */
};

MODULE_DEVICE_TABLE(pci, t1pci_pci_tbl);
MODULE_DESCRIPTION("CAPI4Linux: Driver for AVM T1 PCI card");
MODULE_AUTHOR("Carsten Paeth");
MODULE_LICENSE("GPL");

/* ------------------------------------------------------------- */

static int t1pci_add_card(struct capi_driver *driver,
                          struct capicardparams *p,
	                  struct pci_dev *dev)
{
	avmcard *card;
	avmctrl_info *cinfo;
	int retval;

	card = b1_alloc_card(1);
	if (!card) {
		printk(KERN_WARNING "%s: no memory.\n", driver->name);
		retval = -ENOMEM;
		goto err;
	}

        card->dma = avmcard_dma_alloc(driver->name, dev, 2048+128, 2048+128);
	if (!card->dma) {
		printk(KERN_WARNING "%s: no memory.\n", driver->name);
		retval = -ENOMEM;
		goto err_free;
	}

	cinfo = card->ctrlinfo;
	sprintf(card->name, "t1pci-%x", p->port);
	card->port = p->port;
	card->irq = p->irq;
	card->membase = p->membase;
	card->cardtype = avm_t1pci;

	if (!request_region(card->port, AVMB1_PORTLEN, card->name)) {
		printk(KERN_WARNING
		       "%s: ports 0x%03x-0x%03x in use.\n",
		       driver->name, card->port, card->port + AVMB1_PORTLEN);
		retval = -EBUSY;
		goto err_free_dma;
	}

	card->mbase = ioremap_nocache(card->membase, 64);
	if (!card->mbase) {
		printk(KERN_NOTICE "%s: can't remap memory at 0x%lx\n",
					driver->name, card->membase);
		retval = -EIO;
		goto err_release_region;
	}

	b1dma_reset(card);

	retval = t1pci_detect(card);
	if (retval != 0) {
		if (retval < 6)
			printk(KERN_NOTICE "%s: NO card at 0x%x (%d)\n",
					driver->name, card->port, retval);
		else
			printk(KERN_NOTICE "%s: card at 0x%x, but cabel not connected or T1 has no power (%d)\n",
					driver->name, card->port, retval);
		retval = -EIO;
		goto err_unmap;
	}
	b1dma_reset(card);

	retval = request_irq(card->irq, b1dma_interrupt, SA_SHIRQ, card->name, card);
	if (retval) {
		printk(KERN_ERR "%s: unable to get IRQ %d.\n",
				driver->name, card->irq);
		retval = -EBUSY;
		goto err_unmap;
	}

	cinfo->capi_ctrl = attach_capi_ctr(driver, card->name, cinfo);
	if (!cinfo->capi_ctrl) {
		printk(KERN_ERR "%s: attach controller failed.\n", driver->name);
		retval = -EBUSY;
		goto err_free_irq;
	}
	card->cardnr = cinfo->capi_ctrl->cnr;

	printk(KERN_INFO
		"%s: AVM T1 PCI at i/o %#x, irq %d, mem %#lx\n",
		driver->name, card->port, card->irq, card->membase);

	return 0;

 err_free_irq:
	free_irq(card->irq, card);
 err_unmap:
	iounmap(card->mbase);
 err_release_region:
	release_region(card->port, AVMB1_PORTLEN);
 err_free_dma:
	avmcard_dma_free(card->dma);
 err_free:
	b1_free_card(card);
 err:
	return retval;
}

/* ------------------------------------------------------------- */

static void t1pci_remove_ctr(struct capi_ctr *ctrl)
{
	avmctrl_info *cinfo = (avmctrl_info *)(ctrl->driverdata);
	avmcard *card = cinfo->card;

 	b1dma_reset(card);

	detach_capi_ctr(ctrl);
	free_irq(card->irq, card);
	iounmap(card->mbase);
	release_region(card->port, AVMB1_PORTLEN);
	ctrl->driverdata = 0;
	avmcard_dma_free(card->dma);
	b1_free_card(card);
}

/* ------------------------------------------------------------- */

static char *t1pci_procinfo(struct capi_ctr *ctrl)
{
	avmctrl_info *cinfo = (avmctrl_info *)(ctrl->driverdata);

	if (!cinfo)
		return "";
	sprintf(cinfo->infobuf, "%s %s 0x%x %d 0x%lx",
		cinfo->cardname[0] ? cinfo->cardname : "-",
		cinfo->version[VER_DRIVER] ? cinfo->version[VER_DRIVER] : "-",
		cinfo->card ? cinfo->card->port : 0x0,
		cinfo->card ? cinfo->card->irq : 0,
		cinfo->card ? cinfo->card->membase : 0
		);
	return cinfo->infobuf;
}

/* ------------------------------------------------------------- */

static struct capi_driver t1pci_driver = {
	owner: THIS_MODULE,
	name: "t1pci",
	revision: "0.0",
	load_firmware: b1dma_load_firmware,
	reset_ctr: b1dma_reset_ctr,
	remove_ctr: t1pci_remove_ctr,
	register_appl: b1dma_register_appl,
	release_appl: b1dma_release_appl,
	send_message: b1dma_send_message,
	
	procinfo: t1pci_procinfo,
	ctr_read_proc: b1dmactl_read_proc,
	driver_read_proc: 0,	/* use standard driver_read_proc */
	
	add_card: 0, /* no add_card function */
};

/* ------------------------------------------------------------- */

static int __devinit t1pci_probe(struct pci_dev *dev,
				 const struct pci_device_id *ent)
{
	struct capi_driver *driver = &t1pci_driver;
	struct capicardparams param;
	int retval;

	if (pci_enable_device(dev) < 0) {
		printk(KERN_ERR	"%s: failed to enable AVM-T1-PCI\n",
		       driver->name);
		return -ENODEV;
	}
	pci_set_master(dev);

	param.port = pci_resource_start(dev, 1);
	param.irq = dev->irq;
	param.membase = pci_resource_start(dev, 0);

	printk(KERN_INFO
	       "%s: PCI BIOS reports AVM-T1-PCI at i/o %#x, irq %d, mem %#x\n",
	       driver->name, param.port, param.irq, param.membase);

	retval = t1pci_add_card(driver, &param, dev);
	if (retval != 0) {
		printk(KERN_ERR
		       "%s: no AVM-T1-PCI at i/o %#x, irq %d detected, mem %#x\n",
		       driver->name, param.port, param.irq, param.membase);
		return -ENODEV;
	}
	return 0;
}

static struct pci_driver t1pci_pci_driver = {
       name:           "t1pci",
       id_table:       t1pci_pci_tbl,
       probe:          t1pci_probe,
};

static int __init t1pci_init(void)
{
	int retval;

	MOD_INC_USE_COUNT;

	b1_set_revision(&t1pci_driver, revision);
        attach_capi_driver(&t1pci_driver);

	retval = pci_register_driver(&t1pci_pci_driver);
	if (retval < 0)
		goto err;

	printk(KERN_INFO "%s: %d T1-PCI card(s) detected\n",
	       t1pci_driver.name, retval);
	retval = 0;
	goto out;

 err:
	detach_capi_driver(&t1pci_driver);
 out:
	MOD_DEC_USE_COUNT;
	return retval;
}

static void __exit t1pci_exit(void)
{
	pci_unregister_driver(&t1pci_pci_driver);
	detach_capi_driver(&t1pci_driver);
}

module_init(t1pci_init);
module_exit(t1pci_exit);
