#define COW_MAJOR 60
#define MAJOR_NR COW_MAJOR

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/ctype.h>
#include <linux/stat.h>
#include <linux/vmalloc.h>
#include <linux/blkdev.h>
#include <linux/blk.h>
#include <linux/fs.h>
#include <linux/genhd.h>
#include <linux/devfs_fs.h>
#include <asm/uaccess.h>
#include "2_5compat.h"
#include "cow.h"
#include "ubd_user.h"

#define COW_SHIFT 4

struct cow {
	int count;
	char *cow_path;
	dev_t cow_dev;
	struct block_device *cow_bdev;
	char *backing_path;
	dev_t backing_dev;
	struct block_device *backing_bdev;
	int sectorsize;
	unsigned long *bitmap;
	unsigned long bitmap_len;
	int bitmap_offset;
	int data_offset;
	devfs_handle_t devfs;
	struct semaphore sem;
	struct semaphore io_sem;
	atomic_t working;
	spinlock_t io_lock;
	struct buffer_head *bh;
	struct buffer_head *bhtail;
	void *end_io;
};

#define DEFAULT_COW { \
	.count			= 0, \
	.cow_path		= NULL, \
	.cow_dev		= 0, \
	.backing_path		= NULL, \
	.backing_dev		= 0, \
        .bitmap			= NULL, \
	.bitmap_len		= 0, \
	.bitmap_offset		= 0, \
        .data_offset		= 0, \
	.devfs			= NULL, \
	.working		= ATOMIC_INIT(0), \
	.io_lock		= SPIN_LOCK_UNLOCKED, \
}

#define MAX_DEV (8)
#define MAX_MINOR (MAX_DEV << COW_SHIFT)

struct cow cow_dev[MAX_DEV] = { [ 0 ... MAX_DEV - 1 ] = DEFAULT_COW };

/* Not modified by this driver */
static int blk_sizes[MAX_MINOR] = { [ 0 ... MAX_MINOR - 1 ] = BLOCK_SIZE };
static int hardsect_sizes[MAX_MINOR] = { [ 0 ... MAX_MINOR - 1 ] = 512 };

/* Protected by cow_lock */
static int sizes[MAX_MINOR] = { [ 0 ... MAX_MINOR - 1 ] = 0 };

static struct hd_struct	cow_part[MAX_MINOR] =
	{ [ 0 ... MAX_MINOR - 1 ] = { 0, 0, 0 } };

/* Protected by io_request_lock */
static request_queue_t *cow_queue;

static int cow_open(struct inode *inode, struct file *filp);
static int cow_release(struct inode * inode, struct file * file);
static int cow_ioctl(struct inode * inode, struct file * file,
		     unsigned int cmd, unsigned long arg);
static int cow_revalidate(kdev_t rdev);

static struct block_device_operations cow_blops = {
	.open		= cow_open,
	.release	= cow_release,
	.ioctl		= cow_ioctl,
	.revalidate	= cow_revalidate,
};

/* Initialized in an initcall, and unchanged thereafter */
devfs_handle_t cow_dir_handle;

#define INIT_GENDISK(maj, name, parts, shift, bsizes, max, blops) \
{ \
	.major 		= maj, \
	.major_name  	= name, \
	.minor_shift 	= shift, \
	.max_p  	= 1 << shift, \
	.part  		= parts, \
	.sizes  	= bsizes, \
	.nr_real  	= max, \
	.real_devices  	= NULL, \
	.next  		= NULL, \
	.fops  		= blops, \
	.de_arr  	= NULL, \
	.flags  	= 0 \
}

static spinlock_t cow_lock = SPIN_LOCK_UNLOCKED;

static struct gendisk cow_gendisk = INIT_GENDISK(MAJOR_NR, "cow", cow_part,
						 COW_SHIFT, sizes, MAX_DEV,
						 &cow_blops);

static int cow_add(int n)
{
	struct cow *dev = &cow_dev[n];
	char name[sizeof("nnnnnn\0")];
	int err = -ENODEV;

	if(dev->cow_path == NULL)
		goto out;

	sprintf(name, "%d", n);
	dev->devfs = devfs_register(cow_dir_handle, name, DEVFS_FL_REMOVABLE,
				    MAJOR_NR, n << COW_SHIFT, S_IFBLK |
				    S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP,
				    &cow_blops, NULL);

	init_MUTEX_LOCKED(&dev->sem);
	init_MUTEX(&dev->io_sem);

	return(0);

 out:
	return(err);
}

/*
 * Add buffer_head to back of pending list
 */
