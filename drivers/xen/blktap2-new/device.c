#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/cdrom.h>
#include <linux/hdreg.h>
#include <scsi/scsi.h>
#include <scsi/scsi_ioctl.h>

#include "blktap.h"

int blktap_device_major;

#define dev_to_blktap(_dev) container_of(_dev, struct blktap, device)

static int
blktap_device_open(struct block_device *bdev, fmode_t mode)
{
	struct gendisk *disk = bdev->bd_disk;
	struct blktap_device *tapdev = disk->private_data;

	if (!tapdev)
		return -ENXIO;

	/* NB. we might have bounced a bd trylock by tapdisk. when
	 * failing for reasons not !tapdev, make sure to kick tapdisk
	 * out of destroy wait state again. */

	return 0;
}

static int
blktap_device_release(struct gendisk *disk, fmode_t mode)
{
	struct blktap_device *tapdev = disk->private_data;
	struct block_device *bdev = bdget_disk(disk, 0);
	struct blktap *tap = dev_to_blktap(tapdev);

	bdput(bdev);

	if (!bdev->bd_openers) {
		set_bit(BLKTAP_DEVICE_CLOSED, &tap->dev_inuse);
		blktap_ring_kick_user(tap);
	}

	return 0;
}

static int
blktap_device_getgeo(struct block_device *bd, struct hd_geometry *hg)
{
	/* We don't have real geometry info, but let's at least return
	   values consistent with the size of the device */
	sector_t nsect = get_capacity(bd->bd_disk);
	sector_t cylinders = nsect;

	hg->heads = 0xff;
	hg->sectors = 0x3f;
	sector_div(cylinders, hg->heads * hg->sectors);
	hg->cylinders = cylinders;
	if ((sector_t)(hg->cylinders + 1) * hg->heads * hg->sectors < nsect)
		hg->cylinders = 0xffff;
	return 0;
}

static int
blktap_device_ioctl(struct block_device *bd, fmode_t mode,
		    unsigned command, unsigned long argument)
{
	int i;

	switch (command) {
	case CDROMMULTISESSION:
		BTDBG("FIXME: support multisession CDs later\n");
		for (i = 0; i < sizeof(struct cdrom_multisession); i++)
			if (put_user(0, (char __user *)(argument + i)))
				return -EFAULT;
		return 0;

	case SCSI_IOCTL_GET_IDLUN:
		if (!access_ok(VERIFY_WRITE, argument, 
			sizeof(struct scsi_idlun)))
			return -EFAULT;

		/* return 0 for now. */
		__put_user(0, &((struct scsi_idlun __user *)argument)->dev_id);
		__put_user(0, 
			&((struct scsi_idlun __user *)argument)->host_unique_id);
		return 0;

	default:
		/*printk(KERN_ALERT "ioctl %08x not supported by Xen blkdev\n",
		  command);*/
		return -EINVAL; /* same return as native Linux */
	}

	return 0;
}

static const struct block_device_operations blktap_device_file_operations = {
	.owner     = THIS_MODULE,
	.open      = blktap_device_open,
	.release   = blktap_device_release,
	.ioctl     = blktap_device_ioctl,
	.getgeo    = blktap_device_getgeo
};

/* NB. __blktap holding the queue lock; blktap where unlocked */

static inline struct request*
__blktap_next_queued_rq(struct request_queue *q)
{
	return blk_peek_request(q);
}

static inline void
__blktap_dequeue_rq(struct request *rq)
{
	blk_start_request(rq);
}

/* NB. err == 0 indicates success, failures < 0 */

static inline void
__blktap_end_queued_rq(struct request *rq, int err)
{
	blk_start_request(rq);
	__blk_end_request(rq, err, blk_rq_bytes(rq));
}

static inline void
__blktap_end_rq(struct request *rq, int err)
{
	__blk_end_request(rq, err, blk_rq_bytes(rq));
}

