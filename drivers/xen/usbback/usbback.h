/*
 * usbback.h
 *
 * This file is part of Xen USB backend driver.
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

#ifndef __XEN_USBBACK_H__
#define __XEN_USBBACK_H__

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/vmalloc.h>
#include <linux/kthread.h>
#include <linux/wait.h>
#include <linux/list.h>
#include <linux/kref.h>
#include <xen/evtchn.h>
#include <xen/gnttab.h>
#include <xen/driver_util.h>
#include <xen/interface/xen.h>
#include <xen/xenbus.h>
#include <xen/interface/io/usbif.h>

struct usbstub;

#ifndef BUS_ID_SIZE
#define USBBACK_BUS_ID_SIZE 20
#else
#define USBBACK_BUS_ID_SIZE BUS_ID_SIZE
#endif

#define USB_DEV_ADDR_SIZE 128

typedef struct usbif_st {
	domid_t domid;
	unsigned int handle;
	int num_ports;
	enum usb_spec_version usb_ver;

	struct xenbus_device *xbdev;
	struct list_head usbif_list;

	unsigned int      irq;

	usbif_urb_back_ring_t urb_ring;
	usbif_conn_back_ring_t conn_ring;
	struct vm_struct *urb_ring_area;
	struct vm_struct *conn_ring_area;

	spinlock_t urb_ring_lock;
	spinlock_t conn_ring_lock;
	atomic_t refcnt;

	grant_handle_t urb_shmem_handle;
	grant_ref_t urb_shmem_ref;
	grant_handle_t conn_shmem_handle;
	grant_ref_t conn_shmem_ref;

	struct xenbus_watch backend_watch;

	/* device address lookup table */
	struct usbstub *addr_table[USB_DEV_ADDR_SIZE];
	spinlock_t addr_lock;

	/* connected device list */
	struct list_head stub_list;
	spinlock_t stub_lock;

	/* request schedule */
	struct task_struct *xenusbd;
	unsigned int waiting_reqs;
	wait_queue_head_t waiting_to_free;
	wait_queue_head_t wq;
} usbif_t;

struct vusb_port_id {
	struct list_head id_list;

	char phys_bus[USBBACK_BUS_ID_SIZE];
	domid_t domid;
	unsigned int handle;
	int portnum;
	unsigned is_connected:1;
};

struct usbstub {
	struct kref kref;
	struct list_head dev_list;

	struct vusb_port_id *portid;
	struct usb_device *udev;
	usbif_t *usbif;
	int addr;

	struct list_head submitting_list;
	spinlock_t submitting_lock;
};

usbif_t *usbif_alloc(domid_t domid, unsigned int handle);
void usbif_disconnect(usbif_t *usbif);
void usbif_free(usbif_t *usbif);
int usbif_map(usbif_t *usbif, unsigned long urb_ring_ref,
		unsigned long conn_ring_ref, unsigned int evtchn);

#define usbif_get(_b) (atomic_inc(&(_b)->refcnt))
#define usbif_put(_b) \
	do { \
		if (atomic_dec_and_test(&(_b)->refcnt)) \
			wake_up(&(_b)->waiting_to_free); \
	} while (0)

usbif_t *find_usbif(domid_t domid, unsigned int handle);
int usbback_xenbus_init(void);
void usbback_xenbus_exit(void);
struct vusb_port_id *find_portid_by_busid(const char *busid);
struct vusb_port_id *find_portid(const domid_t domid,
						const unsigned int handle,
						const int portnum);
int portid_add(const char *busid,
					const domid_t domid,
					const unsigned int handle,
					const int portnum);
int portid_remove(const domid_t domid,
					const unsigned int handle,
					const int portnum);
irqreturn_t usbbk_be_int(int irq, void *dev_id);
int usbbk_schedule(void *arg);
struct usbstub *find_attached_device(usbif_t *usbif, int port);
void usbbk_attach_device(usbif_t *usbif, struct usbstub *stub);
void usbbk_detach_device(usbif_t *usbif, struct usbstub *stub);
void usbbk_hotplug_notify(usbif_t *usbif, int portnum, int speed);
void detach_device_without_lock(usbif_t *usbif, struct usbstub *stub);
void usbbk_unlink_urbs(struct usbstub *stub);

int usbstub_init(void);
void usbstub_exit(void);

#endif /* __XEN_USBBACK_H__ */
