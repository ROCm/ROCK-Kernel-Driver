/*
 * drivers/usb/usb.c
 *
 * (C) Copyright Linus Torvalds 1999
 * (C) Copyright Johannes Erdfelt 1999-2001
 * (C) Copyright Andreas Gal 1999
 * (C) Copyright Gregory P. Smith 1999
 * (C) Copyright Deti Fliegl 1999 (new USB architecture)
 * (C) Copyright Randy Dunlap 2000
 * (C) Copyright David Brownell 2000-2001 (kernel hotplug, usb_device_id,
 	more docs, etc)
 * (C) Copyright Yggdrasil Computing, Inc. 2000
 *     (usb_device_id matching changes by Adam J. Richter)
 *
 * NOTE! This is not actually a driver at all, rather this is
 * just a collection of helper routines that implement the
 * generic USB things that the real drivers can use..
 *
 * Think of this as a "USB library" rather than anything else.
 * It should be considered a slave, with no callbacks. Callbacks
 * are evil.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/bitops.h>
#include <linux/slab.h>
#include <linux/interrupt.h>  /* for in_interrupt() */
#include <linux/kmod.h>
#include <linux/init.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/spinlock.h>
#include <asm/byteorder.h>

#ifdef CONFIG_USB_DEBUG
	#define DEBUG
#else
	#undef DEBUG
#endif
#include <linux/usb.h>

#include "hcd.h"

extern int  usb_hub_init(void);
extern void usb_hub_cleanup(void);

/*
 * Prototypes for the device driver probing/loading functions
 */
static void usb_find_drivers(struct usb_device *);
static int  usb_find_interface_driver(struct usb_device *, unsigned int);
static void usb_check_support(struct usb_device *);

/*
 * We have a per-interface "registered driver" list.
 */
LIST_HEAD(usb_driver_list);

devfs_handle_t usb_devfs_handle;	/* /dev/usb dir. */

static struct usb_driver *usb_minors[16];

/**
 *	usb_register - register a USB driver
 *	@new_driver: USB operations for the driver
 *
 *	Registers a USB driver with the USB core.  The list of unattached
 *	interfaces will be rescanned whenever a new driver is added, allowing
 *	the new driver to attach to any recognized devices.
 *	Returns a negative error code on failure and 0 on success.
 */
int usb_register(struct usb_driver *new_driver)
{
	if (new_driver->fops != NULL) {
		if (usb_minors[new_driver->minor/16]) {
			 err("error registering %s driver", new_driver->name);
			return -EINVAL;
		}
		usb_minors[new_driver->minor/16] = new_driver;
	}

	info("registered new driver %s", new_driver->name);

	init_MUTEX(&new_driver->serialize);

	/* Add it to the list of known drivers */
	list_add_tail(&new_driver->driver_list, &usb_driver_list);

	usb_scan_devices();

	usbfs_update_special();

	return 0;
}

/**
 *	usb_scan_devices - scans all unclaimed USB interfaces
 *	Context: !in_interrupt ()
 *
 *	Goes through all unclaimed USB interfaces, and offers them to all
 *	registered USB drivers through the 'probe' function.
 *	This will automatically be called after usb_register is called.
 *	It is called by some of the subsystems layered over USB
 *	after one of their subdrivers are registered.
 */
void usb_scan_devices(void)
{
	struct list_head *tmp;

	down (&usb_bus_list_lock);
	tmp = usb_bus_list.next;
	while (tmp != &usb_bus_list) {
		struct usb_bus *bus = list_entry(tmp,struct usb_bus, bus_list);

		tmp = tmp->next;
		usb_check_support(bus->root_hub);
	}
	up (&usb_bus_list_lock);
}

/*
 * This function is part of a depth-first search down the device tree,
 * removing any instances of a device driver.
 */
static void usb_drivers_purge(struct usb_driver *driver,struct usb_device *dev)
{
	int i;

	if (!dev) {
		err("null device being purged!!!");
		return;
	}

	for (i=0; i<USB_MAXCHILDREN; i++)
		if (dev->children[i])
			usb_drivers_purge(driver, dev->children[i]);

	if (!dev->actconfig)
		return;
			
	for (i = 0; i < dev->actconfig->bNumInterfaces; i++) {
		struct usb_interface *interface = &dev->actconfig->interface[i];
		
		if (interface->driver == driver) {
			if (driver->owner)
				__MOD_INC_USE_COUNT(driver->owner);
			down(&driver->serialize);
			driver->disconnect(dev, interface->private_data);
			up(&driver->serialize);
			if (driver->owner)
				__MOD_DEC_USE_COUNT(driver->owner);
			/* if driver->disconnect didn't release the interface */
			if (interface->driver)
				usb_driver_release_interface(driver, interface);
			/*
			 * This will go through the list looking for another
			 * driver that can handle the device
			 */
			usb_find_interface_driver(dev, i);
		}
	}
}

/**
 *	usb_deregister - unregister a USB driver
 *	@driver: USB operations of the driver to unregister
 *	Context: !in_interrupt ()
 *
 *	Unlinks the specified driver from the internal USB driver list.
 */
void usb_deregister(struct usb_driver *driver)
{
	struct list_head *tmp;

	info("deregistering driver %s", driver->name);
	if (driver->fops != NULL)
		usb_minors[driver->minor/16] = NULL;

	/*
	 * first we remove the driver, to be sure it doesn't get used by
	 * another thread while we are stepping through removing entries
	 */
	list_del(&driver->driver_list);

	down (&usb_bus_list_lock);
	tmp = usb_bus_list.next;
	while (tmp != &usb_bus_list) {
		struct usb_bus *bus = list_entry(tmp,struct usb_bus,bus_list);

		tmp = tmp->next;
		usb_drivers_purge(driver, bus->root_hub);
	}
	up (&usb_bus_list_lock);

	usbfs_update_special();
}

/**
 * usb_ifnum_to_if - get the interface object with a given interface number
 * @dev: the device whose current configuration is considered
 * @ifnum: the desired interface
 *
 * This walks the device descriptor for the currently active configuration
 * and returns a pointer to the interface with that particular interface
 * number, or null.
 *
 * Note that configuration descriptors are not required to assign interface
 * numbers sequentially, so that it would be incorrect to assume that
 * the first interface in that descriptor corresponds to interface zero.
 * This routine helps device drivers avoid such mistakes.
 * However, you should make sure that you do the right thing with any
 * alternate settings available for this interfaces.
 */
struct usb_interface *usb_ifnum_to_if(struct usb_device *dev, unsigned ifnum)
{
	int i;

	for (i = 0; i < dev->actconfig->bNumInterfaces; i++)
		if (dev->actconfig->interface[i].altsetting[0].bInterfaceNumber == ifnum)
			return &dev->actconfig->interface[i];

	return NULL;
}

/**
 * usb_epnum_to_ep_desc - get the endpoint object with a given endpoint number
 * @dev: the device whose current configuration is considered
 * @epnum: the desired endpoint
 *
 * This walks the device descriptor for the currently active configuration,
 * and returns a pointer to the endpoint with that particular endpoint
 * number, or null.
 *
 * Note that interface descriptors are not required to assign endpont
 * numbers sequentially, so that it would be incorrect to assume that
 * the first endpoint in that descriptor corresponds to interface zero.
 * This routine helps device drivers avoid such mistakes.
 */
struct usb_endpoint_descriptor *usb_epnum_to_ep_desc(struct usb_device *dev, unsigned epnum)
{
	int i, j, k;

	for (i = 0; i < dev->actconfig->bNumInterfaces; i++)
		for (j = 0; j < dev->actconfig->interface[i].num_altsetting; j++)
			for (k = 0; k < dev->actconfig->interface[i].altsetting[j].bNumEndpoints; k++)
				if (epnum == dev->actconfig->interface[i].altsetting[j].endpoint[k].bEndpointAddress)
					return &dev->actconfig->interface[i].altsetting[j].endpoint[k];

	return NULL;
}

/*
 * This function is for doing a depth-first search for devices which
 * have support, for dynamic loading of driver modules.
 */
static void usb_check_support(struct usb_device *dev)
{
	int i;

	if (!dev) {
		err("null device being checked!!!");
		return;
	}

	for (i=0; i<USB_MAXCHILDREN; i++)
		if (dev->children[i])
			usb_check_support(dev->children[i]);

	if (!dev->actconfig)
		return;

	/* now we check this device */
	if (dev->devnum > 0)
		for (i = 0; i < dev->actconfig->bNumInterfaces; i++)
			usb_find_interface_driver(dev, i);
}


/**
 * usb_driver_claim_interface - bind a driver to an interface
 * @driver: the driver to be bound
 * @iface: the interface to which it will be bound
 * @priv: driver data associated with that interface
 *
 * This is used by usb device drivers that need to claim more than one
 * interface on a device when probing (audio and acm are current examples).
 * No device driver should directly modify internal usb_interface or
 * usb_device structure members.
 *
 * Few drivers should need to use this routine, since the most natural
 * way to bind to an interface is to return the private data from
 * the driver's probe() method.  Any driver that does use this must
 * first be sure that no other driver has claimed the interface, by
 * checking with usb_interface_claimed().
 */
void usb_driver_claim_interface(struct usb_driver *driver, struct usb_interface *iface, void* priv)
{
	if (!iface || !driver)
		return;

	// FIXME change API to report an error in this case
	if (iface->driver)
	    err ("%s driver booted %s off interface %p",
	    	driver->name, iface->driver->name, iface);
	else
	    dbg("%s driver claimed interface %p", driver->name, iface);

	iface->driver = driver;
	iface->private_data = priv;
} /* usb_driver_claim_interface() */

/**
 * usb_interface_claimed - returns true iff an interface is claimed
 * @iface: the interface being checked
 *
 * This should be used by drivers to check other interfaces to see if
 * they are available or not.  If another driver has claimed the interface,
 * they may not claim it.  Otherwise it's OK to claim it using
 * usb_driver_claim_interface().
 *
 * Returns true (nonzero) iff the interface is claimed, else false (zero).
 */
int usb_interface_claimed(struct usb_interface *iface)
{
	if (!iface)
		return 0;

	return (iface->driver != NULL);
} /* usb_interface_claimed() */

/**
 * usb_driver_release_interface - unbind a driver from an interface
 * @driver: the driver to be unbound
 * @iface: the interface from which it will be unbound
 * 
 * This should be used by drivers to release their claimed interfaces.
 * It is normally called in their disconnect() methods, and only for
 * drivers that bound to more than one interface in their probe().
 *
 * When the USB subsystem disconnect()s a driver from some interface,
 * it automatically invokes this method for that interface.  That
 * means that even drivers that used usb_driver_claim_interface()
 * usually won't need to call this.
 */
void usb_driver_release_interface(struct usb_driver *driver, struct usb_interface *iface)
{
	/* this should never happen, don't release something that's not ours */
	if (!iface || iface->driver != driver)
		return;

	iface->driver = NULL;
	iface->private_data = NULL;
}


