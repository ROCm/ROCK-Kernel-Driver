/**** vi:set ts=8 sts=8 sw=8:************************************************
 *
 *  linux/drivers/ide/pdc202xx.c	Version 0.30	Mar. 18, 2000
 *
 *  Copyright (C) 1998-2000	Andre Hedrick <andre@linux-ide.org>
 *  May be copied or modified under the terms of the GNU General Public License
 *
 *  Promise Ultra33 cards with BIOS v1.20 through 1.28 will need this
 *  compiled into the kernel if you have more than one card installed.
 *  Note that BIOS v1.29 is reported to fix the problem.  Since this is
 *  safe chipset tuning, including this support is harmless
 *
 *  Promise Ultra66 cards with BIOS v1.11 this
 *  compiled into the kernel if you have more than one card installed.
 *
 *  Promise Ultra100 cards.
 *
 *  The latest chipset code will support the following ::
 *  Three Ultra33 controllers and 12 drives.
 *  8 are UDMA supported and 4 are limited to DMA mode 2 multi-word.
 *  The 8/4 ratio is a BIOS code limit by promise.
 *
 *  UNLESS you enable "CONFIG_PDC202XX_BURST"
 *
 */

/*
 *  Portions Copyright (C) 1999 Promise Technology, Inc.
 *  Author: Frank Tiernan (frankt@promise.com)
 *  Released under terms of General Public License
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

#define PDC202XX_DEBUG_DRIVE_INFO		0
#define PDC202XX_DECODE_REGISTER_INFO		0

#ifndef SPLIT_BYTE
#define SPLIT_BYTE(B,H,L)	((H)=(B>>4), (L)=(B-((B>>4)<<4)))
#endif

extern char *ide_xfer_verbose (byte xfer_rate);

/* A Register */
#define	SYNC_ERRDY_EN	0xC0

#define	SYNC_IN		0x80	/* control bit, different for master vs. slave drives */
#define	ERRDY_EN	0x40	/* control bit, different for master vs. slave drives */
#define	IORDY_EN	0x20	/* PIO: IOREADY */
#define	PREFETCH_EN	0x10	/* PIO: PREFETCH */

#define	PA3		0x08	/* PIO"A" timing */
#define	PA2		0x04	/* PIO"A" timing */
#define	PA1		0x02	/* PIO"A" timing */
#define	PA0		0x01	/* PIO"A" timing */

/* B Register */

#define	MB2		0x80	/* DMA"B" timing */
#define	MB1		0x40	/* DMA"B" timing */
#define	MB0		0x20	/* DMA"B" timing */

#define	PB4		0x10	/* PIO_FORCE 1:0 */

#define	PB3		0x08	/* PIO"B" timing */	/* PIO flow Control mode */
#define	PB2		0x04	/* PIO"B" timing */	/* PIO 4 */
#define	PB1		0x02	/* PIO"B" timing */	/* PIO 3 half */
#define	PB0		0x01	/* PIO"B" timing */	/* PIO 3 other half */

/* C Register */
#define	IORDYp_NO_SPEED	0x4F
#define	SPEED_DIS	0x0F

#define	DMARQp		0x80
#define	IORDYp		0x40
#define	DMAR_EN		0x20
#define	DMAW_EN		0x10

#define	MC3		0x08	/* DMA"C" timing */
#define	MC2		0x04	/* DMA"C" timing */
#define	MC1		0x02	/* DMA"C" timing */
#define	MC0		0x01	/* DMA"C" timing */

#if PDC202XX_DECODE_REGISTER_INFO

#define REG_A		0x01
#define REG_B		0x02
#define REG_C		0x04
#define REG_D		0x08

static void decode_registers (byte registers, byte value)
{
	byte	bit = 0, bit1 = 0, bit2 = 0;

	switch(registers) {
		case REG_A:
			printk("A Register ");
			if (value & 0x80) printk("SYNC_IN ");
			if (value & 0x40) printk("ERRDY_EN ");
			if (value & 0x20) printk("IORDY_EN ");
			if (value & 0x10) printk("PREFETCH_EN ");
			if (value & 0x08) { printk("PA3 ");bit2 |= 0x08; }
			if (value & 0x04) { printk("PA2 ");bit2 |= 0x04; }
			if (value & 0x02) { printk("PA1 ");bit2 |= 0x02; }
			if (value & 0x01) { printk("PA0 ");bit2 |= 0x01; }
			printk("PIO(A) = %d ", bit2);
			break;
		case REG_B:
			printk("B Register ");
			if (value & 0x80) { printk("MB2 ");bit1 |= 0x80; }
			if (value & 0x40) { printk("MB1 ");bit1 |= 0x40; }
			if (value & 0x20) { printk("MB0 ");bit1 |= 0x20; }
			printk("DMA(B) = %d ", bit1 >> 5);
			if (value & 0x10) printk("PIO_FORCED/PB4 ");
			if (value & 0x08) { printk("PB3 ");bit2 |= 0x08; }
			if (value & 0x04) { printk("PB2 ");bit2 |= 0x04; }
			if (value & 0x02) { printk("PB1 ");bit2 |= 0x02; }
			if (value & 0x01) { printk("PB0 ");bit2 |= 0x01; }
			printk("PIO(B) = %d ", bit2);
			break;
		case REG_C:
			printk("C Register ");
			if (value & 0x80) printk("DMARQp ");
			if (value & 0x40) printk("IORDYp ");
			if (value & 0x20) printk("DMAR_EN ");
			if (value & 0x10) printk("DMAW_EN ");

			if (value & 0x08) { printk("MC3 ");bit2 |= 0x08; }
			if (value & 0x04) { printk("MC2 ");bit2 |= 0x04; }
			if (value & 0x02) { printk("MC1 ");bit2 |= 0x02; }
			if (value & 0x01) { printk("MC0 ");bit2 |= 0x01; }
			printk("DMA(C) = %d ", bit2);
			break;
		case REG_D:
			printk("D Register ");
			break;
		default:
			return;
	}
	printk("\n        %s ", (registers & REG_D) ? "DP" :
				(registers & REG_C) ? "CP" :
				(registers & REG_B) ? "BP" :
				(registers & REG_A) ? "AP" : "ERROR");
	for (bit=128;bit>0;bit/=2)
		printk("%s", (value & bit) ? "1" : "0");
	printk("\n");
}

