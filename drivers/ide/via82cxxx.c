/**** vi:set ts=8 sts=8 sw=8:************************************************
 *
 * $Id: via82cxxx.c,v 3.34 2002/02/12 11:26:11 vojtech Exp $
 *
 *  Copyright (c) 2000-2001 Vojtech Pavlik
 *
 *  Based on the work of:
 *	Michel Aubry
 *	Jeff Garzik
 *	Andre Hedrick
 */

/*
 * VIA IDE driver for Linux. Supports
 *
 *   vt82c576, vt82c586, vt82c586a, vt82c586b, vt82c596a, vt82c596b,
 *   vt82c686, vt82c686a, vt82c686b, vt8231, vt8233, vt8233c, vt8233a
 *
 * southbridges, which can be found in
 *
 *  VIA Apollo Master, VP, VP2, VP2/97, VP3, VPX, VPX/97, MVP3, MVP4, P6, Pro,
 *    ProII, ProPlus, Pro133, Pro133+, Pro133A, Pro133A Dual, Pro133T, Pro133Z,
 *    PLE133, PLE133T, Pro266, Pro266T, ProP4X266, PM601, PM133, PN133, PL133T,
 *    PX266, PM266, KX133, KT133, KT133A, KT133E, KLE133, KT266, KX266, KM133,
 *    KM133A, KL133, KN133, KM266
 *  PC-Chips VXPro, VXPro+, VXTwo, TXPro-III, TXPro-AGP, AGPPro, ViaGra, BXToo,
 *    BXTel, BXpert
 *  AMD 640, 640 AGP, 750 IronGate, 760, 760MP
 *  ETEQ 6618, 6628, 6629, 6638
 *  Micron Samurai
 *
 * chipsets. Supports
 *
 *   PIO 0-5, MWDMA 0-2, SWDMA 0-2 and UDMA 0-6
 *
 * (this includes UDMA33, 66, 100 and 133) modes. UDMA66 and higher modes are
 * autoenabled only in case the BIOS has detected a 80 wire cable. To ignore
 * the BIOS data and assume the cable is present, use 'ide0=ata66' or
 * 'ide1=ata66' on the kernel command line.
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
#include "pcihost.h"

#define VIA_IDE_ENABLE		0x40
#define VIA_IDE_CONFIG		0x41
#define VIA_FIFO_CONFIG		0x43
#define VIA_MISC_1		0x44
#define VIA_MISC_2		0x45
#define VIA_MISC_3		0x46
#define VIA_DRIVE_TIMING	0x48
#define VIA_8BIT_TIMING		0x4e
#define VIA_ADDRESS_SETUP	0x4c
#define VIA_UDMA_TIMING		0x50

#define VIA_UDMA		0x007
#define VIA_UDMA_NONE		0x000
#define VIA_UDMA_33		0x001
#define VIA_UDMA_66		0x002
#define VIA_UDMA_100		0x003
#define VIA_UDMA_133		0x004
#define VIA_BAD_PREQ		0x010	/* Crashes if PREQ# till DDACK# set */
#define VIA_BAD_CLK66		0x020	/* 66 MHz clock doesn't work correctly */
#define VIA_SET_FIFO		0x040	/* Needs to have FIFO split set */
#define VIA_NO_UNMASK		0x080	/* Doesn't work with IRQ unmasking on */
#define VIA_BAD_ID		0x100	/* Has wrong vendor ID (0x1107) */

/*
 * VIA SouthBridge chips.
 */

