/**** vi:set ts=8 sts=8 sw=8:************************************************
 *
 * Version 0.11	March 27, 2002
 *
 * Copyright (C) 1999-2000	Andre Hedrick (andre@linux-ide.org)
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

#include "ata-timing.h"
#include "pcihost.h"

#undef DISPLAY_AEC62XX_TIMINGS

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
static struct pci_dev *bmide_dev;

static const char *aec6280_get_speed(u8 speed)
{
	switch(speed) {
		case 7: return "6";
		case 6: return "5";
		case 5: return "4";
		case 4: return "3";
		case 3: return "2";
		case 2: return "1";
		case 1: return "0";
		case 0: return "?";
	}
	return "?";
}

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
		case PCI_DEVICE_ID_ARTOP_ATP865:
			p += sprintf(p, "\n                                AEC6280 Chipset without ROM.\n");
			break;
		case PCI_DEVICE_ID_ARTOP_ATP865R:
			p += sprintf(p, "\n                                AEC6280 Chipset with ROM.\n");
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
		case PCI_DEVICE_ID_ARTOP_ATP865:
		case PCI_DEVICE_ID_ARTOP_ATP865R:
			(void) pci_read_config_byte(bmide_dev, 0x44, &art);
			p += sprintf(p, "DMA Mode:       %s(%s)          %s(%s)",
				(c0&0x20)?((art&0x0f)?"UDMA":" DMA"):" PIO",
				aec6280_get_speed(art&0x0f),
				(c0&0x40)?((art&0xf0)?"UDMA":" DMA"):" PIO",
				aec6280_get_speed(art>>4));
			(void) pci_read_config_byte(bmide_dev, 0x45, &art);
			p += sprintf(p, "         %s(%s)          %s(%s)\n",
				(c0&0x20)?((art&0x0f)?"UDMA":" DMA"):" PIO",
				aec6280_get_speed(art&0x0f),
				(c0&0x40)?((art&0xf0)?"UDMA":" DMA"):" PIO",
				aec6280_get_speed(art>>4));
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

struct chipset_bus_clock_list_entry {
	byte		xfer_speed;

	byte		chipset_settings_34;
	byte		ultra_settings_34;

	byte		chipset_settings_33;
	byte		ultra_settings_33;
};

struct chipset_bus_clock_list_entry aec62xx_base [] = {
#ifdef CONFIG_BLK_DEV_IDEDMA
	{	XFER_UDMA_6,	0x41,	0x06,	0x31,	0x07	},
	{	XFER_UDMA_5,	0x41,	0x05,	0x31,	0x06	},
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

/*
 * TO DO: active tuning and correction of cards without a bios.
 */

static byte pci_bus_clock_list (byte speed, struct chipset_bus_clock_list_entry * chipset_table)
{
	for ( ; chipset_table->xfer_speed ; chipset_table++)
		if (chipset_table->xfer_speed == speed) {
			return ((byte) ((system_bus_speed <= 33) ? chipset_table->chipset_settings_33 : chipset_table->chipset_settings_34));
		}
	return 0x00;
}

static byte pci_bus_clock_list_ultra (byte speed, struct chipset_bus_clock_list_entry * chipset_table)
{
	for ( ; chipset_table->xfer_speed ; chipset_table++)
		if (chipset_table->xfer_speed == speed) {
			return ((byte) ((system_bus_speed <= 33) ? chipset_table->ultra_settings_33 : chipset_table->ultra_settings_34));
		}
	return 0x00;
}

static int aec62xx_ratemask(struct ata_device *drive)
{
	struct pci_dev *dev = drive->channel->pci_dev;
	u32 bmide = pci_resource_start(dev, 4);
	int map = 0;

	if (!eighty_ninty_three(drive))
		return XFER_UDMA;

	switch(dev->device) {
		case PCI_DEVICE_ID_ARTOP_ATP865R:
		case PCI_DEVICE_ID_ARTOP_ATP865:
			if (IN_BYTE(bmide+2) & 0x10)
				map |= XFER_UDMA_133;
			else
				map |= XFER_UDMA_100;
		case PCI_DEVICE_ID_ARTOP_ATP860R:
		case PCI_DEVICE_ID_ARTOP_ATP860:
			map |= XFER_UDMA_66;
		case PCI_DEVICE_ID_ARTOP_ATP850UF:
			map |= XFER_UDMA;
	}
	return map;
}

static int aec6210_tune_chipset(struct ata_device *drive, byte speed)
{
	struct pci_dev *dev = drive->channel->pci_dev;
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

	return ide_config_drive_speed(drive, speed);
}

static int aec6260_tune_chipset(struct ata_device *drive, byte speed)
{
	struct pci_dev *dev = drive->channel->pci_dev;
	byte unit		= (drive->select.b.unit & 0x01);
	u8 ultra_pci = drive->channel->unit ? 0x45 : 0x44;
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

	drive->current_speed = speed;
	return ide_config_drive_speed(drive, speed);
}


