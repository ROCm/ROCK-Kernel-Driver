/*
 * linux/drivers/ide/alim15x3.c		Version 0.10	Jun. 9, 2000
 *
 *  Copyright (C) 1998-2000 Michel Aubry, Maintainer
 *  Copyright (C) 1998-2000 Andrzej Krzysztofowicz, Maintainer
 *  Copyright (C) 1999-2000 CJ, cjtsai@ali.com.tw, Maintainer
 *
 *  Copyright (C) 1998-2000 Andre Hedrick (andre@linux-ide.org)
 *  May be copied or modified under the terms of the GNU General Public License
 *
 *  (U)DMA capable version of ali 1533/1543(C), 1535(D)
 *
 **********************************************************************
 *  9/7/99 --Parts from the above author are included and need to be
 *  converted into standard interface, once I finish the thought.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/hdreg.h>
#include <linux/ide.h>

#include <asm/io.h>

#include "ata-timing.h"
#include "pcihost.h"

static byte m5229_revision;
static byte chip_is_1543c_e;

static struct pci_dev *isa_dev;

static void ali15x3_tune_drive(struct ata_device *drive, byte pio)
{
	struct ata_timing *t;
	struct ata_channel *hwif = drive->channel;
	struct pci_dev *dev = hwif->pci_dev;
	int s_time, a_time, c_time;
	byte s_clc, a_clc, r_clc;
	unsigned long flags;
	int port = hwif->unit ? 0x5c : 0x58;
	int portFIFO = hwif->unit ? 0x55 : 0x54;
	byte cd_dma_fifo = 0;

	if (pio == 255)
		pio = ata_timing_mode(drive, XFER_PIO | XFER_EPIO);
	else
		pio = XFER_PIO_0 + min_t(byte, pio, 4);

	t = ata_timing_data(pio);

	/* FIXME: use generic ata-timing library  --bkz */
	s_time = t->setup;
	a_time = t->active;
	if ((s_clc = (s_time * system_bus_speed + 999999) / 1000000) >= 8)
		s_clc = 0;
	if ((a_clc = (a_time * system_bus_speed + 999999) / 1000000) >= 8)
		a_clc = 0;
	c_time = t->cycle;

#if 0
	if ((r_clc = ((c_time - s_time - a_time) * system_bus_speed + 999999) / 1000000) >= 16)
		r_clc = 0;
#endif

	if (!(r_clc = (c_time * system_bus_speed + 999999) / 1000000 - a_clc - s_clc)) {
		r_clc = 1;
	} else {
		if (r_clc >= 16)
			r_clc = 0;
	}

	local_irq_save(flags);

	/*
	 * PIO mode => ATA FIFO on, ATAPI FIFO off
	 */
	pci_read_config_byte(dev, portFIFO, &cd_dma_fifo);
	if (drive->type == ATA_DISK) {
		if (hwif->index) {
			pci_write_config_byte(dev, portFIFO, (cd_dma_fifo & 0x0F) | 0x50);
		} else {
			pci_write_config_byte(dev, portFIFO, (cd_dma_fifo & 0xF0) | 0x05);
		}
	} else {
		if (hwif->index) {
			pci_write_config_byte(dev, portFIFO, cd_dma_fifo & 0x0F);
		} else {
			pci_write_config_byte(dev, portFIFO, cd_dma_fifo & 0xF0);
		}
	}

	pci_write_config_byte(dev, port, s_clc);
	pci_write_config_byte(dev, port+drive->select.b.unit+2, (a_clc << 4) | r_clc);

	local_irq_restore(flags);
}

static int ali15x3_tune_chipset(struct ata_device *drive, byte speed)
{
	struct pci_dev *dev = drive->channel->pci_dev;
	byte unit		= (drive->select.b.unit & 0x01);
	byte tmpbyte		= 0x00;
	int m5229_udma		= drive->channel->unit ? 0x57 : 0x56;

	if (speed < XFER_UDMA_0) {
		byte ultra_enable	= (unit) ? 0x7f : 0xf7;
		/*
		 * clear "ultra enable" bit
		 */
		pci_read_config_byte(dev, m5229_udma, &tmpbyte);
		tmpbyte &= ultra_enable;
		pci_write_config_byte(dev, m5229_udma, tmpbyte);
	}

	if (speed < XFER_SW_DMA_0)
		ali15x3_tune_drive(drive, speed);
#ifdef CONFIG_BLK_DEV_IDEDMA
	/* FIXME: no support for MWDMA and SWDMA modes  --bkz */
	else if (speed >= XFER_UDMA_0) {
		pci_read_config_byte(dev, m5229_udma, &tmpbyte);
		tmpbyte &= (0x0f << ((1-unit) << 2));
		/*
		 * enable ultra dma and set timing
		 */
		tmpbyte |= ((0x08 | ((4-speed)&0x07)) << (unit << 2));
		pci_write_config_byte(dev, m5229_udma, tmpbyte);
		if (speed >= XFER_UDMA_3) {
			pci_read_config_byte(dev, 0x4b, &tmpbyte);
			tmpbyte |= 1;
			pci_write_config_byte(dev, 0x4b, tmpbyte);
		}
	}
#endif /* CONFIG_BLK_DEV_IDEDMA */

	return ide_config_drive_speed(drive, speed);
}

