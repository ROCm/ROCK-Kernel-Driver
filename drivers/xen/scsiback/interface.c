/*
 * interface management.
 *
 * Copyright (c) 2008, FUJITSU Limited
 *
 * Based on the blkback driver code.
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

#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>
#include "common.h"

#include <xen/evtchn.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/vmalloc.h>


static struct kmem_cache *scsiback_cachep;

struct vscsibk_info *vscsibk_info_alloc(domid_t domid)
{
	struct vscsibk_info *info;

	info = kmem_cache_zalloc(scsiback_cachep, GFP_KERNEL);
	if (!info)
		return ERR_PTR(-ENOMEM);

	info->domid = domid;
	spin_lock_init(&info->ring_lock);
	atomic_set(&info->nr_unreplied_reqs, 0);
	init_waitqueue_head(&info->wq);
	init_waitqueue_head(&info->waiting_to_free);

	return info;
}

int scsiback_init_sring(struct vscsibk_info *info, grant_ref_t ring_ref,
			evtchn_port_t evtchn)
{
	struct vm_struct *area;
	struct vscsiif_sring *sring;
	int err;

	if (info->irq) {
		pr_err("scsiback: Already connected through?\n");
		return -1;
	}

	area = xenbus_map_ring_valloc(info->dev, ring_ref);
	if (IS_ERR(area))
		return PTR_ERR(area);
	info->ring_area = area;

	sring = (struct vscsiif_sring *)area->addr;
	BACK_RING_INIT(&info->ring, sring, PAGE_SIZE);

	err = bind_interdomain_evtchn_to_irqhandler(
			info->domid, evtchn,
			scsiback_intr, 0, "vscsiif-backend", info);

	if (err < 0)
		goto unmap_page;
		
	info->irq = err;

	return 0;

unmap_page:
	xenbus_unmap_ring_vfree(info->dev, area);

	return err;
}

void scsiback_disconnect(struct vscsibk_info *info)
{
	if (info->kthread) {
		kthread_stop(info->kthread);
		info->kthread = NULL;
	}

	wait_event(info->waiting_to_free, 
		atomic_read(&info->nr_unreplied_reqs) == 0);

	if (info->irq) {
		unbind_from_irqhandler(info->irq, info);
		info->irq = 0;
	}

	if (info->ring.sring) {
		xenbus_unmap_ring_vfree(info->dev, info->ring_area);
		info->ring.sring = NULL;
	}
}

void scsiback_free(struct vscsibk_info *info)
{
	kmem_cache_free(scsiback_cachep, info);
}

int __init scsiback_interface_init(void)
{
	scsiback_cachep = kmem_cache_create("vscsiif_cache",
		sizeof(struct vscsibk_info), 0, 0, NULL);
	if (!scsiback_cachep) {
		pr_err("scsiback: can't init scsi cache\n");
		return -ENOMEM;
	}
	
	return 0;
}

void scsiback_interface_exit(void)
{
	kmem_cache_destroy(scsiback_cachep);
}
