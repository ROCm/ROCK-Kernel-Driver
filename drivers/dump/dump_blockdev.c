/*
 * Implements the dump driver interface for saving a dump to 
 * a block device through the kernel's generic low level block i/o
 * routines.
 *
 * Started: June 2002 - Mohamed Abbas <mohamed.abbas@intel.com>
 * 	Moved original lkcd kiobuf dump i/o code from dump_base.c
 * 	to use generic dump device interfaces
 *
 * Sept 2002 - Bharata B. Rao <bharata@in.ibm.com>
 * 	Convert dump i/o to directly use bio instead of kiobuf for 2.5
 *
 * Oct 2002  - Suparna Bhattacharya <suparna@in.ibm.com>
 * 	Rework to new dumpdev.h structures, implement open/close/
 * 	silence, misc fixes (blocknr removal, bio_add_page usage)  
 *
 * Copyright (C) 1999 - 2002 Silicon Graphics, Inc. All rights reserved.
 * Copyright (C) 2001 - 2002 Matt D. Robinson.  All rights reserved.
 * Copyright (C) 2002 International Business Machines Corp. 
 *
 * This code is released under version 2 of the GNU GPL.
 */

#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/bio.h>
#include <asm/hardirq.h>
#include <linux/dump.h>
#include "dump_methods.h"

extern void *dump_page_buf;

/* The end_io callback for dump i/o completion */
static int
dump_bio_end_io(struct bio *bio, unsigned int bytes_done, int error)
{
	struct dump_blockdev *dump_bdev;

	if (bio->bi_size) {
		/* some bytes still left to transfer */
		return 1; /* not complete */
	}

	dump_bdev = (struct dump_blockdev *)bio->bi_private;
	if (error) {
		printk("IO error while writing the dump, aborting\n");
	}

	dump_bdev->err = error;

	/* no wakeup needed, since caller polls for completion */
	return 0;
}

/* Check if the dump bio is already mapped to the specified buffer */
static int
dump_block_map_valid(struct dump_blockdev *dev, struct page *page, 
	int len) 
{
	struct bio *bio = dev->bio;
	unsigned long bsize = 0;

	if (!bio->bi_vcnt)
		return 0; /* first time, not mapped */


	if ((bio_page(bio) != page) || (len > bio->bi_vcnt << PAGE_SHIFT))
		return 0; /* buffer not mapped */

	bsize = bdev_hardsect_size(bio->bi_bdev);
	if ((len & (PAGE_SIZE - 1)) || (len & bsize))
		return 0; /* alignment checks needed */

	/* quick check to decide if we need to redo bio_add_page */
	if (bdev_get_queue(bio->bi_bdev)->merge_bvec_fn)
		return 0; /* device may have other restrictions */

	return 1; /* already mapped */
}

/* 
 * Set up the dump bio for i/o from the specified buffer 
 * Return value indicates whether the full buffer could be mapped or not
 */
static int
dump_block_map(struct dump_blockdev *dev, void *buf, int len)
{
	struct page *page = virt_to_page(buf);
	struct bio *bio = dev->bio;
	unsigned long bsize = 0;

	bio->bi_bdev = dev->bdev;
	bio->bi_sector = (dev->start_offset + dev->ddev.curr_offset) >> 9;
	bio->bi_idx = 0; /* reset index to the beginning */

	if (dump_block_map_valid(dev, page, len)) {
		/* already mapped and usable rightaway */
		bio->bi_size = len; /* reset size to the whole bio */
		bio->bi_vcnt = len / PAGE_SIZE; /* Set the proper vector cnt */
	} else {
		/* need to map the bio */
		bio->bi_size = 0;
		bio->bi_vcnt = 0;
		bsize = bdev_hardsect_size(bio->bi_bdev);

		/* first a few sanity checks */
		if (len < bsize) {
			printk("map: len less than hardsect size \n");
			return -EINVAL;
		}

		if ((unsigned long)buf & bsize) {
			printk("map: not aligned \n");
			return -EINVAL;
		}

		/* assume contig. page aligned low mem buffer( no vmalloc) */
		if ((page_address(page) != buf) || (len & (PAGE_SIZE - 1))) {
			printk("map: invalid buffer alignment!\n");
			return -EINVAL; 
		}
		/* finally we can go ahead and map it */
		while (bio->bi_size < len)
			if (bio_add_page(bio, page++, PAGE_SIZE, 0) == 0) {
				break;
			}

		bio->bi_end_io = dump_bio_end_io;
		bio->bi_private = dev;
	}

	if (bio->bi_size != len) {
		printk("map: bio size = %d not enough for len = %d!\n",
			bio->bi_size, len);
		return -E2BIG;
	}
	return 0;
}

static void
dump_free_bio(struct bio *bio)
{
	if (bio)
		kfree(bio->bi_io_vec);
	kfree(bio);
}

