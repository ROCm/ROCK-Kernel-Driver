/*
 * linux/drivers/ide/alim15x3.c		Version 0.10	Jun. 9, 2000
 *
 *  Copyright (C) 1998-2000 Michel Aubry, Maintainer
 *  Copyright (C) 1998-2000 Andrzej Krzysztofowicz, Maintainer
 *  Copyright (C) 1999-2000 CJ, cjtsai@ali.com.tw, Maintainer
 *
 *  Copyright (C) 1998-2000 Andre Hedrick (andre@linux-ide.org)
 *  May be copied or modified under the terms of the GNU General Public License
 *
 *  (U)DMA capable version of ali 1533/1543(C), 1535(D)
 *
 **********************************************************************
 *  9/7/99 --Parts from the above author are included and need to be
 *  converted into standard interface, once I finish the thought.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/hdreg.h>
#include <linux/ide.h>

#include <asm/io.h>

#include "ata-timing.h"
#include "pcihost.h"

#undef DISPLAY_ALI_TIMINGS

#if defined(DISPLAY_ALI_TIMINGS) && defined(CONFIG_PROC_FS)
#include <linux/stat.h>
#include <linux/proc_fs.h>

static int ali_get_info(char *buffer, char **addr, off_t offset, int count);
extern int (*ali_display_info)(char *, char **, off_t, int);  /* ide-proc.c */
static struct pci_dev *bmide_dev;

char *fifo[4] = {
	"FIFO Off",
	"FIFO On ",
	"DMA mode",
	"PIO mode" };

char *udmaT[8] = {
	"1.5T",
	"  2T",
	"2.5T",
	"  3T",
	"3.5T",
	"  4T",
	"  6T",
	"  8T"
};

char *channel_status[8] = {
	"OK            ",
	"busy          ",
	"DRQ           ",
	"DRQ busy      ",
	"error         ",
	"error busy    ",
	"error DRQ     ",
	"error DRQ busy"
};

