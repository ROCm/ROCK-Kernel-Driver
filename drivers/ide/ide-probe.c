/*
 *  linux/drivers/ide/ide-probe.c	Version 1.07	March 18, 2001
 *
 *  Copyright (C) 1994-1998  Linus Torvalds & authors (see below)
 */

/*
 *  Mostly written by Mark Lord <mlord@pobox.com>
 *                and Gadi Oxman <gadio@netvision.net.il>
 *                and Andre Hedrick <andre@linux-ide.org>
 *
 *  See linux/MAINTAINERS for address of current maintainer.
 *
 * This is the IDE probe module, as evolved from hd.c and ide.c.
 *
 * Version 1.00		move drive probing code from ide.c to ide-probe.c
 * Version 1.01		fix compilation problem for m68k
 * Version 1.02		increase WAIT_PIDENTIFY to avoid CD-ROM locking at boot
 *			 by Andrea Arcangeli
 * Version 1.03		fix for (hwif->chipset == ide_4drives)
 * Version 1.04		fixed buggy treatments of known flash memory cards
 *
 * Version 1.05		fix for (hwif->chipset == ide_pdc4030)
 *			added ide6/7/8/9
 *			allowed for secondary flash card to be detectable
 *			 with new flag : drive->ata_flash : 1;
 * Version 1.06		stream line request queue and prep for cascade project.
 * Version 1.07		max_sect <= 255; slower disks would get behind and
 * 			then fall over when they get to 256.	Paul G.
 */

#undef REALLY_SLOW_IO		/* most systems can safely undef this */

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
#include <linux/ide.h>
#include <linux/spinlock.h>

#include <asm/byteorder.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/io.h>

/*
 * CompactFlash cards and their brethern pretend to be removable
 * hard disks, except:
 *	(1) they never have a slave unit, and
 *	(2) they don't have doorlock mechanisms.
 * This test catches them, and is invoked elsewhere when setting
 * appropriate config bits.
 *
 * FIXME: This treatment is probably applicable for *all* PCMCIA (PC CARD)
 * devices, so in linux 2.3.x we should change this to just treat all PCMCIA
 * drives this way, and get rid of the model-name tests below
 * (too big of an interface change for 2.2.x).
 * At that time, we might also consider parameterizing the timeouts and retries,
 * since these are MUCH faster than mechanical drives.	-M.Lord
 */
inline int drive_is_flashcard (ide_drive_t *drive)
{
	struct hd_driveid *id = drive->id;

	if (drive->removable && id != NULL) {
		if (id->config == 0x848a) return 1;	/* CompactFlash */
		if (!strncmp(id->model, "KODAK ATA_FLASH", 15)	/* Kodak */
		 || !strncmp(id->model, "Hitachi CV", 10)	/* Hitachi */
		 || !strncmp(id->model, "SunDisk SDCFB", 13)	/* SunDisk */
		 || !strncmp(id->model, "HAGIWARA HPC", 12)	/* Hagiwara */
		 || !strncmp(id->model, "LEXAR ATA_FLASH", 15)	/* Lexar */
		 || !strncmp(id->model, "ATA_FLASH", 9))	/* Simple Tech */
		{
			return 1;	/* yes, it is a flash memory card */
		}
	}
	return 0;	/* no, it is not a flash memory card */
}

