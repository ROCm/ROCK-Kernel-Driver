/**** vi:set ts=8 sts=8 sw=8:************************************************
 *
 * linux/drivers/ide/cs5530.c		Version 0.6	Mar. 18, 2000
 *
 * Copyright (C) 2000			Andre Hedrick <andre@linux-ide.org>
 * Ditto of GNU General Public License.
 *
 * Copyright (C) 2000			Mark Lord <mlord@pobox.com>
 * May be copied or modified under the terms of the GNU General Public License
 *
 * Development of this chipset driver was funded
 * by the nice folks at National Semiconductor.
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

#include "ata-timing.h"
#include "pcihost.h"

/*
 * Set a new transfer mode at the drive
 */
int cs5530_set_xfer_mode(struct ata_device *drive, byte mode)
{
	int error = 0;

	printk("%s: cs5530_set_xfer_mode(%02x)\n", drive->name, mode);
	error = ide_config_drive_speed(drive, mode);

	return error;
}

/*
 * Here are the standard PIO mode 0-4 timings for each "format".
 * Format-0 uses fast data reg timings, with slower command reg timings.
 * Format-1 uses fast timings for all registers, but won't work with all drives.
 */
static unsigned int cs5530_pio_timings[2][5] =
	{{0x00009172, 0x00012171, 0x00020080, 0x00032010, 0x00040010},
	 {0xd1329172, 0x71212171, 0x30200080, 0x20102010, 0x00100010}};

/*
 * After chip reset, the PIO timings are set to 0x0000e132, which is not valid.
 */
#define CS5530_BAD_PIO(timings) (((timings)&~0x80000000)==0x0000e132)
#define CS5530_BASEREG(hwif)	(((hwif)->dma_base & ~0xf) + ((hwif)->unit ? 0x30 : 0x20))

/*
 * cs5530_tuneproc() handles selection/setting of PIO modes
 * for both the chipset and drive.
 *
 * The ide_init_cs5530() routine guarantees that all drives
 * will have valid default PIO timings set up before we get here.
 */
static void cs5530_tuneproc(struct ata_device *drive, byte pio)	/* pio=255 means "autotune" */
{
	struct ata_channel *hwif = drive->channel;
	unsigned int	format, basereg = CS5530_BASEREG(hwif);

	if (pio == 255)
		pio = ata_timing_mode(drive, XFER_PIO | XFER_EPIO);
	else
		pio = XFER_PIO_0 + min_t(byte, pio, 4);

	if (!cs5530_set_xfer_mode(drive, pio)) {
		format = (inl(basereg+4) >> 31) & 1;
		outl(cs5530_pio_timings[format][pio], basereg+(drive->select.b.unit<<3));
	}
}

#ifdef CONFIG_BLK_DEV_IDEDMA
/*
 * cs5530_config_dma() handles selection/setting of DMA/UDMA modes
 * for both the chipset and drive.
 */
