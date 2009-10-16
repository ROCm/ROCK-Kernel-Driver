/*
 * usbfront.h
 *
 * This file is part of Xen USB Virtual Host Controller driver.
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

#ifndef __XEN_USBFRONT_H__
#define __XEN_USBFRONT_H__

#include <linux/module.h>
#include <linux/usb.h>
#include <linux/list.h>
#include <linux/kthread.h>
#include <linux/wait.h>
#include <asm/io.h>
#include <xen/xenbus.h>
#include <xen/evtchn.h>
#include <xen/gnttab.h>
#include <xen/interface/xen.h>
#include <xen/interface/io/usbif.h>

/*
 * usbfront needs USB HCD headers,
 * drivers/usb/core/hcd.h and drivers/usb/core/hub.h,
 * but, they are not in public include path.
 */
#include "../../usb/core/hcd.h"
#include "../../usb/core/hub.h"

static inline struct usbfront_info *hcd_to_info(struct usb_hcd *hcd)
{
	return (struct usbfront_info *) (hcd->hcd_priv);
}

static inline struct usb_hcd *info_to_hcd(struct usbfront_info *info)
{
	return container_of((void *) info, struct usb_hcd, hcd_priv);
}

/* Private per-URB data */
struct urb_priv {
	struct list_head list;
	struct urb *urb;
	int req_id;	/* RING_REQUEST id for submitting */
	int unlink_req_id; /* RING_REQUEST id for unlinking */
	int status;
	unsigned unlinked:1; /* dequeued marker */
};

/* virtual roothub port status */
struct rhport_status {
	u32 status;
	unsigned resuming:1; /* in resuming */
	unsigned c_connection:1; /* connection changed */
	unsigned long timeout;
};

/* status of attached device */
struct vdevice_status {
	int devnum;
	enum usb_device_state status;
	enum usb_device_speed speed;
};

/* RING request shadow */
struct usb_shadow {
	usbif_urb_request_t req;
	struct urb *urb;
};

/* statistics for tuning, monitoring, ... */
struct xenhcd_stats {
	unsigned long ring_full; /* RING_FULL conditions */
	unsigned long complete; /* normal givebacked urbs */
	unsigned long unlink; /* unlinked urbs */
};

struct usbfront_info {
	/* Virtual Host Controller has 4 urb queues */
	struct list_head pending_submit_list;
	struct list_head pending_unlink_list;
	struct list_head in_progress_list;
	struct list_head giveback_waiting_list;

	spinlock_t lock;

	/* timer that kick pending and giveback waiting urbs */
	struct timer_list watchdog;
	unsigned long actions;

	/* virtual root hub */
	int rh_numports;
	struct rhport_status ports[USB_MAXCHILDREN];
	struct vdevice_status devices[USB_MAXCHILDREN];

	/* Xen related staff */
	struct xenbus_device *xbdev;
	int urb_ring_ref;
	int conn_ring_ref;
	usbif_urb_front_ring_t urb_ring;
	usbif_conn_front_ring_t conn_ring;

	unsigned int irq; /* event channel */
	struct usb_shadow shadow[USB_URB_RING_SIZE];
	unsigned long shadow_free;

	/* RING_RESPONSE thread */
	struct task_struct *kthread;
	wait_queue_head_t wq;
	unsigned int waiting_resp;

	/* xmit statistics */
#ifdef XENHCD_STATS
	struct xenhcd_stats stats;
#define COUNT(x) do { (x)++; } while (0)
#else
#define COUNT(x) do {} while (0)
#endif
};

#define XENHCD_RING_JIFFIES (HZ/200)
#define XENHCD_SCAN_JIFFIES 1

enum xenhcd_timer_action {
	TIMER_RING_WATCHDOG,
	TIMER_SCAN_PENDING_URBS,
};

static inline void
timer_action_done(struct usbfront_info *info, enum xenhcd_timer_action action)
{
	clear_bit(action, &info->actions);
}

static inline void
timer_action(struct usbfront_info *info, enum xenhcd_timer_action action)
{
	if (timer_pending(&info->watchdog)
			&& test_bit(TIMER_SCAN_PENDING_URBS, &info->actions))
		return;

	if (!test_and_set_bit(action, &info->actions)) {
		unsigned long t;

		switch (action) {
		case TIMER_RING_WATCHDOG:
			t = XENHCD_RING_JIFFIES;
			break;
		default:
			t = XENHCD_SCAN_JIFFIES;
			break;
		}
		mod_timer(&info->watchdog, t + jiffies);
	}
}

extern struct kmem_cache *xenhcd_urbp_cachep;
extern struct hc_driver xen_usb20_hc_driver;
extern struct hc_driver xen_usb11_hc_driver;
irqreturn_t xenhcd_int(int irq, void *dev_id);
void xenhcd_rhport_state_change(struct usbfront_info *info,
				int port, enum usb_device_speed speed);
int xenhcd_schedule(void *arg);

#endif /* __XEN_USBFRONT_H__ */
