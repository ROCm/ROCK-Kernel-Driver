/*
 * device.c
 *
 * Copyright (c) Patrick Mochel <mochel@osdl.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Please see Documentation/driver-model.txt for more explanation.
 */

#include <linux/slab.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/pagemap.h>
#include <linux/module.h>
#include <linux/stat.h>
#include <linux/driverfs_fs.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/spinlock.h>

#undef DEBUG

#ifdef DEBUG
# define DBG(x...) printk(x)
#else
# define DBG(x...)
#endif

static struct iobus device_root = {
	bus_id: "root",
	name:	"Logical System Root",
};

int (*platform_notify)(struct device * dev) = NULL;
int (*platform_notify_remove)(struct device * dev) = NULL;

static spinlock_t device_lock;

static ssize_t device_read_status(struct device *, char *, size_t, loff_t);
static ssize_t device_write_status(struct device *,const char *, size_t, loff_t);

static struct driver_file_entry device_status_entry = {
	name:		"status",
	mode:		S_IWUSR | S_IRUGO,
	show:		device_read_status,
	store:		device_write_status,
};

static ssize_t device_read_power(struct device *, char *, size_t, loff_t);
static ssize_t device_write_power(struct device *, const char *, size_t, loff_t);

static struct driver_file_entry device_power_entry = {
	name:		"power",
	mode:		S_IWUSR | S_IRUGO,
	show:		device_read_power,
	store:		device_write_power,
};

/**
 * device_create_file - create a driverfs file for a device
 * @dev:	device requesting file
 * @entry:	entry describing file
 *
 * Allocate space for file entry, copy descriptor, and create.
 */
int device_create_file(struct device * dev, struct driver_file_entry * entry)
{
	struct driver_file_entry * new_entry;
	int error = -ENOMEM;

	if (!dev)
		return -EINVAL;

	if (!valid_device(dev))
		return -EFAULT;

	new_entry = kmalloc(sizeof(*new_entry),GFP_KERNEL);
	if (!new_entry)
		goto done;

	memcpy(new_entry,entry,sizeof(*entry));
	error = driverfs_create_file(new_entry,&dev->dir);
	if (error)
		kfree(new_entry);
 done:
	put_device(dev);
	return error;
}

/**
 * device_remove_file - remove a device's file by name
 * @dev:	device requesting removal
 * @name:	name of the file
 *
 */
void device_remove_file(struct device * dev, const char * name)
{
	if (!dev)
		return;

	if (!valid_device(dev))
		return;

	driverfs_remove_file(&dev->dir,name);

	put_device(dev);
}

/**
 * device_remove_dir - remove a device's directory
 * @dev:	device in question
 */
void device_remove_dir(struct device * dev)
{
	if (dev)
		driverfs_remove_dir(&dev->dir);
}

/**
 * device_make_dir - create a driverfs directory
 * @name:	name of directory
 * @parent:	dentry for the parent directory
 *
 * Do the initial creation of the device's driverfs directory
 * and populate it with the one default file.
 *
 * This is just a helper for device_register(), as we
 * don't export this function. (Yes, that means we don't allow
 * devices to create subdirectories).
 */
static int device_make_dir(struct device * dev)
{
	struct driver_dir_entry * parent = NULL;
	int error;

	INIT_LIST_HEAD(&dev->dir.files);
	dev->dir.mode = (S_IFDIR| S_IRWXU | S_IRUGO | S_IXUGO);
	dev->dir.name = dev->bus_id;

	if (dev->parent)
		parent = &dev->parent->dir;

	error = driverfs_create_dir(&dev->dir,parent);

	if (error)
		return error;

	error = device_create_file(dev,&device_status_entry);
	if (error) {
		device_remove_dir(dev);
		goto done;
	}
	error = device_create_file(dev,&device_power_entry);
	if (error) 
		device_remove_dir(dev);
 done:
	return error;
}

