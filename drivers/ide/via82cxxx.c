/*
 * $Id: via82cxxx.c,v 2.1e 2000/10/03 10:01:00 vojtech Exp $
 *
 *  Copyright (c) 2000 Vojtech Pavlik
 *
 *  Based on the work of:
 *	Michel Aubry
 *	Jeff Garzik
 *	Andre Hedrick
 *
 *  Sponsored by SuSE
 */

/*
 * VIA vt82c586 IDE driver for Linux. Supports
 *
 *   vt82c586, vt82c586a, vt82c586b, vt82c596a, vt82c596b, vt82c686a, vt8231
 *
 * southbridges, which can be found in
 *
 *  VIA Apollo VP, VPX, VPX/97, VP2, VP2/97, VP3, MVP3, MVP4
 *  VIA Apollo Pro, Pro Plus, Pro 133, Pro 133A, ProMedia 601, ProSavage 605
 *  VIA Apollo KX133, KT133
 *  AMD-640, AMD-750 IronGate
 *
 * chipsets. Supports PIO 0-5, MWDMA 0-2, SWDMA 0-2 and
 * UDMA 0-5 (includes UDMA33, 66 and 100) modes. UDMA100 isn't possible
 * on any of the supported chipsets yet.
 *
 * UDMA66 and higher modes are autodetected only in case the BIOS has enabled them.
 * To force UDMA66, use 'ide0=ata66' or 'ide1=ata66' on the kernel command line.
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Should you need to contact me, the author, you can do so either by
 * e-mail - mail your message to <vojtech@suse.cz>, or by paper mail:
 * Vojtech Pavlik, Ucitelska 1576, Prague 8, 182 00 Czech Republic
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include <asm/io.h>

#include "ide_modes.h"

#define VIA_BM_BASE		0x20
#define VIA_IDE_ENABLE		0x40
#define VIA_IDE_CONFIG		0x41
#define VIA_FIFO_CONFIG		0x43
#define VIA_MISC_1		0x44
#define VIA_MISC_2		0x45
#define VIA_MISC_3		0x46
#define VIA_DRIVE_TIMING	0x48
#define VIA_ADDRESS_SETUP	0x4c
#define VIA_UDMA_TIMING		0x50
#define VIA_PRI_SECTOR_SIZE	0x60
#define VIA_SEC_SECTOR_SIZE	0x68

/*
 * VIA SouthBridge chips.
 */

static const struct {
	char *name;
	unsigned short id;
	unsigned char speed;
} via_isa_bridges[] = {
	{ "vt8231",	PCI_DEVICE_ID_VIA_8231,     XFER_UDMA_4 },
	{ "vt82c686a",	PCI_DEVICE_ID_VIA_82C686,   XFER_UDMA_4 },
	{ "vt82c596b",	PCI_DEVICE_ID_VIA_82C596,   XFER_UDMA_4 },
	{ "vt82c596a",	PCI_DEVICE_ID_VIA_82C596,   XFER_UDMA_2 },
	{ "vt82c586b",	PCI_DEVICE_ID_VIA_82C586_0, XFER_UDMA_2 },
	{ "vt82c586a",	PCI_DEVICE_ID_VIA_82C586_0, XFER_UDMA_2 },
	{ "vt82c586",	PCI_DEVICE_ID_VIA_82C586_0, XFER_MW_DMA_2 },
	{ "Unknown SouthBridge",	0,	    XFER_UDMA_2 },
};

static unsigned char via_config;
static unsigned char via_enabled;
static unsigned int via_ata66;

/*
 * PIO 0-5, MWDMA 0-2 and UDMA 0-5 timings (in nanoseconds).
 * These were taken from ATA/ATAPI-6 standard, rev 0a, except
 * for PIO 5, which is a nonstandard extension.
 *
 * Dilemma: 8-bit (register) PIO accesses need more relaxed timing.
 * Hopefully the chipset is taking care of that. If it doesn't
 * do so, define VIA_USE_8_BIT_TIMING to see if it helps.
 */

#ifndef XFER_PIO_5
#define XFER_PIO_5		0x0d
#endif

