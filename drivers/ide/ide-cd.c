/*
 * Copyright (C) 1994, 1995, 1996  scott snyder  <snyder@fnald0.fnal.gov>
 * Copyright (C) 1996-1998  Erik Andersen <andersee@debian.org>
 * Copyright (C) 1998-2000  Jens Axboe <axboe@suse.de>
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * ATAPI CD-ROM driver.  To be used with ide.c.
 * See Documentation/cdrom/ide-cd for usage information.
 *
 * Suggestions are welcome. Patches that work are more welcome though. ;-)
 * For those wishing to work on this driver, please be sure you download
 * and comply with the latest Mt. Fuji (SFF8090 version 4) and ATAPI
 * (SFF-8020i rev 2.6) standards. These documents can be obtained by
 * anonymous ftp from:
 *
 * ftp://fission.dt.wdc.com/pub/standards/SFF_atapi/spec/SFF8020-r2.6/PS/8020r26.ps
 * ftp://ftp.avc-pioneer.com/Mtfuji4/Spec/Fuji4r10.pdf
 *
 * Drives that deviate from these standards will be accomodated as much
 * as possible via compile time or command-line options.  Since I only have
 * a few drives, you generally need to send me patches...
 *
 * ----------------------------------
 * TO DO LIST:
 * -Make it so that Pioneer CD DR-A24X and friends don't get screwed up on
 *   boot
 *
 * ----------------------------------
 * 1.00  Oct 31, 1994 -- Initial version.
 * 1.01  Nov  2, 1994 -- Fixed problem with starting request in
 *                       cdrom_check_status.
 * 1.03  Nov 25, 1994 -- leaving unmask_intr[] as a user-setting (as for disks)
 * (from mlord)       -- minor changes to cdrom_setup()
 *                    -- renamed ide_dev_s to ide_drive_t, enable irq on command
 * 2.00  Nov 27, 1994 -- Generalize packet command interface;
 *                       add audio ioctls.
 * 2.01  Dec  3, 1994 -- Rework packet command interface to handle devices
 *                       which send an interrupt when ready for a command.
 * 2.02  Dec 11, 1994 -- Cache the TOC in the driver.
 *                       Don't use SCMD_PLAYAUDIO_TI; it's not included
 *                       in the current version of ATAPI.
 *                       Try to use LBA instead of track or MSF addressing
 *                       when possible.
 *                       Don't wait for READY_STAT.
 * 2.03  Jan 10, 1995 -- Rewrite block read routines to handle block sizes
 *                       other than 2k and to move multiple sectors in a
 *                       single transaction.
 * 2.04  Apr 21, 1995 -- Add work-around for Creative Labs CD220E drives.
 *                       Thanks to Nick Saw <cwsaw@pts7.pts.mot.com> for
 *                       help in figuring this out.  Ditto for Acer and
 *                       Aztech drives, which seem to have the same problem.
 * 2.04b May 30, 1995 -- Fix to match changes in ide.c version 3.16 -ml
 * 2.05  Jun  8, 1995 -- Don't attempt to retry after an illegal request
 *                       or data protect error.
 *                       Use HWIF and DEV_HWIF macros as in ide.c.
 *                       Always try to do a request_sense after
 *                       a failed command.
 *                       Include an option to give textual descriptions
 *                       of ATAPI errors.
 *                       Fix a bug in handling the sector cache which
 *                       showed up if the drive returned data in 512 byte
 *                       blocks (like Pioneer drives).  Thanks to
 *                       Richard Hirst <srh@gpt.co.uk> for diagnosing this.
 *                       Properly supply the page number field in the
 *                       MODE_SELECT command.
 *                       PLAYAUDIO12 is broken on the Aztech; work around it.
 * 2.05x Aug 11, 1995 -- lots of data structure renaming/restructuring in ide.c
 *                       (my apologies to Scott, but now ide-cd.c is independent)
 * 3.00  Aug 22, 1995 -- Implement CDROMMULTISESSION ioctl.
 *                       Implement CDROMREADAUDIO ioctl (UNTESTED).
 *                       Use input_ide_data() and output_ide_data().
 *                       Add door locking.
 *                       Fix usage count leak in cdrom_open, which happened
 *                       when a read-write mount was attempted.
 *                       Try to load the disk on open.
 *                       Implement CDROMEJECT_SW ioctl (off by default).
 *                       Read total cdrom capacity during open.
 *                       Rearrange logic in cdrom_decode_status.  Issue
 *                       request sense commands for failed packet commands
 *                       from here instead of from cdrom_queue_packet_command.
 *                       Fix a race condition in retrieving error information.
 *                       Suppress printing normal unit attention errors and
 *                       some drive not ready errors.
 *                       Implement CDROMVOLREAD ioctl.
 *                       Implement CDROMREADMODE1/2 ioctls.
 *                       Fix race condition in setting up interrupt handlers
 *                       when the `serialize' option is used.
 * 3.01  Sep  2, 1995 -- Fix ordering of reenabling interrupts in
 *                       cdrom_queue_request.
 *                       Another try at using ide_[input,output]_data.
 * 3.02  Sep 16, 1995 -- Stick total disk capacity in partition table as well.
 *                       Make VERBOSE_IDE_CD_ERRORS dump failed command again.
 *                       Dump out more information for ILLEGAL REQUEST errs.
 *                       Fix handling of errors occurring before the
 *                       packet command is transferred.
 *                       Fix transfers with odd bytelengths.
 * 3.03  Oct 27, 1995 -- Some Creative drives have an id of just `CD'.
 *                       `DCI-2S10' drives are broken too.
 * 3.04  Nov 20, 1995 -- So are Vertos drives.
 * 3.05  Dec  1, 1995 -- Changes to go with overhaul of ide.c and ide-tape.c
 * 3.06  Dec 16, 1995 -- Add support needed for partitions.
 *                       More workarounds for Vertos bugs (based on patches
 *                       from Holger Dietze <dietze@aix520.informatik.uni-leipzig.de>).
 *                       Try to eliminate byteorder assumptions.
 *                       Use atapi_cdrom_subchnl struct definition.
 *                       Add STANDARD_ATAPI compilation option.
 * 3.07  Jan 29, 1996 -- More twiddling for broken drives: Sony 55D,
 *                       Vertos 300.
 *                       Add NO_DOOR_LOCKING configuration option.
 *                       Handle drive_cmd requests w/NULL args (for hdparm -t).
 *                       Work around sporadic Sony55e audio play problem.
 * 3.07a Feb 11, 1996 -- check drive->id for NULL before dereferencing, to fix
 *                       problem with "hde=cdrom" with no drive present.  -ml
 * 3.08  Mar  6, 1996 -- More Vertos workarounds.
 * 3.09  Apr  5, 1996 -- Add CDROMCLOSETRAY ioctl.
 *                       Switch to using MSF addressing for audio commands.
 *                       Reformat to match kernel tabbing style.
 *                       Add CDROM_GET_UPC ioctl.
 * 3.10  Apr 10, 1996 -- Fix compilation error with STANDARD_ATAPI.
 * 3.11  Apr 29, 1996 -- Patch from Heiko Eissfeldt <heiko@colossus.escape.de>
 *                       to remove redundant verify_area calls.
 * 3.12  May  7, 1996 -- Rudimentary changer support.  Based on patches
 *                        from Gerhard Zuber <zuber@berlin.snafu.de>.
 *                       Let open succeed even if there's no loaded disc.
 * 3.13  May 19, 1996 -- Fixes for changer code.
 * 3.14  May 29, 1996 -- Add work-around for Vertos 600.
 *                        (From Hennus Bergman <hennus@sky.ow.nl>.)
 * 3.15  July 2, 1996 -- Added support for Sanyo 3 CD changers
 *                       from Ben Galliart <bgallia@luc.edu> with
 *                       special help from Jeff Lightfoot
 *                       <jeffml@pobox.com>
 * 3.15a July 9, 1996 -- Improved Sanyo 3 CD changer identification
 * 3.16  Jul 28, 1996 -- Fix from Gadi to reduce kernel stack usage for ioctl.
 * 3.17  Sep 17, 1996 -- Tweak audio reads for some drives.
 *                       Start changing CDROMLOADFROMSLOT to CDROM_SELECT_DISC.
 * 3.18  Oct 31, 1996 -- Added module and DMA support.
 *
 *
 * 4.00  Nov 5, 1996   -- New ide-cd maintainer,
 *                        Erik B. Andersen <andersee@debian.org>
 *                     -- Newer Creative drives don't always set the error
 *                        register correctly.  Make sure we see media changes
 *                        regardless.
 *                     -- Integrate with generic cdrom driver.
 *                     -- CDROMGETSPINDOWN and CDROMSETSPINDOWN ioctls, based on
 *                        a patch from Ciro Cattuto <>.
 *                     -- Call set_device_ro.
 *                     -- Implement CDROMMECHANISMSTATUS and CDROMSLOTTABLE
 *                        ioctls, based on patch by Erik Andersen
 *                     -- Add some probes of drive capability during setup.
 *
 * 4.01  Nov 11, 1996  -- Split into ide-cd.c and ide-cd.h
 *                     -- Removed CDROMMECHANISMSTATUS and CDROMSLOTTABLE
 *                        ioctls in favor of a generalized approach
 *                        using the generic cdrom driver.
 *                     -- Fully integrated with the 2.1.X kernel.
 *                     -- Other stuff that I forgot (lots of changes)
 *
 * 4.02  Dec 01, 1996  -- Applied patch from Gadi Oxman <gadio@netvision.net.il>
 *                        to fix the drive door locking problems.
 *
 * 4.03  Dec 04, 1996  -- Added DSC overlap support.
 * 4.04  Dec 29, 1996  -- Added CDROMREADRAW ioclt based on patch
 *                        by Aleks Makarov (xmakarov@sun.felk.cvut.cz)
 *
 * 4.05  Nov 20, 1997  -- Modified to print more drive info on init
 *                        Minor other changes
 *                        Fix errors on CDROMSTOP (If you have a "Dolphin",
 *                        you must define IHAVEADOLPHIN)
 *                        Added identifier so new Sanyo CD-changer works
 *                        Better detection if door locking isn't supported
 *
 * 4.06  Dec 17, 1997  -- fixed endless "tray open" messages  -ml
 * 4.07  Dec 17, 1997  -- fallback to set pc->stat on "tray open"
 * 4.08  Dec 18, 1997  -- spew less noise when tray is empty
 *                     -- fix speed display for ACER 24X, 18X
 * 4.09  Jan 04, 1998  -- fix handling of the last block so we return
 *                        an end of file instead of an I/O error (Gadi)
 * 4.10  Jan 24, 1998  -- fixed a bug so now changers can change to a new
 *                        slot when there is no disc in the current slot.
 *                     -- Fixed a memory leak where info->changer_info was
 *                        malloc'ed but never free'd when closing the device.
 *                     -- Cleaned up the global namespace a bit by making more
 *                        functions static that should already have been.
 * 4.11  Mar 12, 1998  -- Added support for the CDROM_SELECT_SPEED ioctl
 *                        based on a patch for 2.0.33 by Jelle Foks
 *                        <jelle@scintilla.utwente.nl>, a patch for 2.0.33
 *                        by Toni Giorgino <toni@pcape2.pi.infn.it>, the SCSI
 *                        version, and my own efforts.  -erik
 *                     -- Fixed a stupid bug which egcs was kind enough to
 *                        inform me of where "Illegal mode for this track"
 *                        was never returned due to a comparison on data
 *                        types of limited range.
 * 4.12  Mar 29, 1998  -- Fixed bug in CDROM_SELECT_SPEED so write speed is
 *                        now set ionly for CD-R and CD-RW drives.  I had
 *                        removed this support because it produced errors.
 *                        It produced errors _only_ for non-writers. duh.
 * 4.13  May 05, 1998  -- Suppress useless "in progress of becoming ready"
 *                        messages, since this is not an error.
 *                     -- Change error messages to be const
 *                     -- Remove a "\t" which looks ugly in the syslogs
 * 4.14  July 17, 1998 -- Change to pointing to .ps version of ATAPI spec
 *                        since the .pdf version doesn't seem to work...
 *                     -- Updated the TODO list to something more current.
 *
 * 4.15  Aug 25, 1998  -- Updated ide-cd.h to respect mechine endianess,
 *                        patch thanks to "Eddie C. Dost" <ecd@skynet.be>
 *
 * 4.50  Oct 19, 1998  -- New maintainers!
 *                        Jens Axboe <axboe@image.dk>
 *                        Chris Zwilling <chris@cloudnet.com>
 *
 * 4.51  Dec 23, 1998  -- Jens Axboe <axboe@image.dk>
 *                      - ide_cdrom_reset enabled since the ide subsystem
 *                         handles resets fine now. <axboe@image.dk>
 *                      - Transfer size fix for Samsung CD-ROMs, thanks to
 *                        "Ville Hallik" <ville.hallik@mail.ee>.
 *                      - other minor stuff.
 *
 * 4.52  Jan 19, 1999  -- Jens Axboe <axboe@image.dk>
 *                      - Detect DVD-ROM/RAM drives
 *
 * 4.53  Feb 22, 1999   - Include other model Samsung and one Goldstar
 *                        drive in transfer size limit.
 *                      - Fix the I/O error when doing eject without a medium
 *                        loaded on some drives.
 *                      - CDROMREADMODE2 is now implemented through
 *                        CDROMREADRAW, since many drives don't support
 *                        MODE2 (even though ATAPI 2.6 says they must).
 *                      - Added ignore parameter to ide-cd (as a module), eg
 *			  insmod ide-cd ignore='hda hdb'
 *                        Useful when using ide-cd in conjunction with
 *                        ide-scsi. TODO: non-modular way of doing the
 *                        same.
 *
 * 4.54  Aug 5, 1999	- Support for MMC2 class commands through the generic
 *			  packet interface to cdrom.c.
 *			- Unified audio ioctl support, most of it.
 *			- cleaned up various deprecated verify_area().
 *			- Added ide_cdrom_packet() as the interface for
 *			  the Uniform generic_packet().
 *			- bunch of other stuff, will fill in logs later.
 *			- report 1 slot for non-changers, like the other
 *			  cd-rom drivers. don't report select disc for
 *			  non-changers as well.
 *			- mask out audio playing, if the device can't do it.
 *
 * 4.55  Sep 1, 1999	- Eliminated the rest of the audio ioctls, except
 *			  for CDROMREADTOC[ENTRY|HEADER]. Some of the drivers
 *			  use this independently of the actual audio handling.
 *			  They will disappear later when I get the time to
 *			  do it cleanly.
 *			- Minimize the TOC reading - only do it when we
 *			  know a media change has occurred.
 *			- Moved all the CDROMREADx ioctls to the Uniform layer.
 *			- Heiko Eissfeldt <heiko@colossus.escape.de> supplied
 *			  some fixes for CDI.
 *			- CD-ROM leaving door locked fix from Andries
 *			  Brouwer <Andries.Brouwer@cwi.nl>
 *			- Erik Andersen <andersen@xmission.com> unified
 *			  commands across the various drivers and how
 *			  sense errors are handled.
 *
 * 4.56  Sep 12, 1999	- Removed changer support - it is now in the
 *			  Uniform layer.
 *			- Added partition based multisession handling.
 *			- Mode sense and mode select moved to the
 *			  Uniform layer.
 *			- Fixed a problem with WPI CDS-32X drive - it
 *			  failed the capabilities
 *
 * 4.57  Apr 7, 2000	- Fixed sense reporting.
 *			- Fixed possible oops in ide_cdrom_get_last_session()
 *			- Fix locking mania and make ide_cdrom_reset relock
 *			- Stop spewing errors to log when magicdev polls with
 *			  TEST_UNIT_READY on some drives.
 *			- Various fixes from Tobias Ringstrom:
 *			  tray if it was locked prior to the reset.
 *			  - cdrom_read_capacity returns one frame too little.
 *			  - Fix real capacity reporting.
 *
 * 4.58  May 1, 2000	- Clean up ACER50 stuff.
 *			- Fix small problem with ide_cdrom_capacity
 *
 * 4.59  Aug 11, 2000	- Fix changer problem in cdrom_read_toc, we weren't
 *			  correctly sensing a disc change.
 *			- Rearranged some code
 *			- Use extended sense on drives that support it for
 *			  correctly reporting tray status -- from
 *			  Michael D Johnson <johnsom@orst.edu>
 *
 *************************************************************************/

