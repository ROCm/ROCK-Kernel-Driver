/*
 *
 * $Id: aec62xx.c,v 1.0 2002/05/24 14:37:19 vojtech Exp $
 *
 *  Copyright (c) 2002 Vojtech Pavlik
 *
 *  Based on the work of:
 *	Andre Hedrick
 */

/*
 * AEC 6210UF (ATP850UF), AEC6260 (ATP860) and AEC6280 (ATP865) IDE driver for Linux.
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
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

#define AEC_DRIVE_TIMING	0x40
#define AEC_UDMA_NEW		0x44
#define AEC_MISC		0x49
#define AEC_IDE_ENABLE		0x4a
#define AEC_UDMA_OLD		0x54

static unsigned char aec_cyc2udma[17] = { 0, 0, 7, 6, 5, 4, 4, 3, 3, 2, 2, 2, 2, 1, 1, 1, 1 };

/*
 * aec_set_speed_old() writes timing values to
 * the chipset registers for ATP850UF
 */

static void aec_set_speed_old(struct pci_dev *dev, unsigned char dn, struct ata_timing *timing)
{
	unsigned char t;

	pci_write_config_byte(dev, AEC_DRIVE_TIMING + (dn << 1),     FIT(timing->active, 0, 15));
	pci_write_config_byte(dev, AEC_DRIVE_TIMING + (dn << 1) + 1, FIT(timing->recover, 0, 15));

	pci_read_config_byte(dev, AEC_UDMA_OLD, &t);
	t &= ~(3 << (dn << 1));
	if (timing->udma)
		t |= (5 - FIT(timing->udma, 2, 4)) << (dn << 1);
	pci_write_config_byte(dev, AEC_UDMA_OLD, t);
}

/*
 * aec_set_speed_new() writes timing values to the chipset registers for all
 * other Artop chips
 */

static void aec_set_speed_new(struct pci_dev *dev, unsigned char dn, struct ata_timing *timing)
{
	unsigned char t;

	pci_write_config_byte(dev, AEC_DRIVE_TIMING + dn,
		(FIT(timing->active, 0, 15) << 4) | FIT(timing->recover, 0, 15));

	pci_read_config_byte(dev, AEC_UDMA_NEW + (dn >> 1), &t);
	t &= ~(0xf << ((dn & 1) << 2));
	if (timing->udma)
		t |= aec_cyc2udma[FIT(timing->udma, 2, 16)] << ((dn & 1) << 2);
	pci_write_config_byte(dev, AEC_UDMA_NEW + (dn >> 1), t);
}

/*
 * aec_set_drive() computes timing values configures the drive and
 * the chipset to a desired transfer mode. It also can be called
 * by upper layers.
 */

static int aec_set_drive(struct ata_device *drive, unsigned char speed)
{
	struct ata_timing t;
	int T, UT;
	int aec_old;

	aec_old = (drive->channel->pci_dev->device == PCI_DEVICE_ID_ARTOP_ATP850UF);

	if (speed != XFER_PIO_SLOW && speed != drive->current_speed)
		if (ide_config_drive_speed(drive, speed))
			printk(KERN_WARNING "ide%d: Drive %d didn't accept speed setting. Oh, well.\n",
				drive->dn >> 1, drive->dn & 1);

	T = 1000000000 / system_bus_speed;
	UT = T / (aec_old ? 1 : 4);

	ata_timing_compute(drive, speed, &t, T, UT);

	if (aec_old)
		aec_set_speed_old(drive->channel->pci_dev, drive->dn, &t);
	else
		aec_set_speed_new(drive->channel->pci_dev, drive->dn, &t);

	drive->current_speed = speed;

	return 0;
}

/*
 * aec62xx_tune_drive() is a callback from upper layers for
 * PIO-only tuning.
 */

static void aec62xx_tune_drive(struct ata_device *drive, unsigned char pio)
{
	if (pio == 255) {
		aec_set_drive(drive, ata_timing_mode(drive, XFER_PIO | XFER_EPIO));
		return;
	}

	aec_set_drive(drive, XFER_PIO_0 + min_t(byte, pio, 5));
}

#ifdef CONFIG_BLK_DEV_IDEDMA
static int aec62xx_dmaproc(struct ata_device *drive)
{
	short speed;
	int map;

	map = XFER_PIO | XFER_EPIO | XFER_MWDMA | XFER_UDMA | XFER_SWDMA | XFER_UDMA;

	if (drive->channel->udma_four)
		switch (drive->channel->pci_dev->device) {
			case PCI_DEVICE_ID_ARTOP_ATP865R:
			case PCI_DEVICE_ID_ARTOP_ATP865:
				map |= XFER_UDMA_100 | XFER_UDMA_133;
			case PCI_DEVICE_ID_ARTOP_ATP860R:
			case PCI_DEVICE_ID_ARTOP_ATP860:
				map |= XFER_UDMA_66;
		}

	speed = ata_timing_mode(drive, map);
	aec_set_drive(drive, speed);
	udma_enable(drive, drive->channel->autodma && (speed & XFER_MODE) != XFER_PIO, 0);

	return 0;
}
#endif

/*
 * The initialization callback. Here we determine the IDE chip type
 * and initialize its drive independent registers.
 */

