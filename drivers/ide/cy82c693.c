/**** vi:set ts=8 sts=8 sw=8:************************************************
 *
 * linux/drivers/ide/cy82c693.c		Version 0.34	Dec. 13, 1999
 *
 *  Copyright (C) 1998-2000 Andreas S. Krebs (akrebs@altavista.net), Maintainer
 *  Copyright (C) 1998-2000 Andre Hedrick <andre@linux-ide.org>, Integrater
 *
 * CYPRESS CY82C693 chipset IDE controller
 *
 * The CY82C693 chipset is used on Digital's PC-Alpha 164SX boards.
 * Writting the driver was quite simple, since most of the job is
 * done by the generic pci-ide support.
 * The hard part was finding the CY82C693's datasheet on Cypress's
 * web page :-(. But Altavista solved this problem :-).
 *
 *
 * Notes:
 * - I recently got a 16.8G IBM DTTA, so I was able to test it with
 *   a large and fast disk - the results look great, so I'd say the
 *   driver is working fine :-)
 *   hdparm -t reports 8.17 MB/sec at about 6% CPU usage for the DTTA
 * - this is my first linux driver, so there's probably a lot  of room
 *   for optimizations and bug fixing, so feel free to do it.
 * - I had some problems with my IBM DHEA with PIO modes < 2
 *   (lost interrupts) ?????
 *   FIXME: probably because we set wrong timings for 8bit  --bkz
 * - first tests with DMA look okay, they seem to work, but there is a
 *   problem with sound - the BusMaster IDE TimeOut should fixed this
 *
 *
 * History:
 * AMH@1999-08-24: v0.34 init_cy82c693_chip moved to pci_init_cy82c693
 * ASK@1999-01-23: v0.33 made a few minor code clean ups
 *                       removed DMA clock speed setting by default
 *                       added boot message
 * ASK@1998-11-01: v0.32 added support to set BusMaster IDE TimeOut
 *                       added support to set DMA Controller Clock Speed
 * ASK@1998-10-31: v0.31 fixed problem with setting to high DMA modes on some drive
 * ASK@1998-10-29: v0.3 added support to set DMA modes
 * ASK@1998-10-28: v0.2 added support to set PIO modes
 * ASK@1998-10-27: v0.1 first version - chipset detection
 *
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/ide.h>

#include <asm/io.h>

#include "ata-timing.h"
#include "pcihost.h"

/* the current version */
#define CY82_VERSION	"CY82C693U driver v0.34 99-13-12 Andreas S. Krebs (akrebs@altavista.net)"

/*
 *	The following are used to debug the driver.
 */
#define	CY82C693_DEBUG_LOGS	0
#define	CY82C693_DEBUG_INFO	0

/* define CY82C693_SETDMA_CLOCK to set DMA Controller Clock Speed to ATCLK */
#undef CY82C693_SETDMA_CLOCK

/*
 * note: the value for busmaster timeout is tricky and i got it by trial and error !
 *       using a to low value will cause DMA timeouts and drop IDE performance
 *       using a to high value will cause audio playback to scatter
 *       if you know a better value or how to calc it, please let me know
 */
#define BUSMASTER_TIMEOUT	0x50	/* twice the value written in cy82c693ub datasheet */
/*
 * the value above was tested on my machine and it seems to work okay
 */

/* here are the offset definitions for the registers */
#define CY82_IDE_CMDREG		0x04
#define CY82_IDE_ADDRSETUP	0x48

#define CYPRESS_TIMINGS		0x4C

#define CY82_IDE_MASTER_IOR	0x4C
#define CY82_IDE_MASTER_IOW	0x4D
#define CY82_IDE_SLAVE_IOR	0x4E
#define CY82_IDE_SLAVE_IOW	0x4F
#define CY82_IDE_MASTER_8BIT	0x50
#define CY82_IDE_SLAVE_8BIT	0x51

#define CY82_INDEX_PORT		0x22
#define CY82_DATA_PORT		0x23

#define CY82_INDEX_CTRLREG1	0x01
#define CY82_INDEX_CHANNEL0	0x30
#define CY82_INDEX_CHANNEL1	0x31
#define CY82_INDEX_TIMEOUT	0x32

