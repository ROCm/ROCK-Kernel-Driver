/*
 * linux/drivers/ide/sis5513.c		Version 0.11	June 9, 2000
 *
 * Copyright (C) 1999-2000	Andre Hedrick <andre@linux-ide.org>
 * May be copied or modified under the terms of the GNU General Public License
 *
 * Thanks to SIS Taiwan for direct support and hardware.
 * Tested and designed on the SiS620/5513 chipset.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>

#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/ide.h>

#include <asm/io.h>
#include <asm/irq.h>

#include "ide_modes.h"

#define DISPLAY_SIS_TIMINGS
#define SIS5513_DEBUG_DRIVE_INFO	0

static struct pci_dev *host_dev = NULL;

#define SIS5513_FLAG_ATA_00		0x00000000
#define SIS5513_FLAG_ATA_16		0x00000001
#define SIS5513_FLAG_ATA_33		0x00000002
#define SIS5513_FLAG_ATA_66		0x00000004
#define SIS5513_FLAG_LATENCY		0x00000010

static const struct {
	const char *name;
	unsigned short host_id;
	unsigned int flags;
} SiSHostChipInfo[] = {
	{ "SiS530",	PCI_DEVICE_ID_SI_530,	SIS5513_FLAG_ATA_66, },
	{ "SiS540",	PCI_DEVICE_ID_SI_540,	SIS5513_FLAG_ATA_66, },
	{ "SiS620",	PCI_DEVICE_ID_SI_620,	SIS5513_FLAG_ATA_66|SIS5513_FLAG_LATENCY, },
	{ "SiS630",	PCI_DEVICE_ID_SI_630,	SIS5513_FLAG_ATA_66|SIS5513_FLAG_LATENCY, },
	{ "SiS730",	PCI_DEVICE_ID_SI_730,	SIS5513_FLAG_ATA_66|SIS5513_FLAG_LATENCY, },
	{ "SiS5591",	PCI_DEVICE_ID_SI_5591,	SIS5513_FLAG_ATA_33, },
	{ "SiS5597",	PCI_DEVICE_ID_SI_5597,	SIS5513_FLAG_ATA_33, },
	{ "SiS5600",	PCI_DEVICE_ID_SI_5600,	SIS5513_FLAG_ATA_33, },
	{ "SiS5511",	PCI_DEVICE_ID_SI_5511,	SIS5513_FLAG_ATA_16, },
};

#if 0

static struct _pio_mode_mapping {
	byte data_active;
	byte recovery;
	byte pio_mode;
} pio_mode_mapping[] = {
	{ 8, 12, 0 },
	{ 6,  7, 1 },
	{ 4,  4, 2 },
	{ 3,  3, 3 },
	{ 3,  1, 4 }
};

static struct _dma_mode_mapping {
	byte data_active;
	byte recovery;
	byte dma_mode;
} dma_mode_mapping[] = {
	{ 8, 8, 0 },
	{ 3, 2, 1 },
	{ 3, 1, 2 }
};

static struct _udma_mode_mapping {
	byte cycle_time;
	char * udma_mode;
} udma_mode_mapping[] = {
	{ 8, "Mode 0" },
	{ 6, "Mode 1" },
	{ 4, "Mode 2" }, 
	{ 3, "Mode 3" },
	{ 2, "Mode 4" },
	{ 0, "Undefined" }
};

static __inline__ char * find_udma_mode (byte cycle_time)
{
	int n;
	
	for (n = 0; n <= 4; n++)
		if (udma_mode_mapping[n].cycle_time <= cycle_time)
			return udma_mode_mapping[n].udma_mode;
	return udma_mode_mapping[4].udma_mode;
}
#endif

#if defined(DISPLAY_SIS_TIMINGS) && defined(CONFIG_PROC_FS)
#include <linux/stat.h>
#include <linux/proc_fs.h>

static int sis_get_info(char *, char **, off_t, int);
extern int (*sis_display_info)(char *, char **, off_t, int); /* ide-proc.c */
static struct pci_dev *bmide_dev;

static char *cable_type[] = {
	"80 pins",
	"40 pins"
};

