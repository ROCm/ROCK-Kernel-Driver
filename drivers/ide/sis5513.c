/*
 * linux/drivers/ide/sis5513.c		Version 0.13	March 6, 2002
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
 * John Fremlin, Manfred Spraul :
 *			  for checking code correctness, providing patches.
 */

/*
 * Original tests and design on the SiS620/5513 chipset.
 * ATA100 tests and design on the SiS735/5513 chipset.
 * ATA16/33 design from specs
 */

/*
 * TODO:
 *	- Get ridden of SisHostChipInfo[] completness dependancy.
 *	- Get ATA-133 datasheets, implement ATA-133 init code.
 *	- Are there pre-ATA_16 SiS chips ? -> tune init code for them
 *	  or remove ATA_00 define
 *	- More checks in the config registers (force values instead of
 *	  relying on the BIOS setting them correctly).
 *	- Further optimisations ?
 *	  . for example ATA66+ regs 0x48 & 0x4A
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

/* When DEBUG is defined it outputs initial PCI config register
   values and changes made to them by the driver */
// #define DEBUG
/* When BROKEN_LEVEL is defined it limits the DMA mode
   at boot time to its value */
// #define BROKEN_LEVEL XFER_SW_DMA_0
#undef DISPLAY_SIS_TIMINGS

/* Miscellaneaous flags */
#define SIS5513_LATENCY		0x01

/* registers layout and init values are chipset family dependant */
/* 1/ define families */
#define ATA_00		0x00
#define ATA_16		0x01
#define ATA_33		0x02
#define ATA_66		0x03
#define ATA_100a	0x04	// SiS730 is ATA100 with ATA66 layout
#define ATA_100		0x05
#define ATA_133		0x06

/* 2/ variable holding the controller chipset family value */
static unsigned char chipset_family;

/*
 * Debug code: following IDE config registers' changes
 */
#ifdef DEBUG
/* Copy of IDE Config registers 0x00 -> 0x57
   Fewer might be used depending on the actual chipset */
static unsigned char ide_regs_copy[0x58];

static byte sis5513_max_config_register(void) {
	switch(chipset_family) {
		case ATA_00:
		case ATA_16:	return 0x4f;
		case ATA_33:	return 0x52;
		case ATA_66:
		case ATA_100a:
		case ATA_100:
		case ATA_133:
		default:	return 0x57;
	}
}

