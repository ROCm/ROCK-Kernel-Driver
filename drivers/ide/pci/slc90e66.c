/*
 *  linux/drivers/ide/slc90e66.c	Version 0.10	October 4, 2000
 *
 *  Copyright (C) 2000 Andre Hedrick <andre@linux-ide.org>
 *  May be copied or modified under the terms of the GNU General Public License
 *
 * 00:07.1 IDE interface: EFAR Microsystems:
 * 		Unknown device 9130 (prog-if 8a [Master SecP PriP])
 * Control: I/O+ Mem- BusMaster+ SpecCycle- MemWINV-
 * 		VGASnoop- ParErr- Stepping- SERR- FastB2B-
 * Status: Cap- 66Mhz- UDF- FastB2B- ParErr- DEVSEL=medium
 * 		>TAbort- <TAbort- <MAbort- >SERR- <PERR-
 * Latency: 64
 * Interrupt: pin A routed to IRQ 255
 * Region 4: I/O ports at 1050
 *
 * 00: 55 10 30 91 05 00 00 02 00 8a 01 01 00 40 00 00
 * 10: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * 20: 51 10 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * 30: 00 00 00 00 00 00 00 00 00 00 00 00 ff 01 00 00
 * 40: 37 e3 33 e3 b9 55 01 00 0d 00 04 22 00 00 00 00
 * 50: 00 00 ff a0 00 00 00 08 40 00 00 00 00 00 00 00
 * 60: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * 70: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * 80: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * 90: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * a0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * b0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * c0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * d0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * e0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * f0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 *
 * This a look-a-like variation of the ICH0 PIIX4 Ultra-66,
 * but this keeps the ISA-Bridge and slots alive.
 *
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/hdreg.h>
#include <linux/ide.h>
#include <linux/delay.h>
#include <linux/init.h>

#include <asm/io.h>

#include "ide_modes.h"

#define SLC90E66_DEBUG_DRIVE_INFO		0

#define DISPLAY_SLC90E66_TIMINGS

#if defined(DISPLAY_SLC90E66_TIMINGS) && defined(CONFIG_PROC_FS)
#include <linux/stat.h>
#include <linux/proc_fs.h>

static int slc90e66_get_info(char *, char **, off_t, int);
extern int (*slc90e66_display_info)(char *, char **, off_t, int); /* ide-proc.c */
static struct pci_dev *bmide_dev;

static int slc90e66_get_info (char *buffer, char **addr, off_t offset, int count)
{
	char *p = buffer;
	u32 bibma = pci_resource_start(bmide_dev, 4);
        u16 reg40 = 0, psitre = 0, reg42 = 0, ssitre = 0;
	u8  c0 = 0, c1 = 0;
	u8  reg44 = 0, reg47 = 0, reg48 = 0, reg4a = 0, reg4b = 0;

	pci_read_config_word(bmide_dev, 0x40, &reg40);
	pci_read_config_word(bmide_dev, 0x42, &reg42);
	pci_read_config_byte(bmide_dev, 0x44, &reg44);
	pci_read_config_byte(bmide_dev, 0x47, &reg47);
	pci_read_config_byte(bmide_dev, 0x48, &reg48);
	pci_read_config_byte(bmide_dev, 0x4a, &reg4a);
	pci_read_config_byte(bmide_dev, 0x4b, &reg4b);

	psitre = (reg40 & 0x4000) ? 1 : 0;
	ssitre = (reg42 & 0x4000) ? 1 : 0;

        /*
         * at that point bibma+0x2 et bibma+0xa are byte registers
         * to investigate:
         */
#ifdef __mips__	/* only for mips? */
	c0 = inb_p(bibma + 0x02);
	c1 = inb_p(bibma + 0x0a);
#else
	c0 = inb_p((unsigned short)bibma + 0x02);
	c1 = inb_p((unsigned short)bibma + 0x0a);
#endif

	p += sprintf(p, "                                SLC90E66 Chipset.\n");
	p += sprintf(p, "--------------- Primary Channel "
			"---------------- Secondary Channel "
			"-------------\n");
	p += sprintf(p, "                %sabled "
			"                        %sabled\n",
			(c0&0x80) ? "dis" : " en",
			(c1&0x80) ? "dis" : " en");
	p += sprintf(p, "--------------- drive0 --------- drive1 "
			"-------- drive0 ---------- drive1 ------\n");
	p += sprintf(p, "DMA enabled:    %s              %s "
			"            %s               %s\n",
			(c0&0x20) ? "yes" : "no ",
			(c0&0x40) ? "yes" : "no ",
			(c1&0x20) ? "yes" : "no ",
			(c1&0x40) ? "yes" : "no " );
	p += sprintf(p, "UDMA enabled:   %s              %s "
			"            %s               %s\n",
			(reg48&0x01) ? "yes" : "no ",
			(reg48&0x02) ? "yes" : "no ",
			(reg48&0x04) ? "yes" : "no ",
			(reg48&0x08) ? "yes" : "no " );
	p += sprintf(p, "UDMA enabled:   %s                %s "
			"              %s                 %s\n",
			((reg4a&0x04)==0x04) ? "4" :
			((reg4a&0x03)==0x03) ? "3" :
			(reg4a&0x02) ? "2" :
			(reg4a&0x01) ? "1" :
			(reg4a&0x00) ? "0" : "X",
			((reg4a&0x40)==0x40) ? "4" :
			((reg4a&0x30)==0x30) ? "3" :
			(reg4a&0x20) ? "2" :
			(reg4a&0x10) ? "1" :
			(reg4a&0x00) ? "0" : "X",
			((reg4b&0x04)==0x04) ? "4" :
			((reg4b&0x03)==0x03) ? "3" :
			(reg4b&0x02) ? "2" :
			(reg4b&0x01) ? "1" :
			(reg4b&0x00) ? "0" : "X",
			((reg4b&0x40)==0x40) ? "4" :
			((reg4b&0x30)==0x30) ? "3" :
			(reg4b&0x20) ? "2" :
			(reg4b&0x10) ? "1" :
			(reg4b&0x00) ? "0" : "X");

	p += sprintf(p, "UDMA\n");
	p += sprintf(p, "DMA\n");
	p += sprintf(p, "PIO\n");

/*
 *	FIXME.... Add configuration junk data....blah blah......
 */

	return p-buffer;	 /* => must be less than 4k! */
}
#endif  /* defined(DISPLAY_SLC90E66_TIMINGS) && defined(CONFIG_PROC_FS) */

