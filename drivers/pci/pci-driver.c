/*
 * drivers/pci/pci-driver.c
 *
 */

#include <linux/pci.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/pci-dynids.h>
#include "pci.h"

/*
 *  Registration of PCI drivers and handling of hot-pluggable devices.
 */

/**
 * pci_match_one_device - Tell if a PCI device structure has a matching
 *                        PCI device id structure
 * @id: single PCI device id structure to match
 * @dev: the PCI device structure to match against
 * 
 * Returns the matching pci_device_id structure or %NULL if there is no match.
 */

static inline const struct pci_device_id *
pci_match_one_device(const struct pci_device_id *id, const struct pci_dev *dev)
{
	if ((id->vendor == PCI_ANY_ID || id->vendor == dev->vendor) &&
	    (id->device == PCI_ANY_ID || id->device == dev->device) &&
	    (id->subvendor == PCI_ANY_ID || id->subvendor == dev->subsystem_vendor) &&
	    (id->subdevice == PCI_ANY_ID || id->subdevice == dev->subsystem_device) &&
	    !((id->class ^ dev->class) & id->class_mask))
		return id;
	return NULL;
}

/**
 * pci_match_device - Tell if a PCI device structure has a matching
 *                    PCI device id structure
 * @ids: array of PCI device id structures to search in
 * @dev: the PCI device structure to match against
 * 
 * Used by a driver to check whether a PCI device present in the
 * system is in its list of supported devices.Returns the matching
 * pci_device_id structure or %NULL if there is no match.
 */
const struct pci_device_id *
pci_match_device(const struct pci_device_id *ids, const struct pci_dev *dev)
{
	while (ids->vendor || ids->subvendor || ids->class_mask) {
		if (pci_match_one_device(ids, dev))
			return ids;
		ids++;
	}
	return NULL;
}

/**
 * pci_device_probe_static()
 * 
 * returns 0 and sets pci_dev->driver when drv claims pci_dev, else error.
 */
static int
pci_device_probe_static(struct pci_driver *drv, struct pci_dev *pci_dev)
{		   
	int error = -ENODEV;
	const struct pci_device_id *id;

	if (!drv->id_table)
		return error;
	id = pci_match_device(drv->id_table, pci_dev);
	if (id)
		error = drv->probe(pci_dev, id);
	if (error >= 0) {
		pci_dev->driver = drv;
		return 0;
	}
	return error;
}

/**
 * pci_device_probe_dynamic()
 * 
 * Walk the dynamic ID list looking for a match.
 * returns 0 and sets pci_dev->driver when drv claims pci_dev, else error.
 */
static int
pci_device_probe_dynamic(struct pci_driver *drv, struct pci_dev *pci_dev)
{		   
	int error = -ENODEV;
	struct list_head *pos;
	struct dynid *dynid;

	spin_lock(&drv->dynids.lock);
	list_for_each(pos, &drv->dynids.list) {
		dynid = list_entry(pos, struct dynid, node);
		if (pci_match_one_device(&dynid->id, pci_dev)) {
			spin_unlock(&drv->dynids.lock);
			error = drv->probe(pci_dev, &dynid->id);
			if (error >= 0) {
				pci_dev->driver = drv;
				return 0;
			}
			return error;
		}
	}
	spin_unlock(&drv->dynids.lock);
	return error;
}

/**
 * __pci_device_probe()
 * 
 * returns 0  on success, else error.
 * side-effect: pci_dev->driver is set to drv when drv claims pci_dev.
 */
static int
__pci_device_probe(struct pci_driver *drv, struct pci_dev *pci_dev)
{		   
	int error = 0;

	if (!pci_dev->driver && drv->probe) {
		error = pci_device_probe_static(drv, pci_dev);
		if (error >= 0)
			return error;

		error = pci_device_probe_dynamic(drv, pci_dev);
	}
	return error;
}

static int pci_device_probe(struct device * dev)
{
	int error = 0;
	struct pci_driver *drv;
	struct pci_dev *pci_dev;

	drv = to_pci_driver(dev->driver);
	pci_dev = to_pci_dev(dev);
	if (get_device(dev)) {
		error = __pci_device_probe(drv, pci_dev);
		put_device(dev);
	}
	return error;
}

static int pci_device_remove(struct device * dev)
{
	struct pci_dev * pci_dev = to_pci_dev(dev);
	struct pci_driver * drv = pci_dev->driver;

	if (drv) {
		if (drv->remove)
			drv->remove(pci_dev);
		pci_dev->driver = NULL;
	}
	return 0;
}

static int pci_device_suspend(struct device * dev, u32 state, u32 level)
{
	struct pci_dev * pci_dev = to_pci_dev(dev);
	int error = 0;

	if (pci_dev->driver) {
		if (level == SUSPEND_SAVE_STATE && pci_dev->driver->save_state)
			error = pci_dev->driver->save_state(pci_dev,state);
		else if (level == SUSPEND_POWER_DOWN && pci_dev->driver->suspend)
			error = pci_dev->driver->suspend(pci_dev,state);
	}
	return error;
}

