/*  -*- linux-c -*-
 *
 *  Copyright (C) 1995-2002  Linus Torvalds & authors (see below)
 *
 *  Principal Author/Maintainer:  peterd@pnd-pc.demon.co.uk
 *
 *  This file provides support for the second port and cache of Promise
 *  VLB based IDE interfaces, e.g. DC4030VL, DC4030VL-1 and DC4030VL-2.
 *
 *  Thanks are due to Mark Lord for advice and patiently answering stupid
 *  questions, and all those mugs^H^H^H^Hbrave souls who've tested this,
 *  especially Andre Hedrick.
 *
 *  Version 0.01	Initial version, #include'd in ide.c rather than
 *                      compiled separately.
 *                      Reads use Promise commands, writes as before. Drives
 *                      on second channel are read-only.
 *  Version 0.02        Writes working on second channel, reads on both
 *                      channels. Writes fail under high load. Suspect
 *			transfers of >127 sectors don't work.
 *  Version 0.03        Brought into line with ide.c version 5.27.
 *                      Other minor changes.
 *  Version 0.04        Updated for ide.c version 5.30
 *                      Changed initialization strategy
 *  Version 0.05	Kernel integration.  -ml
 *  Version 0.06	Ooops. Add hwgroup to direct call of ide_intr() -ml
 *  Version 0.07	Added support for DC4030 variants
 *			Secondary interface autodetection
 *  Version 0.08	Renamed to pdc4030.c
 *  Version 0.09	Obsolete - never released - did manual write request
 *			splitting before max_sectors[major][minor] available.
 *  Version 0.10	Updated for 2.1 series of kernels
 *  Version 0.11	Updated for 2.3 series of kernels
 *			Autodetection code added.
 *
 *  Version 0.90	Transition to BETA code. No lost/unexpected interrupts
 *  Version 0.91	Bring in line with new bio code in 2.5.1
 *  Version 0.92	Update for IDE driver taskfile changes
 *  Version 0.93	Sync with 2.5.10, minor taskfile changes
 */

/*
 * Once you've compiled it in, you'll have to also enable the interface
 * setup routine from the kernel command line, as in
 *
 *	'linux ide0=dc4030' or 'linux ide1=dc4030'
 *
 * It should now work as a second controller also ('ide1=dc4030') but only
 * if you DON'T have BIOS V4.44, which has a bug. If you have this version
 * and EPROM programming facilities, you need to fix 4 bytes:
 *	2496:	81	81
 *	2497:	3E	3E
 *	2498:	22	98	*
 *	2499:	06	05	*
 *	249A:	F0	F0
 *	249B:	01	01
 *	...
 *	24A7:	81	81
 *	24A8:	3E	3E
 *	24A9:	22	98	*
 *	24AA:	06	05	*
 *	24AB:	70	70
 *	24AC:	01	01
 *
 * As of January 1999, Promise Technology Inc. have finally supplied me with
 * some technical information which has shed a glimmer of light on some of the
 * problems I was having, especially with writes.
 *
 * There are still potential problems with the robustness and efficiency of
 * this driver because I still don't understand what the card is doing with
 * interrupts, however, it has been stable for a while with no reports of ill
 * effects.
 */

#undef DEBUG_READ
#undef DEBUG_WRITE

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/ide.h>
#include <linux/init.h>

#include <asm/io.h>
#include <asm/irq.h>

#include "pdc4030.h"

/*
 * Data transfer functions for polled IO.
 */

/*
 * Some localbus EIDE interfaces require a special access sequence
 * when using 32-bit I/O instructions to transfer data.  We call this
 * the "vlb_sync" sequence, which consists of three successive reads
 * of the sector count register location, with interrupts disabled
 * to ensure that the reads all happen together.
 */
static void read_vlb(struct ata_device *drive, void *buffer, unsigned int wcount)
{
	unsigned long flags;

	__save_flags(flags);	/* local CPU only */
	__cli();		/* local CPU only */
	inb(IDE_NSECTOR_REG);
	inb(IDE_NSECTOR_REG);
	inb(IDE_NSECTOR_REG);
	insl(IDE_DATA_REG, buffer, wcount);
	__restore_flags(flags);	/* local CPU only */
}