static int ali_get_info (char *buffer, char **addr, off_t offset, int count)
{
	byte reg53h, reg5xh, reg5yh, reg5xh1, reg5yh1;
	unsigned int bibma;
	byte c0, c1;
	byte rev, tmp;
	char *p = buffer;
	char *q;

	/* fetch rev. */
	pci_read_config_byte(bmide_dev, 0x08, &rev);
	if (rev >= 0xc1)	/* M1543C or newer */
		udmaT[7] = " ???";
	else
		fifo[3]  = "   ???  ";

	/* first fetch bibma: */
	pci_read_config_dword(bmide_dev, 0x20, &bibma);
	bibma = (bibma & 0xfff0) ;
	/*
	 * at that point bibma+0x2 et bibma+0xa are byte
	 * registers to investigate:
	 */
	c0 = inb((unsigned short)bibma + 0x02);
	c1 = inb((unsigned short)bibma + 0x0a);

	p += sprintf(p,
		"\n                                Ali M15x3 Chipset.\n");
	p += sprintf(p,
		"                                ------------------\n");
	pci_read_config_byte(bmide_dev, 0x78, &reg53h);
	p += sprintf(p, "PCI Clock: %d.\n", reg53h);

	pci_read_config_byte(bmide_dev, 0x53, &reg53h);
	p += sprintf(p,
		"CD_ROM FIFO:%s, CD_ROM DMA:%s\n",
		(reg53h & 0x02) ? "Yes" : "No ",
		(reg53h & 0x01) ? "Yes" : "No " );
	pci_read_config_byte(bmide_dev, 0x74, &reg53h);
	p += sprintf(p,
		"FIFO Status: contains %d Words, runs%s%s\n\n",
		(reg53h & 0x3f),
		(reg53h & 0x40) ? " OVERWR" : "",
		(reg53h & 0x80) ? " OVERRD." : "." );

	p += sprintf(p,
		"-------------------primary channel-------------------secondary channel---------\n\n");

	pci_read_config_byte(bmide_dev, 0x09, &reg53h);
	p += sprintf(p,
		"channel status:       %s                               %s\n",
		(reg53h & 0x20) ? "On " : "Off",
		(reg53h & 0x10) ? "On " : "Off" );

	p += sprintf(p,
		"both channels togth:  %s                               %s\n",
		(c0&0x80) ? "No " : "Yes",
		(c1&0x80) ? "No " : "Yes" );

	pci_read_config_byte(bmide_dev, 0x76, &reg53h);
	p += sprintf(p,
		"Channel state:        %s                    %s\n",
		channel_status[reg53h & 0x07],
		channel_status[(reg53h & 0x70) >> 4] );

	pci_read_config_byte(bmide_dev, 0x58, &reg5xh);
	pci_read_config_byte(bmide_dev, 0x5c, &reg5yh);
	p += sprintf(p,
		"Add. Setup Timing:    %dT                                %dT\n",
		(reg5xh & 0x07) ? (reg5xh & 0x07) : 8,
		(reg5yh & 0x07) ? (reg5yh & 0x07) : 8 );

	pci_read_config_byte(bmide_dev, 0x59, &reg5xh);
	pci_read_config_byte(bmide_dev, 0x5d, &reg5yh);
	p += sprintf(p,
		"Command Act. Count:   %dT                                %dT\n"
		"Command Rec. Count:   %dT                               %dT\n\n",
		(reg5xh & 0x70) ? ((reg5xh & 0x70) >> 4) : 8,
		(reg5yh & 0x70) ? ((reg5yh & 0x70) >> 4) : 8, 
		(reg5xh & 0x0f) ? (reg5xh & 0x0f) : 16,
		(reg5yh & 0x0f) ? (reg5yh & 0x0f) : 16 );

	p += sprintf(p,
		"----------------drive0-----------drive1------------drive0-----------drive1------\n\n");
	p += sprintf(p,
		"DMA enabled:      %s              %s               %s              %s\n",
		(c0&0x20) ? "Yes" : "No ",
		(c0&0x40) ? "Yes" : "No ",
		(c1&0x20) ? "Yes" : "No ",
		(c1&0x40) ? "Yes" : "No " );

	pci_read_config_byte(bmide_dev, 0x54, &reg5xh);
	pci_read_config_byte(bmide_dev, 0x55, &reg5yh);
	q = "FIFO threshold:   %2d Words         %2d Words          %2d Words         %2d Words\n";
	if (rev < 0xc1) {
		if ((rev == 0x20) && (pci_read_config_byte(bmide_dev, 0x4f, &tmp), (tmp &= 0x20))) {
			p += sprintf(p, q, 8, 8, 8, 8);
		} else {
			p += sprintf(p, q,
				(reg5xh & 0x03) + 12,
				((reg5xh & 0x30)>>4) + 12,
				(reg5yh & 0x03) + 12,
				((reg5yh & 0x30)>>4) + 12 );
		}
	} else {
		int t1 = (tmp = (reg5xh & 0x03)) ? (tmp << 3) : 4;
		int t2 = (tmp = ((reg5xh & 0x30)>>4)) ? (tmp << 3) : 4;
		int t3 = (tmp = (reg5yh & 0x03)) ? (tmp << 3) : 4;
		int t4 = (tmp = ((reg5yh & 0x30)>>4)) ? (tmp << 3) : 4;
		p += sprintf(p, q, t1, t2, t3, t4);
	}

#if 0
	p += sprintf(p, 
		"FIFO threshold:   %2d Words         %2d Words          %2d Words         %2d Words\n",
		(reg5xh & 0x03) + 12,
		((reg5xh & 0x30)>>4) + 12,
		(reg5yh & 0x03) + 12,
		((reg5yh & 0x30)>>4) + 12 );
#endif

	p += sprintf(p,
		"FIFO mode:        %s         %s          %s         %s\n",
		fifo[((reg5xh & 0x0c) >> 2)],
		fifo[((reg5xh & 0xc0) >> 6)],
		fifo[((reg5yh & 0x0c) >> 2)],
		fifo[((reg5yh & 0xc0) >> 6)] );

	pci_read_config_byte(bmide_dev, 0x5a, &reg5xh);
	pci_read_config_byte(bmide_dev, 0x5b, &reg5xh1);
	pci_read_config_byte(bmide_dev, 0x5e, &reg5yh);
	pci_read_config_byte(bmide_dev, 0x5f, &reg5yh1);

	p += sprintf(p,/*
		"------------------drive0-----------drive1------------drive0-----------drive1------\n")*/
		"Dt RW act. Cnt    %2dT              %2dT               %2dT              %2dT\n"
		"Dt RW rec. Cnt    %2dT              %2dT               %2dT              %2dT\n\n",
		(reg5xh & 0x70) ? ((reg5xh & 0x70) >> 4) : 8,
		(reg5xh1 & 0x70) ? ((reg5xh1 & 0x70) >> 4) : 8,
		(reg5yh & 0x70) ? ((reg5yh & 0x70) >> 4) : 8,
		(reg5yh1 & 0x70) ? ((reg5yh1 & 0x70) >> 4) : 8,
		(reg5xh & 0x0f) ? (reg5xh & 0x0f) : 16,
		(reg5xh1 & 0x0f) ? (reg5xh1 & 0x0f) : 16,
		(reg5yh & 0x0f) ? (reg5yh & 0x0f) : 16,
		(reg5yh1 & 0x0f) ? (reg5yh1 & 0x0f) : 16 );

	p += sprintf(p,
		"-----------------------------------UDMA Timings--------------------------------\n\n");

	pci_read_config_byte(bmide_dev, 0x56, &reg5xh);
	pci_read_config_byte(bmide_dev, 0x57, &reg5yh);
	p += sprintf(p,
		"UDMA:             %s               %s                %s               %s\n"
		"UDMA timings:     %s             %s              %s             %s\n\n",
		(reg5xh & 0x08) ? "OK" : "No",
		(reg5xh & 0x80) ? "OK" : "No",
		(reg5yh & 0x08) ? "OK" : "No",
		(reg5yh & 0x80) ? "OK" : "No",
		udmaT[(reg5xh & 0x07)],
		udmaT[(reg5xh & 0x70) >> 4],
		udmaT[reg5yh & 0x07],
		udmaT[(reg5yh & 0x70) >> 4] );

	return p-buffer; /* => must be less than 4k! */
}
#endif  /* defined(DISPLAY_ALI_TIMINGS) && defined(CONFIG_PROC_FS) */