static void cow_add_bh(struct cow *cow, struct buffer_head *bh)
{
	unsigned long flags;

	spin_lock_irqsave(&cow->io_lock, flags);
	if(cow->bhtail != NULL){
		cow->bhtail->b_reqnext = bh;
		cow->bhtail = bh;
	}
	else {
		cow->bh = bh;
		cow->bhtail = bh;
	}
	spin_unlock_irqrestore(&cow->io_lock, flags);
}

/*
* Grab first pending buffer
*/
static struct buffer_head *cow_get_bh(struct cow *cow)
{
	struct buffer_head *bh;

	spin_lock_irq(&cow->io_lock);
	bh = cow->bh;
	if(bh != NULL){
		if(bh == cow->bhtail)
			cow->bhtail = NULL;
		cow->bh = bh->b_reqnext;
		bh->b_reqnext = NULL;
	}
	spin_unlock_irq(&cow->io_lock);

	return(bh);
}

static void cow_handle_bh(struct cow *cow, struct buffer_head *bh,
			  struct buffer_head **cow_bh, int ncow_bh)
{
	int i;

	if(ncow_bh > 0)
		ll_rw_block(WRITE, ncow_bh, cow_bh);

	for(i = 0; i < ncow_bh ; i++){
		wait_on_buffer(cow_bh[i]);
		brelse(cow_bh[i]);
	}

	ll_rw_block(WRITE, 1, &bh);
	brelse(bh);
}

static struct buffer_head *cow_new_bh(struct cow *dev, int sector)
{
	struct buffer_head *bh;

	sector = (dev->bitmap_offset + sector / 8) / dev->sectorsize;
	bh = getblk(dev->cow_dev, sector, dev->sectorsize);
	memcpy(bh->b_data, dev->bitmap + sector / (8 * sizeof(dev->bitmap[0])),
	       dev->sectorsize);
	return(bh);
}

/* Copied from loop.c, needed to avoid deadlocking in make_request. */

static int cow_thread(void *data)
{
	struct cow *dev = data;
	struct buffer_head *bh;

	daemonize();
	exit_files(current);

	sprintf(current->comm, "cow%d", dev - cow_dev);

	spin_lock_irq(&current->sigmask_lock);
	sigfillset(&current->blocked);
	flush_signals(current);
	spin_unlock_irq(&current->sigmask_lock);

	atomic_inc(&dev->working);

	current->policy = SCHED_OTHER;
	current->nice = -20;

	current->flags |= PF_NOIO;

	/*
	 * up sem, we are running
	 */
	up(&dev->sem);

	for(;;){
		int start, len, nbh, i, update_bitmap = 0;
		struct buffer_head *cow_bh[2];

		down_interruptible(&dev->io_sem);
		/*
		 * could be upped because of tear-down, not because of
		 * pending work
		 */
		if(!atomic_read(&dev->working))
			break;

		bh = cow_get_bh(dev);
		if(bh == NULL){
			printk(KERN_ERR "cow: missing bh\n");
			continue;
		}

		start = bh->b_blocknr * bh->b_size / dev->sectorsize;
		len = bh->b_size / dev->sectorsize;
		for(i = 0; i < len ; i++){
			if(ubd_test_bit(start + i,
					(unsigned char *) dev->bitmap))
				continue;

			update_bitmap = 1;
			ubd_set_bit(start + i, (unsigned char *) dev->bitmap);
		}

		cow_bh[0] = NULL;
		cow_bh[1] = NULL;
		nbh = 0;
		if(update_bitmap){
			cow_bh[0] = cow_new_bh(dev, start);
			nbh++;
			if(start / dev->sectorsize !=
			   (start + len) / dev->sectorsize){
				cow_bh[1] = cow_new_bh(dev, start + len);
				nbh++;
			}
		}

		bh->b_dev = dev->cow_dev;
		bh->b_blocknr += dev->data_offset / dev->sectorsize;

		cow_handle_bh(dev, bh, cow_bh, nbh);

		/*
		 * upped both for pending work and tear-down, lo_pending
		 * will hit zero then
		 */
		if(atomic_dec_and_test(&dev->working))
			break;
	}

	up(&dev->sem);
	return(0);
}

static int cow_make_request(request_queue_t *q, int rw, struct buffer_head *bh)
{
	struct cow *dev;
	int n, minor;

	minor = MINOR(bh->b_rdev);
	n = minor >> COW_SHIFT;
	dev = &cow_dev[n];

	dev->end_io = NULL;
	if(ubd_test_bit(bh->b_rsector, (unsigned char *) dev->bitmap)){
		bh->b_rdev = dev->cow_dev;
		bh->b_rsector += dev->data_offset / dev->sectorsize;
	}
	else if(rw == WRITE){
		bh->b_dev = dev->cow_dev;
		bh->b_blocknr += dev->data_offset / dev->sectorsize;

		cow_add_bh(dev, bh);
		up(&dev->io_sem);
		return(0);
	}
	else {
		bh->b_rdev = dev->backing_dev;
	}

	return(1);
}

