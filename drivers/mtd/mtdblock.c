/* 
 * Direct MTD block device access
 *
 * $Id: mtdblock.c,v 1.47 2001/10/02 15:05:11 dwmw2 Exp $
 *
 * 02-nov-2000	Nicolas Pitre		Added read-modify-write with cache
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/buffer_head.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/compatmac.h>
#include <linux/buffer_head.h>

#define MAJOR_NR MTD_BLOCK_MAJOR
#define DEVICE_NAME "mtdblock"
#define DEVICE_NR(device) (device)
#define LOCAL_END_REQUEST
#include <linux/blk.h>
#include <linux/devfs_fs_kernel.h>

static void mtd_notify_add(struct mtd_info* mtd);
static void mtd_notify_remove(struct mtd_info* mtd);
static struct mtd_notifier notifier = {
        mtd_notify_add,
        mtd_notify_remove,
        NULL
};

static struct mtdblk_dev {
	struct mtd_info *mtd; /* Locked */
	int count;
	struct semaphore cache_sem;
	unsigned char *cache_data;
	unsigned long cache_offset;
	unsigned int cache_size;
	enum { STATE_EMPTY, STATE_CLEAN, STATE_DIRTY } cache_state;
} *mtdblks[MAX_MTD_DEVICES];

static struct gendisk *mtddisk[MAX_MTD_DEVICES];

static spinlock_t mtdblks_lock;

/*
 * Cache stuff...
 * 
 * Since typical flash erasable sectors are much larger than what Linux's
 * buffer cache can handle, we must implement read-modify-write on flash
 * sectors for each block write requests.  To avoid over-erasing flash sectors
 * and to speed things up, we locally cache a whole flash sector while it is
 * being written to until a different sector is required.
 */

static void erase_callback(struct erase_info *done)
{
	wait_queue_head_t *wait_q = (wait_queue_head_t *)done->priv;
	wake_up(wait_q);
}

static int erase_write (struct mtd_info *mtd, unsigned long pos, 
			int len, const char *buf)
{
	struct erase_info erase;
	DECLARE_WAITQUEUE(wait, current);
	wait_queue_head_t wait_q;
	size_t retlen;
	int ret;

	/*
	 * First, let's erase the flash block.
	 */

	init_waitqueue_head(&wait_q);
	erase.mtd = mtd;
	erase.callback = erase_callback;
	erase.addr = pos;
	erase.len = len;
	erase.priv = (u_long)&wait_q;

	set_current_state(TASK_INTERRUPTIBLE);
	add_wait_queue(&wait_q, &wait);

	ret = MTD_ERASE(mtd, &erase);
	if (ret) {
		set_current_state(TASK_RUNNING);
		remove_wait_queue(&wait_q, &wait);
		printk (KERN_WARNING "mtdblock: erase of region [0x%lx, 0x%x] "
				     "on \"%s\" failed\n",
			pos, len, mtd->name);
		return ret;
	}

	schedule();  /* Wait for erase to finish. */
	remove_wait_queue(&wait_q, &wait);

	/*
	 * Next, writhe data to flash.
	 */

	ret = MTD_WRITE (mtd, pos, len, &retlen, buf);
	if (ret)
		return ret;
	if (retlen != len)
		return -EIO;
	return 0;
}


static int write_cached_data (struct mtdblk_dev *mtdblk)
{
	struct mtd_info *mtd = mtdblk->mtd;
	int ret;

	if (mtdblk->cache_state != STATE_DIRTY)
		return 0;

	DEBUG(MTD_DEBUG_LEVEL2, "mtdblock: writing cached data for \"%s\" "
			"at 0x%lx, size 0x%x\n", mtd->name, 
			mtdblk->cache_offset, mtdblk->cache_size);
	
	ret = erase_write (mtd, mtdblk->cache_offset, 
			   mtdblk->cache_size, mtdblk->cache_data);
	if (ret)
		return ret;

	/*
	 * Here we could argably set the cache state to STATE_CLEAN.
	 * However this could lead to inconsistency since we will not 
	 * be notified if this content is altered on the flash by other 
	 * means.  Let's declare it empty and leave buffering tasks to
	 * the buffer cache instead.
	 */
	mtdblk->cache_state = STATE_EMPTY;
	return 0;
}