static byte m5229_revision;
static byte chip_is_1543c_e;

byte ali_proc = 0;
static struct pci_dev *isa_dev;

static void ali15x3_tune_drive(struct ata_device *drive, byte pio)
{
	struct ata_timing *t;
	struct ata_channel *hwif = drive->channel;
	struct pci_dev *dev = hwif->pci_dev;
	int s_time, a_time, c_time;
	byte s_clc, a_clc, r_clc;
	unsigned long flags;
	int port = hwif->unit ? 0x5c : 0x58;
	int portFIFO = hwif->unit ? 0x55 : 0x54;
	byte cd_dma_fifo = 0;

	if (pio == 255)
		pio = ata_timing_mode(drive, XFER_PIO | XFER_EPIO);
	else
		pio = XFER_PIO_0 + min_t(byte, pio, 4);

	t = ata_timing_data(pio);

	s_time = t->setup;
	a_time = t->active;
	if ((s_clc = (s_time * system_bus_speed + 999) / 1000) >= 8)
		s_clc = 0;
	if ((a_clc = (a_time * system_bus_speed + 999) / 1000) >= 8)
		a_clc = 0;
	c_time = t->cycle;

#if 0
	if ((r_clc = ((c_time - s_time - a_time) * system_bus_speed + 999) / 1000) >= 16)
		r_clc = 0;
#endif

	if (!(r_clc = (c_time * system_bus_speed + 999) / 1000 - a_clc - s_clc)) {
		r_clc = 1;
	} else {
		if (r_clc >= 16)
			r_clc = 0;
	}
	__save_flags(flags);
	__cli();
	
	/* 
	 * PIO mode => ATA FIFO on, ATAPI FIFO off
	 */
	pci_read_config_byte(dev, portFIFO, &cd_dma_fifo);
	if (drive->type == ATA_DISK) {
		if (hwif->index) {
			pci_write_config_byte(dev, portFIFO, (cd_dma_fifo & 0x0F) | 0x50);
		} else {
			pci_write_config_byte(dev, portFIFO, (cd_dma_fifo & 0xF0) | 0x05);
		}
	} else {
		if (hwif->index) {
			pci_write_config_byte(dev, portFIFO, cd_dma_fifo & 0x0F);
		} else {
			pci_write_config_byte(dev, portFIFO, cd_dma_fifo & 0xF0);
		}
	}

	pci_write_config_byte(dev, port, s_clc);
	pci_write_config_byte(dev, port+drive->select.b.unit+2, (a_clc << 4) | r_clc);
	__restore_flags(flags);
}

