/*
 * USB hub driver.
 *
 * (C) Copyright 1999 Linus Torvalds
 * (C) Copyright 1999 Johannes Erdfelt
 * (C) Copyright 1999 Gregory P. Smith
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/list.h>
#include <linux/malloc.h>
#include <linux/smp_lock.h>
#ifdef CONFIG_USB_DEBUG
	#define DEBUG
#else
	#undef DEBUG
#endif
#include <linux/usb.h>
#include <linux/usbdevice_fs.h>

#include <asm/semaphore.h>
#include <asm/uaccess.h>
#include <asm/byteorder.h>

#include "hub.h"

/* Wakes up khubd */
static spinlock_t hub_event_lock = SPIN_LOCK_UNLOCKED;
static DECLARE_MUTEX(usb_address0_sem);

static LIST_HEAD(hub_event_list);	/* List of hubs needing servicing */
static LIST_HEAD(hub_list);		/* List containing all of the hubs (for cleanup) */

static DECLARE_WAIT_QUEUE_HEAD(khubd_wait);
static int khubd_pid = 0;			/* PID of khubd */
static DECLARE_MUTEX_LOCKED(khubd_exited);

static int usb_get_hub_descriptor(struct usb_device *dev, void *data, int size)
{
	return usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
		USB_REQ_GET_DESCRIPTOR, USB_DIR_IN | USB_RT_HUB,
		USB_DT_HUB << 8, 0, data, size, HZ);
}

static int usb_clear_hub_feature(struct usb_device *dev, int feature)
{
	return usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
		USB_REQ_CLEAR_FEATURE, USB_RT_HUB, feature, 0, NULL, 0, HZ);
}

static int usb_clear_port_feature(struct usb_device *dev, int port, int feature)
{
	return usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
		USB_REQ_CLEAR_FEATURE, USB_RT_PORT, feature, port, NULL, 0, HZ);
}

static int usb_set_port_feature(struct usb_device *dev, int port, int feature)
{
	return usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
		USB_REQ_SET_FEATURE, USB_RT_PORT, feature, port, NULL, 0, HZ);
}

static int usb_get_hub_status(struct usb_device *dev, void *data)
{
	return usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
		USB_REQ_GET_STATUS, USB_DIR_IN | USB_RT_HUB, 0, 0,
		data, sizeof(struct usb_hub_status), HZ);
}

static int usb_get_port_status(struct usb_device *dev, int port, void *data)
{
	return usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
		USB_REQ_GET_STATUS, USB_DIR_IN | USB_RT_PORT, 0, port,
		data, sizeof(struct usb_hub_status), HZ);
}

static void hub_irq(struct urb *urb)
{
	struct usb_hub *hub = (struct usb_hub *)urb->context;
	unsigned long flags;

	/* Cause a hub reset after 10 consecutive errors */
	if (urb->status) {
		if (urb->status == -ENOENT)
			return;

		dbg("nonzero status in irq %d", urb->status);

		if ((++hub->nerrors < 10) || hub->error)
			return;

		hub->error = urb->status;
	}

	hub->nerrors = 0;

	/* Something happened, let khubd figure it out */
	if (waitqueue_active(&khubd_wait)) {
		/* Add the hub to the event queue */
		spin_lock_irqsave(&hub_event_lock, flags);
		if (list_empty(&hub->event_list)) {
			list_add(&hub->event_list, &hub_event_list);
			wake_up(&khubd_wait);
		}
		spin_unlock_irqrestore(&hub_event_lock, flags);
	}
}

static void usb_hub_power_on(struct usb_hub *hub)
{
	int i;

	/* Enable power to the ports */
	dbg("enabling power on all ports");
	for (i = 0; i < hub->nports; i++)
		usb_set_port_feature(hub->dev, i + 1, USB_PORT_FEAT_POWER);

	/* Wait for power to be enabled */
	wait_ms(hub->descriptor->bPwrOn2PwrGood * 2);
}

