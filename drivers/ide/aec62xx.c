/*
 * linux/drivers/ide/aec62xx.c		Version 0.09	June. 9, 2000
 *
 * Copyright (C) 1999-2000	Andre Hedrick (andre@linux-ide.org)
 * May be copied or modified under the terms of the GNU General Public License
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

#define DISPLAY_AEC62XX_TIMINGS

#ifndef HIGH_4
#define HIGH_4(H)		((H)=(H>>4))
#endif
#ifndef LOW_4
#define LOW_4(L)		((L)=(L-((L>>4)<<4)))
#endif
#ifndef SPLIT_BYTE
#define SPLIT_BYTE(B,H,L)	((H)=(B>>4), (L)=(B-((B>>4)<<4)))
#endif
#ifndef MAKE_WORD
#define MAKE_WORD(W,HB,LB)	((W)=((HB<<8)+LB))
#endif


#if defined(DISPLAY_AEC62XX_TIMINGS) && defined(CONFIG_PROC_FS)
#include <linux/stat.h>
#include <linux/proc_fs.h>

static int aec62xx_get_info(char *, char **, off_t, int);
extern int (*aec62xx_display_info)(char *, char **, off_t, int); /* ide-proc.c */
extern char *ide_media_verbose(ide_drive_t *);
static struct pci_dev *bmide_dev;