static const struct {
	int mode;
	char *name;
	short setup;	/* t1 */
	short active;	/* t2  or tD */
	short recover;	/* t2i or tK */
	short cycle;	/* t0 */
	short udma;	/* t2CYCTYP/2 */
} via_timing[] = {
	{ XFER_UDMA_5,    "UDMA5", 25,  70,  25, 120,  20 },
	{ XFER_UDMA_4,    "UDMA4", 25,  70,  25, 120,  30 },
	{ XFER_UDMA_3,    "UDMA3", 25,  70,  25, 120,  45 },

	{ XFER_UDMA_2,    "UDMA2", 25,  70,  25, 120,  60 },
	{ XFER_UDMA_1,    "UDMA1", 25,  70,  25, 120,  80 },
	{ XFER_UDMA_0,    "UDMA0", 25,  70,  25, 120, 120 },

	{ XFER_MW_DMA_2,  "MDMA2", 25,  70,  25, 120,   0 },
	{ XFER_MW_DMA_1,  "MDMA1", 45,  80,  50, 150,   0 },
	{ XFER_MW_DMA_0,  "MDMA0", 60, 215, 215, 480,   0 },

	{ XFER_SW_DMA_2,  "SDMA2", 60, 120, 120, 240,   0 },
	{ XFER_SW_DMA_1,  "SDMA1", 90, 240, 240, 480,   0 },
	{ XFER_SW_DMA_0,  "SDMA0",120, 480, 480, 960,   0 },

	{ XFER_PIO_5,     "PIO5",  20,  50,  30, 100,   0 },
	{ XFER_PIO_4,     "PIO4",  25,  70,  25, 120,   0 },
	{ XFER_PIO_3,     "PIO3",  30,  80,  70, 180,   0 },

#ifdef VIA_USE_8_BIT_TIMING
	{ XFER_PIO_2,     "PIO2",  30, 290,  60, 330,   0 },
	{ XFER_PIO_1,     "PIO1",  50, 290, 100, 383,   0 },
	{ XFER_PIO_0,     "PIO0",  70, 290, 300, 600,   0 },
#else
	{ XFER_PIO_2,     "PIO2",  30, 100,  90, 240,   0 },
	{ XFER_PIO_1,     "PIO1",  50, 125, 100, 383,   0 },
	{ XFER_PIO_0,     "PIO0",  70, 165, 150, 600,   0 },
#endif

	{ XFER_PIO_SLOW,  "SLOW", 120, 290, 240, 960,   0 },
	{ 0 }
};

/*
 * VIA /proc entry.
 */

#ifdef CONFIG_PROC_FS

#include <linux/stat.h>
#include <linux/proc_fs.h>

int via_proc = 0;
static struct pci_dev *bmide_dev, *isa_dev;
extern int (*via_display_info)(char *, char **, off_t, int); /* ide-proc.c */

static char *via_fifo[] = { " 1 ", "3/4", "1/2", "1/4" };
static char *via_control3[] = { "No limit", "64", "128", "192" };