static int cs5530_config_dma(struct ata_device *drive)
{
	int			udma_ok = 1, mode = 0;
	struct ata_channel *hwif = drive->channel;
	int			unit = drive->select.b.unit;
	struct ata_device		*mate = &hwif->drives[unit^1];
	struct hd_driveid	*id = drive->id;
	unsigned int		basereg, reg, timings;


	/*
	 * Default to DMA-off in case we run into trouble here.
	 */
	udma_enable(drive, 0, 0);
	outb(inb(hwif->dma_base+2)&~(unit?0x40:0x20), hwif->dma_base+2); /* clear DMA_capable bit */

	/*
	 * The CS5530 specifies that two drives sharing a cable cannot
	 * mix UDMA/MDMA.  It has to be one or the other, for the pair,
	 * though different timings can still be chosen for each drive.
	 * We could set the appropriate timing bits on the fly,
	 * but that might be a bit confusing.  So, for now we statically
	 * handle this requirement by looking at our mate drive to see
	 * what it is capable of, before choosing a mode for our own drive.
	 */
	if (mate->present) {
		struct hd_driveid *mateid = mate->id;
		if (mateid && (mateid->capability & 1) && !udma_black_list(mate)) {
			if ((mateid->field_valid & 4) && (mateid->dma_ultra & 7))
				udma_ok = 1;
			else if ((mateid->field_valid & 2) && (mateid->dma_mword & 7))
				udma_ok = 0;
			else
				udma_ok = 1;
		}
	}

	/*
	 * Now see what the current drive is capable of,
	 * selecting UDMA only if the mate said it was ok.
	 */
	if (id && (id->capability & 1) && hwif->autodma && !udma_black_list(drive)) {
		if (udma_ok && (id->field_valid & 4) && (id->dma_ultra & 7)) {
			if      (id->dma_ultra & 4)
				mode = XFER_UDMA_2;
			else if (id->dma_ultra & 2)
				mode = XFER_UDMA_1;
			else if (id->dma_ultra & 1)
				mode = XFER_UDMA_0;
		}
		if (!mode && (id->field_valid & 2) && (id->dma_mword & 7)) {
			if      (id->dma_mword & 4)
				mode = XFER_MW_DMA_2;
			else if (id->dma_mword & 2)
				mode = XFER_MW_DMA_1;
			else if (id->dma_mword & 1)
				mode = XFER_MW_DMA_0;
		}
	}

	/*
	 * Tell the drive to switch to the new mode; abort on failure.
	 */
	if (!mode || cs5530_set_xfer_mode(drive, mode))
		return 1;	/* failure */

	/*
	 * Now tune the chipset to match the drive:
	 */
	switch (mode) {
		case XFER_UDMA_0:	timings = 0x00921250; break;
		case XFER_UDMA_1:	timings = 0x00911140; break;
		case XFER_UDMA_2:	timings = 0x00911030; break;
		case XFER_MW_DMA_0:	timings = 0x00077771; break;
		case XFER_MW_DMA_1:	timings = 0x00012121; break;
		case XFER_MW_DMA_2:	timings = 0x00002020; break;
		default:
			printk("%s: cs5530_config_dma: huh? mode=%02x\n", drive->name, mode);
			return 1;	/* failure */
	}
	basereg = CS5530_BASEREG(hwif);
	reg = inl(basereg+4);			/* get drive0 config register */
	timings |= reg & 0x80000000;		/* preserve PIO format bit */
	if (unit == 0) {			/* are we configuring drive0? */
		outl(timings, basereg+4);	/* write drive0 config register */
	} else {
		if (timings & 0x00100000)
			reg |=  0x00100000;	/* enable UDMA timings for both drives */
		else
			reg &= ~0x00100000;	/* disable UDMA timings for both drives */
		outl(reg,     basereg+4);	/* write drive0 config register */
		outl(timings, basereg+12);	/* write drive1 config register */
	}
	outb(inb(hwif->dma_base+2)|(unit?0x40:0x20), hwif->dma_base+2);	/* set DMA_capable bit */

	/*
	 * Finally, turn DMA on in software, and exit.
	 */
	udma_enable(drive, 1, 1);	/* success */

	return 0;
}

static int cs5530_udma_setup(struct ata_device *drive, int map)
{
	return cs5530_config_dma(drive);
}
#endif

/*
 * Initialize the cs5530 bridge for reliable IDE DMA operation.
 */