static int ali15x3_tune_chipset(struct ata_device *drive, byte speed)
{
	struct ata_channel *hwif = drive->channel;
	struct pci_dev *dev	= hwif->pci_dev;
	byte unit		= (drive->select.b.unit & 0x01);
	byte tmpbyte		= 0x00;
	int m5229_udma		= hwif->unit ? 0x57 : 0x56;
	int err			= 0;

	if (speed < XFER_UDMA_0) {
		byte ultra_enable	= (unit) ? 0x7f : 0xf7;
		/*
		 * clear "ultra enable" bit
		 */
		pci_read_config_byte(dev, m5229_udma, &tmpbyte);
		tmpbyte &= ultra_enable;
		pci_write_config_byte(dev, m5229_udma, tmpbyte);
	}

	err = ide_config_drive_speed(drive, speed);

#ifdef CONFIG_BLK_DEV_IDEDMA
	if (speed >= XFER_SW_DMA_0) {
		unsigned long dma_base = hwif->dma_base;

		outb(inb(dma_base+2)|(1<<(5+unit)), dma_base+2);
	}

	if (speed >= XFER_UDMA_0) {
		pci_read_config_byte(dev, m5229_udma, &tmpbyte);
		tmpbyte &= (0x0f << ((1-unit) << 2));
		/*
		 * enable ultra dma and set timing
		 */
		tmpbyte |= ((0x08 | ((4-speed)&0x07)) << (unit << 2));
		pci_write_config_byte(dev, m5229_udma, tmpbyte);
		if (speed >= XFER_UDMA_3) {
			pci_read_config_byte(dev, 0x4b, &tmpbyte);
			tmpbyte |= 1;
			pci_write_config_byte(dev, 0x4b, tmpbyte);
		}
	}
#endif /* CONFIG_BLK_DEV_IDEDMA */

	drive->current_speed = speed;

	return (err);
}

static void config_chipset_for_pio(struct ata_device *drive)
{
	ali15x3_tune_drive(drive, 5);
}

