/*
 *  drivers/s390/char/tape_block.c
 *    block device frontend for tape device driver
 *
 *  S390 and zSeries version
 *    Copyright (C) 2001,2002 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Carsten Otte <cotte@de.ibm.com>
 *		 Tuan Ngo-Anh <ngoanh@de.ibm.com>
 *		 Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/interrupt.h>
#include <linux/buffer_head.h>

#include <asm/debug.h>

#include "tape.h"

#define PRINTK_HEADER "TBLOCK:"

#define TAPEBLOCK_MAX_SEC	100
#define TAPEBLOCK_MIN_REQUEUE	3

/*
 * file operation structure for tape block frontend
 */
static int tapeblock_open(struct inode *, struct file *);
static int tapeblock_release(struct inode *, struct file *);

static struct block_device_operations tapeblock_fops = {
	.owner		= THIS_MODULE,
	.open		= tapeblock_open,
	.release	= tapeblock_release,
};

static int tapeblock_major = 0;

/*
 * Post finished request.
 */
static inline void
tapeblock_end_request(struct request *req, int uptodate)
{
	if (end_that_request_first(req, uptodate, req->hard_nr_sectors))
		BUG();
	end_that_request_last(req);
}

static void
__tapeblock_end_request(struct tape_request *ccw_req, void *data)
{
	struct tape_device *device;
	struct request *req;

	device = ccw_req->device;
	req = (struct request *) data;
	tapeblock_end_request(req, ccw_req->rc == 0);
	if (ccw_req->rc == 0)
		/* Update position. */
		device->blk_data.block_position =
			(req->sector + req->nr_sectors) >> TAPEBLOCK_HSEC_S2B;
	else
		/* We lost the position information due to an error. */
		device->blk_data.block_position = -1;
	device->discipline->free_bread(ccw_req);
	if (!list_empty(&device->req_queue) ||
	    elv_next_request(device->blk_data.request_queue))
		tasklet_schedule(&device->blk_data.tasklet);
}

/*
 * Fetch requests from block device queue.
 */
static inline void
__tape_process_blk_queue(struct tape_device *device, struct list_head *new_req)
{
	request_queue_t *queue;
	struct list_head *l;
	struct request *req;
	struct tape_request *ccw_req;
	int nr_queued;

	/* FIXME: we have to make sure that the tapeblock frontend
	   owns the device. tape_state != TS_IN_USE is NOT enough. */
	if (device->tape_state != TS_IN_USE)
		return;
	queue = device->blk_data.request_queue;
	nr_queued = 0;
	/* Count number of requests on ccw queue. */
	list_for_each(l, &device->req_queue)
		nr_queued++;
	while (!blk_queue_plugged(queue) &&
	       elv_next_request(queue) &&
	       nr_queued < TAPEBLOCK_MIN_REQUEUE) {
		req = elv_next_request(queue);
		if (rq_data_dir(req) == WRITE) {
			DBF_EVENT(1, "TBLOCK: Rejecting write request\n");
			blkdev_dequeue_request(req);
			tapeblock_end_request(req, 0);
			continue;
		}
		ccw_req = device->discipline->bread(device, req);
		if (IS_ERR(ccw_req)) {
			if (PTR_ERR(ccw_req) == -ENOMEM)
				break; /* don't try again */
			DBF_EVENT(1, "TBLOCK: bread failed\n");
			blkdev_dequeue_request(req);
			tapeblock_end_request(req, 0);
			continue;
		}
		ccw_req->callback = __tapeblock_end_request;
		ccw_req->callback_data = (void *) req;
		ccw_req->retries = TAPEBLOCK_RETRIES;
		blkdev_dequeue_request(req);
		list_add_tail(new_req, &ccw_req->list);
		nr_queued++;
	}
}

/*
 * Feed requests to the tape device.
 */
static inline int
tape_queue_requests(struct tape_device *device, struct list_head *new_req)
{
	struct list_head *l, *n;
	struct tape_request *ccw_req;
	struct request *req;
	int rc, fail;

	fail = 0;
	list_for_each_safe(l, n, new_req) {
		ccw_req = list_entry(l, struct tape_request, list);
		list_del(&ccw_req->list);
		rc = tape_do_io_async(device, ccw_req);
		if (rc) {
			/*
			 * Start/enqueueing failed. No retries in
			 * this case.
			 */
			req = (struct request *) ccw_req->callback_data;
			tapeblock_end_request(req, 0);
			device->discipline->free_bread(ccw_req);
			fail = 1;
		}
	}
	return fail;
}

/*
 * Tape request queue function. Called from ll_rw_blk.c
 */
static void
tapeblock_request_fn(request_queue_t *queue)
{
	struct list_head new_req;
	struct tape_device *device;

	device = (struct tape_device *) queue->queuedata;
	while (elv_next_request(queue)) {
		INIT_LIST_HEAD(&new_req);
		spin_lock(get_ccwdev_lock(device->cdev));
		__tape_process_blk_queue(device, &new_req);
		spin_unlock(get_ccwdev_lock(device->cdev));
		/*
		 * Now queue the new request to the tape. This needs to be
		 * done without the device lock held.
		 */
		if (tape_queue_requests(device, &new_req) == 0)
			/* All requests queued. Thats enough for now. */
			break;
	}
}

/*
 * Acquire the device lock and process queues for the device.
 */