static int usb_hub_configure(struct usb_hub *hub, struct usb_endpoint_descriptor *endpoint)
{
	struct usb_device *dev = hub->dev;
	struct usb_hub_status hubstatus;
	char portstr[USB_MAXCHILDREN + 1];
	unsigned int pipe;
	int i, maxp, ret;

	hub->descriptor = kmalloc(HUB_DESCRIPTOR_MAX_SIZE, GFP_KERNEL);
	if (!hub->descriptor) {
		err("Unable to kmalloc %d bytes for hub descriptor", HUB_DESCRIPTOR_MAX_SIZE);
		return -1;
	}

	/* Request the entire hub descriptor. */
	ret = usb_get_hub_descriptor(dev, hub->descriptor, HUB_DESCRIPTOR_MAX_SIZE);
		/* <hub->descriptor> is large enough for a hub with 127 ports;
		 * the hub can/will return fewer bytes here. */
	if (ret < 0) {
		err("Unable to get hub descriptor (err = %d)", ret);
		kfree(hub->descriptor);
		return -1;
	}

	hub->nports = dev->maxchild = hub->descriptor->bNbrPorts;
	info("%d port%s detected", hub->nports, (hub->nports == 1) ? "" : "s");

	if (hub->descriptor->wHubCharacteristics & HUB_CHAR_COMPOUND)
		dbg("part of a compound device");
	else
		dbg("standalone hub");

	switch (hub->descriptor->wHubCharacteristics & HUB_CHAR_LPSM) {
		case 0x00:
			dbg("ganged power switching");
			break;
		case 0x01:
			dbg("individual port power switching");
			break;
		case 0x02:
		case 0x03:
			dbg("unknown reserved power switching mode");
			break;
	}

	switch (hub->descriptor->wHubCharacteristics & HUB_CHAR_OCPM) {
		case 0x00:
			dbg("global over-current protection");
			break;
		case 0x08:
			dbg("individual port over-current protection");
			break;
		case 0x10:
		case 0x18:
			dbg("no over-current protection");
                        break;
	}

	dbg("power on to power good time: %dms", hub->descriptor->bPwrOn2PwrGood * 2);
	dbg("hub controller current requirement: %dmA", hub->descriptor->bHubContrCurrent);

	for (i = 0; i < dev->maxchild; i++)
		portstr[i] = hub->descriptor->bitmap[((i + 1) / 8)] & (1 << ((i + 1) % 8)) ? 'F' : 'R';
	portstr[dev->maxchild] = 0;

	dbg("port removable status: %s", portstr);

	ret = usb_get_hub_status(dev, &hubstatus);
	if (ret < 0) {
		err("Unable to get hub status (err = %d)", ret);
		kfree(hub->descriptor);
		return -1;
	}

	le16_to_cpus(&hubstatus.wHubStatus);

	dbg("local power source is %s",
		(hubstatus.wHubStatus & HUB_STATUS_LOCAL_POWER) ? "lost (inactive)" : "good");

	dbg("%sover-current condition exists",
		(hubstatus.wHubStatus & HUB_STATUS_OVERCURRENT) ? "" : "no ");

	/* Start the interrupt endpoint */
	pipe = usb_rcvintpipe(dev, endpoint->bEndpointAddress);
	maxp = usb_maxpacket(dev, pipe, usb_pipeout(pipe));

	if (maxp > sizeof(hub->buffer))
		maxp = sizeof(hub->buffer);

	hub->urb = usb_alloc_urb(0);
	if (!hub->urb) {
		err("couldn't allocate interrupt urb");
		kfree(hub->descriptor);
		return -1;
	}

	FILL_INT_URB(hub->urb, dev, pipe, hub->buffer, maxp, hub_irq,
		hub, endpoint->bInterval);
	ret = usb_submit_urb(hub->urb);
	if (ret) {
		err("usb_submit_urb failed (%d)", ret);
		kfree(hub->descriptor);
		return -1;
	}
		
	/* Wake up khubd */
	wake_up(&khubd_wait);

	usb_hub_power_on(hub);

	return 0;
}

static void *hub_probe(struct usb_device *dev, unsigned int i,
		       const struct usb_device_id *id)