static int do_cached_write (struct mtdblk_dev *mtdblk, unsigned long pos, 
			    int len, const char *buf)
{
	struct mtd_info *mtd = mtdblk->mtd;
	unsigned int sect_size = mtdblk->cache_size;
	size_t retlen;
	int ret;

	DEBUG(MTD_DEBUG_LEVEL2, "mtdblock: write on \"%s\" at 0x%lx, size 0x%x\n",
		mtd->name, pos, len);
	
	if (!sect_size)
		return MTD_WRITE (mtd, pos, len, &retlen, buf);

	while (len > 0) {
		unsigned long sect_start = (pos/sect_size)*sect_size;
		unsigned int offset = pos - sect_start;
		unsigned int size = sect_size - offset;
		if( size > len ) 
			size = len;

		if (size == sect_size) {
			/* 
			 * We are covering a whole sector.  Thus there is no
			 * need to bother with the cache while it may still be
			 * useful for other partial writes.
			 */
			ret = erase_write (mtd, pos, size, buf);
			if (ret)
				return ret;
		} else {
			/* Partial sector: need to use the cache */

			if (mtdblk->cache_state == STATE_DIRTY &&
			    mtdblk->cache_offset != sect_start) {
				ret = write_cached_data(mtdblk);
				if (ret) 
					return ret;
			}

			if (mtdblk->cache_state == STATE_EMPTY ||
			    mtdblk->cache_offset != sect_start) {
				/* fill the cache with the current sector */
				mtdblk->cache_state = STATE_EMPTY;
				ret = MTD_READ(mtd, sect_start, sect_size, &retlen, mtdblk->cache_data);
				if (ret)
					return ret;
				if (retlen != sect_size)
					return -EIO;

				mtdblk->cache_offset = sect_start;
				mtdblk->cache_size = sect_size;
				mtdblk->cache_state = STATE_CLEAN;
			}

			/* write data to our local cache */
			memcpy (mtdblk->cache_data + offset, buf, size);
			mtdblk->cache_state = STATE_DIRTY;
		}

		buf += size;
		pos += size;
		len -= size;
	}

	return 0;
}


static int do_cached_read (struct mtdblk_dev *mtdblk, unsigned long pos, 
			   int len, char *buf)
{
	struct mtd_info *mtd = mtdblk->mtd;
	unsigned int sect_size = mtdblk->cache_size;
	size_t retlen;
	int ret;

	DEBUG(MTD_DEBUG_LEVEL2, "mtdblock: read on \"%s\" at 0x%lx, size 0x%x\n", 
			mtd->name, pos, len);
	
	if (!sect_size)
		return MTD_READ (mtd, pos, len, &retlen, buf);

	while (len > 0) {
		unsigned long sect_start = (pos/sect_size)*sect_size;
		unsigned int offset = pos - sect_start;
		unsigned int size = sect_size - offset;
		if (size > len) 
			size = len;

		/*
		 * Check if the requested data is already cached
		 * Read the requested amount of data from our internal cache if it
		 * contains what we want, otherwise we read the data directly
		 * from flash.
		 */
		if (mtdblk->cache_state != STATE_EMPTY &&
		    mtdblk->cache_offset == sect_start) {
			memcpy (buf, mtdblk->cache_data + offset, size);
		} else {
			ret = MTD_READ (mtd, pos, size, &retlen, buf);
			if (ret)
				return ret;
			if (retlen != size)
				return -EIO;
		}

		buf += size;
		pos += size;
		len -= size;
	}

	return 0;
}

static struct block_device_operations mtd_fops;

static int mtdblock_open(struct inode *inode, struct file *file)
{
	struct mtdblk_dev *mtdblk;
	struct mtd_info *mtd;
	int dev = minor(inode->i_rdev);
	struct gendisk *disk;

	DEBUG(MTD_DEBUG_LEVEL1,"mtdblock_open\n");

	if (dev >= MAX_MTD_DEVICES)
		return -EINVAL;

	mtd = get_mtd_device(NULL, dev);
	if (!mtd)
		return -ENODEV;
	if (MTD_ABSENT == mtd->type) {
		put_mtd_device(mtd);
		return -ENODEV;
	}
	
	spin_lock(&mtdblks_lock);

	/* If it's already open, no need to piss about. */
	if (mtdblks[dev]) {
		mtdblks[dev]->count++;
		spin_unlock(&mtdblks_lock);
		return 0;
	}
	
	/* OK, it's not open. Try to find it */

	/* First we have to drop the lock, because we have to
	   to things which might sleep.
	*/
	spin_unlock(&mtdblks_lock);

	mtdblk = kmalloc(sizeof(struct mtdblk_dev), GFP_KERNEL);
	disk = mtddisk[dev];
	if (!mtdblk || !disk)
		goto Enomem;
	memset(mtdblk, 0, sizeof(*mtdblk));
	mtdblk->count = 1;
	mtdblk->mtd = mtd;

	init_MUTEX (&mtdblk->cache_sem);
	mtdblk->cache_state = STATE_EMPTY;
	if ((mtdblk->mtd->flags & MTD_CAP_RAM) != MTD_CAP_RAM &&
	    mtdblk->mtd->erasesize) {
		mtdblk->cache_size = mtdblk->mtd->erasesize;
		mtdblk->cache_data = vmalloc(mtdblk->mtd->erasesize);
		if (!mtdblk->cache_data)
			goto Enomem;
	}

	/* OK, we've created a new one. Add it to the list. */

	spin_lock(&mtdblks_lock);

	if (mtdblks[dev]) {
		/* Another CPU made one at the same time as us. */
		mtdblks[dev]->count++;
		spin_unlock(&mtdblks_lock);
		put_mtd_device(mtdblk->mtd);
		vfree(mtdblk->cache_data);
		kfree(mtdblk);
		return 0;
	}

	mtdblks[dev] = mtdblk;
	set_device_ro(inode->i_bdev, !(mtdblk->mtd->flags & MTD_WRITEABLE));

	spin_unlock(&mtdblks_lock);

	DEBUG(MTD_DEBUG_LEVEL1, "ok\n");

	return 0;
Enomem:
	put_mtd_device(mtd);
	kfree(mtdblk);
	return -ENOMEM;
}

