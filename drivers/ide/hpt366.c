/*
 * linux/drivers/ide/hpt366.c		Version 0.18	June. 9, 2000
 *
 * Copyright (C) 1999-2000		Andre Hedrick <andre@linux-ide.org>
 * May be copied or modified under the terms of the GNU General Public License
 *
 * Thanks to HighPoint Technologies for their assistance, and hardware.
 * Special Thanks to Jon Burchmore in SanDiego for the deep pockets, his
 * donation of an ABit BP6 mainboard, processor, and memory acellerated
 * development and support.
 *
 * Note that final HPT370 support was done by force extraction of GPL.
 *
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

#define DISPLAY_HPT366_TIMINGS

#if defined(DISPLAY_HPT366_TIMINGS) && defined(CONFIG_PROC_FS)
#include <linux/stat.h>
#include <linux/proc_fs.h>
#endif  /* defined(DISPLAY_HPT366_TIMINGS) && defined(CONFIG_PROC_FS) */

extern char *ide_dmafunc_verbose(ide_dma_action_t dmafunc);

const char *quirk_drives[] = {
	"QUANTUM FIREBALLlct08 08",
	"QUANTUM FIREBALLP KA6.4",
	"QUANTUM FIREBALLP LM20.4",
	"QUANTUM FIREBALLP LM20.5",
        NULL
};

const char *bad_ata100_5[] = {
	NULL
};

const char *bad_ata66_4[] = {
	"WDC AC310200R",
	NULL
};

const char *bad_ata66_3[] = {
	"WDC AC310200R",
	NULL
};

const char *bad_ata33[] = {
	"Maxtor 92720U8", "Maxtor 92040U6", "Maxtor 91360U4", "Maxtor 91020U3", "Maxtor 90845U3", "Maxtor 90650U2",
	"Maxtor 91360D8", "Maxtor 91190D7", "Maxtor 91020D6", "Maxtor 90845D5", "Maxtor 90680D4", "Maxtor 90510D3", "Maxtor 90340D2",
	"Maxtor 91152D8", "Maxtor 91008D7", "Maxtor 90845D6", "Maxtor 90840D6", "Maxtor 90720D5", "Maxtor 90648D5", "Maxtor 90576D4",
	"Maxtor 90510D4",
	"Maxtor 90432D3", "Maxtor 90288D2", "Maxtor 90256D2",
	"Maxtor 91000D8", "Maxtor 90910D8", "Maxtor 90875D7", "Maxtor 90840D7", "Maxtor 90750D6", "Maxtor 90625D5", "Maxtor 90500D4",
	"Maxtor 91728D8", "Maxtor 91512D7", "Maxtor 91303D6", "Maxtor 91080D5", "Maxtor 90845D4", "Maxtor 90680D4", "Maxtor 90648D3", "Maxtor 90432D2",
	NULL
};

struct chipset_bus_clock_list_entry {
	byte		xfer_speed;
	unsigned int	chipset_settings_write;
	unsigned int	chipset_settings_read;
};

struct chipset_bus_clock_list_entry forty_base [] = {

	{	XFER_UDMA_4,	0x900fd943,	0x900fd943	},
	{	XFER_UDMA_3,	0x900ad943,	0x900ad943	},
	{	XFER_UDMA_2,	0x900bd943,	0x900bd943	},
	{	XFER_UDMA_1,	0x9008d943,	0x9008d943	},
	{	XFER_UDMA_0,	0x9008d943,	0x9008d943	},

	{	XFER_MW_DMA_2,	0xa008d943,	0xa008d943	},
	{	XFER_MW_DMA_1,	0xa010d955,	0xa010d955	},
	{	XFER_MW_DMA_0,	0xa010d9fc,	0xa010d9fc	},

	{	XFER_PIO_4,	0xc008d963,	0xc008d963	},
	{	XFER_PIO_3,	0xc010d974,	0xc010d974	},
	{	XFER_PIO_2,	0xc010d997,	0xc010d997	},
	{	XFER_PIO_1,	0xc010d9c7,	0xc010d9c7	},
	{	XFER_PIO_0,	0xc018d9d9,	0xc018d9d9	},
	{	0,		0x0120d9d9,	0x0120d9d9	}
};

