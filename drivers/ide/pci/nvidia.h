#ifndef NFORCE_H
#define NFORCE_H

#include <linux/config.h>
#include <linux/pci.h>
#include <linux/ide.h>

#define DISPLAY_NFORCE_TIMINGS

#if defined(DISPLAY_NFORCE_TIMINGS) && defined(CONFIG_PROC_FS)
#include <linux/stat.h>
#include <linux/proc_fs.h>

static u8 nforce_proc;

static int nforce_get_info(char *, char **, off_t, int);

static ide_pci_host_proc_t nforce_procs[] __initdata = {
	{
		name:		"nforce",
		set:		1,
		get_info:	nforce_get_info,
		parent:		NULL,
	},
};
#endif  /* defined(DISPLAY_NFORCE_TIMINGS) && defined(CONFIG_PROC_FS) */

static unsigned int init_chipset_nforce(struct pci_dev *, const char *);
static void init_hwif_nforce(ide_hwif_t *);
static void init_dma_nforce(ide_hwif_t *, unsigned long);

static ide_pci_device_t nvidia_chipsets[] __initdata = {
	{
		vendor:		PCI_VENDOR_ID_NVIDIA,
		device:		PCI_DEVICE_ID_NVIDIA_NFORCE_IDE,
		name:		"NFORCE",
		init_chipset:	init_chipset_nforce,
		init_iops:	NULL,
		init_hwif:	init_hwif_nforce,
		init_dma:	init_dma_nforce,
		channels:	2,
		autodma:	AUTODMA,
		enablebits:	{{0x50,0x01,0x01}, {0x50,0x02,0x02}},
		bootable:	ON_BOARD,
		extra:		0,
	}
};


#endif /* NFORCE_H */
