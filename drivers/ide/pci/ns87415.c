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
#include <linux/ide.h>
#include <linux/init.h>

#include <asm/io.h>

#include "ns87415.h"

static unsigned int ns87415_count = 0, ns87415_control[MAX_HWIFS] = { 0 };

/*
 * This routine either enables/disables (according to drive->present)
 * the IRQ associated with the port (HWIF(drive)),
 * and selects either PIO or DMA handshaking for the next I/O operation.
 */
static void ns87415_prepare_drive (ide_drive_t *drive, unsigned int use_dma)
{
	ide_hwif_t *hwif = HWIF(drive);
	unsigned int bit, other, new, *old = (unsigned int *) hwif->select_data;
	struct pci_dev *dev = hwif->pci_dev;
	unsigned long flags;

	local_irq_save(flags);
	new = *old;

	/* Adjust IRQ enable bit */
	bit = 1 << (8 + hwif->channel);
	new = drive->present ? (new & ~bit) : (new | bit);

	/* Select PIO or DMA, DMA may only be selected for one drive/channel. */
	bit   = 1 << (20 + drive->select.b.unit       + (hwif->channel << 1));
	other = 1 << (20 + (1 - drive->select.b.unit) + (hwif->channel << 1));
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

static void ns87415_selectproc (ide_drive_t *drive)
{
	ns87415_prepare_drive (drive, drive->using_dma);
}

#ifdef CONFIG_BLK_DEV_IDEDMA
static int ns87415_ide_dma_end (ide_drive_t *drive)
{
	ide_hwif_t      *hwif = HWIF(drive);
	u8 dma_stat = 0, dma_cmd = 0;

	drive->waiting_for_dma = 0;
	dma_stat = hwif->INB(hwif->dma_status);
	/* get dma command mode */
	dma_cmd = hwif->INB(hwif->dma_command);
	/* stop DMA */
	hwif->OUTB(dma_cmd & ~1, hwif->dma_command);
	/* from ERRATA: clear the INTR & ERROR bits */
	dma_cmd = hwif->INB(hwif->dma_command);
	hwif->OUTB(dma_cmd|6, hwif->dma_command);
	/* and free any DMA resources */
	ide_destroy_dmatable(drive);
	/* verify good DMA status */
	return (dma_stat & 7) != 4;
}

static int ns87415_ide_dma_read (ide_drive_t *drive)
{
	/* select DMA xfer */
	ns87415_prepare_drive(drive, 1);
	if (!(__ide_dma_read(drive)))
		return 0;
	/* DMA failed: select PIO xfer */
	ns87415_prepare_drive(drive, 0);
	return 1;
}

static int ns87415_ide_dma_write (ide_drive_t *drive)
{
	/* select DMA xfer */
	ns87415_prepare_drive(drive, 1);
	if (!(__ide_dma_write(drive)))
		return 0;
	/* DMA failed: select PIO xfer */
	ns87415_prepare_drive(drive, 0);
	return 1;
}

static int ns87415_ide_dma_check (ide_drive_t *drive)
{
	if (drive->media != ide_disk)
		return HWIF(drive)->ide_dma_off_quietly(drive);
	return __ide_dma_check(drive);
}
#endif /* CONFIG_BLK_DEV_IDEDMA */

static void __init init_hwif_ns87415 (ide_hwif_t *hwif)
{
	struct pci_dev *dev = hwif->pci_dev;
	unsigned int ctrl, using_inta;
	u8 progif;
#ifdef __sparc_v9__
	int timeout;
	u8 stat;
#endif

	hwif->autodma = 0;
	hwif->selectproc = &ns87415_selectproc;

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
	using_inta = progif & (1 << (hwif->channel << 1));
	if (!using_inta)
		using_inta = ctrl & (1 << (4 + hwif->channel));
	if (hwif->mate) {
		hwif->select_data = hwif->mate->select_data;
	} else {
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
		 *      to SELECT_DRIVE() properly during first probe_hwif().
		 */
		timeout = 10000;
		hwif->OUTB(12, hwif->io_ports[IDE_CONTROL_OFFSET]);
		udelay(10);
		hwif->OUTB(8, hwif->io_ports[IDE_CONTROL_OFFSET]);
		do {
			udelay(50);
			stat = hwif->INB(hwif->io_ports[IDE_STATUS_OFFSET]);
                	if (stat == 0xff)
                        	break;
        	} while ((stat & BUSY_STAT) && --timeout);
#endif
	}

	if (!using_inta)
		hwif->irq = hwif->channel ? 15 : 14;	/* legacy mode */
	else if (!hwif->irq && hwif->mate && hwif->mate->irq)
		hwif->irq = hwif->mate->irq;	/* share IRQ with mate */

	if (!hwif->dma_base)
		return;

#ifdef CONFIG_BLK_DEV_IDEDMA
	hwif->OUTB(0x60, hwif->dma_status);
	hwif->ide_dma_read = &ns87415_ide_dma_read;
	hwif->ide_dma_write = &ns87415_ide_dma_write;
	hwif->ide_dma_check = &ns87415_ide_dma_check;
	hwif->ide_dma_end = &ns87415_ide_dma_end;

	if (!noautodma)
		hwif->autodma = 1;
	hwif->drives[0].autodma = hwif->autodma;
	hwif->drives[1].autodma = hwif->autodma;
#endif /* CONFIG_BLK_DEV_IDEDMA */
}

static void __init init_dma_ns87415 (ide_hwif_t *hwif, unsigned long dmabase)
{
	ide_setup_dma(hwif, dmabase, 8);
}

extern void ide_setup_pci_device(struct pci_dev *, ide_pci_device_t *);

static void __init init_setup_ns87415 (struct pci_dev *dev, ide_pci_device_t *d)
{
	ide_setup_pci_device(dev, d);
}

int __init ns87415_scan_pcidev (struct pci_dev *dev)
{
	ide_pci_device_t *d;

	if (dev->vendor != PCI_VENDOR_ID_NS)
		return 0;

	for (d = ns87415_chipsets; d && d->vendor && d->device; ++d) {
		if (((d->vendor == dev->vendor) &&
		     (d->device == dev->device)) &&
		    (d->init_setup)) {
			d->init_setup(dev, d);
			return 1;
		}
	}
	return 0;
}