static char *recovery_time [] ={
	"12 PCICLK", "1 PCICLK",
	"2 PCICLK", "3 PCICLK",
	"4 PCICLK", "5 PCICLCK",
	"6 PCICLK", "7 PCICLCK",
	"8 PCICLK", "9 PCICLCK",
	"10 PCICLK", "11 PCICLK",
	"13 PCICLK", "14 PCICLK",
	"15 PCICLK", "15 PCICLK"
};

static char * cycle_time [] = {
	"Undefined", "2 CLCK",
	"3 CLK", "4 CLK",
	"5 CLK", "6 CLK",
	"7 CLK", "8 CLK"
};

static char * active_time [] = {
	"8 PCICLK", "1 PCICLCK",
	"2 PCICLK", "2 PCICLK",
	"4 PCICLK", "5 PCICLK",
	"6 PCICLK", "12 PCICLK"
};

static int sis_get_info (char *buffer, char **addr, off_t offset, int count)
{
	int rc;
	char *p = buffer;
	byte reg,reg1;
	u16 reg2, reg3;

	p += sprintf(p, "--------------- Primary Channel ---------------- Secondary Channel -------------\n");
	rc = pci_read_config_byte(bmide_dev, 0x4a, &reg);
	p += sprintf(p, "Channel Status: %s \t \t \t \t %s \n",
		     (reg & 0x02) ? "On" : "Off",
		     (reg & 0x04) ? "On" : "Off");
		     
	rc = pci_read_config_byte(bmide_dev, 0x09, &reg);
	p += sprintf(p, "Operation Mode: %s \t \t \t %s \n",
		     (reg & 0x01) ? "Native" : "Compatible",
		     (reg & 0x04) ? "Native" : "Compatible");
		     	     
	rc = pci_read_config_byte(bmide_dev, 0x48, &reg);
	p += sprintf(p, "Cable Type:     %s \t \t \t %s\n",
		     (reg & 0x10) ? cable_type[1] : cable_type[0],
		     (reg & 0x20) ? cable_type[1] : cable_type[0]);
		     
	rc = pci_read_config_word(bmide_dev, 0x4c, &reg2);
	rc = pci_read_config_word(bmide_dev, 0x4e, &reg3);
	p += sprintf(p, "Prefetch Count: %d \t \t \t \t %d\n",
		     reg2, reg3);

	rc = pci_read_config_byte(bmide_dev, 0x4b, &reg);	     
	p += sprintf(p, "Drive 0:        Postwrite %s \t \t Postwrite %s\n",
		     (reg & 0x10) ? "Enabled" : "Disabled",
		     (reg & 0x40) ? "Enabled" : "Disabled");
	p += sprintf(p, "                Prefetch  %s \t \t Prefetch  %s\n",
		     (reg & 0x01) ? "Enabled" : "Disabled",
		     (reg & 0x04) ? "Enabled" : "Disabled");
		          
	rc = pci_read_config_byte(bmide_dev, 0x41, &reg);
	rc = pci_read_config_byte(bmide_dev, 0x45, &reg1);
	p += sprintf(p, "                UDMA %s \t \t \t UDMA %s\n",
		     (reg & 0x80)  ? "Enabled" : "Disabled",
		     (reg1 & 0x80) ? "Enabled" : "Disabled");
	p += sprintf(p, "                UDMA Cycle Time    %s \t UDMA Cycle Time    %s\n",
		     cycle_time[(reg & 0x70) >> 4], cycle_time[(reg1 & 0x70) >> 4]);
	p += sprintf(p, "                Data Active Time   %s \t Data Active Time   %s\n",
		     active_time[(reg & 0x07)], active_time[(reg1 &0x07)] ); 

	rc = pci_read_config_byte(bmide_dev, 0x40, &reg);
	rc = pci_read_config_byte(bmide_dev, 0x44, &reg1);
	p += sprintf(p, "                Data Recovery Time %s \t Data Recovery Time %s\n",
		     recovery_time[(reg & 0x0f)], recovery_time[(reg1 & 0x0f)]);


	rc = pci_read_config_byte(bmide_dev, 0x4b, &reg);	     
	p += sprintf(p, "Drive 1:        Postwrite %s \t \t Postwrite %s\n",
		     (reg & 0x20) ? "Enabled" : "Disabled",
		     (reg & 0x80) ? "Enabled" : "Disabled");
	p += sprintf(p, "                Prefetch  %s \t \t Prefetch  %s\n",
		     (reg & 0x02) ? "Enabled" : "Disabled",
		     (reg & 0x08) ? "Enabled" : "Disabled");

	rc = pci_read_config_byte(bmide_dev, 0x43, &reg);
	rc = pci_read_config_byte(bmide_dev, 0x47, &reg1);
	p += sprintf(p, "                UDMA %s \t \t \t UDMA %s\n",
		     (reg & 0x80)  ? "Enabled" : "Disabled",
		     (reg1 & 0x80) ? "Enabled" : "Disabled");
	p += sprintf(p, "                UDMA Cycle Time    %s \t UDMA Cycle Time    %s\n",
		     cycle_time[(reg & 0x70) >> 4], cycle_time[(reg1 & 0x70) >> 4]);
	p += sprintf(p, "                Data Active Time   %s \t Data Active Time   %s\n",
		     active_time[(reg & 0x07)], active_time[(reg1 &0x07)] ); 

	rc = pci_read_config_byte(bmide_dev, 0x42, &reg);
	rc = pci_read_config_byte(bmide_dev, 0x46, &reg1);
	p += sprintf(p, "                Data Recovery Time %s \t Data Recovery Time %s\n",
		     recovery_time[(reg & 0x0f)], recovery_time[(reg1 & 0x0f)]);
	return p-buffer;
}
#endif /* defined(DISPLAY_SIS_TIMINGS) && defined(CONFIG_PROC_FS) */

