/*
 * linux/drivers/ide/pci/sis5513.c		Version 0.14ac	Sept 11, 2002
 *
 * Copyright (C) 1999-2000	Andre Hedrick <andre@linux-ide.org>
 * Copyright (C) 2002		Lionel Bouton <Lionel.Bouton@inet6.fr>, Maintainer
 * May be copied or modified under the terms of the GNU General Public License
 *
 *
 * Thanks :
 *
 * SiS Taiwan		: for direct support and hardware.
 * Daniela Engert	: for initial ATA100 advices and numerous others.
 * John Fremlin, Manfred Spraul, Dave Morgan, Peter Kjellerstedt	:
 *			  for checking code correctness, providing patches.
 *
 *
 * Original tests and design on the SiS620/5513 chipset.
 * ATA100 tests and design on the SiS735/5513 chipset.
 * ATA16/33 support from specs
 * ATA133 support for SiS961/962 by L.C. Chang <lcchang@sis.com.tw>
 *
 * Documentation:
 *	SiS chipset documentation available under NDA to companies not
 *	individuals only.
 */

/*
 * Notes/Special cases:
 * - SiS5513 derivatives usually have the same PCI IDE register layout when
 *  supporting the same UDMA modes.
 * - There are exceptions :
 *  . SiS730 and SiS550 use the same layout than ATA_66 chipsets but support
 *   ATA_100
 *  . ATA_133 capable chipsets mark a shift in SiS chipset designs : previously
 *   south and northbridge were integrated, making IDE (a southbridge function)
 *   capabilities easily deduced from the northbridge PCI id. With ATA_133,
 *   chipsets started to be split in the usual north/south bridges chips
 *   -> the driver needs to detect the correct southbridge when faced to newest
 *   northbridges.
 *  . On ATA133 capable chipsets when bit 30 of dword at 0x54 is 1 the
 *   configuration space is moved from 0x40 to 0x70.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/module.h>
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
#include "sis5513.h"

/* When DEBUG is defined it outputs initial PCI config register
   values and changes made to them by the driver */
// #define DEBUG
/* When BROKEN_LEVEL is defined it limits the DMA mode
   at boot time to its value */
// #define BROKEN_LEVEL XFER_SW_DMA_0

/* Miscellaneaous flags */
#define SIS5513_LATENCY		0x01

/* registers layout and init values are chipset family dependant */
/* 1/ define families */
#define ATA_00		0x00
#define ATA_16		0x01
#define ATA_33		0x02
#define ATA_66		0x03
#define ATA_100a	0x04 // SiS730 is ATA100 with ATA66 layout
#define ATA_100		0x05
#define ATA_133a	0x06 // SiS961b with 133 support
#define ATA_133		0x07 // SiS962
/* 2/ variable holding the controller chipset family value */
static u8 chipset_family;


/*
 * Debug code: following IDE config registers' changes
 */
#ifdef DEBUG
/* Copy of IDE Config registers fewer will be used
 * Some odd chipsets hang if unused registers are accessed
 * -> We only access them in #DEBUG code (then we'll see if SiS did
 * it right from day one) */
static u8 ide_regs_copy[0xff];

/* Read config registers, print differences from previous read */
static void sis5513_load_verify_registers(struct pci_dev* dev, char* info) {
	int i;
	u8 reg_val;
	u8 changed=0;

	printk("SIS5513: %s, changed registers:\n", info);
	for(i=0; i<=0xff; i++) {
		pci_read_config_byte(dev, i, &reg_val);
		if (reg_val != ide_regs_copy[i]) {
			printk("%02x: %02x -> %02x\n",
			       i, ide_regs_copy[i], reg_val);
			ide_regs_copy[i]=reg_val;
			changed=1;
		}
	}

	if (!changed) {
		printk("none\n");
	}
}

/* Load config registers, no printing */
static void sis5513_load_registers(struct pci_dev* dev) {
	int i;

	for(i=0; i<=0xff; i++) {
		pci_read_config_byte(dev, i, &(ide_regs_copy[i]));
	}
}

/* Print config space registers a la "lspci -vxxx" */
static void sis5513_print_registers(struct pci_dev* dev, char* marker) {
	int i,j;

	sis5513_load_registers(dev);
	printk("SIS5513 %s\n", marker);

	for(i=0; i<=0xf; i++) {
		printk("SIS5513 dump: %d" "0:", i);
		for(j=0; j<=0xf; j++) {
			printk(" %02x", ide_regs_copy[(i<<16)+j]);
		}
		printk("\n");
	}
}
#endif


/*
 * Devices supported
 */
