/*
 * linux/drivers/ide/amd7409.c		Version 0.05	June 9, 2000
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

#define DISPLAY_VIPER_TIMINGS

#if defined(DISPLAY_VIPER_TIMINGS) && defined(CONFIG_PROC_FS)
#include <linux/stat.h>
#include <linux/proc_fs.h>

static int amd7409_get_info(char *, char **, off_t, int);
extern int (*amd7409_display_info)(char *, char **, off_t, int); /* ide-proc.c */
extern char *ide_media_verbose(ide_drive_t *);
static struct pci_dev *bmide_dev;

static int amd7409_get_info (char *buffer, char **addr, off_t offset, int count)
{
	char *p = buffer;
	u32 bibma = pci_resource_start(bmide_dev, 4);
	u8 c0 = 0, c1 = 0;

	/*
	 * at that point bibma+0x2 et bibma+0xa are byte registers
	 * to investigate:
	 */
	c0 = inb_p((unsigned short)bibma + 0x02);
	c1 = inb_p((unsigned short)bibma + 0x0a);

	p += sprintf(p, "\n                                AMD 7409 VIPER Chipset.\n");
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
#endif  /* defined(DISPLAY_VIPER_TIMINGS) && defined(CONFIG_PROC_FS) */

byte amd7409_proc = 0;

extern char *ide_xfer_verbose (byte xfer_rate);

static unsigned int amd7409_swdma_check (struct pci_dev *dev)
{
	unsigned int class_rev;
	pci_read_config_dword(dev, PCI_CLASS_REVISION, &class_rev);
	class_rev &= 0xff;
	return ((int) (class_rev >= 7) ? 1 : 0);
}

static int amd7409_swdma_error(ide_drive_t *drive)
{
	printk("%s: single-word DMA not support (revision < C4)\n", drive->name);
	return 0;
}

/*
 * Here is where all the hard work goes to program the chipset.
 *
 */
static int amd7409_tune_chipset (ide_drive_t *drive, byte speed)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;
	int err			= 0;
	byte unit		= (drive->select.b.unit & 0x01);
#ifdef CONFIG_BLK_DEV_IDEDMA
	unsigned long dma_base	= hwif->dma_base;
#endif /* CONFIG_BLK_DEV_IDEDMA */
	byte drive_pci		= 0x00;
	byte drive_pci2		= 0x00;
	byte ultra_timing	= 0x00;
	byte dma_pio_timing	= 0x00;
	byte pio_timing		= 0x00;

        switch (drive->dn) {
		case 0: drive_pci = 0x53; drive_pci2 = 0x4b; break;
		case 1: drive_pci = 0x52; drive_pci2 = 0x4a; break;
		case 2: drive_pci = 0x51; drive_pci2 = 0x49; break;
		case 3: drive_pci = 0x50; drive_pci2 = 0x48; break;
		default:
                        return -1;
        }

	pci_read_config_byte(dev, drive_pci, &ultra_timing);
	pci_read_config_byte(dev, drive_pci2, &dma_pio_timing);
	pci_read_config_byte(dev, 0x4c, &pio_timing);

#ifdef DEBUG
	printk("%s: UDMA 0x%02x DMAPIO 0x%02x PIO 0x%02x ",
		drive->name, ultra_timing, dma_pio_timing, pio_timing);
#endif

	ultra_timing &= ~0xC7;
	dma_pio_timing &= ~0xFF;
	pio_timing &= ~(0x03 << drive->dn);

#ifdef DEBUG
	printk(":: UDMA 0x%02x DMAPIO 0x%02x PIO 0x%02x ",
		ultra_timing, dma_pio_timing, pio_timing);
#endif

	switch(speed) {
#ifdef CONFIG_BLK_DEV_IDEDMA
		case XFER_UDMA_4:
			ultra_timing |= 0x45;
			dma_pio_timing |= 0x20;
			break;
		case XFER_UDMA_3:
			ultra_timing |= 0x44;
			dma_pio_timing |= 0x20;
			break;
		case XFER_UDMA_2:
			ultra_timing |= 0x40;
			dma_pio_timing |= 0x20;
			break;
		case XFER_UDMA_1:
			ultra_timing |= 0x41;
			dma_pio_timing |= 0x20;
			break;
		case XFER_UDMA_0:
			ultra_timing |= 0x42;
			dma_pio_timing |= 0x20;
			break;
		case XFER_MW_DMA_2:
			dma_pio_timing |= 0x20;
			break;
		case XFER_MW_DMA_1:
			dma_pio_timing |= 0x21;
			break;
		case XFER_MW_DMA_0:
			dma_pio_timing |= 0x77;
			break;
		case XFER_SW_DMA_2:
			if (!amd7409_swdma_check(dev))
				return amd7409_swdma_error(drive);
			dma_pio_timing |= 0x42;
			break;
		case XFER_SW_DMA_1:
			if (!amd7409_swdma_check(dev))
				return amd7409_swdma_error(drive);
			dma_pio_timing |= 0x65;
			break;
		case XFER_SW_DMA_0:
			if (!amd7409_swdma_check(dev))
				return amd7409_swdma_error(drive);
			dma_pio_timing |= 0xA8;
			break;
#endif /* CONFIG_BLK_DEV_IDEDMA */
		case XFER_PIO_4:
			dma_pio_timing |= 0x20;
			break;
		case XFER_PIO_3:
			dma_pio_timing |= 0x22;
			break;
		case XFER_PIO_2:
			dma_pio_timing |= 0x42;
			break;
		case XFER_PIO_1:
			dma_pio_timing |= 0x65;
			break;
		case XFER_PIO_0:
		default:
			dma_pio_timing |= 0xA8;
			break;
        }

	pio_timing |= (0x03 << drive->dn);

	if (!drive->init_speed)
		drive->init_speed = speed;

#ifdef CONFIG_BLK_DEV_IDEDMA
	pci_write_config_byte(dev, drive_pci, ultra_timing);
#endif /* CONFIG_BLK_DEV_IDEDMA */
	pci_write_config_byte(dev, drive_pci2, dma_pio_timing);
	pci_write_config_byte(dev, 0x4c, pio_timing);

#ifdef DEBUG
	printk(":: UDMA 0x%02x DMAPIO 0x%02x PIO 0x%02x\n",
		ultra_timing, dma_pio_timing, pio_timing);
#endif

#ifdef CONFIG_BLK_DEV_IDEDMA
	if (speed > XFER_PIO_4) {
		outb(inb(dma_base+2)|(1<<(5+unit)), dma_base+2);
	} else {
		outb(inb(dma_base+2) & ~(1<<(5+unit)), dma_base+2);
	}
#endif /* CONFIG_BLK_DEV_IDEDMA */

	err = ide_config_drive_speed(drive, speed);
	drive->current_speed = speed;
	return (err);
}

