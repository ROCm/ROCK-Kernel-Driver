/*
 * xenbus.c
 *
 * Xenbus interface for Xen USB Virtual Host Controller
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

#define GRANT_INVALID_REF 0

static void destroy_rings(struct usbfront_info *info)
{
	if (info->irq)
		unbind_from_irqhandler(info->irq, info);
	info->irq = 0;

	if (info->urb_ring_ref != GRANT_INVALID_REF) {
		gnttab_end_foreign_access(info->urb_ring_ref,
					  (unsigned long)info->urb_ring.sring);
		info->urb_ring_ref = GRANT_INVALID_REF;
	}
	info->urb_ring.sring = NULL;

	if (info->conn_ring_ref != GRANT_INVALID_REF) {
		gnttab_end_foreign_access(info->conn_ring_ref,
					  (unsigned long)info->conn_ring.sring);
		info->conn_ring_ref = GRANT_INVALID_REF;
	}
	info->conn_ring.sring = NULL;
}

static int setup_rings(struct xenbus_device *dev,
			   struct usbfront_info *info)
{
	usbif_urb_sring_t *urb_sring;
	usbif_conn_sring_t *conn_sring;
	int err;

	info->urb_ring_ref = GRANT_INVALID_REF;
	info->conn_ring_ref = GRANT_INVALID_REF;

	urb_sring = (usbif_urb_sring_t *)get_zeroed_page(GFP_NOIO|__GFP_HIGH);
	if (!urb_sring) {
		xenbus_dev_fatal(dev, -ENOMEM, "allocating urb ring");
		return -ENOMEM;
	}
	SHARED_RING_INIT(urb_sring);
	FRONT_RING_INIT(&info->urb_ring, urb_sring, PAGE_SIZE);

	err = xenbus_grant_ring(dev, virt_to_mfn(info->urb_ring.sring));
	if (err < 0) {
		free_page((unsigned long)urb_sring);
		info->urb_ring.sring = NULL;
		goto fail;
	}
	info->urb_ring_ref = err;

	conn_sring = (usbif_conn_sring_t *)get_zeroed_page(GFP_NOIO|__GFP_HIGH);
	if (!conn_sring) {
		xenbus_dev_fatal(dev, -ENOMEM, "allocating conn ring");
		return -ENOMEM;
	}
	SHARED_RING_INIT(conn_sring);
	FRONT_RING_INIT(&info->conn_ring, conn_sring, PAGE_SIZE);

	err = xenbus_grant_ring(dev, virt_to_mfn(info->conn_ring.sring));
	if (err < 0) {
		free_page((unsigned long)conn_sring);
		info->conn_ring.sring = NULL;
		goto fail;
	}
	info->conn_ring_ref = err;

	err = bind_listening_port_to_irqhandler(
		dev->otherend_id, xenhcd_int, IRQF_SAMPLE_RANDOM, "usbif", info);
	if (err <= 0) {
		xenbus_dev_fatal(dev, err,
				 "bind_listening_port_to_irqhandler");
		goto fail;
	}
	info->irq = err;

	return 0;
fail:
	destroy_rings(info);
	return err;
}

static int talk_to_backend(struct xenbus_device *dev,
			   struct usbfront_info *info)
{
	const char *message;
	struct xenbus_transaction xbt;
	int err;

	err = setup_rings(dev, info);
	if (err)
		goto out;

again:
	err = xenbus_transaction_start(&xbt);
	if (err) {
		xenbus_dev_fatal(dev, err, "starting transaction");
		goto destroy_ring;
	}

	err = xenbus_printf(xbt, dev->nodename, "urb-ring-ref", "%u",
			    info->urb_ring_ref);
	if (err) {
		message = "writing urb-ring-ref";
		goto abort_transaction;
	}

	err = xenbus_printf(xbt, dev->nodename, "conn-ring-ref", "%u",
			    info->conn_ring_ref);
	if (err) {
		message = "writing conn-ring-ref";
		goto abort_transaction;
	}

	err = xenbus_printf(xbt, dev->nodename, "event-channel", "%u",
			    irq_to_evtchn_port(info->irq));
	if (err) {
		message = "writing event-channel";
		goto abort_transaction;
	}

	err = xenbus_transaction_end(xbt, 0);
	if (err) {
		if (err == -EAGAIN)
			goto again;
		xenbus_dev_fatal(dev, err, "completing transaction");
		goto destroy_ring;
	}

	return 0;

abort_transaction:
	xenbus_transaction_end(xbt, 1);
	xenbus_dev_fatal(dev, err, "%s", message);

destroy_ring:
	destroy_rings(info);

out:
	return err;
}

static int connect(struct xenbus_device *dev)
{
	struct usbfront_info *info = dev_get_drvdata(&dev->dev);

	usbif_conn_request_t *req;
	int i, idx, err;
	int notify;
	char name[TASK_COMM_LEN];
	struct usb_hcd *hcd;

	hcd = info_to_hcd(info);
	snprintf(name, TASK_COMM_LEN, "xenhcd.%d", hcd->self.busnum);

	err = talk_to_backend(dev, info);
	if (err)
		return err;

	info->kthread = kthread_run(xenhcd_schedule, info, name);
	if (IS_ERR(info->kthread)) {
		err = PTR_ERR(info->kthread);
		info->kthread = NULL;
		xenbus_dev_fatal(dev, err, "Error creating thread");
		return err;
	}
	/* prepare ring for hotplug notification */
	for (idx = 0, i = 0; i < USB_CONN_RING_SIZE; i++) {
		req = RING_GET_REQUEST(&info->conn_ring, idx);
		req->id = idx;
		idx++;
	}
	info->conn_ring.req_prod_pvt = idx;

	RING_PUSH_REQUESTS_AND_CHECK_NOTIFY(&info->conn_ring, notify);
	if (notify)
		notify_remote_via_irq(info->irq);

	return 0;
}

