/*
 *  linux/drivers/ide/rz1000.c		Version 0.05	December 8, 1997
 *
 *  Copyright (C) 1995-1998  Linus Torvalds & author (see below)
 */

/*
 *  Principal Author:  mlord@pobox.com (Mark Lord)
 *
 *  See linux/MAINTAINERS for address of current maintainer.
 *
 *  This file provides support for disabling the buggy read-ahead
 *  mode of the RZ1000 IDE chipset, commonly used on Intel motherboards.
 *
 *  Dunno if this fixes both ports, or only the primary port (?).
 */

#undef REALLY_SLOW_IO		/* most systems can safely undef this */

#include <linux/config.h> /* for CONFIG_BLK_DEV_IDEPCI */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/pci.h>
#include <linux/ide.h>
#include <linux/init.h>

#include <asm/io.h>

#include "rz1000.h"

static void __init init_hwif_rz1000 (ide_hwif_t *hwif)
{
	u16 reg;
	struct pci_dev *dev = hwif->pci_dev;

	hwif->chipset = ide_rz1000;
	if (!pci_read_config_word (dev, 0x40, &reg) &&
	    !pci_write_config_word(dev, 0x40, reg & 0xdfff)) {
		printk("%s: disabled chipset read-ahead "
			"(buggy RZ1000/RZ1001)\n", hwif->name);
	} else {
		hwif->serialized = 1;
		hwif->drives[0].no_unmask = 1;
		hwif->drives[1].no_unmask = 1;
		printk("%s: serialized, disabled unmasking "
			"(buggy RZ1000/RZ1001)\n", hwif->name);
	}
}

extern void ide_setup_pci_device(struct pci_dev *, ide_pci_device_t *);

static void __init init_setup_rz1000 (struct pci_dev *dev, ide_pci_device_t *d)
{
	ide_setup_pci_device(dev, d);
}

int __init rz1000_scan_pcidev (struct pci_dev *dev)
{
	ide_pci_device_t *d;

	if (dev->vendor != PCI_VENDOR_ID_PCTECH)
		return 0;

	for (d = rz1000_chipsets; d && d->vendor && d->device; ++d) {
		if (((d->vendor == dev->vendor) &&
		     (d->device == dev->device)) &&
		    (d->init_setup)) {
			d->init_setup(dev, d);
			return 1;
		}
	}
	return 0;
}

