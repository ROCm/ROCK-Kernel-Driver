/*
 * $Id: mtdblock_ro.c,v 1.9 2001/10/02 15:05:11 dwmw2 Exp $
 *
 * Read-only version of the mtdblock device, without the 
 * read/erase/modify/writeback stuff
 */

#ifdef MTDBLOCK_DEBUG
#define DEBUGLVL debug
#endif							       


#include <linux/module.h>
#include <linux/types.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/compatmac.h>

#define LOCAL_END_REQUEST
#define MAJOR_NR MTD_BLOCK_MAJOR
#define DEVICE_NAME "mtdblock"
#define DEVICE_NR(device) (device)
#include <linux/blk.h>

#define RQFUNC_ARG request_queue_t *q

#ifdef MTDBLOCK_DEBUG
static int debug = MTDBLOCK_DEBUG;
MODULE_PARM(debug, "i");
#endif

static struct gendisk *mtd_disks[MAX_MTD_DEVICES];

static int mtdblock_open(struct inode *inode, struct file *file)
{
	struct mtd_info *mtd = NULL;
	int dev = minor(inode->i_rdev);
	struct gendisk *disk = mtd_disks[dev];

	DEBUG(1,"mtdblock_open\n");

	mtd = get_mtd_device(NULL, dev);
	if (!mtd)
		return -EINVAL;
	if (MTD_ABSENT == mtd->type) {
		put_mtd_device(mtd);
		return -EINVAL;
	}

	set_capacit(disk, mtd->size>>9);
	add_disk(disk);

	DEBUG(1, "ok\n");

	return 0;
}

static release_t mtdblock_release(struct inode *inode, struct file *file)
{
	int dev;
	struct mtd_info *mtd;

   	DEBUG(1, "mtdblock_release\n");

	if (inode == NULL)
		release_return(-ENODEV);
   
	dev = minor(inode->i_rdev);
	mtd = __get_mtd_device(NULL, dev);

	if (!mtd) {
		printk(KERN_WARNING "MTD device is absent on mtd_release!\n");
		release_return(-ENODEV);
	}

	del_gendisk(mtd_disks[dev]);
	
	if (mtd->sync)
		mtd->sync(mtd);

	put_mtd_device(mtd);

	DEBUG(1, "ok\n");

	release_return(0);
}

static inline void mtdblock_end_request(struct request *req, int uptodate)
{
	if (end_that_request_first(req, uptodate, req->hard_cur_sectors))
		return;
	blkdev_dequeue_request(req);
	end_that_request_last(req);
}

static void mtdblock_request(RQFUNC_ARG)
{
   struct request *current_request;
   unsigned int res = 0;
   struct mtd_info *mtd;

   while (1)
   {
      /* Grab the Request and unlink it from the request list, we
	 will execute a return if we are done. */
	if (blk_queue_empty(QUEUE))
		return;

      current_request = CURRENT;

      if (minor(current_request->rq_dev) >= MAX_MTD_DEVICES)
      {
	 printk("mtd: Unsupported device!\n");
	 mtdblock_end_request(current_request, 0);
	 continue;
      }
      
      // Grab our MTD structure

      mtd = __get_mtd_device(NULL, minor(current_request->rq_dev));
      if (!mtd) {
	      printk("MTD device %d doesn't appear to exist any more\n", DEVICE_NR(CURRENT->rq_dev));
	      mtdblock_end_request(current_request, 0);
      }

      if (current_request->sector << 9 > mtd->size ||
	  (current_request->sector + current_request->nr_sectors) << 9 > mtd->size)
      {
	 printk("mtd: Attempt to read past end of device!\n");
	 printk("size: %x, sector: %lx, nr_sectors %lx\n", mtd->size, current_request->sector, current_request->nr_sectors);
	 mtdblock_end_request(current_request, 0);
	 continue;
      }
      
      /* Remove the request we are handling from the request list so nobody messes
         with it */
      /* Now drop the lock that the ll_rw_blk functions grabbed for us
         and process the request. This is necessary due to the extreme time
         we spend processing it. */
      spin_unlock_irq(&io_request_lock);

      // Handle the request
      switch (current_request->cmd)
      {
         size_t retlen;

	 case READ:
	 if (MTD_READ(mtd,current_request->sector<<9, 
		      current_request->nr_sectors << 9, 
		      &retlen, current_request->buffer) == 0)
	    res = 1;
	 else
	    res = 0;
	 break;
	 
	 case WRITE:

	 /* printk("mtdblock_request WRITE sector=%d(%d)\n",current_request->sector,
		current_request->nr_sectors);
	 */

	 // Read only device
	 if ((mtd->flags & MTD_CAP_RAM) == 0)
	 {
	    res = 0;
	    break;
	 }

	 // Do the write
	 if (MTD_WRITE(mtd,current_request->sector<<9, 
		       current_request->nr_sectors << 9, 
		       &retlen, current_request->buffer) == 0)
	    res = 1;
	 else
	    res = 0;
	 break;
	 
	 // Shouldn't happen
	 default:
	 printk("mtd: unknown request\n");
	 break;
      }

      // Grab the lock and re-thread the item onto the linked list
	spin_lock_irq(&io_request_lock);
	mtdblock_end_request(current_request, res);
   }
}



static int mtdblock_ioctl(struct inode * inode, struct file * file,
		      unsigned int cmd, unsigned long arg)
{
	struct mtd_info *mtd;

	mtd = __get_mtd_device(NULL, minor(inode->i_rdev));

	if (!mtd || cmd != BLKFLSBUF)
		return -EINVAL;

	if(!capable(CAP_SYS_ADMIN))
		return -EACCES;
	fsync_bdev(inode->i_bdev);
	invalidate_bdev(inode->i_bdev, 0);
	if (mtd->sync)
		mtd->sync(mtd);
	return 0;
}

static struct block_device_operations mtd_fops = 
{
	.owner		= THIS_MODULE,
	.open		= mtdblock_open,
	.release	= mtdblock_release,
	.ioctl		= mtdblock_ioctl
};

int __init init_mtdblock(void)
{
	int err = -ENOMEM;
	int i;

	for (i = 0; i < MAX_MTD_DEVICES; i++) {
		struct gendisk *disk = alloc_disk();
		if (!disk)
			goto out;
		disk->major = MAJOR_NR;
		disk->first_minor = i;
		sprintf(disk->disk_name, "mtdblock%d", i);
		disk->fops = &mtd_fops;
		mtd_disks[i] = disk;
	}

	if (register_blkdev(MAJOR_NR,DEVICE_NAME,&mtd_fops)) {
		printk(KERN_NOTICE "Can't allocate major number %d for Memory Technology Devices.\n",
		       MTD_BLOCK_MAJOR);
		err = -EAGAIN;
		goto out;
	}

	blk_init_queue(BLK_DEFAULT_QUEUE(MAJOR_NR), &mtdblock_request);
	return 0;
out:
	while (i--)
		put_disk(mtd_disks[i]);
	return err;
}

static void __exit cleanup_mtdblock(void)
{
	int i;
	unregister_blkdev(MAJOR_NR,DEVICE_NAME);
	blk_cleanup_queue(BLK_DEFAULT_QUEUE(MAJOR_NR));
	for (i = 0; i < MAX_MTD_DEVICES; i++)
		put_disk(mtd_disks[i]);
}

module_init(init_mtdblock);
module_exit(cleanup_mtdblock);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Erwin Authried <eauth@softsys.co.at> et al.");
MODULE_DESCRIPTION("Simple read-only block device emulation access to MTD devices");
