#ifndef RZ100X_H
#define RZ100X_H

#include <linux/config.h>
#include <linux/pci.h>
#include <linux/ide.h>

static void init_hwif_rz1000(ide_hwif_t *);

static ide_pci_device_t rz1000_chipsets[] __devinitdata = {
{
		.vendor		= PCI_VENDOR_ID_PCTECH,
		.device		= PCI_DEVICE_ID_PCTECH_RZ1000,
		.name		= "RZ1000",
		.init_hwif	= init_hwif_rz1000,
		.channels	= 2,
		.autodma	= NODMA,
		.bootable	= ON_BOARD,
	},{
		.vendor		= PCI_VENDOR_ID_PCTECH,
		.device		= PCI_DEVICE_ID_PCTECH_RZ1001,
		.name		= "RZ1001",
		.init_hwif	= init_hwif_rz1000,
		.channels	= 2,
		.autodma	= NODMA,
		.bootable	= ON_BOARD,
	}
};

#endif /* RZ100X_H */