#define via_print(format, arg...) p += sprintf(p, format "\n" , ## arg)
#define via_print_drive(name, format, arg...)\
	p += sprintf(p, name); for (i = 0; i < 4; i++) p += sprintf(p, format, ## arg); p += sprintf(p, "\n");

static int via_get_info(char *buffer, char **addr, off_t offset, int count)
{
	short pci_clock, speed[4], cycle[4], setup[4], active[4], recover[4], umul[4], uen[4], udma[4];
	struct pci_dev *dev = bmide_dev;
	unsigned int v, u, i, base;
	unsigned short c, size0, size1;
	unsigned char t, c0, c1;
	char *p = buffer;

	via_print("----------VIA BusMastering IDE Configuration----------------");

	via_print("Driver Version:                     2.1e");

	pci_read_config_byte(isa_dev, PCI_REVISION_ID, &t);
	via_print("South Bridge:                       VIA %s rev %#x", via_isa_bridges[via_config].name, t);

	pci_read_config_word(dev, PCI_COMMAND, &c);
	via_print("Command register:                   %#x", c);

	pci_read_config_byte(dev, PCI_LATENCY_TIMER, &t);
	via_print("Latency timer:                      %d", t);

	pci_clock = system_bus_clock();
	via_print("PCI clock:                          %dMHz", pci_clock);

	pci_read_config_byte(dev, VIA_MISC_1, &t);
	via_print("Master Read  Cycle IRDY:            %dws", (t & 64) >>6 );
	via_print("Master Write Cycle IRDY:            %dws", (t & 32) >> 5 );
	via_print("FIFO Output Data 1/2 Clock Advance: %s", (t & 16) ? "on" : "off" );
	via_print("BM IDE Status Register Read Retry:  %s", (t & 8) ? "on" : "off" );

	pci_read_config_byte(dev, VIA_MISC_3, &t);
	via_print("Max DRDY Pulse Width:               %s%s", via_control3[(t & 0x03)], (t & 0x03) ? "PCI clocks" : "");

	via_print("-----------------------Primary IDE-------Secondary IDE------");
	via_print("Read DMA FIFO flush:   %10s%20s", (t & 0x80) ? "on" : "off", (t & 0x40) ? "on" : "off" );
	via_print("End Sect. FIFO flush:  %10s%20s", (t & 0x20) ? "on" : "off", (t & 0x10) ? "on" : "off" );

	pci_read_config_byte(dev, VIA_IDE_CONFIG, &t);
	via_print("Prefetch Buffer:       %10s%20s", (t & 0x80) ? "on" : "off", (t & 0x20) ? "on" : "off" );
	via_print("Post Write Buffer:     %10s%20s", (t & 0x40) ? "on" : "off", (t & 0x10) ? "on" : "off" );

	pci_read_config_byte(dev, VIA_FIFO_CONFIG, &t);
	via_print("FIFO size:             %10d%20d", 16 - (((t >> 5) & 1) + ((t >> 6) & 1)) * 8,
							  (((t >> 5) & 1) + ((t >> 6) & 1)) * 8);
	via_print("Threshold Prim.:       %10s%20s", via_fifo[(t >> 2) & 3], via_fifo[t & 3]);

	pci_read_config_word(dev, VIA_PRI_SECTOR_SIZE, &size0);
	pci_read_config_word(dev, VIA_SEC_SECTOR_SIZE, &size1);
	via_print("Bytes Per Sector:      %10d%20d", size0 & 0xfff, size1 & 0xfff);

	pci_read_config_dword(dev, VIA_BM_BASE, &base);
	base = (base & 0xfff0) ;

	c0 = inb((unsigned short)base + 0x02);
	c1 = inb((unsigned short)base + 0x0a);

	via_print("Both channels togth:   %10s%20s", (c0 & 0x80) ? "no" : "yes", (c1 & 0x80) ? "no" : "yes" );

	via_print("-------------------drive0----drive1----drive2----drive3-----");

	via_print("BMDMA enabled: %10s%10s%10s%10s", (c0 & 0x20) ? "yes" : "no", (c0 & 0x40) ? "yes" : "no",
						     (c1 & 0x20) ? "yes" : "no", (c1 & 0x40) ? "yes" : "no" );

	pci_read_config_byte(dev, VIA_ADDRESS_SETUP, &t);
	pci_read_config_dword(dev, VIA_DRIVE_TIMING, &v);
	pci_read_config_dword(dev, VIA_UDMA_TIMING, &u);

	for (i = 0; i < 4; i++) {
		setup[i]   = ((t >>  ((3 - i) << 1)     ) & 0x3) + 1;
		active[i]  = ((v >> (((3 - i) << 3) + 4)) & 0xf) + 1;
		recover[i] = ((v >>  ((3 - i) << 3)     ) & 0xf) + 1;
		udma[i]    = ((u >>  ((3 - i) << 3)     ) & 0x7) + 2;
		umul[i]    = ((u >> (((3 - i) & 2) << 3)) & 0x8) ? 2 : 1;
		uen[i]     = ((u >> ((3 - i) << 3)) & 0x20) ? 1 : 0;
		speed[i] = uen[i] ? 20 * pci_clock * umul[i] / udma[i] :
				    20 * pci_clock / (active[i] + recover[i]);
		cycle[i] = uen[i] ? 1000 / (pci_clock * umul[i]) * udma[i] :
				    1000 / pci_clock * (active[i] + recover[i]);
	}

	via_print_drive("Transfer Mode: ", "%10s", uen[i] ? "UDMA" : "DMA/PIO");
	via_print_drive("Address Setup: ", "%8dns", (1000 / pci_clock) * setup[i]);
	via_print_drive("Active Pulse:  ", "%8dns", (1000 / pci_clock) * active[i]);
	via_print_drive("Recovery Time: ", "%8dns", (1000 / pci_clock) * recover[i]);
	via_print_drive("Cycle Time:    ", "%8dns", cycle[i]);
	via_print_drive("Transfer Rate: ", "%4d.%dMB/s", speed[i] / 10, speed[i] % 10);

	return p - buffer;	/* hoping it is less than 4K... */
}

#endif

#define FIT(v,min,max) ((v)>(max)?(max):((v)<(min)?(min):(v)))
#define ENOUGH(v,un) (((v)-1)/(un)+1)

#ifdef DEBUG
#define via_write_config_byte(dev,number,value) do {\
		printk(KERN_DEBUG "VP_IDE: Setting register %#x to %#x\n", number, value);\
		pci_write_config_byte(dev,number,value); } while (0)
