/**** vi:set ts=8 sts=8 sw=8:************************************************
 *
 *  Copyright (C) 2002	     Marcin Dalecki <martin@dalecki.de>
 *  Copyright (C) 1998-2000  Andre Hedrick <andre@linux-ide.org>
 *  Copyright (C) 1995-1998  Mark Lord
 *
 *  May be copied or modified under the terms of the GNU General Public License
 */

/*
 *  This module provides support for automatic detection and configuration of
 *  all PCI ATA host chip chanells interfaces present in a system.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/ide.h>

#include <asm/io.h>
#include <asm/irq.h>

#include "pcihost.h"

/*
 * This is the list of registered PCI chipset driver data structures.
 */
static struct ata_pci_device *ata_pci_device_list = NULL;

/*
 * This function supplies the data necessary to detect the particular chipset.
 *
 * Please note that we don't copy data over. We are just linking it in to the
 * list.
 */
void ata_register_chipset(struct ata_pci_device *d)
{
	struct ata_pci_device *tmp;

	if (!d)
		return;

	d->next = NULL;

	if (!ata_pci_device_list) {
		ata_pci_device_list = d;

		return;
	}

	tmp = ata_pci_device_list;
	while (tmp->next) {
		tmp = tmp->next;
	}

	tmp->next = d;
}

/*
 * Match a PCI IDE port against an entry in ide_hwifs[],
 * based on io_base port if possible.
 */
static struct ata_channel __init *lookup_channel(unsigned long io_base, int bootable, const char *name)
{
	int h;
	struct ata_channel *ch;

	/*
	 * Look for a channel with matching io_base default value.  If chipset is
	 * "ide_unknown", then claim that channel slot.  Otherwise, some other
	 * chipset has already claimed it..  :(
	 */
	for (h = 0; h < MAX_HWIFS; ++h) {
		ch = &ide_hwifs[h];
		if (ch->io_ports[IDE_DATA_OFFSET] == io_base) {
			if (ch->chipset == ide_generic)
				return ch; /* a perfect match */
			if (ch->chipset == ide_unknown)
				return ch; /* match */
			printk(KERN_INFO "%s: port 0x%04lx already claimed by %s\n",
					name, io_base, ch->name);
			return NULL;	/* already claimed */
		}
	}

	/*
	 * Okay, there is no ch matching our io_base, so we'll just claim an
	 * unassigned slot.
	 *
	 * Give preference to claiming other slots before claiming ide0/ide1,
	 * just in case there's another interface yet-to-be-scanned which uses
	 * ports 1f0/170 (the ide0/ide1 defaults).
	 *
	 * Unless there is a bootable card that does not use the standard ports
	 * 1f0/170 (the ide0/ide1 defaults). The (bootable) flag.
	 */

	if (bootable == ON_BOARD) {
		for (h = 0; h < MAX_HWIFS; ++h) {
			ch = &ide_hwifs[h];
			if (ch->chipset == ide_unknown)
				return ch;	/* pick an unused entry */
		}
	} else {
		for (h = 2; h < MAX_HWIFS; ++h) {
			ch = &ide_hwifs[h];
			if (ch->chipset == ide_unknown)
				return ch;	/* pick an unused entry */
		}
	}
	for (h = 0; h < 2; ++h) {
		ch = &ide_hwifs[h];
		if (ch->chipset == ide_unknown)
			return ch;	/* pick an unused entry */
	}
	printk(KERN_INFO "%s: too many ATA interfaces.\n", name);

	return NULL;
}

