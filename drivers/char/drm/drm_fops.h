/**
 * \file drm_fops.h 
 * File operations for DRM
 * 
 * \author Rickard E. (Rik) Faith <faith@valinux.com>
 * \author Daryll Strauss <daryll@valinux.com>
 * \author Gareth Hughes <gareth@valinux.com>
 */

/*
 * Created: Mon Jan  4 08:58:31 1999 by faith@valinux.com
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
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
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "drmP.h"
#include <linux/poll.h>


/**
 * Called whenever a process opens /dev/drm. 
 *
 * \param inode device inode.
 * \param filp file pointer.
 * \param dev device.
 * \return zero on success or a negative number on failure.
 * 
 * Creates and initializes a drm_file structure for the file private data in \p
 * filp and add it into the double linked list in \p dev.
 */
int DRM(open_helper)(struct inode *inode, struct file *filp, drm_device_t *dev)
{
	int	     minor = iminor(inode);
	drm_file_t   *priv;

	if (filp->f_flags & O_EXCL)   return -EBUSY; /* No exclusive opens */
	if (!DRM(cpu_valid)())        return -EINVAL;

	DRM_DEBUG("pid = %d, minor = %d\n", current->pid, minor);

	priv		    = DRM(alloc)(sizeof(*priv), DRM_MEM_FILES);
	if(!priv) return -ENOMEM;

	memset(priv, 0, sizeof(*priv));
	filp->private_data  = priv;
	priv->uid	    = current->euid;
	priv->pid	    = current->pid;
	priv->minor	    = minor;
	priv->dev	    = dev;
	priv->ioctl_count   = 0;
	priv->authenticated = capable(CAP_SYS_ADMIN);
	priv->lock_count    = 0;

	DRIVER_OPEN_HELPER( priv, dev );

	down(&dev->struct_sem);
	if (!dev->file_last) {
		priv->next	= NULL;
		priv->prev	= NULL;
		dev->file_first = priv;
		dev->file_last	= priv;
	} else {
		priv->next	     = NULL;
		priv->prev	     = dev->file_last;
		dev->file_last->next = priv;
		dev->file_last	     = priv;
	}
	up(&dev->struct_sem);

#ifdef __alpha__
	/*
	 * Default the hose
	 */
	if (!dev->hose) {
		struct pci_dev *pci_dev;
		pci_dev = pci_find_class(PCI_CLASS_DISPLAY_VGA << 8, NULL);
		if (pci_dev) dev->hose = pci_dev->sysdata;
		if (!dev->hose) {
			struct pci_bus *b = pci_bus_b(pci_root_buses.next);
			if (b) dev->hose = b->sysdata;
		}
	}
#endif

	return 0;
}

/** No-op. */
int DRM(flush)(struct file *filp)
{
	drm_file_t    *priv   = filp->private_data;
	drm_device_t  *dev    = priv->dev;

	DRM_DEBUG("pid = %d, device = 0x%lx, open_count = %d\n",
		  current->pid, (long)old_encode_dev(dev->device), dev->open_count);
	return 0;
}

/** No-op. */
int DRM(fasync)(int fd, struct file *filp, int on)
{
	drm_file_t    *priv   = filp->private_data;
	drm_device_t  *dev    = priv->dev;
	int	      retcode;

	DRM_DEBUG("fd = %d, device = 0x%lx\n", fd, (long)old_encode_dev(dev->device));
	retcode = fasync_helper(fd, filp, on, &dev->buf_async);
	if (retcode < 0) return retcode;
	return 0;
}

#if !__HAVE_DRIVER_FOPS_POLL
/** No-op. */
unsigned int DRM(poll)(struct file *filp, struct poll_table_struct *wait)
{
	return 0;
}
#endif


#if !__HAVE_DRIVER_FOPS_READ
/** No-op. */
ssize_t DRM(read)(struct file *filp, char __user *buf, size_t count, loff_t *off)
{
	return 0;
}
#endif
