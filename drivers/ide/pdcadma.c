/*
 * linux/drivers/ide/pdcadma.c		Version 0.01	June 21, 2001
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

#include "ata-timing.h"
#include "pcihost.h"

#undef DISPLAY_PDCADMA_TIMINGS

#if defined(DISPLAY_PDCADMA_TIMINGS) && defined(CONFIG_PROC_FS)
#include <linux/stat.h>
#include <linux/proc_fs.h>

static int pdcadma_get_info(char *, char **, off_t, int);
extern int (*pdcadma_display_info)(char *, char **, off_t, int); /* ide-proc.c */
static struct pci_dev *bmide_dev;

static int pdcadma_get_info (char *buffer, char **addr, off_t offset, int count)
{
	char *p = buffer;
	u32 bibma = pci_resource_start(bmide_dev, 4);

	p += sprintf(p, "\n                                PDC ADMA %04X Chipset.\n", bmide_dev->device);
	p += sprintf(p, "UDMA\n");
	p += sprintf(p, "PIO\n");

	return p-buffer;	/* => must be less than 4k! */
}
#endif

byte pdcadma_proc = 0;

extern char *ide_xfer_verbose (byte xfer_rate);

#ifdef CONFIG_BLK_DEV_IDEDMA

/*
 * This initiates/aborts (U)DMA read/write operations on a drive.
 */
static int pdcadma_dmaproc(struct ata_device *drive)
{
	udma_enable(drive, 0, 0);

	return 0;
}
#endif

static unsigned int __init pci_init_pdcadma(struct pci_dev *dev)
{
#if defined(DISPLAY_PDCADMA_TIMINGS) && defined(CONFIG_PROC_FS)
	if (!pdcadma_proc) {
		pdcadma_proc = 1;
		bmide_dev = dev;
		pdcadma_display_info = pdcadma_get_info;
	}
#endif
	return 0;
}

static unsigned int __init ata66_pdcadma(struct ata_channel *channel)
{
	return 1;
}

static void __init ide_init_pdcadma(struct ata_channel *hwif)
{
	hwif->autodma = 0;
	hwif->dma_base = 0;

//	hwif->tuneproc = &pdcadma_tune_drive;
//	hwif->speedproc = &pdcadma_tune_chipset;

//	if (hwif->dma_base) {
//		hwif->XXX_dmaproc = &pdcadma_dmaproc;
//		hwif->autodma = 1;
//	}
}

static void __init ide_dmacapable_pdcadma(struct ata_channel *hwif, unsigned long dmabase)
{
//	ide_setup_dma(hwif, dmabase, 8);
}


/* module data table */
static struct ata_pci_device chipset __initdata = {
	PCI_VENDOR_ID_PDC, PCI_DEVICE_ID_PDC_1841,
	pci_init_pdcadma,
	ata66_pdcadma,
	ide_init_pdcadma,
	ide_dmacapable_pdcadma,
	{
		{0x00,0x00,0x00},
		{0x00,0x00,0x00}
	},
	OFF_BOARD,
	0,
	ATA_F_NODMA
};

int __init init_pdcadma(void)
{
	ata_register_chipset(&chipset);

        return 0;
}
