/**** vi:set ts=8 sts=8 sw=8:************************************************
 *
 * $Id: piix.c,v 1.3 2002/03/29 16:06:06 vojtech Exp $
 *
 *  Copyright (c) 2000-2002 Vojtech Pavlik
 *
 *  Based on the work of:
 *	Andrzej Krzysztofowicz
 *      Andre Hedrick
 *
 *  Thanks to Daniela Egbert for advice on PIIX bugs.
 */

/*
 * Intel PIIX/ICH and Efar Victory66 IDE driver for Linux.
 *
 * UDMA66 and higher modes are autoenabled only in case the BIOS has detected a
 * 80 wire cable. To ignore the BIOS data and assume the cable is present, use
 * 'ide0=ata66' or 'ide1=ata66' on the kernel command line.
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Should you need to contact me, the author, you can do so either by
 * e-mail - mail your message to <vojtech@ucw.cz>, or by paper mail:
 * Vojtech Pavlik, Simunkova 1594, Prague 8, 182 00 Czech Republic
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/blkdev.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/ide.h>

#include <asm/io.h>

#include "ata-timing.h"
#include "pcihost.h"

#define PIIX_IDETIM0		0x40
#define PIIX_IDETIM1		0x42
#define PIIX_SIDETIM		0x44
#define PIIX_IDESTAT		0x47
#define PIIX_UDMACTL		0x48
#define PIIX_UDMATIM		0x4a
#define PIIX_IDECFG		0x54

#define PIIX_UDMA		0x07
#define PIIX_UDMA_NONE		0x00
#define PIIX_UDMA_33		0x01
#define PIIX_UDMA_66		0x02
#define PIIX_UDMA_100		0x03
#define PIIX_UDMA_133		0x04
#define PIIX_NO_SITRE		0x08	/* Chip doesn't have separate slave timing */
#define PIIX_PINGPONG		0x10	/* Enable ping-pong buffers */
#define PIIX_VICTORY		0x20	/* Efar Victory66 has a different UDMA setup */
#define PIIX_CHECK_REV		0x40	/* May be a buggy revision of PIIX */
#define PIIX_NODMA		0x80	/* Don't do DMA with this chip */

#ifdef CONFIG_BLK_DEV_PIIX_TRY133	/* I think even the older ICHs should be able to do UDMA133 */
#undef PIIX_UDMA_100
#define PIIX_UDMA_100 PIIX_UDMA_133
#endif

/*
 * Intel IDE chips
 */

static struct piix_ide_chip {
	unsigned short id;
	unsigned char flags;
} piix_ide_chips[] = {
	{ PCI_DEVICE_ID_INTEL_82801DB_9,	PIIX_UDMA_133 | PIIX_PINGPONG },                    /* Intel 82801DB ICH4 */
	{ PCI_DEVICE_ID_INTEL_82801CA_11,	PIIX_UDMA_100 | PIIX_PINGPONG },                    /* Intel 82801CA ICH3/ICH3-S */
	{ PCI_DEVICE_ID_INTEL_82801CA_10,	PIIX_UDMA_100 | PIIX_PINGPONG },                    /* Intel 82801CAM ICH3-M */
	{ PCI_DEVICE_ID_INTEL_82801E_9,		PIIX_UDMA_100 | PIIX_PINGPONG },                    /* Intel 82801E C-ICH */
	{ PCI_DEVICE_ID_INTEL_82801BA_9,	PIIX_UDMA_100 | PIIX_PINGPONG },                    /* Intel 82801BA ICH2 */
	{ PCI_DEVICE_ID_INTEL_82801BA_8,	PIIX_UDMA_100 | PIIX_PINGPONG },                    /* Intel 82801BAM ICH2-M */
	{ PCI_DEVICE_ID_INTEL_82801AB_1,	PIIX_UDMA_33  | PIIX_PINGPONG },                    /* Intel 82801AB ICH0 */
	{ PCI_DEVICE_ID_INTEL_82801AA_1,	PIIX_UDMA_66  | PIIX_PINGPONG },                    /* Intel 82801AA ICH */
	{ PCI_DEVICE_ID_INTEL_82372FB_1,	PIIX_UDMA_66 },	                                    /* Intel 82372FB PIIX5 */
	{ PCI_DEVICE_ID_INTEL_82443MX_1,	PIIX_UDMA_33 },                                     /* Intel 82443MX MPIIX4 */
	{ PCI_DEVICE_ID_INTEL_82371AB,		PIIX_UDMA_33 },                                     /* Intel 82371AB/EB PIIX4/PIIX4E */
	{ PCI_DEVICE_ID_INTEL_82371SB_1,	PIIX_UDMA_NONE },                                   /* Intel 82371SB PIIX3 */
	{ PCI_DEVICE_ID_INTEL_82371FB_1,	PIIX_UDMA_NONE | PIIX_NO_SITRE | PIIX_CHECK_REV },  /* Intel 82371FB PIIX */
	{ PCI_DEVICE_ID_EFAR_SLC90E66_1,	PIIX_UDMA_66 | PIIX_VICTORY },                      /* Efar Victory66 */
	{ 0 }
};

