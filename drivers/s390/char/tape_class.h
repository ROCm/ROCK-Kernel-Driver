/*
 * Tape class device support
 *
 * Author: Stefan Bader <shbader@de.ibm.com>
 * Based on simple class device code by Greg K-H
 */
#ifndef __TAPE_CLASS_H__
#define __TAPE_CLASS_H__

#if 0
#include <linux/init.h>
#include <linux/module.h>
#endif

#include <linux/fs.h>
#include <linux/major.h>
#include <linux/kobject.h>
#include <linux/kobj_map.h>
#include <linux/cdev.h>

#include <linux/device.h>
#include <linux/kdev_t.h>

#define TAPE390_INTERNAL_CLASS

/*
 * Register a tape device and return a pointer to the cdev structure.
 *
 * device
 *	The pointer to the struct device of the physical (base) device.
 * drivername
 *	The pointer to the drivers name for it's character devices.
 * dev
 *	The intended major/minor number. The major number may be 0 to
 *	get a dynamic major number.
 * fops
 *	The pointer to the drivers file operations for the tape device.
 * devname
 *	The pointer to the name of the character device.
 */
struct cdev *register_tape_dev(
	struct device *		device,
	dev_t			dev,
	struct file_operations *fops,
	char *			devname
);
void unregister_tape_dev(struct cdev *cdev);

#ifdef TAPE390_INTERNAL_CLASS
int tape_setup_class(void);
void tape_cleanup_class(void);
#endif

#endif /* __TAPE_CLASS_H__ */
