/*
 * device.h - generic, centralized driver model
 *
 * Copyright (c) 2001 Patrick Mochel <mochel@osdl.org>
 *
 * This is a relatively simple centralized driver model.
 * The data structures were mainly lifted directly from the PCI
 * driver model. These are thought to be the common fields that
 * are relevant to all device buses.
 *
 * All the devices are arranged in a tree. All devices should
 * have some sort of parent bus of whom they are children of.
 * Devices should not be direct children of the system root.
 *
 * Device drivers should not directly call the device_* routines
 * or access the contents of struct device directly. Instead,
 * abstract that from the drivers and write bus-specific wrappers
 * that do it for you.
 *
 * See Documentation/driver-model.txt for more information.
 */

#ifndef _DEVICE_H_
#define _DEVICE_H_

#include <linux/config.h>
#include <linux/ioport.h>
#include <linux/kobject.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <asm/semaphore.h>
#include <asm/atomic.h>

#define DEVICE_NAME_SIZE	50
#define DEVICE_NAME_HALF	__stringify(20)	/* Less than half to accommodate slop */
#define DEVICE_ID_SIZE		32
#define BUS_ID_SIZE		20


enum {
	SUSPEND_NOTIFY,
	SUSPEND_SAVE_STATE,
	SUSPEND_DISABLE,
	SUSPEND_POWER_DOWN,
};

enum {
	RESUME_POWER_ON,
	RESUME_RESTORE_STATE,
	RESUME_ENABLE,
};

enum device_state {
	DEVICE_UNINITIALIZED	= 0,
	DEVICE_INITIALIZED	= 1,
	DEVICE_REGISTERED	= 2,
	DEVICE_GONE		= 3,
};

struct device;
struct device_driver;
struct device_class;

struct bus_type {
	char			* name;

	struct subsystem	subsys;
	struct kset		drivers;
	struct kset		devices;

	int		(*match)(struct device * dev, struct device_driver * drv);
	struct device * (*add)	(struct device * parent, char * bus_id);
	int		(*hotplug) (struct device *dev, char **envp, 
				    int num_envp, char *buffer, int buffer_size);
};


extern int bus_register(struct bus_type * bus);
extern void bus_unregister(struct bus_type * bus);

extern int bus_rescan_devices(struct bus_type * bus);

extern struct bus_type * get_bus(struct bus_type * bus);
extern void put_bus(struct bus_type * bus);

extern struct bus_type * find_bus(char * name);

/* iterator helpers for buses */

int bus_for_each_dev(struct bus_type * bus, struct device * start, void * data,
		     int (*fn)(struct device *, void *));

int bus_for_each_drv(struct bus_type * bus, struct device_driver * start, 
		     void * data, int (*fn)(struct device_driver *, void *));


/* driverfs interface for exporting bus attributes */

struct bus_attribute {
	struct attribute	attr;
	ssize_t (*show)(struct bus_type *, char * buf);
	ssize_t (*store)(struct bus_type *, const char * buf, size_t count);
};

#define BUS_ATTR(_name,_mode,_show,_store)	\
struct bus_attribute bus_attr_##_name = { 		\
	.attr = {.name = __stringify(_name), .mode = _mode },	\
	.show	= _show,				\
	.store	= _store,				\
};

extern int bus_create_file(struct bus_type *, struct bus_attribute *);
extern void bus_remove_file(struct bus_type *, struct bus_attribute *);

struct device_driver {
	char			* name;
	struct bus_type		* bus;
	struct device_class	* devclass;

	struct semaphore	unload_sem;
	struct kobject		kobj;
	struct list_head	class_list;
	struct list_head	devices;

	int	(*probe)	(struct device * dev);
	int 	(*remove)	(struct device * dev);
	void	(*shutdown)	(struct device * dev);
	int	(*suspend)	(struct device * dev, u32 state, u32 level);
	int	(*resume)	(struct device * dev, u32 level);
};


