/**** vi:set ts=8 sts=8 sw=8:************************************************
 *
 *  linux/drivers/ide/trm290.c		Version 1.02	Mar. 18, 2000
 *
 *  Copyright (c) 1997-1998  Mark Lord
 *  May be copied or modified under the terms of the GNU General Public License
 */

/*
 * This module provides support for the bus-master IDE DMA function
 * of the Tekram TRM290 chip, used on a variety of PCI IDE add-on boards,
 * including a "Precision Instruments" board.  The TRM290 pre-dates
 * the sff-8038 standard (ide-dma.c) by a few months, and differs
 * significantly enough to warrant separate routines for some functions,
 * while re-using others from ide-dma.c.
 *
 * EXPERIMENTAL!  It works for me (a sample of one).
 *
 * Works reliably for me in DMA mode (READs only),
 * DMA WRITEs are disabled by default (see #define below);
 *
 * DMA is not enabled automatically for this chipset,
 * but can be turned on manually (with "hdparm -d1") at run time.
 *
 * I need volunteers with "spare" drives for further testing
 * and development, and maybe to help figure out the peculiarities.
 * Even knowing the registers (below), some things behave strangely.
 */

#define TRM290_NO_DMA_WRITES	/* DMA writes seem unreliable sometimes */

/*
 * TRM-290 PCI-IDE2 Bus Master Chip
 * ================================
 * The configuration registers are addressed in normal I/O port space
 * and are used as follows:
 *
 * trm290_base depends on jumper settings, and is probed for by ide-dma.c
 *
 * trm290_base+2 when WRITTEN: chiptest register (byte, write-only)
 *	bit7 must always be written as "1"
 *	bits6-2 undefined
 *	bit1 1=legacy_compatible_mode, 0=native_pci_mode
 *	bit0 1=test_mode, 0=normal(default)
 *
 * trm290_base+2 when READ: status register (byte, read-only)
 *	bits7-2 undefined
 *	bit1 channel0 busmaster interrupt status 0=none, 1=asserted
 *	bit0 channel0 interrupt status 0=none, 1=asserted
 *
 * trm290_base+3 Interrupt mask register
 *	bits7-5 undefined
 *	bit4 legacy_header: 1=present, 0=absent
 *	bit3 channel1 busmaster interrupt status 0=none, 1=asserted (read only)
 *	bit2 channel1 interrupt status 0=none, 1=asserted (read only)
 *	bit1 channel1 interrupt mask: 1=masked, 0=unmasked(default)
 *	bit0 channel0 interrupt mask: 1=masked, 0=unmasked(default)
 *
 * trm290_base+1 "CPR" Config Pointer Register (byte)
 *	bit7 1=autoincrement CPR bits 2-0 after each access of CDR
 *	bit6 1=min. 1 wait-state posted write cycle (default), 0=0 wait-state
 *	bit5 0=enabled master burst access (default), 1=disable  (write only)
 *	bit4 PCI DEVSEL# timing select: 1=medium(default), 0=fast
 *	bit3 0=primary IDE channel, 1=secondary IDE channel
 *	bits2-0 register index for accesses through CDR port
 *
 * trm290_base+0 "CDR" Config Data Register (word)
 *	two sets of seven config registers,
 *	selected by CPR bit 3 (channel) and CPR bits 2-0 (index 0 to 6),
 *	each index defined below:
 *
 * Index-0 Base address register for command block (word)
 *	defaults: 0x1f0 for primary, 0x170 for secondary
 *
 * Index-1 general config register (byte)
 *	bit7 1=DMA enable, 0=DMA disable
 *	bit6 1=activate IDE_RESET, 0=no action (default)
 *	bit5 1=enable IORDY, 0=disable IORDY (default)
 *	bit4 0=16-bit data port(default), 1=8-bit (XT) data port
 *	bit3 interrupt polarity: 1=active_low, 0=active_high(default)
 *	bit2 power-saving-mode(?): 1=enable, 0=disable(default) (write only)
 *	bit1 bus_master_mode(?): 1=enable, 0=disable(default)
 *	bit0 enable_io_ports: 1=enable(default), 0=disable
 *
 * Index-2 read-ahead counter preload bits 0-7 (byte, write only)
 *	bits7-0 bits7-0 of readahead count
 *
 * Index-3 read-ahead config register (byte, write only)
 *	bit7 1=enable_readahead, 0=disable_readahead(default)
 *	bit6 1=clear_FIFO, 0=no_action
 *	bit5 undefined
 *	bit4 mode4 timing control: 1=enable, 0=disable(default)
 *	bit3 undefined
 *	bit2 undefined
 *	bits1-0 bits9-8 of read-ahead count
 *
 * Index-4 base address register for control block (word)
 *	defaults: 0x3f6 for primary, 0x376 for secondary
 *
 * Index-5 data port timings (shared by both drives) (byte)
 *	standard PCI "clk" (clock) counts, default value = 0xf5
 *
 *	bits7-6 setup time:  00=1clk, 01=2clk, 10=3clk, 11=4clk
 *	bits5-3 hold time:	000=1clk, 001=2clk, 010=3clk,
 *				011=4clk, 100=5clk, 101=6clk,
 *				110=8clk, 111=12clk
 *	bits2-0 active time:	000=2clk, 001=3clk, 010=4clk,
 *				011=5clk, 100=6clk, 101=8clk,
 *				110=12clk, 111=16clk
 *
 * Index-6 command/control port timings (shared by both drives) (byte)
 *	same layout as Index-5, default value = 0xde
 *
 * Suggested CDR programming for PIO mode0 (600ns):
 *	0x01f0,0x21,0xff,0x80,0x03f6,0xf5,0xde	; primary
 *	0x0170,0x21,0xff,0x80,0x0376,0xf5,0xde	; secondary
 *
 * Suggested CDR programming for PIO mode3 (180ns):
 *	0x01f0,0x21,0xff,0x80,0x03f6,0x09,0xde	; primary
 *	0x0170,0x21,0xff,0x80,0x0376,0x09,0xde	; secondary
 *
 * Suggested CDR programming for PIO mode4 (120ns):
 *	0x01f0,0x21,0xff,0x80,0x03f6,0x00,0xde	; primary
 *	0x0170,0x21,0xff,0x80,0x0376,0x00,0xde	; secondary
 *
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/blkdev.h>
#include <linux/init.h>
#include <linux/hdreg.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/ide.h>

#include <asm/io.h>

#include "pcihost.h"

static void trm290_prepare_drive(struct ata_device *drive, unsigned int use_dma)
{
	struct ata_channel *hwif = drive->channel;
	unsigned int reg;
	unsigned long flags;

	/* select PIO or DMA */
	reg = use_dma ? (0x21 | 0x82) : (0x21 & ~0x82);

	local_irq_save(flags);

	if (reg != hwif->select_data) {
		hwif->select_data = reg;
		outb(0x51|(hwif->unit<<3), hwif->config_data+1);	/* set PIO/DMA */
		outw(reg & 0xff, hwif->config_data);
	}

	/* enable IRQ if not probing */
	if (drive->present) {
		reg = inw(hwif->config_data+3) & 0x13;
		reg &= ~(1 << hwif->unit);
		outw(reg, hwif->config_data+3);
	}

	local_irq_restore(flags);
}

