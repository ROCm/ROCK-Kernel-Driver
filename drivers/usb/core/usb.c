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
 * (C) Copyright Greg Kroah-Hartman 2002
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
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/smp_lock.h>

#ifdef CONFIG_USB_DEBUG
	#define DEBUG
#else
	#undef DEBUG
#endif
#include <linux/usb.h>

#include "hcd.h"

extern int  usb_hub_init(void);
extern void usb_hub_cleanup(void);
extern int usb_major_init(void);
extern void usb_major_cleanup(void);

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


/**
 * usb_register - register a USB driver
 * @new_driver: USB operations for the driver
 *
 * Registers a USB driver with the USB core.  The list of unattached
 * interfaces will be rescanned whenever a new driver is added, allowing
 * the new driver to attach to any recognized devices.
 * Returns a negative error code on failure and 0 on success.
 * 
 * NOTE: if you want your driver to use the USB major number, you must call
 * usb_register_dev() to enable that functionality.  This function no longer
 * takes care of that.
 */
int usb_register(struct usb_driver *new_driver)
{
	int retval = 0;

	info("registered new driver %s", new_driver->name);

	init_MUTEX(&new_driver->serialize);

	/* Add it to the list of known drivers */
	list_add_tail(&new_driver->driver_list, &usb_driver_list);

	usb_scan_devices();

	usbfs_update_special();

	return retval;
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

/**
 *	usb_unbind_driver - disconnects a driver from a device
 *	@device: usb device to be disconnected
 *	@intf: interface of the device to be disconnected
 *	Context: BKL held
 *
 *	Handles module usage count correctly
 */

void usb_unbind_driver(struct usb_device *device, struct usb_interface *intf)
{
	struct usb_driver *driver;
	void *priv;
	int m;
	

	driver = intf->driver;
	priv = intf->private_data;
	
	if (!driver)
		return;

	/* as soon as we increase the module use count we drop the BKL
	   before that we must not sleep */
	if (driver->owner) {
		m = try_inc_mod_count(driver->owner);
		if (m == 0) {
			err("Dieing driver still bound to device.\n");
			return;
		}
		unlock_kernel();
	}
	down(&driver->serialize); 	/* if we sleep here on an umanaged driver
					   the holder of the lock guards against
					   module unload */

	driver->disconnect(device, priv);

	up(&driver->serialize);
	if (driver->owner) {
		lock_kernel();
		__MOD_DEC_USE_COUNT(driver->owner);
	}
}

/**
 *	usb_bind_driver - connect a driver to a device's interface
 *	@driver: device driver to be bound to a devices interface
 *	@dev: device to be bound
 *	@ifnum: index number of the interface to be used
 *
 *	Does a save binding of a driver to a device's interface
 *	Returns a pointer to the drivers private description of the binding
 */

void *usb_bind_driver(struct usb_driver *driver, struct usb_device *dev, unsigned int ifnum)
{
	int i,m;
	void *private = NULL;
	const struct usb_device_id *id;
	struct usb_interface *interface;

	if (driver->owner) {
		m = try_inc_mod_count(driver->owner);
		if (m == 0)
			return NULL; /* this horse is dead - don't ride*/
		unlock_kernel();
	}

	interface = &dev->actconfig->interface[ifnum];

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
	if (driver->owner) {
		lock_kernel();
		__MOD_DEC_USE_COUNT(driver->owner);
	}

	return private;
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
			usb_unbind_driver(dev, interface);
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
 * usb_deregister - unregister a USB driver
 * @driver: USB operations of the driver to unregister
 * Context: !in_interrupt (), must be called with BKL held
 *
 * Unlinks the specified driver from the internal USB driver list.
 * 
 * NOTE: If you called usb_register_dev(), you still need to call
 * usb_deregister_dev() to clean up your driver's allocated minor numbers,
 * this * call will no longer do it for you.
 */
void usb_deregister(struct usb_driver *driver)
{
	struct list_head *tmp;

	info("deregistering driver %s", driver->name);

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
 * usb_ifnum_to_ifpos - convert the interface number to the interface position
 * @dev: the device to use
 * @ifnum: the interface number (bInterfaceNumber); not interface position
 *
 * This is used to convert the interface _number_ (as in
 * interface.bInterfaceNumber) to the interface _position_ (as in
 * dev->actconfig->interface + position).  Note that the number is the same as
 * the position for all interfaces _except_ devices with interfaces not
 * sequentially numbered (e.g., 0, 2, 3, etc).
 */
int usb_ifnum_to_ifpos(struct usb_device *dev, unsigned ifnum)
{
	int i;

	for (i = 0; i < dev->actconfig->bNumInterfaces; i++)
		if (dev->actconfig->interface[i].altsetting[0].bInterfaceNumber == ifnum)
			return i;

	return -EINVAL;
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
	struct usb_driver *driver;
	
	if ((!dev) || (ifnum >= dev->actconfig->bNumInterfaces)) {
		err("bad find_interface_driver params");
		return -1;
	}

	down(&dev->serialize);

	interface = dev->actconfig->interface + ifnum;

	if (usb_interface_claimed(interface))
		goto out_err;

	private = NULL;
	lock_kernel();
	for (tmp = usb_driver_list.next; tmp != &usb_driver_list;) {
		driver = list_entry(tmp, struct usb_driver, driver_list);
		tmp = tmp->next;

		private = usb_bind_driver(driver, dev, ifnum);

		/* probe() may have changed the config on us */
		interface = dev->actconfig->interface + ifnum;

		if (private) {
			usb_driver_claim_interface(driver, interface, private);
			up(&dev->serialize);
			unlock_kernel();
			return 0;
		}
	}
	unlock_kernel();

out_err:
	up(&dev->serialize);
	return -1;
}

/**
 * usb_find_interface_driver_for_ifnum - finds a usb interface driver for the specified ifnum
 * @dev: the device to use
 * @ifnum: the interface number (bInterfaceNumber); not interface position!
 *
 * This converts a ifnum to ifpos via a call to usb_ifnum_to_ifpos and then
 * calls usb_find_interface_driver() with the found ifpos.  Note
 * usb_find_interface_driver's ifnum parameter is actually interface position.
 */
int usb_find_interface_driver_for_ifnum(struct usb_device *dev, unsigned ifnum)
{
	int ifpos = usb_ifnum_to_ifpos(dev, ifnum);

	if (0 > ifpos)
		return -EINVAL;

	return usb_find_interface_driver(dev, ifpos);
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

/* driverfs files */

/* devices have one current configuration, with one
 * or more interfaces that are used concurrently 
 */
static ssize_t
show_config (struct device *dev, char *buf, size_t count, loff_t off)
{
	struct usb_device	*udev;

	if (off)
		return 0;
	udev = list_entry (dev, struct usb_device, dev);
	return sprintf (buf, "%u\n", udev->actconfig->bConfigurationValue);
}
static struct driver_file_entry usb_config_entry = {
	name:	"configuration",
	mode:	S_IRUGO,
	show:	show_config,
};

/* interfaces have one current setting; alternates
 * can have different endpoints and class info.
 */
static ssize_t
show_altsetting (struct device *dev, char *buf, size_t count, loff_t off)
{
	struct usb_interface	*interface;

	if (off)
		return 0;
	interface = list_entry (dev, struct usb_interface, dev);
	return sprintf (buf, "%u\n", interface->altsetting->bAlternateSetting);
}
static struct driver_file_entry usb_altsetting_entry = {
	name:	"altsetting",
	mode:	S_IRUGO,
	show:	show_altsetting,
};

/* product driverfs file */
static ssize_t show_product (struct device *dev, char *buf, size_t count, loff_t off)
{
	struct usb_device *udev;
	int len;

	if (off)
		return 0;
	udev = list_entry (dev, struct usb_device, dev);

	len = usb_string(udev, udev->descriptor.iProduct, buf, PAGE_SIZE); 
	buf[len] = '\n';
	buf[len+1] = 0x00;
	return len+1;
}
static struct driver_file_entry usb_product_entry = {
	name:	"product",
	mode:	S_IRUGO,
	show:	show_product,
};

/* manufacturer driverfs file */
static ssize_t
show_manufacturer (struct device *dev, char *buf, size_t count, loff_t off)
{
	struct usb_device *udev;
	int len;

	if (off)
		return 0;
	udev = list_entry (dev, struct usb_device, dev);

	len = usb_string(udev, udev->descriptor.iManufacturer, buf, PAGE_SIZE); 
	buf[len] = '\n';
	buf[len+1] = 0x00;
	return len+1;
}
static struct driver_file_entry usb_manufacturer_entry = {
	name:	"manufacturer",
	mode:	S_IRUGO,
	show:	show_manufacturer,
};

/* serial number driverfs file */
static ssize_t
show_serial (struct device *dev, char *buf, size_t count, loff_t off)
{
	struct usb_device *udev;
	int len;

	if (off)
		return 0;
	udev = list_entry (dev, struct usb_device, dev);

	len = usb_string(udev, udev->descriptor.iSerialNumber, buf, PAGE_SIZE); 
	buf[len] = '\n';
	buf[len+1] = 0x00;
	return len+1;
}
static struct driver_file_entry usb_serial_entry = {
	name:	"serial",
	mode:	S_IRUGO,
	show:	show_serial,
};

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

	/* FIXME should get called for each new configuration not just the
	 * first one for a device. switching configs (or altesettings) should
	 * undo driverfs and HCD state for the previous interfaces.
	 */
	for (ifnum = 0; ifnum < dev->actconfig->bNumInterfaces; ifnum++) {
		struct usb_interface *interface = &dev->actconfig->interface[ifnum];
		struct usb_interface_descriptor *desc = interface->altsetting;
		
		/* register this interface with driverfs */
		interface->dev.parent = &dev->dev;
		interface->dev.bus = &usb_bus_type;
		sprintf (&interface->dev.bus_id[0], "if%d",
			interface->altsetting->bInterfaceNumber);
		if (!desc->iInterface
				|| usb_string (dev, desc->iInterface,
					interface->dev.name,
					sizeof interface->dev.name) <= 0) {
			/* typically devices won't bother with interface
			 * descriptions; this is the normal case.  an
			 * interface's driver might describe it better.
			 * (also: iInterface is per-altsetting ...)
			 */
			sprintf (&interface->dev.name[0],
				"usb-%s-%s interface %d",
				dev->bus->bus_name, dev->devpath,
				interface->altsetting->bInterfaceNumber);
		}
		device_register (&interface->dev);
		device_create_file (&interface->dev, &usb_altsetting_entry);

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

	if (dev->bus->op->allocate)
		dev->bus->op->allocate(dev);

	return dev;
}

/**
 * usb_get_dev - increments the reference count of the device
 * @dev: the device being referenced
 *
 * Each live reference to a device should be refcounted.
 *
 * Drivers for USB interfaces should normally record such references in
 * their probe() methods, when they bind to an interface, and release
 * them by calling usb_put_dev(), in their disconnect() methods.
 *
 * A pointer to the device with the incremented reference counter is returned.
 */
struct usb_device *usb_get_dev (struct usb_device *dev)
{
	if (dev) {
		atomic_inc (&dev->refcnt);
		return dev;
	}
	return NULL;
}

/**
 * usb_free_dev - free a usb device structure when all users of it are finished.
 * @dev: device that's been disconnected
 * Context: !in_interrupt ()
 *
 * Must be called when a user of a device is finished with it.  When the last
 * user of the device calls this function, the memory of the device is freed.
 *
 * Used by hub and virtual root hub drivers.  The device is completely
 * gone, everything is cleaned up, so it's time to get rid of these last
 * records of this device.
 */
void usb_free_dev(struct usb_device *dev)
{
	if (atomic_dec_and_test(&dev->refcnt)) {
		if (dev->bus->op->deallocate)
			dev->bus->op->deallocate(dev);
		usb_destroy_configuration (dev);
		usb_bus_put (dev->bus);
		kfree (dev);
	}
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

	lock_kernel();
	if (dev->actconfig) {
		for (i = 0; i < dev->actconfig->bNumInterfaces; i++) {
			struct usb_interface *interface = &dev->actconfig->interface[i];
			struct usb_driver *driver = interface->driver;
			if (driver) {
				usb_unbind_driver(dev, interface);
				/* if driver->disconnect didn't release the interface */
				if (interface->driver)
					usb_driver_release_interface(driver, interface);
			}
			/* remove our device node for this interface */
			put_device(&interface->dev);
		}
	}
	unlock_kernel();

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
		clear_bit(dev->devnum, dev->bus->devmap.devicemap);
		usbfs_remove_device(dev);
		put_device(&dev->dev);
	}

	/* Decrement the reference count, it'll auto free everything when */
	/* it hits 0 which could very well be now */
	usb_put_dev(dev);
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


// hub-only!! ... and only exported for reset/reinit path.
// otherwise used internally, for usb_new_device()
int usb_set_address(struct usb_device *dev)
{
	return usb_control_msg(dev, usb_snddefctrl(dev), USB_REQ_SET_ADDRESS,
		// FIXME USB_CTRL_SET_TIMEOUT
		0, dev->devnum, 0, NULL, 0, HZ * USB_CTRL_GET_TIMEOUT);
}


/* improve on the default device description, if we can ... and
 * while we're at it, maybe show the vendor and product strings.
 */
static void set_device_description (struct usb_device *dev)
{
	char	*buf, *here, *end;
	int	mfgr = dev->descriptor.iManufacturer;
	int	prod = dev->descriptor.iProduct;

	/* set default; keep it if there are no strings */
	sprintf (dev->dev.name, "USB device %04x:%04x",
		 dev->descriptor.idVendor,
		 dev->descriptor.idProduct);
	if (!mfgr && !prod)
		return;

	if (!(buf = kmalloc(256, GFP_KERNEL)))
		return;
	here = dev->dev.name;
	end = here + sizeof dev->dev.name - 2;
	*end = 0;

	/* much like pci ... describe as either:
	 * - both strings:   'product descr (vendor descr)'
	 * - product only:   'product descr (USB device vvvv:pppp)'
	 * - vendor only:    'USB device vvvv:pppp (vendor descr)'
	 * - neither string: 'USB device vvvv:pppp'
	 */
	if (prod && usb_string (dev, prod, buf, 256) > 0) {
		strncpy (here, buf, end - here);
#ifdef DEBUG
		printk (KERN_INFO "Product: %s\n", buf);
#endif
	} else {
		buf [0] = 0;
		prod = -1;
	}
	here = strchr (here, 0);
	if (mfgr && usb_string (dev, mfgr, buf, 256) > 0) {
		*here++ = ' ';
		*here++ = '(';
		strncpy (here, buf, end - here - 1);
		here = strchr (here, 0);
		*here++ = ')';
#ifdef DEBUG
		printk (KERN_INFO "Manufacturer: %s\n", buf);
#endif
	} else {
		if (prod != -1)
			snprintf (here, end - here - 1,
				" (USB device %04x:%04x)",
				dev->descriptor.idVendor,
				dev->descriptor.idProduct);
		/* both strings unavailable, keep the default */
	}
	kfree(buf);
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
#define NEW_DEVICE_RETRYS	2
#define SET_ADDRESS_RETRYS	2
int usb_new_device(struct usb_device *dev)
{
	int err = 0;
	int i;
	int j;

	/* USB v1.1 5.5.3 */
	/* We read the first 8 bytes from the device descriptor to get to */
	/*  the bMaxPacketSize0 field. Then we set the maximum packet size */
	/*  for the control pipe, and retrieve the rest */
	dev->epmaxpacketin [0] = 8;
	dev->epmaxpacketout[0] = 8;

	for (i = 0; i < NEW_DEVICE_RETRYS; ++i) {

		for (j = 0; j < SET_ADDRESS_RETRYS; ++j) {
			err = usb_set_address(dev);
			if (err >= 0)
				break;
			wait_ms(200);
		}
		if (err < 0) {
			err("USB device not accepting new address=%d (error=%d)",
				dev->devnum, err);
			clear_bit(dev->devnum, dev->bus->devmap.devicemap);
			dev->devnum = -1;
			return 1;
		}

		wait_ms(10);	/* Let the SET_ADDRESS settle */

		err = usb_get_descriptor(dev, USB_DT_DEVICE, 0, &dev->descriptor, 8);
		if (err >= 8)
			break;
		wait_ms(100);
	}

	if (err < 8) {
		if (err < 0)
			err("USB device not responding, giving up (error=%d)", err);
		else
			err("USB device descriptor short read (expected %i, got %i)", 8, err);
		clear_bit(dev->devnum, dev->bus->devmap.devicemap);
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
	
		clear_bit(dev->devnum, dev->bus->devmap.devicemap);
		dev->devnum = -1;
		return 1;
	}

	err = usb_get_configuration(dev);
	if (err < 0) {
		err("unable to get device %d configuration (error=%d)",
			dev->devnum, err);
		clear_bit(dev->devnum, dev->bus->devmap.devicemap);
		dev->devnum = -1;
		return 1;
	}

	/* we set the default configuration here */
	err = usb_set_configuration(dev, dev->config[0].bConfigurationValue);
	if (err) {
		err("failed to set device %d default configuration (error=%d)",
			dev->devnum, err);
		clear_bit(dev->devnum, dev->bus->devmap.devicemap);
		dev->devnum = -1;
		return 1;
	}

	dbg("new device strings: Mfr=%d, Product=%d, SerialNumber=%d",
		dev->descriptor.iManufacturer, dev->descriptor.iProduct, dev->descriptor.iSerialNumber);
	set_device_description (dev);

#ifdef DEBUG
	if (dev->descriptor.iSerialNumber)
		usb_show_string(dev, "SerialNumber", dev->descriptor.iSerialNumber);
#endif

	/* register this device in the driverfs tree */
	err = device_register (&dev->dev);
	if (err)
		return err;
	device_create_file (&dev->dev, &usb_config_entry);
	if (dev->descriptor.iManufacturer)
		device_create_file (&dev->dev, &usb_manufacturer_entry);
	if (dev->descriptor.iProduct)
		device_create_file (&dev->dev, &usb_product_entry);
	if (dev->descriptor.iSerialNumber)
		device_create_file (&dev->dev, &usb_serial_entry);

	/* now that the basic setup is over, add a /proc/bus/usb entry */
	usbfs_add_device(dev);

	/* find drivers willing to handle this device */
	usb_find_drivers(dev);

	/* userspace may load modules and/or configure further */
	call_policy ("add", dev);

	return 0;
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

struct bus_type usb_bus_type = {
	name:	"usb",
};

/*
 * Init
 */
static int __init usb_init(void)
{
	bus_register(&usb_bus_type);
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
	put_bus(&usb_bus_type);
	usb_major_cleanup();
	usbfs_cleanup();
	usb_hub_cleanup();
}

subsys_initcall(usb_init);
module_exit(usb_exit);

/*
 * USB may be built into the kernel or be built as modules.
 * These symbols are exported for device (or host controller)
 * driver modules to use.
 */
EXPORT_SYMBOL(usb_ifnum_to_ifpos);
EXPORT_SYMBOL(usb_ifnum_to_if);
EXPORT_SYMBOL(usb_epnum_to_ep_desc);

EXPORT_SYMBOL(usb_register);
EXPORT_SYMBOL(usb_deregister);
EXPORT_SYMBOL(usb_scan_devices);

EXPORT_SYMBOL(usb_alloc_dev);
EXPORT_SYMBOL(usb_free_dev);
EXPORT_SYMBOL(usb_get_dev);
EXPORT_SYMBOL(usb_hub_tt_clear_buffer);

EXPORT_SYMBOL(usb_find_interface_driver_for_ifnum);
EXPORT_SYMBOL(usb_driver_claim_interface);
EXPORT_SYMBOL(usb_interface_claimed);
EXPORT_SYMBOL(usb_driver_release_interface);
EXPORT_SYMBOL(usb_match_id);

EXPORT_SYMBOL(usb_root_hub_string);
EXPORT_SYMBOL(usb_new_device);
EXPORT_SYMBOL(usb_reset_device);
EXPORT_SYMBOL(usb_connect);
EXPORT_SYMBOL(usb_disconnect);
EXPORT_SYMBOL(usb_bind_driver);
EXPORT_SYMBOL(usb_unbind_driver);

EXPORT_SYMBOL(__usb_get_extra_descriptor);

EXPORT_SYMBOL(usb_get_current_frame_number);

MODULE_LICENSE("GPL");