#define IDECD_VERSION "4.59"

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/cdrom.h>
#include <linux/ide.h>
#include <linux/completion.h>

#include <asm/irq.h>
#include <asm/io.h>
#include <asm/byteorder.h>
#include <asm/uaccess.h>
#include <asm/unaligned.h>

#include <linux/atapi.h>
#include "ide-cd.h"

/****************************************************************************
 * Generic packet command support and error handling routines.
 */

/* Mark that we've seen a media change, and invalidate our internal
   buffers. */
static void cdrom_saw_media_change(struct ata_device *drive)
{
	struct cdrom_info *info = drive->driver_data;

	CDROM_STATE_FLAGS (drive)->media_changed = 1;
	CDROM_STATE_FLAGS (drive)->toc_valid = 0;
	info->nsectors_buffered = 0;
}

static void cdrom_analyze_sense_data(struct ata_device *drive, struct request *rq)
{
	int log = 0;
	/* FIXME --mdcki */
	struct packet_command *pc = (struct packet_command *) rq->special;
	struct packet_command *failed_command = pc->failed_command;

	/* Decode sense data from drive */
	struct request_sense *sense = (struct request_sense *) (pc->buffer - rq->cmd[4]);
	unsigned char fail_cmd;

	if (sense == NULL || failed_command == NULL || failed_command->quiet)
		return;

	fail_cmd = rq->cmd[0];

	/* Check whatever this error should be logged:
	 */
	switch (sense->sense_key) {
		case NO_SENSE:
		case RECOVERED_ERROR:
			break;

		case NOT_READY:

			/* Don't care about tray state messages for e.g.
			 * capacity commands or in-progress or becoming ready.
			 */
			if (sense->asc == 0x3a || sense->asc == 0x04)
				break;
			log = 1;
			break;

		case UNIT_ATTENTION:

			/* Make good and sure we've seen this potential media
			 * change. Some drives (i.e. Creative) fail to present
			 * the correct sense key in the error register.
			 */
			cdrom_saw_media_change(drive);
			break;

		default:
			log = 1;
			break;
	}

	if (!log)
		return;

	/*
	 * If a read toc is executed for a CD-R or CD-RW medium where the first
	 * toc has not been recorded yet, it will fail with 05/24/00 (which is
	 * a confusing error).
	 */

	if (fail_cmd == GPCMD_READ_TOC_PMA_ATIP)
		if (sense->sense_key == 0x05 && sense->asc == 0x24)
			return;

#if VERBOSE_IDE_CD_ERRORS
	{
		int i;
		const char *s;
		char buf[80];

		printk ("ATAPI device %s:\n", drive->name);
		if (sense->error_code==0x70)
			printk("  Error: ");
		else if (sense->error_code==0x71)
			printk("  Deferred Error: ");
		else if (sense->error_code == 0x7f)
			printk("  Vendor-specific Error: ");
		else
			printk("  Unknown Error Type: ");

		if (sense->sense_key < ARY_LEN(sense_key_texts))
			s = sense_key_texts[sense->sense_key];
		else
			s = "bad sense key!";

		printk("%s -- (Sense key=0x%02x)\n", s, sense->sense_key);

		if (sense->asc == 0x40) {
			sprintf(buf, "Diagnostic failure on component 0x%02x",
				 sense->ascq);
			s = buf;
		} else {
			int lo = 0, mid, hi = ARY_LEN(sense_data_texts);
			unsigned long key = (sense->sense_key << 16);
			key |= (sense->asc << 8);
			if (!(sense->ascq >= 0x80 && sense->ascq <= 0xdd))
				key |= sense->ascq;
			s = NULL;

			while (hi > lo) {
				mid = (lo + hi) / 2;
				if (sense_data_texts[mid].asc_ascq == key ||
				    sense_data_texts[mid].asc_ascq == (0xff0000|key)) {
					s = sense_data_texts[mid].text;
					break;
				}
				else if (sense_data_texts[mid].asc_ascq > key)
					hi = mid;
				else
					lo = mid+1;
			}
		}

		if (s == NULL) {
			if (sense->asc > 0x80)
				s = "(vendor-specific error)";
			else
				s = "(reserved error code)";
		}

		printk("  %s -- (asc=0x%02x, ascq=0x%02x)\n",
			s, sense->asc, sense->ascq);

		{

			int lo=0, mid, hi= ARY_LEN (packet_command_texts);
			s = NULL;

			while (hi > lo) {
				mid = (lo + hi) / 2;
				if (packet_command_texts[mid].packet_command == fail_cmd) {
					s = packet_command_texts[mid].text;
					break;
				}
				if (packet_command_texts[mid].packet_command > fail_cmd)
					hi = mid;
				else
					lo = mid+1;
			}

			printk ("  The failed \"%s\" packet command was: \n  \"", s);
			for (i=0; i < CDROM_PACKET_SIZE; i++)
				printk ("%02x ", rq->cmd[i]);
			printk ("\"\n");
		}

		/* The SKSV bit specifies validity of the sense_key_specific
		 * in the next two commands. It is bit 7 of the first byte.
		 * In the case of NOT_READY, if SKSV is set the drive can
		 * give us nice ETA readings.
		 */
		if (sense->sense_key == NOT_READY && (sense->sks[0] & 0x80)) {
			int progress = (sense->sks[1] << 8 | sense->sks[2]) * 100;
			printk("  Command is %02d%% complete\n", progress / 0xffff);

		}

		if (sense->sense_key == ILLEGAL_REQUEST &&
		    (sense->sks[0] & 0x80) != 0) {
			printk("  Error in %s byte %d",
				(sense->sks[0] & 0x40) != 0 ?
				"command packet" : "command data",
				(sense->sks[1] << 8) + sense->sks[2]);

			if ((sense->sks[0] & 0x40) != 0)
				printk (" bit %d", sense->sks[0] & 0x07);

			printk ("\n");
		}
	}

#else

	/* Suppress printing unit attention and `in progress of becoming ready'
	   errors when we're not being verbose. */

	if (sense->sense_key == UNIT_ATTENTION ||
	    (sense->sense_key == NOT_READY && (sense->asc == 4 ||
						sense->asc == 0x3a)))
		return;

	printk("%s: error code: 0x%02x  sense_key: 0x%02x  asc: 0x%02x  ascq: 0x%02x\n",
		drive->name,
		sense->error_code, sense->sense_key,
		sense->asc, sense->ascq);
#endif
}

static void cdrom_queue_request_sense(struct ata_device *drive,
				      struct completion *wait,
				      struct request_sense *sense,
				      struct packet_command *failed_command)
{
	struct cdrom_info *info		= drive->driver_data;
	struct packet_command *pc	= &info->request_sense_pc;
	struct request *rq;

	if (sense == NULL)
		sense = &info->sense_data;

	memset(pc, 0, sizeof(*pc));
	pc->buffer = (void *) sense;
	pc->buflen = 18;
	pc->failed_command = failed_command;

	/* stuff the sense request in front of our current request */
	rq = &info->request_sense_request;
	memset(rq, 0, sizeof(*rq));
	rq->cmd[0] = GPCMD_REQUEST_SENSE;
	rq->cmd[4] = pc->buflen;
	rq->flags = REQ_SENSE;

	/* FIXME --mdcki */
	rq->special = (char *) pc;

	rq->waiting = wait;
	ide_do_drive_cmd(drive, rq, ide_preempt);
}


static void cdrom_end_request(struct ata_device *drive, struct request *rq, int uptodate)
{
	if ((rq->flags & REQ_SENSE) && uptodate)
		cdrom_analyze_sense_data(drive, rq);

	if ((rq->flags & REQ_CMD) && !rq->current_nr_sectors)
		uptodate = 1;

	ata_end_request(drive, rq, uptodate, 0);
}


/* Returns 0 if the request should be continued.
   Returns 1 if the request was ended. */
static int cdrom_decode_status(ide_startstop_t *startstop, struct ata_device *drive, struct request *rq,
				int good_stat, int *stat_ret)
{
	int err, sense_key;
	struct packet_command *pc;
	int ok;

	/* Check for errors. */
	ok = ata_status(drive, good_stat, BAD_R_STAT);
	*stat_ret = drive->status;
	if (ok)
		return 0;

	/* Get the IDE error register. */
	err = GET_ERR();
	sense_key = err >> 4;

	if (rq == NULL) {
		printk("%s: missing rq in %s\n", drive->name, __FUNCTION__);
		*startstop = ATA_OP_FINISHED;
		return 1;
	}

	if (rq->flags & REQ_SENSE) {
		/* We got an error trying to get sense info
		   from the drive (probably while trying
		   to recover from a former error).  Just give up. */

		/* FIXME --mdcki */
		pc = (struct packet_command *) rq->special;
		pc->stat = 1;
		cdrom_end_request(drive, rq, 1);
		*startstop = ata_error(drive, rq, "request sense failure");

		return 1;
	} else if (rq->flags & (REQ_PC | REQ_BLOCK_PC)) {
		/* All other functions, except for READ. */
		struct completion *wait = NULL;

		/* FIXME --mdcki */
		pc = (struct packet_command *) rq->special;

		/* Check for tray open. */
		if (sense_key == NOT_READY) {
			cdrom_saw_media_change (drive);
		} else if (sense_key == UNIT_ATTENTION) {
			/* Check for media change. */
			cdrom_saw_media_change (drive);
			/*printk("%s: media changed\n",drive->name);*/
			return 0;
		} else if (!pc->quiet) {
			/* Otherwise, print an error. */
			ata_dump(drive, rq, "packet command error");
		}

		/* Set the error flag and complete the request.
		   Then, if we have a CHECK CONDITION status, queue a request
		   sense command.  We must be careful, though: we don't want
		   the thread in cdrom_queue_packet_command to wake up until
		   the request sense has completed.  We do this by transferring
		   the semaphore from the packet command request to the request
		   sense request. */

		if (drive->status & ERR_STAT) {
			wait = rq->waiting;
			rq->waiting = NULL;
		}

		pc->stat = 1;
		cdrom_end_request(drive, rq, 1);

		/* FIXME: this is the only place where pc->sense get's used.
		 * Think hard about how to get rid of it...
		 */

		if (drive->status & ERR_STAT)
			cdrom_queue_request_sense(drive, wait, pc->sense, pc);
	} else if (rq->flags & REQ_CMD) {
		/* Handle errors from READ and WRITE requests. */

		if (sense_key == NOT_READY) {
			/* Tray open. */
			cdrom_saw_media_change (drive);

			/* Fail the request. */
			printk ("%s: tray open\n", drive->name);
			cdrom_end_request(drive, rq, 0);
		} else if (sense_key == UNIT_ATTENTION) {
			/* Media change. */
			cdrom_saw_media_change (drive);

			/* Arrange to retry the request.
			   But be sure to give up if we've retried
			   too many times. */
			if (++rq->errors > ERROR_MAX)
				cdrom_end_request(drive, rq, 0);
		} else if (sense_key == ILLEGAL_REQUEST ||
			   sense_key == DATA_PROTECT) {
			/* No point in retrying after an illegal
			   request or data protect error.*/
			ata_dump(drive, rq, "command error");
			cdrom_end_request(drive, rq,  0);
		} else if (sense_key == MEDIUM_ERROR) {
			/* No point in re-trying a zillion times on a bad
			 * sector.  The error is not correctable at all.
			 */
			ata_dump(drive, rq, "media error (bad sector)");
			cdrom_end_request(drive, rq, 0);
		} else if ((err & ~ABRT_ERR) != 0) {
			/* Go to the default handler
			   for other errors. */
			*startstop = ata_error(drive, rq, __FUNCTION__);
			return 1;
		} else if ((++rq->errors > ERROR_MAX)) {
			/* We've racked up too many retries.  Abort. */
			cdrom_end_request(drive, rq, 0);
		}

		/* If we got a CHECK_CONDITION status,
		   queue a request sense command. */
		if (drive->status & ERR_STAT)
			cdrom_queue_request_sense(drive, NULL, NULL, NULL);
	} else
		blk_dump_rq_flags(rq, "ide-cd bad flags");

	/* Retry, or handle the next request. */
	*startstop = ATA_OP_FINISHED;
	return 1;
}

