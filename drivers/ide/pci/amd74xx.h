#ifndef AMD74XX_H
#define AMD74XX_H

#include <linux/config.h>
#include <linux/pci.h>
#include <linux/ide.h>

#define DISPLAY_AMD_TIMINGS

static unsigned int init_chipset_amd74xx(struct pci_dev *, const char *);
static void init_hwif_amd74xx(ide_hwif_t *);

static ide_pci_device_t amd74xx_chipsets[] __devinitdata = {
	{	/* 0 */
		.name		= "AMD7401",
		.init_chipset	= init_chipset_amd74xx,
		.init_hwif	= init_hwif_amd74xx,
		.channels	= 2,
		.autodma	= AUTODMA,
		.enablebits	= {{0x40,0x02,0x02}, {0x40,0x01,0x01}},
		.bootable	= ON_BOARD,
	},{	/* 1 */
		.name		= "AMD7409",
		.init_chipset	= init_chipset_amd74xx,
		.init_hwif	= init_hwif_amd74xx,
		.channels	= 2,
		.autodma	= AUTODMA,
		.enablebits	= {{0x40,0x02,0x02}, {0x40,0x01,0x01}},
		.bootable	= ON_BOARD,
	},{	/* 2 */
		.name		= "AMD7411",
		.init_chipset	= init_chipset_amd74xx,
		.init_hwif	= init_hwif_amd74xx,
		.channels	= 2,
		.autodma	= AUTODMA,
		.enablebits	= {{0x40,0x02,0x02}, {0x40,0x01,0x01}},
		.bootable	= ON_BOARD,
	},{	/* 3 */
		.name		= "AMD7441",
		.init_chipset	= init_chipset_amd74xx,
		.init_hwif	= init_hwif_amd74xx,
		.channels	= 2,
		.autodma	= AUTODMA,
		.enablebits	= {{0x40,0x02,0x02}, {0x40,0x01,0x01}},
		.bootable	= ON_BOARD,
	},{	/* 4 */
		.name		= "AMD8111",
		.init_chipset	= init_chipset_amd74xx,
		.init_hwif	= init_hwif_amd74xx,
		.autodma	= AUTODMA,
		.channels	= 2,
		.enablebits	= {{0x40,0x02,0x02}, {0x40,0x01,0x01}},
		.bootable	= ON_BOARD,
	},
	{	/* 5 */
		.name		= "NFORCE",
		.init_chipset	= init_chipset_amd74xx,
		.init_hwif	= init_hwif_amd74xx,
		.channels	= 2,
		.autodma	= AUTODMA,
		.enablebits	= {{0x50,0x02,0x02}, {0x50,0x01,0x01}},
		.bootable	= ON_BOARD,
	},
	{	/* 6 */
		.name		= "NFORCE2",
		.init_chipset	= init_chipset_amd74xx,
		.init_hwif	= init_hwif_amd74xx,
		.channels	= 2,
		.autodma	= AUTODMA,
		.enablebits	= {{0x50,0x02,0x02}, {0x50,0x01,0x01}},
		.bootable	= ON_BOARD,
	},
	{	/* 7 */
		.name		= "NFORCE2S",
		.init_chipset	= init_chipset_amd74xx,
		.init_hwif	= init_hwif_amd74xx,
		.channels	= 2,
		.autodma	= AUTODMA,
		.enablebits	= {{0x50,0x02,0x02}, {0x50,0x01,0x01}},
		.bootable	= ON_BOARD,
	},
	{	/* 8 */
		.name		= "NFORCE2S-SATA",
		.init_chipset	= init_chipset_amd74xx,
		.init_hwif	= init_hwif_amd74xx,
		.channels	= 2,
		.autodma	= AUTODMA,
		.enablebits	= {{0x50,0x02,0x02}, {0x50,0x01,0x01}},
		.bootable	= ON_BOARD,
	},
	{	/* 9 */
		.name		= "NFORCE3",
		.init_chipset	= init_chipset_amd74xx,
		.init_hwif	= init_hwif_amd74xx,
		.channels	= 2,
		.autodma	= AUTODMA,
		.enablebits	= {{0x50,0x02,0x02}, {0x50,0x01,0x01}},
		.bootable	= ON_BOARD,
	},
	{	/* 10 */
		.name		= "NFORCE3S",
		.init_chipset	= init_chipset_amd74xx,
		.init_hwif	= init_hwif_amd74xx,
		.channels	= 2,
		.autodma	= AUTODMA,
		.enablebits	= {{0x50,0x02,0x02}, {0x50,0x01,0x01}},
		.bootable	= ON_BOARD,
	},
	{	/* 11 */
		.name		= "NFORCE3S-SATA",
		.init_chipset	= init_chipset_amd74xx,
		.init_hwif	= init_hwif_amd74xx,
		.channels	= 2,
		.autodma	= AUTODMA,
		.enablebits	= {{0x50,0x02,0x02}, {0x50,0x01,0x01}},
		.bootable	= ON_BOARD,
	},
	{	/* 12 */
		.name		= "NFORCE3S-SATA2",
		.init_chipset	= init_chipset_amd74xx,
		.init_hwif	= init_hwif_amd74xx,
		.channels	= 2,
		.autodma	= AUTODMA,
		.enablebits	= {{0x50,0x02,0x02}, {0x50,0x01,0x01}},
		.bootable	= ON_BOARD,
	}
};

#endif /* AMD74XX_H */
