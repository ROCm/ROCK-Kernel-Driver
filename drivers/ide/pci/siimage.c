/*
 * linux/drivers/ide/pci/siimage.c		Version 1.02	Jan 30, 2003
 *
 * Copyright (C) 2001-2002	Andre Hedrick <andre@linux-ide.org>
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/hdreg.h>
#include <linux/ide.h>
#include <linux/init.h>

#include <asm/io.h>

#include "ide_modes.h"
#include "siimage.h"

#if defined(DISPLAY_SIIMAGE_TIMINGS) && defined(CONFIG_PROC_FS)
#include <linux/stat.h>
#include <linux/proc_fs.h>

static u8 siimage_proc = 0;
#define SIIMAGE_MAX_DEVS		5
static struct pci_dev *siimage_devs[SIIMAGE_MAX_DEVS];
static int n_siimage_devs;

static char * print_siimage_get_info (char *buf, struct pci_dev *dev, int index)
{
	char *p		= buf;
	u8 mmio		= (pci_get_drvdata(dev) != NULL) ? 1 : 0;
	unsigned long bmdma	= (mmio) ? ((unsigned long) pci_get_drvdata(dev)) :
				    (pci_resource_start(dev, 4));

	p += sprintf(p, "\nController: %d\n", index);
	p += sprintf(p, "SiI%x Chipset.\n", dev->device);
	if (mmio)
		p += sprintf(p, "MMIO Base 0x%lx\n", bmdma);
	p += sprintf(p, "%s-DMA Base 0x%lx\n", (mmio)?"MMIO":"BM", bmdma);
	p += sprintf(p, "%s-DMA Base 0x%lx\n", (mmio)?"MMIO":"BM", bmdma+8);

	p += sprintf(p, "--------------- Primary Channel "
			"---------------- Secondary Channel "
			"-------------\n");
	p += sprintf(p, "--------------- drive0 --------- drive1 "
			"-------- drive0 ---------- drive1 ------\n");
	p += sprintf(p, "PIO Mode:       %s                %s"
			"               %s                 %s\n",
			"?", "?", "?", "?");
	return (char *)p;
}

static int siimage_get_info (char *buffer, char **addr, off_t offset, int count)
{
	char *p = buffer;
	int len;
	u16 i;

	p += sprintf(p, "\n");
	for (i = 0; i < n_siimage_devs; i++) {
		struct pci_dev *dev	= siimage_devs[i];
		p = print_siimage_get_info(p, dev, i);
	}
	/* p - buffer must be less than 4k! */
	len = (p - buffer) - offset;
	*addr = buffer + offset;
	
	return len > count ? count : len;
}

#endif	/* defined(DISPLAY_SIIMAGE_TIMINGS) && defined(CONFIG_PROC_FS) */

static byte siimage_ratemask (ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	u8 mode	= 0, scsc = 0;

	if (hwif->mmio)
		scsc = hwif->INB(HWIFADDR(0x4A));
	else
		pci_read_config_byte(hwif->pci_dev, 0x8A, &scsc);

	switch(hwif->pci_dev->device) {
		case PCI_DEVICE_ID_SII_3112:
			return 4;
		case PCI_DEVICE_ID_SII_680:
			if ((scsc & 0x10) == 0x10)	/* 133 */
				mode = 4;
			else if ((scsc & 0x30) == 0x00)	/* 100 */
				mode = 3;
			else if ((scsc & 0x20) == 0x20)	/* 66 eek */
				BUG();	// mode = 2;
			break;
		default:	return 0;
	}
	if (!eighty_ninty_three(drive))
		mode = min(mode, (u8)1);
	return mode;
}

static byte siimage_taskfile_timing (ide_hwif_t *hwif)
{
	u16 timing	= 0x328a;

	if (hwif->mmio)
		timing = hwif->INW(SELADDR(2));
	else
		pci_read_config_word(hwif->pci_dev, SELREG(2), &timing);

	switch (timing) {
		case 0x10c1:	return 4;
		case 0x10c3:	return 3;
		case 0x1281:	return 2;
		case 0x2283:	return 1;
		case 0x328a:
		default:	return 0;
	}
}

