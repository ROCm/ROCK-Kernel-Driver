/*
 * linux/drivers/block/ide-features.c	Version 0.04	June 9, 2000
 *
 *  Copyright (C) 1999-2000	Linus Torvalds & authors (see below)
 *
 *  Copyright (C) 1999-2000	Andre Hedrick <andre@linux-ide.org>
 *
 *  Extracts if ide.c to address the evolving transfer rate code for
 *  the SETFEATURES_XFER callouts.  Various parts of any given function
 *  are credited to previous ATA-IDE maintainers.
 *
 *  Auto-CRC downgrade for Ultra DMA(ing)
 *
 *  May be copied or modified under the terms of the GNU General Public License
 */

#include <linux/config.h>
#define __NO_VERSION__
#include <linux/module.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/major.h>
#include <linux/errno.h>
#include <linux/genhd.h>
#include <linux/blkpg.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/hdreg.h>
#include <linux/ide.h>

#include <asm/byteorder.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/bitops.h>

/*
 * A Verbose noise maker for debugging on the attempted transfer rates.
 */
char *ide_xfer_verbose (byte xfer_rate)
{
	static struct ide_xfer_par {
		byte rate;
		char *name;
	} xfer_verbose[] = {
		{ XFER_UDMA_7,		"UDMA 7" },
		{ XFER_UDMA_6,		"UDMA 6" },
		{ XFER_UDMA_5,		"UDMA 5" },
		{ XFER_UDMA_4,		"UDMA 4" },
		{ XFER_UDMA_3,		"UDMA 3" },
		{ XFER_UDMA_2,		"UDMA 2" },
		{ XFER_UDMA_1,		"UDMA 1" },
		{ XFER_UDMA_0,		"UDMA 0" },
		{ XFER_MW_DMA_2,	"MW DMA 2" },
		{ XFER_MW_DMA_1,	"MW DMA 1" },
		{ XFER_MW_DMA_0,	"MW DMA 0" },
		{ XFER_SW_DMA_2,	"SW DMA 2" },
		{ XFER_SW_DMA_1,	"SW DMA 1" },
		{ XFER_SW_DMA_0,	"SW DMA 0" },
		{ XFER_PIO_4,		"PIO 4" },
		{ XFER_PIO_3,		"PIO 3" },
		{ XFER_PIO_2,		"PIO 2" },
		{ XFER_PIO_1,		"PIO 1" },
		{ XFER_PIO_0,		"PIO 0" },
		{ XFER_PIO_SLOW,	"PIO SLOW" },
	};

	int i = 0;

	for (; i < ARRAY_SIZE(xfer_verbose); i++)
		if (xfer_verbose[i].rate == xfer_rate)
			return xfer_verbose[i].name;
	return "XFER ERROR";
}

/*
 * hd_driveid data come as little endian,
 * they need to be converted on big endian machines
 */
