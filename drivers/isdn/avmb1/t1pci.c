/*
 * $Id: t1pci.c,v 1.13.6.1 2000/11/28 12:02:45 kai Exp $
 * 
 * Module for AVM T1 PCI-card.
 * 
 * (c) Copyright 1999 by Carsten Paeth (calle@calle.in-berlin.de)
 * 
 * $Log: t1pci.c,v $
 * Revision 1.13.6.1  2000/11/28 12:02:45  kai
 * MODULE_DEVICE_TABLE for 2.4
 *
 * Revision 1.13.2.2  2000/11/26 17:47:53  kai
 * added PCI_DEV_TABLE for 2.4
 *
 * Revision 1.13.2.1  2000/11/26 17:14:19  kai
 * fix device ids
 * also needs patches to include/linux/pci_ids.h
 *
 * Revision 1.13  2000/11/23 20:45:14  kai
 * fixed module_init/exit stuff
 * Note: compiled-in kernel doesn't work pre 2.2.18 anymore.
 *
 * Revision 1.12  2000/11/01 14:05:02  calle
 * - use module_init/module_exit from linux/init.h.
 * - all static struct variables are initialized with "membername:" now.
 * - avm_cs.c, let it work with newer pcmcia-cs.
 *
 * Revision 1.11  2000/08/08 09:24:19  calle
 * calls to pci_enable_device surounded by #ifndef COMPAT_HAS_2_2_PCI
 *
 * Revision 1.10  2000/07/20 10:21:21  calle
 * Bugfix: driver will not be unregistered, if not cards were detected.
 *         this result in an oops in kcapi.c
 *
 * Revision 1.9  2000/05/19 15:43:22  calle
 * added calls to pci_device_start().
 *
 * Revision 1.8  2000/05/06 00:52:36  kai
 * merged changes from kernel tree
 * fixed timer and net_device->name breakage
 *
 * Revision 1.7  2000/04/07 15:26:55  calle
 * better error message if cabel not connected or T1 has no power.
 *
 * Revision 1.6  2000/04/03 13:29:25  calle
 * make Tim Waugh happy (module unload races in 2.3.99-pre3).
 * no real problem there, but now it is much cleaner ...
 *
 * Revision 1.5  2000/02/02 18:36:04  calle
 * - Modules are now locked while init_module is running
 * - fixed problem with memory mapping if address is not aligned
 *
 * Revision 1.4  2000/01/25 14:33:38  calle
 * - Added Support AVM B1 PCI V4.0 (tested with prototype)
 *   - splitted up t1pci.c into b1dma.c for common function with b1pciv4
 *   - support for revision register
 *
 * Revision 1.3  1999/11/13 21:27:16  keil
 * remove KERNELVERSION
 *
 * Revision 1.2  1999/11/05 16:38:02  calle
 * Cleanups before kernel 2.4:
 * - Changed all messages to use card->name or driver->name instead of
 *   constant string.
 * - Moved some data from struct avmcard into new struct avmctrl_info.
 *   Changed all lowlevel capi driver to match the new structur.
 *
 * Revision 1.1  1999/10/26 15:31:42  calle
 * Added driver for T1-PCI card.
 *
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
#include "capicmd.h"
#include "capiutil.h"
#include "capilli.h"
#include "avmcard.h"

static char *revision = "$Revision: 1.13.6.1 $";

#undef CONFIG_T1PCI_DEBUG
#undef CONFIG_T1PCI_POLLDEBUG

/* ------------------------------------------------------------- */

static struct pci_device_id t1pci_pci_tbl[] __initdata = {
	{ PCI_VENDOR_ID_AVM, PCI_DEVICE_ID_AVM_T1, PCI_ANY_ID, PCI_ANY_ID },
	{ }				/* Terminating entry */
};

MODULE_DEVICE_TABLE(pci, t1pci_pci_tbl);
MODULE_AUTHOR("Carsten Paeth <calle@calle.in-berlin.de>");

/* ------------------------------------------------------------- */

static struct capi_driver_interface *di;

/* ------------------------------------------------------------- */

