/*
 * intf.c - class-specific interface management
 */

#define DEBUG

#include <linux/device.h>
#include <linux/module.h>
#include <linux/string.h>
#include "base.h"


#define to_intf(node) container_of(node,struct device_interface,subsys.kobj.entry)

#define to_data(e) container_of(e,struct intf_data,kobj.entry)

#define intf_from_data(d) container_of(d->kobj.subsys,struct device_interface, subsys);


/**
 *	intf_dev_link - create sysfs symlink for interface.
 *	@data:	interface data descriptor.
 *
 *	Create a symlink 'phys' in the interface's directory to 
 */

static int intf_dev_link(struct intf_data * data)
{
	char	name[16];
	snprintf(name,16,"%d",data->intf_num);
	return sysfs_create_link(&data->intf->subsys.kobj,&data->dev->kobj,name);
}

/**
 *	intf_dev_unlink - remove symlink for interface.
 *	@intf:	interface data descriptor.
 *
 */

static void intf_dev_unlink(struct intf_data * data)
{
	char	name[16];
	snprintf(name,16,"%d",data->intf_num);
	sysfs_remove_link(&data->intf->subsys.kobj,name);
}


/**
 *	interface_add_data - attach data descriptor
 *	@data:	interface data descriptor.
 *
 *	This attaches the per-instance interface object to the
 *	interface (by registering its kobject) and the device
 *	itself (by inserting it into the device's list).
 *
 *	Note that there is no explicit protection done in this
 *	function. This should be called from the interface's 
 *	add_device() method, which is called under the protection
 *	of the class's rwsem.
 */

int interface_add_data(struct intf_data * data)
{
	struct device_interface * intf = intf_from_data(data);

	data->intf_num = data->intf->devnum++;
	data->kobj.subsys = &intf->subsys;
	kobject_register(&data->kobj);

	list_add_tail(&data->dev_entry,&data->dev->intf_list);
	intf_dev_link(data);
	return 0;
}


/**
 *	interface_remove_data - detach data descriptor.
 *	@data:	interface data descriptor.
 *
 *	This detaches the per-instance data descriptor by removing 
 *	it from the device's list and unregistering the kobject from
 *	the subsystem.
 */

void interface_remove_data(struct intf_data * data)
{
	intf_dev_unlink(data);
	list_del_init(&data->dev_entry);
	kobject_unregister(&data->kobj);
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

	if (intf->add_device) 
		error = intf->add_device(dev);
	pr_debug(" -> %s (%d)\n",dev->bus_id,error);
	return error;
}

/**
 *	del - detach device from interface.
 *	@data:	interface data descriptor.
 *
 *	Another simple helper. Remove the data descriptor from 
 *	the device and the interface, then call the interface's 
 *	remove_device() method.
 */

static void del(struct intf_data * data)
{
	struct device_interface * intf = intf_from_data(data);

	pr_debug(" -> %s ",data->intf->name);
	interface_remove_data(data);
	if (intf->remove_device)
		intf->remove_device(data);
}

#define to_dev(entry) container_of(entry,struct device,class_list)


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

	down_write(&cls->subsys.rwsem);
	list_for_each(entry,&cls->devices)
		add(intf,to_dev(entry));
	up_write(&cls->subsys.rwsem);
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

	if (cls) {
		pr_debug("register interface '%s' with class '%s'\n",
			 intf->name,cls->name);

		strncpy(intf->subsys.kobj.name,intf->name,KOBJ_NAME_LEN);
		intf->subsys.kobj.subsys = &cls->subsys;
		subsystem_register(&intf->subsys);
		add_intf(intf);
	}
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
	struct list_head * entry;

	down_write(&intf->devclass->subsys.rwsem);
	list_for_each(entry,&intf->subsys.list) {
		struct intf_data * data = to_data(entry);
		del(data);
	}
	up_write(&intf->devclass->subsys.rwsem);
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
	if (cls) {
		pr_debug("unregistering interface '%s' from class '%s'\n",
			 intf->name,cls->name);
		del_intf(intf);
		subsystem_unregister(&intf->subsys);
		put_devclass(cls);
	}
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

	list_for_each(node,&cls->subsys.list) {
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
 *	We iterate over the list of interface data descriptors attached
 *	to the device, and call del() [above] for each. Again, the 
 *	class's rwsem is assumed to be held during this.
 */

void interface_remove_dev(struct device * dev)
{
	struct list_head * entry, * next;

	pr_debug("interfaces: removing device %s\n",dev->name);

	list_for_each_safe(entry,next,&dev->intf_list) {
		struct intf_data * intf_data = to_data(entry);
		del(intf_data);
	}
}

EXPORT_SYMBOL(interface_register);
EXPORT_SYMBOL(interface_unregister);
