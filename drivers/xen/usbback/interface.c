/*
 * interface.c
 *
 * Xen USB backend interface management.
 *
 * Copyright (C) 2009, FUJITSU LABORATORIES LTD.
 * Author: Noboru Iwamatsu <n_iwamatsu@jp.fujitsu.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * or, by your choice,
 *
 * When distributed separately from the Linux kernel or incorporated into
 * other software packages, subject to the following license:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "usbback.h"

static LIST_HEAD(usbif_list);
static DEFINE_SPINLOCK(usbif_list_lock);

usbif_t *find_usbif(int dom_id, int dev_id)
{
	usbif_t *usbif;
	int found = 0;
	unsigned long flags;

	spin_lock_irqsave(&usbif_list_lock, flags);
	list_for_each_entry(usbif, &usbif_list, usbif_list) {
		if (usbif->domid == dom_id
			&& usbif->handle == dev_id) {
			found = 1;
			break;
		}
	}
	spin_unlock_irqrestore(&usbif_list_lock, flags);

	if (found)
		return usbif;

	return NULL;
}

usbif_t *usbif_alloc(domid_t domid, unsigned int handle)
{
	usbif_t *usbif;
	unsigned long flags;
	int i;

	usbif = kzalloc(sizeof(usbif_t), GFP_KERNEL);
	if (!usbif)
		return NULL;

	usbif->domid = domid;
	usbif->handle = handle;
	spin_lock_init(&usbif->ring_lock);
	atomic_set(&usbif->refcnt, 0);
	init_waitqueue_head(&usbif->wq);
	init_waitqueue_head(&usbif->waiting_to_free);
	spin_lock_init(&usbif->plug_lock);
	INIT_LIST_HEAD(&usbif->plugged_devices);
	spin_lock_init(&usbif->addr_lock);
	for (i = 0; i < USB_DEV_ADDR_SIZE; i++) {
		usbif->addr_table[i] = NULL;
	}

	spin_lock_irqsave(&usbif_list_lock, flags);
	list_add(&usbif->usbif_list, &usbif_list);
	spin_unlock_irqrestore(&usbif_list_lock, flags);

	return usbif;
}

static int map_frontend_page(usbif_t *usbif, unsigned long shared_page)
{
	struct gnttab_map_grant_ref op;

	gnttab_set_map_op(&op, (unsigned long)usbif->ring_area->addr,
			  GNTMAP_host_map, shared_page, usbif->domid);

	if (HYPERVISOR_grant_table_op(GNTTABOP_map_grant_ref, &op, 1))
		BUG();

	if (op.status) {
		printk(KERN_ERR "grant table operation failure\n");
		return op.status;
	}

	usbif->shmem_ref = shared_page;
	usbif->shmem_handle = op.handle;

	return 0;
}

static void unmap_frontend_page(usbif_t *usbif)
{
	struct gnttab_unmap_grant_ref op;

	gnttab_set_unmap_op(&op, (unsigned long)usbif->ring_area->addr,
			    GNTMAP_host_map, usbif->shmem_handle);

	if (HYPERVISOR_grant_table_op(GNTTABOP_unmap_grant_ref, &op, 1))
		BUG();
}

int usbif_map(usbif_t *usbif, unsigned long shared_page, unsigned int evtchn)
{
	int err;
	usbif_sring_t *sring;

	if (usbif->irq)
		return 0;

	if ((usbif->ring_area = alloc_vm_area(PAGE_SIZE)) == NULL)
		return -ENOMEM;

	err = map_frontend_page(usbif, shared_page);
	if (err) {
		free_vm_area(usbif->ring_area);
		return err;
	}

	sring = (usbif_sring_t *) usbif->ring_area->addr;
	BACK_RING_INIT(&usbif->ring, sring, PAGE_SIZE);

	err = bind_interdomain_evtchn_to_irqhandler(
			usbif->domid, evtchn, usbbk_be_int, 0, "usbif-backend", usbif);
	if (err < 0)
	{
		unmap_frontend_page(usbif);
		free_vm_area(usbif->ring_area);
		usbif->ring.sring = NULL;
		return err;
	}
	usbif->irq = err;

	return 0;
}

void usbif_disconnect(usbif_t *usbif)
{
	struct usbstub *stub, *tmp;
	unsigned long flags;

	if (usbif->xenusbd) {
		kthread_stop(usbif->xenusbd);
		usbif->xenusbd = NULL;
	}

	spin_lock_irqsave(&usbif->plug_lock, flags);
	list_for_each_entry_safe(stub, tmp, &usbif->plugged_devices, plugged_list) {
		usbbk_unlink_urbs(stub);
		detach_device_without_lock(usbif, stub);
	}
	spin_unlock_irqrestore(&usbif->plug_lock, flags);

	wait_event(usbif->waiting_to_free, atomic_read(&usbif->refcnt) == 0);

	if (usbif->irq) {
		unbind_from_irqhandler(usbif->irq, usbif);
		usbif->irq = 0;
	}

	if (usbif->ring.sring) {
		unmap_frontend_page(usbif);
		free_vm_area(usbif->ring_area);
		usbif->ring.sring = NULL;
	}
}

void usbif_free(usbif_t *usbif)
{
	unsigned long flags;

	spin_lock_irqsave(&usbif_list_lock, flags);
	list_del(&usbif->usbif_list);
	spin_unlock_irqrestore(&usbif_list_lock, flags);
	kfree(usbif);
}