static void write_vlb(struct ata_device *drive, void *buffer, unsigned int wcount)
{
	unsigned long flags;

	__save_flags(flags);	/* local CPU only */
	__cli();		/* local CPU only */
	inb(IDE_NSECTOR_REG);
	inb(IDE_NSECTOR_REG);
	inb(IDE_NSECTOR_REG);
	outsl(IDE_DATA_REG, buffer, wcount);
	__restore_flags(flags);	/* local CPU only */
}

static void read_16(struct ata_device *drive, void *buffer, unsigned int wcount)
{
	insw(IDE_DATA_REG, buffer, wcount<<1);
}

static void write_16(struct ata_device *drive, void *buffer, unsigned int wcount)
{
	outsw(IDE_DATA_REG, buffer, wcount<<1);
}

/*
 * This is used for most PIO data transfers *from* the device.
 */
static void promise_read(struct ata_device *drive, void *buffer, unsigned int wcount)
{
	if (drive->channel->io_32bit)
		read_vlb(drive, buffer, wcount);
	else
		read_16(drive, buffer, wcount);
}

/*
 * This is used for most PIO data transfers *to* the device interface.
 */
static void promise_write(struct ata_device *drive, void *buffer, unsigned int wcount)
{
	if (drive->channel->io_32bit)
		write_vlb(drive, buffer, wcount);
	else
		write_16(drive, buffer, wcount);
}

/*
 * promise_selectproc() is invoked by ide.c
 * in preparation for access to the specified drive.
 */
static void promise_selectproc(struct ata_device *drive)
{
	u8 number;

	number = (drive->channel->unit << 1) + drive->select.b.unit;
	outb(number, IDE_FEATURE_REG);
}

/*
 * pdc4030_cmd handles the set of vendor specific commands that are initiated
 * by command F0. They all have the same success/failure notification -
 * 'P' (=0x50) on success, 'p' (=0x70) on failure.
 */
int pdc4030_cmd(struct ata_device *drive, byte cmd)
{
	unsigned long timeout, timer;
	byte status_val;

	promise_selectproc(drive);	/* redundant? */
	outb(0xF3, IDE_SECTOR_REG);
	outb(cmd, IDE_SELECT_REG);
	outb(PROMISE_EXTENDED_COMMAND, IDE_COMMAND_REG);
	timeout = HZ * 10;
	timeout += jiffies;
	do {
		if(time_after(jiffies, timeout)) {
			return 2; /* device timed out */
		}
		/* This is out of delay_10ms() */
		/* Delays at least 10ms to give interface a chance */
		timer = jiffies + (HZ + 99)/100 + 1;
		while (time_after(timer, jiffies));
		status_val = inb(IDE_SECTOR_REG);
	} while (status_val != 0x50 && status_val != 0x70);

	if(status_val == 0x50)
		return 0; /* device returned success */
	else
		return 1; /* device returned failure */
}

/*
 * pdc4030_identify sends a vendor-specific IDENTIFY command to the drive
 */
int pdc4030_identify(struct ata_device *drive)
{
	return pdc4030_cmd(drive, PROMISE_IDENTIFY);
}

int enable_promise_support = 0;

void __init init_pdc4030 (void)
{
	enable_promise_support = 1;
}

/*
 * setup_pdc4030()
 * Completes the setup of a Promise DC4030 controller card, once found.
 */
