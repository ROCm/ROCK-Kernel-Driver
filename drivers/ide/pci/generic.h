#ifndef IDE_GENERIC_H
#define IDE_GENERIC_H

#include <linux/config.h>
#include <linux/pci.h>
#include <linux/ide.h>

static unsigned int init_chipset_generic(struct pci_dev *, const char *);
static void init_hwif_generic(ide_hwif_t *);

static ide_pci_device_t generic_chipsets[] __devinitdata = {
	{	/* 0 */
		.name		= "NS87410",
		.init_chipset	= init_chipset_generic,
		.init_hwif	= init_hwif_generic,
		.channels	= 2,
		.autodma	= AUTODMA,
		.enablebits	= {{0x43,0x08,0x08}, {0x47,0x08,0x08}},
		.bootable	= ON_BOARD,
        },{	/* 1 */
		.name		= "SAMURAI",
		.init_chipset	= init_chipset_generic,
		.init_hwif	= init_hwif_generic,
		.channels	= 2,
		.autodma	= AUTODMA,
		.bootable	= ON_BOARD,
	},{	/* 2 */
		.name		= "HT6565",
		.init_chipset	= init_chipset_generic,
		.init_hwif	= init_hwif_generic,
		.channels	= 2,
		.autodma	= AUTODMA,
		.bootable	= ON_BOARD,
	},{	/* 3 */
		.name		= "UM8673F",
		.init_chipset	= init_chipset_generic,
		.init_hwif	= init_hwif_generic,
		.channels	= 2,
		.autodma	= NODMA,
		.bootable	= ON_BOARD,
	},{	/* 4 */
		.name		= "UM8886A",
		.init_chipset	= init_chipset_generic,
		.init_hwif	= init_hwif_generic,
		.channels	= 2,
		.autodma	= NODMA,
		.bootable	= ON_BOARD,
	},{	/* 5 */
		.name		= "UM8886BF",
		.init_chipset	= init_chipset_generic,
		.init_hwif	= init_hwif_generic,
		.channels	= 2,
		.autodma	= NODMA,
		.bootable	= ON_BOARD,
	},{	/* 6 */
		.name		= "HINT_IDE",
		.init_chipset	= init_chipset_generic,
		.init_hwif	= init_hwif_generic,
		.channels	= 2,
		.autodma	= AUTODMA,
		.bootable	= ON_BOARD,
	},{	/* 7 */
		.name		= "VIA_IDE",
		.init_chipset	= init_chipset_generic,
		.init_hwif	= init_hwif_generic,
		.channels	= 2,
		.autodma	= NOAUTODMA,
		.bootable	= ON_BOARD,
	},{	/* 8 */
		.name		= "OPTI621V",
		.init_chipset	= init_chipset_generic,
		.init_hwif	= init_hwif_generic,
		.channels	= 2,
		.autodma	= NOAUTODMA,
		.bootable	= ON_BOARD,
	},{	/* 9 */
		.name		= "VIA8237SATA",
		.init_chipset	= init_chipset_generic,
		.init_hwif	= init_hwif_generic,
		.channels	= 2,
		.autodma	= AUTODMA,
		.bootable	= OFF_BOARD,
	},{ /* 10 */
		.name 		= "Piccolo0102",
		.init_chipset	= init_chipset_generic,
		.init_hwif	= init_hwif_generic,
		.channels	= 2,
		.autodma	= NOAUTODMA,
		.bootable	= ON_BOARD,
	},{ /* 11 */
		.name 		= "Piccolo0103",
		.init_chipset	= init_chipset_generic,
		.init_hwif	= init_hwif_generic,
		.channels	= 2,
		.autodma	= NOAUTODMA,
		.bootable	= ON_BOARD,
	},{ /* 12 */
		.name 		= "Piccolo0105",
		.init_chipset	= init_chipset_generic,
		.init_hwif	= init_hwif_generic,
		.channels	= 2,
		.autodma	= NOAUTODMA,
		.bootable	= ON_BOARD,
	}
};

#if 0
static ide_pci_device_t unknown_chipset[] __devinitdata = {
	{	/* 0 */
		.name		= "PCI_IDE",
		.init_chipset	= init_chipset_generic,
		.init_hwif	= init_hwif_generic,
		.channels	= 2,
		.autodma	= AUTODMA,
		.bootable	= ON_BOARD,
	}
};
#endif

#endif /* IDE_GENERIC_H */