byte sis_proc = 0;
extern char *ide_xfer_verbose (byte xfer_rate);

static void config_drive_art_rwp (ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;

	byte reg4bh		= 0;
	byte rw_prefetch	= (0x11 << drive->dn);

	pci_read_config_byte(dev, 0x4b, &reg4bh);
	if (drive->media != ide_disk)
		return;
	
	if ((reg4bh & rw_prefetch) != rw_prefetch)
		pci_write_config_byte(dev, 0x4b, reg4bh|rw_prefetch);
}

static void config_art_rwp_pio (ide_drive_t *drive, byte pio)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;

	byte			timing, drive_pci, test1, test2;

	unsigned short eide_pio_timing[6] = {600, 390, 240, 180, 120, 90};
	unsigned short xfer_pio = drive->id->eide_pio_modes;

	config_drive_art_rwp(drive);
	pio = ide_get_best_pio_mode(drive, 255, pio, NULL);

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
			   (drive->id->eide_pio_modes & 1) ? 0x03 : xfer_pio;
	}

	timing = (xfer_pio >= pio) ? xfer_pio : pio;

/*
 *               Mode 0       Mode 1     Mode 2     Mode 3     Mode 4
 * Active time    8T (240ns)  6T (180ns) 4T (120ns) 3T  (90ns) 3T  (90ns)
 * 0x41 2:0 bits  000          110        100        011        011
 * Recovery time 12T (360ns)  7T (210ns) 4T (120ns) 3T  (90ns) 1T  (30ns)
 * 0x40 3:0 bits 0000         0111       0100       0011       0001
 * Cycle time    20T (600ns) 13T (390ns) 8T (240ns) 6T (180ns) 4T (120ns)
 */

	switch(drive->dn) {
		case 0:		drive_pci = 0x40; break;
		case 1:		drive_pci = 0x42; break;
		case 2:		drive_pci = 0x44; break;
		case 3:		drive_pci = 0x46; break;
		default:	return;
	}

	pci_read_config_byte(dev, drive_pci, &test1);
	pci_read_config_byte(dev, drive_pci|0x01, &test2);

	/*
	 * Do a blanket clear of active and recovery timings.
	 */

	test1 &= ~0x07;
	test2 &= ~0x0F;

	switch(timing) {
		case 4:		test1 |= 0x01; test2 |= 0x03; break;
		case 3:		test1 |= 0x03; test2 |= 0x03; break;
		case 2:		test1 |= 0x04; test2 |= 0x04; break;
		case 1:		test1 |= 0x07; test2 |= 0x06; break;
		default:	break;
	}

	pci_write_config_byte(dev, drive_pci, test1);
	pci_write_config_byte(dev, drive_pci|0x01, test2);
}