/**
 * usb_match_id - find first usb_device_id matching device or interface
 * @dev: the device whose descriptors are considered when matching
 * @interface: the interface of interest
 * @id: array of usb_device_id structures, terminated by zero entry
 *
 * usb_match_id searches an array of usb_device_id's and returns
 * the first one matching the device or interface, or null.
 * This is used when binding (or rebinding) a driver to an interface.
 * Most USB device drivers will use this indirectly, through the usb core,
 * but some layered driver frameworks use it directly.
 * These device tables are exported with MODULE_DEVICE_TABLE, through
 * modutils and "modules.usbmap", to support the driver loading
 * functionality of USB hotplugging.
 *
 * What Matches:
 *
 * The "match_flags" element in a usb_device_id controls which
 * members are used.  If the corresponding bit is set, the
 * value in the device_id must match its corresponding member
 * in the device or interface descriptor, or else the device_id
 * does not match.
 *
 * "driver_info" is normally used only by device drivers,
 * but you can create a wildcard "matches anything" usb_device_id
 * as a driver's "modules.usbmap" entry if you provide an id with
 * only a nonzero "driver_info" field.  If you do this, the USB device
 * driver's probe() routine should use additional intelligence to
 * decide whether to bind to the specified interface.
 * 
 * What Makes Good usb_device_id Tables:
 *
 * The match algorithm is very simple, so that intelligence in
 * driver selection must come from smart driver id records.
 * Unless you have good reasons to use another selection policy,
 * provide match elements only in related groups, and order match
 * specifiers from specific to general.  Use the macros provided
 * for that purpose if you can.
 *
 * The most specific match specifiers use device descriptor
 * data.  These are commonly used with product-specific matches;
 * the USB_DEVICE macro lets you provide vendor and product IDs,
 * and you can also match against ranges of product revisions.
 * These are widely used for devices with application or vendor
 * specific bDeviceClass values.
 *
 * Matches based on device class/subclass/protocol specifications
 * are slightly more general; use the USB_DEVICE_INFO macro, or
 * its siblings.  These are used with single-function devices
 * where bDeviceClass doesn't specify that each interface has
 * its own class. 
 *
 * Matches based on interface class/subclass/protocol are the
 * most general; they let drivers bind to any interface on a
 * multiple-function device.  Use the USB_INTERFACE_INFO
 * macro, or its siblings, to match class-per-interface style 
 * devices (as recorded in bDeviceClass).
 *  
 * Within those groups, remember that not all combinations are
 * meaningful.  For example, don't give a product version range
 * without vendor and product IDs; or specify a protocol without
 * its associated class and subclass.
 */   
const struct usb_device_id *
usb_match_id(struct usb_device *dev, struct usb_interface *interface,
	     const struct usb_device_id *id)
{
	struct usb_interface_descriptor	*intf = 0;

	/* proc_connectinfo in devio.c may call us with id == NULL. */
	if (id == NULL)
		return NULL;

	/* It is important to check that id->driver_info is nonzero,
	   since an entry that is all zeroes except for a nonzero
	   id->driver_info is the way to create an entry that
	   indicates that the driver want to examine every
	   device and interface. */
	for (; id->idVendor || id->bDeviceClass || id->bInterfaceClass ||
	       id->driver_info; id++) {

		if ((id->match_flags & USB_DEVICE_ID_MATCH_VENDOR) &&
		    id->idVendor != dev->descriptor.idVendor)
			continue;

		if ((id->match_flags & USB_DEVICE_ID_MATCH_PRODUCT) &&
		    id->idProduct != dev->descriptor.idProduct)
			continue;

		/* No need to test id->bcdDevice_lo != 0, since 0 is never
		   greater than any unsigned number. */
		if ((id->match_flags & USB_DEVICE_ID_MATCH_DEV_LO) &&
		    (id->bcdDevice_lo > dev->descriptor.bcdDevice))
			continue;

		if ((id->match_flags & USB_DEVICE_ID_MATCH_DEV_HI) &&
		    (id->bcdDevice_hi < dev->descriptor.bcdDevice))
			continue;

		if ((id->match_flags & USB_DEVICE_ID_MATCH_DEV_CLASS) &&
		    (id->bDeviceClass != dev->descriptor.bDeviceClass))
			continue;

		if ((id->match_flags & USB_DEVICE_ID_MATCH_DEV_SUBCLASS) &&
		    (id->bDeviceSubClass!= dev->descriptor.bDeviceSubClass))
			continue;

		if ((id->match_flags & USB_DEVICE_ID_MATCH_DEV_PROTOCOL) &&
		    (id->bDeviceProtocol != dev->descriptor.bDeviceProtocol))
			continue;

		intf = &interface->altsetting [interface->act_altsetting];

		if ((id->match_flags & USB_DEVICE_ID_MATCH_INT_CLASS) &&
		    (id->bInterfaceClass != intf->bInterfaceClass))
			continue;

		if ((id->match_flags & USB_DEVICE_ID_MATCH_INT_SUBCLASS) &&
		    (id->bInterfaceSubClass != intf->bInterfaceSubClass))
		    continue;

		if ((id->match_flags & USB_DEVICE_ID_MATCH_INT_PROTOCOL) &&
		    (id->bInterfaceProtocol != intf->bInterfaceProtocol))
		    continue;

		return id;
	}

	return NULL;
}

/*
 * This entrypoint gets called for each new device.
 *
 * We now walk the list of registered USB drivers,
 * looking for one that will accept this interface.
 *
 * "New Style" drivers use a table describing the devices and interfaces
 * they handle.  Those tables are available to user mode tools deciding
 * whether to load driver modules for a new device.
 *
 * The probe return value is changed to be a private pointer.  This way
 * the drivers don't have to dig around in our structures to set the
 * private pointer if they only need one interface. 
 *
 * Returns: 0 if a driver accepted the interface, -1 otherwise
 */
static int usb_find_interface_driver(struct usb_device *dev, unsigned ifnum)
{
	struct list_head *tmp;
	struct usb_interface *interface;
	void *private;
	const struct usb_device_id *id;
	struct usb_driver *driver;
	int i;
	
	if ((!dev) || (ifnum >= dev->actconfig->bNumInterfaces)) {
		err("bad find_interface_driver params");
		return -1;
	}

	down(&dev->serialize);

	interface = dev->actconfig->interface + ifnum;

	if (usb_interface_claimed(interface))
		goto out_err;

	private = NULL;
	for (tmp = usb_driver_list.next; tmp != &usb_driver_list;) {
		driver = list_entry(tmp, struct usb_driver, driver_list);
		tmp = tmp->next;

		if (driver->owner)
			__MOD_INC_USE_COUNT(driver->owner);
		id = driver->id_table;
		/* new style driver? */
		if (id) {
			for (i = 0; i < interface->num_altsetting; i++) {
			  	interface->act_altsetting = i;
				id = usb_match_id(dev, interface, id);
				if (id) {
					down(&driver->serialize);
					private = driver->probe(dev,ifnum,id);
					up(&driver->serialize);
					if (private != NULL)
						break;
				}
			}

			/* if driver not bound, leave defaults unchanged */
			if (private == NULL)
				interface->act_altsetting = 0;
		} else { /* "old style" driver */
			down(&driver->serialize);
			private = driver->probe(dev, ifnum, NULL);
			up(&driver->serialize);
		}
		if (driver->owner)
			__MOD_DEC_USE_COUNT(driver->owner);

		/* probe() may have changed the config on us */
		interface = dev->actconfig->interface + ifnum;

		if (private) {
			usb_driver_claim_interface(driver, interface, private);
			up(&dev->serialize);
			return 0;
		}
	}

out_err:
	up(&dev->serialize);
	return -1;
}


#ifdef	CONFIG_HOTPLUG

/*
 * USB hotplugging invokes what /proc/sys/kernel/hotplug says
 * (normally /sbin/hotplug) when USB devices get added or removed.
 *
 * This invokes a user mode policy agent, typically helping to load driver
 * or other modules, configure the device, and more.  Drivers can provide
 * a MODULE_DEVICE_TABLE to help with module loading subtasks.
 *
 * Some synchronization is important: removes can't start processing
 * before the add-device processing completes, and vice versa.  That keeps
 * a stack of USB-related identifiers stable while they're in use.  If we
 * know that agents won't complete after they return (such as by forking
 * a process that completes later), it's enough to just waitpid() for the
 * agent -- as is currently done.
 *
 * The reason: we know we're called either from khubd (the typical case)
 * or from root hub initialization (init, kapmd, modprobe, etc).  In both
 * cases, we know no other thread can recycle our address, since we must
 * already have been serialized enough to prevent that.
 */
static void call_policy (char *verb, struct usb_device *dev)
{
	char *argv [3], **envp, *buf, *scratch;
	int i = 0, value;

	if (!hotplug_path [0])
		return;
	if (in_interrupt ()) {
		dbg ("In_interrupt");
		return;
	}
	if (!current->fs->root) {
		/* statically linked USB is initted rather early */
		dbg ("call_policy %s, num %d -- no FS yet", verb, dev->devnum);
		return;
	}
	if (dev->devnum < 0) {
		dbg ("device already deleted ??");
		return;
	}
	if (!(envp = (char **) kmalloc (20 * sizeof (char *), GFP_KERNEL))) {
		dbg ("enomem");
		return;
	}
	if (!(buf = kmalloc (256, GFP_KERNEL))) {
		kfree (envp);
		dbg ("enomem2");
		return;
	}

	/* only one standardized param to hotplug command: type */
	argv [0] = hotplug_path;
	argv [1] = "usb";
	argv [2] = 0;

	/* minimal command environment */
	envp [i++] = "HOME=/";
	envp [i++] = "PATH=/sbin:/bin:/usr/sbin:/usr/bin";

#ifdef	DEBUG
	/* hint that policy agent should enter no-stdout debug mode */
	envp [i++] = "DEBUG=kernel";
#endif
	/* extensible set of named bus-specific parameters,
	 * supporting multiple driver selection algorithms.
	 */
	scratch = buf;

	/* action:  add, remove */
	envp [i++] = scratch;
	scratch += sprintf (scratch, "ACTION=%s", verb) + 1;

#ifdef	CONFIG_USB_DEVICEFS
	/* If this is available, userspace programs can directly read
	 * all the device descriptors we don't tell them about.  Or
	 * even act as usermode drivers.
	 *
	 * FIXME reduce hardwired intelligence here
	 */
	envp [i++] = "DEVFS=/proc/bus/usb";
	envp [i++] = scratch;
	scratch += sprintf (scratch, "DEVICE=/proc/bus/usb/%03d/%03d",
		dev->bus->busnum, dev->devnum) + 1;
#endif

	/* per-device configuration hacks are common */
	envp [i++] = scratch;
	scratch += sprintf (scratch, "PRODUCT=%x/%x/%x",
		dev->descriptor.idVendor,
		dev->descriptor.idProduct,
		dev->descriptor.bcdDevice) + 1;

	/* class-based driver binding models */
	envp [i++] = scratch;
	scratch += sprintf (scratch, "TYPE=%d/%d/%d",
			    dev->descriptor.bDeviceClass,
			    dev->descriptor.bDeviceSubClass,
			    dev->descriptor.bDeviceProtocol) + 1;
	if (dev->descriptor.bDeviceClass == 0) {
		int alt = dev->actconfig->interface [0].act_altsetting;

		/* a simple/common case: one config, one interface, one driver
		 * with current altsetting being a reasonable setting.
		 * everything needs a smart agent and usbfs; or can rely on
		 * device-specific binding policies.
		 */
		envp [i++] = scratch;
		scratch += sprintf (scratch, "INTERFACE=%d/%d/%d",
			dev->actconfig->interface [0].altsetting [alt].bInterfaceClass,
			dev->actconfig->interface [0].altsetting [alt].bInterfaceSubClass,
			dev->actconfig->interface [0].altsetting [alt].bInterfaceProtocol)
			+ 1;
		/* INTERFACE-0, INTERFACE-1, ... ? */
	}
	envp [i++] = 0;
	/* assert: (scratch - buf) < sizeof buf */

	/* NOTE: user mode daemons can call the agents too */

	dbg ("kusbd: %s %s %d", argv [0], verb, dev->devnum);
	value = call_usermodehelper (argv [0], argv, envp);
	kfree (buf);
	kfree (envp);
	if (value != 0)
		dbg ("kusbd policy returned 0x%x", value);
}

