#ifndef PIIX_H
#define PIIX_H

#include <linux/config.h>
#include <linux/pci.h>
#include <linux/ide.h>

#define PIIX_DEBUG_DRIVE_INFO		0

#define DISPLAY_PIIX_TIMINGS

static void init_setup_piix(struct pci_dev *, ide_pci_device_t *);
static unsigned int __devinit init_chipset_piix(struct pci_dev *, const char *);
static void init_hwif_piix(ide_hwif_t *);

#define DECLARE_PIIX_DEV(name_str) \
	{						\
		.name		= name_str,		\
		.init_setup	= init_setup_piix,	\
		.init_chipset	= init_chipset_piix,	\
		.init_hwif	= init_hwif_piix,	\
		.channels	= 2,			\
		.autodma	= AUTODMA,		\
		.enablebits	= {{0x41,0x80,0x80}, {0x43,0x80,0x80}}, \
		.bootable	= ON_BOARD,		\
	}

/*
 *	Table of the various PIIX capability blocks
 *
 */
 
static ide_pci_device_t piix_pci_info[] __devinitdata = {
	/*  0 */ DECLARE_PIIX_DEV("PIIXa"),
	/*  1 */ DECLARE_PIIX_DEV("PIIXb"),

	{	/* 2 */
		.name		= "MPIIX",
		.init_setup	= init_setup_piix,
		.init_hwif	= init_hwif_piix,
		.channels	= 2,
		.autodma	= NODMA,
		.enablebits	= {{0x6D,0x80,0x80}, {0x6F,0x80,0x80}},
		.bootable	= ON_BOARD,
	},

	/*  3 */ DECLARE_PIIX_DEV("PIIX3"),
	/*  4 */ DECLARE_PIIX_DEV("PIIX4"),
	/*  5 */ DECLARE_PIIX_DEV("ICH0"),
	/*  6 */ DECLARE_PIIX_DEV("PIIX4"),
	/*  7 */ DECLARE_PIIX_DEV("ICH"),
	/*  8 */ DECLARE_PIIX_DEV("PIIX4"),
	/*  9 */ DECLARE_PIIX_DEV("PIIX4"),
	/* 10 */ DECLARE_PIIX_DEV("ICH2"),
	/* 11 */ DECLARE_PIIX_DEV("ICH2M"),
	/* 12 */ DECLARE_PIIX_DEV("ICH3M"),
	/* 13 */ DECLARE_PIIX_DEV("ICH3"),
	/* 14 */ DECLARE_PIIX_DEV("ICH4"),
	/* 15 */ DECLARE_PIIX_DEV("ICH5"),
	/* 16 */ DECLARE_PIIX_DEV("C-ICH"),
	/* 17 */ DECLARE_PIIX_DEV("ICH4"),
	/* 18 */ DECLARE_PIIX_DEV("ICH5-SATA"),
	/* 19 */ DECLARE_PIIX_DEV("ICH5"),
	/* 20 */ DECLARE_PIIX_DEV("ICH6")
};

#endif /* PIIX_H */
