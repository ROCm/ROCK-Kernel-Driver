/**
 * \file drm_stub.h
 * Stub support
 *
 * \author Rickard E. (Rik) Faith <faith@valinux.com>
 */

/*
 * Created: Fri Jan 19 10:48:35 2001 by faith@acm.org
 *
 * Copyright 2001 VA Linux Systems, Inc., Sunnyvale, California.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include "drmP.h"
#include "drm_core.h"

unsigned int drm_cards_limit = 16;	/* Enough for one machine */
unsigned int drm_debug = 0;		/* 1 to enable debug output */
EXPORT_SYMBOL(drm_debug);

MODULE_AUTHOR( DRIVER_AUTHOR );
MODULE_DESCRIPTION( DRIVER_DESC );
MODULE_LICENSE("GPL and additional rights");
MODULE_PARM_DESC(drm_cards_limit, "Maximum number of graphics cards");
MODULE_PARM_DESC(drm_debug, "Enable debug output");

module_param_named(cards_limit, drm_cards_limit, int, 0444);
module_param_named(debug, drm_debug, int, 0666);

drm_minor_t *drm_minors;
struct class_simple *drm_class;
struct proc_dir_entry *drm_proc_root;

/**
 * File \c open operation.
 *
 * \param inode device inode.
 * \param filp file pointer.
 *
 * Puts the dev->fops corresponding to the device minor number into
 * \p filp, call the \c open method, and restore the file operations.
 */
static int stub_open(struct inode *inode, struct file *filp)
{
	drm_device_t *dev = NULL;
	int minor = iminor(inode);
	int err = -ENODEV;
	struct file_operations *old_fops;
	
	DRM_DEBUG("\n");

	if (!((minor >= 0) && (minor < drm_cards_limit)))
		return -ENODEV;

	dev = drm_minors[minor].dev;
	if (!dev)
		return -ENODEV;

	old_fops = filp->f_op;
	filp->f_op = fops_get(&dev->driver->fops);
	if (filp->f_op->open && (err = filp->f_op->open(inode, filp))) {
		fops_put(filp->f_op);
		filp->f_op = fops_get(old_fops);
	}
	fops_put(old_fops);

	return err;
}

/** File operations structure */
struct file_operations drm_stub_fops = {
	.owner = THIS_MODULE,
	.open  = stub_open
};

/**
 * Get a device minor number.
 *
 * \param pdev PCI device structure
 * \param ent entry from the PCI ID table with device type flags
 * \return zero on success or a negative number on failure.
 *
 * Attempt to gets inter module "drm" information. If we are first
 * then register the character device and inter module information.
 * Try and register, if we fail to register, backout previous work.
 */
int drm_probe(struct pci_dev *pdev, const struct pci_device_id *ent, struct drm_driver *driver)
{
	struct class_device *dev_class;
	drm_device_t *dev;
	int ret;
	int minor;
	drm_minor_t *minors = &drm_minors[0];

	DRM_DEBUG("\n");

	for (minor = 0; minor < drm_cards_limit; minor++, minors++) {
		if (minors->type == DRM_MINOR_FREE) {

			DRM_DEBUG("assigning minor %d\n", minor);
			dev = drm_calloc(1, sizeof(*dev), DRM_MEM_STUB);
			if (!dev)
				return -ENOMEM;

			*minors = (drm_minor_t){.dev = dev, .type=DRM_MINOR_PRIMARY};
			dev->minor = minor;
			if ((ret=drm_fill_in_dev(dev, pdev, ent, driver))) {
				printk(KERN_ERR "DRM: Fill_in_dev failed.\n");
				goto err_g1;
			}
			if ((ret = drm_proc_init(dev, minor, drm_proc_root, &minors->dev_root))) {
				printk (KERN_ERR "DRM: Failed to initialize /proc/dri.\n");
				goto err_g1;
			}

			pci_enable_device(pdev);
			
			dev_class = class_simple_device_add(drm_class, 
							    MKDEV(DRM_MAJOR, minor), &pdev->dev, "card%d", minor);
			if (IS_ERR(dev_class)) {
				printk(KERN_ERR "DRM: Error class_simple_device_add.\n");
				ret = PTR_ERR(dev_class);
				goto err_g2;
			}
			
			DRM_DEBUG("new minor assigned %d\n", minor);
			return 0;
		}
	}
	DRM_ERROR("out of minors\n");
	return -ENOMEM;
err_g2:
	drm_proc_cleanup(minor, drm_proc_root, minors->dev_root);
err_g1:
	*minors = (drm_minor_t){.dev = NULL, .type = DRM_MINOR_FREE};
	drm_free(dev, sizeof(*dev), DRM_MEM_STUB);
	return ret;
}
EXPORT_SYMBOL(drm_probe);
		

/**
 * Put a device minor number.
 *
 * \param minor minor number.
 * \return always zero.
 *
 * Cleans up the proc resources. If a minor is zero then release the foreign
 * "drm" data, otherwise unregisters the "drm" data, frees the stub list and
 * unregisters the character device. 
 */
int drm_put_minor(drm_device_t *dev)
{
	drm_minor_t *minors = &drm_minors[dev->minor];
	
	DRM_DEBUG("release minor %d\n", dev->minor);
	
	drm_proc_cleanup(dev->minor, drm_proc_root, minors->dev_root);
	class_simple_device_remove(MKDEV(DRM_MAJOR, dev->minor));
	
	*minors = (drm_minor_t){.dev = NULL, .type = DRM_MINOR_FREE};
	drm_free(dev, sizeof(*dev), DRM_MEM_STUB);
	
	return 0;
}