static void siimage_tuneproc (ide_drive_t *drive, byte mode_wanted)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;
	u16 speedt		= 0;
	u8 unit			= drive->select.b.unit;

	if (hwif->mmio)
		speedt = hwif->INW(SELADDR(0x04|(unit<<unit)));
	else
		pci_read_config_word(dev, SELADDR(0x04|(unit<<unit)), &speedt);

	/* cheat for now and use the docs */
//	switch(siimage_taskfile_timing(hwif)) {
	switch(mode_wanted) {
		case 4:		speedt = 0x10c1; break;
		case 3:		speedt = 0x10C3; break;
		case 2:		speedt = 0x1104; break;
		case 1:		speedt = 0x2283; break;
		case 0:
		default:	speedt = 0x328A; break;
	}
	if (hwif->mmio)
		hwif->OUTW(speedt, SELADDR(0x04|(unit<<unit)));
	else
		pci_write_config_word(dev, SELADDR(0x04|(unit<<unit)), speedt);
}

static void config_siimage_chipset_for_pio (ide_drive_t *drive, byte set_speed)
{
	u8 channel_timings	= siimage_taskfile_timing(HWIF(drive));
	u8 speed = 0, set_pio	= ide_get_best_pio_mode(drive, 4, 5, NULL);

	/* WARNING PIO timing mess is going to happen b/w devices, argh */
	if ((channel_timings != set_pio) && (set_pio > channel_timings))
		set_pio = channel_timings;

	siimage_tuneproc(drive, set_pio);
	speed = XFER_PIO_0 + set_pio;
	if (set_speed)
		(void) ide_config_drive_speed(drive, speed);
}

static void config_chipset_for_pio (ide_drive_t *drive, byte set_speed)
{
	config_siimage_chipset_for_pio(drive, set_speed);
}

static int siimage_tune_chipset (ide_drive_t *drive, byte xferspeed)
{
	u8 ultra6[]		= { 0x0F, 0x0B, 0x07, 0x05, 0x03, 0x02, 0x01 };
	u8 ultra5[]		= { 0x0C, 0x07, 0x05, 0x04, 0x02, 0x01 };
	u16 dma[]		= { 0x2208, 0x10C2, 0x10C1 };

	ide_hwif_t *hwif	= HWIF(drive);
	u16 ultra = 0, multi	= 0;
	u8 mode = 0, unit	= drive->select.b.unit;
	u8 speed	= ide_rate_filter(siimage_ratemask(drive), xferspeed);
	u8 scsc = 0, addr_mask	= ((hwif->channel) ?
				    ((hwif->mmio) ? 0xF4 : 0x84) :
				    ((hwif->mmio) ? 0xB4 : 0x80));

	if (hwif->mmio) {
		scsc = hwif->INB(HWIFADDR(0x4A));
		mode = hwif->INB(HWIFADDR(addr_mask));
		multi = hwif->INW(SELADDR(0x08|(unit<<unit)));
		ultra = hwif->INW(SELADDR(0x0C|(unit<<unit)));
	} else {
		pci_read_config_byte(hwif->pci_dev, HWIFADDR(0x8A), &scsc);
		pci_read_config_byte(hwif->pci_dev, addr_mask, &mode);
		pci_read_config_word(hwif->pci_dev,
				SELREG(0x08|(unit<<unit)), &multi);
		pci_read_config_word(hwif->pci_dev,
				SELREG(0x0C|(unit<<unit)), &ultra);
	}

	mode &= ~((unit) ? 0x30 : 0x03);
	ultra &= ~0x3F;
	scsc = ((scsc & 0x30) == 0x00) ? 0 : 1;

	scsc = (hwif->pci_dev->device == PCI_DEVICE_ID_SII_3112) ? 1 : scsc;

	switch(speed) {
		case XFER_PIO_4:
		case XFER_PIO_3:
		case XFER_PIO_2:
		case XFER_PIO_1:
		case XFER_PIO_0:
			siimage_tuneproc(drive, (speed - XFER_PIO_0));
			mode |= ((unit) ? 0x10 : 0x01);
			break;
		case XFER_MW_DMA_2:
		case XFER_MW_DMA_1:
		case XFER_MW_DMA_0:
			multi = dma[speed - XFER_MW_DMA_0];
			mode |= ((unit) ? 0x20 : 0x02);
			config_siimage_chipset_for_pio(drive, 0);
			break;
		case XFER_UDMA_6:
		case XFER_UDMA_5:
		case XFER_UDMA_4:
		case XFER_UDMA_3:
		case XFER_UDMA_2:
		case XFER_UDMA_1:
		case XFER_UDMA_0:
			multi = dma[2];
			ultra |= ((scsc) ? (ultra5[speed - XFER_UDMA_0]) :
					   (ultra6[speed - XFER_UDMA_0]));
			mode |= ((unit) ? 0x30 : 0x03);
			config_siimage_chipset_for_pio(drive, 0);
			break;
		default:
			return 1;
	}

	if (hwif->mmio) {
		hwif->OUTB(mode, HWIFADDR(addr_mask));
		hwif->OUTW(multi, SELADDR(0x08|(unit<<unit)));
		hwif->OUTW(ultra, SELADDR(0x0C|(unit<<unit)));
	} else {
		pci_write_config_byte(hwif->pci_dev, addr_mask, mode);
		pci_write_config_word(hwif->pci_dev,
				SELREG(0x08|(unit<<unit)), multi);
		pci_write_config_word(hwif->pci_dev,
				SELREG(0x0C|(unit<<unit)), ultra);
	}

	return (ide_config_drive_speed(drive, speed));
}

