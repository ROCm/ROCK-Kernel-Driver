/*
 *
 * aec62xx.c, v1.2 2002/05/24
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

#define AEC_BM_STAT_PCH		0x02
#define AEC_BM_STAT_SCH		0x0a

#define AEC_PLLCLK_ATA133	0x10
#define AEC_CABLEPINS_INPUT	0x10

static unsigned char aec_cyc2udma[9] = { 5, 5, 5, 4, 3, 2, 2, 1, 1 };
static unsigned char aec_cyc2act[16] = { 1, 1, 2, 3, 4, 5, 6, 0, 0, 7,  7,  7, 7,  7,  7,  7 };
static unsigned char aec_cyc2rec[16] = { 1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 0, 12, 13, 14 };

/*
 * aec_set_speed_old() writes timing values to
 * the chipset registers for ATP850UF
 */

static void aec_set_speed_old(struct pci_dev *dev, unsigned char dn, struct ata_timing *timing)
{
	unsigned char t;

	pci_write_config_byte(dev, AEC_DRIVE_TIMING + (dn << 1),
		aec_cyc2act[FIT(timing->active, 0, 15)]);
	pci_write_config_byte(dev, AEC_DRIVE_TIMING + (dn << 1) + 1,
		aec_cyc2rec[FIT(timing->recover, 0, 15)]);

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
		(aec_cyc2act[FIT(timing->active, 0, 15)] << 4)
		| aec_cyc2rec[FIT(timing->recover, 0, 15)]);

	pci_read_config_byte(dev, AEC_UDMA_NEW + (dn >> 1), &t);
	t &= ~(0xf << ((dn & 1) << 2));
	if (timing->udma) {
		if (timing->udma >= 2)
			t |= aec_cyc2udma[FIT(timing->udma, 2, 8)] << ((dn & 1) << 2);
		if (timing->mode == XFER_UDMA_5)
			t |= 6;
		if (timing->mode == XFER_UDMA_6)
			t |= 7;
	}
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
	UT = T / (aec_old ? 1 : 2);

	ata_timing_compute(drive, speed, &t, T, UT);
	ata_timing_merge_8bit(&t);

	if (aec_old)
		aec_set_speed_old(drive->channel->pci_dev, drive->dn, &t);
	else
		aec_set_speed_new(drive->channel->pci_dev, drive->dn, &t);

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
static int __init aec62xx_modes_map(struct ata_channel *ch)
{
	u32 bmide = pci_resource_start(ch->pci_dev, 4);
	int map;

	map = XFER_EPIO | XFER_SWDMA | XFER_MWDMA | XFER_UDMA;

	if (ch->udma_four)
		switch (ch->pci_dev->device) {
			case PCI_DEVICE_ID_ARTOP_ATP865R:
			case PCI_DEVICE_ID_ARTOP_ATP865:
				/* Can't use these modes simultaneously,
				   based on which PLL clock was chosen. */
				map |= inb (bmide + AEC_BM_STAT_PCH) & AEC_PLLCLK_ATA133 ? XFER_UDMA_133 : XFER_UDMA_100;
			case PCI_DEVICE_ID_ARTOP_ATP860R:
			case PCI_DEVICE_ID_ARTOP_ATP860:
				map |= XFER_UDMA_66;
		}

	return map;
}
#endif

/*
 * The initialization callback. Here we determine the IDE chip type
 * and initialize its drive independent registers.
 * We return the IRQ assigned to the chip.
 */

static unsigned int __init aec62xx_init_chipset(struct pci_dev *dev)
{
	u32 bmide = pci_resource_start(dev, 4);
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
			/* switch cable detection pins to input-only. */
			outb (inb (bmide + AEC_BM_STAT_SCH) | AEC_CABLEPINS_INPUT, bmide + AEC_BM_STAT_SCH);
	}

/*
 * Print the boot message.
 */

	pci_read_config_byte(dev, PCI_REVISION_ID, &t);
	printk(KERN_INFO "AEC_IDE: %s (rev %02x) controller on pci%s\n",
		dev->name, t, dev->slot_name);

	return dev->irq;
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

	ch->tuneproc = aec62xx_tune_drive;
	ch->speedproc = aec_set_drive;

	ch->io_32bit = 1;
	ch->unmask = 1;

	ch->udma_four = aec62xx_ata66_check(ch);

	for (i = 0; i < 2; i++) {
		ch->drives[i].autotune = 1;
		ch->drives[i].dn = ch->unit * 2 + i;
	}

#ifdef CONFIG_BLK_DEV_IDEDMA
	if (ch->dma_base) {
		ch->highmem = 1;
		ch->modes_map = aec62xx_modes_map(ch);
		ch->udma_setup = udma_generic_setup;
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
		init_channel: aec62xx_init_channel,
		enablebits: { {0x4a,0x02,0x02},	{0x4a,0x04,0x04} },
		bootable: NEVER_BOARD,
		flags: ATA_F_IRQ | ATA_F_DMA
	},
	{
		vendor: PCI_VENDOR_ID_ARTOP,
		device: PCI_DEVICE_ID_ARTOP_ATP860R,
		init_chipset: aec62xx_init_chipset,
		init_channel: aec62xx_init_channel,
		enablebits: { {0x4a,0x02,0x02},	{0x4a,0x04,0x04} },
		bootable: OFF_BOARD,
		flags: ATA_F_IRQ | ATA_F_DMA
	},
	{
		vendor: PCI_VENDOR_ID_ARTOP,
		device: PCI_DEVICE_ID_ARTOP_ATP865,
		init_chipset: aec62xx_init_chipset,
		init_channel: aec62xx_init_channel,
		enablebits: { {0x4a,0x02,0x02},	{0x4a,0x04,0x04} },
		bootable: NEVER_BOARD,
		flags: ATA_F_IRQ | ATA_F_DMA
	},
	{
		vendor: PCI_VENDOR_ID_ARTOP,
		device: PCI_DEVICE_ID_ARTOP_ATP865R,
		init_chipset: aec62xx_init_chipset,
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