static const struct {
	const char *name;
	u16 host_id;
	u8 chipset_family;
	u8 flags;
} SiSHostChipInfo[] = {
	{ "SiS752",	PCI_DEVICE_ID_SI_752,	ATA_133,	0 },
	{ "SiS751",	PCI_DEVICE_ID_SI_751,	ATA_133,	0 },
	{ "SiS750",	PCI_DEVICE_ID_SI_750,	ATA_133,	0 },
	{ "SiS748",	PCI_DEVICE_ID_SI_748,	ATA_133,	0 },
	{ "SiS746",	PCI_DEVICE_ID_SI_746,	ATA_133,	0 },
	{ "SiS745",	PCI_DEVICE_ID_SI_745,	ATA_133,	0 },
	{ "SiS740",	PCI_DEVICE_ID_SI_740,	ATA_100,	0 },
	{ "SiS735",	PCI_DEVICE_ID_SI_735,	ATA_100,	SIS5513_LATENCY },
	{ "SiS730",	PCI_DEVICE_ID_SI_730,	ATA_100a,	SIS5513_LATENCY },
	{ "SiS652",	PCI_DEVICE_ID_SI_652,	ATA_133,	0 },
	{ "SiS651",	PCI_DEVICE_ID_SI_651,	ATA_133,	0 },
	{ "SiS650",	PCI_DEVICE_ID_SI_650,	ATA_133,	0 },
	{ "SiS648",	PCI_DEVICE_ID_SI_648,	ATA_133,	0 },
	{ "SiS646",	PCI_DEVICE_ID_SI_646,	ATA_133,	0 },
	{ "SiS645",	PCI_DEVICE_ID_SI_645,	ATA_133,	0 },
	{ "SiS635",	PCI_DEVICE_ID_SI_635,	ATA_100,	SIS5513_LATENCY },
	{ "SiS640",	PCI_DEVICE_ID_SI_640,	ATA_66,		SIS5513_LATENCY },
	{ "SiS630",	PCI_DEVICE_ID_SI_630,	ATA_66,		SIS5513_LATENCY },
	{ "SiS620",	PCI_DEVICE_ID_SI_620,	ATA_66,		SIS5513_LATENCY },
	{ "SiS550",	PCI_DEVICE_ID_SI_550,	ATA_100a,	0},
	{ "SiS540",	PCI_DEVICE_ID_SI_540,	ATA_66,		0},
	{ "SiS530",	PCI_DEVICE_ID_SI_530,	ATA_66,		0},
	{ "SiS5600",	PCI_DEVICE_ID_SI_5600,	ATA_33,		0},
	{ "SiS5598",	PCI_DEVICE_ID_SI_5598,	ATA_33,		0},
	{ "SiS5597",	PCI_DEVICE_ID_SI_5597,	ATA_33,		0},
	{ "SiS5591",	PCI_DEVICE_ID_SI_5591,	ATA_33,		0},
	{ "SiS5513",	PCI_DEVICE_ID_SI_5513,	ATA_16,		0},
	{ "SiS5511",	PCI_DEVICE_ID_SI_5511,	ATA_16,		0},
};

/* Cycle time bits and values vary accross chip dma capabilities
   These three arrays hold the register layout and the values to set.
   Indexed by chipset_family and (dma_mode - XFER_UDMA_0) */

/* {ATA_00, ATA_16, ATA_33, ATA_66, ATA_100a, ATA_100, ATA_133} */
static u8 cycle_time_offset[] = {0,0,5,4,4,0,0};
static u8 cycle_time_range[] = {0,0,2,3,3,4,4};
static u8 cycle_time_value[][XFER_UDMA_6 - XFER_UDMA_0 + 1] = {
	{0,0,0,0,0,0,0}, /* no udma */
	{0,0,0,0,0,0,0}, /* no udma */
	{3,2,1,0,0,0,0}, /* ATA_33 */
	{7,5,3,2,1,0,0}, /* ATA_66 */
	{7,5,3,2,1,0,0}, /* ATA_100a (730 specific), differences are on cycle_time range and offset */
	{11,7,5,4,2,1,0}, /* ATA_100 */
	{15,10,7,5,3,2,1}, /* ATA_133a (earliest 691 southbridges) */
	{15,10,7,5,3,2,1}, /* ATA_133 */
};
/* CRC Valid Setup Time vary accross IDE clock setting 33/66/100/133
   See SiS962 data sheet for more detail */
static u8 cvs_time_value[][XFER_UDMA_6 - XFER_UDMA_0 + 1] = {
	{0,0,0,0,0,0,0}, /* no udma */
	{0,0,0,0,0,0,0}, /* no udma */
	{2,1,1,0,0,0,0},
	{4,3,2,1,0,0,0},
	{4,3,2,1,0,0,0},
	{6,4,3,1,1,1,0},
	{9,6,4,2,2,2,2},
	{9,6,4,2,2,2,2},
};
/* Initialize time, Active time, Recovery time vary accross
   IDE clock settings. These 3 arrays hold the register value
   for PIO0/1/2/3/4 and DMA0/1/2 mode in order */
static u8 ini_time_value[][8] = {
	{0,0,0,0,0,0,0,0},
	{0,0,0,0,0,0,0,0},
	{2,1,0,0,0,1,0,0},
	{4,3,1,1,1,3,1,1},
	{4,3,1,1,1,3,1,1},
	{6,4,2,2,2,4,2,2},
	{9,6,3,3,3,6,3,3},
	{9,6,3,3,3,6,3,3},
};
static u8 act_time_value[][8] = {
	{0,0,0,0,0,0,0,0},
	{0,0,0,0,0,0,0,0},
	{9,9,9,2,2,7,2,2},
	{19,19,19,5,4,14,5,4},
	{19,19,19,5,4,14,5,4},
	{28,28,28,7,6,21,7,6},
	{38,38,38,10,9,28,10,9},
	{38,38,38,10,9,28,10,9},
};
static u8 rco_time_value[][8] = {
	{0,0,0,0,0,0,0,0},
	{0,0,0,0,0,0,0,0},
	{9,2,0,2,0,7,1,1},
	{19,5,1,5,2,16,3,2},
	{19,5,1,5,2,16,3,2},
	{30,9,3,9,4,25,6,4},
	{40,12,4,12,5,34,12,5},
	{40,12,4,12,5,34,12,5},
};

