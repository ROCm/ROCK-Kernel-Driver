/*
 *  linux/drivers/mmc/mmc_queue.c
 *
 *  Copyright (C) 2003 Russell King, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/module.h>
#include <linux/blkdev.h>

#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include "mmc_queue.h"

/*
 * Prepare a MMC request.  Essentially, this means passing the
 * preparation off to the media driver.  The media driver will
 * create a mmc_io_request in req->special.
 */
static int mmc_prep_request(struct request_queue *q, struct request *req)
{
	struct mmc_queue *mq = q->queuedata;
	int ret = BLKPREP_KILL;

	if (req->flags & REQ_SPECIAL) {
		/*
		 * Special commands already have the command
		 * blocks already setup in req->special.
		 */
		BUG_ON(!req->special);

		ret = BLKPREP_OK;
	} else if (req->flags & (REQ_CMD | REQ_BLOCK_PC)) {
		/*
		 * Block I/O requests need translating according
		 * to the protocol.
		 */
		ret = mq->prep_fn(mq, req);
	} else {
		/*
		 * Everything else is invalid.
		 */
		blk_dump_rq_flags(req, "MMC bad request");
	}

	if (ret == BLKPREP_OK)
		req->flags |= REQ_DONTPREP;

	return ret;
}

static int mmc_queue_thread(void *d)
{
	struct mmc_queue *mq = d;
	struct request_queue *q = mq->queue;
	DECLARE_WAITQUEUE(wait, current);
	int ret;

	/*
	 * Set iothread to ensure that we aren't put to sleep by
	 * the process freezing.  We handle suspension ourselves.
	 */
	current->flags |= PF_MEMALLOC|PF_NOFREEZE;

	daemonize("mmcqd");

	spin_lock_irq(&current->sighand->siglock);
	sigfillset(&current->blocked);
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	mq->thread = current;
	complete(&mq->thread_complete);

	add_wait_queue(&mq->thread_wq, &wait);
	spin_lock_irq(q->queue_lock);
	do {
		struct request *req = NULL;

		set_current_state(TASK_INTERRUPTIBLE);
		if (!blk_queue_plugged(q))
			mq->req = req = elv_next_request(q);
		spin_unlock(q->queue_lock);

		if (!req) {
			if (!mq->thread)
				break;
			schedule();
			continue;
		}
		set_current_state(TASK_RUNNING);

		ret = mq->issue_fn(mq, req);

		spin_lock_irq(q->queue_lock);
		end_request(req, ret);
	} while (1);
	remove_wait_queue(&mq->thread_wq, &wait);

	complete_and_exit(&mq->thread_complete, 0);
	return 0;
}

/*
 * Generic MMC request handler.  This is called for any queue on a
 * particular host.  When the host is not busy, we look for a request
 * on any queue on this host, and attempt to issue it.  This may
 * not be the queue we were asked to process.
 */
static void mmc_request(request_queue_t *q)
{
	struct mmc_queue *mq = q->queuedata;

	if (!mq->req && !blk_queue_plugged(q))
		wake_up(&mq->thread_wq);
}

/**
 * mmc_init_queue - initialise a queue structure.
 * @mq: mmc queue
 * @card: mmc card to attach this queue
 * @lock: queue lock
 *
 * Initialise a MMC card request queue.
 */
int mmc_init_queue(struct mmc_queue *mq, struct mmc_card *card, spinlock_t *lock)
{
	u64 limit = BLK_BOUNCE_HIGH;
	int ret;

	if (card->host->dev->dma_mask)
		limit = *card->host->dev->dma_mask;

	mq->card = card;
	mq->queue = blk_init_queue(mmc_request, lock);
	if (!mq->queue)
		return -ENOMEM;

	blk_queue_prep_rq(mq->queue, mmc_prep_request);
	blk_queue_bounce_limit(mq->queue, limit);

	mq->queue->queuedata = mq;
	mq->req = NULL;

	init_completion(&mq->thread_complete);
	init_waitqueue_head(&mq->thread_wq);

	ret = kernel_thread(mmc_queue_thread, mq, CLONE_KERNEL);
	if (ret < 0) {
		blk_cleanup_queue(mq->queue);
	} else {
		wait_for_completion(&mq->thread_complete);
		init_completion(&mq->thread_complete);
		ret = 0;
	}

	return ret;
}

EXPORT_SYMBOL(mmc_init_queue);

void mmc_cleanup_queue(struct mmc_queue *mq)
{
	mq->thread = NULL;
	wake_up(&mq->thread_wq);
	wait_for_completion(&mq->thread_complete);
	blk_cleanup_queue(mq->queue);

	mq->card = NULL;
}

EXPORT_SYMBOL(mmc_cleanup_queue);
