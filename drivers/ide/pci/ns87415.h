#ifndef NS87415_H
#define NS87415_H

#include <linux/config.h>
#include <linux/pci.h>
#include <linux/ide.h>

static void init_hwif_ns87415(ide_hwif_t *);

static ide_pci_device_t ns87415_chipsets[] __devinitdata = {
	{	/* 0 */
		.name		= "NS87415",
		.init_hwif	= init_hwif_ns87415,
		.channels	= 2,
		.autodma	= AUTODMA,
		.bootable	= ON_BOARD,
	}
};

#endif /* NS87415_H */
