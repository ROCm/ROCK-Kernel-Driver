/*
 * linux/drivers/ide/hpt34x.c		Version 0.31	June. 9, 2000
 *
 * Copyright (C) 1998-2000	Andre Hedrick <andre@linux-ide.org>
 * May be copied or modified under the terms of the GNU General Public License
 *
 *
 * 00:12.0 Unknown mass storage controller:
 * Triones Technologies, Inc.
 * Unknown device 0003 (rev 01)
 *
 * hde: UDMA 2 (0x0000 0x0002) (0x0000 0x0010)
 * hdf: UDMA 2 (0x0002 0x0012) (0x0010 0x0030)
 * hde: DMA 2  (0x0000 0x0002) (0x0000 0x0010)
 * hdf: DMA 2  (0x0002 0x0012) (0x0010 0x0030)
 * hdg: DMA 1  (0x0012 0x0052) (0x0030 0x0070)
 * hdh: DMA 1  (0x0052 0x0252) (0x0070 0x00f0)
 *
 * ide-pci.c reference
 *
 * Since there are two cards that report almost identically,
 * the only discernable difference is the values reported in pcicmd.
 * Booting-BIOS card or HPT363 :: pcicmd == 0x07
 * Non-bootable card or HPT343 :: pcicmd == 0x05
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
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/ide.h>

#include <asm/io.h>
#include <asm/irq.h>
#include "ide_modes.h"

#ifndef SPLIT_BYTE
#define SPLIT_BYTE(B,H,L)	((H)=(B>>4), (L)=(B-((B>>4)<<4)))
#endif

#define HPT343_DEBUG_DRIVE_INFO		0

#undef DISPLAY_HPT34X_TIMINGS

#if defined(DISPLAY_HPT34X_TIMINGS) && defined(CONFIG_PROC_FS)
#include <linux/stat.h>
#include <linux/proc_fs.h>

static int hpt34x_get_info(char *, char **, off_t, int);
extern int (*hpt34x_display_info)(char *, char **, off_t, int); /* ide-proc.c */
extern char *ide_media_verbose(ide_drive_t *);
static struct pci_dev *bmide_dev;

static int hpt34x_get_info (char *buffer, char **addr, off_t offset, int count)
{
	char *p = buffer;
	u32 bibma = pci_resource_start(bmide_dev, 4);
	u8  c0 = 0, c1 = 0;

        /*
         * at that point bibma+0x2 et bibma+0xa are byte registers
         * to investigate:
         */
	c0 = inb_p((unsigned short)bibma + 0x02);
	c1 = inb_p((unsigned short)bibma + 0x0a);

	p += sprintf(p, "\n                                HPT34X Chipset.\n");
	p += sprintf(p, "--------------- Primary Channel ---------------- Secondary Channel -------------\n");
	p += sprintf(p, "                %sabled                         %sabled\n",
			(c0&0x80) ? "dis" : " en",
			(c1&0x80) ? "dis" : " en");
	p += sprintf(p, "--------------- drive0 --------- drive1 -------- drive0 ---------- drive1 ------\n");
	p += sprintf(p, "DMA enabled:    %s              %s             %s               %s\n",
			(c0&0x20) ? "yes" : "no ", (c0&0x40) ? "yes" : "no ",
			(c1&0x20) ? "yes" : "no ", (c1&0x40) ? "yes" : "no " );

	p += sprintf(p, "UDMA\n");
	p += sprintf(p, "DMA\n");
	p += sprintf(p, "PIO\n");

	return p-buffer;	/* => must be less than 4k! */
}
#endif  /* defined(DISPLAY_HPT34X_TIMINGS) && defined(CONFIG_PROC_FS) */

byte hpt34x_proc = 0;

extern char *ide_xfer_verbose (byte xfer_rate);

static void hpt34x_clear_chipset (ide_drive_t *drive)
{
	unsigned int reg1	= 0, tmp1 = 0;
	unsigned int reg2	= 0, tmp2 = 0;

	pci_read_config_dword(HWIF(drive)->pci_dev, 0x44, &reg1);
	pci_read_config_dword(HWIF(drive)->pci_dev, 0x48, &reg2);
	tmp1 = ((0x00 << (3*drive->dn)) | (reg1 & ~(7 << (3*drive->dn))));
	tmp2 = (reg2 & ~(0x11 << drive->dn));
	pci_write_config_dword(HWIF(drive)->pci_dev, 0x44, tmp1);
	pci_write_config_dword(HWIF(drive)->pci_dev, 0x48, tmp2);
}