static int config_chipset_for_dma (ide_drive_t *drive)
{
	u8 speed	= ide_dma_speed(drive, siimage_ratemask(drive));

	config_chipset_for_pio(drive, !speed);

	if (!speed)
		return 0;

	if (ide_set_xfer_rate(drive, speed))
		return 0;

	if (!drive->init_speed)
		drive->init_speed = speed;

	return ide_dma_enable(drive);
}

static int siimage_config_drive_for_dma (ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct hd_driveid *id	= drive->id;

	if (id != NULL && (id->capability & 1) != 0 && drive->autodma) {
		if (!(hwif->atapi_dma))
			goto fast_ata_pio;
		/* Consult the list of known "bad" drives */
		if (hwif->ide_dma_bad_drive(drive))
			goto fast_ata_pio;

		if ((id->field_valid & 4) && siimage_ratemask(drive)) {
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
		config_chipset_for_pio(drive, 1);
		return hwif->ide_dma_off_quietly(drive);
	}
	return hwif->ide_dma_on(drive);
}

/* returns 1 if dma irq issued, 0 otherwise */
static int siimage_io_ide_dma_test_irq (ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	u8 dma_altstat		= 0;

	/* return 1 if INTR asserted */
	if ((hwif->INB(hwif->dma_status) & 4) == 4)
		return 1;

	/* return 1 if Device INTR asserted */
	pci_read_config_byte(hwif->pci_dev, SELREG(1), &dma_altstat);
	if (dma_altstat & 8)
		return 0;	//return 1;
	return 0;
}

static int siimage_mmio_ide_dma_count (ide_drive_t *drive)
{
#ifdef SIIMAGE_VIRTUAL_DMAPIO
	struct request *rq	= HWGROUP(drive)->rq;
	ide_hwif_t *hwif	= HWIF(drive);
	u32 count		= (rq->nr_sectors * SECTOR_SIZE);
	u32 rcount		= 0;

	hwif->OUTL(count, SELADDR(0x1C));
	rcount = hwif->INL(SELADDR(0x1C));

	printk("\n%s: count = %d, rcount = %d, nr_sectors = %lu\n",
		drive->name, count, rcount, rq->nr_sectors);

#endif /* SIIMAGE_VIRTUAL_DMAPIO */
	return __ide_dma_count(drive);
}

/* returns 1 if dma irq issued, 0 otherwise */
static int siimage_mmio_ide_dma_test_irq (ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);

	if (SATA_ERROR_REG) {
		u32 ext_stat = hwif->INL(HWIFADDR(0x10));
		u8 watchdog = 0;
		if (ext_stat & ((hwif->channel) ? 0x40 : 0x10)) {
			u32 sata_error = hwif->INL(SATA_ERROR_REG);
			hwif->OUTL(sata_error, SATA_ERROR_REG);
			watchdog = (sata_error & 0x00680000) ? 1 : 0;
#if 1
			printk(KERN_WARNING "%s: sata_error = 0x%08x, "
				"watchdog = %d, %s\n",
				drive->name, sata_error, watchdog,
				__FUNCTION__);
#endif

		} else {
			watchdog = (ext_stat & 0x8000) ? 1 : 0;
		}
		ext_stat >>= 16;

		if (!(ext_stat & 0x0404) && !watchdog)
			return 0;
	}

	/* return 1 if INTR asserted */
	if ((hwif->INB(hwif->dma_status) & 0x04) == 0x04)
		return 1;

	/* return 1 if Device INTR asserted */
	if ((hwif->INB(SELADDR(1)) & 8) == 8)
		return 0;	//return 1;

	return 0;
}

