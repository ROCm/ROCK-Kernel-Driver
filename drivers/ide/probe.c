/**** vi:set ts=8 sts=8 sw=8:************************************************
 *
 *  Copyright (C) 1994-1998  Linus Torvalds & authors (see below)
 *
 *  Mostly written by Mark Lord <mlord@pobox.com>
 *                and Gadi Oxman <gadio@netvision.net.il>
 *                and Andre Hedrick <andre@linux-ide.org>
 *
 *  See linux/MAINTAINERS for address of current maintainer.
 *
 */

/*
 * This is roughly the code related to device detection and
 * device id handling.
 */

#include <linux/config.h>
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
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/pci.h>
#include <linux/hdreg.h>
#include <linux/ide.h>

#include <asm/byteorder.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/io.h>

extern struct ata_device * get_info_ptr(kdev_t);

/*
 * This is called from the partition-table code in pt/msdos.c.
 *
 * It has two tasks:
 *
 * (I) to handle Ontrack DiskManager by offsetting everything by 63 sectors,
 *  or to handle EZdrive by remapping sector 0 to sector 1.
 *
 * (II) to invent a translated geometry.
 *
 * Part (I) is suppressed if the user specifies the "noremap" option
 * on the command line.
 *
 * Part (II) is suppressed if the user specifies an explicit geometry.
 *
 * The ptheads parameter is either 0 or tells about the number of
 * heads shown by the end of the first nonempty partition.
 * If this is either 16, 32, 64, 128, 240 or 255 we'll believe it.
 *
 * The xparm parameter has the following meaning:
 *	 0 = convert to CHS with fewer than 1024 cyls
 *	     using the same method as Ontrack DiskManager.
 *	 1 = same as "0", plus offset everything by 63 sectors.
 *	-1 = similar to "0", plus redirect sector 0 to sector 1.
 *	 2 = convert to a CHS geometry with "ptheads" heads.
 *
 * Returns 0 if the translation was not possible, if the device was not
 * an IDE disk drive, or if a geometry was "forced" on the commandline.
 * Returns 1 if the geometry translation was successful.
 */
int ide_xlate_1024(kdev_t i_rdev, int xparm, int ptheads, const char *msg)
{
	struct ata_device *drive;
	const char *msg1 = "";
	int heads = 0;
	int c, h, s;
	int transl = 1;		/* try translation */
	int ret = 0;

	drive = get_info_ptr(i_rdev);
	if (!drive)
		return 0;

	/* remap? */
	if (drive->remap_0_to_1 != 2) {
		if (xparm == 1) {		/* DM */
			drive->sect0 = 63;
			msg1 = " [remap +63]";
			ret = 1;
		} else if (xparm == -1) {	/* EZ-Drive */
			if (drive->remap_0_to_1 == 0) {
				drive->remap_0_to_1 = 1;
				msg1 = " [remap 0->1]";
				ret = 1;
			}
		}
	}

	/* There used to be code here that assigned drive->id->CHS to
	 * drive->CHS and that to drive->bios_CHS. However, some disks have
	 * id->C/H/S = 4092/16/63 but are larger than 2.1 GB.  In such cases
	 * that code was wrong.  Moreover, there seems to be no reason to do
	 * any of these things.
	 *
	 * Please note that recent RedHat changes to the disk utils are bogous
	 * and will report spurious errors.
	 */

	/* translate? */
	if (drive->forced_geom)
		transl = 0;

	/* does ptheads look reasonable? */
	if (ptheads == 32 || ptheads == 64 || ptheads == 128 ||
	    ptheads == 240 || ptheads == 255)
		heads = ptheads;

	if (xparm == 2) {
		if (!heads ||
		   (drive->bios_head >= heads && drive->bios_sect == 63))
			transl = 0;
	}
	if (xparm == -1) {
		if (drive->bios_head > 16)
			transl = 0;     /* we already have a translation */
	}

	if (transl) {
		static const u8 dm_head_vals[] = {4, 8, 16, 32, 64, 128, 255, 0};
		const u8 *headp = dm_head_vals;
		unsigned long total;

		/*
		 * If heads is nonzero: find a translation with this many heads
		 * and S=63.  Otherwise: find out how OnTrack Disk Manager
		 * would translate the disk.
		 *
		 * The specs say: take geometry as obtained from Identify,
		 * compute total capacity C*H*S from that, and truncate to
		 * 1024*255*63. Now take S=63, H the first in the sequence 4,
		 * 8, 16, 32, 64, 128, 255 such that 63*H*1024 >= total.
		 * [Please tell aeb@cwi.nl in case this computes a geometry
		 * different from what OnTrack uses.]
		 */

		total = ata_capacity(drive);

		s = 63;

		if (heads) {
			h = heads;
			c = total / (63 * heads);
		} else {
			while (63 * headp[0] * 1024 < total && headp[1] != 0)
				headp++;
			h = headp[0];
			c = total / (63 * headp[0]);
		}

		drive->bios_cyl = c;
		drive->bios_head = h;
		drive->bios_sect = s;
		ret = 1;
	}

	drive->part[0].nr_sects = ata_capacity(drive);

	if (ret)
		printk("%s%s [%d/%d/%d]", msg, msg1,
		       drive->bios_cyl, drive->bios_head, drive->bios_sect);
	return ret;
}