/*
 *  Used to set Fifo configuration via kernel command line:
 */

byte slc90e66_proc = 0;

static byte slc90e66_ratemask (ide_drive_t *drive)
{
	byte mode	= 0x00;

	mode |= 0x02;

	if (!eighty_ninty_three(drive)) {
		mode &= ~0xFE;
		mode |= 0x01;
	}
	return (mode &= ~0xF8);
}

static byte slc90e66_ratefilter (ide_drive_t *drive, byte speed)
{
#ifdef CONFIG_BLK_DEV_IDEDMA
	byte mode = slc90e66_ratemask(drive);

	switch(mode) {
		case 0x04:	// while (speed > XFER_UDMA_6) speed--; break;
		case 0x03:	// while (speed > XFER_UDMA_5) speed--; break;
		case 0x02:	while (speed > XFER_UDMA_4) speed--; break;
		case 0x01:	while (speed > XFER_UDMA_2) speed--; break;
		case 0x00:
		default:	while (speed > XFER_MW_DMA_2) speed--; break;
			break;
	}
#else
	while (speed > XFER_PIO_4) speed--;
#endif /* CONFIG_BLK_DEV_IDEDMA */
//	printk("%s: mode == %02x speed == %02x\n", drive->name, mode, speed);
	return speed;
}

static byte slc90e66_dma_2_pio (byte xfer_rate) {
	switch(xfer_rate) {
		case XFER_UDMA_4:
		case XFER_UDMA_3:
		case XFER_UDMA_2:
		case XFER_UDMA_1:
		case XFER_UDMA_0:
		case XFER_MW_DMA_2:
		case XFER_PIO_4:
			return 4;
		case XFER_MW_DMA_1:
		case XFER_PIO_3:
			return 3;
		case XFER_SW_DMA_2:
		case XFER_PIO_2:
			return 2;
		case XFER_MW_DMA_0:
		case XFER_SW_DMA_1:
		case XFER_SW_DMA_0:
		case XFER_PIO_1:
		case XFER_PIO_0:
		case XFER_PIO_SLOW:
		default:
			return 0;
	}
}

/*
 *  Based on settings done by AMI BIOS
 *  (might be useful if drive is not registered in CMOS for any reason).
 */