static inline void do_identify (ide_drive_t *drive, u8 cmd)
{
	ide_hwif_t *hwif = HWIF(drive);
	int bswap = 1;
	struct hd_driveid *id;

	/* called with interrupts disabled! */
	id = drive->id = kmalloc(SECTOR_WORDS*4, GFP_ATOMIC);
	if (!id) {
		printk(KERN_WARNING "(ide-probe::do_identify) "
			"Out of memory.\n");
		goto err_kmalloc;
	}
	/* read 512 bytes of id info */
	hwif->ata_input_data(drive, id, SECTOR_WORDS);

	local_irq_enable();
	ide_fix_driveid(id);

	if (!drive->forced_lun)
		drive->last_lun = id->last_lun & 0x7;

#if defined (CONFIG_SCSI_EATA_DMA) || defined (CONFIG_SCSI_EATA_PIO) || defined (CONFIG_SCSI_EATA)
	/*
	 * EATA SCSI controllers do a hardware ATA emulation:
	 * Ignore them if there is a driver for them available.
	 */
	if ((id->model[0] == 'P' && id->model[1] == 'M') ||
	    (id->model[0] == 'S' && id->model[1] == 'K')) {
		printk("%s: EATA SCSI HBA %.10s\n", drive->name, id->model);
		goto err_misc;
	}
#endif /* CONFIG_SCSI_EATA_DMA || CONFIG_SCSI_EATA_PIO */

	/*
	 *  WIN_IDENTIFY returns little-endian info,
	 *  WIN_PIDENTIFY *usually* returns little-endian info.
	 */
	if (cmd == WIN_PIDENTIFY) {
		if ((id->model[0] == 'N' && id->model[1] == 'E') /* NEC */
		 || (id->model[0] == 'F' && id->model[1] == 'X') /* Mitsumi */
		 || (id->model[0] == 'P' && id->model[1] == 'i'))/* Pioneer */
			/* Vertos drives may still be weird */
			bswap ^= 1;	
	}
	ide_fixstring(id->model,     sizeof(id->model),     bswap);
	ide_fixstring(id->fw_rev,    sizeof(id->fw_rev),    bswap);
	ide_fixstring(id->serial_no, sizeof(id->serial_no), bswap);

	if (strstr(id->model, "E X A B Y T E N E S T"))
		goto err_misc;

	/* we depend on this a lot! */
	id->model[sizeof(id->model)-1] = '\0';
	printk("%s: %s, ", drive->name, id->model);
	drive->present = 1;

	/*
	 * Check for an ATAPI device
	 */
	if (cmd == WIN_PIDENTIFY) {
		u8 type = (id->config >> 8) & 0x1f;
		printk("ATAPI ");
#ifdef CONFIG_BLK_DEV_PDC4030
		if (hwif->channel == 1 && hwif->chipset == ide_pdc4030) {
			printk(" -- not supported on 2nd Promise port\n");
			goto err_misc;
		}
#endif /* CONFIG_BLK_DEV_PDC4030 */
		switch (type) {
			case ide_floppy:
				if (!strstr(id->model, "CD-ROM")) {
					if (!strstr(id->model, "oppy") &&
					    !strstr(id->model, "poyp") &&
					    !strstr(id->model, "ZIP"))
						printk("cdrom or floppy?, assuming ");
					if (drive->media != ide_cdrom) {
						printk ("FLOPPY");
						break;
					}
				}
				/* Early cdrom models used zero */
				type = ide_cdrom;
			case ide_cdrom:
				drive->removable = 1;
#ifdef CONFIG_PPC
				/* kludge for Apple PowerBook internal zip */
				if (!strstr(id->model, "CD-ROM") &&
				    strstr(id->model, "ZIP")) {
					printk ("FLOPPY");
					type = ide_floppy;
					break;
				}
#endif
				printk ("CD/DVD-ROM");
				break;
			case ide_tape:
				printk ("TAPE");
				break;
			case ide_optical:
				printk ("OPTICAL");
				drive->removable = 1;
				break;
			default:
				printk("UNKNOWN (type %d)", type);
				break;
		}
		printk (" drive\n");
		drive->media = type;
		return;
	}

	/*
	 * Not an ATAPI device: looks like a "regular" hard disk
	 */
	if (id->config & (1<<7))
		drive->removable = 1;
	/*
	 * Prevent long system lockup probing later for non-existant
	 * slave drive if the hwif is actually a flash memory card of
	 * some variety:
	 */
	drive->is_flash = 0;
	if (drive_is_flashcard(drive)) {
		ide_drive_t *mate = &hwif->drives[1^drive->select.b.unit];
		if (!mate->ata_flash) {
			mate->present = 0;
			mate->noprobe = 1;
		}
		drive->is_flash = 1;
	}
	drive->media = ide_disk;
	printk("%s DISK drive\n", (drive->is_flash) ? "CFA" : "ATA" );
	QUIRK_LIST(drive);
	return;

err_misc:
	kfree(id);
err_kmalloc:
	drive->present = 0;
	return;
}

/*
 * try_to_identify() sends an ATA(PI) IDENTIFY request to a drive
 * and waits for a response.  It also monitors irqs while this is
 * happening, in hope of automatically determining which one is
 * being used by the interface.
 *
 * Returns:	0  device was identified
 *		1  device timed-out (no response to identify request)
 *		2  device aborted the command (refused to identify itself)
 */
static int actual_try_to_identify (ide_drive_t *drive, u8 cmd)
{
	ide_hwif_t *hwif = HWIF(drive);
	int rc;
	ide_ioreg_t hd_status;
	unsigned long timeout;
	u8 s = 0, a = 0;

	if (IDE_CONTROL_REG) {
		/* take a deep breath */
		ide_delay_50ms();
		a = hwif->INB(IDE_ALTSTATUS_REG);
		s = hwif->INB(IDE_STATUS_REG);
		if ((a ^ s) & ~INDEX_STAT) {
			printk("%s: probing with STATUS(0x%02x) instead of "
				"ALTSTATUS(0x%02x)\n", drive->name, s, a);
			/* ancient Seagate drives, broken interfaces */
			hd_status = IDE_STATUS_REG;
		} else {
			/* use non-intrusive polling */
			hd_status = IDE_ALTSTATUS_REG;
		}
	} else {
		ide_delay_50ms();
		hd_status = IDE_STATUS_REG;
	}

	/* set features register for atapi
	 * identify command to be sure of reply
	 */
	if ((cmd == WIN_PIDENTIFY))
		/* disable dma & overlap */
		hwif->OUTB(0, IDE_FEATURE_REG);

	if (hwif->identify != NULL) {
		if (hwif->identify(drive))
			return 1;
	} else {
		/* ask drive for ID */
		hwif->OUTB(cmd, IDE_COMMAND_REG);
	}
	timeout = ((cmd == WIN_IDENTIFY) ? WAIT_WORSTCASE : WAIT_PIDENTIFY) / 2;
	timeout += jiffies;
	do {
		if (time_after(jiffies, timeout)) {
			/* drive timed-out */
			return 1;
		}
		/* give drive a breather */
		ide_delay_50ms();
	} while ((hwif->INB(hd_status)) & BUSY_STAT);

	/* wait for IRQ and DRQ_STAT */
	ide_delay_50ms();
	if (OK_STAT((hwif->INB(IDE_STATUS_REG)), DRQ_STAT, BAD_R_STAT)) {
		unsigned long flags;

		/* local CPU only; some systems need this */
		local_irq_save(flags);
		/* drive returned ID */
		do_identify(drive, cmd);
		/* drive responded with ID */
		rc = 0;
		/* clear drive IRQ */
		(void) hwif->INB(IDE_STATUS_REG);
		local_irq_restore(flags);
	} else {
		/* drive refused ID */
		rc = 2;
	}
	return rc;
}