static struct pci_dev *host_dev = NULL;

/*
 * Printing configuration
 */
/* Used for chipset type printing at boot time */
static char* chipset_capability[] = {
	"ATA", "ATA 16",
	"ATA 33", "ATA 66",
	"ATA 100", "ATA 100",
	"ATA 133", "ATA 133"
};

#if defined(DISPLAY_SIS_TIMINGS) && defined(CONFIG_PROC_FS)
#include <linux/stat.h>
#include <linux/proc_fs.h>

static u8 sis_proc = 0;

static struct pci_dev *bmide_dev;

static char* cable_type[] = {
	"80 pins",
	"40 pins"
};

static char* recovery_time[] ={
	"12 PCICLK", "1 PCICLK",
	"2 PCICLK", "3 PCICLK",
	"4 PCICLK", "5 PCICLCK",
	"6 PCICLK", "7 PCICLCK",
	"8 PCICLK", "9 PCICLCK",
	"10 PCICLK", "11 PCICLK",
	"13 PCICLK", "14 PCICLK",
	"15 PCICLK", "15 PCICLK"
};

static char* active_time[] = {
	"8 PCICLK", "1 PCICLCK",
	"2 PCICLK", "3 PCICLK",
	"4 PCICLK", "5 PCICLK",
	"6 PCICLK", "12 PCICLK"
};

static char* cycle_time[] = {
	"Reserved", "2 CLK",
	"3 CLK", "4 CLK",
	"5 CLK", "6 CLK",
	"7 CLK", "8 CLK",
	"9 CLK", "10 CLK",
	"11 CLK", "12 CLK",
	"13 CLK", "14 CLK",
	"15 CLK", "16 CLK"
};

/* Generic add master or slave info function */
static char* get_drives_info (char *buffer, u8 pos)
{
	u8 reg00, reg01, reg10, reg11; /* timing registers */
	u32 regdw0, regdw1;
	char* p = buffer;

/* Postwrite/Prefetch */
	if (chipset_family < ATA_133) {
		pci_read_config_byte(bmide_dev, 0x4b, &reg00);
		p += sprintf(p, "Drive %d:        Postwrite %s \t \t Postwrite %s\n",
			     pos, (reg00 & (0x10 << pos)) ? "Enabled" : "Disabled",
			     (reg00 & (0x40 << pos)) ? "Enabled" : "Disabled");
		p += sprintf(p, "                Prefetch  %s \t \t Prefetch  %s\n",
			     (reg00 & (0x01 << pos)) ? "Enabled" : "Disabled",
			     (reg00 & (0x04 << pos)) ? "Enabled" : "Disabled");
		pci_read_config_byte(bmide_dev, 0x40+2*pos, &reg00);
		pci_read_config_byte(bmide_dev, 0x41+2*pos, &reg01);
		pci_read_config_byte(bmide_dev, 0x44+2*pos, &reg10);
		pci_read_config_byte(bmide_dev, 0x45+2*pos, &reg11);
	} else {
		u32 reg54h;
		u8 drive_pci = 0x40;
		pci_read_config_dword(bmide_dev, 0x54, &reg54h);
		if (reg54h & 0x40000000) {
			// Configuration space remapped to 0x70
			drive_pci = 0x70;
		}
		pci_read_config_dword(bmide_dev, (unsigned long)drive_pci+8*pos, &regdw0);
		pci_read_config_dword(bmide_dev, (unsigned long)drive_pci+8*pos+4, &regdw1);
		p += sprintf(p, "Drive %d:\n", pos);
	}


/* UDMA */
	if (chipset_family >= ATA_133) {
		p += sprintf(p, "                UDMA %s \t \t \t UDMA %s\n",
			     (regdw0 & 0x04) ? "Enabled" : "Disabled",
			     (regdw1 & 0x04) ? "Enabled" : "Disabled");
		p += sprintf(p, "                UDMA Cycle Time    %s \t UDMA Cycle Time    %s\n",
			     cycle_time[(regdw0 & 0xF0) >> 4],
			     cycle_time[(regdw1 & 0xF0) >> 4]);
	} else if (chipset_family >= ATA_33) {
		p += sprintf(p, "                UDMA %s \t \t \t UDMA %s\n",
			     (reg01 & 0x80) ? "Enabled" : "Disabled",
			     (reg11 & 0x80) ? "Enabled" : "Disabled");

		p += sprintf(p, "                UDMA Cycle Time    ");
		switch(chipset_family) {
			case ATA_33:	p += sprintf(p, cycle_time[(reg01 & 0x60) >> 5]); break;
			case ATA_66:
			case ATA_100a:	p += sprintf(p, cycle_time[(reg01 & 0x70) >> 4]); break;
			case ATA_100:
			case ATA_133a:	p += sprintf(p, cycle_time[reg01 & 0x0F]); break;
			case ATA_133:
			default:	p += sprintf(p, "133+ ?"); break;
		}
		p += sprintf(p, " \t UDMA Cycle Time    ");
		switch(chipset_family) {
			case ATA_33:	p += sprintf(p, cycle_time[(reg11 & 0x60) >> 5]); break;
			case ATA_66:
			case ATA_100a:	p += sprintf(p, cycle_time[(reg11 & 0x70) >> 4]); break;
			case ATA_100:
			case ATA_133a:  p += sprintf(p, cycle_time[reg11 & 0x0F]); break;
			case ATA_133:
			default:	p += sprintf(p, "133+ ?"); break;
		}
		p += sprintf(p, "\n");
	}

/* Data Active */
	p += sprintf(p, "                Data Active Time   ");
	switch(chipset_family) {
		case ATA_00:
		case ATA_16: /* confirmed */
		case ATA_33:
		case ATA_66:
		case ATA_100a: p += sprintf(p, active_time[reg01 & 0x07]); break;
		case ATA_100:
		case ATA_133a: p += sprintf(p, active_time[(reg00 & 0x70) >> 4]); break;
		case ATA_133:
		default: p += sprintf(p, "133+ ?"); break;
	}
	p += sprintf(p, " \t Data Active Time   ");
	switch(chipset_family) {
		case ATA_00:
		case ATA_16:
		case ATA_33:
		case ATA_66:
		case ATA_100a: p += sprintf(p, active_time[reg11 & 0x07]); break;
		case ATA_100:
		case ATA_133a: p += sprintf(p, active_time[(reg10 & 0x70) >> 4]); break;
		case ATA_133:
		default: p += sprintf(p, "133+ ?"); break;
	}
	p += sprintf(p, "\n");

/* Data Recovery */
	/* warning: may need (reg&0x07) for pre ATA66 chips */
	if (chipset_family < ATA_133) {
		p += sprintf(p, "                Data Recovery Time %s \t Data Recovery Time %s\n",
			     recovery_time[reg00 & 0x0f], recovery_time[reg10 & 0x0f]);
	}

	return p;
}

