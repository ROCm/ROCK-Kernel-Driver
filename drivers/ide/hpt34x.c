/**** vi:set ts=8 sts=8 sw=8:************************************************
 *
 * linux/drivers/ide/hpt34x.c		Version 0.31	June. 9, 2000
 *
 * Copyright (C) 1998-2000	Andre Hedrick <andre@linux-ide.org>
 * May be copied or modified under the terms of the GNU General Public License
 *
 * ide-pci.c reference:
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

#include "ata-timing.h"
#include "pcihost.h"

#define HPT343_DEBUG_DRIVE_INFO		0

static int hpt34x_tune_chipset(struct ata_device *drive, u8 speed)
{
	struct pci_dev *dev = drive->channel->pci_dev;
	u8 udma = 0, pio = 0;
	u32 reg1, reg2, tmp1, tmp2;

	if (speed >= XFER_UDMA_0)
		udma = 0x01;
	else if (speed >= XFER_SW_DMA_0)
		udma = 0x10;
	else
		pio = speed & 7;

	pci_read_config_dword(dev, 0x44, &reg1);
	pci_read_config_dword(dev, 0x48, &reg2);
	tmp1 = (pio << (3*drive->dn)) | (reg1 & ~(7 << (3*drive->dn)));
	tmp2 = (udma << drive->dn) | (reg2 & ~(0x11 << drive->dn));
	pci_write_config_dword(dev, 0x44, tmp1);
	pci_write_config_dword(dev, 0x48, tmp2);

#if HPT343_DEBUG_DRIVE_INFO
	printk("%s: %02x drive%d (0x%04x 0x%04x) (0x%04x 0x%04x)" \
		" (0x%02x 0x%02x) 0x%04x\n",
		drive->name, speed,
		drive->dn, reg1, tmp1, reg2, tmp2,
		udma, pio, err);
#endif

	return ide_config_drive_speed(drive, speed);
}

static void hpt34x_tune_drive(struct ata_device *drive, u8 pio)
{
	(void) hpt34x_tune_chipset(drive, XFER_PIO_0 + min_t(u8, pio, 4));
}

#ifdef CONFIG_BLK_DEV_IDEDMA
static int hpt34x_udma_setup(struct ata_device *drive, int map)
{
#ifdef CONFIG_HPT34X_AUTODMA
	return udma_generic_setup(drive, map);
#else
	return 0;
#endif
}

static int hpt34x_udma_stop(struct ata_device *drive)
{
	struct ata_channel *ch = drive->channel;
	unsigned long dma_base = ch->dma_base;
	u8 dma_stat;

	outb(inb(dma_base)&~1, dma_base);	/* stop DMA */
	dma_stat = inb(dma_base+2);		/* get DMA status */
	outb(dma_stat|6, dma_base+2);		/* clear the INTR & ERROR bits */
	udma_destroy_table(ch);			/* purge DMA mappings */

	return (dma_stat & 7) != 4;		/* verify good DMA status */
}

static int hpt34x_udma_init(struct ata_device *drive, struct request *rq)
{
	struct ata_channel *ch = drive->channel;
	unsigned long dma_base = ch->dma_base;
	unsigned int count;
	u8 cmd;

	if (!(count = udma_new_table(drive, rq)))
		return ATA_OP_FINISHED;	/* try PIO instead of DMA */

	if (rq_data_dir(rq) == READ)
		cmd = 0x09;
	else
		cmd = 0x01;

	outl(ch->dmatable_dma, dma_base + 4);	/* PRD table */
	outb(cmd, dma_base);			/* specify r/w */
	outb(inb(dma_base+2)|6, dma_base+2);	/* clear INTR & ERROR flags */

	if (drive->type == ATA_DISK) {
		ata_set_handler(drive, ide_dma_intr, WAIT_CMD, NULL);	/* issue cmd to drive */
		OUT_BYTE((cmd == 0x09) ? WIN_READDMA : WIN_WRITEDMA, IDE_COMMAND_REG);
	}

	return ATA_OP_CONTINUES;
}
#endif

/*
 * If the BIOS does not set the IO base addaress to XX00, 343 will fail.
 */
#define	HPT34X_PCI_INIT_REG		0x80

static unsigned int __init pci_init_hpt34x(struct pci_dev *dev)
{
	int i = 0;
	unsigned long hpt34xIoBase = pci_resource_start(dev, 4);
	unsigned short cmd;
	unsigned long flags;

	local_irq_save(flags);

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

	local_irq_restore(flags);

	return dev->irq;
}

static void __init ide_init_hpt34x(struct ata_channel *hwif)
{
	hwif->tuneproc = hpt34x_tune_drive;
	hwif->speedproc = hpt34x_tune_chipset;

#ifdef CONFIG_BLK_DEV_IDEDMA
	if (hwif->dma_base) {
		unsigned short pcicmd = 0;

		pci_read_config_word(hwif->pci_dev, PCI_COMMAND, &pcicmd);
#ifdef CONFIG_IDEDMA_AUTO
		hwif->autodma = (pcicmd & PCI_COMMAND_MEMORY) ? 1 : 0;
#endif
		hwif->udma_stop = hpt34x_udma_stop;
		hwif->udma_init = hpt34x_udma_init;
		hwif->modes_map = XFER_EPIO | XFER_SWDMA | XFER_MWDMA | XFER_UDMA;
		hwif->no_atapi_autodma = 1;
		hwif->udma_setup = hpt34x_udma_setup;
		hwif->highmem = 1;
	} else 
#endif
	{
		hwif->drives[0].autotune = 1;
		hwif->drives[1].autotune = 1;
	}
}


/* module data table */
static struct ata_pci_device chipset __initdata = {
	.vendor = PCI_VENDOR_ID_TTI,
	.device = PCI_DEVICE_ID_TTI_HPT343,
	.init_chipset = pci_init_hpt34x,
	.init_channel =	ide_init_hpt34x,
	.bootable = NEVER_BOARD,
	.extra = 16,
	.flags = ATA_F_DMA
};

int __init init_hpt34x(void)
{
	ata_register_chipset(&chipset);

	return 0;
}
