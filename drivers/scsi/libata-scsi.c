/*
   libata-scsi.c - helper library for ATA

   Copyright 2003-2004 Red Hat, Inc.  All rights reserved.
   Copyright 2003-2004 Jeff Garzik

   The contents of this file are subject to the Open
   Software License version 1.1 that can be found at
   http://www.opensource.org/licenses/osl-1.1.txt and is included herein
   by reference.

   Alternatively, the contents of this file may be used under the terms
   of the GNU General Public License version 2 (the "GPL") as distributed
   in the kernel source COPYING file, in which case the provisions of
   the GPL are applicable instead of the above.  If you wish to allow
   the use of your version of this file only under the terms of the
   GPL and not to allow others to use your version of this file under
   the OSL, indicate your decision by deleting the provisions above and
   replace them with the notice and other provisions required by the GPL.
   If you do not delete the provisions above, a recipient may use your
   version of this file under either the OSL or the GPL.

 */

#include <linux/kernel.h>
#include <linux/blkdev.h>
#include <linux/spinlock.h>
#include <scsi/scsi.h>
#include "scsi.h"
#include "hosts.h"
#include <linux/libata.h>

#include "libata.h"

typedef unsigned int (*ata_xlat_func_t)(struct ata_queued_cmd *qc, u8 *scsicmd);
static void ata_scsi_simulate(struct ata_port *ap, struct ata_device *dev,
			      struct scsi_cmnd *cmd,
			      void (*done)(struct scsi_cmnd *));


/**
 *	ata_std_bios_param - generic bios head/sector/cylinder calculator used by sd.
 *	@sdev: SCSI device for which BIOS geometry is to be determined
 *	@bdev: block device associated with @sdev
 *	@capacity: capacity of SCSI device
 *	@geom: location to which geometry will be output
 *
 *	Generic bios head/sector/cylinder calculator
 *	used by sd. Most BIOSes nowadays expect a XXX/255/16  (CHS) 
 *	mapping. Some situations may arise where the disk is not 
 *	bootable if this is not used.
 *
 *	LOCKING:
 *	Defined by the SCSI layer.  We don't really care.
 *
 *	RETURNS:
 *	Zero.
 */
int ata_std_bios_param(struct scsi_device *sdev, struct block_device *bdev,
		       sector_t capacity, int geom[]) 
{
	geom[0] = 255;
	geom[1] = 63;
	sector_div(capacity, 255*63);
	geom[2] = capacity;

	return 0;
}


/**
 *	ata_scsi_qc_new - acquire new ata_queued_cmd reference
 *	@ap: ATA port to which the new command is attached
 *	@dev: ATA device to which the new command is attached
 *	@cmd: SCSI command that originated this ATA command
 *	@done: SCSI command completion function
 *
 *	Obtain a reference to an unused ata_queued_cmd structure,
 *	which is the basic libata structure representing a single
 *	ATA command sent to the hardware.
 *
 *	If a command was available, fill in the SCSI-specific
 *	portions of the structure with information on the
 *	current command.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 *
 *	RETURNS:
 *	Command allocated, or %NULL if none available.
 */
struct ata_queued_cmd *ata_scsi_qc_new(struct ata_port *ap,
				       struct ata_device *dev,
				       struct scsi_cmnd *cmd,
				       void (*done)(struct scsi_cmnd *))
{
	struct ata_queued_cmd *qc;

	qc = ata_qc_new_init(ap, dev);
	if (qc) {
		qc->scsicmd = cmd;
		qc->scsidone = done;

		if (cmd->use_sg) {
			qc->sg = (struct scatterlist *) cmd->request_buffer;
			qc->n_elem = cmd->use_sg;
		} else {
			qc->sg = &qc->sgent;
			qc->n_elem = 1;
		}
	} else {
		cmd->result = (DID_OK << 16) | (QUEUE_FULL << 1);
		done(cmd);
	}

	return qc;
}

/**
 *	ata_to_sense_error - convert ATA error to SCSI error
 *	@qc: Command that we are erroring out
 *
 *	Converts an ATA error into a SCSI error.
 *
 *	Right now, this routine is laughably primitive.  We
 *	don't even examine what ATA told us, we just look at
 *	the command data direction, and return a fatal SCSI
 *	sense error based on that.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

void ata_to_sense_error(struct ata_queued_cmd *qc)
{
	struct scsi_cmnd *cmd = qc->scsicmd;

	cmd->result = SAM_STAT_CHECK_CONDITION;

	cmd->sense_buffer[0] = 0x70;
	cmd->sense_buffer[2] = MEDIUM_ERROR;
	cmd->sense_buffer[7] = 14 - 8;	/* addnl. sense len. FIXME: correct? */

	/* additional-sense-code[-qualifier] */
	if (cmd->sc_data_direction == SCSI_DATA_READ) {
		cmd->sense_buffer[12] = 0x11; /* "unrecovered read error" */
		cmd->sense_buffer[13] = 0x04;
	} else {
		cmd->sense_buffer[12] = 0x0C; /* "write error -             */
		cmd->sense_buffer[13] = 0x02; /*  auto-reallocation failed" */
	}
}

