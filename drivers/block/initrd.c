
#include <linux/blkdev.h>
#include <linux/genhd.h>
#include <linux/initrd.h>
#include <linux/init.h>
#include <linux/major.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <asm/uaccess.h>


unsigned long initrd_start, initrd_end;
int initrd_below_start_ok;

static int initrd_users;
static spinlock_t initrd_users_lock = SPIN_LOCK_UNLOCKED;

static struct gendisk *initrd_disk;

static ssize_t initrd_read(struct file *file, char *buf,
			   size_t count, loff_t *ppos)
{
	int left = initrd_end - initrd_start - *ppos;

	if (count > left)
		count = left;
	if (count == 0)
		return 0;
	if (copy_to_user(buf, (char *)initrd_start + *ppos, count))
		return -EFAULT;

	*ppos += count;
	return count;
}

static int initrd_release(struct inode *inode,struct file *file)
{

	blkdev_put(inode->i_bdev, BDEV_FILE);

	spin_lock(&initrd_users_lock);
	if (!--initrd_users) {
		spin_unlock(&initrd_users_lock);
		del_gendisk(initrd_disk);
		free_initrd_mem(initrd_start, initrd_end);
		initrd_start = 0;
	} else
		spin_unlock(&initrd_users_lock);

	return 0;
}

static struct file_operations initrd_fops = {
	.read =		initrd_read,
	.release =	initrd_release,
};

static int initrd_open(struct inode *inode, struct file *filp)
{
	if (!initrd_start) 
		return -ENODEV;

	spin_lock(&initrd_users_lock);
	initrd_users++;
	spin_unlock(&initrd_users_lock);

	filp->f_op = &initrd_fops;
	return 0;
}

static struct block_device_operations initrd_bdops = {
	.owner =	THIS_MODULE,
	.open =		initrd_open,
};

static int __init initrd_init(void)
{
	initrd_disk = alloc_disk(1);
	if (!initrd_disk)
		return -ENOMEM;

	initrd_disk->major = RAMDISK_MAJOR;
	initrd_disk->first_minor = INITRD_MINOR;
	initrd_disk->fops = &initrd_bdops;	

	sprintf(initrd_disk->disk_name, "initrd");
	sprintf(initrd_disk->devfs_name, "rd/initrd");

	set_capacity(initrd_disk, (initrd_end-initrd_start+511) >> 9);
	add_disk(initrd_disk);
	return 0;
}

static void __exit initrd_exit(void)
{
	put_disk(initrd_disk);
}

module_init(initrd_init);
module_exit(initrd_exit);
