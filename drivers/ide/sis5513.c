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

#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/hdreg.h>
#include <linux/ide.h>

#include <asm/io.h>
#include <asm/irq.h>

#include "timing.h"
#include "pcihost.h"

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

static u8 sis5513_max_config_register(void) {
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
	u8 reg_val;
	u8 changed = 0;
	u8 max = sis5513_max_config_register();

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
static void sis5513_load_registers(struct pci_dev* dev)
{
	int i;
	u8 max = sis5513_max_config_register();

	for(i=0; i<=max; i++) {
		pci_read_config_byte(dev, i, &(ide_regs_copy[i]));
	}
}

/* Print a register */
static void sis5513_print_register(int reg)
{
	printk(" %0#x:%0#x", reg, ide_regs_copy[reg]);
}

/* Print valuable registers */
static void sis5513_print_registers(struct pci_dev* dev, char* marker) {
	int i;
	u8 max = sis5513_max_config_register();

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
static u8 cycle_time_offset[] = {0,0,5,4,4,0,0};
static u8 cycle_time_range[] = {0,0,2,3,3,4,4};
static u8 cycle_time_value[][XFER_UDMA_5 - XFER_UDMA_0 + 1] = {
	{0,0,0,0,0,0}, /* no udma */
	{0,0,0,0,0,0}, /* no udma */
	{3,2,1,0,0,0},
	{7,5,3,2,1,0},
	{7,5,3,2,1,0},
	{11,7,5,4,2,1},
	{0,0,0,0,0,0} /* not yet known, ask SiS */
};

static struct pci_dev *host_dev = NULL;

static int __init sis5513_modes_map(struct ata_channel *ch)
{
	int map = XFER_EPIO | XFER_SWDMA | XFER_MWDMA;

	switch(chipset_family) {
		case ATA_133:	/* map |= XFER_UDMA_133; */
		case ATA_100:
		case ATA_100a:
			map |= XFER_UDMA_100;
		case ATA_66:
			map |= XFER_UDMA_66;
		case ATA_33:
			map |= XFER_UDMA;
			break;
	}

	return map;
}

/*
 * Configuration functions
 */
/* Enables per-drive prefetch and postwrite */
static void config_drive_art_rwp(struct ata_device *drive)
{
	struct ata_channel *hwif = drive->channel;
	struct pci_dev *dev = hwif->pci_dev;

	u8 reg4bh = 0;
	u8 rw_prefetch = (0x11 << drive->dn);

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
static int config_art_rwp_pio(struct ata_device *drive, u8 pio)
{
	struct ata_channel *hwif = drive->channel;
	struct pci_dev *dev = hwif->pci_dev;
	u8 drive_pci, test1, test2;

#ifdef DEBUG
	sis5513_load_verify_registers(dev, "config_drive_art_rwp_pio start");
#endif

	config_drive_art_rwp(drive);

#ifdef DEBUG
	printk("SIS5513: config_drive_art_rwp_pio, drive %d, pio %d, timing %d\n",
	       drive->dn, pio, timing);
#endif

	drive_pci = 0x40 + (drive->dn << 1);

	/* register layout changed with newer ATA100 chips */
	if (chipset_family < ATA_100) {
		pci_read_config_byte(dev, drive_pci, &test1);
		pci_read_config_byte(dev, drive_pci+1, &test2);

		/* Clear active and recovery timings */
		test1 &= ~0x0F;
		test2 &= ~0x07;

		switch(pio) {
			case 4:		test1 |= 0x01; test2 |= 0x03; break;
			case 3:		test1 |= 0x03; test2 |= 0x03; break;
			case 2:		test1 |= 0x04; test2 |= 0x04; break;
			case 1:		test1 |= 0x07; test2 |= 0x06; break;
			default:	break;
		}
		pci_write_config_byte(dev, drive_pci, test1);
		pci_write_config_byte(dev, drive_pci+1, test2);
	} else {
		switch(pio) { /*   active  recovery
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

	return ide_config_drive_speed(drive, XFER_PIO_0 + min_t(u8, pio, 4));
}

static int sis5513_tune_chipset(struct ata_device *drive, u8 speed)
{
	struct ata_channel *hwif = drive->channel;
	struct pci_dev *dev	= hwif->pci_dev;

	u8 drive_pci, reg;

#ifdef DEBUG
	sis5513_load_verify_registers(dev, "sis5513_tune_chipset start");
	printk("SIS5513: sis5513_tune_chipset, drive %d, speed %d\n",
	       drive->dn, speed);
#endif

	if (drive->dn > 3)	/* FIXME: remove this  --bkz */
		return 1;

	drive_pci = 0x40 + (drive->dn << 1);

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
		case XFER_PIO_4:
		case XFER_PIO_3:
		case XFER_PIO_2:
		case XFER_PIO_1:
		case XFER_PIO_0:
			return config_art_rwp_pio(drive, speed - XFER_PIO_0);
		default:
			return config_art_rwp_pio(drive, 0);
	}
#ifdef DEBUG
	sis5513_load_verify_registers(dev, "sis5513_tune_chipset end");
#endif
	return ide_config_drive_speed(drive, speed);
}

static void sis5513_tune_drive(struct ata_device *drive, u8 pio)
{
	if (pio == 255)
		pio = ata_best_pio_mode(drive) - XFER_PIO_0;

	(void)config_art_rwp_pio(drive, min_t(u8, pio, 4));
}

/* Chip detection and general config */
static unsigned int __init pci_init_sis5513(struct pci_dev *dev)
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
			u8 latency = (chipset_family == ATA_100)? 0x80 : 0x10; /* Lacking specs */
			pci_write_config_byte(dev, PCI_LATENCY_TIMER, latency);
		}
	}

	/* Make general config ops here
	   1/ tell IDE channels to operate in Compabitility mode only
	   2/ tell old chips to allow per drive IDE timings */
	if (host_dev) {
		u8 reg;
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
	}
#ifdef DEBUG
	sis5513_load_verify_registers(dev, "pci_init_sis5513 end");
#endif
	return 0;
}

static unsigned int __init ata66_sis5513(struct ata_channel *hwif)
{
	u8 reg48h, ata66 = 0;
	u8 mask = hwif->unit ? 0x20 : 0x10;
	pci_read_config_byte(hwif->pci_dev, 0x48, &reg48h);

	if (chipset_family >= ATA_66) {
		ata66 = (reg48h & mask) ? 0 : 1;
	}
        return ata66;
}

static void __init ide_init_sis5513(struct ata_channel *hwif)
{

	hwif->irq = hwif->unit ? 15 : 14;

	hwif->udma_four = ata66_sis5513(hwif);

	hwif->tuneproc = &sis5513_tune_drive;
	hwif->speedproc = &sis5513_tune_chipset;

#ifdef CONFIG_BLK_DEV_IDEDMA
	if (hwif->dma_base && host_dev && chipset_family > ATA_16) {
		hwif->highmem = 1;
		hwif->modes_map = sis5513_modes_map(hwif);
		hwif->udma_setup = udma_generic_setup;
	}
#endif

	return;
}


/* module data table */
static struct ata_pci_device chipset __initdata = {
	.vendor = PCI_VENDOR_ID_SI,
	.device = PCI_DEVICE_ID_SI_5513,
	.init_chipset = pci_init_sis5513,
	.init_channel = ide_init_sis5513,
	.enablebits = {{0x4a,0x02,0x02}, {0x4a,0x04,0x04} },
	.bootable = ON_BOARD,
};

int __init init_sis5513(void)
{
	ata_register_chipset(&chipset);

        return 0;
}