static void slc90e66_tune_drive (ide_drive_t *drive, byte pio)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;
	int is_slave		= (&hwif->drives[1] == drive);
	int master_port		= hwif->channel ? 0x42 : 0x40;
	int slave_port		= 0x44;
	unsigned long flags;
	u16 master_data;
	byte slave_data;
				 /* ISP  RTC */
	byte timings[][2]	= { { 0, 0 },
				    { 0, 0 },
				    { 1, 0 },
				    { 2, 1 },
				    { 2, 3 }, };

	pio = ide_get_best_pio_mode(drive, pio, 5, NULL);
	spin_lock_irqsave(&ide_lock, flags);
	pci_read_config_word(dev, master_port, &master_data);
	if (is_slave) {
		master_data = master_data | 0x4000;
		if (pio > 1)
			/* enable PPE, IE and TIME */
			master_data = master_data | 0x0070;
		pci_read_config_byte(dev, slave_port, &slave_data);
		slave_data = slave_data & (hwif->channel ? 0x0f : 0xf0);
		slave_data = slave_data | (((timings[pio][0] << 2) | timings[pio][1]) << (hwif->channel ? 4 : 0));
	} else {
		master_data = master_data & 0xccf8;
		if (pio > 1)
			/* enable PPE, IE and TIME */
			master_data = master_data | 0x0007;
		master_data = master_data | (timings[pio][0] << 12) | (timings[pio][1] << 8);
	}
	pci_write_config_word(dev, master_port, master_data);
	if (is_slave)
		pci_write_config_byte(dev, slave_port, slave_data);
	spin_unlock_irqrestore(&ide_lock, flags);
}

static int slc90e66_tune_chipset (ide_drive_t *drive, byte xferspeed)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;
	byte maslave		= hwif->channel ? 0x42 : 0x40;
	byte speed		= slc90e66_ratefilter(drive, xferspeed);
	int a_speed		= 7 << (drive->dn * 4);
	int u_flag		= 1 << drive->dn;
	int u_speed		= 0;
	int			sitre;
	short			reg4042, reg44, reg48, reg4a;

	pci_read_config_word(dev, maslave, &reg4042);
	sitre = (reg4042 & 0x4000) ? 1 : 0;
	pci_read_config_word(dev, 0x44, &reg44);
	pci_read_config_word(dev, 0x48, &reg48);
	pci_read_config_word(dev, 0x4a, &reg4a);

	switch(speed) {
#ifdef CONFIG_BLK_DEV_IDEDMA
		case XFER_UDMA_4:	u_speed = 4 << (drive->dn * 4); break;
		case XFER_UDMA_3:	u_speed = 3 << (drive->dn * 4); break;
		case XFER_UDMA_2:	u_speed = 2 << (drive->dn * 4); break;
		case XFER_UDMA_1:	u_speed = 1 << (drive->dn * 4); break;
		case XFER_UDMA_0:	u_speed = 0 << (drive->dn * 4); break;
		case XFER_MW_DMA_2:
		case XFER_MW_DMA_1:
		case XFER_SW_DMA_2:	break;
#endif /* CONFIG_BLK_DEV_IDEDMA */
		case XFER_PIO_4:
		case XFER_PIO_3:
		case XFER_PIO_2:
		case XFER_PIO_0:        break;
		default:		return -1;
	}

	if (speed >= XFER_UDMA_0) {
		if (!(reg48 & u_flag))
			pci_write_config_word(dev, 0x48, reg48|u_flag);
		if ((reg4a & u_speed) != u_speed) {
			pci_write_config_word(dev, 0x4a, reg4a & ~a_speed);
			pci_read_config_word(dev, 0x4a, &reg4a);
			pci_write_config_word(dev, 0x4a, reg4a|u_speed);
		}
	} else {
		if (reg48 & u_flag)
			pci_write_config_word(dev, 0x48, reg48 & ~u_flag);
		if (reg4a & a_speed)
			pci_write_config_word(dev, 0x4a, reg4a & ~a_speed);
	}

	slc90e66_tune_drive(drive, slc90e66_dma_2_pio(speed));
	return (ide_config_drive_speed(drive, speed));
}

#ifdef CONFIG_BLK_DEV_IDEDMA
static int slc90e66_config_drive_for_dma (ide_drive_t *drive)
{
	struct hd_driveid *id	= drive->id;
	byte mode		= slc90e66_ratemask(drive);
	byte speed, tspeed, dma = 1;

	switch(mode) {
		case 0x02:
			if (id->dma_ultra & 0x0010)
				{ speed = XFER_UDMA_4; break; }
			if (id->dma_ultra & 0x0008)
				{ speed = XFER_UDMA_3; break; }
		case 0x01:
			if (id->dma_ultra & 0x0004)
				{ speed = XFER_UDMA_2; break; }
			if (id->dma_ultra & 0x0002)
				{ speed = XFER_UDMA_1; break; }
			if (id->dma_ultra & 0x0001)
				{ speed = XFER_UDMA_0; break; }
		case 0x00:
			if (id->dma_mword & 0x0004)
				{ speed = XFER_MW_DMA_2; break; }
			if (id->dma_mword & 0x0002)
				{ speed = XFER_MW_DMA_1; break; }
			if (id->dma_1word & 0x0004)
				{ speed = XFER_SW_DMA_2; break; }
		default:
			tspeed = ide_get_best_pio_mode(drive, 255, 5, NULL);
			speed = slc90e66_dma_2_pio(XFER_PIO_0 + tspeed);
			dma = 0;
			break;
	}

	(void) slc90e66_tune_chipset(drive, speed);
//	return ((int)   (dma) ? ide_dma_on : ide_dma_off_quietly);
	return ((int)	((id->dma_ultra >> 11) & 7) ? ide_dma_on :
			((id->dma_ultra >> 8) & 7) ? ide_dma_on :
			((id->dma_mword >> 8) & 7) ? ide_dma_on :
			((id->dma_1word >> 8) & 7) ? ide_dma_on :
						     ide_dma_off_quietly);
}

