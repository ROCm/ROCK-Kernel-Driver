#ifndef PDC_ADMA_H
#define PDC_ADMA_H

#include <linux/config.h>
#include <linux/pci.h>
#include <linux/ide.h>

#undef DISPLAY_PDCADMA_TIMINGS

#if defined(DISPLAY_PDCADMA_TIMINGS) && defined(CONFIG_PROC_FS)
#include <linux/stat.h>
#include <linux/proc_fs.h>

static u8 pdcadma_proc;

static int pdcadma_get_info(char *, char **, off_t, int);

static ide_pci_host_proc_t pdcadma_procs[] __initdata = {
	{
		.name		= "pdcadma",
		.set		= 1,
		.get_info	= pdcadma_get_info,
		.parent		= NULL,
	},
};
#endif  /* defined(DISPLAY_PDCADMA_TIMINGS) && defined(CONFIG_PROC_FS) */

static void init_setup_pdcadma(struct pci_dev *, ide_pci_device_t *);
static unsigned int init_chipset_pdcadma(struct pci_dev *, const char *);
static void init_hwif_pdcadma(ide_hwif_t *);
static void init_dma_pdcadma(ide_hwif_t *, unsigned long);

static ide_pci_device_t pdcadma_chipsets[] __devinitdata = {
	{	/* 0 */
		.vendor		= PCI_VENDOR_ID_PDC,
		.device		= PCI_DEVICE_ID_PDC_1841,
		.name		= "PDCADMA",
		.init_setup	= init_setup_pdcadma,
		.init_chipset	= init_chipset_pdcadma,
		.init_iops	= NULL,
		.init_hwif	= init_hwif_pdcadma,
		.init_dma	= init_dma_pdcadma,
		.channels	= 2,
		.autodma	= NODMA,
		.enablebits	= {{0x00,0x00,0x00}, {0x00,0x00,0x00}},
		.bootable	= OFF_BOARD,
		.extra		= 0,
	},{
		.vendor		= 0,
		.device		= 0,
		.channels	= 0,
		.bootable	= EOL,
	}
};

#endif /* PDC_ADMA_H */