static int aec62xx_get_info (char *buffer, char **addr, off_t offset, int count)
{
	char *p = buffer;

	u32 bibma = pci_resource_start(bmide_dev, 4);
	u8 c0 = 0, c1 = 0;
	u8 art = 0, uart = 0;

	switch(bmide_dev->device) {
		case PCI_DEVICE_ID_ARTOP_ATP850UF:
			p += sprintf(p, "\n                                AEC6210 Chipset.\n");
			break;
		case PCI_DEVICE_ID_ARTOP_ATP860:
			p += sprintf(p, "\n                                AEC6260 No Bios Chipset.\n");
			break;
		case PCI_DEVICE_ID_ARTOP_ATP860R:
			p += sprintf(p, "\n                                AEC6260 Chipset.\n");
			break;
		default:
			p += sprintf(p, "\n                                AEC62?? Chipset.\n");
			break;
	}

        /*
         * at that point bibma+0x2 et bibma+0xa are byte registers
         * to investigate:
         */
	c0 = inb_p((unsigned short)bibma + 0x02);
	c1 = inb_p((unsigned short)bibma + 0x0a);

	p += sprintf(p, "--------------- Primary Channel ---------------- Secondary Channel -------------\n");
	(void) pci_read_config_byte(bmide_dev, 0x4a, &art);
	p += sprintf(p, "                %sabled                         %sabled\n",
		(art&0x02)?" en":"dis",(art&0x04)?" en":"dis");
	p += sprintf(p, "--------------- drive0 --------- drive1 -------- drive0 ---------- drive1 ------\n");
	p += sprintf(p, "DMA enabled:    %s              %s             %s               %s\n",
		(c0&0x20)?"yes":"no ",(c0&0x40)?"yes":"no ",(c1&0x20)?"yes":"no ",(c1&0x40)?"yes":"no ");

	switch(bmide_dev->device) {
		case PCI_DEVICE_ID_ARTOP_ATP850UF:
			(void) pci_read_config_byte(bmide_dev, 0x54, &art);
			p += sprintf(p, "DMA Mode:       %s(%s)          %s(%s)         %s(%s)           %s(%s)\n",
				(c0&0x20)?((art&0x03)?"UDMA":" DMA"):" PIO",
				(art&0x02)?"2":(art&0x01)?"1":"0",
				(c0&0x40)?((art&0x0c)?"UDMA":" DMA"):" PIO",
				(art&0x08)?"2":(art&0x04)?"1":"0",
				(c1&0x20)?((art&0x30)?"UDMA":" DMA"):" PIO",
				(art&0x20)?"2":(art&0x10)?"1":"0",
				(c1&0x40)?((art&0xc0)?"UDMA":" DMA"):" PIO",
				(art&0x80)?"2":(art&0x40)?"1":"0");
			(void) pci_read_config_byte(bmide_dev, 0x40, &art);
			p += sprintf(p, "Active:         0x%02x", art);
			(void) pci_read_config_byte(bmide_dev, 0x42, &art);
			p += sprintf(p, "             0x%02x", art);
			(void) pci_read_config_byte(bmide_dev, 0x44, &art);
			p += sprintf(p, "            0x%02x", art);
			(void) pci_read_config_byte(bmide_dev, 0x46, &art);
			p += sprintf(p, "              0x%02x\n", art);
			(void) pci_read_config_byte(bmide_dev, 0x41, &art);
			p += sprintf(p, "Recovery:       0x%02x", art);
			(void) pci_read_config_byte(bmide_dev, 0x43, &art);
			p += sprintf(p, "             0x%02x", art);
			(void) pci_read_config_byte(bmide_dev, 0x45, &art);
			p += sprintf(p, "            0x%02x", art);
			(void) pci_read_config_byte(bmide_dev, 0x47, &art);
			p += sprintf(p, "              0x%02x\n", art);
			break;
		case PCI_DEVICE_ID_ARTOP_ATP860:
		case PCI_DEVICE_ID_ARTOP_ATP860R:
			(void) pci_read_config_byte(bmide_dev, 0x44, &art);
			p += sprintf(p, "DMA Mode:       %s(%s)          %s(%s)",
				(c0&0x20)?((art&0x07)?"UDMA":" DMA"):" PIO",
				((art&0x06)==0x06)?"4":((art&0x05)==0x05)?"4":((art&0x04)==0x04)?"3":((art&0x03)==0x03)?"2":((art&0x02)==0x02)?"1":((art&0x01)==0x01)?"0":"?",
				(c0&0x40)?((art&0x70)?"UDMA":" DMA"):" PIO",
				((art&0x60)==0x60)?"4":((art&0x50)==0x50)?"4":((art&0x40)==0x40)?"3":((art&0x30)==0x30)?"2":((art&0x20)==0x20)?"1":((art&0x10)==0x10)?"0":"?");
			(void) pci_read_config_byte(bmide_dev, 0x45, &art);
			p += sprintf(p, "         %s(%s)           %s(%s)\n",
				(c1&0x20)?((art&0x07)?"UDMA":" DMA"):" PIO",
				((art&0x06)==0x06)?"4":((art&0x05)==0x05)?"4":((art&0x04)==0x04)?"3":((art&0x03)==0x03)?"2":((art&0x02)==0x02)?"1":((art&0x01)==0x01)?"0":"?",
				(c1&0x40)?((art&0x70)?"UDMA":" DMA"):" PIO",
				((art&0x60)==0x60)?"4":((art&0x50)==0x50)?"4":((art&0x40)==0x40)?"3":((art&0x30)==0x30)?"2":((art&0x20)==0x20)?"1":((art&0x10)==0x10)?"0":"?");
			(void) pci_read_config_byte(bmide_dev, 0x40, &art);
			p += sprintf(p, "Active:         0x%02x", HIGH_4(art));
			(void) pci_read_config_byte(bmide_dev, 0x41, &art);
			p += sprintf(p, "             0x%02x", HIGH_4(art));
			(void) pci_read_config_byte(bmide_dev, 0x42, &art);
			p += sprintf(p, "            0x%02x", HIGH_4(art));
			(void) pci_read_config_byte(bmide_dev, 0x43, &art);
			p += sprintf(p, "              0x%02x\n", HIGH_4(art));
			(void) pci_read_config_byte(bmide_dev, 0x40, &art);
			p += sprintf(p, "Recovery:       0x%02x", LOW_4(art));
			(void) pci_read_config_byte(bmide_dev, 0x41, &art);
			p += sprintf(p, "             0x%02x", LOW_4(art));
			(void) pci_read_config_byte(bmide_dev, 0x42, &art);
			p += sprintf(p, "            0x%02x", LOW_4(art));
			(void) pci_read_config_byte(bmide_dev, 0x43, &art);
			p += sprintf(p, "              0x%02x\n", LOW_4(art));
			(void) pci_read_config_byte(bmide_dev, 0x49, &uart);
			p += sprintf(p, "reg49h = 0x%02x ", uart);
			(void) pci_read_config_byte(bmide_dev, 0x4a, &uart);
			p += sprintf(p, "reg4ah = 0x%02x\n", uart);
			break;
		default:
			break;
	}

	return p-buffer;/* => must be less than 4k! */
}
#endif	/* defined(DISPLAY_AEC62xx_TIMINGS) && defined(CONFIG_PROC_FS) */