int __init setup_pdc4030(struct ata_channel *hwif)
{
        struct ata_device *drive;
	struct ata_channel *hwif2;
	struct dc_ident ident;
	int i;
	ide_startstop_t ret;

	if (!hwif)
		return 0;

	drive = &hwif->drives[0];
	hwif2 = &ide_hwifs[hwif->index+1];
	if (hwif->chipset == ide_pdc4030) /* we've already been found ! */
		return 1;

	if (inb(IDE_NSECTOR_REG) == 0xFF || inb(IDE_SECTOR_REG) == 0xFF) {
		return 0;
	}
	ata_irq_enable(drive, 1);
	if (pdc4030_cmd(drive, PROMISE_GET_CONFIG)) {
		return 0;
	}

	/* FIXME: Make this go away. */
	spin_lock_irq(hwif->lock);
	ret = ata_status_poll(drive, DATA_READY, BAD_W_STAT, WAIT_DRQ, NULL);
	if (ret != ATA_OP_READY) {
		printk(KERN_INFO
			"%s: Failed Promise read config!\n",hwif->name);
		spin_unlock_irq(hwif->lock);

		return 0;
	}
	spin_unlock_irq(hwif->lock);

	promise_read(drive, &ident, SECTOR_WORDS);
	if (ident.id[1] != 'P' || ident.id[0] != 'T') {
		return 0;
	}
	printk(KERN_INFO "%s: Promise caching controller, ",hwif->name);
	switch(ident.type) {
		case 0x43:	printk("DC4030VL-2, "); break;
		case 0x41:	printk("DC4030VL-1, "); break;
		case 0x40:	printk("DC4030VL, "); break;
		default:
			printk("unknown - type 0x%02x - please report!\n"
			       ,ident.type);
			printk("Please e-mail the following data to "
			       "promise@pnd-pc.demon.co.uk along with\n"
			       "a description of your card and drives:\n");
			for (i=0; i < 0x90; i++) {
				printk("%02x ", ((unsigned char *)&ident)[i]);
				if ((i & 0x0f) == 0x0f) printk("\n");
			}
			return 0;
	}
	printk("%dKB cache, ",(int)ident.cache_mem);
	switch(ident.irq) {
            case 0x00: hwif->irq = 14; break;
            case 0x01: hwif->irq = 12; break;
            default:   hwif->irq = 15; break;
	}
	printk("on IRQ %d\n",hwif->irq);

	/*
	 * Once found and identified, we set up the next hwif in the array
	 * (hwif2 = ide_hwifs[hwif->index+1]) with the same io ports, irq
	 * and other settings as the main hwif. This gives us two "mated"
	 * hwifs pointing to the Promise card.
	 *
	 * We also have to shift the default values for the remaining
	 * interfaces "up by one" to make room for the second interface on the
	 * same set of values.
	 */

	hwif->chipset	= hwif2->chipset = ide_pdc4030;
	hwif->unit	= ATA_PRIMARY;
	hwif2->unit	= ATA_SECONDARY;
	hwif->ata_read  = hwif2->ata_read = promise_read;
	hwif->ata_write = hwif2->ata_write = promise_write;
	hwif->selectproc = hwif2->selectproc = promise_selectproc;
	hwif->serialized = hwif2->serialized = 1;

/* Shift the remaining interfaces up by one */
	for (i=MAX_HWIFS-1 ; i > hwif->index+1 ; i--) {
		struct ata_channel *h = &ide_hwifs[i];

#ifdef DEBUG
		printk(KERN_DEBUG "pdc4030: Shifting i/f %d values to i/f %d\n",i-1,i);
#endif
		ide_init_hwif_ports(&h->hw, (h-1)->io_ports[IDE_DATA_OFFSET], 0, NULL);
		memcpy(h->io_ports, h->hw.io_ports, sizeof(h->io_ports));
		h->noprobe = (h-1)->noprobe;
	}
	ide_init_hwif_ports(&hwif2->hw, hwif->io_ports[IDE_DATA_OFFSET], 0, NULL);
	memcpy(hwif2->io_ports, hwif->hw.io_ports, sizeof(hwif2->io_ports));
	hwif2->irq = hwif->irq;
	hwif2->hw.irq = hwif->hw.irq = hwif->irq;
	hwif->io_32bit = hwif2->io_32bit = 1;
	for (i=0; i<2 ; i++) {
		if (!ident.current_tm[i].cyl)
			hwif->drives[i].noprobe = 1;
		if (!ident.current_tm[i+2].cyl)
			hwif2->drives[i].noprobe = 1;
	}
        return 1;
}

/*
 * detect_pdc4030()
 * Tests for the presence of a DC4030 Promise card on this interface
 * Returns: 1 if found, 0 if not found
 */
int __init detect_pdc4030(struct ata_channel *hwif)
{
	struct ata_device *drive = &hwif->drives[0];

	if (IDE_DATA_REG == 0) { /* Skip test for non-existent interface */
		return 0;
	}
	outb(0xF3, IDE_SECTOR_REG);
	outb(0x14, IDE_SELECT_REG);
	outb(PROMISE_EXTENDED_COMMAND, IDE_COMMAND_REG);

	mdelay(50);

	if (inb(IDE_ERROR_REG) == 'P' &&
	    inb(IDE_NSECTOR_REG) == 'T' &&
	    inb(IDE_SECTOR_REG) == 'I') {
		return 1;
	} else {
		return 0;
	}
}

void __init ide_probe_for_pdc4030(void)
{
	unsigned int	index;
	struct ata_channel *hwif;

	if (enable_promise_support == 0)
		return;
	for (index = 0; index < MAX_HWIFS; index++) {
		hwif = &ide_hwifs[index];
		if (hwif->chipset == ide_unknown && detect_pdc4030(hwif)) {
			setup_pdc4030(hwif);
		}
	}
}

