/*
 * linux/drivers/ide/amd74xx.c		Version 0.05	June 9, 2000
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

static int amd74xx_get_info(char *, char **, off_t, int);
extern int (*amd74xx_display_info)(char *, char **, off_t, int); /* ide-proc.c */
static struct pci_dev *bmide_dev;

static int amd74xx_get_info (char *buffer, char **addr, off_t offset, int count)
{
	char *p = buffer;
	u32 bibma = pci_resource_start(bmide_dev, 4);
	u8 c0 = 0, c1 = 0;

	/*
	 * at that point bibma+0x2 et bibma+0xa are byte registers
	 * to investigate:
	 */
	c0 = IN_BYTE((unsigned short)bibma + 0x02);
	c1 = IN_BYTE((unsigned short)bibma + 0x0a);

	p += sprintf(p, "\n                                "
			"AMD %04X VIPER Chipset.\n", bmide_dev->device);
	p += sprintf(p, "--------------- Primary Channel "
			"---------------- Secondary Channel "
			"-------------\n");
	p += sprintf(p, "                %sabled "
			"                        %sabled\n",
			(c0&0x80) ? "dis" : " en",
			(c1&0x80) ? "dis" : " en");
	p += sprintf(p, "--------------- drive0 --------- drive1 "
			"-------- drive0 ---------- drive1 ------\n");
	p += sprintf(p, "DMA enabled:    %s              %s "
			"            %s               %s\n",
			(c0&0x20) ? "yes" : "no ", (c0&0x40) ? "yes" : "no ",
			(c1&0x20) ? "yes" : "no ", (c1&0x40) ? "yes" : "no " );
	p += sprintf(p, "UDMA\n");
	p += sprintf(p, "DMA\n");
	p += sprintf(p, "PIO\n");

	return p-buffer;	/* => must be less than 4k! */
}
#endif  /* defined(DISPLAY_VIPER_TIMINGS) && defined(CONFIG_PROC_FS) */

byte amd74xx_proc = 0;

static int amd74xx_mode5_check (struct pci_dev *dev)
{
	switch(dev->device) {
		case PCI_DEVICE_ID_AMD_VIPER_7411:
		case PCI_DEVICE_ID_AMD_OPUS_7441:
			return 1;
		default:
			return 0;
	}
}

static unsigned int amd74xx_swdma_check (struct pci_dev *dev)
{
	unsigned int class_rev;

	if (amd74xx_mode5_check(dev))
		return 1;

	pci_read_config_dword(dev, PCI_CLASS_REVISION, &class_rev);
	class_rev &= 0xff;
	return ((int) (class_rev >= 7) ? 1 : 0);
}

static int amd74xx_swdma_error (ide_drive_t *drive)
{
	printk("%s: single-word DMA not support (revision < C4)\n", drive->name);
	return 0;
}

static byte amd74xx_ratemask (ide_drive_t *drive)
{
	struct pci_dev *dev = HWIF(drive)->pci_dev;
	byte mode = 0x00;

        switch(dev->device) {
		case PCI_DEVICE_ID_AMD_OPUS_7441:
		case PCI_DEVICE_ID_AMD_VIPER_7411:	{ mode |= 0x03; break; }
		case PCI_DEVICE_ID_AMD_VIPER_7409:	{ mode |= 0x02; break; }
		case PCI_DEVICE_ID_AMD_COBRA_7401:	{ mode |= 0x01; break; }
		default:
			return (mode &= ~0xFF);
	}

	if (!eighty_ninty_three(drive)) {
		mode &= ~0xFE;
		mode |= 0x01;
	}
	return (mode &= ~0xF8);
}

static byte amd74xx_ratefilter (ide_drive_t *drive, byte speed)
{
#ifdef CONFIG_BLK_DEV_IDEDMA
	byte mode = amd74xx_ratemask(drive);

	switch(mode) {
		case 0x04:	// while (speed > XFER_UDMA_6) speed--; break;
		case 0x03:	while (speed > XFER_UDMA_5) speed--; break;
		case 0x02:	while (speed > XFER_UDMA_4) speed--; break;
		case 0x01:	while (speed > XFER_UDMA_2) speed--; break;
		case 0x00:
		default:	while (speed > XFER_MW_DMA_2) speed--; break;
			break;
	}
#else
	while (speed > XFER_PIO_4) speed--;
#endif /* CONFIG_BLK_DEV_IDEDMA */
//	printk("%s: mode == %02x speed == %02x\n", drive->name, mode, speed);
	return speed;
}

/*
 * Here is where all the hard work goes to program the chipset.
 */
