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
#include <linux/bio.h>
#include <asm/uaccess.h>

#include <scsi/scsi.h>
#include <scsi/scsi_ioctl.h>


/* Command group 3 is reserved and should never be used.  */
const unsigned char scsi_command_size[8] =
{
	6, 10, 10, 12,
	16, 12, 10, 10
};

#define BLK_DEFAULT_TIMEOUT	(60 * HZ)

/* defined in ../scsi/scsi.h  ... should it be included? */
#ifndef SCSI_SENSE_BUFFERSIZE
#define SCSI_SENSE_BUFFERSIZE 64
#endif

static int blk_do_rq(request_queue_t *q, struct block_device *bdev, 
		     struct request *rq)
{
	DECLARE_COMPLETION(wait);
	int err = 0;

	rq->rq_disk = bdev->bd_disk;

	/*
	 * we need an extra reference to the request, so we can look at
	 * it after io completion
	 */
	rq->ref_count++;

	rq->flags |= REQ_NOMERGE;
	rq->waiting = &wait;
        drive_stat_acct(rq, rq->nr_sectors, 1);
	elv_add_request(q, rq, 1, 1);
	generic_unplug_device(q);
	wait_for_completion(&wait);

	if (rq->errors)
		err = -EIO;

	return err;
}

#include <scsi/sg.h>

static int sg_get_version(int *p)
{
	static int sg_version_num = 30527;
	return put_user(sg_version_num, p);
}

static int scsi_get_idlun(request_queue_t *q, int *p)
{
	return put_user(0, p);
}

static int scsi_get_bus(request_queue_t *q, int *p)
{
	return put_user(0, p);
}

static int sg_get_timeout(request_queue_t *q)
{
	return q->sg_timeout;
}

static int sg_set_timeout(request_queue_t *q, int *p)
{
	int timeout, err = get_user(timeout, p);

	if (!err)
		q->sg_timeout = timeout;

	return err;
}

static int sg_get_reserved_size(request_queue_t *q, int *p)
{
	return put_user(q->sg_reserved_size, p);
}

static int sg_set_reserved_size(request_queue_t *q, int *p)
{
	int size, err = get_user(size, p);

	if (!err)
		q->sg_reserved_size = size;

	return err;
}

/*
 * will always return that we are ATAPI even for a real SCSI drive, I'm not
 * so sure this is worth doing anything about (why would you care??)
 */
static int sg_emulated_host(request_queue_t *q, int *p)
{
	return put_user(1, p);
}

static int sg_io(request_queue_t *q, struct block_device *bdev,
		 struct sg_io_hdr *uptr)
{
	unsigned long uaddr, start_time;
	int reading, writing, nr_sectors;
	struct sg_io_hdr hdr;
	struct request *rq;
	struct bio *bio;
	char sense[SCSI_SENSE_BUFFERSIZE];
	void *buffer;

	if (!access_ok(VERIFY_WRITE, uptr, sizeof(*uptr)))
		return -EFAULT;
	if (copy_from_user(&hdr, uptr, sizeof(*uptr)))
		return -EFAULT;

	if (hdr.interface_id != 'S')
		return -EINVAL;
	if (hdr.cmd_len > sizeof(rq->cmd))
		return -EINVAL;
	if (!access_ok(VERIFY_READ, hdr.cmdp, hdr.cmd_len))
		return -EFAULT;

	if (hdr.dxfer_len > 65536)
		return -EINVAL;

	/*
	 * we'll do that later
	 */
	if (hdr.iovec_count)
		return -EOPNOTSUPP;