#else

static inline void
call_policy (char *verb, struct usb_device *dev)
{ } 

#endif	/* CONFIG_HOTPLUG */


/*
 * This entrypoint gets called for each new device.
 *
 * All interfaces are scanned for matching drivers.
 */
static void usb_find_drivers(struct usb_device *dev)
{
	unsigned ifnum;
	unsigned rejected = 0;
	unsigned claimed = 0;

	for (ifnum = 0; ifnum < dev->actconfig->bNumInterfaces; ifnum++) {
		struct usb_interface *interface = &dev->actconfig->interface[ifnum];
		
		/* register this interface with driverfs */
		interface->dev.parent = &dev->dev;
		sprintf (&interface->dev.bus_id[0], "%03d", ifnum);
		sprintf (&interface->dev.name[0], "figure out some name...");
		device_register (&interface->dev);

		/* if this interface hasn't already been claimed */
		if (!usb_interface_claimed(interface)) {
			if (usb_find_interface_driver(dev, ifnum))
				rejected++;
			else
				claimed++;
		}
	}
 
	if (rejected)
		dbg("unhandled interfaces on device");

	if (!claimed) {
		warn("USB device %d (vend/prod 0x%x/0x%x) is not claimed by any active driver.",
			dev->devnum,
			dev->descriptor.idVendor,
			dev->descriptor.idProduct);
#ifdef DEBUG
		usb_show_device(dev);
#endif
	}
}

/**
 * usb_alloc_dev - allocate a usb device structure (usbcore-internal)
 * @parent: hub to which device is connected
 * @bus: bus used to access the device
 * Context: !in_interrupt ()
 *
 * Only hub drivers (including virtual root hub drivers for host
 * controllers) should ever call this.
 *
 * This call is synchronous, and may not be used in an interrupt context.
 */
struct usb_device *usb_alloc_dev(struct usb_device *parent, struct usb_bus *bus)
{
	struct usb_device *dev;

	dev = kmalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return NULL;

	memset(dev, 0, sizeof(*dev));

	usb_bus_get(bus);

	if (!parent)
		dev->devpath [0] = '/';
	dev->bus = bus;
	dev->parent = parent;
	atomic_set(&dev->refcnt, 1);
	INIT_LIST_HEAD(&dev->filelist);

	init_MUTEX(&dev->serialize);

	dev->bus->op->allocate(dev);

	return dev;
}

// usbcore-internal ...
// but usb_dec_dev_use() is #defined to this, and that's public!!
void usb_free_dev(struct usb_device *dev)
{
	if (atomic_dec_and_test(&dev->refcnt)) {
		dev->bus->op->deallocate(dev);
		usb_destroy_configuration(dev);

		usb_bus_put(dev->bus);

		kfree(dev);
	}
}

/**
 * usb_inc_dev_use - record another reference to a device
 * @dev: the device being referenced
 *
 * Each live reference to a device should be refcounted.
 *
 * Device drivers should normally record such references in their
 * open() methods.
 * Drivers should then release them, using usb_dec_dev_use(), in their
 * close() methods.
 */
void usb_inc_dev_use(struct usb_device *dev)
{
	atomic_inc(&dev->refcnt);
}


/**
 * usb_alloc_urb - creates a new urb for a USB driver to use
 * @iso_packets: number of iso packets for this urb
 * @mem_flags: the type of memory to allocate, see kmalloc() for a list of
 *	valid options for this.
 *
 * Creates an urb for the USB driver to use, initializes a few internal
 * structures, incrementes the usage counter, and returns a pointer to it.
 *
 * If no memory is available, NULL is returned.
 *
 * If the driver want to use this urb for interrupt, control, or bulk
 * endpoints, pass '0' as the number of iso packets.
 *
 * The driver must call usb_free_urb() when it is finished with the urb.
 */
struct urb *usb_alloc_urb(int iso_packets, int mem_flags)
{
	struct urb *urb;

	urb = (struct urb *)kmalloc(sizeof(struct urb) + 
		iso_packets * sizeof(struct usb_iso_packet_descriptor),
		mem_flags);
	if (!urb) {
		err("alloc_urb: kmalloc failed");
		return NULL;
	}

	memset(urb, 0, sizeof(*urb));
	urb->count = (atomic_t)ATOMIC_INIT(1);
	spin_lock_init(&urb->lock);

	return urb;
}

/**
 * usb_free_urb - frees the memory used by a urb when all users of it are finished
 * @urb: pointer to the urb to free
 *
 * Must be called when a user of a urb is finished with it.  When the last user
 * of the urb calls this function, the memory of the urb is freed.
 *
 * Note: The transfer buffer associated with the urb is not freed, that must be
 * done elsewhere.
 */
void usb_free_urb(struct urb *urb)
{
	if (urb)
		if (atomic_dec_and_test(&urb->count))
			kfree(urb);
}

/**
 * usb_get_urb - incrementes the reference count of the urb
 * @urb: pointer to the urb to modify
 *
 * This must be  called whenever a urb is transfered from a device driver to a
 * host controller driver.  This allows proper reference counting to happen
 * for urbs.
 *
 * A pointer to the urb with the incremented reference counter is returned.
 */
struct urb * usb_get_urb(struct urb *urb)
{
	if (urb) {
		atomic_inc(&urb->count);
		return urb;
	} else
		return NULL;
}
		
		
/*-------------------------------------------------------------------*/

/**
 * usb_submit_urb - asynchronously issue a transfer request for an endpoint
 * @urb: pointer to the urb describing the request
 * @mem_flags: the type of memory to allocate, see kmalloc() for a list
 *	of valid options for this.
 *
 * This submits a transfer request, and transfers control of the URB
 * describing that request to the USB subsystem.  Request completion will
 * indicated later, asynchronously, by calling the completion handler.
 * This call may be issued in interrupt context.
 *
 * The caller must have correctly initialized the URB before submitting
 * it.  Functions such as usb_fill_bulk_urb() and usb_fill_control_urb() are
 * available to ensure that most fields are correctly initialized, for
 * the particular kind of transfer, although they will not initialize
 * any transfer flags.
 *
 * Successful submissions return 0; otherwise this routine returns a
 * negative error number.  If the submission is successful, the complete
 * fuction of the urb will be called when the USB host driver is
 * finished with the urb (either a successful transmission, or some
 * error case.)
 *
 * Unreserved Bandwidth Transfers:
 *
 * Bulk or control requests complete only once.  When the completion
 * function is called, control of the URB is returned to the device
 * driver which issued the request.  The completion handler may then
 * immediately free or reuse that URB.
 *
 * Bulk URBs will be queued if the USB_QUEUE_BULK transfer flag is set
 * in the URB.  This can be used to maximize bandwidth utilization by
 * letting the USB controller start work on the next URB without any
 * delay to report completion (scheduling and processing an interrupt)
 * and then submit that next request.
 *
 * For control endpoints, the synchronous usb_control_msg() call is
 * often used (in non-interrupt context) instead of this call.
 *
 * Reserved Bandwidth Transfers:
 *
 * Periodic URBs (interrupt or isochronous) are completed repeatedly,
 * until the original request is aborted.  When the completion callback
 * indicates the URB has been unlinked (with a special status code),
 * control of that URB returns to the device driver.  Otherwise, the
 * completion handler does not control the URB, and should not change
 * any of its fields.
 *
 * Note that isochronous URBs should be submitted in a "ring" data
 * structure (using urb->next) to ensure that they are resubmitted
 * appropriately.
 *
 * If the USB subsystem can't reserve sufficient bandwidth to perform
 * the periodic request, and bandwidth reservation is being done for
 * this controller, submitting such a periodic request will fail.
 *
 * Memory Flags:
 *
 * General rules for how to decide which mem_flags to use:
 * 
 * Basically the rules are the same as for kmalloc.  There are four
 * different possible values; GFP_KERNEL, GFP_NOFS, GFP_NOIO and
 * GFP_ATOMIC.
 *
 * GFP_NOFS is not ever used, as it has not been implemented yet.
 *
 * There are three situations you must use GFP_ATOMIC.
 *    a) you are inside a completion handler, an interrupt, bottom half,
 *       tasklet or timer.
 *    b) you are holding a spinlock or rwlock (does not apply to
 *       semaphores)
 *    c) current->state != TASK_RUNNING, this is the case only after
 *       you've changed it.
 * 
 * GFP_NOIO is used in the block io path and error handling of storage
 * devices.
 *
 * All other situations use GFP_KERNEL.
 *
 * Specfic rules for how to decide which mem_flags to use:
 *
 *    - start_xmit, timeout, and receive methods of network drivers must
 *      use GFP_ATOMIC (spinlock)
 *    - queuecommand methods of scsi drivers must use GFP_ATOMIC (spinlock)
 *    - If you use a kernel thread with a network driver you must use
 *      GFP_NOIO, unless b) or c) apply
 *    - After you have done a down() you use GFP_KERNEL, unless b) or c)
 *      apply or your are in a storage driver's block io path
 *    - probe and disconnect use GFP_KERNEL unless b) or c) apply
 *    - Changing firmware on a running storage or net device uses
 *      GFP_NOIO, unless b) or c) apply
 *
 */
int usb_submit_urb(struct urb *urb, int mem_flags)
{
	if (urb && urb->dev && urb->dev->bus && urb->dev->bus->op)
		return urb->dev->bus->op->submit_urb(urb, mem_flags);
	else
		return -ENODEV;
}

/*-------------------------------------------------------------------*/

