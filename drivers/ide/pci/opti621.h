#ifndef OPTI621_H
#define OPTI621_H

#include <linux/config.h>
#include <linux/pci.h>
#include <linux/ide.h>

static void init_setup_opti621(struct pci_dev *, ide_pci_device_t *);
static void init_hwif_opti621(ide_hwif_t *);

static ide_pci_device_t opti621_chipsets[] __devinitdata = {
	{	/* 0 */
		.vendor		= PCI_VENDOR_ID_OPTI,
		.device		= PCI_DEVICE_ID_OPTI_82C621,
		.name		= "OPTI621",
		.init_setup	= init_setup_opti621,
		.init_hwif	= init_hwif_opti621,
		.channels	= 2,
		.autodma	= AUTODMA,
		.enablebits	= {{0x45,0x80,0x00}, {0x40,0x08,0x00}},
		.bootable	= ON_BOARD,
	},{	/* 1 */
		.vendor		= PCI_VENDOR_ID_OPTI,
		.device		= PCI_DEVICE_ID_OPTI_82C825,
		.name		= "OPTI621X",
		.init_setup	= init_setup_opti621,
		.init_hwif	= init_hwif_opti621,
		.channels	= 2,
		.autodma	= AUTODMA,
		.enablebits	= {{0x45,0x80,0x00}, {0x40,0x08,0x00}},
		.bootable	= ON_BOARD,
	}
};

#endif /* OPTI621_H */