static void trm290_selectproc(struct ata_device *drive)
{
	trm290_prepare_drive(drive, drive->using_dma);
}

#ifdef CONFIG_BLK_DEV_IDEDMA
static void trm290_udma_start(struct ata_device *drive, struct request *__rq)
{
	/* Nothing to be done here. */
}

static int trm290_udma_stop(struct ata_device *drive)
{
	struct ata_channel *ch = drive->channel;

	udma_destroy_table(ch);	/* purge DMA mappings */

	return (inw(ch->dma_base + 2) != 0x00ff);
}

static int trm290_udma_init(struct ata_device *drive, struct request *rq)
{
	struct ata_channel *ch = drive->channel;
	unsigned int count;
	int writing;
	int reading;


	if (rq_data_dir(rq) == READ)
		reading = 1;
	else
		reading = 0;

	if (!reading) {
		reading = 0;
		writing = 1;
#ifdef TRM290_NO_DMA_WRITES
		trm290_prepare_drive(drive, 0);	/* select PIO xfer */

		return ATA_OP_FINISHED;
#endif
	} else {
		reading = 2;
		writing = 0;
	}

	if (!(count = udma_new_table(drive, rq))) {
		trm290_prepare_drive(drive, 0);	/* select PIO xfer */
		return ATA_OP_FINISHED;	/* try PIO instead of DMA */
	}

	trm290_prepare_drive(drive, 1);	/* select DMA xfer */

	outl(ch->dmatable_dma|reading|writing, ch->dma_base);
	outw((count * 2) - 1, ch->dma_base+2); /* start DMA */

	if (drive->type == ATA_DISK) {
		ata_set_handler(drive, ide_dma_intr, WAIT_CMD, NULL);
		outb(reading ? WIN_READDMA : WIN_WRITEDMA, IDE_COMMAND_REG);
	}

	return ATA_OP_CONTINUES;
}