static int __init setup_pci_baseregs(struct pci_dev *dev, const char *name)
{
	u8 reg;
	u8 progif = 0;

	/*
	 * Place both IDE interfaces into PCI "native" mode:
	 */
	if (pci_read_config_byte(dev, PCI_CLASS_PROG, &progif) || (progif & 5) != 5) {
		if ((progif & 0xa) != 0xa) {
			printk("%s: device not capable of full native PCI mode\n", name);
			return 1;
		}
		printk("%s: placing both ports into native PCI mode\n", name);
		pci_write_config_byte(dev, PCI_CLASS_PROG, progif|5);
		if (pci_read_config_byte(dev, PCI_CLASS_PROG, &progif) || (progif & 5) != 5) {
			printk("%s: rewrite of PROGIF failed, wanted 0x%04x, got 0x%04x\n", name, progif|5, progif);
			return 1;
		}
	}
	/*
	 * Setup base registers for IDE command/control spaces for each interface:
	 */
	for (reg = 0; reg < 4; reg++) {
		struct resource *res = dev->resource + reg;
		if ((res->flags & IORESOURCE_IO) == 0)
			continue;
		if (!res->start) {
			printk("%s: Missing I/O address #%d\n", name, reg);
			return 1;
		}
	}
	return 0;
}

#ifdef CONFIG_BLK_DEV_IDEDMA
/*
 * Setup DMA transfers on the channel.
 */
static void __init setup_channel_dma(struct pci_dev *dev,
		struct ata_pci_device* d,
		int autodma,
		struct ata_channel *ch)
{
	unsigned long dma_base;

	if (d->flags & ATA_F_NOADMA)
		autodma = 0;

	if (autodma)
		ch->autodma = 1;

	if (!((d->flags & ATA_F_DMA) || ((dev->class >> 8) == PCI_CLASS_STORAGE_IDE && (dev->class & 0x80))))
		return;

	/*
	 * Fetch the DMA Bus-Master-I/O-Base-Address (BMIBA) from PCI space:
	 */
	dma_base = pci_resource_start(dev, 4);
	if (dma_base) {
		/* PDC20246, PDC20262, HPT343, & HPT366 */
		if ((ch->unit == ATA_PRIMARY) && d->extra) {
			request_region(dma_base + 16, d->extra, dev->name);
			ch->dma_extra = d->extra;
		}

		/* If we are on the second channel, the dma base address will
		 * be one entry away from the primary interface.
		 */
		if (ch->unit == ATA_SECONDARY)
				dma_base += 8;

		if (d->flags & ATA_F_SIMPLEX) {
			outb(inb(dma_base + 2) & 0x60, dma_base + 2);
			if (inb(dma_base + 2) & 0x80)
				printk(KERN_INFO "%s: simplex device: DMA forced\n", dev->name);
		} else {
			/* If the device claims "simplex" DMA, this means only
			 * one of the two interfaces can be trusted with DMA at
			 * any point in time.  So we should enable DMA only on
			 * one of the two interfaces.
			 */
			if ((inb(dma_base + 2) & 0x80)) {
				if ((!ch->drives[0].present && !ch->drives[1].present) ||
				    ch->unit == ATA_SECONDARY) {
					printk(KERN_INFO "%s: simplex device:  DMA disabled\n", dev->name);
					dma_base = 0;
				}
			}
		}
	} else {
		printk(KERN_INFO "%s: %s Bus-Master DMA was disabled by BIOS\n",
		       ch->name, dev->name);

		return;
	}

	/* The function below will check itself whatever there is something to
	 * be done or not. We don't have therefore to care whatever it was
	 * already enabled by the primary channel run.
	 */
	pci_set_master(dev);
	if (d->init_dma)
		d->init_dma(ch, dma_base);
	else
		ata_init_dma(ch, dma_base);
}
#endif

/*
 * Setup a particular port on an ATA host controller.
 *
 * This gets called once for the master and for the slave interface.
 */
