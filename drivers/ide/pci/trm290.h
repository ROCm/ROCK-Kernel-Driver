#ifndef TRM290_H
#define TRM290_H

#include <linux/config.h>
#include <linux/pci.h>
#include <linux/ide.h>

extern void init_hwif_trm290(ide_hwif_t *);

static ide_pci_device_t trm290_chipsets[] __devinitdata = {
	{	/* 0 */
		.name		= "TRM290",
		.init_hwif	= init_hwif_trm290,
		.channels	= 2,
		.autodma	= NOAUTODMA,
		.bootable	= ON_BOARD,
	}
};

#endif /* TRM290_H */
