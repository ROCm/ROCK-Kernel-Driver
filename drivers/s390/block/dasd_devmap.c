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
 * 05/04/02 split from dasd.c, code restructuring.
 */

#include <linux/config.h>
#include <linux/version.h>
#include <linux/ctype.h>
#include <linux/init.h>

#include <asm/debug.h>
#include <asm/irq.h>
#include <asm/uaccess.h>

/* This is ugly... */
#define PRINTK_HEADER "dasd_devmap:"

#include "dasd_int.h"

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
int
dasd_devno(char *str, char **endp)
{
	/* remove leading '0x' */
	if (*str == '0') {
		str++;
		if (*str == 'x')
			str++;
	}
	/* We require at least one hex digit */
	if (!isxdigit(*str))
		return -EINVAL;
	return simple_strtoul(str, endp, 16);
}

/*
 * Read colon separated list of dasd features. Currently there is
 * only one: "ro" for read-only devices. The default feature set
 * is empty (value 0).
 */
int
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
		dasd_devmap_t *devmap, *tmp;
		struct list_head *l;

		devmap = NULL;
		/* Find previous devmap for device number i */
		list_for_each(l, &dasd_devno_hashlists[devno & 255]) {
			tmp = list_entry(l, dasd_devmap_t, devno_list);
			if (tmp->devno == devno) {
				devmap = tmp;
				break;
			}
		}
		if (devmap == NULL) {
			/* This devno is new. */
			devmap = (dasd_devmap_t *)
				kmalloc(sizeof(dasd_devmap_t), GFP_KERNEL);
			if (devmap == NULL)
				return -ENOMEM;
			devindex = dasd_max_devindex++;
			devmap->devindex = devindex;
			devmap->devno = devno;
			devmap->features = features;
			devmap->devreg = NULL;
			devmap->device = NULL;
			list_add(&devmap->devindex_list,
				 &dasd_devindex_hashlists[devindex & 255]);
			list_add(&devmap->devno_list,
				 &dasd_devno_hashlists[devno & 255]);
		}
		if (devmap->devreg == NULL) {
			/* The devreg is missing. */
			devmap->devreg = (devreg_t *)
				kmalloc(sizeof(devreg_t), GFP_KERNEL);
			if (devmap->devreg == NULL)
				return -ENOMEM;
			memset(devmap->devreg, sizeof(devreg_t), 0);
			devmap->devreg->ci.devno = devno;
			devmap->devreg->flag = DEVREG_TYPE_DEVNO;
			devmap->devreg->oper_func = dasd_oper_handler;
			s390_device_register(devmap->devreg);
		}
	}
	spin_unlock(&dasd_devmap_lock);
	return 0;
}

/*
 * Removes the devreg_t structures for a range of devices. This does
 * NOT remove the range itself. The mapping between devno and kdevs
 * for the devices is remembered until dasd_forget_ranges() is called.
 */
