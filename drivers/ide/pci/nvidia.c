/*
 * linux/drivers/ide/nvidia.c		Version 0.01
 *
 * Copyright (C) 2002-2002		Andre Hedrick <andre@linux-ide.org>
 * May be copied or modified under the terms of the GNU General Public License
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>

#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/ide.h>

#include <asm/io.h>
#include <asm/irq.h>

#include "ide_modes.h"
#include "nvidia.h"

#if defined(DISPLAY_NFORCE_TIMINGS) && defined(CONFIG_PROC_FS)
#include <linux/stat.h>
#include <linux/proc_fs.h>

static u8 nforce_proc = 0;
static struct pci_dev *bmide_dev;

static int nforce_get_info (char *buffer, char **addr, off_t offset, int count)
{
	char *p = buffer;
	u32 bibma = pci_resource_start(bmide_dev, 4);
	u8 c0 = 0, c1 = 0;

	/*
	 * at that point bibma+0x2 et bibma+0xa are byte registers
	 * to investigate:
	 */
	c0 = inb((unsigned short)bibma + 0x02);
	c1 = inb((unsigned short)bibma + 0x0a);

	p += sprintf(p, "\n                                "
			"nVidia %04X Chipset.\n", bmide_dev->device);
	p += sprintf(p, "--------------- Primary Channel "
			"---------------- Secondary Channel "
			"-------------\n");
	p += sprintf(p, "                %sabled "
			"                        %sabled\n",
			(c0&0x80) ? "dis" : " en",
			(c1&0x80) ? "dis" : " en");
	p += sprintf(p, "--------------- drive0 --------- drive1 "
			"-------- drive0 ---------- drive1 ------\n");
	p += sprintf(p, "DMA enabled:    %s              %s "
			"            %s               %s\n",
			(c0&0x20) ? "yes" : "no ", (c0&0x40) ? "yes" : "no ",
			(c1&0x20) ? "yes" : "no ", (c1&0x40) ? "yes" : "no " );
	p += sprintf(p, "UDMA\n");
	p += sprintf(p, "DMA\n");
	p += sprintf(p, "PIO\n");

	return p-buffer;	/* => must be less than 4k! */
}
#endif  /* defined(DISPLAY_NFORCE_TIMINGS) && defined(CONFIG_PROC_FS) */

static u8 nforce_ratemask (ide_drive_t *drive)
{
	u8 mode;

        switch(HWIF(drive)->pci_dev->device) {
		case PCI_DEVICE_ID_NVIDIA_NFORCE_IDE:
			mode = 3;
			break;
		default:
			return 0;
	}
	if (!eighty_ninty_three(drive))
		mode = min(mode, (u8)1);
	return mode;
}

/*
 * Here is where all the hard work goes to program the chipset.
 */