/**
 * usb_unlink_urb - abort/cancel a transfer request for an endpoint
 * @urb: pointer to urb describing a previously submitted request
 *
 * This routine cancels an in-progress request.  The requests's
 * completion handler will be called with a status code indicating
 * that the request has been canceled, and that control of the URB
 * has been returned to that device driver.  This is the only way
 * to stop an interrupt transfer, so long as the device is connected.
 *
 * When the USB_ASYNC_UNLINK transfer flag for the URB is clear, this
 * request is synchronous.  Success is indicated by returning zero,
 * at which time the urb will have been unlinked,
 * and the completion function will see status -ENOENT.  Failure is
 * indicated by any other return value.  This mode may not be used
 * when unlinking an urb from an interrupt context, such as a bottom
 * half or a completion handler,
 *
 * When the USB_ASYNC_UNLINK transfer flag for the URB is set, this
 * request is asynchronous.  Success is indicated by returning -EINPROGRESS,
 * at which time the urb will normally not have been unlinked,
 * and the completion function will see status -ECONNRESET.  Failure is
 * indicated by any other return value.
 */
int usb_unlink_urb(struct urb *urb)
{
	if (urb && urb->dev && urb->dev->bus && urb->dev->bus->op)
		return urb->dev->bus->op->unlink_urb(urb);
	else
		return -ENODEV;
}
/*-------------------------------------------------------------------*
 *                         SYNCHRONOUS CALLS                         *
 *-------------------------------------------------------------------*/

struct usb_api_data {
	wait_queue_head_t wqh;
	int done;
};

static void usb_api_blocking_completion(struct urb *urb)
{
	struct usb_api_data *awd = (struct usb_api_data *)urb->context;

	awd->done = 1;
	wmb();
	wake_up(&awd->wqh);
}

// Starts urb and waits for completion or timeout
static int usb_start_wait_urb(struct urb *urb, int timeout, int* actual_length)
{ 
	DECLARE_WAITQUEUE(wait, current);
	struct usb_api_data awd;
	int status;

	init_waitqueue_head(&awd.wqh); 	
	awd.done = 0;

	set_current_state(TASK_UNINTERRUPTIBLE);
	add_wait_queue(&awd.wqh, &wait);

	urb->context = &awd;
	status = usb_submit_urb(urb, GFP_KERNEL);
	if (status) {
		// something went wrong
		usb_free_urb(urb);
		set_current_state(TASK_RUNNING);
		remove_wait_queue(&awd.wqh, &wait);
		return status;
	}

	while (timeout && !awd.done)
	{
		timeout = schedule_timeout(timeout);
		set_current_state(TASK_UNINTERRUPTIBLE);
		rmb();
	}

	set_current_state(TASK_RUNNING);
	remove_wait_queue(&awd.wqh, &wait);

	if (!timeout && !awd.done) {
		if (urb->status != -EINPROGRESS) {	/* No callback?!! */
			printk(KERN_ERR "usb: raced timeout, "
			    "pipe 0x%x status %d time left %d\n",
			    urb->pipe, urb->status, timeout);
			status = urb->status;
		} else {
			printk("usb_control/bulk_msg: timeout\n");
			usb_unlink_urb(urb);  // remove urb safely
			status = -ETIMEDOUT;
		}
	} else
		status = urb->status;

	if (actual_length)
		*actual_length = urb->actual_length;

	usb_free_urb(urb);
  	return status;
}

/*-------------------------------------------------------------------*/
// returns status (negative) or length (positive)
int usb_internal_control_msg(struct usb_device *usb_dev, unsigned int pipe, 
			    struct usb_ctrlrequest *cmd,  void *data, int len, int timeout)
{
	struct urb *urb;
	int retv;
	int length;

	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb)
		return -ENOMEM;
  
	FILL_CONTROL_URB(urb, usb_dev, pipe, (unsigned char*)cmd, data, len,
		   usb_api_blocking_completion, 0);

	retv = usb_start_wait_urb(urb, timeout, &length);
	if (retv < 0)
		return retv;
	else
		return length;
}

/**
 *	usb_control_msg - Builds a control urb, sends it off and waits for completion
 *	@dev: pointer to the usb device to send the message to
 *	@pipe: endpoint "pipe" to send the message to
 *	@request: USB message request value
 *	@requesttype: USB message request type value
 *	@value: USB message value
 *	@index: USB message index value
 *	@data: pointer to the data to send
 *	@size: length in bytes of the data to send
 *	@timeout: time in jiffies to wait for the message to complete before
 *		timing out (if 0 the wait is forever)
 *	Context: !in_interrupt ()
 *
 *	This function sends a simple control message to a specified endpoint
 *	and waits for the message to complete, or timeout.
 *	
 *	If successful, it returns 0, otherwise a negative error number.
 *
 *	Don't use this function from within an interrupt context, like a
 *	bottom half handler.  If you need an asynchronous message, or need to send
 *	a message from within interrupt context, use usb_submit_urb()
 */
int usb_control_msg(struct usb_device *dev, unsigned int pipe, __u8 request, __u8 requesttype,
			 __u16 value, __u16 index, void *data, __u16 size, int timeout)
{
	struct usb_ctrlrequest *dr = kmalloc(sizeof(struct usb_ctrlrequest), GFP_KERNEL);
	int ret;
	
	if (!dr)
		return -ENOMEM;

	dr->bRequestType= requesttype;
	dr->bRequest = request;
	dr->wValue = cpu_to_le16p(&value);
	dr->wIndex = cpu_to_le16p(&index);
	dr->wLength = cpu_to_le16p(&size);

	//dbg("usb_control_msg");	

	ret = usb_internal_control_msg(dev, pipe, dr, data, size, timeout);

	kfree(dr);

	return ret;
}


/**
 *	usb_bulk_msg - Builds a bulk urb, sends it off and waits for completion
 *	@usb_dev: pointer to the usb device to send the message to
 *	@pipe: endpoint "pipe" to send the message to
 *	@data: pointer to the data to send
 *	@len: length in bytes of the data to send
 *	@actual_length: pointer to a location to put the actual length transferred in bytes
 *	@timeout: time in jiffies to wait for the message to complete before
 *		timing out (if 0 the wait is forever)
 *	Context: !in_interrupt ()
 *
 *	This function sends a simple bulk message to a specified endpoint
 *	and waits for the message to complete, or timeout.
 *	
 *	If successful, it returns 0, otherwise a negative error number.
 *	The number of actual bytes transferred will be stored in the 
 *	actual_length paramater.
 *
 *	Don't use this function from within an interrupt context, like a
 *	bottom half handler.  If you need an asynchronous message, or need to
 *	send a message from within interrupt context, use usb_submit_urb()
 */
int usb_bulk_msg(struct usb_device *usb_dev, unsigned int pipe, 
			void *data, int len, int *actual_length, int timeout)
{
	struct urb *urb;

	if (len < 0)
		return -EINVAL;

	urb=usb_alloc_urb(0, GFP_KERNEL);
	if (!urb)
		return -ENOMEM;

	FILL_BULK_URB(urb, usb_dev, pipe, data, len,
		    usb_api_blocking_completion, 0);

	return usb_start_wait_urb(urb,timeout,actual_length);
}

/**
 * usb_get_current_frame_number - return current bus frame number
 * @dev: the device whose bus is being queried
 *
 * Returns the current frame number for the USB host controller
 * used with the given USB device.  This can be used when scheduling
 * isochronous requests.
 *
 * Note that different kinds of host controller have different
 * "scheduling horizons".  While one type might support scheduling only
 * 32 frames into the future, others could support scheduling up to
 * 1024 frames into the future.
 */
int usb_get_current_frame_number(struct usb_device *dev)
{
	return dev->bus->op->get_frame_number (dev);
}

/*-------------------------------------------------------------------*/

static int usb_parse_endpoint(struct usb_endpoint_descriptor *endpoint, unsigned char *buffer, int size)
{
	struct usb_descriptor_header *header;
	unsigned char *begin;
	int parsed = 0, len, numskipped;

	header = (struct usb_descriptor_header *)buffer;

	/* Everything should be fine being passed into here, but we sanity */
	/*  check JIC */
	if (header->bLength > size) {
		err("ran out of descriptors parsing");
		return -1;
	}
		
	if (header->bDescriptorType != USB_DT_ENDPOINT) {
		warn("unexpected descriptor 0x%X, expecting endpoint descriptor, type 0x%X",
			endpoint->bDescriptorType, USB_DT_ENDPOINT);
		return parsed;
	}

	if (header->bLength == USB_DT_ENDPOINT_AUDIO_SIZE)
		memcpy(endpoint, buffer, USB_DT_ENDPOINT_AUDIO_SIZE);
	else
		memcpy(endpoint, buffer, USB_DT_ENDPOINT_SIZE);
	
	le16_to_cpus(&endpoint->wMaxPacketSize);

	buffer += header->bLength;
	size -= header->bLength;
	parsed += header->bLength;

	/* Skip over the rest of the Class Specific or Vendor Specific */
	/*  descriptors */
	begin = buffer;
	numskipped = 0;
	while (size >= sizeof(struct usb_descriptor_header)) {
		header = (struct usb_descriptor_header *)buffer;

		if (header->bLength < 2) {
			err("invalid descriptor length of %d", header->bLength);
			return -1;
		}

		/* If we find another "proper" descriptor then we're done  */
		if ((header->bDescriptorType == USB_DT_ENDPOINT) ||
		    (header->bDescriptorType == USB_DT_INTERFACE) ||
		    (header->bDescriptorType == USB_DT_CONFIG) ||
		    (header->bDescriptorType == USB_DT_DEVICE))
			break;

		dbg("skipping descriptor 0x%X",
			header->bDescriptorType);
		numskipped++;

		buffer += header->bLength;
		size -= header->bLength;
		parsed += header->bLength;
	}
	if (numskipped)
		dbg("skipped %d class/vendor specific endpoint descriptors", numskipped);

	/* Copy any unknown descriptors into a storage area for drivers */
	/*  to later parse */
	len = (int)(buffer - begin);
	if (!len) {
		endpoint->extra = NULL;
		endpoint->extralen = 0;
		return parsed;
	}

	endpoint->extra = kmalloc(len, GFP_KERNEL);

	if (!endpoint->extra) {
		err("couldn't allocate memory for endpoint extra descriptors");
		endpoint->extralen = 0;
		return parsed;
	}

	memcpy(endpoint->extra, begin, len);
	endpoint->extralen = len;

	return parsed;
}