#else
#define via_write_config_byte	pci_write_config_byte
#endif

static int via_set_speed(ide_drive_t *drive, byte speed)
{
	struct pci_dev *dev = HWIF(drive)->pci_dev;
	int i, T, err = 0;
	unsigned char t, setup, active, recover, cycle;

	if (drive->dn > 3) return -1;

	T = 1000 / system_bus_clock();

	if (speed > via_isa_bridges[via_config].speed)
		speed = via_isa_bridges[via_config].speed;

	for (i = 0; via_timing[i].mode && via_timing[i].mode != speed; i++);

#ifdef DEBUG
	printk(KERN_DEBUG "VP_IDE: Setting drive %d to %s\n", drive->dn, via_timing[i].name);
#endif

	setup   = ENOUGH(via_timing[i].setup,   T);
	active  = ENOUGH(via_timing[i].active,  T);
	recover = ENOUGH(via_timing[i].recover, T);
	cycle   = ENOUGH(via_timing[i].cycle,   T);

	if (active + recover < cycle) {
		active += (cycle - (active + recover)) / 2;
		recover = cycle - active;
	}

/*
 * PIO address setup
 */

	pci_read_config_byte(dev, VIA_ADDRESS_SETUP, &t);
	t = (t & ~(3 << ((3 - drive->dn) << 1))) | (FIT(setup - 1, 0, 3) << ((3 - drive->dn) << 1));
	via_write_config_byte(dev, VIA_ADDRESS_SETUP, t);

/*
 * PIO active & recover
 */

	via_write_config_byte(dev, VIA_DRIVE_TIMING + (3 - drive->dn),
		(FIT(active - 1, 0, 0xf) << 4) | FIT(recover - 1, 0, 0xf));

/*
 * UDMA cycle
 */

	switch(via_isa_bridges[via_config].speed) {
		case XFER_UDMA_2: t = via_timing[i].udma ? (0xe0 | (FIT(ENOUGH(via_timing[i].udma, T), 2, 5) - 2)) : 0x03; break;
		case XFER_UDMA_4: t = via_timing[i].udma ? (0xe8 | (FIT(ENOUGH(via_timing[i].udma, T/2), 2, 9) - 2)) : 0x0f; break;
        }

	if (via_isa_bridges[via_config].speed != XFER_MW_DMA_2)
		via_write_config_byte(dev, VIA_UDMA_TIMING + (3 - drive->dn), t);

/*
 * Drive init
 */

	if (!drive->init_speed) drive->init_speed = speed;
	if ((err = ide_config_drive_speed(drive, speed)))
		return err;
	drive->current_speed = speed;

	return 0;
}