static struct via_isa_bridge {
	char *name;
	unsigned short id;
	unsigned char rev_min;
	unsigned char rev_max;
	unsigned short flags;
} via_isa_bridges [] __initdata = {
#ifdef FUTURE_BRIDGES
	{ "vt8237",	PCI_DEVICE_ID_VIA_8237,     0x00, 0x2f, VIA_UDMA_133 },
	{ "vt8235",	PCI_DEVICE_ID_VIA_8235,     0x00, 0x2f, VIA_UDMA_133 },
#endif
	{ "vt8233a",	PCI_DEVICE_ID_VIA_8233A,    0x00, 0x2f, VIA_UDMA_133 },
	{ "vt8233c",	PCI_DEVICE_ID_VIA_8233C_0,  0x00, 0x2f, VIA_UDMA_100 },
	{ "vt8233",	PCI_DEVICE_ID_VIA_8233_0,   0x00, 0x2f, VIA_UDMA_100 },
	{ "vt8231",	PCI_DEVICE_ID_VIA_8231,     0x00, 0x2f, VIA_UDMA_100 },
	{ "vt82c686b",	PCI_DEVICE_ID_VIA_82C686,   0x40, 0x4f, VIA_UDMA_100 },
	{ "vt82c686a",	PCI_DEVICE_ID_VIA_82C686,   0x10, 0x2f, VIA_UDMA_66 },
	{ "vt82c686",	PCI_DEVICE_ID_VIA_82C686,   0x00, 0x0f, VIA_UDMA_33 | VIA_BAD_CLK66 },
	{ "vt82c596b",	PCI_DEVICE_ID_VIA_82C596,   0x10, 0x2f, VIA_UDMA_66 },
	{ "vt82c596a",	PCI_DEVICE_ID_VIA_82C596,   0x00, 0x0f, VIA_UDMA_33 | VIA_BAD_CLK66 },
	{ "vt82c586b",	PCI_DEVICE_ID_VIA_82C586_0, 0x47, 0x4f, VIA_UDMA_33 | VIA_SET_FIFO },
	{ "vt82c586b",	PCI_DEVICE_ID_VIA_82C586_0, 0x40, 0x46, VIA_UDMA_33 | VIA_SET_FIFO | VIA_BAD_PREQ },
	{ "vt82c586b",	PCI_DEVICE_ID_VIA_82C586_0, 0x30, 0x3f, VIA_UDMA_33 | VIA_SET_FIFO },
	{ "vt82c586a",	PCI_DEVICE_ID_VIA_82C586_0, 0x20, 0x2f, VIA_UDMA_33 | VIA_SET_FIFO },
	{ "vt82c586",	PCI_DEVICE_ID_VIA_82C586_0, 0x00, 0x0f, VIA_UDMA_NONE | VIA_SET_FIFO },
	{ "vt82c576",	PCI_DEVICE_ID_VIA_82C576,   0x00, 0x2f, VIA_UDMA_NONE | VIA_SET_FIFO | VIA_NO_UNMASK },
	{ "vt82c576",	PCI_DEVICE_ID_VIA_82C576,   0x00, 0x2f, VIA_UDMA_NONE | VIA_SET_FIFO | VIA_NO_UNMASK | VIA_BAD_ID },
	{ NULL }
};

static struct via_isa_bridge *via_config;
static unsigned char via_enabled;
static unsigned int via_80w;
static char *via_dma[] = { "MWDMA16", "UDMA33", "UDMA66", "UDMA100", "UDMA133" };

/*
 * via_set_speed() writes timing values to the chipset registers
 */

static void via_set_speed(struct pci_dev *dev, unsigned char dn, struct ata_timing *timing)
{
	unsigned char t;

	pci_read_config_byte(dev, VIA_ADDRESS_SETUP, &t);
	t = (t & ~(3 << ((3 - dn) << 1))) | ((FIT(timing->setup, 1, 4) - 1) << ((3 - dn) << 1));
	pci_write_config_byte(dev, VIA_ADDRESS_SETUP, t);

	pci_write_config_byte(dev, VIA_8BIT_TIMING + (1 - (dn >> 1)),
		((FIT(timing->act8b, 1, 16) - 1) << 4) | (FIT(timing->rec8b, 1, 16) - 1));

	pci_write_config_byte(dev, VIA_DRIVE_TIMING + (3 - dn),
		((FIT(timing->active, 1, 16) - 1) << 4) | (FIT(timing->recover, 1, 16) - 1));

	switch (via_config->flags & VIA_UDMA) {
		case VIA_UDMA_33:  t = timing->udma ? (0xe0 | (FIT(timing->udma, 2, 5) - 2)) : 0x03; break;
		case VIA_UDMA_66:  t = timing->udma ? (0xe8 | (FIT(timing->udma, 2, 9) - 2)) : 0x0f; break;
		case VIA_UDMA_100: t = timing->udma ? (0xe0 | (FIT(timing->udma, 2, 9) - 2)) : 0x07; break;
		case VIA_UDMA_133: t = timing->udma ? (0xe0 | (FIT(timing->udma, 2, 9) - 2)) : 0x07; break;
		default: return;
	}

	pci_write_config_byte(dev, VIA_UDMA_TIMING + (3 - dn), t);
}

/*
 * via_set_drive() computes timing values configures the drive and
 * the chipset to a desired transfer mode. It also can be called
 * by upper layers.
 */