/*
 * Prepares the dump device so we can take a dump later. 
 * The caller is expected to have filled up the dev_id field in the 
 * block dump dev structure.
 *
 * At dump time when dump_block_write() is invoked it will be too 
 * late to recover, so as far as possible make sure obvious errors 
 * get caught right here and reported back to the caller.
 */
static int
dump_block_open(struct dump_dev *dev, unsigned long arg)
{
	struct dump_blockdev *dump_bdev = DUMP_BDEV(dev);
	struct block_device *bdev;
	int retval = 0;
	struct bio_vec *bvec;

	/* make sure this is a valid block device */
	if (!arg) {
		retval = -EINVAL;
		goto err;
	}

	/* Convert it to the new dev_t format */
	arg = MKDEV((arg >> OLDMINORBITS), (arg & OLDMINORMASK));
	
	/* get a corresponding block_dev struct for this */
	bdev = bdget((dev_t)arg);
	if (!bdev) {
		retval = -ENODEV;
		goto err;
	}

	/* get the block device opened */
	if ((retval = blkdev_get(bdev, O_RDWR | O_LARGEFILE, 0))) {
		goto err1;
	}

	if ((dump_bdev->bio = kmalloc(sizeof(struct bio), GFP_KERNEL)) 
		== NULL) {
		printk("Cannot allocate bio\n");
		retval = -ENOMEM;
		goto err2;
	}

	bio_init(dump_bdev->bio);

	if ((bvec = kmalloc(sizeof(struct bio_vec) * 
		(DUMP_BUFFER_SIZE >> PAGE_SHIFT), GFP_KERNEL)) == NULL) {
		retval = -ENOMEM;
		goto err3;
	}

	/* assign the new dump dev structure */
	dump_bdev->dev_id = (dev_t)arg;
	dump_bdev->bdev = bdev;

	/* make a note of the limit */
	dump_bdev->limit = bdev->bd_inode->i_size;
	
	/* now make sure we can map the dump buffer */
	dump_bdev->bio->bi_io_vec = bvec;
	dump_bdev->bio->bi_max_vecs = DUMP_BUFFER_SIZE >> PAGE_SHIFT;

	retval = dump_block_map(dump_bdev, dump_config.dumper->dump_buf, 
		DUMP_BUFFER_SIZE);
		
	if (retval) {
		printk("open: dump_block_map failed, ret %d\n", retval);
		goto err3;
	}

	printk("Block device (%d,%d) successfully configured for dumping\n",
	       MAJOR(dump_bdev->dev_id),
	       MINOR(dump_bdev->dev_id));


	/* after opening the block device, return */
	return retval;

err3:	dump_free_bio(dump_bdev->bio);
	dump_bdev->bio = NULL;
err2:	if (bdev) blkdev_put(bdev);
		goto err;
err1:	if (bdev) bdput(bdev);
	dump_bdev->bdev = NULL;
err:	return retval;
}

/*
 * Close the dump device and release associated resources
 * Invoked when unconfiguring the dump device.
 */
static int
dump_block_release(struct dump_dev *dev)
{
	struct dump_blockdev *dump_bdev = DUMP_BDEV(dev);

	/* release earlier bdev if present */
	if (dump_bdev->bdev) {
		blkdev_put(dump_bdev->bdev);
		dump_bdev->bdev = NULL;
	}

	dump_free_bio(dump_bdev->bio);
	dump_bdev->bio = NULL;

	return 0;
}


/*
 * Prepare the dump device for use (silence any ongoing activity
 * and quiesce state) when the system crashes.
 */
static int
dump_block_silence(struct dump_dev *dev)
{
	struct dump_blockdev *dump_bdev = DUMP_BDEV(dev);
	struct request_queue *q = bdev_get_queue(dump_bdev->bdev);
	int ret;

	/* If we can't get request queue lock, refuse to take the dump */
	if (!spin_trylock(q->queue_lock))
		return -EBUSY;

	ret = elv_queue_empty(q);
	spin_unlock(q->queue_lock);

	/* For now we assume we have the device to ourselves */
	/* Just a quick sanity check */
	if (!ret) {
		/* Warn the user and move on */
		printk(KERN_ALERT "Warning: Non-empty request queue\n");
		printk(KERN_ALERT "I/O requests in flight at dump time\n");
	}

	/* 
	 * Move to a softer level of silencing where no spin_lock_irqs 
	 * are held on other cpus
	 */
	dump_silence_level = DUMP_SOFT_SPIN_CPUS;	

	ret = __dump_irq_enable();
	if (ret) {
		return ret;
	}

	printk("Dumping to block device (%d,%d) on CPU %d ...\n",
	       MAJOR(dump_bdev->dev_id), MINOR(dump_bdev->dev_id),
	       smp_processor_id());
	
	return 0;
}