static char* get_masters_info(char* buffer)
{
	return get_drives_info(buffer, 0);
}

static char* get_slaves_info(char* buffer)
{
	return get_drives_info(buffer, 1);
}

/* Main get_info, called on /proc/ide/sis reads */
static int sis_get_info (char *buffer, char **addr, off_t offset, int count)
{
	char *p = buffer;
	int len;
	u8 reg;
	u16 reg2, reg3;

	p += sprintf(p, "\nSiS 5513 ");
	switch(chipset_family) {
		case ATA_00: p += sprintf(p, "Unknown???"); break;
		case ATA_16: p += sprintf(p, "DMA 16"); break;
		case ATA_33: p += sprintf(p, "Ultra 33"); break;
		case ATA_66: p += sprintf(p, "Ultra 66"); break;
		case ATA_100a:
		case ATA_100: p += sprintf(p, "Ultra 100"); break;
		case ATA_133a:
		case ATA_133: p += sprintf(p, "Ultra 133"); break;
		default: p+= sprintf(p, "Unknown???"); break;
	}
	p += sprintf(p, " chipset\n");
	p += sprintf(p, "--------------- Primary Channel "
		     "---------------- Secondary Channel "
		     "-------------\n");

/* Status */
	pci_read_config_byte(bmide_dev, 0x4a, &reg);
	if (chipset_family == ATA_133) {
		pci_read_config_word(bmide_dev, 0x50, &reg2);
		pci_read_config_word(bmide_dev, 0x52, &reg3);
	}
	p += sprintf(p, "Channel Status: ");
	if (chipset_family < ATA_66) {
		p += sprintf(p, "%s \t \t \t \t %s\n",
			     (reg & 0x04) ? "On" : "Off",
			     (reg & 0x02) ? "On" : "Off");
	} else if (chipset_family < ATA_133) {
		p += sprintf(p, "%s \t \t \t \t %s \n",
			     (reg & 0x02) ? "On" : "Off",
			     (reg & 0x04) ? "On" : "Off");
	} else { /* ATA_133 */
		p += sprintf(p, "%s \t \t \t \t %s \n",
			     (reg2 & 0x02) ? "On" : "Off",
			     (reg3 & 0x02) ? "On" : "Off");
	}

/* Operation Mode */
	pci_read_config_byte(bmide_dev, 0x09, &reg);
	p += sprintf(p, "Operation Mode: %s \t \t \t %s \n",
		     (reg & 0x01) ? "Native" : "Compatible",
		     (reg & 0x04) ? "Native" : "Compatible");

/* 80-pin cable ? */
	if (chipset_family >= ATA_133) {
		p += sprintf(p, "Cable Type:     %s \t \t \t %s\n",
			     (reg2 & 0x01) ? cable_type[1] : cable_type[0],
			     (reg3 & 0x01) ? cable_type[1] : cable_type[0]);
	} else if (chipset_family > ATA_33) {
		pci_read_config_byte(bmide_dev, 0x48, &reg);
		p += sprintf(p, "Cable Type:     %s \t \t \t %s\n",
			     (reg & 0x10) ? cable_type[1] : cable_type[0],
			     (reg & 0x20) ? cable_type[1] : cable_type[0]);
	}

/* Prefetch Count */
	if (chipset_family < ATA_133) {
		pci_read_config_word(bmide_dev, 0x4c, &reg2);
		pci_read_config_word(bmide_dev, 0x4e, &reg3);
		p += sprintf(p, "Prefetch Count: %d \t \t \t \t %d\n",
			     reg2, reg3);
	}

	p = get_masters_info(p);
	p = get_slaves_info(p);

	len = (p - buffer) - offset;
	*addr = buffer + offset;
	
	return len > count ? count : len;
}
#endif /* defined(DISPLAY_SIS_TIMINGS) && defined(CONFIG_PROC_FS) */

