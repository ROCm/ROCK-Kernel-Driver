#ifndef SLC90E66_H
#define SLC90E66_H

#include <linux/config.h>
#include <linux/pci.h>
#include <linux/ide.h>

#define DISPLAY_SLC90E66_TIMINGS

#define SLC90E66_DEBUG_DRIVE_INFO	0

static unsigned int init_chipset_slc90e66(struct pci_dev *, const char *);
static void init_hwif_slc90e66(ide_hwif_t *);

static ide_pci_device_t slc90e66_chipsets[] __devinitdata = {
	{	/* 0 */
		.vendor		= PCI_VENDOR_ID_EFAR,
		.device		= PCI_DEVICE_ID_EFAR_SLC90E66_1,
		.name		= "SLC90E66",
		.init_chipset	= init_chipset_slc90e66,
		.init_hwif	= init_hwif_slc90e66,
		.channels	= 2,
		.autodma	= AUTODMA,
		.enablebits	= {{0x41,0x80,0x80}, {0x43,0x80,0x80}},
		.bootable	= ON_BOARD,
	}
};

#endif /* SLC90E66_H */