static void config_chipset_for_pio(ide_drive_t *drive)
{
	short eide_pio_timing[] = {600, 383, 240, 180, 120, 100};
	signed char pio, ide_pio;

	if (drive->id->eide_pio_iordy > 0) {	/* Has timing table */
		for (pio = 5; pio >= 0; pio--)
			if (drive->id->eide_pio_iordy <= eide_pio_timing[pio])
				break;
	} else {				/* No timing table -> use mode capabilities */
		pio = (drive->id->eide_pio_modes & 4) ? 5 :
		      (drive->id->eide_pio_modes & 2) ? 4 :
		      (drive->id->eide_pio_modes & 1) ? 3 :
		      (drive->id->tPIO == 2) ? 2 :
		      (drive->id->tPIO == 1) ? 1 : 0;
	}

	ide_pio = ide_get_best_pio_mode(drive, 255, 5, NULL);
	pio = (pio >= ide_pio) ? pio : ide_pio;	/* Phew. What about the blacklist? */

	if (!pio) pio = (!drive->id->tPIO) ? XFER_PIO_0 : XFER_PIO_SLOW;
		else pio += XFER_PIO_0;

	/* Fixup because of broken ide-probe.c */
	drive->dn = HWIF(drive)->channel * 2 + (drive->select.b.unit & 1); 

	via_set_speed(drive, pio);
}

static void via82cxxx_tune_drive(ide_drive_t *drive, byte pio)
{
	if (pio == 255) {
		config_chipset_for_pio(drive);
		return;
	}
	
	if (pio > 5) pio = 5;
	
	via_set_speed(drive, XFER_PIO_0 + pio);
}

#ifdef CONFIG_BLK_DEV_IDEDMA

static int config_chipset_for_dma(ide_drive_t *drive)
{
	struct hd_driveid *id = drive->id;
	unsigned char ultra66 = eighty_ninty_three(drive);
	unsigned char speed =

		((id->dma_ultra & 0x0010) && ultra66) ? XFER_UDMA_4 :
		((id->dma_ultra & 0x0008) && ultra66) ? XFER_UDMA_3 :
		(id->dma_ultra & 0x0004) ? XFER_UDMA_2 :
		(id->dma_ultra & 0x0002) ? XFER_UDMA_1 :
		(id->dma_ultra & 0x0001) ? XFER_UDMA_0 :
		(id->dma_mword & 0x0004) ? XFER_MW_DMA_2 :
		(id->dma_mword & 0x0002) ? XFER_MW_DMA_1 :
		(id->dma_mword & 0x0001) ? XFER_MW_DMA_0 :
		(id->dma_1word & 0x0004) ? XFER_SW_DMA_2 :
		(id->dma_1word & 0x0002) ? XFER_SW_DMA_1 :
		(id->dma_1word & 0x0001) ? XFER_SW_DMA_0 : 0;
		
	if (!speed) return (int) ide_dma_off_quietly;

	via_set_speed(drive, speed);

	return (int) ide_dma_on;
}

/*
 * Almost a library function.
 */

static int config_drive_xfer_rate(ide_drive_t *drive)
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


int via82cxxx_dmaproc(ide_dma_action_t func, ide_drive_t *drive)
{
	if (func == ide_dma_check)
			return config_drive_xfer_rate(drive);
	return ide_dmaproc(func, drive);
}
#endif /* CONFIG_BLK_DEV_IDEDMA */

