/**** vi:set ts=8 sts=8 sw=8:************************************************
 *
 * $Id: amd74xx.c,v 2.8 2002/03/14 11:52:20 vojtech Exp $
 *
 *  Copyright (c) 2000-2002 Vojtech Pavlik
 *
 *  Based on the work of:
 *	Andre Hedrick
 */

/*
 * AMD 755/756/766/8111 IDE driver for Linux.
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

#define AMD_IDE_ENABLE		(0x00 + amd_config->base)
#define AMD_IDE_CONFIG		(0x01 + amd_config->base)
#define AMD_CABLE_DETECT	(0x02 + amd_config->base)
#define AMD_DRIVE_TIMING	(0x08 + amd_config->base)
#define AMD_8BIT_TIMING		(0x0e + amd_config->base)
#define AMD_ADDRESS_SETUP	(0x0c + amd_config->base)
#define AMD_UDMA_TIMING		(0x10 + amd_config->base)

#define AMD_UDMA		0x07
#define AMD_UDMA_33		0x01
#define AMD_UDMA_66		0x02
#define AMD_UDMA_100		0x03
#define AMD_BAD_SWDMA		0x08
#define AMD_BAD_FIFO		0x10

/*
 * AMD SouthBridge chips.
 */

static struct amd_ide_chip {
	unsigned short id;
	unsigned char rev;
	unsigned int base;
	unsigned char flags;
} amd_ide_chips[] = {
	{ PCI_DEVICE_ID_AMD_8111_IDE,  0x00, 0x40, AMD_UDMA_100 },			/* AMD-8111 */
	{ PCI_DEVICE_ID_AMD_OPUS_7441, 0x00, 0x40, AMD_UDMA_100 },			/* AMD-768 Opus */
	{ PCI_DEVICE_ID_AMD_VIPER_7411, 0x00, 0x40, AMD_UDMA_100 | AMD_BAD_FIFO },	/* AMD-766 Viper */
	{ PCI_DEVICE_ID_AMD_VIPER_7409, 0x07, 0x40, AMD_UDMA_66 },			/* AMD-756/c4+ Viper */
	{ PCI_DEVICE_ID_AMD_VIPER_7409, 0x00, 0x40, AMD_UDMA_66 | AMD_BAD_SWDMA },	/* AMD-756 Viper */
	{ PCI_DEVICE_ID_AMD_COBRA_7401, 0x00, 0x40, AMD_UDMA_33 | AMD_BAD_SWDMA },	/* AMD-755 Cobra */
	{ PCI_DEVICE_ID_NVIDIA_NFORCE_IDE, 0x00, 0x50, AMD_UDMA_100 },			/* nVidia nForce */
	{ 0 }
};

static struct amd_ide_chip *amd_config;
static unsigned char amd_enabled;
static unsigned int amd_80w;

static unsigned char amd_cyc2udma[] = { 6, 6, 5, 4, 0, 1, 1, 2, 2, 3, 3 };
#if 0
static unsigned char amd_udma2cyc[] = { 4, 6, 8, 10, 3, 2, 1, 1 };
#endif
static char *amd_dma[] = { "MWDMA16", "UDMA33", "UDMA66", "UDMA100" };

/*
 * amd_set_speed() writes timing values to the chipset registers
 */

static void amd_set_speed(struct pci_dev *dev, unsigned char dn, struct ata_timing *timing)
{
	unsigned char t;

	pci_read_config_byte(dev, AMD_ADDRESS_SETUP, &t);
	t = (t & ~(3 << ((3 - dn) << 1))) | ((FIT(timing->setup, 1, 4) - 1) << ((3 - dn) << 1));
	pci_write_config_byte(dev, AMD_ADDRESS_SETUP, t);

	pci_write_config_byte(dev, AMD_8BIT_TIMING + (1 - (dn >> 1)),
		((FIT(timing->act8b, 1, 16) - 1) << 4) | (FIT(timing->rec8b, 1, 16) - 1));

	pci_write_config_byte(dev, AMD_DRIVE_TIMING + (3 - dn),
		((FIT(timing->active, 1, 16) - 1) << 4) | (FIT(timing->recover, 1, 16) - 1));

	switch (amd_config->flags & AMD_UDMA) {
		case AMD_UDMA_33:  t = timing->udma ? (0xc0 | (FIT(timing->udma, 2, 5) - 2)) : 0x03; break;
		case AMD_UDMA_66:  t = timing->udma ? (0xc0 | amd_cyc2udma[FIT(timing->udma, 2, 10)]) : 0x03; break;
		case AMD_UDMA_100: t = timing->udma ? (0xc0 | amd_cyc2udma[FIT(timing->udma, 1, 10)]) : 0x03; break;
		default: return;
	}

	pci_write_config_byte(dev, AMD_UDMA_TIMING + (3 - dn), t);
}