void ide_fix_driveid(struct hd_driveid *id)
{
#ifndef __LITTLE_ENDIAN
# ifdef __BIG_ENDIAN
	int i;
	unsigned short *stringcast;

	id->config         = __le16_to_cpu(id->config);
	id->cyls           = __le16_to_cpu(id->cyls);
	id->reserved2      = __le16_to_cpu(id->reserved2);
	id->heads          = __le16_to_cpu(id->heads);
	id->track_bytes    = __le16_to_cpu(id->track_bytes);
	id->sector_bytes   = __le16_to_cpu(id->sector_bytes);
	id->sectors        = __le16_to_cpu(id->sectors);
	id->vendor0        = __le16_to_cpu(id->vendor0);
	id->vendor1        = __le16_to_cpu(id->vendor1);
	id->vendor2        = __le16_to_cpu(id->vendor2);
	stringcast = (unsigned short *)&id->serial_no[0];
	for (i = 0; i < (20/2); i++)
		stringcast[i] = __le16_to_cpu(stringcast[i]);
	id->buf_type       = __le16_to_cpu(id->buf_type);
	id->buf_size       = __le16_to_cpu(id->buf_size);
	id->ecc_bytes      = __le16_to_cpu(id->ecc_bytes);
	stringcast = (unsigned short *)&id->fw_rev[0];
	for (i = 0; i < (8/2); i++)
		stringcast[i] = __le16_to_cpu(stringcast[i]);
	stringcast = (unsigned short *)&id->model[0];
	for (i = 0; i < (40/2); i++)
		stringcast[i] = __le16_to_cpu(stringcast[i]);
	id->dword_io       = __le16_to_cpu(id->dword_io);
	id->reserved50     = __le16_to_cpu(id->reserved50);
	id->field_valid    = __le16_to_cpu(id->field_valid);
	id->cur_cyls       = __le16_to_cpu(id->cur_cyls);
	id->cur_heads      = __le16_to_cpu(id->cur_heads);
	id->cur_sectors    = __le16_to_cpu(id->cur_sectors);
	id->cur_capacity0  = __le16_to_cpu(id->cur_capacity0);
	id->cur_capacity1  = __le16_to_cpu(id->cur_capacity1);
	id->lba_capacity   = __le32_to_cpu(id->lba_capacity);
	id->dma_1word      = __le16_to_cpu(id->dma_1word);
	id->dma_mword      = __le16_to_cpu(id->dma_mword);
	id->eide_pio_modes = __le16_to_cpu(id->eide_pio_modes);
	id->eide_dma_min   = __le16_to_cpu(id->eide_dma_min);
	id->eide_dma_time  = __le16_to_cpu(id->eide_dma_time);
	id->eide_pio       = __le16_to_cpu(id->eide_pio);
	id->eide_pio_iordy = __le16_to_cpu(id->eide_pio_iordy);
	for (i = 0; i < 2; i++)
		id->words69_70[i] = __le16_to_cpu(id->words69_70[i]);
	for (i = 0; i < 4; i++)
		id->words71_74[i] = __le16_to_cpu(id->words71_74[i]);
	id->queue_depth	   = __le16_to_cpu(id->queue_depth);
	for (i = 0; i < 4; i++)
		id->words76_79[i] = __le16_to_cpu(id->words76_79[i]);
	id->major_rev_num  = __le16_to_cpu(id->major_rev_num);
	id->minor_rev_num  = __le16_to_cpu(id->minor_rev_num);
	id->command_set_1  = __le16_to_cpu(id->command_set_1);
	id->command_set_2  = __le16_to_cpu(id->command_set_2);
	id->cfsse          = __le16_to_cpu(id->cfsse);
	id->cfs_enable_1   = __le16_to_cpu(id->cfs_enable_1);
	id->cfs_enable_2   = __le16_to_cpu(id->cfs_enable_2);
	id->csf_default    = __le16_to_cpu(id->csf_default);
	id->dma_ultra      = __le16_to_cpu(id->dma_ultra);
	id->word89         = __le16_to_cpu(id->word89);
	id->word90         = __le16_to_cpu(id->word90);
	id->CurAPMvalues   = __le16_to_cpu(id->CurAPMvalues);
	id->word92         = __le16_to_cpu(id->word92);
	id->hw_config      = __le16_to_cpu(id->hw_config);
	id->acoustic       = __le16_to_cpu(id->acoustic);
	for (i = 0; i < 5; i++)
		id->words95_99[i]  = __le16_to_cpu(id->words95_99[i]);
	id->lba_capacity_2 = __le64_to_cpu(id->lba_capacity_2);
	for (i = 0; i < 22; i++)
		id->words104_125[i]   = __le16_to_cpu(id->words104_125[i]);
	id->last_lun       = __le16_to_cpu(id->last_lun);
	id->word127        = __le16_to_cpu(id->word127);
	id->dlf            = __le16_to_cpu(id->dlf);
	id->csfo           = __le16_to_cpu(id->csfo);
	for (i = 0; i < 26; i++)
		id->words130_155[i] = __le16_to_cpu(id->words130_155[i]);
	id->word156        = __le16_to_cpu(id->word156);
	for (i = 0; i < 3; i++)
		id->words157_159[i] = __le16_to_cpu(id->words157_159[i]);
	id->cfa_power      = __le16_to_cpu(id->cfa_power);
	for (i = 0; i < 14; i++)
		id->words161_175[i] = __le16_to_cpu(id->words161_175[i]);
	for (i = 0; i < 31; i++)
		id->words176_205[i] = __le16_to_cpu(id->words176_205[i]);
	for (i = 0; i < 48; i++)
		id->words206_254[i] = __le16_to_cpu(id->words206_254[i]);
	id->integrity_word  = __le16_to_cpu(id->integrity_word);
# else
#  error "Please fix <asm/byteorder.h>"
# endif
#endif
}

