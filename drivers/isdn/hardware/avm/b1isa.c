/* $Id: b1isa.c,v 1.10.6.6 2001/09/23 22:24:33 kai Exp $
 * 
 * Module for AVM B1 ISA-card.
 * 
 * Copyright 1999 by Carsten Paeth <calle@calle.de>
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/capi.h>
#include <linux/init.h>
#include <asm/io.h>
#include <linux/isdn/capicmd.h>
#include <linux/isdn/capiutil.h>
#include <linux/isdn/capilli.h>
#include "avmcard.h"

static char *revision = "$Revision: 1.10.6.6 $";

/* ------------------------------------------------------------- */

MODULE_DESCRIPTION("CAPI4Linux: Driver for AVM B1 ISA card");
MODULE_AUTHOR("Carsten Paeth");
MODULE_LICENSE("GPL");

/* ------------------------------------------------------------- */

static struct capi_driver_interface *di;

/* ------------------------------------------------------------- */

static void b1isa_remove_ctr(struct capi_ctr *ctrl)
{
	avmctrl_info *cinfo = (avmctrl_info *)(ctrl->driverdata);
	avmcard *card = cinfo->card;
	unsigned int port = card->port;

	b1_reset(port);
	b1_reset(port);

	di->detach_ctr(ctrl);
	free_irq(card->irq, card);
	release_region(card->port, AVMB1_PORTLEN);
	b1_free_card(card);

	MOD_DEC_USE_COUNT;
}

/* ------------------------------------------------------------- */

static int b1isa_add_card(struct capi_driver *driver, struct capicardparams *p)
{
	avmctrl_info *cinfo;
	avmcard *card;
	int retval;

	MOD_INC_USE_COUNT;

	card = b1_alloc_card(1);
	if (!card) {
		printk(KERN_WARNING "b1isa: no memory.\n");
		retval = -ENOMEM;
		goto err;
	}

	cinfo = card->ctrlinfo;

	sprintf(card->name, "b1isa-%x", p->port);
	card->port = p->port;
	card->irq = p->irq;
	card->cardtype = avm_b1isa;

	if (   card->port != 0x150 && card->port != 0x250
	    && card->port != 0x300 && card->port != 0x340) {
		printk(KERN_WARNING "b1isa: illegal port 0x%x.\n", card->port);
		retval = -EINVAL;
		goto err_free;
	}
	if (b1_irq_table[card->irq & 0xf] == 0) {
		printk(KERN_WARNING "b1isa: irq %d not valid.\n", card->irq);
		retval = -EINVAL;
		goto err_free;
	}
	if (!request_region(card->port, AVMB1_PORTLEN, card->name)) {
		printk(KERN_WARNING "b1isa: ports 0x%03x-0x%03x in use.\n",
		       card->port, card->port + AVMB1_PORTLEN);
		retval = -EBUSY;
		goto err_free;
	}
	retval = request_irq(card->irq, b1_interrupt, 0, card->name, card);
	if (retval) {
		printk(KERN_ERR "b1isa: unable to get IRQ %d.\n", card->irq);
		goto err_release_region;
	}
	b1_reset(card->port);
	if ((retval = b1_detect(card->port, card->cardtype)) != 0) {
		printk(KERN_NOTICE "b1isa: NO card at 0x%x (%d)\n",
		       card->port, retval);
		retval = -ENODEV;
		goto err_free_irq;
	}
	b1_reset(card->port);
	b1_getrevision(card);

	cinfo->capi_ctrl = di->attach_ctr(driver, card->name, cinfo);
	if (!cinfo->capi_ctrl) {
		printk(KERN_ERR "b1isa: attach controller failed.\n");
		retval = -EBUSY;
		goto err_free_irq;
	}

	printk(KERN_INFO
		"%s: AVM B1 ISA at i/o %#x, irq %d, revision %d\n",
		driver->name, card->port, card->irq, card->revision);

	return 0;

 err_free_irq:
	free_irq(card->irq, card);
 err_release_region:
	release_region(card->port, AVMB1_PORTLEN);
 err_free:
	b1_free_card(card);
 err:
	MOD_DEC_USE_COUNT;
	return retval;
}

static char *b1isa_procinfo(struct capi_ctr *ctrl)
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

static struct capi_driver b1isa_driver = {
	owner: THIS_MODULE,
	name: "b1isa",
	revision: "0.0",
	load_firmware: b1_load_firmware,
	reset_ctr: b1_reset_ctr,
	remove_ctr: b1isa_remove_ctr,
	register_appl: b1_register_appl,
	release_appl: b1_release_appl,
	send_message: b1_send_message,
	
	procinfo: b1isa_procinfo,
	ctr_read_proc: b1ctl_read_proc,
	driver_read_proc: 0,	/* use standard driver_read_proc */
	
	add_card: b1isa_add_card,
};

static int __init b1isa_init(void)
{
	struct capi_driver *driver = &b1isa_driver;
	char *p;
	int retval = 0;

	MOD_INC_USE_COUNT;

	if ((p = strchr(revision, ':')) != 0 && p[1]) {
		strncpy(driver->revision, p + 2, sizeof(driver->revision));
		driver->revision[sizeof(driver->revision)-1] = 0;
		if ((p = strchr(driver->revision, '$')) != 0 && p > driver->revision)
			*(p-1) = 0;
	}

	printk(KERN_INFO "%s: revision %s\n", driver->name, driver->revision);

        di = attach_capi_driver(driver);

	if (!di) {
		printk(KERN_ERR "%s: failed to attach capi_driver\n",
				driver->name);
		retval = -EIO;
	}
	MOD_DEC_USE_COUNT;
	return retval;
}

static void __exit b1isa_exit(void)
{
    detach_capi_driver(&b1isa_driver);
}

module_init(b1isa_init);
module_exit(b1isa_exit);
