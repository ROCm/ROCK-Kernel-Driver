/*
 * Copyright (C) 2003 Sistina Software (UK) Limited.
 *
 * This file is released under the GPL.
 */

#include "dm.h"

#include <linux/module.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/bio.h>
#include <linux/slab.h>

typedef typeof(jiffies) jiffy_t;

/*
 * Flakey: Used for testing only, simulates intermittent,
 * catastrophic device failure.
 */
struct flakey {
	struct dm_dev *dev;
	jiffy_t start_time;
	sector_t start;
	unsigned up_interval;
	unsigned down_interval;
};

/*
 * Construct a flakey mapping: <dev_path> <offset> <up interval> <down interval>
 */
static int flakey_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
	struct flakey *f;

	if (argc != 4) {
		ti->error = "dm-flakey: Invalid argument count";
		return -EINVAL;
	}

	f = kmalloc(sizeof(*f), GFP_KERNEL);
	if (!f) {
		ti->error = "dm-flakey: Cannot allocate linear context";
		return -ENOMEM;
	}
	f->start_time = jiffies;

	if (sscanf(argv[1], SECTOR_FORMAT, &f->start) != 1) {
		ti->error = "dm-flakey: Invalid device sector";
		goto bad;
	}

	if (sscanf(argv[2], "%u", &f->up_interval) != 1) {
		ti->error = "dm-flakey: Invalid up interval";
		goto bad;
	}

	if (sscanf(argv[3], "%u", &f->down_interval) != 1) {
		ti->error = "dm-flakey: Invalid down interval";
		goto bad;
	}

	if (dm_get_device(ti, argv[0], f->start, ti->len,
			  dm_table_get_mode(ti->table), &f->dev)) {
		ti->error = "dm-flakey: Device lookup failed";
		goto bad;
	}

	ti->private = f;
	return 0;

 bad:
	kfree(f);
	return -EINVAL;
}

static void flakey_dtr(struct dm_target *ti)
{
	struct flakey *f = (struct flakey *) ti->private;

	dm_put_device(ti, f->dev);
	kfree(f);
}

static int flakey_map(struct dm_target *ti, struct bio *bio,
		      union map_info *map_context)
{
	struct flakey *f = (struct flakey *) ti->private;
	unsigned elapsed;

	/* are we alive ? */
	elapsed = (jiffies - f->start_time) / HZ;
	elapsed %= (f->up_interval + f->down_interval);
	if (elapsed >= f->up_interval)
		return -EIO;

	else {
		bio->bi_bdev = f->dev->bdev;
		bio->bi_sector = f->start + (bio->bi_sector - ti->begin);
	}

	return 1;
}

static int flakey_status(struct dm_target *ti, status_type_t type,
			 char *result, unsigned int maxlen)
{
	struct flakey *f = (struct flakey *) ti->private;
	char buffer[32];

	switch (type) {
	case STATUSTYPE_INFO:
		result[0] = '\0';
		break;

	case STATUSTYPE_TABLE:
		format_dev_t(buffer, f->dev->bdev->bd_dev);
		snprintf(result, maxlen, "%s " SECTOR_FORMAT, buffer, f->start);
		break;
	}
	return 0;
}

static struct target_type flakey_target = {
	.name   = "flakey",
	.version= {1, 0, 1},
	.module = THIS_MODULE,
	.ctr    = flakey_ctr,
	.dtr    = flakey_dtr,
	.map    = flakey_map,
	.status = flakey_status,
};

int __init dm_flakey_init(void)
{
	int r = dm_register_target(&flakey_target);

	if (r < 0)
		DMERR("flakey: register failed %d", r);

	return r;
}

void __exit dm_flakey_exit(void)
{
	int r = dm_unregister_target(&flakey_target);

	if (r < 0)
		DMERR("flakey: unregister failed %d", r);
}

/* Module hooks */
module_init(dm_flakey_init);
module_exit(dm_flakey_exit);

MODULE_DESCRIPTION(DM_NAME " flakey target");
MODULE_AUTHOR("Joe Thornber <thornber@sistina.com>");
MODULE_LICENSE("GPL");