static int
dasd_clear_range(int from, int to)
{
	int devno;

	if (from > to) {
		MESSAGE(KERN_ERR,
			"Invalid device range %04x-%04x", from, to);
		return -EINVAL;
	}
	spin_lock(&dasd_devmap_lock);
	for (devno = from; devno <= to; devno++) {
		struct list_head *l;
		dasd_devmap_t *devmap = NULL;
		/* Find previous devmap for device number i */
		list_for_each(l, &dasd_devno_hashlists[devno & 255]) {
			devmap = list_entry(l, dasd_devmap_t, devno_list);
			if (devmap->devno == devno)
				break;
		}
		if (devmap == NULL)
			continue;
		if (devmap->device != NULL)
			BUG();
		if (devmap->devreg == NULL)
			continue;
		s390_device_unregister(devmap->devreg);
		kfree(devmap->devreg);
		devmap->devreg = NULL;
	}
	spin_unlock(&dasd_devmap_lock);
	return 0;
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
		dasd_devmap_t *devmap;
		list_for_each_safe(l, next, &dasd_devno_hashlists[i]) {
			devmap = list_entry(l, dasd_devmap_t, devno_list);
			if (devmap->device != NULL)
				BUG();
			if (devmap->devreg != NULL) {
				s390_device_unregister(devmap->devreg);
				kfree(devmap->devreg);
				devmap->devreg = NULL;
			}
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
dasd_devmap_t *
dasd_devmap_from_devno(int devno)
{
	struct list_head *l;
	dasd_devmap_t *devmap, *tmp;
		
	devmap = NULL;
	spin_lock(&dasd_devmap_lock);
	/* Find devmap for device with device number devno */
	list_for_each(l, &dasd_devno_hashlists[devno&255]) {
		tmp = list_entry(l, dasd_devmap_t, devno_list);
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
dasd_devmap_t *
dasd_devmap_from_devindex(int devindex)
{
	struct list_head *l;
	dasd_devmap_t *devmap, *tmp;
		
	devmap = NULL;
	spin_lock(&dasd_devmap_lock);
	/* Find devmap for device with device index devindex */
	list_for_each(l, &dasd_devindex_hashlists[devindex & 255]) {
		tmp = list_entry(l, dasd_devmap_t, devindex_list);
		if (tmp->devindex == devindex) {
			/* Found the device, return devno */
			devmap = tmp;
			break;
		}
	}
	spin_unlock(&dasd_devmap_lock);
	return devmap;
}

/*
 * Find the devmap for a device by its irq line.
 */
dasd_devmap_t *
dasd_devmap_from_irq(int irq)
{
	struct list_head *l;
	dasd_devmap_t *devmap, *tmp;
	int i;

	devmap = NULL;
	spin_lock(&dasd_devmap_lock);
	for (i = 0; (i < 256) && (devmap == NULL); i++) {
		list_for_each(l, &dasd_devno_hashlists[i & 255]) {
			tmp = list_entry(l, dasd_devmap_t, devno_list);
			if (tmp->device != NULL &&
			    tmp->device->devinfo.irq == irq) {
				devmap = tmp;
				break;
			}
		}
	}
	spin_unlock(&dasd_devmap_lock);
	return devmap;
}

/*
 * Find the devmap for a device corresponding to a kdev.
 */
dasd_devmap_t *
dasd_devmap_from_kdev(kdev_t kdev)
{
	int devindex;

	/* Find the devindex for kdev. */
	devindex = dasd_gendisk_major_index(major(kdev));
	if (devindex < 0)
		/* No such major -> no devmap */
		return NULL;
	devindex += minor(kdev) >> DASD_PARTN_BITS;

	/* Now find the devmap by the devindex. */
	return dasd_devmap_from_devindex(devindex);
}

/*
 * Find the devmap for a device corresponding to a block_device.
 */
dasd_devmap_t *
dasd_devmap_from_bdev(struct block_device *bdev)
{
	return dasd_devmap_from_kdev(to_kdev_t(bdev->bd_dev));
}

/*
 * Find the device structure for device number devno. If it does not
 * exists yet, allocate it. Increase the reference counter in the device
 * structure and return a pointer to it.
 */
dasd_device_t *
dasd_get_device(dasd_devmap_t *devmap)
{
	dasd_device_t *device;

	spin_lock(&dasd_devmap_lock);
	device = devmap->device;
	if (device != NULL)
		atomic_inc(&device->ref_count);
	spin_unlock(&dasd_devmap_lock);
	if (device != NULL)
		return device;

	device = dasd_alloc_device(devmap);
	if (IS_ERR(device))
		return device;

	spin_lock(&dasd_devmap_lock);
	if (devmap->device != NULL) {
		/* Someone else was faster. */
		dasd_free_device(device);
		device = devmap->device;
	} else
		devmap->device = device;
	atomic_inc(&device->ref_count);
	spin_unlock(&dasd_devmap_lock);
	return device;
}

/*
 * Decrease the reference counter of a devices structure. If the
 * reference counter reaches zero and the device status is
 * DASD_STATE_NEW the device structure is freed. 
 */
void
dasd_put_device(dasd_devmap_t *devmap)
{
	dasd_device_t *device;

	spin_lock(&dasd_devmap_lock);
	device = devmap->device;
	if (atomic_dec_return(&device->ref_count) == 0 &&
	    device->state == DASD_STATE_NEW) {
		devmap->device = NULL;
		dasd_free_device(device);
	}
	spin_unlock(&dasd_devmap_lock);
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

EXPORT_SYMBOL(dasd_devmap_from_devno);
EXPORT_SYMBOL(dasd_devmap_from_devindex);
EXPORT_SYMBOL(dasd_devmap_from_irq);
EXPORT_SYMBOL(dasd_devmap_from_kdev);
EXPORT_SYMBOL(dasd_get_device);
EXPORT_SYMBOL(dasd_put_device);