{
	struct usb_interface_descriptor *interface;
	struct usb_endpoint_descriptor *endpoint;
	struct usb_hub *hub;
	unsigned long flags;

	interface = &dev->actconfig->interface[i].altsetting[0];

	/* Some hubs have a subclass of 1, which AFAICT according to the */
	/*  specs is not defined, but it works */
	if ((interface->bInterfaceSubClass != 0) &&
	    (interface->bInterfaceSubClass != 1)) {
		err("invalid subclass (%d) for USB hub device #%d",
			interface->bInterfaceSubClass, dev->devnum);
		return NULL;
	}

	/* Multiple endpoints? What kind of mutant ninja-hub is this? */
	if (interface->bNumEndpoints != 1) {
		err("invalid bNumEndpoints (%d) for USB hub device #%d",
			interface->bNumEndpoints, dev->devnum);
		return NULL;
	}

	endpoint = &interface->endpoint[0];

	/* Output endpoint? Curiousier and curiousier.. */
	if (!(endpoint->bEndpointAddress & USB_DIR_IN)) {
		err("Device #%d is hub class, but has output endpoint?",
			dev->devnum);
		return NULL;
	}

	/* If it's not an interrupt endpoint, we'd better punt! */
	if ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) != USB_ENDPOINT_XFER_INT) {
		err("Device #%d is hub class, but has endpoint other than interrupt?",
			dev->devnum);
		return NULL;
	}

	/* We found a hub */
	info("USB hub found");

	hub = kmalloc(sizeof(*hub), GFP_KERNEL);
	if (!hub) {
		err("couldn't kmalloc hub struct");
		return NULL;
	}

	memset(hub, 0, sizeof(*hub));

	INIT_LIST_HEAD(&hub->event_list);
	hub->dev = dev;

	/* Record the new hub's existence */
	spin_lock_irqsave(&hub_event_lock, flags);
	INIT_LIST_HEAD(&hub->hub_list);
	list_add(&hub->hub_list, &hub_list);
	spin_unlock_irqrestore(&hub_event_lock, flags);

	if (usb_hub_configure(hub, endpoint) >= 0)
		return hub;

	err("hub configuration failed for device #%d", dev->devnum);

	/* free hub, but first clean up its list. */
	spin_lock_irqsave(&hub_event_lock, flags);

	/* Delete it and then reset it */
	list_del(&hub->event_list);
	INIT_LIST_HEAD(&hub->event_list);
	list_del(&hub->hub_list);
	INIT_LIST_HEAD(&hub->hub_list);

	spin_unlock_irqrestore(&hub_event_lock, flags);

	kfree(hub);

	return NULL;
}

static void hub_disconnect(struct usb_device *dev, void *ptr)
{
	struct usb_hub *hub = (struct usb_hub *)ptr;
	unsigned long flags;

	spin_lock_irqsave(&hub_event_lock, flags);

	/* Delete it and then reset it */
	list_del(&hub->event_list);
	INIT_LIST_HEAD(&hub->event_list);
	list_del(&hub->hub_list);
	INIT_LIST_HEAD(&hub->hub_list);

	spin_unlock_irqrestore(&hub_event_lock, flags);

	if (hub->urb) {
		usb_unlink_urb(hub->urb);
		usb_free_urb(hub->urb);
		hub->urb = NULL;
	}

	if (hub->descriptor) {
		kfree(hub->descriptor);
		hub->descriptor = NULL;
	}

	/* Free the memory */
	kfree(hub);
}

static int hub_ioctl(struct usb_device *hub, unsigned int code, void *user_data)
{
	/* assert ifno == 0 (part of hub spec) */
	switch (code) {
	case USBDEVFS_HUB_PORTINFO: {
		struct usbdevfs_hub_portinfo *info = user_data;
		unsigned long flags;
		int i;

		spin_lock_irqsave(&hub_event_lock, flags);
		if (hub->devnum <= 0)
			info->nports = 0;
		else {
			info->nports = hub->maxchild;
			for (i = 0; i < info->nports; i++) {
				if (hub->children[i] == NULL)
					info->port[i] = 0;
				else
					info->port[i] = hub->children[i]->devnum;
			}
		}
		spin_unlock_irqrestore(&hub_event_lock, flags);

		return info->nports + 1;
		}

	default:
		return -ENOSYS;
	}
}

static int usb_hub_reset(struct usb_hub *hub)
{
	struct usb_device *dev = hub->dev;
	int i;

	/* Disconnect any attached devices */
	for (i = 0; i < hub->nports; i++) {
		if (dev->children[i])
			usb_disconnect(&dev->children[i]);
	}

	/* Attempt to reset the hub */
	if (hub->urb)
		usb_unlink_urb(hub->urb);
	else
		return -1;

	if (usb_reset_device(dev))
		return -1;

	if (usb_submit_urb(hub->urb))
		return -1;

	usb_hub_power_on(hub);

	return 0;
}