/**
 *	ata_scsi_slave_config - Set SCSI device attributes
 *	@sdev: SCSI device to examine
 *
 *	This is called before we actually start reading
 *	and writing to the device, to configure certain
 *	SCSI mid-layer behaviors.
 *
 *	LOCKING:
 *	Defined by SCSI layer.  We don't really care.
 */

int ata_scsi_slave_config(struct scsi_device *sdev)
{
	sdev->use_10_for_rw = 1;
	sdev->use_10_for_ms = 1;
	blk_queue_max_phys_segments(sdev->request_queue, LIBATA_MAX_PRD);

	return 0;	/* scsi layer doesn't check return value, sigh */
}

/**
 *	ata_scsi_error - SCSI layer error handler callback
 *	@host: SCSI host on which error occurred
 *
 *	Handles SCSI-layer-thrown error events.
 *
 *	LOCKING:
 *	Inherited from SCSI layer (none, can sleep)
 *
 *	RETURNS:
 *	Zero.
 */

int ata_scsi_error(struct Scsi_Host *host)
{
	struct ata_port *ap;

	DPRINTK("ENTER\n");

	ap = (struct ata_port *) &host->hostdata[0];
	ap->ops->eng_timeout(ap);

	DPRINTK("EXIT\n");
	return 0;
}

/**
 *	ata_scsi_rw_xlat - Translate SCSI r/w command into an ATA one
 *	@qc: Storage for translated ATA taskfile
 *	@scsicmd: SCSI command to translate
 *
 *	Converts any of six SCSI read/write commands into the
 *	ATA counterpart, including starting sector (LBA),
 *	sector count, and taking into account the device's LBA48
 *	support.
 *
 *	Commands %READ_6, %READ_10, %READ_16, %WRITE_6, %WRITE_10, and
 *	%WRITE_16 are currently supported.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 *
 *	RETURNS:
 *	Zero on success, non-zero on error.
 */

static unsigned int ata_scsi_rw_xlat(struct ata_queued_cmd *qc, u8 *scsicmd)
{
	struct ata_taskfile *tf = &qc->tf;
	unsigned int lba48 = tf->flags & ATA_TFLAG_LBA48;

	tf->flags |= ATA_TFLAG_ISADDR | ATA_TFLAG_DEVICE;
	tf->hob_nsect = 0;
	tf->hob_lbal = 0;
	tf->hob_lbam = 0;
	tf->hob_lbah = 0;
	tf->protocol = qc->dev->xfer_protocol;
	tf->device |= ATA_LBA;

	if (scsicmd[0] == READ_10 || scsicmd[0] == READ_6 ||
	    scsicmd[0] == READ_16) {
		tf->command = qc->dev->read_cmd;
	} else {
		tf->command = qc->dev->write_cmd;
		tf->flags |= ATA_TFLAG_WRITE;
	}

	if (scsicmd[0] == READ_10 || scsicmd[0] == WRITE_10) {
		if (lba48) {
			tf->hob_nsect = scsicmd[7];
			tf->hob_lbal = scsicmd[2];

			qc->nsect = ((unsigned int)scsicmd[7] << 8) |
					scsicmd[8];
		} else {
			/* if we don't support LBA48 addressing, the request
			 * -may- be too large. */
			if ((scsicmd[2] & 0xf0) || scsicmd[7])
				return 1;

			/* stores LBA27:24 in lower 4 bits of device reg */
			tf->device |= scsicmd[2];

			qc->nsect = scsicmd[8];
		}

		tf->nsect = scsicmd[8];
		tf->lbal = scsicmd[5];
		tf->lbam = scsicmd[4];
		tf->lbah = scsicmd[3];

		VPRINTK("ten-byte command\n");
		return 0;
	}

	if (scsicmd[0] == READ_6 || scsicmd[0] == WRITE_6) {
		qc->nsect = tf->nsect = scsicmd[4];
		tf->lbal = scsicmd[3];
		tf->lbam = scsicmd[2];
		tf->lbah = scsicmd[1] & 0x1f; /* mask out reserved bits */

		VPRINTK("six-byte command\n");
		return 0;
	}

	if (scsicmd[0] == READ_16 || scsicmd[0] == WRITE_16) {
		/* rule out impossible LBAs and sector counts */
		if (scsicmd[2] || scsicmd[3] || scsicmd[10] || scsicmd[11])
			return 1;

		if (lba48) {
			tf->hob_nsect = scsicmd[12];
			tf->hob_lbal = scsicmd[6];
			tf->hob_lbam = scsicmd[5];
			tf->hob_lbah = scsicmd[4];

			qc->nsect = ((unsigned int)scsicmd[12] << 8) |
					scsicmd[13];
		} else {
			/* once again, filter out impossible non-zero values */
			if (scsicmd[4] || scsicmd[5] || scsicmd[12] ||
			    (scsicmd[6] & 0xf0))
				return 1;

			/* stores LBA27:24 in lower 4 bits of device reg */
			tf->device |= scsicmd[2];

			qc->nsect = scsicmd[13];
		}

		tf->nsect = scsicmd[13];
		tf->lbal = scsicmd[9];
		tf->lbam = scsicmd[8];
		tf->lbah = scsicmd[7];

		VPRINTK("sixteen-byte command\n");
		return 0;
	}

	DPRINTK("no-byte command\n");
	return 1;
}

