/*
 * linux/drivers/char/raw.c
 *
 * Front-end raw character devices.  These can be bound to any block
 * devices to provide genuine Unix raw character device semantics.
 *
 * We reserve minor number 0 for a control interface.  ioctl()s on this
 * device are used to bind the other minor numbers to block devices.
 */

#include <linux/init.h>
#include <linux/fs.h>
#include <linux/major.h>
#include <linux/blkdev.h>
#include <linux/raw.h>
#include <linux/capability.h>
#include <linux/smp_lock.h>
#include <asm/uaccess.h>

typedef struct raw_device_data_s {
	struct block_device *binding;
	int inuse;
	struct semaphore mutex;
} raw_device_data_t;

static raw_device_data_t raw_devices[256];

static ssize_t rw_raw_dev(int rw, struct file *, char *, size_t, loff_t *);

ssize_t	raw_read(struct file *, char *, size_t, loff_t *);
ssize_t	raw_write(struct file *, const char *, size_t, loff_t *);
int	raw_open(struct inode *, struct file *);
int	raw_release(struct inode *, struct file *);
int	raw_ctl_ioctl(struct inode *, struct file *, unsigned int, unsigned long);
int	raw_ioctl(struct inode *, struct file *, unsigned int, unsigned long);


static struct file_operations raw_fops = {
	read:		raw_read,
	write:		raw_write,
	open:		raw_open,
	release:	raw_release,
	ioctl:		raw_ioctl,
};

static struct file_operations raw_ctl_fops = {
	ioctl:		raw_ctl_ioctl,
	open:		raw_open,
};

static int __init raw_init(void)
{
	int i;
	register_chrdev(RAW_MAJOR, "raw", &raw_fops);

	for (i = 0; i < 256; i++)
		init_MUTEX(&raw_devices[i].mutex);

	return 0;
}

__initcall(raw_init);

/* 
 * Open/close code for raw IO.
 *
 * Set the device's soft blocksize to the minimum possible.  This gives the 
 * finest possible alignment and has no adverse impact on performance.
 */
int raw_open(struct inode *inode, struct file *filp)
{
	int minor;
	struct block_device * bdev;
	int err;

	minor = minor(inode->i_rdev);
	
	/* 
	 * Is it the control device? 
	 */
	
	if (minor == 0) {
		filp->f_op = &raw_ctl_fops;
		return 0;
	}
	
	down(&raw_devices[minor].mutex);

	/*
	 * No, it is a normal raw device.  All we need to do on open is
	 * to check that the device is bound.
	 */
	bdev = raw_devices[minor].binding;
	err = -ENODEV;
	if (!bdev)
		goto out;

	atomic_inc(&bdev->bd_count);
	err = blkdev_get(bdev, filp->f_mode, 0, BDEV_RAW);
	if (!err) {
		int minsize = bdev_hardsect_size(bdev);

		if (bdev) {
			int ret;

			ret = set_blocksize(bdev, minsize);
			if (ret)
				printk("%s: set_blocksize() failed: %d\n",
					__FUNCTION__, ret);
		}
		raw_devices[minor].inuse++;
	}
 out:
	up(&raw_devices[minor].mutex);
	
	return err;
}

int raw_release(struct inode *inode, struct file *filp)
{
	int minor;
	struct block_device *bdev;
	
	minor = minor(inode->i_rdev);
	down(&raw_devices[minor].mutex);
	bdev = raw_devices[minor].binding;
	raw_devices[minor].inuse--;
	up(&raw_devices[minor].mutex);
	blkdev_put(bdev, BDEV_RAW);
	return 0;
}

/* Forward ioctls to the underlying block device. */ 
int raw_ioctl(struct inode *inode, 
		  struct file *filp,
		  unsigned int command, 
		  unsigned long arg)
{
	int minor = minor(inode->i_rdev);
	int err; 
	struct block_device *b; 

	err = -ENODEV;
	if (minor < 1 && minor > 255)
		goto out;

	b = raw_devices[minor].binding;
	err = -EINVAL;
	if (b == NULL)
		goto out;
	if (b->bd_inode && b->bd_op && b->bd_op->ioctl)
		err = b->bd_op->ioctl(b->bd_inode, NULL, command, arg); 
out:
	return err;
}