static int amd74xx_tune_chipset (ide_drive_t *drive, byte xferspeed)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;
	byte speed		= amd74xx_ratefilter(drive, xferspeed);
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

	ultra_timing	&= ~0xC7;
	dma_pio_timing	&= ~0xFF;
	pio_timing	&= ~(0x03 << drive->dn);

	switch(speed) {
#ifdef CONFIG_BLK_DEV_IDEDMA
		case XFER_UDMA_7:
		case XFER_UDMA_6:
			speed = XFER_UDMA_5;
		case XFER_UDMA_5:
			ultra_timing |= 0x46;
			dma_pio_timing |= 0x20;
			break;
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
			if (!amd74xx_swdma_check(dev))
				return amd74xx_swdma_error(drive);
			dma_pio_timing |= 0x42;
			break;
		case XFER_SW_DMA_1:
			if (!amd74xx_swdma_check(dev))
				return amd74xx_swdma_error(drive);
			dma_pio_timing |= 0x65;
			break;
		case XFER_SW_DMA_0:
			if (!amd74xx_swdma_check(dev))
				return amd74xx_swdma_error(drive);
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

#ifdef CONFIG_BLK_DEV_IDEDMA
	pci_write_config_byte(dev, drive_pci, ultra_timing);
#endif /* CONFIG_BLK_DEV_IDEDMA */
	pci_write_config_byte(dev, drive_pci2, dma_pio_timing);
	pci_write_config_byte(dev, 0x4c, pio_timing);

	return (ide_config_drive_speed(drive, speed));
}

static void amd74xx_tune_drive (ide_drive_t *drive, byte pio)
{
	byte speed;
	pio = ide_get_best_pio_mode(drive, pio, 5, NULL);
	switch(pio) {
		case 4:		speed = XFER_PIO_4;break;
		case 3:		speed = XFER_PIO_3;break;
		case 2:		speed = XFER_PIO_2;break;
		case 1:		speed = XFER_PIO_1;break;
		default:	speed = XFER_PIO_0;break;
	}
	(void) amd74xx_tune_chipset(drive, speed);
}

#ifdef CONFIG_BLK_DEV_IDEDMA
/*
 * This allows the configuration of ide_pci chipset registers
 * for cards that learn about the drive's UDMA, DMA, PIO capabilities
 * after the drive is reported by the OS.
 */
static int config_chipset_for_dma (ide_drive_t *drive)
{
	struct hd_driveid *id   = drive->id;
	byte mode		= amd74xx_ratemask(drive);
	byte swdma		= amd74xx_swdma_check(HWIF(drive)->pci_dev);
	byte speed		= 0;
	int  rval;

	amd74xx_tune_drive(drive, 5);
	
	switch(mode) {
		case 0x04:
			if (id->dma_ultra & 0x0040)
				{ speed = XFER_UDMA_6; break; }
		case 0x03:
			if (id->dma_ultra & 0x0020)
				{ speed = XFER_UDMA_5; break; }
		case 0x02:
			if (id->dma_ultra & 0x0010)
				{ speed = XFER_UDMA_4; break; }
			if (id->dma_ultra & 0x0008)
				{ speed = XFER_UDMA_3; break; }
		case 0x01:
			if (id->dma_ultra & 0x0004)
				{ speed = XFER_UDMA_2; break; }
			if (id->dma_ultra & 0x0002)
				{ speed = XFER_UDMA_1; break; }
			if (id->dma_ultra & 0x0001)
				{ speed = XFER_UDMA_0; break; }
		case 0x00:
			if (id->dma_mword & 0x0004)
				{ speed = XFER_MW_DMA_2; break; }
			if (id->dma_mword & 0x0002)
				{ speed = XFER_MW_DMA_1; break; }
			if (id->dma_mword & 0x0001)
				{ speed = XFER_MW_DMA_0; break; }
			if ((id->dma_1word & 0x0004) && (swdma))
				{ speed = XFER_SW_DMA_2; break; }
			if ((id->dma_1word & 0x0002) && (swdma))
				{ speed = XFER_SW_DMA_1; break; }
			if ((id->dma_1word & 0x0001) && (swdma))
				{ speed = XFER_SW_DMA_0; break; }
		default:
			return ((int) ide_dma_off_quietly);
	}

	(void) amd74xx_tune_chipset(drive, speed);
//	return ((int) (dma) ? ide_dma_on : ide_dma_off_quietly);
	rval = (int)(   ((id->dma_ultra >> 11) & 7) ? ide_dma_on :
			 ((id->dma_ultra >> 8) & 7) ? ide_dma_on :
			 ((id->dma_mword >> 8) & 7) ? ide_dma_on :
			 (((id->dma_1word >> 8) & 7) && (swdma)) ? ide_dma_on :
						     ide_dma_off_quietly);
	return rval;
}