extern int driver_register(struct device_driver * drv);
extern void driver_unregister(struct device_driver * drv);

extern struct device_driver * get_driver(struct device_driver * drv);
extern void put_driver(struct device_driver * drv);


/* driverfs interface for exporting driver attributes */

struct driver_attribute {
	struct attribute	attr;
	ssize_t (*show)(struct device_driver *, char * buf);
	ssize_t (*store)(struct device_driver *, const char * buf, size_t count);
	int (*exists)(struct device_driver *);
};

#define DRIVER_ATTR(_name,_mode,_show,_store)	\
struct driver_attribute driver_attr_##_name = { 		\
	.attr = {.name = __stringify(_name), .mode = _mode },	\
	.show	= _show,				\
	.store	= _store,				\
	.exists	= NULL,				        \
};

#define DRIVER_ATTR_EXISTS(_name,_mode,_show,_store,_exists)	\
struct driver_attribute driver_attr_##_name = { 		\
	.attr = {.name = __stringify(_name), .mode = _mode },	\
	.show	= _show,				\
	.store	= _store,				\
	.exists	= _exists,				\
};


extern int driver_create_file(struct device_driver *, struct driver_attribute *);
extern void driver_remove_file(struct device_driver *, struct driver_attribute *);


/*
 * device classes
 */
struct device_class {
	char			* name;
	u32			devnum;

	struct subsystem	subsys;
	struct kset		devices;
	struct kset		drivers;

	int	(*add_device)(struct device *);
	void	(*remove_device)(struct device *);
	int	(*hotplug)(struct device *dev, char **envp, 
			   int num_envp, char *buffer, int buffer_size);
};

extern int devclass_register(struct device_class *);
extern void devclass_unregister(struct device_class *);

extern struct device_class * get_devclass(struct device_class *);
extern void put_devclass(struct device_class *);


struct devclass_attribute {
	struct attribute	attr;
	ssize_t (*show)(struct device_class *, char * buf);
	ssize_t (*store)(struct device_class *, const char * buf, size_t count);
};

#define DEVCLASS_ATTR(_name,_str,_mode,_show,_store)	\
struct devclass_attribute devclass_attr_##_name = { 		\
	.attr = {.name	= _str,	.mode	= _mode },	\
	.show	= _show,				\
	.store	= _store,				\
};

extern int devclass_create_file(struct device_class *, struct devclass_attribute *);
extern void devclass_remove_file(struct device_class *, struct devclass_attribute *);


/*
 * device interfaces
 * These are the logical interfaces of device classes. 
 * These entities map directly to specific userspace interfaces, like 
 * device nodes.
 * Interfaces are registered with the device class they belong to. When
 * a device is registered with the class, each interface's add_device 
 * callback is called. It is up to the interface to decide whether or not
 * it supports the device.
 */

struct device_interface {
	char			* name;
	struct device_class	* devclass;

	struct kset		kset;
	u32			devnum;

	int (*add_device)	(struct device *);
	int (*remove_device)	(struct device *);
};

extern int interface_register(struct device_interface *);
extern void interface_unregister(struct device_interface *);


struct device {
	struct list_head node;		/* node in sibling list */
	struct list_head bus_list;	/* node in bus's list */
	struct list_head class_list;
	struct list_head driver_list;
	struct list_head children;
	struct device 	* parent;

	struct kobject kobj;
	char	name[DEVICE_NAME_SIZE];	/* descriptive ascii string */
	char	bus_id[BUS_ID_SIZE];	/* position on parent bus */

	struct bus_type	* bus;		/* type of bus device is on */
	struct device_driver *driver;	/* which driver has allocated this
					   device */
	void		*driver_data;	/* data private to the driver */

	u32		class_num;	/* class-enumerated value */
	void		* class_data;	/* class-specific data */

	void		*platform_data;	/* Platform specific data (e.g. ACPI,
					   BIOS data relevant to device) */

	u32		power_state;  /* Current operating state. In
					   ACPI-speak, this is D0-D3, D0
					   being fully functional, and D3
					   being off. */

