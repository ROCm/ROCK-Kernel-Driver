/*
 * drivers/usb/file.c
 *
 * (C) Copyright Linus Torvalds 1999
 * (C) Copyright Johannes Erdfelt 1999-2001
 * (C) Copyright Andreas Gal 1999
 * (C) Copyright Gregory P. Smith 1999
 * (C) Copyright Deti Fliegl 1999 (new USB architecture)
 * (C) Copyright Randy Dunlap 2000
 * (C) Copyright David Brownell 2000-2001 (kernel hotplug, usb_device_id,
 	more docs, etc)
 * (C) Copyright Yggdrasil Computing, Inc. 2000
 *     (usb_device_id matching changes by Adam J. Richter)
 * (C) Copyright Greg Kroah-Hartman 2002
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/spinlock.h>
#include <linux/errno.h>

#ifdef CONFIG_USB_DEBUG
	#define DEBUG
#else
	#undef DEBUG
#endif
#include <linux/usb.h>

#define MAX_USB_MINORS	256
static struct file_operations *usb_minors[MAX_USB_MINORS];
static spinlock_t minor_lock = SPIN_LOCK_UNLOCKED;

static int usb_open(struct inode * inode, struct file * file)
{
	int minor = minor(inode->i_rdev);
	struct file_operations *c;
	int err = -ENODEV;
	struct file_operations *old_fops, *new_fops = NULL;

	spin_lock (&minor_lock);
	c = usb_minors[minor];

	if (!c || !(new_fops = fops_get(c))) {
		spin_unlock(&minor_lock);
		return err;
	}
	spin_unlock(&minor_lock);

	old_fops = file->f_op;
	file->f_op = new_fops;
	/* Curiouser and curiouser... NULL ->open() as "no device" ? */
	if (file->f_op->open)
		err = file->f_op->open(inode,file);
	if (err) {
		fops_put(file->f_op);
		file->f_op = fops_get(old_fops);
	}
	fops_put(old_fops);
	return err;
}

static struct file_operations usb_fops = {
	.owner =	THIS_MODULE,
	.open =		usb_open,
};

int usb_major_init(void)
{
	if (register_chrdev(USB_MAJOR, "usb", &usb_fops)) {
		err("unable to get major %d for usb devices", USB_MAJOR);
		return -EBUSY;
	}

	devfs_mk_dir("usb");
	return 0;
}

void usb_major_cleanup(void)
{
	devfs_remove("usb");
	unregister_chrdev(USB_MAJOR, "usb");
}

/**
 * usb_register_dev - register a USB device, and ask for a minor number
 * @fops: the file operations for this USB device
 * @minor: the requested starting minor for this device.
 * @num_minors: number of minor numbers requested for this device
 * @start_minor: place to put the new starting minor number
 *
 * This should be called by all USB drivers that use the USB major number.
 * If CONFIG_USB_DYNAMIC_MINORS is enabled, the minor number will be
 * dynamically allocated out of the list of available ones.  If it is not
 * enabled, the minor number will be based on the next available free minor,
 * starting at the requested @minor.
 *
 * usb_deregister_dev() must be called when the driver is done with
 * the minor numbers given out by this function.
 *
 * Returns -EINVAL if something bad happens with trying to register a
 * device, and 0 on success, alone with a value that the driver should
 * use in start_minor.
 */
int usb_register_dev (struct file_operations *fops, int minor, int num_minors, int *start_minor)
{
	int i;
	int j;
	int good_spot;
	int retval = -EINVAL;

#ifdef CONFIG_USB_DYNAMIC_MINORS
	/* 
	 * We don't care what the device tries to start at, we want to start
	 * at zero to pack the devices into the smallest available space with
	 * no holes in the minor range.
	 */
	minor = 0;
#endif

	dbg ("asking for %d minors, starting at %d", num_minors, minor);

	if (fops == NULL)
		goto exit;

	*start_minor = 0; 
	spin_lock (&minor_lock);
	for (i = minor; i < MAX_USB_MINORS; ++i) {
		if (usb_minors[i])
			continue;

		good_spot = 1;
		for (j = 1; j <= num_minors-1; ++j)
			if (usb_minors[i+j]) {
				good_spot = 0;
				break;
			}
		if (good_spot == 0)
			continue;

		*start_minor = i;
		dbg("found a minor chunk free, starting at %d", i);
		for (i = *start_minor; i < (*start_minor + num_minors); ++i)
			usb_minors[i] = fops;

		retval = 0;
		goto exit;
	}
exit:
	spin_unlock (&minor_lock);
	return retval;
}
EXPORT_SYMBOL(usb_register_dev);

/**
 * usb_deregister_dev - deregister a USB device's dynamic minor.
 * @num_minors: number of minor numbers to put back.
 * @start_minor: the starting minor number
 *
 * Used in conjunction with usb_register_dev().  This function is called
 * when the USB driver is finished with the minor numbers gotten from a
 * call to usb_register_dev() (usually when the device is disconnected
 * from the system.)
 * 
 * This should be called by all drivers that use the USB major number.
 */
void usb_deregister_dev (int num_minors, int start_minor)
{
	int i;

	dbg ("removing %d minors starting at %d", num_minors, start_minor);

	spin_lock (&minor_lock);
	for (i = start_minor; i < (start_minor + num_minors); ++i)
		usb_minors[i] = NULL;
	spin_unlock (&minor_lock);
}
EXPORT_SYMBOL(usb_deregister_dev);