static u8 sis5513_ratemask (ide_drive_t *drive)
{
#if 0
	u8 rates[] = { 0, 0, 1, 2, 3, 3, 4, 4 };
	u8 mode = rates[chipset_family];
#else
	u8 mode;

	switch(chipset_family) {
		case ATA_133:
		case ATA_133a:
			mode = 4;
			break;
		case ATA_100:
		case ATA_100a:
			mode = 3;
			break;
		case ATA_66:
			mode = 2;
			break;
		case ATA_33:
			return 1;
		case ATA_16:
                case ATA_00:	
		default:
			return 0;
	}
#endif
	if (!eighty_ninty_three(drive))
		mode = min(mode, (u8)1);
	return mode;
}

/*
 * Configuration functions
 */
/* Enables per-drive prefetch and postwrite */
static void config_drive_art_rwp (ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;

	u8 reg4bh		= 0;
	u8 rw_prefetch		= (0x11 << drive->dn);

#ifdef DEBUG
	printk("SIS5513: config_drive_art_rwp, drive %d\n", drive->dn);
	sis5513_load_verify_registers(dev, "config_drive_art_rwp start");
#endif

	if (drive->media != ide_disk)
		return;
	pci_read_config_byte(dev, 0x4b, &reg4bh);

	if ((reg4bh & rw_prefetch) != rw_prefetch)
		pci_write_config_byte(dev, 0x4b, reg4bh|rw_prefetch);
#ifdef DEBUG
	sis5513_load_verify_registers(dev, "config_drive_art_rwp end");
#endif
}


/* Set per-drive active and recovery time */
static void config_art_rwp_pio (ide_drive_t *drive, u8 pio)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;

	u8			timing, drive_pci, test1, test2;

	u16 eide_pio_timing[6] = {600, 390, 240, 180, 120, 90};
	u16 xfer_pio = drive->id->eide_pio_modes;

#ifdef DEBUG
	sis5513_load_verify_registers(dev, "config_drive_art_rwp_pio start");
#endif

	config_drive_art_rwp(drive);
	pio = ide_get_best_pio_mode(drive, 255, pio, NULL);

	if (xfer_pio> 4)
		xfer_pio = 0;

	if (drive->id->eide_pio_iordy > 0) {
		for (xfer_pio = 5;
			(xfer_pio > 0) &&
			(drive->id->eide_pio_iordy > eide_pio_timing[xfer_pio]);
			xfer_pio--);
	} else {
		xfer_pio = (drive->id->eide_pio_modes & 4) ? 0x05 :
			   (drive->id->eide_pio_modes & 2) ? 0x04 :
			   (drive->id->eide_pio_modes & 1) ? 0x03 : xfer_pio;
	}

	timing = (xfer_pio >= pio) ? xfer_pio : pio;

#ifdef DEBUG
	printk("SIS5513: config_drive_art_rwp_pio, "
		"drive %d, pio %d, timing %d\n",
	       drive->dn, pio, timing);
#endif

	/* In pre ATA_133 case, drives sit at 0x40 + 4*drive->dn */
	drive_pci = 0x40;
	/* In SiS962 case drives sit at (0x40 or 0x70) + 8*drive->dn) */
	if (chipset_family >= ATA_133) {
		u32 reg54h;
		pci_read_config_dword(dev, 0x54, &reg54h);
		if (reg54h & 0x40000000) drive_pci = 0x70;
		drive_pci += ((drive->dn)*0x4);
	} else {
		drive_pci += ((drive->dn)*0x2);
	}

	/* register layout changed with newer ATA100 chips */
	if (chipset_family < ATA_100) {
		pci_read_config_byte(dev, drive_pci, &test1);
		pci_read_config_byte(dev, drive_pci+1, &test2);

		/* Clear active and recovery timings */
		test1 &= ~0x0F;
		test2 &= ~0x07;

		switch(timing) {
			case 4:		test1 |= 0x01; test2 |= 0x03; break;
			case 3:		test1 |= 0x03; test2 |= 0x03; break;
			case 2:		test1 |= 0x04; test2 |= 0x04; break;
			case 1:		test1 |= 0x07; test2 |= 0x06; break;
			default:	break;
		}
		pci_write_config_byte(dev, drive_pci, test1);
		pci_write_config_byte(dev, drive_pci+1, test2);
	} else if (chipset_family < ATA_133) {
		switch(timing) { /*		active  recovery
						  v     v */
			case 4:		test1 = 0x30|0x01; break;
			case 3:		test1 = 0x30|0x03; break;
			case 2:		test1 = 0x40|0x04; break;
			case 1:		test1 = 0x60|0x07; break;
			default:	break;
		}
		pci_write_config_byte(dev, drive_pci, test1);
	} else { /* ATA_133 */
		u32 test3;
		pci_read_config_dword(dev, drive_pci, &test3);
		test3 &= 0xc0c00fff;
		if (test3 & 0x08) {
			test3 |= (unsigned long)ini_time_value[ATA_133-ATA_00][timing] << 12;
			test3 |= (unsigned long)act_time_value[ATA_133-ATA_00][timing] << 16;
			test3 |= (unsigned long)rco_time_value[ATA_133-ATA_00][timing] << 24;
		} else {
			test3 |= (unsigned long)ini_time_value[ATA_100-ATA_00][timing] << 12;
			test3 |= (unsigned long)act_time_value[ATA_100-ATA_00][timing] << 16;
			test3 |= (unsigned long)rco_time_value[ATA_100-ATA_00][timing] << 24;
		}
		pci_write_config_dword(dev, drive_pci, test3);
	}