/*
 * amd_set_drive() computes timing values configures the drive and
 * the chipset to a desired transfer mode. It also can be called
 * by upper layers.
 */

static int amd_set_drive(struct ata_device *drive, unsigned char speed)
{
	struct ata_device *peer = drive->channel->drives + (~drive->dn & 1);
	struct ata_timing t, p;
	int T, UT;

	if (speed != XFER_PIO_SLOW && speed != drive->current_speed)
		if (ide_config_drive_speed(drive, speed))
			printk(KERN_WARNING "ide%d: Drive %d didn't accept speed setting. Oh, well.\n",
				drive->dn >> 1, drive->dn & 1);

	T = 1000000000 / system_bus_speed;
	UT = T / min_t(int, max_t(int, amd_config->flags & AMD_UDMA, 1), 2);

	ata_timing_compute(drive, speed, &t, T, UT);

	if (peer->present) {
		ata_timing_compute(peer, peer->current_speed, &p, T, UT);
		ata_timing_merge(&p, &t, &t, IDE_TIMING_8BIT);
	}

	if (speed == XFER_UDMA_5 && system_bus_speed <= 33333) t.udma = 1;

	amd_set_speed(drive->channel->pci_dev, drive->dn, &t);

	return 0;
}

/*
 * amd74xx_tune_drive() is a callback from upper layers for
 * PIO-only tuning.
 */

static void amd74xx_tune_drive(struct ata_device *drive, u8 pio)
{
	if (!((amd_enabled >> drive->channel->unit) & 1))
		return;

	if (pio == 255) {
		amd_set_drive(drive, ata_timing_mode(drive, XFER_PIO | XFER_EPIO));
		return;
	}

	amd_set_drive(drive, XFER_PIO_0 + min_t(byte, pio, 5));
}

#ifdef CONFIG_BLK_DEV_IDEDMA
static int __init amd_modes_map(struct ata_channel *ch)
{
	short w80 = ch->udma_four;
	int map = XFER_EPIO | XFER_MWDMA | XFER_UDMA |
		  ((amd_config->flags & AMD_BAD_SWDMA) ? 0 : XFER_SWDMA) |
		  (w80 && (amd_config->flags & AMD_UDMA) >= AMD_UDMA_66 ? XFER_UDMA_66 : 0) |
		  (w80 && (amd_config->flags & AMD_UDMA) >= AMD_UDMA_100 ? XFER_UDMA_100 : 0);

	return map;
}
#endif

/*
 * The initialization callback. Here we determine the IDE chip type
 * and initialize its drive independent registers.
 */

static unsigned int __init amd74xx_init_chipset(struct pci_dev *dev)
{
	unsigned char t;
	unsigned int u;
	int i;

/*
 * Find out what AMD IDE is this.
 */

	for (amd_config = amd_ide_chips; amd_config->id; amd_config++) {
			pci_read_config_byte(dev, PCI_REVISION_ID, &t);
			if (dev->device == amd_config->id && t >= amd_config->rev)
				break;
		}

	if (!amd_config->id) {
		printk(KERN_WARNING "AMD_IDE: Unknown AMD IDE Chip, contact Vojtech Pavlik <vojtech@ucw.cz>\n");
		return -ENODEV;
	}

/*
 * Check 80-wire cable presence.
 */

	switch (amd_config->flags & AMD_UDMA) {

		case AMD_UDMA_100:
			pci_read_config_byte(dev, AMD_CABLE_DETECT, &t);
			amd_80w = ((u & 0x3) ? 1 : 0) | ((u & 0xc) ? 2 : 0);
			for (i = 24; i >= 0; i -= 8)
				if (((u >> i) & 4) && !(amd_80w & (1 << (1 - (i >> 4))))) {
					printk(KERN_WARNING "AMD_IDE: Bios didn't set cable bits corectly. Enabling workaround.\n");
					amd_80w |= (1 << (1 - (i >> 4)));
				}
			break;

		case AMD_UDMA_66:
			pci_read_config_dword(dev, AMD_UDMA_TIMING, &u);
			for (i = 24; i >= 0; i -= 8)
				if ((u >> i) & 4)
					amd_80w |= (1 << (1 - (i >> 4)));
			break;
	}

	pci_read_config_dword(dev, AMD_IDE_ENABLE, &u);
	amd_enabled = ((u & 1) ? 2 : 0) | ((u & 2) ? 1 : 0);

/*
 * Take care of prefetch & postwrite.
 */

	pci_read_config_byte(dev, AMD_IDE_CONFIG, &t);
	pci_write_config_byte(dev, AMD_IDE_CONFIG,
		(amd_config->flags & AMD_BAD_FIFO) ? (t & 0x0f) : (t | 0xf0));

/*
 * Print the boot message.
 */

	pci_read_config_byte(dev, PCI_REVISION_ID, &t);
	printk(KERN_INFO "AMD_IDE: %s (rev %02x) %s controller on pci%s\n",
		dev->name, t, amd_dma[amd_config->flags & AMD_UDMA], dev->slot_name);

	return 0;
}

