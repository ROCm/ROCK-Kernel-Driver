/*
 * File...........: linux/drivers/s390/block/dasd_devmap.c
 * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
 *		    Horst Hummel <Horst.Hummel@de.ibm.com>
 *		    Carsten Otte <Cotte@de.ibm.com>
 *		    Martin Schwidefsky <schwidefsky@de.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * (C) IBM Corporation, IBM Deutschland Entwicklung GmbH, 1999-2001
 *
 * Device mapping and dasd= parameter parsing functions. All devmap
 * functions may not be called from interrupt context. In particular
 * dasd_get_device is a no-no from interrupt context.
 *
 * $Revision: 1.28 $
 */

#include <linux/config.h>
#include <linux/ctype.h>
#include <linux/init.h>

#include <asm/debug.h>
#include <asm/uaccess.h>

/* This is ugly... */
#define PRINTK_HEADER "dasd_devmap:"

#include "dasd_int.h"

/*
 * dasd_devmap_t is used to store the features and the relation
 * between device number and device index. To find a dasd_devmap_t
 * that corresponds to a device number of a device index each
 * dasd_devmap_t is added to two linked lists, one to search by
 * the device number and one to search by the device index. As
 * soon as big minor numbers are available the device index list
 * can be removed since the device number will then be identical
 * to the device index.
 */
struct dasd_devmap {
	struct list_head list;
	char bus_id[BUS_ID_SIZE];
        unsigned int devindex;
        unsigned short features;
	struct dasd_device *device;
};

/*
 * Parameter parsing functions for dasd= parameter. The syntax is:
 *   <devno>		: (0x)?[0-9a-fA-F]+
 *   <busid>		: [0-0a-f]\.[0-9a-f]\.(0x)?[0-9a-fA-F]+
 *   <feature>		: ro
 *   <feature_list>	: \(<feature>(:<feature>)*\)
 *   <devno-range>	: <devno>(-<devno>)?<feature_list>?
 *   <busid-range>	: <busid>(-<busid>)?<feature_list>?
 *   <devices>		: <devno-range>|<busid-range>
 *   <dasd_module>	: dasd_diag_mod|dasd_eckd_mod|dasd_fba_mod
 *
 *   <dasd>		: autodetect|probeonly|<devices>(,<devices>)*
 */

int dasd_probeonly =  0;	/* is true, when probeonly mode is active */
int dasd_autodetect = 0;	/* is true, when autodetection is active */

/*
 * char *dasd[] is intended to hold the ranges supplied by the dasd= statement
 * it is named 'dasd' to directly be filled by insmod with the comma separated
 * strings when running as a module.
 */
static char *dasd[256];

/*
 * Single spinlock to protect devmap structures and lists.
 */
static spinlock_t dasd_devmap_lock = SPIN_LOCK_UNLOCKED;

/*
 * Hash lists for devmap structures.
 */
static struct list_head dasd_hashlists[256];
int dasd_max_devindex;

static struct dasd_devmap *dasd_add_busid(char *, int);

static inline int
dasd_hash_busid(char *bus_id)
{
	int hash, i;

	hash = 0;
	for (i = 0; (i < BUS_ID_SIZE) && *bus_id; i++, bus_id++)
		hash += *bus_id;
	return hash & 0xff;
}

#ifndef MODULE
/*
 * The parameter parsing functions for builtin-drivers are called
 * before kmalloc works. Store the pointers to the parameters strings
 * into dasd[] for later processing.
 */
static int __init
dasd_call_setup(char *str)
{
	static int count = 0;

	if (count < 256)
		dasd[count++] = str;
	return 1;
}

__setup ("dasd=", dasd_call_setup);
#endif	/* #ifndef MODULE */

/*
 * Read a device busid/devno from a string.
 */