static int siimage_mmio_ide_dma_verbose (ide_drive_t *drive)
{
	int temp = __ide_dma_verbose(drive);
#if 0
	drive->using_dma = 0;
#endif
	return temp;
}

static int siimage_busproc (ide_drive_t * drive, int state)
{
	ide_hwif_t *hwif	= HWIF(drive);
	u32 stat_config		= 0;

	if (hwif->mmio) {
		stat_config = hwif->INL(SELADDR(0));
	} else
		pci_read_config_dword(hwif->pci_dev, SELREG(0), &stat_config);

	switch (state) {
		case BUSSTATE_ON:
			hwif->drives[0].failures = 0;
			hwif->drives[1].failures = 0;
			break;
		case BUSSTATE_OFF:
			hwif->drives[0].failures = hwif->drives[0].max_failures + 1;
			hwif->drives[1].failures = hwif->drives[1].max_failures + 1;
			break;
		case BUSSTATE_TRISTATE:
			hwif->drives[0].failures = hwif->drives[0].max_failures + 1;
			hwif->drives[1].failures = hwif->drives[1].max_failures + 1;
			break;
		default:
			return 0;
	}
	hwif->bus_state = state;
	return 0;
}

static int siimage_reset_poll (ide_drive_t *drive)
{
	if (SATA_STATUS_REG) {
		ide_hwif_t *hwif	= HWIF(drive);

		if ((hwif->INL(SATA_STATUS_REG) & 0x03) != 0x03) {
			printk(KERN_WARNING "%s: reset phy dead, status=0x%08x\n",
				hwif->name, hwif->INL(SATA_STATUS_REG));
			HWGROUP(drive)->poll_timeout = 0;
#if 0
			drive->failures++;
			return ide_stopped;
#else
			return ide_started;
#endif
			return 1;
		}
		return 0;
	} else {
		return 0;
	}
}

static void siimage_pre_reset (ide_drive_t *drive)
{
	if (drive->media != ide_disk)
		return;

	if (HWIF(drive)->pci_dev->device == PCI_DEVICE_ID_SII_3112) {
		drive->special.b.set_geometry = 0;
		drive->special.b.recalibrate = 0;
	}
}

static void siimage_reset (ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	u8 reset		= 0;

	if (hwif->mmio) {
		reset = hwif->INB(SELADDR(0));
		hwif->OUTB((reset|0x03), SELADDR(0));
		udelay(25);
		hwif->OUTB(reset, SELADDR(0));
		(void) hwif->INB(SELADDR(0));
	} else {
		pci_read_config_byte(hwif->pci_dev, SELREG(0), &reset);
		pci_write_config_byte(hwif->pci_dev, SELREG(0), reset|0x03);
		udelay(25);
		pci_write_config_byte(hwif->pci_dev, SELREG(0), reset);
		pci_read_config_byte(hwif->pci_dev, SELREG(0), &reset);
	}

	if (SATA_STATUS_REG) {
		u32 sata_stat = hwif->INL(SATA_STATUS_REG);
		printk(KERN_WARNING "%s: reset phy, status=0x%08x, %s\n",
			hwif->name, sata_stat, __FUNCTION__);
		if (!(sata_stat)) {
			printk(KERN_WARNING "%s: reset phy dead, status=0x%08x\n",
				hwif->name, sata_stat);
			drive->failures++;
		}
	}

}

