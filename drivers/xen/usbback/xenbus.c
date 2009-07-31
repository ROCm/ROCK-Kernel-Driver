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

#include <xen/xenbus.h>
#include "usbback.h"

static int start_xenusbd(usbif_t *usbif)
{
        int err = 0;
        char name[TASK_COMM_LEN];

        snprintf(name, TASK_COMM_LEN, "usbback.%d.%d", usbif->domid, usbif->handle);
        usbif->xenusbd = kthread_run(usbbk_schedule, usbif, name);
        if (IS_ERR(usbif->xenusbd)) {
                err = PTR_ERR(usbif->xenusbd);
                usbif->xenusbd = NULL;
                xenbus_dev_error(usbif->xbdev, err, "start xenusbd");
        }
        return err;
}

static int usbback_remove(struct xenbus_device *dev)
{
	usbif_t *usbif = dev_get_drvdata(&dev->dev);

	if (usbif) {
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
	int err;

	if (usb_disabled())
		return -ENODEV;

	handle = simple_strtoul(strrchr(dev->otherend,'/')+1, NULL, 0);
	usbif = usbif_alloc(dev->otherend_id, handle);
	if (!usbif) {
		xenbus_dev_fatal(dev, -ENOMEM, "allocating backend interface");
		return -ENOMEM;
	}
	usbif->xbdev = dev;
	dev_set_drvdata(&dev->dev, usbif);

	err = xenbus_switch_state(dev, XenbusStateInitWait);
	if (err)
		goto fail;

	return 0;

fail:
	usbback_remove(dev);
	return err;
}

static int connect_ring(usbif_t *usbif)
{
	struct xenbus_device *dev = usbif->xbdev;
	unsigned long ring_ref;
	unsigned int evtchn;
	int err;

	err = xenbus_gather(XBT_NIL, dev->otherend,
			    "ring-ref", "%lu", &ring_ref,
			    "event-channel", "%u", &evtchn, NULL);
	if (err) {
		xenbus_dev_fatal(dev, err,
				 "reading %s/ring-ref and event-channel",
				 dev->otherend);
		return err;
	}

	printk("usbback: ring-ref %ld, event-channel %d\n",
	       ring_ref, evtchn);

	err = usbif_map(usbif, ring_ref, evtchn);
	if (err) {
		xenbus_dev_fatal(dev, err, "mapping ring-ref %lu port %u",
				 ring_ref, evtchn);
		return err;
	}

	return 0;
}

void usbback_do_hotplug(usbif_t *usbif)
{
	struct xenbus_transaction xbt;
	struct xenbus_device *dev = usbif->xbdev;
	struct usbstub *stub = NULL;
	int err;
	char port_str[8];
	int i;
	int num_ports;
	int state;

again:
		err = xenbus_transaction_start(&xbt);
		if (err) {
			xenbus_dev_fatal(dev, err, "starting transaction");
			return;
		}

		err = xenbus_scanf(xbt, dev->nodename,
					"num-ports", "%d", &num_ports);

		for (i = 1; i <= num_ports; i++) {
			stub = find_attached_device(usbif, i);
			if (stub)
				state = stub->udev->speed;
			else
				state = 0;
			sprintf(port_str, "port-%d", i);
			err = xenbus_printf(xbt, dev->nodename, port_str, "%d", state);
			if (err) {
				xenbus_dev_fatal(dev, err, "writing port-%d state", i);
				goto abort;
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
}

void usbback_reconfigure(usbif_t *usbif)
{
	struct xenbus_device *dev = usbif->xbdev;

	if (dev->state == XenbusStateConnected)
		xenbus_switch_state(dev, XenbusStateReconfiguring);
}

void frontend_changed(struct xenbus_device *dev,
				     enum xenbus_state frontend_state)
{
	usbif_t *usbif = dev_get_drvdata(&dev->dev);
	int err;

	switch (frontend_state) {
	case XenbusStateInitialising:
		if (dev->state == XenbusStateClosed) {
			printk("%s: %s: prepare for reconnect\n",
			       __FUNCTION__, dev->nodename);
			xenbus_switch_state(dev, XenbusStateInitWait);
		}
		break;

	case XenbusStateInitialised:
		err = connect_ring(usbif);
		if (err)
			break;
		start_xenusbd(usbif);
		usbback_do_hotplug(usbif);
		xenbus_switch_state(dev, XenbusStateConnected);
		break;

	case XenbusStateConnected:
		if (dev->state == XenbusStateConnected)
			break;
		xenbus_switch_state(dev, XenbusStateConnected);
		break;

	case XenbusStateClosing:
		usbif_disconnect(usbif);
		xenbus_switch_state(dev, XenbusStateClosing);
		break;

	case XenbusStateClosed:
		xenbus_switch_state(dev, XenbusStateClosed);
		break;

	case XenbusStateReconfiguring:
		usbback_do_hotplug(usbif);
		xenbus_switch_state(dev, XenbusStateReconfigured);
		break;

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

int usbback_xenbus_init(void)
{
	return xenbus_register_backend(&usbback_driver);
}

void usbback_xenbus_exit(void)
{
	xenbus_unregister_driver(&usbback_driver);
}
