/*
 *  linux/drivers/ide/rz1000.c		Version 0.05	December 8, 1997
 *
 *  Copyright (C) 1995-1998  Linus Torvalds & author (see below)
 */

/*
 *  Principal Author:  mlord@pobox.com (Mark Lord)
 *
 *  See linux/MAINTAINERS for address of current maintainer.
 *
 *  This file provides support for disabling the buggy read-ahead
 *  mode of the RZ1000 IDE chipset, commonly used on Intel motherboards.
 *
 *  Dunno if this fixes both ports, or only the primary port (?).
 */

#undef REALLY_SLOW_IO		/* most systems can safely undef this */

#include <linux/config.h> /* for CONFIG_BLK_DEV_IDEPCI */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/pci.h>
#include <linux/ide.h>
#include <linux/init.h>

#include <asm/io.h>

#ifdef CONFIG_BLK_DEV_IDEPCI

void __init ide_init_rz1000 (ide_hwif_t *hwif)	/* called from ide-pci.c */
{
	unsigned short reg;
	struct pci_dev *dev = hwif->pci_dev;

	hwif->chipset = ide_rz1000;
	if (!pci_read_config_word (dev, 0x40, &reg)
	 && !pci_write_config_word(dev, 0x40, reg & 0xdfff))
	{
		printk("%s: disabled chipset read-ahead (buggy RZ1000/RZ1001)\n", hwif->name);
	} else {
		hwif->serialized = 1;
		hwif->drives[0].no_unmask = 1;
		hwif->drives[1].no_unmask = 1;
		printk("%s: serialized, disabled unmasking (buggy RZ1000/RZ1001)\n", hwif->name);
	}
}

#else

static void __init init_rz1000 (struct pci_dev *dev, const char *name)
{
	unsigned short reg, h;

	if (!pci_read_config_word (dev, PCI_COMMAND, &reg) && !(reg & PCI_COMMAND_IO)) {
		printk("%s: buggy IDE controller disabled (BIOS)\n", name);
		return;
	}
	if (!pci_read_config_word (dev, 0x40, &reg)
	 && !pci_write_config_word(dev, 0x40, reg & 0xdfff))
	{
		printk("IDE: disabled chipset read-ahead (buggy %s)\n", name);
	} else {
		for (h = 0; h < MAX_HWIFS; ++h) {
			ide_hwif_t *hwif = &ide_hwifs[h];
			if ((hwif->io_ports[IDE_DATA_OFFSET] == 0x1f0 || hwif->io_ports[IDE_DATA_OFFSET] == 0x170)
			 && (hwif->chipset == ide_unknown || hwif->chipset == ide_generic))
			{
				hwif->chipset = ide_rz1000;
				hwif->serialized = 1;
				hwif->drives[0].no_unmask = 1;
				hwif->drives[1].no_unmask = 1;
				if (hwif->io_ports[IDE_DATA_OFFSET] == 0x170)
					hwif->channel = 1;
				printk("%s: serialized, disabled unmasking (buggy %s)\n", hwif->name, name);
			}
		}
	}
}

void __init ide_probe_for_rz100x (void)	/* called from ide.c */
{
	struct pci_dev *dev = NULL;

	while ((dev = pci_find_device(PCI_VENDOR_ID_PCTECH, PCI_DEVICE_ID_PCTECH_RZ1000, dev))!=NULL)
		init_rz1000 (dev, "RZ1000");
	while ((dev = pci_find_device(PCI_VENDOR_ID_PCTECH, PCI_DEVICE_ID_PCTECH_RZ1001, dev))!=NULL)
		init_rz1000 (dev, "RZ1001");
}

#endif CONFIG_BLK_DEV_IDEPCI