static void t1pci_remove_ctr(struct capi_ctr *ctrl)
{
	avmctrl_info *cinfo = (avmctrl_info *)(ctrl->driverdata);
	avmcard *card = cinfo->card;

 	b1dma_reset(card);

	di->detach_ctr(ctrl);
	free_irq(card->irq, card);
	iounmap((void *) (((unsigned long) card->mbase) & PAGE_MASK));
	release_region(card->port, AVMB1_PORTLEN);
	ctrl->driverdata = 0;
	kfree(card->ctrlinfo);
	kfree(card->dma);
	kfree(card);

	MOD_DEC_USE_COUNT;
}

/* ------------------------------------------------------------- */

static int t1pci_add_card(struct capi_driver *driver, struct capicardparams *p)
{
	unsigned long base, page_offset;
	avmcard *card;
	avmctrl_info *cinfo;
	int retval;

	MOD_INC_USE_COUNT;

	card = (avmcard *) kmalloc(sizeof(avmcard), GFP_ATOMIC);

	if (!card) {
		printk(KERN_WARNING "%s: no memory.\n", driver->name);
	        MOD_DEC_USE_COUNT;
		return -ENOMEM;
	}
	memset(card, 0, sizeof(avmcard));
	card->dma = (avmcard_dmainfo *) kmalloc(sizeof(avmcard_dmainfo), GFP_ATOMIC);
	if (!card->dma) {
		printk(KERN_WARNING "%s: no memory.\n", driver->name);
		kfree(card);
	        MOD_DEC_USE_COUNT;
		return -ENOMEM;
	}
	memset(card->dma, 0, sizeof(avmcard_dmainfo));
        cinfo = (avmctrl_info *) kmalloc(sizeof(avmctrl_info), GFP_ATOMIC);
	if (!cinfo) {
		printk(KERN_WARNING "%s: no memory.\n", driver->name);
		kfree(card->dma);
		kfree(card);
	        MOD_DEC_USE_COUNT;
		return -ENOMEM;
	}
	memset(cinfo, 0, sizeof(avmctrl_info));
	card->ctrlinfo = cinfo;
	cinfo->card = card;
	sprintf(card->name, "t1pci-%x", p->port);
	card->port = p->port;
	card->irq = p->irq;
	card->membase = p->membase;
	card->cardtype = avm_t1pci;

	if (check_region(card->port, AVMB1_PORTLEN)) {
		printk(KERN_WARNING
		       "%s: ports 0x%03x-0x%03x in use.\n",
		       driver->name, card->port, card->port + AVMB1_PORTLEN);
	        kfree(card->ctrlinfo);
		kfree(card->dma);
		kfree(card);
	        MOD_DEC_USE_COUNT;
		return -EBUSY;
	}

	base = card->membase & PAGE_MASK;
	page_offset = card->membase - base;
	card->mbase = ioremap_nocache(base, page_offset + 64);
	if (card->mbase) {
		card->mbase += page_offset;
	} else {
		printk(KERN_NOTICE "%s: can't remap memory at 0x%lx\n",
					driver->name, card->membase);
	        kfree(card->ctrlinfo);
		kfree(card->dma);
		kfree(card);
	        MOD_DEC_USE_COUNT;
		return -EIO;
	}

	b1dma_reset(card);

	if ((retval = t1pci_detect(card)) != 0) {
		if (retval < 6)
			printk(KERN_NOTICE "%s: NO card at 0x%x (%d)\n",
					driver->name, card->port, retval);
		else
			printk(KERN_NOTICE "%s: card at 0x%x, but cabel not connected or T1 has no power (%d)\n",
					driver->name, card->port, retval);
                iounmap((void *) (((unsigned long) card->mbase) & PAGE_MASK));
	        kfree(card->ctrlinfo);
		kfree(card->dma);
		kfree(card);
	        MOD_DEC_USE_COUNT;
		return -EIO;
	}
	b1dma_reset(card);

	request_region(p->port, AVMB1_PORTLEN, card->name);

	retval = request_irq(card->irq, b1dma_interrupt, SA_SHIRQ, card->name, card);
	if (retval) {
		printk(KERN_ERR "%s: unable to get IRQ %d.\n",
				driver->name, card->irq);
                iounmap((void *) (((unsigned long) card->mbase) & PAGE_MASK));
		release_region(card->port, AVMB1_PORTLEN);
	        kfree(card->ctrlinfo);
		kfree(card->dma);
		kfree(card);
	        MOD_DEC_USE_COUNT;
		return -EBUSY;
	}

	cinfo->capi_ctrl = di->attach_ctr(driver, card->name, cinfo);
	if (!cinfo->capi_ctrl) {
		printk(KERN_ERR "%s: attach controller failed.\n", driver->name);
                iounmap((void *) (((unsigned long) card->mbase) & PAGE_MASK));
		free_irq(card->irq, card);
		release_region(card->port, AVMB1_PORTLEN);
	        kfree(card->ctrlinfo);
		kfree(card->dma);
		kfree(card);
	        MOD_DEC_USE_COUNT;
		return -EBUSY;
	}
	card->cardnr = cinfo->capi_ctrl->cnr;

	skb_queue_head_init(&card->dma->send_queue);

	printk(KERN_INFO
		"%s: AVM T1 PCI at i/o %#x, irq %d, mem %#lx\n",
		driver->name, card->port, card->irq, card->membase);

	return 0;
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

static int ncards = 0;

static int __init t1pci_init(void)
{
	struct capi_driver *driver = &t1pci_driver;
	struct pci_dev *dev = NULL;
	char *p;
	int retval;

	MOD_INC_USE_COUNT;

	if ((p = strchr(revision, ':'))) {
		strncpy(driver->revision, p + 1, sizeof(driver->revision));
		p = strchr(driver->revision, '$');
		*p = 0;
	}

	printk(KERN_INFO "%s: revision %s\n", driver->name, driver->revision);

        di = attach_capi_driver(driver);

	if (!di) {
		printk(KERN_ERR "%s: failed to attach capi_driver\n",
				driver->name);
		MOD_DEC_USE_COUNT;
		return -EIO;
	}

#ifdef CONFIG_PCI
	if (!pci_present()) {
		printk(KERN_ERR "%s: no PCI bus present\n", driver->name);
    		detach_capi_driver(driver);
		MOD_DEC_USE_COUNT;
		return -EIO;
	}

	while ((dev = pci_find_device(PCI_VENDOR_ID_AVM, PCI_DEVICE_ID_AVM_T1, dev))) {
		struct capicardparams param;

		param.port = pci_resource_start(dev, 1);
 		param.irq = dev->irq;
		param.membase = pci_resource_start(dev, 0);

		retval = pci_enable_device (dev);
		if (retval != 0) {
		        printk(KERN_ERR
			"%s: failed to enable AVM-T1-PCI at i/o %#x, irq %d, mem %#x err=%d\n",
			driver->name, param.port, param.irq, param.membase, retval);
    			detach_capi_driver(&t1pci_driver);
			MOD_DEC_USE_COUNT;
			return -EIO;
		}

		printk(KERN_INFO
			"%s: PCI BIOS reports AVM-T1-PCI at i/o %#x, irq %d, mem %#x\n",
			driver->name, param.port, param.irq, param.membase);
		retval = t1pci_add_card(driver, &param);
		if (retval != 0) {
		        printk(KERN_ERR
			"%s: no AVM-T1-PCI at i/o %#x, irq %d detected, mem %#x\n",
			driver->name, param.port, param.irq, param.membase);
    			detach_capi_driver(&t1pci_driver);
			MOD_DEC_USE_COUNT;
			return retval;
		}
		ncards++;
	}
	if (ncards) {
		printk(KERN_INFO "%s: %d T1-PCI card(s) detected\n",
				driver->name, ncards);
		MOD_DEC_USE_COUNT;
		return 0;
	}
	printk(KERN_ERR "%s: NO T1-PCI card detected\n", driver->name);
	detach_capi_driver(&t1pci_driver);
	MOD_DEC_USE_COUNT;
	return -ESRCH;
#else
	printk(KERN_ERR "%s: kernel not compiled with PCI.\n", driver->name);
	MOD_DEC_USE_COUNT;
	return -EIO;
#endif
}

static void __exit t1pci_exit(void)
{
    detach_capi_driver(&t1pci_driver);
}

module_init(t1pci_init);
module_exit(t1pci_exit);
