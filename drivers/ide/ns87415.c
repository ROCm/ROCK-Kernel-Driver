/*
 * linux/drivers/ide/ns87415.c		Version 1.01  Mar. 18, 2000
 *
 * Copyright (C) 1997-1998	Mark Lord <mlord@pobox.com>
 * Copyright (C) 1998		Eddie C. Dost <ecd@skynet.be>
 * Copyright (C) 1999-2000	Andre Hedrick <andre@linux-ide.org>
 *
 * Inspired by an earlier effort from David S. Miller <davem@redhat.com>
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/ide.h>

#include <asm/io.h>

#include "pcihost.h"

static unsigned int ns87415_count = 0, ns87415_control[MAX_HWIFS] = { 0 };

/*
 * This routine either enables/disables (according to drive->present)
 * the IRQ associated with the port (drive->channel),
 * and selects either PIO or DMA handshaking for the next I/O operation.
 */
static void ns87415_prepare_drive(struct ata_device *drive, unsigned int use_dma)
{
	struct ata_channel *hwif = drive->channel;
	unsigned int bit, other, new, *old = (unsigned int *) hwif->select_data;
	struct pci_dev *dev = hwif->pci_dev;
	unsigned long flags;

	local_irq_save(flags);
	new = *old;

	/* Adjust IRQ enable bit */
	bit = 1 << (8 + hwif->unit);
	new = drive->present ? (new & ~bit) : (new | bit);

	/* Select PIO or DMA, DMA may only be selected for one drive/channel. */
	bit   = 1 << (20 + drive->select.b.unit       + (hwif->unit << 1));
	other = 1 << (20 + (1 - drive->select.b.unit) + (hwif->unit << 1));
	new = use_dma ? ((new & ~other) | bit) : (new & ~bit);

	if (new != *old) {
		unsigned char stat;

		/*
		 * Don't change DMA engine settings while Write Buffers
		 * are busy.
		 */
		(void) pci_read_config_byte(dev, 0x43, &stat);
		while (stat & 0x03) {
			udelay(1);
			(void) pci_read_config_byte(dev, 0x43, &stat);
		}

		*old = new;
		(void) pci_write_config_dword(dev, 0x40, new);

		/*
		 * And let things settle...
		 */
		udelay(10);
	}

	local_irq_restore(flags);
}

static void ns87415_selectproc(struct ata_device *drive)
{
	ns87415_prepare_drive (drive, drive->using_dma);
}

#ifdef CONFIG_BLK_DEV_IDEDMA

static int ns87415_udma_stop(struct ata_device *drive)
{
	struct ata_channel *ch = drive->channel;
	unsigned long dma_base = ch->dma_base;
	u8 dma_stat;

	dma_stat = inb(ch->dma_base+2);
	outb(inb(dma_base)&~1, dma_base);	/* stop DMA */
	outb(inb(dma_base)|6, dma_base);	/* from ERRATA: clear the INTR & ERROR bits */
	udma_destroy_table(ch);				/* and free any DMA resources */

	return (dma_stat & 7) != 4;	/* verify good DMA status */

}

static int ns87415_udma_init(struct ata_device *drive, struct request *rq)
{
	ns87415_prepare_drive(drive, 1);	/* select DMA xfer */

	if (udma_pci_init(drive, rq))		/* use standard DMA stuff */
		return ATA_OP_CONTINUES;

	ns87415_prepare_drive(drive, 0);	/* DMA failed: select PIO xfer */

	return ATA_OP_FINISHED;
}

static int ns87415_udma_setup(struct ata_device *drive, int map)
{
	if (drive->type != ATA_DISK) {
		udma_enable(drive, 0, 0);

		return 0;
	}
	return udma_pci_setup(drive, map);
}
#endif

static void __init ide_init_ns87415(struct ata_channel *hwif)
{
	struct pci_dev *dev = hwif->pci_dev;
	unsigned int ctrl, using_inta;
	u8 progif;

	/* Set a good latency timer and cache line size value. */
	(void) pci_write_config_byte(dev, PCI_LATENCY_TIMER, 64);
#ifdef __sparc_v9__
	(void) pci_write_config_byte(dev, PCI_CACHE_LINE_SIZE, 0x10);
#endif

	/*
	 * We cannot probe for IRQ: both ports share common IRQ on INTA.
	 * Also, leave IRQ masked during drive probing, to prevent infinite
	 * interrupts from a potentially floating INTA..
	 *
	 * IRQs get unmasked in selectproc when drive is first used.
	 */
	(void) pci_read_config_dword(dev, 0x40, &ctrl);
	(void) pci_read_config_byte(dev, 0x09, &progif);
	/* is irq in "native" mode? */
	using_inta = progif & (1 << (hwif->unit << 1));
	if (!using_inta)
		using_inta = ctrl & (1 << (4 + hwif->unit));
	if (hwif->unit == ATA_SECONDARY) {

		/* FIXME: If we are initializing the secondary channel, let us
		 * assume that the primary channel got initialized just a tad
		 * bit before now.  It would be much cleaner if the data in
		 * ns87415_control just got duplicated.
		 */

		if (!hwif->select_data)
		    hwif->select_data = (unsigned long)
			&ns87415_control[ns87415_count - 1];
	} else {
		if (!hwif->select_data)
		    hwif->select_data = (unsigned long)
			&ns87415_control[ns87415_count++];
		ctrl |= (1 << 8) | (1 << 9);	/* mask both IRQs */
		if (using_inta)
			ctrl &= ~(1 << 6);	/* unmask INTA */
		*((unsigned int *)hwif->select_data) = ctrl;
		(void) pci_write_config_dword(dev, 0x40, ctrl);

		/*
		 * Set prefetch size to 512 bytes for both ports,
		 * but don't turn on/off prefetching here.
		 */
		pci_write_config_byte(dev, 0x55, 0xee);

#ifdef __sparc_v9__
		/*
		 * XXX: Reset the device, if we don't it will not respond
		 *      to select properly during first probe.
		 */
		ata_reset(hwif);
#endif
	}

	if (hwif->dma_base)
		outb(0x60, hwif->dma_base + 2);

	if (!using_inta)
		hwif->irq = hwif->unit ? 15 : 14;	/* legacy mode */
	else {
		static int primary_irq = 0;

		/* Ugly way to let the primary and secondary channel on the
		 * chip use the same IRQ line.
		 */

		if (hwif->unit == ATA_PRIMARY)
			primary_irq = hwif->irq;
		else if (!hwif->irq)
			hwif->irq = primary_irq;
	}

#ifdef CONFIG_BLK_DEV_IDEDMA
	if (hwif->dma_base) {
		hwif->udma_stop = ns87415_udma_stop;
		hwif->udma_init = ns87415_udma_init;
		hwif->udma_setup = ns87415_udma_setup;
	}
#endif

	hwif->selectproc = &ns87415_selectproc;
}

/* module data table */
static struct ata_pci_device chipset __initdata = {
	.vendor = PCI_VENDOR_ID_NS,
	.device = PCI_DEVICE_ID_NS_87415,
	.init_channel = ide_init_ns87415,
	.bootable = ON_BOARD,
};

int __init init_ns87415(void)
{
	ata_register_chipset(&chipset);

        return 0;
}