static int __init setup_host_channel(struct pci_dev *dev,
		struct ata_pci_device *d,
		int port,
		u8 class_rev,
		int pciirq,
		int autodma)
{
	unsigned long base = 0;
	unsigned long ctl = 0;
	ide_pci_enablebit_t *e = &(d->enablebits[port]);
	struct ata_channel *ch;

	u8 tmp;
	if (port == ATA_SECONDARY) {

		/* If this is a Promise FakeRaid controller, the 2nd controller
		 * will be marked as disabled while it is actually there and
		 * enabled by the bios for raid purposes.  Skip the normal "is
		 * it enabled" test for those.
		 */
		if (d->flags & ATA_F_PHACK)
			goto controller_ok;
	}

	/* Test whatever the port is enabled.
	 */
	if (e->reg) {
		if (pci_read_config_byte(dev, e->reg, &tmp))
			return 0; /* error! */
		if ((tmp & e->mask) != e->val)
			return 0;
	}

	/* Nothing to be done for the second port.
	 */
	if (port == ATA_SECONDARY) {
		if ((d->flags & ATA_F_HPTHACK) && (class_rev < 0x03))
			return 0;
	}
controller_ok:
	if ((dev->class >> 8) != PCI_CLASS_STORAGE_IDE || (dev->class & (port ? 4 : 1)) != 0) {
		ctl  = dev->resource[(2 * port) + 1].start;
		base = dev->resource[2 * port].start;
		if (!(ctl & PCI_BASE_ADDRESS_IO_MASK) || !(base & PCI_BASE_ADDRESS_IO_MASK)) {
			printk(KERN_WARNING "%s: error: IO reported as MEM by BIOS!\n", dev->name);
			/* try it with the default values */
			ctl = 0;
			base = 0;
		}
	}
	if (ctl && !base) {
		printk(KERN_WARNING "%s: error: missing MEM base info from BIOS!\n", dev->name);
		/* we will still try to get along with the default */
	}
	if (base && !ctl) {
		printk(KERN_WARNING "%s: error: missing IO base info from BIOS!\n", dev->name);
		/* we will still try to get along with the default */
	}

	/* Fill in the default values: */
	if (!ctl)
		ctl = port ? 0x374 : 0x3f4;
	if (!base)
		base = port ? 0x170 : 0x1f0;

	if ((ch = lookup_channel(base, d->bootable, dev->name)) == NULL)
		return -ENOMEM;	/* no room */

	if (ch->io_ports[IDE_DATA_OFFSET] != base) {
		ide_init_hwif_ports(&ch->hw, base, (ctl | 2), NULL);
		memcpy(ch->io_ports, ch->hw.io_ports, sizeof(ch->io_ports));
		ch->noprobe = !ch->io_ports[IDE_DATA_OFFSET];
	}

	ch->chipset = ide_pci;
	ch->pci_dev = dev;
	ch->unit = port;
	if (!ch->irq)
		ch->irq = pciirq;

	/* Serialize the interfaces if requested by configuration information.
	 */
	if (d->flags & ATA_F_SER)
	    ch->serialized = 1;

	/* Cross wired IRQ lines on UMC chips and no DMA transfers.*/
	if (d->flags & ATA_F_FIXIRQ) {
		ch->irq = port ? 15 : 14;
		goto no_dma;
	}
	if (d->flags & ATA_F_NODMA)
		goto no_dma;

	if (ch->udma_four)
		printk("%s: warning: ATA-66/100 forced bit set!\n", dev->name);


#ifdef CONFIG_BLK_DEV_IDEDMA
	/*
	 * Setup DMA transfers on the channel.
	 */
	setup_channel_dma(dev, d, autodma, ch);
#endif
no_dma:
	/* Call chipset-specific routine for each enabled channel. */
	if (d->init_channel)
		d->init_channel(ch);

#ifdef CONFIG_BLK_DEV_IDEDMA
	if ((d->flags & ATA_F_NOADMA) || noautodma)
		ch->autodma = 0;
#endif

	return 0;
}

/*
 * Looks at the primary/secondary channels on a PCI IDE device and, if they are
 * enabled, prepares the IDE driver for use with them.  This generic code works
 * for most PCI chipsets.
 *
 * One thing that is not standardized is the location of the primary/secondary
 * interface "enable/disable" bits.  For chipsets that we "know" about, this
 * information is in the struct ata_pci_device struct; for all other chipsets,
 * we just assume both interfaces are enabled.
 */