struct chipset_bus_clock_list_entry thirty_three_base [] = {

	{	XFER_UDMA_4,	0x90c9a731,	0x90c9a731	},
	{	XFER_UDMA_3,	0x90cfa731,	0x90cfa731	},
	{	XFER_UDMA_2,	0x90caa731,	0x90caa731	},
	{	XFER_UDMA_1,	0x90cba731,	0x90cba731	},
	{	XFER_UDMA_0,	0x90c8a731,	0x90c8a731	},

	{	XFER_MW_DMA_2,	0xa0c8a731,	0xa0c8a731	},
	{	XFER_MW_DMA_1,	0xa0c8a732,	0xa0c8a732	},	/* 0xa0c8a733 */
	{	XFER_MW_DMA_0,	0xa0c8a797,	0xa0c8a797	},

	{	XFER_PIO_4,	0xc0c8a731,	0xc0c8a731	},
	{	XFER_PIO_3,	0xc0c8a742,	0xc0c8a742	},
	{	XFER_PIO_2,	0xc0d0a753,	0xc0d0a753	},
	{	XFER_PIO_1,	0xc0d0a7a3,	0xc0d0a7a3	},	/* 0xc0d0a793 */
	{	XFER_PIO_0,	0xc0d0a7aa,	0xc0d0a7aa	},	/* 0xc0d0a7a7 */
	{	0,		0x0120a7a7,	0x0120a7a7	}
};

struct chipset_bus_clock_list_entry twenty_five_base [] = {

	{	XFER_UDMA_4,	0x90c98521,	0x90c98521	},
	{	XFER_UDMA_3,	0x90cf8521,	0x90cf8521	},
	{	XFER_UDMA_2,	0x90cf8521,	0x90cf8521	},
	{	XFER_UDMA_1,	0x90cb8521,	0x90cb8521	},
	{	XFER_UDMA_0,	0x90cb8521,	0x90cb8521	},

	{	XFER_MW_DMA_2,	0xa0ca8521,	0xa0ca8521	},
	{	XFER_MW_DMA_1,	0xa0ca8532,	0xa0ca8532	},
	{	XFER_MW_DMA_0,	0xa0ca8575,	0xa0ca8575	},

	{	XFER_PIO_4,	0xc0ca8521,	0xc0ca8521	},
	{	XFER_PIO_3,	0xc0ca8532,	0xc0ca8532	},
	{	XFER_PIO_2,	0xc0ca8542,	0xc0ca8542	},
	{	XFER_PIO_1,	0xc0d08572,	0xc0d08572	},
	{	XFER_PIO_0,	0xc0d08585,	0xc0d08585	},
	{	0,		0x01208585,	0x01208585	}
};

struct chipset_bus_clock_list_entry thirty_three_base_hpt370[] = {
	{	XFER_UDMA_5,	0x1A85F442,	0x16454e31	},
	{	XFER_UDMA_4,	0x16454e31,	0x16454e31	},
	{	XFER_UDMA_3,	0x166d4e31,	0x166d4e31	},
	{	XFER_UDMA_2,	0x16494e31,	0x16494e31	},
	{	XFER_UDMA_1,	0x164d4e31,	0x164d4e31	},
	{	XFER_UDMA_0,	0x16514e31,	0x16514e31	},

	{	XFER_MW_DMA_2,	0x26514e21,	0x26514e21	},
	{	XFER_MW_DMA_1,	0x26514e33,	0x26514e33	},
	{	XFER_MW_DMA_0,	0x26514e97,	0x26514e97	},

	{	XFER_PIO_4,	0x06514e21,	0x06514e21	},
	{	XFER_PIO_3,	0x06514e22,	0x06514e22	},
	{	XFER_PIO_2,	0x06514e33,	0x06514e33	},
	{	XFER_PIO_1,	0x06914e43,	0x06914e43	},
	{	XFER_PIO_0,	0x06914e57,	0x06914e57	},
	{	0,		0x06514e57,	0x06514e57	}
};

#define HPT366_DEBUG_DRIVE_INFO		0
#define HPT370_ALLOW_ATA100_5		1
#define HPT366_ALLOW_ATA66_4		1
#define HPT366_ALLOW_ATA66_3		1

