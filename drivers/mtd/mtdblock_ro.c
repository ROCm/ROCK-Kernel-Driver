/*
 * $Id: mtdblock_ro.c,v 1.13 2002/03/11 16:03:29 sioux Exp $
 *
 * Read-only flash, read-write RAM version of the mtdblock device,
 * without caching.
 */

#ifdef MTDBLOCK_DEBUG
#define DEBUGLVL debug
#endif							       


#include <linux/module.h>
#include <linux/types.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/compatmac.h>
#include <linux/buffer_head.h>
#include <linux/genhd.h>

#define MAJOR_NR MTD_BLOCK_MAJOR
#define DEVICE_NAME "mtdblock"
#include <linux/blk.h>

#ifdef MTDBLOCK_DEBUG
static int debug = MTDBLOCK_DEBUG;
MODULE_PARM(debug, "i");
#endif

struct mtdro_dev {
	struct gendisk *disk;
	struct mtd_info *mtd;
	int open;
};

static struct mtdro_dev mtd_dev[MAX_MTD_DEVICES];
static DECLARE_MUTEX(mtd_sem);

static struct request_queue mtdro_queue;
static spinlock_t mtdro_lock = SPIN_LOCK_UNLOCKED;

static int mtdblock_open(struct inode *inode, struct file *file)
{
	struct mtdro_dev *mdev = inode->i_bdev->bd_disk->private_data;
	int ret = 0;

	DEBUG(1,"mtdblock_open\n");

	down(&mtd_sem);
	if (mdev->mtd == NULL) {
		mdev->mtd = get_mtd_device(NULL, minor(inode->i_rdev));
		if (!mdev->mtd || mdev->mtd->type == MTD_ABSENT) {
			if (mdev->mtd)
				put_mtd_device(mdev->mtd);
			ret = -ENODEV;
		}
	}

	if (ret == 0) {
		set_device_ro(inode->i_bdev, !(mdev->mtd->flags & MTD_CAP_RAM));
		mdev->open++;
	}
	up(&mtd_sem);

	DEBUG(1, "%s\n", ret ? "ok" : "nodev");

	return ret;
}

static release_t mtdblock_release(struct inode *inode, struct file *file)
{
	struct mtdro_dev *mdev = inode->i_bdev->bd_disk->private_data;

   	DEBUG(1, "mtdblock_release\n");

   	down(&mtd_sem);
   	if (mdev->open-- == 0) {
		struct mtd_info *mtd = mdev->mtd;

		mdev->mtd = NULL;
		if (mtd->sync)
			mtd->sync(mtd);

		put_mtd_device(mtd);
	}
	up(&mtd_sem);

	DEBUG(1, "ok\n");

	release_return(0);
}

static void mtdblock_request(request_queue_t *q)
{
	struct request *req;
	
	while ((req = elv_next_request(q)) != NULL) {
		struct mtdro_dev *mdev = req->rq_disk->private_data;
		struct mtd_info *mtd = mdev->mtd;
		unsigned int res;

		if (!(req->flags & REQ_CMD)) {
			res = 0;
			goto end_req;
		}

		if ((req->sector + req->current_nr_sectors) > (mtd->size >> 9)) {
			printk("mtd: Attempt to read past end of device!\n");
			printk("size: %x, sector: %lx, nr_sectors: %x\n",
				mtd->size, req->sector, req->current_nr_sectors);
			res = 0;
			goto end_req;
		}
      
		/* Now drop the lock that the ll_rw_blk functions grabbed for
		   us and process the request. This is necessary due to the
		   extreme time we spend processing it. */
		spin_unlock_irq(q->queue_lock);

		/* Handle the request */
		switch (rq_data_dir(req)) {
			size_t retlen;

			case READ:
				if (MTD_READ(mtd, req->sector << 9,
					     req->current_nr_sectors << 9, 
					     &retlen, req->buffer) == 0)
					res = 1;
				else
					res = 0;
				break;

			case WRITE:
				/* printk("mtdblock_request WRITE sector=%d(%d)\n",
					req->sector, req->current_nr_sectors);
				 */

				/* Read only device */
				if ((mtd->flags & MTD_CAP_RAM) == 0) {
					res = 0;
					break;
				}

				/* Do the write */
				if (MTD_WRITE(mtd, req->sector << 9, 
					      req->current_nr_sectors << 9, 
					      &retlen, req->buffer) == 0)
					res = 1;
				else
					res = 0;
				break;

			/* Shouldn't happen */
			default:
				printk("mtd: unknown request\n");
				res = 0;
				break;
		}

		/* Grab the lock and re-thread the item onto the linked list */
		spin_lock_irq(q->queue_lock);
 end_req:
		if (!end_that_request_first(req, res, req->hard_cur_sectors)) {
			blkdev_dequeue_request(req);
			end_that_request_last(req);
		}
	}
}

static int mtdblock_ioctl(struct inode * inode, struct file * file,
		      unsigned int cmd, unsigned long arg)
{
	struct mtdro_dev *mdev = inode->i_bdev->bd_disk->private_data;

	if (cmd != BLKFLSBUF)
		return -EINVAL;

	fsync_bdev(inode->i_bdev);
	invalidate_bdev(inode->i_bdev, 0);
	if (mdev->mtd->sync)
		mdev->mtd->sync(mdev->mtd);

	return 0;
}

static struct block_device_operations mtd_fops = {
	.owner		= THIS_MODULE,
	.open		= mtdblock_open,
	.release	= mtdblock_release,
	.ioctl		= mtdblock_ioctl
};

/* Called with mtd_table_mutex held. */
static void mtd_notify_add(struct mtd_info* mtd)
{
	struct gendisk *disk;

        if (!mtd || mtd->type == MTD_ABSENT || mtd->index >= MAX_MTD_DEVICES)
                return;

	disk = alloc_disk(1);
	if (disk) {
		disk->major = MAJOR_NR;
		disk->first_minor = mtd->index;
		disk->fops = &mtd_fops;
		sprintf(disk->disk_name, "mtdblock%d", mtd->index);

		mtd_dev[mtd->index].disk = disk;
		set_capacity(disk, mtd->size / 512);
		disk->queue = &mtdro_queue;
		disk->private_data = &mtd_dev[mtd->index];
		add_disk(disk);
	}
}

/* Called with mtd_table_mutex held. */
static void mtd_notify_remove(struct mtd_info* mtd)
{
	struct mtdro_dev *mdev;
	struct gendisk *disk;

        if (!mtd || mtd->type == MTD_ABSENT || mtd->index >= MAX_MTD_DEVICES)
                return;

	mdev = &mtd_dev[mtd->index];

	disk = mdev->disk;
	mdev->disk = NULL;

	if (disk) {
		del_gendisk(disk);
        	put_disk(disk);
        }
}

static struct mtd_notifier notifier = {
	.add	= mtd_notify_add,
        .remove	= mtd_notify_remove,
};

int __init init_mtdblock(void)
{
	if (register_blkdev(MAJOR_NR, DEVICE_NAME))
		return -EAGAIN;

	blk_init_queue(&mtdro_queue, &mtdblock_request, &mtdro_lock);
	register_mtd_user(&notifier);

	return 0;
}

static void __exit cleanup_mtdblock(void)
{
	unregister_mtd_user(&notifier);
	unregister_blkdev(MAJOR_NR,DEVICE_NAME);
	blk_cleanup_queue(&mtdro_queue);
}

module_init(init_mtdblock);
module_exit(cleanup_mtdblock);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Erwin Authried <eauth@softsys.co.at> et al.");
MODULE_DESCRIPTION("Simple uncached block device emulation access to MTD devices");
