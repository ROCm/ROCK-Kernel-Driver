/*
 * Copyright (C) 2001 Jens Axboe <axboe@suse.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public Licens
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-
 *
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/completion.h>
#include <linux/cdrom.h>
#include <linux/slab.h>
#include <linux/times.h>
#include <asm/uaccess.h>

#include <scsi/scsi.h>
#include <scsi/scsi_ioctl.h>
#include <scsi/scsi_cmnd.h>

/* Command group 3 is reserved and should never be used.  */
const unsigned char scsi_command_size[8] =
{
	6, 10, 10, 12,
	16, 12, 10, 10
};

EXPORT_SYMBOL(scsi_command_size);

#define BLK_DEFAULT_TIMEOUT	(60 * HZ)

#include <scsi/sg.h>

static int sg_get_version(int __user *p)
{
	static int sg_version_num = 30527;
	return put_user(sg_version_num, p);
}

static int scsi_get_idlun(request_queue_t *q, int __user *p)
{
	return put_user(0, p);
}

static int scsi_get_bus(request_queue_t *q, int __user *p)
{
	return put_user(0, p);
}

static int sg_get_timeout(request_queue_t *q)
{
	return q->sg_timeout / (HZ / USER_HZ);
}

static int sg_set_timeout(request_queue_t *q, int __user *p)
{
	int timeout, err = get_user(timeout, p);

	if (!err)
		q->sg_timeout = timeout * (HZ / USER_HZ);

	return err;
}

static int sg_get_reserved_size(request_queue_t *q, int __user *p)
{
	return put_user(q->sg_reserved_size, p);
}

static int sg_set_reserved_size(request_queue_t *q, int __user *p)
{
	int size, err = get_user(size, p);

	if (err)
		return err;

	if (size < 0)
		return -EINVAL;
	if (size > (q->max_sectors << 9))
		return -EINVAL;

	q->sg_reserved_size = size;
	return 0;
}

/*
 * will always return that we are ATAPI even for a real SCSI drive, I'm not
 * so sure this is worth doing anything about (why would you care??)
 */
static int sg_emulated_host(request_queue_t *q, int __user *p)
{
	return put_user(1, p);
}

static int sg_io(request_queue_t *q, struct gendisk *bd_disk,
		 struct sg_io_hdr *hdr)
{
	unsigned long start_time;
	int reading, writing;
	struct request *rq;
	struct bio *bio;
	char sense[SCSI_SENSE_BUFFERSIZE];
	unsigned char cmd[BLK_MAX_CDB];

	if (hdr->interface_id != 'S')
		return -EINVAL;
	if (hdr->cmd_len > BLK_MAX_CDB)
		return -EINVAL;
	if (copy_from_user(cmd, hdr->cmdp, hdr->cmd_len))
		return -EFAULT;

	/*
	 * we'll do that later
	 */
	if (hdr->iovec_count)
		return -EOPNOTSUPP;

	if (hdr->dxfer_len > (q->max_sectors << 9))
		return -EIO;

	reading = writing = 0;
	if (hdr->dxfer_len) {
		switch (hdr->dxfer_direction) {
		default:
			return -EINVAL;
		case SG_DXFER_TO_FROM_DEV:
			reading = 1;
			/* fall through */
		case SG_DXFER_TO_DEV:
			writing = 1;
			break;
		case SG_DXFER_FROM_DEV:
			reading = 1;
			break;
		}

		rq = blk_rq_map_user(q, writing ? WRITE : READ, hdr->dxferp,
				     hdr->dxfer_len);

		if (IS_ERR(rq))
			return PTR_ERR(rq);
	} else
		rq = blk_get_request(q, READ, __GFP_WAIT);

	/*
	 * fill in request structure
	 */
	rq->cmd_len = hdr->cmd_len;
	memcpy(rq->cmd, cmd, hdr->cmd_len);
	if (sizeof(rq->cmd) != hdr->cmd_len)
		memset(rq->cmd + hdr->cmd_len, 0, sizeof(rq->cmd) - hdr->cmd_len);

	memset(sense, 0, sizeof(sense));
	rq->sense = sense;
	rq->sense_len = 0;

	rq->flags |= REQ_BLOCK_PC;
	bio = rq->bio;

	rq->timeout = (hdr->timeout * HZ) / 1000;
	if (!rq->timeout)
		rq->timeout = q->sg_timeout;
	if (!rq->timeout)
		rq->timeout = BLK_DEFAULT_TIMEOUT;

	start_time = jiffies;

	/* ignore return value. All information is passed back to caller
	 * (if he doesn't check that is his problem).
	 * N.B. a non-zero SCSI status is _not_ necessarily an error.
	 */
	blk_execute_rq(q, bd_disk, rq);

