/*
 * linux/drivers/ide/serverworks.c		Version 0.2	17 Oct 2000
 *
 *  Copyright (C) 2000 Cobalt Networks, Inc. <asun@cobalt.com>
 *  May be copied or modified under the terms of the GNU General Public License
 *
 *  interface borrowed from alim15x3.c:
 *  Copyright (C) 1998-2000 Michel Aubry, Maintainer
 *  Copyright (C) 1998-2000 Andrzej Krzysztofowicz, Maintainer
 *
 *  Copyright (C) 1998-2000 Andre Hedrick <andre@linux-ide.org>
 *
 *  IDE support for the ServerWorks OSB4 IDE chipset
 *
 * here's the default lspci:
 *
 * 00:0f.1 IDE interface: ServerWorks: Unknown device 0211 (prog-if 8a [Master SecP PriP])
 *	Control: I/O+ Mem- BusMaster+ SpecCycle- MemWINV- VGASnoop- ParErr- Stepping- SERR+ FastB2B-
 *	Status: Cap- 66Mhz- UDF- FastB2B- ParErr- DEVSEL=medium >TAbort- <TAbort- <MAbort- >SERR- <PERR-
 *	Latency: 255
 *	Region 4: I/O ports at c200
 * 00: 66 11 11 02 05 01 00 02 00 8a 01 01 00 ff 80 00
 * 10: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * 20: 01 c2 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * 30: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * 40: 99 99 99 99 ff ff ff ff 0c 0c 00 00 00 00 00 00
 * 50: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * 60: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * 70: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * 80: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * 90: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * a0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * b0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * c0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * d0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * e0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * f0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 *
 * 00:0f.1 IDE interface: ServerWorks: Unknown device 0212 (rev 92) (prog-if 8a [Master SecP PriP])
 *         Subsystem: ServerWorks: Unknown device 0212
 *         Control: I/O+ Mem- BusMaster+ SpecCycle- MemWINV- VGASnoop- ParErr- Stepping- SERR- FastB2B-
 *         Status: Cap- 66Mhz- UDF- FastB2B- ParErr- DEVSEL=medium >TAbort- <TAbort- <MAbort- >SERR- <PERR-
 *         Latency: 64, cache line size 08
 *         Region 0: I/O ports at 01f0
 *         Region 1: I/O ports at 03f4
 *         Region 2: I/O ports at 0170
 *         Region 3: I/O ports at 0374
 *         Region 4: I/O ports at 08b0
 *         Region 5: I/O ports at 1000
 *
 * 00:0f.1 IDE interface: ServerWorks: Unknown device 0212 (rev 92)
 * 00: 66 11 12 02 05 00 00 02 92 8a 01 01 08 40 80 00
 * 10: f1 01 00 00 f5 03 00 00 71 01 00 00 75 03 00 00
 * 20: b1 08 00 00 01 10 00 00 00 00 00 00 66 11 12 02
 * 30: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * 40: 4f 4f 4f 4f 20 ff ff ff f0 50 44 44 00 00 00 00
 * 50: 00 00 00 00 07 00 44 02 0f 04 03 00 00 00 00 00
 * 60: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * 70: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * 80: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * 90: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * a0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * b0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * c0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * d0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * e0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * f0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 *
 *
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/hdreg.h>
#include <linux/ide.h>
#include <linux/init.h>
#include <linux/delay.h>

#include <asm/io.h>

#include "ide_modes.h"

#define SVWKS_DEBUG_DRIVE_INFO		0

#define DISPLAY_SVWKS_TIMINGS

#if defined(DISPLAY_SVWKS_TIMINGS) && defined(CONFIG_PROC_FS)
#include <linux/stat.h>
#include <linux/proc_fs.h>

static struct pci_dev *bmide_dev;

static int svwks_get_info(char *, char **, off_t, int);
extern int (*svwks_display_info)(char *, char **, off_t, int); /* ide-proc.c */
extern char *ide_media_verbose(ide_drive_t *);

