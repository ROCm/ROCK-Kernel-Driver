/**** vi:set ts=8 sts=8 sw=8:************************************************
 *
 *  linux/drivers/ide/pdc202xx.c	Version 0.30	May. 28, 2002
 *
 *  Copyright (C) 1998-2000	Andre Hedrick <andre@linux-ide.org>
 *  Copyright (C) 2002		BartÅ^Âomiej Å»oÅ^Ânierkiewicz
 *
 *  Portions Copyright (C) 1999-2002 Promise Technology, Inc.
 *  Author:	Frank Tiernan <frankt@promise.com>
 *		Hank Yang <support@promise.com.tw>
 *
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
 *
 *  8 are UDMA supported and 4 are limited to DMA mode 2 multi-word.
 *  The 8/4 ratio is a BIOS code limit by promise.
 *
 *  UNLESS you enable "CONFIG_PDC202XX_BURST"
 *
 *  History:
 *  Sync 2.5 driver with Promise 2.4 driver v1.20.0.7 (07/11/02):
 *		- Add PDC20271 support
 *		- Disable LBA48 support on PDC20262
 *		- Fix ATAPI UDMA port value
 *		- Add new quirk drive
 *		- Adjust timings for all drives when using ATA133
 *		- Update pdc202xx_reset() waiting time
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

#define PDC202XX_DEBUG_DRIVE_INFO		0
#define PDC202XX_DECODE_REGISTER_INFO		0

/* A Register */
#define	SYNC_ERRDY_EN	0xC0

#define	SYNC_IN		0x80	/* control bit, different for master vs. slave drives */
#define	ERRDY_EN	0x40	/* control bit, different for master vs. slave drives */
#define	IORDY_EN	0x20	/* PIO: IOREADY */
#define	PREFETCH_EN	0x10	/* PIO: PREFETCH */

#define PDC_CLK		0x11
#define PDC_PRIMARY	0x1a
#define PDC_SECONDARY	0x1b
#define PDC_UDMA	0x1f

#if PDC202XX_DECODE_REGISTER_INFO

struct pdc_bit_messages {
	u8 mask;
	const char *msg;
};

static struct pdc_bit_messages pdc_reg_A[] = {
	{ 0x80, "SYNC_IN" },
	{ 0x40, "ERRDY_EN" },
	{ 0x20, "IORDY_EN" },
	{ 0x10, "PREFETCH_EN" },
	/* PA3-PA0 - PIO "A" timing */
};

static struct pdc_bit_messages pdc_reg_B[] = {
	/* MB2-MB0 - DMA "B" timing */
	{ 0x10, "PIO_FORCED/PB4" },	/* PIO_FORCE 1:0 */
	/* PB3-PB0 - PIO "B" timing */
};

static struct pdc_bit_messages pdc_reg_C[] = {
	{ 0x80, "DMARQp" },
	{ 0x40, "IORDYp" },
	{ 0x20, "DMAR_EN" },
	{ 0x10, "DMAW_EN" },
	/* MC3-MC0 - DMA "C" timing */
};

static void pdc_dump_bits(struct pdc_bit_messages *msgs, byte bits)
{
	int i;

	printk(KERN_DEBUG " { ");

	for (i = 0; i < ARRAY_SIZE(msgs); i++, msgs++)
		if (bits & msgs->mask)
			printk(KERN_DEBUG "%s ", msgs->msg);

	printk(KERN_DEBUG " }\n");
}
#endif /* PDC202XX_DECODE_REGISTER_INFO */

static struct ata_device* drives[4];