/* the max PIO mode - from datasheet */
#define CY82C693_MAX_PIO	4

/* the min and max PCI bus speed in MHz - from datasheet */
#define CY82C963_MIN_BUS_SPEED	25
#define CY82C963_MAX_BUS_SPEED	33

/* the struct for the PIO mode timings (in clocks) */
typedef struct pio_clocks_s {
	u8 address_time;	/* Address setup */
	/* 0xF0=Active/data, 0x0F=Recovery */
	u8 time_16r;		/* 16bit IOR */
	u8 time_16w;		/* 16bit IOW */
	u8 time_8;		/* 8bit */
} pio_clocks_t;

/*
 * calc clocks using bus_speed
 * returns (rounded up) time in bus clocks for time in ns
 */
static u8 calc_clk(int time, int bus_speed)
{
	int clocks;

	clocks = (time*bus_speed+999999)/1000000 -1;

	if (clocks < 0)
		clocks = 0;

	if (clocks > 0x0F)
		clocks = 0x0F;

	return (u8)clocks;
}

/*
 * compute the values for the clock registers for PIO
 * mode and pci_clk [MHz] speed
 *
 * NOTE: for mode 0,1 and 2 drives 8-bit IDE command control registers are used
 *       for mode 3 and 4 drives 8 and 16-bit timings are the same
 *
 */
/* FIXME: use generic ata-timings library  --bkz */
static void compute_clocks(u8 pio, pio_clocks_t *p_pclk)
{
	struct ata_timing *t;
	int clk1, clk2;

	t = ata_timing_data(XFER_PIO_0 + pio);

	/* we don't check against CY82C693's min and max speed,
	 * so you can play with the idebus=xx parameter
	 * FIXME: warn about going out of specification  --bkz
	 */

	if (pio > CY82C693_MAX_PIO)
		pio = CY82C693_MAX_PIO;

	/* address setup */
	p_pclk->address_time = calc_clk(t->setup, system_bus_speed);

	/* active */
	clk1 = calc_clk(t->active, system_bus_speed);

	/* FIXME: check why not t->cycle - t->active ?  --bkz */
	/* recovery */
	clk2 = calc_clk(t->cycle - t->active - t->setup, system_bus_speed);

	clk1 = (clk1 << 4) | clk2; /* combine active and recovery clocks */

	/* note: we use the same values for 16bit IOR and IOW
         *	those are all the same, since I don't have other
	 *	timings than those from ata-timing.h
	 */
	p_pclk->time_16w = p_pclk->time_16r = clk1;

	/* FIXME: ugh...  --bkz */
	/* what are good values for 8bit ?? */
	p_pclk->time_8 = clk1;
}

#ifdef CONFIG_BLK_DEV_IDEDMA
/*
 * set DMA mode a specific channel for CY82C693
 */
static void cy82c693_dma_enable(struct ata_device *drive, int mode, int single)
{
        byte index;
	byte data;

        if (mode>2)	/* make sure we set a valid mode */
		mode = 2;

	if (mode > drive->id->tDMA)  /* to be absolutly sure we have a valid mode */
		mode = drive->id->tDMA;

        index = (drive->channel->unit == 0) ? CY82_INDEX_CHANNEL0 : CY82_INDEX_CHANNEL1;

#if CY82C693_DEBUG_LOGS
	/* for debug let's show the previous values */

	OUT_BYTE(index, CY82_INDEX_PORT);
	data = IN_BYTE(CY82_DATA_PORT);

	printk (KERN_INFO "%s (ch=%d, dev=%d): DMA mode is %d (single=%d)\n", drive->name, drive->channel->unit, drive->select.b.unit, (data&0x3), ((data>>2)&1));
#endif

	data = (byte)mode|(byte)(single<<2);

	OUT_BYTE(index, CY82_INDEX_PORT);
	OUT_BYTE(data, CY82_DATA_PORT);

#if CY82C693_DEBUG_INFO
	printk (KERN_INFO "%s (ch=%d, dev=%d): set DMA mode to %d (single=%d)\n", drive->name, drive->channel->unit, drive->select.b.unit, mode, single);
#endif

	/*
	 * note: below we set the value for Bus Master IDE TimeOut Register
	 * I'm not absolutly sure what this does, but it solved my problem
	 * with IDE DMA and sound, so I now can play sound and work with
	 * my IDE driver at the same time :-)
	 *
	 * If you know the correct (best) value for this register please
	 * let me know - ASK
	 */

	data = BUSMASTER_TIMEOUT;
	OUT_BYTE(CY82_INDEX_TIMEOUT, CY82_INDEX_PORT);
	OUT_BYTE(data, CY82_DATA_PORT);

#if CY82C693_DEBUG_INFO
	printk (KERN_INFO "%s: Set IDE Bus Master TimeOut Register to 0x%X\n", drive->name, data);
#endif
}

