/* $Id: b1pci.c,v 1.1.4.1.2.1 2001/12/21 15:00:17 kai Exp $
 * 
 * Module for AVM B1 PCI-card.
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
#include <asm/io.h>
#include <linux/init.h>
#include <linux/isdn/capicmd.h>
#include <linux/isdn/capiutil.h>
#include <linux/isdn/capilli.h>
#include "avmcard.h"

static char *revision = "$Revision: 1.1.4.1.2.1 $";

/* ------------------------------------------------------------- */

static struct pci_device_id b1pci_pci_tbl[] __devinitdata = {
	{ PCI_VENDOR_ID_AVM, PCI_DEVICE_ID_AVM_B1, PCI_ANY_ID, PCI_ANY_ID },
	{ }				/* Terminating entry */
};

MODULE_DEVICE_TABLE(pci, b1pci_pci_tbl);
MODULE_DESCRIPTION("CAPI4Linux: Driver for AVM B1 PCI card");
MODULE_AUTHOR("Carsten Paeth");
MODULE_LICENSE("GPL");

/* ------------------------------------------------------------- */

static char *b1pci_procinfo(struct capi_ctr *ctrl)
{
	avmctrl_info *cinfo = (avmctrl_info *)(ctrl->driverdata);

	if (!cinfo)
		return "";
	sprintf(cinfo->infobuf, "%s %s 0x%x %d r%d",
		cinfo->cardname[0] ? cinfo->cardname : "-",
		cinfo->version[VER_DRIVER] ? cinfo->version[VER_DRIVER] : "-",
		cinfo->card ? cinfo->card->port : 0x0,
		cinfo->card ? cinfo->card->irq : 0,
		cinfo->card ? cinfo->card->revision : 0
		);
	return cinfo->infobuf;
}

/* ------------------------------------------------------------- */

static int b1pci_probe(struct capi_driver *driver,
		       struct capicardparams *p,
		       struct pci_dev *pdev)
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

	cinfo = card->ctrlinfo;
	sprintf(card->name, "b1pci-%x", p->port);
	card->port = p->port;
	card->irq = p->irq;
	card->cardtype = avm_b1pci;
	
	if (!request_region(card->port, AVMB1_PORTLEN, card->name)) {
		printk(KERN_WARNING
		       "%s: ports 0x%03x-0x%03x in use.\n",
		       driver->name, card->port, card->port + AVMB1_PORTLEN);
		retval = -EBUSY;
		goto err_free;
	}
	b1_reset(card->port);
	retval = b1_detect(card->port, card->cardtype);
	if (retval) {
		printk(KERN_NOTICE "%s: NO card at 0x%x (%d)\n",
		       driver->name, card->port, retval);
		retval = -ENODEV;
		goto err_release_region;
	}
	b1_reset(card->port);
	b1_getrevision(card);
	
	retval = request_irq(card->irq, b1_interrupt, SA_SHIRQ, card->name, card);
	if (retval) {
		printk(KERN_ERR "%s: unable to get IRQ %d.\n",
		       driver->name, card->irq);
		retval = -EBUSY;
		goto err_release_region;
	}
	
	cinfo->capi_ctrl = attach_capi_ctr(driver, card->name, cinfo);
	if (!cinfo->capi_ctrl) {
		printk(KERN_ERR "%s: attach controller failed.\n",
		       driver->name);
		retval = -EBUSY;
		goto err_free_irq;
	}

	if (card->revision >= 4) {
		printk(KERN_INFO
			"%s: AVM B1 PCI V4 at i/o %#x, irq %d, revision %d (no dma)\n",
			driver->name, card->port, card->irq, card->revision);
	} else {
		printk(KERN_INFO
			"%s: AVM B1 PCI at i/o %#x, irq %d, revision %d\n",
			driver->name, card->port, card->irq, card->revision);
	}

	pci_set_drvdata(pdev, card);
	return 0;

 err_free_irq:
	free_irq(card->irq, card);
 err_release_region:
	release_region(card->port, AVMB1_PORTLEN);
 err_free:
	b1_free_card(card);
 err:
	return retval;
}