static int aec62xx_tune_chipset(struct ata_device *drive, byte speed)
{
	switch (drive->channel->pci_dev->device) {
		case PCI_DEVICE_ID_ARTOP_ATP865:
		case PCI_DEVICE_ID_ARTOP_ATP865R:
		case PCI_DEVICE_ID_ARTOP_ATP860:
		case PCI_DEVICE_ID_ARTOP_ATP860R:
			return aec6260_tune_chipset(drive, speed);
		case PCI_DEVICE_ID_ARTOP_ATP850UF:
			return aec6210_tune_chipset(drive, speed);
		default:
			return -1;
	}
}

#ifdef CONFIG_BLK_DEV_IDEDMA
static int config_chipset_for_dma(struct ata_device *drive, u8 udma)
{
	int map;
	u8 mode;

	if (drive->type != ATA_DISK)
		return 0;

	if (udma)
		map = aec62xx_ratemask(drive);
	else
		map = XFER_SWDMA | XFER_MWDMA;

	mode = ata_timing_mode(drive, map);

	if (mode < XFER_SW_DMA_0)
		return 0;

	return !aec62xx_tune_chipset(drive, mode);
}
#endif /* CONFIG_BLK_DEV_IDEDMA */

static void aec62xx_tune_drive(struct ata_device *drive, byte pio)
{
	byte speed;

	if (pio == 255)
		speed = ata_timing_mode(drive, XFER_PIO | XFER_EPIO);
	else
		speed = XFER_PIO_0 + min_t(byte, pio, 4);

	(void) aec62xx_tune_chipset(drive, speed);
}

#ifdef CONFIG_BLK_DEV_IDEDMA
static int config_drive_xfer_rate(struct ata_device *drive)
{
	struct hd_driveid *id = drive->id;
	int on = 1;
	int verbose = 1;

	if (id && (id->capability & 1) && drive->channel->autodma) {
		/* Consult the list of known "bad" drives */
		if (udma_black_list(drive)) {
			on = 0;
			goto fast_ata_pio;
		}
		on = 0;
		verbose = 0;
		if (id->field_valid & 4) {
			if (id->dma_ultra & 0x007F) {
				/* Force if Capable UltraDMA */
				on = config_chipset_for_dma(drive, 1);
				if ((id->field_valid & 2) &&
				    (!on))
					goto try_dma_modes;
			}
		} else if (id->field_valid & 2) {
try_dma_modes:
			if ((id->dma_mword & 0x0007) ||
			    (id->dma_1word & 0x0007)) {
				/* Force if Capable regular DMA modes */
				on = config_chipset_for_dma(drive, 0);
				if (!on)
					goto no_dma_set;
			}
		} else if (udma_white_list(drive)) {
			if (id->eide_dma_time > 150) {
				goto no_dma_set;
			}
			/* Consult the list of known "good" drives */
			on = config_chipset_for_dma(drive, 0);
			if (!on)
				goto no_dma_set;
		} else {
			goto fast_ata_pio;
		}
	} else if ((id->capability & 8) || (id->field_valid & 2)) {
fast_ata_pio:
		on = 0;
		verbose = 0;
no_dma_set:
		aec62xx_tune_drive(drive, 5);
	}
	udma_enable(drive, on, verbose);
	return 0;
}

int aec62xx_dmaproc(struct ata_device *drive)
{
	return config_drive_xfer_rate(drive);
}
#endif

static unsigned int __init aec62xx_init_chipset(struct pci_dev *dev)
{
	u8 reg49h = 0;
	u8 reg4ah = 0;

	switch(dev->device) {
		case PCI_DEVICE_ID_ARTOP_ATP865:
		case PCI_DEVICE_ID_ARTOP_ATP865R:
			/* Clear reset and test bits.  */
			pci_read_config_byte(dev, 0x49, &reg49h);
			pci_write_config_byte(dev, 0x49, reg49h & ~0x30);
			/* Enable chip interrupt output.  */
			pci_read_config_byte(dev, 0x4a, &reg4ah);
			pci_write_config_byte(dev, 0x4a, reg4ah & ~0x01);
#ifdef CONFIG_AEC6280_BURST
			/* Must be greater than 0x80 for burst mode.  */
			pci_write_config_byte(dev, PCI_LATENCY_TIMER, 0x90);
			/* Enable burst mode.  */
			pci_read_config_byte(dev, 0x4a, &reg4ah);
			pci_write_config_byte(dev, 0x4a, reg4ah | 0x80);
#endif
			break;
		default:
			break;
	}

	if (dev->resource[PCI_ROM_RESOURCE].start) {
		pci_write_config_dword(dev, PCI_ROM_ADDRESS, dev->resource[PCI_ROM_RESOURCE].start | PCI_ROM_ADDRESS_ENABLE);
		printk("%s: ROM enabled at 0x%08lx\n", dev->name, dev->resource[PCI_ROM_RESOURCE].start);
	}

#if defined(DISPLAY_AEC62XX_TIMINGS) && defined(CONFIG_PROC_FS)
	if (!aec62xx_proc) {
		aec62xx_proc = 1;
		bmide_dev = dev;
		aec62xx_display_info = &aec62xx_get_info;
	}
#endif

	return dev->irq;
}