static void config_chipset_for_pio (ide_drive_t *drive)
{
	unsigned short eide_pio_timing[6] = {960, 480, 240, 180, 120, 90};
	unsigned short xfer_pio	= drive->id->eide_pio_modes;
	byte			timing, speed, pio;

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
			   (drive->id->eide_pio_modes & 1) ? 0x03 :
			   (drive->id->tPIO & 2) ? 0x02 :
			   (drive->id->tPIO & 1) ? 0x01 : xfer_pio;
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
	(void) amd7409_tune_chipset(drive, speed);
	drive->current_speed = speed;
}

static void amd7409_tune_drive (ide_drive_t *drive, byte pio)
{
	byte speed;
	switch(pio) {
		case 4:		speed = XFER_PIO_4;break;
		case 3:		speed = XFER_PIO_3;break;
		case 2:		speed = XFER_PIO_2;break;
		case 1:		speed = XFER_PIO_1;break;
		default:	speed = XFER_PIO_0;break;
	}
	(void) amd7409_tune_chipset(drive, speed);
}

#ifdef CONFIG_BLK_DEV_IDEDMA
/*
 * This allows the configuration of ide_pci chipset registers
 * for cards that learn about the drive's UDMA, DMA, PIO capabilities
 * after the drive is reported by the OS.
 */