#ifdef CONFIG_BLK_DEV_IDEDMA
static int config_chipset_for_dma(struct ata_device *drive, byte ultra33)
{
	struct hd_driveid *id	= drive->id;
	byte speed		= 0x00;
	byte ultra66		= eighty_ninty_three(drive);
	byte ultra100		= (m5229_revision>=0xc4) ? 1 : 0;
	int  rval;

	if ((id->dma_ultra & 0x0020) && (ultra100) && (ultra66) && (ultra33)) {
		speed = XFER_UDMA_5;
	} else if ((id->dma_ultra & 0x0010) && (ultra66) && (ultra33)) {
		speed = XFER_UDMA_4;
	} else if ((id->dma_ultra & 0x0008) && (ultra66) && (ultra33)) {
		speed = XFER_UDMA_3;
	} else if ((id->dma_ultra & 0x0004) && (ultra33)) {
		speed = XFER_UDMA_2;
	} else if ((id->dma_ultra & 0x0002) && (ultra33)) {
		speed = XFER_UDMA_1;
	} else if ((id->dma_ultra & 0x0001) && (ultra33)) {
		speed = XFER_UDMA_0;
	} else if (id->dma_mword & 0x0004) {
		speed = XFER_MW_DMA_2;
	} else if (id->dma_mword & 0x0002) {
		speed = XFER_MW_DMA_1;
	} else if (id->dma_mword & 0x0001) {
		speed = XFER_MW_DMA_0;
	} else if (id->dma_1word & 0x0004) {
		speed = XFER_SW_DMA_2;
	} else if (id->dma_1word & 0x0002) {
		speed = XFER_SW_DMA_1;
	} else if (id->dma_1word & 0x0001) {
		speed = XFER_SW_DMA_0;
	} else {
		return 0;
	}

	(void) ali15x3_tune_chipset(drive, speed);

	if (!drive->init_speed)
		drive->init_speed = speed;

	rval = (int)(	((id->dma_ultra >> 11) & 3) ? 1:
			((id->dma_ultra >> 8) & 7) ? 1:
			((id->dma_mword >> 8) & 7) ? 1:
			((id->dma_1word >> 8) & 7) ? 1:
						     0);

	return rval;
}

static byte ali15x3_can_ultra(struct ata_device *drive)
{
#ifndef CONFIG_WDC_ALI15X3
	struct hd_driveid *id	= drive->id;
#endif /* CONFIG_WDC_ALI15X3 */

	if (m5229_revision <= 0x20) {
		return 0;
	} else if ((m5229_revision < 0xC2) &&
#ifndef CONFIG_WDC_ALI15X3
		   ((chip_is_1543c_e && strstr(id->model, "WDC ")) ||
		    (drive->type != ATA_DISK))) {
#else /* CONFIG_WDC_ALI15X3 */
		   (drive->type != ATA_DISK)) {
#endif /* CONFIG_WDC_ALI15X3 */
		return 0;
	} else {
		return 1;
	}
}