static unsigned int __init aec62xx_ata66_check(struct ata_channel *ch)
{
	u8 mask	= ch->unit ? 0x02 : 0x01;
	u8 ata66 = 0;

	pci_read_config_byte(ch->pci_dev, 0x49, &ata66);

	return ((ata66 & mask) ? 0 : 1);
}

static void __init aec62xx_init_channel(struct ata_channel *hwif)
{
	hwif->tuneproc = aec62xx_tune_drive;
	hwif->speedproc = aec62xx_tune_chipset;
	hwif->drives[0].autotune = 1;
	hwif->drives[1].autotune = 1;

#ifdef CONFIG_BLK_DEV_IDEDMA
	if (hwif->dma_base) {
		hwif->XXX_udma = aec62xx_dmaproc;
		hwif->highmem = 1;
# ifdef CONFIG_IDEDMA_AUTO
		if (!noautodma)
			hwif->autodma = 1;
# endif
	}
#endif
}

static void __init aec62xx_init_dma(struct ata_channel *hwif, unsigned long dmabase)
{
	u8 reg54h = 0;

	/* FIXME: we need some locking here */
	pci_read_config_byte(hwif->pci_dev, 0x54, &reg54h);
	pci_write_config_byte(hwif->pci_dev, 0x54, reg54h & ~(hwif->unit ? 0xF0 : 0x0F));
	ata_init_dma(hwif, dmabase);
}

/* module data table */
static struct ata_pci_device chipsets[] __initdata = {
	{
		vendor: PCI_VENDOR_ID_ARTOP,
		device: PCI_DEVICE_ID_ARTOP_ATP850UF,
		init_chipset: aec62xx_init_chipset,
		init_channel: aec62xx_init_channel,
		init_dma: aec62xx_init_dma,
		enablebits: { {0x4a,0x02,0x02},	{0x4a,0x04,0x04} },
		bootable: OFF_BOARD,
		flags: ATA_F_SER | ATA_F_IRQ | ATA_F_DMA
	},
	{
		vendor: PCI_VENDOR_ID_ARTOP,
		device: PCI_DEVICE_ID_ARTOP_ATP860,
		init_chipset: aec62xx_init_chipset,
		ata66_check: aec62xx_ata66_check,
		init_channel: aec62xx_init_channel,
		enablebits: { {0x00,0x00,0x00},	{0x00,0x00,0x00} },
		bootable: NEVER_BOARD,
		flags: ATA_F_IRQ | ATA_F_NOADMA | ATA_F_DMA
	},
	{
		vendor: PCI_VENDOR_ID_ARTOP,
		device: PCI_DEVICE_ID_ARTOP_ATP860R,
		init_chipset: aec62xx_init_chipset,
		ata66_check: aec62xx_ata66_check,
		init_channel: aec62xx_init_channel,
		enablebits: { {0x4a,0x02,0x02},	{0x4a,0x04,0x04} },
		bootable: OFF_BOARD,
		flags: ATA_F_IRQ | ATA_F_DMA
	},
	{
		vendor: PCI_VENDOR_ID_ARTOP,
		device: PCI_DEVICE_ID_ARTOP_ATP865,
		init_chipset: aec62xx_init_chipset,
		ata66_check: aec62xx_ata66_check,
		init_channel: aec62xx_init_channel,
		enablebits: { {0x00,0x00,0x00},	{0x00,0x00,0x00} },
		bootable: NEVER_BOARD,
		flags: ATA_F_IRQ | ATA_F_DMA
	},
	{
		vendor: PCI_VENDOR_ID_ARTOP,
		device: PCI_DEVICE_ID_ARTOP_ATP865R,
		init_chipset: aec62xx_init_chipset,
		ata66_check: aec62xx_ata66_check,
		init_channel: aec62xx_init_channel,
		enablebits: { {0x00,0x00,0x00},	{0x00,0x00,0x00} },
		bootable: OFF_BOARD,
		flags: ATA_F_IRQ | ATA_F_DMA
	},
	{
		vendor: PCI_VENDOR_ID_ARTOP,
		device: PCI_DEVICE_ID_ARTOP_ATP865,
		init_chipset: aec62xx_init_chipset,
		ata66_check: aec62xx_ata66_check,
		init_channel: aec62xx_init_channel,
		enablebits: { {0x00,0x00,0x00},	{0x00,0x00,0x00} },
		bootable: NEVER_BOARD,
		flags: ATA_F_IRQ | ATA_F_NOADMA | ATA_F_DMA
	},
	{
		vendor: PCI_VENDOR_ID_ARTOP,
		device: PCI_DEVICE_ID_ARTOP_ATP865R,
		init_chipset: aec62xx_init_chipset,
		ata66_check: aec62xx_ata66_check,
		init_channel: aec62xx_init_channel,
		enablebits: { {0x4a,0x02,0x02},	{0x4a,0x04,0x04} },
		bootable: OFF_BOARD,
		flags: ATA_F_IRQ | ATA_F_DMA
	},
};

int __init init_aec62xx(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(chipsets); ++i) {
		ata_register_chipset(&chipsets[i]);
	}

	return 0;
}