static struct piix_ide_chip *piix_config;
static unsigned char piix_enabled;
static unsigned int piix_80w;

static char *piix_dma[] = { "MWDMA16", "UDMA33", "UDMA66", "UDMA100", "UDMA133" };

/*
 * piix_set_speed() writes timing values to the chipset registers
 */

static void piix_set_speed(struct pci_dev *dev, unsigned char dn, struct ata_timing *timing, int umul)
{
	unsigned short t;
	unsigned char u;
	unsigned int c;

	pci_read_config_word(dev, PIIX_IDETIM0 + (dn & 2), &t);

	switch (dn & 1) {

		case 1: 
			if (timing->cycle > 9) {
				t &= ~0x30;
				break;
			}

			if (~piix_config->flags & PIIX_NO_SITRE) {
				pci_read_config_byte(dev, PIIX_SIDETIM, &u);
				u &= ~(0xf << ((dn & 2) << 1));
				t |= 0x30;
				u |= (4 - FIT(timing->recover, 1, 4)) << ((dn & 2) << 1);
				u |= (5 - FIT(timing->active, 2, 5)) << (((dn & 2) << 1) + 2);
				pci_write_config_byte(dev, PIIX_SIDETIM, u);
				break;
			}

		case 0:
			if ((~dn & 1) && timing->cycle > 9) {
				t &= ~0x03;
				break;
			}

			t &= 0xccff;
			t |= 0x03 << ((dn & 1) << 2);
			t |= (4 - FIT(timing->recover, 1, 4)) << 8;
			t |= (5 - FIT(timing->active, 2, 5)) << 12;
	}

	pci_write_config_word(dev, PIIX_IDETIM0 + (dn & 2), t);

	if (!(piix_config->flags & PIIX_UDMA)) return;

	pci_read_config_byte(dev, PIIX_UDMACTL, &u);
	u &= ~(1 << dn);

	if (timing->udma) {

		u |= 1 << dn;

		pci_read_config_word(dev, PIIX_UDMATIM, &t);

		if (piix_config->flags & PIIX_VICTORY) {
			t &= ~(0x07 << (dn << 2));
			t |= (8 - FIT(timing->udma, 2, 8)) << (dn << 2);
		} else {
			t &= ~(0x03 << (dn << 2));
			t |= (4 - FIT(timing->udma, 2, 4)) << (dn << 2);
		}

		pci_write_config_word(dev, PIIX_UDMATIM, t);

		if ((piix_config->flags & PIIX_UDMA) > PIIX_UDMA_33
			&& ~piix_config->flags & PIIX_VICTORY) {

			pci_read_config_dword(dev, PIIX_IDECFG, &c);
			
			if ((piix_config->flags & PIIX_UDMA) > PIIX_UDMA_66)
				c &= ~(1 << (dn + 12));
			c &= ~(1 << dn);

			switch (umul) {
				case 2: c |= 1 << dn;		break;
				case 4: c |= 1 << (dn + 12);	break;
			}

			pci_write_config_dword(dev, PIIX_IDECFG, c);
		}
	}

	pci_write_config_byte(dev, PIIX_UDMACTL, u);
}

/*
 * piix_set_drive() computes timing values configures the drive and
 * the chipset to a desired transfer mode. It also can be called
 * by upper layers.
 */

