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

#include "drmP.h"

static unsigned int cards_limit = 16;	/* Enough for one machine */
static unsigned int debug = 0;		/* 1 to enable debug output */

MODULE_AUTHOR( DRIVER_AUTHOR );
MODULE_DESCRIPTION( DRIVER_DESC );
MODULE_LICENSE("GPL and additional rights");
MODULE_PARM_DESC(cards_limit, "Maximum number of graphics cards");
MODULE_PARM_DESC(debug, "Enable debug output");

module_param(cards_limit, int, 0444);
module_param(debug, int, 0666);

drm_global_t *DRM(global);

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

	if (!((minor >= 0) && (minor < DRM(global)->cards_limit)))
		return -ENODEV;

	dev = DRM(global)->minors[minor].dev;
	if (!dev)
		return -ENODEV;

	old_fops = filp->f_op;
	filp->f_op = fops_get(dev->fops);
	if (filp->f_op->open && (err = filp->f_op->open(inode, filp))) {
		fops_put(filp->f_op);
		filp->f_op = fops_get(old_fops);
	}
	fops_put(old_fops);

	return err;
}

/** File operations structure */
static struct file_operations DRM(stub_fops) = {
	.owner = THIS_MODULE,
	.open  = stub_open
};

/**
 * Get a device minor number.
 *
 * \param pdev PCI device structure
 * \param ent entry from the PCI ID table with device type flags
 * \return negative number on failure.
 *
 * Search an empty entry and initialize it to the given parameters, and 
 * create the proc init entry via proc_init().
 */
static int get_minor(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct class_device *dev_class;
	drm_device_t *dev;
	int ret;
	int minor;
	drm_minor_t *minors = &DRM(global)->minors[0];

	DRM_DEBUG("\n");

	for (minor=0; minor<DRM(global)->cards_limit; minor++, minors++) {
		if (minors->type == DRM_MINOR_FREE) {

			DRM_DEBUG("assigning minor %d\n", minor);
			dev = DRM(calloc)(1, sizeof(*dev), DRM_MEM_STUB);
			if (!dev)
				return -ENOMEM;

			*minors = (drm_minor_t){.dev = dev, .type=DRM_MINOR_PRIMARY};
			dev->minor = minor;
			if ((ret=DRM(fill_in_dev)(dev, pdev, ent))) {
				printk(KERN_ERR "DRM: Fill_in_dev failed.\n");
				goto err_g1;
			}
			if ((ret = DRM(proc_init)(dev, minor, DRM(global)->proc_root, &dev->dev_root))) {
				printk (KERN_ERR "DRM: Failed to initialize /proc/dri.\n");
				goto err_g1;
			}

			pci_enable_device(pdev);
			
			dev_class = class_simple_device_add(DRM(global)->drm_class, 
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
	DRM(proc_cleanup)(minor, DRM(global)->proc_root, minors->dev_root);
err_g1:
	*minors = (drm_minor_t){.dev = NULL, .type = DRM_MINOR_FREE};
	DRM(free)(dev, sizeof(*dev), DRM_MEM_STUB);
	return ret;
}
		

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
int DRM(put_minor)(drm_device_t *dev)
{
	drm_minor_t *minors = &DRM(global)->minors[dev->minor];
	int i;
	
	DRM_DEBUG("release minor %d\n", dev->minor);
	
	DRM(proc_cleanup)(dev->minor, DRM(global)->proc_root, dev->dev_root);
	class_simple_device_remove(MKDEV(DRM_MAJOR, dev->minor));
	
	*minors = (drm_minor_t){.dev = NULL, .type = DRM_MINOR_FREE};
	DRM(free)(dev, sizeof(*dev), DRM_MEM_STUB);
	
	/* if any device pointers are non-NULL we are not the last module */
	for (i=0; i<DRM(global)->cards_limit; i++) {
		if (DRM(global)->minors[i].type != DRM_MINOR_FREE) {
			DRM_DEBUG("inter_module_put called\n");
			inter_module_put("drm");
			return 0;
		}
	}
	DRM_DEBUG("unregistering inter_module.\n");
	inter_module_unregister("drm");
	remove_proc_entry("dri", NULL);
	class_simple_destroy(DRM(global)->drm_class);

	unregister_chrdev(DRM_MAJOR, "drm");
	DRM(free)(DRM(global)->minors, sizeof(*DRM(global)->minors) * 
		  DRM(global)->cards_limit, DRM_MEM_STUB);
	DRM(free)(DRM(global), sizeof(*DRM(global)), DRM_MEM_STUB);
	DRM(global) = NULL;

	return 0;
}

/**
 * Register.
 *
 * \param pdev - PCI device structure
 * \param ent entry from the PCI ID table with device type flags
 * \return zero on success or a negative number on failure.
 *
 * Attempt to gets inter module "drm" information. If we are first
 * then register the character device and inter module information.
 * Try and register, if we fail to register, backout previous work.
 */
int DRM(probe)(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	drm_global_t *global;
	int ret = -ENOMEM;

	DRM_DEBUG("\n");

	/* use the inter_module_get to check - as if the same module
		registers chrdev twice it succeeds */
	global = (drm_global_t *)inter_module_get("drm");
	if (global) {
		DRM(global) = global;
		global = NULL;
	} else {
		DRM_DEBUG("first probe\n");

		global = DRM(calloc)(1, sizeof(*global), DRM_MEM_STUB);
		if(!global) 
			return -ENOMEM;

		global->cards_limit = (cards_limit < DRM_MAX_MINOR + 1 ? cards_limit : DRM_MAX_MINOR + 1);
		global->minors = DRM(calloc)(global->cards_limit, 
					sizeof(*global->minors), DRM_MEM_STUB);
		if(!global->minors) 
			goto err_p1;

		if (register_chrdev(DRM_MAJOR, "drm", &DRM(stub_fops)))
			goto err_p1;
	
		global->drm_class = class_simple_create(THIS_MODULE, "drm");
		if (IS_ERR(global->drm_class)) {
			printk (KERN_ERR "DRM: Error creating drm class.\n");
			ret = PTR_ERR(global->drm_class);
			goto err_p2;
		}

		global->proc_root = create_proc_entry("dri", S_IFDIR, NULL);
		if (!global->proc_root) {
			DRM_ERROR("Cannot create /proc/dri\n");
			ret = -1;
			goto err_p3;
		}
		DRM_DEBUG("calling inter_module_register\n");
		inter_module_register("drm", THIS_MODULE, global);
		
		DRM(global) = global;
	}
	if ((ret = get_minor(pdev, ent))) {
		if (global)
			goto err_p3;
		return ret;
	}
	return 0;
err_p3:
	class_simple_destroy(global->drm_class);
err_p2:
	unregister_chrdev(DRM_MAJOR, "drm");
	DRM(free)(global->minors, sizeof(*global->minors) * global->cards_limit, DRM_MEM_STUB);
err_p1:	
	DRM(free)(global, sizeof(*global), DRM_MEM_STUB);
	DRM(global) = NULL;
	return ret;
}