static void usb_hub_disconnect(struct usb_device *dev)
{
	struct usb_device *parent = dev->parent;
	int i;

	/* Find the device pointer to disconnect */
	if (parent) {
		for (i = 0; i < parent->maxchild; i++) {
			if (parent->children[i] == dev) {
				usb_disconnect(&parent->children[i]);
				return;
			}
		}
	}

	err("cannot disconnect hub %d", dev->devnum);
}

#define HUB_RESET_TRIES		5
#define HUB_PROBE_TRIES		2
#define HUB_SHORT_RESET_TIME	10
#define HUB_LONG_RESET_TIME	200
#define HUB_RESET_TIMEOUT	500

static int usb_hub_port_wait_reset(struct usb_device *hub, int port,
				struct usb_device *dev, unsigned int delay)
{
	int delay_time, ret;
	struct usb_port_status portsts;
	unsigned short portchange, portstatus;

	for (delay_time = 0; delay_time < HUB_RESET_TIMEOUT; delay_time += delay) {
		/* wait to give the device a chance to reset */
		wait_ms(delay);

		/* read and decode port status */
		ret = usb_get_port_status(hub, port + 1, &portsts);
		if (ret < 0) {
			err("get_port_status(%d) failed (err = %d)", port + 1, ret);
			return -1;
		}

		portstatus = le16_to_cpu(portsts.wPortStatus);
		portchange = le16_to_cpu(portsts.wPortChange);
		dbg("port %d, portstatus %x, change %x, %s", port + 1,
			portstatus, portchange,
			portstatus & (1 << USB_PORT_FEAT_LOWSPEED) ? "1.5 Mb/s" : "12 Mb/s");

		/* bomb out completely if something weird happened */
		if ((portchange & USB_PORT_STAT_C_CONNECTION) ||
		    !(portstatus & USB_PORT_STAT_CONNECTION))
			return -1;

		/* if we`ve finished resetting, then break out of the loop */
		if (!(portstatus & USB_PORT_STAT_RESET) &&
		    (portstatus & USB_PORT_STAT_ENABLE)) {
			dev->slow = (portstatus & USB_PORT_STAT_LOW_SPEED) ? 1 : 0;
			return 0;
		}

		/* switch to the long delay after two short delay failures */
		if (delay_time >= 2 * HUB_SHORT_RESET_TIME)
			delay = HUB_LONG_RESET_TIME;

		dbg("port %d of hub %d not reset yet, waiting %dms", port + 1,
			hub->devnum, delay);
	}

	return -1;
}

static int usb_hub_port_reset(struct usb_device *hub, int port,
				struct usb_device *dev, unsigned int delay)
{
	int i;

	/* Reset the port */
	for (i = 0; i < HUB_RESET_TRIES; i++) {
		usb_set_port_feature(hub, port + 1, USB_PORT_FEAT_RESET);

		/* return success if the port reset OK */
		if (!usb_hub_port_wait_reset(hub, port, dev, delay)) {
			usb_clear_port_feature(hub, port + 1, USB_PORT_FEAT_C_RESET);
			return 0;
		}

		dbg("port %d of hub %d not enabled, trying reset again...",
			port + 1, hub->devnum);
		delay = HUB_LONG_RESET_TIME;
	}

	err("Cannot enable port %i of hub %d, disabling port.",
		port + 1, hub->devnum);
	err("Maybe the USB cable is bad?");

	return -1;
}

void usb_hub_port_disable(struct usb_device *hub, int port)
{
	int ret;

	ret = usb_clear_port_feature(hub, port + 1, USB_PORT_FEAT_ENABLE);
	if (ret)
		err("cannot disable port %d of hub %d (err = %d)",
			port + 1, hub->devnum, ret);
}

static void usb_hub_port_connect_change(struct usb_device *hub, int port,
					struct usb_port_status *portsts)
{
	struct usb_device *dev;
	unsigned short portstatus, portchange;
	unsigned int delay = HUB_SHORT_RESET_TIME;
	int i;
	char *portstr, *tempstr;

	portstatus = le16_to_cpu(portsts->wPortStatus);
	portchange = le16_to_cpu(portsts->wPortChange);
	dbg("port %d, portstatus %x, change %x, %s", port + 1, portstatus,
		portchange, portstatus & (1 << USB_PORT_FEAT_LOWSPEED) ? "1.5 Mb/s" : "12 Mb/s");

