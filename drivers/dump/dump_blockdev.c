/*
 * Implements the dump driver interface for saving a dump to
 * a block device through the kernel's generic low level block I/O
 * routines, or through polling I/O.
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
 * Oct 2004 - Mar 2005 - Mohamed Abbas <mohamed.abbas@intel.com>
 *                       Jason Uhlenkott <jasonuhl@sgi.com>
 *	Implement polling I/O (adapted from lkdump, with thanks
 *	to Nobuhiro Tachino).
 *
 * Copyright (C) 1999 - 2005 Silicon Graphics, Inc. All rights reserved.
 * Copyright (C) 2001 - 2002 Matt D. Robinson.  All rights reserved.
 * Copyright (C) 2002 International Business Machines Corp.
 * Copyright (C) 2004 FUJITSU LIMITED
 *
 * This code is released under version 2 of the GNU GPL.
 */

#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/bio.h>
#include <linux/diskdump.h>
#include <linux/dump.h>
#include <linux/delay.h>
#include <asm/dump.h>
#include <asm/hardirq.h>
#include "dump_methods.h"


/* ----- Support functions for interrupt-driven dumps ----- */


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
		printk("LKCD: IO error while writing the dump, aborting\n");
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
		bio->bi_vcnt = (len + PAGE_SIZE - 1) / PAGE_SIZE; /* Set the proper vector cnt */
	} else {
		/* need to map the bio */
		bio->bi_size = 0;
		bio->bi_vcnt = 0;
		bsize = bdev_hardsect_size(bio->bi_bdev);

		/* first a few sanity checks */
		if (len < bsize) {
			printk("LKCD: map: len less than hardsect size \n");
			return -EINVAL;
		}

		if ((unsigned long)buf & bsize) {
			printk("LKCD: map: not aligned\n");
			return -EINVAL;
		}

		/* assume contig. page aligned low mem buffer( no vmalloc) */
		if ((page_address(page) != buf) || (len & (PAGE_SIZE - 1))) {
			printk("LKCD: map: invalid buffer alignment!\n");
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
		printk("LKCD: map: bio size = %d not enough for len = %d!\n",
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


/* ----- Support functions for polling I/O based dumps ----- */

static DECLARE_MUTEX(disk_dump_mutex);
static LIST_HEAD(disk_dump_types);
static struct disk_dump_device dump_device;
static struct disk_dump_partition dump_part;
static unsigned long long timestamp_base;
static unsigned long timestamp_hz;
static unsigned long flags_global;
static int polling_mode;
static void dump_blockdev_unconfigure(void);

void diskdump_setup_timestamp(void)
{
        unsigned long long t=0;

        platform_timestamp(timestamp_base);
        udelay(1000000/HZ);
        platform_timestamp(t);
        timestamp_hz = (unsigned long)(t - timestamp_base);
        diskdump_update();
}

void diskdump_update(void)
{
        unsigned long long t=0;

        touch_nmi_watchdog();

        /* update jiffies */
        platform_timestamp(t);
        while (t > timestamp_base + timestamp_hz) {
                timestamp_base += timestamp_hz;
                jiffies++;
                platform_timestamp(t);
        }

        dump_run_timers();
        dump_run_tasklet();
        dump_run_workqueue();
}
EXPORT_SYMBOL_GPL(diskdump_update);

static void *find_real_device(struct device *dev,
                              struct disk_dump_type **_dump_type)
{
        void *real_device;
        struct disk_dump_type *dump_type;

        list_for_each_entry(dump_type, &disk_dump_types, list) {
		real_device = dump_type->probe(dev);
		if (real_device) {
                        *_dump_type = dump_type;
                        return real_device;
                }
	}
	return NULL;
}

static int register_disk_dump_device(struct device *dev, struct block_device *bdev)
{
        struct disk_dump_type *dump_type = NULL;
        void *real_device;
        int ret = 0;

	if (!bdev->bd_part)
		return -EINVAL;

	down(&disk_dump_mutex);

	real_device = find_real_device(dev, &dump_type);
	if (!real_device) {
		ret = -ENXIO;
		goto err;
        }

	if (dump_device.device == real_device) {
		ret = -EEXIST;
		goto err;
	} else if (dump_device.device) {
		BUG();
	}

	dump_device.device = real_device;

	ret = dump_type->add_device(&dump_device);
	if (ret < 0) {
		dump_device.device = NULL;
		goto err;
	}

	dump_device.dump_type = dump_type;
	dump_part.device = &dump_device;
	dump_part.bdev = bdev;
	dump_part.nr_sects   = bdev->bd_part->nr_sects;
	dump_part.start_sect = bdev->bd_part->start_sect;

err:
	up(&disk_dump_mutex);
	return ret;
}

static void unregister_disk_dump_device(struct block_device *bdev)
{
	struct disk_dump_type *dump_type;

	down(&disk_dump_mutex);

	if(!dump_part.device) {
		up(&disk_dump_mutex);
		return;
	}

	BUG_ON(dump_part.device != &dump_device);
	BUG_ON(dump_part.bdev != bdev);

	dump_part.device = NULL;
	dump_part.bdev = NULL;

	dump_type = dump_device.dump_type;
	dump_type->remove_device(&dump_device);
	dump_device.device = NULL;
	dump_device.dump_type = NULL;

	up(&disk_dump_mutex);
}

int register_disk_dump_type(struct disk_dump_type *dump_type)
{
	down(&disk_dump_mutex);
	list_add(&dump_type->list, &disk_dump_types);
	up(&disk_dump_mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(register_disk_dump_type);

int unregister_disk_dump_type(struct disk_dump_type *dump_type)
{
	lock_kernel(); /* guard against the dump ioctl */

	if (dump_device.dump_type == dump_type)
		dump_blockdev_unconfigure();

	down(&disk_dump_mutex);
	list_del(&dump_type->list);
	up(&disk_dump_mutex);

	unlock_kernel();
	return 0;
}
EXPORT_SYMBOL_GPL(unregister_disk_dump_type);


/* --------------------------------------------------- */


static int
dump_block_intr_open(struct dump_dev *dev, unsigned long arg)
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
		goto err;
	}

	if ((dump_bdev->bio = kmalloc(sizeof(struct bio), GFP_KERNEL))
		== NULL) {
		printk("LKCD: Cannot allocate bio\n");
		retval = -ENOMEM;
		goto err1;
	}

	bio_init(dump_bdev->bio);

	if ((bvec = kmalloc(sizeof(struct bio_vec) *
		(DUMP_BUFFER_SIZE >> PAGE_SHIFT), GFP_KERNEL)) == NULL) {
		retval = -ENOMEM;
		goto err2;
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
		printk("LKCD: open: dump_block_map failed, ret %d\n", retval);
		goto err2;
	}

	printk("LKCD: Block device (%d,%d) successfully configured for dumping\n",
	       MAJOR(dump_bdev->dev_id),
	       MINOR(dump_bdev->dev_id));


	/* after opening the block device, return */
	return retval;

err2:	dump_free_bio(dump_bdev->bio);
	dump_bdev->bio = NULL;
err1:	if (bdev)
		blkdev_put(bdev);
	dump_bdev->bdev = NULL;
err:	return retval;
}

static int
dump_block_poll_open(struct dump_dev *dev, unsigned long arg)
{
	struct dump_blockdev *dump_bdev = DUMP_BDEV(dev);
	struct block_device *bdev;
	int retval = 0;
        struct device *target = NULL;

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
		goto err;
	}

	dump_bdev->bio = 0;
	dump_bdev->dev_id = (dev_t)arg;
	dump_bdev->bdev = bdev;

	/* make a note of the limit */
	dump_bdev->limit = bdev->bd_inode->i_size;

	target = get_device(bdev->bd_disk->driverfs_dev);
	if (!target) {
		retval = -EINVAL;
		goto err1;
	}
	retval = register_disk_dump_device(target,bdev);
	if (retval == -EEXIST)
		retval = 0;
	else if (retval < 0)
		goto err1;

        printk("LKCD: Block device (%d,%d) successfully configured for dumping using polling I/O\n",
		MAJOR((dev_t)arg), MINOR((dev_t)arg));

	/* after opening the block device, return */
	return retval;

err1:	if (bdev)
		blkdev_put(bdev);
err:	return retval;
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
dump_block_open(struct dump_dev *dev, const char *arg)
{
	unsigned long devid;

	if ((sscanf(arg, "%lx", &devid)) != 1)
		return -EINVAL;

	if (devid < 0)
		return -EINVAL;

	if (dump_config.polling){
		polling_mode = 1;
		if (!dump_block_poll_open(dev, devid)) {
			return 0;
		} else {
			/*
			 * If polling I/O isn't supported by this
			 * device, fall back to interrupt-driven mode.
			 */
			dump_config.polling = 0;
		}
	}

	polling_mode = 0;
	return dump_block_intr_open(dev, devid);
}

/*
 * Close the dump device and release associated resources.
 * Invoked when unconfiguring the dump device.
 */
static int
dump_block_release(struct dump_dev *dev)
{
	struct dump_blockdev *dump_bdev = DUMP_BDEV(dev);

	/* release earlier bdev if present */
	if (dump_bdev->bdev) {
		unregister_disk_dump_device(dump_bdev->bdev);
		blkdev_put(dump_bdev->bdev);
		dump_bdev->bdev = NULL;
	}

	if (dump_bdev->bio) {
		dump_free_bio(dump_bdev->bio);
		dump_bdev->bio = NULL;
	}

	return 0;
}

static int
dump_block_intr_silence(struct dump_dev *dev)
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
		printk("LKCD: Warning: Non-empty request queue\n");
		printk("LKCD: I/O requests in flight at dump time\n");
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

	printk("LKCD: Dumping to block device (%d,%d) on CPU %d ...\n",
	       MAJOR(dump_bdev->dev_id), MINOR(dump_bdev->dev_id),
	       smp_processor_id());

	return 0;
}

static int
dump_block_poll_silence(struct dump_dev *dev)
{
	struct dump_blockdev *dump_bdev = DUMP_BDEV(dev);
	int ret;

	local_irq_save(flags_global);
	preempt_disable();

	touch_nmi_watchdog();

	if (down_trylock(&disk_dump_mutex))
		return -EBUSY;

	dump_polling_oncpu = smp_processor_id() + 1;

	/*
	 * Setup timer/tasklet
	 */
	dump_clear_timers();
	dump_clear_tasklet();
	dump_clear_workqueue();

	diskdump_setup_timestamp();

	BUG_ON(dump_part.bdev != dump_bdev->bdev);

	/*
	 * Move to a softer level of silencing where no spin_lock_irqs
	 * are held on other cpus
	 */
	dump_silence_level = DUMP_SOFT_SPIN_CPUS;

	touch_nmi_watchdog();

	if (dump_device.ops.quiesce)
		if ((ret = dump_device.ops.quiesce(&dump_device)) < 0) {
			printk("LKCD: Quiesce failed. error %d\n", ret);
			return ret;
		}
	touch_nmi_watchdog();
	printk("LKCD: Dumping to block device (%d,%d) on CPU %d using polling I/O ...\n",
	       MAJOR(dump_bdev->dev_id), MINOR(dump_bdev->dev_id),
	       smp_processor_id());

	return 0;
}

/*
 * Prepare the dump device for use (silence any ongoing activity
 * and quiesce state) when the system crashes.
 */
static int
dump_block_silence(struct dump_dev *dev)
{
	if (polling_mode)
		return dump_block_poll_silence(dev);
	else
		return dump_block_intr_silence(dev);
}


static int
dump_block_intr_resume(struct dump_dev *dev)
{
	__dump_irq_restore();
	return 0;
}

static int
dump_block_poll_resume(struct dump_dev *dev)
{
	struct dump_blockdev *dump_bdev = DUMP_BDEV(dev);

	BUG_ON(dump_part.bdev != dump_bdev->bdev);

	if (dump_device.device && dump_device.ops.shutdown)
		if (dump_device.ops.shutdown(&dump_device))
			printk("LKCD: polling dev: adapter shutdown failed.\n");

	dump_polling_oncpu = 0;
	preempt_enable_no_resched();
	local_irq_restore(flags_global);
	up(&disk_dump_mutex);
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
	if (polling_mode)
		return dump_block_poll_resume(dev);
	else
		return dump_block_intr_resume(dev);
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
		printk("LKCD: seek: non-page aligned\n");
		return -EINVAL;
	}

	if (offset & (bdev_hardsect_size(dump_bdev->bdev) - 1)) {
		printk("LKCD: seek: not sector aligned \n");
		return -EINVAL;
	}

	if (offset > dump_bdev->limit) {
		printk("LKCD: seek: not enough space left on device!\n");
		return -ENOSPC;
	}
	dev->curr_offset = off;
	return 0;
}


static int
dump_block_intr_write(struct dump_dev *dev, void *buf,
	unsigned long len)
{
	struct dump_blockdev *dump_bdev = DUMP_BDEV(dev);
	loff_t offset = dev->curr_offset + dump_bdev->start_offset;
	int retval = -ENOSPC;

	if (offset >= dump_bdev->limit) {
		printk("LKCD: write: not enough space left on device!\n");
		goto out;
	}

	/* don't write more blocks than our max limit */
	if (offset + len > dump_bdev->limit)
		len = dump_bdev->limit - offset;


	retval = dump_block_map(dump_bdev, buf, len);
	if (retval){
		printk("LKCD: write: dump_block_map failed! err %d\n", retval);
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

static int
dump_block_poll_write(struct dump_dev *dev, void *buf,
	unsigned long len)
{
	struct dump_blockdev *dump_bdev = DUMP_BDEV(dev);
	loff_t offset = dev->curr_offset + dump_bdev->start_offset;
	int retval = -ENOSPC;
	int ret;

	if (offset >= dump_bdev->limit) {
		printk("LKCD: write: not enough space left on device!\n");
		goto out;
	}

	/* don't write more blocks than our max limit */
	if (offset + len > dump_bdev->limit)
		len = dump_bdev->limit - offset;

	if (dump_part.bdev != dump_bdev->bdev) {
		return -EBUSY;
		goto out;
	}

	local_irq_disable();
	touch_nmi_watchdog();
	ret = dump_device.ops.rw_block(&dump_part, WRITE, offset >> DUMP_PAGE_SHIFT,
		buf, len >> DUMP_PAGE_SHIFT);
	if (ret < 0) {
		printk("LKCD: write error\n");
		goto out;
	}

	retval = len;
out:
	return retval;
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
	if (polling_mode)
		return dump_block_poll_write(dev, buf, len);
	else
		return dump_block_intr_write(dev, buf, len);
}


/*
 * Name: dump_block_ready()
 * Func: check if the last dump i/o is over and ready for next request
 */
static int
dump_block_ready(struct dump_dev *dev, void *buf)
{
	struct dump_blockdev *dump_bdev;
	request_queue_t *q;

	if (polling_mode)
		return 0;

	dump_bdev = DUMP_BDEV(dev);
	q = bdev_get_queue(dump_bdev->bio->bi_bdev);

	/* check for io completion */
	if (dump_bdev->err == -EAGAIN) {
		q->unplug_fn(q);
		return -EAGAIN;
	}

	if (dump_bdev->err) {
		printk("LKCD: dump i/o err\n");
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
	.ddev = {.type = 1, .ops = &dump_blockdev_ops,
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

/*
 * Unregister and reregister ourselves.  This has the side effect
 * of unconfiguring the current dump device.
 */
static void
dump_blockdev_unconfigure(void)
{
	dump_unregister_device(&dump_blockdev->ddev);
	if (dump_register_device(&dump_blockdev->ddev) < 0)
		printk("LKCD: block device driver registration failed\n");
}

static int __init
dump_blockdev_init(void)
{
	if (dump_register_device(&dump_blockdev->ddev) < 0) {
		printk("LKCD: block device driver registration failed\n");
		return -1;
	}

	printk("LKCD: block device driver registered\n");
	return 0;
}

static void __exit
dump_blockdev_cleanup(void)
{
	dump_unregister_device(&dump_blockdev->ddev);
	printk("LKCD: block device driver unregistered\n");
}

MODULE_AUTHOR("LKCD Development Team <lkcd-devel@lists.sourceforge.net>");
MODULE_DESCRIPTION("Block Dump Driver for Linux Kernel Crash Dump (LKCD)");
MODULE_LICENSE("GPL");

module_init(dump_blockdev_init);
module_exit(dump_blockdev_cleanup);