/*
 * Invoked when dumping is done. This is the time to put things back 
 * (i.e. undo the effects of dump_block_silence) so the device is 
 * available for normal use.
 */
static int
dump_block_resume(struct dump_dev *dev)
{
	__dump_irq_restore();
	return 0;
}


/*
 * Seek to the specified offset in the dump device.
 * Makes sure this is a valid offset, otherwise returns an error.
 */
static int
dump_block_seek(struct dump_dev *dev, loff_t off)
{
	struct dump_blockdev *dump_bdev = DUMP_BDEV(dev);
	loff_t offset = off + dump_bdev->start_offset;
	
	if (offset & ( PAGE_SIZE - 1)) {
		printk("seek: non-page aligned\n");
		return -EINVAL;
	}

	if (offset & (bdev_hardsect_size(dump_bdev->bdev) - 1)) {
		printk("seek: not sector aligned \n");
		return -EINVAL;
	}

	if (offset > dump_bdev->limit) {
		printk("seek: not enough space left on device!\n");
		return -ENOSPC; 
	}
	dev->curr_offset = off;
	return 0;
}

/*
 * Write out a buffer after checking the device limitations, 
 * sector sizes, etc. Assumes the buffer is in directly mapped 
 * kernel address space (not vmalloc'ed).
 *
 * Returns: number of bytes written or -ERRNO. 
 */
static int
dump_block_write(struct dump_dev *dev, void *buf, 
	unsigned long len)
{
	struct dump_blockdev *dump_bdev = DUMP_BDEV(dev);
	loff_t offset = dev->curr_offset + dump_bdev->start_offset;
	int retval = -ENOSPC;

	if (offset >= dump_bdev->limit) {
		printk("write: not enough space left on device!\n");
		goto out;
	}

	/* don't write more blocks than our max limit */
	if (offset + len > dump_bdev->limit) 
		len = dump_bdev->limit - offset;


	retval = dump_block_map(dump_bdev, buf, len);
	if (retval){
		printk("write: dump_block_map failed! err %d\n", retval);
		goto out;
	}

	/*
	 * Write out the data to disk.
	 * Assumes the entire buffer mapped to a single bio, which we can
	 * submit and wait for io completion. In the future, may consider
	 * increasing the dump buffer size and submitting multiple bio s 
	 * for better throughput.
	 */
	dump_bdev->err = -EAGAIN;
	submit_bio(WRITE, dump_bdev->bio);

	dump_bdev->ddev.curr_offset += len;
	retval = len;
 out:
	return retval;
}

/*
 * Name: dump_block_ready()
 * Func: check if the last dump i/o is over and ready for next request
 */
static int
dump_block_ready(struct dump_dev *dev, void *buf)
{
	struct dump_blockdev *dump_bdev = DUMP_BDEV(dev);
	request_queue_t *q = bdev_get_queue(dump_bdev->bio->bi_bdev);

	/* check for io completion */
	if (dump_bdev->err == -EAGAIN) {
		q->unplug_fn(q);
		return -EAGAIN;
	}

	if (dump_bdev->err) {
		printk("dump i/o err\n");
		return dump_bdev->err;
	}

	return 0;
}


struct dump_dev_ops dump_blockdev_ops = {
	.open 		= dump_block_open,
	.release	= dump_block_release,
	.silence	= dump_block_silence,
	.resume 	= dump_block_resume,
	.seek		= dump_block_seek,
	.write		= dump_block_write,
	/* .read not implemented */
	.ready		= dump_block_ready
};

static struct dump_blockdev default_dump_blockdev = {
	.ddev = {.type_name = "blockdev", .ops = &dump_blockdev_ops, 
			.curr_offset = 0},
	/* 
	 * leave enough room for the longest swap header possibly written 
	 * written by mkswap (likely the largest page size supported by
	 * the arch
	 */
	.start_offset 	= DUMP_HEADER_OFFSET,
	.err 		= 0
	/* assume the rest of the fields are zeroed by default */
};	
	
struct dump_blockdev *dump_blockdev = &default_dump_blockdev;

static int __init
dump_blockdev_init(void)
{
	if (dump_register_device(&dump_blockdev->ddev) < 0) {
		printk("block device driver registration failed\n");
		return -1;
	}
		
	printk("block device driver for LKCD registered\n");
	return 0;
}

static void __exit
dump_blockdev_cleanup(void)
{
	dump_unregister_device(&dump_blockdev->ddev);
	printk("block device driver for LKCD unregistered\n");
}

MODULE_AUTHOR("LKCD Development Team <lkcd-devel@lists.sourceforge.net>");
MODULE_DESCRIPTION("Block Dump Driver for Linux Kernel Crash Dump (LKCD)");
MODULE_LICENSE("GPL");

module_init(dump_blockdev_init);
module_exit(dump_blockdev_cleanup);