static release_t mtdblock_release(struct inode *inode, struct file *file)
{
	int dev;
	struct mtdblk_dev *mtdblk;
   	DEBUG(MTD_DEBUG_LEVEL1, "mtdblock_release\n");

	if (inode == NULL)
		release_return(-ENODEV);

	dev = minor(inode->i_rdev);
	mtdblk = mtdblks[dev];

	down(&mtdblk->cache_sem);
	write_cached_data(mtdblk);
	up(&mtdblk->cache_sem);

	spin_lock(&mtdblks_lock);
	if (!--mtdblk->count) {
		/* It was the last usage. Free the device */
		mtdblks[dev] = NULL;
		spin_unlock(&mtdblks_lock);
		if (mtdblk->mtd->sync)
			mtdblk->mtd->sync(mtdblk->mtd);
		put_mtd_device(mtdblk->mtd);
		vfree(mtdblk->cache_data);
		kfree(mtdblk);
	} else {
		spin_unlock(&mtdblks_lock);
	}

	DEBUG(MTD_DEBUG_LEVEL1, "ok\n");

	release_return(0);
}  


/* 
 * This is a special request_fn because it is executed in a process context 
 * to be able to sleep independently of the caller.  The queue_lock 
 * is held upon entry and exit.
 * The head of our request queue is considered active so there is no need 
 * to dequeue requests before we are done.
 */
static struct request_queue mtd_queue;
static void handle_mtdblock_request(void)
{
	struct request *req;
	struct mtdblk_dev *mtdblk;
	unsigned int res;

	while ((req = elv_next_request(&mtd_queue)) != NULL) {
		struct mtdblk_dev **p = req->rq_disk->private_data;
		spin_unlock_irq(mtd_queue.queue_lock);
		mtdblk = *p;
		res = 0;

		if (! (req->flags & REQ_CMD))
			goto end_req;

		if ((req->sector + req->current_nr_sectors) > (mtdblk->mtd->size >> 9))
			goto end_req;

		// Handle the request
		switch (rq_data_dir(req))
		{
			int err;

			case READ:
			down(&mtdblk->cache_sem);
			err = do_cached_read (mtdblk, req->sector << 9, 
					req->current_nr_sectors << 9,
					req->buffer);
			up(&mtdblk->cache_sem);
			if (!err)
				res = 1;
			break;

			case WRITE:
			// Read only device
			if ( !(mtdblk->mtd->flags & MTD_WRITEABLE) ) 
				break;

			// Do the write
			down(&mtdblk->cache_sem);
			err = do_cached_write (mtdblk, req->sector << 9,
					req->current_nr_sectors << 9, 
					req->buffer);
			up(&mtdblk->cache_sem);
			if (!err)
				res = 1;
			break;
		}

end_req:
		spin_lock_irq(mtd_queue.queue_lock);
		if (!end_that_request_first(req, res, req->hard_cur_sectors)) {
			blkdev_dequeue_request(req);
			end_that_request_last(req);
		}

	}
}

static volatile int leaving = 0;
static DECLARE_MUTEX_LOCKED(thread_sem);
static DECLARE_WAIT_QUEUE_HEAD(thr_wq);