static inline int
dasd_busid(char **str, int *id0, int *id1, int *devno)
{
	int val, old_style;
 
	/* check for leading '0x' */
	old_style = 0;
	if ((*str)[0] == '0' && (*str)[1] == 'x') {
		*str += 2;
		old_style = 1;
	}
	if (!isxdigit((*str)[0]))	/* We require at least one hex digit */
		return -EINVAL;
	val = simple_strtoul(*str, str, 16);
	if (old_style || (*str)[0] != '.') {
		*id0 = *id1 = 0;
		if (val < 0 || val > 0xffff)
			return -EINVAL;
		*devno = val;
		return 0;
	}
	/* New style x.y.z busid */
	if (val < 0 || val > 0xff)
		return -EINVAL;
	*id0 = val;
	(*str)++;
	if (!isxdigit((*str)[0]))	/* We require at least one hex digit */
		return -EINVAL;
	val = simple_strtoul(*str, str, 16);
	if (val < 0 || val > 0xff || (*str)++[0] != '.')
		return -EINVAL;
	*id1 = val;
	if (!isxdigit((*str)[0]))	/* We require at least one hex digit */
		return -EINVAL;
	val = simple_strtoul(*str, str, 16);
	if (val < 0 || val > 0xffff)
		return -EINVAL;
	*devno = val;
	return 0;
}

/*
 * Read colon separated list of dasd features. Currently there is
 * only one: "ro" for read-only devices. The default feature set
 * is empty (value 0).
 */
static inline int
dasd_feature_list(char *str, char **endp)
{
	int features, len, rc;

	rc = 0;
	if (*str != '(') {
		*endp = str;
		return DASD_FEATURE_DEFAULT;
	}
	str++;
	features = 0;

	while (1) {
		for (len = 0; 
		     str[len] && str[len] != ':' && str[len] != ')'; len++);
		if (len == 2 && !strncmp(str, "ro", 2))
			features |= DASD_FEATURE_READONLY;
		else if (len == 4 && !strncmp(str, "diag", 4))
			features |= DASD_FEATURE_USEDIAG;
		else {
			MESSAGE(KERN_WARNING,
				"unsupported feature: %*s, "
				"ignoring setting", len, str);
			rc = -EINVAL;
		}
		str += len;
		if (*str != ':')
			break;
		str++;
	}
	if (*str != ')') {
		MESSAGE(KERN_WARNING, "%s",
			"missing ')' in dasd parameter string\n");
		rc = -EINVAL;
	} else
		str++;
	*endp = str;
	if (rc != 0)
		return rc;
	return features;
}

/*
 * Read comma separated list of dasd ranges.
 */
static inline int
dasd_ranges_list(char *str)
{
	struct dasd_devmap *devmap;
	int from, from_id0, from_id1;
	int to, to_id0, to_id1;
	int features, rc;
	char bus_id[BUS_ID_SIZE+1], *orig_str;

	orig_str = str;
	while (1) {
		rc = dasd_busid(&str, &from_id0, &from_id1, &from);
		if (rc == 0) {
			to = from;
			to_id0 = from_id0;
			to_id1 = from_id1;
			if (*str == '-') {
				str++;
				rc = dasd_busid(&str, &to_id0, &to_id1, &to);
			}
		}
		if (rc == 0 &&
		    (from_id0 != to_id0 || from_id1 != to_id1 || from > to))
			rc = -EINVAL;
		if (rc) {
			MESSAGE(KERN_ERR, "Invalid device range %s", orig_str);
			return rc;
		}
		features = dasd_feature_list(str, &str);
		if (features < 0)
			return -EINVAL;
		while (from <= to) {
			sprintf(bus_id, "%01x.%01x.%04x",
				from_id0, from_id1, from++);
			devmap = dasd_add_busid(bus_id, features);
			if (IS_ERR(devmap))
				return PTR_ERR(devmap);
		}
		if (*str != ',')
			break;
		str++;
	}
	if (*str != '\0') {
		MESSAGE(KERN_WARNING,
			"junk at end of dasd parameter string: %s\n", str);
		return -EINVAL;
	}
	return 0;
}

/*
 * Parse a single dasd= parameter.
 */
static int
dasd_parameter(char *str)
{
	if (strcmp ("autodetect", str) == 0) {
		dasd_autodetect = 1;
		MESSAGE (KERN_INFO, "%s",
			 "turning to autodetection mode");
		return 0;
	}
	if (strcmp ("probeonly", str) == 0) {
		dasd_probeonly = 1;
		MESSAGE(KERN_INFO, "%s",
			"turning to probeonly mode");
		return 0;
	}
	/* turn off autodetect mode and scan for dasd ranges */
	dasd_autodetect = 0;
	return dasd_ranges_list(str);
}

/*
 * Parse parameters stored in dasd[] and dasd_disciplines[].
 */