static void proc_reports_siimage (struct pci_dev *dev, u8 clocking, const char *name)
{
	if (dev->device == PCI_DEVICE_ID_SII_3112)
		goto sata_skip;

	printk(KERN_INFO "%s: BASE CLOCK ", name);
	clocking &= ~0x0C;
	switch(clocking) {
		case 0x03: printk("DISABLED !\n"); break;
		case 0x02: printk("== 2X PCI \n"); break;
		case 0x01: printk("== 133 \n"); break;
		case 0x00: printk("== 100 \n"); break;
		default:
			BUG();
	}

sata_skip:

#if defined(DISPLAY_SIIMAGE_TIMINGS) && defined(CONFIG_PROC_FS)
	siimage_devs[n_siimage_devs++] = dev;

	if (!siimage_proc) {
		siimage_proc = 1;
		ide_pci_register_host_proc(&siimage_procs[0]);
	}
#endif /* DISPLAY_SIIMAGE_TIMINGS && CONFIG_PROC_FS */
}

static unsigned int setup_mmio_siimage (struct pci_dev *dev, const char *name)
{
	unsigned long bar5	= pci_resource_start(dev, 5);
	unsigned long end5	= pci_resource_end(dev, 5);
	u8 tmpbyte	= 0;
	unsigned long addr;
	void *ioaddr;

	ioaddr = ioremap_nocache(bar5, (end5 - bar5));

	if (ioaddr == NULL)
		return 0;

	pci_set_master(dev);
	pci_set_drvdata(dev, ioaddr);
	addr = (unsigned long) ioaddr;

	if (dev->device == PCI_DEVICE_ID_SII_3112) {
		writel(0, DEVADDR(0x148));
		writel(0, DEVADDR(0x1C8));
	}

	writeb(0, DEVADDR(0xB4));
	writeb(0, DEVADDR(0xF4));
	tmpbyte = readb(DEVADDR(0x4A));

	switch(tmpbyte) {
		case 0x01:
			writeb(tmpbyte|0x10, DEVADDR(0x4A));
			tmpbyte = readb(DEVADDR(0x4A));
		case 0x31:
			/* if clocking is disabled */
			/* 133 clock attempt to force it on */
			writeb(tmpbyte & ~0x20, DEVADDR(0x4A));
			tmpbyte = readb(DEVADDR(0x4A));
		case 0x11:
		case 0x21:
			break;
		default:
			tmpbyte &= ~0x30;
			tmpbyte |= 0x20;
			writeb(tmpbyte, DEVADDR(0x4A));
			break;
	}
	
	writeb(0x72, DEVADDR(0xA1));
	writew(0x328A, DEVADDR(0xA2));
	writel(0x62DD62DD, DEVADDR(0xA4));
	writel(0x43924392, DEVADDR(0xA8));
	writel(0x40094009, DEVADDR(0xAC));
	writeb(0x72, DEVADDR(0xE1));
	writew(0x328A, DEVADDR(0xE2));
	writel(0x62DD62DD, DEVADDR(0xE4));
	writel(0x43924392, DEVADDR(0xE8));
	writel(0x40094009, DEVADDR(0xEC));

	if (dev->device == PCI_DEVICE_ID_SII_3112) {
		writel(0xFFFF0000, DEVADDR(0x108));
		writel(0xFFFF0000, DEVADDR(0x188));
		writel(0x00680000, DEVADDR(0x148));
		writel(0x00680000, DEVADDR(0x1C8));
	}

	tmpbyte = readb(DEVADDR(0x4A));

	proc_reports_siimage(dev, (tmpbyte>>=4), name);
	return 1;
}