byte aec62xx_proc = 0;

#ifdef CONFIG_AEC62XX_TUNING

struct chipset_bus_clock_list_entry {
	byte		xfer_speed;

	byte		chipset_settings_34;
	byte		ultra_settings_34;

	byte		chipset_settings_33;
	byte		ultra_settings_33;
};

struct chipset_bus_clock_list_entry aec62xx_base [] = {
#ifdef CONFIG_BLK_DEV_IDEDMA
	{	XFER_UDMA_4,	0x41,	0x04,	0x31,	0x05	},
	{	XFER_UDMA_3,	0x41,	0x03,	0x31,	0x04	},
	{	XFER_UDMA_2,	0x41,	0x02,	0x31,	0x03	},
	{	XFER_UDMA_1,	0x41,	0x01,	0x31,	0x02	},
	{	XFER_UDMA_0,	0x41,	0x01,	0x31,	0x01	},

	{	XFER_MW_DMA_2,	0x41,	0x00,	0x31,	0x00	},
	{	XFER_MW_DMA_1,	0x42,	0x00,	0x31,	0x00	},
	{	XFER_MW_DMA_0,	0x7a,	0x00,	0x0a,	0x00	},
#endif /* CONFIG_BLK_DEV_IDEDMA */
	{	XFER_PIO_4,	0x41,	0x00,	0x31,	0x00	},
	{	XFER_PIO_3,	0x43,	0x00,	0x33,	0x00	},
	{	XFER_PIO_2,	0x78,	0x00,	0x08,	0x00	},
	{	XFER_PIO_1,	0x7a,	0x00,	0x0a,	0x00	},
	{	XFER_PIO_0,	0x70,	0x00,	0x00,	0x00	},
	{	0,		0x00,	0x00,	0x00,	0x00	}
};

extern char *ide_xfer_verbose (byte xfer_rate);

/*
 * TO DO: active tuning and correction of cards without a bios.
 */

static byte pci_bus_clock_list (byte speed, struct chipset_bus_clock_list_entry * chipset_table)
{
	int bus_speed = system_bus_clock();

	for ( ; chipset_table->xfer_speed ; chipset_table++)
		if (chipset_table->xfer_speed == speed) {
			return ((byte) ((bus_speed <= 33) ? chipset_table->chipset_settings_33 : chipset_table->chipset_settings_34));
		}
	return 0x00;
}

static byte pci_bus_clock_list_ultra (byte speed, struct chipset_bus_clock_list_entry * chipset_table)
{
	int bus_speed = system_bus_clock();

	for ( ; chipset_table->xfer_speed ; chipset_table++)
		if (chipset_table->xfer_speed == speed) {
			return ((byte) ((bus_speed <= 33) ? chipset_table->ultra_settings_33 : chipset_table->ultra_settings_34));
		}
	return 0x00;
}

static int aec6210_tune_chipset (ide_drive_t *drive, byte speed)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;
	int err			= 0;
	unsigned short d_conf	= 0x0000;
	byte ultra		= 0x00;
	byte ultra_conf		= 0x00;
	byte tmp0		= 0x00;
	byte tmp1		= 0x00;
	byte tmp2		= 0x00;
	unsigned long flags;

	__save_flags(flags);	/* local CPU only */
	__cli();		/* local CPU only */

	pci_read_config_word(dev, 0x40|(2*drive->dn), &d_conf);
	tmp0 = pci_bus_clock_list(speed, aec62xx_base);
	SPLIT_BYTE(tmp0,tmp1,tmp2);
	MAKE_WORD(d_conf,tmp1,tmp2);
	pci_write_config_word(dev, 0x40|(2*drive->dn), d_conf);

	tmp1 = 0x00;
	tmp2 = 0x00;
	pci_read_config_byte(dev, 0x54, &ultra);
	tmp1 = ((0x00 << (2*drive->dn)) | (ultra & ~(3 << (2*drive->dn))));
	ultra_conf = pci_bus_clock_list_ultra(speed, aec62xx_base);
	tmp2 = ((ultra_conf << (2*drive->dn)) | (tmp1 & ~(3 << (2*drive->dn))));
	pci_write_config_byte(dev, 0x54, tmp2);

	__restore_flags(flags);	/* local CPU only */

	err = ide_config_drive_speed(drive, speed);
	return(err);
}

