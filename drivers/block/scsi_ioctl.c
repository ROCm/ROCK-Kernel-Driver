/*
 * Copyright (C) 2001 Jens Axboe <axboe@suse.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of

 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public Licens
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-
 *
 */
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/config.h>
#include <linux/swap.h>
#include <linux/init.h>
#include <linux/smp_lock.h>
#include <linux/module.h>
#include <linux/blk.h>
#include <linux/completion.h>
#include <linux/cdrom.h>
#include <linux/slab.h>

#include <scsi/scsi.h>

#include <asm/uaccess.h>

int blk_do_rq(request_queue_t *q, struct request *rq)
{
	DECLARE_COMPLETION(wait);
	int err = 0;

	rq->flags |= REQ_NOMERGE;
	rq->waiting = &wait;
	elv_add_request(q, rq, 1);
	generic_unplug_device(q);
	wait_for_completion(&wait);

	/*
	 * for now, never retry anything
	 */
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
	return HZ;
}

static int sg_set_timeout(request_queue_t *q, int *p)
{
	int timeout;
	int error = get_user(timeout, p);
	return error;
}

static int reserved_size = 0;

static int sg_get_reserved_size(request_queue_t *q, int *p)
{
	return put_user(reserved_size, p);
}

static int sg_set_reserved_size(request_queue_t *q, int *p)
{
	int size;
	int error = get_user(size, p);
	if (!error)
		reserved_size = size;
	return error;
}

static int sg_emulated_host(request_queue_t *q, int *p)
{
	return put_user(1, p);
}

static int sg_io(request_queue_t *q, struct sg_io_hdr *uptr)
{
	int i, err;
	struct sg_io_hdr hdr;
	struct request *rq;
	void *buffer;

	if (!access_ok(VERIFY_WRITE, uptr, sizeof(*uptr)))
		return -EFAULT;
	if (copy_from_user(&hdr, uptr, sizeof(*uptr)))
		return -EFAULT;

	if ( hdr.cmd_len > sizeof(rq->cmd) )
		return -EINVAL;

	buffer = NULL;
	if (hdr.dxfer_len) {
		unsigned int bytes = (hdr.dxfer_len + 511) & ~511;

		switch (hdr.dxfer_direction) {
		default:
			return -EINVAL;
		case SG_DXFER_TO_DEV:
		case SG_DXFER_FROM_DEV:
		case SG_DXFER_TO_FROM_DEV:
			break;
		}
		buffer = kmalloc(bytes, GFP_USER);
		if (!buffer)
			return -ENOMEM;
		if (hdr.dxfer_direction == SG_DXFER_TO_DEV ||
		    hdr.dxfer_direction == SG_DXFER_TO_FROM_DEV)
			copy_from_user(buffer, hdr.dxferp, hdr.dxfer_len);
	}

	rq = blk_get_request(q, WRITE, __GFP_WAIT);
	rq->timeout = 60*HZ;
	rq->data = buffer;
	rq->data_len = hdr.dxfer_len;
	rq->flags = REQ_BLOCK_PC;
	memset(rq->cmd, 0, sizeof(rq->cmd));
	copy_from_user(rq->cmd, hdr.cmdp, hdr.cmd_len);
	err = blk_do_rq(q, rq);

	blk_put_request(rq);

	copy_to_user(uptr, &hdr, sizeof(*uptr));
	if (buffer) {
		if (hdr.dxfer_direction == SG_DXFER_FROM_DEV ||
		    hdr.dxfer_direction == SG_DXFER_TO_FROM_DEV)
			copy_to_user(hdr.dxferp, buffer, hdr.dxfer_len);
		kfree(buffer);
	}
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
			return sg_io(q, (struct sg_io_hdr *) arg);
		case CDROMCLOSETRAY:
			close = 1;
		case CDROMEJECT:
			rq = blk_get_request(q, WRITE, __GFP_WAIT);
			rq->flags = REQ_BLOCK_PC;
			rq->data = NULL;
			rq->data_len = 0;
			rq->timeout = 60*HZ;
			memset(rq->cmd, 0, sizeof(rq->cmd));
			rq->cmd[0] = GPCMD_START_STOP_UNIT;
			rq->cmd[4] = 0x02 + (close != 0);
			err = blk_do_rq(q, rq);
			blk_put_request(rq);
			break;
		default:
			err = -ENOTTY;
	}

	blk_put_queue(q);
	return err;
}

EXPORT_SYMBOL(scsi_cmd_ioctl);