	/* Clear the connection change status */
	usb_clear_port_feature(hub, port + 1, USB_PORT_FEAT_C_CONNECTION);

	/* Disconnect any existing devices under this port */
	if (hub->children[port])
		usb_disconnect(&hub->children[port]);

	/* Return now if nothing is connected */
	if (!(portstatus & USB_PORT_STAT_CONNECTION)) {
		if (portstatus & USB_PORT_STAT_ENABLE)
			usb_hub_port_disable(hub, port);

		return;
	}

	down(&usb_address0_sem);

	tempstr = kmalloc(1024, GFP_KERNEL);
	portstr = kmalloc(1024, GFP_KERNEL);

	for (i = 0; i < HUB_PROBE_TRIES; i++) {
		struct usb_device *pdev, *cdev;

		/* Allocate a new device struct */
		dev = usb_alloc_dev(hub, hub->bus);
		if (!dev) {
			err("couldn't allocate usb_device");
			break;
		}

		hub->children[port] = dev;

		/* Reset the device */
		if (usb_hub_port_reset(hub, port, dev, delay)) {
			usb_free_dev(dev);
			break;
		}

		/* Find a new device ID for it */
		usb_connect(dev);

		/* Create a readable topology string */
		cdev = dev;
		pdev = dev->parent;
		if (portstr && tempstr) {
			portstr[0] = 0;
			while (pdev) {
				int port;

				for (port = 0; port < pdev->maxchild; port++)
					if (pdev->children[port] == cdev)
						break;

				strcpy(tempstr, portstr);
				if (!strlen(tempstr))
					sprintf(portstr, "%d", port + 1);
				else
					sprintf(portstr, "%d/%s", port + 1, tempstr);

				cdev = pdev;
				pdev = pdev->parent;
			}
			info("USB new device connect on bus%d/%s, assigned device number %d",
				dev->bus->busnum, portstr, dev->devnum);
		} else
			info("USB new device connect on bus%d, assigned device number %d",
				dev->bus->busnum, dev->devnum);

		/* Run it through the hoops (find a driver, etc) */
		if (!usb_new_device(dev))
			goto done;

		/* Free the configuration if there was an error */
		usb_free_dev(dev);

		/* Switch to a long reset time */
		delay = HUB_LONG_RESET_TIME;
	}

	hub->children[port] = NULL;
	usb_hub_port_disable(hub, port);
done:
	up(&usb_address0_sem);
	if (portstr)
		kfree(portstr);
	if (tempstr)
		kfree(tempstr);
}