static int piix_set_drive(struct ata_device *drive, unsigned char speed)
{
	struct ata_device *peer = drive->channel->drives + (~drive->dn & 1);
	struct ata_timing t, p;
	int err, T, UT, umul = 1;

	if (speed != XFER_PIO_SLOW && speed != drive->current_speed)
		if ((err = ide_config_drive_speed(drive, speed)))
			return err;

	if (speed > XFER_UDMA_2 && (piix_config->flags & PIIX_UDMA) >= PIIX_UDMA_66)
		umul = 2;
	if (speed > XFER_UDMA_4 && (piix_config->flags & PIIX_UDMA) >= PIIX_UDMA_100)
		umul = 4;
	
	T = 1000000000 / system_bus_speed;
	UT = T / umul;

	ata_timing_compute(drive, speed, &t, T, UT);

	if ((piix_config->flags & PIIX_NO_SITRE) && peer->present) {
			ata_timing_compute(peer, peer->current_speed, &p, T, UT);
			if (t.cycle <= 9 && p.cycle <= 9)
				ata_timing_merge(&p, &t, &t, IDE_TIMING_ALL);
	}

	piix_set_speed(drive->channel->pci_dev, drive->dn, &t, umul);

	drive->current_speed = speed;

	return 0;
}

/*
 * piix_tune_drive() is a callback from upper layers for
 * PIO-only tuning.
 */

static void piix_tune_drive(struct ata_device *drive, unsigned char pio)
{
	if (!((piix_enabled >> drive->channel->unit) & 1))
		return;

	if (pio == 255) {
		piix_set_drive(drive, ata_timing_mode(drive, XFER_PIO | XFER_EPIO));
		return;
	}

	piix_set_drive(drive, XFER_PIO_0 + min_t(byte, pio, 5));
}

#ifdef CONFIG_BLK_DEV_IDEDMA

int piix_dmaproc(struct ata_device *drive)
{
	short w80 = drive->channel->udma_four;

	short speed = ata_timing_mode(drive,
			XFER_PIO | XFER_EPIO |
			(piix_config->flags & PIIX_NODMA ? 0 : (XFER_SWDMA | XFER_MWDMA |
			(piix_config->flags & PIIX_UDMA ? XFER_UDMA : 0) |
			(w80 && (piix_config->flags & PIIX_UDMA) >= PIIX_UDMA_66 ? XFER_UDMA_66 : 0) |
			(w80 && (piix_config->flags & PIIX_UDMA) >= PIIX_UDMA_100 ? XFER_UDMA_100 : 0) |
			(w80 && (piix_config->flags & PIIX_UDMA) >= PIIX_UDMA_133 ? XFER_UDMA_133 : 0))));

	piix_set_drive(drive, speed);

	udma_enable(drive, drive->channel->autodma && (speed & XFER_MODE) != XFER_PIO, 0);

	return 0;
}

#endif

/*
 * The initialization callback. Here we determine the IDE chip type
 * and initialize its drive independent registers.
 */
static unsigned int __init piix_init_chipset(struct pci_dev *dev)
{
	unsigned int u;
	unsigned short w;
	unsigned char t;
	int i;

/*
 * Find out which Intel IDE this is.
 */

	for (piix_config = piix_ide_chips; piix_config->id != 0; ++piix_config)
		if (dev->device == piix_config->id)
			break;

	if (!piix_config->id) {
		printk(KERN_WARNING "PIIX: Unknown PIIX/ICH chip %#x, contact Vojtech Pavlik <vojtech@ucw.cz>\n", dev->device);
		return -ENODEV;
	}

/*
 * Check for possibly broken DMA configs.
 */

	{
		struct pci_dev *orion = NULL;

		if (piix_config->flags & PIIX_CHECK_REV) {
			pci_read_config_byte(dev, PCI_REVISION_ID, &t);
			if (t < 2) {
				printk(KERN_INFO "PIIX: Found buggy old PIIX rev %#x, disabling DMA\n", t);
				piix_config->flags |= PIIX_NODMA;
			}
		}

		if ((orion = pci_find_device(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82454GX, NULL))) {
			pci_read_config_byte(orion, PCI_REVISION_ID, &t);
			if (t < 4) {
				printk(KERN_INFO "PIIX: Found buggy 82454GX Orion bridge rev %#x, disabling DMA\n", t);
				piix_config->flags |= PIIX_NODMA;
			}
		}
	}

/*
 * Check 80-wire cable presence.
 */

	switch (piix_config->flags & PIIX_UDMA) {

		case PIIX_UDMA_66:
			if (piix_config->flags && PIIX_VICTORY) {
				pci_read_config_byte(dev, PIIX_IDESTAT, &t);
				piix_80w = ((t & 2) ? 1 : 0) | ((t & 1) ? 2 : 0);
				break;
			}

#ifndef CONFIG_BLK_DEV_PIIX_TRY133
		case PIIX_UDMA_100:
#endif
		case PIIX_UDMA_133:
			pci_read_config_dword(dev, PIIX_IDECFG, &u);
			piix_80w = ((u & 0x30) ? 1 : 0) | ((u & 0xc0) ? 2 : 0);
			break;
	}

/*
 * Enable ping-pong buffers where applicable.
 */

	if (piix_config->flags & PIIX_PINGPONG) {
		pci_read_config_dword(dev, PIIX_IDECFG, &u);
		u |= 0x400;
		pci_write_config_dword(dev, PIIX_IDECFG, u);
	}

/*
 * Detect enabled interfaces, enable slave separate timing if possible.
 */

	for (i = 0; i < 2; i++) {
		pci_read_config_word(dev, PIIX_IDETIM0 + (i << 1), &w);
		piix_enabled |= (w & 0x8000) ? (1 << i) : 0;
		w &= 0x8c00;
		if (~piix_config->flags & PIIX_NO_SITRE) w |= 0x4000;
		w |= 0x44;
		pci_write_config_word(dev, PIIX_IDETIM0 + (i << 1), w);
	}

/*
 * Print the boot message.
 */

	printk(KERN_INFO "PIIX: %s %s controller on pci%s\n",
		dev->name, piix_dma[piix_config->flags & PIIX_UDMA], dev->slot_name);

	return 0;
}