static int ali15x3_config_drive_for_dma(struct ata_device *drive)
{
	struct hd_driveid *id = drive->id;
	struct ata_channel *hwif = drive->channel;
	int on = 1;
	int verbose = 1;
	byte can_ultra_dma = ali15x3_can_ultra(drive);

	if ((m5229_revision<=0x20) && (drive->type != ATA_DISK)) {
		udma_enable(drive, 0, 0);
		return 0;
	}

	if ((id != NULL) && ((id->capability & 1) != 0) && hwif->autodma) {
		/* Consult the list of known "bad" drives */
		if (udma_black_list(drive)) {
			on = 0;
			goto fast_ata_pio;
		}
		on = 0;
		verbose = 0;
		if ((id->field_valid & 4) && (m5229_revision >= 0xC2)) {
			if (id->dma_ultra & 0x003F) {
				/* Force if Capable UltraDMA */
				on = config_chipset_for_dma(drive, can_ultra_dma);
				if ((id->field_valid & 2) &&
				    (!on))
					goto try_dma_modes;
			}
		} else if (id->field_valid & 2) {
try_dma_modes:
			if ((id->dma_mword & 0x0007) ||
			    (id->dma_1word & 0x0007)) {
				/* Force if Capable regular DMA modes */
				on = config_chipset_for_dma(drive, can_ultra_dma);
				if (!on)
					goto no_dma_set;
			}
		} else if (udma_white_list(drive)) {
			if (id->eide_dma_time > 150) {
				goto no_dma_set;
			}
			/* Consult the list of known "good" drives */
			on = config_chipset_for_dma(drive, can_ultra_dma);
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
		config_chipset_for_pio(drive);
	}

	udma_enable(drive, on, verbose);

	return 0;
}

static int ali15x3_udma_write(struct ata_device *drive, struct request *rq)
{
	if ((m5229_revision < 0xC2) && (drive->type != ATA_DISK))
		return 1;	/* try PIO instead of DMA */

	return ata_do_udma(0, drive, rq);
}

static int ali15x3_dmaproc(struct ata_device *drive)
{
	return ali15x3_config_drive_for_dma(drive);
}
#endif

static unsigned int __init ali15x3_init_chipset(struct pci_dev *dev)
{
	unsigned long fixdma_base = pci_resource_start(dev, 4);

	pci_read_config_byte(dev, PCI_REVISION_ID, &m5229_revision);

	isa_dev = pci_find_device(PCI_VENDOR_ID_AL, PCI_DEVICE_ID_AL_M1533, NULL);

	if (!fixdma_base) {
		/*
		 *
		 */
	} else {
		/*
		 * enable DMA capable bit, and "not" simplex only
		 */
		outb(inb(fixdma_base+2) & 0x60, fixdma_base+2);

		if (inb(fixdma_base+2) & 0x80)
			printk("%s: simplex device: DMA will fail!!\n", dev->name);
	}

#if defined(DISPLAY_ALI_TIMINGS) && defined(CONFIG_PROC_FS)
	if (!ali_proc) {
		ali_proc = 1;
		bmide_dev = dev;
		ali_display_info = &ali_get_info;
	}
#endif  /* defined(DISPLAY_ALI_TIMINGS) && defined(CONFIG_PROC_FS) */

	return 0;
}

/*
 * This checks if the controller and the cable are capable
 * of UDMA66 transfers. It doesn't check the drives.
 * But see note 2 below!
 */
static unsigned int __init ali15x3_ata66_check(struct ata_channel *hwif)
{
	struct pci_dev *dev	= hwif->pci_dev;
	unsigned int ata66	= 0;
	byte cable_80_pin[2]	= { 0, 0 };

	unsigned long flags;
	byte tmpbyte;

	__save_flags(flags);
	__cli();

	if (m5229_revision >= 0xC2) {
		/*
		 * 1543C-B?, 1535, 1535D, 1553
		 * Note 1: not all "motherboard" support this detection
		 * Note 2: if no udma 66 device, the detection may "error".
		 *         but in this case, we will not set the device to
		 *         ultra 66, the detection result is not important
		 */

		/*
		 * enable "Cable Detection", m5229, 0x4b, bit3
		 */
		pci_read_config_byte(dev, 0x4b, &tmpbyte);
		pci_write_config_byte(dev, 0x4b, tmpbyte | 0x08);

		/*
		 * set south-bridge's enable bit, m1533, 0x79
		 */
		pci_read_config_byte(isa_dev, 0x79, &tmpbyte);
		if (m5229_revision == 0xC2) {
			/*
			 * 1543C-B0 (m1533, 0x79, bit 2)
			 */
			pci_write_config_byte(isa_dev, 0x79, tmpbyte | 0x04);
		} else if (m5229_revision >= 0xC3) {
			/*
			 * 1553/1535 (m1533, 0x79, bit 1)
			 */
			pci_write_config_byte(isa_dev, 0x79, tmpbyte | 0x02);
		}
		/*
		 * Ultra66 cable detection (from Host View)
		 * m5229, 0x4a, bit0: primary, bit1: secondary 80 pin
		 */
		pci_read_config_byte(dev, 0x4a, &tmpbyte);
		/*
		 * 0x4a, bit0 is 0 => primary channel
		 * has 80-pin (from host view)
		 */
		if (!(tmpbyte & 0x01)) cable_80_pin[0] = 1;
		/*
		 * 0x4a, bit1 is 0 => secondary channel
		 * has 80-pin (from host view)
		 */
		if (!(tmpbyte & 0x02)) cable_80_pin[1] = 1;
		/*
		 * Allow ata66 if cable of current channel has 80 pins
		 */
		ata66 = (hwif->unit)?cable_80_pin[1]:cable_80_pin[0];
	} else {
		/*
		 * revision 0x20 (1543-E, 1543-F)
		 * revision 0xC0, 0xC1 (1543C-C, 1543C-D, 1543C-E)
		 * clear CD-ROM DMA write bit, m5229, 0x4b, bit 7
		 */
		pci_read_config_byte(dev, 0x4b, &tmpbyte);
		/*
		 * clear bit 7
		 */
		pci_write_config_byte(dev, 0x4b, tmpbyte & 0x7F);
		/*
		 * check m1533, 0x5e, bit 1~4 == 1001 => & 00011110 = 00010010
		 */
		pci_read_config_byte(isa_dev, 0x5e, &tmpbyte);
		chip_is_1543c_e = ((tmpbyte & 0x1e) == 0x12) ? 1: 0;
	}

	/*
	 * CD_ROM DMA on (m5229, 0x53, bit0)
	 *      Enable this bit even if we want to use PIO
	 * PIO FIFO off (m5229, 0x53, bit1)
	 *      The hardware will use 0x54h and 0x55h to control PIO FIFO
	 */
	pci_read_config_byte(dev, 0x53, &tmpbyte);
	tmpbyte = (tmpbyte & (~0x02)) | 0x01;

	pci_write_config_byte(dev, 0x53, tmpbyte);

	__restore_flags(flags);

	return(ata66);
}

static void __init ali15x3_init_channel(struct ata_channel *hwif)
{
#ifndef CONFIG_SPARC64
	byte ideic, inmir;
	byte irq_routing_table[] = { -1,  9, 3, 10, 4,  5, 7,  6,
				      1, 11, 0, 12, 0, 14, 0, 15 };

	hwif->irq = hwif->unit ? 15 : 14;

	if (isa_dev) {
		/*
		 * read IDE interface control
		 */
		pci_read_config_byte(isa_dev, 0x58, &ideic);

		/* bit0, bit1 */
		ideic = ideic & 0x03;

		/* get IRQ for IDE Controller */
		if ((hwif->unit && ideic == 0x03) || (!hwif->unit && !ideic)) {
			/*
			 * get SIRQ1 routing table
			 */
			pci_read_config_byte(isa_dev, 0x44, &inmir);
			inmir = inmir & 0x0f;
			hwif->irq = irq_routing_table[inmir];
		} else if (hwif->unit && !(ideic & 0x01)) {
			/*
			 * get SIRQ2 routing table
			 */
			pci_read_config_byte(isa_dev, 0x75, &inmir);
			inmir = inmir & 0x0f;
			hwif->irq = irq_routing_table[inmir];
		}
	}
#endif /* CONFIG_SPARC64 */

	hwif->tuneproc = &ali15x3_tune_drive;
	hwif->drives[0].autotune = 1;
	hwif->drives[1].autotune = 1;
	hwif->speedproc = ali15x3_tune_chipset;

#ifdef CONFIG_BLK_DEV_IDEDMA
	if ((hwif->dma_base) && (m5229_revision >= 0x20)) {
		/*
		 * M1543C or newer for DMAing
		 */
		hwif->udma_write = ali15x3_udma_write;
		hwif->XXX_udma = ali15x3_dmaproc;
		hwif->autodma = 1;
	}

	if (noautodma)
		hwif->autodma = 0;
#else
	hwif->autodma = 0;
#endif
}

static void __init ali15x3_init_dma(struct ata_channel *ch, unsigned long dmabase)
{
	if ((dmabase) && (m5229_revision < 0x20))
		return;

	ata_init_dma(ch, dmabase);
}


/* module data table */
static struct ata_pci_device chipset __initdata = {
	vendor: PCI_VENDOR_ID_AL,
        device: PCI_DEVICE_ID_AL_M5229,
	init_chipset: ali15x3_init_chipset,
	ata66_check: ali15x3_ata66_check,
	init_channel: ali15x3_init_channel,
	init_dma: ali15x3_init_dma,
	enablebits: { {0x00,0x00,0x00}, {0x00,0x00,0x00} },
	bootable: ON_BOARD
};

int __init init_ali15x3(void)
{
	ata_register_chipset(&chipset);

        return 0;
}