static ide_startstop_t cdrom_timer_expiry(struct ata_device *drive, struct request *rq, unsigned long *wait)
{
	/*
	 * Some commands are *slow* and normally take a long time to
	 * complete. Usually we can use the ATAPI "disconnect" to bypass
	 * this, but not all commands/drives support that. Let
	 * ide_timer_expiry keep polling us for these.
	 */
	switch (rq->cmd[0]) {
		case GPCMD_BLANK:
		case GPCMD_FORMAT_UNIT:
		case GPCMD_RESERVE_RZONE_TRACK:
			*wait = WAIT_CMD;
			return ATA_OP_CONTINUES;
		default:
			*wait = 0;
			break;
	}

	return ATA_OP_FINISHED;
}

/* Set up the device registers for transferring a packet command on DEV,
   expecting to later transfer XFERLEN bytes.  HANDLER is the routine
   which actually transfers the command to the drive.  If this is a
   drq_interrupt device, this routine will arrange for HANDLER to be
   called when the interrupt from the drive arrives.  Otherwise, HANDLER
   will be called immediately after the drive is prepared for the transfer. */

static ide_startstop_t cdrom_start_packet_command(struct ata_device *drive,
						  struct request *rq,
						  int xferlen,
						  ata_handler_t handler)
{
	struct cdrom_info *info = drive->driver_data;
	int ret;

	/* Wait for the controller to be idle. */
	ret = ata_status_poll(drive, 0, BUSY_STAT, WAIT_READY, rq);
	if (ret != ATA_OP_READY)
		return ret;

	if (info->dma) {
		if (info->cmd == READ || info->cmd == WRITE)
			info->dma = udma_init(drive, rq);
		else
			printk("ide-cd: DMA set, but not allowed\n");
	}

	/* Set up the controller registers. */
	OUT_BYTE(info->dma, IDE_FEATURE_REG);
	OUT_BYTE(0, IDE_NSECTOR_REG);
	OUT_BYTE(0, IDE_SECTOR_REG);

	OUT_BYTE(xferlen & 0xff, IDE_LCYL_REG);
	OUT_BYTE(xferlen >> 8  , IDE_HCYL_REG);
	ata_irq_enable(drive, 1);
	if (info->dma)
		udma_start(drive, rq);

	if (CDROM_CONFIG_FLAGS (drive)->drq_interrupt) {
		ata_set_handler(drive, handler, WAIT_CMD, cdrom_timer_expiry);
		OUT_BYTE (WIN_PACKETCMD, IDE_COMMAND_REG); /* packet command */
		ret = ATA_OP_CONTINUES;
	} else {
		OUT_BYTE (WIN_PACKETCMD, IDE_COMMAND_REG); /* packet command */
		ret = handler(drive, rq);
	}

	return ret;
}

/*
 * Send a packet command cmd to the drive.  The device registers must have
 * already been prepared by cdrom_start_packet_command.  "handler" is the
 * interrupt handler to call when the command completes or there's data ready.
 */
static ide_startstop_t cdrom_transfer_packet_command(struct ata_device *drive,
		struct request *rq,
		unsigned char *cmd, unsigned long timeout,
		ata_handler_t handler)
{
	ide_startstop_t startstop;

	if (CDROM_CONFIG_FLAGS (drive)->drq_interrupt) {
		/* Here we should have been called after receiving an interrupt
		   from the device.  DRQ should how be set. */
		int stat_dum;

		/* Check for errors. */
		if (cdrom_decode_status(&startstop, drive, rq, DRQ_STAT, &stat_dum))
			return startstop;
	} else {
		/* Otherwise, we must wait for DRQ to get set. */
		startstop = ata_status_poll(drive, DRQ_STAT, BUSY_STAT,
				WAIT_READY, rq);
		if (startstop != ATA_OP_READY)
			return startstop;
	}

	/* Arm the interrupt handler and send the command to the device. */
	ata_set_handler(drive, handler, timeout, cdrom_timer_expiry);
	atapi_write(drive, cmd, CDROM_PACKET_SIZE);

	return ATA_OP_CONTINUES;
}

/****************************************************************************
 * Block read functions.
 */

/*
 * Buffer up to SECTORS_TO_TRANSFER sectors from the drive in our sector
 * buffer.  Once the first sector is added, any subsequent sectors are
 * assumed to be continuous (until the buffer is cleared).  For the first
 * sector added, SECTOR is its sector number.  (SECTOR is then ignored until
 * the buffer is cleared.)
 */
static void cdrom_buffer_sectors(struct ata_device *drive, unsigned long sector,
                                  int sectors_to_transfer)
{
	struct cdrom_info *info = drive->driver_data;

	/* Number of sectors to read into the buffer. */
	int sectors_to_buffer = MIN (sectors_to_transfer,
				     (SECTOR_BUFFER_SIZE >> SECTOR_BITS) -
				       info->nsectors_buffered);

	char *dest;

	/* If we couldn't get a buffer, don't try to buffer anything... */
	if (info->buffer == NULL)
		sectors_to_buffer = 0;

	/* If this is the first sector in the buffer, remember its number. */
	if (info->nsectors_buffered == 0)
		info->sector_buffered = sector;

	/* Read the data into the buffer. */
	dest = info->buffer + info->nsectors_buffered * SECTOR_SIZE;
	while (sectors_to_buffer > 0) {
		atapi_read(drive, dest, SECTOR_SIZE);
		--sectors_to_buffer;
		--sectors_to_transfer;
		++info->nsectors_buffered;
		dest += SECTOR_SIZE;
	}

	/* Throw away any remaining data. */
	while (sectors_to_transfer > 0) {
		char dum[SECTOR_SIZE];
		atapi_read(drive, dum, sizeof (dum));
		--sectors_to_transfer;
	}
}

/*
 * Check the contents of the interrupt reason register from the cdrom
 * and attempt to recover if there are problems.  Returns  0 if everything's
 * ok; nonzero if the request has been terminated.
 */
static inline
int cdrom_read_check_ireason(struct ata_device *drive, struct request *rq, int len, int ireason)
{
	ireason &= 3;
	if (ireason == 2) return 0;

	if (ireason == 0) {
		/* Whoops... The drive is expecting to receive data from us! */
		printk ("%s: cdrom_read_intr: "
			"Drive wants to transfer data the wrong way!\n",
			drive->name);

		/* Throw some data at the drive so it doesn't hang
		   and quit this request. */
		while (len > 0) {
			u8 dummy[4];

			atapi_write(drive, dummy, sizeof(dummy));
			len -= sizeof(dummy);
		}
	} else  if (ireason == 1) {
		/* Some drives (ASUS) seem to tell us that status
		 * info is available. just get it and ignore.
		 */
		ata_status(drive, 0, 0);
		return 0;
	} else {
		/* Drive wants a command packet, or invalid ireason... */
		printk ("%s: cdrom_read_intr: bad interrupt reason %d\n",
			drive->name, ireason);
	}

	cdrom_end_request(drive, rq, 0);
	return -1;
}

/*
 * Interrupt routine.  Called when a read request has completed.
 */
static ide_startstop_t cdrom_read_intr(struct ata_device *drive, struct request *rq)
{
	int stat;
	int ireason, len, sectors_to_transfer, nskip;
	struct cdrom_info *info = drive->driver_data;
	int dma = info->dma, dma_error = 0;
	ide_startstop_t startstop;

	/* Check for errors. */
	if (dma) {
		info->dma = 0;
		if ((dma_error = udma_stop(drive)))
			udma_enable(drive, 0, 1);
	}

	if (cdrom_decode_status(&startstop, drive, rq, 0, &stat))
		return startstop;

	if (dma) {
		if (!dma_error) {
			ata_end_request(drive, rq, 1, rq->nr_sectors);

			return ATA_OP_FINISHED;
		} else
			return ata_error(drive, rq, "dma error");
	}

	/* Read the interrupt reason and the transfer length. */
	ireason = IN_BYTE (IDE_NSECTOR_REG);
	len = IN_BYTE (IDE_LCYL_REG) + 256 * IN_BYTE (IDE_HCYL_REG);

	/* If DRQ is clear, the command has completed. */
	if ((stat & DRQ_STAT) == 0) {
		/* If we're not done filling the current buffer, complain.
		   Otherwise, complete the command normally. */
		if (rq->current_nr_sectors > 0) {
			printk ("%s: cdrom_read_intr: data underrun (%u blocks)\n",
				drive->name, rq->current_nr_sectors);
			cdrom_end_request(drive, rq, 0);
		} else
			cdrom_end_request(drive, rq, 1);
		return ATA_OP_FINISHED;
	}

	/* Check that the drive is expecting to do the same thing we are. */
	if (cdrom_read_check_ireason(drive, rq, len, ireason))
		return ATA_OP_FINISHED;

	/* Assume that the drive will always provide data in multiples
	   of at least SECTOR_SIZE, as it gets hairy to keep track
	   of the transfers otherwise. */
	if ((len % SECTOR_SIZE) != 0) {
		printk ("%s: cdrom_read_intr: Bad transfer size %d\n",
			drive->name, len);
		if (CDROM_CONFIG_FLAGS (drive)->limit_nframes)
			printk ("  This drive is not supported by this version of the driver\n");
		else {
			printk ("  Trying to limit transfer sizes\n");
			CDROM_CONFIG_FLAGS (drive)->limit_nframes = 1;
		}
		cdrom_end_request(drive, rq, 0);
		return ATA_OP_FINISHED;
	}

	/* The number of sectors we need to read from the drive. */
	sectors_to_transfer = len / SECTOR_SIZE;

	/* First, figure out if we need to bit-bucket
	   any of the leading sectors. */
	nskip = MIN((int)(rq->current_nr_sectors - bio_sectors(rq->bio)), sectors_to_transfer);

	while (nskip > 0) {
		/* We need to throw away a sector. */
		char dum[SECTOR_SIZE];
		atapi_read(drive, dum, SECTOR_SIZE);

		--rq->current_nr_sectors;
		--nskip;
		--sectors_to_transfer;
	}

	/* Now loop while we still have data to read from the drive. */
	while (sectors_to_transfer > 0) {
		int this_transfer;

		/* If we've filled the present buffer but there's another
		   chained buffer after it, move on. */
		if (rq->current_nr_sectors == 0 && rq->nr_sectors)
			cdrom_end_request(drive, rq, 1);

		/* If the buffers are full, cache the rest of the data in our
		   internal buffer. */
		if (rq->current_nr_sectors == 0) {
			cdrom_buffer_sectors(drive, rq->sector, sectors_to_transfer);
			sectors_to_transfer = 0;
		} else {
			/* Transfer data to the buffers.
			   Figure out how many sectors we can transfer
			   to the current buffer. */
			this_transfer = MIN (sectors_to_transfer,
					     rq->current_nr_sectors);

			/* Read this_transfer sectors
			   into the current buffer. */
			while (this_transfer > 0) {
				atapi_read(drive, rq->buffer, SECTOR_SIZE);
				rq->buffer += SECTOR_SIZE;
				--rq->nr_sectors;
				--rq->current_nr_sectors;
				++rq->sector;
				--this_transfer;
				--sectors_to_transfer;
			}
		}
	}

	/* Done moving data! Wait for another interrupt. */
	ata_set_handler(drive, cdrom_read_intr, WAIT_CMD, NULL);

	return ATA_OP_CONTINUES;
}

/*
 * Try to satisfy some of the current read request from our cached data.
 * Returns nonzero if the request has been completed, zero otherwise.
 */
static int cdrom_read_from_buffer(struct ata_device *drive, struct request *rq)
{
	struct cdrom_info *info = drive->driver_data;

	/* Can't do anything if there's no buffer. */
	if (info->buffer == NULL) return 0;

	/* Loop while this request needs data and the next block is present
	   in our cache. */
	while (rq->nr_sectors > 0 &&
	       rq->sector >= info->sector_buffered &&
	       rq->sector < info->sector_buffered + info->nsectors_buffered) {
		if (rq->current_nr_sectors == 0)
			cdrom_end_request(drive, rq, 1);

		memcpy (rq->buffer,
			info->buffer +
			(rq->sector - info->sector_buffered) * SECTOR_SIZE,
			SECTOR_SIZE);
		rq->buffer += SECTOR_SIZE;
		--rq->current_nr_sectors;
		--rq->nr_sectors;
		++rq->sector;
	}

	/* If we've satisfied the current request,
	   terminate it successfully. */
	if (rq->nr_sectors == 0) {
		cdrom_end_request(drive, rq, 1);
		return -1;
	}

	/* Move on to the next buffer if needed. */
	if (rq->current_nr_sectors == 0)
		cdrom_end_request(drive, rq, 1);

	/* If this condition does not hold, then the kluge i use to
	   represent the number of sectors to skip at the start of a transfer
	   will fail.  I think that this will never happen, but let's be
	   paranoid and check. */
	if (rq->current_nr_sectors < bio_sectors(rq->bio) &&
	    (rq->sector % SECTORS_PER_FRAME) != 0) {
		printk ("%s: %s: buffer botch (%ld)\n",
			drive->name, __FUNCTION__, rq->sector);
		cdrom_end_request(drive, rq, 0);
		return -1;
	}

	return 0;
}