static int pci_device_resume(struct device * dev, u32 level)
{
	struct pci_dev * pci_dev = to_pci_dev(dev);

	if (pci_dev->driver) {
		/* We may not call PCI drivers resume at
		   RESUME_POWER_ON because interrupts are not yet
		   working at that point. Calling resume at
		   RESUME_RESTORE_STATE seems like solution. */
		if (level == RESUME_RESTORE_STATE && pci_dev->driver->resume)
			pci_dev->driver->resume(pci_dev);
	}
	return 0;
}

static inline void
dynid_init(struct dynid *dynid)
{
	memset(dynid, 0, sizeof(*dynid));
	INIT_LIST_HEAD(&dynid->node);
}

/**
 * store_new_id
 * @ pdrv
 * @ buf
 * @ count
 *
 * Adds a new dynamic pci device ID to this driver,
 * and causes the driver to probe for all devices again.
 */
static inline ssize_t
store_new_id(struct device_driver * driver, const char * buf, size_t count)
{
	struct dynid *dynid;
	struct bus_type * bus;
	struct pci_driver *pdrv = to_pci_driver(driver);
	__u32 vendor=PCI_ANY_ID, device=PCI_ANY_ID, subvendor=PCI_ANY_ID,
		subdevice=PCI_ANY_ID, class=0, class_mask=0;
	unsigned long driver_data=0;
	int fields=0;

	fields = sscanf(buf, "%x %x %x %x %x %x %lux",
			&vendor, &device, &subvendor, &subdevice,
			&class, &class_mask, &driver_data);
	if (fields < 0)
		return -EINVAL;

	dynid = kmalloc(sizeof(*dynid), GFP_KERNEL);
	if (!dynid)
		return -ENOMEM;
	dynid_init(dynid);

	dynid->id.vendor = vendor;
	dynid->id.device = device;
	dynid->id.subvendor = subvendor;
	dynid->id.subdevice = subdevice;
	dynid->id.class = class;
	dynid->id.class_mask = class_mask;
	dynid->id.driver_data = pdrv->dynids.use_driver_data ?
		driver_data : 0UL;

	spin_lock(&pdrv->dynids.lock);
	list_add_tail(&pdrv->dynids.list, &dynid->node);
	spin_unlock(&pdrv->dynids.lock);

	bus = get_bus(pdrv->driver.bus);
	if (bus) {
		if (get_driver(&pdrv->driver)) {
			down_write(&bus->subsys.rwsem);
			driver_attach(&pdrv->driver);
			up_write(&bus->subsys.rwsem);
			put_driver(&pdrv->driver);
		}
		put_bus(bus);
	}
	
	return count;
}

static DRIVER_ATTR(new_id, S_IWUSR, NULL, store_new_id);

#define kobj_to_pci_driver(obj) container_of(obj, struct device_driver, kobj)
#define attr_to_driver_attribute(obj) container_of(obj, struct driver_attribute, attr)

static ssize_t
pci_driver_attr_show(struct kobject * kobj, struct attribute *attr, char *buf)
{
	struct device_driver *driver = kobj_to_pci_driver(kobj);
	struct driver_attribute *dattr = attr_to_driver_attribute(attr);
	ssize_t ret = 0;

	if (get_driver(driver)) {
		if (dattr->show)
			ret = dattr->show(driver, buf);
		put_driver(driver);
	}
	return ret;
}

static ssize_t
pci_driver_attr_store(struct kobject * kobj, struct attribute *attr,
		      const char *buf, size_t count)
{
	struct device_driver *driver = kobj_to_pci_driver(kobj);
	struct driver_attribute *dattr = attr_to_driver_attribute(attr);
	ssize_t ret = 0;

	if (get_driver(driver)) {
		if (dattr->store)
			ret = dattr->store(driver, buf, count);
		put_driver(driver);
	}
	return ret;
}

static struct sysfs_ops pci_driver_sysfs_ops = {
	.show = pci_driver_attr_show,
	.store = pci_driver_attr_store,
};
static struct kobj_type pci_driver_kobj_type = {
	.sysfs_ops = &pci_driver_sysfs_ops,
};

static int
pci_populate_driver_dir(struct pci_driver * drv)
{
	int error = 0;

	if (drv->probe != NULL)
		error = sysfs_create_file(&drv->driver.kobj,
					  &driver_attr_new_id.attr);
	return error;
}

static inline void
pci_init_dynids(struct pci_dynids *dynids)
{
	memset(dynids, 0, sizeof(*dynids));
	spin_lock_init(&dynids->lock);
	INIT_LIST_HEAD(&dynids->list);
}

static void
pci_free_dynids(struct pci_driver *drv)
{
	struct list_head *pos, *n;
	struct dynid *dynid;

	spin_lock(&drv->dynids.lock);
	list_for_each_safe(pos, n, &drv->dynids.list) {
		dynid = list_entry(pos, struct dynid, node);
		list_del(&dynid->node);
		kfree(dynid);
	}
	spin_unlock(&drv->dynids.lock);
}