	/* write to all output members */
	hdr->status = rq->errors;	
	hdr->masked_status = (hdr->status >> 1) & 0x1f;
	hdr->msg_status = 0;
	hdr->host_status = 0;
	hdr->driver_status = 0;
	hdr->info = 0;
	if (hdr->masked_status || hdr->host_status || hdr->driver_status)
		hdr->info |= SG_INFO_CHECK;
	hdr->resid = rq->data_len;
	hdr->duration = ((jiffies - start_time) * 1000) / HZ;
	hdr->sb_len_wr = 0;

	if (rq->sense_len && hdr->sbp) {
		int len = min((unsigned int) hdr->mx_sb_len, rq->sense_len);

		if (!copy_to_user(hdr->sbp, rq->sense, len))
			hdr->sb_len_wr = len;
	}

	if (blk_rq_unmap_user(rq, hdr->dxferp, bio, hdr->dxfer_len))
		return -EFAULT;

	/* may not have succeeded, but output values written to control
	 * structure (struct sg_io_hdr).  */
	return 0;
}

#define FORMAT_UNIT_TIMEOUT		(2 * 60 * 60 * HZ)
#define START_STOP_TIMEOUT		(60 * HZ)
#define MOVE_MEDIUM_TIMEOUT		(5 * 60 * HZ)
#define READ_ELEMENT_STATUS_TIMEOUT	(5 * 60 * HZ)
#define READ_DEFECT_DATA_TIMEOUT	(60 * HZ )
#define OMAX_SB_LEN 16          /* For backward compatibility */

static int sg_scsi_ioctl(request_queue_t *q, struct gendisk *bd_disk,
			 Scsi_Ioctl_Command __user *sic)
{
	struct request *rq;
	int err, in_len, out_len, bytes, opcode, cmdlen;
	char *buffer = NULL, sense[SCSI_SENSE_BUFFERSIZE];

	/*
	 * get in an out lengths, verify they don't exceed a page worth of data
	 */
	if (get_user(in_len, &sic->inlen))
		return -EFAULT;
	if (get_user(out_len, &sic->outlen))
		return -EFAULT;
	if (in_len > PAGE_SIZE || out_len > PAGE_SIZE)
		return -EINVAL;
	if (get_user(opcode, sic->data))
		return -EFAULT;

	bytes = max(in_len, out_len);
	if (bytes) {
		buffer = kmalloc(bytes, q->bounce_gfp | GFP_USER);
		if (!buffer)
			return -ENOMEM;

		memset(buffer, 0, bytes);
	}

	rq = blk_get_request(q, in_len ? WRITE : READ, __GFP_WAIT);

	cmdlen = COMMAND_SIZE(opcode);

	/*
	 * get command and data to send to device, if any
	 */
	err = -EFAULT;
	rq->cmd_len = cmdlen;
	if (copy_from_user(rq->cmd, sic->data, cmdlen))
		goto error;

	if (copy_from_user(buffer, sic->data + cmdlen, in_len))
		goto error;

	switch (opcode) {
		case SEND_DIAGNOSTIC:
		case FORMAT_UNIT:
			rq->timeout = FORMAT_UNIT_TIMEOUT;
			break;
		case START_STOP:
			rq->timeout = START_STOP_TIMEOUT;
			break;
		case MOVE_MEDIUM:
			rq->timeout = MOVE_MEDIUM_TIMEOUT;
			break;
		case READ_ELEMENT_STATUS:
			rq->timeout = READ_ELEMENT_STATUS_TIMEOUT;
			break;
		case READ_DEFECT_DATA:
			rq->timeout = READ_DEFECT_DATA_TIMEOUT;
			break;
		default:
			rq->timeout = BLK_DEFAULT_TIMEOUT;
			break;
	}

	memset(sense, 0, sizeof(sense));
	rq->sense = sense;
	rq->sense_len = 0;

	rq->data = buffer;
	rq->data_len = bytes;
	rq->flags |= REQ_BLOCK_PC;

	blk_execute_rq(q, bd_disk, rq);
	err = rq->errors & 0xff;	/* only 8 bit SCSI status */
	if (err) {
		if (rq->sense_len && rq->sense) {
			bytes = (OMAX_SB_LEN > rq->sense_len) ?
				rq->sense_len : OMAX_SB_LEN;
			if (copy_to_user(sic->data, rq->sense, bytes))
				err = -EFAULT;
		}
	} else {
		if (copy_to_user(sic->data, buffer, out_len))
			err = -EFAULT;
	}
	
error:
	kfree(buffer);
	blk_put_request(rq);
	return err;
}