static unsigned int __init piix_ata66_check(struct ata_channel *hwif)
{
	return ((piix_enabled & piix_80w) >> hwif->unit) & 1;
}

static void __init piix_init_channel(struct ata_channel *hwif)
{
	int i;

	hwif->tuneproc = &piix_tune_drive;
	hwif->speedproc = &piix_set_drive;
	hwif->autodma = 0;
	hwif->io_32bit = 1;
	hwif->unmask = 1;
	for (i = 0; i < 2; i++) {
		hwif->drives[i].autotune = 1;
		hwif->drives[i].dn = hwif->unit * 2 + i;
	}

#ifdef CONFIG_BLK_DEV_IDEDMA
	if (hwif->dma_base) {
		hwif->highmem = 1;
		hwif->XXX_udma = piix_dmaproc;
# ifdef CONFIG_IDEDMA_AUTO
		if (!noautodma)
			hwif->autodma = 1;
# endif
	}
#endif
}

/*
 * We allow the BM-DMA driver only work on enabled interfaces,
 * and only if DMA is safe with the chip and bridge.
 */
static void __init piix_init_dma(struct ata_channel *hwif, unsigned long dmabase)
{
	if (((piix_enabled >> hwif->unit) & 1)
		&& !(piix_config->flags & PIIX_NODMA))
			ata_init_dma(hwif, dmabase);
}