/*
 * Routine to send a read packet command to the drive.  This is usually called
 * directly from cdrom_start_read.  However, for drq_interrupt devices, it is
 * called from an interrupt when the drive is ready to accept the command.
 */
static ide_startstop_t cdrom_start_read_continuation(struct ata_device *drive, struct request *rq)
{
	int nsect, sector, nframes, frame, nskip;

	/* Number of sectors to transfer. */
	nsect = rq->nr_sectors;

	/* Starting sector. */
	sector = rq->sector;

	/* If the requested sector doesn't start on a cdrom block boundary,
	   we must adjust the start of the transfer so that it does,
	   and remember to skip the first few sectors.
	   If the CURRENT_NR_SECTORS field is larger than the size
	   of the buffer, it will mean that we're to skip a number
	   of sectors equal to the amount by which CURRENT_NR_SECTORS
	   is larger than the buffer size. */
	nskip = (sector % SECTORS_PER_FRAME);
	if (nskip > 0) {
		/* Sanity check... */
		if (rq->current_nr_sectors != bio_sectors(rq->bio) &&
			(rq->sector % CD_FRAMESIZE != 0)) {
			printk ("%s: %s: buffer botch (%u)\n",
				drive->name, __FUNCTION__, rq->current_nr_sectors);
			cdrom_end_request(drive, rq, 0);
			return ATA_OP_FINISHED;
		}
		sector -= nskip;
		nsect += nskip;
		rq->current_nr_sectors += nskip;
	}

	/* Convert from sectors to cdrom blocks, rounding up the transfer
	   length if needed. */
	nframes = (nsect + SECTORS_PER_FRAME-1) / SECTORS_PER_FRAME;
	frame = sector / SECTORS_PER_FRAME;

	/* Largest number of frames was can transfer at once is 64k-1. For
	   some drives we need to limit this even more. */
	nframes = MIN(nframes, (CDROM_CONFIG_FLAGS (drive)->limit_nframes) ?
		(65534 / CD_FRAMESIZE) : 65535);

	/* Send the command to the drive and return. */
	return cdrom_transfer_packet_command(drive, rq, rq->cmd, WAIT_CMD, &cdrom_read_intr);
}


#define IDECD_SEEK_THRESHOLD	(1000)			/* 1000 blocks */
#define IDECD_SEEK_TIMER	(5 * WAIT_MIN_SLEEP)	/* 100 ms */
#define IDECD_SEEK_TIMEOUT     WAIT_CMD			/* 10 sec */

static ide_startstop_t cdrom_seek_intr(struct ata_device *drive, struct request *rq)
{
	struct cdrom_info *info = drive->driver_data;
	int stat;
	static int retry = 10;
	ide_startstop_t startstop;

	if (cdrom_decode_status (&startstop, drive, rq, 0, &stat))
		return startstop;
	CDROM_CONFIG_FLAGS(drive)->seeking = 1;

	if (retry && jiffies - info->start_seek > IDECD_SEEK_TIMER) {
		if (--retry == 0) {
			/*
			 * this condition is far too common, to bother
			 * users about it
			 */
#if 0
			printk("%s: disabled DSC seek overlap\n", drive->name);
#endif
			drive->dsc_overlap = 0;
		}
	}
	return ATA_OP_FINISHED;
}

static ide_startstop_t cdrom_start_seek_continuation(struct ata_device *drive, struct request *rq)
{
	unsigned char cmd[CDROM_PACKET_SIZE];
	sector_t sector;
	int frame, nskip;

	sector = rq->sector;
	nskip = (sector % SECTORS_PER_FRAME);
	if (nskip > 0)
		sector -= nskip;
	frame = sector / SECTORS_PER_FRAME;

	memset(rq->cmd, 0, sizeof(rq->cmd));
	cmd[0] = GPCMD_SEEK;
	put_unaligned(cpu_to_be32(frame), (unsigned int *) &cmd[2]);

	return cdrom_transfer_packet_command(drive, rq, cmd, WAIT_CMD, &cdrom_seek_intr);
}

static ide_startstop_t cdrom_start_seek(struct ata_device *drive, struct request *rq, sector_t block)
{
	struct cdrom_info *info = drive->driver_data;

	info->dma = 0;
	info->cmd = 0;
	info->start_seek = jiffies;
	return cdrom_start_packet_command(drive, rq, 0, cdrom_start_seek_continuation);
}

/*
 * Fix up a possibly partially-processed request so that we can
 * start it over entirely -- remember to call prep_rq_fn again since we
 * may have changed the layout
 */
static void restore_request (struct request *rq)
{
	if (rq->buffer != bio_data(rq->bio)) {
		sector_t n = (rq->buffer - (char *) bio_data(rq->bio)) / SECTOR_SIZE;
		rq->buffer = bio_data(rq->bio);
		rq->nr_sectors += n;
		rq->sector -= n;
	}
	rq->hard_cur_sectors = rq->current_nr_sectors = bio_sectors(rq->bio);
	rq->hard_nr_sectors = rq->nr_sectors;
	rq->hard_sector = rq->sector;
	rq->q->prep_rq_fn(rq->q, rq);
}

/*
 * Start a read request from the CD-ROM.
 */
static ide_startstop_t cdrom_start_read(struct ata_device *drive, struct request *rq, sector_t block)
{
	struct cdrom_info *info = drive->driver_data;

	restore_request(rq);

	/* Satisfy whatever we can of this request from our cached sector. */
	if (cdrom_read_from_buffer(drive, rq))
		return ATA_OP_FINISHED;

	blk_attempt_remerge(&drive->queue, rq);

	/* Clear the local sector buffer. */
	info->nsectors_buffered = 0;

	/* use dma, if possible. */
	if (drive->using_dma && (rq->sector % SECTORS_PER_FRAME == 0) &&
				(rq->nr_sectors % SECTORS_PER_FRAME == 0))
		info->dma = 1;
	else
		info->dma = 0;

	info->cmd = READ;
	/* Start sending the read request to the drive. */
	return cdrom_start_packet_command(drive, rq, 32768, cdrom_start_read_continuation);
}

/****************************************************************************
 * Execute all other packet commands.
 */

/* Interrupt routine for packet command completion. */
static ide_startstop_t cdrom_pc_intr(struct ata_device *drive, struct request *rq)
{
	int ireason, len, stat, thislen;

	/* FIXME --mdcki */
	struct packet_command *pc = (struct packet_command *) rq->special;
	ide_startstop_t startstop;

	/* Check for errors. */
	if (cdrom_decode_status (&startstop, drive, rq, 0, &stat))
		return startstop;

	/* Read the interrupt reason and the transfer length. */
	ireason = IN_BYTE (IDE_NSECTOR_REG);
	len = IN_BYTE (IDE_LCYL_REG) + 256 * IN_BYTE (IDE_HCYL_REG);

	/* If DRQ is clear, the command has completed.
	   Complain if we still have data left to transfer. */
	if ((stat & DRQ_STAT) == 0) {
		/* Some of the trailing request sense fields are optional, and
		   some drives don't send them.  Sigh. */
		if (rq->cmd[0] == GPCMD_REQUEST_SENSE &&
		    pc->buflen > 0 &&
		    pc->buflen <= 5) {
			while (pc->buflen > 0) {
				*pc->buffer++ = 0;
				--pc->buflen;
			}
		}

		if (pc->buflen == 0)
			cdrom_end_request(drive, rq, 1);
		else {
			/* Comment this out, because this always happens
			   right after a reset occurs, and it is annoying to
			   always print expected stuff.  */
			/*
			printk ("%s: cdrom_pc_intr: data underrun %d\n",
				drive->name, pc->buflen);
			*/
			pc->stat = 1;
			cdrom_end_request(drive, rq, 1);
		}
		return ATA_OP_FINISHED;
	}

	/* Figure out how much data to transfer. */
	thislen = pc->buflen;
	if (thislen > len) thislen = len;

	/* The drive wants to be written to. */
	if ((ireason & 3) == 0) {
		/* Transfer the data. */
		atapi_write(drive, pc->buffer, thislen);

		/* If we haven't moved enough data to satisfy the drive,
		   add some padding. */
		while (len > thislen) {
			u8 dummy[4];

			atapi_write(drive, dummy, sizeof(dummy));
			len -= sizeof(dummy);
		}

		/* Keep count of how much data we've moved. */
		pc->buffer += thislen;
		pc->buflen -= thislen;
	}

	/* Same drill for reading. */
	else if ((ireason & 3) == 2) {
		/* Transfer the data. */
		atapi_read(drive, pc->buffer, thislen);

		/* If we haven't moved enough data to satisfy the drive,
		   add some padding. */
		while (len > thislen) {
			u8 dummy[4];

			atapi_read(drive, dummy, sizeof(dummy));
			len -= sizeof(dummy);
		}

		/* Keep count of how much data we've moved. */
		pc->buffer += thislen;
		pc->buflen -= thislen;
	} else {
		printk ("%s: cdrom_pc_intr: The drive "
			"appears confused (ireason = 0x%2x)\n",
			drive->name, ireason);
		pc->stat = 1;
	}

	/* Now we wait for another interrupt. */
	ata_set_handler(drive, cdrom_pc_intr, WAIT_CMD, cdrom_timer_expiry);

	return ATA_OP_CONTINUES;
}

static ide_startstop_t cdrom_do_pc_continuation(struct ata_device *drive, struct request *rq)
{
	unsigned long timeout;

	/* FIXME --mdcki */
	struct packet_command *pc = (struct packet_command *) rq->special;

	if (pc->timeout)
		timeout = pc->timeout;
	else
		timeout = WAIT_CMD;

	/* Send the command to the drive and return. */
	return cdrom_transfer_packet_command(drive, rq, rq->cmd, timeout, &cdrom_pc_intr);
}

static ide_startstop_t cdrom_do_packet_command(struct ata_device *drive, struct request *rq)
{
	int len;

	/* FIXME --mdcki */
	struct packet_command *pc = (struct packet_command *) rq->special;
	struct cdrom_info *info = drive->driver_data;

	info->dma = 0;
	info->cmd = 0;
	pc->stat = 0;
	len = pc->buflen;

	/* Start sending the command to the drive. */
	return cdrom_start_packet_command (drive, rq, len, cdrom_do_pc_continuation);
}


/* Sleep for TIME jiffies.
   Not to be called from an interrupt handler. */
static
void cdrom_sleep (int time)
{
	int sleep = time;

	do {
		set_current_state(TASK_INTERRUPTIBLE);
		sleep = schedule_timeout(sleep);
	} while (sleep);
}

static
int cdrom_queue_packet_command(struct ata_device *drive, unsigned char *cmd,
		struct request_sense *sense, struct packet_command *pc)
{
	struct request rq;
	int retries = 10;

	/* Start of retry loop. */
	do {
		memset(&rq, 0, sizeof(rq));
		memcpy(rq.cmd, cmd, CDROM_PACKET_SIZE);
		rq.flags = REQ_PC;

		/* FIXME --mdcki */
		rq.special = (void *) pc;
		if (ide_do_drive_cmd(drive, &rq, ide_wait)) {
			printk("%s: do_drive_cmd returned stat=%02x,err=%02x\n",
				drive->name, rq.buffer[0], rq.buffer[1]);

			/* FIXME: we should probably abort/retry or something */
			if (sense) {

				/* Decode the error here at least for error
				 * reporting to upper layers.!
				 */

			}
		}
		if (pc->stat != 0) {
			/* The request failed.  Retry if it was due to a unit
			   attention status
			   (usually means media was changed). */

			if (sense && sense->sense_key == UNIT_ATTENTION)
				cdrom_saw_media_change (drive);
			else if (sense && sense->sense_key == NOT_READY &&
				 sense->asc == 4 && sense->ascq != 4) {
				/* The drive is in the process of loading
				   a disk.  Retry, but wait a little to give
				   the drive time to complete the load. */
				cdrom_sleep(2 * HZ);
			} else {
				/* Otherwise, don't retry. */
				retries = 0;
			}
			--retries;
		}

		/* End of retry loop. */
	} while (pc->stat != 0 && retries >= 0);

	/* Return an error if the command failed. */
	return pc->stat ? -EIO : 0;
}

/*
 * Write handling
 */
static inline int cdrom_write_check_ireason(struct ata_device *drive, struct request *rq,
	int len, int ireason)
{
	/* Two notes about IDE interrupt reason here - 0 means that
	 * the drive wants to receive data from us, 2 means that
	 * the drive is expecting data from us.
	 */
	ireason &= 3;

	if (ireason == 2) {
		/* Whoops... The drive wants to send data. */
		printk("%s: cdrom_write_intr: wrong transfer direction!\n",
			drive->name);

		/* Throw some data at the drive so it doesn't hang
		   and quit this request. */
		while (len > 0) {
			u8 dummy[4];
			atapi_write(drive, dummy, sizeof(dummy));
			len -= sizeof(dummy);
		}
	} else {
		/* Drive wants a command packet, or invalid ireason... */
		printk("%s: cdrom_write_intr: bad interrupt reason %d\n",
			drive->name, ireason);
	}

	cdrom_end_request(drive, rq, 0);
	return 1;
}