static int trm290_udma_irq_status(struct ata_device *drive)
{
	return (inw(drive->channel->dma_base + 2) == 0x00ff);
}

static int trm290_udma_setup(struct ata_device *drive, int map)
{
	return udma_pci_setup(drive, map);
}
#endif

/*
 * Invoked from ide-dma.c at boot time.
 */
static void __init trm290_init_channel(struct ata_channel *hwif)
{
	unsigned int cfgbase = 0;
	unsigned long flags;
	u8 reg;
	struct pci_dev *dev = hwif->pci_dev;

	hwif->chipset = ide_trm290;
	hwif->seg_boundary_mask = 0xffffffff;
	cfgbase = pci_resource_start(dev, 4);
	if ((dev->class & 5) && cfgbase)
	{
		hwif->config_data = cfgbase;
		printk("TRM290: chip config base at 0x%04lx\n", hwif->config_data);
	} else {
		hwif->config_data = 0x3df0;
		printk("TRM290: using default config base at 0x%04lx\n", hwif->config_data);
	}

	local_irq_save(flags);
	/* put config reg into first byte of hwif->select_data */
	outb(0x51|(hwif->unit<<3), hwif->config_data+1);
	hwif->select_data = 0x21;			/* select PIO as default */
	outb(hwif->select_data, hwif->config_data);
	reg = inb(hwif->config_data+3);			/* get IRQ info */
	reg = (reg & 0x10) | 0x03;			/* mask IRQs for both ports */
	outb(reg, hwif->config_data+3);
	local_irq_restore(flags);

	if ((reg & 0x10))
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

	ata_init_dma(hwif, (hwif->config_data + 4) ^ (hwif->unit ? 0x0080 : 0x0000));

#ifdef CONFIG_BLK_DEV_IDEDMA
	hwif->udma_start = trm290_udma_start;
	hwif->udma_stop = trm290_udma_stop;
	hwif->udma_init = trm290_udma_init;
	hwif->udma_irq_status = trm290_udma_irq_status;
	hwif->udma_setup = trm290_udma_setup;
#endif

	hwif->selectproc = &trm290_selectproc;
#if 1
	{
		/*
		 * My trm290-based card doesn't seem to work with all possible values
		 * for the control basereg, so this kludge ensures that we use only
		 * values that are known to work.  Ugh.		-ml
		 */
		unsigned short old, compat = hwif->unit ? 0x374 : 0x3f4;
		static unsigned short next_offset = 0;

		outb(0x54|(hwif->unit<<3), hwif->config_data+1);
		old = inw(hwif->config_data) & ~1;
		if (old != compat && inb(old+2) == 0xff) {
			compat += (next_offset += 0x400);	/* leave lower 10 bits untouched */
			hwif->io_ports[IDE_CONTROL_OFFSET] = compat + 2;
			outw(compat|1, hwif->config_data);
			printk("%s: control basereg workaround: old=0x%04x, new=0x%04x\n", hwif->name, old, inw(hwif->config_data) & ~1);
		}
	}
#endif
}

/* module data table */
static struct ata_pci_device chipset __initdata = {
	.vendor = PCI_VENDOR_ID_TEKRAM,
	.device = PCI_DEVICE_ID_TEKRAM_DC290,
	.init_channel = trm290_init_channel,
	.bootable = ON_BOARD
};

int __init init_trm290(void)
{
        ata_register_chipset(&chipset);

        return 0;
}