static void b1pci_remove(struct pci_dev *pdev)
{
	avmcard *card = pci_get_drvdata(pdev);
	avmctrl_info *cinfo = card->ctrlinfo;
	unsigned int port = card->port;

	b1_reset(port);
	b1_reset(port);

	detach_capi_ctr(cinfo->capi_ctrl);
	free_irq(card->irq, card);
	release_region(card->port, AVMB1_PORTLEN);
	b1_free_card(card);
}

/* ------------------------------------------------------------- */

static struct capi_driver b1pci_driver = {
	owner: THIS_MODULE,
	name: "b1pci",
	revision: "0.0",
	load_firmware: b1_load_firmware,
	reset_ctr: b1_reset_ctr,
	register_appl: b1_register_appl,
	release_appl: b1_release_appl,
	send_message: b1_send_message,
	
	procinfo: b1pci_procinfo,
	ctr_read_proc: b1ctl_read_proc,
	driver_read_proc: 0,	/* use standard driver_read_proc */
};

#ifdef CONFIG_ISDN_DRV_AVMB1_B1PCIV4
/* ------------------------------------------------------------- */

static char *b1pciv4_procinfo(struct capi_ctr *ctrl)
{
	avmctrl_info *cinfo = (avmctrl_info *)(ctrl->driverdata);

	if (!cinfo)
		return "";
	sprintf(cinfo->infobuf, "%s %s 0x%x %d 0x%lx r%d",
		cinfo->cardname[0] ? cinfo->cardname : "-",
		cinfo->version[VER_DRIVER] ? cinfo->version[VER_DRIVER] : "-",
		cinfo->card ? cinfo->card->port : 0x0,
		cinfo->card ? cinfo->card->irq : 0,
		cinfo->card ? cinfo->card->membase : 0,
		cinfo->card ? cinfo->card->revision : 0
		);
	return cinfo->infobuf;
}

/* ------------------------------------------------------------- */

static int b1pciv4_probe(struct capi_driver *driver,
			 struct capicardparams *p,
			 struct pci_dev *pdev)
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

        card->dma = avmcard_dma_alloc(driver->name, pdev, 2048+128, 2048+128);
	if (!card->dma) {
		printk(KERN_WARNING "%s: dma alloc.\n", driver->name);
		retval = -ENOMEM;
		goto err_free;
	}

	cinfo = card->ctrlinfo;
	sprintf(card->name, "b1pciv4-%x", p->port);
	card->port = p->port;
	card->irq = p->irq;
	card->membase = p->membase;
	card->cardtype = avm_b1pci;

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
		retval = -ENOMEM;
		goto err_release_region;
	}

	b1dma_reset(card);

	retval = b1pciv4_detect(card);
	if (retval) {
		printk(KERN_NOTICE "%s: NO card at 0x%x (%d)\n",
					driver->name, card->port, retval);
		retval = -ENODEV;
		goto err_unmap;
	}
	b1dma_reset(card);
	b1_getrevision(card);

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
		"%s: AVM B1 PCI V4 at i/o %#x, irq %d, mem %#lx, revision %d (dma)\n",
		driver->name, card->port, card->irq,
		card->membase, card->revision);

	pci_set_drvdata(pdev, card);
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

static void b1pciv4_remove(struct pci_dev *pdev)
{
	avmcard *card = pci_get_drvdata(pdev);
	avmctrl_info *cinfo = card->ctrlinfo;

 	b1dma_reset(card);

	detach_capi_ctr(cinfo->capi_ctrl);
	free_irq(card->irq, card);
	iounmap(card->mbase);
	release_region(card->port, AVMB1_PORTLEN);
        avmcard_dma_free(card->dma);
	b1_free_card(card);
}

/* ------------------------------------------------------------- */


static struct capi_driver b1pciv4_driver = {
	owner: THIS_MODULE,
	name: "b1pciv4",
	revision: "0.0",
	load_firmware: b1dma_load_firmware,
	reset_ctr: b1dma_reset_ctr,
	register_appl: b1dma_register_appl,
	release_appl: b1dma_release_appl,
	send_message: b1dma_send_message,
	