static void __init setup_pci_device(struct pci_dev *dev, struct ata_pci_device *d)
{
	int autodma = 0;
	int pciirq = 0;
	unsigned short pcicmd = 0;
	unsigned short tried_config = 0;
	unsigned int class_rev;

#ifdef CONFIG_IDEDMA_AUTO
	if (!noautodma)
		autodma = 1;
#endif

	if (pci_enable_device(dev)) {
		printk(KERN_WARNING "%s: Could not enable PCI device.\n", dev->name);
		return;
	}

check_if_enabled:
	if (pci_read_config_word(dev, PCI_COMMAND, &pcicmd)) {
		printk(KERN_ERR "%s: error accessing PCI regs\n", dev->name);
		return;
	}
	if (!(pcicmd & PCI_COMMAND_IO)) {	/* is device disabled? */
		/*
		 * PnP BIOS was *supposed* to have set this device up for us,
		 * but we can do it ourselves, so long as the BIOS has assigned
		 * an IRQ (or possibly the device is using a "legacy header"
		 * for IRQs).  Maybe the user deliberately *disabled* the
		 * device, but we'll eventually ignore it again if no drives
		 * respond.
		 */
		if (tried_config++
		 || setup_pci_baseregs(dev, dev->name)
		 || pci_write_config_word(dev, PCI_COMMAND, pcicmd | PCI_COMMAND_IO)) {
			printk("%s: device disabled (BIOS)\n", dev->name);
			return;
		}
		autodma = 0;	/* default DMA off if we had to configure it here */
		goto check_if_enabled;
	}
	if (tried_config)
		printk("%s: device enabled (Linux)\n", dev->name);

	pci_read_config_dword(dev, PCI_CLASS_REVISION, &class_rev);
	class_rev &= 0xff;

	if (d->vendor == PCI_VENDOR_ID_TTI && PCI_DEVICE_ID_TTI_HPT343) {
		/* see comments in hpt34x.c to see why... */
		d->bootable = (pcicmd & PCI_COMMAND_MEMORY) ? OFF_BOARD : NEVER_BOARD;
	}

	printk(KERN_INFO "ATA: chipset rev.: %d\n", class_rev);

	/*
	 * Can we trust the reported IRQ?
	 */
	pciirq = dev->irq;

	if (dev->class >> 8 == PCI_CLASS_STORAGE_RAID) {
		/* By rights we want to ignore these, but the Promise Fastrak
		   people have some strange ideas about proprietary so we have
		   to act otherwise on those. The Supertrak however we need
		   to skip */
		if (d->vendor == PCI_VENDOR_ID_PROMISE && d->device == PCI_DEVICE_ID_PROMISE_20265) {
			printk(KERN_INFO "ATA: Found promise 20265 in RAID mode.\n");
			if(dev->bus->self && dev->bus->self->vendor == PCI_VENDOR_ID_INTEL &&
				dev->bus->self->device == PCI_DEVICE_ID_INTEL_I960)
			{
				printk(KERN_INFO "ATA: Skipping Promise PDC20265 attached to I2O RAID controller.\n");
				return;
			}
		}
		/* Its attached to something else, just a random bridge.
		   Suspect a fastrak and fall through */
	}
	if ((dev->class & ~(0xfa)) != ((PCI_CLASS_STORAGE_IDE << 8) | 5)) {
		printk(KERN_INFO "ATA: non-legacy mode: IRQ probe delayed\n");

		/*
		 * This allows off board ide-pci cards to enable a BIOS,
		 * verify interrupt settings of split-mirror pci-config
		 * space, place chipset into init-mode, and/or preserve
		 * an interrupt if the card is not native ide support.
		 */
		if (d->init_chipset)
			pciirq = d->init_chipset(dev);
		else {
			if (d->flags & ATA_F_IRQ)
				pciirq = dev->irq;
			else
				pciirq =  0;
		}
	} else if (tried_config) {
		printk(KERN_INFO "ATA: will probe IRQs later\n");
		pciirq = 0;
	} else if (!pciirq) {
		printk(KERN_INFO "ATA: invalid IRQ (%d): will probe later\n", pciirq);
		pciirq = 0;
	} else {
		if (d->init_chipset)
			d->init_chipset(dev);
#ifdef __sparc__
		printk(KERN_INFO "ATA: 100%% native mode on irq %s\n", __irq_itoa(pciirq));
#else
		printk(KERN_INFO "ATA: 100%% native mode on irq %d\n", pciirq);
#endif
	}

	/*
	 * Set up IDE chanells. First the primary, then the secondary.
	 */
	setup_host_channel(dev, d, ATA_PRIMARY, class_rev, pciirq, autodma);
	setup_host_channel(dev, d, ATA_SECONDARY, class_rev, pciirq, autodma);
}