static int svwks_get_info (char *buffer, char **addr, off_t offset, int count)
{
	char *p = buffer;
	u32 bibma = pci_resource_start(bmide_dev, 4);
	u32 reg40, reg44;
	u16 reg48, reg56;
	u8  c0 = 0, c1 = 0, reg54;

	pci_read_config_dword(bmide_dev, 0x40, &reg40);
	pci_read_config_dword(bmide_dev, 0x44, &reg44);
	pci_read_config_word(bmide_dev, 0x48, &reg48);
	pci_read_config_byte(bmide_dev, 0x54, &reg54);
	pci_read_config_word(bmide_dev, 0x56, &reg56);

        /*
         * at that point bibma+0x2 et bibma+0xa are byte registers
         * to investigate:
         */
	c0 = inb_p((unsigned short)bibma + 0x02);
	c1 = inb_p((unsigned short)bibma + 0x0a);

	switch(bmide_dev->device) {
		case PCI_DEVICE_ID_SERVERWORKS_CSB5IDE:
			p += sprintf(p, "\n                                ServerWorks CSB5 Chipset.\n");
			break;
		case PCI_DEVICE_ID_SERVERWORKS_OSB4:
			p += sprintf(p, "\n                                ServerWorks OSB4 Chipset.\n");
			break;
		default:
			p += sprintf(p, "\n                                ServerWorks 0x%04x Chipset.\n", bmide_dev->device);
			break;
	}

	p += sprintf(p, "------------------------------- General Status ---------------------------------\n");
#if 0
	p += sprintf(p, "                                     : %s\n", "str");
#endif
	p += sprintf(p, "--------------- Primary Channel ---------------- Secondary Channel -------------\n");
	p += sprintf(p, "                %sabled                         %sabled\n",
			(c0&0x80) ? "dis" : " en",
			(c1&0x80) ? "dis" : " en");
	p += sprintf(p, "--------------- drive0 --------- drive1 -------- drive0 ---------- drive1 ------\n");
	p += sprintf(p, "DMA enabled:    %s              %s             %s               %s\n",
			(c0&0x20) ? "yes" : "no ",
			(c0&0x40) ? "yes" : "no ",
			(c1&0x20) ? "yes" : "no ",
			(c1&0x40) ? "yes" : "no " );
	p += sprintf(p, "UDMA enabled:   %s              %s             %s               %s\n",
			(reg54 & 0x01) ? "yes" : "no ",
			(reg54 & 0x02) ? "yes" : "no ",
			(reg54 & 0x04) ? "yes" : "no ",
			(reg54 & 0x08) ? "yes" : "no " );
	p += sprintf(p, "UDMA enabled:   %s                %s               %s                 %s\n",
			((reg56&0x0005)==0x0005)?"5":
				((reg56&0x0004)==0x0004)?"4":
				((reg56&0x0003)==0x0003)?"3":
				((reg56&0x0002)==0x0002)?"2":
				((reg56&0x0001)==0x0001)?"1":
				((reg56&0x000F))?"?":"0",
			((reg56&0x0050)==0x0050)?"5":
				((reg56&0x0040)==0x0040)?"4":
				((reg56&0x0030)==0x0030)?"3":
				((reg56&0x0020)==0x0020)?"2":
				((reg56&0x0010)==0x0010)?"1":
				((reg56&0x00F0))?"?":"0",
			((reg56&0x0500)==0x0500)?"5":
				((reg56&0x0400)==0x0400)?"4":
				((reg56&0x0300)==0x0300)?"3":
				((reg56&0x0200)==0x0200)?"2":
				((reg56&0x0100)==0x0100)?"1":
				((reg56&0x0F00))?"?":"0",
			((reg56&0x5000)==0x5000)?"5":
				((reg56&0x4000)==0x4000)?"4":
				((reg56&0x3000)==0x3000)?"3":
				((reg56&0x2000)==0x2000)?"2":
				((reg56&0x1000)==0x1000)?"1":
				((reg56&0xF000))?"?":"0");
	p += sprintf(p, "DMA enabled:    %s                %s               %s                 %s\n",
			((reg44&0x00002000)==0x00002000)?"2":
				((reg44&0x00002100)==0x00002100)?"1":
				((reg44&0x00007700)==0x00007700)?"0":
				((reg44&0x0000FF00)==0x0000FF00)?"X":"?",
			((reg44&0x00000020)==0x00000020)?"2":
				((reg44&0x00000021)==0x00000021)?"1":
				((reg44&0x00000077)==0x00000077)?"0":
				((reg44&0x000000FF)==0x000000FF)?"X":"?",
			((reg44&0x20000000)==0x20000000)?"2":
				((reg44&0x21000000)==0x21000000)?"1":
				((reg44&0x77000000)==0x77000000)?"0":
				((reg44&0xFF000000)==0xFF000000)?"X":"?",
			((reg44&0x00200000)==0x00200000)?"2":
				((reg44&0x00210000)==0x00210000)?"1":
				((reg44&0x00770000)==0x00770000)?"0":
				((reg44&0x00FF0000)==0x00FF0000)?"X":"?");
#if 0
	if (bmide_dev->device == PCI_DEVICE_ID_SERVERWORKS_CSB5IDE)
		p += sprintf(p, "PIO  enabled:   %s                %s               %s                 %s\n",
	if (bmide_dev->device == PCI_DEVICE_ID_SERVERWORKS_OSB4)
#endif
		p += sprintf(p, "PIO  enabled:   %s                %s               %s                 %s\n",
			((reg40&0x00002000)==0x00002000)?"4":
				((reg40&0x00002200)==0x00002200)?"3":
				((reg40&0x00003400)==0x00003400)?"2":
				((reg40&0x00004700)==0x00004700)?"1":
				((reg40&0x00005D00)==0x00005D00)?"0":"?",
			((reg40&0x00000020)==0x00000020)?"4":
				((reg40&0x00000022)==0x00000022)?"3":
				((reg40&0x00000034)==0x00000034)?"2":
				((reg40&0x00000047)==0x00000047)?"1":
				((reg40&0x0000005D)==0x0000005D)?"0":"?",
			((reg40&0x20000000)==0x20000000)?"4":
				((reg40&0x22000000)==0x22000000)?"3":
				((reg40&0x34000000)==0x34000000)?"2":
				((reg40&0x47000000)==0x47000000)?"1":
				((reg40&0x5D000000)==0x5D000000)?"0":"?",
			((reg40&0x00200000)==0x00200000)?"4":
				((reg40&0x00220000)==0x00220000)?"3":
				((reg40&0x00340000)==0x00340000)?"2":
				((reg40&0x00470000)==0x00470000)?"1":
				((reg40&0x005D0000)==0x005D0000)?"0":"?");
	return p-buffer;	 /* => must be less than 4k! */
}
#endif  /* defined(DISPLAY_SVWKS_TIMINGS) && defined(CONFIG_PROC_FS) */

static byte svwks_revision = 0;

byte svwks_proc = 0;

extern char *ide_xfer_verbose (byte xfer_rate);

static struct pci_dev *isa_dev;

static int svwks_tune_chipset (ide_drive_t *drive, byte speed)
{
	byte udma_modes[]	= { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05 };
	byte dma_modes[]	= { 0x77, 0x21, 0x20 };
	byte pio_modes[]	= { 0x5d, 0x47, 0x34, 0x22, 0x20 };

	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;
	byte unit		= (drive->select.b.unit & 0x01);
	byte csb5		= (dev->device == PCI_DEVICE_ID_SERVERWORKS_CSB5IDE) ? 1 : 0;

#ifdef CONFIG_BLK_DEV_IDEDMA
	unsigned long dma_base	= hwif->dma_base;
#endif /* CONFIG_BLK_DEV_IDEDMA */
	int err;

	byte drive_pci		= 0x00;
	byte drive_pci2		= 0x00;
	byte drive_pci3		= hwif->channel ? 0x57 : 0x56;

	byte ultra_enable	= 0x00;
	byte ultra_timing	= 0x00;
	byte dma_timing		= 0x00;
	byte pio_timing		= 0x00;
	unsigned short csb5_pio	= 0x00;

	byte pio	= ide_get_best_pio_mode(drive, 255, 5, NULL);

        switch (drive->dn) {
		case 0: drive_pci = 0x41; drive_pci2 = 0x45; break;
		case 1: drive_pci = 0x40; drive_pci2 = 0x44; break;
		case 2: drive_pci = 0x43; drive_pci2 = 0x47; break;
		case 3: drive_pci = 0x42; drive_pci2 = 0x46; break;
		default:
			return -1;
	}

	pci_read_config_byte(dev, drive_pci, &pio_timing);
	pci_read_config_byte(dev, drive_pci2, &dma_timing);
	pci_read_config_byte(dev, drive_pci3, &ultra_timing);
	pci_read_config_word(dev, 0x4A, &csb5_pio);
	pci_read_config_byte(dev, 0x54, &ultra_enable);

#ifdef DEBUG
	printk("%s: UDMA 0x%02x DMAPIO 0x%02x PIO 0x%02x ",
		drive->name, ultra_timing, dma_timing, pio_timing);
#endif

	pio_timing	&= ~0xFF;
	dma_timing	&= ~0xFF;
	ultra_timing	&= ~(0x0F << (4*unit));
	ultra_enable	&= ~(0x01 << drive->dn);
	csb5_pio	&= ~(0x0F << (4*drive->dn));

	switch(speed) {
		case XFER_PIO_4:
		case XFER_PIO_3:
		case XFER_PIO_2:
		case XFER_PIO_1:
		case XFER_PIO_0:
			pio_timing |= pio_modes[speed - XFER_PIO_0];
			csb5_pio   |= ((speed - XFER_PIO_0) << (4*drive->dn));
			break;
#ifdef CONFIG_BLK_DEV_IDEDMA
		case XFER_MW_DMA_2:
		case XFER_MW_DMA_1:
		case XFER_MW_DMA_0:
			pio_timing |= pio_modes[pio];
			csb5_pio   |= (pio << (4*drive->dn));
			dma_timing |= dma_modes[speed - XFER_MW_DMA_0];
			break;

		case XFER_UDMA_5:
		case XFER_UDMA_4:
		case XFER_UDMA_3:
		case XFER_UDMA_2:
		case XFER_UDMA_1:
		case XFER_UDMA_0:
			pio_timing |= pio_modes[pio];
			csb5_pio   |= (pio << (4*drive->dn));
			dma_timing |= dma_modes[2];
			ultra_timing |= ((udma_modes[speed - XFER_UDMA_0]) << (4*unit));
			ultra_enable |= (0x01 << drive->dn);
#endif
		default:
			break;
	}

#ifdef DEBUG
	printk("%s: UDMA 0x%02x DMAPIO 0x%02x PIO 0x%02x ",
		drive->name, ultra_timing, dma_timing, pio_timing);
#endif

#if OSB4_DEBUG_DRIVE_INFO
	printk("%s: %s drive%d\n", drive->name, ide_xfer_verbose(speed), drive->dn);
#endif /* OSB4_DEBUG_DRIVE_INFO */

	if (!drive->init_speed)
		drive->init_speed = speed;

	pci_write_config_byte(dev, drive_pci, pio_timing);
	if (csb5)
		pci_write_config_word(dev, 0x4A, csb5_pio);

#ifdef CONFIG_BLK_DEV_IDEDMA
	pci_write_config_byte(dev, drive_pci2, dma_timing);
	pci_write_config_byte(dev, drive_pci3, ultra_timing);
	pci_write_config_byte(dev, 0x54, ultra_enable);
	
	if (speed > XFER_PIO_4) {
		outb(inb(dma_base+2)|(1<<(5+unit)), dma_base+2);
	} else {
		outb(inb(dma_base+2) & ~(1<<(5+unit)), dma_base+2);
	}
#endif /* CONFIG_BLK_DEV_IDEDMA */

	err = ide_config_drive_speed(drive, speed);
	drive->current_speed = speed;
	return err;
}

static void config_chipset_for_pio (ide_drive_t *drive)
{
	unsigned short eide_pio_timing[6] = {960, 480, 240, 180, 120, 90};
	unsigned short xfer_pio = drive->id->eide_pio_modes;
	byte			timing, speed, pio;

	pio = ide_get_best_pio_mode(drive, 255, 5, NULL);

	if (xfer_pio> 4)
		xfer_pio = 0;

	if (drive->id->eide_pio_iordy > 0) {
		for (xfer_pio = 5;
			xfer_pio>0 &&
			drive->id->eide_pio_iordy>eide_pio_timing[xfer_pio];
			xfer_pio--);
	} else {
		xfer_pio = (drive->id->eide_pio_modes & 4) ? 0x05 :
			   (drive->id->eide_pio_modes & 2) ? 0x04 :
			   (drive->id->eide_pio_modes & 1) ? 0x03 :
			   (drive->id->tPIO & 2) ? 0x02 :
			   (drive->id->tPIO & 1) ? 0x01 : xfer_pio;
	}

	timing = (xfer_pio >= pio) ? xfer_pio : pio;

	switch(timing) {
		case 4: speed = XFER_PIO_4;break;
		case 3: speed = XFER_PIO_3;break;
		case 2: speed = XFER_PIO_2;break;
		case 1: speed = XFER_PIO_1;break;
		default:
			speed = (!drive->id->tPIO) ? XFER_PIO_0 : XFER_PIO_SLOW;
			break;
	}
	(void) svwks_tune_chipset(drive, speed);
	drive->current_speed = speed;
}

static void svwks_tune_drive (ide_drive_t *drive, byte pio)
{
	byte speed;
	switch(pio) {
		case 4:		speed = XFER_PIO_4;break;
		case 3:		speed = XFER_PIO_3;break;
		case 2:		speed = XFER_PIO_2;break;
		case 1:		speed = XFER_PIO_1;break;
		default:	speed = XFER_PIO_0;break;
	}
	(void) svwks_tune_chipset(drive, speed);
}

#ifdef CONFIG_BLK_DEV_IDEDMA
static int config_chipset_for_dma (ide_drive_t *drive)
{
	struct hd_driveid *id	= drive->id;
	struct pci_dev *dev	= HWIF(drive)->pci_dev;
	byte udma_66		= eighty_ninty_three(drive);
	byte			speed;

	int ultra66		= (dev->device == PCI_DEVICE_ID_SERVERWORKS_CSB5IDE) ? 1 : 0;
	/* need specs to figure out if osb4 is capable of ata/66/100 */
	int ultra100		= (dev->device == PCI_DEVICE_ID_SERVERWORKS_CSB5IDE) ? 1 : 0;

	if ((id->dma_ultra & 0x0020) && (udma_66) && (ultra100)) {
		speed = XFER_UDMA_5;
	} else if (id->dma_ultra & 0x0010) {
		speed = ((udma_66) && (ultra66)) ? XFER_UDMA_4 : XFER_UDMA_2;
	} else if (id->dma_ultra & 0x0008) {
		speed = ((udma_66) && (ultra66)) ? XFER_UDMA_3 : XFER_UDMA_1;
	} else if (id->dma_ultra & 0x0004) {
		speed = XFER_UDMA_2;
	} else if (id->dma_ultra & 0x0002) {
		speed = XFER_UDMA_1;
	} else if (id->dma_ultra & 0x0001) {
		speed = XFER_UDMA_0;
	} else if (id->dma_mword & 0x0004) {
		speed = XFER_MW_DMA_2;
	} else if (id->dma_mword & 0x0002) {
		speed = XFER_MW_DMA_1;
	} else if (id->dma_1word & 0x0004) {
		speed = XFER_SW_DMA_2;
	} else {
		speed = XFER_PIO_0 + ide_get_best_pio_mode(drive, 255, 5, NULL);
	}

	(void) svwks_tune_chipset(drive, speed);

	return ((int)	((id->dma_ultra >> 11) & 7) ? ide_dma_on :
			((id->dma_ultra >> 8) & 7) ? ide_dma_on :
			((id->dma_mword >> 8) & 7) ? ide_dma_on :
			((id->dma_1word >> 8) & 7) ? ide_dma_on :
						     ide_dma_off_quietly);
}

static int config_drive_xfer_rate (ide_drive_t *drive)
{
	struct hd_driveid *id = drive->id;
	ide_dma_action_t dma_func = ide_dma_on;

	if (id && (id->capability & 1) && HWIF(drive)->autodma) {
		/* Consult the list of known "bad" drives */
		if (ide_dmaproc(ide_dma_bad_drive, drive)) {
			dma_func = ide_dma_off;
			goto fast_ata_pio;
		}
		dma_func = ide_dma_off_quietly;
		if (id->field_valid & 4) {
			if (id->dma_ultra & 0x002F) {
				/* Force if Capable UltraDMA */
				dma_func = config_chipset_for_dma(drive);
				if ((id->field_valid & 2) &&
				    (dma_func != ide_dma_on))
					goto try_dma_modes;
			}
		} else if (id->field_valid & 2) {
try_dma_modes:
			if ((id->dma_mword & 0x0007) ||
			    (id->dma_1word & 0x007)) {
				/* Force if Capable regular DMA modes */
				dma_func = config_chipset_for_dma(drive);
				if (dma_func != ide_dma_on)
					goto no_dma_set;
			}
		} else if (ide_dmaproc(ide_dma_good_drive, drive)) {
			if (id->eide_dma_time > 150) {
				goto no_dma_set;
			}
			/* Consult the list of known "good" drives */
			dma_func = config_chipset_for_dma(drive);
			if (dma_func != ide_dma_on)
				goto no_dma_set;
		} else {
			goto fast_ata_pio;
		}
	} else if ((id->capability & 8) || (id->field_valid & 2)) {
fast_ata_pio:
		dma_func = ide_dma_off_quietly;
no_dma_set:
		config_chipset_for_pio(drive);
	}
	return HWIF(drive)->dmaproc(dma_func, drive);
}

static int svwks_dmaproc(ide_dma_action_t func, ide_drive_t *drive)
{
	switch (func) {
		case ide_dma_check:
			return config_drive_xfer_rate(drive);
		default :
			break;
	}
	/* Other cases are done by generic IDE-DMA code. */
	return ide_dmaproc(func, drive);
}
#endif /* CONFIG_BLK_DEV_IDEDMA */

unsigned int __init pci_init_svwks (struct pci_dev *dev, const char *name)
{
	unsigned int reg64;

	pci_read_config_byte(dev, PCI_REVISION_ID, &svwks_revision);

	if (dev->device == PCI_DEVICE_ID_SERVERWORKS_OSB4IDE) {
		isa_dev = pci_find_device(PCI_VENDOR_ID_SERVERWORKS, PCI_DEVICE_ID_SERVERWORKS_OSB4, NULL);

		pci_read_config_dword(isa_dev, 0x64, &reg64);
#ifdef DEBUG
		printk("%s: reg64 == 0x%08x\n", name, reg64);
#endif

//		reg64 &= ~0x0000A000;
//#ifdef CONFIG_SMP
//		reg64 |= 0x00008000;
//#endif
	/* Assume the APIC was set up properly by the BIOS for now . If it
	   wasnt we need to do a fix up _way_ earlier. Bits 15,10,3 control
	   APIC enable, routing and decode */
	   
		reg64 &= ~0x00002000;	
		pci_write_config_dword(isa_dev, 0x64, reg64);
	}
	pci_write_config_byte(dev, PCI_LATENCY_TIMER, 0x40);

#if defined(DISPLAY_SVWKS_TIMINGS) && defined(CONFIG_PROC_FS)
	if (!svwks_proc) {
		svwks_proc = 1;
		bmide_dev = dev;
		svwks_display_info = &svwks_get_info;
	}
#endif /* DISPLAY_SVWKS_TIMINGS && CONFIG_PROC_FS */
	return 0;
}

/* On Dell PowerEdge servers with a CSB5, the top two bits of the subsystem
 * device ID indicate presence of an 80-pin cable.
 * Bit 15 clear = secondary IDE channel does not have 80-pin cable.
 * Bit 15 set   = secondary IDE channel has 80-pin cable.
 * Bit 14 clear = primary IDE channel does not have 80-pin cable.
 * Bit 14 set   = primary IDE channel has 80-pin cable.
 */

static unsigned int __init ata66_svwks_dell (ide_hwif_t *hwif)
{
	struct pci_dev *dev	= hwif->pci_dev;
	if (dev->subsystem_vendor == PCI_VENDOR_ID_DELL        &&
	    dev->vendor           == PCI_VENDOR_ID_SERVERWORKS &&
	    dev->device           == PCI_DEVICE_ID_SERVERWORKS_CSB5IDE)
		return ((1 << (hwif->channel + 14)) &
			dev->subsystem_device) ? 1 : 0;

	return 0;

}

unsigned int __init ata66_svwks (ide_hwif_t *hwif)
{
	struct pci_dev *dev	= hwif->pci_dev;
	if (dev->subsystem_vendor == PCI_VENDOR_ID_DELL)
		return ata66_svwks_dell (hwif);
	
	return 0;
}

void __init ide_init_svwks (ide_hwif_t *hwif)
{
	if (!hwif->irq)
		hwif->irq = hwif->channel ? 15 : 14;

	hwif->tuneproc = &svwks_tune_drive;
	hwif->speedproc = &svwks_tune_chipset;

#ifndef CONFIG_BLK_DEV_IDEDMA
	hwif->drives[0].autotune = 1;
	hwif->drives[1].autotune = 1;
	hwif->autodma = 0;
	return;
#else /* CONFIG_BLK_DEV_IDEDMA */

	if (hwif->dma_base) {
		if (!noautodma)
			hwif->autodma = 1;
		hwif->dmaproc = &svwks_dmaproc;
	} else {
		hwif->autodma = 0;
		hwif->drives[0].autotune = 1;
		hwif->drives[1].autotune = 1;
	}
#endif /* !CONFIG_BLK_DEV_IDEDMA */
}