int cow_init(void)
{
	int i;

	cow_dir_handle = devfs_mk_dir (NULL, "cow", NULL);
	if (devfs_register_blkdev(MAJOR_NR, "cow", &cow_blops)) {
		printk(KERN_ERR "cow: unable to get major %d\n", MAJOR_NR);
		return -1;
	}
	read_ahead[MAJOR_NR] = 8;		/* 8 sector (4kB) read-ahead */
	blksize_size[MAJOR_NR] = blk_sizes;
	blk_size[MAJOR_NR] = sizes;
	INIT_HARDSECT(hardsect_size, MAJOR_NR, hardsect_sizes);

	cow_queue = BLK_DEFAULT_QUEUE(MAJOR_NR);
	blk_init_queue(cow_queue, NULL);
	INIT_ELV(cow_queue, &cow_queue->elevator);
	blk_queue_make_request(cow_queue, cow_make_request);

	add_gendisk(&cow_gendisk);

	for(i=0;i<MAX_DEV;i++)
		cow_add(i);

	return(0);
}

__initcall(cow_init);

static int reader(__u64 start, char *buf, int count, void *arg)
{
	dev_t dev = *((dev_t *) arg);
	struct buffer_head *bh;
	__u64 block;
	int cur, offset, left, n, blocksize = get_hardsect_size(dev);

	if(blocksize == 0)
		panic("Zero blocksize");

	block = start / blocksize;
	offset = start % blocksize;
	left = count;
	cur = 0;
	while(left > 0){
		n = (left > blocksize) ? blocksize : left;

		bh = bread(dev, block, (n < 512) ? 512 : n);
		if(bh == NULL)
			return(-EIO);

		n -= offset;
		memcpy(&buf[cur], bh->b_data + offset, n);
		block++;
		left -= n;
		cur += n;
		offset = 0;
		brelse(bh);
	}

	return(count);
}

static int cow_open(struct inode *inode, struct file *filp)
{
	int (*dev_ioctl)(struct inode *, struct file *, unsigned int,
			 unsigned long);
	mm_segment_t fs;
	struct cow *dev;
	__u64 size;
	__u32 version, align;
	time_t mtime;
	char *backing_file;
	int n, offset, err = 0;

	n = DEVICE_NR(inode->i_rdev);
	if(n >= MAX_DEV)
		return(-ENODEV);
	dev = &cow_dev[n];
	offset = n << COW_SHIFT;

	spin_lock(&cow_lock);

	if(dev->count == 0){
		dev->cow_dev = name_to_kdev_t(dev->cow_path);
		if(dev->cow_dev == 0){
			printk(KERN_ERR "cow_open - name_to_kdev_t(\"%s\") "
			       "failed\n", dev->cow_path);
			err = -ENODEV;
		}

		dev->backing_dev = name_to_kdev_t(dev->backing_path);
		if(dev->backing_dev == 0){
			printk(KERN_ERR "cow_open - name_to_kdev_t(\"%s\") "
			       "failed\n", dev->backing_path);
			err = -ENODEV;
		}

		if(err)
			goto out;

		dev->cow_bdev = bdget(dev->cow_dev);
		if(dev->cow_bdev == NULL){
			printk(KERN_ERR "cow_open - bdget(\"%s\") failed\n",
			       dev->cow_path);
			err = -ENOMEM;
		}
		dev->backing_bdev = bdget(dev->backing_dev);
		if(dev->backing_bdev == NULL){
			printk(KERN_ERR "cow_open - bdget(\"%s\") failed\n",
			       dev->backing_path);
			err = -ENOMEM;
		}

		if(err)
			goto out;

		err = blkdev_get(dev->cow_bdev, FMODE_READ|FMODE_WRITE, 0,
				 BDEV_RAW);
		if(err){
			printk("cow_open - blkdev_get of COW device failed, "
			       "error = %d\n", err);
			goto out;
		}

		err = blkdev_get(dev->backing_bdev, FMODE_READ, 0, BDEV_RAW);
		if(err){
			printk("cow_open - blkdev_get of backing device "
			       "failed, error = %d\n", err);
			goto out;
		}

		err = read_cow_header(reader, &dev->cow_dev, &version,
				      &backing_file, &mtime, &size,
				      &dev->sectorsize, &align,
				      &dev->bitmap_offset);
		if(err){
			printk(KERN_ERR "cow_open - read_cow_header failed, "
			       "err = %d\n", err);
			goto out;
		}

		cow_sizes(version, size, dev->sectorsize, align,
			  dev->bitmap_offset, &dev->bitmap_len,
			  &dev->data_offset);
		dev->bitmap = (void *) vmalloc(dev->bitmap_len);
		if(dev->bitmap == NULL){
			err = -ENOMEM;
			printk(KERN_ERR "Failed to vmalloc COW bitmap\n");
			goto out;
		}
		flush_tlb_kernel_vm();

		err = reader(dev->bitmap_offset, (char *) dev->bitmap,
			     dev->bitmap_len, &dev->cow_dev);
		if(err < 0){
			printk(KERN_ERR "Failed to read COW bitmap\n");
			vfree(dev->bitmap);
			goto out;
		}

		dev_ioctl = dev->backing_bdev->bd_op->ioctl;
		fs = get_fs();
		set_fs(KERNEL_DS);
		err = (*dev_ioctl)(inode, filp, BLKGETSIZE,
				   (unsigned long) &sizes[offset]);
		set_fs(fs);
		if(err){
			printk(KERN_ERR "cow_open - BLKGETSIZE failed, "
			       "error = %d\n", err);
			goto out;
		}

		kernel_thread(cow_thread, dev,
			      CLONE_FS | CLONE_FILES | CLONE_SIGHAND);
		down(&dev->sem);
	}
	dev->count++;
 out:
	spin_unlock(&cow_lock);
	return(err);
}

