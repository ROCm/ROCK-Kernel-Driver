#ifndef CS5520_H
#define CS5520_H

#include <linux/config.h>
#include <linux/pci.h>
#include <linux/ide.h>

#define DISPLAY_CS5520_TIMINGS

#if defined(DISPLAY_CS5520_TIMINGS) && defined(CONFIG_PROC_FS)
#include <linux/stat.h>
#include <linux/proc_fs.h>

static u8 cs5520_proc;

static int cs5520_get_info(char *, char **, off_t, int);

static ide_pci_host_proc_t cs5520_procs[] __initdata = {
	{
		.name		= "cs5520",
		.set		= 1,
		.get_info	= cs5520_get_info,
		.parent		= NULL,
	},
};
#endif  /* defined(DISPLAY_CS5520_TIMINGS) && defined(CONFIG_PROC_FS) */

static unsigned int init_chipset_cs5520(struct pci_dev *, const char *);
static void init_hwif_cs5520(ide_hwif_t *);
static void cs5520_init_setup_dma(struct pci_dev *dev, struct ide_pci_device_s *d, ide_hwif_t *hwif);

static ide_pci_device_t cyrix_chipsets[] __devinitdata = {
	{
		.vendor		= PCI_VENDOR_ID_CYRIX,
		.device		= PCI_DEVICE_ID_CYRIX_5510,
		.name		= "Cyrix 5510",
		.init_chipset	= init_chipset_cs5520,
		.init_setup_dma = cs5520_init_setup_dma,
		.init_iops	= NULL,
		.init_hwif	= init_hwif_cs5520,
		.isa_ports	= 1,
		.channels	= 2,
		.autodma	= AUTODMA,
		.bootable	= ON_BOARD,
		.extra		= 0,
	},
	{
		.vendor		= PCI_VENDOR_ID_CYRIX,
		.device		= PCI_DEVICE_ID_CYRIX_5520,
		.name		= "Cyrix 5520",
		.init_chipset	= init_chipset_cs5520,
		.init_setup_dma = cs5520_init_setup_dma,
		.init_iops	= NULL,
		.init_hwif	= init_hwif_cs5520,
		.isa_ports	= 1,
		.channels	= 2,
		.autodma	= AUTODMA,
		.bootable	= ON_BOARD,
		.extra		= 0,
	}
};


#endif /* CS5520_H */