static int try_to_identify (ide_drive_t *drive, u8 cmd)
{
	ide_hwif_t *hwif = HWIF(drive);
	int retval;
	int autoprobe = 0;
	unsigned long cookie = 0;

	if (IDE_CONTROL_REG && !hwif->irq) {
		autoprobe = 1;
		cookie = probe_irq_on();
		/* enable device irq */
		hwif->OUTB(drive->ctl, IDE_CONTROL_REG);
	}

	retval = actual_try_to_identify(drive, cmd);

	if (autoprobe) {
		int irq;
		/* mask device irq */
		hwif->OUTB(drive->ctl|2, IDE_CONTROL_REG);
		/* clear drive IRQ */
		(void) hwif->INB(IDE_STATUS_REG);
		udelay(5);
		irq = probe_irq_off(cookie);
		if (!hwif->irq) {
			if (irq > 0) {
				hwif->irq = irq;
			} else {
				/* Mmmm.. multiple IRQs..
				 * don't know which was ours
				 */
				printk("%s: IRQ probe failed (0x%lx)\n",
					drive->name, cookie);
#ifdef CONFIG_BLK_DEV_CMD640
#ifdef CMD640_DUMP_REGS
				if (hwif->chipset == ide_cmd640) {
					printk("%s: Hmmm.. probably a driver "
						"problem.\n", drive->name);
					CMD640_DUMP_REGS;
				}
#endif /* CMD640_DUMP_REGS */
#endif /* CONFIG_BLK_DEV_CMD640 */
			}
		}
	}
	return retval;
}


/*
 * do_probe() has the difficult job of finding a drive if it exists,
 * without getting hung up if it doesn't exist, without trampling on
 * ethernet cards, and without leaving any IRQs dangling to haunt us later.
 *
 * If a drive is "known" to exist (from CMOS or kernel parameters),
 * but does not respond right away, the probe will "hang in there"
 * for the maximum wait time (about 30 seconds), otherwise it will
 * exit much more quickly.
 *
 * Returns:	0  device was identified
 *		1  device timed-out (no response to identify request)
 *		2  device aborted the command (refused to identify itself)
 *		3  bad status from device (possible for ATAPI drives)
 *		4  probe was not attempted because failure was obvious
 */
static int do_probe (ide_drive_t *drive, u8 cmd)
{
	int rc;
	ide_hwif_t *hwif = HWIF(drive);

	if (drive->present) {
		/* avoid waiting for inappropriate probes */
		if ((drive->media != ide_disk) && (cmd == WIN_IDENTIFY))
			return 4;
	}
#ifdef DEBUG
	printk("probing for %s: present=%d, media=%d, probetype=%s\n",
		drive->name, drive->present, drive->media,
		(cmd == WIN_IDENTIFY) ? "ATA" : "ATAPI");
#endif

	/* needed for some systems
	 * (e.g. crw9624 as drive0 with disk as slave)
	 */
	ide_delay_50ms();
	SELECT_DRIVE(drive);
	ide_delay_50ms();
	if (hwif->INB(IDE_SELECT_REG) != drive->select.all && !drive->present) {
		if (drive->select.b.unit != 0) {
			/* exit with drive0 selected */
			SELECT_DRIVE(&hwif->drives[0]);
			/* allow BUSY_STAT to assert & clear */
			ide_delay_50ms();
		}
		/* no i/f present: mmm.. this should be a 4 -ml */
		return 3;
	}

	if (OK_STAT((hwif->INB(IDE_STATUS_REG)), READY_STAT, BUSY_STAT) ||
	    drive->present || cmd == WIN_PIDENTIFY) {
		/* send cmd and wait */
		if ((rc = try_to_identify(drive, cmd))) {
			/* failed: try again */
			rc = try_to_identify(drive,cmd);
		}
		if (hwif->INB(IDE_STATUS_REG) == (BUSY_STAT|READY_STAT))
			return 4;

		if (rc == 1 && cmd == WIN_PIDENTIFY && drive->autotune != 2) {
			unsigned long timeout;
			printk("%s: no response (status = 0x%02x), "
				"resetting drive\n", drive->name,
				hwif->INB(IDE_STATUS_REG));
			ide_delay_50ms();
			hwif->OUTB(drive->select.all, IDE_SELECT_REG);
			ide_delay_50ms();
			hwif->OUTB(WIN_SRST, IDE_COMMAND_REG);
			timeout = jiffies;
			while (((hwif->INB(IDE_STATUS_REG)) & BUSY_STAT) &&
			       time_before(jiffies, timeout + WAIT_WORSTCASE))
				ide_delay_50ms();
			rc = try_to_identify(drive, cmd);
		}
		if (rc == 1)
			printk("%s: no response (status = 0x%02x)\n",
				drive->name, hwif->INB(IDE_STATUS_REG));
		/* ensure drive irq is clear */
		(void) hwif->INB(IDE_STATUS_REG);
	} else {
		/* not present or maybe ATAPI */
		rc = 3;
	}
	if (drive->select.b.unit != 0) {
		/* exit with drive0 selected */
		SELECT_DRIVE(&hwif->drives[0]);
		ide_delay_50ms();
		/* ensure drive irq is clear */
		(void) hwif->INB(IDE_STATUS_REG);
	}
	return rc;
}

