/*
 * intf.c - class-specific interface management
 */

#undef DEBUG

#include <linux/device.h>
#include <linux/module.h>
#include <linux/string.h>
#include "base.h"


#define to_intf(node) container_of(node,struct device_interface,kset.kobj.entry)

#define to_dev(d) container_of(d,struct device,class_list)

/**
 *	intf_dev_link - create sysfs symlink for interface.
 *	@intf:	interface.
 *	@dev:	device.
 *
 *	Create a symlink 'phys' in the interface's directory to 
 */

static int intf_dev_link(struct device_interface * intf, struct device * dev)
{
	return sysfs_create_link(&intf->kset.kobj,&dev->kobj,dev->bus_id);
}

/**
 *	intf_dev_unlink - remove symlink for interface.
 *	@intf:	interface.
 *	@dev:	device.
 *
 */

static void intf_dev_unlink(struct device_interface * intf, struct device * dev)
{
	sysfs_remove_link(&intf->kset.kobj,dev->bus_id);
}


/**
 *	add - attach device to interface
 *	@intf:	interface.
 *	@dev:	device.
 *
 *	This is just a simple helper. Check the interface's interface
 *	helper and call it. This is called when adding an interface
 *	the class's devices, or a device to the class's interfaces.
 */

static int add(struct device_interface * intf, struct device * dev)
{
	int error = 0;

	if (intf->add_device) {
		if (!(error = intf->add_device(dev)))
			intf_dev_link(intf,dev);
	}
	pr_debug(" -> %s (%d)\n",dev->bus_id,error);
	return error;
}

/**
 *	del - detach device from interface.
 *	@intf:	interface.
 *	@dev:	device.
 */

static void del(struct device_interface * intf, struct device * dev)
{
	pr_debug(" -> %s ",intf->name);
	if (intf->remove_device)
		intf->remove_device(dev);
	intf_dev_unlink(intf,dev);
}


/**
 *	add_intf - add class's devices to interface.
 *	@intf:	interface.
 *
 *	Loop over the devices registered with the class, and call
 *	the interface's add_device() method for each.
 *
 *	On an error, we won't break, but we will print debugging info.
 */
static void add_intf(struct device_interface * intf)
{
	struct device_class * cls = intf->devclass;
	struct list_head * entry;

	list_for_each(entry,&cls->devices.list)
		add(intf,to_dev(entry));
}

/**
 *	interface_register - register an interface with a device class.
 *	@intf:	interface.
 *
 *	An interface may be loaded after drivers and devices have been
 *	added to the class. So, we must add each device already known to
 *	the class to the interface as its registered.
 */

int interface_register(struct device_interface * intf)
{
	struct device_class * cls = get_devclass(intf->devclass);

	down(&devclass_sem);
	if (cls) {
		pr_debug("register interface '%s' with class '%s'\n",
			 intf->name,cls->name);

		strncpy(intf->kset.kobj.name,intf->name,KOBJ_NAME_LEN);
		kset_set_kset_s(intf,cls->subsys);
		kset_register(&intf->kset);
		add_intf(intf);
	}
	up(&devclass_sem);
	return 0;
}


/**
 *	del_intf - remove devices from interface.
 *	@intf:	interface being unloaded.
 *
 *	This loops over the devices registered with a class and 
 *	calls the interface's remove_device() method for each.
 *	This is called when an interface is being unregistered.
 */

static void del_intf(struct device_interface * intf)
{
	struct device_class * cls = intf->devclass;
	struct list_head * entry;

	list_for_each(entry,&cls->devices.list) {
		struct device * dev = to_dev(entry);
		del(intf,dev);
	}
}

/**
 *	interface_unregister - remove interface from class.
 *	@intf:	interface.
 *
 *	This is called when an interface in unloaded, giving it a
 *	chance to remove itself from devicse that have been added to 
 *	it.
 */

void interface_unregister(struct device_interface * intf)
{
	struct device_class * cls = intf->devclass;

	down(&devclass_sem);
	if (cls) {
		pr_debug("unregistering interface '%s' from class '%s'\n",
			 intf->name,cls->name);
		del_intf(intf);
		kset_unregister(&intf->kset);
		put_devclass(cls);
	}
	up(&devclass_sem);
}


/**
 *	interface_add_dev - add device to interfaces.
 *	@dev:	device.
 *
 *	This is a helper for the class driver core. When a 
 *	device is being added to a class, this is called to add
 *	the device to all the interfaces in the class.
 *
 *	The operation is simple enough: loop over the interfaces
 *	and call add() [above] for each. The class rwsem is assumed
 *	to be held.
 */

int interface_add_dev(struct device * dev)
{
	struct device_class * cls = dev->driver->devclass;
	struct list_head * node;

	pr_debug("interfaces: adding device %s\n",dev->name);

	list_for_each(node,&cls->subsys.kset.list) {
		struct device_interface * intf = to_intf(node);
		add(intf,dev);
	}
	return 0;
}


/**
 *	interface_remove_dev - remove device from interfaces.
 *	@dev:	device.
 *
 *	This is another helper for the class driver core, and called
 *	when the device is being removed from the class. 
 *	
 *	We iterate over the list of the class's devices and call del() 
 *	[above] for each. Again, the class's rwsem is _not_ held, but
 *	the devclass_sem is (see class.c).
 */

void interface_remove_dev(struct device * dev)
{
	struct list_head * entry, * next;
	struct device_class * cls = dev->driver->devclass;

	pr_debug("interfaces: removing device %s\n",dev->name);

	list_for_each_safe(entry,next,&cls->subsys.kset.list) {
		struct device_interface * intf = to_intf(entry);
		del(intf,dev);
	}
}

EXPORT_SYMBOL(interface_register);
EXPORT_SYMBOL(interface_unregister);
