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

#define DRM_STUB_MAXCARDS 16	/* Enough for one machine */

/** Stub list. One for each minor. */
static struct drm_stub_list {
	const char             *name;
	struct file_operations *fops;	/**< file operations */
	struct proc_dir_entry  *dev_root;	/**< proc directory entry */
} *DRM(stub_list);

static struct proc_dir_entry *DRM(stub_root);

/** Stub information */
static struct drm_stub_info {
	int (*info_register)(const char *name, struct file_operations *fops,
			     drm_device_t *dev);
	int (*info_unregister)(int minor);
} DRM(stub_info);

/**
 * File \c open operation.
 *
 * \param inode device inode.
 * \param filp file pointer.
 *
 * Puts the drm_stub_list::fops corresponding to the device minor number into
 * \p filp, call the \c open method, and restore the file operations.
 */
static int DRM(stub_open)(struct inode *inode, struct file *filp)
{
	int                    minor = iminor(inode);
	int                    err   = -ENODEV;
	struct file_operations *old_fops;

	if (!DRM(stub_list) || !DRM(stub_list)[minor].fops) return -ENODEV;
	old_fops   = filp->f_op;
	filp->f_op = fops_get(DRM(stub_list)[minor].fops);
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
	.open  = DRM(stub_open)
};

/**
 * Get a device minor number.
 *
 * \param name driver name.
 * \param fops file operations.
 * \param dev DRM device.
 * \return minor number on success, or a negative number on failure.
 *
 * Allocate and initialize ::stub_list if one doesn't exist already.  Search an
 * empty entry and initialize it to the given parameters, and create the proc
 * init entry via proc_init().
 */
static int DRM(stub_getminor)(const char *name, struct file_operations *fops,
			      drm_device_t *dev)
{
	int i;

	if (!DRM(stub_list)) {
		DRM(stub_list) = DRM(alloc)(sizeof(*DRM(stub_list))
					    * DRM_STUB_MAXCARDS, DRM_MEM_STUB);
		if(!DRM(stub_list)) return -1;
		for (i = 0; i < DRM_STUB_MAXCARDS; i++) {
			DRM(stub_list)[i].name = NULL;
			DRM(stub_list)[i].fops = NULL;
		}
	}
	for (i = 0; i < DRM_STUB_MAXCARDS; i++) {
		if (!DRM(stub_list)[i].fops) {
			DRM(stub_list)[i].name = name;
			DRM(stub_list)[i].fops = fops;
			DRM(stub_root) = DRM(proc_init)(dev, i, DRM(stub_root),
							&DRM(stub_list)[i]
							.dev_root);
			return i;
		}
	}
	return -1;
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
static int DRM(stub_putminor)(int minor)
{
	if (minor < 0 || minor >= DRM_STUB_MAXCARDS) return -1;
	DRM(stub_list)[minor].name = NULL;
	DRM(stub_list)[minor].fops = NULL;
	DRM(proc_cleanup)(minor, DRM(stub_root),
			  DRM(stub_list)[minor].dev_root);
	if (minor) {
		inter_module_put("drm");
	} else {
		inter_module_unregister("drm");
		DRM(free)(DRM(stub_list),
			  sizeof(*DRM(stub_list)) * DRM_STUB_MAXCARDS,
			  DRM_MEM_STUB);
		unregister_chrdev(DRM_MAJOR, "drm");
	}
	return 0;
}

/**
 * Register.
 *
 * \param name driver name.
 * \param fops file operations
 * \param dev DRM device.
 * \return zero on success or a negative number on failure.
 *
 * Attempt to register the char device and get the foreign "drm" data. If
 * successful then another module already registered so gets the stub info,
 * otherwise use this module stub info and make it available for other modules.
 *
 * Finally calls stub_info::info_register.
 */
int DRM(stub_register)(const char *name, struct file_operations *fops,
		       drm_device_t *dev)
{
	struct drm_stub_info *i = NULL;

	DRM_DEBUG("\n");
	if (register_chrdev(DRM_MAJOR, "drm", &DRM(stub_fops)))
		i = (struct drm_stub_info *)inter_module_get("drm");

	if (i) {
				/* Already registered */
		DRM(stub_info).info_register   = i->info_register;
		DRM(stub_info).info_unregister = i->info_unregister;
		DRM_DEBUG("already registered\n");
	} else if (DRM(stub_info).info_register != DRM(stub_getminor)) {
		DRM(stub_info).info_register   = DRM(stub_getminor);
		DRM(stub_info).info_unregister = DRM(stub_putminor);
		DRM_DEBUG("calling inter_module_register\n");
		inter_module_register("drm", THIS_MODULE, &DRM(stub_info));
	}
	if (DRM(stub_info).info_register)
		return DRM(stub_info).info_register(name, fops, dev);
	return -1;
}

/**
 * Unregister.
 *
 * \param minor
 *
 * Calls drm_stub_info::unregister.
 */
int DRM(stub_unregister)(int minor)
{
	DRM_DEBUG("%d\n", minor);
	if (DRM(stub_info).info_unregister)
		return DRM(stub_info).info_unregister(minor);
	return -1;
}