static int config_chipset_for_dma (ide_drive_t *drive)
{
	struct hd_driveid *id	= drive->id;
	byte udma_66		= eighty_ninty_three(drive);
	byte udma_100		= 0;
	byte speed		= 0x00;
	int  rval;

	if ((id->dma_ultra & 0x0020) && (udma_66)&& (udma_100)) {
		speed = XFER_UDMA_5;
	} else if ((id->dma_ultra & 0x0010) && (udma_66)) {
		speed = XFER_UDMA_4;
	} else if ((id->dma_ultra & 0x0008) && (udma_66)) {
		speed = XFER_UDMA_3;
	} else if (id->dma_ultra & 0x0004) {
		speed = XFER_UDMA_2;
	} else if (id->dma_ultra & 0x0002) {
		speed = XFER_UDMA_1;
	} else if (id->dma_ultra & 0x0001) {
		speed = XFER_UDMA_0;
	} else if (id->dma_mword & 0x0004) {
		speed = XFER_MW_DMA_2;
	} else if (id->dma_mword & 0x0002) {
		speed = XFER_MW_DMA_1;
	} else if (id->dma_mword & 0x0001) {
		speed = XFER_MW_DMA_0;
	} else {
		return ((int) ide_dma_off_quietly);
	}

	(void) amd7409_tune_chipset(drive, speed);

	rval = (int)(	((id->dma_ultra >> 11) & 3) ? ide_dma_on :
			((id->dma_ultra >> 8) & 7) ? ide_dma_on :
			((id->dma_mword >> 8) & 7) ? ide_dma_on :
						     ide_dma_off_quietly);

	return rval;
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
			if (id->dma_ultra & 0x002F) {
				/* Force if Capable UltraDMA */
				dma_func = config_chipset_for_dma(drive);
				if ((id->field_valid & 2) &&
				    (dma_func != ide_dma_on))
					goto try_dma_modes;
			}
		} else if (id->field_valid & 2) {
try_dma_modes:
			if ((id->dma_mword & 0x0007) ||
			    ((id->dma_1word & 0x007) &&
			     (amd7409_swdma_check(HWIF(drive)->pci_dev)))) {
				/* Force if Capable regular DMA modes */
				dma_func = config_chipset_for_dma(drive);
				if (dma_func != ide_dma_on)
					goto no_dma_set;
			}
			
		} else if (ide_dmaproc(ide_dma_good_drive, drive)) {
			if (id->eide_dma_time > 150) {
				goto no_dma_set;
			}
			/* Consult the list of known "good" drives */
			dma_func = config_chipset_for_dma(drive);
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
	return HWIF(drive)->dmaproc(dma_func, drive);
}

/*
 * amd7409_dmaproc() initiates/aborts (U)DMA read/write operations on a drive.
 */

int amd7409_dmaproc (ide_dma_action_t func, ide_drive_t *drive)
{
	switch (func) {
		case ide_dma_check:
			return config_drive_xfer_rate(drive);
		default:
			break;
	}
	return ide_dmaproc(func, drive);	/* use standard DMA stuff */
}
#endif /* CONFIG_BLK_DEV_IDEDMA */

unsigned int __init pci_init_amd7409 (struct pci_dev *dev, const char *name)
{
	unsigned long fixdma_base = pci_resource_start(dev, 4);

#ifdef CONFIG_BLK_DEV_IDEDMA
	if (!amd7409_swdma_check(dev))
		printk("%s: disabling single-word DMA support (revision < C4)\n", name);
#endif /* CONFIG_BLK_DEV_IDEDMA */

	if (!fixdma_base) {
		/*
		 *
		 */
	} else {
		/*
		 * enable DMA capable bit, and "not" simplex only
		 */
		outb(inb(fixdma_base+2) & 0x60, fixdma_base+2);

		if (inb(fixdma_base+2) & 0x80)
			printk("%s: simplex device: DMA will fail!!\n", name);
	}
#if defined(DISPLAY_VIPER_TIMINGS) && defined(CONFIG_PROC_FS)
	if (!amd7409_proc) {
		amd7409_proc = 1;
		bmide_dev = dev;
		amd7409_display_info = &amd7409_get_info;
	}
#endif /* DISPLAY_VIPER_TIMINGS && CONFIG_PROC_FS */

	return 0;
}

unsigned int __init ata66_amd7409 (ide_hwif_t *hwif)
{
#ifdef CONFIG_AMD7409_OVERRIDE
	byte ata66 = 1;
#else
	byte ata66 = 0;
#endif /* CONFIG_AMD7409_OVERRIDE */

#if 0
	pci_read_config_byte(hwif->pci_dev, 0x48, &ata66);
	return ((ata66 & 0x02) ? 0 : 1);
#endif
	return ata66;
}

void __init ide_init_amd7409 (ide_hwif_t *hwif)
{
	hwif->tuneproc = &amd7409_tune_drive;
	hwif->speedproc = &amd7409_tune_chipset;

#ifndef CONFIG_BLK_DEV_IDEDMA
	hwif->drives[0].autotune = 1;
	hwif->drives[1].autotune = 1;
	hwif->autodma = 0;
	return;
#else

	if (hwif->dma_base) {
		hwif->dmaproc = &amd7409_dmaproc;
		hwif->autodma = 1;
	} else {
		hwif->autodma = 0;
		hwif->drives[0].autotune = 1;
		hwif->drives[1].autotune = 1;
	}
#endif /* CONFIG_BLK_DEV_IDEDMA */
}

void __init ide_dmacapable_amd7409 (ide_hwif_t *hwif, unsigned long dmabase)
{
	ide_setup_dma(hwif, dmabase, 8);
}