static int usb_parse_interface(struct usb_interface *interface, unsigned char *buffer, int size)
{
	int i, len, numskipped, retval, parsed = 0;
	struct usb_descriptor_header *header;
	struct usb_interface_descriptor *ifp;
	unsigned char *begin;

	interface->act_altsetting = 0;
	interface->num_altsetting = 0;
	interface->max_altsetting = USB_ALTSETTINGALLOC;

	interface->altsetting = kmalloc(sizeof(struct usb_interface_descriptor) * interface->max_altsetting, GFP_KERNEL);
	
	if (!interface->altsetting) {
		err("couldn't kmalloc interface->altsetting");
		return -1;
	}

	while (size > 0) {
		if (interface->num_altsetting >= interface->max_altsetting) {
			void *ptr;
			int oldmas;

			oldmas = interface->max_altsetting;
			interface->max_altsetting += USB_ALTSETTINGALLOC;
			if (interface->max_altsetting > USB_MAXALTSETTING) {
				warn("too many alternate settings (max %d)",
					USB_MAXALTSETTING);
				return -1;
			}

			ptr = interface->altsetting;
			interface->altsetting = kmalloc(sizeof(struct usb_interface_descriptor) * interface->max_altsetting, GFP_KERNEL);
			if (!interface->altsetting) {
				err("couldn't kmalloc interface->altsetting");
				interface->altsetting = ptr;
				return -1;
			}
			memcpy(interface->altsetting, ptr, sizeof(struct usb_interface_descriptor) * oldmas);

			kfree(ptr);
		}

		ifp = interface->altsetting + interface->num_altsetting;
		ifp->endpoint = NULL;
		ifp->extra = NULL;
		ifp->extralen = 0;
		interface->num_altsetting++;

		memcpy(ifp, buffer, USB_DT_INTERFACE_SIZE);

		/* Skip over the interface */
		buffer += ifp->bLength;
		parsed += ifp->bLength;
		size -= ifp->bLength;

		begin = buffer;
		numskipped = 0;

		/* Skip over any interface, class or vendor descriptors */
		while (size >= sizeof(struct usb_descriptor_header)) {
			header = (struct usb_descriptor_header *)buffer;

			if (header->bLength < 2) {
				err("invalid descriptor length of %d", header->bLength);
				return -1;
			}

			/* If we find another "proper" descriptor then we're done  */
			if ((header->bDescriptorType == USB_DT_INTERFACE) ||
			    (header->bDescriptorType == USB_DT_ENDPOINT) ||
			    (header->bDescriptorType == USB_DT_CONFIG) ||
			    (header->bDescriptorType == USB_DT_DEVICE))
				break;

			numskipped++;

			buffer += header->bLength;
			parsed += header->bLength;
			size -= header->bLength;
		}

		if (numskipped)
			dbg("skipped %d class/vendor specific interface descriptors", numskipped);

		/* Copy any unknown descriptors into a storage area for */
		/*  drivers to later parse */
		len = (int)(buffer - begin);
		if (len) {
			ifp->extra = kmalloc(len, GFP_KERNEL);

			if (!ifp->extra) {
				err("couldn't allocate memory for interface extra descriptors");
				ifp->extralen = 0;
				return -1;
			}
			memcpy(ifp->extra, begin, len);
			ifp->extralen = len;
		}

		/* Did we hit an unexpected descriptor? */
		header = (struct usb_descriptor_header *)buffer;
		if ((size >= sizeof(struct usb_descriptor_header)) &&
		    ((header->bDescriptorType == USB_DT_CONFIG) ||
		     (header->bDescriptorType == USB_DT_DEVICE)))
			return parsed;

		if (ifp->bNumEndpoints > USB_MAXENDPOINTS) {
			warn("too many endpoints");
			return -1;
		}

		ifp->endpoint = (struct usb_endpoint_descriptor *)
			kmalloc(ifp->bNumEndpoints *
			sizeof(struct usb_endpoint_descriptor), GFP_KERNEL);
		if (!ifp->endpoint) {
			err("out of memory");
			return -1;	
		}

		memset(ifp->endpoint, 0, ifp->bNumEndpoints *
			sizeof(struct usb_endpoint_descriptor));
	
		for (i = 0; i < ifp->bNumEndpoints; i++) {
			header = (struct usb_descriptor_header *)buffer;

			if (header->bLength > size) {
				err("ran out of descriptors parsing");
				return -1;
			}
		
			retval = usb_parse_endpoint(ifp->endpoint + i, buffer, size);
			if (retval < 0)
				return retval;

			buffer += retval;
			parsed += retval;
			size -= retval;
		}

		/* We check to see if it's an alternate to this one */
		ifp = (struct usb_interface_descriptor *)buffer;
		if (size < USB_DT_INTERFACE_SIZE ||
		    ifp->bDescriptorType != USB_DT_INTERFACE ||
		    !ifp->bAlternateSetting)
			return parsed;
	}

	return parsed;
}

int usb_parse_configuration(struct usb_config_descriptor *config, char *buffer)
{
	int i, retval, size;
	struct usb_descriptor_header *header;

	memcpy(config, buffer, USB_DT_CONFIG_SIZE);
	le16_to_cpus(&config->wTotalLength);
	size = config->wTotalLength;

	if (config->bNumInterfaces > USB_MAXINTERFACES) {
		warn("too many interfaces");
		return -1;
	}

	config->interface = (struct usb_interface *)
		kmalloc(config->bNumInterfaces *
		sizeof(struct usb_interface), GFP_KERNEL);
	dbg("kmalloc IF %p, numif %i", config->interface, config->bNumInterfaces);
	if (!config->interface) {
		err("out of memory");
		return -1;	
	}

	memset(config->interface, 0,
	       config->bNumInterfaces * sizeof(struct usb_interface));

	buffer += config->bLength;
	size -= config->bLength;
	
	config->extra = NULL;
	config->extralen = 0;

	for (i = 0; i < config->bNumInterfaces; i++) {
		int numskipped, len;
		char *begin;

		/* Skip over the rest of the Class Specific or Vendor */
		/*  Specific descriptors */
		begin = buffer;
		numskipped = 0;
		while (size >= sizeof(struct usb_descriptor_header)) {
			header = (struct usb_descriptor_header *)buffer;

			if ((header->bLength > size) || (header->bLength < 2)) {
				err("invalid descriptor length of %d", header->bLength);
				return -1;
			}

			/* If we find another "proper" descriptor then we're done  */
			if ((header->bDescriptorType == USB_DT_ENDPOINT) ||
			    (header->bDescriptorType == USB_DT_INTERFACE) ||
			    (header->bDescriptorType == USB_DT_CONFIG) ||
			    (header->bDescriptorType == USB_DT_DEVICE))
				break;

			dbg("skipping descriptor 0x%X", header->bDescriptorType);
			numskipped++;

			buffer += header->bLength;
			size -= header->bLength;
		}
		if (numskipped)
			dbg("skipped %d class/vendor specific endpoint descriptors", numskipped);

		/* Copy any unknown descriptors into a storage area for */
		/*  drivers to later parse */
		len = (int)(buffer - begin);
		if (len) {
			if (config->extralen) {
				warn("extra config descriptor");
			} else {
				config->extra = kmalloc(len, GFP_KERNEL);
				if (!config->extra) {
					err("couldn't allocate memory for config extra descriptors");
					config->extralen = 0;
					return -1;
				}

				memcpy(config->extra, begin, len);
				config->extralen = len;
			}
		}

		retval = usb_parse_interface(config->interface + i, buffer, size);
		if (retval < 0)
			return retval;

		buffer += retval;
		size -= retval;
	}

	return size;
}

// hub-only!! ... and only exported for reset/reinit path.
// otherwise used internally on disconnect/destroy path
void usb_destroy_configuration(struct usb_device *dev)
{
	int c, i, j, k;
	
	if (!dev->config)
		return;

	if (dev->rawdescriptors) {
		for (i = 0; i < dev->descriptor.bNumConfigurations; i++)
			kfree(dev->rawdescriptors[i]);

		kfree(dev->rawdescriptors);
	}

	for (c = 0; c < dev->descriptor.bNumConfigurations; c++) {
		struct usb_config_descriptor *cf = &dev->config[c];

		if (!cf->interface)
			break;

		for (i = 0; i < cf->bNumInterfaces; i++) {
			struct usb_interface *ifp =
				&cf->interface[i];
				
			if (!ifp->altsetting)
				break;

			for (j = 0; j < ifp->num_altsetting; j++) {
				struct usb_interface_descriptor *as =
					&ifp->altsetting[j];
					
				if(as->extra) {
					kfree(as->extra);
				}

				if (!as->endpoint)
					break;
					
				for(k = 0; k < as->bNumEndpoints; k++) {
					if(as->endpoint[k].extra) {
						kfree(as->endpoint[k].extra);
					}
				}	
				kfree(as->endpoint);
			}

			kfree(ifp->altsetting);
		}
		kfree(cf->interface);
	}
	kfree(dev->config);
}

/* for returning string descriptors in UTF-16LE */
static int ascii2utf (char *ascii, __u8 *utf, int utfmax)
{
	int retval;

	for (retval = 0; *ascii && utfmax > 1; utfmax -= 2, retval += 2) {
		*utf++ = *ascii++ & 0x7f;
		*utf++ = 0;
	}
	return retval;
}

/*
 * root_hub_string is used by each host controller's root hub code,
 * so that they're identified consistently throughout the system.
 */
int usb_root_hub_string (int id, int serial, char *type, __u8 *data, int len)
{
	char buf [30];

	// assert (len > (2 * (sizeof (buf) + 1)));
	// assert (strlen (type) <= 8);

	// language ids
	if (id == 0) {
		*data++ = 4; *data++ = 3;	/* 4 bytes data */
		*data++ = 0; *data++ = 0;	/* some language id */
		return 4;

	// serial number
	} else if (id == 1) {
		sprintf (buf, "%x", serial);

	// product description
	} else if (id == 2) {
		sprintf (buf, "USB %s Root Hub", type);

	// id 3 == vendor description

	// unsupported IDs --> "stall"
	} else
	    return 0;

	data [0] = 2 + ascii2utf (buf, data + 2, len - 2);
	data [1] = 3;
	return data [0];
}

/*
 * __usb_get_extra_descriptor() finds a descriptor of specific type in the
 * extra field of the interface and endpoint descriptor structs.
 */

int __usb_get_extra_descriptor(char *buffer, unsigned size, unsigned char type, void **ptr)
{
	struct usb_descriptor_header *header;

	while (size >= sizeof(struct usb_descriptor_header)) {
		header = (struct usb_descriptor_header *)buffer;

		if (header->bLength < 2) {
			err("invalid descriptor length of %d", header->bLength);
			return -1;
		}

		if (header->bDescriptorType == type) {
			*ptr = header;
			return 0;
		}

		buffer += header->bLength;
		size -= header->bLength;
	}
	return -1;
}

/**
 * usb_disconnect - disconnect a device (usbcore-internal)
 * @pdev: pointer to device being disconnected
 * Context: !in_interrupt ()
 *
 * Something got disconnected. Get rid of it, and all of its children.
 *
 * Only hub drivers (including virtual root hub drivers for host
 * controllers) should ever call this.
 *
 * This call is synchronous, and may not be used in an interrupt context.
 */