static struct usb_hcd *create_hcd(struct xenbus_device *dev)
{
	int i;
	int err = 0;
	int num_ports;
	int usb_ver;
	struct usb_hcd *hcd = NULL;
	struct usbfront_info *info = NULL;

	err = xenbus_scanf(XBT_NIL, dev->otherend,
					"num-ports", "%d", &num_ports);
	if (err != 1) {
		xenbus_dev_fatal(dev, err, "reading num-ports");
		return ERR_PTR(-EINVAL);
	}
	if (num_ports < 1 || num_ports > USB_MAXCHILDREN) {
		xenbus_dev_fatal(dev, err, "invalid num-ports");
		return ERR_PTR(-EINVAL);
	}

	err = xenbus_scanf(XBT_NIL, dev->otherend,
					"usb-ver", "%d", &usb_ver);
	if (err != 1) {
		xenbus_dev_fatal(dev, err, "reading usb-ver");
		return ERR_PTR(-EINVAL);
	}
	switch (usb_ver) {
	case USB_VER_USB11:
		hcd = usb_create_hcd(&xen_usb11_hc_driver, &dev->dev, dev_name(&dev->dev));
		break;
	case USB_VER_USB20:
		hcd = usb_create_hcd(&xen_usb20_hc_driver, &dev->dev, dev_name(&dev->dev));
		break;
	default:
		xenbus_dev_fatal(dev, err, "invalid usb-ver");
		return ERR_PTR(-EINVAL);
	}
	if (!hcd) {
		xenbus_dev_fatal(dev, err,
				"fail to allocate USB host controller");
		return ERR_PTR(-ENOMEM);
	}

	info = hcd_to_info(hcd);
	info->xbdev = dev;
	info->rh_numports = num_ports;