#if defined(DISPLAY_HPT366_TIMINGS) && defined(CONFIG_PROC_FS)
static int hpt366_get_info(char *, char **, off_t, int);
extern int (*hpt366_display_info)(char *, char **, off_t, int); /* ide-proc.c */
extern char *ide_media_verbose(ide_drive_t *);
static struct pci_dev *bmide_dev;
static struct pci_dev *bmide2_dev;

static int hpt366_get_info (char *buffer, char **addr, off_t offset, int count)
{
	char *p		= buffer;
	u32 bibma	= bmide_dev->resource[4].start;
	u32 bibma2 	= bmide2_dev->resource[4].start;
	char *chipset_names[] = {"HPT366", "HPT366", "HPT368", "HPT370", "HPT370A"};
	u8  c0 = 0, c1 = 0;
	u32 class_rev;

	pci_read_config_dword(bmide_dev, PCI_CLASS_REVISION, &class_rev);
	class_rev &= 0xff;

        /*
         * at that point bibma+0x2 et bibma+0xa are byte registers
         * to investigate:
         */
	c0 = inb_p((unsigned short)bibma + 0x02);
	if (bmide2_dev)
		c1 = inb_p((unsigned short)bibma2 + 0x02);

	p += sprintf(p, "\n                                %s Chipset.\n", chipset_names[class_rev]);
	p += sprintf(p, "--------------- Primary Channel ---------------- Secondary Channel -------------\n");
	p += sprintf(p, "                %sabled                         %sabled\n",
			(c0&0x80) ? "dis" : " en",
			(c1&0x80) ? "dis" : " en");
	p += sprintf(p, "--------------- drive0 --------- drive1 -------- drive0 ---------- drive1 ------\n");
	p += sprintf(p, "DMA enabled:    %s              %s             %s               %s\n",
			(c0&0x20) ? "yes" : "no ", (c0&0x40) ? "yes" : "no ",
			(c1&0x20) ? "yes" : "no ", (c1&0x40) ? "yes" : "no " );

	p += sprintf(p, "UDMA\n");
	p += sprintf(p, "DMA\n");
	p += sprintf(p, "PIO\n");

	return p-buffer;/* => must be less than 4k! */
}
#endif  /* defined(DISPLAY_HPT366_TIMINGS) && defined(CONFIG_PROC_FS) */

byte hpt366_proc = 0;

extern char *ide_xfer_verbose (byte xfer_rate);
byte hpt363_shared_irq = 0;
byte hpt363_shared_pin = 0;

static unsigned int pci_rev_check_hpt3xx (struct pci_dev *dev)
{
	unsigned int class_rev;
	pci_read_config_dword(dev, PCI_CLASS_REVISION, &class_rev);
	class_rev &= 0xff;
	return ((int) (class_rev > 0x02) ? 1 : 0);
}

static unsigned int pci_rev2_check_hpt3xx (struct pci_dev *dev)
{
	unsigned int class_rev;
	pci_read_config_dword(dev, PCI_CLASS_REVISION, &class_rev);
	class_rev &= 0xff;
	return ((int) (class_rev > 0x01) ? 1 : 0);
}

static int check_in_drive_lists (ide_drive_t *drive, const char **list)
{
	struct hd_driveid *id = drive->id;

	if (quirk_drives == list) {
		while (*list) {
			if (strstr(id->model, *list++)) {
				return 1;
			}
		}
	} else {
		while (*list) {
			if (!strcmp(*list++,id->model)) {
				return 1;
			}
		}
	}
	return 0;
}

static unsigned int pci_bus_clock_list (byte speed, int direction, struct chipset_bus_clock_list_entry * chipset_table)
{
	for ( ; chipset_table->xfer_speed ; chipset_table++)
		if (chipset_table->xfer_speed == speed) {
			return (direction) ? chipset_table->chipset_settings_write : chipset_table->chipset_settings_read;
		}
	return (direction) ? chipset_table->chipset_settings_write : chipset_table->chipset_settings_read;
}

