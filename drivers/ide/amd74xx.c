/*
 * $Id: amd74xx.c,v 2.8 2002/03/14 11:52:20 vojtech Exp $
 *
 *  Copyright (c) 2000-2002 Vojtech Pavlik
 *
 *  Based on the work of:
 *	Andre Hedrick
 */

/*
 * AMD 755/756/766/8111 IDE driver for Linux.
 *
 * UDMA66 and higher modes are autoenabled only in case the BIOS has detected a
 * 80 wire cable. To ignore the BIOS data and assume the cable is present, use
 * 'ide0=ata66' or 'ide1=ata66' on the kernel command line.
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
 * e-mail - mail your message to <vojtech@ucw.cz>, or by paper mail:
 * Vojtech Pavlik, Simunkova 1594, Prague 8, 182 00 Czech Republic
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/blkdev.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/ide.h>
#include <asm/io.h>

#include "ata-timing.h"

#define AMD_IDE_ENABLE		(0x00 + amd_config->base)
#define AMD_IDE_CONFIG		(0x01 + amd_config->base)
#define AMD_CABLE_DETECT	(0x02 + amd_config->base)
#define AMD_DRIVE_TIMING	(0x08 + amd_config->base)
#define AMD_8BIT_TIMING		(0x0e + amd_config->base)
#define AMD_ADDRESS_SETUP	(0x0c + amd_config->base)
#define AMD_UDMA_TIMING		(0x10 + amd_config->base)

#define AMD_UDMA		0x07
#define AMD_UDMA_33		0x01
#define AMD_UDMA_66		0x02
#define AMD_UDMA_100		0x03
#define AMD_BAD_SWDMA		0x08
#define AMD_BAD_FIFO		0x10

/*
 * AMD SouthBridge chips.
 */

static struct amd_ide_chip {
	unsigned short id;
	unsigned char rev;
	unsigned int base;
	unsigned char flags;
} amd_ide_chips[] = {
	{ PCI_DEVICE_ID_AMD_8111_IDE,  0x00, 0x40, AMD_UDMA_100 },			/* AMD-8111 */
	{ PCI_DEVICE_ID_AMD_OPUS_7441, 0x00, 0x40, AMD_UDMA_100 },			/* AMD-768 Opus */
	{ PCI_DEVICE_ID_AMD_VIPER_7411, 0x00, 0x40, AMD_UDMA_100 | AMD_BAD_FIFO },	/* AMD-766 Viper */
	{ PCI_DEVICE_ID_AMD_VIPER_7409, 0x07, 0x40, AMD_UDMA_66 },			/* AMD-756/c4+ Viper */
	{ PCI_DEVICE_ID_AMD_VIPER_7409, 0x00, 0x40, AMD_UDMA_66 | AMD_BAD_SWDMA },	/* AMD-756 Viper */
	{ PCI_DEVICE_ID_AMD_COBRA_7401, 0x00, 0x40, AMD_UDMA_33 | AMD_BAD_SWDMA },	/* AMD-755 Cobra */
	{ PCI_DEVICE_ID_NVIDIA_NFORCE_IDE, 0x00, 0x50, AMD_UDMA_100 },			/* nVidia nForce */
	{ 0 }
};

static struct amd_ide_chip *amd_config;
static unsigned char amd_enabled;
static unsigned int amd_80w;
static unsigned int amd_clock;

static unsigned char amd_cyc2udma[] = { 6, 6, 5, 4, 0, 1, 1, 2, 2, 3, 3 };
static unsigned char amd_udma2cyc[] = { 4, 6, 8, 10, 3, 2, 1, 1 };
static char *amd_dma[] = { "MWDMA16", "UDMA33", "UDMA66", "UDMA100" };

/*
 * AMD /proc entry.
 */

#ifdef CONFIG_PROC_FS

#include <linux/stat.h>
#include <linux/proc_fs.h>

byte amd74xx_proc;
int amd_base;
static struct pci_dev *bmide_dev;
extern int (*amd74xx_display_info)(char *, char **, off_t, int); /* ide-proc.c */

