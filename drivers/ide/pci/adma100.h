#ifndef ADMA_100_H
#define ADMA_100_H

#include <linux/config.h>
#include <linux/pci.h>
#include <linux/ide.h>

extern void init_setup_pdcadma(struct pci_dev *, ide_pci_device_t *);
extern unsigned int init_chipset_pdcadma(struct pci_dev *, const char *);
extern void init_hwif_pdcadma(ide_hwif_t *);
extern void init_dma_pdcadma(ide_hwif_t *, unsigned long);

static ide_pci_device_t pdcadma_chipsets[] __devinitdata = {
	{
		.vendor		= PCI_VENDOR_ID_PDC,
		.device		= PCI_DEVICE_ID_PDC_1841,
		.name		= "ADMA100",
		.init_setup	= init_setup_pdcadma,
		.init_chipset	= init_chipset_pdcadma,
		.init_iops	= NULL,
		.init_hwif	= init_hwif_pdcadma,
		.init_dma	= init_dma_pdcadma,
		.channels	= 2,
		.autodma	= NODMA,
		.enablebits	= {{0x00,0x00,0x00}, {0x00,0x00,0x00}},
		.bootable	= OFF_BOARD,
		.extra		= 0
	},{
		.vendor		= 0,
		.device		= 0,
		.channels	= 0,
		.bootable	= EOL,
	}
}

#endif /* ADMA_100_H */