void usb_disconnect(struct usb_device **pdev)
{
	struct usb_device * dev = *pdev;
	int i;

	if (!dev)
		return;

	*pdev = NULL;

	info("USB disconnect on device %d", dev->devnum);

	if (dev->actconfig) {
		for (i = 0; i < dev->actconfig->bNumInterfaces; i++) {
			struct usb_interface *interface = &dev->actconfig->interface[i];
			struct usb_driver *driver = interface->driver;
			if (driver) {
				if (driver->owner)
					__MOD_INC_USE_COUNT(driver->owner);
				down(&driver->serialize);
				driver->disconnect(dev, interface->private_data);
				up(&driver->serialize);
				if (driver->owner)
					__MOD_DEC_USE_COUNT(driver->owner);
				/* if driver->disconnect didn't release the interface */
				if (interface->driver)
					usb_driver_release_interface(driver, interface);
			}
			/* remove our device node for this interface */
			put_device(&interface->dev);
		}
	}

	/* Free up all the children.. */
	for (i = 0; i < USB_MAXCHILDREN; i++) {
		struct usb_device **child = dev->children + i;
		if (*child)
			usb_disconnect(child);
	}

	/* Let policy agent unload modules etc */
	call_policy ("remove", dev);

	/* Free the device number and remove the /proc/bus/usb entry */
	if (dev->devnum > 0) {
		clear_bit(dev->devnum, &dev->bus->devmap.devicemap);
		usbfs_remove_device(dev);
		put_device(&dev->dev);
	}

	/* Free up the device itself */
	usb_free_dev(dev);
}

/**
 * usb_connect - connects a new device during enumeration (usbcore-internal)
 * @dev: partially enumerated device
 *
 * Connect a new USB device. This basically just initializes
 * the USB device information and sets up the topology - it's
 * up to the low-level driver to reset the port and actually
 * do the setup (the upper levels don't know how to do that).
 *
 * Only hub drivers (including virtual root hub drivers for host
 * controllers) should ever call this.
 */
void usb_connect(struct usb_device *dev)
{
	int devnum;
	// FIXME needs locking for SMP!!
	/* why? this is called only from the hub thread, 
	 * which hopefully doesn't run on multiple CPU's simultaneously 8-)
	 * ... it's also called from modprobe/rmmod/apmd threads as part
	 * of virtual root hub init/reinit.  In the init case, the hub code 
	 * won't have seen this, but not so for reinit ... 
	 */
	dev->descriptor.bMaxPacketSize0 = 8;  /* Start off at 8 bytes  */
#ifndef DEVNUM_ROUND_ROBIN
	devnum = find_next_zero_bit(dev->bus->devmap.devicemap, 128, 1);
#else	/* round_robin alloc of devnums */
	/* Try to allocate the next devnum beginning at bus->devnum_next. */
	devnum = find_next_zero_bit(dev->bus->devmap.devicemap, 128, dev->bus->devnum_next);
	if (devnum >= 128)
		devnum = find_next_zero_bit(dev->bus->devmap.devicemap, 128, 1);

	dev->bus->devnum_next = ( devnum >= 127 ? 1 : devnum + 1);
#endif	/* round_robin alloc of devnums */

	if (devnum < 128) {
		set_bit(devnum, dev->bus->devmap.devicemap);
		dev->devnum = devnum;
	}
}

/*
 * These are the actual routines to send
 * and receive control messages.
 */

// hub-only!! ... and only exported for reset/reinit path.
// otherwise used internally, for usb_new_device()
int usb_set_address(struct usb_device *dev)
{
	return usb_control_msg(dev, usb_snddefctrl(dev), USB_REQ_SET_ADDRESS,
		// FIXME USB_CTRL_SET_TIMEOUT
		0, dev->devnum, 0, NULL, 0, HZ * USB_CTRL_GET_TIMEOUT);
}

/**
 * usb_get_descriptor - issues a generic GET_DESCRIPTOR request
 * @dev: the device whose descriptor is being retrieved
 * @type: the descriptor type (USB_DT_*)
 * @index: the number of the descriptor
 * @buf: where to put the descriptor
 * @size: how big is "buf"?
 * Context: !in_interrupt ()
 *
 * Gets a USB descriptor.  Convenience functions exist to simplify
 * getting some types of descriptors.  Use
 * usb_get_device_descriptor() for USB_DT_DEVICE,
 * and usb_get_string() or usb_string() for USB_DT_STRING.
 * Configuration descriptors (USB_DT_CONFIG) are part of the device
 * structure, at least for the current configuration.
 * In addition to a number of USB-standard descriptors, some
 * devices also use class-specific or vendor-specific descriptors.
 *
 * This call is synchronous, and may not be used in an interrupt context.
 *
 * Returns zero on success, or else the status code returned by the
 * underlying usb_control_msg() call.
 */
int usb_get_descriptor(struct usb_device *dev, unsigned char type, unsigned char index, void *buf, int size)
{
	int i = 5;
	int result;
	
	memset(buf,0,size);	// Make sure we parse really received data

	while (i--) {
		/* retries if the returned length was 0; flakey device */
		if ((result = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
				    USB_REQ_GET_DESCRIPTOR, USB_DIR_IN,
				    (type << 8) + index, 0, buf, size,
				    HZ * USB_CTRL_GET_TIMEOUT)) > 0
				|| result == -EPIPE)
			break;
	}
	return result;
}

/**
 * usb_get_string - gets a string descriptor
 * @dev: the device whose string descriptor is being retrieved
 * @langid: code for language chosen (from string descriptor zero)
 * @index: the number of the descriptor
 * @buf: where to put the string
 * @size: how big is "buf"?
 * Context: !in_interrupt ()
 *
 * Retrieves a string, encoded using UTF-16LE (Unicode, 16 bits per character,
 * in little-endian byte order).
 * The usb_string() function will often be a convenient way to turn
 * these strings into kernel-printable form.
 *
 * Strings may be referenced in device, configuration, interface, or other
 * descriptors, and could also be used in vendor-specific ways.
 *
 * This call is synchronous, and may not be used in an interrupt context.
 *
 * Returns zero on success, or else the status code returned by the
 * underlying usb_control_msg() call.
 */
int usb_get_string(struct usb_device *dev, unsigned short langid, unsigned char index, void *buf, int size)
{
	return usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
		USB_REQ_GET_DESCRIPTOR, USB_DIR_IN,
		(USB_DT_STRING << 8) + index, langid, buf, size,
		HZ * USB_CTRL_GET_TIMEOUT);
}

/**
 * usb_get_device_descriptor - (re)reads the device descriptor
 * @dev: the device whose device descriptor is being updated
 * Context: !in_interrupt ()
 *
 * Updates the copy of the device descriptor stored in the device structure,
 * which dedicates space for this purpose.  Note that several fields are
 * converted to the host CPU's byte order:  the USB version (bcdUSB), and
 * vendors product and version fields (idVendor, idProduct, and bcdDevice).
 * That lets device drivers compare against non-byteswapped constants.
 *
 * There's normally no need to use this call, although some devices
 * will change their descriptors after events like updating firmware.
 *
 * This call is synchronous, and may not be used in an interrupt context.
 *
 * Returns zero on success, or else the status code returned by the
 * underlying usb_control_msg() call.
 */
int usb_get_device_descriptor(struct usb_device *dev)
{
	int ret = usb_get_descriptor(dev, USB_DT_DEVICE, 0, &dev->descriptor,
				     sizeof(dev->descriptor));
	if (ret >= 0) {
		le16_to_cpus(&dev->descriptor.bcdUSB);
		le16_to_cpus(&dev->descriptor.idVendor);
		le16_to_cpus(&dev->descriptor.idProduct);
		le16_to_cpus(&dev->descriptor.bcdDevice);
	}
	return ret;
}

/**
 * usb_get_status - issues a GET_STATUS call
 * @dev: the device whose status is being checked
 * @type: USB_RECIP_*; for device, interface, or endpoint
 * @target: zero (for device), else interface or endpoint number
 * @data: pointer to two bytes of bitmap data
 * Context: !in_interrupt ()
 *
 * Returns device, interface, or endpoint status.  Normally only of
 * interest to see if the device is self powered, or has enabled the
 * remote wakeup facility; or whether a bulk or interrupt endpoint
 * is halted ("stalled").
 *
 * Bits in these status bitmaps are set using the SET_FEATURE request,
 * and cleared using the CLEAR_FEATURE request.  The usb_clear_halt()
 * function should be used to clear halt ("stall") status.
 *
 * This call is synchronous, and may not be used in an interrupt context.
 *
 * Returns zero on success, or else the status code returned by the
 * underlying usb_control_msg() call.
 */
int usb_get_status(struct usb_device *dev, int type, int target, void *data)
{
	return usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
		USB_REQ_GET_STATUS, USB_DIR_IN | type, 0, target, data, 2,
		HZ * USB_CTRL_GET_TIMEOUT);
}


// hub-only!! ... and only exported for reset/reinit path.
// otherwise used internally, for config/altsetting reconfig.
void usb_set_maxpacket(struct usb_device *dev)
{
	int i, b;

	for (i=0; i<dev->actconfig->bNumInterfaces; i++) {
		struct usb_interface *ifp = dev->actconfig->interface + i;
		struct usb_interface_descriptor *as = ifp->altsetting + ifp->act_altsetting;
		struct usb_endpoint_descriptor *ep = as->endpoint;
		int e;

		for (e=0; e<as->bNumEndpoints; e++) {
			b = ep[e].bEndpointAddress & USB_ENDPOINT_NUMBER_MASK;
			if ((ep[e].bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) ==
				USB_ENDPOINT_XFER_CONTROL) {	/* Control => bidirectional */
				dev->epmaxpacketout[b] = ep[e].wMaxPacketSize;
				dev->epmaxpacketin [b] = ep[e].wMaxPacketSize;
				}
			else if (usb_endpoint_out(ep[e].bEndpointAddress)) {
				if (ep[e].wMaxPacketSize > dev->epmaxpacketout[b])
					dev->epmaxpacketout[b] = ep[e].wMaxPacketSize;
			}
			else {
				if (ep[e].wMaxPacketSize > dev->epmaxpacketin [b])
					dev->epmaxpacketin [b] = ep[e].wMaxPacketSize;
			}
		}
	}
}

/**
 * usb_clear_halt - tells device to clear endpoint halt/stall condition
 * @dev: device whose endpoint is halted
 * @pipe: endpoint "pipe" being cleared
 * Context: !in_interrupt ()
 *
 * This is used to clear halt conditions for bulk and interrupt endpoints,
 * as reported by URB completion status.  Endpoints that are halted are
 * sometimes referred to as being "stalled".  Such endpoints are unable
 * to transmit or receive data until the halt status is cleared.  Any URBs
 * queued queued for such an endpoint should normally be unlinked before
 * clearing the halt condition.
 *
 * Note that control and isochronous endpoints don't halt, although control
 * endpoints report "protocol stall" (for unsupported requests) using the
 * same status code used to report a true stall.
 *
 * This call is synchronous, and may not be used in an interrupt context.
 *
 * Returns zero on success, or else the status code returned by the
 * underlying usb_control_msg() call.
 */