static ide_startstop_t cdrom_write_intr(struct ata_device *drive, struct request *rq)
{
	int stat, ireason, len, sectors_to_transfer, uptodate;
	struct cdrom_info *info = drive->driver_data;
	int dma_error = 0, dma = info->dma;
	ide_startstop_t startstop;

	/* Check for errors. */
	if (dma) {
		info->dma = 0;
		if ((dma_error = udma_stop(drive))) {
			printk("ide-cd: write dma error\n");
			udma_enable(drive, 0, 1);
		}
	}

	if (cdrom_decode_status(&startstop, drive, rq, 0, &stat)) {
		printk("ide-cd: write_intr decode_status bad\n");
		return startstop;
	}

	/*
	 * using dma, transfer is complete now
	 */
	if (dma) {
		if (dma_error)
			return ata_error(drive, rq, "dma error");

		ata_end_request(drive, rq, 1, rq->nr_sectors);

		return ATA_OP_FINISHED;
	}

	/* Read the interrupt reason and the transfer length. */
	ireason = IN_BYTE(IDE_NSECTOR_REG);
	len = IN_BYTE(IDE_LCYL_REG) + 256 * IN_BYTE(IDE_HCYL_REG);

	/* If DRQ is clear, the command has completed. */
	if ((stat & DRQ_STAT) == 0) {
		/* If we're not done writing, complain.
		 * Otherwise, complete the command normally.
		 */
		uptodate = 1;
		if (rq->current_nr_sectors > 0) {
			printk("%s: write_intr: data underrun (%u blocks)\n",
			drive->name, rq->current_nr_sectors);
			uptodate = 0;
		}
		cdrom_end_request(drive, rq, uptodate);
		return ATA_OP_FINISHED;
	}

	/* Check that the drive is expecting to do the same thing we are. */
	if (ireason & 3)
		if (cdrom_write_check_ireason(drive, rq, len, ireason))
			return ATA_OP_FINISHED;

	sectors_to_transfer = len / SECTOR_SIZE;

	/*
	 * now loop and write out the data
	 */
	while (sectors_to_transfer > 0) {
		int this_transfer;

		if (!rq->current_nr_sectors) {
			printk("ide-cd: write_intr: oops\n");
			break;
		}

		/*
		 * Figure out how many sectors we can transfer
		 */
		this_transfer = MIN(sectors_to_transfer,rq->current_nr_sectors);

		while (this_transfer > 0) {
			atapi_write(drive, rq->buffer, SECTOR_SIZE);
			rq->buffer += SECTOR_SIZE;
			--rq->nr_sectors;
			--rq->current_nr_sectors;
			++rq->sector;
			--this_transfer;
			--sectors_to_transfer;
		}

		/*
		 * current buffer complete, move on
		 */
		if (rq->current_nr_sectors == 0 && rq->nr_sectors)
			cdrom_end_request(drive, rq, 1);
	}

	/* re-arm handler */
	ata_set_handler(drive, cdrom_write_intr, 5 * WAIT_CMD, NULL);

	return ATA_OP_CONTINUES;
}

static ide_startstop_t cdrom_start_write_cont(struct ata_device *drive, struct request *rq)
{
	return cdrom_transfer_packet_command(drive, rq, rq->cmd, 2 * WAIT_CMD, cdrom_write_intr);
}

static ide_startstop_t cdrom_start_write(struct ata_device *drive, struct request *rq)
{
	struct cdrom_info *info = drive->driver_data;

	/*
	 * writes *must* be 2kB frame aligned
	 */
	if ((rq->nr_sectors & 3) || (rq->sector & 3)) {
		cdrom_end_request(drive, rq, 0);
		return ATA_OP_FINISHED;
	}

	/*
	 * for dvd-ram and such media, it's a really big deal to get
	 * big writes all the time. so scour the queue and attempt to
	 * remerge requests, often the plugging will not have had time
	 * to do this properly
	 */
	blk_attempt_remerge(&drive->queue, rq);

	info->nsectors_buffered = 0;

        /* use dma, if possible. we don't need to check more, since we
	 * know that the transfer is always (at least!) 2KB aligned */
	info->dma = drive->using_dma ? 1 : 0;
	info->cmd = WRITE;

	/* Start sending the read request to the drive. */
	return cdrom_start_packet_command(drive, rq, 32768, cdrom_start_write_cont);
}

#define IDE_LARGE_SEEK(b1,b2,t)	(((b1) > (b2) + (t)) || ((b2) > (b1) + (t)))

/****************************************************************************
 * cdrom driver request routine.
 */
static ide_startstop_t
ide_cdrom_do_request(struct ata_device *drive, struct request *rq, sector_t block)
{
	int ret;
	struct cdrom_info *info = drive->driver_data;

	if (rq->flags & REQ_CMD) {
		if (CDROM_CONFIG_FLAGS(drive)->seeking) {
			unsigned long elpased = jiffies - info->start_seek;

			if (!ata_status(drive, SEEK_STAT, 0)) {
				if (elpased < IDECD_SEEK_TIMEOUT) {
					ide_stall_queue(drive, IDECD_SEEK_TIMER);
					return ATA_OP_FINISHED;
				}
				printk ("%s: DSC timeout\n", drive->name);
			}
			CDROM_CONFIG_FLAGS(drive)->seeking = 0;
		}
		if (IDE_LARGE_SEEK(info->last_block, block, IDECD_SEEK_THRESHOLD) && drive->dsc_overlap) {
			ret = cdrom_start_seek(drive, rq, block);
		} else {
			if (rq_data_dir(rq) == READ)
				ret = cdrom_start_read(drive, rq, block);
			else
				ret = cdrom_start_write(drive, rq);
		}
		info->last_block = block;
		return ret;
	} else if (rq->flags & (REQ_PC | REQ_SENSE)) {
		ret = cdrom_do_packet_command(drive, rq);

		return ret;
	} else if (rq->flags & REQ_SPECIAL) {
		/*
		 * FIXME: Kill REQ_SEPCIAL and replace it with commands queued
		 * at the request queue instead as suggested by Linus.
		 *
		 * right now this can only be a reset...
		 */

		cdrom_end_request(drive, rq, 1);

		return ATA_OP_FINISHED;
	} else if (rq->flags & REQ_BLOCK_PC) {
		struct packet_command pc;
		ide_startstop_t startstop;

		memset(&pc, 0, sizeof(pc));
		pc.quiet = 1;
		pc.timeout = 60 * HZ;

		/* FIXME --mdcki */
		rq->special = (char *) &pc;

		startstop = cdrom_do_packet_command(drive, rq);

		if (pc.stat)
			++rq->errors;

		return startstop;
	}

	blk_dump_rq_flags(rq, "ide-cd bad flags");

	cdrom_end_request(drive, rq, 0);

	return ATA_OP_FINISHED;
}



/****************************************************************************
 * Ioctl handling.
 *
 * Routines which queue packet commands take as a final argument a pointer
 * to a request_sense struct.  If execution of the command results
 * in an error with a CHECK CONDITION status, this structure will be filled
 * with the results of the subsequent request sense command.  The pointer
 * can also be NULL, in which case no sense information is returned.
 */

#if ! STANDARD_ATAPI
static inline
int bin2bcd (int x)
{
	return (x%10) | ((x/10) << 4);
}


static inline
int bcd2bin (int x)
{
	return (x >> 4) * 10 + (x & 0x0f);
}

static
void msf_from_bcd (struct atapi_msf *msf)
{
	msf->minute = bcd2bin (msf->minute);
	msf->second = bcd2bin (msf->second);
	msf->frame  = bcd2bin (msf->frame);
}

#endif /* not STANDARD_ATAPI */


static inline
void lba_to_msf(int lba, u8 *m, u8 *s, u8 *f)
{
	lba += CD_MSF_OFFSET;
	lba &= 0xffffff;  /* negative lbas use only 24 bits */
	*m = lba / (CD_SECS * CD_FRAMES);
	lba %= (CD_SECS * CD_FRAMES);
	*s = lba / CD_FRAMES;
	*f = lba % CD_FRAMES;
}


static inline
int msf_to_lba(u8 m, u8 s, u8 f)
{
	return (((m * CD_SECS) + s) * CD_FRAMES + f) - CD_MSF_OFFSET;
}

static int cdrom_check_status(struct ata_device *drive, struct request_sense *sense)
{
	unsigned char cmd[CDROM_PACKET_SIZE];
	struct packet_command pc;
	struct cdrom_info *info = drive->driver_data;
	struct cdrom_device_info *cdi = &info->devinfo;

	memset(&pc, 0, sizeof(pc));
	pc.sense = sense;

	cmd[0] = GPCMD_TEST_UNIT_READY;

#if !STANDARD_ATAPI
        /* the Sanyo 3 CD changer uses byte 7 of TEST_UNIT_READY to
           switch CDs instead of supporting the LOAD_UNLOAD opcode   */

        cmd[7] = cdi->sanyo_slot % 3;
#endif

	return cdrom_queue_packet_command(drive, cmd, sense, &pc);
}


/* Lock the door if LOCKFLAG is nonzero; unlock it otherwise. */
static int
cdrom_lockdoor(struct ata_device *drive, int lockflag, struct request_sense *sense)
{
	struct packet_command pc;
	int stat;

	/* If the drive cannot lock the door, just pretend. */
	if (CDROM_CONFIG_FLAGS(drive)->no_doorlock) {
		stat = 0;
	} else {
		unsigned char cmd[CDROM_PACKET_SIZE];

		memset(&pc, 0, sizeof(pc));
		pc.sense = sense;
		cmd[0] = GPCMD_PREVENT_ALLOW_MEDIUM_REMOVAL;
		cmd[4] = lockflag ? 1 : 0;
		stat = cdrom_queue_packet_command(drive, cmd, sense, &pc);
	}

	/* If we got an illegal field error, the drive
	   probably cannot lock the door. */
	if (stat != 0 &&
	    sense->sense_key == ILLEGAL_REQUEST &&
	    (sense->asc == 0x24 || sense->asc == 0x20)) {
		printk ("%s: door locking not supported\n",
			drive->name);
		CDROM_CONFIG_FLAGS (drive)->no_doorlock = 1;
		stat = 0;
	}

	/* no medium, that's alright. */
	if (stat != 0 && sense->sense_key == NOT_READY && sense->asc == 0x3a)
		stat = 0;

	if (stat == 0)
		CDROM_STATE_FLAGS (drive)->door_locked = lockflag;

	return stat;
}


/* Eject the disk if EJECTFLAG is 0.
   If EJECTFLAG is 1, try to reload the disk. */
static int cdrom_eject(struct ata_device *drive, int ejectflag,
		       struct request_sense *sense)
{
	struct packet_command pc;
	unsigned char cmd[CDROM_PACKET_SIZE];

	if (CDROM_CONFIG_FLAGS(drive)->no_eject && !ejectflag)
		return -EDRIVE_CANT_DO_THIS;

	/* reload fails on some drives, if the tray is locked */
	if (CDROM_STATE_FLAGS(drive)->door_locked && ejectflag)
		return 0;

	memset(&pc, 0, sizeof (pc));
	pc.sense = sense;

	cmd[0] = GPCMD_START_STOP_UNIT;
	cmd[4] = 0x02 + (ejectflag != 0);
	return cdrom_queue_packet_command(drive, cmd, sense, &pc);
}

static int cdrom_read_capacity(struct ata_device *drive, u32 *capacity,
			       struct request_sense *sense)
{
	struct {
		__u32 lba;
		__u32 blocklen;
	} capbuf;

	int stat;
	unsigned char cmd[CDROM_PACKET_SIZE];
	struct packet_command pc;

	memset(&pc, 0, sizeof(pc));
	pc.sense = sense;

	cmd[0] = GPCMD_READ_CDVD_CAPACITY;
	pc.buffer = (char *)&capbuf;
	pc.buflen = sizeof(capbuf);
	stat = cdrom_queue_packet_command(drive, cmd, sense, &pc);
	if (stat == 0)
		*capacity = 1 + be32_to_cpu(capbuf.lba);

	return stat;
}

static int cdrom_read_tocentry(struct ata_device *drive, int trackno, int msf_flag,
				int format, char *buf, int buflen,
				struct request_sense *sense)
{
	unsigned char cmd[CDROM_PACKET_SIZE];
	struct packet_command pc;

	memset(&pc, 0, sizeof(pc));
	pc.sense = sense;

	pc.buffer =  buf;
	pc.buflen = buflen;
	pc.quiet = 1;

	cmd[0] = GPCMD_READ_TOC_PMA_ATIP;
	if (msf_flag)
		cmd[1] = 2;
	cmd[6] = trackno;
	cmd[7] = (buflen >> 8);
	cmd[8] = (buflen & 0xff);
	cmd[9] = (format << 6);

	return cdrom_queue_packet_command(drive, cmd, sense, &pc);
}