static void usb_hub_events(void)
{
	unsigned long flags;
	struct list_head *tmp;
	struct usb_device *dev;
	struct usb_hub *hub;
	struct usb_hub_status hubsts;
	unsigned short hubstatus, hubchange;
	int i, ret;

	/*
	 *  We restart the list everytime to avoid a deadlock with
	 * deleting hubs downstream from this one. This should be
	 * safe since we delete the hub from the event list.
	 * Not the most efficient, but avoids deadlocks.
	 */
	while (1) {
		spin_lock_irqsave(&hub_event_lock, flags);

		if (list_empty(&hub_event_list))
			goto he_unlock;

		/* Grab the next entry from the beginning of the list */
		tmp = hub_event_list.next;

		hub = list_entry(tmp, struct usb_hub, event_list);
		dev = hub->dev;

		list_del(tmp);
		INIT_LIST_HEAD(tmp);

		spin_unlock_irqrestore(&hub_event_lock, flags);

		if (hub->error) {
			dbg("resetting hub %d for error %d", dev->devnum, hub->error);

			if (usb_hub_reset(hub)) {
				err("error resetting hub %d - disconnecting", dev->devnum);
				usb_hub_disconnect(dev);
				continue;
			}

			hub->nerrors = 0;
			hub->error = 0;
		}

		for (i = 0; i < hub->nports; i++) {
			struct usb_port_status portsts;
			unsigned short portstatus, portchange;

			ret = usb_get_port_status(dev, i + 1, &portsts);
			if (ret < 0) {
				err("get_port_status failed (err = %d)", ret);
				continue;
			}

			portstatus = le16_to_cpu(portsts.wPortStatus);
			portchange = le16_to_cpu(portsts.wPortChange);

			if (portchange & USB_PORT_STAT_C_CONNECTION) {
				dbg("port %d connection change", i + 1);

				usb_hub_port_connect_change(dev, i, &portsts);
			} else if (portchange & USB_PORT_STAT_C_ENABLE) {
				dbg("port %d enable change, status %x", i + 1, portstatus);
				usb_clear_port_feature(dev, i + 1, USB_PORT_FEAT_C_ENABLE);

				/*
				 * EM interference sometimes causes bad shielded USB devices to 
				 * be shutdown by the hub, this hack enables them again.
				 * Works at least with mouse driver. 
				 */
				if (!(portstatus & USB_PORT_STAT_ENABLE) && 
				    (portstatus & USB_PORT_STAT_CONNECTION) && (dev->children[i])) {
					err("already running port %i disabled by hub (EMI?), re-enabling...",
						i + 1);
					usb_hub_port_connect_change(dev, i, &portsts);
				}
			}

			if (portchange & USB_PORT_STAT_C_SUSPEND) {
				dbg("port %d suspend change", i + 1);
				usb_clear_port_feature(dev, i + 1,  USB_PORT_FEAT_C_SUSPEND);
			}
			
			if (portchange & USB_PORT_STAT_C_OVERCURRENT) {
				err("port %d over-current change", i + 1);
				usb_clear_port_feature(dev, i + 1, USB_PORT_FEAT_C_OVER_CURRENT);
				usb_hub_power_on(hub);
			}

			if (portchange & USB_PORT_STAT_C_RESET) {
				dbg("port %d reset change", i + 1);
				usb_clear_port_feature(dev, i + 1, USB_PORT_FEAT_C_RESET);
			}
		} /* end for i */

		/* deal with hub status changes */
		if (usb_get_hub_status(dev, &hubsts) < 0)
			err("get_hub_status failed");
		else {
			hubstatus = le16_to_cpup(&hubsts.wHubStatus);
			hubchange = le16_to_cpup(&hubsts.wHubChange);
			if (hubchange & HUB_CHANGE_LOCAL_POWER) {
				dbg("hub power change");
				usb_clear_hub_feature(dev, C_HUB_LOCAL_POWER);
			}
			if (hubchange & HUB_CHANGE_OVERCURRENT) {
				dbg("hub overcurrent change");
				wait_ms(500);	/* Cool down */
				usb_clear_hub_feature(dev, C_HUB_OVER_CURRENT);
                        	usb_hub_power_on(hub);
			}
		}
        } /* end while (1) */

he_unlock:
	spin_unlock_irqrestore(&hub_event_lock, flags);
}

static int usb_hub_thread(void *__hub)
{
	lock_kernel();

	/*
	 * This thread doesn't need any user-level access,
	 * so get rid of all our resources
	 */

	daemonize();

	/* Setup a nice name */
	strcpy(current->comm, "khubd");

	/* Send me a signal to get me die (for debugging) */
	do {
		usb_hub_events();
		interruptible_sleep_on(&khubd_wait);
	} while (!signal_pending(current));

	dbg("usb_hub_thread exiting");

	up_and_exit(&khubd_exited, 0);
}