/*
 * promise_read_intr() is the handler for disk read/multread interrupts
 */
static ide_startstop_t promise_read_intr(struct ata_device *drive, struct request *rq)
{
	int total_remaining;
	unsigned int sectors_left, sectors_avail, nsect;
	unsigned long flags;
	char *to;

	if (!ata_status(drive, DATA_READY, BAD_R_STAT))
		return ata_error(drive, rq, __FUNCTION__);

read_again:
	do {
		sectors_left = inb(IDE_NSECTOR_REG);
		inb(IDE_SECTOR_REG);
	} while (inb(IDE_NSECTOR_REG) != sectors_left);
	sectors_avail = rq->nr_sectors - sectors_left;
	if (!sectors_avail)
		goto read_again;

read_next:
	nsect = rq->current_nr_sectors;
	if (nsect > sectors_avail)
		nsect = sectors_avail;
	sectors_avail -= nsect;
	to = bio_kmap_irq(rq->bio, &flags) + ide_rq_offset(rq);
	promise_read(drive, to, nsect * SECTOR_WORDS);
#ifdef DEBUG_READ
	printk(KERN_DEBUG "%s:  promise_read: sectors(%ld-%ld), "
	       "buf=0x%08lx, rem=%ld\n", drive->name, rq->sector,
	       rq->sector+nsect-1, (unsigned long) to, rq->nr_sectors-nsect);
#endif
	bio_kunmap_irq(to, &flags);
	rq->sector += nsect;
	rq->errors = 0;
	rq->nr_sectors -= nsect;
	total_remaining = rq->nr_sectors;
	if ((rq->current_nr_sectors -= nsect) <= 0)
		__ata_end_request(drive, rq, 1, 0);

	/*
	 * Now the data has been read in, do the following:
	 *
	 * if there are still sectors left in the request, if we know there are
	 * still sectors available from the interface, go back and read the
	 * next bit of the request.  else if DRQ is asserted, there are more
	 * sectors available, so go back and find out how many, then read them
	 * in.  else if BUSY is asserted, we are going to get an interrupt, so
	 * set the handler for the interrupt and just return
	 */

	if (total_remaining > 0) {
		if (sectors_avail)
			goto read_next;
		ata_status(drive, 0, 0);
		if (drive->status & DRQ_STAT)
			goto read_again;
		if (drive->status & BUSY_STAT) {
			ata_set_handler(drive, promise_read_intr, WAIT_CMD, NULL);
#ifdef DEBUG_READ
			printk(KERN_DEBUG "%s: promise_read: waiting for"
			       "interrupt\n", drive->name);
#endif
			return ATA_OP_CONTINUES;
		}
		printk(KERN_ERR "%s: Eeek! promise_read_intr: sectors left "
		       "!DRQ !BUSY\n", drive->name);
		return ata_error(drive, rq, "promise read intr");
	}
	return ATA_OP_FINISHED;
}

/*
 * promise_complete_pollfunc()
 * This is the polling function for waiting (nicely!) until drive stops
 * being busy. It is invoked at the end of a write, after the previous poll
 * has finished.
 *
 * Once not busy, the end request is called.
 */
static ide_startstop_t promise_complete_pollfunc(struct ata_device *drive, struct request *rq)
{
	struct ata_channel *ch = drive->channel;

	if (!ata_status(drive, 0, BUSY_STAT)) {
		if (time_before(jiffies, ch->poll_timeout)) {
			ata_set_handler(drive, promise_complete_pollfunc, HZ/100, NULL);

			return ATA_OP_CONTINUES; /* continue polling... */
		}
		ch->poll_timeout = 0;
		printk(KERN_ERR "%s: completion timeout - still busy!\n",
		       drive->name);
		return ata_error(drive, rq, "busy timeout");
	}

	ch->poll_timeout = 0;
#ifdef DEBUG_WRITE
	printk(KERN_DEBUG "%s: Write complete - end_request\n", drive->name);
#endif
	__ata_end_request(drive, rq, 1, rq->nr_sectors);

	return ATA_OP_FINISHED;
}

/*
 * promise_multwrite() transfers a block of up to mcount sectors of data
 * to a drive as part of a disk multiple-sector write operation.
 *
 * Returns 0 on success.
 *
 * Note that we may be called from two contexts - the do_rw_disk context
 * and IRQ context. The IRQ can happen any time after we've output the
 * full "mcount" number of sectors, so we must make sure we update the
 * state _before_ we output the final part of the data!
 */