/*
 * Fix crossover IRQ line setups between primary and secondary channel.  Quite
 * a common bug apparently.
 */
static void __init pdc20270_device_order_fixup (struct pci_dev *dev, struct ata_pci_device *d)
{
	struct pci_dev *dev2 = NULL;
	struct pci_dev *findev;
	struct ata_pci_device *d2;

	if (dev->bus->self &&
	    dev->bus->self->vendor == PCI_VENDOR_ID_DEC &&
	    dev->bus->self->device == PCI_DEVICE_ID_DEC_21150) {
		if (PCI_SLOT(dev->devfn) & 2) {
			return;
		}
		d->extra = 0;
		pci_for_each_dev(findev) {
			if ((findev->vendor == dev->vendor) &&
			    (findev->device == dev->device) &&
			    (PCI_SLOT(findev->devfn) & 2)) {
				u8 irq = 0;
				u8 irq2 = 0;
				dev2 = findev;
				pci_read_config_byte(dev, PCI_INTERRUPT_LINE, &irq);
				pci_read_config_byte(dev2, PCI_INTERRUPT_LINE, &irq2);
                                if (irq != irq2) {
					dev2->irq = dev->irq;
                                        pci_write_config_byte(dev2, PCI_INTERRUPT_LINE, irq);
                                }

			}
		}
	}
	printk(KERN_INFO "ATA: %s: controller, PCI slot %s\n",
			dev->name, dev->slot_name);
	setup_pci_device(dev, d);
	if (!dev2)
		return;
	d2 = d;
	printk(KERN_INFO "ATA: %s: controller, PCI slot %s\n",
			dev2->name, dev2->slot_name);
	setup_pci_device(dev2, d2);
}

static void __init hpt374_device_order_fixup (struct pci_dev *dev, struct ata_pci_device *d)
{
	struct pci_dev *dev2 = NULL;
	struct pci_dev *findev;
	struct ata_pci_device *d2;

	if (PCI_FUNC(dev->devfn) & 1)
		return;

	pci_for_each_dev(findev) {
		if ((findev->vendor == dev->vendor) &&
		    (findev->device == dev->device) &&
		    ((findev->devfn - dev->devfn) == 1) &&
		    (PCI_FUNC(findev->devfn) & 1)) {
			dev2 = findev;
			break;
		}
	}

	printk(KERN_INFO "ATA: %s: controller, PCI slot %s\n",
		dev->name, dev->slot_name);
	setup_pci_device(dev, d);
	if (!dev2) {
		return;
	} else {
		byte irq = 0, irq2 = 0;
		pci_read_config_byte(dev, PCI_INTERRUPT_LINE, &irq);
		pci_read_config_byte(dev2, PCI_INTERRUPT_LINE, &irq2);
		if (irq != irq2) {
			pci_write_config_byte(dev2, PCI_INTERRUPT_LINE, irq);
			dev2->irq = dev->irq;
			printk("%s: pci-config space interrupt fixed.\n",
				dev2->name);
		}
	}
	d2 = d;
	printk(KERN_INFO "ATA: %s: controller, PCI slot %s\n",
		dev2->name, dev2->slot_name);
	setup_pci_device(dev2, d2);

}