/* Try to read the entire TOC for the disk into our internal buffer. */
static int cdrom_read_toc(struct ata_device *drive, struct request_sense *sense)
{
	int stat, ntracks, i;
	struct cdrom_info *info = drive->driver_data;
	struct cdrom_device_info *cdi = &info->devinfo;
	struct atapi_toc *toc = info->toc;
	struct {
		struct atapi_toc_header hdr;
		struct atapi_toc_entry  ent;
	} ms_tmp;

	if (toc == NULL) {
		/* Try to allocate space. */
		toc = (struct atapi_toc *) kmalloc (sizeof (struct atapi_toc),
						    GFP_KERNEL);
		info->toc = toc;
		if (toc == NULL) {
			printk ("%s: No cdrom TOC buffer!\n", drive->name);
			return -ENOMEM;
		}
	}

	/* Check to see if the existing data is still valid.
	   If it is, just return. */
	cdrom_check_status(drive, sense);

	if (CDROM_STATE_FLAGS(drive)->toc_valid)
		return 0;

	/* First read just the header, so we know how long the TOC is. */
	stat = cdrom_read_tocentry(drive, 0, 1, 0, (char *) &toc->hdr,
				    sizeof(struct atapi_toc_header), sense);
	if (stat) return stat;

#if ! STANDARD_ATAPI
	if (CDROM_CONFIG_FLAGS (drive)->toctracks_as_bcd) {
		toc->hdr.first_track = bcd2bin (toc->hdr.first_track);
		toc->hdr.last_track  = bcd2bin (toc->hdr.last_track);
	}
#endif  /* not STANDARD_ATAPI */

	ntracks = toc->hdr.last_track - toc->hdr.first_track + 1;
	if (ntracks <= 0)
		return -EIO;
	if (ntracks > MAX_TRACKS)
		ntracks = MAX_TRACKS;

	/* Now read the whole schmeer. */
	stat = cdrom_read_tocentry(drive, toc->hdr.first_track, 1, 0,
				  (char *)&toc->hdr,
				   sizeof(struct atapi_toc_header) +
				   (ntracks + 1) *
				   sizeof(struct atapi_toc_entry), sense);

	if (stat && toc->hdr.first_track > 1) {
		/* Cds with CDI tracks only don't have any TOC entries,
		   despite of this the returned values are
		   first_track == last_track = number of CDI tracks + 1,
		   so that this case is indistinguishable from the same
		   layout plus an additional audio track.
		   If we get an error for the regular case, we assume
		   a CDI without additional audio tracks. In this case
		   the readable TOC is empty (CDI tracks are not included)
		   and only holds the Leadout entry. Heiko EiÃ^ßfeldt */
		ntracks = 0;
		stat = cdrom_read_tocentry(drive, CDROM_LEADOUT, 1, 0,
					   (char *)&toc->hdr,
					   sizeof(struct atapi_toc_header) +
					   (ntracks + 1) *
					   sizeof(struct atapi_toc_entry),
					   sense);
		if (stat) {
			return stat;
		}
#if ! STANDARD_ATAPI
		if (CDROM_CONFIG_FLAGS (drive)->toctracks_as_bcd) {
			toc->hdr.first_track = bin2bcd(CDROM_LEADOUT);
			toc->hdr.last_track = bin2bcd(CDROM_LEADOUT);
		} else
#endif  /* not STANDARD_ATAPI */
		{
			toc->hdr.first_track = CDROM_LEADOUT;
			toc->hdr.last_track = CDROM_LEADOUT;
		}
	}

	if (stat)
		return stat;

	toc->hdr.toc_length = ntohs (toc->hdr.toc_length);

#if ! STANDARD_ATAPI
	if (CDROM_CONFIG_FLAGS (drive)->toctracks_as_bcd) {
		toc->hdr.first_track = bcd2bin (toc->hdr.first_track);
		toc->hdr.last_track  = bcd2bin (toc->hdr.last_track);
	}
#endif  /* not STANDARD_ATAPI */

	for (i=0; i<=ntracks; i++) {
#if ! STANDARD_ATAPI
		if (CDROM_CONFIG_FLAGS (drive)->tocaddr_as_bcd) {
			if (CDROM_CONFIG_FLAGS (drive)->toctracks_as_bcd)
				toc->ent[i].track = bcd2bin (toc->ent[i].track);
			msf_from_bcd (&toc->ent[i].addr.msf);
		}
#endif  /* not STANDARD_ATAPI */
		toc->ent[i].addr.lba = msf_to_lba (toc->ent[i].addr.msf.minute,
						   toc->ent[i].addr.msf.second,
						   toc->ent[i].addr.msf.frame);
	}

	/* Read the multisession information. */
	if (toc->hdr.first_track != CDROM_LEADOUT) {
		/* Read the multisession information. */
		stat = cdrom_read_tocentry(drive, 0, 1, 1, (char *)&ms_tmp,
					   sizeof(ms_tmp), sense);
		if (stat) return stat;
	} else {
		ms_tmp.ent.addr.msf.minute = 0;
		ms_tmp.ent.addr.msf.second = 2;
		ms_tmp.ent.addr.msf.frame  = 0;
		ms_tmp.hdr.first_track = ms_tmp.hdr.last_track = CDROM_LEADOUT;
	}

#if ! STANDARD_ATAPI
	if (CDROM_CONFIG_FLAGS (drive)->tocaddr_as_bcd)
		msf_from_bcd (&ms_tmp.ent.addr.msf);
#endif  /* not STANDARD_ATAPI */

	toc->last_session_lba = msf_to_lba (ms_tmp.ent.addr.msf.minute,
					    ms_tmp.ent.addr.msf.second,
					    ms_tmp.ent.addr.msf.frame);

	toc->xa_flag = (ms_tmp.hdr.first_track != ms_tmp.hdr.last_track);

	/* Now try to get the total cdrom capacity. */
	/* FIXME: This is making worng assumptions about register layout. */
	stat = cdrom_get_last_written(cdi, (unsigned long *) &toc->capacity);
	if (stat)
		stat = cdrom_read_capacity(drive, &toc->capacity, sense);
	if (stat)
		toc->capacity = 0x1fffff;

	drive->channel->gd->sizes[drive->select.b.unit << PARTN_BITS] = (toc->capacity * SECTORS_PER_FRAME) >> (BLOCK_SIZE_BITS - 9);
	drive->part[0].nr_sects = toc->capacity * SECTORS_PER_FRAME;

	/* Remember that we've read this stuff. */
	CDROM_STATE_FLAGS (drive)->toc_valid = 1;

	return 0;
}


static int cdrom_read_subchannel(struct ata_device *drive, int format, char *buf,
				 int buflen, struct request_sense *sense)
{
	unsigned char cmd[CDROM_PACKET_SIZE];
	struct packet_command pc;

	memset(&pc, 0, sizeof(pc));
	pc.sense = sense;

	pc.buffer = buf;
	pc.buflen = buflen;
	cmd[0] = GPCMD_READ_SUBCHANNEL;
	cmd[1] = 2;     /* MSF addressing */
	cmd[2] = 0x40;  /* request subQ data */
	cmd[3] = format;
	cmd[7] = (buflen >> 8);
	cmd[8] = (buflen & 0xff);

	return cdrom_queue_packet_command(drive, cmd, sense, &pc);
}

/* ATAPI cdrom drives are free to select the speed you request or any slower
   rate :-( Requesting too fast a speed will _not_ produce an error. */
static int cdrom_select_speed(struct ata_device *drive, int speed,
			      struct request_sense *sense)
{
	unsigned char cmd[CDROM_PACKET_SIZE];
	struct packet_command pc;
	memset(&pc, 0, sizeof(pc));
	pc.sense = sense;

	if (speed == 0)
		speed = 0xffff; /* set to max */
	else
		speed *= 177;   /* Nx to kbytes/s */

	cmd[0] = GPCMD_SET_SPEED;
	/* Read Drive speed in kbytes/second MSB */
	cmd[2] = (speed >> 8) & 0xff;
	/* Read Drive speed in kbytes/second LSB */
	cmd[3] = speed & 0xff;
	if (CDROM_CONFIG_FLAGS(drive)->cd_r ||
	    CDROM_CONFIG_FLAGS(drive)->cd_rw ||
	    CDROM_CONFIG_FLAGS(drive)->dvd_r) {
		/* Write Drive speed in kbytes/second MSB */
		cmd[4] = (speed >> 8) & 0xff;
		/* Write Drive speed in kbytes/second LSB */
		cmd[5] = speed & 0xff;
       }

	return cdrom_queue_packet_command(drive, cmd, sense, &pc);
}

static int cdrom_play_audio(struct ata_device *drive, int lba_start, int lba_end)
{
	struct request_sense sense;
	unsigned char cmd[CDROM_PACKET_SIZE];
	struct packet_command pc;

	memset(&pc, 0, sizeof (pc));
	pc.sense = &sense;

	cmd[0] = GPCMD_PLAY_AUDIO_MSF;
	lba_to_msf(lba_start, &cmd[3], &cmd[4], &cmd[5]);
	lba_to_msf(lba_end-1, &cmd[6], &cmd[7], &cmd[8]);

	return cdrom_queue_packet_command(drive, cmd, &sense, &pc);
}

static int cdrom_get_toc_entry(struct ata_device *drive, int track,
				struct atapi_toc_entry **ent)
{
	struct cdrom_info *info = drive->driver_data;
	struct atapi_toc *toc = info->toc;
	int ntracks;

	/*
	 * don't serve cached data, if the toc isn't valid
	 */
	if (!CDROM_STATE_FLAGS(drive)->toc_valid)
		return -EINVAL;

	/* Check validity of requested track number. */
	ntracks = toc->hdr.last_track - toc->hdr.first_track + 1;
	if (toc->hdr.first_track == CDROM_LEADOUT) ntracks = 0;
	if (track == CDROM_LEADOUT)
		*ent = &toc->ent[ntracks];
	else if (track < toc->hdr.first_track ||
		 track > toc->hdr.last_track)
		return -EINVAL;
	else
		*ent = &toc->ent[track - toc->hdr.first_track];

	return 0;
}

/* the generic packet interface to cdrom.c */
static int ide_cdrom_packet(struct cdrom_device_info *cdi,
			    struct cdrom_generic_command *cgc)
{
	struct packet_command pc;
	struct ata_device *drive = (struct ata_device *) cdi->handle;

	if (cgc->timeout <= 0)
		cgc->timeout = WAIT_CMD;

	/* here we queue the commands from the uniform CD-ROM
	   layer. the packet must be complete, as we do not
	   touch it at all. */
	memset(&pc, 0, sizeof(pc));
	pc.buffer = cgc->buffer;
	pc.buflen = cgc->buflen;
	pc.quiet = cgc->quiet;
	pc.timeout = cgc->timeout;
	pc.sense = cgc->sense;
	cgc->stat = cdrom_queue_packet_command(drive, cgc->cmd, cgc->sense, &pc);
	if (!cgc->stat)
		cgc->buflen -= pc.buflen;

	return cgc->stat;
}


static
int ide_cdrom_dev_ioctl (struct cdrom_device_info *cdi,
			 unsigned int cmd, unsigned long arg)
{
	struct cdrom_generic_command cgc;
	char buffer[16];
	int stat;

	init_cdrom_command(&cgc, buffer, sizeof(buffer), CGC_DATA_UNKNOWN);

	/* These will be moved into the Uniform layer shortly... */
	switch (cmd) {
	case CDROMSETSPINDOWN: {
		char spindown;

		if (copy_from_user(&spindown, (void *) arg, sizeof(char)))
			return -EFAULT;

                if ((stat = cdrom_mode_sense(cdi, &cgc, GPMODE_CDROM_PAGE, 0)))
			return stat;

		buffer[11] = (buffer[11] & 0xf0) | (spindown & 0x0f);

		return cdrom_mode_select(cdi, &cgc);
	}

	case CDROMGETSPINDOWN: {
		char spindown;

                if ((stat = cdrom_mode_sense(cdi, &cgc, GPMODE_CDROM_PAGE, 0)))
			return stat;

		spindown = buffer[11] & 0x0f;

		if (copy_to_user((void *) arg, &spindown, sizeof (char)))
			return -EFAULT;

		return 0;
	}

	default:
		return -EINVAL;
	}

}

static
int ide_cdrom_audio_ioctl (struct cdrom_device_info *cdi,
			   unsigned int cmd, void *arg)
{
	struct ata_device *drive = (struct ata_device *) cdi->handle;
	struct cdrom_info *info = drive->driver_data;
	int stat;

	switch (cmd) {
	/*
	 * emulate PLAY_AUDIO_TI command with PLAY_AUDIO_10, since
	 * atapi doesn't support it
	 */
	case CDROMPLAYTRKIND: {
		unsigned long lba_start, lba_end;
		struct cdrom_ti *ti = (struct cdrom_ti *)arg;
		struct atapi_toc_entry *first_toc, *last_toc;

		stat = cdrom_get_toc_entry(drive, ti->cdti_trk0, &first_toc);
		if (stat)
			return stat;

		stat = cdrom_get_toc_entry(drive, ti->cdti_trk1, &last_toc);
		if (stat)
			return stat;

		if (ti->cdti_trk1 != CDROM_LEADOUT)
			++last_toc;
		lba_start = first_toc->addr.lba;
		lba_end   = last_toc->addr.lba;

		if (lba_end <= lba_start)
			return -EINVAL;

		return cdrom_play_audio(drive, lba_start, lba_end);
	}

	case CDROMREADTOCHDR: {
		struct cdrom_tochdr *tochdr = (struct cdrom_tochdr *) arg;
		struct atapi_toc *toc;

		/* Make sure our saved TOC is valid. */
		stat = cdrom_read_toc(drive, NULL);
		if (stat) return stat;

		toc = info->toc;
		tochdr->cdth_trk0 = toc->hdr.first_track;
		tochdr->cdth_trk1 = toc->hdr.last_track;

		return 0;
	}

	case CDROMREADTOCENTRY: {
		struct cdrom_tocentry *tocentry = (struct cdrom_tocentry*) arg;
		struct atapi_toc_entry *toce;

		stat = cdrom_get_toc_entry (drive, tocentry->cdte_track, &toce);
		if (stat) return stat;

		tocentry->cdte_ctrl = toce->control;
		tocentry->cdte_adr  = toce->adr;
		if (tocentry->cdte_format == CDROM_MSF) {
			lba_to_msf (toce->addr.lba,
				   &tocentry->cdte_addr.msf.minute,
				   &tocentry->cdte_addr.msf.second,
				   &tocentry->cdte_addr.msf.frame);
		} else
			tocentry->cdte_addr.lba = toce->addr.lba;

		return 0;
	}

	default:
		return -EINVAL;
	}
}

static
int ide_cdrom_reset (struct cdrom_device_info *cdi)
{
	struct ata_device *drive = (struct ata_device *) cdi->handle;
	struct request_sense sense;
	struct request req;
	int ret;

	memset(&req, 0, sizeof(req));
	req.flags = REQ_SPECIAL;
	ret = ide_do_drive_cmd(drive, &req, ide_wait);

	/*
	 * A reset will unlock the door. If it was previously locked,
	 * lock it again.
	 */
	if (CDROM_STATE_FLAGS(drive)->door_locked)
		cdrom_lockdoor(drive, 1, &sense);

	return ret;
}