static unsigned int __init amd74xx_ata66_check(struct ata_channel *hwif)
{
	return ((amd_enabled & amd_80w) >> hwif->unit) & 1;
}

static void __init amd74xx_init_channel(struct ata_channel *hwif)
{
	int i;

	hwif->udma_four = amd74xx_ata66_check(hwif);

	hwif->tuneproc = &amd74xx_tune_drive;
	hwif->speedproc = &amd_set_drive;

	hwif->io_32bit = 1;
	hwif->unmask = 1;

	for (i = 0; i < 2; i++) {
		hwif->drives[i].autotune = 1;
		hwif->drives[i].dn = hwif->unit * 2 + i;
	}

#ifdef CONFIG_BLK_DEV_IDEDMA
	if (hwif->dma_base) {
		hwif->highmem = 1;
		hwif->modes_map = amd_modes_map(hwif);
		hwif->udma_setup = udma_generic_setup;
	}
#endif
}

/*
 * We allow the BM-DMA driver only work on enabled interfaces.
 */
static void __init amd74xx_init_dma(struct ata_channel *ch, unsigned long dmabase)
{
	if ((amd_enabled >> ch->unit) & 1)
		ata_init_dma(ch, dmabase);
}


/* module data table */
static struct ata_pci_device chipsets[] __initdata = {
	{
		.vendor = PCI_VENDOR_ID_AMD,
		.device = PCI_DEVICE_ID_AMD_COBRA_7401,
		.init_chipset = amd74xx_init_chipset,
		.init_channel = amd74xx_init_channel,
		.init_dma = amd74xx_init_dma,
		.enablebits = {{0x40,0x01,0x01}, {0x40,0x02,0x02}},
		.bootable = ON_BOARD
	},
	{
		.vendor = PCI_VENDOR_ID_AMD,
		.device = PCI_DEVICE_ID_AMD_VIPER_7409,
		.init_chipset = amd74xx_init_chipset,
		.init_channel = amd74xx_init_channel,
		.init_dma = amd74xx_init_dma,
		.enablebits = {{0x40,0x01,0x01}, {0x40,0x02,0x02}},
		.bootable = ON_BOARD,
		.flags = ATA_F_SIMPLEX
	},
	{
		.vendor = PCI_VENDOR_ID_AMD,
		.device = PCI_DEVICE_ID_AMD_VIPER_7411,
		.init_chipset = amd74xx_init_chipset,
		.init_channel = amd74xx_init_channel,
		.init_dma = amd74xx_init_dma,
		.enablebits = {{0x40,0x01,0x01}, {0x40,0x02,0x02}},
		.bootable = ON_BOARD
	},
	{
		.vendor = PCI_VENDOR_ID_AMD,
		.device = PCI_DEVICE_ID_AMD_OPUS_7441,
		.init_chipset = amd74xx_init_chipset,
		.init_channel = amd74xx_init_channel,
		.init_dma = amd74xx_init_dma,
		.enablebits = {{0x40,0x01,0x01}, {0x40,0x02,0x02}},
		.bootable = ON_BOARD
	},
	{
		.vendor = PCI_VENDOR_ID_AMD,
		.device = PCI_DEVICE_ID_AMD_8111_IDE,
		.init_chipset = amd74xx_init_chipset,
		.init_channel = amd74xx_init_channel,
		.init_dma = amd74xx_init_dma,
		.enablebits = {{0x40,0x01,0x01}, {0x40,0x02,0x02}},
		.bootable = ON_BOARD
	},
	{
		.vendor = PCI_VENDOR_ID_NVIDIA,
		.device = PCI_DEVICE_ID_NVIDIA_NFORCE_IDE,
		.init_chipset = amd74xx_init_chipset,
		.init_channel = amd74xx_init_channel,
		.init_dma = amd74xx_init_dma,
		.enablebits = {{0x50,0x01,0x01}, {0x50,0x02,0x02}},
		.bootable = ON_BOARD
	},
};

int __init init_amd74xx(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(chipsets); ++i)
		ata_register_chipset(&chipsets[i]);

        return 0;
}