/**
 *	ata_scsi_translate - Translate then issue SCSI command to ATA device
 *	@ap: ATA port to which the command is addressed
 *	@dev: ATA device to which the command is addressed
 *	@cmd: SCSI command to execute
 *	@done: SCSI command completion function
 *
 *	Our ->queuecommand() function has decided that the SCSI
 *	command issued can be directly translated into an ATA
 *	command, rather than handled internally.
 *
 *	This function sets up an ata_queued_cmd structure for the
 *	SCSI command, and sends that ata_queued_cmd to the hardware.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

static void ata_scsi_translate(struct ata_port *ap, struct ata_device *dev,
			      struct scsi_cmnd *cmd,
			      void (*done)(struct scsi_cmnd *),
			      ata_xlat_func_t xlat_func)
{
	struct ata_queued_cmd *qc;
	u8 *scsicmd = cmd->cmnd;

	VPRINTK("ENTER\n");

	if (unlikely(cmd->request_bufflen < 1)) {
		printk(KERN_WARNING "ata%u(%u): empty request buffer\n",
		       ap->id, dev->devno);
		goto err_out;
	}

	qc = ata_scsi_qc_new(ap, dev, cmd, done);
	if (!qc)
		return;

	if (cmd->sc_data_direction == SCSI_DATA_READ ||
	    cmd->sc_data_direction == SCSI_DATA_WRITE)
		qc->flags |= ATA_QCFLAG_SG; /* data is present; dma-map it */

	if (xlat_func(qc, scsicmd))
		goto err_out;

	/* select device, send command to hardware */
	if (ata_qc_issue(qc))
		goto err_out;

	VPRINTK("EXIT\n");
	return;

err_out:
	ata_bad_cdb(cmd, done);
	DPRINTK("EXIT - badcmd\n");
}

/**
 *	ata_scsi_rbuf_get - Map response buffer.
 *	@cmd: SCSI command containing buffer to be mapped.
 *	@buf_out: Pointer to mapped area.
 *
 *	Maps buffer contained within SCSI command @cmd.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 *
 *	RETURNS:
 *	Length of response buffer.
 */

static unsigned int ata_scsi_rbuf_get(struct scsi_cmnd *cmd, u8 **buf_out)
{
	u8 *buf;
	unsigned int buflen;

	if (cmd->use_sg) {
		struct scatterlist *sg;

		sg = (struct scatterlist *) cmd->request_buffer;
		buf = kmap_atomic(sg->page, KM_USER0) + sg->offset;
		buflen = sg->length;
	} else {
		buf = cmd->request_buffer;
		buflen = cmd->request_bufflen;
	}

	memset(buf, 0, buflen);
	*buf_out = buf;
	return buflen;
}

