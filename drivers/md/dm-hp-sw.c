/*
 * Copyright (C) 2005 Mike Christie, All rights reserved.
 * Copyright (C) 2007 Hannes Reinecke
 * Copyright (C) 2007 SUSE Linux Products GmbH
 *
 * This file is released under the GPL.
 *
 * Basic, very basic, support for HP StorageWorks and FSC FibreCat
 */
#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>

#define DM_MSG_PREFIX "multipath hp_sw"

#include "dm.h"
#include "dm-hw-handler.h"

struct hp_sw_handler {
	spinlock_t lock;

	unsigned int count;
	unsigned char sense[SCSI_SENSE_BUFFERSIZE];
};

#define HP_SW_FAILOVER_TIMEOUT (60 * HZ)

static inline void free_bio(struct bio *bio)
{
	__free_page(bio->bi_io_vec[0].bv_page);
	bio_put(bio);
}

static int hp_sw_endio(struct bio *bio, unsigned int bytes_done, int error)
{
	struct dm_path *path = bio->bi_private;

	if (bio->bi_size)
		return 1;

	/* We also need to look at the sense keys here whether or not to
	 * switch to the next PG etc.
	 *
	 * For now simple logic: either it works or it doesn't.
	 */
	if (error)
		dm_pg_init_complete(path, MP_FAIL_PATH);
	else
		dm_pg_init_complete(path, 0);

	/* request is freed in block layer */
	free_bio(bio);

	return 0;
}

static struct bio *get_failover_bio(struct dm_path *path, unsigned data_size)
{
	struct bio *bio;
	struct page *page;

	bio = bio_alloc(GFP_ATOMIC, 1);
	if (!bio) {
		DMERR("get_failover_bio: bio_alloc() failed.");
		return NULL;
	}

	bio->bi_rw |= (1 << BIO_RW);
	bio->bi_bdev = path->dev->bdev;
	bio->bi_sector = 0;
	bio->bi_private = path;
	bio->bi_end_io = hp_sw_endio;

	page = alloc_page(GFP_ATOMIC);
	if (!page) {
		DMERR("get_failover_bio: alloc_page() failed.");
		bio_put(bio);
		return NULL;
	}

	if (bio_add_page(bio, page, data_size, 0) != data_size) {
		DMERR("get_failover_bio: alloc_page() failed.");
		__free_page(page);
		bio_put(bio);
		return NULL;
	}

	return bio;
}

static struct request *get_failover_req(struct hp_sw_handler *h,
					struct bio *bio, struct dm_path *path)
{
	struct request *rq;
	struct block_device *bdev = bio->bi_bdev;
	struct request_queue *q = bdev_get_queue(bdev);

	/* FIXME: Figure out why it fails with GFP_ATOMIC. */
	rq = blk_get_request(q, WRITE, __GFP_WAIT);
	if (!rq) {
		DMERR("get_failover_req: blk_get_request failed");
		return NULL;
	}

	rq->bio = rq->biotail = bio;
	blk_rq_bio_prep(q, rq, bio);

	rq->rq_disk = bdev->bd_contains->bd_disk;

	/* bio backed don't set data */
	rq->buffer = rq->data = NULL;
	/* rq data_len used for pc cmd's request_bufflen */
	rq->data_len = bio->bi_size;

	rq->sense = h->sense;
	memset(rq->sense, 0, SCSI_SENSE_BUFFERSIZE);
	rq->sense_len = 0;

	memset(&rq->cmd, 0, BLK_MAX_CDB);

	rq->timeout = HP_SW_FAILOVER_TIMEOUT;
	rq->cmd_type = REQ_TYPE_BLOCK_PC;
	rq->cmd_flags |= (REQ_FAILFAST | REQ_NOMERGE);

	return rq;
}

static struct request *hp_sw_command_get(struct hp_sw_handler *h,
					 struct dm_path *path)
{
	struct bio *bio;
	struct request *rq;
	unsigned char *cdb;
	unsigned char start_stop_cmd[] = {
		0, 0, 0, 0, 0, 0
	};
	unsigned data_size = sizeof(start_stop_cmd);