static
int ide_cdrom_tray_move(struct cdrom_device_info *cdi, int position)
{
	struct ata_device *drive = (struct ata_device *) cdi->handle;
	struct request_sense sense;

	if (position) {
		int stat = cdrom_lockdoor(drive, 0, &sense);
		if (stat)
			return stat;
	}

	return cdrom_eject(drive, !position, &sense);
}

static
int ide_cdrom_lock_door(struct cdrom_device_info *cdi, int lock)
{
	struct ata_device *drive = (struct ata_device *) cdi->handle;
	struct request_sense sense;

	return cdrom_lockdoor(drive, lock, &sense);
}

static
int ide_cdrom_select_speed (struct cdrom_device_info *cdi, int speed)
{
	struct ata_device *drive = (struct ata_device *) cdi->handle;
	struct request_sense sense;
	int stat;

	if ((stat = cdrom_select_speed (drive, speed, &sense)) < 0)
		return stat;

        cdi->speed = CDROM_STATE_FLAGS (drive)->current_speed;
        return 0;
}

static
int ide_cdrom_drive_status (struct cdrom_device_info *cdi, int slot_nr)
{
	struct ata_device *drive = (struct ata_device *) cdi->handle;

	if (slot_nr == CDSL_CURRENT) {
		struct request_sense sense;
		int stat = cdrom_check_status(drive, &sense);
		if (stat == 0 || sense.sense_key == UNIT_ATTENTION)
			return CDS_DISC_OK;

		if (sense.sense_key == NOT_READY && sense.asc == 0x04 &&
		    sense.ascq == 0x04)
			return CDS_DISC_OK;


		/*
		 * If not using Mt Fuji extended media tray reports,
		 * just return TRAY_OPEN since ATAPI doesn't provide
		 * any other way to detect this...
		 */
		if (sense.sense_key == NOT_READY) {
			if (sense.asc == 0x3a && sense.ascq == 1)
				return CDS_NO_DISC;
			else
				return CDS_TRAY_OPEN;
		}

		return CDS_DRIVE_NOT_READY;
	}
	return -EINVAL;
}

static
int ide_cdrom_get_last_session (struct cdrom_device_info *cdi,
				struct cdrom_multisession *ms_info)
{
	struct atapi_toc *toc;
	struct ata_device *drive = (struct ata_device *) cdi->handle;
	struct cdrom_info *info = drive->driver_data;
	struct request_sense sense;
	int ret;

	if (!CDROM_STATE_FLAGS(drive)->toc_valid || info->toc == NULL)
		if ((ret = cdrom_read_toc(drive, &sense)))
			return ret;

	toc = info->toc;
	ms_info->addr.lba = toc->last_session_lba;
	ms_info->xa_flag = toc->xa_flag;

	return 0;
}

static
int ide_cdrom_get_mcn (struct cdrom_device_info *cdi,
		       struct cdrom_mcn *mcn_info)
{
	int stat;
	char mcnbuf[24];
	struct ata_device *drive = (struct ata_device *) cdi->handle;

/* get MCN */
	if ((stat = cdrom_read_subchannel(drive, 2, mcnbuf, sizeof (mcnbuf), NULL)))
		return stat;

	memcpy (mcn_info->medium_catalog_number, mcnbuf+9,
		sizeof (mcn_info->medium_catalog_number)-1);
	mcn_info->medium_catalog_number[sizeof (mcn_info->medium_catalog_number)-1]
		= '\0';

	return 0;
}



/****************************************************************************
 * Other driver requests (open, close, check media change).
 */

static
int ide_cdrom_check_media_change_real (struct cdrom_device_info *cdi,
				       int slot_nr)
{
	struct ata_device *drive = (struct ata_device *) cdi->handle;
	int retval;

	if (slot_nr == CDSL_CURRENT) {
		cdrom_check_status(drive, NULL);
		retval = CDROM_STATE_FLAGS (drive)->media_changed;
		CDROM_STATE_FLAGS (drive)->media_changed = 0;
		return retval;
	} else {
		return -EINVAL;
	}
}


static
int ide_cdrom_open_real (struct cdrom_device_info *cdi, int purpose)
{
	return 0;
}


/*
 * Close down the device.  Invalidate all cached blocks.
 */

static
void ide_cdrom_release_real (struct cdrom_device_info *cdi)
{
}



/****************************************************************************
 * Device initialization.
 */
static struct cdrom_device_ops ide_cdrom_dops = {
	.open =			ide_cdrom_open_real,
	.release =		ide_cdrom_release_real,
	.drive_status =		ide_cdrom_drive_status,
	.media_changed =	ide_cdrom_check_media_change_real,
	.tray_move =		ide_cdrom_tray_move,
	.lock_door =		ide_cdrom_lock_door,
	.select_speed =		ide_cdrom_select_speed,
	.get_last_session =	ide_cdrom_get_last_session,
	.get_mcn =		ide_cdrom_get_mcn,
	.reset =		ide_cdrom_reset,
	.audio_ioctl =		ide_cdrom_audio_ioctl,
	.dev_ioctl =		ide_cdrom_dev_ioctl,
	.capability =		CDC_CLOSE_TRAY | CDC_OPEN_TRAY | CDC_LOCK |
				CDC_SELECT_SPEED | CDC_SELECT_DISC |
				CDC_MULTI_SESSION | CDC_MCN |
				CDC_MEDIA_CHANGED | CDC_PLAY_AUDIO | CDC_RESET |
				CDC_IOCTLS | CDC_DRIVE_STATUS | CDC_CD_R |
				CDC_CD_RW | CDC_DVD | CDC_DVD_R| CDC_DVD_RAM |
				CDC_GENERIC_PACKET,
	.generic_packet =	ide_cdrom_packet,
};

static int ide_cdrom_register(struct ata_device *drive, int nslots)
{
	struct cdrom_info *info = drive->driver_data;
	struct cdrom_device_info *devinfo = &info->devinfo;
	int minor = (drive->select.b.unit) << PARTN_BITS;

	devinfo->dev = mk_kdev(drive->channel->major, minor);
	devinfo->ops = &ide_cdrom_dops;
	devinfo->mask = 0;
	devinfo->speed = CDROM_STATE_FLAGS (drive)->current_speed;
	devinfo->capacity = nslots;
	devinfo->handle = (void *) drive;
	strcpy(devinfo->name, drive->name);

	/* set capability mask to match the probe. */
	if (!CDROM_CONFIG_FLAGS (drive)->cd_r)
		devinfo->mask |= CDC_CD_R;
	if (!CDROM_CONFIG_FLAGS (drive)->cd_rw)
		devinfo->mask |= CDC_CD_RW;
	if (!CDROM_CONFIG_FLAGS (drive)->dvd)
		devinfo->mask |= CDC_DVD;
	if (!CDROM_CONFIG_FLAGS (drive)->dvd_r)
		devinfo->mask |= CDC_DVD_R;
	if (!CDROM_CONFIG_FLAGS (drive)->dvd_ram)
		devinfo->mask |= CDC_DVD_RAM;
	if (!CDROM_CONFIG_FLAGS (drive)->is_changer)
		devinfo->mask |= CDC_SELECT_DISC;
	if (!CDROM_CONFIG_FLAGS (drive)->audio_play)
		devinfo->mask |= CDC_PLAY_AUDIO;
	if (!CDROM_CONFIG_FLAGS (drive)->close_tray)
		devinfo->mask |= CDC_CLOSE_TRAY;

	/* FIXME: I'm less that sure that this is the proper thing to do, since
	 * ware already adding the devices to devfs int ide.c upon device
	 * registration.
	 */

	devinfo->de = devfs_register(drive->de, "cd", DEVFS_FL_DEFAULT,
				     drive->channel->major, minor,
				     S_IFBLK | S_IRUGO | S_IWUGO,
				     ide_fops, NULL);

	return register_cdrom(devinfo);
}

static
int ide_cdrom_get_capabilities(struct ata_device *drive, struct atapi_capabilities_page *cap)
{
	struct cdrom_info *info = drive->driver_data;
	struct cdrom_device_info *cdi = &info->devinfo;
	struct cdrom_generic_command cgc;
	int stat, attempts = 3, size = sizeof(*cap);

	/*
	 * ACER50 (and others?) require the full spec length mode sense
	 * page capabilities size, but older drives break.
	 */
	if (drive->id) {
		if (!(!strcmp(drive->id->model, "ATAPI CD ROM DRIVE 50X MAX") ||
		    !strcmp(drive->id->model, "WPI CDS-32X")))
			size -= sizeof(cap->pad);
	}

	/* we have to cheat a little here. the packet will eventually
	 * be queued with ide_cdrom_packet(), which extracts the
	 * drive from cdi->handle. Since this device hasn't been
	 * registered with the Uniform layer yet, it can't do this.
	 * Same goes for cdi->ops.
	 */
	cdi->handle = (struct ata_device *) drive;
	cdi->ops = &ide_cdrom_dops;
	init_cdrom_command(&cgc, cap, size, CGC_DATA_UNKNOWN);
	do { /* we seem to get stat=0x01,err=0x00 the first time (??) */
		stat = cdrom_mode_sense(cdi, &cgc, GPMODE_CAPABILITIES_PAGE, 0);
		if (!stat)
			break;
	} while (--attempts);
	return stat;
}

static
int ide_cdrom_probe_capabilities(struct ata_device *drive)
{
	struct cdrom_info *info = drive->driver_data;
	struct cdrom_device_info *cdi = &info->devinfo;
	struct atapi_capabilities_page cap;
	int nslots = 1;

	if (CDROM_CONFIG_FLAGS (drive)->nec260) {
		CDROM_CONFIG_FLAGS (drive)->no_eject = 0;
		CDROM_CONFIG_FLAGS (drive)->audio_play = 1;
		return nslots;
	}

	if (ide_cdrom_get_capabilities(drive, &cap))
		return 0;

	if (cap.lock == 0)
		CDROM_CONFIG_FLAGS (drive)->no_doorlock = 1;
	if (cap.eject)
		CDROM_CONFIG_FLAGS (drive)->no_eject = 0;
	if (cap.cd_r_write)
		CDROM_CONFIG_FLAGS (drive)->cd_r = 1;
	if (cap.cd_rw_write)
		CDROM_CONFIG_FLAGS (drive)->cd_rw = 1;
	if (cap.test_write)
		CDROM_CONFIG_FLAGS (drive)->test_write = 1;
	if (cap.dvd_ram_read || cap.dvd_r_read || cap.dvd_rom)
		CDROM_CONFIG_FLAGS (drive)->dvd = 1;
	if (cap.dvd_ram_write)
		CDROM_CONFIG_FLAGS (drive)->dvd_ram = 1;
	if (cap.dvd_r_write)
		CDROM_CONFIG_FLAGS (drive)->dvd_r = 1;
	if (cap.audio_play)
		CDROM_CONFIG_FLAGS (drive)->audio_play = 1;
	if (cap.mechtype == mechtype_caddy || cap.mechtype == mechtype_popup)
		CDROM_CONFIG_FLAGS (drive)->close_tray = 0;

	/* Some drives used by Apple don't advertise audio play
	 * but they do support reading TOC & audio datas
	 */
	if (strcmp (drive->id->model, "MATSHITADVD-ROM SR-8187") == 0 ||
	    strcmp (drive->id->model, "MATSHITADVD-ROM SR-8186") == 0)
		CDROM_CONFIG_FLAGS (drive)->audio_play = 1;

#if ! STANDARD_ATAPI
	if (cdi->sanyo_slot > 0) {
		CDROM_CONFIG_FLAGS (drive)->is_changer = 1;
		nslots = 3;
	}

	else
#endif /* not STANDARD_ATAPI */
	if (cap.mechtype == mechtype_individual_changer ||
	    cap.mechtype == mechtype_cartridge_changer) {
		if ((nslots = cdrom_number_of_slots(cdi)) > 1) {
			CDROM_CONFIG_FLAGS (drive)->is_changer = 1;
			CDROM_CONFIG_FLAGS (drive)->supp_disc_present = 1;
		}
	}

	/* The ACER/AOpen 24X cdrom has the speed fields byte-swapped */
	if (drive->id && !drive->id->model[0] && !strncmp(drive->id->fw_rev, "241N", 4)) {
		CDROM_STATE_FLAGS (drive)->current_speed  =
			(((unsigned int)cap.curspeed) + (176/2)) / 176;
		CDROM_CONFIG_FLAGS (drive)->max_speed =
			(((unsigned int)cap.maxspeed) + (176/2)) / 176;
	} else {
		CDROM_STATE_FLAGS (drive)->current_speed  =
			(ntohs(cap.curspeed) + (176/2)) / 176;
		CDROM_CONFIG_FLAGS (drive)->max_speed =
			(ntohs(cap.maxspeed) + (176/2)) / 176;
	}

	/* don't print speed if the drive reported 0.
	 */
	printk("%s: ATAPI", drive->name);
	if (CDROM_CONFIG_FLAGS(drive)->max_speed)
		printk(" %dX", CDROM_CONFIG_FLAGS(drive)->max_speed);
	printk(" %s", CDROM_CONFIG_FLAGS(drive)->dvd ? "DVD-ROM" : "CD-ROM");

	if (CDROM_CONFIG_FLAGS (drive)->dvd_r|CDROM_CONFIG_FLAGS (drive)->dvd_ram)
		printk (" DVD%s%s",
		(CDROM_CONFIG_FLAGS (drive)->dvd_r)? "-R" : "",
		(CDROM_CONFIG_FLAGS (drive)->dvd_ram)? "-RAM" : "");

        if (CDROM_CONFIG_FLAGS (drive)->cd_r|CDROM_CONFIG_FLAGS (drive)->cd_rw)
		printk (" CD%s%s",
		(CDROM_CONFIG_FLAGS (drive)->cd_r)? "-R" : "",
		(CDROM_CONFIG_FLAGS (drive)->cd_rw)? "/RW" : "");

        if (CDROM_CONFIG_FLAGS (drive)->is_changer)
		printk (" changer w/%d slots", nslots);
        else
		printk (" drive");

	printk (", %dkB Cache", be16_to_cpu(cap.buffer_size));

#ifdef CONFIG_BLK_DEV_IDEDMA
	if (drive->using_dma)
		udma_print(drive);
#endif
	printk("\n");

	return nslots;
}