int check_in_drive_lists(struct ata_device *drive)
{
	static const char *pdc_quirk_drives[] = {
		"QUANTUM FIREBALLlct08 08",
		"QUANTUM FIREBALLP KA6.4",
		"QUANTUM FIREBALLP KA9.1",
		"QUANTUM FIREBALLP LM20.4",
		"QUANTUM FIREBALLP KX13.6",
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

static int __init pdc202xx_modes_map(struct ata_channel *ch)
{
	int map = XFER_EPIO | XFER_SWDMA | XFER_MWDMA | XFER_UDMA;

	switch(ch->pci_dev->device) {
		case PCI_DEVICE_ID_PROMISE_20276:
		case PCI_DEVICE_ID_PROMISE_20275:
		case PCI_DEVICE_ID_PROMISE_20271:
		case PCI_DEVICE_ID_PROMISE_20269:
			map |= XFER_UDMA_133;
		case PCI_DEVICE_ID_PROMISE_20268R:
		case PCI_DEVICE_ID_PROMISE_20268:
			map &= ~XFER_SWDMA;
		case PCI_DEVICE_ID_PROMISE_20267:
		case PCI_DEVICE_ID_PROMISE_20265:
			map |= XFER_UDMA_100;
		case PCI_DEVICE_ID_PROMISE_20262:
			map |= XFER_UDMA_66;

			if (!ch->udma_four) {
				printk(KERN_WARNING "%s: 40-pin cable, speed reduced to UDMA(33) mode.\n", ch->name);
				map &= ~XFER_UDMA_80W;
			}
	}

	return map;
}

static int pdc202xx_tune_chipset(struct ata_device *drive, byte speed)
{
	struct pci_dev *dev = drive->channel->pci_dev;
	u32 drive_conf;
	u8 drive_pci, AP, BP, CP, TA = 0, TB, TC = 0;
#if PDC202XX_DECODE_REGISTER_INFO
	u8 DP;
#endif

	drive_pci = 0x60 + (drive->dn << 2);

	if ((drive->type != ATA_DISK) && (speed < XFER_SW_DMA_0))
		return -1;

	pci_read_config_dword(dev, drive_pci, &drive_conf);
	pci_read_config_byte(dev, drive_pci, &AP);
	pci_read_config_byte(dev, drive_pci + 1, &BP);
	pci_read_config_byte(dev, drive_pci + 2, &CP);

	switch(speed) {
#ifdef CONFIG_BLK_DEV_IDEDMA
		case XFER_UDMA_5:
		case XFER_UDMA_4:	TB = 0x20; TC = 0x01; break;
		case XFER_UDMA_3:	TB = 0x40; TC = 0x02; break;
		case XFER_UDMA_2:	TB = 0x20; TC = 0x01; break;
		case XFER_UDMA_1:	TB = 0x40; TC = 0x02; break;
		case XFER_UDMA_0:	TB = 0x60; TC = 0x03; break;
		case XFER_MW_DMA_2:	TB = 0x60; TC = 0x03; break;
		case XFER_MW_DMA_1:	TB = 0x60; TC = 0x04; break;
		case XFER_MW_DMA_0:	TB = 0x60; TC = 0x05; break;
		case XFER_SW_DMA_2:	TB = 0x60; TC = 0x05; break;
		case XFER_SW_DMA_1:	TB = 0x80; TC = 0x06; break;
		case XFER_SW_DMA_0:	TB = 0xC0; TC = 0x0B; break;
#endif
		case XFER_PIO_4:	TA = 0x01; TB = 0x04; break;
		case XFER_PIO_3:	TA = 0x02; TB = 0x06; break;
		case XFER_PIO_2:	TA = 0x03; TB = 0x08; break;
		case XFER_PIO_1:	TA = 0x05; TB = 0x0C; break;
		case XFER_PIO_0:
		default:		TA = 0x09; TB = 0x13; break;
	}

#ifdef CONFIG_BLK_DEV_IDEDMA
        if (speed >= XFER_SW_DMA_0) {
		pci_write_config_byte(dev, drive_pci + 1, (BP & ~0xf0) | TB);
		pci_write_config_byte(dev, drive_pci + 2, (CP & ~0x0f) | TC);
	} else
#endif
	{
		pci_write_config_byte(dev, drive_pci, (AP & ~0x0f) | TA);
		pci_write_config_byte(dev, drive_pci + 1, (BP & ~0x07) | TB);
	}

#if PDC202XX_DECODE_REGISTER_INFO
	pci_read_config_byte(dev, drive_pci, &AP);
	pci_read_config_byte(dev, drive_pci + 1, &BP);
	pci_read_config_byte(dev, drive_pci + 2, &CP);
	pci_read_config_byte(dev, drive_pci + 3, &DP);

	printk(KERN_DEBUG "AP(%x): PIO(A) = %d\n", AP, AP & 0x0f);
	pdc_dump_bits(pdc_reg_A, AP);

	printk(KERN_DEBUG "BP(%x): DMA(B) = %d PIO(B) = %d\n",
			  BP, (BP & 0xe0) >> 5, BP & 0x0f);
	pdc_dump_bits(pdc_reg_B, BP);

	printk(KERN_DEBUG "CP(%x): DMA(C) = %d\n", CP, CP & 0x0f);
	pdc_dump_bits(pdc_reg_C, CP);

	printk(KERN_DEBUG "DP(%x)\n", DP);
#endif

#if PDC202XX_DEBUG_DRIVE_INFO
	printk("%s: %02x drive%d 0x%08x ",
		drive->name, speed,
		drive->dn, drive_conf);
		pci_read_config_dword(dev, drive_pci, &drive_conf);
	printk("0x%08x\n", drive_conf);
#endif

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
	u32 high_16 = pci_resource_start(hwif->pci_dev, 4);
	u32 indexreg = high_16 + (hwif->unit ? 0x09 : 0x01);
	u32 datareg = indexreg + 2;

	u8 adj = (drive->dn % 2) ? 0x08 : 0x00;
	u8 thold = 0x10;
	int err, i, j = hwif->unit ? 2 : 0;

#ifdef CONFIG_BLK_DEV_IDEDMA
	/* Setting tHOLD bit to 0 if using UDMA mode 2 */
	if (speed == XFER_UDMA_2) {
		OUT_BYTE((thold + adj), indexreg);
		OUT_BYTE((IN_BYTE(datareg) & 0x7f), datareg);
	}
#endif

	for (i = 0 ; i < 2 ; i++)
		if (hwif->drives[i].present)
			drives[i+j] = &hwif->drives[i];

	err = ide_config_drive_speed(drive, speed);

	/* For modes < UDMA mode 6 we need only to SET_FEATURE */
	if (speed < XFER_UDMA_6)
		return err;

	/* We need to adjust timings to ATA133 clock if ATA133 drives exist */
	for (i = 0 ; i < 4 ; i++) {
		if (!drives[i])
			continue;

		/* Primary = 0x01, Secondary = 0x09 */
		indexreg = high_16 + ((i > 1) ? 0x09 : 0x01);
		datareg = indexreg + 2;

		/* Master = 0x00, Slave = 0x08 */
		adj = (i % 2) ? 0x08 : 0x00;

		switch (drives[i]->current_speed) {
#ifdef CONFIG_BLK_DEV_IDEDMA
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
#endif
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
	}

	return err;
}

/*   0    1    2    3    4    5    6   7   8
 * 960, 480, 390, 300, 240, 180, 120, 90, 60
 *           180, 150, 120,  90,  60
 * DMA_Speed
 * 180, 120,  90,  90,  90,  60,  30
 *  11,   5,   4,   3,   2,   1,   0
 */
static void pdc202xx_tune_drive(struct ata_device *drive, u8 pio)
{
	u8 speed;

	if (pio == 255)
		speed = ata_best_pio_mode(drive);
	else	speed = XFER_PIO_0 + min_t(byte, pio, 4);

	pdc202xx_tune_chipset(drive, speed);
}

#ifdef CONFIG_BLK_DEV_IDEDMA
static int pdc202xx_tx_udma_setup(struct ata_device *drive, int map)
{
	struct hd_driveid *id = drive->id;
	struct ata_channel *ch = drive->channel;
	u32 indexreg = ch->dma_base + 1;
	u32 datareg = indexreg + 2;
	u8 adj = (drive->dn % 2) ? 0x08 : 0x00;

	if (drive->type != ATA_DISK)
		return 0;

	/* IORDY_EN & PREFETCH_EN */
	if (id->capability & 4)
		set_2regs(0x13, (IN_BYTE(datareg)|0x03));

	return udma_generic_setup(drive, map);
}

static int pdc202xx_udma_setup(struct ata_device *drive, int map)
{
	struct hd_driveid *id = drive->id;
	struct ata_channel *hwif = drive->channel;
	struct hd_driveid *mate_id = hwif->drives[!(drive->dn%2)].id;
	struct pci_dev *dev = hwif->pci_dev;
	u32 high_16 = pci_resource_start(dev, 4);
	u32 drive_conf;
	u8 drive_pci = 0, AP, tmp, mode = -1;
	u8 CLKSPD, mask = hwif->unit ? 0x08 : 0x02;

	/*
	 * Set the control register to use the 66Mhz system
	 * clock for UDMA 3/4 mode operations. If one drive on
	 * a channel is U66 capable but the other isn't we
	 * fall back to U33 mode. The BIOS INT 13 hooks turn
	 * the clock on then off for each read/write issued.
	 * We can do the same in device specific udma_start/stop()
	 * routines or better try to readjust timings.
	 *
	 * FIXME: move this to pdc202xx_tuneproc()
	 *        right now you can't downgrade from U66 to U33  --bkz
	 */
	if (id->dma_ultra & 0x0078) {	/* UDMA 3, 4, 5 and 6 */
		CLKSPD = IN_BYTE(high_16 + PDC_CLK);
		/* check cable and mate (must be at least udma3 capable) */
		if (!hwif->udma_four ||
		    !mate_id || !(mate_id->dma_ultra & 0x0078))
			OUT_BYTE(CLKSPD & ~mask, high_16 + PDC_CLK);
		else
			/* cable ok, mate ok or single drive */
			OUT_BYTE(CLKSPD | mask, high_16 + PDC_CLK);
	}

	if (drive->dn > 3)	/* FIXME: remove this --bkz */
		return 0;

	drive_pci = 0x60 + (drive->dn << 2);
	pci_read_config_dword(dev, drive_pci, &drive_conf);
	if ((drive_conf != 0x004ff304) && (drive_conf != 0x004ff3c4))
		goto chipset_is_set;

	/* FIXME: what if SYNC_ERRDY is enabled for slave
		  and disabled for master? --bkz */
	pci_read_config_byte(dev, drive_pci, &AP);
	/* enable SYNC_ERRDY for master and slave (if enabled for master) */
	if (!(AP & SYNC_ERRDY_EN)) {
		if (!(drive->dn % 2)) {
			pci_write_config_byte(dev, drive_pci, AP|SYNC_ERRDY_EN);
		} else {
			pci_read_config_byte(dev, drive_pci - 4, &tmp);
			if (tmp & SYNC_ERRDY_EN)
				pci_write_config_byte(dev, drive_pci, AP|SYNC_ERRDY_EN);
		}
	}

chipset_is_set:

	if (drive->type != ATA_DISK)
		return 0;

	pci_read_config_byte(dev, drive_pci, &AP);
	if (id->capability & 4)		/* IORDY_EN */
		pci_write_config_byte(dev, drive_pci, AP|IORDY_EN);
	pci_read_config_byte(dev, drive_pci, &AP);
	if (drive->type == ATA_DISK)	/* PREFETCH_EN */
		pci_write_config_byte(dev, drive_pci, AP|PREFETCH_EN);

	map = hwif->modes_map;

	if (!eighty_ninty_three(drive))
		map &= ~XFER_UDMA_80W;

	mode = ata_timing_mode(drive, map);
	if (mode < XFER_SW_DMA_0) {
		/* restore original pci-config space */
		pci_write_config_dword(dev, drive_pci, drive_conf);
		return 0;
	}

	return udma_generic_setup(drive, map);
}

static void pdc202xx_udma_start(struct ata_device *drive, struct request *rq)
{
	struct ata_channel *ch = drive->channel;
	u32 high_16 = pci_resource_start(ch->pci_dev, 4);
	unsigned long atapi_port = high_16 + (ch->unit ? 0x24 : 0x20);

	/* Enable ATAPI UDMA port for 48bit data on PDC20265/PDC20267 */
	if (drive->addressing) {
		unsigned long word_count = 0, hankval;
		u32 clockreg = high_16 + PDC_CLK;
		u8 clock = IN_BYTE(clockreg);

		OUT_BYTE(clock | (ch->unit ? 0x08 : 0x02), clockreg);
		word_count = (rq->nr_sectors << 8);
		hankval = (rq_data_dir(rq) == READ) ? 0x05 << 24 : 0x06 << 24;
		hankval |= word_count;
		outl(hankval, atapi_port);
	}

	/* Note that this is done *after* the cmd has been issued to the drive,
	 * as per the BM-IDE spec.  The Promise Ultra33 doesn't work correctly
	 * when we do this part before issuing the drive cmd.
	 */

	outb(inb(ch->dma_base) | 1, ch->dma_base); /* start DMA */
}

static int pdc202xx_udma_stop(struct ata_device *drive)
{
	struct ata_channel *ch = drive->channel;
	u32 high_16 = pci_resource_start(ch->pci_dev, 4);
	unsigned long atapi_port = high_16 + (ch->unit ? 0x24 : 0x20);
	unsigned long dma_base = ch->dma_base;
	u8 dma_stat;

	/* Disable ATAPI UDMA port for 48bit data on PDC20265/PDC20267 */
	if (drive->addressing) {
		u32 clockreg = high_16 + PDC_CLK;
		u8 clock;

		outl(0, atapi_port);		/* zero out extra */
		clock = IN_BYTE(clockreg);
		OUT_BYTE(clock & ~(ch->unit ? 0x08 : 0x02), clockreg);
	}

	outb(inb(dma_base)&~1, dma_base);	/* stop DMA */
	dma_stat = inb(dma_base+2);		/* get DMA status */
	outb(dma_stat|6, dma_base+2);		/* clear the INTR & ERROR bits */
	udma_destroy_table(ch);			/* purge DMA mappings */

	return (dma_stat & 7) != 4 ? (0x10 | dma_stat) : 0;	/* verify good DMA status */
}

static void pdc202xx_bug(struct ata_device *drive)
{
	if (!drive->channel->resetproc)
		return;
	/* Assume naively that resetting the drive may help. */
	drive->channel->resetproc(drive);
}

#endif

/* FIXME: use generic ata_reset()  --bzolnier */
static void pdc202xx_reset(struct ata_device *drive)
{
	outb(0x04, drive->channel->io_ports[IDE_CONTROL_OFFSET]);
	udelay(10);
	outb(0x00, drive->channel->io_ports[IDE_CONTROL_OFFSET]);
	printk(KERN_INFO "PDC202XX: %s channel reset.\n",
			 drive->channel->unit ? "Secondary" : "Primary");
}

/*
 * software host reset
 *
 * BIOS will set UDMA timing on if the drive supports it.
 * The user may then want to turn it off. A bug is
 * that device cannot handle a downgrade in timing from
 * UDMA to DMA. Disk accesses after issuing a set
 * feature command will result in errors.
 *
 * A software reset leaves the timing registers intact,
 * but resets the drives on both channels.
 */
#if 0
static void pdc202xx_reset_host(struct pci_dev *dev)
{
	u32 high_16 = pci_resource_start(dev, 4);
	u8 burst = IN_BYTE(high_16 + PDC_UDMA);

	set_reg_and_wait(burst | 0x10, high_16 + PDC_UDMA, 100);
	/* FIXME: 2 seconds ?! */
	set_reg_and_wait(burst & ~0x10, high_16 + PDC_UDMA, 2000);
	printk(KERN_INFO "%s: device reseted.\n", dev->name);
}

void pdc202xx_reset(struct ata_device *drive)
{
	struct ata_channel *ch = drive->channel;
	printk(KERN_INFO "%s: channel needs reset.\n", ch->name);
	pdc202xx_reset_host(ch->pci_dev);
}
#endif

static unsigned int __init pdc202xx_init_chipset(struct pci_dev *dev)
{
	u32 high_16 = pci_resource_start(dev, 4);
	u32 burstreg = high_16 + PDC_UDMA;
	u8 burst = IN_BYTE(burstreg);

	set_reg_and_wait(burst | 0x10, burstreg, 100);
	/* FIXME: 2 seconds ?! */
	set_reg_and_wait(burst & ~0x10, burstreg, 2000);

	if (dev->resource[PCI_ROM_RESOURCE].start) {
		pci_write_config_dword(dev, PCI_ROM_ADDRESS, dev->resource[PCI_ROM_RESOURCE].start | PCI_ROM_ADDRESS_ENABLE);
		printk(KERN_INFO "%s: ROM enabled at 0x%08lx\n", dev->name, dev->resource[PCI_ROM_RESOURCE].start);
	}

#if 0
	switch (dev->device) {
		case PCI_DEVICE_ID_PROMISE_20267:
		case PCI_DEVICE_ID_PROMISE_20265:
		case PCI_DEVICE_ID_PROMISE_20262:
			pdc202xx_reset_host(dev);
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
#endif

#ifdef CONFIG_PDC202XX_BURST
	if (!(burst & 1)) {
		printk(KERN_INFO "%s: forcing (U)DMA BURST.\n", dev->name);
		OUT_BYTE(burst | 1, burstreg);
		burst = IN_BYTE(burstreg);
	}
#endif
	printk(KERN_INFO "%s: (U)DMA BURST %sabled, "
			 "primary %s mode, secondary %s mode.\n",
	       dev->name, (burst & 1) ? "en" : "dis",
	       (IN_BYTE(high_16 + PDC_PRIMARY) & 1) ? "MASTER" : "PCI",
	       (IN_BYTE(high_16 + PDC_SECONDARY) & 1) ? "MASTER" : "PCI" );

	return dev->irq;
}

#if 0
/* chipsets newer then 20267 */
static unsigned int __init pdc202xx_tx_init_chipset(struct pci_dev *dev)
{
	if (dev->resource[PCI_ROM_RESOURCE].start) {
		pci_write_config_dword(dev, PCI_ROM_ADDRESS, dev->resource[PCI_ROM_RESOURCE].start | PCI_ROM_ADDRESS_ENABLE);
		printk(KERN_INFO "%s: ROM enabled at 0x%08lx.\n", dev->name, dev->resource[PCI_ROM_RESOURCE].start);
	}
	return dev->irq;
}
#endif

static unsigned int __init pdc202xx_ata66_check(struct ata_channel *ch)
{
	u16 CIS;

	pci_read_config_word(ch->pci_dev, 0x50, &CIS);
	return !(CIS & (1 << (10 + ch->unit)));
}

/* chipsets newer then 20267 */
static unsigned int __init pdc202xx_tx_ata66_check(struct ata_channel *ch)
{
	OUT_BYTE(0x0b, ch->dma_base + 1);
	return !(IN_BYTE(ch->dma_base + 3) & 0x04);
}

static void __init ide_init_pdc202xx(struct ata_channel *hwif)
{
	hwif->tuneproc  = &pdc202xx_tune_drive;
	hwif->quirkproc = &check_in_drive_lists;
	hwif->resetproc = &pdc202xx_reset;

        switch(hwif->pci_dev->device) {
		case PCI_DEVICE_ID_PROMISE_20276:
		case PCI_DEVICE_ID_PROMISE_20275:
		case PCI_DEVICE_ID_PROMISE_20271:
		case PCI_DEVICE_ID_PROMISE_20269:
		case PCI_DEVICE_ID_PROMISE_20268:
		case PCI_DEVICE_ID_PROMISE_20268R:
			hwif->udma_four = pdc202xx_tx_ata66_check(hwif);

			hwif->speedproc = &pdc202xx_new_tune_chipset;
#ifdef CONFIG_BLK_DEV_IDEDMA
			if (hwif->dma_base)
				hwif->udma_setup = pdc202xx_tx_udma_setup;
#endif
			break;
		case PCI_DEVICE_ID_PROMISE_20267:
		case PCI_DEVICE_ID_PROMISE_20265:
#ifdef CONFIG_BLK_DEV_IDEDMA
			/* we need special functions for lba48 */
			if (hwif->dma_base) {
				hwif->udma_start = pdc202xx_udma_start;
				hwif->udma_stop = pdc202xx_udma_stop;
			}
#endif
		/* PDC20262 doesn't support LBA48 */
		case PCI_DEVICE_ID_PROMISE_20262:
			hwif->udma_four = pdc202xx_ata66_check(hwif);

		case PCI_DEVICE_ID_PROMISE_20246:
#ifdef CONFIG_BLK_DEV_IDEDMA
			if (hwif->dma_base)
				hwif->udma_setup = pdc202xx_udma_setup;
#endif
			hwif->speedproc = &pdc202xx_tune_chipset;
		default:
			break;
	}

#ifdef CONFIG_BLK_DEV_IDEDMA
	if (hwif->dma_base) {
		hwif->modes_map = pdc202xx_modes_map(hwif);
		hwif->udma_irq_lost = pdc202xx_bug;
		hwif->udma_timeout = pdc202xx_bug;
		hwif->highmem = 1;
	} else
#endif
	{
		hwif->drives[0].autotune = 1;
		hwif->drives[1].autotune = 1;
	}
}


/* module data table */
static struct ata_pci_device chipsets[] __initdata = {
	{
		vendor: PCI_VENDOR_ID_PROMISE,
		device: PCI_DEVICE_ID_PROMISE_20246,
		init_chipset: pdc202xx_init_chipset,
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
		init_channel: ide_init_pdc202xx,
		bootable: OFF_BOARD,
		flags: ATA_F_IRQ  | ATA_F_DMA
	},
	{
		vendor: PCI_VENDOR_ID_PROMISE,
		device: PCI_DEVICE_ID_PROMISE_20269,
		init_chipset: pdc202xx_init_chipset,
		init_channel: ide_init_pdc202xx,
		bootable: OFF_BOARD,
		flags: ATA_F_IRQ | ATA_F_DMA
	},
	{
		vendor: PCI_VENDOR_ID_PROMISE,
		device: PCI_DEVICE_ID_PROMISE_20271,
		init_chipset: pdc202xx_init_chipset,
		init_channel: ide_init_pdc202xx,
		bootable: OFF_BOARD,
		flags: ATA_F_IRQ | ATA_F_DMA
	},
	{
		vendor: PCI_VENDOR_ID_PROMISE,
		device: PCI_DEVICE_ID_PROMISE_20275,
		init_chipset: pdc202xx_init_chipset,
		init_channel: ide_init_pdc202xx,
		bootable: OFF_BOARD,
		flags: ATA_F_IRQ | ATA_F_DMA
	},
	{
		vendor: PCI_VENDOR_ID_PROMISE,
		device: PCI_DEVICE_ID_PROMISE_20276,
		init_chipset: pdc202xx_init_chipset,
		init_channel: ide_init_pdc202xx,
		bootable: OFF_BOARD,
		flags: ATA_F_IRQ | ATA_F_DMA
	},
};

int __init init_pdc202xx(void)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(chipsets); ++i)
		ata_register_chipset(&chipsets[i]);

        return 0;
}