static int cow_release(struct inode * inode, struct file * file)
{
	struct cow *dev;
	int n, err;

	n = DEVICE_NR(inode->i_rdev);
	if(n >= MAX_DEV)
		return(-ENODEV);
	dev = &cow_dev[n];

	spin_lock(&cow_lock);

	if(--dev->count > 0)
		goto out;

	err = blkdev_put(dev->cow_bdev, BDEV_RAW);
	if(err)
		printk("cow_release - blkdev_put of cow device failed, "
		       "error = %d\n", err);
	bdput(dev->cow_bdev);
	dev->cow_bdev = 0;

	err = blkdev_put(dev->backing_bdev, BDEV_RAW);
	if(err)
		printk("cow_release - blkdev_put of backing device failed, "
		       "error = %d\n", err);
	bdput(dev->backing_bdev);
	dev->backing_bdev = 0;

 out:
	spin_unlock(&cow_lock);
	return(0);
}

static int cow_ioctl(struct inode * inode, struct file * file,
		     unsigned int cmd, unsigned long arg)
{
	struct cow *dev;
	int (*dev_ioctl)(struct inode *, struct file *, unsigned int,
			 unsigned long);
	int n;

	n = DEVICE_NR(inode->i_rdev);
	if(n >= MAX_DEV)
		return(-ENODEV);
	dev = &cow_dev[n];

	dev_ioctl = dev->backing_bdev->bd_op->ioctl;
	return((*dev_ioctl)(inode, file, cmd, arg));
}

static int cow_revalidate(kdev_t rdev)
{
	printk(KERN_ERR "Need to implement cow_revalidate\n");
	return(0);
}

static int parse_unit(char **ptr)
{
	char *str = *ptr, *end;
	int n = -1;

	if(isdigit(*str)) {
		n = simple_strtoul(str, &end, 0);
		if(end == str)
			return(-1);
		*ptr = end;
	}
	else if (('a' <= *str) && (*str <= 'h')) {
		n = *str - 'a';
		str++;
		*ptr = str;
	}
	return(n);
}

static int cow_setup(char *str)
{
	struct cow *dev;
	char *cow_name, *backing_name;
	int unit;

	unit = parse_unit(&str);
	if(unit < 0){
		printk(KERN_ERR "cow_setup - Couldn't parse unit number\n");
		return(1);
	}

	if(*str != '='){
		printk(KERN_ERR "cow_setup - Missing '=' after unit "
		       "number\n");
		return(1);
	}
	str++;

	cow_name = str;
	backing_name = strchr(str, ',');
	if(backing_name == NULL){
		printk(KERN_ERR "cow_setup - missing backing device name\n");
		return(0);
	}
	*backing_name = '\0';
	backing_name++;

	spin_lock(&cow_lock);

	dev = &cow_dev[unit];
	dev->cow_path = cow_name;
	dev->backing_path = backing_name;

	spin_unlock(&cow_lock);
	return(0);
}

__setup("cow", cow_setup);

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