static int via_set_drive(struct ata_device *drive, unsigned char speed)
{
	struct ata_device *peer = drive->channel->drives + (~drive->dn & 1);
	struct ata_timing t, p;
	unsigned int T, UT;

	if (speed != XFER_PIO_SLOW && speed != drive->current_speed)
		if (ide_config_drive_speed(drive, speed))
			printk(KERN_WARNING "ide%d: Drive %d didn't accept speed setting. Oh, well.\n",
				drive->dn >> 1, drive->dn & 1);

	T = 1000000000 / system_bus_speed;

	switch (via_config->flags & VIA_UDMA) {
		case VIA_UDMA_33:   UT = T;   break;
		case VIA_UDMA_66:   UT = T/2; break;
		case VIA_UDMA_100:  UT = T/3; break;
		case VIA_UDMA_133:  UT = T/4; break;
		default: UT = T;
	}

	ata_timing_compute(drive, speed, &t, T, UT);

	if (peer->present) {
		ata_timing_compute(peer, peer->current_speed, &p, T, UT);
		ata_timing_merge(&p, &t, &t, IDE_TIMING_8BIT);
	}

	via_set_speed(drive->channel->pci_dev, drive->dn, &t);

	return 0;
}

/*
 * via82cxxx_tune_drive() is a callback from upper layers for
 * PIO-only tuning.
 */

static void via82cxxx_tune_drive(struct ata_device *drive, unsigned char pio)
{
	if (!((via_enabled >> drive->channel->unit) & 1))
		return;

	if (pio == 255) {
		via_set_drive(drive, ata_timing_mode(drive, XFER_PIO | XFER_EPIO));
		return;
	}

	via_set_drive(drive, XFER_PIO_0 + min_t(byte, pio, 5));
}

#ifdef CONFIG_BLK_DEV_IDEDMA
static int __init via_modes_map(struct ata_channel *ch)
{
	short w80 = ch->udma_four;
	int map = XFER_EPIO | XFER_SWDMA | XFER_MWDMA |
		  (via_config->flags & VIA_UDMA ? XFER_UDMA : 0) |
		  (w80 && (via_config->flags & VIA_UDMA) >= VIA_UDMA_66 ? XFER_UDMA_66 : 0) |
		  (w80 && (via_config->flags & VIA_UDMA) >= VIA_UDMA_100 ? XFER_UDMA_100 : 0) |
		  (w80 && (via_config->flags & VIA_UDMA) >= VIA_UDMA_133 ? XFER_UDMA_133 : 0);

	return map;
}
#endif

/*
 * The initialization callback. Here we determine the IDE chip type
 * and initialize its drive independent registers.
 */