static int config_drive_xfer_rate (ide_drive_t *drive)
{
	struct hd_driveid *id	= drive->id;
	ide_hwif_t *hwif	= HWIF(drive);
	ide_dma_action_t dma_func = ide_dma_on;

	drive->init_speed = 0;

	if (id && (id->capability & 1) && hwif->autodma) {
		/* Consult the list of known "bad" drives */
		if (ide_dmaproc(ide_dma_bad_drive, drive)) {
			dma_func = ide_dma_off;
			goto fast_ata_pio;
		}
		dma_func = ide_dma_off_quietly;
		if (id->field_valid & 4) {
			if (id->dma_ultra & 0x003F) {
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
			     (amd74xx_swdma_check(HWIF(drive)->pci_dev)))) {
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
		amd74xx_tune_drive(drive, 5);
	}
	return HWIF(drive)->dmaproc(dma_func, drive);
}

/*
 * amd74xx_dmaproc() initiates/aborts (U)DMA read/write operations on a drive.
 */

int amd74xx_dmaproc (ide_dma_action_t func, ide_drive_t *drive)
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

unsigned int __init pci_init_amd74xx (struct pci_dev *dev, const char *name)
{
	unsigned long fixdma_base = pci_resource_start(dev, 4);

#ifdef CONFIG_BLK_DEV_IDEDMA
	if (!amd74xx_swdma_check(dev))
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
		OUT_BYTE(IN_BYTE(fixdma_base+2) & 0x60, fixdma_base+2);

		if (IN_BYTE(fixdma_base+2) & 0x80)
			printk("%s: simplex device: DMA will fail!!\n", name);
	}
#if defined(DISPLAY_VIPER_TIMINGS) && defined(CONFIG_PROC_FS)
	if (!amd74xx_proc) {
		amd74xx_proc = 1;
		bmide_dev = dev;
		amd74xx_display_info = &amd74xx_get_info;
	}
#endif /* DISPLAY_VIPER_TIMINGS && CONFIG_PROC_FS */

	return 0;
}

unsigned int __init ata66_amd74xx (ide_hwif_t *hwif)
{
	struct pci_dev *dev	= hwif->pci_dev;
	byte cable_80_pin[2]	= { 0, 0 };
	byte ata66		= 0;
	byte tmpbyte;

	/*
	 * Ultra66 cable detection (from Host View)
	 * 7411, 7441, 0x42, bit0: primary, bit2: secondary 80 pin
	 */
	pci_read_config_byte(dev, 0x42, &tmpbyte);

	/*
	 * 0x42, bit0 is 1 => primary channel
	 * has 80-pin (from host view)
	 */
	if (tmpbyte & 0x01) cable_80_pin[0] = 1;

	/*
	 * 0x42, bit2 is 1 => secondary channel
	 * has 80-pin (from host view)
	 */
	if (tmpbyte & 0x04) cable_80_pin[1] = 1;

	switch(dev->device) {
		case PCI_DEVICE_ID_AMD_OPUS_7441:
		case PCI_DEVICE_ID_AMD_VIPER_7411:
			ata66 = (hwif->channel) ?
				cable_80_pin[1] :
				cable_80_pin[0];
		default:
			break;
	}
#ifdef CONFIG_AMD74XX_OVERRIDE
	return(1);
#else
	return (unsigned int) ata66;
#endif /* CONFIG_AMD74XX_OVERRIDE */
}

void __init ide_init_amd74xx (ide_hwif_t *hwif)
{
	hwif->tuneproc = &amd74xx_tune_drive;
	hwif->speedproc = &amd74xx_tune_chipset;

	if (!hwif->dma_base) {
		hwif->drives[0].autotune = 1;
		hwif->drives[1].autotune = 1;
		hwif->autodma = 0;
		return;
	}

#ifndef CONFIG_BLK_DEV_IDEDMA
	hwif->dmaproc = &amd74xx_dmaproc;
#ifdef CONFIG_IDEDMA_AUTO
	if (!noautodma)
		hwif->autodma = 1;
#endif /* CONFIG_IDEDMA_AUTO */
#endif /* CONFIG_BLK_DEV_IDEDMA */
}

void __init ide_dmacapable_amd74xx (ide_hwif_t *hwif, unsigned long dmabase)
{
	ide_setup_dma(hwif, dmabase, 8);
}

extern void ide_setup_pci_device (struct pci_dev *dev, ide_pci_device_t *d);

void __init fixup_device_amd74xx (struct pci_dev *dev, ide_pci_device_t *d)
{
	if (dev->resource[0].start != 0x01F1)
		ide_register_xp_fix(dev);

	printk("%s: IDE controller on PCI bus %02x dev %02x\n",
		d->name, dev->bus->number, dev->devfn);
	ide_setup_pci_device(dev, d);
}