int usb_clear_halt(struct usb_device *dev, int pipe)
{
	int result;
	__u16 status;
	unsigned char *buffer;
	int endp=usb_pipeendpoint(pipe)|(usb_pipein(pipe)<<7);

/*
	if (!usb_endpoint_halted(dev, endp & 0x0f, usb_endpoint_out(endp)))
		return 0;
*/

	result = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
		USB_REQ_CLEAR_FEATURE, USB_RECIP_ENDPOINT, 0, endp, NULL, 0,
		HZ * USB_CTRL_SET_TIMEOUT);

	/* don't clear if failed */
	if (result < 0)
		return result;

	buffer = kmalloc(sizeof(status), GFP_KERNEL);
	if (!buffer) {
		err("unable to allocate memory for configuration descriptors");
		return -ENOMEM;
	}

	result = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
		USB_REQ_GET_STATUS, USB_DIR_IN | USB_RECIP_ENDPOINT, 0, endp,
		// FIXME USB_CTRL_GET_TIMEOUT, yes?  why not usb_get_status() ?
		buffer, sizeof(status), HZ * USB_CTRL_SET_TIMEOUT);

	memcpy(&status, buffer, sizeof(status));
	kfree(buffer);

	if (result < 0)
		return result;

	if (le16_to_cpu(status) & 1)
		return -EPIPE;		/* still halted */

	usb_endpoint_running(dev, usb_pipeendpoint(pipe), usb_pipeout(pipe));

	/* toggle is reset on clear */

	usb_settoggle(dev, usb_pipeendpoint(pipe), usb_pipeout(pipe), 0);

	return 0;
}

/**
 * usb_set_interface - Makes a particular alternate setting be current
 * @dev: the device whose interface is being updated
 * @interface: the interface being updated
 * @alternate: the setting being chosen.
 * Context: !in_interrupt ()
 *
 * This is used to enable data transfers on interfaces that may not
 * be enabled by default.  Not all devices support such configurability.
 *
 * Within any given configuration, each interface may have several
 * alternative settings.  These are often used to control levels of
 * bandwidth consumption.  For example, the default setting for a high
 * speed interrupt endpoint may not send more than about 4KBytes per
 * microframe, and isochronous endpoints may never be part of a an
 * interface's default setting.  To access such bandwidth, alternate
 * interface setting must be made current.
 *
 * Note that in the Linux USB subsystem, bandwidth associated with
 * an endpoint in a given alternate setting is not reserved until an
 * is submitted that needs that bandwidth.  Some other operating systems
 * allocate bandwidth early, when a configuration is chosen.
 *
 * This call is synchronous, and may not be used in an interrupt context.
 *
 * Returns zero on success, or else the status code returned by the
 * underlying usb_control_msg() call.
 */
int usb_set_interface(struct usb_device *dev, int interface, int alternate)
{
	struct usb_interface *iface;
	struct usb_interface_descriptor *iface_as;
	int i, ret;

	iface = usb_ifnum_to_if(dev, interface);
	if (!iface) {
		warn("selecting invalid interface %d", interface);
		return -EINVAL;
	}

	/* 9.4.10 says devices don't need this, if the interface
	   only has one alternate setting */
	if (iface->num_altsetting == 1) {
		dbg("ignoring set_interface for dev %d, iface %d, alt %d",
			dev->devnum, interface, alternate);
		return 0;
	}

	if ((ret = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
	    USB_REQ_SET_INTERFACE, USB_RECIP_INTERFACE, alternate,
	    interface, NULL, 0, HZ * 5)) < 0)
		return ret;

	iface->act_altsetting = alternate;

	/* 9.1.1.5: reset toggles for all endpoints affected by this iface-as
	 *
	 * Note:
	 * Despite EP0 is always present in all interfaces/AS, the list of
	 * endpoints from the descriptor does not contain EP0. Due to its
	 * omnipresence one might expect EP0 being considered "affected" by
	 * any SetInterface request and hence assume toggles need to be reset.
	 * However, EP0 toggles are re-synced for every individual transfer
	 * during the SETUP stage - hence EP0 toggles are "don't care" here.
	 */

	iface_as = &iface->altsetting[alternate];
	for (i = 0; i < iface_as->bNumEndpoints; i++) {
		u8	ep = iface_as->endpoint[i].bEndpointAddress;

		usb_settoggle(dev, ep&USB_ENDPOINT_NUMBER_MASK, usb_endpoint_out(ep), 0);
	}

	/* usb_set_maxpacket() sets the maxpacket size for all EP in all
	 * interfaces but it shouldn't do any harm here: we have changed
	 * the AS for the requested interface only, hence for unaffected
	 * interfaces it's just re-application of still-valid values.
	 */
	usb_set_maxpacket(dev);
	return 0;
}

/**
 * usb_set_configuration - Makes a particular device setting be current
 * @dev: the device whose configuration is being updated
 * @configuration: the configuration being chosen.
 * Context: !in_interrupt ()
 *
 * This is used to enable non-default device modes.  Not all devices
 * support this kind of configurability.  By default, configuration
 * zero is selected after enumeration; many devices only have a single
 * configuration.
 *
 * USB devices may support one or more configurations, which affect
 * power consumption and the functionality available.  For example,
 * the default configuration is limited to using 100mA of bus power,
 * so that when certain device functionality requires more power,
 * and the device is bus powered, that functionality will be in some
 * non-default device configuration.  Other device modes may also be
 * reflected as configuration options, such as whether two ISDN
 * channels are presented as independent 64Kb/s interfaces or as one
 * bonded 128Kb/s interface.
 *
 * Note that USB has an additional level of device configurability,
 * associated with interfaces.  That configurability is accessed using
 * usb_set_interface().
 *
 * This call is synchronous, and may not be used in an interrupt context.
 *
 * Returns zero on success, or else the status code returned by the
 * underlying usb_control_msg() call.
 */
int usb_set_configuration(struct usb_device *dev, int configuration)
{
	int i, ret;
	struct usb_config_descriptor *cp = NULL;
	
	for (i=0; i<dev->descriptor.bNumConfigurations; i++) {
		if (dev->config[i].bConfigurationValue == configuration) {
			cp = &dev->config[i];
			break;
		}
	}
	if (!cp) {
		warn("selecting invalid configuration %d", configuration);
		return -EINVAL;
	}

	if ((ret = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
			USB_REQ_SET_CONFIGURATION, 0, configuration, 0,
			NULL, 0, HZ * USB_CTRL_SET_TIMEOUT)) < 0)
		return ret;

	dev->actconfig = cp;
	dev->toggle[0] = 0;
	dev->toggle[1] = 0;
	usb_set_maxpacket(dev);

	return 0;
}

// hub-only!! ... and only in reset path, or usb_new_device()
// (used by real hubs and virtual root hubs)
int usb_get_configuration(struct usb_device *dev)
{
	int result;
	unsigned int cfgno, length;
	unsigned char *buffer;
	unsigned char *bigbuffer;
 	struct usb_config_descriptor *desc;

	if (dev->descriptor.bNumConfigurations > USB_MAXCONFIG) {
		warn("too many configurations");
		return -EINVAL;
	}

	if (dev->descriptor.bNumConfigurations < 1) {
		warn("not enough configurations");
		return -EINVAL;
	}

	dev->config = (struct usb_config_descriptor *)
		kmalloc(dev->descriptor.bNumConfigurations *
		sizeof(struct usb_config_descriptor), GFP_KERNEL);
	if (!dev->config) {
		err("out of memory");
		return -ENOMEM;	
	}
	memset(dev->config, 0, dev->descriptor.bNumConfigurations *
		sizeof(struct usb_config_descriptor));

	dev->rawdescriptors = (char **)kmalloc(sizeof(char *) *
		dev->descriptor.bNumConfigurations, GFP_KERNEL);
	if (!dev->rawdescriptors) {
		err("out of memory");
		return -ENOMEM;
	}

	buffer = kmalloc(8, GFP_KERNEL);
	if (!buffer) {
		err("unable to allocate memory for configuration descriptors");
		return -ENOMEM;
	}
	desc = (struct usb_config_descriptor *)buffer;

	for (cfgno = 0; cfgno < dev->descriptor.bNumConfigurations; cfgno++) {
		/* We grab the first 8 bytes so we know how long the whole */
		/*  configuration is */
		result = usb_get_descriptor(dev, USB_DT_CONFIG, cfgno, buffer, 8);
		if (result < 8) {
			if (result < 0)
				err("unable to get descriptor");
			else {
				err("config descriptor too short (expected %i, got %i)", 8, result);
				result = -EINVAL;
			}
			goto err;
		}

  	  	/* Get the full buffer */
		length = le16_to_cpu(desc->wTotalLength);

		bigbuffer = kmalloc(length, GFP_KERNEL);
		if (!bigbuffer) {
			err("unable to allocate memory for configuration descriptors");
			result = -ENOMEM;
			goto err;
		}

		/* Now that we know the length, get the whole thing */
		result = usb_get_descriptor(dev, USB_DT_CONFIG, cfgno, bigbuffer, length);
		if (result < 0) {
			err("couldn't get all of config descriptors");
			kfree(bigbuffer);
			goto err;
		}	
	
		if (result < length) {
			err("config descriptor too short (expected %i, got %i)", length, result);
			result = -EINVAL;
			kfree(bigbuffer);
			goto err;
		}

		dev->rawdescriptors[cfgno] = bigbuffer;

		result = usb_parse_configuration(&dev->config[cfgno], bigbuffer);
		if (result > 0)
			dbg("descriptor data left");
		else if (result < 0) {
			result = -EINVAL;
			goto err;
		}
	}

	kfree(buffer);
	return 0;
err:
	kfree(buffer);
	dev->descriptor.bNumConfigurations = cfgno;
	return result;
}

/**
 * usb_string - returns ISO 8859-1 version of a string descriptor
 * @dev: the device whose string descriptor is being retrieved
 * @index: the number of the descriptor
 * @buf: where to put the string
 * @size: how big is "buf"?
 * Context: !in_interrupt ()
 * 
 * This converts the UTF-16LE encoded strings returned by devices, from
 * usb_get_string_descriptor(), to null-terminated ISO-8859-1 encoded ones
 * that are more usable in most kernel contexts.  Note that all characters
 * in the chosen descriptor that can't be encoded using ISO-8859-1
 * are converted to the question mark ("?") character, and this function
 * chooses strings in the first language supported by the device.
 *
 * The ASCII (or, redundantly, "US-ASCII") character set is the seven-bit
 * subset of ISO 8859-1. ISO-8859-1 is the eight-bit subset of Unicode,
 * and is appropriate for use many uses of English and several other
 * Western European languages.  (But it doesn't include the "Euro" symbol.)
 *
 * This call is synchronous, and may not be used in an interrupt context.
 *
 * Returns length of the string (>= 0) or usb_control_msg status (< 0).
 */