static unsigned int __init aec62xx_init_chipset(struct pci_dev *dev)
{
	unsigned char t;

/*
 * Initialize if needed.
 */

	switch (dev->device) {

		case PCI_DEVICE_ID_ARTOP_ATP865R:
		case PCI_DEVICE_ID_ARTOP_ATP865:

			/* Clear reset and test bits. */
			pci_read_config_byte(dev, AEC_MISC, &t);
			pci_write_config_byte(dev, AEC_MISC, t & ~0x30);

			/* Enable chip interrupt output. */
			pci_read_config_byte(dev, AEC_IDE_ENABLE, &t);
			pci_write_config_byte(dev, AEC_IDE_ENABLE, t & ~0x01);

#ifdef CONFIG_AEC6280_BURST
			/* Must be greater than 0x80 for burst mode. */
			pci_write_config_byte(dev, PCI_LATENCY_TIMER, 0x90);

			/* Enable burst mode. */
			pci_read_config_byte(dev, AEC_IDE_ENABLE, &t);
			pci_write_config_byte(dev, AEC_IDE_ENABLE, t | 0x80);

#endif
	}

/*
 * Print the boot message.
 */

	pci_read_config_byte(dev, PCI_REVISION_ID, &t);
	printk(KERN_INFO "AEC_IDE: %s (rev %02x) controller on pci%s\n",
		dev->name, t, dev->slot_name);

	return 0;
}

static unsigned int __init aec62xx_ata66_check(struct ata_channel *ch)
{
	unsigned char t;

	if (ch->pci_dev->device == PCI_DEVICE_ID_ARTOP_ATP850UF)
		return 0;

	pci_read_config_byte(ch->pci_dev, AEC_MISC, &t);
	return ((t & (1 << ch->unit)) ? 0 : 1);
}

static void __init aec62xx_init_channel(struct ata_channel *ch)
{
	int i;

	ch->tuneproc = &aec62xx_tune_drive;
	ch->speedproc = &aec_set_drive;
	ch->autodma = 0;

	ch->io_32bit = 1;
	ch->unmask = 1;

	for (i = 0; i < 2; i++) {
		ch->drives[i].autotune = 1;
		ch->drives[i].dn = ch->unit * 2 + i;
	}

#ifdef CONFIG_BLK_DEV_IDEDMA
	if (ch->dma_base) {
		ch->highmem = 1;
		ch->XXX_udma = aec62xx_dmaproc;
#ifdef CONFIG_IDEDMA_AUTO
		if (!noautodma)
			ch->autodma = 1;
#endif
	}
#endif
}

/*
 * We allow the BM-DMA driver only work on enabled interfaces.
 */
static void __init aec62xx_init_dma(struct ata_channel *ch, unsigned long dmabase)
{
	unsigned char t;

	pci_read_config_byte(ch->pci_dev, AEC_IDE_ENABLE, &t);
	if (t & (1 << ((ch->unit << 1) + 2)))
		ata_init_dma(ch, dmabase);
}

/* module data table */
static struct ata_pci_device chipsets[] __initdata = {
	{
		vendor: PCI_VENDOR_ID_ARTOP,
		device: PCI_DEVICE_ID_ARTOP_ATP850UF,
		init_chipset: aec62xx_init_chipset,
		init_channel: aec62xx_init_channel,
		init_dma: aec62xx_init_dma,
		enablebits: { {0x4a,0x02,0x02},	{0x4a,0x04,0x04} },
		bootable: OFF_BOARD,
		flags: ATA_F_SER | ATA_F_IRQ | ATA_F_DMA
	},
	{
		vendor: PCI_VENDOR_ID_ARTOP,
		device: PCI_DEVICE_ID_ARTOP_ATP860,
		init_chipset: aec62xx_init_chipset,
		ata66_check: aec62xx_ata66_check,
		init_channel: aec62xx_init_channel,
		enablebits: { {0x4a,0x02,0x02},	{0x4a,0x04,0x04} },
		bootable: NEVER_BOARD,
		flags: ATA_F_IRQ | ATA_F_NOADMA | ATA_F_DMA
	},
	{
		vendor: PCI_VENDOR_ID_ARTOP,
		device: PCI_DEVICE_ID_ARTOP_ATP860R,
		init_chipset: aec62xx_init_chipset,
		ata66_check: aec62xx_ata66_check,
		init_channel: aec62xx_init_channel,
		enablebits: { {0x4a,0x02,0x02},	{0x4a,0x04,0x04} },
		bootable: OFF_BOARD,
		flags: ATA_F_IRQ | ATA_F_DMA
	},
	{
		vendor: PCI_VENDOR_ID_ARTOP,
		device: PCI_DEVICE_ID_ARTOP_ATP865,
		init_chipset: aec62xx_init_chipset,
		ata66_check: aec62xx_ata66_check,
		init_channel: aec62xx_init_channel,
		enablebits: { {0x4a,0x02,0x02},	{0x4a,0x04,0x04} },
		bootable: NEVER_BOARD,
		flags: ATA_F_IRQ | ATA_F_DMA
	},
	{
		vendor: PCI_VENDOR_ID_ARTOP,
		device: PCI_DEVICE_ID_ARTOP_ATP865R,
		init_chipset: aec62xx_init_chipset,
		ata66_check: aec62xx_ata66_check,
		init_channel: aec62xx_init_channel,
		enablebits: { {0x4a,0x02,0x02},	{0x4a,0x04,0x04} },
		bootable: OFF_BOARD,
		flags: ATA_F_IRQ | ATA_F_DMA
	}
};

int __init init_aec62xx(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(chipsets); i++)
		ata_register_chipset(chipsets + i);

	return 0;
}