static void hpt366_tune_chipset (ide_drive_t *drive, byte speed, int direction)
{
	byte regtime		= (drive->select.b.unit & 0x01) ? 0x44 : 0x40;
	byte regfast		= (HWIF(drive)->channel) ? 0x55 : 0x51;
			/*
			 * since the channel is always 0 it does not matter.
			 */

	unsigned int reg1	= 0;
	unsigned int reg2	= 0;
	byte drive_fast		= 0;

	/*
	 * Disable the "fast interrupt" prediction.
	 */
	pci_read_config_byte(HWIF(drive)->pci_dev, regfast, &drive_fast);
	if (drive_fast & 0x02)
		pci_write_config_byte(HWIF(drive)->pci_dev, regfast, drive_fast & ~0x20);

	pci_read_config_dword(HWIF(drive)->pci_dev, regtime, &reg1);
	/* detect bus speed by looking at control reg timing: */
	switch((reg1 >> 8) & 7) {
		case 5:
			reg2 = pci_bus_clock_list(speed, direction, forty_base);
			break;
		case 9:
			reg2 = pci_bus_clock_list(speed, direction, twenty_five_base);
			break;
		default:
		case 7:
			reg2 = pci_bus_clock_list(speed, direction, thirty_three_base);
			break;
	}
	/*
	 * Disable on-chip PIO FIFO/buffer (to avoid problems handling I/O errors later)
	 */
	if (speed >= XFER_MW_DMA_0) {
		reg2 = (reg2 & ~0xc0000000) | (reg1 & 0xc0000000);
	} else {
		reg2 = (reg2 & ~0x30070000) | (reg1 & 0x30070000);
	}	
	reg2 &= ~0x80000000;

	pci_write_config_dword(HWIF(drive)->pci_dev, regtime, reg2);
}

static void hpt370_tune_chipset (ide_drive_t *drive, byte speed, int direction)
{
	byte regfast		= (HWIF(drive)->channel) ? 0x55 : 0x51;
	byte reg5bh		= (speed != XFER_UDMA_5) ? 0x22 : (direction) ? 0x20 : 0x22;
	unsigned int list_conf	= pci_bus_clock_list(speed, direction, thirty_three_base_hpt370);
	unsigned int drive_conf = 0;
	unsigned int conf_mask	= (speed >= XFER_MW_DMA_0) ? 0xc0000000 : 0x30070000;
	byte drive_pci		= 0;
	byte drive_fast		= 0;

	switch (drive->dn) {
		case 0: drive_pci = 0x40; break;
		case 1: drive_pci = 0x44; break;
		case 2: drive_pci = 0x48; break;
		case 3: drive_pci = 0x4c; break;
		default: return;
	}
	/*
	 * Disable the "fast interrupt" prediction.
	 */
	pci_read_config_byte(HWIF(drive)->pci_dev, regfast, &drive_fast);
	if (drive_fast & 0x80)
		pci_write_config_byte(HWIF(drive)->pci_dev, regfast, drive_fast & ~0x80);

	pci_read_config_dword(HWIF(drive)->pci_dev, drive_pci, &drive_conf);
	pci_write_config_byte(HWIF(drive)->pci_dev, 0x5b, reg5bh);

	list_conf = (list_conf & ~conf_mask) | (drive_conf & conf_mask);
	/*
	 * Disable on-chip PIO FIFO/buffer (to avoid problems handling I/O errors later)
	 */
	list_conf &= ~0x80000000;

	pci_write_config_dword(HWIF(drive)->pci_dev, drive_pci, list_conf);
}

static int hpt3xx_tune_chipset (ide_drive_t *drive, byte speed)
{
	if ((drive->media != ide_disk) && (speed < XFER_SW_DMA_0))
		return -1;

	if (!drive->init_speed)
		drive->init_speed = speed;

	if (pci_rev_check_hpt3xx(HWIF(drive)->pci_dev)) {
		hpt370_tune_chipset(drive, speed, 0);
        } else {
                hpt366_tune_chipset(drive, speed, 0);
        }
	drive->current_speed = speed;
	return ((int) ide_config_drive_speed(drive, speed));
}