static int hpt34x_tune_chipset (ide_drive_t *drive, byte speed)
{
	int			err;
	byte			hi_speed, lo_speed;
	unsigned int reg1	= 0, tmp1 = 0;
	unsigned int reg2	= 0, tmp2 = 0;

	SPLIT_BYTE(speed, hi_speed, lo_speed);

	if (hi_speed & 7) {
		hi_speed = (hi_speed & 4) ? 0x01 : 0x10;
	} else {
		lo_speed <<= 5;
		lo_speed >>= 5;
	}

	pci_read_config_dword(HWIF(drive)->pci_dev, 0x44, &reg1);
	pci_read_config_dword(HWIF(drive)->pci_dev, 0x48, &reg2);
	tmp1 = ((lo_speed << (3*drive->dn)) | (reg1 & ~(7 << (3*drive->dn))));
	tmp2 = ((hi_speed << drive->dn) | reg2);
	err = ide_config_drive_speed(drive, speed);
	pci_write_config_dword(HWIF(drive)->pci_dev, 0x44, tmp1);
	pci_write_config_dword(HWIF(drive)->pci_dev, 0x48, tmp2);

	if (!drive->init_speed)
		drive->init_speed = speed;

#if HPT343_DEBUG_DRIVE_INFO
	printk("%s: %s drive%d (0x%04x 0x%04x) (0x%04x 0x%04x)" \
		" (0x%02x 0x%02x) 0x%04x\n",
		drive->name, ide_xfer_verbose(speed),
		drive->dn, reg1, tmp1, reg2, tmp2,
		hi_speed, lo_speed, err);
#endif /* HPT343_DEBUG_DRIVE_INFO */

	drive->current_speed = speed;
	return(err);
}

static void config_chipset_for_pio (ide_drive_t *drive)
{
	unsigned short eide_pio_timing[6] = {960, 480, 240, 180, 120, 90};
	unsigned short xfer_pio = drive->id->eide_pio_modes;

	byte	timing, speed, pio;

	pio = ide_get_best_pio_mode(drive, 255, 5, NULL);

	if (xfer_pio> 4)
		xfer_pio = 0;

	if (drive->id->eide_pio_iordy > 0) {
		for (xfer_pio = 5;
			xfer_pio>0 &&
			drive->id->eide_pio_iordy>eide_pio_timing[xfer_pio];
			xfer_pio--);
	} else {
		xfer_pio = (drive->id->eide_pio_modes & 4) ? 0x05 :
			   (drive->id->eide_pio_modes & 2) ? 0x04 :
			   (drive->id->eide_pio_modes & 1) ? 0x03 : xfer_pio;
	}

	timing = (xfer_pio >= pio) ? xfer_pio : pio;

	switch(timing) {
		case 4: speed = XFER_PIO_4;break;
		case 3: speed = XFER_PIO_3;break;
		case 2: speed = XFER_PIO_2;break;
		case 1: speed = XFER_PIO_1;break;
		default:
			speed = (!drive->id->tPIO) ? XFER_PIO_0 : XFER_PIO_SLOW;
			break;
		}
	(void) hpt34x_tune_chipset(drive, speed);
}

static void hpt34x_tune_drive (ide_drive_t *drive, byte pio)
{
	byte speed;

	switch(pio) {
		case 4:		speed = XFER_PIO_4;break;
		case 3:		speed = XFER_PIO_3;break;
		case 2:		speed = XFER_PIO_2;break;
		case 1:		speed = XFER_PIO_1;break;
		default:	speed = XFER_PIO_0;break;
	}
	hpt34x_clear_chipset(drive);
	(void) hpt34x_tune_chipset(drive, speed);
}

#ifdef CONFIG_BLK_DEV_IDEDMA
/*
 * This allows the configuration of ide_pci chipset registers
 * for cards that learn about the drive's UDMA, DMA, PIO capabilities
 * after the drive is reported by the OS.  Initally for designed for
 * HPT343 UDMA chipset by HighPoint|Triones Technologies, Inc.
 */
