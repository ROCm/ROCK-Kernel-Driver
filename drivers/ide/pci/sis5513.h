#ifndef SIS5513_H
#define SIS5513_H

#include <linux/config.h>
#include <linux/pci.h>
#include <linux/ide.h>

#define DISPLAY_SIS_TIMINGS

static unsigned int init_chipset_sis5513(struct pci_dev *, const char *);
static void init_hwif_sis5513(ide_hwif_t *);

static ide_pci_device_t sis5513_chipsets[] __devinitdata = {
	{	/* 0 */
		.vendor		= PCI_VENDOR_ID_SI,
		.device		= PCI_DEVICE_ID_SI_5513,
		.name		= "SIS5513",
		.init_chipset	= init_chipset_sis5513,
		.init_hwif	= init_hwif_sis5513,
		.channels	= 2,
		.autodma	= NOAUTODMA,
		.enablebits	= {{0x4a,0x02,0x02}, {0x4a,0x04,0x04}},
		.bootable	= ON_BOARD,
	}
};

#endif /* SIS5513_H */
