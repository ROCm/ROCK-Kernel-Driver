/*
 * linux/drivers/pci/pci-sysfs-dynids.c
 *  Copyright (C) 2003 Dell Computer Corporation
 *  by Matt Domsch <Matt_Domsch@dell.com>
 *
 * sysfs interface for exporting dynamic device IDs
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/stat.h>
#include <linux/sysfs.h>
#include <linux/pci-dynids.h>
#include "pci.h"

/**
 *	dynid_create_file - create sysfs file for a dynamic ID
 *	@pdrv:	pci_driver
 *	@dattr:	the attribute to create
 */
static int dynid_create_file(struct pci_driver * pdrv, struct dynid_attribute * dattr)
{
	int error;

	if (get_driver(&pdrv->driver)) {
		error = sysfs_create_file(&pdrv->dynids.kobj,&dattr->attr);
		put_driver(&pdrv->driver);
	} else
		error = -EINVAL;
	return error;
}

/**
 *	dynid_remove_file - remove sysfs file for a dynamic ID
 *	@drv:	driver.
 *	@id:	the id to to remove
 */
static void dynid_remove_file(struct pci_driver * pdrv, struct dynid_attribute * dattr)
{
	if (get_driver(&pdrv->driver)) {
		sysfs_remove_file(&pdrv->dynids.kobj,&dattr->attr);
		put_driver(&pdrv->driver);
	}
}

#define kobj_to_dynids(obj) container_of(obj,struct pci_dynamic_id_kobj,kobj)
#define dynids_to_pci_driver(obj) container_of(obj,struct pci_driver,dynids)
#define attr_to_dattr(_attr) container_of(_attr, struct dynid_attribute, attr)

static inline ssize_t
default_show_id(const struct pci_device_id * id, char * buf)
{
	return snprintf(buf, PAGE_SIZE, "%08x %08x %08x %08x %08x %08x\n",
			id->vendor,
			id->device,
			id->subvendor,
			id->subdevice,
			id->class,
			id->class_mask);
}

static ssize_t
dynid_show_id(struct pci_driver * pdrv, struct dynid_attribute *dattr, char *buf)
{
	return pdrv->dynids.show_id ?
		pdrv->dynids.show_id(dattr->id, dattr, buf) :
		default_show_id(dattr->id, buf);
}	

static ssize_t
default_show_new_id(char * buf)
{
	char *p = buf;	
	p += sprintf(p,
		     "echo vendor device subvendor subdevice class classmask\n");
	p += sprintf(p,
		     "where each field is a 32-bit value in ABCD (hex) format (no leading 0x).\n");
	p += sprintf(p,
		     "Pass only as many fields as you need to override the defaults below.\n");
	p += sprintf(p,
		     "Default vendor, device, subvendor, and subdevice fields\n");
	p += sprintf(p, "are set to FFFFFFFF (PCI_ANY_ID).\n");
	p += sprintf(p,
		     "Default class and classmask fields are set to 0.\n");
	return p - buf;
}

static inline void
__dattr_init(struct dynid_attribute *dattr)
{
	memset(dattr, 0, sizeof(*dattr));
	INIT_LIST_HEAD(&dattr->node);
	dattr->attr.mode = S_IRUGO;
	dattr->attr.name = dattr->name;
}

static inline ssize_t
default_store_new_id(struct pci_driver * pdrv, const char * buf, size_t count)
{
	struct dynid_attribute *dattr;
	struct pci_device_id *id;
	__u32 vendor=PCI_ANY_ID, device=PCI_ANY_ID, subvendor=PCI_ANY_ID,
		subdevice=PCI_ANY_ID, class=0, class_mask=0;
	int fields=0, error=0;

	fields = sscanf(buf, "%x %x %x %x %x %x",
			&vendor, &device, &subvendor, &subdevice,
			&class, &class_mask);
	if (fields < 0) return -EINVAL;

	dattr = kmalloc(sizeof(*dattr), GFP_KERNEL);
	if (!dattr) return -ENOMEM;
	__dattr_init(dattr);
	
	id = kmalloc(sizeof(*id), GFP_KERNEL);
	if (!id) {
		kfree(dattr);
		return -ENOMEM;
	}
	dattr->id = id;
	dattr->show = dynid_show_id;

	id->vendor = vendor;
	id->device = device;
	id->subvendor = subvendor;
	id->subdevice = subdevice;
	id->class = class;
	id->class_mask = class_mask;

	spin_lock(&pdrv->dynids.lock);
	snprintf(dattr->name, KOBJ_NAME_LEN, "%d", pdrv->dynids.nextname);
	pdrv->dynids.nextname++;
	spin_unlock(&pdrv->dynids.lock);
	
	error = dynid_create_file(pdrv,dattr);
	if (error) {
		kfree(id);
		kfree(dattr);
		return error;
	}

	spin_lock(&pdrv->dynids.lock);
	list_add(&pdrv->dynids.list, &dattr->node);
	spin_unlock(&pdrv->dynids.lock);
	return count;
}