#endif /* PDC202XX_DECODE_REGISTER_INFO */

int check_in_drive_lists(struct ata_device *drive)
{
	static const char *pdc_quirk_drives[] = {
		"QUANTUM FIREBALLlct08 08",
		"QUANTUM FIREBALLP KA6.4",
		"QUANTUM FIREBALLP KA9.1",
		"QUANTUM FIREBALLP LM20.4",
		"QUANTUM FIREBALLP KX20.5",
		"QUANTUM FIREBALLP KX27.3",
		"QUANTUM FIREBALLP LM20.5",
		NULL
	};
     const char**list = pdc_quirk_drives;
	struct hd_driveid *id = drive->id;

	while (*list)
		if (strstr(id->model, *list++))
			return 2;
	return 0;
}

static int pdc202xx_ratemask(struct ata_device *drive)
{
	struct pci_dev *dev = drive->channel->pci_dev;
	int map = 0;

	if (!eighty_ninty_three(drive))
		return XFER_UDMA;

	switch(dev->device) {
		case PCI_DEVICE_ID_PROMISE_20276:
		case PCI_DEVICE_ID_PROMISE_20275:
		case PCI_DEVICE_ID_PROMISE_20269:
			map |= XFER_UDMA_133;
		case PCI_DEVICE_ID_PROMISE_20268R:
		case PCI_DEVICE_ID_PROMISE_20268:
		case PCI_DEVICE_ID_PROMISE_20267:
		case PCI_DEVICE_ID_PROMISE_20265:
			map |= XFER_UDMA_100;
		case PCI_DEVICE_ID_PROMISE_20262:
			map |= XFER_UDMA_66;
		case PCI_DEVICE_ID_PROMISE_20246:
			map |= XFER_UDMA;
	}
	return map;
}