#ifdef DEBUG
	sis5513_load_verify_registers(dev, "config_drive_art_rwp_pio start");
#endif
}

static int config_chipset_for_pio (ide_drive_t *drive, u8 pio)
{
#if 0
	config_art_rwp_pio(drive, pio);
	return ide_config_drive_speed(drive, (XFER_PIO_0 + pio));
#else
	u8 speed;

	switch(pio) {
		case 4:		speed = XFER_PIO_4; break;
		case 3:		speed = XFER_PIO_3; break;
		case 2:		speed = XFER_PIO_2; break;
		case 1:		speed = XFER_PIO_1; break;
		default:	speed = XFER_PIO_0; break;
	}

	config_art_rwp_pio(drive, pio);
	return ide_config_drive_speed(drive, speed);
#endif
}

static int sis5513_tune_chipset (ide_drive_t *drive, u8 xferspeed)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;

	u8 drive_pci, reg, speed;
	u32 regdw;

#ifdef DEBUG
	sis5513_load_verify_registers(dev, "sis5513_tune_chipset start");
#endif

#ifdef BROKEN_LEVEL
#ifdef DEBUG
	printk("SIS5513: BROKEN_LEVEL activated, speed=%d -> speed=%d\n", xferspeed, BROKEN_LEVEL);
#endif
	if (xferspeed > BROKEN_LEVEL) xferspeed = BROKEN_LEVEL;
#endif

	speed = ide_rate_filter(sis5513_ratemask(drive), xferspeed);

#ifdef DEBUG
	printk("SIS5513: sis5513_tune_chipset, drive %d, speed %d\n",
	       drive->dn, xferspeed);
#endif

	/* See config_art_rwp_pio for drive pci config registers */
	drive_pci = 0x40;
	if (chipset_family >= ATA_133) {
		u32 reg54h;
		pci_read_config_dword(dev, 0x54, &reg54h);
		if (reg54h & 0x40000000) drive_pci = 0x70;
		drive_pci += ((drive->dn)*0x4);
		pci_read_config_dword(dev, (unsigned long)drive_pci, &regdw);
		/* Disable UDMA bit for non UDMA modes on UDMA chips */
		if (speed < XFER_UDMA_0) {
			regdw &= 0xfffffffb;
			pci_write_config_dword(dev, (unsigned long)drive_pci, regdw);
		}
	
	} else {
		drive_pci += ((drive->dn)*0x2);
		pci_read_config_byte(dev, drive_pci+1, &reg);
		/* Disable UDMA bit for non UDMA modes on UDMA chips */
		if ((speed < XFER_UDMA_0) && (chipset_family > ATA_16)) {
			reg &= 0x7F;
			pci_write_config_byte(dev, drive_pci+1, reg);
		}
	}

	/* Config chip for mode */
	switch(speed) {
		case XFER_UDMA_6:
		case XFER_UDMA_5:
		case XFER_UDMA_4:
		case XFER_UDMA_3:
		case XFER_UDMA_2:
		case XFER_UDMA_1:
		case XFER_UDMA_0:
			if (chipset_family >= ATA_133) {
				regdw |= 0x04;
				regdw &= 0xfffff00f;
				/* check if ATA133 enable */
				if (regdw & 0x08) {
					regdw |= (unsigned long)cycle_time_value[ATA_133-ATA_00][speed-XFER_UDMA_0] << 4;
					regdw |= (unsigned long)cvs_time_value[ATA_133-ATA_00][speed-XFER_UDMA_0] << 8;
				} else {
				/* if ATA133 disable, we should not set speed above UDMA5 */
					if (speed > XFER_UDMA_5)
						speed = XFER_UDMA_5;
					regdw |= (unsigned long)cycle_time_value[ATA_100-ATA_00][speed-XFER_UDMA_0] << 4;
					regdw |= (unsigned long)cvs_time_value[ATA_100-ATA_00][speed-XFER_UDMA_0] << 8;
				}
				pci_write_config_dword(dev, (unsigned long)drive_pci, regdw);
			} else {
				/* Force the UDMA bit on if we want to use UDMA */
				reg |= 0x80;
				/* clean reg cycle time bits */
				reg &= ~((0xFF >> (8 - cycle_time_range[chipset_family]))
					 << cycle_time_offset[chipset_family]);
				/* set reg cycle time bits */
				reg |= cycle_time_value[chipset_family-ATA_00][speed-XFER_UDMA_0]
					<< cycle_time_offset[chipset_family];
				pci_write_config_byte(dev, drive_pci+1, reg);
			}
			break;
		case XFER_MW_DMA_2:
		case XFER_MW_DMA_1:
		case XFER_MW_DMA_0:
		case XFER_SW_DMA_2:
		case XFER_SW_DMA_1:
		case XFER_SW_DMA_0:
			break;
		case XFER_PIO_4: return((int) config_chipset_for_pio(drive, 4));
		case XFER_PIO_3: return((int) config_chipset_for_pio(drive, 3));
		case XFER_PIO_2: return((int) config_chipset_for_pio(drive, 2));
		case XFER_PIO_1: return((int) config_chipset_for_pio(drive, 1));
		case XFER_PIO_0:
		default:	 return((int) config_chipset_for_pio(drive, 0));	
	}