static unsigned int __init init_chipset_siimage (struct pci_dev *dev, const char *name)
{
	u32 class_rev	= 0;
	u8 tmpbyte	= 0;
	u8 BA5_EN	= 0;

        pci_read_config_dword(dev, PCI_CLASS_REVISION, &class_rev);
        class_rev &= 0xff;
	pci_write_config_byte(dev, PCI_CACHE_LINE_SIZE, (class_rev) ? 1 : 255);	

	pci_read_config_byte(dev, 0x8A, &BA5_EN);
	if ((BA5_EN & 0x01) || (pci_resource_start(dev, 5))) {
		if (setup_mmio_siimage(dev, name)) {
			return 0;
		}
	}

	pci_write_config_byte(dev, 0x80, 0x00);
	pci_write_config_byte(dev, 0x84, 0x00);
	pci_read_config_byte(dev, 0x8A, &tmpbyte);
	switch(tmpbyte) {
		case 0x00:
		case 0x01:
			/* 133 clock attempt to force it on */
			pci_write_config_byte(dev, 0x8A, tmpbyte|0x10);
			pci_read_config_byte(dev, 0x8A, &tmpbyte);
		case 0x30:
		case 0x31:
			/* if clocking is disabled */
			/* 133 clock attempt to force it on */
			pci_write_config_byte(dev, 0x8A, tmpbyte & ~0x20);
			pci_read_config_byte(dev, 0x8A, &tmpbyte);
		case 0x10:
		case 0x11:
		case 0x20:
		case 0x21:
			break;
		default:
			tmpbyte &= ~0x30;
			tmpbyte |= 0x20;
			pci_write_config_byte(dev, 0x8A, tmpbyte);
			break;
	}

	pci_read_config_byte(dev, 0x8A, &tmpbyte);
	pci_write_config_byte(dev, 0xA1, 0x72);
	pci_write_config_word(dev, 0xA2, 0x328A);
	pci_write_config_dword(dev, 0xA4, 0x62DD62DD);
	pci_write_config_dword(dev, 0xA8, 0x43924392);
	pci_write_config_dword(dev, 0xAC, 0x40094009);
	pci_write_config_byte(dev, 0xB1, 0x72);
	pci_write_config_word(dev, 0xB2, 0x328A);
	pci_write_config_dword(dev, 0xB4, 0x62DD62DD);
	pci_write_config_dword(dev, 0xB8, 0x43924392);
	pci_write_config_dword(dev, 0xBC, 0x40094009);

	pci_read_config_byte(dev, 0x8A, &tmpbyte);
	proc_reports_siimage(dev, (tmpbyte>>=4), name);
	return 0;
}

static void __init init_mmio_iops_siimage (ide_hwif_t *hwif)
{
	struct pci_dev *dev	= hwif->pci_dev;
	unsigned long addr	= (unsigned long) pci_get_drvdata(hwif->pci_dev);
	u8 ch			= hwif->channel;
//	u16 i			= 0;
	hw_regs_t hw;

	default_hwif_mmiops(hwif);
	memset(&hw, 0, sizeof(hw_regs_t));

#if 1
#ifdef SIIMAGE_BUFFERED_TASKFILE
	hw.io_ports[IDE_DATA_OFFSET]	= DEVADDR((ch) ? 0xD0 : 0x90);
	hw.io_ports[IDE_ERROR_OFFSET]	= DEVADDR((ch) ? 0xD1 : 0x91);
	hw.io_ports[IDE_NSECTOR_OFFSET]	= DEVADDR((ch) ? 0xD2 : 0x92);
	hw.io_ports[IDE_SECTOR_OFFSET]	= DEVADDR((ch) ? 0xD3 : 0x93);
	hw.io_ports[IDE_LCYL_OFFSET]	= DEVADDR((ch) ? 0xD4 : 0x94);
	hw.io_ports[IDE_HCYL_OFFSET]	= DEVADDR((ch) ? 0xD5 : 0x95);
	hw.io_ports[IDE_SELECT_OFFSET]	= DEVADDR((ch) ? 0xD6 : 0x96);
	hw.io_ports[IDE_STATUS_OFFSET]	= DEVADDR((ch) ? 0xD7 : 0x97);
	hw.io_ports[IDE_CONTROL_OFFSET]	= DEVADDR((ch) ? 0xDA : 0x9A);
#else /* ! SIIMAGE_BUFFERED_TASKFILE */
	hw.io_ports[IDE_DATA_OFFSET]	= DEVADDR((ch) ? 0xC0 : 0x80);
	hw.io_ports[IDE_ERROR_OFFSET]	= DEVADDR((ch) ? 0xC1 : 0x81);
	hw.io_ports[IDE_NSECTOR_OFFSET]	= DEVADDR((ch) ? 0xC2 : 0x82);
	hw.io_ports[IDE_SECTOR_OFFSET]	= DEVADDR((ch) ? 0xC3 : 0x83);
	hw.io_ports[IDE_LCYL_OFFSET]	= DEVADDR((ch) ? 0xC4 : 0x84);
	hw.io_ports[IDE_HCYL_OFFSET]	= DEVADDR((ch) ? 0xC5 : 0x85);
	hw.io_ports[IDE_SELECT_OFFSET]	= DEVADDR((ch) ? 0xC6 : 0x86);
	hw.io_ports[IDE_STATUS_OFFSET]	= DEVADDR((ch) ? 0xC7 : 0x87);
	hw.io_ports[IDE_CONTROL_OFFSET]	= DEVADDR((ch) ? 0xCA : 0x8A);
#endif /* SIIMAGE_BUFFERED_TASKFILE */
#else
#ifdef SIIMAGE_BUFFERED_TASKFILE
	for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; i++)
		hw.io_ports[i] = DEVADDR((ch) ? 0xD0 : 0x90)|(i);
	hw.io_ports[IDE_CONTROL_OFFSET] = DEVADDR((ch) ? 0xDA : 0x9A);