/*
 *
 */
static void enable_nest (ide_drive_t *drive)
{
	ide_hwif_t *hwif = HWIF(drive);
	unsigned long timeout;

	printk("%s: enabling %s -- ", hwif->name, drive->id->model);
	SELECT_DRIVE(drive);
	ide_delay_50ms();
	hwif->OUTB(EXABYTE_ENABLE_NEST, IDE_COMMAND_REG);
	timeout = jiffies + WAIT_WORSTCASE;
	do {
		if (time_after(jiffies, timeout)) {
			printk("failed (timeout)\n");
			return;
		}
		ide_delay_50ms();
	} while ((hwif->INB(IDE_STATUS_REG)) & BUSY_STAT);

	ide_delay_50ms();

	if (!OK_STAT((hwif->INB(IDE_STATUS_REG)), 0, BAD_STAT)) {
		printk("failed (status = 0x%02x)\n", hwif->INB(IDE_STATUS_REG));
	} else {
		printk("success\n");
	}

	/* if !(success||timed-out) */
	if (do_probe(drive, WIN_IDENTIFY) >= 2) {
		/* look for ATAPI device */
		(void) do_probe(drive, WIN_PIDENTIFY);
	}
}

/*
 * probe_for_drive() tests for existence of a given drive using do_probe().
 *
 * Returns:	0  no device was found
 *		1  device was found (note: drive->present might still be 0)
 */
static inline u8 probe_for_drive (ide_drive_t *drive)
{
	/* skip probing? */
	if (drive->noprobe)
		return drive->present;

	/* if !(success||timed-out) */
	if (do_probe(drive, WIN_IDENTIFY) >= 2) {
		/* look for ATAPI device */
		(void) do_probe(drive, WIN_PIDENTIFY);
	}
	if (drive->id && strstr(drive->id->model, "E X A B Y T E N E S T"))
		enable_nest(drive);
	if (!drive->present)
		/* drive not found */
		return 0;

	/* identification failed? */
	if (drive->id == NULL) {
		if (drive->media == ide_disk) {
			printk("%s: non-IDE drive, CHS=%d/%d/%d\n",
				drive->name, drive->cyl,
				drive->head, drive->sect);
		} else if (drive->media == ide_cdrom) {
			printk("%s: ATAPI cdrom (?)\n", drive->name);
		} else {
			/* nuke it */
			drive->present = 0;
		}
	}
	/* drive was found */
	return 1;
}

#define hwif_check_region(addr, num) \
	((hwif->mmio) ? check_mem_region((addr),(num)) : check_region((addr),(num)))

static int hwif_check_regions (ide_hwif_t *hwif)
{
	u32 i		= 0;
	int addr_errs	= 0;

	if (hwif->mmio == 2)
		return 0;
	addr_errs  = hwif_check_region(hwif->io_ports[IDE_DATA_OFFSET], 1);
	for (i = IDE_ERROR_OFFSET; i <= IDE_STATUS_OFFSET; i++)
		addr_errs += hwif_check_region(hwif->io_ports[i], 1);
	if (hwif->io_ports[IDE_CONTROL_OFFSET])
		addr_errs += hwif_check_region(hwif->io_ports[IDE_CONTROL_OFFSET], 1);
#if defined(CONFIG_AMIGA) || defined(CONFIG_MAC)
	if (hwif->io_ports[IDE_IRQ_OFFSET])
		addr_errs += hwif_check_region(hwif->io_ports[IDE_IRQ_OFFSET], 1);
#endif /* (CONFIG_AMIGA) || (CONFIG_MAC) */
	/* If any errors are return, we drop the hwif interface. */
	hwif->straight8 = 0;
	return(addr_errs);
}

//EXPORT_SYMBOL(hwif_check_regions);

#define hwif_request_region(addr, num, name)	\
	((hwif->mmio) ? request_mem_region((addr),(num),(name)) : request_region((addr),(num),(name)))