#define amd_print(format, arg...) p += sprintf(p, format "\n" , ## arg)
#define amd_print_drive(name, format, arg...)\
	p += sprintf(p, name); for (i = 0; i < 4; i++) p += sprintf(p, format, ## arg); p += sprintf(p, "\n");

static int amd_get_info(char *buffer, char **addr, off_t offset, int count)
{
	int speed[4], cycle[4], setup[4], active[4], recover[4], den[4],
		 uen[4], udma[4], active8b[4], recover8b[4];
	struct pci_dev *dev = bmide_dev;
	unsigned int v, u, i;
	unsigned short c, w;
	unsigned char t;
	char *p = buffer;

	amd_print("----------AMD BusMastering IDE Configuration----------------");

	amd_print("Driver Version:                     2.8");
	amd_print("South Bridge:                       %s", bmide_dev->name);

	pci_read_config_byte(dev, PCI_REVISION_ID, &t);
	amd_print("Revision:                           IDE %#x", t);
	amd_print("Highest DMA rate:                   %s", amd_dma[amd_config->flags & AMD_UDMA]);

	amd_print("BM-DMA base:                        %#x", amd_base);
	amd_print("PCI clock:                          %d.%dMHz", amd_clock / 1000, amd_clock / 100 % 10);
	
	amd_print("-----------------------Primary IDE-------Secondary IDE------");

	pci_read_config_byte(dev, AMD_IDE_CONFIG, &t);
	amd_print("Prefetch Buffer:       %10s%20s", (t & 0x80) ? "yes" : "no", (t & 0x20) ? "yes" : "no");
	amd_print("Post Write Buffer:     %10s%20s", (t & 0x40) ? "yes" : "no", (t & 0x10) ? "yes" : "no");

	pci_read_config_byte(dev, AMD_IDE_ENABLE, &t);
	amd_print("Enabled:               %10s%20s", (t & 0x02) ? "yes" : "no", (t & 0x01) ? "yes" : "no");

	c = inb(amd_base + 0x02) | (inb(amd_base + 0x0a) << 8);
	amd_print("Simplex only:          %10s%20s", (c & 0x80) ? "yes" : "no", (c & 0x8000) ? "yes" : "no");

	amd_print("Cable Type:            %10s%20s", (amd_80w & 1) ? "80w" : "40w", (amd_80w & 2) ? "80w" : "40w");

	if (!amd_clock)
                return p - buffer;

	amd_print("-------------------drive0----drive1----drive2----drive3-----");

	pci_read_config_byte(dev, AMD_ADDRESS_SETUP, &t);
	pci_read_config_dword(dev, AMD_DRIVE_TIMING, &v);
	pci_read_config_word(dev, AMD_8BIT_TIMING, &w);
	pci_read_config_dword(dev, AMD_UDMA_TIMING, &u);

	for (i = 0; i < 4; i++) {
		setup[i]     = ((t >> ((3 - i) << 1)) & 0x3) + 1;
		recover8b[i] = ((w >> ((1 - (i >> 1)) << 3)) & 0xf) + 1;
		active8b[i]  = ((w >> (((1 - (i >> 1)) << 3) + 4)) & 0xf) + 1;
		active[i]    = ((v >> (((3 - i) << 3) + 4)) & 0xf) + 1;
		recover[i]   = ((v >> ((3 - i) << 3)) & 0xf) + 1;

		udma[i] = amd_udma2cyc[((u >> ((3 - i) << 3)) & 0x7)];
		uen[i]  = ((u >> ((3 - i) << 3)) & 0x40) ? 1 : 0;
		den[i]  = (c & ((i & 1) ? 0x40 : 0x20) << ((i & 2) << 2));

		if (den[i] && uen[i] && udma[i] == 1) {
			speed[i] = amd_clock * 3;
			cycle[i] = 666666 / amd_clock;
			continue;
		}

		speed[i] = 4 * amd_clock / ((den[i] && uen[i]) ? udma[i] : (active[i] + recover[i]) * 2);
		cycle[i] = 1000000 * ((den[i] && uen[i]) ? udma[i] : (active[i] + recover[i]) * 2) / amd_clock / 2;
	}

	amd_print_drive("Transfer Mode: ", "%10s", den[i] ? (uen[i] ? "UDMA" : "DMA") : "PIO");

	amd_print_drive("Address Setup: ", "%8dns", 1000000 * setup[i] / amd_clock);
	amd_print_drive("Cmd Active:    ", "%8dns", 1000000 * active8b[i] / amd_clock);
	amd_print_drive("Cmd Recovery:  ", "%8dns", 1000000 * recover8b[i] / amd_clock);
	amd_print_drive("Data Active:   ", "%8dns", 1000000 * active[i] / amd_clock);
	amd_print_drive("Data Recovery: ", "%8dns", 1000000 * recover[i] / amd_clock);
	amd_print_drive("Cycle Time:    ", "%8dns", cycle[i]);
	amd_print_drive("Transfer Rate: ", "%4d.%dMB/s", speed[i] / 1000, speed[i] / 100 % 10);

	return p - buffer;	/* hoping it is less than 4K... */
}

#endif

/*
 * amd_set_speed() writes timing values to the chipset registers
 */

static void amd_set_speed(struct pci_dev *dev, unsigned char dn, struct ata_timing *timing)
{
	unsigned char t;

	pci_read_config_byte(dev, AMD_ADDRESS_SETUP, &t);
	t = (t & ~(3 << ((3 - dn) << 1))) | ((FIT(timing->setup, 1, 4) - 1) << ((3 - dn) << 1));
	pci_write_config_byte(dev, AMD_ADDRESS_SETUP, t);

	pci_write_config_byte(dev, AMD_8BIT_TIMING + (1 - (dn >> 1)),
		((FIT(timing->act8b, 1, 16) - 1) << 4) | (FIT(timing->rec8b, 1, 16) - 1));

	pci_write_config_byte(dev, AMD_DRIVE_TIMING + (3 - dn),
		((FIT(timing->active, 1, 16) - 1) << 4) | (FIT(timing->recover, 1, 16) - 1));

	switch (amd_config->flags & AMD_UDMA) {
		case AMD_UDMA_33:  t = timing->udma ? (0xc0 | (FIT(timing->udma, 2, 5) - 2)) : 0x03; break;
		case AMD_UDMA_66:  t = timing->udma ? (0xc0 | amd_cyc2udma[FIT(timing->udma, 2, 10)]) : 0x03; break;
		case AMD_UDMA_100: t = timing->udma ? (0xc0 | amd_cyc2udma[FIT(timing->udma, 1, 10)]) : 0x03; break;
		default: return;
	}

	pci_write_config_byte(dev, AMD_UDMA_TIMING + (3 - dn), t);
}

/*
 * amd_set_drive() computes timing values configures the drive and
 * the chipset to a desired transfer mode. It also can be called
 * by upper layers.
 */

static int amd_set_drive(ide_drive_t *drive, unsigned char speed)
{
	ide_drive_t *peer = drive->channel->drives + (~drive->dn & 1);
	struct ata_timing t, p;
	int T, UT;

	if (speed != XFER_PIO_SLOW && speed != drive->current_speed)
		if (ide_config_drive_speed(drive, speed))
			printk(KERN_WARNING "ide%d: Drive %d didn't accept speed setting. Oh, well.\n",
				drive->dn >> 1, drive->dn & 1);

	T = 1000000000 / amd_clock;
	UT = T / min_t(int, max_t(int, amd_config->flags & AMD_UDMA, 1), 2);

	ata_timing_compute(drive, speed, &t, T, UT);

	if (peer->present) {
		ata_timing_compute(peer, peer->current_speed, &p, T, UT);
		ata_timing_merge(&p, &t, &t, IDE_TIMING_8BIT);
	}

	if (speed == XFER_UDMA_5 && amd_clock <= 33333) t.udma = 1;

	amd_set_speed(drive->channel->pci_dev, drive->dn, &t);

	if (!drive->init_speed)	
		drive->init_speed = speed;
	drive->current_speed = speed;

	return 0;
}

/*
 * amd74xx_tune_drive() is a callback from upper layers for
 * PIO-only tuning.
 */

static void amd74xx_tune_drive(ide_drive_t *drive, unsigned char pio)
{
	if (!((amd_enabled >> drive->channel->unit) & 1))
		return;

	if (pio == 255) {
		amd_set_drive(drive, ata_timing_mode(drive, XFER_PIO | XFER_EPIO));
		return;
	}

	amd_set_drive(drive, XFER_PIO_0 + min_t(byte, pio, 5));
}

#ifdef CONFIG_BLK_DEV_IDEDMA
int amd74xx_dmaproc(struct ata_device *drive)
{
	short w80 = drive->channel->udma_four;

	short speed = ata_timing_mode(drive,
			XFER_PIO | XFER_EPIO | XFER_MWDMA | XFER_UDMA |
			((amd_config->flags & AMD_BAD_SWDMA) ? 0 : XFER_SWDMA) |
			(w80 && (amd_config->flags & AMD_UDMA) >= AMD_UDMA_66 ? XFER_UDMA_66 : 0) |
			(w80 && (amd_config->flags & AMD_UDMA) >= AMD_UDMA_100 ? XFER_UDMA_100 : 0));

	amd_set_drive(drive, speed);

	udma_enable(drive, drive->channel->autodma && (speed & XFER_MODE) != XFER_PIO, 0);

	return 0;
}
#endif

/*
 * The initialization callback. Here we determine the IDE chip type
 * and initialize its drive independent registers.
 */

unsigned int __init pci_init_amd74xx(struct pci_dev *dev, const char *name)
{
	unsigned char t;
	unsigned int u;
	int i;

/*
 * Find out what AMD IDE is this.
 */

	for (amd_config = amd_ide_chips; amd_config->id; amd_config++) {
			pci_read_config_byte(dev, PCI_REVISION_ID, &t);
			if (dev->device == amd_config->id && t >= amd_config->rev)
				break;
		}

	if (!amd_config->id) {
		printk(KERN_WARNING "AMD_IDE: Unknown AMD IDE Chip, contact Vojtech Pavlik <vojtech@ucw.cz>\n");
		return -ENODEV;
	}

/*
 * Check 80-wire cable presence.
 */

	switch (amd_config->flags & AMD_UDMA) {

		case AMD_UDMA_100:
			pci_read_config_byte(dev, AMD_CABLE_DETECT, &t);
			amd_80w = ((u & 0x3) ? 1 : 0) | ((u & 0xc) ? 2 : 0);
			for (i = 24; i >= 0; i -= 8)
				if (((u >> i) & 4) && !(amd_80w & (1 << (1 - (i >> 4))))) {
					printk(KERN_WARNING "AMD_IDE: Bios didn't set cable bits corectly. Enabling workaround.\n");
					amd_80w |= (1 << (1 - (i >> 4)));
				}
			break;

		case AMD_UDMA_66:
			pci_read_config_dword(dev, AMD_UDMA_TIMING, &u);
			for (i = 24; i >= 0; i -= 8)
				if ((u >> i) & 4)
					amd_80w |= (1 << (1 - (i >> 4)));
			break;
	}

	pci_read_config_dword(dev, AMD_IDE_ENABLE, &u);
	amd_enabled = ((u & 1) ? 2 : 0) | ((u & 2) ? 1 : 0);

/*
 * Take care of prefetch & postwrite.
 */

	pci_read_config_byte(dev, AMD_IDE_CONFIG, &t);
	pci_write_config_byte(dev, AMD_IDE_CONFIG,
		(amd_config->flags & AMD_BAD_FIFO) ? (t & 0x0f) : (t | 0xf0));

/*
 * Determine the system bus clock.
 */

	amd_clock = system_bus_speed * 1000;

	switch (amd_clock) {
		case 33000: amd_clock = 33333; break;
		case 37000: amd_clock = 37500; break;
		case 41000: amd_clock = 41666; break;
	}

	if (amd_clock < 20000 || amd_clock > 50000) {
		printk(KERN_WARNING "AMD_IDE: User given PCI clock speed impossible (%d), using 33 MHz instead.\n", amd_clock);
		printk(KERN_WARNING "AMD_IDE: Use ide0=ata66 if you want to assume 80-wire cable\n");
		amd_clock = 33333;
	}

/*
 * Print the boot message.
 */

	pci_read_config_byte(dev, PCI_REVISION_ID, &t);
	printk(KERN_INFO "AMD_IDE: %s (rev %02x) %s controller on pci%s\n",
		dev->name, t, amd_dma[amd_config->flags & AMD_UDMA], dev->slot_name);

/*
 * Register /proc/ide/amd74xx entry
 */

#ifdef CONFIG_PROC_FS
	if (!amd74xx_proc) {
		amd_base = pci_resource_start(dev, 4);
		bmide_dev = dev;
		amd74xx_display_info = &amd_get_info;
		amd74xx_proc = 1;
	}
#endif

	return 0;
}

unsigned int __init ata66_amd74xx(struct ata_channel *hwif)
{
	return ((amd_enabled & amd_80w) >> hwif->unit) & 1;
}

void __init ide_init_amd74xx(struct ata_channel *hwif)
{
	int i;

	hwif->tuneproc = &amd74xx_tune_drive;
	hwif->speedproc = &amd_set_drive;
	hwif->autodma = 0;

	hwif->io_32bit = 1;
	hwif->unmask = 1;

	for (i = 0; i < 2; i++) {
		hwif->drives[i].autotune = 1;
		hwif->drives[i].dn = hwif->unit * 2 + i;
	}

#ifdef CONFIG_BLK_DEV_IDEDMA
	if (hwif->dma_base) {
		hwif->highmem = 1;
		hwif->XXX_udma = amd74xx_dmaproc;
# ifdef CONFIG_IDEDMA_AUTO
		if (!noautodma)
			hwif->autodma = 1;
# endif
	}
#endif
}

/*
 * We allow the BM-DMA driver only work on enabled interfaces.
 */

void __init ide_dmacapable_amd74xx(struct ata_channel *hwif, unsigned long dmabase)
{
	if ((amd_enabled >> hwif->unit) & 1)
		ide_setup_dma(hwif, dmabase, 8);
}