int mtdblock_thread(void *dummy)
{
	struct task_struct *tsk = current;
	DECLARE_WAITQUEUE(wait, tsk);

	/* we might get involved when memory gets low, so use PF_MEMALLOC */
	tsk->flags |= PF_MEMALLOC;
	daemonize("mtdblockd");

	while (!leaving) {
		add_wait_queue(&thr_wq, &wait);
		set_current_state(TASK_INTERRUPTIBLE);
		spin_lock_irq(mtd_queue.queue_lock);
		if (!elv_next_request(&mtd_queue) || blk_queue_plugged(&mtd_queue)) {
			spin_unlock_irq(mtd_queue.queue_lock);
			schedule();
			remove_wait_queue(&thr_wq, &wait); 
		} else {
			remove_wait_queue(&thr_wq, &wait); 
			set_current_state(TASK_RUNNING);
			handle_mtdblock_request();
			spin_unlock_irq(mtd_queue.queue_lock);
		}
	}

	up(&thread_sem);
	return 0;
}

static void mtdblock_request(struct request_queue *q)
{
	/* Don't do anything, except wake the thread if necessary */
	wake_up(&thr_wq);
}


static int mtdblock_ioctl(struct inode * inode, struct file * file,
		      unsigned int cmd, unsigned long arg)
{
	struct mtdblk_dev *mtdblk;

	mtdblk = mtdblks[minor(inode->i_rdev)];

#ifdef PARANOIA
	if (!mtdblk)
		BUG();
#endif

	switch (cmd) {
	case BLKFLSBUF:
		fsync_bdev(inode->i_bdev);
		invalidate_bdev(inode->i_bdev, 0);
		down(&mtdblk->cache_sem);
		write_cached_data(mtdblk);
		up(&mtdblk->cache_sem);
		if (mtdblk->mtd->sync)
			mtdblk->mtd->sync(mtdblk->mtd);
		return 0;

	default:
		return -EINVAL;
	}
}

static struct block_device_operations mtd_fops = 
{
	.owner		= THIS_MODULE,
	.open		= mtdblock_open,
	.release	= mtdblock_release,
	.ioctl		= mtdblock_ioctl
};

/* Notification that a new device has been added. Create the devfs entry for
 * it. */

static void mtd_notify_add(struct mtd_info* mtd)
{
	struct gendisk *disk;
        char name[16];

        if (!mtd || mtd->type == MTD_ABSENT)
                return;

#ifdef CONFIG_DEVFS_FS
        sprintf(name, DEVICE_NAME"/%d", mtd->index);
        devfs_register(NULL, name, DEVFS_FL_DEFAULT,
			MTD_BLOCK_MAJOR, mtd->index,
                        S_IFBLK | S_IRUGO | S_IWUGO,
                        &mtd_fops, NULL);
#endif

	disk = alloc_disk(1);
	if (disk) {
		disk->major = MAJOR_NR;
		disk->first_minor = mtd->index;
		disk->fops = &mtd_fops;
		sprintf(disk->disk_name, "mtdblock%d", mtd->index);

		mtddisk[mtd->index] = disk;
		set_capacity(disk, mtd->size / 512);
		disk->private_data = &mtdblks[mtd->index];
		disk->queue = &mtd_queue;
		add_disk(disk);
	}
}

static void mtd_notify_remove(struct mtd_info* mtd)
{
        if (!mtd || mtd->type == MTD_ABSENT)
                return;

        devfs_remove(DEVICE_NAME"/%d", mtd->index);

        if (mtddisk[mtd->index]) {
		del_gendisk(mtddisk[mtd->index]);
        	put_disk(mtddisk[mtd->index]);
        	mtddisk[mtd->index] = NULL;
        }
}

static spinlock_t mtddev_lock = SPIN_LOCK_UNLOCKED;

int __init init_mtdblock(void)
{
	spin_lock_init(&mtdblks_lock);

	if (register_blkdev(MAJOR_NR, DEVICE_NAME))
		return -EAGAIN;

#ifdef CONFIG_DEVFS_FS
	devfs_mk_dir(DEVICE_NAME);
#endif
	register_mtd_user(&notifier);
	
	init_waitqueue_head(&thr_wq);
	blk_init_queue(&mtd_queue, &mtdblock_request, &mtddev_lock);
	kernel_thread (mtdblock_thread, NULL, CLONE_FS|CLONE_FILES|CLONE_SIGHAND);
	return 0;
}

static void __exit cleanup_mtdblock(void)
{
	leaving = 1;
	wake_up(&thr_wq);
	down(&thread_sem);
	unregister_mtd_user(&notifier);
#ifdef CONFIG_DEVFS_FS
	devfs_remove(DEVICE_NAME);
#endif
	unregister_blkdev(MAJOR_NR,DEVICE_NAME);
	blk_cleanup_queue(&mtd_queue);
}

module_init(init_mtdblock);
module_exit(cleanup_mtdblock);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nicolas Pitre <nico@cam.org> et al.");
MODULE_DESCRIPTION("Caching read/erase/writeback block device emulation access to MTD devices");