static unsigned int __init via82cxxx_init_chipset(struct pci_dev *dev)
{
	struct pci_dev *isa = NULL;
	unsigned char t, v;
	unsigned int u;
	int i;

/*
 * Find the ISA bridge to see how good the IDE is.
 */

	for (via_config = via_isa_bridges; via_config->id; via_config++)
		if ((isa = pci_find_device(PCI_VENDOR_ID_VIA +
			!!(via_config->flags & VIA_BAD_ID), via_config->id, NULL))) {

			pci_read_config_byte(isa, PCI_REVISION_ID, &t);
			if (t >= via_config->rev_min && t <= via_config->rev_max)
				break;
		}

	if (!via_config->id) {
		printk(KERN_WARNING "VP_IDE: Unknown VIA SouthBridge, contact Vojtech Pavlik <vojtech@ucw.cz>\n");
		return -ENODEV;
	}

/*
 * Check 80-wire cable presence and setup Clk66.
 */

	switch (via_config->flags & VIA_UDMA) {

		case VIA_UDMA_66:
			pci_read_config_dword(dev, VIA_UDMA_TIMING, &u);	/* Enable Clk66 */
			pci_write_config_dword(dev, VIA_UDMA_TIMING, u | 0x80008);
			for (i = 24; i >= 0; i -= 8)
				if (((u >> (i & 16)) & 8) && ((u >> i) & 0x20) && (((u >> i) & 7) < 2))
					via_80w |= (1 << (1 - (i >> 4)));	/* 2x PCI clock and UDMA w/ < 3T/cycle */
			break;

		case VIA_UDMA_100:
			pci_read_config_dword(dev, VIA_UDMA_TIMING, &u);
			for (i = 24; i >= 0; i -= 8)
				if (((u >> i) & 0x10) || (((u >> i) & 0x20) && (((u >> i) & 7) < 4)))
					via_80w |= (1 << (1 - (i >> 4)));	/* BIOS 80-wire bit or UDMA w/ < 60ns/cycle */
			break;

		case VIA_UDMA_133:
			pci_read_config_dword(dev, VIA_UDMA_TIMING, &u);
			for (i = 24; i >= 0; i -= 8)
				if (((u >> i) & 0x10) || (((u >> i) & 0x20) && (((u >> i) & 7) < 8)))
					via_80w |= (1 << (1 - (i >> 4)));	/* BIOS 80-wire bit or UDMA w/ < 60ns/cycle */
			break;

	}

	if (via_config->flags & VIA_BAD_CLK66) {			/* Disable Clk66 */
		pci_read_config_dword(dev, VIA_UDMA_TIMING, &u);	/* Would cause trouble on 596a and 686 */
		pci_write_config_dword(dev, VIA_UDMA_TIMING, u & ~0x80008);
	}

/*
 * Check whether interfaces are enabled.
 */

	pci_read_config_byte(dev, VIA_IDE_ENABLE, &v);
	via_enabled = ((v & 1) ? 2 : 0) | ((v & 2) ? 1 : 0);

/*
 * Set up FIFO sizes and thresholds.
 */

	pci_read_config_byte(dev, VIA_FIFO_CONFIG, &t);

	if (via_config->flags & VIA_BAD_PREQ)				/* Disable PREQ# till DDACK# */
		t &= 0x7f;						/* Would crash on 586b rev 41 */

	if (via_config->flags & VIA_SET_FIFO) {				/* Fix FIFO split between channels */
		t &= (t & 0x9f);
		switch (via_enabled) {
			case 1: t |= 0x00; break;			/* 16 on primary */
			case 2: t |= 0x60; break;			/* 16 on secondary */
			case 3: t |= 0x20; break;			/* 8 pri 8 sec */
		}
	}

	pci_write_config_byte(dev, VIA_FIFO_CONFIG, t);

/*
 * Print the boot message.
 */

	pci_read_config_byte(isa, PCI_REVISION_ID, &t);
	printk(KERN_INFO "VP_IDE: VIA %s (rev %02x) ATA %s controller on PCI %s\n",
		via_config->name, t, via_dma[via_config->flags & VIA_UDMA], dev->slot_name);

	return 0;
}

static unsigned int __init via82cxxx_ata66_check(struct ata_channel *hwif)
{
	return ((via_enabled & via_80w) >> hwif->unit) & 1;
}

static void __init via82cxxx_init_channel(struct ata_channel *hwif)
{
	int i;

	hwif->udma_four = via82cxxx_ata66_check(hwif);

	hwif->tuneproc = &via82cxxx_tune_drive;
	hwif->speedproc = &via_set_drive;
	hwif->io_32bit = 1;

	hwif->unmask = (via_config->flags & VIA_NO_UNMASK) ? 0 : 1;
	for (i = 0; i < 2; i++) {
		hwif->drives[i].autotune = 1;
		hwif->drives[i].dn = hwif->unit * 2 + i;
	}

#ifdef CONFIG_BLK_DEV_IDEDMA
	if (hwif->dma_base) {
		hwif->highmem = 1;
		hwif->modes_map = via_modes_map(hwif);
		hwif->udma_setup = udma_generic_setup;
	}
#endif
}

/*
 * We allow the BM-DMA driver to only work on enabled interfaces.
 */

static void __init via82cxxx_init_dma(struct ata_channel *hwif, unsigned long dmabase)
{
	if ((via_enabled >> hwif->unit) & 1)
		ata_init_dma(hwif, dmabase);
}

/* module data table */
static struct ata_pci_device chipsets[] __initdata = {
	{
		vendor: PCI_VENDOR_ID_VIA,
		device:	PCI_DEVICE_ID_VIA_82C576_1,
		init_chipset: via82cxxx_init_chipset,
		init_channel: via82cxxx_init_channel,
		init_dma: via82cxxx_init_dma,
		enablebits: {{0x40,0x02,0x02}, {0x40,0x01,0x01}},
		bootable: ON_BOARD,
	},
	{
		vendor:	PCI_VENDOR_ID_VIA,
		device:	PCI_DEVICE_ID_VIA_82C586_1,
		init_chipset: via82cxxx_init_chipset,
		init_channel: via82cxxx_init_channel,
		init_dma: via82cxxx_init_dma,
		enablebits: {{0x40,0x02,0x02}, {0x40,0x01,0x01}},
		bootable: ON_BOARD,
	},
};

int __init init_via82cxxx(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(chipsets); ++i) {
		ata_register_chipset(&chipsets[i]);
	}

	return 0;
}
