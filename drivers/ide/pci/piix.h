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

#define DECLARE_PIIX_DEV(pci_id, name_str) \
	{						\
		.vendor		= PCI_VENDOR_ID_INTEL,	\
		.device		= pci_id,		\
		.name		= name_str,		\
		.init_setup	= init_setup_piix,	\
		.init_chipset	= init_chipset_piix,	\
		.init_iops	= NULL,			\
		.init_hwif	= init_hwif_piix,	\
		.channels	= 2,			\
		.autodma	= AUTODMA,		\
		.enablebits	= {{0x41,0x80,0x80}, {0x43,0x80,0x80}}, \
		.bootable	= ON_BOARD,		\
		.extra		= 0,			\
	}

/*
 *	Table of the various PIIX capability blocks
 *
 */
 
static ide_pci_device_t piix_pci_info[] __devinitdata = {
	/* 0  */ DECLARE_PIIX_DEV(PCI_DEVICE_ID_INTEL_82371FB_0,  "PIIXa"),
	/* 1  */ DECLARE_PIIX_DEV(PCI_DEVICE_ID_INTEL_82371FB_1,  "PIIXb"),

	{	/* 2 */
		.vendor		= PCI_VENDOR_ID_INTEL,
		.device		= PCI_DEVICE_ID_INTEL_82371MX,
		.name		= "MPIIX",
		.init_setup	= init_setup_piix,
		.init_chipset	= NULL,
		.init_iops	= NULL,
		.init_hwif	= init_hwif_piix,
		.channels	= 2,
		.autodma	= NODMA,
		.enablebits	= {{0x6D,0x80,0x80}, {0x6F,0x80,0x80}},
		.bootable	= ON_BOARD,
		.extra		= 0,
	},

	/* 3  */ DECLARE_PIIX_DEV(PCI_DEVICE_ID_INTEL_82371SB_1,  "PIIX3"),
	/* 4  */ DECLARE_PIIX_DEV(PCI_DEVICE_ID_INTEL_82371AB,    "PIIX4"),
	/* 5  */ DECLARE_PIIX_DEV(PCI_DEVICE_ID_INTEL_82801AB_1,  "ICH0"),
	/* 6  */ DECLARE_PIIX_DEV(PCI_DEVICE_ID_INTEL_82443MX_1,  "PIIX4"),
	/* 7  */ DECLARE_PIIX_DEV(PCI_DEVICE_ID_INTEL_82801AA_1,  "ICH"),
	/* 8  */ DECLARE_PIIX_DEV(PCI_DEVICE_ID_INTEL_82372FB_1,  "PIIX4"),
	/* 9  */ DECLARE_PIIX_DEV(PCI_DEVICE_ID_INTEL_82451NX,    "PIIX4"),
	/* 10 */ DECLARE_PIIX_DEV(PCI_DEVICE_ID_INTEL_82801BA_9,  "ICH2"),
	/* 11 */ DECLARE_PIIX_DEV(PCI_DEVICE_ID_INTEL_82801BA_8,  "ICH2M"),
	/* 12 */ DECLARE_PIIX_DEV(PCI_DEVICE_ID_INTEL_82801CA_10, "ICH3M"),
	/* 13 */ DECLARE_PIIX_DEV(PCI_DEVICE_ID_INTEL_82801CA_11, "ICH3"),
	/* 14 */ DECLARE_PIIX_DEV(PCI_DEVICE_ID_INTEL_82801DB_11, "ICH4"),
	/* 15 */ DECLARE_PIIX_DEV(PCI_DEVICE_ID_INTEL_82801EB_11, "ICH5"),
	/* 16 */ DECLARE_PIIX_DEV(PCI_DEVICE_ID_INTEL_82801E_11,  "C-ICH"),
	/* 17 */ DECLARE_PIIX_DEV(PCI_DEVICE_ID_INTEL_82801DB_10, "ICH4"),
	/* 18 */ DECLARE_PIIX_DEV(PCI_DEVICE_ID_INTEL_82801EB_1,  "ICH5-SATA"),
	/* 19 */ DECLARE_PIIX_DEV(PCI_DEVICE_ID_INTEL_ESB_2,      "ICH5"),
	/* 20 */ DECLARE_PIIX_DEV(PCI_DEVICE_ID_INTEL_ICH6_19,     "ICH6"),
	{
		.vendor		= 0,
		.device		= 0,
		.channels	= 0,
		.init_setup	= NULL,
		.bootable	= EOL,
	}
};

#endif /* PIIX_H */