static ssize_t
dynid_show_new_id(struct pci_driver * pdrv, struct dynid_attribute *unused, char * buf)
{
	return pdrv->dynids.show_new_id ?
		pdrv->dynids.show_new_id(pdrv, buf) :
		default_show_new_id(buf);
}

static ssize_t
dynid_store_new_id(struct pci_driver * pdrv, struct dynid_attribute *unused, const char * buf, size_t count)
{
	return pdrv->dynids.store_new_id ?
		pdrv->dynids.store_new_id(pdrv, buf, count) :
		default_store_new_id(pdrv, buf, count);
}

#define DYNID_ATTR(_name,_mode,_show,_store) \
struct dynid_attribute dynid_attr_##_name = { 		\
	.attr = {.name = __stringify(_name), .mode = _mode },	\
        .id   = NULL,                                   \
	.show	= _show,				\
	.store	= _store,				\
}

static DYNID_ATTR(new_id,S_IRUSR|S_IWUSR,dynid_show_new_id,dynid_store_new_id);

static struct attribute * dynids_def_attrs[] = {
	&dynid_attr_new_id.attr,
	NULL,
};

static ssize_t
dynid_show(struct kobject * kobj, struct attribute *attr, char *buf)
{
	struct pci_dynamic_id_kobj *dynid_kobj = kobj_to_dynids(kobj);
	struct pci_driver *pdrv = dynids_to_pci_driver(dynid_kobj);
	struct dynid_attribute *dattr = attr_to_dattr(attr);

	if (dattr->show)
		return dattr->show(pdrv, dattr, buf);
	return -ENOSYS;
}

static ssize_t
dynid_store(struct kobject * kobj, struct attribute *attr, const char *buf, size_t count)
{
	struct pci_dynamic_id_kobj *dynid_kobj = kobj_to_dynids(kobj);
	struct pci_driver *pdrv = dynids_to_pci_driver(dynid_kobj);
	struct dynid_attribute *dattr = attr_to_dattr(attr);
	
	if (dattr->store)
		return dattr->store(pdrv, dattr, buf, count);
	return -ENOSYS;
}

static void
dynids_release(struct kobject *kobj)
{
	struct pci_dynamic_id_kobj *dynids = kobj_to_dynids(kobj);
	struct pci_driver *pdrv = dynids_to_pci_driver(dynids);
	struct list_head *pos, *n;
	struct dynid_attribute *dattr;

	spin_lock(&dynids->lock);
	list_for_each_safe(pos, n, &dynids->list) {
		dattr = list_entry(pos, struct dynid_attribute, node);
		dynid_remove_file(pdrv, dattr);
		list_del(&dattr->node);
		if (dattr->id)
			kfree(dattr->id);
		kfree(dattr);
	}
	spin_unlock(&dynids->lock);
}

static struct sysfs_ops dynids_attr_ops = {
	.show = dynid_show,
	.store = dynid_store,
};
static struct kobj_type dynids_kobj_type = {
	.release = dynids_release,
	.sysfs_ops = &dynids_attr_ops,
	.default_attrs = dynids_def_attrs,
};

/**
 * pci_register_dynids - initialize and register driver dynamic_ids kobject
 * @driver - the device_driver structure
 * @dynids - the dynamic ids structure
 */
int
pci_register_dynids(struct pci_driver *drv)
{
	struct device_driver *driver = &drv->driver;
	struct pci_dynamic_id_kobj *dynids = &drv->dynids;
	if (drv->probe) {
		dynids->kobj.parent = &driver->kobj;
		dynids->kobj.ktype = &dynids_kobj_type;
		snprintf(dynids->kobj.name, KOBJ_NAME_LEN, "dynamic_id");
		return kobject_register(&dynids->kobj);
	}
	return -ENODEV;
}