#ifdef CONFIG_BLK_DEV_IDEDMA
static int ali15x3_udma_setup(struct ata_device *drive, int map)
{
#ifndef CONFIG_WDC_ALI15X3
	if ((m5229_revision < 0xC2) && chip_is_1543c_e &&
	    strstr(drive->id->model, "WDC "))
		map &= ~XFER_UDMA_ALL;
#endif
	return udma_generic_setup(drive, map);
}

static int ali15x3_udma_init(struct ata_device *drive, struct request *rq)
{
	if ((m5229_revision < 0xC2) && (drive->type != ATA_DISK))
		return ATA_OP_FINISHED;	/* try PIO instead of DMA */

	return udma_pci_init(drive, rq);
}

static int __init ali15x3_modes_map(struct ata_channel *ch)
{
	int map = XFER_EPIO | XFER_SWDMA | XFER_MWDMA;

	if (m5229_revision <= 0x20)
		return map;

	map |= XFER_UDMA;

	if (m5229_revision >= 0xC2) {
		map |= XFER_UDMA_66;
		if (m5229_revision >= 0xC4)
			map |= XFER_UDMA_100;
	}

	return map;
}
#endif

static unsigned int __init ali15x3_init_chipset(struct pci_dev *dev)
{
	unsigned long fixdma_base = pci_resource_start(dev, 4);

	pci_read_config_byte(dev, PCI_REVISION_ID, &m5229_revision);

	isa_dev = pci_find_device(PCI_VENDOR_ID_AL, PCI_DEVICE_ID_AL_M1533, NULL);

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
			printk("%s: simplex device: DMA will fail!!\n", dev->name);
	}

	return 0;
}

/*
 * This checks if the controller and the cable are capable
 * of UDMA66 transfers. It doesn't check the drives.
 * But see note 2 below!
 */
static unsigned int __init ali15x3_ata66_check(struct ata_channel *hwif)
{
	struct pci_dev *dev	= hwif->pci_dev;
	unsigned int ata66	= 0;
	byte cable_80_pin[2]	= { 0, 0 };

	unsigned long flags;
	byte tmpbyte;

	local_irq_save(flags);

	if (m5229_revision >= 0xC2) {
		/*
		 * 1543C-B?, 1535, 1535D, 1553
		 * Note 1: not all "motherboard" support this detection
		 * Note 2: if no udma 66 device, the detection may "error".
		 *         but in this case, we will not set the device to
		 *         ultra 66, the detection result is not important
		 */

		/*
		 * enable "Cable Detection", m5229, 0x4b, bit3
		 */
		pci_read_config_byte(dev, 0x4b, &tmpbyte);
		pci_write_config_byte(dev, 0x4b, tmpbyte | 0x08);

		/*
		 * set south-bridge's enable bit, m1533, 0x79
		 */
		pci_read_config_byte(isa_dev, 0x79, &tmpbyte);
		if (m5229_revision == 0xC2) {
			/*
			 * 1543C-B0 (m1533, 0x79, bit 2)
			 */
			pci_write_config_byte(isa_dev, 0x79, tmpbyte | 0x04);
		} else if (m5229_revision >= 0xC3) {
			/*
			 * 1553/1535 (m1533, 0x79, bit 1)
			 */
			pci_write_config_byte(isa_dev, 0x79, tmpbyte | 0x02);
		}
		/*
		 * Ultra66 cable detection (from Host View)
		 * m5229, 0x4a, bit0: primary, bit1: secondary 80 pin
		 */
		pci_read_config_byte(dev, 0x4a, &tmpbyte);
		/*
		 * 0x4a, bit0 is 0 => primary channel
		 * has 80-pin (from host view)
		 */
		if (!(tmpbyte & 0x01)) cable_80_pin[0] = 1;
		/*
		 * 0x4a, bit1 is 0 => secondary channel
		 * has 80-pin (from host view)
		 */
		if (!(tmpbyte & 0x02)) cable_80_pin[1] = 1;
		/*
		 * Allow ata66 if cable of current channel has 80 pins
		 */
		ata66 = (hwif->unit)?cable_80_pin[1]:cable_80_pin[0];
	} else {
		/*
		 * revision 0x20 (1543-E, 1543-F)
		 * revision 0xC0, 0xC1 (1543C-C, 1543C-D, 1543C-E)
		 * clear CD-ROM DMA write bit, m5229, 0x4b, bit 7
		 */
		pci_read_config_byte(dev, 0x4b, &tmpbyte);
		/*
		 * clear bit 7
		 */
		pci_write_config_byte(dev, 0x4b, tmpbyte & 0x7F);
		/*
		 * check m1533, 0x5e, bit 1~4 == 1001 => & 00011110 = 00010010
		 */
		pci_read_config_byte(isa_dev, 0x5e, &tmpbyte);
		chip_is_1543c_e = ((tmpbyte & 0x1e) == 0x12) ? 1: 0;
	}

	/*
	 * CD_ROM DMA on (m5229, 0x53, bit0)
	 *      Enable this bit even if we want to use PIO
	 * PIO FIFO off (m5229, 0x53, bit1)
	 *      The hardware will use 0x54h and 0x55h to control PIO FIFO
	 */
	pci_read_config_byte(dev, 0x53, &tmpbyte);
	tmpbyte = (tmpbyte & (~0x02)) | 0x01;

	pci_write_config_byte(dev, 0x53, tmpbyte);

	local_irq_restore(flags);

	return (ata66);
}

