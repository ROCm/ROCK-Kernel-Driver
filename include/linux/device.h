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

#include <linux/types.h>
#include <linux/config.h>
#include <linux/ioport.h>
#include <linux/list.h>
#include <linux/sched.h>
#include <linux/driverfs_fs.h>

#define DEVICE_NAME_SIZE	80
#define DEVICE_ID_SIZE		32
#define BUS_ID_SIZE		16


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

enum {
	REMOVE_NOTIFY,
	REMOVE_FREE_RESOURCES,
};

struct device;
struct iobus;

struct device_driver {
	int	(*probe)	(struct device * dev);
	int 	(*remove)	(struct device * dev, u32 flags);

	int	(*suspend)	(struct device * dev, u32 state, u32 stage);
	int	(*resume)	(struct device * dev, u32 stage);
};

struct device {
	struct list_head node;		/* node in sibling list */
	struct iobus	*parent;	/* parent bus */

	struct iobus	*subordinate;	/* only valid if this device is a
					   bridge device */

	char	name[DEVICE_NAME_SIZE];	/* descriptive ascii string */
	char	bus_id[BUS_ID_SIZE];	/* position on parent bus */

	spinlock_t	lock;		/* lock for the device to ensure two
					   different layers don't access it at
					   the same time. */
	atomic_t	refcount;	/* refcount to make sure the device
					 * persists for the right amount of time */

	struct driver_dir_entry	dir;

	struct device_driver *driver;	/* which driver has allocated this
					   device */
	void		*driver_data;	/* data private to the driver */
	void		*platform_data;	/* Platform specific data (e.g. ACPI,
					   BIOS data relevant to device */

	u32		current_state;  /* Current operating state. In
					   ACPI-speak, this is D0-D3, D0
					   being fully functional, and D3
					   being off. */

	unsigned char *saved_state;	/* saved device state */
};

/*
 * struct bus_type - descriptor for a type of bus
 * There are some instances when you need to know what type of bus a
 * device is on. Instead of having some sort of enumerated integer type,
 * each struct iobus will have a pointer to a struct bus_type that gives
 * actually meaningful data.
 * There should be one struct bus_type for each type of bus (one for PCI,
 * one for USB, etc).
 */
struct iobus_driver {
	char	name[16];	/* ascii descriptor of type of bus */
	struct	list_head node; /* node in global list of bus drivers */

	int	(*scan)		(struct iobus*);
	int	(*add_device)	(struct iobus*, char*);
};

struct iobus {
	spinlock_t	lock;		/* lock for bus */
	atomic_t	refcount;

	struct list_head node;		/* node in sibling list */
	struct iobus *parent;		/* parent bus */
	struct list_head children;	/* children buses */
	struct list_head devices;	/* children devices */

	struct device *self;		/* pointer to controlling device */
	struct driver_dir_entry	dir;	/* driverfs directory */

	char	name[DEVICE_NAME_SIZE];
	char	bus_id[BUS_ID_SIZE];

	struct	iobus_driver *driver;	/* bus operations */
};

static inline struct device *
list_to_dev(struct list_head *node)
{
	return list_entry(node, struct device, node);
}

static inline struct iobus *
list_to_iobus(const struct list_head *node)
{
	return list_entry(node, struct iobus, node);
}

/*
 * High level routines for use by the bus drivers
 */
extern int device_register(struct device * dev);

extern int iobus_register(struct iobus * iobus);

extern int device_create_file(struct device *device, struct driver_file_entry * entry);
extern void device_remove_file(struct device * dev, const char * name);

/*
 * Platform "fixup" functions - allow the platform to have their say
 * about devices and actions that the general device layer doesn't
 * know about.
 */
/* Notify platform of device discovery */
extern int (*platform_notify)(struct device * dev);

extern int (*platform_notify_remove)(struct device * dev);

/* device and bus locking helpers.
 *
 * FIXME: Is there anything else we need to do?
 */
static inline void lock_device(struct device * dev)
{
	spin_lock(&dev->lock);
}

static inline void unlock_device(struct device * dev)
{
	spin_unlock(&dev->lock);
}

/**
 * get_device - atomically increment the reference count for the device.
 *
 */
static inline void get_device(struct device * dev)
{
	BUG_ON(!atomic_read(&dev->refcount));
	atomic_inc(&dev->refcount);
}

extern void put_device(struct device * dev);

static inline void lock_iobus(struct iobus * iobus)
{
	spin_lock(&iobus->lock);
}

static inline void unlock_iobus(struct iobus * iobus)
{
	spin_unlock(&iobus->lock);
}

static inline void get_iobus(struct iobus * iobus)
{
	BUG_ON(!atomic_read(&iobus->refcount));
	atomic_inc(&iobus->refcount);
}

extern void put_iobus(struct iobus * iobus);

#endif /* _DEVICE_H_ */
