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
#include <linux/locks.h>
#include <linux/swap.h>
#include <linux/init.h>
#include <linux/smp_lock.h>
#include <linux/module.h>
#include <linux/blk.h>
#include <linux/completion.h>

#include <linux/cdrom.h>

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

int block_ioctl(struct block_device *bdev, unsigned int cmd, unsigned long arg)
{
	request_queue_t *q;
	struct request *rq;
	int close = 0, err;

	q = blk_get_queue(to_kdev_t(bdev->bd_dev));
	if (!q)
		return -ENXIO;

	switch (cmd) {
		case CDROMCLOSETRAY:
			close = 1;
		case CDROMEJECT:
			rq = blk_get_request(q, WRITE, __GFP_WAIT);
			rq->flags = REQ_BLOCK_PC;
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

EXPORT_SYMBOL(block_ioctl);
