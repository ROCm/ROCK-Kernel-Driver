/*
 * linux/drivers/ide/pci/pdcadma.c		Version 0.05	Sept 10, 2002
 *
 * Copyright (C) 1999-2000		Andre Hedrick <andre@linux-ide.org>
 * May be copied or modified under the terms of the GNU General Public License
 *
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>

#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/ide.h>

#include <asm/io.h>
#include <asm/irq.h>

#include "ide_modes.h"
#include "pdcadma.h"

#if defined(DISPLAY_PDCADMA_TIMINGS) && defined(CONFIG_PROC_FS)
#include <linux/stat.h>
#include <linux/proc_fs.h>

static u8 pdcadma_proc = 0;
#define PDC_MAX_DEVS		5
static struct pci_dev *pdc_devs[PDC_MAX_DEVS];
static int n_pdc_devs;

static int pdcadma_get_info (char *buffer, char **addr, off_t offset, int count)
{
	char *p = buffer;
	int i;

	for (i = 0; i < n_pdc_devs; i++) {
		struct pci_dev *dev	= pdc_devs[i];
		unsigned long bibma = pci_resource_start(dev, 4);

		p += sprintf(p, "\n                                "
			"PDC ADMA %04X Chipset.\n", dev->device);
		p += sprintf(p, "UDMA\n");
		p += sprintf(p, "PIO\n");

	}
	return p-buffer;	/* => must be less than 4k! */
}
#endif  /* defined(DISPLAY_PDCADMA_TIMINGS) && defined(CONFIG_PROC_FS) */

/*
 * pdcadma_dma functions() initiates/aborts (U)DMA read/write
 * operations on a drive.
 */
#if 0
        int (*ide_dma_read)(ide_drive_t *drive);
        int (*ide_dma_write)(ide_drive_t *drive);
        int (*ide_dma_begin)(ide_drive_t *drive);
        int (*ide_dma_end)(ide_drive_t *drive);
        int (*ide_dma_check)(ide_drive_t *drive);
        int (*ide_dma_on)(ide_drive_t *drive);
        int (*ide_dma_off)(ide_drive_t *drive);
        int (*ide_dma_off_quietly)(ide_drive_t *drive);
        int (*ide_dma_test_irq)(ide_drive_t *drive);
        int (*ide_dma_host_on)(ide_drive_t *drive);
        int (*ide_dma_host_off)(ide_drive_t *drive);
        int (*ide_dma_bad_drive)(ide_drive_t *drive);
        int (*ide_dma_good_drive)(ide_drive_t *drive);
        int (*ide_dma_count)(ide_drive_t *drive);
        int (*ide_dma_verbose)(ide_drive_t *drive);
        int (*ide_dma_retune)(ide_drive_t *drive);
        int (*ide_dma_lostirq)(ide_drive_t *drive);
        int (*ide_dma_timeout)(ide_drive_t *drive);

#endif

static unsigned int __init init_chipset_pdcadma (struct pci_dev *dev, const char *name)
{
#if defined(DISPLAY_PDCADMA_TIMINGS) && defined(CONFIG_PROC_FS)
	pdc_devs[n_pdc_devs++] = dev;

	if (!pdcadma_proc) {
		pdcadma_proc = 1;
		ide_pci_register_host_proc(&pdcadma_procs[0]);
	}
#endif /* DISPLAY_PDCADMA_TIMINGS && CONFIG_PROC_FS */
	return 0;
}

static void __init init_hwif_pdcadma (ide_hwif_t *hwif)
{
	hwif->autodma = 0;
	hwif->dma_base = 0;

//	hwif->tuneproc = &pdcadma_tune_drive;
//	hwif->speedproc = &pdcadma_tune_chipset;

//	if (hwif->dma_base) {
//		hwif->autodma = 1;
//	}

	hwif->udma_four = 1;

//	hwif->atapi_dma = 1;
//	hwif->ultra_mask = 0x7f;
//	hwif->mwdma_mask = 0x07;
//	hwif->swdma_mask = 0x07;

}

static void __init init_dma_pdcadma (ide_hwif_t *hwif, unsigned long dmabase)
{
#if 0
	ide_setup_dma(hwif, dmabase, 8);
#endif
}

extern void ide_setup_pci_device(struct pci_dev *, ide_pci_device_t *);

static int __devinit pdcadma_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	ide_pci_device_t *d = &pdcadma_chipsets[id->driver_data];
	if (dev->device != d->device)
		BUG();
	ide_setup_pci_device(dev, d);
	MOD_INC_USE_COUNT;
	return 1;
}

static struct pci_device_id pdcadma_pci_tbl[] __devinitdata = {
	{ PCI_VENDOR_ID_PDC, PCI_DEVICE_ID_PDC_1841, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ 0, },
};

static struct pci_driver driver = {
	.name		= "PDCADMA-IDE",
	.id_table	= pdcadma_pci_tbl,
	.probe		= pdcadma_init_one,
};

static int pdcadma_ide_init(void)
{
	return ide_pci_register_driver(&driver);
}

static void pdcadma_ide_exit(void)
{
	ide_pci_unregister_driver(&driver);
}

module_init(pdcadma_ide_init);
module_exit(pdcadma_ide_exit);

MODULE_AUTHOR("Andre Hedrick");
MODULE_DESCRIPTION("PCI driver module for PDCADMA IDE");
MODULE_LICENSE("GPL");