#else /* ! SIIMAGE_BUFFERED_TASKFILE */
	for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; i++)
		hw.io_ports[i] = DEVADDR((ch) ? 0xC0 : 0x80)|(i);
	hw.io_ports[IDE_CONTROL_OFFSET] = DEVADDR((ch) ? 0xCA : 0x8A);
#endif /* SIIMAGE_BUFFERED_TASKFILE */
#endif

#if 0
	printk(KERN_DEBUG "%s: ", hwif->name);
	for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; i++)
		printk("0x%08x ", DEVADDR((ch) ? 0xC0 : 0x80)|(i));
	printk("0x%08x ", DEVADDR((ch) ? 0xCA : 0x8A)|(i));
#endif

	hw.io_ports[IDE_IRQ_OFFSET]	= 0;

        if (dev->device == PCI_DEVICE_ID_SII_3112) {
		hw.sata_scr[SATA_STATUS_OFFSET]	= DEVADDR((ch) ? 0x184 : 0x104);
		hw.sata_scr[SATA_ERROR_OFFSET]	= DEVADDR((ch) ? 0x188 : 0x108);
		hw.sata_scr[SATA_CONTROL_OFFSET]= DEVADDR((ch) ? 0x180 : 0x100);
		hw.sata_misc[SATA_MISC_OFFSET]	= DEVADDR((ch) ? 0x1C0 : 0x140);
		hw.sata_misc[SATA_PHY_OFFSET]	= DEVADDR((ch) ? 0x1C4 : 0x144);
		hw.sata_misc[SATA_IEN_OFFSET]	= DEVADDR((ch) ? 0x1C8 : 0x148);
	}

	hw.priv				= (void *) addr;
//	hw.priv				= pci_get_drvdata(hwif->pci_dev);
	hw.irq				= hwif->pci_dev->irq;

	memcpy(&hwif->hw, &hw, sizeof(hw));
	memcpy(hwif->io_ports, hwif->hw.io_ports, sizeof(hwif->hw.io_ports));

	if (hwif->pci_dev->device == PCI_DEVICE_ID_SII_3112) {
		memcpy(hwif->sata_scr, hwif->hw.sata_scr, sizeof(hwif->hw.sata_scr));
		memcpy(hwif->sata_misc, hwif->hw.sata_misc, sizeof(hwif->hw.sata_misc));
	}

#ifdef SIIMAGE_BUFFERED_TASKFILE
        hwif->addressing = 1;
#endif /* SIIMAGE_BUFFERED_TASKFILE */
	hwif->irq			= hw.irq;
	hwif->hwif_data			= pci_get_drvdata(hwif->pci_dev);

#ifdef SIIMAGE_LARGE_DMA
	hwif->dma_base			= DEVADDR((ch) ? 0x18 : 0x10);
	hwif->dma_base2			= DEVADDR((ch) ? 0x08 : 0x00);
	hwif->dma_prdtable		= (hwif->dma_base2 + 4);
#else /* ! SIIMAGE_LARGE_DMA */
	hwif->dma_base			= DEVADDR((ch) ? 0x08 : 0x00);
	hwif->dma_base2			= DEVADDR((ch) ? 0x18 : 0x10);
