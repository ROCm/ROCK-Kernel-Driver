/*
 *  linux/drivers/ide/generic.c		Version 0.10	Sept 11, 2002
 *
 *  Copyright (C) 2001-2002	Andre Hedrick <andre@linux-ide.org>
 */


#undef REALLY_SLOW_IO		/* most systems can safely undef this */

#include <linux/config.h> /* for CONFIG_BLK_DEV_IDEPCI */
#include <linux/types.h>
#include <linux/module.h>
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

#include "generic.h"

static unsigned int __init init_chipset_generic (struct pci_dev *dev, const char *name)
{
	return 0;
}

static void __init init_hwif_generic (ide_hwif_t *hwif)
{
	switch(hwif->pci_dev->device) {
		case PCI_DEVICE_ID_UMC_UM8673F:
		case PCI_DEVICE_ID_UMC_UM8886A:
		case PCI_DEVICE_ID_UMC_UM8886BF:
			hwif->irq = hwif->channel ? 15 : 14;
			break;
		default:
			break;
	}

	if (!(hwif->dma_base))
		return;

	hwif->atapi_dma = 1;
	hwif->ultra_mask = 0x7f;
	hwif->mwdma_mask = 0x07;
	hwif->swdma_mask = 0x07;

#ifdef CONFIG_BLK_DEV_IDEDMA
	if (!noautodma)
		hwif->autodma = 1;
	hwif->drives[0].autodma = hwif->autodma;
	hwif->drives[1].autodma = hwif->autodma;
#endif /* CONFIG_BLK_DEV_IDEDMA */
}

static void init_dma_generic (ide_hwif_t *hwif, unsigned long dmabase)
{
	ide_setup_dma(hwif, dmabase, 8);
}

extern void ide_setup_pci_device(struct pci_dev *, ide_pci_device_t *);

static void __init init_setup_generic (struct pci_dev *dev, ide_pci_device_t *d)
{
	if ((d->vendor == PCI_VENDOR_ID_UMC) &&
	    (d->device == PCI_DEVICE_ID_UMC_UM8886A) &&
	    (!(PCI_FUNC(dev->devfn) & 1)))
		return; /* UM8886A/BF pair */

	if ((d->vendor == PCI_VENDOR_ID_OPTI) &&
	    (d->device == PCI_DEVICE_ID_OPTI_82C558) &&
	    (!(PCI_FUNC(dev->devfn) & 1)))
		return;

	ide_setup_pci_device(dev, d);
}

static void __init init_setup_unknown (struct pci_dev *dev, ide_pci_device_t *d)
{
	ide_setup_pci_device(dev, d);
}

#if 0

	/* Logic to add back later on */
	
	if ((dev->class >> 8) == PCI_CLASS_STORAGE_IDE) {
		ide_pci_device_t *unknown = unknown_chipset;
//		unknown->vendor = dev->vendor;
//		unknown->device = dev->device;
		init_setup_unknown(dev, unknown);
		return 1;
	}
	return 0;
#endif	

/**
 *	generic_init_one	-	called when a PIIX is found
 *	@dev: the generic device
 *	@id: the matching pci id
 *
 *	Called when the PCI registration layer (or the IDE initialization)
 *	finds a device matching our IDE device tables.
 */
 
static int __devinit generic_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	ide_pci_device_t *d = &generic_chipsets[id->driver_data];

	if (dev->device != d->device)
		BUG();
	d->init_setup(dev, d);
	return 0;
}

static struct pci_device_id generic_pci_tbl[] __devinitdata = {
	{ PCI_VENDOR_ID_NS,     PCI_DEVICE_ID_NS_87410,            PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ PCI_VENDOR_ID_PCTECH, PCI_DEVICE_ID_PCTECH_SAMURAI_IDE,  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 1},
	{ PCI_VENDOR_ID_HOLTEK, PCI_DEVICE_ID_HOLTEK_6565,         PCI_ANY_ID, PCI_ANY_ID, 0, 0, 2},
	{ PCI_VENDOR_ID_UMC,    PCI_DEVICE_ID_UMC_UM8673F,         PCI_ANY_ID, PCI_ANY_ID, 0, 0, 3},
	{ PCI_VENDOR_ID_UMC,    PCI_DEVICE_ID_UMC_UM8886A,         PCI_ANY_ID, PCI_ANY_ID, 0, 0, 4},
	{ PCI_VENDOR_ID_UMC,    PCI_DEVICE_ID_UMC_UM8886BF,        PCI_ANY_ID, PCI_ANY_ID, 0, 0, 5},
	{ PCI_VENDOR_ID_HINT,   PCI_DEVICE_ID_HINT_VXPROII_IDE,    PCI_ANY_ID, PCI_ANY_ID, 0, 0, 6},
	{ PCI_VENDOR_ID_VIA,    PCI_DEVICE_ID_VIA_82C561,          PCI_ANY_ID, PCI_ANY_ID, 0, 0, 7},
	{ PCI_VENDOR_ID_OPTI,   PCI_DEVICE_ID_OPTI_82C558,         PCI_ANY_ID, PCI_ANY_ID, 0, 0, 8},
	{ 0, },
};

static struct pci_driver driver = {
	name:		"PCI IDE",
	id_table:	generic_pci_tbl,
	probe:		generic_init_one,
};

static int generic_ide_init(void)
{
	return ide_pci_register_driver(&driver);
}

static void generic_ide_exit(void)
{
	ide_pci_unregister_driver(&driver);
}

module_init(generic_ide_init);
module_exit(generic_ide_exit);

MODULE_AUTHOR("Andre Hedrick");
MODULE_DESCRIPTION("PCI driver module for generic PCI IDE");
MODULE_LICENSE("GPL");

EXPORT_NO_SYMBOLS;