static void __init hpt366_device_order_fixup (struct pci_dev *dev, struct ata_pci_device *d)
{
	struct pci_dev *dev2 = NULL, *findev;
	struct ata_pci_device *d2;
	unsigned char pin1 = 0, pin2 = 0;
	unsigned int class_rev;

	if (PCI_FUNC(dev->devfn) & 1)
		return;

	pci_read_config_dword(dev, PCI_CLASS_REVISION, &class_rev);
	class_rev &= 0xff;

	switch(class_rev) {
		case 5:
		case 4:
		case 3:	printk(KERN_INFO "ATA: %s: controller, PCI slot %s\n",
					dev->name, dev->slot_name);
			setup_pci_device(dev, d);
			return;
		default:	break;
	}

	pci_read_config_byte(dev, PCI_INTERRUPT_PIN, &pin1);
	pci_for_each_dev(findev) {
		if (findev->vendor == dev->vendor &&
		    findev->device == dev->device &&
		    ((findev->devfn - dev->devfn) == 1) &&
		    (PCI_FUNC(findev->devfn) & 1)) {
			dev2 = findev;
			pci_read_config_byte(dev2, PCI_INTERRUPT_PIN, &pin2);
			if ((pin1 != pin2) && (dev->irq == dev2->irq)) {
				d->bootable = ON_BOARD;
				printk(KERN_INFO "ATAL: %s: onboard version of chipset, pin1=%d pin2=%d\n", dev->name, pin1, pin2);
			}
			break;
		}
	}
	printk(KERN_INFO "ATA: %s: controller, PCI slot %s\n",
			dev->name, dev->slot_name);
	setup_pci_device(dev, d);
	if (!dev2)
		return;
	d2 = d;
	printk(KERN_INFO "ATA: %s: controller, PCI slot %s\n",
			dev2->name, dev2->slot_name);
	setup_pci_device(dev2, d2);
}



/*
 * This finds all PCI IDE controllers and calls appropriate initialization
 * functions for them.
 */
static void __init scan_pcidev(struct pci_dev *dev)
{
	unsigned short vendor;
	unsigned short device;
	struct ata_pci_device *d;

	vendor = dev->vendor;
	device = dev->device;



	/* Look up the chipset information.
	 * We expect only one match.
	 */
	for (d = ata_pci_device_list; d; d = d->next) {
		if (d->vendor == vendor && d->device == device)
			break;
	}

	if (!d) {
		/* Only check the device calls, if it wasn't listed, since
		 * there are in esp. some pdc202xx chips which "work around"
		 * beeing grabbed by generic drivers.
		 */
		if ((dev->class >> 8) == PCI_CLASS_STORAGE_IDE) {
			printk(KERN_INFO "ATA: unknown interface: %s, PCI slot %s\n",
					dev->name, dev->slot_name);
		}
		return;
	}

	if (d->init_channel == ATA_PCI_IGNORE)
		printk(KERN_INFO "ATA: %s: ignored by PCI bus scan\n", dev->name);
	else if ((d->vendor == PCI_VENDOR_ID_OPTI && d->device == PCI_DEVICE_ID_OPTI_82C558) && !(PCI_FUNC(dev->devfn) & 1))
		return;
	else if ((d->vendor == PCI_VENDOR_ID_CONTAQ && d->device == PCI_DEVICE_ID_CONTAQ_82C693) && (!(PCI_FUNC(dev->devfn) & 1) || !((dev->class >> 8) == PCI_CLASS_STORAGE_IDE)))
		return;	/* CY82C693 is more than only a IDE controller */
	else if ((d->vendor == PCI_VENDOR_ID_ITE && d->device == PCI_DEVICE_ID_ITE_IT8172G) && (!(PCI_FUNC(dev->devfn) & 1) || !((dev->class >> 8) == PCI_CLASS_STORAGE_IDE)))
		return;	/* IT8172G is also more than only an IDE controller */
	else if ((d->vendor == PCI_VENDOR_ID_UMC && d->device == PCI_DEVICE_ID_UMC_UM8886A) && !(PCI_FUNC(dev->devfn) & 1))
		return;	/* UM8886A/BF pair */
	else if (d->flags & ATA_F_HPTHACK) {
		if (d->device == PCI_DEVICE_ID_TTI_HPT366)
			hpt366_device_order_fixup(dev, d);
		if (d->device == PCI_DEVICE_ID_TTI_HPT374)
			hpt374_device_order_fixup(dev, d);
	} else if (d->vendor == PCI_VENDOR_ID_PROMISE && d->device == PCI_DEVICE_ID_PROMISE_20268R)
		pdc20270_device_order_fixup(dev, d);
	else {
		printk(KERN_INFO "ATA: %s, PCI slot %s\n",
				dev->name, dev->slot_name);
		setup_pci_device(dev, d);
	}
}