static void
tapeblock_tasklet(unsigned long data)
{
	struct list_head new_req;
	struct tape_device *device;

	device = (struct tape_device *) data;
	while (elv_next_request(device->blk_data.request_queue)) {
		INIT_LIST_HEAD(&new_req);
		spin_lock_irq(get_ccwdev_lock(device->cdev));
		__tape_process_blk_queue(device, &new_req);
		spin_unlock_irq(get_ccwdev_lock(device->cdev));
		/*
		 * Now queue the new request to the tape. This needs to be
		 * done without the device lock held.
		 */
		if (tape_queue_requests(device, &new_req) == 0)
			/* All requests queued. Thats enough for now. */
			break;
	}
}

/*
 * This function is called for every new tapedevice
 */
int
tapeblock_setup_device(struct tape_device * device)
{
	struct tape_blk_data *d = &device->blk_data;
	request_queue_t *q;
	struct gendisk *disk = alloc_disk(1);
	int rc;

	if (!disk)
		return -ENOMEM;

	tasklet_init(&d->tasklet, tapeblock_tasklet, (unsigned long)device);

	spin_lock_init(&d->request_queue_lock);
	q = blk_init_queue(tapeblock_request_fn, &d->request_queue_lock);
	if (!q) {
		rc = -ENXIO;
		goto put_disk;
	}
	d->request_queue = q;
	elevator_exit(q);
	rc = elevator_init(q, &elevator_noop);
	if (rc)
		goto cleanup_queue;

	/* FIXME: We should be able to sense the sectore size */
	blk_queue_hardsect_size(q, TAPEBLOCK_HSEC_SIZE);
	blk_queue_max_sectors(q, TAPEBLOCK_MAX_SEC);
	blk_queue_max_phys_segments(q, -1L);
	blk_queue_max_hw_segments(q, -1L);
	blk_queue_max_segment_size(q, -1L);
	blk_queue_segment_boundary(q, -1L);

	disk->major = tapeblock_major;
	disk->first_minor = device->first_minor;
	disk->fops = &tapeblock_fops;
	disk->private_data = device;
	disk->queue = q;
	//set_capacity(disk, size);

	sprintf(disk->disk_name, "tBLK/%d", device->first_minor / TAPE_MINORS_PER_DEV);

	add_disk(disk);
	d->disk = disk;
	return 0;

 cleanup_queue:
	blk_cleanup_queue(q);
 put_disk:
	put_disk(disk);
	return rc;
}

void
tapeblock_cleanup_device(struct tape_device *device)
{
	struct tape_blk_data *d = &device->blk_data;

	del_gendisk(d->disk);
	put_disk(d->disk);
	blk_cleanup_queue(d->request_queue);

	tasklet_kill(&d->tasklet);
}

/*
 * Detect number of blocks of the tape.
 * FIXME: can we extent this to detect the blocks size as well ?
 */
static int tapeblock_mediumdetect(struct tape_device *device)
{
	unsigned int nr_of_blks;
	int rc;

	PRINT_INFO("Detecting media size...\n");
	rc = tape_mtop(device, MTREW, 1);
	if (rc)
		return rc;
	rc = tape_mtop(device, MTFSF, 1);
	if (rc)
		return rc;
	rc = tape_mtop(device, MTTELL, 1);
	if (rc)
		return rc;
	nr_of_blks = rc - 1; /* don't count FM */
	rc = tape_mtop(device, MTREW, 1);
	if (rc)
		return rc;
	PRINT_INFO("Found %i blocks on media\n", nr_of_blks);
	return 0;
}

/*
 * Block frontend tape device open function.
 */
static int
tapeblock_open(struct inode *inode, struct file *filp)
{
	struct gendisk *disk = inode->i_bdev->bd_disk;
	struct tape_device *device = disk->private_data;
	int rc;

	/*
	 * FIXME: this new tapeblock_open function is from 2.5.69.
	 * It doesn't do tape_get_device anymore but picks the device
	 * pointer from disk->private_data. It is stored in 
	 * tapeblock_setup_device but WITHOUT proper ref-counting.
	 */
	rc = tape_open(device);
	if (rc)
		goto put_device;
	rc = tape_assign(device);
	if (rc)
		goto release;
	device->blk_data.block_position = -1;
	rc = tapeblock_mediumdetect(device);
	if (rc)
		goto unassign;
	return 0;

 unassign:
	tape_unassign(device);
 release:
	tape_release(device);
 put_device:
	tape_put_device(device);
	return rc;
}

/*
 * Block frontend tape device release function.
 */
static int
tapeblock_release(struct inode *inode, struct file *filp)
{
	struct gendisk *disk = inode->i_bdev->bd_disk;
	struct tape_device *device = disk->private_data;

	tape_release(device);
	tape_unassign(device);
	tape_put_device(device);

	return 0;
}

/*
 * Initialize block device frontend.
 */
int
tapeblock_init(void)
{
	int rc;

	/* Register the tape major number to the kernel */
	rc = register_blkdev(tapeblock_major, "tBLK");
	if (rc < 0)
		return rc;

	if (tapeblock_major == 0)
		tapeblock_major = rc;
	PRINT_INFO("tape gets major %d for block device\n", tapeblock_major);
	return 0;
}

/*
 * Deregister major for block device frontend
 */
void
tapeblock_exit(void)
{
	unregister_blkdev(tapeblock_major, "tBLK");
}
