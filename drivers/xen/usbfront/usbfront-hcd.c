/*
 * usbfront-hcd.c
 *
 * Xen USB Virtual Host Controller driver
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

#include "usbfront.h"
#include "usbfront-dbg.c"
#include "usbfront-hub.c"
#include "usbfront-q.c"

static void xenhcd_watchdog(unsigned long param)
{
	struct usbfront_info *info = (struct usbfront_info *) param;
	unsigned long flags;

	spin_lock_irqsave(&info->lock, flags);
	if (likely(HC_IS_RUNNING(info_to_hcd(info)->state))) {
		timer_action_done(info, TIMER_RING_WATCHDOG);
		xenhcd_giveback_unlinked_urbs(info);
		xenhcd_kick_pending_urbs(info);
	}
	spin_unlock_irqrestore(&info->lock, flags);
}

/*
 * one-time HC init
 */
static int xenhcd_setup(struct usb_hcd *hcd)
{
	struct usbfront_info *info = hcd_to_info(hcd);

	spin_lock_init(&info->lock);
	INIT_LIST_HEAD(&info->pending_submit_list);
	INIT_LIST_HEAD(&info->pending_unlink_list);
	INIT_LIST_HEAD(&info->in_progress_list);
	INIT_LIST_HEAD(&info->giveback_waiting_list);
	init_timer(&info->watchdog);
	info->watchdog.function = xenhcd_watchdog;
	info->watchdog.data = (unsigned long) info;
	return 0;
}

/*
 * start HC running
 */
static int xenhcd_run(struct usb_hcd *hcd)
{
	hcd->uses_new_polling = 1;
	hcd->poll_rh = 0;
	hcd->state = HC_STATE_RUNNING;
	create_debug_file(hcd_to_info(hcd));
	return 0;
}

/*
 * stop running HC
 */
static void xenhcd_stop(struct usb_hcd *hcd)
{
	struct usbfront_info *info = hcd_to_info(hcd);

	del_timer_sync(&info->watchdog);
	remove_debug_file(info);
	spin_lock_irq(&info->lock);
	/* cancel all urbs */
	hcd->state = HC_STATE_HALT;
	xenhcd_cancel_all_enqueued_urbs(info);
	xenhcd_giveback_unlinked_urbs(info);
	spin_unlock_irq(&info->lock);
}

/*
 * called as .urb_enqueue()
 * non-error returns are promise to giveback the urb later
 */
static int xenhcd_urb_enqueue(struct usb_hcd *hcd,
				    struct urb *urb,
				    gfp_t mem_flags)
{
	struct usbfront_info *info = hcd_to_info(hcd);
	struct urb_priv *urbp;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&info->lock, flags);

	urbp = alloc_urb_priv(urb);
	if (!urbp) {
		ret = -ENOMEM;
		goto done;
	}
	urbp->status = 1;

	ret = xenhcd_submit_urb(info, urbp);
	if (ret != 0)
		free_urb_priv(urbp);

done:
	spin_unlock_irqrestore(&info->lock, flags);
	return ret;
}

/*
 * called as .urb_dequeue()
 */
static int xenhcd_urb_dequeue(struct usb_hcd *hcd,
			      struct urb *urb, int status)
{
	struct usbfront_info *info = hcd_to_info(hcd);
	struct urb_priv *urbp;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&info->lock, flags);

	urbp = urb->hcpriv;
	if (!urbp)
		goto done;

	urbp->status = status;
	ret = xenhcd_unlink_urb(info, urbp);

done:
	spin_unlock_irqrestore(&info->lock, flags);
	return ret;
}

/*
 * called from usb_get_current_frame_number(),
 * but, almost all drivers not use such function.
 */
static int xenhcd_get_frame(struct usb_hcd *hcd)
{
	/* it means error, but probably no problem :-) */
	return 0;
}

static const char hcd_name[] = "xen_hcd";

struct hc_driver xen_usb20_hc_driver = {
	.description = hcd_name,
	.product_desc = "Xen USB2.0 Virtual Host Controller",
	.hcd_priv_size = sizeof(struct usbfront_info),
	.flags = HCD_USB2,

	/* basic HC lifecycle operations */
	.reset = xenhcd_setup,
	.start = xenhcd_run,
	.stop = xenhcd_stop,

	/* managing urb I/O */
	.urb_enqueue = xenhcd_urb_enqueue,
	.urb_dequeue = xenhcd_urb_dequeue,
	.get_frame_number = xenhcd_get_frame,

	/* root hub operations */
	.hub_status_data = xenhcd_hub_status_data,
	.hub_control = xenhcd_hub_control,
#ifdef XENHCD_PM
#ifdef CONFIG_PM
	.bus_suspend = xenhcd_bus_suspend,
	.bus_resume = xenhcd_bus_resume,
#endif
#endif
};

struct hc_driver xen_usb11_hc_driver = {
	.description = hcd_name,
	.product_desc = "Xen USB1.1 Virtual Host Controller",
	.hcd_priv_size = sizeof(struct usbfront_info),
	.flags = HCD_USB11,

	/* basic HC lifecycle operations */
	.reset = xenhcd_setup,
	.start = xenhcd_run,
	.stop = xenhcd_stop,

	/* managing urb I/O */
	.urb_enqueue = xenhcd_urb_enqueue,
	.urb_dequeue = xenhcd_urb_dequeue,
	.get_frame_number = xenhcd_get_frame,

	/* root hub operations */
	.hub_status_data = xenhcd_hub_status_data,
	.hub_control = xenhcd_hub_control,
#ifdef XENHCD_PM
#ifdef CONFIG_PM
	.bus_suspend = xenhcd_bus_suspend,
	.bus_resume = xenhcd_bus_resume,
#endif
#endif
};