int promise_multwrite(struct ata_device *drive, struct request *rq, unsigned int mcount)
{
	do {
		char *buffer;
		int nsect = rq->current_nr_sectors;
		unsigned long flags;

		if (nsect > mcount)
			nsect = mcount;
		mcount -= nsect;

		buffer = bio_kmap_irq(rq->bio, &flags) + ide_rq_offset(rq);
		rq->sector += nsect;
		rq->nr_sectors -= nsect;
		rq->current_nr_sectors -= nsect;

		/* Do we move to the next bh after this? */
		if (!rq->current_nr_sectors) {
			struct bio *bio = rq->bio->bi_next;

			/* end early early we ran out of requests */
			if (!bio) {
				mcount = 0;
			} else {
				rq->bio = bio;
				rq->current_nr_sectors = bio_sectors(bio);
				rq->hard_cur_sectors = rq->current_nr_sectors;
			}
		}

		/*
		 * Ok, we're all setup for the interrupt
		 * re-entering us on the last transfer.
		 */
		promise_write(drive, buffer, nsect << 7);
		bio_kunmap_irq(buffer, &flags);
	} while (mcount);

	return 0;
}

/*
 * promise_write_pollfunc() is the handler for disk write completion polling.
 */
static ide_startstop_t promise_write_pollfunc(struct ata_device *drive, struct request *rq)
{
	struct ata_channel *ch = drive->channel;

	if (inb(IDE_NSECTOR_REG) != 0) {
		if (time_before(jiffies, ch->poll_timeout)) {
			ata_set_handler(drive, promise_write_pollfunc, HZ/100, NULL);

			return ATA_OP_CONTINUES; /* continue polling... */
		}
		ch->poll_timeout = 0;
		printk(KERN_ERR "%s: write timed out!\n", drive->name);
		ata_status(drive, 0, 0);

		return ata_error(drive, rq, "write timeout");
	}

	/*
	 * Now write out last 4 sectors and poll for not BUSY
	 */
	promise_multwrite(drive, rq, 4);
	ch->poll_timeout = jiffies + WAIT_WORSTCASE;
	ata_set_handler(drive, promise_complete_pollfunc, HZ/100, NULL);
#ifdef DEBUG_WRITE
	printk(KERN_DEBUG "%s: Done last 4 sectors - status = %02x\n",
		drive->name, drive->status);
#endif

	return ATA_OP_CONTINUES;
}

/*
 * This transfers a block of one or more sectors of data to a drive as part of
 * a disk write operation. All but 4 sectors are transferred in the first
 * attempt, then the interface is polled (nicely!) for completion before the
 * final 4 sectors are transferred. There is no interrupt generated on writes
 * (at least on the DC4030VL-2), we just have to poll for NOT BUSY.
 */
static ide_startstop_t promise_do_write(struct ata_device *drive, struct request *rq)
{
	struct ata_channel *ch = drive->channel;

#ifdef DEBUG_WRITE
	printk(KERN_DEBUG "%s: promise_write: sectors(%ld-%ld), "
	       "buffer=%p\n", drive->name, rq->sector,
	       rq->sector + rq->nr_sectors - 1, rq->buffer);
#endif
	/*
	 * If there are more than 4 sectors to transfer, do n-4 then go into
	 * the polling strategy as defined above.
	 */
	if (rq->nr_sectors > 4) {
		if (promise_multwrite(drive, rq, rq->nr_sectors - 4)) {

			return ATA_OP_FINISHED;
		}
		ch->poll_timeout = jiffies + WAIT_WORSTCASE;
		ata_set_handler(drive, promise_write_pollfunc, HZ/100, NULL);

		return ATA_OP_CONTINUES;
	} else {
	/*
	 * There are 4 or fewer sectors to transfer, do them all in one go
	 * and wait for NOT BUSY.
	 */
		if (promise_multwrite(drive, rq, rq->nr_sectors))
			return ATA_OP_FINISHED;

		ch->poll_timeout = jiffies + WAIT_WORSTCASE;
		ata_set_handler(drive, promise_complete_pollfunc, HZ/100, NULL);

#ifdef DEBUG_WRITE
		printk(KERN_DEBUG "%s: promise_write: <= 4 sectors, "
			"status = %02x\n", drive->name, drive->status);
#endif
		return ATA_OP_CONTINUES;
	}
}