static struct usb_device_id hub_id_table [] = {
    { match_flags: USB_DEVICE_ID_MATCH_INT_CLASS,
      bInterfaceClass: USB_CLASS_HUB},
    { }						/* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, hub_id_table);

static struct usb_driver hub_driver = {
	name:		"hub",
	probe:		hub_probe,
	ioctl:		hub_ioctl,
	disconnect:	hub_disconnect,
	id_table:	hub_id_table,
};

/*
 * This should be a separate module.
 */
int usb_hub_init(void)
{
	int pid;

	if (usb_register(&hub_driver) < 0) {
		err("Unable to register USB hub driver");
		return -1;
	}

	pid = kernel_thread(usb_hub_thread, NULL,
		CLONE_FS | CLONE_FILES | CLONE_SIGHAND);
	if (pid >= 0) {
		khubd_pid = pid;

		return 0;
	}

	/* Fall through if kernel_thread failed */
	usb_deregister(&hub_driver);
	err("failed to start usb_hub_thread");

	return -1;
}

void usb_hub_cleanup(void)
{
	int ret;

	/* Kill the thread */
	ret = kill_proc(khubd_pid, SIGTERM, 1);

	down(&khubd_exited);

	/*
	 * Hub resources are freed for us by usb_deregister. It calls
	 * usb_driver_purge on every device which in turn calls that
	 * devices disconnect function if it is using this driver.
	 * The hub_disconnect function takes care of releasing the
	 * individual hub resources. -greg
	 */
	usb_deregister(&hub_driver);
} /* usb_hub_cleanup() */

/*
 * WARNING - If a driver calls usb_reset_device, you should simulate a
 * disconnect() and probe() for other interfaces you doesn't claim. This
 * is left up to the driver writer right now. This insures other drivers
 * have a chance to re-setup their interface.
 *
 * Take a look at proc_resetdevice in devio.c for some sample code to
 * do this.
 */
int usb_reset_device(struct usb_device *dev)
{
	struct usb_device *parent = dev->parent;
	struct usb_device_descriptor descriptor;
	int i, ret, port = -1;

	if (!parent) {
		err("attempting to reset root hub!");
		return -EINVAL;
	}

	for (i = 0; i < parent->maxchild; i++)
		if (parent->children[i] == dev) {
			port = i;
			break;
		}

	if (port < 0)
		return -ENOENT;

	down(&usb_address0_sem);

	/* Send a reset to the device */
	if (usb_hub_port_reset(parent, port, dev, HUB_SHORT_RESET_TIME)) {
		usb_hub_port_disable(parent, port);
		up(&usb_address0_sem);
		return(-ENODEV);
	}

	/* Reprogram the Address */
	ret = usb_set_address(dev);
	if (ret < 0) {
		err("USB device not accepting new address (error=%d)", ret);
		usb_hub_port_disable(parent, port);
		up(&usb_address0_sem);
		return ret;
	}

	/* Let the SET_ADDRESS settle */
	wait_ms(10);

	up(&usb_address0_sem);

	/*
	 * Now we fetch the configuration descriptors for the device and
	 * see if anything has changed. If it has, we dump the current
	 * parsed descriptors and reparse from scratch. Then we leave
	 * the device alone for the caller to finish setting up.
	 *
	 * If nothing changed, we reprogram the configuration and then
	 * the alternate settings.
	 */
	ret = usb_get_descriptor(dev, USB_DT_DEVICE, 0, &descriptor,
			sizeof(descriptor));
	if (ret < 0)
		return ret;

	le16_to_cpus(&descriptor.bcdUSB);
	le16_to_cpus(&descriptor.idVendor);
	le16_to_cpus(&descriptor.idProduct);
	le16_to_cpus(&descriptor.bcdDevice);

	if (memcmp(&dev->descriptor, &descriptor, sizeof(descriptor))) {
		usb_destroy_configuration(dev);

		ret = usb_get_device_descriptor(dev);
		if (ret < sizeof(dev->descriptor)) {
			if (ret < 0)
				err("unable to get device descriptor (error=%d)", ret);
			else
				err("USB device descriptor short read (expected %i, got %i)", sizeof(dev->descriptor), ret);
        
			clear_bit(dev->devnum, &dev->bus->devmap.devicemap);
			dev->devnum = -1;
			return -EIO;
		}

		ret = usb_get_configuration(dev);
		if (ret < 0) {
			err("unable to get configuration (error=%d)", ret);
			usb_destroy_configuration(dev);
			clear_bit(dev->devnum, &dev->bus->devmap.devicemap);
			dev->devnum = -1;
			return 1;
		}

		dev->actconfig = dev->config;
		usb_set_maxpacket(dev);

		return 1;
	}

	ret = usb_set_configuration(dev, dev->actconfig->bConfigurationValue);
	if (ret < 0) {
		err("failed to set active configuration (error=%d)", ret);
		return ret;
	}

	for (i = 0; i < dev->actconfig->bNumInterfaces; i++) {
		struct usb_interface *intf = &dev->actconfig->interface[i];
		struct usb_interface_descriptor *as = &intf->altsetting[intf->act_altsetting];

		ret = usb_set_interface(dev, as->bInterfaceNumber, as->bAlternateSetting);
		if (ret < 0) {
			err("failed to set active alternate setting for interface %d (error=%d)", i, ret);
			return ret;
		}
	}

	return 0;
}

