/*
 * linux/drivers/ide/sl82c105.c
 *
 * SL82C105/Winbond 553 IDE driver
 *
 * Maintainer unknown.
 *
 * Changelog:
 *
 * 15/11/1998	RMK	Drive tuning added from Rebel.com's kernel
 *			sources
 * 30/03/2002	RMK	Add fixes specified in W83C553F errata.
 *			(with special thanks to Todd Inglett)
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/pci.h>
#include <linux/ide.h>

#include <asm/io.h>
#include <asm/dma.h>

#include "ata-timing.h"
#include "pcihost.h"

/*
 * SL82C105 PCI config register 0x40 bits.
 */
#define CTRL_IDE_IRQB	(1 << 30)
#define CTRL_IDE_IRQA	(1 << 28)
#define CTRL_LEGIRQ	(1 << 11)
#define CTRL_P1F16	(1 << 5)
#define CTRL_P1EN	(1 << 4)
#define CTRL_P0F16	(1 << 1)
#define	CTRL_P0EN	(1 << 0)

/*
 * Convert a PIO mode and cycle time to the required on/off
 * times for the interface.  This has protection against run-away
 * timings.
 */
static unsigned int get_timing_sl82c105(struct ata_timing *t)
{
	unsigned int cmd_on;
	unsigned int cmd_off;

	cmd_on = (t->active + 29) / 30;
	cmd_off = (t->cycle - 30 * cmd_on + 29) / 30;

	if (cmd_on > 32)
		cmd_on = 32;
	if (cmd_on == 0)
		cmd_on = 1;

	if (cmd_off > 32)
		cmd_off = 32;
	if (cmd_off == 0)
		cmd_off = 1;

	return (cmd_on - 1) << 8 | (cmd_off - 1) | ((t->mode > XFER_PIO_2) ? 0x40 : 0x00);
}

/*
 * Configure the drive and chipset for PIO
 */
static void config_for_pio(struct ata_device *drive, int pio, int report)
{
	struct ata_channel *hwif = drive->channel;
	struct pci_dev *dev = hwif->pci_dev;
	struct ata_timing *t;
	unsigned short drv_ctrl = 0x909;
	unsigned int xfer_mode, reg;

	reg = (hwif->unit ? 0x4c : 0x44) + (drive->select.b.unit ? 4 : 0);

	if (pio == 255)
		xfer_mode = ata_timing_mode(drive, XFER_PIO | XFER_EPIO);
	else
		xfer_mode = XFER_PIO_0 + min_t(byte, pio, 4);

	t = ata_timing_data(xfer_mode);

	if (ide_config_drive_speed(drive, xfer_mode) == 0)
		drv_ctrl = get_timing_sl82c105(t);

	if (!drive->using_dma) {
		/*
		 * If we are actually using MW DMA, then we can not
		 * reprogram the interface drive control register.
		 */
		pci_write_config_word(dev, reg, drv_ctrl);
		pci_read_config_word(dev, reg, &drv_ctrl);

		if (report) {
			printk("%s: selected %02x (%dns) (%04X)\n",
					drive->name, xfer_mode,
					t->cycle, drv_ctrl);
		}
	}
}

/*
 * Configure the drive and the chipset for DMA
 */
static int config_for_dma(struct ata_device *drive)
{
	struct ata_channel *hwif = drive->channel;
	struct pci_dev *dev = hwif->pci_dev;
	unsigned short drv_ctrl = 0x909;
	unsigned int reg;

	reg = (hwif->unit ? 0x4c : 0x44) + (drive->select.b.unit ? 4 : 0);

	if (ide_config_drive_speed(drive, XFER_MW_DMA_2) == 0)
		drv_ctrl = 0x0240;

	pci_write_config_word(dev, reg, drv_ctrl);

	return 0;
}


/*
 * Check to see if the drive and
 * chipset is capable of DMA mode
 */
static int sl82c105_dma_setup(struct ata_device *drive, int map)
{
	int on = 0;

	do {
		struct hd_driveid *id = drive->id;
		struct ata_channel *hwif = drive->channel;

		if (!hwif->autodma)
			break;

		if (!id || !(id->capability & 1))
			break;

		/* Consult the list of known "bad" drives */
		if (udma_black_list(drive)) {
			on = 0;
			break;
		}

		if (id->field_valid & 2) {
			if  (id->dma_mword & 7 || id->dma_1word & 7)
				on = 1;
			break;
		}

		if (udma_white_list(drive)) {
			on = 1;
			break;
		}
	} while (0);

	if (on)
		config_for_dma(drive);
	else
		config_for_pio(drive, 4, 0);

	udma_enable(drive, on, 0);

	return 0;
}

/*
 * The SL82C105 holds off all IDE interrupts while in DMA mode until
 * all DMA activity is completed.  Sometimes this causes problems (eg,
 * when the drive wants to report an error condition).
 *
 * 0x7e is a "chip testing" register.  Bit 2 resets the DMA controller
 * state machine.  We need to kick this to work around various bugs.
 */
static inline void sl82c105_reset_host(struct pci_dev *dev)
{
	u16 val;

	pci_read_config_word(dev, 0x7e, &val);
	pci_write_config_word(dev, 0x7e, val | (1 << 2));
	pci_write_config_word(dev, 0x7e, val & ~(1 << 2));
}

static void sl82c105_dma_enable(struct ata_device *drive, int on, int verbose)
{
	if (!on || config_for_dma(drive)) {
		config_for_pio(drive, 4, 0);
		on = 0;
	}
	udma_pci_enable(drive, on, verbose);
}