static inline void
blktap_end_rq(struct request *rq, int err)
{
	struct request_queue *q = rq->q;

	spin_lock_irq(q->queue_lock);
	__blktap_end_rq(rq, err);
	spin_unlock_irq(q->queue_lock);
}

void
blktap_device_end_request(struct blktap *tap,
			  struct blktap_request *request,
			  int error)
{
	struct blktap_device *tapdev = &tap->device;
	struct request *rq = request->rq;

	blktap_ring_unmap_request(tap, request);

	blktap_ring_free_request(tap, request);

	dev_dbg(disk_to_dev(tapdev->gd),
		"end_request: op=%d error=%d bytes=%d\n",
		rq_data_dir(rq), error, blk_rq_bytes(rq));

	blktap_end_rq(rq, error);
}

int
blktap_device_make_request(struct blktap *tap, struct request *rq)
{
	struct blktap_device *tapdev = &tap->device;
	struct blktap_request *request;
	int write, nsegs;
	int err;

	request = blktap_ring_make_request(tap);
	if (IS_ERR(request)) {
		err = PTR_ERR(request);
		request = NULL;

		if (err == -ENOSPC || err == -ENOMEM)
			goto stop;

		goto fail;
	}

	write = rq_data_dir(rq) == WRITE;
	nsegs = blk_rq_map_sg(rq->q, rq, request->sg_table);

	dev_dbg(disk_to_dev(tapdev->gd),
		"make_request: op=%c bytes=%d nsegs=%d\n",
		write ? 'w' : 'r', blk_rq_bytes(rq), nsegs);

	request->rq = rq;
	request->operation = write ? BLKIF_OP_WRITE : BLKIF_OP_READ;
	if (unlikely(rq->cmd_type == REQ_TYPE_BLOCK_PC))
		request->operation = BLKIF_OP_PACKET;

	err = blktap_request_get_pages(tap, request, nsegs);
	if (err)
		goto stop;

	err = blktap_ring_map_request(tap, request);
	if (err)
		goto fail;

	blktap_ring_submit_request(tap, request);

	return 0;

stop:
	tap->stats.st_oo_req++;
	err = -EBUSY;

_out:
	if (request)
		blktap_ring_free_request(tap, request);

	return err;
fail:
	if (printk_ratelimit())
		dev_warn(disk_to_dev(tapdev->gd),
			 "make request: %d, failing\n", err);
	goto _out;
}

/*
 * called from tapdisk context
 */
void
blktap_device_run_queue(struct blktap *tap)
{
	struct blktap_device *tapdev = &tap->device;
	struct request_queue *q;
	struct request *rq;
	int err;

	if (!tapdev->gd)
		return;

	q = tapdev->gd->queue;

	spin_lock_irq(&tapdev->lock);
	queue_flag_clear(QUEUE_FLAG_STOPPED, q);

	do {
		rq = __blktap_next_queued_rq(q);
		if (!rq)
			break;

		if (rq->cmd_type != REQ_TYPE_FS) {
			__blktap_end_queued_rq(rq, -EOPNOTSUPP);
			continue;
		}

		spin_unlock_irq(&tapdev->lock);

		err = blktap_device_make_request(tap, rq);

		spin_lock_irq(&tapdev->lock);

		if (err == -EBUSY) {
			blk_stop_queue(q);
			break;
		}

		__blktap_dequeue_rq(rq);

		if (unlikely(err))
			__blktap_end_rq(rq, err);
	} while (1);

	spin_unlock_irq(&tapdev->lock);
}

static void
blktap_device_do_request(struct request_queue *rq)
{
	struct blktap_device *tapdev = rq->queuedata;
	struct blktap *tap = dev_to_blktap(tapdev);

	blktap_ring_kick_user(tap);
}

static void
blktap_device_configure(struct blktap *tap,
			struct blktap_params *params)
{
	struct request_queue *rq;
	struct blktap_device *dev = &tap->device;