static int aec6260_tune_chipset (ide_drive_t *drive, byte speed)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;
	byte unit		= (drive->select.b.unit & 0x01);
	byte ultra_pci		= hwif->channel ? 0x45 : 0x44;
	int err			= 0;
	byte drive_conf		= 0x00;
	byte ultra_conf		= 0x00;
	byte ultra		= 0x00;
	byte tmp1		= 0x00;
	byte tmp2		= 0x00;
	unsigned long flags;

	__save_flags(flags);	/* local CPU only */
	__cli();		/* local CPU only */

	pci_read_config_byte(dev, 0x40|drive->dn, &drive_conf);
	drive_conf = pci_bus_clock_list(speed, aec62xx_base);
	pci_write_config_byte(dev, 0x40|drive->dn, drive_conf);

	pci_read_config_byte(dev, ultra_pci, &ultra);
	tmp1 = ((0x00 << (4*unit)) | (ultra & ~(7 << (4*unit))));
	ultra_conf = pci_bus_clock_list_ultra(speed, aec62xx_base);
	tmp2 = ((ultra_conf << (4*unit)) | (tmp1 & ~(7 << (4*unit))));
	pci_write_config_byte(dev, ultra_pci, tmp2);
	__restore_flags(flags);	/* local CPU only */

	if (!drive->init_speed)
		drive->init_speed = speed;

	err = ide_config_drive_speed(drive, speed);
	drive->current_speed = speed;
	return(err);
}


static int aec62xx_tune_chipset (ide_drive_t *drive, byte speed)
{
	if (HWIF(drive)->pci_dev->device == PCI_DEVICE_ID_ARTOP_ATP850UF) {
		return ((int) aec6210_tune_chipset(drive, speed));
	} else {
		return ((int) aec6260_tune_chipset(drive, speed));
	}
}

#ifdef CONFIG_BLK_DEV_IDEDMA
static int config_aec6210_chipset_for_dma (ide_drive_t *drive, byte ultra)
{
	struct hd_driveid *id	= drive->id;
	ide_hwif_t *hwif	= HWIF(drive);
	byte unit		= (drive->select.b.unit & 0x01);
	unsigned long dma_base	= hwif->dma_base;
	byte speed		= -1;

	if (drive->media != ide_disk)
		return ((int) ide_dma_off_quietly);

	if (((id->dma_ultra & 0x0010) ||
	     (id->dma_ultra & 0x0008) ||
	     (id->dma_ultra & 0x0004)) && (ultra)) {
		speed = XFER_UDMA_2;
	} else if ((id->dma_ultra & 0x0002) && (ultra)) {
		speed = XFER_UDMA_1;
	} else if ((id->dma_ultra & 0x0001) && (ultra)) {
		speed = XFER_UDMA_0;
	} else if (id->dma_mword & 0x0004) {
		speed = XFER_MW_DMA_2;
	} else if (id->dma_mword & 0x0002) {
		speed = XFER_MW_DMA_1;
	} else if (id->dma_mword & 0x0001) {
		speed = XFER_MW_DMA_0;
	} else if (id->dma_1word & 0x0004) {
		speed = XFER_SW_DMA_2;
	} else if (id->dma_1word & 0x0002) {
		speed = XFER_SW_DMA_1;
	} else if (id->dma_1word & 0x0001) {
		speed = XFER_SW_DMA_0;
	} else {
 		return ((int) ide_dma_off_quietly);
 	}

	outb(inb(dma_base+2) & ~(1<<(5+unit)), dma_base+2);
	(void) aec6210_tune_chipset(drive, speed);

	return ((int)	((id->dma_ultra >> 11) & 3) ? ide_dma_off :
			((id->dma_ultra >> 8) & 7) ? ide_dma_on :
			((id->dma_mword >> 8) & 7) ? ide_dma_on :
			((id->dma_1word >> 8) & 7) ? ide_dma_on :
						     ide_dma_off_quietly);
}