/**
 *	ata_scsi_rbuf_put - Unmap response buffer.
 *	@cmd: SCSI command containing buffer to be unmapped.
 *
 *	Unmaps response buffer contained within @cmd.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

static inline void ata_scsi_rbuf_put(struct scsi_cmnd *cmd)
{
	if (cmd->use_sg) {
		struct scatterlist *sg;

		sg = (struct scatterlist *) cmd->request_buffer;
		kunmap_atomic(sg->page, KM_USER0);
	}
}

/**
 *	ata_scsi_rbuf_fill - wrapper for SCSI command simulators
 *	@args: Port / device / SCSI command of interest.
 *	@actor: Callback hook for desired SCSI command simulator
 *
 *	Takes care of the hard work of simulating a SCSI command...
 *	Mapping the response buffer, calling the command's handler,
 *	and handling the handler's return value.  This return value
 *	indicates whether the handler wishes the SCSI command to be
 *	completed successfully, or not.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

void ata_scsi_rbuf_fill(struct ata_scsi_args *args,
		        unsigned int (*actor) (struct ata_scsi_args *args,
			     		   u8 *rbuf, unsigned int buflen))
{
	u8 *rbuf;
	unsigned int buflen, rc;
	struct scsi_cmnd *cmd = args->cmd;

	buflen = ata_scsi_rbuf_get(cmd, &rbuf);
	rc = actor(args, rbuf, buflen);
	ata_scsi_rbuf_put(cmd);

	if (rc)
		ata_bad_cdb(cmd, args->done);
	else {
		cmd->result = SAM_STAT_GOOD;
		args->done(cmd);
	}
}

/**
 *	ata_scsiop_inq_std - Simulate INQUIRY command
 *	@args: Port / device / SCSI command of interest.
 *	@rbuf: Response buffer, to which simulated SCSI cmd output is sent.
 *	@buflen: Response buffer length.
 *
 *	Returns standard device identification data associated
 *	with non-EVPD INQUIRY command output.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

unsigned int ata_scsiop_inq_std(struct ata_scsi_args *args, u8 *rbuf,
			       unsigned int buflen)
{
	const u8 hdr[] = {
		TYPE_DISK,
		0,
		0x5,	/* claim SPC-3 version compatibility */
		2,
		96 - 4
	};

	VPRINTK("ENTER\n");

	memcpy(rbuf, hdr, sizeof(hdr));

	if (buflen > 36) {
		memcpy(&rbuf[8], args->dev->vendor, 8);
		memcpy(&rbuf[16], args->dev->product, 16);
		memcpy(&rbuf[32], DRV_VERSION, 4);
	}

	if (buflen > 63) {
		const u8 versions[] = {
			0x60,	/* SAM-3 (no version claimed) */

			0x03,
			0x20,	/* SBC-2 (no version claimed) */

			0x02,
			0x60	/* SPC-3 (no version claimed) */
		};

		memcpy(rbuf + 59, versions, sizeof(versions));
	}

	return 0;
}

/**
 *	ata_scsiop_inq_00 - Simulate INQUIRY EVPD page 0, list of pages
 *	@args: Port / device / SCSI command of interest.
 *	@rbuf: Response buffer, to which simulated SCSI cmd output is sent.
 *	@buflen: Response buffer length.
 *
 *	Returns list of inquiry EVPD pages available.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

unsigned int ata_scsiop_inq_00(struct ata_scsi_args *args, u8 *rbuf,
			      unsigned int buflen)
{
	const u8 pages[] = {
		0x00,	/* page 0x00, this page */
		0x80,	/* page 0x80, unit serial no page */
		0x83	/* page 0x83, device ident page */
	};
	rbuf[3] = sizeof(pages);	/* number of supported EVPD pages */

	if (buflen > 6)
		memcpy(rbuf + 4, pages, sizeof(pages));

	return 0;
}

/**
 *	ata_scsiop_inq_80 - Simulate INQUIRY EVPD page 80, device serial number
 *	@args: Port / device / SCSI command of interest.
 *	@rbuf: Response buffer, to which simulated SCSI cmd output is sent.
 *	@buflen: Response buffer length.
 *
 *	Returns ATA device serial number.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

unsigned int ata_scsiop_inq_80(struct ata_scsi_args *args, u8 *rbuf,
			      unsigned int buflen)
{
	const u8 hdr[] = {
		0,
		0x80,			/* this page code */
		0,
		ATA_SERNO_LEN,		/* page len */
	};
	memcpy(rbuf, hdr, sizeof(hdr));

	if (buflen > (ATA_SERNO_LEN + 4))
		ata_dev_id_string(args->dev, (unsigned char *) &rbuf[4],
				  ATA_ID_SERNO_OFS, ATA_SERNO_LEN);

	return 0;
}

static const char *inq_83_str = "Linux ATA-SCSI simulator";

