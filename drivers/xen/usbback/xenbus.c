/*
 * xenbus.c
 *
 * Xenbus interface for USB backend driver.
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

static int start_xenusbd(usbif_t *usbif)
{
	int err = 0;
	char name[TASK_COMM_LEN];

	snprintf(name, TASK_COMM_LEN, "usbback.%d.%d", usbif->domid,
			usbif->handle);
	usbif->xenusbd = kthread_run(usbbk_schedule, usbif, name);
	if (IS_ERR(usbif->xenusbd)) {
		err = PTR_ERR(usbif->xenusbd);
		usbif->xenusbd = NULL;
		xenbus_dev_error(usbif->xbdev, err, "start xenusbd");
	}

	return err;
}

static void backend_changed(struct xenbus_watch *watch,
			const char **vec, unsigned int len)
{
	struct xenbus_transaction xbt;
	int err;
	int i;
	char node[8];
	char *busid;
	struct vusb_port_id *portid = NULL;

	usbif_t *usbif = container_of(watch, usbif_t, backend_watch);
	struct xenbus_device *dev = usbif->xbdev;

again:
	err = xenbus_transaction_start(&xbt);
	if (err) {
		xenbus_dev_fatal(dev, err, "starting transaction");
		return;
	}

	for (i = 1; i <= usbif->num_ports; i++) {
		sprintf(node, "port/%d", i);
		busid = xenbus_read(xbt, dev->nodename, node, NULL);
		if (IS_ERR(busid)) {
			err = PTR_ERR(busid);
			xenbus_dev_fatal(dev, err, "reading port/%d", i);
			goto abort;
		}

		/*
		 * remove portid, if the port is not connected,
		 */
		if (strlen(busid) == 0) {
			portid = find_portid(usbif->domid, usbif->handle, i);
			if (portid) {
				if (portid->is_connected)
					xenbus_dev_fatal(dev, err,
						"can't remove port/%d, unbind first", i);
				else
					portid_remove(usbif->domid, usbif->handle, i);
			}
			continue; /* never configured, ignore */
		}

		/*
		 * add portid,
		 * if the port is not configured and not used from other usbif.
		 */
		portid = find_portid(usbif->domid, usbif->handle, i);
		if (portid) {
			if ((strncmp(portid->phys_bus, busid, USBBACK_BUS_ID_SIZE)))
				xenbus_dev_fatal(dev, err,
					"can't add port/%d, remove first", i);
			else
				continue; /* already configured, ignore */
		} else {
			if (find_portid_by_busid(busid))
				xenbus_dev_fatal(dev, err,
					"can't add port/%d, busid already used", i);
			else
				portid_add(busid, usbif->domid, usbif->handle, i);
		}
	}

	err = xenbus_transaction_end(xbt, 0);
	if (err == -EAGAIN)
		goto again;
	if (err)
		xenbus_dev_fatal(dev, err, "completing transaction");

	return;

abort:
	xenbus_transaction_end(xbt, 1);

	return;
}

static int usbback_remove(struct xenbus_device *dev)
{
	usbif_t *usbif = dev_get_drvdata(&dev->dev);
	int i;

	if (usbif->backend_watch.node) {
		unregister_xenbus_watch(&usbif->backend_watch);
		kfree(usbif->backend_watch.node);
		usbif->backend_watch.node = NULL;
	}

	if (usbif) {
		/* remove all ports */
		for (i = 1; i <= usbif->num_ports; i++)
			portid_remove(usbif->domid, usbif->handle, i);
		usbif_disconnect(usbif);
		usbif_free(usbif);;
	}
	dev_set_drvdata(&dev->dev, NULL);

	return 0;
}

