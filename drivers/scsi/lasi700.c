/* -*- mode: c; c-basic-offset: 8 -*- */

/* PARISC LASI driver for the 53c700 chip
 *
 * Copyright (C) 2001 by James.Bottomley@HansenPartnership.com
**-----------------------------------------------------------------------------
**  
**  This program is free software; you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation; either version 2 of the License, or
**  (at your option) any later version.
**
**  This program is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**  GNU General Public License for more details.
**
**  You should have received a copy of the GNU General Public License
**  along with this program; if not, write to the Free Software
**  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
**-----------------------------------------------------------------------------
 */

/*
 * Many thanks to Richard Hirst <rhirst@linuxcare.com> for patiently
 * debugging this driver on the parisc architecture and suggesting
 * many improvements and bug fixes.
 *
 * Thanks also go to Linuxcare Inc. for providing several PARISC
 * machines for me to debug the driver on.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/stat.h>
#include <linux/mm.h>
#include <linux/blkdev.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/dma-mapping.h>

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/hardware.h>
#include <asm/parisc-device.h>
#include <asm/delay.h>

#include "scsi.h"
#include "hosts.h"

#include "lasi700.h"
#include "53c700.h"

MODULE_AUTHOR("James Bottomley");
MODULE_DESCRIPTION("lasi700 SCSI Driver");
MODULE_LICENSE("GPL");

static struct parisc_device_id lasi700_ids[] = {
	LASI700_ID_TABLE,
	LASI710_ID_TABLE,
	{ 0 }
};

static Scsi_Host_Template lasi700_template = {
	.name		= "LASI SCSI 53c700",
	.proc_name	= "lasi700",
	.this_id	= 7,
	.module		= THIS_MODULE,
};
MODULE_DEVICE_TABLE(parisc, lasi700_ids);

static int __init
lasi700_probe(struct parisc_device *dev)
{
	unsigned long base = dev->hpa + LASI_SCSI_CORE_OFFSET;
	struct NCR_700_Host_Parameters *hostdata;
	struct Scsi_Host *host;

	hostdata = kmalloc(sizeof(*hostdata), GFP_KERNEL);
	if (!hostdata) {
		printk(KERN_ERR "%s: Failed to allocate host data\n",
		       dev->dev.bus_id);
		return -ENOMEM;
	}
	memset(hostdata, 0, sizeof(struct NCR_700_Host_Parameters));

	hostdata->dev = &dev->dev;
	dma_set_mask(&dev->dev, 0xffffffffUL);
	hostdata->base = base;
	hostdata->differential = 0;

	if (dev->id.sversion == LASI_700_SVERSION) {
		hostdata->clock = LASI700_CLOCK;
		hostdata->force_le_on_be = 1;
	} else {
		hostdata->clock = LASI710_CLOCK;
		hostdata->force_le_on_be = 0;
		hostdata->chip710 = 1;
		hostdata->dmode_extra = DMODE_FC2;
	}

	NCR_700_set_mem_mapped(hostdata);

	host = NCR_700_detect(&lasi700_template, hostdata);
	if (!host)
		goto out_kfree;

	host->irq = dev->irq;
	if (request_irq(dev->irq, NCR_700_intr, SA_SHIRQ,
				dev->dev.bus_id, host)) {
		printk(KERN_ERR "%s: irq problem, detaching\n",
		       dev->dev.bus_id);
		goto out_put_host;
	}

	if (scsi_add_host(host, &dev->dev))
		goto out_free_irq;
	dev_set_drvdata(&dev->dev, host);
	scsi_scan_host(host);

	return 0;

 out_free_irq:
	free_irq(host->irq, host);
 out_put_host:
	scsi_host_put(host);
 out_kfree:
	kfree(hostdata);
	return -ENODEV;
}

static int __exit
lasi700_driver_remove(struct parisc_device *dev)
{
	struct Scsi_Host *host = dev_get_drvdata(&dev->dev);
	struct NCR_700_Host_Parameters *hostdata = 
		(struct NCR_700_Host_Parameters *)host->hostdata[0];

	scsi_remove_host(host);
	NCR_700_release(host);
	free_irq(host->irq, host);
	kfree(hostdata);

	return 0;
}

static struct parisc_driver lasi700_driver = {
	.name =		"Lasi SCSI",
	.id_table =	lasi700_ids,
	.probe =	lasi700_probe,
	.remove =	__devexit_p(lasi700_driver_remove),
};

static int __init
lasi700_init(void)
{
	return register_parisc_driver(&lasi700_driver);
}

static void __exit
lasi700_exit(void)
{
	unregister_parisc_driver(&lasi700_driver);
}

module_init(lasi700_init);
module_exit(lasi700_exit);