static void hwif_register (ide_hwif_t *hwif)
{
	u32 i = 0;

	/* register with global device tree */
	strncpy(hwif->gendev.bus_id,hwif->name,BUS_ID_SIZE);
	snprintf(hwif->gendev.name,DEVICE_NAME_SIZE,"IDE Controller");
	device_register(&hwif->gendev);

	if (hwif->mmio == 2)
		return;
	if (hwif->io_ports[IDE_CONTROL_OFFSET])
		hwif_request_region(hwif->io_ports[IDE_CONTROL_OFFSET], 1, hwif->name);
#if defined(CONFIG_AMIGA) || defined(CONFIG_MAC)
	if (hwif->io_ports[IDE_IRQ_OFFSET])
		hwif_request_region(hwif->io_ports[IDE_IRQ_OFFSET], 1, hwif->name);
#endif /* (CONFIG_AMIGA) || (CONFIG_MAC) */
	if (((unsigned long)hwif->io_ports[IDE_DATA_OFFSET] | 7) ==
	    ((unsigned long)hwif->io_ports[IDE_STATUS_OFFSET])) {
		hwif_request_region(hwif->io_ports[IDE_DATA_OFFSET], 8, hwif->name);
		hwif->straight8 = 1;
		return;
	}

	for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; i++)
		hwif_request_region(hwif->io_ports[i], 1, hwif->name);
}

//EXPORT_SYMBOL(hwif_register);

/*
 * This routine only knows how to look for drive units 0 and 1
 * on an interface, so any setting of MAX_DRIVES > 2 won't work here.
 */
void probe_hwif (ide_hwif_t *hwif)
{
	unsigned int unit;
	unsigned long flags;
	unsigned int irqd;

	if (hwif->noprobe)
		return;
#ifdef CONFIG_BLK_DEV_IDE
	if (hwif->io_ports[IDE_DATA_OFFSET] == HD_DATA) {
		extern void probe_cmos_for_drives(ide_hwif_t *);
		probe_cmos_for_drives(hwif);
	}
#endif

	if ((hwif->chipset != ide_4drives || !hwif->mate->present) &&
#if CONFIG_BLK_DEV_PDC4030
	    (hwif->chipset != ide_pdc4030 || hwif->channel == 0) &&
#endif /* CONFIG_BLK_DEV_PDC4030 */
	    (hwif_check_regions(hwif))) {
		u16 msgout = 0;
		for (unit = 0; unit < MAX_DRIVES; ++unit) {
			ide_drive_t *drive = &hwif->drives[unit];
			if (drive->present) {
				drive->present = 0;
				printk("%s: ERROR, PORTS ALREADY IN USE\n",
					drive->name);
				msgout = 1;
			}
		}
		if (!msgout)
			printk("%s: ports already in use, skipping probe\n",
				hwif->name);
		return;	
	}

	/*
	 * We must always disable IRQ, as probe_for_drive will assert IRQ, but
	 * we'll install our IRQ driver much later...
	 */
	irqd = hwif->irq;
	if (irqd)
		disable_irq(hwif->irq);

	local_irq_set(flags);
	/*
	 * Second drive should only exist if first drive was found,
	 * but a lot of cdrom drives are configured as single slaves.
	 */
	for (unit = 0; unit < MAX_DRIVES; ++unit) {
		ide_drive_t *drive = &hwif->drives[unit];
		drive->dn = ((hwif->channel ? 2 : 0) + unit);
		hwif->drives[unit].dn = ((hwif->channel ? 2 : 0) + unit);
		(void) probe_for_drive(drive);
		if (drive->present && !hwif->present) {
			hwif->present = 1;
			if (hwif->chipset != ide_4drives ||
			    !hwif->mate->present) {
				hwif_register(hwif);
			}
		}
	}
	if (hwif->io_ports[IDE_CONTROL_OFFSET] && hwif->reset) {
		unsigned long timeout = jiffies + WAIT_WORSTCASE;
		u8 stat;

		printk("%s: reset\n", hwif->name);
		hwif->OUTB(12, hwif->io_ports[IDE_CONTROL_OFFSET]);
		udelay(10);
		hwif->OUTB(8, hwif->io_ports[IDE_CONTROL_OFFSET]);
		do {
			ide_delay_50ms();
			stat = hwif->INB(hwif->io_ports[IDE_STATUS_OFFSET]);
		} while ((stat & BUSY_STAT) && time_after(timeout, jiffies));

	}
	local_irq_restore(flags);
	/*
	 * Use cached IRQ number. It might be (and is...) changed by probe
	 * code above
	 */
	if (irqd)
		enable_irq(irqd);

	for (unit = 0; unit < MAX_DRIVES; ++unit) {
		ide_drive_t *drive = &hwif->drives[unit];
		if (drive->present) {
			if (hwif->tuneproc != NULL && drive->autotune == 1)
				/* auto-tune PIO mode */
				hwif->tuneproc(drive, 255);
			/*
			 * MAJOR HACK BARF :-/
			 *
			 * FIXME: chipsets own this cruft!
			 */
			/*
			 * Move here to prevent module loading clashing.
			 */
	//		drive->autodma = hwif->autodma;
			if ((drive->autotune != 2) && (hwif->ide_dma_check)) {
				/*
				 * Force DMAing for the beginning of the check.
				 * Some chipsets appear to do interesting
				 * things, if not checked and cleared.
				 *   PARANOIA!!!
				 */
				hwif->ide_dma_off_quietly(drive);
				hwif->ide_dma_check(drive);
			}
		}
	}
}

EXPORT_SYMBOL(probe_hwif);