void iobus_remove_dir(struct iobus * iobus)
{
	if (iobus)
		driverfs_remove_dir(&iobus->dir);
}

static int iobus_make_dir(struct iobus * iobus)
{
	struct driver_dir_entry * parent = NULL;
	int error;

	INIT_LIST_HEAD(&iobus->dir.files);
	iobus->dir.mode = (S_IFDIR| S_IRWXU | S_IRUGO | S_IXUGO);
	iobus->dir.name = iobus->bus_id;

	if (iobus->parent)
		parent = &iobus->parent->dir;

	error = driverfs_create_dir(&iobus->dir,parent);
	return error;
}

/**
 * device_register - register a device
 * @dev:	pointer to the device structure
 *
 * First, make sure that the device has a parent, create
 * a directory for it, then add it to the parent's list of
 * children.
 */
int device_register(struct device *dev)
{
	struct iobus * parent;
	int error = -EFAULT;

	if (!dev)
		return -EINVAL;

	if (!dev->parent)
		dev->parent = &device_root;
	parent = dev->parent;

	DBG("DEV: registering device: ID = '%s', name = %s, parent = %s\n",
	    dev->bus_id, dev->name, parent->bus_id);

	if (valid_iobus(parent)) {
		if (!valid_device(dev)) {
			put_iobus(parent);
			goto register_done;
		}
	} else
		return -EFAULT;

	if (!strlen(dev->name)) {
		error = -EINVAL;
		goto register_done;
	}

	error = device_make_dir(dev);
	if (error)
		goto register_done;


	/* finally add it to its parent's list */
	lock_iobus(parent);
	list_add_tail(&dev->node, &parent->devices);
	unlock_iobus(parent);

	/* notify platform of device entry */
	if (platform_notify)
		platform_notify(dev);

 register_done:
	put_device(dev);
	put_iobus(parent);

	return error;
}

/**
 * put_device - clean up device
 * @dev:	device in question
 *
 * Decrement reference count for device.
 * If it hits 0, we need to clean it up.
 * However, we may be here in interrupt context, and it may
 * take some time to do proper clean up (removing files, calling
 * back down to device to clean up everything it has).
 * So, we remove it from its parent's list and add it to the list of
 * devices to be cleaned up.
 */
void put_device(struct device * dev)
{
	struct iobus * parent;

	if (!atomic_dec_and_lock(&dev->refcount,&dev->lock))
		return;

	parent = dev->parent;
	dev->parent = NULL;
	unlock_device(dev);

	DBG("DEV: Unregistering device. ID = '%s', name = '%s'\n",
	    dev->bus_id,dev->name);

	/* disavow parent's knowledge */
	lock_iobus(parent);
	list_del_init(&dev->node);
	unlock_iobus(parent);

	/* remove the driverfs directory */
	device_remove_dir(dev);

	if (dev->subordinate)
		iobus_remove_dir(dev->subordinate);

	/* Notify the platform of the removal, in case they
	 * need to do anything...
	 */
	if (platform_notify_remove)
		platform_notify_remove(dev);

	/* Tell the driver to clean up after itself.
	 * Note that we likely didn't allocate the device,
	 * so this is the driver's chance to free that up...
	 */
	if (dev->driver && dev->driver->remove)
		dev->driver->remove(dev,REMOVE_FREE_RESOURCES);

	put_iobus(parent);
}

int iobus_register(struct iobus *bus)
{
	struct iobus * parent;
	int error = -EINVAL;

	if (!bus)
		return -EINVAL;

	if (!bus->parent)
		bus->parent = &device_root;
	parent = bus->parent;

	DBG("DEV: registering bus. ID = '%s' name = '%s' parent = %p\n",
	    bus->bus_id,bus->name,bus->parent);

	if (valid_iobus(parent)) {
		if (!valid_iobus(bus)) {
			put_iobus(parent);
			goto register_done;
		}
	} else
		goto register_done;

	if (!strlen(bus->bus_id))
		goto register_done_put;

	error = iobus_make_dir(bus);
	if (error)
		goto register_done_put;

	lock_iobus(parent);
	list_add_tail(&bus->node,&parent->children);
	unlock_iobus(parent);

 register_done_put:
	put_iobus(bus);
	put_iobus(parent);
 register_done:
	return error;
}