static unsigned int __init pci_init_cs5530(struct pci_dev *dev)
{
	struct pci_dev *master_0 = NULL, *cs5530_0 = NULL;
	unsigned short pcicmd = 0;
	unsigned long flags;

	pci_for_each_dev (dev) {
		if (dev->vendor == PCI_VENDOR_ID_CYRIX) {
			switch (dev->device) {
				case PCI_DEVICE_ID_CYRIX_PCI_MASTER:
					master_0 = dev;
					break;
				case PCI_DEVICE_ID_CYRIX_5530_LEGACY:
					cs5530_0 = dev;
					break;
			}
		}
	}
	if (!master_0) {
		printk("%s: unable to locate PCI MASTER function\n", dev->name);
		return 0;
	}
	if (!cs5530_0) {
		printk("%s: unable to locate CS5530 LEGACY function\n", dev->name);
		return 0;
	}

	save_flags(flags);
	cli();	/* all CPUs (there should only be one CPU with this chipset) */

	/*
	 * Enable BusMaster and MemoryWriteAndInvalidate for the cs5530:
	 * -->  OR 0x14 into 16-bit PCI COMMAND reg of function 0 of the cs5530
	 */
	pci_read_config_word (cs5530_0, PCI_COMMAND, &pcicmd);
	pci_write_config_word(cs5530_0, PCI_COMMAND, pcicmd | PCI_COMMAND_MASTER | PCI_COMMAND_INVALIDATE);

	/*
	 * Set PCI CacheLineSize to 16-bytes:
	 * --> Write 0x04 into 8-bit PCI CACHELINESIZE reg of function 0 of the cs5530
	 */
	pci_write_config_byte(cs5530_0, PCI_CACHE_LINE_SIZE, 0x04);

	/*
	 * Disable trapping of UDMA register accesses (Win98 hack):
	 * --> Write 0x5006 into 16-bit reg at offset 0xd0 of function 0 of the cs5530
	 */
	pci_write_config_word(cs5530_0, 0xd0, 0x5006);

	/*
	 * Bit-1 at 0x40 enables MemoryWriteAndInvalidate on internal X-bus:
	 * The other settings are what is necessary to get the register
	 * into a sane state for IDE DMA operation.
	 */
	pci_write_config_byte(master_0, 0x40, 0x1e);

	/* 
	 * Set max PCI burst size (16-bytes seems to work best):
	 *	   16bytes: set bit-1 at 0x41 (reg value of 0x16)
	 *	all others: clear bit-1 at 0x41, and do:
	 *	  128bytes: OR 0x00 at 0x41
	 *	  256bytes: OR 0x04 at 0x41
	 *	  512bytes: OR 0x08 at 0x41
	 *	 1024bytes: OR 0x0c at 0x41
	 */
	pci_write_config_byte(master_0, 0x41, 0x14);

	/*
	 * These settings are necessary to get the chip
	 * into a sane state for IDE DMA operation.
	 */
	pci_write_config_byte(master_0, 0x42, 0x00);
	pci_write_config_byte(master_0, 0x43, 0xc1);

	restore_flags(flags);

	return 0;
}

/*
 * This gets invoked by the IDE driver once for each channel,
 * and performs channel-specific pre-initialization before drive probing.
 */
static void __init ide_init_cs5530(struct ata_channel *hwif)
{
	u32 basereg, d0_timings;

	hwif->serialized = 1;

#ifdef CONFIG_BLK_DEV_IDEDMA
	if (hwif->dma_base) {
		hwif->highmem = 1;
		hwif->udma_setup = cs5530_udma_setup;
	}
#endif

		hwif->tuneproc = &cs5530_tuneproc;
		basereg = CS5530_BASEREG(hwif);
		d0_timings = inl(basereg+0);
		if (CS5530_BAD_PIO(d0_timings)) {	/* PIO timings not initialized? */
			outl(cs5530_pio_timings[(d0_timings>>31)&1][0], basereg+0);
			if (!hwif->drives[0].autotune)
				hwif->drives[0].autotune = 1;	/* needs autotuning later */
		}
		if (CS5530_BAD_PIO(inl(basereg+8))) {	/* PIO timings not initialized? */
			outl(cs5530_pio_timings[(d0_timings>>31)&1][0], basereg+8);
			if (!hwif->drives[1].autotune)
				hwif->drives[1].autotune = 1;	/* needs autotuning later */
		}
}


/* module data table */
static struct ata_pci_device chipset __initdata = {
	vendor: PCI_VENDOR_ID_CYRIX,
	device: PCI_DEVICE_ID_CYRIX_5530_IDE,
	init_chipset: pci_init_cs5530,
	init_channel: ide_init_cs5530,
	bootable: ON_BOARD,
	flags: ATA_F_DMA
};

int __init init_cs5530(void)
{
	ata_register_chipset(&chipset);

        return 0;
}
