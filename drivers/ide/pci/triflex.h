/* 
 * triflex.h
 *
 * Copyright (C) 2002 Hewlett-Packard Development Group, L.P.
 * Author: Torben Mathiasen <torben.mathiasen@hp.com>
 *
 */
#ifndef TRIFLEX_H
#define TRIFLEX_H

#include <linux/config.h>
#include <linux/pci.h>
#include <linux/ide.h>

static unsigned int __devinit init_chipset_triflex(struct pci_dev *, const char *);
static void init_hwif_triflex(ide_hwif_t *);

static ide_pci_device_t triflex_devices[] __devinitdata = {
	{
		.vendor 	= PCI_VENDOR_ID_COMPAQ,
		.device		= PCI_DEVICE_ID_COMPAQ_TRIFLEX_IDE,
		.name		= "TRIFLEX",
		.init_chipset	= init_chipset_triflex,
		.init_hwif	= init_hwif_triflex,
		.channels	= 2,
		.autodma	= AUTODMA,
		.enablebits	= {{0x80, 0x01, 0x01}, {0x80, 0x02, 0x02}},
		.bootable	= ON_BOARD,
	}
};

static struct pci_device_id triflex_pci_tbl[] = {
	{ PCI_VENDOR_ID_COMPAQ, PCI_DEVICE_ID_COMPAQ_TRIFLEX_IDE, PCI_ANY_ID, 
		PCI_ANY_ID, 0, 0, 0 },
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, triflex_pci_tbl);

#endif /* TRIFLEX_H */