int hwif_init (ide_hwif_t *hwif);
int probe_hwif_init (ide_hwif_t *hwif)
{
	hwif->initializing = 1;
	probe_hwif(hwif);
	hwif_init(hwif);

#if 1
	if (hwif->present) {
		u16 unit = 0;
		for (unit = 0; unit < MAX_DRIVES; ++unit) {
			ide_drive_t *drive = &hwif->drives[unit];
			if (drive->present) {
				ata_attach(drive);
			}
		}
	}
#endif
	hwif->initializing = 0;
	return 0;
}

EXPORT_SYMBOL(probe_hwif_init);

#if MAX_HWIFS > 1
/*
 * save_match() is used to simplify logic in init_irq() below.
 *
 * A loophole here is that we may not know about a particular
 * hwif's irq until after that hwif is actually probed/initialized..
 * This could be a problem for the case where an hwif is on a
 * dual interface that requires serialization (eg. cmd640) and another
 * hwif using one of the same irqs is initialized beforehand.
 *
 * This routine detects and reports such situations, but does not fix them.
 */
void save_match (ide_hwif_t *hwif, ide_hwif_t *new, ide_hwif_t **match)
{
	ide_hwif_t *m = *match;

	if (m && m->hwgroup && m->hwgroup != new->hwgroup) {
		if (!new->hwgroup)
			return;
		printk("%s: potential irq problem with %s and %s\n",
			hwif->name, new->name, m->name);
	}
	if (!m || m->irq != hwif->irq) /* don't undo a prior perfect match */
		*match = new;
}
EXPORT_SYMBOL(save_match);
#endif /* MAX_HWIFS > 1 */

/*
 * init request queue
 */
static void ide_init_queue(ide_drive_t *drive)
{
	request_queue_t *q = &drive->queue;
	int max_sectors;

	q->queuedata = HWGROUP(drive);
	blk_init_queue(q, do_ide_request, &ide_lock);
	blk_queue_segment_boundary(q, 0xffff);

#ifdef CONFIG_BLK_DEV_PDC4030
	max_sectors = 127;
#else
	max_sectors = 255;
#endif
	blk_queue_max_sectors(q, max_sectors);

	/* IDE DMA can do PRD_ENTRIES number of segments. */
	blk_queue_max_hw_segments(q, PRD_ENTRIES);

	/* This is a driver limit and could be eliminated. */
	blk_queue_max_phys_segments(q, PRD_ENTRIES);

	ide_toggle_bounce(drive, 1);
}

/*
 * This routine sets up the irq for an ide interface, and creates a new
 * hwgroup for the irq/hwif if none was previously assigned.
 *
 * Much of the code is for correctly detecting/handling irq sharing
 * and irq serialization situations.  This is somewhat complex because
 * it handles static as well as dynamic (PCMCIA) IDE interfaces.
 *
 * The SA_INTERRUPT in sa_flags means ide_intr() is always entered with
 * interrupts completely disabled.  This can be bad for interrupt latency,
 * but anything else has led to problems on some machines.  We re-enable
 * interrupts as much as we can safely do in most places.
 */