	procinfo: b1pciv4_procinfo,
	ctr_read_proc: b1dmactl_read_proc,
	driver_read_proc: 0,	/* use standard driver_read_proc */
};

#endif /* CONFIG_ISDN_DRV_AVMB1_B1PCIV4 */

static int __devinit b1pci_pci_probe(struct pci_dev *pdev,
				     const struct pci_device_id *ent)
{
	struct capi_driver *driver = &b1pci_driver;
	struct capicardparams param;
	int retval;

	if (pci_enable_device(pdev) < 0) {
		printk(KERN_ERR "%s: failed to enable AVM-B1\n",
		       driver->name);
		return -ENODEV;
	}
	param.irq = pdev->irq;

	if (pci_resource_start(pdev, 2)) { /* B1 PCI V4 */
#ifdef CONFIG_ISDN_DRV_AVMB1_B1PCIV4
		driver = &b1pciv4_driver;

		pci_set_master(pdev);
#endif
		param.membase = pci_resource_start(pdev, 0);
		param.port = pci_resource_start(pdev, 2);

		printk(KERN_INFO
		"%s: PCI BIOS reports AVM-B1 V4 at i/o %#x, irq %d, mem %#x\n",
		driver->name, param.port, param.irq, param.membase);
#ifdef CONFIG_ISDN_DRV_AVMB1_B1PCIV4
		retval = b1pciv4_probe(driver, &param, pdev);
#else
		retval = b1pci_probe(driver, &param, pdev);
#endif
		if (retval != 0) {
		        printk(KERN_ERR
			"%s: no AVM-B1 V4 at i/o %#x, irq %d, mem %#x detected\n",
			driver->name, param.port, param.irq, param.membase);
		}
	} else {
		param.membase = 0;
		param.port = pci_resource_start(pdev, 1);

		printk(KERN_INFO
		"%s: PCI BIOS reports AVM-B1 at i/o %#x, irq %d\n",
		driver->name, param.port, param.irq);
		retval = b1pci_probe(driver, &param, pdev);
		if (retval != 0) {
		        printk(KERN_ERR
			"%s: no AVM-B1 at i/o %#x, irq %d detected\n",
			driver->name, param.port, param.irq);
		}
	}
	return retval;
}

static void __devexit b1pci_pci_remove(struct pci_dev *pdev)
{
	avmcard *card = pci_get_drvdata(pdev);

	if (card->dma)
		b1pciv4_remove(pdev);
	else
		b1pci_remove(pdev);
}

static struct pci_driver b1pci_pci_driver = {
	name:		"b1pci",
	id_table:	b1pci_pci_tbl,
	probe:		b1pci_pci_probe,
	remove:		__devexit_p(b1pci_pci_remove),
};

static int __init b1pci_init(void)
{
	int retval;

	MOD_INC_USE_COUNT;

	b1_set_revision(&b1pci_driver, revision);
        attach_capi_driver(&b1pci_driver);

#ifdef CONFIG_ISDN_DRV_AVMB1_B1PCIV4
	b1_set_revision(&b1pciv4_driver, revision);
        attach_capi_driver(&b1pciv4_driver);
#endif

	retval = pci_module_init(&b1pci_pci_driver);
	if (retval < 0) 
		goto err;

	printk(KERN_INFO "%s: %d B1-PCI card(s) detected\n",
	       b1pci_driver.name, retval);
	retval = 0;
	goto out;

 err:
	detach_capi_driver(&b1pci_driver);
#ifdef CONFIG_ISDN_DRV_AVMB1_B1PCIV4
	detach_capi_driver(&b1pciv4_driver);
#endif
 out:
	MOD_DEC_USE_COUNT;
	return retval;
}

static void __exit b1pci_exit(void)
{
	pci_unregister_driver(&b1pci_pci_driver);

	detach_capi_driver(&b1pci_driver);
#ifdef CONFIG_ISDN_DRV_AVMB1_B1PCIV4
	detach_capi_driver(&b1pciv4_driver);
#endif
}

module_init(b1pci_init);
module_exit(b1pci_exit);
