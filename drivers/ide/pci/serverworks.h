
#ifndef SERVERWORKS_H
#define SERVERWORKS_H

#include <linux/config.h>
#include <linux/pci.h>
#include <linux/ide.h>

#undef SVWKS_DEBUG_DRIVE_INFO

#define SVWKS_CSB5_REVISION_NEW	0x92 /* min PCI_REVISION_ID for UDMA5 (A2.0) */
#define SVWKS_CSB6_REVISION	0xa0 /* min PCI_REVISION_ID for UDMA4 (A1.0) */

/* Seagate Barracuda ATA IV Family drives in UDMA mode 5
 * can overrun their FIFOs when used with the CSB5 */
const char *svwks_bad_ata100[] = {
	"ST320011A",
	"ST340016A",
	"ST360021A",
	"ST380021A",
	NULL
};

#define DISPLAY_SVWKS_TIMINGS	1

static void init_setup_svwks(struct pci_dev *, ide_pci_device_t *);
static void init_setup_csb6(struct pci_dev *, ide_pci_device_t *);
static unsigned int init_chipset_svwks(struct pci_dev *, const char *);
static void init_hwif_svwks(ide_hwif_t *);
static void init_dma_svwks(ide_hwif_t *, unsigned long);

static ide_pci_device_t serverworks_chipsets[] __devinitdata = {
	{	/* 0 */
		.name		= "SvrWks OSB4",
		.init_setup	= init_setup_svwks,
		.init_chipset	= init_chipset_svwks,
		.init_hwif	= init_hwif_svwks,
		.channels	= 2,
		.autodma	= AUTODMA,
		.bootable	= ON_BOARD,
	},{	/* 1 */
		.name		= "SvrWks CSB5",
		.init_setup	= init_setup_svwks,
		.init_chipset	= init_chipset_svwks,
		.init_hwif	= init_hwif_svwks,
		.init_dma	= init_dma_svwks,
		.channels	= 2,
		.autodma	= AUTODMA,
		.bootable	= ON_BOARD,
	},{	/* 2 */
		.name		= "SvrWks CSB6",
		.init_setup	= init_setup_csb6,
		.init_chipset	= init_chipset_svwks,
		.init_hwif	= init_hwif_svwks,
		.init_dma	= init_dma_svwks,
		.channels	= 2,
		.autodma	= AUTODMA,
		.bootable	= ON_BOARD,
	},{	/* 3 */
		.name		= "SvrWks CSB6",
		.init_setup	= init_setup_csb6,
		.init_chipset	= init_chipset_svwks,
		.init_hwif	= init_hwif_svwks,
		.init_dma	= init_dma_svwks,
		.channels	= 1,	/* 2 */
		.autodma	= AUTODMA,
		.bootable	= ON_BOARD,
	}
};

#endif /* SERVERWORKS_H */