static int usbback_probe(struct xenbus_device *dev,
			  const struct xenbus_device_id *id)
{
	usbif_t *usbif;
	unsigned int handle;
	int num_ports;
	int usb_ver;
	int err;

	if (usb_disabled())
		return -ENODEV;

	handle = simple_strtoul(strrchr(dev->otherend, '/') + 1, NULL, 0);
	usbif = usbif_alloc(dev->otherend_id, handle);
	if (!usbif) {
		xenbus_dev_fatal(dev, -ENOMEM, "allocating backend interface");
		return -ENOMEM;
	}
	usbif->xbdev = dev;
	dev_set_drvdata(&dev->dev, usbif);

	err = xenbus_scanf(XBT_NIL, dev->nodename,
				"num-ports", "%d", &num_ports);
	if (err != 1) {
		xenbus_dev_fatal(dev, err, "reading num-ports");
		goto fail;
	}
	if (num_ports < 1 || num_ports > USB_MAXCHILDREN) {
		xenbus_dev_fatal(dev, err, "invalid num-ports");
		goto fail;
	}
	usbif->num_ports = num_ports;

	err = xenbus_scanf(XBT_NIL, dev->nodename,
				"usb-ver", "%d", &usb_ver);
	if (err != 1) {
		xenbus_dev_fatal(dev, err, "reading usb-ver");
		goto fail;
	}
	switch (usb_ver) {
	case USB_VER_USB11:
	case USB_VER_USB20:
		usbif->usb_ver = usb_ver;
		break;
	default:
		xenbus_dev_fatal(dev, err, "invalid usb-ver");
		goto fail;
	}

	err = xenbus_switch_state(dev, XenbusStateInitWait);
	if (err)
		goto fail;

	return 0;

fail:
	usbback_remove(dev);
	return err;
}

static int connect_rings(usbif_t *usbif)
{
	struct xenbus_device *dev = usbif->xbdev;
	unsigned long urb_ring_ref;
	unsigned long conn_ring_ref;
	unsigned int evtchn;
	int err;

	err = xenbus_gather(XBT_NIL, dev->otherend,
			    "urb-ring-ref", "%lu", &urb_ring_ref,
			    "conn-ring-ref", "%lu", &conn_ring_ref,
			    "event-channel", "%u", &evtchn, NULL);
	if (err) {
		xenbus_dev_fatal(dev, err,
				 "reading %s/ring-ref and event-channel",
				 dev->otherend);
		return err;
	}

	printk("usbback: urb-ring-ref %ld, conn-ring-ref %ld, event-channel %d\n",
	       urb_ring_ref, conn_ring_ref, evtchn);

	err = usbif_map(usbif, urb_ring_ref, conn_ring_ref, evtchn);
	if (err) {
		xenbus_dev_fatal(dev, err,
				"mapping urb-ring-ref %lu conn-ring-ref %lu port %u",
				urb_ring_ref, conn_ring_ref, evtchn);
		return err;
	}

	return 0;
}

static void frontend_changed(struct xenbus_device *dev,
				     enum xenbus_state frontend_state)
{
	usbif_t *usbif = dev_get_drvdata(&dev->dev);
	int err;

	switch (frontend_state) {
	case XenbusStateInitialised:
	case XenbusStateReconfiguring:
	case XenbusStateReconfigured:
		break;

	case XenbusStateInitialising:
		if (dev->state == XenbusStateClosed) {
			printk("%s: %s: prepare for reconnect\n",
			       __FUNCTION__, dev->nodename);
			xenbus_switch_state(dev, XenbusStateInitWait);
		}
		break;

	case XenbusStateConnected:
		if (dev->state == XenbusStateConnected)
			break;
		err = connect_rings(usbif);
		if (err)
			break;
		err = start_xenusbd(usbif);
		if (err)
			break;
		err = xenbus_watch_path2(dev, dev->nodename, "port",
					&usbif->backend_watch, backend_changed);
		if (err)
			break;
		xenbus_switch_state(dev, XenbusStateConnected);
		break;

	case XenbusStateClosing:
		usbif_disconnect(usbif);
		xenbus_switch_state(dev, XenbusStateClosing);
		break;

	case XenbusStateClosed:
		xenbus_switch_state(dev, XenbusStateClosed);
		if (xenbus_dev_is_online(dev))
			break;
		/* fall through if not online */
	case XenbusStateUnknown:
		device_unregister(&dev->dev);
		break;

	default:
		xenbus_dev_fatal(dev, -EINVAL, "saw state %d at frontend",
				 frontend_state);
		break;
	}
}

static const struct xenbus_device_id usbback_ids[] = {
	{ "vusb" },
	{ "" },
};

static struct xenbus_driver usbback_driver = {
	.name = "vusb",
	.ids = usbback_ids,
	.probe = usbback_probe,
	.otherend_changed = frontend_changed,
	.remove = usbback_remove,
};

int __init usbback_xenbus_init(void)
{
	return xenbus_register_backend(&usbback_driver);
}

void __exit usbback_xenbus_exit(void)
{
	xenbus_unregister_driver(&usbback_driver);
}