int
dasd_parse(void)
{
	int rc, i;

	rc = 0;
	for (i = 0; i < 256; i++) {
		if (dasd[i] == NULL)
			break;
		rc = dasd_parameter(dasd[i]);
		if (rc) {
			DBF_EVENT(DBF_ALERT, "%s", "invalid range found");
			break;
		}
	}
	return rc;
}

/*
 * Add a devmap for the device specified by busid. It is possible that
 * the devmap already exists (dasd= parameter). The order of the devices
 * added through this function will define the kdevs for the individual
 * devices. 
 */
static struct dasd_devmap *
dasd_add_busid(char *bus_id, int features)
{
	struct dasd_devmap *devmap, *new, *tmp;
	int hash;

	new = (struct dasd_devmap *)
		kmalloc(sizeof(struct dasd_devmap), GFP_KERNEL);
	if (!new)
		return ERR_PTR(-ENOMEM);
	spin_lock(&dasd_devmap_lock);
	devmap = 0;
	hash = dasd_hash_busid(bus_id);
	list_for_each_entry(tmp, &dasd_hashlists[hash], list)
		if (strncmp(tmp->bus_id, bus_id, BUS_ID_SIZE) == 0) {
			devmap = tmp;
			break;
		}
	if (!devmap) {
		/* This bus_id is new. */
		new->devindex = dasd_max_devindex++;
		strncpy(new->bus_id, bus_id, BUS_ID_SIZE);
		new->features = features;
		new->device = 0;
		list_add(&new->list, &dasd_hashlists[hash]);
		devmap = new;
		new = 0;
	}
	spin_unlock(&dasd_devmap_lock);
	if (new)
		kfree(new);
	return devmap;
}

/*
 * Find devmap for device with given bus_id.
 */
static struct dasd_devmap *
dasd_find_busid(char *bus_id)
{
	struct dasd_devmap *devmap, *tmp;
	int hash;

	spin_lock(&dasd_devmap_lock);
	devmap = ERR_PTR(-ENODEV);
	hash = dasd_hash_busid(bus_id);
	list_for_each_entry(tmp, &dasd_hashlists[hash], list) {
		if (strncmp(tmp->bus_id, bus_id, BUS_ID_SIZE) == 0) {
			devmap = tmp;
			break;
		}
	}
	spin_unlock(&dasd_devmap_lock);
	return devmap;
}

/*
 * Check if busid has been added to the list of dasd ranges.
 */
int
dasd_busid_known(char *bus_id)
{
	return IS_ERR(dasd_find_busid(bus_id)) ? -ENOENT : 0;
}

/*
 * Forget all about the device numbers added so far.
 * This may only be called at module unload or system shutdown.
 */
static void
dasd_forget_ranges(void)
{
	struct dasd_devmap *devmap, *n;
	int i;

	spin_lock(&dasd_devmap_lock);
	for (i = 0; i < 256; i++) {
		list_for_each_entry_safe(devmap, n, &dasd_hashlists[i], list) {
			if (devmap->device != NULL)
				BUG();
			list_del(&devmap->list);
			kfree(devmap);
		}
	}
	spin_unlock(&dasd_devmap_lock);
}

/*
 * Find the device struct by its device index.
 */
struct dasd_device *
dasd_device_from_devindex(int devindex)
{
	struct dasd_devmap *devmap, *tmp;
	struct dasd_device *device;
	int i;

	spin_lock(&dasd_devmap_lock);
	devmap = 0;
	for (i = 0; (i < 256) && !devmap; i++)
		list_for_each_entry(tmp, &dasd_hashlists[i], list)
			if (tmp->devindex == devindex) {
				/* Found the devmap for the device. */
				devmap = tmp;
				break;
			}
	if (devmap && devmap->device) {
		device = devmap->device;
		dasd_get_device(device);
	} else
		device = ERR_PTR(-ENODEV);
	spin_unlock(&dasd_devmap_lock);
	return device;
}

/*
 * Return devmap for cdev. If no devmap exists yet, create one and
 * connect it to the cdev.
 */