void __init ide_scan_pcibus(int scan_direction)
{
	struct pci_dev *dev;

	if (!scan_direction) {
		pci_for_each_dev(dev) {
			scan_pcidev(dev);
		}
	} else {
		pci_for_each_dev_reverse(dev) {
			scan_pcidev(dev);
		}
	}
}

/* known chips without particular chipset driver module data table */
/* Those are id's of chips we don't deal currently with, but which still need
 * some generic quirk handling.
 */
static struct ata_pci_device chipsets[] __initdata = {
	{
		vendor: PCI_VENDOR_ID_PCTECH,
		device: PCI_DEVICE_ID_PCTECH_SAMURAI_IDE,
		bootable: ON_BOARD
	},
	{
		vendor: PCI_VENDOR_ID_CMD,
		device: PCI_DEVICE_ID_CMD_640,
		init_channel: ATA_PCI_IGNORE,
		bootable: ON_BOARD
	},
	{
		vendor: PCI_VENDOR_ID_NS,
		device: PCI_DEVICE_ID_NS_87410,
		enablebits: {{0x43,0x08,0x08}, {0x47,0x08,0x08}},
		bootable: ON_BOARD
	},
	{
		vendor: PCI_VENDOR_ID_HINT,
		device: PCI_DEVICE_ID_HINT_VXPROII_IDE,
		bootable: ON_BOARD
	},
	{
		vendor: PCI_VENDOR_ID_HOLTEK,
		device: PCI_DEVICE_ID_HOLTEK_6565,
		bootable: ON_BOARD
	},
	{
		vendor: PCI_VENDOR_ID_INTEL,
		device: PCI_DEVICE_ID_INTEL_82371MX,
		enablebits: {{0x6D,0x80,0x80}, {0x00,0x00,0x00}},
		bootable: ON_BOARD,
		flags: ATA_F_NODMA
	},
	{
		vendor: PCI_VENDOR_ID_UMC,
		device: PCI_DEVICE_ID_UMC_UM8673F,
		bootable: ON_BOARD,
		flags: ATA_F_FIXIRQ
	},
	{
		vendor: PCI_VENDOR_ID_UMC,
		device: PCI_DEVICE_ID_UMC_UM8886A,
		bootable: ON_BOARD,
		flags: ATA_F_FIXIRQ
	},
	{
		vendor: PCI_VENDOR_ID_UMC,
		device: PCI_DEVICE_ID_UMC_UM8886BF,
		bootable: ON_BOARD,
		flags: ATA_F_FIXIRQ
	},
	{
		vendor: PCI_VENDOR_ID_VIA,
		device: PCI_DEVICE_ID_VIA_82C561,
		bootable: ON_BOARD,
		flags: ATA_F_NOADMA
	},
	{
		vendor: PCI_VENDOR_ID_VIA,
		device: PCI_DEVICE_ID_VIA_82C586_1,
		bootable: ON_BOARD,
		flags: ATA_F_NOADMA
	},
	{
		vendor: PCI_VENDOR_ID_TTI,
		device: PCI_DEVICE_ID_TTI_HPT366,
		bootable: OFF_BOARD,
		extra: 240,
		flags: ATA_F_IRQ | ATA_F_HPTHACK
	}
};

int __init init_ata_pci_misc(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(chipsets); ++i) {
		ata_register_chipset(&chipsets[i]);
	}

	return 0;
}