static int config_drive_xfer_rate (ide_drive_t *drive)
{
	struct hd_driveid *id = drive->id;
	ide_dma_action_t dma_func = ide_dma_on;

	drive->init_speed = 0;

	if (id && (id->capability & 1) && HWIF(drive)->autodma) {
		/* Consult the list of known "bad" drives */
		if (ide_dmaproc(ide_dma_bad_drive, drive)) {
			dma_func = ide_dma_off;
			goto fast_ata_pio;
		}
		dma_func = ide_dma_off_quietly;
		if (id->field_valid & 4) {
			if (id->dma_ultra & 0x007F) {
				/* Force if Capable UltraDMA */
				dma_func = slc90e66_config_drive_for_dma(drive);
				if ((id->field_valid & 2) &&
				    (dma_func != ide_dma_on))
					goto try_dma_modes;
			}
		} else if (id->field_valid & 2) {
try_dma_modes:
			if ((id->dma_mword & 0x0007) ||
			    (id->dma_1word & 0x007)) {
				/* Force if Capable regular DMA modes */
				dma_func = slc90e66_config_drive_for_dma(drive);
				if (dma_func != ide_dma_on)
					goto no_dma_set;
			}
		} else if (ide_dmaproc(ide_dma_good_drive, drive)) {
			if (id->eide_dma_time > 150) {
				goto no_dma_set;
			}
			/* Consult the list of known "good" drives */
			dma_func = slc90e66_config_drive_for_dma(drive);
			if (dma_func != ide_dma_on)
				goto no_dma_set;
		} else {
			goto fast_ata_pio;
		}
	} else if ((id->capability & 8) || (id->field_valid & 2)) {
fast_ata_pio:
		dma_func = ide_dma_off_quietly;
no_dma_set:
		slc90e66_tune_drive(drive, 5);
	}
	return HWIF(drive)->dmaproc(dma_func, drive);
}

static int slc90e66_dmaproc(ide_dma_action_t func, ide_drive_t *drive)
{
	switch (func) {
		case ide_dma_check:
			return config_drive_xfer_rate(drive);
		default :
			break;
	}
	/* Other cases are done by generic IDE-DMA code. */
	return ide_dmaproc(func, drive);
}
#endif /* CONFIG_BLK_DEV_IDEDMA */

unsigned int __init pci_init_slc90e66 (struct pci_dev *dev, const char *name)
{
#if defined(DISPLAY_SLC90E66_TIMINGS) && defined(CONFIG_PROC_FS)
	if (!slc90e66_proc) {
		slc90e66_proc = 1;
		bmide_dev = dev;
		slc90e66_display_info = &slc90e66_get_info;
	}
#endif /* DISPLAY_SLC90E66_TIMINGS && CONFIG_PROC_FS */
	return 0;
}

unsigned int __init ata66_slc90e66 (ide_hwif_t *hwif)
{
	byte reg47 = 0, ata66 = 0;
	byte mask = hwif->channel ? 0x01 : 0x02;	/* bit0:Primary */

	pci_read_config_byte(hwif->pci_dev, 0x47, &reg47);
	ata66 = (reg47 & mask) ? 0 : 1;	/* bit[0(1)]: 0:80, 1:40 */
	return ata66;
}

void __init ide_init_slc90e66 (ide_hwif_t *hwif)
{
	if (!hwif->irq)
		hwif->irq = hwif->channel ? 15 : 14;

	hwif->autodma = 0;
	hwif->speedproc = &slc90e66_tune_chipset;
	hwif->tuneproc = &slc90e66_tune_drive;
	hwif->drives[0].autotune = 1;
	hwif->drives[1].autotune = 1;

	if (!hwif->dma_base)
		return;

#ifdef CONFIG_BLK_DEV_IDEDMA 
	hwif->dmaproc = &slc90e66_dmaproc;
#ifdef CONFIG_IDEDMA_AUTO
	if (!noautodma)
		hwif->autodma = 1;
#endif /* CONFIG_IDEDMA_AUTO */
#endif /* !CONFIG_BLK_DEV_IDEDMA */
}