static int nforce_tune_chipset (ide_drive_t *drive, u8 xferspeed)
{
	static const u8 drive_pci[] = { 0x63, 0x62, 0x61, 0x60 };
	static const u8 drive_pci2[] = { 0x5b, 0x5a, 0x59, 0x58 };

	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;
	u8 speed	= ide_rate_filter(nforce_ratemask(drive), xferspeed);
	u8 ultra_timing	= 0, dma_pio_timing = 0, pio_timing = 0;

	pci_read_config_byte(dev, drive_pci[drive->dn], &ultra_timing);
	pci_read_config_byte(dev, drive_pci2[drive->dn], &dma_pio_timing);
	pci_read_config_byte(dev, 0x5c, &pio_timing);

	ultra_timing	&= ~0xC7;
	dma_pio_timing	&= ~0xFF;
	pio_timing	&= ~(0x03 << drive->dn);

	switch(speed) {
#ifdef CONFIG_BLK_DEV_IDEDMA
		case XFER_UDMA_7:
		case XFER_UDMA_6:
			speed = XFER_UDMA_5;
		case XFER_UDMA_5:
			ultra_timing |= 0x46;
			dma_pio_timing |= 0x20;
			break;
		case XFER_UDMA_4:
			ultra_timing |= 0x45;
			dma_pio_timing |= 0x20;
			break;
		case XFER_UDMA_3:
			ultra_timing |= 0x44;
			dma_pio_timing |= 0x20;
			break;
		case XFER_UDMA_2:
			ultra_timing |= 0x40;
			dma_pio_timing |= 0x20;
			break;
		case XFER_UDMA_1:
			ultra_timing |= 0x41;
			dma_pio_timing |= 0x20;
			break;
		case XFER_UDMA_0:
			ultra_timing |= 0x42;
			dma_pio_timing |= 0x20;
			break;
		case XFER_MW_DMA_2:
			dma_pio_timing |= 0x20;
			break;
		case XFER_MW_DMA_1:
			dma_pio_timing |= 0x21;
			break;
		case XFER_MW_DMA_0:
			dma_pio_timing |= 0x77;
			break;
		case XFER_SW_DMA_2:
			dma_pio_timing |= 0x42;
			break;
		case XFER_SW_DMA_1:
			dma_pio_timing |= 0x65;
			break;
		case XFER_SW_DMA_0:
			dma_pio_timing |= 0xA8;
			break;
#endif /* CONFIG_BLK_DEV_IDEDMA */
		case XFER_PIO_4:
			dma_pio_timing |= 0x20;
			break;
		case XFER_PIO_3:
			dma_pio_timing |= 0x22;
			break;
		case XFER_PIO_2:
			dma_pio_timing |= 0x42;
			break;
		case XFER_PIO_1:
			dma_pio_timing |= 0x65;
			break;
		case XFER_PIO_0:
		default:
			dma_pio_timing |= 0xA8;
			break;
        }

	pio_timing |= (0x03 << drive->dn);

#ifdef CONFIG_BLK_DEV_IDEDMA
	pci_write_config_byte(dev, drive_pci[drive->dn], ultra_timing);
#endif /* CONFIG_BLK_DEV_IDEDMA */
	pci_write_config_byte(dev, drive_pci2[drive->dn], dma_pio_timing);
	pci_write_config_byte(dev, 0x5c, pio_timing);

	return (ide_config_drive_speed(drive, speed));
}

static void nforce_tune_drive (ide_drive_t *drive, u8 pio)
{
	pio = ide_get_best_pio_mode(drive, pio, 5, NULL);
	(void) nforce_tune_chipset(drive, (XFER_PIO_0 + pio));
}

#ifdef CONFIG_BLK_DEV_IDEDMA
/*
 * This allows the configuration of ide_pci chipset registers
 * for cards that learn about the drive's UDMA, DMA, PIO capabilities
 * after the drive is reported by the OS.
 */
static int config_chipset_for_dma (ide_drive_t *drive)
{
	u8 speed = ide_dma_speed(drive, nforce_ratemask(drive));

	if (!(speed))
		return 0;

	(void) nforce_tune_chipset(drive, speed);
	return ide_dma_enable(drive);
}

static int nforce_config_drive_xfer_rate (ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct hd_driveid *id	= drive->id;

	drive->init_speed = 0;

	if (id && (id->capability & 1) && drive->autodma) {
		/* Consult the list of known "bad" drives */
		if (hwif->ide_dma_bad_drive(drive))
			goto fast_ata_pio;
		if (id->field_valid & 4) {
			if (id->dma_ultra & hwif->ultra_mask) {
				/* Force if Capable UltraDMA */
				int dma = config_chipset_for_dma(drive);
				if ((id->field_valid & 2) && !dma)
					goto try_dma_modes;
			}
		} else if (id->field_valid & 2) {
try_dma_modes:
			if ((id->dma_mword & hwif->mwdma_mask) ||
			    (id->dma_1word & hwif->swdma_mask)) {
				/* Force if Capable regular DMA modes */
				if (!config_chipset_for_dma(drive))
					goto no_dma_set;
			}

		} else if (hwif->ide_dma_good_drive(drive) &&
			   (id->eide_dma_time < 150)) {
			/* Consult the list of known "good" drives */
			if (!config_chipset_for_dma(drive))
				goto no_dma_set;
		} else {
			goto fast_ata_pio;
		}
	} else if ((id->capability & 8) || (id->field_valid & 2)) {
fast_ata_pio:
no_dma_set:
		nforce_tune_drive(drive, 5);
		return hwif->ide_dma_off_quietly(drive);
	}
	return hwif->ide_dma_on(drive);
}
#endif /* CONFIG_BLK_DEV_IDEDMA */