	dev = &tap->device;
	rq  = dev->gd->queue;

	spin_lock_irq(&dev->lock);

	set_capacity(dev->gd, params->capacity);

	/* Hard sector size and max sectors impersonate the equiv. hardware. */
	blk_queue_logical_block_size(rq, params->sector_size);
	blk_queue_max_hw_sectors(rq, 512);

	/* Each segment in a request is up to an aligned page in size. */
	blk_queue_segment_boundary(rq, PAGE_SIZE - 1);
	blk_queue_max_segment_size(rq, PAGE_SIZE);

	/* Ensure a merged request will fit in a single I/O ring slot. */
	blk_queue_max_segments(rq, BLKIF_MAX_SEGMENTS_PER_REQUEST);

	/* Make sure buffer addresses are sector-aligned. */
	blk_queue_dma_alignment(rq, 511);

	spin_unlock_irq(&dev->lock);
}

static int
blktap_device_validate_params(struct blktap *tap,
			      struct blktap_params *params)
{
	struct device *dev = tap->ring.dev;
	int sector_order, name_sz;

	sector_order = ffs(params->sector_size) - 1;

	if (sector_order <  9 ||
	    sector_order > 12 ||
	    params->sector_size != 1U<<sector_order)
		goto fail;

	if (!params->capacity ||
	    (params->capacity > ULLONG_MAX >> sector_order))
		goto fail;

	name_sz = min(sizeof(params->name), sizeof(tap->name));
	if (strnlen(params->name, name_sz) >= name_sz)
		goto fail;

	return 0;

fail:
	params->name[name_sz-1] = 0;
	dev_err(dev, "capacity: %llu, sector-size: %lu, name: %s\n",
		params->capacity, params->sector_size, params->name);
	return -EINVAL;
}

int
blktap_device_destroy(struct blktap *tap)
{
	struct blktap_device *tapdev = &tap->device;
	struct block_device *bdev;
	struct gendisk *gd;
	int err;

	gd = tapdev->gd;
	if (!gd)
		return 0;

	bdev = bdget_disk(gd, 0);

	err = !mutex_trylock(&bdev->bd_mutex);
	if (err) {
		/* NB. avoid a deadlock. the last opener syncs the
		 * bdev holding bd_mutex. */
		err = -EBUSY;
		goto out_nolock;
	}

	if (bdev->bd_openers) {
		err = -EBUSY;
		goto out;
	}

	del_gendisk(gd);
	gd->private_data = NULL;

	blk_cleanup_queue(gd->queue);

	put_disk(gd);
	tapdev->gd = NULL;

	clear_bit(BLKTAP_DEVICE, &tap->dev_inuse);
	err = 0;
out:
	mutex_unlock(&bdev->bd_mutex);
out_nolock:
	bdput(bdev);

	return err;
}

static void
blktap_device_fail_queue(struct blktap *tap)
{
	struct blktap_device *tapdev = &tap->device;
	struct request_queue *q = tapdev->gd->queue;

	spin_lock_irq(&tapdev->lock);
	queue_flag_clear(QUEUE_FLAG_STOPPED, q);

	do {
		struct request *rq = __blktap_next_queued_rq(q);
		if (!rq)
			break;

		__blktap_end_queued_rq(rq, -EIO);
	} while (1);

	spin_unlock_irq(&tapdev->lock);
}

static int
blktap_device_try_destroy(struct blktap *tap)
{
	int err;

	err = blktap_device_destroy(tap);
	if (err)
		blktap_device_fail_queue(tap);

	return err;
}

void
blktap_device_destroy_sync(struct blktap *tap)
{
	wait_event(tap->ring.poll_wait,
		   !blktap_device_try_destroy(tap));
}

static char *blktap_devnode(struct gendisk *gd, mode_t *mode)
{
	return kasprintf(GFP_KERNEL, BLKTAP2_DEV_DIR "tapdev%u",
			 gd->first_minor);
}