static struct dasd_devmap *
dasd_devmap_from_cdev(struct ccw_device *cdev)
{
	struct dasd_devmap *devmap;

	if (cdev->dev.driver_data)
		return (struct dasd_devmap *) cdev->dev.driver_data;
	devmap = dasd_find_busid(cdev->dev.bus_id);
	if (!IS_ERR(devmap)) {
		cdev->dev.driver_data = devmap;
		return devmap;
	}
	devmap = dasd_add_busid(cdev->dev.bus_id, DASD_FEATURE_DEFAULT);
	if (!IS_ERR(devmap))
		cdev->dev.driver_data = devmap;
	return devmap;
}

/*
 * Create a dasd device structure for cdev.
 */
struct dasd_device *
dasd_create_device(struct ccw_device *cdev)
{
	struct dasd_devmap *devmap;
	struct dasd_device *device;
	int rc;

	devmap = dasd_devmap_from_cdev(cdev);
	if (IS_ERR(devmap))
		return (void *) devmap;

	device = dasd_alloc_device();
	if (IS_ERR(device))
		return device;
	atomic_set(&device->ref_count, 2);

	spin_lock(&dasd_devmap_lock);
	if (!devmap->device) {
		devmap->device = device;
		device->devindex = devmap->devindex;
		if (devmap->features & DASD_FEATURE_READONLY)
			set_bit(DASD_FLAG_RO, &device->flags);
		else
			clear_bit(DASD_FLAG_RO, &device->flags);
		if (devmap->features & DASD_FEATURE_USEDIAG)
			set_bit(DASD_FLAG_USE_DIAG, &device->flags);
		else
			clear_bit(DASD_FLAG_USE_DIAG, &device->flags);
		get_device(&cdev->dev);
		device->cdev = cdev;
		rc = 0;
	} else
		/* Someone else was faster. */
		rc = -EBUSY;
	spin_unlock(&dasd_devmap_lock);

	if (rc) {
		dasd_free_device(device);
		return ERR_PTR(rc);
	}
	return device;
}

/*
 * Wait queue for dasd_delete_device waits.
 */
static DECLARE_WAIT_QUEUE_HEAD(dasd_delete_wq);

/*
 * Remove a dasd device structure. The passed referenced
 * is destroyed.
 */
void
dasd_delete_device(struct dasd_device *device)
{
	struct ccw_device *cdev;
	struct dasd_devmap *devmap;

	/* First remove device pointer from devmap. */
	devmap = dasd_find_busid(device->cdev->dev.bus_id);
	spin_lock(&dasd_devmap_lock);
	if (devmap->device != device) {
		spin_unlock(&dasd_devmap_lock);
		dasd_put_device(device);
		return;
	}
	devmap->device = NULL;
	spin_unlock(&dasd_devmap_lock);

	/* Drop ref_count by 2, one for the devmap reference and
	 * one for the passed reference. */
	atomic_sub(2, &device->ref_count);

	/* Wait for reference counter to drop to zero. */
	wait_event(dasd_delete_wq, atomic_read(&device->ref_count) == 0);

	/* Disconnect dasd_device structure from ccw_device structure. */
	cdev = device->cdev;
	device->cdev = NULL;

	/* Disconnect dasd_devmap structure from ccw_device structure. */
	cdev->dev.driver_data = NULL;

	/* Put ccw_device structure. */
	put_device(&cdev->dev);

	/* Now the device structure can be freed. */
	dasd_free_device(device);
}

/*
 * Reference counter dropped to zero. Wake up waiter
 * in dasd_delete_device.
 */
void
dasd_put_device_wake(struct dasd_device *device)
{
	wake_up(&dasd_delete_wq);
}

/*
 * Return dasd_device structure associated with cdev.
 */
struct dasd_device *
dasd_device_from_cdev(struct ccw_device *cdev)
{
	struct dasd_devmap *devmap;
	struct dasd_device *device;

	device = ERR_PTR(-ENODEV);
	spin_lock(&dasd_devmap_lock);
	devmap = cdev->dev.driver_data;
	if (devmap && devmap->device) {
		device = devmap->device;
		dasd_get_device(device);
	}
	spin_unlock(&dasd_devmap_lock);
	return device;
}

/*
 * SECTION: files in sysfs
 */

/*
 * readonly controls the readonly status of a dasd
 */