static int config_chipset_for_pio (ide_drive_t *drive, byte pio)
{
	int err;
	byte speed;

	switch(pio) {
		case 4:		speed = XFER_PIO_4; break;
		case 3:		speed = XFER_PIO_3; break;
		case 2:		speed = XFER_PIO_2; break;
		case 1:		speed = XFER_PIO_1; break;
		default:	speed = XFER_PIO_0; break;
	}

	config_art_rwp_pio(drive, pio);
	drive->current_speed = speed;
	err = ide_config_drive_speed(drive, speed);
	return err;
}

static int sis5513_tune_chipset (ide_drive_t *drive, byte speed)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;

	byte			drive_pci, test1, test2;
	byte			unmask, four_two, mask = 0;

	if (host_dev) {
		switch(host_dev->device) {
			case PCI_DEVICE_ID_SI_530:
			case PCI_DEVICE_ID_SI_540:
			case PCI_DEVICE_ID_SI_620:
			case PCI_DEVICE_ID_SI_630:
			case PCI_DEVICE_ID_SI_730:
				unmask   = 0xF0;
				four_two = 0x01;
				break;
			default:
				unmask   = 0xE0;
				four_two = 0x00;
				break;
		}
	} else {
		unmask   = 0xE0;
		four_two = 0x00;
	}

	switch(drive->dn) {
		case 0:		drive_pci = 0x40;break;
		case 1:		drive_pci = 0x42;break;
		case 2:		drive_pci = 0x44;break;
		case 3:		drive_pci = 0x46;break;
		default:	return ide_dma_off;
	}

	pci_read_config_byte(dev, drive_pci, &test1);
	pci_read_config_byte(dev, drive_pci|0x01, &test2);

	if ((speed <= XFER_MW_DMA_2) && (test2 & 0x80)) {
		pci_write_config_byte(dev, drive_pci|0x01, test2 & ~0x80);
		pci_read_config_byte(dev, drive_pci|0x01, &test2);
	} else {
		pci_write_config_byte(dev, drive_pci|0x01, test2 & ~unmask);
	}

	switch(speed) {
#ifdef CONFIG_BLK_DEV_IDEDMA
		case XFER_UDMA_5: mask = 0x80; break;
		case XFER_UDMA_4: mask = 0x90; break;
		case XFER_UDMA_3: mask = 0xA0; break;
		case XFER_UDMA_2: mask = (four_two) ? 0xB0 : 0xA0; break;
		case XFER_UDMA_1: mask = (four_two) ? 0xD0 : 0xC0; break;
		case XFER_UDMA_0: mask = unmask; break;
		case XFER_MW_DMA_2:
		case XFER_MW_DMA_1:
		case XFER_MW_DMA_0:
		case XFER_SW_DMA_2:
		case XFER_SW_DMA_1:
		case XFER_SW_DMA_0: break;
#endif /* CONFIG_BLK_DEV_IDEDMA */
		case XFER_PIO_4: return((int) config_chipset_for_pio(drive, 4));
		case XFER_PIO_3: return((int) config_chipset_for_pio(drive, 3));
		case XFER_PIO_2: return((int) config_chipset_for_pio(drive, 2));
		case XFER_PIO_1: return((int) config_chipset_for_pio(drive, 1));
		case XFER_PIO_0:
		default:	 return((int) config_chipset_for_pio(drive, 0));
	}

	if (speed > XFER_MW_DMA_2)
		pci_write_config_byte(dev, drive_pci|0x01, test2|mask);

	drive->current_speed = speed;
	return ((int) ide_config_drive_speed(drive, speed));
}

static void sis5513_tune_drive (ide_drive_t *drive, byte pio)
{
	(void) config_chipset_for_pio(drive, pio);
}

#ifdef CONFIG_BLK_DEV_IDEDMA
/*
 * ((id->hw_config & 0x4000|0x2000) && (HWIF(drive)->udma_four))
 */
