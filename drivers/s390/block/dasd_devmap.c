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
 * $Revision: 1.15 $
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
	struct list_head devindex_list;
	struct list_head devno_list;
        unsigned int devindex;
        unsigned short devno;
        unsigned short features;
	struct dasd_device *device;
};

/*
 * Parameter parsing functions for dasd= parameter. The syntax is:
 *   <devno>		: (0x)?[0-9a-fA-F]+
 *   <feature>		: ro
 *   <feature_list>	: \(<feature>(:<feature>)*\)
 *   <range>		: <devno>(-<devno>)?<feature_list>?
 *   <dasd_module>	: dasd_diag_mod|dasd_eckd_mod|dasd_fba_mod
 *
 *   <dasd>		: autodetect|probeonly|<range>(,<range>)*
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
static struct list_head dasd_devindex_hashlists[256];
static struct list_head dasd_devno_hashlists[256];
int dasd_max_devindex;

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
 * Read a device number from a string. The number is always in hex,
 * a leading 0x is accepted.
 */
static inline int
dasd_devno(char *str, char **endp)
{
	int val;
 
	/* remove leading '0x' */
	if (*str == '0') {
		str++;
		if (*str == 'x')
			str++;
	}
	/* We require at least one hex digit */
	if (!isxdigit(*str))
		return -EINVAL;
	val = simple_strtoul(str, endp, 16);
	if ((val > 0xFFFF) || (val < 0))
		return -EINVAL;
	return val;
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
	features = 0;
	while (1) {
		for (len = 0; 
		     str[len] && str[len] != ':' && str[len] != ')'; len++);
		if (len == 2 && !strncmp(str, "ro", 2))
			features |= DASD_FEATURE_READONLY;
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
	int from, to, features, rc;

	while (1) {
		to = from = dasd_devno(str, &str);
		if (*str == '-') {
			str++;
			to = dasd_devno(str, &str);
		}
		features = dasd_feature_list(str, &str);
		/* Negative numbers in from/to/features indicate errors */
		if (from >= 0 && to >= 0 && features >= 0) {
			rc = dasd_add_range(from, to, features);
			if (rc)
				return rc;
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
 * Add a range of devices and creates the corresponding devreg_t
 * structures. The order of the ranges added through this function
 * will define the kdevs for the individual devices. 
 */
int
dasd_add_range(int from, int to, int features)
{
	int devindex;
	int devno;

	if (from > to) {
		MESSAGE(KERN_ERR,
			"Invalid device range %04x-%04x", from, to);
		return -EINVAL;
	}
	spin_lock(&dasd_devmap_lock);
	for (devno = from; devno <= to; devno++) {
		struct dasd_devmap *devmap, *tmp;

		devmap = NULL;
		/* Find previous devmap for device number i */
		list_for_each_entry(tmp, &dasd_devno_hashlists[devno & 255],
				    devno_list) {
			if (tmp->devno == devno) {
				devmap = tmp;
				break;
			}
		}
		if (devmap == NULL) {
			/* This devno is new. */
			devmap = (struct dasd_devmap *)
				kmalloc(sizeof(struct dasd_devmap),GFP_KERNEL);
			if (devmap == NULL)
				return -ENOMEM;
			devindex = dasd_max_devindex++;
			devmap->devindex = devindex;
			devmap->devno = devno;
			devmap->features = features;
			devmap->device = NULL;
			list_add(&devmap->devindex_list,
				 &dasd_devindex_hashlists[devindex & 255]);
			list_add(&devmap->devno_list,
				 &dasd_devno_hashlists[devno & 255]);
		}
	}
	spin_unlock(&dasd_devmap_lock);
	return 0;
}

/*
 * Check if devno has been added to the list of dasd ranges.
 */
int
dasd_devno_in_range(int devno)
{
	struct dasd_devmap *devmap;
	int ret;
		
	ret = -ENOENT;
	spin_lock(&dasd_devmap_lock);
	/* Find devmap for device with device number devno */
	list_for_each_entry(devmap, &dasd_devno_hashlists[devno&255],
			    devno_list) {
		if (devmap->devno == devno) {
			/* Found the device. */
			ret = 0;
			break;
		}
	}
	spin_unlock(&dasd_devmap_lock);
	return ret;
}

/*
 * Forget all about the device numbers added so far.
 * This may only be called at module unload or system shutdown.
 */
static void
dasd_forget_ranges(void)
{
	int i;

	spin_lock(&dasd_devmap_lock);
	for (i = 0; i < 256; i++) {
		struct list_head *l, *next;
		struct dasd_devmap *devmap;
		list_for_each_safe(l, next, &dasd_devno_hashlists[i]) {
			devmap = list_entry(l, struct dasd_devmap, devno_list);
			if (devmap->device != NULL)
				BUG();
			list_del(&devmap->devindex_list);
			list_del(&devmap->devno_list);
			kfree(devmap);
		}
	}
	spin_unlock(&dasd_devmap_lock);
}

/*
 * Find the devmap structure from a devno. Can be removed as soon
 * as big minors are available.
 */
static struct dasd_devmap *
dasd_devmap_from_devno(int devno)
{
	struct dasd_devmap *devmap, *tmp;
		
	devmap = NULL;
	spin_lock(&dasd_devmap_lock);
	/* Find devmap for device with device number devno */
	list_for_each_entry(tmp, &dasd_devno_hashlists[devno&255], devno_list) {
		if (tmp->devno == devno) {
			/* Found the device, return devmap */
			devmap = tmp;
			break;
		}
	}
	spin_unlock(&dasd_devmap_lock);
	return devmap;
}

/*
 * Find the devmap for a device by its device index. Can be removed
 * as soon as big minors are available.
 */
static struct dasd_devmap *
dasd_devmap_from_devindex(int devindex)
{
	struct dasd_devmap *devmap, *tmp;
		
	devmap = NULL;
	spin_lock(&dasd_devmap_lock);
	/* Find devmap for device with device index devindex */
	list_for_each_entry(tmp, &dasd_devindex_hashlists[devindex & 255],
			    devindex_list) {
		if (tmp->devindex == devindex) {
			/* Found the device, return devno */
			devmap = tmp;
			break;
		}
	}
	spin_unlock(&dasd_devmap_lock);
	return devmap;
}

struct dasd_device *
dasd_device_from_devindex(int devindex)
{
	struct dasd_devmap *devmap;
	struct dasd_device *device;

	devmap = dasd_devmap_from_devindex(devindex);
	spin_lock(&dasd_devmap_lock);
	device = devmap->device;
	if (device)
		dasd_get_device(device);
	else
		device = ERR_PTR(-ENODEV);
	spin_unlock(&dasd_devmap_lock);
	return device;
}

/*
 * Create a dasd device structure for cdev.
 */
struct dasd_device *
dasd_create_device(struct ccw_device *cdev)
{
	struct dasd_devmap *devmap;
	struct dasd_device *device;
	int devno;
	int rc;

	devno = _ccw_device_get_device_number(cdev);
	rc = dasd_add_range(devno, devno, DASD_FEATURE_DEFAULT);
	if (rc)
		return ERR_PTR(rc);

	if (!(devmap = dasd_devmap_from_devno (devno)))
		return ERR_PTR(-ENODEV);

	device = dasd_alloc_device(devmap->devindex);
	if (IS_ERR(device))
		return device;
	atomic_set(&device->ref_count, 1);
	device->ro_flag = (devmap->features & DASD_FEATURE_READONLY) ? 1 : 0;
	device->use_diag_flag = 1;

	spin_lock_irq(get_ccwdev_lock(cdev));
	if (cdev->dev.driver_data == NULL) {
		get_device(&cdev->dev);
		cdev->dev.driver_data = device;
		device->gdp->driverfs_dev = &cdev->dev;
		device->cdev = cdev;
		rc = 0;
	} else
		/* Someone else was faster. */
		rc = -EBUSY;
	spin_unlock_irq(get_ccwdev_lock(cdev));
	if (rc) {
		dasd_free_device(device);
		return ERR_PTR(rc);
	}
	/* Device created successfully. Make it known via devmap. */
	spin_lock(&dasd_devmap_lock);
	devmap->device = device;
	spin_unlock(&dasd_devmap_lock);

	return device;
}

/*
 * Wait queue for dasd_delete_device waits.
 */
static DECLARE_WAIT_QUEUE_HEAD(dasd_delete_wq);

/*
 * Remove a dasd device structure.
 */
void
dasd_delete_device(struct dasd_device *device)
{
	struct ccw_device *cdev;
	struct dasd_devmap *devmap;
	int devno;

	/* First remove device pointer from devmap. */
	devno = _ccw_device_get_device_number(device->cdev);
	devmap = dasd_devmap_from_devno (devno);
	spin_lock(&dasd_devmap_lock);
	devmap->device = NULL;
	spin_unlock(&dasd_devmap_lock);

	/* Wait for reference counter to drop to zero. */
	atomic_dec(&device->ref_count);
	wait_event(dasd_delete_wq, atomic_read(&device->ref_count) == 0);

	/* Disconnect dasd_device structure from ccw_device structure. */
	cdev = device->cdev;
	device->cdev = NULL;
	device->gdp->driverfs_dev = NULL;
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

int
dasd_devmap_init(void)
{
	int i;

	/* Initialize devmap structures. */
	dasd_max_devindex = 0;
	for (i = 0; i < 256; i++) {
		INIT_LIST_HEAD(&dasd_devindex_hashlists[i]);
		INIT_LIST_HEAD(&dasd_devno_hashlists[i]);
	}
	return 0;

}

void
dasd_devmap_exit(void)
{
	dasd_forget_ranges();
}