int scsi_cmd_ioctl(struct gendisk *bd_disk, unsigned int cmd, void __user *arg)
{
	request_queue_t *q;
	struct request *rq;
	int close = 0, err;

	q = bd_disk->queue;
	if (!q)
		return -ENXIO;

	if (blk_get_queue(q))
		return -ENXIO;

	switch (cmd) {
		/*
		 * new sgv3 interface
		 */
		case SG_GET_VERSION_NUM:
			err = sg_get_version(arg);
			break;
		case SCSI_IOCTL_GET_IDLUN:
			err = scsi_get_idlun(q, arg);
			break;
		case SCSI_IOCTL_GET_BUS_NUMBER:
			err = scsi_get_bus(q, arg);
			break;
		case SG_SET_TIMEOUT:
			err = sg_set_timeout(q, arg);
			break;
		case SG_GET_TIMEOUT:
			err = sg_get_timeout(q);
			break;
		case SG_GET_RESERVED_SIZE:
			err = sg_get_reserved_size(q, arg);
			break;
		case SG_SET_RESERVED_SIZE:
			err = sg_set_reserved_size(q, arg);
			break;
		case SG_EMULATED_HOST:
			err = sg_emulated_host(q, arg);
			break;
		case SG_IO: {
			struct sg_io_hdr hdr;

			err = -EFAULT;
			if (copy_from_user(&hdr, arg, sizeof(hdr)))
				break;
			err = sg_io(q, bd_disk, &hdr);
			if (err == -EFAULT)
				break;

			if (copy_to_user(arg, &hdr, sizeof(hdr)))
				err = -EFAULT;
			break;
		}
		case CDROM_SEND_PACKET: {
			struct cdrom_generic_command cgc;
			struct sg_io_hdr hdr;

			err = -EFAULT;
			if (copy_from_user(&cgc, arg, sizeof(cgc)))
				break;
			cgc.timeout = clock_t_to_jiffies(cgc.timeout);
			memset(&hdr, 0, sizeof(hdr));
			hdr.interface_id = 'S';
			hdr.cmd_len = sizeof(cgc.cmd);
			hdr.dxfer_len = cgc.buflen;
			err = 0;
			switch (cgc.data_direction) {
				case CGC_DATA_UNKNOWN:
					hdr.dxfer_direction = SG_DXFER_UNKNOWN;
					break;
				case CGC_DATA_WRITE:
					hdr.dxfer_direction = SG_DXFER_TO_DEV;
					break;
				case CGC_DATA_READ:
					hdr.dxfer_direction = SG_DXFER_FROM_DEV;
					break;
				case CGC_DATA_NONE:
					hdr.dxfer_direction = SG_DXFER_NONE;
					break;
				default:
					err = -EINVAL;
			}
			if (err)
				break;

			hdr.dxferp = cgc.buffer;
			hdr.sbp = cgc.sense;
			if (hdr.sbp)
				hdr.mx_sb_len = sizeof(struct request_sense);
			hdr.timeout = cgc.timeout;
			hdr.cmdp = ((struct cdrom_generic_command __user*) arg)->cmd;
			hdr.cmd_len = sizeof(cgc.cmd);

			err = sg_io(q, bd_disk, &hdr);
			if (err == -EFAULT)
				break;

			if (hdr.status)
				err = -EIO;

			cgc.stat = err;
			cgc.buflen = hdr.resid;
			if (copy_to_user(arg, &cgc, sizeof(cgc)))
				err = -EFAULT;

			break;
		}

		/*
		 * old junk scsi send command ioctl
		 */
		case SCSI_IOCTL_SEND_COMMAND:
			err = -EINVAL;
			if (!arg)
				break;

			err = sg_scsi_ioctl(q, bd_disk, arg);
			break;
		case CDROMCLOSETRAY:
			close = 1;
		case CDROMEJECT:
			rq = blk_get_request(q, WRITE, __GFP_WAIT);
			rq->flags |= REQ_BLOCK_PC;
			rq->data = NULL;
			rq->data_len = 0;
			rq->timeout = BLK_DEFAULT_TIMEOUT;
			memset(rq->cmd, 0, sizeof(rq->cmd));
			rq->cmd[0] = GPCMD_START_STOP_UNIT;
			rq->cmd[4] = 0x02 + (close != 0);
			rq->cmd_len = 6;
			err = blk_execute_rq(q, bd_disk, rq);
			blk_put_request(rq);
			break;
		default:
			err = -ENOTTY;
	}

	blk_put_queue(q);
	return err;
}

EXPORT_SYMBOL(scsi_cmd_ioctl);