static void __init ali15x3_init_channel(struct ata_channel *hwif)
{
#ifndef CONFIG_SPARC64
	byte ideic, inmir;
	byte irq_routing_table[] = { -1,  9, 3, 10, 4,  5, 7,  6,
				      1, 11, 0, 12, 0, 14, 0, 15 };

	hwif->irq = hwif->unit ? 15 : 14;

	if (isa_dev) {
		/*
		 * read IDE interface control
		 */
		pci_read_config_byte(isa_dev, 0x58, &ideic);

		/* bit0, bit1 */
		ideic = ideic & 0x03;

		/* get IRQ for IDE Controller */
		if ((hwif->unit && ideic == 0x03) || (!hwif->unit && !ideic)) {
			/*
			 * get SIRQ1 routing table
			 */
			pci_read_config_byte(isa_dev, 0x44, &inmir);
			inmir = inmir & 0x0f;
			hwif->irq = irq_routing_table[inmir];
		} else if (hwif->unit && !(ideic & 0x01)) {
			/*
			 * get SIRQ2 routing table
			 */
			pci_read_config_byte(isa_dev, 0x75, &inmir);
			inmir = inmir & 0x0f;
			hwif->irq = irq_routing_table[inmir];
		}
	}
#endif /* CONFIG_SPARC64 */

	hwif->udma_four = ali15x3_ata66_check(hwif);

	hwif->tuneproc = &ali15x3_tune_drive;
	hwif->drives[0].autotune = 1;
	hwif->drives[1].autotune = 1;
	hwif->speedproc = ali15x3_tune_chipset;

#ifdef CONFIG_BLK_DEV_IDEDMA
	if ((hwif->dma_base) && (m5229_revision >= 0x20)) {
		/*
		 * M1543C or newer for DMAing
		 */
		hwif->modes_map = ali15x3_modes_map(hwif);
		if (m5229_revision < 0xC2)
			hwif->no_atapi_autodma = 1;
		hwif->udma_setup = ali15x3_udma_setup;
		hwif->udma_init = ali15x3_udma_init;
	}
#endif
}

static void __init ali15x3_init_dma(struct ata_channel *ch, unsigned long dmabase)
{
	if (dmabase && (m5229_revision < 0x20)) {
		ch->autodma = 0;
		return;
	}

	ata_init_dma(ch, dmabase);
}


/* module data table */
static struct ata_pci_device chipsets[] __initdata = {
	{
		.vendor = PCI_VENDOR_ID_AL,
	        .device = PCI_DEVICE_ID_AL_M5219,
		/* FIXME: Perhaps we should use the same init routines
		 * as below here. */
		.enablebits = { {0x00,0x00,0x00}, {0x00,0x00,0x00} },
		.bootable = ON_BOARD,
		.flags = ATA_F_SIMPLEX
	},
	{
		.vendor = PCI_VENDOR_ID_AL,
	        .device = PCI_DEVICE_ID_AL_M5229,
		.init_chipset = ali15x3_init_chipset,
		.init_channel = ali15x3_init_channel,
		.init_dma = ali15x3_init_dma,
		.enablebits = { {0x00,0x00,0x00}, {0x00,0x00,0x00} },
		.bootable = ON_BOARD
	}
};

int __init init_ali15x3(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(chipsets); ++i)
		ata_register_chipset(&chipsets[i]);

        return 0;
}