/*
 * do_pdc4030_io() is called from promise_do_request, having had the block
 * number already set up. It issues a READ or WRITE command to the Promise
 * controller, assuming LBA has been used to set up the block number.
 */
ide_startstop_t do_pdc4030_io(struct ata_device *drive, struct ata_taskfile *args, struct request *rq)
{
	struct hd_drive_task_hdr *taskfile = &(args->taskfile);
	unsigned long timeout;

	/* Check that it's a regular command. If not, bomb out early. */
	if (!(rq->flags & REQ_CMD)) {
		blk_dump_rq_flags(rq, "pdc4030 bad flags");
		__ata_end_request(drive, rq, 0, 0);

		return ATA_OP_FINISHED;
	}

	ata_irq_enable(drive, 1);
	ata_mask(drive);

	ata_out_regfile(drive, taskfile);

	outb(taskfile->device_head, IDE_SELECT_REG);
	outb(args->cmd, IDE_COMMAND_REG);

	switch (rq_data_dir(rq)) {
	case READ:

		/*
		 * The card's behaviour is odd at this point. If the data is
		 * available, DRQ will be true, and no interrupt will be
		 * generated by the card. If this is the case, we need to call
		 * the "interrupt" handler (promise_read_intr) directly.
		 * Otherwise, if an interrupt is going to occur, bit0 of the
		 * SELECT register will be high, so we can set the handler the
		 * just return and be interrupted.  If neither of these is the
		 * case, we wait for up to 50ms (badly I'm afraid!) until one
		 * of them is.
		 */

		timeout = jiffies + HZ/20; /* 50ms wait */
		do {
			if (!ata_status(drive, 0, DRQ_STAT)) {
				udelay(1);
				return promise_read_intr(drive, rq);
			}
			if (inb(IDE_SELECT_REG) & 0x01) {
#ifdef DEBUG_READ
				printk(KERN_DEBUG "%s: read: waiting for "
				                  "interrupt\n", drive->name);
#endif
				ata_set_handler(drive, promise_read_intr, WAIT_CMD, NULL);

				return ATA_OP_CONTINUES;
			}
			udelay(1);
		} while (time_before(jiffies, timeout));

		printk(KERN_ERR "%s: reading: No DRQ and not waiting - Odd!\n",
			drive->name);
		return ATA_OP_FINISHED;

	case WRITE: {
		ide_startstop_t ret;

		/*
		 * Strategy on write is: look for the DRQ that should have been
		 * immediately asserted copy the request into the hwgroup's
		 * scratchpad call the promise_write function to deal with
		 * writing the data out.
		 *
		 * NOTE: No interrupts are generated on writes. Write
		 * completion must be polled
		 */

		ret = ata_status_poll(drive, DATA_READY, drive->bad_wstat,
					WAIT_DRQ, rq);
		if (ret != ATA_OP_READY) {
			printk(KERN_ERR "%s: no DRQ after issuing "
			       "PROMISE_WRITE\n", drive->name);

			return ret;
		}
		if (!drive->channel->unmask)
			__cli();	/* local CPU only */

		return promise_do_write(drive, rq);
	}

	default:
		printk(KERN_ERR "pdc4030: command not READ or WRITE! Huh?\n");

		__ata_end_request(drive, rq, 0, 0);
		return ATA_OP_FINISHED;
	}
}

ide_startstop_t promise_do_request(struct ata_device *drive, struct request *rq, sector_t block)
{
	struct ata_taskfile args;

	memset(&args, 0, sizeof(args));

	/* The four drives on the two logical (one physical) interfaces
	   are distinguished by writing the drive number (0-3) to the
	   Feature register.
	   FIXME: Is promise_selectproc now redundant??
	*/
	args.taskfile.feature		= (drive->channel->unit << 1) + drive->select.b.unit;
	args.taskfile.sector_count	= rq->nr_sectors;
	args.taskfile.sector_number	= block;
	args.taskfile.low_cylinder	= (block>>=8);
	args.taskfile.high_cylinder	= (block>>=8);
	args.taskfile.device_head	= ((block>>8)&0x0f)|drive->select.all;
	args.cmd = (rq_data_dir(rq) == READ) ? PROMISE_READ : PROMISE_WRITE;
	args.XXX_handler	= NULL;
	rq->special	= &args;

	return do_pdc4030_io(drive, &args, rq);
}