static int config_aec6260_chipset_for_dma (ide_drive_t *drive, byte ultra)
{
	struct hd_driveid *id	= drive->id;
	ide_hwif_t *hwif	= HWIF(drive);
	byte unit		= (drive->select.b.unit & 0x01);
	unsigned long dma_base	= hwif->dma_base;
	byte speed		= -1;
	byte ultra66		= eighty_ninty_three(drive);

	if (drive->media != ide_disk)
		return ((int) ide_dma_off_quietly);

	if ((id->dma_ultra & 0x0010) && (ultra) && (ultra66)) {
		speed = XFER_UDMA_4;
	} else if ((id->dma_ultra & 0x0008) && (ultra) && (ultra66)) {
		speed = XFER_UDMA_3;
	} else if ((id->dma_ultra & 0x0004) && (ultra)) {
		speed = XFER_UDMA_2;
	} else if ((id->dma_ultra & 0x0002) && (ultra)) {
		speed = XFER_UDMA_1;
	} else if ((id->dma_ultra & 0x0001) && (ultra)) {
		speed = XFER_UDMA_0;
	} else if (id->dma_mword & 0x0004) {
		speed = XFER_MW_DMA_2;
	} else if (id->dma_mword & 0x0002) {
		speed = XFER_MW_DMA_1;
	} else if (id->dma_mword & 0x0001) {
		speed = XFER_MW_DMA_0;
	} else if (id->dma_1word & 0x0004) {
		speed = XFER_SW_DMA_2;
	} else if (id->dma_1word & 0x0002) {
		speed = XFER_SW_DMA_1;
	} else if (id->dma_1word & 0x0001) {
		speed = XFER_SW_DMA_0;
	} else {
		return ((int) ide_dma_off_quietly);
	}

	outb(inb(dma_base+2) & ~(1<<(5+unit)), dma_base+2);
	(void) aec6260_tune_chipset(drive, speed);

	return ((int)	((id->dma_ultra >> 11) & 3) ? ide_dma_on :
			((id->dma_ultra >> 8) & 7) ? ide_dma_on :
			((id->dma_mword >> 8) & 7) ? ide_dma_on :
			((id->dma_1word >> 8) & 7) ? ide_dma_on :
						     ide_dma_off_quietly);
}

static int config_chipset_for_dma (ide_drive_t *drive, byte ultra)
{
	switch(HWIF(drive)->pci_dev->device) {	
		case PCI_DEVICE_ID_ARTOP_ATP850UF:
			return config_aec6210_chipset_for_dma(drive, ultra);
		case PCI_DEVICE_ID_ARTOP_ATP860:
		case PCI_DEVICE_ID_ARTOP_ATP860R:
			return config_aec6260_chipset_for_dma(drive, ultra);
		default:
			return ((int) ide_dma_off_quietly);
	}
}

#endif /* CONFIG_BLK_DEV_IDEDMA */

static void aec62xx_tune_drive (ide_drive_t *drive, byte pio)
{
	byte speed;
	byte new_pio = XFER_PIO_0 + ide_get_best_pio_mode(drive, 255, 5, NULL);

	switch(pio) {
		case 5:		speed = new_pio; break;
		case 4:		speed = XFER_PIO_4; break;
		case 3:		speed = XFER_PIO_3; break;
		case 2:		speed = XFER_PIO_2; break;
		case 1:		speed = XFER_PIO_1; break;
		default:	speed = XFER_PIO_0; break;
	}

	switch(HWIF(drive)->pci_dev->device) {
		case PCI_DEVICE_ID_ARTOP_ATP850UF:
			(void) aec6210_tune_chipset(drive, speed);
		case PCI_DEVICE_ID_ARTOP_ATP860:
		case PCI_DEVICE_ID_ARTOP_ATP860R:
			(void) aec6260_tune_chipset(drive, speed);
		default:
			break;
        }
}