static int pdc202xx_tune_chipset(struct ata_device *drive, byte speed)
{
	struct ata_channel *hwif = drive->channel;
	struct pci_dev *dev = hwif->pci_dev;

	unsigned int		drive_conf;
	byte			drive_pci, AP, BP, CP, DP;
	byte			TA = 0, TB = 0, TC = 0;

	switch (drive->dn) {
		case 0: drive_pci = 0x60; break;
		case 1: drive_pci = 0x64; break;
		case 2: drive_pci = 0x68; break;
		case 3: drive_pci = 0x6c; break;
		default: return -1;
	}

	if ((drive->type != ATA_DISK) && (speed < XFER_SW_DMA_0))
		return -1;

	pci_read_config_dword(dev, drive_pci, &drive_conf);
	pci_read_config_byte(dev, (drive_pci), &AP);
	pci_read_config_byte(dev, (drive_pci)|0x01, &BP);
	pci_read_config_byte(dev, (drive_pci)|0x02, &CP);
	pci_read_config_byte(dev, (drive_pci)|0x03, &DP);

#ifdef CONFIG_BLK_DEV_IDEDMA
	if (speed >= XFER_SW_DMA_0) {
		if ((BP & 0xF0) && (CP & 0x0F)) {
			/* clear DMA modes of upper 842 bits of B Register */
			/* clear PIO forced mode upper 1 bit of B Register */
			pci_write_config_byte(dev, (drive_pci)|0x01, BP & ~0xF0);
			pci_read_config_byte(dev, (drive_pci)|0x01, &BP);

			/* clear DMA modes of lower 8421 bits of C Register */
			pci_write_config_byte(dev, (drive_pci)|0x02, CP & ~0x0F);
			pci_read_config_byte(dev, (drive_pci)|0x02, &CP);
		}
	} else {
#else
	{
#endif /* CONFIG_BLK_DEV_IDEDMA */
		if ((AP & 0x0F) || (BP & 0x07)) {
			/* clear PIO modes of lower 8421 bits of A Register */
			pci_write_config_byte(dev, (drive_pci), AP & ~0x0F);
			pci_read_config_byte(dev, (drive_pci), &AP);

			/* clear PIO modes of lower 421 bits of B Register */
			pci_write_config_byte(dev, (drive_pci)|0x01, BP & ~0x07);
			pci_read_config_byte(dev, (drive_pci)|0x01, &BP);

			pci_read_config_byte(dev, (drive_pci), &AP);
			pci_read_config_byte(dev, (drive_pci)|0x01, &BP);
		}
	}

	pci_read_config_byte(dev, (drive_pci), &AP);
	pci_read_config_byte(dev, (drive_pci)|0x01, &BP);
	pci_read_config_byte(dev, (drive_pci)|0x02, &CP);

	switch(speed) {
#ifdef CONFIG_BLK_DEV_IDEDMA
		/* case XFER_UDMA_6: */
		case XFER_UDMA_5:
		case XFER_UDMA_4:	TB = 0x20; TC = 0x01; break;	/* speed 8 == UDMA mode 4 */
		case XFER_UDMA_3:	TB = 0x40; TC = 0x02; break;	/* speed 7 == UDMA mode 3 */
		case XFER_UDMA_2:	TB = 0x20; TC = 0x01; break;	/* speed 6 == UDMA mode 2 */
		case XFER_UDMA_1:	TB = 0x40; TC = 0x02; break;	/* speed 5 == UDMA mode 1 */
		case XFER_UDMA_0:	TB = 0x60; TC = 0x03; break;	/* speed 4 == UDMA mode 0 */
		case XFER_MW_DMA_2:	TB = 0x60; TC = 0x03; break;	/* speed 4 == MDMA mode 2 */
		case XFER_MW_DMA_1:	TB = 0x60; TC = 0x04; break;	/* speed 3 == MDMA mode 1 */
		case XFER_MW_DMA_0:	TB = 0x60; TC = 0x05; break;	/* speed 2 == MDMA mode 0 */
		case XFER_SW_DMA_2:	TB = 0x60; TC = 0x05; break;	/* speed 0 == SDMA mode 2 */
		case XFER_SW_DMA_1:	TB = 0x80; TC = 0x06; break;	/* speed 1 == SDMA mode 1 */
		case XFER_SW_DMA_0:	TB = 0xC0; TC = 0x0B; break;	/* speed 0 == SDMA mode 0 */
#endif /* CONFIG_BLK_DEV_IDEDMA */
		case XFER_PIO_4:	TA = 0x01; TB = 0x04; break;
		case XFER_PIO_3:	TA = 0x02; TB = 0x06; break;
		case XFER_PIO_2:	TA = 0x03; TB = 0x08; break;
		case XFER_PIO_1:	TA = 0x05; TB = 0x0C; break;
		case XFER_PIO_0:
		default:		TA = 0x09; TB = 0x13; break;
	}

#ifdef CONFIG_BLK_DEV_IDEDMA
        if (speed >= XFER_SW_DMA_0) {
		pci_write_config_byte(dev, (drive_pci)|0x01, BP|TB);
		pci_write_config_byte(dev, (drive_pci)|0x02, CP|TC);
	} else {
#else
	{
#endif /* CONFIG_BLK_DEV_IDEDMA */
		pci_write_config_byte(dev, (drive_pci), AP|TA);
		pci_write_config_byte(dev, (drive_pci)|0x01, BP|TB);
	}

#if PDC202XX_DECODE_REGISTER_INFO
	pci_read_config_byte(dev, (drive_pci), &AP);
	pci_read_config_byte(dev, (drive_pci)|0x01, &BP);
	pci_read_config_byte(dev, (drive_pci)|0x02, &CP);
	pci_read_config_byte(dev, (drive_pci)|0x03, &DP);

	decode_registers(REG_A, AP);
	decode_registers(REG_B, BP);
	decode_registers(REG_C, CP);
	decode_registers(REG_D, DP);
#endif /* PDC202XX_DECODE_REGISTER_INFO */

	if (!drive->init_speed)
		drive->init_speed = speed;
	drive->current_speed = speed;

#if PDC202XX_DEBUG_DRIVE_INFO
	printk("%s: %s drive%d 0x%08x ",
		drive->name, ide_xfer_verbose(speed),
		drive->dn, drive_conf);
		pci_read_config_dword(dev, drive_pci, &drive_conf);
	printk("0x%08x\n", drive_conf);
#endif /* PDC202XX_DEBUG_DRIVE_INFO */

	return ide_config_drive_speed(drive, speed);
}

#define set_2regs(a, b) \
        OUT_BYTE((a + adj), indexreg); \
	OUT_BYTE(b, datareg);

#define set_reg_and_wait(value, reg, delay) \
	OUT_BYTE(value, reg); \
        mdelay(delay);


static int pdc202xx_new_tune_chipset(struct ata_device *drive, byte speed)
{
	struct ata_channel *hwif = drive->channel;
#ifdef CONFIG_BLK_DEV_IDEDMA
	unsigned long indexreg	= (hwif->dma_base + 1);
	unsigned long datareg	= (hwif->dma_base + 3);
#else
	struct pci_dev *dev	= hwif->pci_dev;
	unsigned long high_16	= pci_resource_start(dev, 4);
	unsigned long indexreg	= high_16 + (hwif->unit ? 0x09 : 0x01);
	unsigned long datareg	= (indexreg + 2);
#endif /* CONFIG_BLK_DEV_IDEDMA */
	byte thold		= 0x10;
	byte adj		= (drive->dn%2) ? 0x08 : 0x00;

#ifdef CONFIG_BLK_DEV_IDEDMA
	if (speed == XFER_UDMA_2) {
		OUT_BYTE((thold + adj), indexreg);
		OUT_BYTE((IN_BYTE(datareg) & 0x7f), datareg);
	}
	switch (speed) {
		case XFER_UDMA_7:
			speed = XFER_UDMA_6;
		case XFER_UDMA_6:
			set_2regs(0x10, 0x1a);
			set_2regs(0x11, 0x01);
			set_2regs(0x12, 0xcb);
			break;
		case XFER_UDMA_5:
			set_2regs(0x10, 0x1a);
			set_2regs(0x11, 0x02);
			set_2regs(0x12, 0xcb);
			break;
		case XFER_UDMA_4:
			set_2regs(0x10, 0x1a);
			set_2regs(0x11, 0x03);
			set_2regs(0x12, 0xcd);
			break;
		case XFER_UDMA_3:
			set_2regs(0x10, 0x1a);
			set_2regs(0x11, 0x05);
			set_2regs(0x12, 0xcd);
			break;
		case XFER_UDMA_2:
			set_2regs(0x10, 0x2a);
			set_2regs(0x11, 0x07);
			set_2regs(0x12, 0xcd);
			break;
		case XFER_UDMA_1:
			set_2regs(0x10, 0x3a);
			set_2regs(0x11, 0x0a);
			set_2regs(0x12, 0xd0);
			break;
		case XFER_UDMA_0:
			set_2regs(0x10, 0x4a);
			set_2regs(0x11, 0x0f);
			set_2regs(0x12, 0xd5);
			break;
		case XFER_MW_DMA_2:
			set_2regs(0x0e, 0x69);
			set_2regs(0x0f, 0x25);
			break;
		case XFER_MW_DMA_1:
			set_2regs(0x0e, 0x6b);
			set_2regs(0x0f, 0x27);
			break;
		case XFER_MW_DMA_0:
			set_2regs(0x0e, 0xdf);
			set_2regs(0x0f, 0x5f);
			break;
#else
	switch (speed) {
#endif /* CONFIG_BLK_DEV_IDEDMA */
		case XFER_PIO_4:
			set_2regs(0x0c, 0x23);
			set_2regs(0x0d, 0x09);
			set_2regs(0x13, 0x25);
			break;
		case XFER_PIO_3:
			set_2regs(0x0c, 0x27);
			set_2regs(0x0d, 0x0d);
			set_2regs(0x13, 0x35);
			break;
		case XFER_PIO_2:
			set_2regs(0x0c, 0x23);
			set_2regs(0x0d, 0x26);
			set_2regs(0x13, 0x64);
			break;
		case XFER_PIO_1:
			set_2regs(0x0c, 0x46);
			set_2regs(0x0d, 0x29);
			set_2regs(0x13, 0xa4);
			break;
		case XFER_PIO_0:
			set_2regs(0x0c, 0xfb);
			set_2regs(0x0d, 0x2b);
			set_2regs(0x13, 0xac);
			break;
		default:
			;
	}

	if (!drive->init_speed)
		drive->init_speed = speed;
	drive->current_speed = speed;

	return ide_config_drive_speed(drive, speed);
}

/*   0    1    2    3    4    5    6   7   8
 * 960, 480, 390, 300, 240, 180, 120, 90, 60
 *           180, 150, 120,  90,  60
 * DMA_Speed
 * 180, 120,  90,  90,  90,  60,  30
 *  11,   5,   4,   3,   2,   1,   0
 */
static void config_chipset_for_pio(struct ata_device *drive, byte pio)
{
	byte speed;

	if (pio == 255)
		speed = ata_timing_mode(drive, XFER_PIO | XFER_EPIO);
	else	speed = XFER_PIO_0 + min_t(byte, pio, 4);

	pdc202xx_tune_chipset(drive, speed);
}

#ifdef CONFIG_BLK_DEV_IDEDMA
static int config_chipset_for_dma(struct ata_device *drive, byte udma)
{
	struct hd_driveid *id	= drive->id;
	struct ata_channel *hwif = drive->channel;
	struct hd_driveid *mate_id = hwif->drives[!(drive->dn%2)].id;
	struct pci_dev *dev	= hwif->pci_dev;
	unsigned long high_16   = pci_resource_start(dev, 4);
	unsigned long dma_base  = hwif->dma_base;
	unsigned long indexreg	= dma_base + 1;
	unsigned long datareg	= dma_base + 3;
	byte iordy		= 0x13;
	byte adj		= (drive->dn%2) ? 0x08 : 0x00;
	byte cable		= 0;
	byte jumpbit		= 0;
	unsigned int		drive_conf;
	byte drive_pci = 0, mate_pci = 0;
	byte			test1, test2, mode = -1;
	byte			AP;
	unsigned short		EP;
	byte CLKSPD		= 0;
	/* primary - second bit, secondary - fourth bit */
	byte mask		= hwif->unit ? 0x08 : 0x02;
	unsigned short c_mask	= hwif->unit ? (1<<11) : (1<<10);
	int map;

	byte needs_80w = ((id->dma_ultra & 0x0008) ||
			  (id->dma_ultra & 0x0010) ||
			  (id->dma_ultra & 0x0020) ||
			  (id->dma_ultra & 0x0040));

	switch(dev->device) {
		case PCI_DEVICE_ID_PROMISE_20275:
		case PCI_DEVICE_ID_PROMISE_20276:
		case PCI_DEVICE_ID_PROMISE_20269:
		case PCI_DEVICE_ID_PROMISE_20268R:
		case PCI_DEVICE_ID_PROMISE_20268:
			OUT_BYTE(0x0b, indexreg);
			cable = ((IN_BYTE(datareg) & 0x04));
			jumpbit = 1;
			break;
		case PCI_DEVICE_ID_PROMISE_20267:
		case PCI_DEVICE_ID_PROMISE_20265:
		case PCI_DEVICE_ID_PROMISE_20262:
			pci_read_config_word(dev, 0x50, &EP);
			cable = (EP & c_mask);
			jumpbit = 0;
			break;
		default:
			cable = 1; jumpbit = 0;
			break;
	}

	if (!jumpbit)
		CLKSPD = IN_BYTE(high_16 + 0x11);
	/*
	 * Set the control register to use the 66Mhz system
	 * clock for UDMA 3/4 mode operation. If one drive on
	 * a channel is U66 capable but the other isn't we
	 * fall back to U33 mode. The BIOS INT 13 hooks turn
	 * the clock on then off for each read/write issued. I don't
	 * do that here because it would require modifying the
	 * kernel, seperating the fop routines from the kernel or
	 * somehow hooking the fops calls. It may also be possible to
	 * leave the 66Mhz clock on and readjust the timing
	 * parameters.
	 */
	if (needs_80w) {
		/* FIXME: this check is wrong for 20246 --bkz */
		if (cable) {
			printk(KERN_WARNING "%s: channel requires an 80-pin cable.\n", hwif->name);
			printk(KERN_WARNING "%s: reduced to UDMA(33) mode.\n", drive->name);
			if (!jumpbit)
				OUT_BYTE(CLKSPD & ~mask, (high_16 + 0x11));
		}
		if (!jumpbit) {
			if (mate_id) {	/* check if mate is at least udma3 */
				if ((mate_id->dma_ultra & 0x0040) ||
				    (mate_id->dma_ultra & 0x0020) ||
				    (mate_id->dma_ultra & 0x0010) ||
				    (mate_id->dma_ultra & 0x0008)) {
					OUT_BYTE(CLKSPD | mask, (high_16 + 0x11));
				} else {
					OUT_BYTE(CLKSPD & ~mask, (high_16 + 0x11));
				}
			} else {	/* single drive */
				OUT_BYTE(CLKSPD | mask, (high_16 + 0x11));
			}
		}
	}

	if (jumpbit) {
		if (drive->type != ATA_DISK)
			return 0;
		if (id->capability & 4) {	/* IORDY_EN & PREFETCH_EN */
			set_2regs(iordy, (IN_BYTE(datareg)|0x03));
		}
		goto jumpbit_is_set;
	}

	switch(drive->dn) {
		case 0:	drive_pci = 0x60;
		case 2:	drive_pci = 0x68;
			pci_read_config_dword(dev, drive_pci, &drive_conf);
			if ((drive_conf != 0x004ff304) && (drive_conf != 0x004ff3c4))
				goto chipset_is_set;
			pci_read_config_byte(dev, drive_pci, &test1);
			if (!(test1 & SYNC_ERRDY_EN))
				pci_write_config_byte(dev, drive_pci, test1|SYNC_ERRDY_EN);
			break;
		case 1:	drive_pci = 0x64; mate_pci = 0x60;
		case 3:	drive_pci = 0x6c; mate_pci = 0x68;
			pci_read_config_dword(dev, drive_pci, &drive_conf);
			if ((drive_conf != 0x004ff304) && (drive_conf != 0x004ff3c4))
				goto chipset_is_set;
			pci_read_config_byte(dev, mate_pci, &test1);
			pci_read_config_byte(dev, drive_pci, &test2);
			if ((test1 & SYNC_ERRDY_EN) && !(test2 & SYNC_ERRDY_EN))
				pci_write_config_byte(dev, drive_pci, test2|SYNC_ERRDY_EN);
			break;
		default:
			return 0;
	}

chipset_is_set:

	if (drive->type != ATA_DISK)
		return 0;

	pci_read_config_byte(dev, (drive_pci), &AP);
	if (id->capability & 4)	/* IORDY_EN */
		pci_write_config_byte(dev, (drive_pci), AP|IORDY_EN);
	pci_read_config_byte(dev, (drive_pci), &AP);
	if (drive->type == ATA_DISK)	/* PREFETCH_EN */
		pci_write_config_byte(dev, (drive_pci), AP|PREFETCH_EN);

jumpbit_is_set:

	if (udma) {
		map = pdc202xx_ratemask(drive);
	} else {
		if (!jumpbit)
			map = XFER_SWDMA | XFER_MWDMA;
		else
			map = XFER_MWDMA;
	}

	mode = ata_timing_mode(drive, map);
	if (mode < XFER_SW_DMA_0) {
		/* restore original pci-config space */
		if (!jumpbit)
			pci_write_config_dword(dev, drive_pci, drive_conf);
		return 0;
	}

	return !(hwif->speedproc(drive, mode));
}

static int config_drive_xfer_rate(struct ata_device *drive)
{
	struct hd_driveid *id = drive->id;
	struct ata_channel *hwif = drive->channel;
	int on = 0;
	int verbose = 1;

	if (id && (id->capability & 1) && hwif->autodma) {
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
		config_chipset_for_pio(drive, 5);
	}

	udma_enable(drive, on, verbose);

	return 0;
}

static int pdc202xx_udma_start(struct ata_device *drive, struct request *rq)
{
	u8 lba48hack = 0, clock = 0;
	struct ata_channel *ch = drive->channel;
	struct pci_dev *dev	= ch->pci_dev;
	unsigned long high_16	= pci_resource_start(dev, 4);
	unsigned long atapi_reg	= high_16 + (ch->unit ? 0x24 : 0x00);

	switch (dev->device) {
		case PCI_DEVICE_ID_PROMISE_20267:
		case PCI_DEVICE_ID_PROMISE_20265:
		case PCI_DEVICE_ID_PROMISE_20262:
			lba48hack = 1;
			clock = IN_BYTE(high_16 + 0x11);
		default:
			break;
	}

	if (drive->addressing && lba48hack) {
		unsigned long word_count = 0;

		outb(clock|(ch->unit ? 0x08 : 0x02), high_16 + 0x11);
		word_count = (rq->nr_sectors << 8);
		word_count = (rq_data_dir(rq) == READ) ? word_count | 0x05000000 : word_count | 0x06000000;
		outl(word_count, atapi_reg);
	}

	/* Note that this is done *after* the cmd has been issued to the drive,
	 * as per the BM-IDE spec.  The Promise Ultra33 doesn't work correctly
	 * when we do this part before issuing the drive cmd.
	 */

	outb(inb(ch->dma_base) | 1, ch->dma_base); /* start DMA */

	return 0;
}

int pdc202xx_udma_stop(struct ata_device *drive)
{
	u8 lba48hack = 0, clock = 0;
	struct ata_channel *ch = drive->channel;
	struct pci_dev *dev	= ch->pci_dev;
	unsigned long high_16	= pci_resource_start(dev, 4);
	unsigned long atapi_reg	= high_16 + (ch->unit ? 0x24 : 0x00);
	unsigned long dma_base = ch->dma_base;
	u8 dma_stat;

	switch (dev->device) {
		case PCI_DEVICE_ID_PROMISE_20267:
		case PCI_DEVICE_ID_PROMISE_20265:
		case PCI_DEVICE_ID_PROMISE_20262:
			lba48hack = 1;
			/* FIXME: why do we need this here --bkz */
			clock = IN_BYTE(high_16 + 0x11);
 		default:
			break;
	}

	if (drive->addressing && lba48hack) {
		outl(0, atapi_reg);	/* zero out extra */
		clock = IN_BYTE(high_16 + 0x11);
		OUT_BYTE(clock & ~(ch->unit ? 0x08:0x02), high_16 + 0x11);
	}

	drive->waiting_for_dma = 0;
	outb(inb(dma_base)&~1, dma_base);	/* stop DMA */
	dma_stat = inb(dma_base+2);		/* get DMA status */
	outb(dma_stat|6, dma_base+2);		/* clear the INTR & ERROR bits */
	udma_destroy_table(ch);			/* purge DMA mappings */

	return (dma_stat & 7) != 4 ? (0x10 | dma_stat) : 0;	/* verify good DMA status */
}

static int pdc202xx_udma_irq_status(struct ata_device *drive)
{
	struct ata_channel *ch = drive->channel;
	u8 dma_stat = 0;
	u8 sc1d	= 0;
	u8 newchip = 0;
	u8 clock = 0;
	struct pci_dev *dev = ch->pci_dev;
	unsigned long high_16 = pci_resource_start(dev, 4);
	unsigned long dma_base = ch->dma_base;

	switch (dev->device) {
		case PCI_DEVICE_ID_PROMISE_20275:
		case PCI_DEVICE_ID_PROMISE_20276:
		case PCI_DEVICE_ID_PROMISE_20269:
		case PCI_DEVICE_ID_PROMISE_20268R:
		case PCI_DEVICE_ID_PROMISE_20268:
			newchip = 1;
			break;
		case PCI_DEVICE_ID_PROMISE_20267:
		case PCI_DEVICE_ID_PROMISE_20265:
		case PCI_DEVICE_ID_PROMISE_20262:
			/* FIXME: why do we need this here --bkz */
			clock = IN_BYTE(high_16 + 0x11);
		default:
			break;
	}

	dma_stat = IN_BYTE(dma_base + 2);
	if (newchip)
		return (dma_stat & 4) == 4;

	sc1d = IN_BYTE(high_16 + 0x001d);
	if (ch->unit) {
		if ((sc1d & 0x50) == 0x50) goto somebody_else;
		else if ((sc1d & 0x40) == 0x40)
			return (dma_stat & 4) == 4;
	} else {
		if ((sc1d & 0x05) == 0x05) goto somebody_else;
		else if ((sc1d & 0x04) == 0x04)
			return (dma_stat & 4) == 4;
	}
somebody_else:
	return (dma_stat & 4) == 4;	/* return 1 if INTR asserted */
}

static void pdc202xx_bug (struct ata_device *drive)
{
	if (!drive->channel->resetproc)
		return;
	/* Assume naively that resetting the drive may help. */
	drive->channel->resetproc(drive);
}

static int pdc202xx_dmaproc(struct ata_device *drive)
{
	return config_drive_xfer_rate(drive);
}
#endif

void pdc202xx_new_reset(struct ata_device *drive)
{
	set_reg_and_wait(0x04,IDE_CONTROL_REG, 1000);
	set_reg_and_wait(0x00,IDE_CONTROL_REG, 1000);
	printk("PDC202XX: %s channel reset.\n",
		drive->channel->unit ? "Secondary" : "Primary");
}

void pdc202xx_reset(struct ata_device *drive)
{
	unsigned long high_16	= pci_resource_start(drive->channel->pci_dev, 4);
	byte udma_speed_flag	= IN_BYTE(high_16 + 0x001f);

	set_reg_and_wait(udma_speed_flag | 0x10, high_16 + 0x001f, 100);
	set_reg_and_wait(udma_speed_flag & ~0x10, high_16 + 0x001f, 2000);		/* 2 seconds ?! */
	printk("PDC202XX: %s channel reset.\n",
		drive->channel->unit ? "Secondary" : "Primary");
}

static unsigned int __init pdc202xx_init_chipset(struct pci_dev *dev)
{
	unsigned long high_16	= pci_resource_start(dev, 4);
	byte udma_speed_flag	= IN_BYTE(high_16 + 0x001f);
	byte primary_mode	= IN_BYTE(high_16 + 0x001a);
	byte secondary_mode	= IN_BYTE(high_16 + 0x001b);
	byte newchip		= 0;

	if (dev->resource[PCI_ROM_RESOURCE].start) {
		pci_write_config_dword(dev, PCI_ROM_ADDRESS, dev->resource[PCI_ROM_RESOURCE].start | PCI_ROM_ADDRESS_ENABLE);
		printk("%s: ROM enabled at 0x%08lx\n", dev->name, dev->resource[PCI_ROM_RESOURCE].start);
	}

	switch (dev->device) {
		case PCI_DEVICE_ID_PROMISE_20275:
		case PCI_DEVICE_ID_PROMISE_20276:
		case PCI_DEVICE_ID_PROMISE_20269:
		case PCI_DEVICE_ID_PROMISE_20268R:
		case PCI_DEVICE_ID_PROMISE_20268:
			newchip = 1;
			break;
		case PCI_DEVICE_ID_PROMISE_20267:
		case PCI_DEVICE_ID_PROMISE_20265:
		case PCI_DEVICE_ID_PROMISE_20262:
			/*
			 * software reset - this is required because the BIOS
			 * will set UDMA timing on if the drive supports it.
			 * The user may want to turn udma off. A bug is that
			 * that device cannot handle a downgrade in timing from
			 * UDMA to DMA. Disk accesses after issuing a set
			 * feature command will result in errors.
			 *
			 * A software reset leaves the timing registers intact,
			 * but resets the drives.
			 */
			set_reg_and_wait(udma_speed_flag | 0x10, high_16 + 0x001f, 100);
			set_reg_and_wait(udma_speed_flag & ~0x10, high_16 + 0x001f, 2000);   /* 2 seconds ?! */
			break;
		default:
			if ((dev->class >> 8) != PCI_CLASS_STORAGE_IDE) {
				byte irq = 0, irq2 = 0;
				pci_read_config_byte(dev, PCI_INTERRUPT_LINE, &irq);
				pci_read_config_byte(dev, (PCI_INTERRUPT_LINE)|0x80, &irq2);	/* 0xbc */
				if (irq != irq2) {
					pci_write_config_byte(dev, (PCI_INTERRUPT_LINE)|0x80, irq);	/* 0xbc */
					printk("%s: pci-config space interrupt mirror fixed.\n", dev->name);
				}
			}
			break;
	}

	if (newchip)
		goto fttk_tx_series;

	printk("%s: (U)DMA Burst Bit %sABLED " \
		"Primary %s Mode " \
		"Secondary %s Mode.\n",
		dev->name,
		(udma_speed_flag & 1) ? "EN" : "DIS",
		(primary_mode & 1) ? "MASTER" : "PCI",
		(secondary_mode & 1) ? "MASTER" : "PCI" );

#ifdef CONFIG_PDC202XX_BURST
	if (!(udma_speed_flag & 1)) {
		printk("%s: FORCING BURST BIT 0x%02x -> 0x%02x ", dev->name, udma_speed_flag, (udma_speed_flag|1));
		OUT_BYTE(udma_speed_flag|1, high_16 + 0x001f);
		printk("%sCTIVE\n", (IN_BYTE(high_16 + 0x001f) & 1) ? "A" : "INA");
	}
#endif /* CONFIG_PDC202XX_BURST */

fttk_tx_series:
	return dev->irq;
}

static unsigned int __init ata66_pdc202xx(struct ata_channel *hwif)
{
	unsigned short mask = (hwif->unit) ? (1<<11) : (1<<10);
	unsigned short CIS;

        switch(hwif->pci_dev->device) {
		case PCI_DEVICE_ID_PROMISE_20275:
		case PCI_DEVICE_ID_PROMISE_20276:
		case PCI_DEVICE_ID_PROMISE_20269:
		case PCI_DEVICE_ID_PROMISE_20268:
		case PCI_DEVICE_ID_PROMISE_20268R:
			OUT_BYTE(0x0b, (hwif->dma_base + 1));
			return (!(IN_BYTE((hwif->dma_base + 3)) & 0x04));
		default:
			pci_read_config_word(hwif->pci_dev, 0x50, &CIS);
			return (!(CIS & mask));
	}
}

static void __init ide_init_pdc202xx(struct ata_channel *hwif)
{
	hwif->tuneproc  = &config_chipset_for_pio;
	hwif->quirkproc = &check_in_drive_lists;

        switch(hwif->pci_dev->device) {
		case PCI_DEVICE_ID_PROMISE_20275:
		case PCI_DEVICE_ID_PROMISE_20276:
		case PCI_DEVICE_ID_PROMISE_20269:
		case PCI_DEVICE_ID_PROMISE_20268:
		case PCI_DEVICE_ID_PROMISE_20268R:
			hwif->speedproc = &pdc202xx_new_tune_chipset;
			hwif->resetproc = &pdc202xx_new_reset;
			break;
		case PCI_DEVICE_ID_PROMISE_20267:
		case PCI_DEVICE_ID_PROMISE_20265:
		case PCI_DEVICE_ID_PROMISE_20262:
			hwif->resetproc	= &pdc202xx_reset;
		case PCI_DEVICE_ID_PROMISE_20246:
			hwif->speedproc = &pdc202xx_tune_chipset;
		default:
			break;
	}

#ifdef CONFIG_BLK_DEV_IDEDMA
	if (hwif->dma_base) {
		hwif->udma_start = pdc202xx_udma_start;
		hwif->udma_stop = pdc202xx_udma_stop;
		hwif->udma_irq_status = pdc202xx_udma_irq_status;
		hwif->udma_irq_lost = pdc202xx_bug;
		hwif->udma_timeout = pdc202xx_bug;
		hwif->XXX_udma = pdc202xx_dmaproc;
		hwif->highmem = 1;
		if (!noautodma)
			hwif->autodma = 1;
	} else {
		hwif->drives[0].autotune = 1;
		hwif->drives[1].autotune = 1;
		hwif->autodma = 0;
	}
#else
	hwif->drives[0].autotune = 1;
	hwif->drives[1].autotune = 1;
	hwif->autodma = 0;
#endif
}


/* module data table */
static struct ata_pci_device chipsets[] __initdata = {
	{
		vendor: PCI_VENDOR_ID_PROMISE,
		device: PCI_DEVICE_ID_PROMISE_20246,
		init_chipset: pdc202xx_init_chipset,
		ata66_check: NULL,
		init_channel: ide_init_pdc202xx,
#ifndef CONFIG_PDC202XX_FORCE
		enablebits: {{0x50,0x02,0x02}, {0x50,0x04,0x04}},
#endif
		bootable: OFF_BOARD,
		extra: 16,
		flags: ATA_F_IRQ | ATA_F_DMA
	},
	{
		vendor: PCI_VENDOR_ID_PROMISE,
		device: PCI_DEVICE_ID_PROMISE_20262,
		init_chipset: pdc202xx_init_chipset,
		ata66_check: ata66_pdc202xx,
		init_channel: ide_init_pdc202xx,
#ifndef CONFIG_PDC202XX_FORCE
		enablebits: {{0x50,0x02,0x02}, {0x50,0x04,0x04}},
#endif
		bootable: OFF_BOARD,
		extra: 48,
		flags: ATA_F_IRQ | ATA_F_PHACK | ATA_F_DMA
	},
	{
		vendor: PCI_VENDOR_ID_PROMISE,
		device: PCI_DEVICE_ID_PROMISE_20265,
		init_chipset: pdc202xx_init_chipset,
		ata66_check: ata66_pdc202xx,
		init_channel: ide_init_pdc202xx,
#ifndef CONFIG_PDC202XX_FORCE
		enablebits: {{0x50,0x02,0x02}, {0x50,0x04,0x04}},
		bootable: OFF_BOARD,
#else
		bootable: ON_BOARD,
#endif
		extra: 48,
		flags: ATA_F_IRQ | ATA_F_PHACK  | ATA_F_DMA
	},
	{
		vendor: PCI_VENDOR_ID_PROMISE,
		device: PCI_DEVICE_ID_PROMISE_20267,
		init_chipset: pdc202xx_init_chipset,
		ata66_check: ata66_pdc202xx,
		init_channel: ide_init_pdc202xx,
#ifndef CONFIG_PDC202XX_FORCE
		enablebits: {{0x50,0x02,0x02}, {0x50,0x04,0x04}},
#endif
		bootable: OFF_BOARD,
		extra: 48,
		flags: ATA_F_IRQ  | ATA_F_DMA
	},
	{
		vendor: PCI_VENDOR_ID_PROMISE,
		device: PCI_DEVICE_ID_PROMISE_20268,
		init_chipset: pdc202xx_init_chipset,
		ata66_check: ata66_pdc202xx,
		init_channel: ide_init_pdc202xx,
		bootable: OFF_BOARD,
		flags: ATA_F_IRQ | ATA_F_DMA
	},
	/* Promise used a different PCI identification for the raid card
	 * apparently to try and prevent Linux detecting it and using our own
	 * raid code. We want to detect it for the ataraid drivers, so we have
	 * to list both here.. */
	{
		vendor: PCI_VENDOR_ID_PROMISE,
		device: PCI_DEVICE_ID_PROMISE_20268R,
		init_chipset: pdc202xx_init_chipset,
		ata66_check: ata66_pdc202xx,
		init_channel: ide_init_pdc202xx,
		bootable: OFF_BOARD,
		flags: ATA_F_IRQ  | ATA_F_DMA
	},
	{
		vendor: PCI_VENDOR_ID_PROMISE,
		device: PCI_DEVICE_ID_PROMISE_20269,
		init_chipset: pdc202xx_init_chipset,
		ata66_check: ata66_pdc202xx,
		init_channel: ide_init_pdc202xx,
		bootable: OFF_BOARD,
		flags: ATA_F_IRQ | ATA_F_DMA
	},
	{
		vendor: PCI_VENDOR_ID_PROMISE,
		device: PCI_DEVICE_ID_PROMISE_20275,
		init_chipset: pdc202xx_init_chipset,
		ata66_check: ata66_pdc202xx,
		init_channel: ide_init_pdc202xx,
		bootable: OFF_BOARD,
		flags: ATA_F_IRQ | ATA_F_DMA
	},
	{
		vendor: PCI_VENDOR_ID_PROMISE,
		device: PCI_DEVICE_ID_PROMISE_20276,
		init_chipset: pdc202xx_init_chipset,
		ata66_check: ata66_pdc202xx,
		init_channel: ide_init_pdc202xx,
		bootable: OFF_BOARD,
		flags: ATA_F_IRQ | ATA_F_DMA
	},
};

int __init init_pdc202xx(void)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(chipsets); ++i) {
		ata_register_chipset(&chipsets[i]);
	}

        return 0;
}