/**
 *	ata_scsiop_inq_83 - Simulate INQUIRY EVPD page 83, device identity
 *	@args: Port / device / SCSI command of interest.
 *	@rbuf: Response buffer, to which simulated SCSI cmd output is sent.
 *	@buflen: Response buffer length.
 *
 *	Returns device identification.  Currently hardcoded to
 *	return "Linux ATA-SCSI simulator".
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

unsigned int ata_scsiop_inq_83(struct ata_scsi_args *args, u8 *rbuf,
			      unsigned int buflen)
{
	rbuf[1] = 0x83;			/* this page code */
	rbuf[3] = 4 + strlen(inq_83_str);	/* page len */

	/* our one and only identification descriptor (vendor-specific) */
	if (buflen > (strlen(inq_83_str) + 4 + 4)) {
		rbuf[4 + 0] = 2;	/* code set: ASCII */
		rbuf[4 + 3] = strlen(inq_83_str);
		memcpy(rbuf + 4 + 4, inq_83_str, strlen(inq_83_str));
	}

	return 0;
}

/**
 *	ata_scsiop_noop -
 *	@args: Port / device / SCSI command of interest.
 *	@rbuf: Response buffer, to which simulated SCSI cmd output is sent.
 *	@buflen: Response buffer length.
 *
 *	No operation.  Simply returns success to caller, to indicate
 *	that the caller should successfully complete this SCSI command.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

unsigned int ata_scsiop_noop(struct ata_scsi_args *args, u8 *rbuf,
			    unsigned int buflen)
{
	VPRINTK("ENTER\n");
	return 0;
}

/**
 *	ata_msense_push - Push data onto MODE SENSE data output buffer
 *	@ptr_io: (input/output) Location to store more output data
 *	@last: End of output data buffer
 *	@buf: Pointer to BLOB being added to output buffer
 *	@buflen: Length of BLOB
 *
 *	Store MODE SENSE data on an output buffer.
 *
 *	LOCKING:
 *	None.
 */

static void ata_msense_push(u8 **ptr_io, const u8 *last,
			    const u8 *buf, unsigned int buflen)
{
	u8 *ptr = *ptr_io;

	if ((ptr + buflen - 1) > last)
		return;

	memcpy(ptr, buf, buflen);

	ptr += buflen;

	*ptr_io = ptr;
}

/**
 *	ata_msense_caching - Simulate MODE SENSE caching info page
 *	@dev: Device associated with this MODE SENSE command
 *	@ptr_io: (input/output) Location to store more output data
 *	@last: End of output data buffer
 *
 *	Generate a caching info page, which conditionally indicates
 *	write caching to the SCSI layer, depending on device
 *	capabilities.
 *
 *	LOCKING:
 *	None.
 */

static unsigned int ata_msense_caching(struct ata_device *dev, u8 **ptr_io,
				       const u8 *last)
{
	u8 page[7] = { 0xf, 0, 0x10, 0, 0x8, 0xa, 0 };
	if (dev->flags & ATA_DFLAG_WCACHE)
		page[6] = 0x4;

	ata_msense_push(ptr_io, last, page, sizeof(page));
	return sizeof(page);
}

/**
 *	ata_msense_ctl_mode - Simulate MODE SENSE control mode page
 *	@dev: Device associated with this MODE SENSE command
 *	@ptr_io: (input/output) Location to store more output data
 *	@last: End of output data buffer
 *
 *	Generate a generic MODE SENSE control mode page.
 *
 *	LOCKING:
 *	None.
 */

static unsigned int ata_msense_ctl_mode(u8 **ptr_io, const u8 *last)
{
	const u8 page[] = {0xa, 0xa, 2, 0, 0, 0, 0, 0, 0xff, 0xff, 0, 30};

	ata_msense_push(ptr_io, last, page, sizeof(page));
	return sizeof(page);
}

/**
 *	ata_scsiop_mode_sense - Simulate MODE SENSE 6, 10 commands
 *	@args: Port / device / SCSI command of interest.
 *	@rbuf: Response buffer, to which simulated SCSI cmd output is sent.
 *	@buflen: Response buffer length.
 *
 *	Simulate MODE SENSE commands.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

unsigned int ata_scsiop_mode_sense(struct ata_scsi_args *args, u8 *rbuf,
				  unsigned int buflen)
{
	u8 *scsicmd = args->cmd->cmnd, *p, *last;
	struct ata_device *dev = args->dev;
	unsigned int page_control, six_byte, output_len;

	VPRINTK("ENTER\n");

	six_byte = (scsicmd[0] == MODE_SENSE);

	/* we only support saved and current values (which we treat
	 * in the same manner)
	 */
	page_control = scsicmd[2] >> 6;
	if ((page_control != 0) && (page_control != 3))
		return 1;

	if (six_byte)
		output_len = 4;
	else
		output_len = 8;

	p = rbuf + output_len;
	last = rbuf + buflen - 1;

	switch(scsicmd[2] & 0x3f) {
	case 0x08:		/* caching */
		output_len += ata_msense_caching(dev, &p, last);
		break;

	case 0x0a: {		/* control mode */
		output_len += ata_msense_ctl_mode(&p, last);
		break;
		}

	case 0x3f:		/* all pages */
		output_len += ata_msense_caching(dev, &p, last);
		output_len += ata_msense_ctl_mode(&p, last);
		break;

	default:		/* invalid page code */
		return 1;
	}

	if (six_byte) {
		output_len--;
		rbuf[0] = output_len;
	} else {
		output_len -= 2;
		rbuf[0] = output_len >> 8;
		rbuf[1] = output_len;
	}

	return 0;
}