	unsigned char *saved_state;	/* saved device state */
	u64		*dma_mask;	/* dma mask (if dma'able device) */

	void	(*release)(struct device * dev);
};

static inline struct device *
list_to_dev(struct list_head *node)
{
	return list_entry(node, struct device, node);
}

static inline void *
dev_get_drvdata (struct device *dev)
{
	return dev->driver_data;
}

static inline void
dev_set_drvdata (struct device *dev, void *data)
{
	dev->driver_data = data;
}

/*
 * High level routines for use by the bus drivers
 */
extern int device_register(struct device * dev);
extern void device_unregister(struct device * dev);
extern void device_initialize(struct device * dev);
extern int device_add(struct device * dev);
extern void device_del(struct device * dev);

/*
 * Manual binding of a device to driver. See drivers/base/bus.c 
 * for information on use.
 */
extern void device_bind_driver(struct device * dev);
extern void device_release_driver(struct device * dev);


/* driverfs interface for exporting device attributes */

struct device_attribute {
	struct attribute	attr;
	ssize_t (*show)(struct device * dev, char * buf);
	ssize_t (*store)(struct device * dev, const char * buf, size_t count);
};

#define DEVICE_ATTR(_name,_mode,_show,_store) \
struct device_attribute dev_attr_##_name = { 		\
	.attr = {.name = __stringify(_name), .mode = _mode },	\
	.show	= _show,				\
	.store	= _store,				\
};


extern int device_create_file(struct device *device, struct device_attribute * entry);
extern void device_remove_file(struct device * dev, struct device_attribute * attr);

/*
 * Platform "fixup" functions - allow the platform to have their say
 * about devices and actions that the general device layer doesn't
 * know about.
 */
/* Notify platform of device discovery */
extern int (*platform_notify)(struct device * dev);

extern int (*platform_notify_remove)(struct device * dev);


/**
 * get_device - atomically increment the reference count for the device.
 *
 */
extern struct device * get_device(struct device * dev);
extern void put_device(struct device * dev);

/* drivers/base/sys.c */

struct sys_root {
	u32		id;
	struct device 	dev;
	struct device	sysdev;
};

extern int sys_register_root(struct sys_root *);
extern void sys_unregister_root(struct sys_root *);


struct sys_device {
	char		* name;
	u32		id;
	struct sys_root	* root;
	struct device	dev;
};

extern int sys_device_register(struct sys_device *);
extern void sys_device_unregister(struct sys_device *);

extern struct bus_type system_bus_type;

/* drivers/base/platform.c */

struct platform_device {
	char		* name;
	u32		id;
	struct device	dev;
};

extern int platform_device_register(struct platform_device *);
extern void platform_device_unregister(struct platform_device *);

extern struct bus_type platform_bus_type;
extern struct device legacy_bus;

/* drivers/base/power.c */
extern int device_suspend(u32 state, u32 level);
extern void device_resume(u32 level);
extern void device_shutdown(void);


/* drivers/base/firmware.c */
extern int firmware_register(struct subsystem *);
extern void firmware_unregister(struct subsystem *);

/* debugging and troubleshooting/diagnostic helpers. */
#define dev_printk(level, dev, format, arg...)	\
	printk(level "%s %s: " format , (dev)->driver->name , (dev)->bus_id , ## arg)

#ifdef DEBUG
#define dev_dbg(dev, format, arg...)		\
	dev_printk(KERN_DEBUG , dev , format , ## arg)
#else
#define dev_dbg(dev, format, arg...) do {} while (0)
#endif

#define dev_err(dev, format, arg...)		\
	dev_printk(KERN_ERR , dev , format , ## arg)
#define dev_info(dev, format, arg...)		\
	dev_printk(KERN_INFO , dev , format , ## arg)
#define dev_warn(dev, format, arg...)		\
	dev_printk(KERN_WARNING , dev , format , ## arg)

#endif /* _DEVICE_H_ */