#ifdef DEBUG
	sis5513_load_verify_registers(dev, "sis5513_tune_chipset end");
#endif
	return ((int) ide_config_drive_speed(drive, speed));
}

static void sis5513_tune_drive (ide_drive_t *drive, u8 pio)
{
	(void) config_chipset_for_pio(drive, pio);
}

/*
 * ((id->hw_config & 0x4000|0x2000) && (HWIF(drive)->udma_four))
 */
static int config_chipset_for_dma (ide_drive_t *drive)
{
	u8 speed	= ide_dma_speed(drive, sis5513_ratemask(drive));

#ifdef DEBUG
	printk("SIS5513: config_chipset_for_dma, drive %d, ultra %x\n",
	       drive->dn, drive->id->dma_ultra);
#endif

	if (!(speed))
		return 0;

	sis5513_tune_chipset(drive, speed);
	return ide_dma_enable(drive);
}

static int sis5513_config_drive_xfer_rate (ide_drive_t *drive)
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
		sis5513_tune_drive(drive, 5);
		return hwif->ide_dma_off_quietly(drive);
	}
	return hwif->ide_dma_on(drive);
}

/* initiates/aborts (U)DMA read/write operations on a drive. */
static int sis5513_config_xfer_rate (ide_drive_t *drive)
{
	config_drive_art_rwp(drive);
	config_art_rwp_pio(drive, 5);
	return sis5513_config_drive_xfer_rate(drive);
}

/* Chip detection and general config */
static unsigned int __init init_chipset_sis5513 (struct pci_dev *dev, const char *name)
{
	struct pci_dev *host;
	int i = 0;

	/* Find the chip */
	for (i = 0; i < ARRAY_SIZE(SiSHostChipInfo) && !host_dev; i++) {
		host = pci_find_device (PCI_VENDOR_ID_SI,
					SiSHostChipInfo[i].host_id,
					NULL);
		if (!host)
			continue;

		host_dev = host;
		chipset_family = SiSHostChipInfo[i].chipset_family;
	
		/* check 100/133 chipset family */
		if (chipset_family == ATA_133) {
			u32 reg54h;
			u16 reg02h;
			pci_read_config_dword(dev, 0x54, &reg54h);
			pci_write_config_dword(dev, 0x54, (reg54h & 0x7fffffff));
			pci_read_config_word(dev, 0x02, &reg02h);
			pci_write_config_dword(dev, 0x54, reg54h);
			/* devid 5518 here means SiS962 or later
			   which supports ATA133 */
			if (reg02h != 0x5518) {
				u8 reg49h;
				unsigned long sbrev;
				/* SiS961 family */

		/*
		 * FIXME !!! GAK!!!!!!!!!! PCI DIRECT POKING 
		 */
				outl(0x80001008, 0x0cf8);
				sbrev = inl(0x0cfc);

				pci_read_config_byte(dev, 0x49, &reg49h);
				if (((sbrev & 0xff) == 0x10) && (reg49h & 0x80))
					chipset_family = ATA_133a;
				else
					chipset_family = ATA_100;
			}
		}
		printk(SiSHostChipInfo[i].name);
		printk("    %s controller", chipset_capability[chipset_family]);
		printk("\n");

#ifdef DEBUG
		sis5513_print_registers(dev, "pci_init_sis5513 start");
#endif

		if (SiSHostChipInfo[i].flags & SIS5513_LATENCY) {
			u8 latency = (chipset_family == ATA_100)? 0x80 : 0x10; /* Lacking specs */
			pci_write_config_byte(dev, PCI_LATENCY_TIMER, latency);
		}
	}

	/* Make general config ops here
	   1/ tell IDE channels to operate in Compabitility mode only
	   2/ tell old chips to allow per drive IDE timings */
	if (host_dev) {
		u8 reg;
		u16 regw;
		switch(chipset_family) {
			case ATA_133:
				/* SiS962 operation mode */
				pci_read_config_word(dev, 0x50, &regw);
				if (regw & 0x08)
					pci_write_config_word(dev, 0x50, regw&0xfff7);
				pci_read_config_word(dev, 0x52, &regw);
				if (regw & 0x08)
					pci_write_config_word(dev, 0x52, regw&0xfff7);
				break;
			case ATA_133a:
			case ATA_100:
				/* Set compatibility bit */
				pci_read_config_byte(dev, 0x49, &reg);
				if (!(reg & 0x01)) {
					pci_write_config_byte(dev, 0x49, reg|0x01);
				}
				break;
			case ATA_100a:
			case ATA_66:
				/* On ATA_66 chips the bit was elsewhere */
				pci_read_config_byte(dev, 0x52, &reg);
				if (!(reg & 0x04)) {
					pci_write_config_byte(dev, 0x52, reg|0x04);
				}
				break;
			case ATA_33:
				/* On ATA_33 we didn't have a single bit to set */
				pci_read_config_byte(dev, 0x09, &reg);
				if ((reg & 0x0f) != 0x00) {
					pci_write_config_byte(dev, 0x09, reg&0xf0);
				}
			case ATA_16:
				/* force per drive recovery and active timings
				   needed on ATA_33 and below chips */
				pci_read_config_byte(dev, 0x52, &reg);
				if (!(reg & 0x08)) {
					pci_write_config_byte(dev, 0x52, reg|0x08);
				}
				break;
			case ATA_00:
			default: break;
		}

#if defined(DISPLAY_SIS_TIMINGS) && defined(CONFIG_PROC_FS)
		if (!sis_proc) {
			sis_proc = 1;
			bmide_dev = dev;
			ide_pci_register_host_proc(&sis_procs[0]);
		}
#endif
	}
#ifdef DEBUG
	sis5513_load_verify_registers(dev, "pci_init_sis5513 end");
#endif
	return 0;
}