/**
 * iobus_unregister - remove bus and children from device tree
 * @bus:	pointer to bus structure
 *
 * Remove device from parent's list of children and decrement
 * reference count on controlling device. That should take care of
 * the rest of the cleanup.
 */
void put_iobus(struct iobus * iobus)
{
	struct iobus * parent;

	if (!atomic_dec_and_lock(&iobus->refcount,&iobus->lock))
		return;

	parent = iobus->parent;
	iobus->parent = NULL;
	unlock_iobus(iobus);

	if (!list_empty(&iobus->devices) ||
	    !list_empty(&iobus->children))
		BUG();

	/* disavow parent's knowledge */
	if (parent) {
		lock_iobus(parent);
		list_del(&iobus->node);
		unlock_iobus(parent);

		put_iobus(parent);
	}

	/* unregister itself */
	put_device(iobus->self);

	return;
}

/**
 * device_init_dev - initialise a struct device
 * @dev:	pointer to device struct
 */
void device_init_dev(struct device * dev)
{
	INIT_LIST_HEAD(&dev->node);
	spin_lock_init(&dev->lock);
	atomic_set(&dev->refcount,1);
}

/**
 * device_alloc_dev - allocate and initialise a device structure
 *
 */
struct device * device_alloc(void)
{
	struct device * dev;

	dev = kmalloc(sizeof(struct device), GFP_KERNEL);

	if (!dev)
		return NULL;

	memset(dev,0,sizeof(struct device));
	device_init_dev(dev);

	return dev;
}

void iobus_init(struct iobus *bus)
{
	spin_lock_init(&bus->lock);
	atomic_set(&bus->refcount,1);

	INIT_LIST_HEAD(&bus->node);
	INIT_LIST_HEAD(&bus->children);
	INIT_LIST_HEAD(&bus->devices);
}

struct iobus *iobus_alloc(void)
{
	struct iobus *bus;

	bus = kmalloc(sizeof(struct iobus), GFP_KERNEL);

	if (!bus)
		return NULL;

	memset(bus,0,sizeof(struct iobus));

	iobus_init(bus);

	return bus;
}

/**
 * device_read_status - report some device information
 * @page:	page-sized buffer to write into
 * @count:	number of bytes requested
 * @off:	offset into buffer
 * @data:	device-specific data
 *
 * Report some human-readable information about the device.
 * This includes the name, the bus id, and the current power state.
 */
static ssize_t device_read_status(struct device * dev, char * page, size_t count, loff_t off)
{
	char *str = page;

	if (off)
		return 0;

	str += sprintf(str,"Name:       %s\n",dev->name);
	str += sprintf(str,"Bus ID:     %s\n",dev->bus_id);

	return (str - page);
}

/**
 * device_write_status - forward a command to a driver
 * @buf:	encoded command
 * @count:	number of bytes in buffer
 * @off:	offset into buffer to start with
 * @data:	device-specific data
 *
 * Send a comamnd to a device driver.
 * The following actions are supported:
 * probe - scan slot for device
 * remove - detach driver from slot
 * suspend <state> <stage> - perform <stage> for entering <state>
 * resume <stage> - perform <stage> for waking device up.
 * (See Documentation/driver-model.txt for the theory of an n-stage
 * suspend sequence).
 */