/**
 *	ata_scsiop_read_cap - Simulate READ CAPACITY[ 16] commands
 *	@args: Port / device / SCSI command of interest.
 *	@rbuf: Response buffer, to which simulated SCSI cmd output is sent.
 *	@buflen: Response buffer length.
 *
 *	Simulate READ CAPACITY commands.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

unsigned int ata_scsiop_read_cap(struct ata_scsi_args *args, u8 *rbuf,
			        unsigned int buflen)
{
	u64 n_sectors = args->dev->n_sectors;
	u32 tmp;

	VPRINTK("ENTER\n");

	n_sectors--;		/* one off */

	tmp = n_sectors;	/* note: truncates, if lba48 */
	if (args->cmd->cmnd[0] == READ_CAPACITY) {
		rbuf[0] = tmp >> (8 * 3);
		rbuf[1] = tmp >> (8 * 2);
		rbuf[2] = tmp >> (8 * 1);
		rbuf[3] = tmp;

		tmp = ATA_SECT_SIZE;
		rbuf[6] = tmp >> 8;
		rbuf[7] = tmp;

	} else {
		rbuf[2] = n_sectors >> (8 * 7);
		rbuf[3] = n_sectors >> (8 * 6);
		rbuf[4] = n_sectors >> (8 * 5);
		rbuf[5] = n_sectors >> (8 * 4);
		rbuf[6] = tmp >> (8 * 3);
		rbuf[7] = tmp >> (8 * 2);
		rbuf[8] = tmp >> (8 * 1);
		rbuf[9] = tmp;

		tmp = ATA_SECT_SIZE;
		rbuf[12] = tmp >> 8;
		rbuf[13] = tmp;
	}

	return 0;
}

/**
 *	ata_scsiop_report_luns - Simulate REPORT LUNS command
 *	@args: Port / device / SCSI command of interest.
 *	@rbuf: Response buffer, to which simulated SCSI cmd output is sent.
 *	@buflen: Response buffer length.
 *
 *	Simulate REPORT LUNS command.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

unsigned int ata_scsiop_report_luns(struct ata_scsi_args *args, u8 *rbuf,
				   unsigned int buflen)
{
	VPRINTK("ENTER\n");
	rbuf[3] = 8;	/* just one lun, LUN 0, size 8 bytes */

	return 0;
}

/**
 *	ata_scsi_badcmd - End a SCSI request with an error
 *	@cmd: SCSI request to be handled
 *	@done: SCSI command completion function
 *	@asc: SCSI-defined additional sense code
 *	@ascq: SCSI-defined additional sense code qualifier
 *
 *	Helper function that completes a SCSI command with
 *	%SAM_STAT_CHECK_CONDITION, with a sense key %ILLEGAL_REQUEST
 *	and the specified additional sense codes.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

void ata_scsi_badcmd(struct scsi_cmnd *cmd, void (*done)(struct scsi_cmnd *), u8 asc, u8 ascq)
{
	DPRINTK("ENTER\n");
	cmd->result = SAM_STAT_CHECK_CONDITION;

	cmd->sense_buffer[0] = 0x70;
	cmd->sense_buffer[2] = ILLEGAL_REQUEST;
	cmd->sense_buffer[7] = 14 - 8;	/* addnl. sense len. FIXME: correct? */
	cmd->sense_buffer[12] = asc;
	cmd->sense_buffer[13] = ascq;

	done(cmd);
}