	for (i = 0; i < USB_URB_RING_SIZE; i++) {
		info->shadow[i].req.id = i + 1;
		info->shadow[i].urb = NULL;
	}
	info->shadow[USB_URB_RING_SIZE-1].req.id = 0x0fff;

	return hcd;
}

static int usbfront_probe(struct xenbus_device *dev,
			  const struct xenbus_device_id *id)
{
	int err;
	struct usb_hcd *hcd;
	struct usbfront_info *info;

	if (usb_disabled())
		return -ENODEV;

	hcd = create_hcd(dev);
	if (IS_ERR(hcd)) {
		err = PTR_ERR(hcd);
		xenbus_dev_fatal(dev, err,
				"fail to create usb host controller");
		goto fail;
	}

	info = hcd_to_info(hcd);
	dev_set_drvdata(&dev->dev, info);

	err = usb_add_hcd(hcd, 0, 0);
	if (err != 0) {
		xenbus_dev_fatal(dev, err,
				"fail to adding USB host controller");
		goto fail;
	}

	init_waitqueue_head(&info->wq);

	return 0;

fail:
	usb_put_hcd(hcd);
	dev_set_drvdata(&dev->dev, NULL);
	return err;
}

static void usbfront_disconnect(struct xenbus_device *dev)
{
	struct usbfront_info *info = dev_get_drvdata(&dev->dev);
	struct usb_hcd *hcd = info_to_hcd(info);

	usb_remove_hcd(hcd);
	if (info->kthread) {
		kthread_stop(info->kthread);
		info->kthread = NULL;
	}
	xenbus_frontend_closed(dev);
}

static void backend_changed(struct xenbus_device *dev,
				     enum xenbus_state backend_state)
{
	switch (backend_state) {
	case XenbusStateInitialising:
	case XenbusStateInitialised:
	case XenbusStateConnected:
	case XenbusStateReconfiguring:
	case XenbusStateReconfigured:
	case XenbusStateUnknown:
	case XenbusStateClosed:
		break;

	case XenbusStateInitWait:
		if (dev->state != XenbusStateInitialising)
			break;
		if (!connect(dev))
			xenbus_switch_state(dev, XenbusStateConnected);
		break;

	case XenbusStateClosing:
		usbfront_disconnect(dev);
		break;

	default:
		xenbus_dev_fatal(dev, -EINVAL, "saw state %d at frontend",
				 backend_state);
		break;
	}
}

static int usbfront_remove(struct xenbus_device *dev)
{
	struct usbfront_info *info = dev_get_drvdata(&dev->dev);
	struct usb_hcd *hcd = info_to_hcd(info);

	destroy_rings(info);
	usb_put_hcd(hcd);

	return 0;
}

static const struct xenbus_device_id usbfront_ids[] = {
	{ "vusb" },
	{ "" },
};
MODULE_ALIAS("xen:vusb");

static struct xenbus_driver usbfront_driver = {
	.name = "vusb",
	.ids = usbfront_ids,
	.probe = usbfront_probe,
	.otherend_changed = backend_changed,
	.remove = usbfront_remove,
};

static int __init usbfront_init(void)
{
	if (!is_running_on_xen())
		return -ENODEV;

	xenhcd_urbp_cachep = kmem_cache_create("xenhcd_urb_priv",
			sizeof(struct urb_priv), 0, 0, NULL);
	if (!xenhcd_urbp_cachep) {
		printk(KERN_ERR "usbfront failed to create kmem cache\n");
		return -ENOMEM;
	}

	return xenbus_register_frontend(&usbfront_driver);
}

static void __exit usbfront_exit(void)
{
	kmem_cache_destroy(xenhcd_urbp_cachep);
	xenbus_unregister_driver(&usbfront_driver);
}

module_init(usbfront_init);
module_exit(usbfront_exit);

MODULE_AUTHOR("");
MODULE_DESCRIPTION("Xen USB Virtual Host Controller driver (usbfront)");
MODULE_LICENSE("Dual BSD/GPL");