int ide_driveid_update(struct ata_device *drive)
{
	/*
	 * Re-read drive->id for possible DMA mode
	 * change (copied from ide-probe.c)
	 */
	struct hd_driveid *id;
	unsigned long timeout, flags;

	SELECT_MASK(drive->channel, drive, 1);
	if (IDE_CONTROL_REG)
		OUT_BYTE(drive->ctl,IDE_CONTROL_REG);
	ide_delay_50ms();
	OUT_BYTE(WIN_IDENTIFY, IDE_COMMAND_REG);
	timeout = jiffies + WAIT_WORSTCASE;
	do {
		if (0 < (signed long)(jiffies - timeout)) {
			SELECT_MASK(drive->channel, drive, 0);
			return 0;	/* drive timed-out */
		}
		ide_delay_50ms();	/* give drive a breather */
	} while (IN_BYTE(IDE_ALTSTATUS_REG) & BUSY_STAT);
	ide_delay_50ms();	/* wait for IRQ and DRQ_STAT */
	if (!OK_STAT(GET_STAT(),DRQ_STAT,BAD_R_STAT)) {
		SELECT_MASK(drive->channel, drive, 0);
		printk("%s: CHECK for good STATUS\n", drive->name);
		return 0;
	}
	__save_flags(flags);	/* local CPU only */
	__cli();		/* local CPU only; some systems need this */
	SELECT_MASK(drive->channel, drive, 0);
	id = kmalloc(SECTOR_WORDS*4, GFP_ATOMIC);
	if (!id) {
		__restore_flags(flags);	/* local CPU only */
		return 0;
	}
	ata_read(drive, id, SECTOR_WORDS);
	GET_STAT();		/* clear drive IRQ */
	ide__sti();		/* local CPU only */
	__restore_flags(flags);	/* local CPU only */
	ide_fix_driveid(id);
	if (id) {
		drive->id->dma_ultra = id->dma_ultra;
		drive->id->dma_mword = id->dma_mword;
		drive->id->dma_1word = id->dma_1word;
		/* anything more ? */
		kfree(id);
	}

	return 1;
}

/*
 *  All hosts that use the 80c ribbon must use this!
 */
byte eighty_ninty_three(struct ata_device *drive)
{
	return ((byte) ((drive->channel->udma_four) &&
#ifndef CONFIG_IDEDMA_IVB
			(drive->id->hw_config & 0x4000) &&
#endif
			(drive->id->hw_config & 0x6000)) ? 1 : 0);
}

/*
 * Similar to ide_wait_stat(), except it never calls ide_error internally.
 * This is a kludge to handle the new ide_config_drive_speed() function,
 * and should not otherwise be used anywhere.  Eventually, the tuneproc's
 * should be updated to return ide_startstop_t, in which case we can get
 * rid of this abomination again.  :)   -ml
 *
 * It is gone..........
 *
 * const char *msg == consider adding for verbose errors.
 */