/*
 * standard prep_rq_fn that builds 10 byte cmds
 */
static int ll_10byte_cmd_build(request_queue_t *q, struct request *rq)
{
	int hard_sect = queue_hardsect_size(q);
	sector_t block = rq->hard_sector / (hard_sect >> 9);
	unsigned long blocks = rq->hard_nr_sectors / (hard_sect >> 9);

	if (!(rq->flags & REQ_CMD))
		return 0;

	if (rq->hard_nr_sectors != rq->nr_sectors) {
		printk(KERN_ERR "ide-cd: hard_nr_sectors differs from nr_sectors! %lu %lu\n",
				rq->nr_sectors, rq->hard_nr_sectors);
	}
	memset(rq->cmd, 0, sizeof(rq->cmd));

	if (rq_data_dir(rq) == READ)
		rq->cmd[0] = GPCMD_READ_10;
	else
		rq->cmd[0] = GPCMD_WRITE_10;

	/*
	 * fill in lba
	 */
	rq->cmd[2] = (block >> 24) & 0xff;
	rq->cmd[3] = (block >> 16) & 0xff;
	rq->cmd[4] = (block >>  8) & 0xff;
	rq->cmd[5] = block & 0xff;

	/*
	 * and transfer length
	 */
	rq->cmd[7] = (blocks >> 8) & 0xff;
	rq->cmd[8] = blocks & 0xff;

	return 0;
}

static int ide_cdrom_setup(struct ata_device *drive)
{
	struct cdrom_info *info = drive->driver_data;
	struct cdrom_device_info *cdi = &info->devinfo;
	int minor = drive->select.b.unit << PARTN_BITS;
	int nslots;

	/*
	 * default to read-only always and fix latter at the bottom
	 */
	set_device_ro(mk_kdev(drive->channel->major, minor), 1);
	blk_queue_hardsect_size(&drive->queue, CD_FRAMESIZE);

	blk_queue_prep_rq(&drive->queue, ll_10byte_cmd_build);

	drive->ready_stat	= 0;

	CDROM_STATE_FLAGS (drive)->media_changed = 1;
	CDROM_STATE_FLAGS (drive)->toc_valid     = 0;
	CDROM_STATE_FLAGS (drive)->door_locked   = 0;

#if NO_DOOR_LOCKING
	CDROM_CONFIG_FLAGS (drive)->no_doorlock = 1;
#else
	CDROM_CONFIG_FLAGS (drive)->no_doorlock = 0;
#endif

	if (drive->id != NULL)
		CDROM_CONFIG_FLAGS (drive)->drq_interrupt =
			((drive->id->config & 0x0060) == 0x20);
	else
		CDROM_CONFIG_FLAGS (drive)->drq_interrupt = 0;

	CDROM_CONFIG_FLAGS (drive)->is_changer = 0;
	CDROM_CONFIG_FLAGS (drive)->cd_r = 0;
	CDROM_CONFIG_FLAGS (drive)->cd_rw = 0;
	CDROM_CONFIG_FLAGS (drive)->test_write = 0;
	CDROM_CONFIG_FLAGS (drive)->dvd = 0;
	CDROM_CONFIG_FLAGS (drive)->dvd_r = 0;
	CDROM_CONFIG_FLAGS (drive)->dvd_ram = 0;
	CDROM_CONFIG_FLAGS (drive)->no_eject = 1;
	CDROM_CONFIG_FLAGS (drive)->supp_disc_present = 0;
	CDROM_CONFIG_FLAGS (drive)->audio_play = 0;
	CDROM_CONFIG_FLAGS (drive)->close_tray = 1;

	/* limit transfer size per interrupt. */
	CDROM_CONFIG_FLAGS (drive)->limit_nframes = 0;
	if (drive->id != NULL) {
		/* a testament to the nice quality of Samsung drives... */
		if (!strcmp(drive->id->model, "SAMSUNG CD-ROM SCR-2430"))
			CDROM_CONFIG_FLAGS (drive)->limit_nframes = 1;
		else if (!strcmp(drive->id->model, "SAMSUNG CD-ROM SCR-2432"))
			CDROM_CONFIG_FLAGS (drive)->limit_nframes = 1;
		/* the 3231 model does not support the SET_CD_SPEED command */
		else if (!strcmp(drive->id->model, "SAMSUNG CD-ROM SCR-3231"))
			cdi->mask |= CDC_SELECT_SPEED;
	}

#if ! STANDARD_ATAPI
	/* by default Sanyo 3 CD changer support is turned off and
           ATAPI Rev 2.2+ standard support for CD changers is used */
	cdi->sanyo_slot = 0;

	CDROM_CONFIG_FLAGS (drive)->nec260 = 0;
	CDROM_CONFIG_FLAGS (drive)->toctracks_as_bcd = 0;
	CDROM_CONFIG_FLAGS (drive)->tocaddr_as_bcd = 0;
	CDROM_CONFIG_FLAGS (drive)->playmsf_as_bcd = 0;
	CDROM_CONFIG_FLAGS (drive)->subchan_as_bcd = 0;

	if (drive->id != NULL) {
		if (strcmp (drive->id->model, "V003S0DS") == 0 &&
		    drive->id->fw_rev[4] == '1' &&
		    drive->id->fw_rev[6] <= '2') {
			/* Vertos 300.
			   Some versions of this drive like to talk BCD. */
			CDROM_CONFIG_FLAGS (drive)->toctracks_as_bcd = 1;
			CDROM_CONFIG_FLAGS (drive)->tocaddr_as_bcd = 1;
			CDROM_CONFIG_FLAGS (drive)->playmsf_as_bcd = 1;
			CDROM_CONFIG_FLAGS (drive)->subchan_as_bcd = 1;
		} else if (strcmp (drive->id->model, "V006E0DS") == 0 &&
		    drive->id->fw_rev[4] == '1' &&
		    drive->id->fw_rev[6] <= '2') {
			/* Vertos 600 ESD. */
			CDROM_CONFIG_FLAGS (drive)->toctracks_as_bcd = 1;
		} else if (strcmp (drive->id->model,
				 "NEC CD-ROM DRIVE:260") == 0 &&
			 strncmp (drive->id->fw_rev, "1.01", 4) == 0) { /* FIXME */
			/* Old NEC260 (not R).
			   This drive was released before the 1.2 version
			   of the spec. */
			CDROM_CONFIG_FLAGS (drive)->tocaddr_as_bcd = 1;
			CDROM_CONFIG_FLAGS (drive)->playmsf_as_bcd = 1;
			CDROM_CONFIG_FLAGS (drive)->subchan_as_bcd = 1;
			CDROM_CONFIG_FLAGS (drive)->nec260         = 1;
		} else if (strcmp (drive->id->model, "WEARNES CDD-120") == 0 &&
			 strncmp (drive->id->fw_rev, "A1.1", 4) == 0) { /* FIXME */
			/* Wearnes */
			CDROM_CONFIG_FLAGS (drive)->playmsf_as_bcd = 1;
			CDROM_CONFIG_FLAGS (drive)->subchan_as_bcd = 1;
		}

                /* Sanyo 3 CD changer uses a non-standard command
                    for CD changing */
                 else if ((strcmp(drive->id->model, "CD-ROM CDR-C3 G") == 0) ||
                         (strcmp(drive->id->model, "CD-ROM CDR-C3G") == 0) ||
                         (strcmp(drive->id->model, "CD-ROM CDR_C36") == 0)) {
                        /* uses CD in slot 0 when value is set to 3 */
                        cdi->sanyo_slot = 3;
                }


	}
#endif /* not STANDARD_ATAPI */

	info->toc		= NULL;
	info->buffer		= NULL;
	info->sector_buffered	= 0;
	info->nsectors_buffered	= 0;
	info->changer_info      = NULL;
	info->last_block	= 0;
	info->start_seek	= 0;

	nslots = ide_cdrom_probe_capabilities (drive);

	if (CDROM_CONFIG_FLAGS(drive)->dvd_ram)
		set_device_ro(mk_kdev(drive->channel->major, minor), 0);

	if (ide_cdrom_register (drive, nslots)) {
		printk ("%s: ide_cdrom_setup failed to register device with the cdrom driver.\n", drive->name);
		info->devinfo.handle = NULL;
		return 1;
	}

	return 0;
}

/* Forwarding functions to generic routines. */
static int ide_cdrom_ioctl(struct ata_device *drive,
		     struct inode *inode, struct file *file,
		     unsigned int cmd, unsigned long arg)
{
	return cdrom_ioctl(inode, file, cmd, arg);
}

static int ide_cdrom_open (struct inode *ip, struct file *fp, struct ata_device *drive)
{
	struct cdrom_info *info = drive->driver_data;
	int rc = -ENOMEM;

	MOD_INC_USE_COUNT;
	if (info->buffer == NULL)
		info->buffer = (char *) kmalloc(SECTOR_BUFFER_SIZE, GFP_KERNEL);
	if ((info->buffer == NULL) || (rc = cdrom_open(ip, fp))) {
		drive->usage--;
		MOD_DEC_USE_COUNT;
	}
	return rc;
}

static
void ide_cdrom_release (struct inode *inode, struct file *file,
			struct ata_device *drive)
{
	cdrom_release (inode, file);
	MOD_DEC_USE_COUNT;
}

static
int ide_cdrom_check_media_change(struct ata_device *drive)
{
	return cdrom_media_changed(mk_kdev (drive->channel->major,
			(drive->select.b.unit) << PARTN_BITS));
}

static
void ide_cdrom_revalidate(struct ata_device *drive)
{
	struct cdrom_info *info = drive->driver_data;
	struct atapi_toc *toc;
	int minor = drive->select.b.unit << PARTN_BITS;
	struct request_sense sense;

	cdrom_read_toc(drive, &sense);

	if (!CDROM_STATE_FLAGS(drive)->toc_valid)
		return;

	toc = info->toc;

	/* for general /dev/cdrom like mounting, one big disc */
	drive->part[0].nr_sects = toc->capacity * SECTORS_PER_FRAME;
	drive->channel->gd->sizes[minor] = toc->capacity * BLOCKS_PER_FRAME;
	blk_size[drive->channel->major] = drive->channel->gd->sizes;
}

static sector_t ide_cdrom_capacity(struct ata_device *drive)
{
	u32 capacity;

	if (cdrom_read_capacity(drive, &capacity, NULL))
		return 0;

	return capacity * SECTORS_PER_FRAME;
}

static
int ide_cdrom_cleanup(struct ata_device *drive)
{
	struct cdrom_info *info = drive->driver_data;
	struct cdrom_device_info *devinfo = &info->devinfo;

	if (ide_unregister_subdriver (drive))
		return 1;
	if (info->buffer != NULL)
		kfree(info->buffer);
	if (info->toc != NULL)
		kfree(info->toc);
	if (info->changer_info != NULL)
		kfree(info->changer_info);
	if (devinfo->handle == drive && unregister_cdrom (devinfo))
		printk ("%s: ide_cdrom_cleanup failed to unregister device from the cdrom driver.\n", drive->name);
	kfree(info);
	drive->driver_data = NULL;
	return 0;
}

static void ide_cdrom_attach(struct ata_device *drive);

static struct ata_operations ide_cdrom_driver = {
	.owner =		THIS_MODULE,
	.attach =		ide_cdrom_attach,
	.cleanup =		ide_cdrom_cleanup,
	.standby =		NULL,
	.do_request =		ide_cdrom_do_request,
	.end_request =		NULL,
	.ioctl =		ide_cdrom_ioctl,
	.open =			ide_cdrom_open,
	.release =		ide_cdrom_release,
	.check_media_change =	ide_cdrom_check_media_change,
	.revalidate =		ide_cdrom_revalidate,
	.capacity =		ide_cdrom_capacity,
};

/* options */
static char *ignore = NULL;

static void ide_cdrom_attach(struct ata_device *drive)
{
	struct cdrom_info *info;
	char *req;
	struct ata_channel *channel;
	int unit;

	if (drive->type != ATA_ROM)
		return;

	req = drive->driver_req;
	if (req[0] != '\0' && strcmp(req, "ide-cdrom"))
		return;

	/* skip drives that we were told to ignore */
	if (ignore && !strcmp(ignore, drive->name)) {
		printk(KERN_INFO "%s: ignored\n", drive->name);
		return;
	}
	info = (struct cdrom_info *) kmalloc (sizeof (struct cdrom_info), GFP_KERNEL);
	if (!info) {
		printk(KERN_ERR "%s: Can't allocate a cdrom structure\n", drive->name);
		return;
	}
	if (ide_register_subdriver (drive, &ide_cdrom_driver)) {
		printk(KERN_ERR "%s: Failed to register the driver with ide.c\n", drive->name);
		kfree (info);
		return;
	}


	memset(info, 0, sizeof (struct cdrom_info));
	drive->driver_data = info;

	if (ide_cdrom_setup (drive)) {
		if (ide_cdrom_cleanup (drive))
			printk (KERN_ERR "%s: ide_cdrom_cleanup failed in ide_cdrom_init\n", drive->name);
		return;
	}

	channel = drive->channel;
	unit = drive - channel->drives;

	ata_revalidate(mk_kdev(channel->major, unit << PARTN_BITS));
}

MODULE_PARM(ignore, "s");
MODULE_DESCRIPTION("ATAPI CD-ROM Driver");

static void __exit ide_cdrom_exit(void)
{
	unregister_ata_driver(&ide_cdrom_driver);
}

int ide_cdrom_init(void)
{
	return ata_driver_module(&ide_cdrom_driver);
}

module_init(ide_cdrom_init);
module_exit(ide_cdrom_exit);
MODULE_LICENSE("GPL");