static ssize_t
dasd_ro_show(struct device *dev, char *buf)
{
	struct dasd_devmap *devmap;
	int ro_flag;

	devmap = dev->driver_data;
	if (devmap)
		ro_flag = (devmap->features & DASD_FEATURE_READONLY) != 0;
	else
		ro_flag = (DASD_FEATURE_DEFAULT & DASD_FEATURE_READONLY) != 0;
	return snprintf(buf, PAGE_SIZE, ro_flag ? "1\n" : "0\n");
}

static ssize_t
dasd_ro_store(struct device *dev, const char *buf, size_t count)
{
	struct dasd_devmap *devmap;
	int ro_flag;

	devmap = dasd_devmap_from_cdev(to_ccwdev(dev));
	ro_flag = buf[0] == '1';
	spin_lock(&dasd_devmap_lock);
	if (ro_flag)
		devmap->features |= DASD_FEATURE_READONLY;
	else
		devmap->features &= ~DASD_FEATURE_READONLY;
	if (devmap->device) {
		if (devmap->device->gdp)
			set_disk_ro(devmap->device->gdp, ro_flag);
		if (ro_flag)
			set_bit(DASD_FLAG_RO, &devmap->device->flags);
		else
			clear_bit(DASD_FLAG_RO, &devmap->device->flags);
	}
	spin_unlock(&dasd_devmap_lock);
	return count;
}

static DEVICE_ATTR(readonly, 0644, dasd_ro_show, dasd_ro_store);

/*
 * use_diag controls whether the driver should use diag rather than ssch
 * to talk to the device
 */
/* TODO: Implement */
static ssize_t 
dasd_use_diag_show(struct device *dev, char *buf)
{
	struct dasd_devmap *devmap;
	int use_diag;

	devmap = dev->driver_data;
	if (devmap)
		use_diag = (devmap->features & DASD_FEATURE_USEDIAG) != 0;
	else
		use_diag = (DASD_FEATURE_DEFAULT & DASD_FEATURE_USEDIAG) != 0;
	return sprintf(buf, use_diag ? "1\n" : "0\n");
}

static ssize_t
dasd_use_diag_store(struct device *dev, const char *buf, size_t count)
{
	struct dasd_devmap *devmap;
	int use_diag;

	devmap = dasd_devmap_from_cdev(to_ccwdev(dev));
	use_diag = buf[0] == '1';
	spin_lock(&dasd_devmap_lock);
	/* Changing diag discipline flag is only allowed in offline state. */
	if (!devmap->device) {
		if (use_diag)
			devmap->features |= DASD_FEATURE_USEDIAG;
		else
			devmap->features &= ~DASD_FEATURE_USEDIAG;
	} else
		count = -EPERM;
	spin_unlock(&dasd_devmap_lock);
	return count;
}

static
DEVICE_ATTR(use_diag, 0644, dasd_use_diag_show, dasd_use_diag_store);

static ssize_t
dasd_discipline_show(struct device *dev, char *buf)
{
	struct dasd_devmap *devmap;
	char *dname;

	spin_lock(&dasd_devmap_lock);
	dname = "none";
	devmap = dev->driver_data;
	if (devmap && devmap->device && devmap->device->discipline)
		dname = devmap->device->discipline->name;
	spin_unlock(&dasd_devmap_lock);
	return snprintf(buf, PAGE_SIZE, "%s\n", dname);
}

static DEVICE_ATTR(discipline, 0444, dasd_discipline_show, NULL);

static struct attribute * dasd_attrs[] = {
	&dev_attr_readonly.attr,
	&dev_attr_discipline.attr,
	&dev_attr_use_diag.attr,
	NULL,
};

static struct attribute_group dasd_attr_group = {
	.attrs = dasd_attrs,
};

int
dasd_add_sysfs_files(struct ccw_device *cdev)
{
	return sysfs_create_group(&cdev->dev.kobj, &dasd_attr_group);
}

void
dasd_remove_sysfs_files(struct ccw_device *cdev)
{
	sysfs_remove_group(&cdev->dev.kobj, &dasd_attr_group);
}


int
dasd_devmap_init(void)
{
	int i;

	/* Initialize devmap structures. */
	dasd_max_devindex = 0;
	for (i = 0; i < 256; i++)
		INIT_LIST_HEAD(&dasd_hashlists[i]);
	return 0;

}

void
dasd_devmap_exit(void)
{
	dasd_forget_ranges();
}