#ifdef CONFIG_BLK_DEV_IDEDMA
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
		} else if (ide_dmaproc(ide_dma_good_drive, drive)) {
			if (id->eide_dma_time > 150) {
				goto no_dma_set;
			}
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
		aec62xx_tune_drive(drive, 5);
	}
	return HWIF(drive)->dmaproc(dma_func, drive);
}

/*
 * aec62xx_dmaproc() initiates/aborts (U)DMA read/write operations on a drive.
 */
int aec62xx_dmaproc (ide_dma_action_t func, ide_drive_t *drive)
{
	switch (func) {
		case ide_dma_check:
			return config_drive_xfer_rate(drive);
		case ide_dma_lostirq:
		case ide_dma_timeout:
			switch(HWIF(drive)->pci_dev->device) {
				case PCI_DEVICE_ID_ARTOP_ATP860:
				case PCI_DEVICE_ID_ARTOP_ATP860R:
//					{
//						int i = 0;
//						byte reg49h = 0;
//						pci_read_config_byte(HWIF(drive)->pci_dev, 0x49, &reg49h);
//						for (i=0;i<256;i++)
//							pci_write_config_byte(HWIF(drive)->pci_dev, 0x49, reg49h|0x10);
//						pci_write_config_byte(HWIF(drive)->pci_dev, 0x49, reg49h & ~0x10);
//					}
//					return 0;
				default:
					break;
			}
		default:
			break;
	}
	return ide_dmaproc(func, drive);	/* use standard DMA stuff */
}
#endif /* CONFIG_BLK_DEV_IDEDMA */
#endif /* CONFIG_AEC62XX_TUNING */

unsigned int __init pci_init_aec62xx (struct pci_dev *dev, const char *name)
{
	if (dev->resource[PCI_ROM_RESOURCE].start) {
		pci_write_config_dword(dev, PCI_ROM_ADDRESS, dev->resource[PCI_ROM_RESOURCE].start | PCI_ROM_ADDRESS_ENABLE);
		printk("%s: ROM enabled at 0x%08lx\n", name, dev->resource[PCI_ROM_RESOURCE].start);
	}

#if defined(DISPLAY_AEC62XX_TIMINGS) && defined(CONFIG_PROC_FS)
	if (!aec62xx_proc) {
		aec62xx_proc = 1;
		bmide_dev = dev;
		aec62xx_display_info = &aec62xx_get_info;
	}
#endif /* DISPLAY_AEC62XX_TIMINGS && CONFIG_PROC_FS */

	return dev->irq;
}

unsigned int __init ata66_aec62xx (ide_hwif_t *hwif)
{
	byte mask	= hwif->channel ? 0x02 : 0x01;
	byte ata66	= 0;

	pci_read_config_byte(hwif->pci_dev, 0x49, &ata66);
	return ((ata66 & mask) ? 0 : 1);
}

void __init ide_init_aec62xx (ide_hwif_t *hwif)
{
#ifdef CONFIG_AEC62XX_TUNING
	hwif->tuneproc = &aec62xx_tune_drive;
	hwif->speedproc = &aec62xx_tune_chipset;
#ifdef CONFIG_BLK_DEV_IDEDMA
	if (hwif->dma_base)
		hwif->dmaproc = &aec62xx_dmaproc;
#else /* !CONFIG_BLK_DEV_IDEDMA */
	hwif->drives[0].autotune = 1;
	hwif->drives[1].autotune = 1;
#endif /* CONFIG_BLK_DEV_IDEDMA */
#endif /* CONFIG_AEC62XX_TUNING */
}

void __init ide_dmacapable_aec62xx (ide_hwif_t *hwif, unsigned long dmabase)
{
#ifdef CONFIG_AEC62XX_TUNING
	unsigned long flags;
	byte reg54h = 0;

	__save_flags(flags);	/* local CPU only */
	__cli();		/* local CPU only */

	pci_read_config_byte(hwif->pci_dev, 0x54, &reg54h);
	pci_write_config_byte(hwif->pci_dev, 0x54, reg54h & ~(hwif->channel ? 0xF0 : 0x0F));

	__restore_flags(flags);	/* local CPU only */
#endif /* CONFIG_AEC62XX_TUNING */
	ide_setup_dma(hwif, dmabase, 8);
}
