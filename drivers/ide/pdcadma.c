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

#include "ide_modes.h"

#undef DISPLAY_PDCADMA_TIMINGS

#if defined(DISPLAY_PDCADMA_TIMINGS) && defined(CONFIG_PROC_FS)
#include <linux/stat.h>
#include <linux/proc_fs.h>

static int pdcadma_get_info(char *, char **, off_t, int);
extern int (*pdcadma_display_info)(char *, char **, off_t, int); /* ide-proc.c */
extern char *ide_media_verbose(ide_drive_t *);
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
#endif  /* defined(DISPLAY_PDCADMA_TIMINGS) && defined(CONFIG_PROC_FS) */

byte pdcadma_proc = 0;

extern char *ide_xfer_verbose (byte xfer_rate);

#ifdef CONFIG_BLK_DEV_IDEDMA
/*
 * pdcadma_dmaproc() initiates/aborts (U)DMA read/write operations on a drive.
 */

int pdcadma_dmaproc (ide_dma_action_t func, ide_drive_t *drive)
{
	switch (func) {
		case ide_dma_check:
			func = ide_dma_off_quietly;
		default:
			break;
	}
	return ide_dmaproc(func, drive);	/* use standard DMA stuff */
}
#endif /* CONFIG_BLK_DEV_IDEDMA */

unsigned int __init pci_init_pdcadma (struct pci_dev *dev, const char *name)
{
#if defined(DISPLAY_PDCADMA_TIMINGS) && defined(CONFIG_PROC_FS)
	if (!pdcadma_proc) {
		pdcadma_proc = 1;
		bmide_dev = dev;
		pdcadma_display_info = &pdcadma_get_info;
	}
#endif /* DISPLAY_PDCADMA_TIMINGS && CONFIG_PROC_FS */
	return 0;
}

unsigned int __init ata66_pdcadma (ide_hwif_t *hwif)
{
	return 1;
}

void __init ide_init_pdcadma (ide_hwif_t *hwif)
{
	hwif->autodma = 0;
	hwif->dma_base = 0;

//	hwif->tuneproc = &pdcadma_tune_drive;
//	hwif->speedproc = &pdcadma_tune_chipset;

//	if (hwif->dma_base) {
//		hwif->dmaproc = &pdcadma_dmaproc;
//		hwif->autodma = 1;
//	}
}

void __init ide_dmacapable_pdcadma (ide_hwif_t *hwif, unsigned long dmabase)
{
//	ide_setup_dma(hwif, dmabase, 8);
}