/*
 * ATAPI devices can cause the SL82C105 DMA state machine to go gaga.
 * Winbond recommend that the DMA state machine is reset prior to
 * setting the bus master DMA enable bit.
 *
 * The generic IDE core will have disabled the BMEN bit before this
 * function is called.
 */
static int sl82c105_dma_init(struct ata_device *drive, struct request *rq)
{
	sl82c105_reset_host(drive->channel->pci_dev);

	return udma_pci_init(drive, rq);
}

static void sl82c105_timeout(struct ata_device *drive)
{
	sl82c105_reset_host(drive->channel->pci_dev);
}

/*
 * If we get an IRQ timeout, it might be that the DMA state machine
 * got confused.  Fix from Todd Inglett.  Details from Winbond.
 *
 * This function is called when the IDE timer expires, the drive
 * indicates that it is READY, and we were waiting for DMA to complete.
 */
static void sl82c105_lostirq(struct ata_device *drive)
{
	struct ata_channel *ch = drive->channel;
	struct pci_dev *dev = ch->pci_dev;
	u32 val, mask = ch->unit ? CTRL_IDE_IRQB : CTRL_IDE_IRQA;
	unsigned long dma_base = ch->dma_base;

	printk("sl82c105: lost IRQ: resetting host\n");

	/*
	 * Check the raw interrupt from the drive.
	 */
	pci_read_config_dword(dev, 0x40, &val);
	if (val & mask)
		printk("sl82c105: drive was requesting IRQ, but host lost it\n");

	/*
	 * Was DMA enabled?  If so, disable it - we're resetting the
	 * host.  The IDE layer will be handling the drive for us.
	 */
	val = inb(dma_base);
	if (val & 1) {
		outb(val & ~1, dma_base);
		printk("sl82c105: DMA was enabled\n");
	}

	sl82c105_reset_host(dev);
}

/*
 * We only deal with PIO mode here - DMA mode 'using_dma' is not
 * initialised at the point that this function is called.
 */
static void tune_sl82c105(struct ata_device *drive, byte pio)
{
	config_for_pio(drive, pio, 1);

	/*
	 * We support 32-bit I/O on this interface, and it
	 * doesn't have problems with interrupts.
	 */
	drive->channel->io_32bit = 1;
	drive->channel->unmask = 1;
}

/*
 * Return the revision of the Winbond bridge
 * which this function is part of.
 */
static __init unsigned int sl82c105_bridge_revision(struct pci_dev *dev)
{
	struct pci_dev *bridge;
	unsigned char rev;

	/*
	 * The bridge should be part of the same device, but function 0.
	 */
	bridge = pci_find_slot(dev->bus->number,
			       PCI_DEVFN(PCI_SLOT(dev->devfn), 0));
	if (!bridge)
		return -1;

	/*
	 * Make sure it is a Winbond 553 and is an ISA bridge.
	 */
	if (bridge->vendor != PCI_VENDOR_ID_WINBOND ||
	    bridge->device != PCI_DEVICE_ID_WINBOND_83C553 ||
	    bridge->class >> 8 != PCI_CLASS_BRIDGE_ISA)
		return -1;

	/*
	 * We need to find function 0's revision, not function 1
	 */
	pci_read_config_byte(bridge, PCI_REVISION_ID, &rev);

	return rev;
}

/*
 * Enable the PCI device
 */
static unsigned int __init sl82c105_init_chipset(struct pci_dev *dev)
{
	u32 val;

	pci_read_config_dword(dev, 0x40, &val);
	val |= CTRL_P0EN | CTRL_P0F16 | CTRL_P1EN | CTRL_P1F16;
	pci_write_config_dword(dev, 0x40, val);

	return dev->irq;
}

static void __init sl82c105_init_dma(struct ata_channel *ch, unsigned long dma_base)
{
	unsigned int bridge_rev;
	byte dma_state;

	dma_state = inb(dma_base + 2);
	bridge_rev = sl82c105_bridge_revision(ch->pci_dev);
	if (bridge_rev <= 5) {
		ch->autodma = 0;
		ch->drives[0].autotune = 1;
		ch->drives[1].autotune = 1;
		printk("    %s: Winbond 553 bridge revision %d, BM-DMA disabled\n",
		       ch->name, bridge_rev);
		dma_state &= ~0x60;
	} else {
		dma_state |= 0x60;
	}
	outb(dma_state, dma_base + 2);

	ata_init_dma(ch, dma_base);

	if (bridge_rev <= 5)
		ch->udma_setup = NULL;
	else {
		ch->udma_setup    = sl82c105_dma_setup;
		ch->udma_enable   = sl82c105_dma_enable;
		ch->udma_init	  = sl82c105_dma_init;
		ch->udma_timeout  = sl82c105_timeout;
		ch->udma_irq_lost = sl82c105_lostirq;
	}
}

/*
 * Initialise the chip
 */
static void __init sl82c105_init_channel(struct ata_channel *ch)
{
	ch->tuneproc = tune_sl82c105;
}


/* module data table */
static struct ata_pci_device chipset __initdata = {
	vendor: PCI_VENDOR_ID_WINBOND,
	device: PCI_DEVICE_ID_WINBOND_82C105,
	init_chipset: sl82c105_init_chipset,
	init_channel: sl82c105_init_channel,
	init_dma: sl82c105_init_dma,
	enablebits: { {0x40,0x01,0x01}, {0x40,0x10,0x10} },
	bootable: ON_BOARD
};

int __init init_sl82c105(void)
{
	ata_register_chipset(&chipset);

	return 0;
}