	/* get bio backing */
	bio = get_failover_bio(path, data_size);
	if (!bio) {
		DMERR("command_get: no bio");
		return NULL;
	}

	cdb = (unsigned char *)bio_data(bio);
	memset(cdb, 0, data_size);

	/* get request for block layer packet command */
	rq = get_failover_req(h, bio, path);
	if (!rq) {
		DMERR("command_get: no rq");
		free_bio(bio);
		return NULL;
	}

	/* Prepare the command. */
	rq->cmd[0] = START_STOP;
	rq->cmd[4] = 1;
	rq->cmd_len = COMMAND_SIZE(rq->cmd[0]);

	/* Initialize retry count */
	h->count = 5;

	return rq;
}

static void hp_sw_pg_init(struct hw_handler *hwh, unsigned bypassed,
			  struct dm_path *path)
{
	struct request *rq;
	struct request_queue *q = bdev_get_queue(path->dev->bdev);

	if (!q) {
		DMINFO("pg_init: no queue");
		goto fail_path;
	}

	rq = hp_sw_command_get(hwh->context, path);
	if (!rq) {
		DMERR("pg_init: no rq");
		goto fail_path;
	}

	DMINFO("sending START_STOP_UNIT command");
	elv_add_request(q, rq, ELEVATOR_INSERT_FRONT, 1);
	return;

fail_path:
	dm_pg_init_complete(path, MP_FAIL_PATH);
}

static unsigned hp_sw_error(struct hw_handler *hwh, struct bio *bio)
{
	struct hp_sw_handler *h = hwh->context;
	int sense;

#if 0
	if (bio_sense_valid(bio)) {
		sense = bio_sense_value(bio); /* sense key / asc / ascq */

		if (sense == 0x020403) {
			/*
			 * LUN Not Ready - Manual Intervention Required
			 *
			 * HP sources seem to indicate that we can retry
			 * this one.
			 */
			if (!h->count) {
				return MP_FAIL_PATH;
			} else {
				DMINFO("retrying START_STOP_UNIT command");
				h->count--;
				return 0;
			}
		}
	}
#endif
	/* Try default handler */
	return dm_scsi_err_handler(hwh, bio);
}

static struct hp_sw_handler *alloc_hp_sw_handler(void)
{
	struct hp_sw_handler *h = kmalloc(sizeof(*h), GFP_KERNEL);

	if (h) {
		memset(h, 0, sizeof(*h));
		spin_lock_init(&h->lock);
	}

	return h;
}

static int hp_sw_create(struct hw_handler *hwh, unsigned argc, char **argv)
{
	struct hp_sw_handler *h;

	h = alloc_hp_sw_handler();
	if (!h)
		return -ENOMEM;

	hwh->context = h;

	return 0;
}

static void hp_sw_destroy(struct hw_handler *hwh)
{
	struct hp_sw_handler *h = (struct hp_sw_handler *) hwh->context;

	kfree(h);
	hwh->context = NULL;
}

static struct hw_handler_type hp_sw_hwh = {
	.name = "hp_sw",
	.module = THIS_MODULE,
	.create = hp_sw_create,
	.destroy = hp_sw_destroy,
	.pg_init = hp_sw_pg_init,
	.error = hp_sw_error,
};

static int __init hp_sw_init(void)
{
	int r;

	r = dm_register_hw_handler(&hp_sw_hwh);
	if (r < 0)
		DMERR("register failed %d", r);

	DMINFO("version 0.5 loaded");

	return r;
}

static void __exit hp_sw_exit(void)
{
	int r;

	r = dm_unregister_hw_handler(&hp_sw_hwh);
	if (r < 0)
		DMERR("unregister failed %d", r);
}

module_init(hp_sw_init);
module_exit(hp_sw_exit);

MODULE_DESCRIPTION("HP StorageWorks and FSC FibreCat support for dm-multipath");
MODULE_AUTHOR("Mike Christie <michaelc@cs.wisc.edu>");
MODULE_LICENSE("GPL");