/*
 * used to set DMA mode for CY82C693 (single and multi modes)
 */
static int cy82c693_udma_setup(struct ata_device *drive, int map)
{
	/*
	 * Set dma mode for drive everything else is done by the defaul func.
	 */
	struct hd_driveid *id = drive->id;

#if CY82C693_DEBUG_INFO
	printk (KERN_INFO "dma_on: %s\n", drive->name);
#endif

	if (id != NULL) {
		/* Enable DMA on any drive that has DMA (multi or single) enabled */
		if (id->field_valid & 2) {       /* regular DMA */
			int mmode, smode;

			mmode = id->dma_mword & (id->dma_mword >> 8);
			smode = id->dma_1word & (id->dma_1word >> 8);

			if (mmode != 0)
				cy82c693_dma_enable(drive, (mmode >> 1), 0); /* enable multi */
			else if (smode != 0)
				cy82c693_dma_enable(drive, (smode >> 1), 1); /* enable single */
		}
	}
	udma_enable(drive, 1, 1);

	return 0;
}
#endif

/*
 * tune ide drive - set PIO mode
 */
static void cy82c693_tune_drive(struct ata_device *drive, byte pio)
{
	struct ata_channel *hwif = drive->channel;
	struct pci_dev *dev = hwif->pci_dev;
	pio_clocks_t pclk;
	unsigned int addrCtrl;
	u8 ior, iow, bit8;

	/* FIXME: probaly broken  --bkz */
	/* select primary or secondary channel */
	if (hwif->index > 0) {  /* drive is on the secondary channel */
		dev = pci_find_slot(dev->bus->number, dev->devfn+1);
		if (!dev) {
			printk(KERN_ERR "%s: tune_drive: Cannot find secondary interface!\n", drive->name);
			return;
		}
	}

	if (drive->select.b.unit == 0) {
		ior = CY82_IDE_MASTER_IOR;
		iow = CY82_IDE_MASTER_IOW;
		bit8 = CY82_IDE_MASTER_8BIT;
	} else {
		ior = CY82_IDE_SLAVE_IOR;
		iow = CY82_IDE_SLAVE_IOW;
		bit8 = CY82_IDE_SLAVE_8BIT;
	}

#if CY82C693_DEBUG_LOGS
	/* for debug let's show the register values */

	/*
	 * get address setup control register
	 * mine master or slave data
	 */
	pci_read_config_dword(dev, CY82_IDE_ADDRSETUP, &addrCtrl);

	if (drive->select.b.unit == 0)
		addrCtrl &= 0x0F;
	else {
		addrCtrl &= 0xF0;
		addrCtrl >>= 4;
	}

	/* now let's get the remaining registers */
	pci_read_config_byte(dev, ior, &pclk.time_16r);
	pci_read_config_byte(dev, iow, &pclk.time_16w);
	pci_read_config_byte(dev, bit8, &pclk.time_8);

	printk(KERN_INFO "%s (ch=%d, dev=%d): PIO timing is (addr=0x%X,"
			 " ior=0x%X, iow=0x%X, 8bit=0x%X)\n",
			 drive->name, hwif->unit, drive->select.b.unit,
			 addrCtrl, pclk.time_16r, pclk.time_16w, pclk.time_8);
#endif /* CY82C693_DEBUG_LOGS */

        /* first let's calc the pio modes */
	pio = ata_timing_mode(drive, XFER_PIO | XFER_EPIO) - XFER_PIO_0;

#if CY82C693_DEBUG_INFO
	printk (KERN_INFO "%s: Selected PIO mode %d\n", drive->name, pio);
#endif

        compute_clocks(pio, &pclk);  /* let's calc the values for this PIO mode */

	/*
	 * set address setup control register
	 */
	pci_read_config_dword(dev, CY82_IDE_ADDRSETUP, &addrCtrl);
	if (drive->select.b.unit == 0) {
		addrCtrl &= (~0x0F);
		addrCtrl |= (unsigned int)pclk.address_time;
	} else {
		addrCtrl &= (~0xF0);
		addrCtrl |= ((unsigned int)pclk.address_time<<4);
	}
	pci_write_config_dword(dev, CY82_IDE_ADDRSETUP, addrCtrl);

	/* now let's set the remaining registers */
	pci_write_config_byte(dev, ior, pclk.time_16r);
	pci_write_config_byte(dev, iow, pclk.time_16w);
	pci_write_config_byte(dev, bit8, pclk.time_8);

#if CY82C693_DEBUG_INFO
	printk(KERN_INFO "%s (ch=%d, dev=%d): set PIO timing to (addr=0x%X,"
			 " ior=0x%X, iow=0x%X, 8bit=0x%X)\n", drive->name,
			 hwif->unit, drive->select.b.unit, addrCtrl,
			 pclk.time_16r, pclk.time_16w, pclk.time_8);
#endif
}