/**
 *	atapi_scsi_queuecmd - Send CDB to ATAPI device
 *	@ap: Port to which ATAPI device is attached.
 *	@dev: Target device for CDB.
 *	@cmd: SCSI command being sent to device.
 *	@done: SCSI command completion function.
 *
 *	Sends CDB to ATAPI device.  If the Linux SCSI layer sends a
 *	non-data command, then this function handles the command
 *	directly, via polling.  Otherwise, the bmdma engine is started.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

static void atapi_scsi_queuecmd(struct ata_port *ap, struct ata_device *dev,
			       struct scsi_cmnd *cmd, void (*done)(struct scsi_cmnd *))
{
	struct ata_queued_cmd *qc;
	u8 *scsicmd = cmd->cmnd;

	VPRINTK("ENTER, drv_stat = 0x%x\n", ata_chk_status(ap));

	if (cmd->sc_data_direction == SCSI_DATA_UNKNOWN) {
		DPRINTK("unknown data, scsicmd 0x%x\n", scsicmd[0]);
		ata_bad_cdb(cmd, done);
		return;
	}

	switch(scsicmd[0]) {
	case READ_6:
	case WRITE_6:
	case MODE_SELECT:
	case MODE_SENSE:
		DPRINTK("read6/write6/modesel/modesense trap\n");
		ata_bad_scsiop(cmd, done);
		return;

	default:
		/* do nothing */
		break;
	}

	qc = ata_scsi_qc_new(ap, dev, cmd, done);
	if (!qc) {
		printk(KERN_ERR "ata%u: command queue empty\n", ap->id);
		return;
	}

	qc->flags |= ATA_QCFLAG_ATAPI;

	qc->tf.flags |= ATA_TFLAG_ISADDR | ATA_TFLAG_DEVICE;
	if (cmd->sc_data_direction == SCSI_DATA_WRITE) {
		qc->tf.flags |= ATA_TFLAG_WRITE;
		DPRINTK("direction: write\n");
	}

	qc->tf.command = ATA_CMD_PACKET;

	if (cmd->sc_data_direction == SCSI_DATA_NONE) {
		qc->tf.protocol = ATA_PROT_ATAPI;
		qc->flags |= ATA_QCFLAG_POLL;
		qc->tf.ctl |= ATA_NIEN;	/* disable interrupts */
	} else {
		qc->tf.protocol = ATA_PROT_ATAPI_DMA;
		qc->flags |= ATA_QCFLAG_SG; /* data is present; dma-map it */
		qc->tf.feature |= ATAPI_PKT_DMA;
	}

	atapi_start(qc);
}

/**
 *	ata_scsi_find_dev - lookup ata_device from scsi_cmnd
 *	@ap: ATA port to which the device is attached
 *	@cmd: SCSI command to be sent to the device
 *
 *	Given various information provided in struct scsi_cmnd,
 *	map that onto an ATA bus, and using that mapping
 *	determine which ata_device is associated with the
 *	SCSI command to be sent.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 *
 *	RETURNS:
 *	Associated ATA device, or %NULL if not found.
 */

static inline struct ata_device *
ata_scsi_find_dev(struct ata_port *ap, struct scsi_cmnd *cmd)
{
	struct ata_device *dev;

	/* skip commands not addressed to targets we simulate */
	if (likely(cmd->device->id < ATA_MAX_DEVICES))
		dev = &ap->device[cmd->device->id];
	else
		return NULL;

	if (unlikely((cmd->device->channel != 0) ||
		     (cmd->device->lun != 0)))
		return NULL;

	if (unlikely(!ata_dev_present(dev)))
		return NULL;

#ifndef ATA_ENABLE_ATAPI
	if (unlikely(dev->class == ATA_DEV_ATAPI))
		return NULL;
#endif

	return dev;
}

/**
 *	ata_get_xlat_func - check if SCSI to ATA translation is possible
 *	@cmd: SCSI command opcode to consider
 *
 *	Look up the SCSI command given, and determine whether the
 *	SCSI command is to be translated or simulated.
 *
 *	RETURNS:
 *	Pointer to translation function if possible, %NULL if not.
 */

static inline ata_xlat_func_t ata_get_xlat_func(u8 cmd)
{
	switch (cmd) {
	case READ_6:
	case READ_10:
	case READ_16:

	case WRITE_6:
	case WRITE_10:
	case WRITE_16:
		return ata_scsi_rw_xlat;
	}

	return NULL;
}

/**
 *	ata_scsi_dump_cdb - dump SCSI command contents to dmesg
 *	@ap: ATA port to which the command was being sent
 *	@cmd: SCSI command to dump
 *
 *	Prints the contents of a SCSI command via printk().
 */

static inline void ata_scsi_dump_cdb(struct ata_port *ap,
				     struct scsi_cmnd *cmd)
{
#ifdef ATA_DEBUG
	u8 *scsicmd = cmd->cmnd;

	DPRINTK("CDB (%u:%d,%d,%d) %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
		ap->id,
		cmd->device->channel, cmd->device->id, cmd->device->lun,
		scsicmd[0], scsicmd[1], scsicmd[2], scsicmd[3],
		scsicmd[4], scsicmd[5], scsicmd[6], scsicmd[7],
		scsicmd[8]);
#endif
}

