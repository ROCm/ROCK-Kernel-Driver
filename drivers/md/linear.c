/*
   linear.c : Multiple Devices driver for Linux
	      Copyright (C) 1994-96 Marc ZYNGIER
	      <zyngier@ufr-info-p7.ibp.fr> or
	      <maz@gloups.fdn.fr>

   Linear mode management functions.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.
   
   You should have received a copy of the GNU General Public License
   (for example /usr/src/linux/COPYING); if not, write to the Free
   Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  
*/

#include <linux/module.h>

#include <linux/raid/md.h>
#include <linux/slab.h>
#include <linux/bio.h>
#include <linux/raid/linear.h>

#define MAJOR_NR MD_MAJOR
#define MD_DRIVER
#define MD_PERSONALITY
#define DEVICE_NR(device) (minor(device))

static int linear_run (mddev_t *mddev)
{
	linear_conf_t *conf;
	struct linear_hash *table;
	mdk_rdev_t *rdev;
	int size, i, nb_zone, cnt;
	unsigned int curr_offset;
	struct list_head *tmp;

	MOD_INC_USE_COUNT;

	conf = kmalloc (sizeof (*conf), GFP_KERNEL);
	if (!conf)
		goto out;
	memset(conf, 0, sizeof(*conf));
	mddev->private = conf;

	/*
	 * Find the smallest device.
	 */

	conf->smallest = NULL;
	cnt = 0;
	ITERATE_RDEV(mddev,rdev,tmp) {
		int j = rdev->raid_disk;
		dev_info_t *disk = conf->disks + j;

		if (j < 0 || j > mddev->raid_disks || disk->rdev) {
			printk("linear: disk numbering problem. Aborting!\n");
			goto out;
		}

		disk->rdev = rdev;
		disk->size = rdev->size;

		if (!conf->smallest || (disk->size < conf->smallest->size))
			conf->smallest = disk;
		cnt++;
	}
	if (cnt != mddev->raid_disks) {
		printk("linear: not enough drives present. Aborting!\n");
		goto out;
	}

	nb_zone = conf->nr_zones =
		md_size[mdidx(mddev)] / conf->smallest->size +
		((md_size[mdidx(mddev)] % conf->smallest->size) ? 1 : 0);
  
	conf->hash_table = kmalloc (sizeof (struct linear_hash) * nb_zone,
					GFP_KERNEL);
	if (!conf->hash_table)
		goto out;

	/*
	 * Here we generate the linear hash table
	 */
	table = conf->hash_table;
	size = 0;
	curr_offset = 0;
	for (i = 0; i < cnt; i++) {
		dev_info_t *disk = conf->disks + i;

		disk->offset = curr_offset;
		curr_offset += disk->size;

		if (size < 0) {
			table[-1].dev1 = disk;
		}
		size += disk->size;

		while (size>0) {
			table->dev0 = disk;
			table->dev1 = NULL;
			size -= conf->smallest->size;
			table++;
		}
	}
	if (table-conf->hash_table != nb_zone)
		BUG();

	return 0;

out:
	if (conf)
		kfree(conf);
	MOD_DEC_USE_COUNT;
	return 1;
}

static int linear_stop (mddev_t *mddev)
{
	linear_conf_t *conf = mddev_to_conf(mddev);
  
	kfree(conf->hash_table);
	kfree(conf);

	MOD_DEC_USE_COUNT;

	return 0;
}

static int linear_make_request (request_queue_t *q, struct bio *bio)
{
	mddev_t *mddev = q->queuedata;
	linear_conf_t *conf = mddev_to_conf(mddev);
	struct linear_hash *hash;
	dev_info_t *tmp_dev;
	long block;

	block = bio->bi_sector >> 1;
	hash = conf->hash_table + (block / conf->smallest->size);
  
	if (block >= (hash->dev0->size + hash->dev0->offset)) {
		if (!hash->dev1) {
			printk ("linear_make_request : hash->dev1==NULL for block %ld\n",
						block);
			bio_io_error(bio, bio->bi_size);
			return 0;
		}
		tmp_dev = hash->dev1;
	} else
		tmp_dev = hash->dev0;
    
	if (block >= (tmp_dev->size + tmp_dev->offset)
				|| block < tmp_dev->offset) {
		printk ("linear_make_request: Block %ld out of bounds on dev %s size %ld offset %ld\n", block, bdevname(tmp_dev->rdev->bdev), tmp_dev->size, tmp_dev->offset);
		bio_io_error(bio, bio->bi_size);
		return 0;
	}
	bio->bi_bdev = tmp_dev->rdev->bdev;
	bio->bi_sector = bio->bi_sector - (tmp_dev->offset << 1);

	return 1;
}

static int linear_status (char *page, mddev_t *mddev)
{
	int sz = 0;

#undef MD_DEBUG
#ifdef MD_DEBUG
	int j;
	linear_conf_t *conf = mddev_to_conf(mddev);
  
	sz += sprintf(page+sz, "      ");
	for (j = 0; j < conf->nr_zones; j++)
	{
		sz += sprintf(page+sz, "[%s",
			bdev_partition_name(conf->hash_table[j].dev0->rdev->bdev));

		if (conf->hash_table[j].dev1)
			sz += sprintf(page+sz, "/%s] ",
			  bdev_partition_name(conf->hash_table[j].dev1->rdev->bdev));
		else
			sz += sprintf(page+sz, "] ");
	}
	sz += sprintf(page+sz, "\n");
#endif
	sz += sprintf(page+sz, " %dk rounding", mddev->chunk_size/1024);
	return sz;
}


static mdk_personality_t linear_personality=
{
	.name		= "linear",
	.make_request	= linear_make_request,
	.run		= linear_run,
	.stop		= linear_stop,
	.status		= linear_status,
};

static int __init linear_init (void)
{
	return register_md_personality (LINEAR, &linear_personality);
}

static void linear_exit (void)
{
	unregister_md_personality (LINEAR);
}


module_init(linear_init);
module_exit(linear_exit);
MODULE_LICENSE("GPL");