/**
 * pci_register_driver - register a new pci driver
 * @drv: the driver structure to register
 * 
 * Adds the driver structure to the list of registered drivers
 * Returns the number of pci devices which were claimed by the driver
 * during registration.  The driver remains registered even if the
 * return value is zero.
 */
int
pci_register_driver(struct pci_driver *drv)
{
	int count = 0;

	/* initialize common driver fields */
	drv->driver.name = drv->name;
	drv->driver.bus = &pci_bus_type;
	drv->driver.probe = pci_device_probe;
	drv->driver.resume = pci_device_resume;
	drv->driver.suspend = pci_device_suspend;
	drv->driver.remove = pci_device_remove;
	drv->driver.kobj.ktype = &pci_driver_kobj_type;
	pci_init_dynids(&drv->dynids);

	/* register with core */
	count = driver_register(&drv->driver);

	if (count >= 0) {
		pci_populate_driver_dir(drv);
	}

	return count ? count : 1;
}

/**
 * pci_unregister_driver - unregister a pci driver
 * @drv: the driver structure to unregister
 * 
 * Deletes the driver structure from the list of registered PCI drivers,
 * gives it a chance to clean up by calling its remove() function for
 * each device it was responsible for, and marks those devices as
 * driverless.
 */

void
pci_unregister_driver(struct pci_driver *drv)
{
	driver_unregister(&drv->driver);
	pci_free_dynids(drv);
}

static struct pci_driver pci_compat_driver = {
	.name = "compat"
};

/**
 * pci_dev_driver - get the pci_driver of a device
 * @dev: the device to query
 *
 * Returns the appropriate pci_driver structure or %NULL if there is no 
 * registered driver for the device.
 */
struct pci_driver *
pci_dev_driver(const struct pci_dev *dev)
{
	if (dev->driver)
		return dev->driver;
	else {
		int i;
		for(i=0; i<=PCI_ROM_RESOURCE; i++)
			if (dev->resource[i].flags & IORESOURCE_BUSY)
				return &pci_compat_driver;
	}
	return NULL;
}

/**
 * pci_bus_match - Tell if a PCI device structure has a matching PCI device id structure
 * @ids: array of PCI device id structures to search in
 * @dev: the PCI device structure to match against
 * 
 * Used by a driver to check whether a PCI device present in the
 * system is in its list of supported devices.Returns the matching
 * pci_device_id structure or %NULL if there is no match.
 */
static int pci_bus_match(struct device * dev, struct device_driver * drv) 
{
	const struct pci_dev * pci_dev = to_pci_dev(dev);
	struct pci_driver * pci_drv = to_pci_driver(drv);
	const struct pci_device_id * ids = pci_drv->id_table;
	const struct pci_device_id *found_id;
	struct list_head *pos;
	struct dynid *dynid;

	if (!ids)
		return 0;

	found_id = pci_match_device(ids, pci_dev);
	if (found_id)
		return 1;

	spin_lock(&pci_drv->dynids.lock);
	list_for_each(pos, &pci_drv->dynids.list) {
		dynid = list_entry(pos, struct dynid, node);
		if (pci_match_one_device(&dynid->id, pci_dev)) {
			spin_unlock(&pci_drv->dynids.lock);
			return 1;
		}
	}
	spin_unlock(&pci_drv->dynids.lock);

	return 0;
}

/**
 * pci_get_dev - increments the reference count of the pci device structure
 * @dev: the device being referenced
 *
 * Each live reference to a device should be refcounted.
 *
 * Drivers for PCI devices should normally record such references in
 * their probe() methods, when they bind to a device, and release
 * them by calling pci_put_dev(), in their disconnect() methods.
 *
 * A pointer to the device with the incremented reference counter is returned.
 */
struct pci_dev *pci_get_dev (struct pci_dev *dev)
{
	struct device *tmp;

	if (!dev)
		return NULL;

	tmp = get_device(&dev->dev);
	if (tmp)        
		return to_pci_dev(tmp);
	else
		return NULL;
}

/**
 * pci_put_dev - release a use of the pci device structure
 * @dev: device that's been disconnected
 *
 * Must be called when a user of a device is finished with it.  When the last
 * user of the device calls this function, the memory of the device is freed.
 */
void pci_put_dev(struct pci_dev *dev)
{
	if (dev)
		put_device(&dev->dev);
}

struct bus_type pci_bus_type = {
	.name		= "pci",
	.match		= pci_bus_match,
	.hotplug	= pci_hotplug,
};

static int __init pci_driver_init(void)
{
	return bus_register(&pci_bus_type);
}

postcore_initcall(pci_driver_init);

EXPORT_SYMBOL(pci_match_device);
EXPORT_SYMBOL(pci_register_driver);
EXPORT_SYMBOL(pci_unregister_driver);
EXPORT_SYMBOL(pci_dev_driver);
EXPORT_SYMBOL(pci_bus_type);
EXPORT_SYMBOL(pci_get_dev);
EXPORT_SYMBOL(pci_put_dev);