/*
 * Drive ID data come as little endian, it needs to be converted on big endian
 * machines.
 */
void ata_fix_driveid(struct hd_driveid *id)
{
#ifndef __LITTLE_ENDIAN
# ifdef __BIG_ENDIAN
	int i;
	u16 *stringcast;

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
	stringcast = (u16 *)&id->serial_no[0];
	for (i = 0; i < (20/2); i++)
		stringcast[i] = __le16_to_cpu(stringcast[i]);
	id->buf_type       = __le16_to_cpu(id->buf_type);
	id->buf_size       = __le16_to_cpu(id->buf_size);
	id->ecc_bytes      = __le16_to_cpu(id->ecc_bytes);
	stringcast = (u16 *)&id->fw_rev[0];
	for (i = 0; i < (8/2); i++)
		stringcast[i] = __le16_to_cpu(stringcast[i]);
	stringcast = (u16 *)&id->model[0];
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
	for (i = 0; i < 2; ++i)
		id->words69_70[i] = __le16_to_cpu(id->words69_70[i]);
	for (i = 0; i < 4; ++i)
		id->words71_74[i] = __le16_to_cpu(id->words71_74[i]);
	id->queue_depth	   = __le16_to_cpu(id->queue_depth);
	for (i = 0; i < 4; ++i)
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

void ide_fixstring(char *s, const int bytecount, const int byteswap)
{
	char *p = s;
	char *end = &s[bytecount & ~1]; /* bytecount must be even */

	if (byteswap) {
		/* convert from big-endian to host byte order */
		for (p = end ; p != s;) {
			unsigned short *pp = (unsigned short *) (p -= 2);
			*pp = ntohs(*pp);
		}
	}

	/* strip leading blanks */
	while (s != end && *s == ' ')
		++s;

	/* compress internal blanks and strip trailing blanks */
	while (s != end && *s) {
		if (*s++ != ' ' || (s != end && *s && *s != ' '))
			*p++ = *(s-1);
	}

	/* wipe out trailing garbage */
	while (p != end)
		*p++ = '\0';
}

/*
 *  All hosts that use the 80c ribbon must use this!
 */
int eighty_ninty_three(struct ata_device *drive)
{
	return ((drive->channel->udma_four) &&
#ifndef CONFIG_IDEDMA_IVB
		(drive->id->hw_config & 0x4000) &&
#endif
		(drive->id->hw_config & 0x6000)) ? 1 : 0;
}

/* FIXME: Channel lock should be held.
 */
int ide_config_drive_speed(struct ata_device *drive, u8 speed)
{
	struct ata_channel *ch = drive->channel;
	int ret;

#if defined(CONFIG_BLK_DEV_IDEDMA) && !defined(__CRIS__)
	u8 unit = (drive->select.b.unit & 0x01);
	outb(inb(ch->dma_base + 2) & ~(1 << (5 + unit)), ch->dma_base + 2);
#endif

	/*
         * Select the drive, and issue the SETFEATURES command.
         */
	disable_irq(ch->irq);	/* disable_irq_nosync ?? */
	udelay(1);
	ata_select(drive, 0);
	ata_mask(drive);
	udelay(1);
	ata_irq_enable(drive, 0);
	OUT_BYTE(speed, IDE_NSECTOR_REG);
	OUT_BYTE(SETFEATURES_XFER, IDE_FEATURE_REG);
	OUT_BYTE(WIN_SETFEATURES, IDE_COMMAND_REG);
	if (drive->quirk_list == 2)
		ata_irq_enable(drive, 1);
	udelay(1);
	ret = ata_status_poll(drive, 0, BUSY_STAT, WAIT_CMD, NULL);
	ata_mask(drive);
	enable_irq(ch->irq);

	if (ret != ATA_OP_READY) {
		ata_dump(drive, NULL, "set drive speed");
		return 1;
	}

	drive->id->dma_ultra &= ~0xFF00;
	drive->id->dma_mword &= ~0x0F00;
	drive->id->dma_1word &= ~0x0F00;

#if defined(CONFIG_BLK_DEV_IDEDMA) && !defined(__CRIS__)
	if (speed > XFER_PIO_4) {
		outb(inb(ch->dma_base + 2)|(1 << (5 + unit)), ch->dma_base + 2);
	} else {
		outb(inb(ch->dma_base + 2) & ~(1 << (5 + unit)), ch->dma_base + 2);
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

	drive->current_speed = speed;

	return 0;
}

static inline void do_identify(struct ata_device *drive, u8 cmd)
{
	int bswap = 1;
	struct hd_driveid *id;

	id = drive->id = kmalloc (SECTOR_WORDS*4, GFP_ATOMIC);	/* called with interrupts disabled! */
	if (!id) {
		printk(KERN_WARNING "(ide-probe::do_identify) Out of memory.\n");
		goto err_kmalloc;
	}

	/* Read 512 bytes of id info.
	 *
	 * Please note that it is well known that some *very* old drives are
	 * able to provide only 256 of them, since this was the amount read by
	 * DOS.
	 *
	 * However let's try to get away with this...
	 */

	ata_read(drive, id, SECTOR_WORDS);
	local_irq_enable();
	ata_fix_driveid(id);

	if (id->word156 == 0x4d42) {
		printk("%s: drive->id->word156 == 0x%04x \n", drive->name, drive->id->word156);
	}

	if (!drive->forced_lun)
		drive->last_lun = id->last_lun & 0x7;
#if defined (CONFIG_SCSI_EATA_DMA) || defined (CONFIG_SCSI_EATA_PIO) || defined (CONFIG_SCSI_EATA)
	/*
	 * EATA SCSI controllers do a hardware ATA emulation:
	 * Ignore them if there is a driver for them available.
	 */
	if ((id->model[0] == 'P' && id->model[1] == 'M')
	 || (id->model[0] == 'S' && id->model[1] == 'K')) {
		printk("%s: EATA SCSI HBA %.10s\n", drive->name, id->model);
		goto err_misc;
	}
#endif

	/*
	 *  WIN_IDENTIFY returns little-endian info,
	 *  WIN_PIDENTIFY *usually* returns little-endian info.
	 */
	if (cmd == WIN_PIDENTIFY) {
		if ((id->model[0] == 'N' && id->model[1] == 'E') /* NEC */
		 || (id->model[0] == 'F' && id->model[1] == 'X') /* Mitsumi */
		 || (id->model[0] == 'P' && id->model[1] == 'i'))/* Pioneer */
			bswap ^= 1;	/* Vertos drives may still be weird */
	}
	ide_fixstring(id->model,     sizeof(id->model),     bswap);
	ide_fixstring(id->fw_rev,    sizeof(id->fw_rev),    bswap);
	ide_fixstring(id->serial_no, sizeof(id->serial_no), bswap);

	if (strstr(id->model, "E X A B Y T E N E S T"))
		goto err_misc;

	id->model[sizeof(id->model)-1] = '\0';	/* we depend on this a lot! */
	printk("%s: %s, ", drive->name, id->model);
	drive->present = 1;

	/*
	 * Check for an ATAPI device:
	 */
	if (cmd == WIN_PIDENTIFY) {
		u8 type = (id->config >> 8) & 0x1f;
		printk("ATAPI ");
#ifdef CONFIG_BLK_DEV_PDC4030
		if (drive->channel->unit == 1 && drive->channel->chipset == ide_pdc4030) {
			printk(" -- not supported on 2nd Promise port\n");
			goto err_misc;
		}
#endif
		switch (type) {
			case ATA_FLOPPY:
				if (!strstr(id->model, "CD-ROM")) {
					if (!strstr(id->model, "oppy") && !strstr(id->model, "poyp") && !strstr(id->model, "ZIP"))
						printk("cdrom or floppy?, assuming ");
					if (drive->type != ATA_ROM) {
						printk ("FLOPPY");
						break;
					}
				}
				type = ATA_ROM;	/* Early cdrom models used zero */
			case ATA_ROM:
				drive->removable = 1;
#ifdef CONFIG_PPC
				/* kludge for Apple PowerBook internal zip */
				if (!strstr(id->model, "CD-ROM") && strstr(id->model, "ZIP")) {
					printk ("FLOPPY");
					type = ATA_FLOPPY;
					break;
				}
#endif
				printk ("CD/DVD-ROM");
				break;
			case ATA_TAPE:
				printk ("TAPE");
				break;
			case ATA_MOD:
				printk ("OPTICAL");
				drive->removable = 1;
				break;
			default:
				printk("UNKNOWN (type %d)", type);
				break;
		}
		printk (" drive\n");
		drive->type = type;
		return;
	}

	/*
	 * Not an ATAPI device: looks like a "regular" hard disk:
	 */
	if (id->config & (1<<7))
		drive->removable = 1;

	/*
	 * FIXME: This is just plain ugly or plain unnecessary.
	 *
	 * Prevent long system lockup probing later for non-existant slave
	 * drive if the hwif is actually a flash memory card of some variety:
	 */

	if (drive_is_flashcard(drive)) {
		struct ata_device *mate = &drive->channel->drives[1 ^ drive->select.b.unit];
		if (!mate->ata_flash) {
			mate->present = 0;
			mate->noprobe = 1;
		}
	}
	drive->type = ATA_DISK;
	printk("DISK drive\n");

	/* Initialize our quirk list. */
	if (drive->channel->quirkproc)
		drive->quirk_list = drive->channel->quirkproc(drive);

	/* Initialize queue depth settings */
	drive->queue_depth = 1;
#ifdef CONFIG_BLK_DEV_IDE_TCQ_DEPTH
	drive->queue_depth = CONFIG_BLK_DEV_IDE_TCQ_DEPTH;
#else
	drive->queue_depth = drive->id->queue_depth + 1;
#endif
	if (drive->queue_depth < 1 || drive->queue_depth > IDE_MAX_TAG)
		drive->queue_depth = IDE_MAX_TAG;

	return;

err_misc:
	kfree(id);
err_kmalloc:
	drive->present = 0;

	return;
}

/*
 * Sends an ATA(PI) IDENTIFY request to a drive and wait for a response.  It
 * also monitor irqs while this is happening, in hope of automatically
 * determining which one is being used by the interface.
 *
 * Returns:	0  device was identified
 *		1  device timed-out (no response to identify request)
 *		2  device aborted the command (refused to identify itself)
 */
static int identify(struct ata_device *drive, u8 cmd)
{
	struct ata_channel *ch = drive->channel;
	int rc = 1;
	int autoprobe = 0;
	unsigned long cookie = 0;
	ide_ioreg_t hd_status;
	unsigned long timeout;


	/* FIXME: perhaps we should be just using allways the status register,
	 * since it should simplify the code significantly.
	 */
	if (ch->io_ports[IDE_CONTROL_OFFSET]) {
		u8 s;
		u8 a;

		if (!drive->channel->irq) {
			autoprobe = 1;
			cookie = probe_irq_on();
			ata_irq_enable(drive, 1);	/* enable device irq */
		}

		/* take a deep breath */
		mdelay(50);
		a = IN_BYTE(ch->io_ports[IDE_ALTSTATUS_OFFSET]);
		s = IN_BYTE(ch->io_ports[IDE_STATUS_OFFSET]);
		if ((a ^ s) & ~INDEX_STAT) {
			printk("%s: probing with STATUS(0x%02x) instead of ALTSTATUS(0x%02x)\n", drive->name, s, a);
			hd_status = ch->io_ports[IDE_STATUS_OFFSET];	/* ancient Seagate drives, broken interfaces */
		} else {
			hd_status = ch->io_ports[IDE_ALTSTATUS_OFFSET];	/* use non-intrusive polling */
		}
	} else {
		mdelay(50);
		hd_status = ch->io_ports[IDE_STATUS_OFFSET];
	}

	/* set features register for atapi identify command to be sure of reply */
	if ((cmd == WIN_PIDENTIFY))
		OUT_BYTE(0,IDE_FEATURE_REG);	/* disable dma & overlap */

#if CONFIG_BLK_DEV_PDC4030
	if (drive->channel->chipset == ide_pdc4030) {
		/* DC4030 hosted drives need their own identify... */
		extern int pdc4030_identify(struct ata_device *);

		if (pdc4030_identify(drive))
			goto out;
	} else
#endif
		OUT_BYTE(cmd, IDE_COMMAND_REG);		/* ask drive for ID */
	timeout = ((cmd == WIN_IDENTIFY) ? WAIT_WORSTCASE : WAIT_PIDENTIFY) / 2;
	timeout += jiffies;
	do {
		if (time_after(jiffies, timeout))
			goto out;	/* drive timed-out */
		mdelay(50);		/* give drive a breather */
	} while (IN_BYTE(hd_status) & BUSY_STAT);

	mdelay(50);		/* wait for IRQ and DRQ_STAT */

	if (ata_status(drive, DRQ_STAT, BAD_R_STAT)) {
		unsigned long flags;

		local_irq_save(flags);		/* some systems need this */
		do_identify(drive, cmd);	/* drive returned ID */
		rc = 0;				/* drive responded with ID */
		ata_status(drive, 0, 0);	/* clear drive IRQ */
		local_irq_restore(flags);	/* local CPU only */
	} else
		rc = 2;			/* drive refused ID */

out:
	if (autoprobe) {
		int irq;

		ata_irq_enable(drive, 0);	/* mask device irq */
		ata_status(drive, 0, 0);	/* clear drive IRQ */
		udelay(5);
		irq = probe_irq_off(cookie);
		if (!drive->channel->irq) {
			if (irq > 0)
				drive->channel->irq = irq;
			else	/* Mmmm.. multiple IRQs.. don't know which was ours */
				printk("%s: IRQ probe failed (0x%lx)\n", drive->name, cookie);
		}
	}

	return rc;
}


/*
 * This has the difficult job of finding a drive if it exists, without getting
 * hung up if it doesn't exist, without trampling on ethernet cards, and
 * without leaving any IRQs dangling to haunt us later.
 *
 * If a drive is "known" to exist (from CMOS or kernel parameters), but does
 * not respond right away, the probe will "hang in there" for the maximum wait
 * time (about 30 seconds), otherwise it will exit much more quickly.
 *
 * Returns:	0  device was identified
 *		1  device timed-out (no response to identify request)
 *		2  device aborted the command (refused to identify itself)
 *		3  bad status from device (possible for ATAPI drives)
 *		4  probe was not attempted because failure was obvious
 */
static int do_probe(struct ata_device *drive, u8 cmd)
{
	int rc;
	struct ata_channel *ch = drive->channel;
	u8 select;

	if (drive->present) {	/* avoid waiting for inappropriate probes */
		if ((drive->type != ATA_DISK) && (cmd == WIN_IDENTIFY))
			return 4;
	}
#ifdef DEBUG
	printk("probing for %s: present=%d, type=%02x, probetype=%s\n",
		drive->name, drive->present, drive->type,
		(cmd == WIN_IDENTIFY) ? "ATA" : "ATAPI");
#endif
	mdelay(50);	/* needed for some systems (e.g. crw9624 as drive0 with disk as slave) */
	ata_select(drive, 50000);
	select = IN_BYTE(IDE_SELECT_REG);
	if (select != drive->select.all && !drive->present) {
		if (drive->select.b.unit != 0) {
			ata_select(&ch->drives[0], 50000);	/* exit with drive0 selected */
		}
		return 3;    /* no i/f present: mmm.. this should be a 4 -ml */
	}

	if (ata_status(drive, READY_STAT, BUSY_STAT) || drive->present || cmd == WIN_PIDENTIFY)	{
		if ((rc = identify(drive,cmd)))   /* send cmd and wait */
			rc = identify(drive,cmd); /* failed: try again */
		if (rc == 1 && cmd == WIN_PIDENTIFY && drive->autotune != 2) {
			unsigned long timeout;
			printk("%s: no response (status = 0x%02x), resetting drive\n",
					drive->name, drive->status);
			mdelay(50);
			OUT_BYTE(drive->select.all, IDE_SELECT_REG);
			mdelay(50);
			OUT_BYTE(WIN_SRST, IDE_COMMAND_REG);
			timeout = jiffies;
			while (!ata_status(drive, 0, BUSY_STAT) && time_before(jiffies, timeout + WAIT_WORSTCASE))
				mdelay(50);
			rc = identify(drive, cmd);
		}
		if (rc == 1)
			printk("%s: no response (status = 0x%02x)\n",
					drive->name, drive->status);
		ata_status(drive, 0, 0);	/* ensure drive irq is clear */
	} else
		rc = 3;				/* not present or maybe ATAPI */

	if (drive->select.b.unit != 0) {
		ata_select(&ch->drives[0], 50000);	/* exit with drive0 selected */
		ata_status(drive, 0, 0);		/* ensure drive irq is clear */
	}

	return rc;
}

/*
 * Probe for drivers on a channel.
 *
 * This routine only knows how to look for drive units 0 and 1 on an interface,
 * so any setting of MAX_DRIVES > 2 won't work here.
 */
static void channel_probe(struct ata_channel *ch)
{
	unsigned int i;
	unsigned long flags;
	int error;

	if (ch->noprobe)
		return;

	ch->straight8 = 0;

	local_save_flags(flags);
	local_irq_enable();	/* needed for jiffies and irq probing */

	/*
	 * Check for the presence of a channel by probing for drives on it.
	 */
	for (i = 0; i < MAX_DRIVES; ++i) {
		struct ata_device *drive = &ch->drives[i];

		if (drive->noprobe)	/* don't look for this one */
			continue;

		if (do_probe(drive, WIN_IDENTIFY) >= 2) { /* if !(success||timed-out) */
			do_probe(drive, WIN_PIDENTIFY); /* look for ATAPI device */
		}

		/* Special handling of EXABYTE controller cards. */
		if (drive->id && strstr(drive->id->model, "E X A B Y T E N E S T")) {
			unsigned long timeout;

			printk("%s: enabling %s -- ", drive->channel->name, drive->id->model);
			ata_select(drive, 50000);
			OUT_BYTE(EXABYTE_ENABLE_NEST, IDE_COMMAND_REG);
			timeout = jiffies + WAIT_WORSTCASE;
			do {
				if (time_after(jiffies, timeout)) {
					printk("failed (timeout)\n");
					return;
				}
				mdelay(50);
			} while (!ata_status(drive, 0, BUSY_STAT));
			mdelay(50);
			if (!ata_status(drive, 0, BAD_STAT))
				printk("failed (status = 0x%02x)\n", drive->status);
			else
				printk("success\n");

			if (do_probe(drive, WIN_IDENTIFY) >= 2) { /* if !(success||timed-out) */
				do_probe(drive, WIN_PIDENTIFY); /* look for ATAPI device */
		}
		}

		if (!drive->present)
			continue;	/* drive not found */

		if (!drive->id) {	/* identification failed? */
			if (drive->type == ATA_DISK)
				printk ("%s: pre-ATA drive, CHS=%d/%d/%d\n",
						drive->name, drive->cyl, drive->head, drive->sect);
			else if (drive->type == ATA_ROM)
				printk("%s: ATAPI cdrom (?)\n", drive->name);
			else
				drive->present = 0;	/* nuke it */
		}

		/* drive found, there is a channel it is attached too. */
		if (drive->present)
			ch->present = 1;
	}

	if (!ch->present)
		goto not_found;

	error = 0;

	if (((unsigned long)ch->io_ports[IDE_DATA_OFFSET] | 7) ==
			((unsigned long)ch->io_ports[IDE_STATUS_OFFSET])) {
		error += !request_region(ch->io_ports[IDE_DATA_OFFSET], 8, ch->name);
		ch->straight8 = 1;
	} else {
		for (i = 0; i < 8; i++)
			error += !request_region(ch->io_ports[i], 1, ch->name);
	}
	if (ch->io_ports[IDE_CONTROL_OFFSET])
		error += !request_region(ch->io_ports[IDE_CONTROL_OFFSET], 1, ch->name);
#if defined(CONFIG_AMIGA) || defined(CONFIG_MAC)
	if (ch->io_ports[IDE_IRQ_OFFSET])
		error += !request_region(ch->io_ports[IDE_IRQ_OFFSET], 1, ch->name);
#endif

	/* Some neccessary register area was already used. Skip this device.
	 */

	if (
#if CONFIG_BLK_DEV_PDC4030
			(ch->chipset != ide_pdc4030 || ch->unit == 0) &&
#endif
			error) {

		/* FIXME: We should be dealing properly with partial IO region
		 * allocations here.
		 */

		ch->present = 0;
		printk("%s: error: ports already in use!\n", ch->name);
	}

	if (!ch->present)
		goto not_found;

	/* Register this hardware interface within the global device tree.
	 *
	 * FIXME: This should be handled as a pci subdevice in a generic way.
	 */
	sprintf(ch->dev.bus_id, "ata@%02x", ch->unit);
	strcpy(ch->dev.name, "ATA/ATAPI Host-Channel");
	ch->dev.driver_data = ch;
#ifdef CONFIG_PCI
	if (ch->pci_dev)
		ch->dev.parent = &ch->pci_dev->dev;
	else
#endif
		ch->dev.parent = NULL; /* Would like to do = &device_legacy */

	device_register(&ch->dev);

	if (ch->reset)
		ata_reset(ch);

	local_irq_restore(flags);

	/*
	 * Now setup the PIO transfer modes of the drives on this channel.
	 */
	for (i = 0; i < MAX_DRIVES; ++i) {
		struct ata_device *drive = &ch->drives[i];

		if (drive->present && (drive->autotune == 1)) {
			if (drive->channel->tuneproc)
				drive->channel->tuneproc(drive, 255);	/* auto-tune PIO mode */
		}
	}

	return;

not_found:
	local_irq_restore(flags);
}

/*
 * This routine sets up the irq for an ide interface, and creates a new hwgroup
 * for the irq/channel if none was previously assigned.
 *
 * Much of the code is for correctly detecting/handling irq sharing and irq
 * serialization situations.  This is somewhat complex because it handles
 * static as well as dynamic (PCMCIA) IDE interfaces.
 *
 * The SA_INTERRUPT in sa_flags means ata_irq_request() is always entered with
 * interrupts completely disabled.  This can be bad for interrupt latency, but
 * anything else has led to problems on some machines.  We re-enable interrupts
 * as much as we can safely do in most places.
 */
static int init_irq(struct ata_channel *ch)
{
	unsigned long flags;
	int i;
	spinlock_t *lock;
	spinlock_t *new_lock;
	unsigned long *active;
	unsigned long *new_active;
	struct ata_channel *match = NULL;

	/* Spare allocation before sleep. */
	new_lock = kmalloc(sizeof(*lock), GFP_KERNEL);
	new_active = kmalloc(sizeof(*active), GFP_KERNEL);
	*new_active = 0L;

	spin_lock_irqsave(&ide_lock, flags);
	ch->lock = NULL;

#if MAX_HWIFS > 1
	/*
	 * Group up with any other channels that share our irq(s).
	 */
	for (i = 0; i < MAX_HWIFS; ++i) {
		struct ata_channel *h = &ide_hwifs[i];

		/* scan only initialized channels */
		if (!h->lock)
			continue;

		if (ch->irq != h->irq)
		        continue;

		ch->sharing_irq = h->sharing_irq = 1;

		if (ch->chipset != ide_pci || h->chipset != ide_pci ||
		     ch->serialized || h->serialized) {
			if (match && match->lock && match->lock != h->lock)
				printk("%s: potential irq problem with %s and %s\n", ch->name, h->name, match->name);
			/* don't undo a prior perfect match */
			if (!match || match->irq != ch->irq)
				match = h;
		}
	}
#endif
	/*
	 * If we are still without a lock group, then form a new one
	 */
	if (!match) {
		lock = new_lock;
		active = new_active;
		if (!lock) {
			spin_unlock_irqrestore(&ide_lock, flags);

			return 1;
		}
		spin_lock_init(lock);
	} else {
		lock = match->lock;
		active = match->active;
		if(new_lock)
			kfree(new_lock);
	}

	/*
	 * Allocate the irq, if not already obtained for another channel
	 */
	if (!match || match->irq != ch->irq) {
		struct ata_device tmp;
#ifdef CONFIG_IDEPCI_SHARE_IRQ
		int sa = IDE_CHIPSET_IS_PCI(ch->chipset) ? SA_SHIRQ : SA_INTERRUPT;
#else
		int sa = IDE_CHIPSET_IS_PCI(ch->chipset) ? SA_INTERRUPT|SA_SHIRQ : SA_INTERRUPT;
#endif

		/* Enable interrupts triggered by the drive.  We use a shallow
		 * device structure, just to use the generic function very
		 * early.
		 */
		tmp.channel = ch;
		ata_irq_enable(&tmp, 1);

		if (request_irq(ch->irq, &ata_irq_request, sa, ch->name, ch)) {
			if (!match) {
				kfree(lock);
				kfree(active);
			}

			spin_unlock_irqrestore(&ide_lock, flags);

			return 1;
		}
	}

	/*
	 * Everything is okay. Tag us as member of this lock group.
	 */
	ch->lock = lock;
	ch->active = active;

	init_timer(&ch->timer);
	ch->timer.function = &ide_timer_expiry;
	ch->timer.data = (unsigned long) ch;

	for (i = 0; i < MAX_DRIVES; ++i) {
		struct ata_device *drive = &ch->drives[i];
		request_queue_t *q;
		int max_sectors = 255;

		if (!drive->present)
			continue;

		if (!ch->drive)
			ch->drive = drive;

		/*
		 * Init the per device request queue
		 */

		q = &drive->queue;
		q->queuedata = drive->channel;
		blk_init_queue(q, do_ide_request, drive->channel->lock);
		blk_queue_segment_boundary(q, 0xffff);

		/* ATA can do up to 128K per request, pdc4030 needs smaller limit */
#ifdef CONFIG_BLK_DEV_PDC4030
		if (drive->channel->chipset == ide_pdc4030)
			max_sectors = 127;
#endif
		blk_queue_max_sectors(q, max_sectors);

		/* IDE DMA can do PRD_ENTRIES number of segments. */
		blk_queue_max_hw_segments(q, PRD_ENTRIES);

		/* FIXME: This is a driver limit and could be eliminated. */
		blk_queue_max_phys_segments(q, PRD_ENTRIES);
	}
	spin_unlock_irqrestore(&ide_lock, flags);

#if !defined(__mc68000__) && !defined(CONFIG_APUS) && !defined(__sparc__)
	printk("%s at 0x%03x-0x%03x,0x%03x on irq %d", ch->name,
		ch->io_ports[IDE_DATA_OFFSET],
		ch->io_ports[IDE_DATA_OFFSET]+7,
		ch->io_ports[IDE_CONTROL_OFFSET], ch->irq);
#elif defined(__sparc__)
	printk("%s at 0x%03lx-0x%03lx,0x%03lx on irq %s", ch->name,
		ch->io_ports[IDE_DATA_OFFSET],
		ch->io_ports[IDE_DATA_OFFSET]+7,
		ch->io_ports[IDE_CONTROL_OFFSET], __irq_itoa(ch->irq));
#else
	printk("%s at %p on irq 0x%08x", ch->name,
		ch->io_ports[IDE_DATA_OFFSET], ch->irq);
#endif
	if (match)
		printk(" (%sed with %s)",
			ch->sharing_irq ? "shar" : "serializ", match->name);
	printk("\n");

	return 0;
}

/*
 * Returns the queue which corresponds to a given device.
 *
 * FIXME: this should take struct block_device * as argument in future.
 */
static request_queue_t *ata_get_queue(kdev_t dev)
{
	struct ata_channel *ch = (struct ata_channel *)blk_dev[major(dev)].data;

	/* FIXME: ALLERT: This discriminates between master and slave! */
	return &ch->drives[DEVICE_NR(dev) & 1].queue;
}

/* Number of minor numbers we consume par channel. */
#define ATA_MINORS	(MAX_DRIVES * (1 << PARTN_BITS))

static void channel_init(struct ata_channel *ch)
{
	struct gendisk *gd;
	unsigned int unit;
	extern devfs_handle_t ide_devfs_handle;

	if (!ch->present)
		return;

	/* we set it back to 1 if all is ok below */
	ch->present = 0;

	if (!ch->irq) {
		if (!(ch->irq = ide_default_irq(ch->io_ports[IDE_DATA_OFFSET]))) {
			printk("%s: DISABLED, NO IRQ\n", ch->name);

			return;
		}
	}
#ifdef CONFIG_BLK_DEV_HD
	if (ch->irq == HD_IRQ && ch->io_ports[IDE_DATA_OFFSET] != HD_DATA) {
		printk("%s: CANNOT SHARE IRQ WITH OLD HARDDISK DRIVER (hd.c)\n", ch->name);

		return;
	}
#endif

	if (devfs_register_blkdev(ch->major, ch->name, ide_fops)) {
		printk("%s: UNABLE TO GET MAJOR NUMBER %d\n", ch->name, ch->major);

		return;
	}

	if (init_irq(ch)) {
		int irq = ch->irq;
		/*
		 * It failed to initialise. Find the default IRQ for
		 * this port and try that.
		 */
		if (!(ch->irq = ide_default_irq(ch->io_ports[IDE_DATA_OFFSET]))) {
			printk(KERN_INFO "%s: disabled; unable to get IRQ %d.\n", ch->name, irq);
			(void) unregister_blkdev (ch->major, ch->name);

			return;
		}
		if (init_irq(ch)) {
			printk(KERN_INFO "%s: probed IRQ %d and default IRQ %d failed.\n",
				ch->name, irq, ch->irq);
			(void) unregister_blkdev(ch->major, ch->name);

			return;
		}
		printk(KERN_INFO "%s: probed IRQ %d failed, using default.\n", ch->name, ch->irq);
	}

	/* Initialize partition and global device data.
	 */

	gd = kmalloc (sizeof(struct gendisk), GFP_KERNEL);
	if (!gd)
		goto err_kmalloc_gd;

	memset(gd, 0, sizeof(struct gendisk));
	gd->sizes = kmalloc(ATA_MINORS * sizeof(int), GFP_KERNEL);
	if (!gd->sizes)
		goto err_kmalloc_gd_sizes;
	memset(gd->sizes, 0, ATA_MINORS*sizeof(gd->sizes[0]));

	gd->part = kmalloc(ATA_MINORS * sizeof(struct hd_struct), GFP_KERNEL);
	if (!gd->part)
		goto err_kmalloc_gd_part;
	memset(gd->part, 0, ATA_MINORS * sizeof(struct hd_struct));

	gd->de_arr = kmalloc (sizeof(*gd->de_arr) * MAX_DRIVES, GFP_KERNEL);
	if (!gd->de_arr)
		goto err_kmalloc_gd_de_arr;
	memset(gd->de_arr, 0, sizeof(*gd->de_arr) * MAX_DRIVES);

	gd->flags = kmalloc (sizeof(*gd->flags) * MAX_DRIVES, GFP_KERNEL);
	if (!gd->flags)
		goto err_kmalloc_gd_flags;
	memset(gd->flags, 0, sizeof(*gd->flags) * MAX_DRIVES);

	for (unit = 0; unit < MAX_DRIVES; ++unit)
		ch->drives[unit].part = &gd->part[unit << PARTN_BITS];

	gd->major	= ch->major;		/* our major device number */
	gd->major_name	= IDE_MAJOR_NAME;	/* treated special in genhd.c */
	gd->minor_shift	= PARTN_BITS;		/* num bits for partitions */
	gd->nr_real	= MAX_DRIVES;		/* current num real drives */
	gd->next	= NULL;			/* linked list of major devs */
	gd->fops        = ide_fops;             /* file operations */

	ch->gd = gd;
	add_gendisk(gd);

	for (unit = 0; unit < MAX_DRIVES; ++unit) {
		char name[80];

		ch->drives[unit].dn = ((ch->unit ? 2 : 0) + unit);
		sprintf(name, "host%d/bus%d/target%d/lun%d",
			ch->index, ch->unit, unit, ch->drives[unit].lun);
		if (ch->drives[unit].present)
			ch->drives[unit].de = devfs_mk_dir(ide_devfs_handle, name, NULL);
	}

	blk_dev[ch->major].data = ch;
	blk_dev[ch->major].queue = ata_get_queue;

	/* All went well, flag this channel entry as valid again. */
	ch->present = 1;

	return;

err_kmalloc_gd_flags:
	kfree(gd->de_arr);
err_kmalloc_gd_de_arr:
	kfree(gd->part);
err_kmalloc_gd_part:
	kfree(gd->sizes);
err_kmalloc_gd_sizes:
	kfree(gd);
err_kmalloc_gd:
	printk(KERN_CRIT "(%s) Out of memory\n", __FUNCTION__);
}

/*
 * FIXME: consider moving this to main.c, since this is the only place where
 * it's used.
 *
 * Probe only for drives on channes which are not already present.
 */
int ideprobe_init(void)
{
	unsigned int i;
	int probe[MAX_HWIFS];

	for (i = 0; i < MAX_HWIFS; ++i)
		probe[i] = !ide_hwifs[i].present;

	/*
	 * Probe for drives in the usual way.. CMOS/BIOS, then poke at ports
	 */
	for (i = 0; i < MAX_HWIFS; ++i) {
		if (!probe[i])
			continue;
		channel_probe(&ide_hwifs[i]);
	}
	for (i = 0; i < MAX_HWIFS; ++i) {
		if (!probe[i])
			continue;
		channel_init(&ide_hwifs[i]);
	}
	return 0;
}

EXPORT_SYMBOL(ata_fix_driveid);
EXPORT_SYMBOL(ide_fixstring);
EXPORT_SYMBOL(eighty_ninty_three);
EXPORT_SYMBOL(ide_config_drive_speed);