/**
 *	ata_scsi_queuecmd - Issue SCSI cdb to libata-managed device
 *	@cmd: SCSI command to be sent
 *	@done: Completion function, called when command is complete
 *
 *	In some cases, this function translates SCSI commands into
 *	ATA taskfiles, and queues the taskfiles to be sent to
 *	hardware.  In other cases, this function simulates a
 *	SCSI device by evaluating and responding to certain
 *	SCSI commands.  This creates the overall effect of
 *	ATA and ATAPI devices appearing as SCSI devices.
 *
 *	LOCKING:
 *	Releases scsi-layer-held lock, and obtains host_set lock.
 *
 *	RETURNS:
 *	Zero.
 */

int ata_scsi_queuecmd(struct scsi_cmnd *cmd, void (*done)(struct scsi_cmnd *))
{
	struct ata_port *ap;
	struct ata_device *dev;

	ap = (struct ata_port *) &cmd->device->host->hostdata[0];

	ata_scsi_dump_cdb(ap, cmd);

	dev = ata_scsi_find_dev(ap, cmd);
	if (unlikely(!dev)) {
		cmd->result = (DID_BAD_TARGET << 16);
		done(cmd);
		goto out_unlock;
	}

	if (dev->class == ATA_DEV_ATA) {
		ata_xlat_func_t xlat_func = ata_get_xlat_func(cmd->cmnd[0]);

		if (xlat_func)
			ata_scsi_translate(ap, dev, cmd, done, xlat_func);
		else
			ata_scsi_simulate(ap, dev, cmd, done);
	} else
		atapi_scsi_queuecmd(ap, dev, cmd, done);

out_unlock:
	return 0;
}

/**
 *	ata_scsi_simulate - simulate SCSI command on ATA device
 *	@ap: Port to which ATA device is attached.
 *	@dev: Target device for CDB.
 *	@cmd: SCSI command being sent to device.
 *	@done: SCSI command completion function.
 *
 *	Interprets and directly executes a select list of SCSI commands
 *	that can be handled internally.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

static void ata_scsi_simulate(struct ata_port *ap, struct ata_device *dev,
			      struct scsi_cmnd *cmd,
			      void (*done)(struct scsi_cmnd *))
{
	struct ata_scsi_args args;
	u8 *scsicmd = cmd->cmnd;

	args.ap = ap;
	args.dev = dev;
	args.cmd = cmd;
	args.done = done;

	switch(scsicmd[0]) {
		case TEST_UNIT_READY:		/* FIXME: correct? */
		case FORMAT_UNIT:		/* FIXME: correct? */
		case SEND_DIAGNOSTIC:		/* FIXME: correct? */
			ata_scsi_rbuf_fill(&args, ata_scsiop_noop);
			break;

		case INQUIRY:
			if (scsicmd[1] & 2)	           /* is CmdDt set?  */
				ata_bad_cdb(cmd, done);
			else if ((scsicmd[1] & 1) == 0)    /* is EVPD clear? */
				ata_scsi_rbuf_fill(&args, ata_scsiop_inq_std);
			else if (scsicmd[2] == 0x00)
				ata_scsi_rbuf_fill(&args, ata_scsiop_inq_00);
			else if (scsicmd[2] == 0x80)
				ata_scsi_rbuf_fill(&args, ata_scsiop_inq_80);
			else if (scsicmd[2] == 0x83)
				ata_scsi_rbuf_fill(&args, ata_scsiop_inq_83);
			else
				ata_bad_cdb(cmd, done);
			break;

		case MODE_SENSE:
		case MODE_SENSE_10:
			ata_scsi_rbuf_fill(&args, ata_scsiop_mode_sense);
			break;

		case MODE_SELECT:	/* unconditionally return */
		case MODE_SELECT_10:	/* bad-field-in-cdb */
			ata_bad_cdb(cmd, done);
			break;

		case READ_CAPACITY:
			ata_scsi_rbuf_fill(&args, ata_scsiop_read_cap);
			break;

		case SERVICE_ACTION_IN:
			if ((scsicmd[1] & 0x1f) == SAI_READ_CAPACITY_16)
				ata_scsi_rbuf_fill(&args, ata_scsiop_read_cap);
			else
				ata_bad_cdb(cmd, done);
			break;

		case REPORT_LUNS:
			ata_scsi_rbuf_fill(&args, ata_scsiop_report_luns);
			break;

		/* mandantory commands we haven't implemented yet */
		case REQUEST_SENSE:

		/* all other commands */
		default:
			ata_bad_scsiop(cmd, done);
			break;
	}
}

