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
#include <linux/raid/linear.h>

#define MAJOR_NR MD_MAJOR
#define MD_DRIVER
#define MD_PERSONALITY

/*
 * find which device holds a particular offset 
 */
static inline dev_info_t *which_dev(mddev_t *mddev, sector_t sector)
{
	struct linear_hash *hash;
	linear_conf_t *conf = mddev_to_conf(mddev);
	sector_t block = sector >> 1;

	/*
	 * sector_div(a,b) returns the remainer and sets a to a/b
	 */
	(void)sector_div(block, conf->smallest->size);
	hash = conf->hash_table + block;

	if ((sector>>1) >= (hash->dev0->size + hash->dev0->offset))
		return hash->dev1;
	else
		return hash->dev0;
}


/**
 *	linear_mergeable_bvec -- tell bio layer if a two requests can be merged
 *	@q: request queue
 *	@bio: the buffer head that's been built up so far
 *	@biovec: the request that could be merged to it.
 *
 *	Return amount of bytes we can take at this offset
 */
static int linear_mergeable_bvec(request_queue_t *q, struct bio *bio, struct bio_vec *biovec)
{
	mddev_t *mddev = q->queuedata;
	dev_info_t *dev0;
	unsigned long maxsectors, bio_sectors = bio->bi_size >> 9;

	dev0 = which_dev(mddev, bio->bi_sector);
	maxsectors = (dev0->size << 1) - (bio->bi_sector - (dev0->offset<<1));

	if (maxsectors < bio_sectors)
		maxsectors = 0;
	else
		maxsectors -= bio_sectors;

	if (maxsectors <= (PAGE_SIZE >> 9 ) && bio_sectors == 0)
		return biovec->bv_len;
	/* The bytes available at this offset could be really big,
	 * so we cap at 2^31 to avoid overflow */
	if (maxsectors > (1 << (31-9)))
		return 1<<31;
	return maxsectors << 9;
}

static int linear_run (mddev_t *mddev)
{
	linear_conf_t *conf;
	struct linear_hash *table;
	mdk_rdev_t *rdev;
	int size, i, nb_zone, cnt;
	unsigned int curr_offset;
	struct list_head *tmp;

	conf = kmalloc (sizeof (*conf) + mddev->raid_disks*sizeof(dev_info_t),
			GFP_KERNEL);
	if (!conf)
		goto out;
	memset(conf, 0, sizeof(*conf) + mddev->raid_disks*sizeof(dev_info_t));
	mddev->private = conf;

	/*
	 * Find the smallest device.
	 */

	conf->smallest = NULL;
	cnt = 0;
	mddev->array_size = 0;

	ITERATE_RDEV(mddev,rdev,tmp) {
		int j = rdev->raid_disk;
		dev_info_t *disk = conf->disks + j;

		if (j < 0 || j > mddev->raid_disks || disk->rdev) {
			printk("linear: disk numbering problem. Aborting!\n");
			goto out;
		}

		disk->rdev = rdev;
		blk_queue_stack_limits(mddev->queue,
				       rdev->bdev->bd_disk->queue);
		disk->size = rdev->size;
		mddev->array_size += rdev->size;

		if (!conf->smallest || (disk->size < conf->smallest->size))
			conf->smallest = disk;
		cnt++;
	}
	if (cnt != mddev->raid_disks) {
		printk("linear: not enough drives present. Aborting!\n");
		goto out;
	}

	/*
	 * This code was restructured to work around a gcc-2.95.3 internal
	 * compiler error.  Alter it with care.
	 */
	{
		sector_t sz;
		unsigned round;
		unsigned long base;

		sz = mddev->array_size;
		base = conf->smallest->size;
		round = sector_div(sz, base);
		nb_zone = conf->nr_zones = sz + (round ? 1 : 0);
	}
			
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

	blk_queue_merge_bvec(mddev->queue, linear_mergeable_bvec);
	return 0;

out:
	if (conf)
		kfree(conf);
	return 1;
}

static int linear_stop (mddev_t *mddev)
{
	linear_conf_t *conf = mddev_to_conf(mddev);
  
	kfree(conf->hash_table);
	kfree(conf);

	return 0;
}

static int linear_make_request (request_queue_t *q, struct bio *bio)
{
	mddev_t *mddev = q->queuedata;
	dev_info_t *tmp_dev;
	sector_t block;

	tmp_dev = which_dev(mddev, bio->bi_sector);
	block = bio->bi_sector >> 1;
  
	if (unlikely(!tmp_dev)) {
		printk("linear_make_request: hash->dev1==NULL for block %llu\n",
			(unsigned long long)block);
		bio_io_error(bio, bio->bi_size);
		return 0;
	}
    
	if (unlikely(block >= (tmp_dev->size + tmp_dev->offset)
		     || block < tmp_dev->offset)) {
		char b[BDEVNAME_SIZE];

		printk("linear_make_request: Block %llu out of bounds on "
			"dev %s size %ld offset %ld\n",
			(unsigned long long)block,
			bdevname(tmp_dev->rdev->bdev, b),
			tmp_dev->size, tmp_dev->offset);
		bio_io_error(bio, bio->bi_size);
		return 0;
	}
	if (unlikely(bio->bi_sector + (bio->bi_size >> 9) >
		     (tmp_dev->offset + tmp_dev->size)<<1)) {
		/* This bio crosses a device boundary, so we have to
		 * split it.
		 */
		struct bio_pair *bp;
		bp = bio_split(bio, bio_split_pool, 
			       (bio->bi_sector + (bio->bi_size >> 9) -
				(tmp_dev->offset + tmp_dev->size))<<1);
		if (linear_make_request(q, &bp->bio1))
			generic_make_request(&bp->bio1);
		if (linear_make_request(q, &bp->bio2))
			generic_make_request(&bp->bio2);
		bio_pair_release(bp);
		return 0;
	}
		    
	bio->bi_bdev = tmp_dev->rdev->bdev;
	bio->bi_sector = bio->bi_sector - (tmp_dev->offset << 1) + tmp_dev->rdev->data_offset;

	return 1;
}

static void linear_status (struct seq_file *seq, mddev_t *mddev)
{

#undef MD_DEBUG
#ifdef MD_DEBUG
	int j;
	linear_conf_t *conf = mddev_to_conf(mddev);
  
	seq_printf(seq, "      ");
	for (j = 0; j < conf->nr_zones; j++)
	{
		char b[BDEVNAME_SIZE];
		seq_printf(seq, "[%s",
			   bdevname(conf->hash_table[j].dev0->rdev->bdev,b));

		if (conf->hash_table[j].dev1)
			seq_printf(seq, "/%s] ",
				   bdevname(conf->hash_table[j].dev1->rdev->bdev,b));
		else
			seq_printf(seq, "] ");
	}
	seq_printf(seq, "\n");
#endif
	seq_printf(seq, " %dk rounding", mddev->chunk_size/1024);
}


static mdk_personality_t linear_personality=
{
	.name		= "linear",
	.owner		= THIS_MODULE,
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
MODULE_ALIAS("md-personality-1"); /* LINEAR */