	nr_sectors = 0;
	reading = writing = 0;
	buffer = NULL;
	bio = NULL;
	if (hdr.dxfer_len) {
		unsigned int bytes = (hdr.dxfer_len + 511) & ~511;

		switch (hdr.dxfer_direction) {
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

		uaddr = (unsigned long) hdr.dxferp;
		if (writing && !access_ok(VERIFY_WRITE, uaddr, bytes))
			return -EFAULT;
		else if (reading && !access_ok(VERIFY_READ, uaddr, bytes))
			return -EFAULT;

		/*
		 * first try to map it into a bio. reading from device will
		 * be a write to vm.
		 */
		bio = bio_map_user(bdev, uaddr, hdr.dxfer_len, reading);
		if (bio) {
			if (writing)
				bio->bi_rw |= (1 << BIO_RW);

			nr_sectors = (bio->bi_size + 511) >> 9;

			if (bio->bi_size < hdr.dxfer_len) {
				bio_endio(bio, bio->bi_size, 0);
				bio_unmap_user(bio, 0);
				bio = NULL;
			}
		}

		/*
		 * if bio setup failed, fall back to slow approach
		 */
		if (!bio) {
			buffer = kmalloc(bytes, q->bounce_gfp | GFP_USER);
			if (!buffer)
				return -ENOMEM;

			nr_sectors = bytes >> 9;
			if (writing)
				copy_from_user(buffer,hdr.dxferp,hdr.dxfer_len);
			else
				memset(buffer, 0, hdr.dxfer_len);
		}
	}

	rq = blk_get_request(q, WRITE, __GFP_WAIT);

	/*
	 * fill in request structure
	 */
	rq->cmd_len = hdr.cmd_len;
	copy_from_user(rq->cmd, hdr.cmdp, hdr.cmd_len);
	if (sizeof(rq->cmd) != hdr.cmd_len)
		memset(rq->cmd + hdr.cmd_len, 0, sizeof(rq->cmd) - hdr.cmd_len);

	memset(sense, 0, sizeof(sense));
	rq->sense = sense;
	rq->sense_len = 0;

	rq->flags |= REQ_BLOCK_PC;
	if (writing)
		rq->flags |= REQ_RW;

	rq->hard_nr_sectors = rq->nr_sectors = nr_sectors;
	rq->hard_cur_sectors = rq->current_nr_sectors = nr_sectors;

	if (bio) {
		/*
		 * subtle -- if bio_map_user() ended up bouncing a bio, it
		 * would normally disappear when its bi_end_io is run.
		 * however, we need it for the unmap, so grab an extra
		 * reference to it
		 */
		bio_get(bio);

		rq->nr_phys_segments = bio_phys_segments(q, bio);
		rq->nr_hw_segments = bio_hw_segments(q, bio);
		rq->current_nr_sectors = bio_cur_sectors(bio);
		rq->hard_cur_sectors = rq->current_nr_sectors;
		rq->buffer = bio_data(bio);
	}

	rq->data_len = hdr.dxfer_len;
	rq->data = buffer;

	rq->timeout = hdr.timeout;
	if (!rq->timeout)
		rq->timeout = q->sg_timeout;
	if (!rq->timeout)
		rq->timeout = BLK_DEFAULT_TIMEOUT;

	rq->bio = rq->biotail = bio;

	start_time = jiffies;

	/* ignore return value. All information is passed back to caller
	 * (if he doesn't check that is his problem).
	 * N.B. a non-zero SCSI status is _not_ necessarily an error.
	 */
	blk_do_rq(q, bdev, rq);
	
	if (bio) {
		bio_unmap_user(bio, reading);
		bio_put(bio);
	}

	/* write to all output members */
	hdr.status = rq->errors;	
	hdr.masked_status = (hdr.status >> 1) & 0x1f;
	hdr.msg_status = 0;
	hdr.host_status = 0;
	hdr.driver_status = 0;
	hdr.info = 0;
	if (hdr.masked_status || hdr.host_status || hdr.driver_status)
		hdr.info |= SG_INFO_CHECK;
	hdr.resid = rq->data_len;
	hdr.duration = (jiffies - start_time) * (1000 / HZ);
	hdr.sb_len_wr = 0;

	if (rq->sense_len && hdr.sbp) {
		int len = (hdr.mx_sb_len < rq->sense_len) ? 
				hdr.mx_sb_len : rq->sense_len;

		if (!copy_to_user(hdr.sbp, rq->sense, len))
			hdr.sb_len_wr = len;
	}

	blk_put_request(rq);

	copy_to_user(uptr, &hdr, sizeof(*uptr));

	if (buffer) {
		if (reading)
			copy_to_user(hdr.dxferp, buffer, hdr.dxfer_len);

		kfree(buffer);
	}
	/* may not have succeeded, but output values written to control
	 * structure (struct sg_io_hdr).  */
	return 0;
}

#define FORMAT_UNIT_TIMEOUT		(2 * 60 * 60 * HZ)
#define START_STOP_TIMEOUT		(60 * HZ)
#define MOVE_MEDIUM_TIMEOUT		(5 * 60 * HZ)
#define READ_ELEMENT_STATUS_TIMEOUT	(5 * 60 * HZ)
#define READ_DEFECT_DATA_TIMEOUT	(60 * HZ )

static int sg_scsi_ioctl(request_queue_t *q, struct block_device *bdev,
			 Scsi_Ioctl_Command *sic)
{
	struct request *rq;
	int err, in_len, out_len, bytes, opcode, cmdlen;
	char *buffer = NULL, sense[24];

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

	rq = blk_get_request(q, WRITE, __GFP_WAIT);

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
	if (in_len)
		rq->flags |= REQ_RW;

	blk_do_rq(q, bdev, rq);
	err = rq->errors & 0xff;	/* only 8 bit SCSI status */
	if (err) {
		if (rq->sense_len)
			if (copy_to_user(sic->data, rq->sense, rq->sense_len))
				err = -EFAULT;
	} else {
		if (copy_to_user(sic->data, buffer, out_len))
			err = -EFAULT;
	}
	
error:
	kfree(buffer);
	blk_put_request(rq);
	return err;
}

int scsi_cmd_ioctl(struct block_device *bdev, unsigned int cmd, unsigned long arg)
{
	request_queue_t *q;
	struct request *rq;
	int close = 0, err;

	q = bdev_get_queue(bdev);
	if (!q)
		return -ENXIO;

	switch (cmd) {
		/*
		 * new sgv3 interface
		 */
		case SG_GET_VERSION_NUM:
			return sg_get_version((int *) arg);
		case SCSI_IOCTL_GET_IDLUN:
			return scsi_get_idlun(q, (int *) arg);
		case SCSI_IOCTL_GET_BUS_NUMBER:
			return scsi_get_bus(q, (int *) arg);
		case SG_SET_TIMEOUT:
			return sg_set_timeout(q, (int *) arg);
		case SG_GET_TIMEOUT:
			return sg_get_timeout(q);
		case SG_GET_RESERVED_SIZE:
			return sg_get_reserved_size(q, (int *) arg);
		case SG_SET_RESERVED_SIZE:
			return sg_set_reserved_size(q, (int *) arg);
		case SG_EMULATED_HOST:
			return sg_emulated_host(q, (int *) arg);
		case SG_IO:
			err = bd_claim(bdev, current);
			if (err)
				break;
			err = sg_io(q, bdev, (struct sg_io_hdr *) arg);
			bd_release(bdev);
			break;
		/*
		 * old junk scsi send command ioctl
		 */
		case SCSI_IOCTL_SEND_COMMAND:
			if (!arg)
				return -EINVAL;

			err = bd_claim(bdev, current);
			if (err)
				break;
			err = sg_scsi_ioctl(q, bdev, (Scsi_Ioctl_Command *)arg);
			bd_release(bdev);
			break;
		case CDROMCLOSETRAY:
			close = 1;
		case CDROMEJECT:
			rq = blk_get_request(q, WRITE, __GFP_WAIT);
			rq->flags = REQ_BLOCK_PC;
			rq->data = NULL;
			rq->data_len = 0;
			rq->timeout = BLK_DEFAULT_TIMEOUT;
			memset(rq->cmd, 0, sizeof(rq->cmd));
			rq->cmd[0] = GPCMD_START_STOP_UNIT;
			rq->cmd[4] = 0x02 + (close != 0);
			rq->cmd_len = 6;
			err = blk_do_rq(q, bdev, rq);
			blk_put_request(rq);
			break;
		default:
			err = -ENOTTY;
	}

	blk_put_queue(q);
	return err;
}

EXPORT_SYMBOL(scsi_cmd_ioctl);
EXPORT_SYMBOL(scsi_command_size);