int init_irq (ide_hwif_t *hwif)
{
	unsigned long flags;
	unsigned int index;
	ide_hwgroup_t *hwgroup, *new_hwgroup;
	ide_hwif_t *match = NULL;

#if 0
	/* Allocate the buffer and no sleep allowed */
	new_hwgroup = kmalloc(sizeof(ide_hwgroup_t),GFP_ATOMIC);
#else
	/* Allocate the buffer and potentially sleep first */
	new_hwgroup = kmalloc(sizeof(ide_hwgroup_t),GFP_KERNEL);
#endif
	
	spin_lock_irqsave(&ide_lock, flags);

	hwif->hwgroup = NULL;
#if MAX_HWIFS > 1
	/*
	 * Group up with any other hwifs that share our irq(s).
	 */
	for (index = 0; index < MAX_HWIFS; index++) {
		ide_hwif_t *h = &ide_hwifs[index];
		if (h->hwgroup) {  /* scan only initialized hwif's */
			if (hwif->irq == h->irq) {
				hwif->sharing_irq = h->sharing_irq = 1;
				if (hwif->chipset != ide_pci ||
				    h->chipset != ide_pci) {
					save_match(hwif, h, &match);
				}
			}
			if (hwif->serialized) {
				if (hwif->mate && hwif->mate->irq == h->irq)
					save_match(hwif, h, &match);
			}
			if (h->serialized) {
				if (h->mate && hwif->irq == h->mate->irq)
					save_match(hwif, h, &match);
			}
		}
	}
#endif /* MAX_HWIFS > 1 */
	/*
	 * If we are still without a hwgroup, then form a new one
	 */
	if (match) {
		hwgroup = match->hwgroup;
		if(new_hwgroup)
			kfree(new_hwgroup);
	} else {
		hwgroup = new_hwgroup;
		if (!hwgroup) {
			spin_unlock_irqrestore(&ide_lock, flags);
			return 1;
		}
		memset(hwgroup, 0, sizeof(ide_hwgroup_t));
		hwgroup->hwif     = hwif->next = hwif;
		hwgroup->rq       = NULL;
		hwgroup->handler  = NULL;
		hwgroup->drive    = NULL;
		hwgroup->busy     = 0;
		init_timer(&hwgroup->timer);
		hwgroup->timer.function = &ide_timer_expiry;
		hwgroup->timer.data = (unsigned long) hwgroup;
	}

	/*
	 * Allocate the irq, if not already obtained for another hwif
	 */
	if (!match || match->irq != hwif->irq) {
		int sa = SA_INTERRUPT;
#if defined(__mc68000__) || defined(CONFIG_APUS)
		sa = SA_SHIRQ;
#endif /* __mc68000__ || CONFIG_APUS */

		if (IDE_CHIPSET_IS_PCI(hwif->chipset)) {
			sa = SA_SHIRQ;
#ifndef CONFIG_IDEPCI_SHARE_IRQ
			sa |= SA_INTERRUPT;
#endif /* CONFIG_IDEPCI_SHARE_IRQ */
		}

		if (hwif->io_ports[IDE_CONTROL_OFFSET])
			/* clear nIEN */
			hwif->OUTB(0x08, hwif->io_ports[IDE_CONTROL_OFFSET]);

		if (request_irq(hwif->irq,&ide_intr,sa,hwif->name,hwgroup)) {
			if (!match)
				kfree(hwgroup);
			spin_unlock_irqrestore(&ide_lock, flags);
			return 1;
		}
	}

	/*
	 * Everything is okay, so link us into the hwgroup
	 */
	hwif->hwgroup = hwgroup;
	hwif->next = hwgroup->hwif->next;
	hwgroup->hwif->next = hwif;

	for (index = 0; index < MAX_DRIVES; ++index) {
		ide_drive_t *drive = &hwif->drives[index];
		if (!drive->present)
			continue;
		if (!hwgroup->drive)
			hwgroup->drive = drive;
		drive->next = hwgroup->drive->next;
		hwgroup->drive->next = drive;
		ide_init_queue(drive);
	}
	if (!hwgroup->hwif) {
		hwgroup->hwif = HWIF(hwgroup->drive);
#ifdef DEBUG
		printk("%s : Adding missed hwif to hwgroup!!\n", hwif->name);
#endif
	}

	/* all CPUs; safe now that hwif->hwgroup is set up */
	spin_unlock_irqrestore(&ide_lock, flags);

#if !defined(__mc68000__) && !defined(CONFIG_APUS) && !defined(__sparc__)
	printk("%s at 0x%03lx-0x%03lx,0x%03lx on irq %d", hwif->name,
		hwif->io_ports[IDE_DATA_OFFSET],
		hwif->io_ports[IDE_DATA_OFFSET]+7,
		hwif->io_ports[IDE_CONTROL_OFFSET], hwif->irq);
#elif defined(__sparc__)
	printk("%s at 0x%03lx-0x%03lx,0x%03lx on irq %s", hwif->name,
		hwif->io_ports[IDE_DATA_OFFSET],
		hwif->io_ports[IDE_DATA_OFFSET]+7,
		hwif->io_ports[IDE_CONTROL_OFFSET], __irq_itoa(hwif->irq));
#else
	printk("%s at %x on irq 0x%08x", hwif->name,
		hwif->io_ports[IDE_DATA_OFFSET], hwif->irq);
#endif /* __mc68000__ && CONFIG_APUS */
	if (match)
		printk(" (%sed with %s)",
			hwif->sharing_irq ? "shar" : "serializ", match->name);
	printk("\n");
	return 0;
}

EXPORT_SYMBOL(init_irq);

/*
 * init_gendisk() (as opposed to ide_geninit) is called for each major device,
 * after probing for drives, to allocate partition tables and other data
 * structures needed for the routines in genhd.c.  ide_geninit() gets called
 * somewhat later, during the partition check.
 */
static void init_gendisk (ide_hwif_t *hwif)
{
	struct gendisk *gd;
	unsigned int unit, units, minors;
	extern devfs_handle_t ide_devfs_handle;
	char *names;

	units = MAX_DRIVES;

	minors    = units * (1<<PARTN_BITS);
	gd        = kmalloc(MAX_DRIVES * sizeof(struct gendisk), GFP_KERNEL);
	if (!gd)
		goto err_kmalloc_gd;
	memset(gd, 0, MAX_DRIVES * sizeof(struct gendisk));

	names = kmalloc(4 * MAX_DRIVES, GFP_KERNEL);
	if (!names)
		goto err_kmalloc_gd_names;
	memset(names, 0, 4 * MAX_DRIVES);

	for (unit = 0; unit < units; ++unit) {
		gd[unit].major  = hwif->major;
		gd[unit].first_minor = unit << PARTN_BITS;
		sprintf(names + 4*unit, "hd%c",'a'+hwif->index*MAX_DRIVES+unit);
		gd[unit].major_name = names + 4*unit;
		gd[unit].minor_shift = PARTN_BITS; 
		gd[unit].fops = ide_fops;

		snprintf(gd[unit].disk_dev.bus_id,BUS_ID_SIZE,"%u.%u",
			 hwif->index,unit);
		snprintf(gd[unit].disk_dev.name,DEVICE_NAME_SIZE,
			 "%s","IDE Drive");
		gd[unit].disk_dev.parent = &hwif->gendev;
		gd[unit].disk_dev.bus = &ide_bus_type;
		device_register(&gd[unit].disk_dev);

		hwif->drives[unit].disk = gd + unit;
	}

	for (unit = 0; unit < units; ++unit) {
		char name[64];
		ide_add_generic_settings(hwif->drives + unit);
		sprintf (name, "host%d/bus%d/target%d/lun%d",
			(hwif->channel && hwif->mate) ?
			hwif->mate->index : hwif->index,
			hwif->channel, unit, hwif->drives[unit].lun);
		if (hwif->drives[unit].present)
			hwif->drives[unit].de = devfs_mk_dir(ide_devfs_handle, name, NULL);
	}
	return;

err_kmalloc_gd_names:
	kfree(gd);
err_kmalloc_gd:
	printk(KERN_WARNING "(ide::init_gendisk) Out of memory\n");
}

