#ifndef ALI15X3_H
#define ALI15X3_H

#include <linux/config.h>
#include <linux/pci.h>
#include <linux/ide.h>

#define DISPLAY_ALI_TIMINGS

static unsigned int init_chipset_ali15x3(struct pci_dev *, const char *);
static void init_hwif_common_ali15x3(ide_hwif_t *);
static void init_hwif_ali15x3(ide_hwif_t *);
static void init_dma_ali15x3(ide_hwif_t *, unsigned long);

static ide_pci_device_t ali15x3_chipsets[] __devinitdata = {
	{	/* 0 */
		.vendor		= PCI_VENDOR_ID_AL,
		.device		= PCI_DEVICE_ID_AL_M5229,
		.name		= "ALI15X3",
		.init_chipset	= init_chipset_ali15x3,
		.init_hwif	= init_hwif_ali15x3,
		.init_dma	= init_dma_ali15x3,
		.channels	= 2,
		.autodma	= AUTODMA,
		.bootable	= ON_BOARD,
	}
};

#endif /* ALI15X3 */