static unsigned int __init pci_init_cy82c693(struct pci_dev *dev)
{
#ifdef CY82C693_SETDMA_CLOCK
	u8 data;
#endif

	/* write info about this verion of the driver */
	printk (KERN_INFO CY82_VERSION "\n");

#ifdef CY82C693_SETDMA_CLOCK
       /* okay let's set the DMA clock speed */

        OUT_BYTE(CY82_INDEX_CTRLREG1, CY82_INDEX_PORT);
        data = IN_BYTE(CY82_DATA_PORT);

#if CY82C693_DEBUG_INFO
	printk (KERN_INFO "%s: Peripheral Configuration Register: 0x%X\n", dev->name, data);
#endif

        /*
	 * for some reason sometimes the DMA controller
	 * speed is set to ATCLK/2 ???? - we fix this here
	 *
	 * note: i don't know what causes this strange behaviour,
	 *       but even changing the dma speed doesn't solve it :-(
	 *       the ide performance is still only half the normal speed
	 *
	 *       if anybody knows what goes wrong with my machine, please
	 *       let me know - ASK
         */

	data |= 0x03;

        OUT_BYTE(CY82_INDEX_CTRLREG1, CY82_INDEX_PORT);
        OUT_BYTE(data, CY82_DATA_PORT);

# if CY82C693_DEBUG_INFO
	printk (KERN_INFO "%s: New Peripheral Configuration Register: 0x%X\n", dev->name, data);
# endif

#endif
	return 0;
}

/*
 * the init function - called for each ide channel once
 */
static void __init ide_init_cy82c693(struct ata_channel *hwif)
{
	hwif->chipset = ide_cy82c693;
	hwif->tuneproc = cy82c693_tune_drive;
	hwif->drives[0].autotune = 1;
	hwif->drives[1].autotune = 1;

#ifdef CONFIG_BLK_DEV_IDEDMA
	if (hwif->dma_base) {
		hwif->highmem = 1;
		hwif->udma_setup = cy82c693_udma_setup;
	}
#endif
}


/* module data table */
static struct ata_pci_device chipset __initdata = {
	.vendor = PCI_VENDOR_ID_CONTAQ,
	.device = PCI_DEVICE_ID_CONTAQ_82C693,
	.init_chipset = pci_init_cy82c693,
	.init_channel = ide_init_cy82c693,
	.bootable = ON_BOARD,
	.flags = ATA_F_DMA
};

int __init init_cy82c693(void)
{
	ata_register_chipset(&chipset);

	return 0;
}