static int config_chipset_for_dma (ide_drive_t *drive, byte ultra)
{
	struct hd_driveid *id	= drive->id;
	byte speed		= 0x00;

	if (drive->media != ide_disk)
		return ((int) ide_dma_off_quietly);

	hpt34x_clear_chipset(drive);

	if ((id->dma_ultra & 0x0010) && ultra) {
		speed = XFER_UDMA_2;
	} else if ((id->dma_ultra & 0x0008) && ultra) {
		speed = XFER_UDMA_2;
	} else if ((id->dma_ultra & 0x0004) && ultra) {
		speed = XFER_UDMA_2;
	} else if ((id->dma_ultra & 0x0002) && ultra) {
		speed = XFER_UDMA_1;
	} else if ((id->dma_ultra & 0x0001) && ultra) {
		speed = XFER_UDMA_0;
	} else if (id->dma_mword & 0x0004) {
		speed = XFER_MW_DMA_2;
	} else if (id->dma_mword & 0x0002) {
		speed = XFER_MW_DMA_1;
	} else if (id->dma_mword & 0x0001) {
		speed = XFER_MW_DMA_0;
	} else if (id->dma_1word & 0x0004) {
		speed = XFER_SW_DMA_2;
	} else if (id->dma_1word & 0x0002) {
		speed = XFER_SW_DMA_1;
	} else if (id->dma_1word & 0x0001) {
		speed = XFER_SW_DMA_0;
        } else {
		return ((int) ide_dma_off_quietly);
	}

	(void) hpt34x_tune_chipset(drive, speed);

	return ((int)	((id->dma_ultra >> 11) & 3) ? ide_dma_off :
			((id->dma_ultra >> 8) & 7) ? ide_dma_on :
			((id->dma_mword >> 8) & 7) ? ide_dma_on :
			((id->dma_1word >> 8) & 7) ? ide_dma_on :
						     ide_dma_off_quietly);
}

static int config_drive_xfer_rate (ide_drive_t *drive)
{
	struct hd_driveid *id = drive->id;
	ide_dma_action_t dma_func = ide_dma_on;

	if (id && (id->capability & 1) && HWIF(drive)->autodma) {
		/* Consult the list of known "bad" drives */
		if (ide_dmaproc(ide_dma_bad_drive, drive)) {
			dma_func = ide_dma_off;
			goto fast_ata_pio;
		}
		dma_func = ide_dma_off_quietly;
		if (id->field_valid & 4) {
			if (id->dma_ultra & 0x0007) {
				/* Force if Capable UltraDMA */
				dma_func = config_chipset_for_dma(drive, 1);
				if ((id->field_valid & 2) &&
				    (dma_func != ide_dma_on))
					goto try_dma_modes;
			}
		} else if (id->field_valid & 2) {
try_dma_modes:
			if ((id->dma_mword & 0x0007) ||
			    (id->dma_1word & 0x0007)) {
				/* Force if Capable regular DMA modes */
				dma_func = config_chipset_for_dma(drive, 0);
				if (dma_func != ide_dma_on)
					goto no_dma_set;
			}
		} else if (ide_dmaproc(ide_dma_good_drive, drive)) {
			if (id->eide_dma_time > 150) {
				goto no_dma_set;
			}
			/* Consult the list of known "good" drives */
			dma_func = config_chipset_for_dma(drive, 0);
			if (dma_func != ide_dma_on)
				goto no_dma_set;
		} else {
			goto fast_ata_pio;
		}
	} else if ((id->capability & 8) || (id->field_valid & 2)) {
fast_ata_pio:
		dma_func = ide_dma_off_quietly;
no_dma_set:
		config_chipset_for_pio(drive);
	}

#ifndef CONFIG_HPT34X_AUTODMA
	if (dma_func == ide_dma_on)
		dma_func = ide_dma_off;
#endif /* CONFIG_HPT34X_AUTODMA */

	return HWIF(drive)->dmaproc(dma_func, drive);
}

/*
 * hpt34x_dmaproc() initiates/aborts (U)DMA read/write operations on a drive.
 *
 * This is specific to the HPT343 UDMA bios-less chipset
 * and HPT345 UDMA bios chipset (stamped HPT363)
 * by HighPoint|Triones Technologies, Inc.
 */

int hpt34x_dmaproc (ide_dma_action_t func, ide_drive_t *drive)
{
	ide_hwif_t *hwif = HWIF(drive);
	unsigned long dma_base = hwif->dma_base;
	unsigned int count, reading = 0;
	byte dma_stat;

	switch (func) {
		case ide_dma_check:
			return config_drive_xfer_rate(drive);
		case ide_dma_read:
			reading = 1 << 3;
		case ide_dma_write:
			if (!(count = ide_build_dmatable(drive, func)))
				return 1;	/* try PIO instead of DMA */
			outl(hwif->dmatable_dma, dma_base + 4); /* PRD table */
			reading |= 0x01;
			outb(reading, dma_base);		/* specify r/w */
			outb(inb(dma_base+2)|6, dma_base+2);	/* clear INTR & ERROR flags */
			drive->waiting_for_dma = 1;
			if (drive->media != ide_disk)
				return 0;
			ide_set_handler(drive, &ide_dma_intr, WAIT_CMD, NULL);	/* issue cmd to drive */
			OUT_BYTE((reading == 9) ? WIN_READDMA : WIN_WRITEDMA, IDE_COMMAND_REG);
			return 0;
		case ide_dma_end:	/* returns 1 on error, 0 otherwise */
			drive->waiting_for_dma = 0;
			outb(inb(dma_base)&~1, dma_base);	/* stop DMA */
			dma_stat = inb(dma_base+2);		/* get DMA status */
			outb(dma_stat|6, dma_base+2);		/* clear the INTR & ERROR bits */
			ide_destroy_dmatable(drive);		/* purge DMA mappings */
			return (dma_stat & 7) != 4;		/* verify good DMA status */
		default:
			break;
	}
	return ide_dmaproc(func, drive);	/* use standard DMA stuff */
}
#endif /* CONFIG_BLK_DEV_IDEDMA */