/* Read config registers, print differences from previous read */
static void sis5513_load_verify_registers(struct pci_dev* dev, char* info) {
	int i;
	byte reg_val;
	byte changed=0;
	byte max = sis5513_max_config_register();

	printk("SIS5513: %s, changed registers:\n", info);
	for(i=0; i<=max; i++) {
		pci_read_config_byte(dev, i, &reg_val);
		if (reg_val != ide_regs_copy[i]) {
			printk("%0#x: %0#x -> %0#x\n",
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
	byte max = sis5513_max_config_register();

	for(i=0; i<=max; i++) {
		pci_read_config_byte(dev, i, &(ide_regs_copy[i]));
	}
}

/* Print a register */
static void sis5513_print_register(int reg) {
	printk(" %0#x:%0#x", reg, ide_regs_copy[reg]);
}

/* Print valuable registers */
static void sis5513_print_registers(struct pci_dev* dev, char* marker) {
	int i;
	byte max = sis5513_max_config_register();

	sis5513_load_registers(dev);
	printk("SIS5513 %s\n", marker);
	printk("SIS5513 dump:");
	for(i=0x00; i<0x40; i++) {
		if ((i % 0x10)==0) printk("\n             ");
		sis5513_print_register(i);
	}
	for(; i<49; i++) {
		sis5513_print_register(i);
	}
	printk("\n             ");

	for(; i<=max; i++) {
		sis5513_print_register(i);
	}
	printk("\n");
}
#endif


/*
 * Devices supported
 */
static const struct {
	const char *name;
	unsigned short host_id;
	unsigned char chipset_family;
	unsigned char flags;
} SiSHostChipInfo[] = {
	{ "SiS750",	PCI_DEVICE_ID_SI_750,	ATA_100,	SIS5513_LATENCY },
	{ "SiS745",	PCI_DEVICE_ID_SI_745,	ATA_100,	SIS5513_LATENCY },
	{ "SiS740",	PCI_DEVICE_ID_SI_740,	ATA_100,	SIS5513_LATENCY },
	{ "SiS735",	PCI_DEVICE_ID_SI_735,	ATA_100,	SIS5513_LATENCY },
	{ "SiS730",	PCI_DEVICE_ID_SI_730,	ATA_100a,	SIS5513_LATENCY },
	{ "SiS650",	PCI_DEVICE_ID_SI_650,	ATA_100,	SIS5513_LATENCY },
	{ "SiS645",	PCI_DEVICE_ID_SI_645,	ATA_100,	SIS5513_LATENCY },
	{ "SiS635",	PCI_DEVICE_ID_SI_635,	ATA_100,	SIS5513_LATENCY },
	{ "SiS640",	PCI_DEVICE_ID_SI_640,	ATA_66,		SIS5513_LATENCY },
	{ "SiS630",	PCI_DEVICE_ID_SI_630,	ATA_66,		SIS5513_LATENCY },
	{ "SiS620",	PCI_DEVICE_ID_SI_620,	ATA_66,		SIS5513_LATENCY },
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
static byte cycle_time_offset[] = {0,0,5,4,4,0,0};
static byte cycle_time_range[] = {0,0,2,3,3,4,4};
static byte cycle_time_value[][XFER_UDMA_5 - XFER_UDMA_0 + 1] = {
	{0,0,0,0,0,0}, /* no udma */
	{0,0,0,0,0,0}, /* no udma */
	{3,2,1,0,0,0},
	{7,5,3,2,1,0},
	{7,5,3,2,1,0},
	{11,7,5,4,2,1},
	{0,0,0,0,0,0} /* not yet known, ask SiS */
};

static struct pci_dev *host_dev = NULL;


/*
 * Printing configuration
 */
#if defined(DISPLAY_SIS_TIMINGS) && defined(CONFIG_PROC_FS)
#include <linux/stat.h>
#include <linux/proc_fs.h>

static int sis_get_info(char *, char **, off_t, int);
extern int (*sis_display_info)(char *, char **, off_t, int); /* ide-proc.c */
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
	"Reserved", "Reserved",
	"Reserved", "Reserved"
};

/* Generic add master or slave info function */
static char* get_drives_info (char *buffer, byte pos)
{
	byte reg00, reg01, reg10, reg11; /* timing registers */
	char* p = buffer;

/* Postwrite/Prefetch */
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

/* UDMA */
	if (chipset_family >= ATA_33) {
		p += sprintf(p, "                UDMA %s \t \t \t UDMA %s\n",
			     (reg01 & 0x80)  ? "Enabled" : "Disabled",
			     (reg11 & 0x80) ? "Enabled" : "Disabled");

		p += sprintf(p, "                UDMA Cycle Time    ");
		switch(chipset_family) {
			case ATA_33:	p += sprintf(p, cycle_time[(reg01 & 0x60) >> 5]); break;
			case ATA_66:
			case ATA_100a:	p += sprintf(p, cycle_time[(reg01 & 0x70) >> 4]); break;
			case ATA_100:	p += sprintf(p, cycle_time[reg01 & 0x0F]); break;
			case ATA_133:
			default:	p += sprintf(p, "133+ ?"); break;
		}
		p += sprintf(p, " \t UDMA Cycle Time    ");
		switch(chipset_family) {
			case ATA_33:	p += sprintf(p, cycle_time[(reg11 & 0x60) >> 5]); break;
			case ATA_66:
			case ATA_100a:	p += sprintf(p, cycle_time[(reg11 & 0x70) >> 4]); break;
			case ATA_100:	p += sprintf(p, cycle_time[reg11 & 0x0F]); break;
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
		case ATA_100: p += sprintf(p, active_time[(reg00 & 0x70) >> 4]); break;
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
		case ATA_100: p += sprintf(p, active_time[(reg10 & 0x70) >> 4]); break;
		case ATA_133:
		default: p += sprintf(p, "133+ ?"); break;
	}
	p += sprintf(p, "\n");

/* Data Recovery */
	/* warning: may need (reg&0x07) for pre ATA66 chips */
	p += sprintf(p, "                Data Recovery Time %s \t Data Recovery Time %s\n",
		     recovery_time[reg00 & 0x0f], recovery_time[reg10 & 0x0f]);

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
	byte reg;
	u16 reg2, reg3;

	p += sprintf(p, "\nSiS 5513 ");
	switch(chipset_family) {
		case ATA_00: p += sprintf(p, "Unknown???"); break;
		case ATA_16: p += sprintf(p, "DMA 16"); break;
		case ATA_33: p += sprintf(p, "Ultra 33"); break;
		case ATA_66: p += sprintf(p, "Ultra 66"); break;
		case ATA_100a:
		case ATA_100: p += sprintf(p, "Ultra 100"); break;
		case ATA_133:
		default: p+= sprintf(p, "Ultra 133+"); break;
	}
	p += sprintf(p, " chipset\n");
	p += sprintf(p, "--------------- Primary Channel ---------------- Secondary Channel -------------\n");

/* Status */
	pci_read_config_byte(bmide_dev, 0x4a, &reg);
	p += sprintf(p, "Channel Status: ");
	if (chipset_family < ATA_66) {
		p += sprintf(p, "%s \t \t \t \t %s\n",
			     (reg & 0x04) ? "On" : "Off",
			     (reg & 0x02) ? "On" : "Off");
	} else {
		p += sprintf(p, "%s \t \t \t \t %s \n",
			     (reg & 0x02) ? "On" : "Off",
			     (reg & 0x04) ? "On" : "Off");
	}

/* Operation Mode */
	pci_read_config_byte(bmide_dev, 0x09, &reg);
	p += sprintf(p, "Operation Mode: %s \t \t \t %s \n",
		     (reg & 0x01) ? "Native" : "Compatible",
		     (reg & 0x04) ? "Native" : "Compatible");

/* 80-pin cable ? */
	if (chipset_family > ATA_33) {
		pci_read_config_byte(bmide_dev, 0x48, &reg);
		p += sprintf(p, "Cable Type:     %s \t \t \t %s\n",
			     (reg & 0x10) ? cable_type[1] : cable_type[0],
			     (reg & 0x20) ? cable_type[1] : cable_type[0]);
	}

/* Prefetch Count */
	pci_read_config_word(bmide_dev, 0x4c, &reg2);
	pci_read_config_word(bmide_dev, 0x4e, &reg3);
	p += sprintf(p, "Prefetch Count: %d \t \t \t \t %d\n",
		     reg2, reg3);

	p = get_masters_info(p);
	p = get_slaves_info(p);

	return p-buffer;
}
#endif /* defined(DISPLAY_SIS_TIMINGS) && defined(CONFIG_PROC_FS) */


byte sis_proc = 0;
extern char *ide_xfer_verbose (byte xfer_rate);


/*
 * Configuration functions
 */
/* Enables per-drive prefetch and postwrite */
static void config_drive_art_rwp (ide_drive_t *drive)
{
	struct ata_channel *hwif = drive->channel;
	struct pci_dev *dev	= hwif->pci_dev;

	byte reg4bh		= 0;
	byte rw_prefetch	= (0x11 << drive->dn);

#ifdef DEBUG
	printk("SIS5513: config_drive_art_rwp, drive %d\n", drive->dn);
	sis5513_load_verify_registers(dev, "config_drive_art_rwp start");
#endif

	if (drive->type != ATA_DISK)
		return;
	pci_read_config_byte(dev, 0x4b, &reg4bh);

	if ((reg4bh & rw_prefetch) != rw_prefetch)
		pci_write_config_byte(dev, 0x4b, reg4bh|rw_prefetch);
#ifdef DEBUG
	sis5513_load_verify_registers(dev, "config_drive_art_rwp end");
#endif
}


/* Set per-drive active and recovery time */
static void config_art_rwp_pio (ide_drive_t *drive, byte pio)
{
	struct ata_channel *hwif = drive->channel;
	struct pci_dev *dev	= hwif->pci_dev;

	byte			timing, drive_pci, test1, test2;

	unsigned short eide_pio_timing[6] = {600, 390, 240, 180, 120, 90};
	unsigned short xfer_pio = drive->id->eide_pio_modes;

#ifdef DEBUG
	sis5513_load_verify_registers(dev, "config_drive_art_rwp_pio start");
#endif

	config_drive_art_rwp(drive);

	if (pio == 255)
		pio = ata_timing_mode(drive, XFER_PIO | XFER_EPIO) - XFER_PIO_0;

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
	printk("SIS5513: config_drive_art_rwp_pio, drive %d, pio %d, timing %d\n",
	       drive->dn, pio, timing);
#endif

	switch(drive->dn) {
		case 0:		drive_pci = 0x40; break;
		case 1:		drive_pci = 0x42; break;
		case 2:		drive_pci = 0x44; break;
		case 3:		drive_pci = 0x46; break;
		default:	return;
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
	} else {
		switch(timing) { /*   active  recovery
					  v     v */
			case 4:		test1 = 0x30|0x01; break;
			case 3:		test1 = 0x30|0x03; break;
			case 2:		test1 = 0x40|0x04; break;
			case 1:		test1 = 0x60|0x07; break;
			default:	break;
		}
		pci_write_config_byte(dev, drive_pci, test1);
	}

#ifdef DEBUG
	sis5513_load_verify_registers(dev, "config_drive_art_rwp_pio start");
#endif
}

static int config_chipset_for_pio (ide_drive_t *drive, byte pio)
{
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
	return ide_config_drive_speed(drive, speed);
}

static int sis5513_tune_chipset (ide_drive_t *drive, byte speed)
{
	struct ata_channel *hwif = drive->channel;
	struct pci_dev *dev	= hwif->pci_dev;

	byte			drive_pci, reg;

#ifdef DEBUG
	sis5513_load_verify_registers(dev, "sis5513_tune_chipset start");
	printk("SIS5513: sis5513_tune_chipset, drive %d, speed %d\n",
	       drive->dn, speed);
#endif
	switch(drive->dn) {
		case 0:		drive_pci = 0x40; break;
		case 1:		drive_pci = 0x42; break;
		case 2:		drive_pci = 0x44; break;
		case 3:		drive_pci = 0x46; break;
		default:	return 0;
	}

#ifdef BROKEN_LEVEL
#ifdef DEBUG
	printk("SIS5513: BROKEN_LEVEL activated, speed=%d -> speed=%d\n", speed, BROKEN_LEVEL);
#endif
	if (speed > BROKEN_LEVEL) speed = BROKEN_LEVEL;
#endif

	pci_read_config_byte(dev, drive_pci+1, &reg);
	/* Disable UDMA bit for non UDMA modes on UDMA chips */
	if ((speed < XFER_UDMA_0) && (chipset_family > ATA_16)) {
		reg &= 0x7F;
		pci_write_config_byte(dev, drive_pci+1, reg);
	}

	/* Config chip for mode */
	switch(speed) {
#ifdef CONFIG_BLK_DEV_IDEDMA
		case XFER_UDMA_5:
		case XFER_UDMA_4:
		case XFER_UDMA_3:
		case XFER_UDMA_2:
		case XFER_UDMA_1:
		case XFER_UDMA_0:
			/* Force the UDMA bit on if we want to use UDMA */
			reg |= 0x80;
			/* clean reg cycle time bits */
			reg &= ~((0xFF >> (8 - cycle_time_range[chipset_family]))
				 << cycle_time_offset[chipset_family]);
			/* set reg cycle time bits */
			reg |= cycle_time_value[chipset_family-ATA_00][speed-XFER_UDMA_0]
				<< cycle_time_offset[chipset_family];
			pci_write_config_byte(dev, drive_pci+1, reg);
			break;
		case XFER_MW_DMA_2:
		case XFER_MW_DMA_1:
		case XFER_MW_DMA_0:
		case XFER_SW_DMA_2:
		case XFER_SW_DMA_1:
		case XFER_SW_DMA_0:
			break;
#endif /* CONFIG_BLK_DEV_IDEDMA */
		case XFER_PIO_4: return((int) config_chipset_for_pio(drive, 4));
		case XFER_PIO_3: return((int) config_chipset_for_pio(drive, 3));
		case XFER_PIO_2: return((int) config_chipset_for_pio(drive, 2));
		case XFER_PIO_1: return((int) config_chipset_for_pio(drive, 1));
		case XFER_PIO_0:
		default:	 return((int) config_chipset_for_pio(drive, 0));
	}
	drive->current_speed = speed;
#ifdef DEBUG
	sis5513_load_verify_registers(dev, "sis5513_tune_chipset end");
#endif
	return ((int) ide_config_drive_speed(drive, speed));
}

static void sis5513_tune_drive (ide_drive_t *drive, byte pio)
{
	(void) config_chipset_for_pio(drive, pio);
}

#ifdef CONFIG_BLK_DEV_IDEDMA
/*
 * ((id->hw_config & 0x4000|0x2000) && (drive->channel->udma_four))
 */
static int config_chipset_for_dma (ide_drive_t *drive, byte ultra)
{
	struct hd_driveid *id = drive->id;
	struct ata_channel *hwif = drive->channel;

	byte			speed = 0;

	byte unit		= (drive->select.b.unit & 0x01);
	byte udma_66		= eighty_ninty_three(drive);

#ifdef DEBUG
	printk("SIS5513: config_chipset_for_dma, drive %d, ultra %d\n",
	       drive->dn, ultra);
#endif

	if ((id->dma_ultra & 0x0020) && ultra && udma_66 && (chipset_family >= ATA_100a))
		speed = XFER_UDMA_5;
	else if ((id->dma_ultra & 0x0010) && ultra && udma_66 && (chipset_family >= ATA_66))
		speed = XFER_UDMA_4;
	else if ((id->dma_ultra & 0x0008) && ultra && udma_66 && (chipset_family >= ATA_66))
		speed = XFER_UDMA_3;
	else if ((id->dma_ultra & 0x0004) && ultra && (chipset_family >= ATA_33))
		speed = XFER_UDMA_2;
	else if ((id->dma_ultra & 0x0002) && ultra && (chipset_family >= ATA_33))
		speed = XFER_UDMA_1;
	else if ((id->dma_ultra & 0x0001) && ultra && (chipset_family >= ATA_33))
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
		return 0;

	outb(inb(hwif->dma_base+2)|(1<<(5+unit)), hwif->dma_base+2);

	sis5513_tune_chipset(drive, speed);

	return ((int)	((id->dma_ultra >> 11) & 7) ? 1 :
			((id->dma_ultra >> 8) & 7) ? 1 :
			((id->dma_mword >> 8) & 7) ? 1 :
			((id->dma_1word >> 8) & 7) ? 1 :
						     0);
}

static int config_drive_xfer_rate (ide_drive_t *drive)
{
	struct hd_driveid *id = drive->id;
	int on = 0;
	int verbose = 1;

	config_chipset_for_pio(drive, 5);

	if (id && (id->capability & 1) && drive->channel->autodma) {
		/* Consult the list of known "bad" drives */
		if (udma_black_list(drive)) {
			on = 0;
			goto fast_ata_pio;
		}
		on = 0;
		verbose = 0;
		if (id->field_valid & 4) {
			if (id->dma_ultra & 0x003F) {
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
		} else if ((udma_white_list(drive)) &&
			   (id->eide_dma_time > 150)) {
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
		(void) config_chipset_for_pio(drive, 5);
	}

	udma_enable(drive, on, verbose);

	return 0;
}

static int sis5513_dmaproc(struct ata_device *drive)
{
	config_drive_art_rwp(drive);
	config_art_rwp_pio(drive, 5);

	return config_drive_xfer_rate(drive);
}
#endif

/* Chip detection and general config */
unsigned int __init pci_init_sis5513(struct pci_dev *dev)
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
		printk(SiSHostChipInfo[i].name);
		printk("\n");

#ifdef DEBUG
		sis5513_print_registers(dev, "pci_init_sis5513 start");
#endif

		if (SiSHostChipInfo[i].flags & SIS5513_LATENCY) {
			byte latency = (chipset_family == ATA_100)? 0x80 : 0x10; /* Lacking specs */
			pci_write_config_byte(dev, PCI_LATENCY_TIMER, latency);
		}
	}

	/* Make general config ops here
	   1/ tell IDE channels to operate in Compabitility mode only
	   2/ tell old chips to allow per drive IDE timings */
	if (host_dev) {
		byte reg;
		switch(chipset_family) {
			case ATA_133:
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
			sis_display_info = &sis_get_info;
		}
#endif
	}
#ifdef DEBUG
	sis5513_load_verify_registers(dev, "pci_init_sis5513 end");
#endif
	return 0;
}

unsigned int __init ata66_sis5513(struct ata_channel *hwif)
{
	byte reg48h = 0, ata66 = 0;
	byte mask = hwif->unit ? 0x20 : 0x10;
	pci_read_config_byte(hwif->pci_dev, 0x48, &reg48h);

	if (chipset_family >= ATA_66) {
		ata66 = (reg48h & mask) ? 0 : 1;
	}
        return ata66;
}

void __init ide_init_sis5513(struct ata_channel *hwif)
{

	hwif->irq = hwif->unit ? 15 : 14;

	hwif->tuneproc = &sis5513_tune_drive;
	hwif->speedproc = &sis5513_tune_chipset;

	if (!(hwif->dma_base))
		return;

	if (host_dev) {
#ifdef CONFIG_BLK_DEV_IDEDMA
		if (chipset_family > ATA_16) {
			hwif->autodma = noautodma ? 0 : 1;
			hwif->highmem = 1;
			hwif->XXX_udma = sis5513_dmaproc;
		} else {
#endif
			hwif->autodma = 0;
#ifdef CONFIG_BLK_DEV_IDEDMA
		}
#endif
	}
	return;
}
