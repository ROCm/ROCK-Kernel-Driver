#ifndef SC1200_H
#define SC1200_H

#include <linux/config.h>
#include <linux/pci.h>
#include <linux/ide.h>

#define DISPLAY_SC1200_TIMINGS

static unsigned int init_chipset_sc1200(struct pci_dev *, const char *);
static void init_hwif_sc1200(ide_hwif_t *);

static ide_pci_device_t sc1200_chipsets[] __devinitdata = {
	{	/* 0 */
		.name		= "SC1200",
		.init_chipset	= init_chipset_sc1200,
		.init_hwif	= init_hwif_sc1200,
		.channels	= 2,
		.autodma	= AUTODMA,
		.bootable	= ON_BOARD,
	}
};

#endif /* SC1200_H */