/*
 * Deal with ioctls against the raw-device control interface, to bind
 * and unbind other raw devices.  
 */

int raw_ctl_ioctl(struct inode *inode, 
		  struct file *filp,
		  unsigned int command, 
		  unsigned long arg)
{
	struct raw_config_request rq;
	int err;
	int minor;
	
	switch (command) {
	case RAW_SETBIND:
	case RAW_GETBIND:

		/* First, find out which raw minor we want */

		err = -EFAULT;
		if (copy_from_user(&rq, (void *) arg, sizeof(rq)))
			goto out;
		
		minor = rq.raw_minor;
		err = -EINVAL;
		if (minor <= 0 || minor > MINORMASK)
			goto out;

		if (command == RAW_SETBIND) {
			/*
			 * This is like making block devices, so demand the
			 * same capability
			 */
			err = -EPERM;
			if (!capable(CAP_SYS_ADMIN))
				goto out;

			/* 
			 * For now, we don't need to check that the underlying
			 * block device is present or not: we can do that when
			 * the raw device is opened.  Just check that the
			 * major/minor numbers make sense. 
			 */

			err = -EINVAL;
			if ((rq.block_major == 0 && rq.block_minor != 0) ||
					rq.block_major > MAX_BLKDEV ||
					rq.block_minor > MINORMASK)
				goto out;
			
			down(&raw_devices[minor].mutex);
			err = -EBUSY;
			if (raw_devices[minor].inuse) {
				up(&raw_devices[minor].mutex);
				goto out;
			}
			if (raw_devices[minor].binding)
				bdput(raw_devices[minor].binding);
			raw_devices[minor].binding = 
				bdget(kdev_t_to_nr(mk_kdev(rq.block_major,
							rq.block_minor)));
			up(&raw_devices[minor].mutex);
		} else {
			struct block_device *bdev;
			kdev_t dev;

			bdev = raw_devices[minor].binding;
			if (bdev) {
				dev = to_kdev_t(bdev->bd_dev);
				rq.block_major = major(dev);
				rq.block_minor = minor(dev);
			} else {
				rq.block_major = rq.block_minor = 0;
			}
			err = -EFAULT;
			if (copy_to_user((void *) arg, &rq, sizeof(rq)))
				goto out;
		}
		err = 0;
		break;
		
	default:
		err = -EINVAL;
		break;
	}
out:
	return err;
}

ssize_t raw_read(struct file *filp, char * buf, size_t size, loff_t *offp)
{
	return rw_raw_dev(READ, filp, buf, size, offp);
}

ssize_t	raw_write(struct file *filp, const char *buf, size_t size, loff_t *offp)
{
	return rw_raw_dev(WRITE, filp, (char *)buf, size, offp);
}

ssize_t
rw_raw_dev(int rw, struct file *filp, char *buf, size_t size, loff_t *offp)
{
	struct block_device *bdev;
	struct inode *inode;
	int minor;
	ssize_t ret = 0;

	minor = minor(filp->f_dentry->d_inode->i_rdev);
	bdev = raw_devices[minor].binding;
	inode = bdev->bd_inode;

	if (size == 0)
		goto out;
	if (size < 0) {
		ret = -EINVAL;
		goto out;
	}
	if (*offp >= inode->i_size) {
		ret = -ENXIO;
		goto out;
	}
	if (size + *offp > inode->i_size)
		size = inode->i_size - *offp;

	ret = generic_file_direct_IO(rw, inode, buf, *offp, size);
	if (ret > 0)
		*offp += ret;
	if (inode->i_mapping->nrpages)
		invalidate_inode_pages2(inode->i_mapping);
out:
	return ret;
}