unsigned int __init pci_init_via82cxxx(struct pci_dev *dev, const char *name)
{
	struct pci_dev *isa = NULL;
	unsigned char f, t, m;
	unsigned int u, i;

/*
 * Find ISA bridge to see how good the IDE is.
 */

	for (via_config = 0; via_isa_bridges[via_config].id; via_config++)
		if ((isa = pci_find_device(PCI_VENDOR_ID_VIA, via_isa_bridges[via_config].id, NULL)))	
			break;
/*
 * Read revision.
 */

	if (via_isa_bridges[via_config].id == PCI_DEVICE_ID_VIA_82C586_0) {
		pci_read_config_byte(isa, PCI_REVISION_ID, &t);
		if (t < 0x30) via_config++;			/* vt82c586a */
		if (t < 0x20) via_config++;			/* vt82c586 */
	}

	if (via_isa_bridges[via_config].id == PCI_DEVICE_ID_VIA_82C596) {
		pci_read_config_byte(isa, PCI_REVISION_ID, &t);
		if (t < 0x10) via_config++;			/* vt82c596a */
	}

/*
 * Check UDMA66 mode set by BIOS.
 */

	pci_read_config_dword(dev, VIA_UDMA_TIMING, &u);

	for (i = 0; i < 4; i++) {
		pci_read_config_byte(dev, VIA_UDMA_TIMING + (3 - i), &t);
		if ((u & (0x80000 >> ((i >> 1) << 4))) && ((t & 7) < 2)) via_ata66 |= (1 << (i >> 1));
	} 

#ifdef DEBUG
	printk(KERN_DEBUG "VP_IDE: BIOS enabled ATA66: primary: %s, secondary: %s\n",
		via_ata66 & 1 ? "yes" : "no", via_ata66 & 2 ? "yes" : "no");
#endif

/*
 * Set UDMA66 double clock bits.
 */
	if (via_isa_bridges[via_config].speed == XFER_UDMA_4)
		pci_write_config_dword(dev, VIA_UDMA_TIMING, u | 0x80008);

/*
 * Set up FIFO, flush, prefetch and post-writes.
 */

	pci_read_config_dword(dev, VIA_IDE_ENABLE, &u);
	pci_read_config_byte(dev, VIA_FIFO_CONFIG, &f);
	pci_read_config_byte(dev, VIA_IDE_CONFIG, &t);
	pci_read_config_byte(dev, VIA_MISC_3, &m);

	f &= 0x90; t &= 0x0f; m &= 0x0f;

	switch (u & 3) {
		case 2: via_enabled = 1; f |= 0x06; t |= 0xc0; m |= 0xa0; break;	/* primary only, 3/4 */
		case 1: via_enabled = 2; f |= 0x69; t |= 0x30; m |= 0x50; break;	/* secondary only, 3/4 */
		case 3: via_enabled = 3;
		default:                 f |= 0x2a; t |= 0xf0; m |= 0xf0; break;	/* fifo evenly distributed */
	}

	via_write_config_byte(dev, VIA_FIFO_CONFIG, f);
	via_write_config_byte(dev, VIA_IDE_CONFIG, t);
	via_write_config_byte(dev, VIA_MISC_3, m);

/*
 * Print the boot message.
 */

	printk(KERN_INFO "VP_IDE: VIA %s IDE %s controller on pci%d:%d.%d\n",
			via_isa_bridges[via_config].name,
			via_isa_bridges[via_config].speed >= XFER_UDMA_4 ? "UDMA66" :
			via_isa_bridges[via_config].speed >= XFER_UDMA_2 ? "UDMA33" : "MWDMA16",
			dev->bus->number, PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn));
/*
 * Register /proc/ide/via entry
 */

#if defined(CONFIG_PROC_FS)
	if (!via_proc) {
		via_proc = 1;
		bmide_dev = dev;
		isa_dev = isa;
		via_display_info = &via_get_info;
	}
#endif

	return 0;
}

/*
 * Since we don't have a way to detect the 80-wire ribbon cable
 * we rely on the BIOS detection. We also check the IDENTIFY byte
 * 93 to check the drive's point of view. I think we could return
 * '1' here 
 */

unsigned int __init ata66_via82cxxx(ide_hwif_t *hwif)
{
	return ((via_enabled & via_ata66) >> hwif->channel) & 1;
}

void __init ide_init_via82cxxx(ide_hwif_t *hwif)
{
	hwif->tuneproc = &via82cxxx_tune_drive;
	hwif->speedproc = &via_set_speed;
	hwif->drives[0].autotune = 1;
	hwif->drives[1].autotune = 1;
	hwif->autodma = 0;

#ifdef CONFIG_BLK_DEV_IDEDMA
	if (hwif->dma_base) {
		hwif->dmaproc = &via82cxxx_dmaproc;
		hwif->autodma = 1;
	}
#endif /* CONFIG_BLK_DEV_IDEDMA */
}

/*
 * We allow the BM-DMA driver only work on enabled interfaces.
 */

void __init ide_dmacapable_via82cxxx(ide_hwif_t *hwif, unsigned long dmabase)
{
	if ((via_enabled >> hwif->channel) & 1)
		ide_setup_dma(hwif, dmabase, 8);
}