unsigned int __init init_chipset_nforce (struct pci_dev *dev, const char *name)
{
#if defined(DISPLAY_NFORCE_TIMINGS) && defined(CONFIG_PROC_FS)
	if (!nforce_proc) {
		nforce_proc = 1;
		bmide_dev = dev;
		ide_pci_register_host_proc(&nforce_procs[0]);
	}
#endif /* DISPLAY_NFORCE_TIMINGS && CONFIG_PROC_FS */
	return 0;
}

static unsigned int __init ata66_nforce (ide_hwif_t *hwif)
{
	struct pci_dev *dev	= hwif->pci_dev;
	u8 cable_80_pin[2]	= { 0, 0 };
	u8 ata66		= 0;
	u8 tmpbyte;

	/*
	 * Ultra66 cable detection (from Host View)
	 * 7411, 7441, 0x52, bit0: primary, bit2: secondary 80 pin
	 */
	pci_read_config_byte(dev, 0x52, &tmpbyte);

	/*
	 * 0x52, bit0 is 1 => primary channel
	 * has 80-pin (from host view)
	 */
	if (tmpbyte & 0x01) cable_80_pin[0] = 1;

	/*
	 * 0x52, bit2 is 1 => secondary channel
	 * has 80-pin (from host view)
	 */
	if (tmpbyte & 0x04) cable_80_pin[1] = 1;

	switch(dev->device) {
		case PCI_DEVICE_ID_NVIDIA_NFORCE_IDE:
			ata66 = (hwif->channel) ?
				cable_80_pin[1] :
				cable_80_pin[0];
		default:
			break;
	}
	return (unsigned int) ata66;
}

static void __init init_hwif_nforce (ide_hwif_t *hwif)
{
	hwif->tuneproc = &nforce_tune_drive;
	hwif->speedproc = &nforce_tune_chipset;
	hwif->autodma = 0;

	if (!hwif->dma_base) {
		hwif->drives[0].autotune = 1;
		hwif->drives[1].autotune = 1;
		return;
	}

	hwif->atapi_dma = 1;
	hwif->ultra_mask = 0x3f;
	hwif->mwdma_mask = 0x07;
	hwif->swdma_mask = 0x07;

#ifdef CONFIG_BLK_DEV_IDEDMA
	if (!(hwif->udma_four))
		hwif->udma_four = ata66_nforce(hwif);
	hwif->ide_dma_check = &nforce_config_drive_xfer_rate;
	if (!noautodma)
		hwif->autodma = 1;
	hwif->drives[0].autodma = hwif->autodma;
	hwif->drives[1].autodma = hwif->autodma;
#endif /* CONFIG_BLK_DEV_IDEDMA */
}

/* FIXME - not needed */
static void __init init_dma_nforce (ide_hwif_t *hwif, unsigned long dmabase)
{
	ide_setup_dma(hwif, dmabase, 8);
}

extern void ide_setup_pci_device(struct pci_dev *, ide_pci_device_t *);

static int __devinit nforce_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	ide_pci_device_t *d = &nvidia_chipsets[id->driver_data];
	if (dev->device != d->device)
		BUG();
	ide_setup_pci_device(dev, d);
	return 0;
}

static struct pci_device_id nforce_pci_tbl[] __devinitdata = {
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE_IDE, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ 0, },
};

static struct pci_driver driver = {
	name:		"nForce IDE",
	id_table:	nforce_pci_tbl,
	probe:		nforce_init_one,
};

static int nforce_ide_init(void)
{
	return ide_pci_register_driver(&driver);
}

static void nforce_ide_exit(void)
{
	ide_pci_unregister_driver(&driver);
}

module_init(nforce_ide_init);
module_exit(nforce_ide_exit);

MODULE_AUTHOR("Andre Hedrick");
MODULE_DESCRIPTION("PCI driver module for nVidia nForce IDE");
MODULE_LICENSE("GPL");

EXPORT_NO_SYMBOLS;