static int config_chipset_for_dma (ide_drive_t *drive, byte ultra)
{
	struct hd_driveid *id	= drive->id;
	ide_hwif_t *hwif	= HWIF(drive);

	byte			four_two = 0, speed = 0;
	int			err;

	byte unit		= (drive->select.b.unit & 0x01);
	byte udma_66		= eighty_ninty_three(drive);
	byte ultra_100		= 0;

	if (host_dev) {
		switch(host_dev->device) {
			case PCI_DEVICE_ID_SI_730:
				ultra_100 = 1;
			case PCI_DEVICE_ID_SI_530:
			case PCI_DEVICE_ID_SI_540:
			case PCI_DEVICE_ID_SI_620:
			case PCI_DEVICE_ID_SI_630:
				four_two = 0x01;
				break;
			default:
				four_two = 0x00; break;
		}
	}

	if ((id->dma_ultra & 0x0020) && (ultra) && (udma_66) && (four_two) && (ultra_100))
		speed = XFER_UDMA_5;
	else if ((id->dma_ultra & 0x0010) && (ultra) && (udma_66) && (four_two))
		speed = XFER_UDMA_4;
	else if ((id->dma_ultra & 0x0008) && (ultra) && (udma_66) && (four_two))
		speed = XFER_UDMA_3;
	else if ((id->dma_ultra & 0x0004) && (ultra))
		speed = XFER_UDMA_2;
	else if ((id->dma_ultra & 0x0002) && (ultra))
		speed = XFER_UDMA_1;
	else if ((id->dma_ultra & 0x0001) && (ultra))
		speed = XFER_UDMA_0;
	else if (id->dma_mword & 0x0004)
		speed = XFER_MW_DMA_2;
	else if (id->dma_mword & 0x0002)
		speed = XFER_MW_DMA_1;
	else if (id->dma_mword & 0x0001)
		speed = XFER_MW_DMA_0;
	else if (id->dma_1word & 0x0004)
		speed = XFER_SW_DMA_2;
	else if (id->dma_1word & 0x0002)
		speed = XFER_SW_DMA_1;
	else if (id->dma_1word & 0x0001)
		speed = XFER_SW_DMA_0;
	else
		return ((int) ide_dma_off_quietly);

	outb(inb(hwif->dma_base+2)|(1<<(5+unit)), hwif->dma_base+2);

	err = sis5513_tune_chipset(drive, speed);

#if SIS5513_DEBUG_DRIVE_INFO
	printk("%s: %s drive%d\n", drive->name, ide_xfer_verbose(speed), drive->dn);
#endif /* SIS5513_DEBUG_DRIVE_INFO */

	return ((int)	((id->dma_ultra >> 11) & 3) ? ide_dma_on :
			((id->dma_ultra >> 8) & 7) ? ide_dma_on :
			((id->dma_mword >> 8) & 7) ? ide_dma_on :
			((id->dma_1word >> 8) & 7) ? ide_dma_on :
						     ide_dma_off_quietly);
}

static int config_drive_xfer_rate (ide_drive_t *drive)
{
	struct hd_driveid *id		= drive->id;
	ide_dma_action_t dma_func	= ide_dma_off_quietly;

	if (id && (id->capability & 1) && HWIF(drive)->autodma) {
		/* Consult the list of known "bad" drives */
		if (ide_dmaproc(ide_dma_bad_drive, drive)) {
			dma_func = ide_dma_off;
			goto fast_ata_pio;
		}
		dma_func = ide_dma_off_quietly;
		if (id->field_valid & 4) {
			if (id->dma_ultra & 0x001F) {
				/* Force if Capable UltraDMA */
				dma_func = config_chipset_for_dma(drive, 1);
				if ((id->field_valid & 2) &&
				    (dma_func != ide_dma_on))
					goto try_dma_modes;
			}
		} else if (id->field_valid & 2) {
try_dma_modes:
			if ((id->dma_mword & 0x0007) ||
			    (id->dma_1word & 0x0007)) {
				/* Force if Capable regular DMA modes */
				dma_func = config_chipset_for_dma(drive, 0);
				if (dma_func != ide_dma_on)
					goto no_dma_set;
			}
		} else if ((ide_dmaproc(ide_dma_good_drive, drive)) &&
			   (id->eide_dma_time > 150)) {
			/* Consult the list of known "good" drives */
			dma_func = config_chipset_for_dma(drive, 0);
			if (dma_func != ide_dma_on)
				goto no_dma_set;
		} else {
			goto fast_ata_pio;
		}
	} else if ((id->capability & 8) || (id->field_valid & 2)) {
fast_ata_pio:
		dma_func = ide_dma_off_quietly;
no_dma_set:
		(void) config_chipset_for_pio(drive, 5);
	}

	return HWIF(drive)->dmaproc(dma_func, drive);
}