/*
 * If the BIOS does not set the IO base addaress to XX00, 343 will fail.
 */
#define	HPT34X_PCI_INIT_REG		0x80

unsigned int __init pci_init_hpt34x (struct pci_dev *dev, const char *name)
{
	int i = 0;
	unsigned long hpt34xIoBase = pci_resource_start(dev, 4);
	unsigned short cmd;
	unsigned long flags;

	__save_flags(flags);	/* local CPU only */
	__cli();		/* local CPU only */

	pci_write_config_byte(dev, HPT34X_PCI_INIT_REG, 0x00);
	pci_read_config_word(dev, PCI_COMMAND, &cmd);

	if (cmd & PCI_COMMAND_MEMORY) {
		if (pci_resource_start(dev, PCI_ROM_RESOURCE)) {
			pci_write_config_byte(dev, PCI_ROM_ADDRESS, dev->resource[PCI_ROM_RESOURCE].start | PCI_ROM_ADDRESS_ENABLE);
			printk(KERN_INFO "HPT345: ROM enabled at 0x%08lx\n", dev->resource[PCI_ROM_RESOURCE].start);
		}
		pci_write_config_byte(dev, PCI_LATENCY_TIMER, 0xF0);
	} else {
		pci_write_config_byte(dev, PCI_LATENCY_TIMER, 0x20);
	}

	pci_write_config_word(dev, PCI_COMMAND, cmd & ~PCI_COMMAND_IO);
	dev->resource[0].start = (hpt34xIoBase + 0x20);
	dev->resource[1].start = (hpt34xIoBase + 0x34);
	dev->resource[2].start = (hpt34xIoBase + 0x28);
	dev->resource[3].start = (hpt34xIoBase + 0x3c);
	for(i=0; i<4; i++)
		dev->resource[i].flags |= PCI_BASE_ADDRESS_SPACE_IO;
	/*
	 * Since 20-23 can be assigned and are R/W, we correct them.
	 */
	pci_write_config_dword(dev, PCI_BASE_ADDRESS_0, dev->resource[0].start);
	pci_write_config_dword(dev, PCI_BASE_ADDRESS_1, dev->resource[1].start);
	pci_write_config_dword(dev, PCI_BASE_ADDRESS_2, dev->resource[2].start);
	pci_write_config_dword(dev, PCI_BASE_ADDRESS_3, dev->resource[3].start);
	pci_write_config_word(dev, PCI_COMMAND, cmd);

	__restore_flags(flags);	/* local CPU only */

#if defined(DISPLAY_HPT34X_TIMINGS) && defined(CONFIG_PROC_FS)
	if (!hpt34x_proc) {
		hpt34x_proc = 1;
		bmide_dev = dev;
		hpt34x_display_info = &hpt34x_get_info;
	}
#endif /* DISPLAY_HPT34X_TIMINGS && CONFIG_PROC_FS */

	return dev->irq;
}

void __init ide_init_hpt34x (ide_hwif_t *hwif)
{
	hwif->tuneproc = &hpt34x_tune_drive;
	hwif->speedproc = &hpt34x_tune_chipset;

#ifdef CONFIG_BLK_DEV_IDEDMA
	if (hwif->dma_base) {
		unsigned short pcicmd = 0;

		pci_read_config_word(hwif->pci_dev, PCI_COMMAND, &pcicmd);
		hwif->autodma = (pcicmd & PCI_COMMAND_MEMORY) ? 1 : 0;
		hwif->dmaproc = &hpt34x_dmaproc;
	} else {
		hwif->drives[0].autotune = 1;
		hwif->drives[1].autotune = 1;
	}
#else /* !CONFIG_BLK_DEV_IDEDMA */
	hwif->drives[0].autotune = 1;
	hwif->drives[1].autotune = 1;
	hwif->autodma = 0;
#endif /* CONFIG_BLK_DEV_IDEDMA */
}
