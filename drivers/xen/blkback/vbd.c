/******************************************************************************
 * blkback/vbd.c
 * 
 * Routines for managing virtual block devices (VBDs).
 * 
 * Copyright (c) 2003-2005, Keir Fraser & Steve Hand
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "common.h"

#define vbd_sz(_v)   ((_v)->bdev->bd_part ?				\
	(_v)->bdev->bd_part->nr_sects : get_capacity((_v)->bdev->bd_disk))

unsigned long long vbd_size(struct vbd *vbd)
{
	return vbd->bdev ? vbd_sz(vbd) : 0;
}

unsigned long vbd_secsize(struct vbd *vbd)
{
	return vbd->bdev ? bdev_logical_block_size(vbd->bdev) : 0;
}

int vbd_create(blkif_t *blkif, blkif_vdev_t handle, unsigned major,
	       unsigned minor, fmode_t mode, bool cdrom)
{
	struct vbd *vbd;
	struct block_device *bdev;
	struct request_queue *q;

	vbd = &blkif->vbd;
	vbd->handle   = handle; 
	vbd->size     = 0;
	vbd->type     = cdrom ? VDISK_CDROM : 0;

	if (!(mode & FMODE_WRITE)) {
		mode &= ~FMODE_EXCL; /* xend doesn't even allow mode="r!" */
		vbd->type |= VDISK_READONLY;
	}
	vbd->mode = mode;

	vbd->pdevice  = MKDEV(major, minor);

	bdev = blkdev_get_by_dev(vbd->pdevice, mode, blkif);

	if (IS_ERR(bdev)) {
		if (PTR_ERR(bdev) != -ENOMEDIUM) {
			DPRINTK("vbd_creat: device %08x could not be opened\n",
				vbd->pdevice);
			return -ENOENT;
		}

		DPRINTK("vbd_creat: device %08x has no medium\n",
			vbd->pdevice);
		if (cdrom)
			return -ENOMEDIUM;

		bdev = blkdev_get_by_dev(vbd->pdevice, mode | FMODE_NDELAY,
					 blkif);
		if (IS_ERR(bdev))
			return -ENOMEDIUM;

		if (bdev->bd_disk) {
			if (bdev->bd_disk->flags & GENHD_FL_CD)
				vbd->type |= VDISK_CDROM;
			if (bdev->bd_disk->flags & GENHD_FL_REMOVABLE)
				vbd->type |= VDISK_REMOVABLE;
		}

		blkdev_put(bdev, mode);
		return -ENOMEDIUM;
	}

	vbd->bdev = bdev;

	if (vbd->bdev->bd_disk == NULL) {
		DPRINTK("vbd_creat: device %08x doesn't exist.\n",
			vbd->pdevice);
		vbd_free(vbd);
		return -ENOENT;
	}

	vbd->size = vbd_size(vbd);

	if (bdev->bd_disk->flags & GENHD_FL_CD)
		vbd->type |= VDISK_CDROM;
	if (vbd->bdev->bd_disk->flags & GENHD_FL_REMOVABLE)
		vbd->type |= VDISK_REMOVABLE;

	q = bdev_get_queue(bdev);
	if (q && q->flush_flags)
		vbd->flush_support = true;

	if (q && blk_queue_secdiscard(q))
		vbd->discard_secure = true;

	DPRINTK("Successful creation of handle=%04x (dom=%u)\n",
		handle, blkif->domid);
	return 0;
}

void vbd_free(struct vbd *vbd)
{
	if (vbd->bdev)
		blkdev_put(vbd->bdev, vbd->mode);
	vbd->bdev = NULL;
}

int vbd_translate(struct phys_req *req, blkif_t *blkif, int operation)
{
	struct vbd *vbd = &blkif->vbd;
	int rc = -EACCES;

	if ((operation != READ) && !(vbd->mode & FMODE_WRITE))
		goto out;

	if (vbd->bdev == NULL) {
		rc = -ENOMEDIUM;
		goto out;
	}

	if (likely(req->nr_sects)) {
		blkif_sector_t end = req->sector_number + req->nr_sects;

		if (unlikely(end < req->sector_number))
			goto out;
		if (unlikely(end > vbd_sz(vbd)))
			goto out;
	}

	req->dev  = vbd->pdevice;
	req->bdev = vbd->bdev;
	rc = 0;

 out:
	return rc;
}

void vbd_resize(blkif_t *blkif)
{
	struct vbd *vbd = &blkif->vbd;
	struct xenbus_transaction xbt;
	int err;
	struct xenbus_device *dev = blkif->be->dev;
	unsigned long long new_size = vbd_size(vbd);

	pr_info("VBD Resize: new size %Lu\n", new_size);
	vbd->size = new_size;
again:
	err = xenbus_transaction_start(&xbt);
	if (err) {
		pr_warning("Error %d starting transaction", err);
		return;
	}
	err = xenbus_printf(xbt, dev->nodename, "sectors", "%Lu",
			    vbd_size(vbd));
	if (err) {
		pr_warning("Error %d writing new size", err);
		goto abort;
	}

	err = xenbus_printf(xbt, dev->nodename, "sector-size", "%lu",
			    vbd_secsize(vbd));
	if (err) {
		pr_warning("Error writing new sector size");
		goto abort;
	}

	/*
	 * Write the current state; we will use this to synchronize
	 * the front-end. If the current state is "connected" the
	 * front-end will get the new size information online.
	 */
	err = xenbus_printf(xbt, dev->nodename, "state", "%d", dev->state);
	if (err) {
		pr_warning("Error %d writing the state", err);
		goto abort;
	}

	err = xenbus_transaction_end(xbt, 0);
	if (err == -EAGAIN)
		goto again;
	if (err)
		pr_warning("Error %d ending transaction", err);
	return;
abort:
	xenbus_transaction_end(xbt, 1);
}