int
blktap_device_create(struct blktap *tap, struct blktap_params *params)
{
	int minor, err;
	struct gendisk *gd;
	struct request_queue *rq;
	struct blktap_device *tapdev;

	gd     = NULL;
	rq     = NULL;
	tapdev = &tap->device;
	minor  = tap->minor;

	if (test_bit(BLKTAP_DEVICE, &tap->dev_inuse))
		return -EEXIST;

	if (blktap_device_validate_params(tap, params))
		return -EINVAL;

	gd = alloc_disk(1);
	if (!gd) {
		err = -ENOMEM;
		goto fail;
	}

	if (minor < 26) {
		sprintf(gd->disk_name, "td%c", 'a' + minor % 26);
	} else if (minor < (26 + 1) * 26) {
		sprintf(gd->disk_name, "td%c%c",
			'a' + minor / 26 - 1,'a' + minor % 26);
	} else {
		const unsigned int m1 = (minor / 26 - 1) / 26 - 1;
		const unsigned int m2 = (minor / 26 - 1) % 26;
		const unsigned int m3 =  minor % 26;
		sprintf(gd->disk_name, "td%c%c%c",
			'a' + m1, 'a' + m2, 'a' + m3);
	}

	gd->major = blktap_device_major;
	gd->first_minor = minor;
	gd->devnode = blktap_devnode;
	gd->fops = &blktap_device_file_operations;
	gd->private_data = tapdev;

	spin_lock_init(&tapdev->lock);
	rq = blk_init_queue(blktap_device_do_request, &tapdev->lock);
	if (!rq) {
		err = -ENOMEM;
		goto fail;
	}
	elevator_init(rq, "noop");

	gd->queue     = rq;
	rq->queuedata = tapdev;
	tapdev->gd    = gd;

	blktap_device_configure(tap, params);
	add_disk(gd);

	if (params->name[0])
		strncpy(tap->name, params->name, sizeof(tap->name)-1);

	set_bit(BLKTAP_DEVICE, &tap->dev_inuse);

	dev_info(disk_to_dev(gd), "sector-size: %u capacity: %llu\n",
		 queue_logical_block_size(rq),
		 (unsigned long long)get_capacity(gd));

	return 0;

fail:
	if (gd)
		del_gendisk(gd);
	if (rq)
		blk_cleanup_queue(rq);

	return err;
}

size_t
blktap_device_debug(struct blktap *tap, char *buf, size_t size)
{
	struct gendisk *disk = tap->device.gd;
	struct request_queue *q;
	struct block_device *bdev;
	char *s = buf, *end = buf + size;

	if (!disk)
		return 0;

	q = disk->queue;

	s += snprintf(s, end - s,
		      "disk capacity:%llu sector size:%u\n",
		      (unsigned long long)get_capacity(disk),
		      queue_logical_block_size(q));

	s += snprintf(s, end - s,
		      "queue flags:%#lx plugged:%d stopped:%d empty:%d\n",
		      q->queue_flags,
		      blk_queue_plugged(q), blk_queue_stopped(q),
		      elv_queue_empty(q));

	bdev = bdget_disk(disk, 0);
	if (bdev) {
		s += snprintf(s, end - s,
			      "bdev openers:%d closed:%d\n",
			      bdev->bd_openers,
			      test_bit(BLKTAP_DEVICE_CLOSED, &tap->dev_inuse));
		bdput(bdev);
	}

	return s - buf;
}

int __init
blktap_device_init()
{
	int major;

	/* Dynamically allocate a major for this device */
	major = register_blkdev(0, "tapdev");
	if (major < 0) {
		BTERR("Couldn't register blktap device\n");
		return -ENOMEM;
	}

	blktap_device_major = major;
	BTINFO("blktap device major %d\n", major);

	return 0;
}

void
blktap_device_exit(void)
{
	if (blktap_device_major)
		unregister_blkdev(blktap_device_major, "tapdev");
}