static void config_chipset_for_pio (ide_drive_t *drive)
{
	unsigned short eide_pio_timing[6] = {960, 480, 240, 180, 120, 90};
	unsigned short xfer_pio = drive->id->eide_pio_modes;
	byte	timing, speed, pio;

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
	(void) hpt3xx_tune_chipset(drive, speed);
}

static void hpt3xx_tune_drive (ide_drive_t *drive, byte pio)
{
	byte speed;
	switch(pio) {
		case 4:		speed = XFER_PIO_4;break;
		case 3:		speed = XFER_PIO_3;break;
		case 2:		speed = XFER_PIO_2;break;
		case 1:		speed = XFER_PIO_1;break;
		default:	speed = XFER_PIO_0;break;
	}
	(void) hpt3xx_tune_chipset(drive, speed);
}

#ifdef CONFIG_BLK_DEV_IDEDMA
/*
 * This allows the configuration of ide_pci chipset registers
 * for cards that learn about the drive's UDMA, DMA, PIO capabilities
 * after the drive is reported by the OS.  Initally for designed for
 * HPT366 UDMA chipset by HighPoint|Triones Technologies, Inc.
 *
 * check_in_drive_lists(drive, bad_ata66_4)
 * check_in_drive_lists(drive, bad_ata66_3)
 * check_in_drive_lists(drive, bad_ata33)
 *
 */
static int config_chipset_for_dma (ide_drive_t *drive)
{
	struct hd_driveid *id	= drive->id;
	byte speed		= 0x00;
	byte ultra66		= eighty_ninty_three(drive);
	int  rval;

	if ((drive->media != ide_disk) && (speed < XFER_SW_DMA_0))
		return ((int) ide_dma_off_quietly);

	if ((id->dma_ultra & 0x0020) &&
	    (!check_in_drive_lists(drive, bad_ata100_5)) &&
	    (HPT370_ALLOW_ATA100_5) &&
	    (pci_rev_check_hpt3xx(HWIF(drive)->pci_dev)) &&
	    (ultra66)) {
		speed = XFER_UDMA_5;
	} else if ((id->dma_ultra & 0x0010) &&
		   (!check_in_drive_lists(drive, bad_ata66_4)) &&
		   (HPT366_ALLOW_ATA66_4) &&
		   (ultra66)) {
		speed = XFER_UDMA_4;
	} else if ((id->dma_ultra & 0x0008) &&
		   (!check_in_drive_lists(drive, bad_ata66_3)) &&
		   (HPT366_ALLOW_ATA66_3) &&
		   (ultra66)) {
		speed = XFER_UDMA_3;
	} else if (id->dma_ultra && (!check_in_drive_lists(drive, bad_ata33))) {
		if (id->dma_ultra & 0x0004) {
			speed = XFER_UDMA_2;
		} else if (id->dma_ultra & 0x0002) {
			speed = XFER_UDMA_1;
		} else if (id->dma_ultra & 0x0001) {
			speed = XFER_UDMA_0;
		}
	} else if (id->dma_mword & 0x0004) {
		speed = XFER_MW_DMA_2;
	} else if (id->dma_mword & 0x0002) {
		speed = XFER_MW_DMA_1;
	} else if (id->dma_mword & 0x0001) {
		speed = XFER_MW_DMA_0;
	} else {
		return ((int) ide_dma_off_quietly);
	}

	(void) hpt3xx_tune_chipset(drive, speed);

	rval = (int)(	((id->dma_ultra >> 11) & 7) ? ide_dma_on :
			((id->dma_ultra >> 8) & 7) ? ide_dma_on :
			((id->dma_mword >> 8) & 7) ? ide_dma_on :
						     ide_dma_off_quietly);
	return rval;
}

int hpt3xx_quirkproc (ide_drive_t *drive)
{
	return ((int) check_in_drive_lists(drive, quirk_drives));
}

void hpt3xx_intrproc (ide_drive_t *drive)
{
	if (drive->quirk_list) {
		/* drives in the quirk_list may not like intr setups/cleanups */
	} else {
		OUT_BYTE((drive)->ctl|2, HWIF(drive)->io_ports[IDE_CONTROL_OFFSET]);
	}
}