int ide_config_drive_speed(struct ata_device *drive, byte speed)
{
	struct ata_channel *hwif = drive->channel;
	int i;
	int error = 1;
	byte stat;

#if defined(CONFIG_BLK_DEV_IDEDMA) && !defined(CONFIG_DMA_NONPCI)
	byte unit = (drive->select.b.unit & 0x01);
	outb(inb(hwif->dma_base+2) & ~(1<<(5+unit)), hwif->dma_base+2);
#endif /* (CONFIG_BLK_DEV_IDEDMA) && !(CONFIG_DMA_NONPCI) */

	/*
	 * Don't use ide_wait_cmd here - it will
	 * attempt to set_geometry and recalibrate,
	 * but for some reason these don't work at
	 * this point (lost interrupt).
	 */
        /*
         * Select the drive, and issue the SETFEATURES command
         */
	disable_irq(hwif->irq);	/* disable_irq_nosync ?? */
	udelay(1);
	SELECT_DRIVE(drive->channel, drive);
	SELECT_MASK(drive->channel, drive, 0);
	udelay(1);
	if (IDE_CONTROL_REG)
		OUT_BYTE(drive->ctl | 2, IDE_CONTROL_REG);
	OUT_BYTE(speed, IDE_NSECTOR_REG);
	OUT_BYTE(SETFEATURES_XFER, IDE_FEATURE_REG);
	OUT_BYTE(WIN_SETFEATURES, IDE_COMMAND_REG);
	if ((IDE_CONTROL_REG) && (drive->quirk_list == 2))
		OUT_BYTE(drive->ctl, IDE_CONTROL_REG);
	udelay(1);
	/*
	 * Wait for drive to become non-BUSY
	 */
	if ((stat = GET_STAT()) & BUSY_STAT) {
		unsigned long flags, timeout;
		__save_flags(flags);	/* local CPU only */
		ide__sti();		/* local CPU only -- for jiffies */
		timeout = jiffies + WAIT_CMD;
		while ((stat = GET_STAT()) & BUSY_STAT) {
			if (0 < (signed long)(jiffies - timeout))
				break;
		}
		__restore_flags(flags); /* local CPU only */
	}

	/*
	 * Allow status to settle, then read it again.
	 * A few rare drives vastly violate the 400ns spec here,
	 * so we'll wait up to 10usec for a "good" status
	 * rather than expensively fail things immediately.
	 * This fix courtesy of Matthew Faupel & Niccolo Rigacci.
	 */
	for (i = 0; i < 10; i++) {
		udelay(1);
		if (OK_STAT((stat = GET_STAT()), DRIVE_READY, BUSY_STAT|DRQ_STAT|ERR_STAT)) {
			error = 0;
			break;
		}
	}

	SELECT_MASK(drive->channel, drive, 0);

	enable_irq(hwif->irq);

	if (error) {
		ide_dump_status(drive, NULL, "set_drive_speed_status", stat);
		return error;
	}

	drive->id->dma_ultra &= ~0xFF00;
	drive->id->dma_mword &= ~0x0F00;
	drive->id->dma_1word &= ~0x0F00;

#if defined(CONFIG_BLK_DEV_IDEDMA) && !defined(CONFIG_DMA_NONPCI)
	if (speed > XFER_PIO_4) {
		outb(inb(hwif->dma_base+2)|(1<<(5+unit)), hwif->dma_base+2);
	} else {
		outb(inb(hwif->dma_base+2) & ~(1<<(5+unit)), hwif->dma_base+2);
	}
#endif

	switch(speed) {
		case XFER_UDMA_7:   drive->id->dma_ultra |= 0x8080; break;
		case XFER_UDMA_6:   drive->id->dma_ultra |= 0x4040; break;
		case XFER_UDMA_5:   drive->id->dma_ultra |= 0x2020; break;
		case XFER_UDMA_4:   drive->id->dma_ultra |= 0x1010; break;
		case XFER_UDMA_3:   drive->id->dma_ultra |= 0x0808; break;
		case XFER_UDMA_2:   drive->id->dma_ultra |= 0x0404; break;
		case XFER_UDMA_1:   drive->id->dma_ultra |= 0x0202; break;
		case XFER_UDMA_0:   drive->id->dma_ultra |= 0x0101; break;
		case XFER_MW_DMA_2: drive->id->dma_mword |= 0x0404; break;
		case XFER_MW_DMA_1: drive->id->dma_mword |= 0x0202; break;
		case XFER_MW_DMA_0: drive->id->dma_mword |= 0x0101; break;
		case XFER_SW_DMA_2: drive->id->dma_1word |= 0x0404; break;
		case XFER_SW_DMA_1: drive->id->dma_1word |= 0x0202; break;
		case XFER_SW_DMA_0: drive->id->dma_1word |= 0x0101; break;
		default: break;
	}
	return error;
}

EXPORT_SYMBOL(ide_fix_driveid);
EXPORT_SYMBOL(ide_driveid_update);
EXPORT_SYMBOL(eighty_ninty_three);
EXPORT_SYMBOL(ide_config_drive_speed);