/*
 * sis5513_dmaproc() initiates/aborts (U)DMA read/write operations on a drive.
 */
int sis5513_dmaproc (ide_dma_action_t func, ide_drive_t *drive)
{
	switch (func) {
		case ide_dma_check:
			config_drive_art_rwp(drive);
			config_art_rwp_pio(drive, 5);
			return config_drive_xfer_rate(drive);
		default:
			break;
	}
	return ide_dmaproc(func, drive);	/* use standard DMA stuff */
}
#endif /* CONFIG_BLK_DEV_IDEDMA */

unsigned int __init pci_init_sis5513 (struct pci_dev *dev, const char *name)
{
	struct pci_dev *host;
	int i = 0;
	byte latency = 0;

	pci_read_config_byte(dev, PCI_LATENCY_TIMER, &latency);

	for (i = 0; i < ARRAY_SIZE (SiSHostChipInfo) && !host_dev; i++) {
		host = pci_find_device (PCI_VENDOR_ID_SI,
					SiSHostChipInfo[i].host_id,
					NULL);
		if (!host)
			continue;

		host_dev = host;
		printk(SiSHostChipInfo[i].name);
		printk("\n");
		if (SiSHostChipInfo[i].flags & SIS5513_FLAG_LATENCY) {
			if (latency != 0x10)
				pci_write_config_byte(dev, PCI_LATENCY_TIMER, 0x10);
		}
	}

	if (host_dev) {
		byte reg52h = 0;

		pci_read_config_byte(dev, 0x52, &reg52h);
		if (!(reg52h & 0x04)) {
			/* set IDE controller to operate in Compabitility mode only */
			pci_write_config_byte(dev, 0x52, reg52h|0x04);
		}
#if defined(DISPLAY_SIS_TIMINGS) && defined(CONFIG_PROC_FS)
		if (!sis_proc) {
			sis_proc = 1;
			bmide_dev = dev;
			sis_display_info = &sis_get_info;
		}
#endif /* defined(DISPLAY_SIS_TIMINGS) && defined(CONFIG_PROC_FS) */
	}
	return 0;
}

unsigned int __init ata66_sis5513 (ide_hwif_t *hwif)
{
	byte reg48h = 0, ata66 = 0;
	byte mask = hwif->channel ? 0x20 : 0x10;
	pci_read_config_byte(hwif->pci_dev, 0x48, &reg48h);

	if (host_dev) {
		switch(host_dev->device) {
			case PCI_DEVICE_ID_SI_530:
			case PCI_DEVICE_ID_SI_540:
			case PCI_DEVICE_ID_SI_620:
			case PCI_DEVICE_ID_SI_630:
			case PCI_DEVICE_ID_SI_730:
				ata66 = (reg48h & mask) ? 0 : 1;
			default:
				break;
		}
	}
        return (ata66);
}

void __init ide_init_sis5513 (ide_hwif_t *hwif)
{

	hwif->irq = hwif->channel ? 15 : 14;

	hwif->tuneproc = &sis5513_tune_drive;
	hwif->speedproc = &sis5513_tune_chipset;

	if (!(hwif->dma_base))
		return;

	if (host_dev) {
		switch(host_dev->device) {
#ifdef CONFIG_BLK_DEV_IDEDMA
			case PCI_DEVICE_ID_SI_530:
			case PCI_DEVICE_ID_SI_540:
			case PCI_DEVICE_ID_SI_620:
			case PCI_DEVICE_ID_SI_630:
			case PCI_DEVICE_ID_SI_730:
			case PCI_DEVICE_ID_SI_5600:
			case PCI_DEVICE_ID_SI_5597:
			case PCI_DEVICE_ID_SI_5591:
				hwif->autodma = 1;
				hwif->dmaproc = &sis5513_dmaproc;
				break;
#endif /* CONFIG_BLK_DEV_IDEDMA */
			default:
				hwif->autodma = 0;
				break;
		}
	}
	return;
}