void hpt3xx_maskproc (ide_drive_t *drive, int mask)
{
	if (drive->quirk_list) {
		if (pci_rev_check_hpt3xx(HWIF(drive)->pci_dev)) {
			byte reg5a = 0;
			pci_read_config_byte(HWIF(drive)->pci_dev, 0x5a, &reg5a);
			if (((reg5a & 0x10) >> 4) != mask)
				pci_write_config_byte(HWIF(drive)->pci_dev, 0x5a, mask ? (reg5a | 0x10) : (reg5a & ~0x10));
		} else {
			if (mask) {
				disable_irq(HWIF(drive)->irq);
			} else {
				enable_irq(HWIF(drive)->irq);
			}
		}
	} else {
		if (IDE_CONTROL_REG)
			OUT_BYTE(mask ? (drive->ctl | 2) : (drive->ctl & ~2), IDE_CONTROL_REG);
	}
}

void hpt370_rw_proc (ide_drive_t *drive, ide_dma_action_t func)
{
	if ((func != ide_dma_write) || (func != ide_dma_read))
		return;
	hpt370_tune_chipset(drive, drive->current_speed, (func == ide_dma_write));
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
			if (id->dma_mword & 0x0007) {
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

/*
 * hpt366_dmaproc() initiates/aborts (U)DMA read/write operations on a drive.
 *
 * This is specific to the HPT366 UDMA bios chipset
 * by HighPoint|Triones Technologies, Inc.
 */
int hpt366_dmaproc (ide_dma_action_t func, ide_drive_t *drive)
{
	byte reg50h = 0, reg52h = 0, reg5ah = 0, dma_stat = 0;
	unsigned long dma_base = HWIF(drive)->dma_base;

	switch (func) {
		case ide_dma_check:
			return config_drive_xfer_rate(drive);
		case ide_dma_test_irq:	/* returns 1 if dma irq issued, 0 otherwise */
			dma_stat = inb(dma_base+2);
			return (dma_stat & 4) == 4;	/* return 1 if INTR asserted */
		case ide_dma_lostirq:
			pci_read_config_byte(HWIF(drive)->pci_dev, 0x50, &reg50h);
			pci_read_config_byte(HWIF(drive)->pci_dev, 0x52, &reg52h);
			pci_read_config_byte(HWIF(drive)->pci_dev, 0x5a, &reg5ah);
			printk("%s: (%s)  reg50h=0x%02x, reg52h=0x%02x, reg5ah=0x%02x\n",
				drive->name,
				ide_dmafunc_verbose(func),
				reg50h, reg52h, reg5ah);
			if (reg5ah & 0x10)
				pci_write_config_byte(HWIF(drive)->pci_dev, 0x5a, reg5ah & ~0x10);
			break;
		case ide_dma_timeout:
		default:
			break;
	}
	return ide_dmaproc(func, drive);	/* use standard DMA stuff */
}

int hpt370_dmaproc (ide_dma_action_t func, ide_drive_t *drive)
{
	switch (func) {
		case ide_dma_check:
			return config_drive_xfer_rate(drive);
		default:
			break;
	}
	return ide_dmaproc(func, drive);	/* use standard DMA stuff */
}
#endif /* CONFIG_BLK_DEV_IDEDMA */

unsigned int __init pci_init_hpt366 (struct pci_dev *dev, const char *name)
{
	byte test = 0;

	if (dev->resource[PCI_ROM_RESOURCE].start)
		pci_write_config_byte(dev, PCI_ROM_ADDRESS, dev->resource[PCI_ROM_RESOURCE].start | PCI_ROM_ADDRESS_ENABLE);

	pci_read_config_byte(dev, PCI_CACHE_LINE_SIZE, &test);

#if 0
	if (test != 0x08)
		pci_write_config_byte(dev, PCI_CACHE_LINE_SIZE, 0x08);
#else
	if (test != (L1_CACHE_BYTES / 4))
		pci_write_config_byte(dev, PCI_CACHE_LINE_SIZE, (L1_CACHE_BYTES / 4));
#endif

	pci_read_config_byte(dev, PCI_LATENCY_TIMER, &test);
	if (test != 0x78)
		pci_write_config_byte(dev, PCI_LATENCY_TIMER, 0x78);

	pci_read_config_byte(dev, PCI_MIN_GNT, &test);
	if (test != 0x08)
		pci_write_config_byte(dev, PCI_MIN_GNT, 0x08);

	pci_read_config_byte(dev, PCI_MAX_LAT, &test);
	if (test != 0x08)
		pci_write_config_byte(dev, PCI_MAX_LAT, 0x08);

#if defined(DISPLAY_HPT366_TIMINGS) && defined(CONFIG_PROC_FS)
	if (!hpt366_proc) {
		hpt366_proc = 1;
		bmide_dev = dev;
		if (pci_rev_check_hpt3xx(dev))
			bmide2_dev = dev;
		hpt366_display_info = &hpt366_get_info;
	}
	if ((hpt366_proc) && ((dev->devfn - bmide_dev->devfn) == 1)) {
		bmide2_dev = dev;
	}
#endif /* DISPLAY_HPT366_TIMINGS && CONFIG_PROC_FS */

	return dev->irq;
}

unsigned int __init ata66_hpt366 (ide_hwif_t *hwif)
{
	byte ata66 = 0;

	pci_read_config_byte(hwif->pci_dev, 0x5a, &ata66);
#ifdef DEBUG
	printk("HPT366: reg5ah=0x%02x ATA-%s Cable Port%d\n",
		ata66, (ata66 & 0x02) ? "33" : "66",
		PCI_FUNC(hwif->pci_dev->devfn));
#endif /* DEBUG */
	return ((ata66 & 0x02) ? 0 : 1);
}

void __init ide_init_hpt366 (ide_hwif_t *hwif)
{
	hwif->tuneproc	= &hpt3xx_tune_drive;
	hwif->speedproc	= &hpt3xx_tune_chipset;
	hwif->quirkproc	= &hpt3xx_quirkproc;
	hwif->intrproc	= &hpt3xx_intrproc;
	hwif->maskproc	= &hpt3xx_maskproc;

	if (pci_rev2_check_hpt3xx(hwif->pci_dev)) {
		/* do nothing now but will split device types */
	}

#ifdef CONFIG_BLK_DEV_IDEDMA
	if (hwif->dma_base) {
		if (pci_rev_check_hpt3xx(hwif->pci_dev)) {
			byte reg5ah = 0;
			pci_read_config_byte(hwif->pci_dev, 0x5a, &reg5ah);
			if (reg5ah & 0x10)	/* interrupt force enable */
				pci_write_config_byte(hwif->pci_dev, 0x5a, reg5ah & ~0x10);
			hwif->dmaproc = &hpt370_dmaproc;
			hwif->rwproc = &hpt370_rw_proc;
		} else {
			hwif->dmaproc = &hpt366_dmaproc;
		}
		hwif->autodma = 1;
	} else {
		hwif->autodma = 0;
		hwif->drives[0].autotune = 1;
		hwif->drives[1].autotune = 1;
	}
#else /* !CONFIG_BLK_DEV_IDEDMA */
	hwif->drives[0].autotune = 1;
	hwif->drives[1].autotune = 1;
	hwif->autodma = 0;
#endif /* CONFIG_BLK_DEV_IDEDMA */
}

void __init ide_dmacapable_hpt366 (ide_hwif_t *hwif, unsigned long dmabase)
{
	byte masterdma = 0, slavedma = 0;
	byte dma_new = 0, dma_old = inb(dmabase+2);
	byte primary	= hwif->channel ? 0x4b : 0x43;
	byte secondary	= hwif->channel ? 0x4f : 0x47;
	unsigned long flags;

	__save_flags(flags);	/* local CPU only */
	__cli();		/* local CPU only */

	dma_new = dma_old;
	pci_read_config_byte(hwif->pci_dev, primary, &masterdma);
	pci_read_config_byte(hwif->pci_dev, secondary, &slavedma);

	if (masterdma & 0x30)	dma_new |= 0x20;
	if (slavedma & 0x30)	dma_new |= 0x40;
	if (dma_new != dma_old) outb(dma_new, dmabase+2);

	__restore_flags(flags);	/* local CPU only */

	ide_setup_dma(hwif, dmabase, 8);
}
