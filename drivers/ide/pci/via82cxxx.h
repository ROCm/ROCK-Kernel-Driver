#ifndef VIA82CXXX_H
#define VIA82CXXX_H

#include <linux/config.h>
#include <linux/pci.h>
#include <linux/ide.h>

#define DISPLAY_VIA_TIMINGS

static unsigned int init_chipset_via82cxxx(struct pci_dev *, const char *);
static void init_hwif_via82cxxx(ide_hwif_t *);

static ide_pci_device_t via82cxxx_chipsets[] __devinitdata = {
	{	/* 0 */
		.vendor		= PCI_VENDOR_ID_VIA,
		.device		= PCI_DEVICE_ID_VIA_82C576_1,
		.name		= "VP_IDE",
		.init_chipset	= init_chipset_via82cxxx,
		.init_hwif	= init_hwif_via82cxxx,
		.channels	= 2,
		.autodma	= NOAUTODMA,
		.enablebits	= {{0x40,0x02,0x02}, {0x40,0x01,0x01}},
		.bootable	= ON_BOARD,
	},{	/* 1 */
		.vendor		= PCI_VENDOR_ID_VIA,
		.device		= PCI_DEVICE_ID_VIA_82C586_1,
		.name		= "VP_IDE",
		.init_chipset	= init_chipset_via82cxxx,
		.init_hwif	= init_hwif_via82cxxx,
		.channels	= 2,
		.autodma	= NOAUTODMA,
		.enablebits	= {{0x40,0x02,0x02}, {0x40,0x01,0x01}},
		.bootable	= ON_BOARD,
	}
};

#endif /* VIA82CXXX_H */