int usb_string(struct usb_device *dev, int index, char *buf, size_t size)
{
	unsigned char *tbuf;
	int err;
	unsigned int u, idx;

	if (size <= 0 || !buf || !index)
		return -EINVAL;
	buf[0] = 0;
	tbuf = kmalloc(256, GFP_KERNEL);
	if (!tbuf)
		return -ENOMEM;

	/* get langid for strings if it's not yet known */
	if (!dev->have_langid) {
		err = usb_get_string(dev, 0, 0, tbuf, 4);
		if (err < 0) {
			err("error getting string descriptor 0 (error=%d)", err);
			goto errout;
		} else if (tbuf[0] < 4) {
			err("string descriptor 0 too short");
			err = -EINVAL;
			goto errout;
		} else {
			dev->have_langid = -1;
			dev->string_langid = tbuf[2] | (tbuf[3]<< 8);
				/* always use the first langid listed */
			dbg("USB device number %d default language ID 0x%x",
				dev->devnum, dev->string_langid);
		}
	}

	/*
	 * Just ask for a maximum length string and then take the length
	 * that was returned.
	 */
	err = usb_get_string(dev, dev->string_langid, index, tbuf, 255);
	if (err < 0)
		goto errout;

	size--;		/* leave room for trailing NULL char in output buffer */
	for (idx = 0, u = 2; u < err; u += 2) {
		if (idx >= size)
			break;
		if (tbuf[u+1])			/* high byte */
			buf[idx++] = '?';  /* non ISO-8859-1 character */
		else
			buf[idx++] = tbuf[u];
	}
	buf[idx] = 0;
	err = idx;

 errout:
	kfree(tbuf);
	return err;
}

/**
 * usb_make_path - returns device path in the hub tree
 * @dev: the device whose path is being constructed
 * @buf: where to put the string
 * @size: how big is "buf"?
 * Context: !in_interrupt ()
 *
 * Returns length of the string (>= 0) or out of memory status (< 0).
 *
 * NOTE:  prefer to use use dev->devpath directly.
 */
int usb_make_path(struct usb_device *dev, char *buf, size_t size)
{
	struct usb_device *pdev = dev->parent;
	char *tmp;
	char *port;
	int i;

	if (!(port = kmalloc(size, GFP_KERNEL)))
		return -ENOMEM;
	if (!(tmp = kmalloc(size, GFP_KERNEL))) {
		kfree(port);
		return -ENOMEM;
	}

	*port = 0;
	while (pdev) {
		for (i = 0; i < pdev->maxchild; i++)
			if (pdev->children[i] == dev)
				break;

		if (pdev->children[i] != dev) {
			kfree(port);
			kfree(tmp);
			return -ENODEV;
		}

		strcpy(tmp, port);
		snprintf(port, size, strlen(port) ? "%d.%s" : "%d", i + 1, tmp);

		dev = pdev;
		pdev = dev->parent;
	}

	snprintf(buf, size, "usb%d:%s", dev->bus->busnum, port);
	kfree(port);
	kfree(tmp);
	return strlen(buf);
}

/*
 * By the time we get here, the device has gotten a new device ID
 * and is in the default state. We need to identify the thing and
 * get the ball rolling..
 *
 * Returns 0 for success, != 0 for error.
 *
 * This call is synchronous, and may not be used in an interrupt context.
 *
 * Only hub drivers (including virtual root hub drivers for host
 * controllers) should ever call this.
 */
int usb_new_device(struct usb_device *dev)
{
	int err;

	/* USB v1.1 5.5.3 */
	/* We read the first 8 bytes from the device descriptor to get to */
	/*  the bMaxPacketSize0 field. Then we set the maximum packet size */
	/*  for the control pipe, and retrieve the rest */
	dev->epmaxpacketin [0] = 8;
	dev->epmaxpacketout[0] = 8;

	err = usb_set_address(dev);
	if (err < 0) {
		err("USB device not accepting new address=%d (error=%d)",
			dev->devnum, err);
		clear_bit(dev->devnum, &dev->bus->devmap.devicemap);
		dev->devnum = -1;
		return 1;
	}

	wait_ms(10);	/* Let the SET_ADDRESS settle */

	err = usb_get_descriptor(dev, USB_DT_DEVICE, 0, &dev->descriptor, 8);
	if (err < 8) {
		if (err < 0)
			err("USB device not responding, giving up (error=%d)", err);
		else
			err("USB device descriptor short read (expected %i, got %i)", 8, err);
		clear_bit(dev->devnum, &dev->bus->devmap.devicemap);
		dev->devnum = -1;
		return 1;
	}
	dev->epmaxpacketin [0] = dev->descriptor.bMaxPacketSize0;
	dev->epmaxpacketout[0] = dev->descriptor.bMaxPacketSize0;

	err = usb_get_device_descriptor(dev);
	if (err < (signed)sizeof(dev->descriptor)) {
		if (err < 0)
			err("unable to get device descriptor (error=%d)", err);
		else
			err("USB device descriptor short read (expected %Zi, got %i)",
				sizeof(dev->descriptor), err);
	
		clear_bit(dev->devnum, &dev->bus->devmap.devicemap);
		dev->devnum = -1;
		return 1;
	}

	err = usb_get_configuration(dev);
	if (err < 0) {
		err("unable to get device %d configuration (error=%d)",
			dev->devnum, err);
		clear_bit(dev->devnum, &dev->bus->devmap.devicemap);
		dev->devnum = -1;
		return 1;
	}

	/* we set the default configuration here */
	err = usb_set_configuration(dev, dev->config[0].bConfigurationValue);
	if (err) {
		err("failed to set device %d default configuration (error=%d)",
			dev->devnum, err);
		clear_bit(dev->devnum, &dev->bus->devmap.devicemap);
		dev->devnum = -1;
		return 1;
	}

	dbg("new device strings: Mfr=%d, Product=%d, SerialNumber=%d",
		dev->descriptor.iManufacturer, dev->descriptor.iProduct, dev->descriptor.iSerialNumber);
#ifdef DEBUG
	if (dev->descriptor.iManufacturer)
		usb_show_string(dev, "Manufacturer", dev->descriptor.iManufacturer);
	if (dev->descriptor.iProduct)
		usb_show_string(dev, "Product", dev->descriptor.iProduct);
	if (dev->descriptor.iSerialNumber)
		usb_show_string(dev, "SerialNumber", dev->descriptor.iSerialNumber);
#endif

	/* register this device in the driverfs tree */
	err = device_register (&dev->dev);
	if (err)
		return err;

	/* now that the basic setup is over, add a /proc/bus/usb entry */
	usbfs_add_device(dev);

	/* find drivers willing to handle this device */
	usb_find_drivers(dev);

	/* userspace may load modules and/or configure further */
	call_policy ("add", dev);

	return 0;
}

static int usb_open(struct inode * inode, struct file * file)
{
	int minor = minor(inode->i_rdev);
	struct usb_driver *c = usb_minors[minor/16];
	int err = -ENODEV;
	struct file_operations *old_fops, *new_fops = NULL;

	/*
	 * No load-on-demand? Randy, could you ACK that it's really not
	 * supposed to be done?					-- AV
	 */
	if (!c || !(new_fops = fops_get(c->fops)))
		return err;
	old_fops = file->f_op;
	file->f_op = new_fops;
	/* Curiouser and curiouser... NULL ->open() as "no device" ? */
	if (file->f_op->open)
		err = file->f_op->open(inode,file);
	if (err) {
		fops_put(file->f_op);
		file->f_op = fops_get(old_fops);
	}
	fops_put(old_fops);
	return err;
}

static struct file_operations usb_fops = {
	owner:		THIS_MODULE,
	open:		usb_open,
};

int usb_major_init(void)
{
	if (devfs_register_chrdev(USB_MAJOR, "usb", &usb_fops)) {
		err("unable to get major %d for usb devices", USB_MAJOR);
		return -EBUSY;
	}

	usb_devfs_handle = devfs_mk_dir(NULL, "usb", NULL);

	return 0;
}

void usb_major_cleanup(void)
{
	devfs_unregister(usb_devfs_handle);
	devfs_unregister_chrdev(USB_MAJOR, "usb");
}


#ifdef CONFIG_PROC_FS
struct list_head *usb_driver_get_list(void)
{
	return &usb_driver_list;
}

struct list_head *usb_bus_get_list(void)
{
	return &usb_bus_list;
}
#endif


/*
 * Init
 */
static int __init usb_init(void)
{
	usb_major_init();
	usbfs_init();
	usb_hub_init();

	return 0;
}

/*
 * Cleanup
 */
static void __exit usb_exit(void)
{
	usb_major_cleanup();
	usbfs_cleanup();
	usb_hub_cleanup();
}

subsys_initcall(usb_init);
module_exit(usb_exit);

/*
 * USB may be built into the kernel or be built as modules.
 * If the USB core [and maybe a host controller driver] is built
 * into the kernel, and other device drivers are built as modules,
 * then these symbols need to be exported for the modules to use.
 */
EXPORT_SYMBOL(usb_ifnum_to_if);
EXPORT_SYMBOL(usb_epnum_to_ep_desc);

EXPORT_SYMBOL(usb_register);
EXPORT_SYMBOL(usb_deregister);
EXPORT_SYMBOL(usb_scan_devices);

EXPORT_SYMBOL(usb_alloc_dev);
EXPORT_SYMBOL(usb_free_dev);
EXPORT_SYMBOL(usb_inc_dev_use);

EXPORT_SYMBOL(usb_driver_claim_interface);
EXPORT_SYMBOL(usb_interface_claimed);
EXPORT_SYMBOL(usb_driver_release_interface);
EXPORT_SYMBOL(usb_match_id);

EXPORT_SYMBOL(usb_root_hub_string);
EXPORT_SYMBOL(usb_new_device);
EXPORT_SYMBOL(usb_reset_device);
EXPORT_SYMBOL(usb_connect);
EXPORT_SYMBOL(usb_disconnect);

EXPORT_SYMBOL(__usb_get_extra_descriptor);

EXPORT_SYMBOL(usb_get_current_frame_number);

// asynchronous request completion model
EXPORT_SYMBOL(usb_alloc_urb);
EXPORT_SYMBOL(usb_free_urb);
EXPORT_SYMBOL(usb_get_urb);
EXPORT_SYMBOL(usb_submit_urb);
EXPORT_SYMBOL(usb_unlink_urb);

// synchronous request completion model
EXPORT_SYMBOL(usb_control_msg);
EXPORT_SYMBOL(usb_bulk_msg);
// synchronous control message convenience routines
EXPORT_SYMBOL(usb_get_descriptor);
EXPORT_SYMBOL(usb_get_device_descriptor);
EXPORT_SYMBOL(usb_get_status);
EXPORT_SYMBOL(usb_get_string);
EXPORT_SYMBOL(usb_string);
EXPORT_SYMBOL(usb_clear_halt);
EXPORT_SYMBOL(usb_set_configuration);
EXPORT_SYMBOL(usb_set_interface);

EXPORT_SYMBOL(usb_make_path);
EXPORT_SYMBOL(usb_devfs_handle);
MODULE_LICENSE("GPL");
