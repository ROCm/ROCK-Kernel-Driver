#ifndef SIIMAGE_H
#define SIIMAGE_H

#include <linux/config.h>
#include <linux/pci.h>
#include <linux/ide.h>

#include <asm/io.h>

#define DISPLAY_SIIMAGE_TIMINGS

#undef SIIMAGE_VIRTUAL_DMAPIO
#undef SIIMAGE_BUFFERED_TASKFILE
#undef SIIMAGE_LARGE_DMA

#define SII_DEBUG 0

#if SII_DEBUG
#define siiprintk(x...)	printk(x)
#else
#define siiprintk(x...)
#endif

static unsigned int init_chipset_siimage(struct pci_dev *, const char *);
static void init_iops_siimage(ide_hwif_t *);
static void init_hwif_siimage(ide_hwif_t *);

static ide_pci_device_t siimage_chipsets[] __devinitdata = {
	{	/* 0 */
		.vendor		= PCI_VENDOR_ID_CMD,
		.device		= PCI_DEVICE_ID_SII_680,
		.name		= "SiI680",
		.init_chipset	= init_chipset_siimage,
		.init_iops	= init_iops_siimage,
		.init_hwif	= init_hwif_siimage,
		.channels	= 2,
		.autodma	= AUTODMA,
		.bootable	= ON_BOARD,
	},{	/* 1 */
		.vendor		= PCI_VENDOR_ID_CMD,
		.device		= PCI_DEVICE_ID_SII_3112,
		.name		= "SiI3112 Serial ATA",
		.init_chipset	= init_chipset_siimage,
		.init_iops	= init_iops_siimage,
		.init_hwif	= init_hwif_siimage,
		.channels	= 2,
		.autodma	= AUTODMA,
		.bootable	= ON_BOARD,
	},{	/* 2 */
		.vendor		= PCI_VENDOR_ID_CMD,
		.device		= PCI_DEVICE_ID_SII_1210SA,
		.name		= "Adaptec AAR-1210SA",
		.init_chipset	= init_chipset_siimage,
		.init_iops	= init_iops_siimage,
		.init_hwif	= init_hwif_siimage,
		.channels	= 2,
		.autodma	= AUTODMA,
		.bootable	= ON_BOARD,
	}
};

#endif /* SIIMAGE_H */