/* module data table */
static struct ata_pci_device chipsets[] __initdata = {
	{
		vendor: PCI_VENDOR_ID_INTEL,
		device: PCI_DEVICE_ID_INTEL_82371FB_1,
		init_chipset: piix_init_chipset,
		ata66_check: piix_ata66_check,
		init_channel: piix_init_channel,
		init_dma: piix_init_dma,
		enablebits: {{0x41,0x80,0x80}, {0x43,0x80,0x80}},
		bootable: ON_BOARD
	},
	{
		vendor: PCI_VENDOR_ID_INTEL,
		device: PCI_DEVICE_ID_INTEL_82371SB_1,
		init_chipset: piix_init_chipset,
		ata66_check: piix_ata66_check,
		init_channel: piix_init_channel,
		init_dma: piix_init_dma,
		enablebits: {{0x41,0x80,0x80}, {0x43,0x80,0x80}},
		bootable: ON_BOARD
	},
	{
		vendor: PCI_VENDOR_ID_INTEL,
		device: PCI_DEVICE_ID_INTEL_82371AB,
		init_chipset: piix_init_chipset,
		ata66_check: piix_ata66_check,
		init_channel: piix_init_channel,
		init_dma: piix_init_dma,
		enablebits: {{0x41,0x80,0x80}, {0x43,0x80,0x80}},
		bootable: ON_BOARD
	},
	{
		vendor: PCI_VENDOR_ID_INTEL,
		device: PCI_DEVICE_ID_INTEL_82443MX_1,
		init_chipset: piix_init_chipset,
		ata66_check: piix_ata66_check,
		init_channel: piix_init_channel,
		init_dma: piix_init_dma,
		enablebits: {{0x41,0x80,0x80}, {0x43,0x80,0x80}},
		bootable: ON_BOARD
	},
	{
		vendor: PCI_VENDOR_ID_INTEL,
		device: PCI_DEVICE_ID_INTEL_82372FB_1,
		init_chipset: piix_init_chipset,
		ata66_check: piix_ata66_check,
		init_channel: piix_init_channel,
		init_dma: piix_init_dma,
		enablebits: {{0x41,0x80,0x80}, {0x43,0x80,0x80}},
		bootable: ON_BOARD
	},
	{
		vendor: PCI_VENDOR_ID_INTEL,
		device: PCI_DEVICE_ID_INTEL_82801AA_1,
		init_chipset: piix_init_chipset,
		ata66_check: piix_ata66_check,
		init_channel: piix_init_channel,
		init_dma: piix_init_dma,
		enablebits: {{0x41,0x80,0x80}, {0x43,0x80,0x80}},
		bootable: ON_BOARD
	},
	{
		vendor: PCI_VENDOR_ID_INTEL,
		device: PCI_DEVICE_ID_INTEL_82801AB_1,
		init_chipset: piix_init_chipset,
		ata66_check: piix_ata66_check,
		init_channel: piix_init_channel,
		init_dma: piix_init_dma,
		enablebits: {{0x41,0x80,0x80}, {0x43,0x80,0x80}},
		bootable: ON_BOARD
	},
	{
		vendor: PCI_VENDOR_ID_INTEL,
		device: PCI_DEVICE_ID_INTEL_82801BA_9,
		init_chipset: piix_init_chipset,
		ata66_check: piix_ata66_check,
		init_channel: piix_init_channel,
		init_dma: piix_init_dma,
		enablebits: {{0x41,0x80,0x80}, {0x43,0x80,0x80}},
		bootable: ON_BOARD
	},
	{
		vendor: PCI_VENDOR_ID_INTEL,
		device: PCI_DEVICE_ID_INTEL_82801BA_8,
		init_chipset: piix_init_chipset,
		ata66_check: piix_ata66_check,
		init_channel: piix_init_channel,
		init_dma: piix_init_dma,
		enablebits: {{0x41,0x80,0x80}, {0x43,0x80,0x80}},
		bootable: ON_BOARD
	},
	{
		vendor: PCI_VENDOR_ID_INTEL,
		device: PCI_DEVICE_ID_INTEL_82801E_9,
		init_chipset: piix_init_chipset,
		ata66_check: piix_ata66_check,
		init_channel: piix_init_channel,
		init_dma: piix_init_dma,
		enablebits: {{0x41,0x80,0x80}, {0x43,0x80,0x80}},
		bootable: ON_BOARD
	},
	{
		vendor: PCI_VENDOR_ID_INTEL,
		device: PCI_DEVICE_ID_INTEL_82801CA_10,
		init_chipset: piix_init_chipset,
		ata66_check: piix_ata66_check,
		init_channel: piix_init_channel,
		init_dma: piix_init_dma,
		enablebits: {{0x41,0x80,0x80}, {0x43,0x80,0x80}},
		bootable: ON_BOARD
	},
	{
		vendor: PCI_VENDOR_ID_INTEL,
		device: PCI_DEVICE_ID_INTEL_82801CA_11,
		init_chipset: piix_init_chipset,
		ata66_check: piix_ata66_check,
		init_channel: piix_init_channel,
		init_dma: piix_init_dma,
		enablebits: {{0x41,0x80,0x80}, {0x43,0x80,0x80}},
		bootable: ON_BOARD
	},
	{
		vendor: PCI_VENDOR_ID_INTEL,
		device: PCI_DEVICE_ID_INTEL_82801DB_9,
		init_chipset: piix_init_chipset,
		ata66_check: piix_ata66_check,
		init_channel: piix_init_channel,
		init_dma: piix_init_dma,
		enablebits: {{0x41,0x80,0x80}, {0x43,0x80,0x80}},
		bootable: ON_BOARD
	},
	{
		vendor: PCI_VENDOR_ID_EFAR,
		device: PCI_DEVICE_ID_EFAR_SLC90E66_1,
		init_chipset: piix_init_chipset,
		ata66_check: piix_ata66_check,
		init_channel: piix_init_channel,
		init_dma: piix_init_dma,
		enablebits: {{0x41,0x80,0x80}, {0x43,0x80,0x80}},
		bootable: ON_BOARD
	},
};

int __init init_piix(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(chipsets); ++i) {
		ata_register_chipset(&chipsets[i]);
	}

	return 0;
}