EXPORT_SYMBOL(init_gendisk);

int hwif_init (ide_hwif_t *hwif)
{
	if (!hwif->present)
		return 0;
	if (!hwif->irq) {
		if (!(hwif->irq = ide_default_irq(hwif->io_ports[IDE_DATA_OFFSET])))
		{
			printk("%s: DISABLED, NO IRQ\n", hwif->name);
			return (hwif->present = 0);
		}
	}
#ifdef CONFIG_BLK_DEV_HD
	if (hwif->irq == HD_IRQ && hwif->io_ports[IDE_DATA_OFFSET] != HD_DATA) {
		printk("%s: CANNOT SHARE IRQ WITH OLD "
			"HARDDISK DRIVER (hd.c)\n", hwif->name);
		return (hwif->present = 0);
	}
#endif /* CONFIG_BLK_DEV_HD */

	/* we set it back to 1 if all is ok below */	
	hwif->present = 0;

	if (register_blkdev (hwif->major, hwif->name, ide_fops)) {
		printk("%s: UNABLE TO GET MAJOR NUMBER %d\n",
			hwif->name, hwif->major);
		return (hwif->present = 0);
	}
	
	if (init_irq(hwif)) {
		int i = hwif->irq;
		/*
		 *	It failed to initialise. Find the default IRQ for 
		 *	this port and try that.
		 */
		if (!(hwif->irq = ide_default_irq(hwif->io_ports[IDE_DATA_OFFSET]))) {
			printk("%s: Disabled unable to get IRQ %d.\n",
				hwif->name, i);
			(void) unregister_blkdev(hwif->major, hwif->name);
			return (hwif->present = 0);
		}
		if (init_irq(hwif)) {
			printk("%s: probed IRQ %d and default IRQ %d failed.\n",
				hwif->name, i, hwif->irq);
			(void) unregister_blkdev(hwif->major, hwif->name);
			return (hwif->present = 0);
		}
		printk("%s: probed IRQ %d failed, using default.\n",
			hwif->name, hwif->irq);
	}
	
	init_gendisk(hwif);
	blk_dev[hwif->major].data = hwif;
	blk_dev[hwif->major].queue = ide_get_queue;
	hwif->present = 1;	/* success */
	return 1;
}

EXPORT_SYMBOL(hwif_init);

void export_ide_init_queue (ide_drive_t *drive)
{
	ide_init_queue(drive);
}

EXPORT_SYMBOL(export_ide_init_queue);

u8 export_probe_for_drive (ide_drive_t *drive)
{
	return probe_for_drive(drive);
}

EXPORT_SYMBOL(export_probe_for_drive);

int ideprobe_init (void);
static ide_module_t ideprobe_module = {
	IDE_PROBE_MODULE,
	ideprobe_init,
	NULL
};

int ideprobe_init (void)
{
	unsigned int index;
	int probe[MAX_HWIFS];

	MOD_INC_USE_COUNT;
	memset(probe, 0, MAX_HWIFS * sizeof(int));
	for (index = 0; index < MAX_HWIFS; ++index)
		probe[index] = !ide_hwifs[index].present;

	/*
	 * Probe for drives in the usual way.. CMOS/BIOS, then poke at ports
	 */
	for (index = 0; index < MAX_HWIFS; ++index)
		if (probe[index])
			probe_hwif(&ide_hwifs[index]);
	for (index = 0; index < MAX_HWIFS; ++index)
		if (probe[index])
			hwif_init(&ide_hwifs[index]);
	for (index = 0; index < MAX_HWIFS; ++index) {
		if (probe[index]) {
			ide_hwif_t *hwif = &ide_hwifs[index];
			int unit;
			if (!hwif->present)
				continue;
			for (unit = 0; unit < MAX_DRIVES; ++unit)
				ata_attach(&hwif->drives[unit]);
		}
	}
	if (!ide_probe)
		ide_probe = &ideprobe_module;
	MOD_DEC_USE_COUNT;
	return 0;
}

#ifdef MODULE
extern int (*ide_xlate_1024_hook)(kdev_t, int, int, const char *);

int init_module (void)
{
	unsigned int index;
	
	for (index = 0; index < MAX_HWIFS; ++index)
		ide_unregister(index);
	ideprobe_init();
	create_proc_ide_interfaces();
	ide_xlate_1024_hook = ide_xlate_1024;
	return 0;
}

void cleanup_module (void)
{
	ide_probe = NULL;
	ide_xlate_1024_hook = 0;
}
MODULE_LICENSE("GPL");
#endif /* MODULE */