#endif /* SIIMAGE_LARGE_DMA */
	hwif->mmio			= 1;
}

static void __init init_iops_siimage (ide_hwif_t *hwif)
{
	struct pci_dev *dev	= hwif->pci_dev;
	u32 class_rev		= 0;

	pci_read_config_dword(dev, PCI_CLASS_REVISION, &class_rev);
	class_rev &= 0xff;

	hwif->rqsize = 128;
	if ((dev->device == PCI_DEVICE_ID_SII_3112) && (!(class_rev)))
		hwif->rqsize = 16;

	if (pci_get_drvdata(dev) == NULL)
		return;
	init_mmio_iops_siimage(hwif);
}

static unsigned int __init ata66_siimage (ide_hwif_t *hwif)
{
	if (pci_get_drvdata(hwif->pci_dev) == NULL) {
		u8 ata66 = 0;
		pci_read_config_byte(hwif->pci_dev, SELREG(0), &ata66);
		return (ata66 & 0x01) ? 1 : 0;
	}

	return (hwif->INB(SELADDR(0)) & 0x01) ? 1 : 0;
}

static void __init init_hwif_siimage (ide_hwif_t *hwif)
{
	hwif->autodma = 0;
	hwif->busproc   = &siimage_busproc;
	hwif->resetproc = &siimage_reset;
	hwif->speedproc = &siimage_tune_chipset;
	hwif->tuneproc	= &siimage_tuneproc;
	hwif->reset_poll = &siimage_reset_poll;
	hwif->pre_reset = &siimage_pre_reset;

	if (!hwif->dma_base) {
		hwif->drives[0].autotune = 1;
		hwif->drives[1].autotune = 1;
		return;
	}

	hwif->ultra_mask = 0x7f;
	hwif->mwdma_mask = 0x07;
	hwif->swdma_mask = 0x07;

	if (hwif->pci_dev->device != PCI_DEVICE_ID_SII_3112)
		hwif->atapi_dma = 1;

	hwif->ide_dma_check = &siimage_config_drive_for_dma;
	if (!(hwif->udma_four))
		hwif->udma_four = ata66_siimage(hwif);

	if (hwif->mmio) {
		hwif->ide_dma_count = &siimage_mmio_ide_dma_count;
		hwif->ide_dma_test_irq = &siimage_mmio_ide_dma_test_irq;
		hwif->ide_dma_verbose = &siimage_mmio_ide_dma_verbose;
	} else {
		hwif->ide_dma_test_irq = & siimage_io_ide_dma_test_irq;
	}
	if (!noautodma)
		hwif->autodma = 1;
	hwif->drives[0].autodma = hwif->autodma;
	hwif->drives[1].autodma = hwif->autodma;
}

static void __init init_dma_siimage (ide_hwif_t *hwif, unsigned long dmabase)
{
	ide_setup_dma(hwif, dmabase, 8);
}

extern void ide_setup_pci_device(struct pci_dev *, ide_pci_device_t *);


static int __devinit siimage_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	ide_pci_device_t *d = &siimage_chipsets[id->driver_data];
	if (dev->device != d->device)
		BUG();
	ide_setup_pci_device(dev, d);
	MOD_INC_USE_COUNT;
	return 0;
}

static struct pci_device_id siimage_pci_tbl[] __devinitdata = {
	{ PCI_VENDOR_ID_CMD, PCI_DEVICE_ID_SII_680,  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ PCI_VENDOR_ID_CMD, PCI_DEVICE_ID_SII_3112, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 1},
	{ 0, },
};

static struct pci_driver driver = {
	.name		= "SiI IDE",
	.id_table	= siimage_pci_tbl,
	.probe		= siimage_init_one,
};

static int siimage_ide_init(void)
{
	return ide_pci_register_driver(&driver);
}

static void siimage_ide_exit(void)
{
	ide_pci_unregister_driver(&driver);
}

module_init(siimage_ide_init);
module_exit(siimage_ide_exit);

MODULE_AUTHOR("Andre Hedrick");
MODULE_DESCRIPTION("PCI driver module for SiI IDE");
MODULE_LICENSE("GPL");