static ssize_t device_write_status(struct device * dev, const char* buf, size_t count, loff_t off)
{
	char command[20];
	int num;
	int arg = 0;
	int error = 0;

	if (off)
		return 0;

	/* everything involves dealing with the driver. */
	if (!dev->driver)
		return 0;

	num = sscanf(buf,"%10s %d",command,&arg);

	if (!num)
		return 0;

	if (!strcmp(command,"probe")) {
		if (dev->driver->probe)
			error = dev->driver->probe(dev);

	} else if (!strcmp(command,"remove")) {
		if (dev->driver->remove)
			error = dev->driver->remove(dev,REMOVE_NOTIFY);
	} else
		error = -EFAULT;
	return error < 0 ? error : count;
}

static ssize_t
device_read_power(struct device * dev, char * page, size_t count, loff_t off)
{
	char	* str = page;

	if (off)
		return 0;

	str += sprintf(str,"State:      %d\n",dev->current_state);

	return (str - page);
}

static ssize_t
device_write_power(struct device * dev, const char * buf, size_t count, loff_t off)
{
	char	str_command[20];
	char	str_stage[20];
	int	num_args;
	u32	state;
	u32	int_stage;
	int	error = 0;

	if (off)
		return 0;

	if (!dev->driver)
		goto done;

	num_args = sscanf(buf,"%s %s %u",str_command,str_stage,&state);

	error = -EINVAL;

	if (!num_args)
		goto done;

	if (!strnicmp(str_command,"suspend",7)) {
		if (num_args != 3)
			goto done;
		if (!strnicmp(str_stage,"notify",6))
			int_stage = SUSPEND_NOTIFY;
		else if (!strnicmp(str_stage,"save",4))
			int_stage = SUSPEND_SAVE_STATE;
		else if (!strnicmp(str_stage,"disable",7))
			int_stage = SUSPEND_DISABLE;
		else if (!strnicmp(str_stage,"powerdown",8))
			int_stage = SUSPEND_POWER_DOWN;
		else
			goto done;

		if (dev->driver->suspend)
			error = dev->driver->suspend(dev,state,int_stage);
		else
			error = 0;
	} else if (!strnicmp(str_command,"resume",6)) {
		if (num_args != 2)
			goto done;

		if (!strnicmp(str_stage,"poweron",7))
			int_stage = RESUME_POWER_ON;
		else if (!strnicmp(str_stage,"restore",7))
			int_stage = RESUME_RESTORE_STATE;
		else if (!strnicmp(str_stage,"enable",6))
			int_stage = RESUME_ENABLE;
		else
			goto done;

		if (dev->driver->resume)
			error = dev->driver->resume(dev,int_stage);
		else
			error = 0;
	}
 done:
	return error < 0 ? error : count;
}

static int __init device_init_root(void)
{
	/* initialize parent bus lists */
	iobus_init(&device_root);

	/* don't call iobus_register, as the only thing it really
	 * needs to do is create the root directory. Easier
	 * to just do it here than special case it elsewhere..
	 */
	return iobus_make_dir(&device_root);
}

int __init device_driver_init(void)
{
	int error = 0;

	DBG("DEV: Initialising Device Tree\n");

	spin_lock_init(&device_lock);

	error = init_driverfs_fs();

	if (error) {
		panic("DEV: could not initialise driverfs\n");
		return error;
	}

	error = device_init_root();
	if (error) {
		printk(KERN_ERR "%s: device root init failed!\n", __FUNCTION__);
		return error;
	}

	DBG("DEV: Done Initialising\n");
	return error;
}

void __exit device_driver_exit(void)
{

}

static int __init device_setup(char *str)
{
	return 0;
}

__setup("device=",device_setup);

EXPORT_SYMBOL(device_register);
EXPORT_SYMBOL(device_alloc);
EXPORT_SYMBOL(device_init_dev);

EXPORT_SYMBOL(device_create_file);
EXPORT_SYMBOL(device_remove_file);

EXPORT_SYMBOL(iobus_register);
EXPORT_SYMBOL(iobus_alloc);
EXPORT_SYMBOL(iobus_init);

EXPORT_SYMBOL(device_driver_init);