static unsigned int __init ata66_sis5513 (ide_hwif_t *hwif)
{
	u8 ata66 = 0;

	if (chipset_family >= ATA_133) {
		u16 regw = 0;
		u16 reg_addr = hwif->channel ? 0x52: 0x50;
		pci_read_config_word(hwif->pci_dev, reg_addr, &regw);
		ata66 = (regw & 0x8000) ? 0 : 1;
	} else if (chipset_family >= ATA_66) {
		u8 reg48h = 0;
		u8 mask = hwif->channel ? 0x20 : 0x10;
		pci_read_config_byte(hwif->pci_dev, 0x48, &reg48h);
		ata66 = (reg48h & mask) ? 0 : 1;
	}
        return ata66;
}

static void __init init_hwif_sis5513 (ide_hwif_t *hwif)
{
	hwif->autodma = 0;

	if (!hwif->irq)
		hwif->irq = hwif->channel ? 15 : 14;

	hwif->tuneproc = &sis5513_tune_drive;
	hwif->speedproc = &sis5513_tune_chipset;

	if (!(hwif->dma_base)) {
		hwif->drives[0].autotune = 1;
		hwif->drives[1].autotune = 1;
		return;
	}

	hwif->atapi_dma = 1;
	hwif->ultra_mask = 0x7f;
	hwif->mwdma_mask = 0x07;
	hwif->swdma_mask = 0x07;

	if (!host_dev)
		return;

	if (!(hwif->udma_four))
		hwif->udma_four = ata66_sis5513(hwif);

	if (chipset_family > ATA_16) {
		hwif->ide_dma_check = &sis5513_config_xfer_rate;
		if (!noautodma)
			hwif->autodma = 1;
	}
	hwif->drives[0].autodma = hwif->autodma;
	hwif->drives[1].autodma = hwif->autodma;
	return;
}

static void __init init_dma_sis5513 (ide_hwif_t *hwif, unsigned long dmabase)
{
	ide_setup_dma(hwif, dmabase, 8);
}

extern void ide_setup_pci_device(struct pci_dev *, ide_pci_device_t *);


static int __devinit sis5513_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	ide_pci_device_t *d = &sis5513_chipsets[id->driver_data];
	if (dev->device != d->device)
		BUG();
	ide_setup_pci_device(dev, d);
	MOD_INC_USE_COUNT;
	return 0;
}

static struct pci_device_id sis5513_pci_tbl[] __devinitdata = {
	{ PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_5513, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ 0, },
};

static struct pci_driver driver = {
	.name		= "SIS IDE",
	.id_table	= sis5513_pci_tbl,
	.probe		= sis5513_init_one,
};

static int sis5513_ide_init(void)
{
	return ide_pci_register_driver(&driver);
}

static void sis5513_ide_exit(void)
{
	ide_pci_unregister_driver(&driver);
}

module_init(sis5513_ide_init);
module_exit(sis5513_ide_exit);

MODULE_AUTHOR("Lionel Bouton, L C Chang, Andre Hedrick");
MODULE_DESCRIPTION("PCI driver module for SIS IDE");
MODULE_LICENSE("GPL");

/*
 * TODO:
 *	- Get ridden of SisHostChipInfo[] completness dependancy.
 *	- Study drivers/ide/ide-timing.h.
 *	- Are there pre-ATA_16 SiS5513 chips ? -> tune init code for them
 *	  or remove ATA_00 define
 *	- More checks in the config registers (force values instead of
 *	  relying on the BIOS setting them correctly).
 *	- Further optimisations ?
 *	  . for example ATA66+ regs 0x48 & 0x4A
 */

