/* 
 * File...........: linux/drivers/s390/block/dasd.c
 * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
 *                  Horst Hummel <Horst.Hummel@de.ibm.com>  
 *                  Carsten Otte <Cotte@de.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * (C) IBM Corporation, IBM Deutschland Entwicklung GmbH, 1999,2000
 *
 * History of changes (starts July 2000)
 * 11/09/00 complete redesign after code review
 * 02/01/01 removed some warnings
 * 02/01/01 added dynamic registration of ioctls
 *          fixed bug in registration of new majors
 *          fixed handling of request during dasd_end_request
 *          fixed handling of plugged queues
 *          fixed partition handling and HDIO_GETGEO
 *          fixed traditional naming scheme for devices beyond 702
 *          fixed some race conditions related to modules
 *          added devfs suupport
 * 03/06/01 refined dynamic attach/detach for leaving devices which are online.
 * 06/09/01 refined dynamic modifiaction of devices
 *          renewed debug feature exploitation
 */

#include <linux/module.h>
#include <linux/config.h>
#include <linux/version.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/tqueue.h>
#include <linux/timer.h>
#include <linux/slab.h>
#include <linux/genhd.h>
#include <linux/hdreg.h>
#include <linux/interrupt.h>
#include <linux/ctype.h>
#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#endif				/* CONFIG_PROC_FS */
#include <linux/spinlock.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/blkpg.h>

#include <asm/ccwcache.h>
#include <asm/dasd.h>
#include <asm/debug.h>

#include <asm/atomic.h>
#include <asm/delay.h>
#include <asm/io.h>
#include <asm/semaphore.h>
#include <asm/ebcdic.h>
#include <asm/uaccess.h>
#include <asm/irq.h>
#include <asm/s390_ext.h>
#include <asm/s390dyn.h>
#include <asm/idals.h>

#ifdef CONFIG_DASD_ECKD
#include "dasd_eckd.h"
#endif				/*  CONFIG_DASD_ECKD */
#ifdef CONFIG_DASD_FBA
#include "dasd_fba.h"
#endif				/*  CONFIG_DASD_FBA */
#ifdef CONFIG_DASD_DIAG
#include "dasd_diag.h"
#endif				/*  CONFIG_DASD_DIAG */

/* SECTION: exported variables of dasd.c */

debug_info_t *dasd_debug_area;

MODULE_AUTHOR ("Holger Smolinski <Holger.Smolinski@de.ibm.com>");
MODULE_DESCRIPTION ("Linux on S/390 DASD device driver,"
		    " Copyright 2000 IBM Corporation");
MODULE_SUPPORTED_DEVICE ("dasd");
MODULE_PARM (dasd, "1-" __MODULE_STRING (256) "s");
EXPORT_SYMBOL (dasd_discipline_enq);
EXPORT_SYMBOL (dasd_discipline_deq);
EXPORT_SYMBOL (dasd_start_IO);
EXPORT_SYMBOL (dasd_int_handler);
EXPORT_SYMBOL (dasd_alloc_request);
EXPORT_SYMBOL (dasd_free_request);
EXPORT_SYMBOL(dasd_ioctl_no_register);  
EXPORT_SYMBOL(dasd_ioctl_no_unregister);

/* SECTION: Constant definitions to be used within this file */

#define PRINTK_HEADER DASD_NAME": "

#define DASD_EMERGENCY_REQUESTS 16
#define DASD_MIN_SIZE_FOR_QUEUE 32
#undef CONFIG_DYNAMIC_QUEUE_MIN_SIZE
#define DASD_CHANQ_MAX_SIZE 6

/* SECTION: prototypes for static functions of dasd.c */

static request_fn_proc do_dasd_request;
static int dasd_set_device_level (unsigned int, int, dasd_discipline_t *, int);
static request_queue_t *dasd_get_queue (kdev_t kdev);
static void cleanup_dasd (void);
int dasd_fillgeo(int kdev,struct hd_geometry *geo);

static struct block_device_operations dasd_device_operations;

/* SECTION: static variables of dasd.c */

static devfs_handle_t dasd_devfs_handle;

/* SECTION: exported variables of dasd.c */

debug_info_t *dasd_debug_area;

#ifdef CONFIG_DASD_DYNAMIC
/* SECTION: managing dynamic configuration of dasd_driver */

static struct list_head dasd_devreg_head = LIST_HEAD_INIT(dasd_devreg_head);

/* 
 * function: dasd_create_devreg
 * creates a dasd_devreg_t related to a devno
 */
static inline dasd_devreg_t *
dasd_create_devreg (int devno)
{
	dasd_devreg_t *r = kmalloc (sizeof (dasd_devreg_t), GFP_KERNEL);
	if (r != NULL) {
		memset (r, 0, sizeof (dasd_devreg_t));
		r->devreg.ci.devno = devno;
		r->devreg.flag = DEVREG_TYPE_DEVNO;
		r->devreg.oper_func = dasd_oper_handler;
	}
	return r;
}

/* 
 * function: dasd_destroy_devreg
 * destroys the dasd_devreg_t given as argument
 */
static inline void
dasd_destroy_devreg (dasd_devreg_t * devreg)
{
	kfree (devreg);
}

#endif				/* CONFIG_DASD_DYNAMIC */

/* SECTION: managing setup of dasd_driver */

/* default setting is probeonly, autodetect */
static int dasd_probeonly = 1;	/* is true, when probeonly mode is active */
static int dasd_autodetect = 1;	/* is true, when autodetection is active */

/* dasd_range_t are used for ordering the DASD devices */
typedef struct dasd_range_t {
	unsigned int from;	/* first DASD in range */
	unsigned int to;	/* last DASD in range */
	char discipline[4];	/* placeholder to force discipline */
	struct dasd_range_t *next;	/* next one in linked list */
} dasd_range_t;

static dasd_range_t *dasd_range_head = NULL;	/* anchor for list of ranges */
static spinlock_t range_lock = SPIN_LOCK_UNLOCKED;
static spinlock_t dasd_open_count_lock;

/* 
 * function: dasd_create_range
 * creates a dasd_range_t according to the arguments 
 * FIXME: no check is performed for reoccurrence of a devno
 */
static inline dasd_range_t *
dasd_create_range (int from, int to)
{
	dasd_range_t *range = NULL;
	range = (dasd_range_t *) kmalloc (sizeof (dasd_range_t), GFP_KERNEL);
	if (range == NULL)
		return NULL;
	memset (range, 0, sizeof (dasd_range_t));
	range->from = from;
	if (to == 0) {		/* single devno ? */
		range->to = from;
	} else {
		range->to = to;
	}
	return range;
}

/* 
 * function dasd_destroy_range
 * destroy a range allocated wit dasd_crate_range
 * CAUTION: must not be callen in arunning sysztem, because it destroys 
 * the mapping of DASDs
 */
static inline void
dasd_destroy_range (dasd_range_t * range)
{
	kfree (range);
}

/* 
 * function: dasd_append_range
 * appends the range given as argument to the list anchored at dasd_range_head. 
 */
static inline void
dasd_append_range (dasd_range_t * range)
{
	dasd_range_t *temp;
	long flags;

	spin_lock_irqsave (&range_lock, flags);
	if (dasd_range_head == NULL) {
		dasd_range_head = range;
	} else {
		for (temp = dasd_range_head;
		     temp && temp->next;
		     temp = temp->next) ;
		temp->next = range;
	}
	spin_unlock_irqrestore (&range_lock, flags);
}

/*
 * function dasd_dechain_range
 * removes a range from the chain of ranges
 * CAUTION: must not be called in a running system because it destroys 
 * the mapping of devices
 */
static inline void
dasd_dechain_range (dasd_range_t * range)
{
	dasd_range_t *temp, *prev = NULL;
	unsigned long flags;

	spin_lock_irqsave (&range_lock, flags);
	for (temp = dasd_range_head; temp != NULL; temp = temp->next) {
		if (temp == range)
			break;
		prev = temp;
	}
	if (!temp)
		BUG ();
	if (prev) {
		prev->next = temp->next;
	} else {
		dasd_range_head = temp->next;
	}
	spin_unlock_irqrestore (&range_lock, flags);
}

/* 
 * function: dasd_add_range
 * creates a dasd_range_t according to the arguments and
 * appends it to the list of ranges
 * additionally a devreg_t is created and added to the list of devregs 
 */
static inline dasd_range_t*
dasd_add_range (int from, int to)
{
	dasd_range_t *range;
	range = dasd_create_range (from, to);
	if (!range) return NULL;

	dasd_append_range (range);
#ifdef CONFIG_DASD_DYNAMIC
	/* allocate and chain devreg infos for the devnos... */
	{
		int i;
		for (i = range->from; i <= range->to; i++) {
			dasd_devreg_t *reg = dasd_create_devreg (i);
			s390_device_register (&reg->devreg);
                        list_add(&reg->list,&dasd_devreg_head);
		}
	}
#endif				/* CONFIG_DASD_DYNAMIC */
	return range;
}

/* 
 * function: dasd_remove_range
 * removes a range and the corresponding devregs from all of the chains
 * CAUTION: must not be called in a running system because it destroys
 * the mapping of devices!
 */
static inline void
dasd_remove_range (dasd_range_t * range)
{
#ifdef CONFIG_DASD_DYNAMIC
	/* deallocate and dechain devreg infos for the devnos... */
	{
		int i;
		for (i = range->from; i <= range->to; i++) {
                        struct list_head *l;
                        dasd_devreg_t *reg = NULL;
			list_for_each (l, &dasd_devreg_head) {
                                reg = list_entry(l,dasd_devreg_t,list);
				if (reg->devreg.flag == DEVREG_TYPE_DEVNO &&
				    reg->devreg.ci.devno == i &&
				    reg->devreg.oper_func == dasd_oper_handler)
					break;
			}
			if (l == &dasd_devreg_head)
				BUG ();
                        list_del(&reg->list);
			s390_device_unregister (&reg->devreg);
			dasd_destroy_devreg (reg);
		}
	}
#endif				/* CONFIG_DASD_DYNAMIC */
	dasd_dechain_range (range);
	dasd_destroy_range (range);
}

/* 
 * function: dasd_devindex_from_devno
 * finds the logical number of the devno supplied as argument in the list
 * of dasd ranges and returns it or ENODEV when not found
 */
static int
dasd_devindex_from_devno (int devno)
{
	dasd_range_t *temp;
	int devindex = 0;
	unsigned long flags;

	spin_lock_irqsave (&range_lock, flags);
	for (temp = dasd_range_head; temp; temp = temp->next) {
		if (devno >= temp->from && devno <= temp->to) {
			spin_unlock_irqrestore (&range_lock, flags);
			return devindex + devno - temp->from;
		}
		devindex += temp->to - temp->from + 1;
	}
	spin_unlock_irqrestore (&range_lock, flags);
	return -ENODEV;
}

/* SECTION: parsing the dasd= parameter of the parmline/insmod cmdline */

/* 
 * char *dasd[] is intended to hold the ranges supplied by the dasd= statement
 * it is named 'dasd' to directly be filled by insmod with the comma separated
 * strings when running as a module.
 * a maximum of 256 ranges can be supplied, as the parmline is limited to
 * <1024 Byte anyway.
 */
char *dasd[256] = {NULL,};

#ifndef MODULE
/* 
 * function: dasd_split_parm_string
 * splits the parmline given to the kernel into comma separated strings
 * which are filled into the 'dasd[]' array, to be parsed later on
 */
static void
dasd_split_parm_string (char *str)
{
	char *tmp = str;
	int count = 0;
	do {
		char *end;
		int len;
		end = strchr (tmp, ',');
		if (end == NULL) {
			len = strlen (tmp) + 1;
		} else {
			len = (long) end - (long) tmp + 1;
			*end = '\0';
			end++;
		}
		dasd[count] = kmalloc (len * sizeof (char), GFP_ATOMIC);
		if (dasd == NULL) {
			printk (KERN_WARNING PRINTK_HEADER
			    "can't store dasd= parameter no %d\n", count + 1);
			break;
		}
		memset (dasd[count], 0, len * sizeof (char));
		memcpy (dasd[count], tmp, len * sizeof (char));
		count++;
		tmp = end;
	} while (tmp != NULL && *tmp != '\0');
}

/* 
 * dasd_parm_string holds a concatenated version of all 'dasd=' parameters
 * supplied in the parmline, which is later to be split by 
 * dasd_split_parm_string
 * FIXME: why first concatenate then split ?
 */
static char dasd_parm_string[1024] __initdata = {0,};

/* 
 * function: dasd_setup
 * is invoked for any single 'dasd=' parameter supplied in the parmline
 * it merges all the arguments into dasd_parm_string
 */
void __init
dasd_setup (char *str, int *ints)
{
	int len = strlen (dasd_parm_string);
	if (len != 0) {
		strcat (dasd_parm_string, ",");
	}
	strcat (dasd_parm_string, str);
}
/* 
 * function: dasd_call_setup 
 * is the 2.4 version of dasd_setup and
 * is invoked for any single 'dasd=' parameter supplied in the parmline
 */
int __init
dasd_call_setup (char *str)
{
        int dummy;
        dasd_setup(str,&dummy);
	return 1;
}

__setup ("dasd=", dasd_call_setup);

#endif /* MODULE */

/* 
 * function: dasd_strtoul
 * provides a wrapper to simple_strtoul to strip leading '0x' and
 * interpret any argument to dasd=[range,...] as hexadecimal
 */
static inline int
dasd_strtoul (char *str, char **stra)
{
	char *temp = str;
	int val;
	if (*temp == '0') {
		temp++;		/* strip leading zero */
		if (*temp == 'x')
			temp++;	/* strip leading x */
	}
	val = simple_strtoul (temp, &temp, 16);		/* interpret anything as hex */
	*stra = temp;
	return val;
}

/* 
 * function: dasd_parse
 * examines the strings given in the string array str and
 * creates and adds the ranges to the apropriate lists
 */
static inline void
dasd_parse (char **str)
{
	char *temp;
	int from, to;

	if (*str) {
		/* turn off probeonly mode, if any dasd parameter is present */
		dasd_probeonly = 0;
		dasd_autodetect = 0;
	}
	while (*str) {
		temp = *str;
		from = 0;
		to = 0;
		if (strcmp ("autodetect", *str) == 0) {
			dasd_autodetect = 1;
			printk (KERN_INFO "turning to autodetection mode\n");
			break;
		} else if (strcmp ("probeonly", *str) == 0) {
			dasd_probeonly = 1;
			printk (KERN_INFO "turning to probeonly mode\n");
			break;
		} else {
			/* turn off autodetect mode, if any range is present */
			dasd_autodetect = 0;
			from = dasd_strtoul (temp, &temp);
			if (*temp == '-') {
				temp++;
				to = dasd_strtoul (temp, &temp);
			}
			dasd_add_range (from, to);
		}
		str++;
	}
}

/* SECTION: Dealing with devices registered to multiple major numbers */

static spinlock_t dasd_major_lock = SPIN_LOCK_UNLOCKED;

static major_info_t dasd_major_info[] =
{
	{
		list: LIST_HEAD_INIT(dasd_major_info[1].list )
	},
	{
		list: LIST_HEAD_INIT(dasd_major_info[0].list ),
		gendisk: {
                        INIT_GENDISK(94,DASD_NAME,DASD_PARTN_BITS,DASD_PER_MAJOR)
		},
		flags : DASD_MAJOR_INFO_IS_STATIC
	}
};

static major_info_t *
get_new_major_info (void)
{
	major_info_t *major_info = NULL;

	major_info = kmalloc (sizeof (major_info_t), GFP_KERNEL);
	if (major_info) {
		static major_info_t temp_major_info =
		{
			gendisk: {
				INIT_GENDISK(0,DASD_NAME,DASD_PARTN_BITS,DASD_PER_MAJOR)
			}
		};
		memcpy (major_info, &temp_major_info, sizeof (major_info_t));
	}
	return major_info;
}

static int
dasd_register_major (major_info_t * major_info)
{
	int rc = 0;
	int major;
	unsigned long flags;

	if (major_info == NULL) {
		major_info = get_new_major_info ();
		if (!major_info) {
			printk (KERN_WARNING PRINTK_HEADER
				"Cannot get memory to allocate another major number\n");
			return -ENOMEM;
		} else {
			printk (KERN_INFO PRINTK_HEADER
				"Created another major number\n");
		}
	}
	major = major_info->gendisk.major;
	major_info->gendisk.de_arr = (devfs_handle_t*)
		kmalloc(DASD_PER_MAJOR * sizeof(devfs_handle_t), GFP_KERNEL);
	memset(major_info->gendisk.de_arr,0,DASD_PER_MAJOR * sizeof(devfs_handle_t));
	major_info->gendisk.flags = (char*)
		kmalloc(DASD_PER_MAJOR * sizeof(char), GFP_KERNEL);
	memset(major_info->gendisk.flags,0,DASD_PER_MAJOR * sizeof(char));

	rc = devfs_register_blkdev (major, DASD_NAME, &dasd_device_operations);
	if (rc < 0) {
		printk (KERN_WARNING PRINTK_HEADER
		      "Cannot register to major no %d, rc = %d\n", major, rc);
		return rc;
	} else {
		major_info->flags |= DASD_MAJOR_INFO_REGISTERED;
	}
        /* Insert the new major info into dasd_major_info if needed */
        if (!(major_info->flags & DASD_MAJOR_INFO_IS_STATIC) ){
                spin_lock_irqsave (&dasd_major_lock, flags);
		list_add_tail(&major_info->list,&dasd_major_info[0].list);
		spin_unlock_irqrestore (&dasd_major_lock, flags);
        }
	if (major == 0) {
		major = rc;
		rc = 0;
	}
	major_info->dasd_device = (dasd_device_t **) kmalloc (DASD_PER_MAJOR * sizeof (dasd_device_t *),
							      GFP_ATOMIC);
	if (!major_info->dasd_device)
		goto out_devfs;
	memset (major_info->dasd_device, 0, DASD_PER_MAJOR * sizeof (dasd_device_t *));
	blk_size[major] =
	    (int *) kmalloc ((1 << MINORBITS) * sizeof (int), GFP_ATOMIC);
	if (!blk_size[major])
		goto out_dasd_device;
	memset (blk_size[major], 0, (1 << MINORBITS) * sizeof (int));
	blksize_size[major] =
	    (int *) kmalloc ((1 << MINORBITS) * sizeof (int), GFP_ATOMIC);
	if (!blksize_size[major])
		goto out_blk_size;
	memset (blksize_size[major], 0, (1 << MINORBITS) * sizeof (int));
	hardsect_size[major] =
	    (int *) kmalloc ((1 << MINORBITS) * sizeof (int), GFP_ATOMIC);
	if (!hardsect_size[major])
		goto out_blksize_size;
	memset (hardsect_size[major], 0, (1 << MINORBITS) * sizeof (int));
	max_sectors[major] =
	    (int *) kmalloc ((1 << MINORBITS) * sizeof (int), GFP_ATOMIC);
	if (!max_sectors[major])
		goto out_hardsect_size;
	memset (max_sectors[major], 0, (1 << MINORBITS) * sizeof (int));

	/* finally do the gendisk stuff */
	major_info->gendisk.part = kmalloc ((1 << MINORBITS) *
					    sizeof (struct hd_struct),
					    GFP_ATOMIC);
	if (!major_info->gendisk.part)
		goto out_max_sectors;
	memset (major_info->gendisk.part, 0, (1 << MINORBITS) *
		sizeof (struct hd_struct));

	INIT_BLK_DEV(major,do_dasd_request,dasd_get_queue,NULL);

	major_info->gendisk.major = major;
	major_info->gendisk.next = gendisk_head;
	major_info->gendisk.sizes = blk_size[major];
	gendisk_head = &major_info->gendisk;
	return major;
out_max_sectors:
	kfree(max_sectors[major]);
out_hardsect_size:
	kfree(hardsect_size[major]);
out_blksize_size:
	kfree(blksize_size[major]);
out_blk_size:
	kfree(blk_size[major]);
out_dasd_device:
	kfree(major_info->dasd_device);
out_devfs:
	devfs_unregister_blkdev(major, DASD_NAME);
	return -ENOMEM;
}

static int
dasd_unregister_major (major_info_t * major_info)
{
	int rc = 0;
	int major;
	struct gendisk *dd, *prev = NULL;
	unsigned long flags;

	if (major_info == NULL) {
		return -EINVAL;
	}
	major = major_info->gendisk.major;
        INIT_BLK_DEV(major,NULL,NULL,NULL);
	blk_size[major] = NULL;
	blksize_size[major] = NULL;
	hardsect_size[major] = NULL;
	max_sectors[major] = NULL;

	/* do the gendisk stuff */
	for (dd = gendisk_head; dd; dd = dd->next) {
		if (dd == &major_info->gendisk) {
			if (prev)
				prev->next = dd->next;
			else
				gendisk_head = dd->next;
			break;
		}
		prev = dd;
	}
	if (dd == NULL) {
		return -ENOENT;
	}
	kfree (major_info->gendisk.de_arr);
	kfree (major_info->gendisk.flags);
	kfree (major_info->dasd_device);
	kfree (blk_size[major]);
	kfree (blksize_size[major]);
	kfree (hardsect_size[major]);
	kfree (max_sectors[major]);
	kfree (major_info->gendisk.part);

	rc = devfs_unregister_blkdev (major, DASD_NAME);
	if (rc < 0) {
		printk (KERN_WARNING PRINTK_HEADER
		  "Cannot unregister from major no %d, rc = %d\n", major, rc);
		return rc;
	} else {
		major_info->flags &= ~DASD_MAJOR_INFO_REGISTERED;
	}
        /* Delete the new major info from dasd_major_info if needed */
        if (!(major_info->flags & DASD_MAJOR_INFO_IS_STATIC)) {
                spin_lock_irqsave (&dasd_major_lock, flags);
		list_del(&major_info->list);
		spin_unlock_irqrestore (&dasd_major_lock, flags);
		kfree (major_info);
        }
	return rc;
}

/* 
 * function: dasd_device_from_kdev
 * finds the device structure corresponding to the kdev supplied as argument
 * in the major_info structures and returns it or NULL when not found
 */
static inline dasd_device_t *
dasd_device_from_kdev (kdev_t kdev)
{
	major_info_t *major_info = NULL;
	struct list_head *l;
	unsigned long flags;

	spin_lock_irqsave (&dasd_major_lock, flags);
	list_for_each(l,&dasd_major_info[0].list) {
		major_info = list_entry(l,major_info_t,list);
		if ( major_info->gendisk.major == MAJOR(kdev) )
			break;
	}
	spin_unlock_irqrestore (&dasd_major_lock, flags);
	if (major_info != &dasd_major_info[0])
		return major_info->dasd_device[MINOR (kdev) >> DASD_PARTN_BITS];
	return NULL;
}

/* 
 * function: dasd_device_from_devno
 * finds the address of the device structure corresponding to the devno 
 * supplied as argument in the major_info structures and returns 
 * it or NULL when not found
 */
static inline dasd_device_t **
dasd_device_from_devno (int devno)
{
	major_info_t *major_info;
	struct list_head *l;
	int devindex = dasd_devindex_from_devno (devno);
	unsigned long flags;

	spin_lock_irqsave (&dasd_major_lock, flags);
	list_for_each(l,&dasd_major_info[0].list) {
		major_info = list_entry(l,major_info_t,list);
		if (devindex < DASD_PER_MAJOR) {
			spin_unlock_irqrestore (&dasd_major_lock, flags);
			return &major_info->dasd_device[devindex];
		}
		devindex -= DASD_PER_MAJOR;
	}
	spin_unlock_irqrestore (&dasd_major_lock, flags);
	return NULL;
}

/* SECTION: managing dasd disciplines */

/* anchor and spinlock for list of disciplines */
static dasd_discipline_t *dasd_disciplines;
static spinlock_t discipline_lock = SPIN_LOCK_UNLOCKED;

/* 
 * function dasd_discipline_enq 
 * chains the discpline given as argument to the head of disiplines
 * head chaining policy is required to allow module disciplines to
 * be preferred against those, who are statically linked
 */
void
dasd_discipline_enq (dasd_discipline_t * d)
{
	spin_lock (&discipline_lock);
	d->next = dasd_disciplines;
	dasd_disciplines = d;
	spin_unlock (&discipline_lock);
}

/* 
 * function dasd_discipline_deq 
 * removes the discipline given as argument from the list of disciplines
 */
int
dasd_discipline_deq (dasd_discipline_t * d)
{
	int rc = 0;
	spin_lock (&discipline_lock);
	if (dasd_disciplines == d) {
		dasd_disciplines = dasd_disciplines->next;
	} else {
		dasd_discipline_t *b;
		b = dasd_disciplines;
		while (b && b->next != d)
			b = b->next;
		if (b != NULL) {
			b->next = b->next->next;
		} else {
			rc = -ENOENT;
		}
	}
	spin_unlock (&discipline_lock);
	return rc;
}

static inline dasd_discipline_t *
dasd_find_discipline (dasd_device_t * device)
{
	dasd_discipline_t *temp;
	for (temp = dasd_disciplines; temp != NULL; temp = temp->next) {
		if (temp->id_check)
			if (temp->id_check (&device->devinfo)) {
				continue;
                        }
		if (temp->check_characteristics) {
			if (temp->check_characteristics (device)) {
				continue;
                        }
		}
		break;
	}
	return temp;
}

/* SECTION: profiling stuff */

static dasd_profile_info_t dasd_global_profile;

/*
 * macro: dasd_profile_add_counter
 * increments counter in global and local profiling structures
 * according to the value
 */
#define dasd_profile_add_counter( value, counter, device ) \
{ \
        int ind; \
        long help; \
	for (ind = 0, help = value >> 3; \
             ind < 31 && help; \
             help = help >> 1, ind++) {} \
	dasd_global_profile.counter[ind]++; \
        device->profile.counter[ind]++; \
}

/*
 * function dasd_profile_add 
 * adds the profiling information from the cqr given as argument to the
 * global and device specific profiling information 
 */
void
dasd_profile_add (ccw_req_t * cqr)
{
	long strtime, irqtime, endtime, tottime;
	long tottimeps, sectors;
	dasd_device_t *device = cqr->device;

	if (!cqr->req)		/* safeguard against abnormal cqrs */
		return;
	sectors = ((struct request *) (cqr->req))->nr_sectors;
	strtime = ((cqr->startclk - cqr->buildclk) >> 12);
	irqtime = ((cqr->stopclk - cqr->startclk) >> 12);
	endtime = ((cqr->endclk - cqr->stopclk) >> 12);
	tottime = ((cqr->endclk - cqr->buildclk) >> 12);
	tottimeps = tottime / sectors;

	if (!dasd_global_profile.dasd_io_reqs) {
		memset (&dasd_global_profile, 0, sizeof (dasd_profile_info_t));
	};
	if (!device->profile.dasd_io_reqs) {
		memset (&device->profile, 0, sizeof (dasd_profile_info_t));
	};

        dasd_global_profile.dasd_io_reqs++;
        device->profile.dasd_io_reqs++;
	dasd_profile_add_counter (sectors, dasd_io_secs, device);
	dasd_profile_add_counter (tottime, dasd_io_times, device);
	dasd_profile_add_counter (tottimeps, dasd_io_timps, device);
	dasd_profile_add_counter (strtime, dasd_io_time1, device);
	dasd_profile_add_counter (irqtime, dasd_io_time2, device);
	dasd_profile_add_counter (irqtime / sectors, dasd_io_time2ps, device);
	dasd_profile_add_counter (endtime, dasd_io_time3, device);
}

/* SECTION: (de)queueing of requests to channel program queues */

/* 
 * function dasd_chanq_enq 
 * appends the cqr given as argument to the queue
 * has to be called with the queue lock (namely the s390_irq_lock) acquired
 */
static inline void
dasd_chanq_enq (dasd_chanq_t * q, ccw_req_t * cqr)
{
	if (q->head != NULL) {
		q->tail->next = cqr;
	} else
		q->head = cqr;
	cqr->next = NULL;
	q->tail = cqr;
	check_then_set (&cqr->status, CQR_STATUS_FILLED, CQR_STATUS_QUEUED);
}

/* 
 * function dasd_chanq_enq_head
 * chains the cqr given as argument to the queue head
 * has to be called with the queue lock (namely the s390_irq_lock) acquired
 */
static inline void
dasd_chanq_enq_head (dasd_chanq_t * q, ccw_req_t * cqr)
{
	cqr->next = q->head;
	q->head = cqr;
	if (q->tail == NULL)
		q->tail = cqr;
	check_then_set (&cqr->status,
		       CQR_STATUS_FILLED,
		       CQR_STATUS_QUEUED);
}

/* 
 * function dasd_chanq_deq
 * dechains the cqr given as argument from the queue
 * has to be called with the queue lock (namely the s390_irq_lock) acquired
 */
int
dasd_chanq_deq (dasd_chanq_t * q, ccw_req_t * cqr)
{
	ccw_req_t *prev;

	if (cqr == NULL)
                BUG ();

	if (cqr == q->head) {
		q->head = cqr->next;
		if (q->head == NULL)
			q->tail = NULL;

	} else {
		prev = q->head;
		while (prev && prev->next != cqr)
			prev = prev->next;
		if (prev == NULL)
			return -ENOENT;
		prev->next = cqr->next;
		if (prev->next == NULL)
			q->tail = prev;
	}
	cqr->next = NULL;
	return 0;
}

/* SECTION: All the gendisk stuff */

/* 
 * function dasd_partn_detect
 * calls the function in genhd, which is appropriate to setup a partitioned disk
 */
static void
dasd_partn_detect (dasd_device_t * dev)
{
	major_info_t *major_info = dev->major_info;
	struct gendisk *dd = &major_info->gendisk;
	int minor = MINOR (dev->kdev);

	register_disk (dd,
		       MKDEV (dd->major, minor),
		       1 << DASD_PARTN_BITS,
		       &dasd_device_operations,
		       (dev->sizes.blocks << dev->sizes.s2b_shift));
}

/* SECTION: Managing wrappers for ccwcache */

/* array and spinlock of emergency requests */
static ccw_req_t *dasd_emergency_req[DASD_EMERGENCY_REQUESTS];
static spinlock_t dasd_emergency_req_lock = SPIN_LOCK_UNLOCKED;

/* 
 * function dasd_init_emergeny_req
 * allocates emergeny requests
 */
static inline void __init
dasd_init_emergency_req (void)
{
	int i;
	for (i = 0; i < DASD_EMERGENCY_REQUESTS; i++) {
		dasd_emergency_req[i] = (ccw_req_t *) get_free_page (GFP_KERNEL);
		memset (dasd_emergency_req[i], 0, PAGE_SIZE);
	}
}

/* 
 * function dasd_cleanup_emergeny_req
 * tries to free emergeny requests skipping those, which are currently in use
 */
static inline void
dasd_cleanup_emergency_req (void)
{
	int i;
	for (i = 0; i < DASD_EMERGENCY_REQUESTS; i++) {
		if (dasd_emergency_req[i])
			free_page ((long) (dasd_emergency_req[i]));
		else
			printk (KERN_WARNING PRINTK_HEADER
				"losing page for emergency request in use\n");
	}
}

/* 
 * function dasd_alloc_request
 * tries to return space for a channel program of length cplength with
 * additional data of size datasize. 
 * If the ccwcache cannot fulfill the request it tries the emergeny requests
 * before giving up finally
 * FIXME: initialization of ccw_req_t should be done by function of ccwcache
 */
ccw_req_t *
dasd_alloc_request (char *magic, int cplength, int datasize)
{
	ccw_req_t *rv = NULL;
	int i;
	unsigned long flags;

	if ((rv = ccw_alloc_request (magic, cplength, datasize)) != NULL) {
		return rv;
	}
	if ((((sizeof (ccw_req_t) + 7) & -8) + 
             cplength*sizeof(ccw1_t) + datasize) > PAGE_SIZE) {
            BUG();
        }
	spin_lock_irqsave (&dasd_emergency_req_lock, flags);
	for (i = 0; i < DASD_EMERGENCY_REQUESTS; i++) {
		if (dasd_emergency_req[i] != NULL) {
			rv = dasd_emergency_req[i];
			dasd_emergency_req[i] = NULL;
			break;
		}
	}
	spin_unlock_irqrestore (&dasd_emergency_req_lock, flags);
	if (rv) {
		memset (rv, 0, PAGE_SIZE);
		rv->cache = (kmem_cache_t *) (dasd_emergency_req + i);
		strncpy ((char *) (&rv->magic), magic, 4);
		ASCEBC ((char *) (&rv->magic), 4);
		rv->cplength = cplength;
		rv->datasize = datasize;
		rv->data = (void *) ((long) rv + PAGE_SIZE - datasize);
		rv->cpaddr = (ccw1_t *) ((long) rv + sizeof (ccw_req_t));
	} else {
		panic ("No way to fulfill request for I/O request\n");
	}
	return rv;
}

/* 
 * function dasd_free_request
 * returns a ccw_req_t to the appropriate cache or emergeny request line
 */
void
dasd_free_request (ccw_req_t * request)
{
	if ((request->cache >= (kmem_cache_t *) dasd_emergency_req) &&
	    (request->cache < (kmem_cache_t *) (dasd_emergency_req +
						DASD_EMERGENCY_REQUESTS))) {
		*((ccw_req_t **) (request->cache)) = request;
	} else {
		ccw_free_request (request);
	}
}			

/* SECTION: Managing the device queues etc. */


/* 
 * function dasd_start_IO
 * attempts to start the IO and returns an appropriate return code
 */
int
dasd_start_IO (ccw_req_t * cqr)
{
	int rc = 0;
	dasd_device_t *device = cqr->device;
	int irq;
        unsigned long long now;

	if (!cqr) {
		BUG ();
	}
	irq = device->devinfo.irq;
	if (strncmp ((char *) &cqr->magic, device->discipline->ebcname, 4)) {
		DASD_MESSAGE (KERN_WARNING, device,
			      " ccw_req_t 0x%08X magic doesn't match"
			      " discipline 0x%08X\n",
			      cqr->magic,
			      *(unsigned int *) device->discipline->name);
		return -EINVAL;
	}

        asm volatile ("STCK %0":"=m" (now));
        rc = do_IO (irq, cqr->cpaddr, (long) cqr, cqr->lpm, cqr->options);
        switch (rc) {
        case 0:
            break;
        case -ENODEV:
                check_then_set (&cqr->status, 
                               CQR_STATUS_QUEUED, CQR_STATUS_FAILED);
            break;
        case -EIO:
                check_then_set (&cqr->status, 
                               CQR_STATUS_QUEUED, CQR_STATUS_FAILED);
            break;
        case -EBUSY:
                DASD_MESSAGE (KERN_WARNING, device,"%s",
                              "device busy, retry later\n");
                break;
        default:
                DASD_MESSAGE (KERN_ERR, device,
                              "line %d unknown RC=%d, please report"
                              " to linux390@de.ibm.com\n",
                              __LINE__, rc);
                BUG();
            break;
        }
	if (rc == 0) {
                check_then_set (&cqr->status, 
                               CQR_STATUS_QUEUED, CQR_STATUS_IN_IO);
                cqr->startclk = now;
	}
	return rc;
}

/* 
 * function sleep_on_req
 * attempts to start the IO and waits for completion
 * FIXME: replace handmade sleeping by wait_event
 */
static int
sleep_on_req (ccw_req_t * req)
{
	unsigned long flags;
	int cs;
	int rc = 0;
	dasd_device_t *device = (dasd_device_t *) req->device;

	s390irq_spin_lock_irqsave (device->devinfo.irq, flags);
	dasd_chanq_enq (&device->queue, req);
        /* let the bh start the request to keep them in order */
	dasd_schedule_bh (device); 
        
	do {
		s390irq_spin_unlock_irqrestore (device->devinfo.irq, flags);
                wait_event (device->wait_q,
                            (((cs=req->status)==CQR_STATUS_DONE)||
                            (cs==CQR_STATUS_FAILED)));
		s390irq_spin_lock_irqsave (device->devinfo.irq, flags);
		cs = req->status;
	} while (cs != CQR_STATUS_DONE && cs != CQR_STATUS_FAILED);

	s390irq_spin_unlock_irqrestore (device->devinfo.irq, flags);
	if (cs == CQR_STATUS_FAILED) {
		rc = -EIO;
	}
	return rc;

}				/* end sleep_on_req */

/* 
 * function dasd_end_request
 * posts the buffer_cache about a finalized request
 * FIXME: for requests splitted to serveral cqrs
 */
static inline void
dasd_end_request (struct request *req, int uptodate)
{
	while (end_that_request_first (req, uptodate, DASD_NAME)) {}
#ifndef DEVICE_NO_RANDOM
	add_blkdev_randomness (MAJOR (req->rq_dev));
#endif
	end_that_request_last (req);
	return;
}

/* 
 * function dasd_get_queue
 * returns the queue corresponding to a device behind a kdev
 */
static request_queue_t *
dasd_get_queue (kdev_t kdev)
{
	dasd_device_t *device = dasd_device_from_kdev (kdev);
	return &device->request_queue;
}

/* 
 * function dasd_check_expire_time
 * check the request given as argument for expiration
 * and returns 0 if not yet expired, nonzero else
 */
static inline int
dasd_check_expire_time (ccw_req_t * cqr)
{
	unsigned long long now;
	int rc = 0;

	asm volatile ("STCK %0":"=m" (now));
	if ( cqr->expires && 
             cqr->expires + cqr->startclk <  now) {
		DASD_MESSAGE (KERN_ERR, ((dasd_device_t*)cqr->device),
			      "IO timeout 0x%08lx%08lx usecs in req %p\n",
			      (long) (cqr->expires >> 44), 
                              (long) (cqr->expires >> 12), cqr);
		cqr->expires <<=1;
	}
	return rc;
}

/* 
 * function dasd_finalize_request
 * implemets the actions to perform, when a request is finally finished
 * namely in status CQR_STATUS_DONE || CQR_STATUS_FAILED
 */
static inline void
dasd_finalize_request (ccw_req_t * cqr)
{
        dasd_device_t *device = cqr->device;
        
	asm volatile ("STCK %0":"=m" (cqr->endclk));
	if (cqr->req) {
                dasd_end_request (cqr->req, (cqr->status == CQR_STATUS_DONE));
                dasd_profile_add (cqr);
		/* free request if nobody is waiting on it */
		dasd_free_request (cqr);
	} else {
		/* during format we don't have the request structure */
		/* notify sleeping task about finished postprocessing */
		wake_up (&device->wait_q);
	}
	return;
}

/* 
 * function dasd_process_queues
 * transfers the requests on the queue given as argument to the chanq
 * if possible, the request ist started on a fastpath
 */
static void
dasd_process_queues (dasd_device_t *device)
{
        unsigned long flags;
        struct request *req;
        request_queue_t * queue = &device->request_queue;
	dasd_chanq_t *qp = &device->queue;
        int irq = device -> devinfo.irq;
        ccw_req_t *final_requests= NULL;
        static int chanq_min_size = DASD_MIN_SIZE_FOR_QUEUE;
        int chanq_max_size = DASD_CHANQ_MAX_SIZE;
        ccw_req_t * cqr=NULL,*temp;
        dasd_erp_postaction_fn_t erp_postaction;

        s390irq_spin_lock_irqsave (irq, flags);
        /* First we dechain the requests, processed with completed status */
        while ( qp -> head && 
                ((qp -> head -> status == CQR_STATUS_DONE) || 
                 (qp -> head -> status == CQR_STATUS_FAILED) ||
		 (qp -> head -> status == CQR_STATUS_ERROR) ) ) {
		dasd_erp_action_fn_t erp_action;
		ccw_req_t *erp_cqr = NULL;
		/*  preprocess requests with CQR_STATUS_ERROR */
		if (qp -> head -> status == CQR_STATUS_ERROR) {
			if ((qp -> head -> dstat -> flag & DEVSTAT_HALT_FUNCTION) ||
			    (qp->head->retries-- == 0 ) ||
			    (device->discipline->erp_action==NULL) ||
			    ((erp_action=device->discipline->erp_action(qp->head))==NULL)||
			    ((erp_cqr = erp_action(qp->head))== NULL)) {
				check_then_set (&qp->head->status,
                                                CQR_STATUS_ERROR,
                                                CQR_STATUS_FAILED);
                                continue;
			} else {
                                if (erp_cqr != qp->head){
                                        dasd_chanq_enq_head (qp, erp_cqr);
                                }
				/* chain of completed requests is now broken */
				continue; 
			}
		} else if ( qp -> head -> refers ) { /* we deal with an ERP */
                        char *uptodatestr;
                        if ( qp -> head -> status == CQR_STATUS_DONE) {
                                uptodatestr = "ERP successful";
                        } else {
                                uptodatestr = "ERP unsuccessful";
                        }
                        
                        if (device->discipline->erp_postaction == NULL ||
                            ((erp_postaction = device->discipline->erp_postaction (qp->head)) == NULL)) {
                                /* 
                                                         * maybe we shoud set it to FAILED, 
                                                         * because we are very paranoid ;) 
                                                         */
                                erp_postaction = default_erp_postaction;
                        }
                        DASD_MESSAGE (KERN_INFO, device,
                                      "%s: postaction [<%p>]\n",
                                      uptodatestr, erp_postaction);
                        erp_postaction (qp->head);
                        continue;
                }
                
		/* dechain request now */
                if ( final_requests == NULL )
                        final_requests = qp -> head;
                cqr = qp -> head;
                qp -> head = qp -> head -> next;
		if (qp->head == NULL)
			qp->tail = NULL;
        }
        if ( cqr )
            cqr -> next = NULL;
        /* Now we try to fetch requests from the request queue */
        for (temp = cqr; temp != NULL ;temp=temp-> next )
                if ( temp ->status == CQR_STATUS_QUEUED)
                        chanq_max_size --;
	while ( (! queue->plugged) &&
		(! list_empty(&queue->queue_head)) && 
                (req=dasd_next_request(queue)) != NULL) {
                /* queue empty or certain critera fulfilled -> transfer */
                if ( qp -> head == NULL ||
                     chanq_max_size > 0 ||
                     (req->nr_sectors >= chanq_min_size)) {
                        ccw_req_t *cqr;
                        /* relocate request according to partition table */
                        req->sector += device->major_info->gendisk.part[MINOR (req->rq_dev)].start_sect;
                        cqr = device->discipline->build_cp_from_req (device, req);
                        if (cqr == NULL) {
                                DASD_MESSAGE (KERN_WARNING, device,
                                              "CCW creation failed on request %p\n", req);
				/* revert relocation of request */
                                req->sector -= device->major_info->gendisk.part[MINOR (req->rq_dev)].start_sect;
                                break; /* terminate request queue loop */
                                
                        } 
#ifdef CONFIG_DYNAMIC_QUEUE_MIN_SIZE
                        chanq_min_size = (chanq_min_size + req->nr_sectors)>>1; 
#endif /* CONFIG_DYNAMIC_QUEUE_MIN_SIZE */
                        dasd_dequeue_request(queue,req);
			dasd_chanq_enq (qp, cqr);
                } else { /* queue not empty OR criteria not met */
                        break; /* terminate request queue loop */
                }
        }
        /* we process the requests with non-final status */
        if ( qp -> head ) {
                switch ( qp->head->status ) {
                case CQR_STATUS_QUEUED:
                        /* try to start the first I/O that can be started */
                        if ( device->discipline->start_IO (qp->head) != 0) 
                                BUG();
                        break;
                case CQR_STATUS_IN_IO:
                        /* Check, if to invoke the missing interrupt handler */
                        if ( dasd_check_expire_time (qp->head) ) {
                                /* to be filled with MIH */
                        }
                        break;

                case CQR_STATUS_PENDING:
                        /* just wait */
                        break;
                default: 
                        BUG();
                }
        }
        /* Now clean the requests with final status */
        while ( final_requests ) {
                cqr = final_requests;
                final_requests = cqr-> next;
                dasd_finalize_request( cqr );
        }
        s390irq_spin_unlock_irqrestore (irq, flags);
}

/* 
 * function dasd_run_bh
 * acquires the locks needed and then runs the bh
 */
static void
dasd_run_bh (dasd_device_t *device)
{
	long flags;
	spin_lock_irqsave (&io_request_lock, flags);
	atomic_set(&device->bh_scheduled,0);
	dasd_process_queues (device);
	spin_unlock_irqrestore (&io_request_lock, flags);
}

/* 
 * function dasd_schedule_bh
 * schedules the request_fn to run with next run_bh cycle
 */
void
dasd_schedule_bh (dasd_device_t *device)
{
	/* Protect against rescheduling, when already running */
        if (atomic_compare_and_swap(0,1,&device->bh_scheduled)) {
                return;
        }

	INIT_LIST_HEAD(&device->bh_tq.list);
	device->bh_tq.sync = 0;
	device->bh_tq.routine = (void *) (void *) dasd_run_bh;
	device->bh_tq.data = device;

	queue_task (&device->bh_tq, &tq_immediate);
	mark_bh (IMMEDIATE_BH);
	return;
}

/* 
 * function do_dasd_request
 * is called from ll_rw_blk.c and provides the caller of 
 * dasd_process_queues
 */
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
static void
do_dasd_request (request_queue_t * queue)
{
        dasd_device_t *device = (dasd_device_t *)
                ((long)queue-(long)offsetof (dasd_device_t, request_queue));
	dasd_process_queues (device);
}
#else
static void
do_dasd_request (void)
{
	major_info_t *major_info;
	dasd_device_t *device;
	int i;

	for (major_info = dasd_major_info;
	     major_info != NULL;
	     major_info = major_info->next) {
		for (i = 0; i < DASD_PER_MAJOR; i++) {
			device = major_info->dasd_device[i];
			if (!device)
				continue;	/* remove indentation level */
			dasd_process_queues (device);
		}
	}
}
#endif				/* LINUX_IS_24 */

/*
 * DASD_HANDLE_STATE_CHANGE_PENDING 
 *
 * DESCRIPTION
 *   Handles the state change pending interrupt.
 *   Search for the device related request queue and check if the first 
 *   cqr in queue in in status 'CQR_STATUE_PENDING'.
 *   If so the status is set to 'CQR_STATUS_QUEUED' to reactivate
 *   the device.
 *
 *  PARAMETER
 *   stat               device status of state change pending interrupt.
 */
void 
dasd_handle_state_change_pending (devstat_t *stat)
{
        dasd_device_t **device_addr;
        ccw_req_t     *cqr;

	device_addr = dasd_device_from_devno (stat->devno);

        if (device_addr == NULL) {
		printk (KERN_INFO PRINTK_HEADER
                        "unable to find device for state change pending "
                        "interrupt: devno%04X\n",
                        stat->devno);
        } else {
                /* re-activate first request in queue */
                cqr = (*device_addr)->queue.head;

                if (cqr->status == CQR_STATUS_PENDING) {

                        DASD_MESSAGE (KERN_INFO, (*device_addr),
                                      "%s",
                                      "device request queue restarted by "
                                      "state change pending interrupt\n");

                        del_timer(&(*device_addr)->timer);

                        check_then_set(&cqr->status, 
                                       CQR_STATUS_PENDING, 
                                       CQR_STATUS_QUEUED);

                        dasd_schedule_bh(*device_addr);

                }
        }
} /* end dasd_handle_state_change_pending */

/* 
 * function dasd_int_handler
 * is the DASD driver's default interrupt handler for SSCH-IO
 */
void
dasd_int_handler (int irq, void *ds, struct pt_regs *regs)
{
	int ip;
        int devno;
	ccw_req_t *cqr;
	dasd_device_t *device;
        unsigned long long now;
#ifdef ERP_DEBUG
	static int counter = 0;
#endif
	dasd_era_t era = dasd_era_none; /* default is everything is okay */
	devstat_t *stat = (devstat_t *)ds;

        asm volatile ("STCK %0":"=m" (now));
        if (stat == NULL) {
                BUG();
	}

        /* first of all check for state change pending interrupt */
        if (stat->dstat & (DEV_STAT_ATTENTION | 
                           DEV_STAT_DEV_END   |
                           DEV_STAT_UNIT_EXCEP )) {

                dasd_handle_state_change_pending (stat);
                //return; /* TBD */
        }

	ip = stat->intparm;
	if (!ip) {		/* no intparm: unsolicited interrupt */
		printk (KERN_INFO PRINTK_HEADER
                        "unsolicited interrupt: irq0x%x devno%04X\n",
                        irq,stat->devno);
		return;
	}
	if (ip & 0x80000001) {
		printk (KERN_INFO PRINTK_HEADER
                        "spurious interrupt: irq0x%x devno%04X, parm %08x\n",
                        irq,stat->devno,ip);
		return;
	}
	cqr = (ccw_req_t *)(long)ip;
	device = (dasd_device_t *) cqr->device;
	if (device == NULL || device != ds-offsetof(dasd_device_t,dev_status)) {
                BUG();
	}
	devno = device->devinfo.devno;
	if (device->devinfo.irq != irq) {
                BUG();
	}
	if (strncmp (device->discipline->ebcname, (char *) &cqr->magic, 4)) {
                BUG();
	}
#ifdef ERP_DEBUG
                if ((++counter % 937 >= 0) &&
                    (  counter % 937 <= 10) &&
                    (  counter < 5000    ) &&
                    (  counter > 2000    )   ){
                        static int fake_count = 0;
                        printk ( KERN_INFO PRINTK_HEADER "***********************************************\n");
                        printk ( KERN_INFO PRINTK_HEADER "Faking I/O error to recover from; cntr=%i / %02X\n",counter,++fake_count);
                        printk ( KERN_INFO PRINTK_HEADER "***********************************************\n");
                        era = dasd_era_recover;
                        stat->flag |= DEVSTAT_FLAG_SENSE_AVAIL;
                        stat->dstat |= 0x02;
// sense 32
                        {
                                char *sense = stat->ii.sense.data;
                                sense [25] = 0x1D;
                                sense [27] = 0x00;
                                //sense [25] = (fake_count % 256); //0x1B;
                                //sense [27] = 0x00;
                        }
// sense 24
//                        {
//                                char *sense = stat->ii.sense.data;
//                                sense [0] = (counter % 0xFF); //0x1B;
//                                sense [1] = ((counter * 7) % 0xFF); //0x1B;
//                                sense [2] = (fake_count % 0xFF); //0x1B;
//                                sense [27] = 0x80;
//                        }

/*
                memset(stat->ii.sense.data,0,32);
                stat->ii.sense.data[2] = 0x06;
                stat->ii.sense.data[4] = 0x04;
                stat->ii.sense.data[5] = 0x60;
                stat->ii.sense.data[6] = 0x41;
                stat->ii.sense.data[8] = 0xff;
                stat->ii.sense.data[9] = 0xff;
                stat->ii.sense.data[15] = 0x05;
                stat->ii.sense.data[16] = 0x21;
                stat->ii.sense.data[18] = 0x60;
                stat->ii.sense.data[19] = 0x3b;
                stat->ii.sense.data[20] = 0x24;
                stat->ii.sense.data[21] = 0x61;
                stat->ii.sense.data[22] = 0x65;
                stat->ii.sense.data[23] = 0x03;
                stat->ii.sense.data[24] = 0x04;
                stat->ii.sense.data[25] = 0x10;
                stat->ii.sense.data[26] = 0x4e;
*/
        }
#endif
        /* first of all lets try to find out the appropriate era_action */
        if ( stat->flag & DEVSTAT_FLAG_SENSE_AVAIL ||
             stat->dstat & ~(DEV_STAT_CHN_END | DEV_STAT_DEV_END) ) {
                /* anything abnormal ? */
                if ( device->discipline->examine_error == NULL ||
                     stat->flag & DEVSTAT_HALT_FUNCTION ) {
                        era = dasd_era_fatal;
                } else {
                        era = device->discipline->examine_error (cqr, stat);
                }
        }
        if ( era == dasd_era_none ) {
		if (device->level == DASD_DEVICE_LEVEL_ANALYSIS_PENDING)
			device->level = DASD_DEVICE_LEVEL_ANALYSIS_PREPARED;
                check_then_set(&cqr->status, 
                               CQR_STATUS_IN_IO, CQR_STATUS_DONE);
                cqr->stopclk=now;
                cqr=cqr->next;
		/* start the next queued request if possible -> fast_io */
                if (cqr->status == CQR_STATUS_QUEUED) {
                        if (device->discipline->start_IO (cqr) != 0) {
                                printk (KERN_WARNING PRINTK_HEADER
                                        "Interrupt fastpath failed!\n");
                        } 
                }
        } else { /* error */
                if (cqr->dstat == NULL)
                        cqr->dstat = kmalloc (sizeof (devstat_t), GFP_ATOMIC);
                if (cqr->dstat) {
                        memcpy (cqr->dstat, stat, sizeof (devstat_t));
                } else {
                        PRINT_ERR ("no memory for dstat...ignoring\n");
                }
                /* dump sense data */
                if (device->discipline &&
                    device->discipline->dump_sense) {
                        char *errmsg = device->discipline->dump_sense (device, cqr);
                        if (errmsg != NULL) {
                                printk ("Sense data:\n%s", errmsg);
                                free_page ((unsigned long) errmsg);
                        } else {
                                printk (KERN_WARNING PRINTK_HEADER
                                        "No memory to dump error message\n");
                        }
                }
                switch(era) {
                case dasd_era_fatal:
                        check_then_set (&cqr->status,CQR_STATUS_IN_IO,
                                        CQR_STATUS_FAILED);
                        break;
                case dasd_era_recover:
                        check_then_set (&cqr->status,CQR_STATUS_IN_IO,
                                        CQR_STATUS_ERROR);
                        break;
                default:
                        BUG();
                }
        }                
	dasd_schedule_bh (device);
}

/* SECTION: Some stuff related to error recovery */

/*
 * DEFAULT_ERP_ACTION
 *
 * DESCRIPTION
 *   sets up the default-ERP ccw_req_t, namely one, which performs a TIC
 *   to the original channel program with a retry counter of 16
 *
 * PARAMETER
 *   cqr                failed CQR
 *
 * RETURN VALUES
 *   erp                CQR performing the ERP
 */
ccw_req_t *
default_erp_action (ccw_req_t * cqr)
{
	ccw_req_t *erp = dasd_alloc_request ((char *) &cqr->magic, 1, 0);

	printk (KERN_WARNING PRINTK_HEADER
		"Default ERP called... \n");

        if (erp == NULL)
               return NULL;

	erp->cpaddr->cmd_code = CCW_CMD_TIC;
	erp->cpaddr->cda = (__u32)(void *)cqr->cpaddr;
	erp->function = default_erp_action;
	erp->refers = cqr;
	erp->device = cqr->device;
	erp->magic = cqr->magic;
	erp->retries = 16;

	erp->status = CQR_STATUS_FILLED;

	return erp;
}

/*
 * DEFAULT_ERP_POSTACTION
 *
 * DESCRIPTION
 *   Frees all ERPs of the current ERP Chain and set the status
 *   of the original CQR either to CQR_STATUS_DONE if ERP was successful
 *   or to CQR_STATUS_FAILED if ERP was NOT successful.
 *
 * PARAMETER
 *   erp                current erp_head
 *
 * RETURN VALUES
 *   cqr                pointer to the original CQR
 */
ccw_req_t *
default_erp_postaction (ccw_req_t * erp)
{
	ccw_req_t *cqr = NULL, *free_erp = NULL;
	dasd_device_t *device = NULL;
        int success;
        
	device = (dasd_device_t *) (erp->device);

	if (erp->status == CQR_STATUS_DONE)
		success = 1;
	else
		success = 0;

#ifdef ERP_DEBUG

	/* print current erp_chain */
        printk (KERN_WARNING PRINTK_HEADER
                "default ERP postaction called for erp chain:\n");
        {
                ccw_req_t *temp_erp = NULL;
                for (temp_erp = erp; temp_erp != NULL; temp_erp = temp_erp->refers){
                        printk(KERN_WARNING PRINTK_HEADER 
                               "       erp %p refers to %p with erp function %p\n",
                               temp_erp,
                               temp_erp->refers,
                               temp_erp->function );
                }
        }

#endif /* ERP_DEBUG*/

	if (erp->refers == NULL || erp->function == NULL) {
                BUG();
	}
	if (erp->function != default_erp_action) {
                printk (KERN_WARNING PRINTK_HEADER
                        "default ERP postaction called ERP action [<%p>]\n",
                        erp->function);
	}
	/* free all ERPs - but NOT the original cqr */
        
	while (erp->refers != NULL) {
                free_erp = erp;
		erp = erp->refers;
		/* remove the request from the device queue */
		dasd_chanq_deq (&device->queue,	free_erp);
		/* free the finished erp request */
		dasd_free_request (free_erp);
	}
        
	/* save ptr to original cqr */
	cqr = erp;

#ifdef ERP_DEBUG
	printk (KERN_INFO PRINTK_HEADER
		"default_erp_postaction - left original request = %p \n",cqr);
#endif /* ERP_DEBUG */

	/* set corresponding status to original cqr */
	if (success) {
		check_then_set (&cqr->status, 
                                CQR_STATUS_ERROR, 
                                CQR_STATUS_DONE);
	} else {
		check_then_set (&cqr->status,
                                CQR_STATUS_ERROR,
                                CQR_STATUS_FAILED);
	}

#ifdef ERP_DEBUG
	/* print current erp_chain */
	printk (KERN_WARNING PRINTK_HEADER
		"default ERP postaction finished with remaining chain:\n");
	{
		ccw_req_t *temp_erp = NULL;
		for (temp_erp = cqr; temp_erp != NULL; temp_erp = temp_erp->refers) {
			printk (KERN_WARNING PRINTK_HEADER
				" erp %p refers to %p \n",
				temp_erp, temp_erp->refers);
		}
	}
#endif /* ERP_DEBUG */

	return cqr;
}				/* end default_erp_postaction */

/* SECTION: The helpers of the struct file_operations */

/* 
 * function dasd_format 
 * performs formatting of _device_ according to _fdata_
 * Note: The discipline's format_function is assumed to deliver formatting
 * commands to format a single unit of the device. In terms of the ECKD
 * devices this means CCWs are generated to format a single track.
 */

static int
dasd_format (dasd_device_t * device, format_data_t * fdata)
{
	int rc = 0;
        int format_done = 0;
	ccw_req_t *req = NULL;
	format_data_t temp =
	{
		fdata->start_unit,
		fdata->stop_unit,
		fdata->blksize,
		fdata->intensity
	};
        
        spin_lock (&dasd_open_count_lock);
	if (device->open_count != 1) {
		DASD_MESSAGE (KERN_INFO, device,
			      "device is already open %d times",
			      device->open_count);
                spin_unlock(&dasd_open_count_lock);
		return -EINVAL;
	}
	if (!device->discipline->format_device) {
                spin_unlock(&dasd_open_count_lock);
		return -EINVAL;
	}
        device->open_count = -1;
        spin_unlock (&dasd_open_count_lock);
	/* downgrade state of the device */
	dasd_set_device_level (device->devinfo.irq,
			       DASD_DEVICE_LEVEL_RECOGNIZED,
			       device->discipline,
			       0);
	DASD_MESSAGE (KERN_INFO, device, 
                      "Starting format from %d to %d (%d B blocks flags %d",
                      fdata->start_unit,
                      fdata->stop_unit,
                      fdata->blksize,
                      fdata->intensity);
	/* Invalidate first track */
	if (fdata->start_unit == DASD_FORMAT_DEFAULT_START_UNIT &&
	    fdata->stop_unit  == DASD_FORMAT_DEFAULT_STOP_UNIT  &&
	    fdata->intensity  == DASD_FORMAT_DEFAULT_INTENSITY    ) {
		format_data_t temp2 =
		{0, 0, fdata->blksize, 0x04};
		DASD_MESSAGE (KERN_INFO, device, 
                              "%s", 
                              "Invalidating first track...");
		req = device->discipline->format_device (device, &temp2);
		if (req) {
			rc = sleep_on_req (req);
			dasd_free_request (req);	/* request is no longer used */
		} else {
			rc = -EINVAL;
		}
		if (rc) {
                        DASD_MESSAGE (KERN_WARNING, device, 
                                      "%s",
                                      "Can't invalidate Track 0\n");
		} else {
                        DASD_MESSAGE (KERN_INFO, device, 
                                      "%s", 
                                      "...Invalidation complete");
                }
		temp.start_unit++;
	}
	/* format remainnig tracks of device */
	while (!rc                                                                 &&
	       ((req = device->discipline->format_device (device, &temp)) != NULL)   ) {
                                format_done=1;
		if ((rc = sleep_on_req (req)) != 0) {


			DASD_MESSAGE (KERN_WARNING, device,
				      " Formatting failed with rc = %d\n",
				      rc);
			break;
		}
                
		dasd_free_request (req);	/* request is no longer used */
		temp.start_unit++;
	}

	if (!rc         &&
	    req == NULL   ) {
		if (fdata->start_unit == DASD_FORMAT_DEFAULT_START_UNIT &&
		    fdata->stop_unit  == DASD_FORMAT_DEFAULT_STOP_UNIT  &&
	    	    fdata->intensity  == DASD_FORMAT_DEFAULT_INTENSITY    ) {
			format_data_t temp2 =
			{0, 0, fdata->blksize, fdata->intensity};
			DASD_MESSAGE (KERN_INFO, device, 
                                      "%s", 
                                      "Revalidating first track...");
			req = device->discipline->format_device (device, &temp2);
			if (req) {
				rc = sleep_on_req (req);
				dasd_free_request (req);	/* request is no longer used */
			} else {
				rc = -EINVAL;
			}
			if (rc) {
				DASD_MESSAGE (KERN_WARNING, device,
                                              "%s",
                                              "Can't revalidate Track 0\n");
			} else {
                                DASD_MESSAGE (KERN_INFO, device, 
                                              "%s", 
                                              "...Revalidation complete");
                        }
		}
	}			/* end if no more requests */

        /* check if at least one format cp was build in discipline */
        if (!format_done) {
                rc = -EINVAL;
        }

	if (rc)
		DASD_MESSAGE (KERN_WARNING, device,
			      "%s", " Formatting finished unsuccessfully");
	else
		DASD_MESSAGE (KERN_INFO, device,
			      "%s", " Formatting finished successfully");

        /* 
         * re-analyse device
         */
        dasd_set_device_level (device->devinfo.irq,
                               DASD_DEVICE_LEVEL_ONLINE,
                               device->discipline,
                               0);
        udelay (1500000);
                
        dasd_set_device_level (device->devinfo.irq,
                               DASD_DEVICE_LEVEL_ONLINE,
                               device->discipline,
                               0);

        spin_lock (&dasd_open_count_lock);
        device->open_count=1;
        spin_unlock (&dasd_open_count_lock);
	return rc;
}				/* end dasd_format */

static struct list_head dasd_ioctls = LIST_HEAD_INIT(dasd_ioctls);

static dasd_ioctl_list_t *
dasd_find_ioctl( int no )
{
	struct list_head *curr;
	list_for_each(curr,&dasd_ioctls){
		if (list_entry(curr,dasd_ioctl_list_t,list)->no == no ){
			return list_entry(curr,dasd_ioctl_list_t,list);
		}
	}
	return NULL;
}

int
dasd_ioctl_no_register ( int no, dasd_ioctl_fn_t handler )
{
	dasd_ioctl_list_t *new;
	if (dasd_find_ioctl(no))
		return -EBUSY;
	new = kmalloc(sizeof(dasd_ioctl_list_t),GFP_KERNEL);
 	if ( new ==  NULL )
		return -ENOMEM;
	new -> no = no;
	new -> handler = handler;
	list_add(&new->list,&dasd_ioctls);
#ifdef MODULE
        MOD_INC_USE_COUNT;
#endif
	return 0;
}

int
dasd_ioctl_no_unregister ( int no, dasd_ioctl_fn_t handler )
{	
	dasd_ioctl_list_t *old = dasd_find_ioctl(no);
	if ( old == NULL )
		return -ENOENT;
	if ( old->no != no ||
	     old->handler != handler )
		return -EINVAL;
	list_del(&old->list);
	kfree(old);
#ifdef MODULE
        MOD_DEC_USE_COUNT;
#endif
	return 0;
}

static int
do_dasd_ioctl (struct inode *inp, /* unsigned */ int no, unsigned long data)
{
	int rc = 0;
	dasd_device_t *device = dasd_device_from_kdev (inp->i_rdev);
	major_info_t *major_info;

	if (!device) {
		printk (KERN_WARNING PRINTK_HEADER
			"No device registered as device (%d:%d)\n",
			MAJOR (inp->i_rdev), MINOR (inp->i_rdev));
		return -EINVAL;
	}
	if ((_IOC_DIR (no) != _IOC_NONE) && (data == 0)) {
		PRINT_DEBUG ("empty data ptr");
		return -EINVAL;
	}
	major_info = device->major_info;
#if 0
	printk (KERN_DEBUG PRINTK_HEADER
		"ioctl 0x%08x %s'0x%x'%d(%d) on /dev/%s (%d:%d,"
		" devno 0x%04X on irq %d) with data %8lx\n",
		no,
		_IOC_DIR (no) == _IOC_NONE ? "0" :
		_IOC_DIR (no) == _IOC_READ ? "r" :
		_IOC_DIR (no) == _IOC_WRITE ? "w" :
		_IOC_DIR (no) == (_IOC_READ | _IOC_WRITE) ? "rw" : "u",
		_IOC_TYPE (no), _IOC_NR (no), _IOC_SIZE (no),
		device->name, MAJOR (inp->i_rdev), MINOR (inp->i_rdev),
		device->devinfo.devno, device->devinfo.irq,
		data);
#endif
	switch (no) {
	case BLKGETSIZE:{	/* Return device size */
			long blocks = blk_size[MAJOR (inp->i_rdev)][MINOR (inp->i_rdev)] << 1;
			rc = copy_to_user ((long *) data, &blocks, sizeof (long));
			if (rc)
				rc = -EFAULT;
			break;
		}
	case BLKRRPART:{
                        if (!capable(CAP_SYS_ADMIN)) {
                            rc = -EACCES;
                            break;
                        }
			fsync_dev(inp->i_rdev);
			dasd_partn_detect (device);
			invalidate_buffers(inp->i_rdev);
			rc = 0;
			break;
		}
	case HDIO_GETGEO:{
			struct hd_geometry geo = {0,};
			rc = dasd_fillgeo(inp->i_rdev, &geo);
			if (rc)
				break;

			rc = copy_to_user ((struct hd_geometry *) data, &geo,
					   sizeof (struct hd_geometry));
			if (rc)
				rc = -EFAULT;
			break;
		}
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
        case BLKSSZGET:
        case BLKROSET:
        case BLKROGET:
        case BLKRASET:
        case BLKRAGET:
        case BLKFLSBUF:
        case BLKPG: 
        case BLKELVGET:
        case BLKELVSET:
                return blk_ioctl(inp->i_rdev, no, data);
                break;
#else
        case BLKRASET:
                if(!capable(CAP_SYS_ADMIN))
                        return -EACCES;
                if(!dev || arg > 0xff)
                        return -EINVAL;
                read_ahead[MAJOR(dev)] = arg;
                rc = 0;
                break;
        case BLKRAGET:
                if (!arg)
                        return -EINVAL;
                rc = put_user(read_ahead[MAJOR(dev)], (long *) arg);
                break;
        case BLKSSZGET: {
            /* Block size of media */
            rc = copy_to_user((int *)data,
                              &blksize_size[MAJOR(device->kdev)]
                              [MINOR(device->kdev)],
                              sizeof(int)) ? -EFAULT : 0;
        }
        RO_IOCTLS (inp->i_rdev, data);
	case BLKFLSBUF:{
                if (!capable(CAP_SYS_ADMIN)) 
                        return -EACCES;
                fsync_dev(inp->i_rdev);
                invalidate_buffers(inp->i_rdev);
                rc = 0;
                break;
        }
#endif				/* LINUX_IS_24 */
	case BIODASDRSID:{
			rc = copy_to_user ((void *) data,
					   &(device->devinfo.sid_data),
					   sizeof (senseid_t)) ? -EFAULT : 0;
			break;
		}
	case BIODASDRWTB:{
			int offset = 0;
			int xlt;
			rc = copy_from_user (&xlt, (void *) data,
					     sizeof (int)) ? -EFAULT : 0;
			if (rc)
				break;
			offset = major_info->gendisk.part[MINOR (inp->i_rdev)].start_sect >>
			    device->sizes.s2b_shift;
			xlt += offset;
			rc = copy_to_user ((void *) data, &xlt,
					   sizeof (int)) ? -EFAULT : 0;
			break;
		}
	case BIODASDFORMAT:{
			/* fdata == NULL is a valid arg to dasd_format ! */
			int partn;
			format_data_t fdata =
			{
				DASD_FORMAT_DEFAULT_START_UNIT,
				DASD_FORMAT_DEFAULT_STOP_UNIT,
				DASD_FORMAT_DEFAULT_BLOCKSIZE,
				DASD_FORMAT_DEFAULT_INTENSITY};

                        if (!capable(CAP_SYS_ADMIN)) {
                            rc = -EACCES;
                            break;
                        }
			if (data) {
				rc = copy_from_user (&fdata, (void *) data,
						     sizeof (format_data_t));
				if (rc) {
					rc = -EFAULT;
					break;
				}
			}
			partn = MINOR (inp->i_rdev) & ((1 << major_info->gendisk.minor_shift) - 1);
			if (partn != 0) {
				printk (KERN_WARNING PRINTK_HEADER
					" devno 0x%04X on subchannel %d = /dev/%s (%d:%d)"
				     " Cannot low-level format a partition\n",
					device->devinfo.devno, device->devinfo.irq, device->name,
				    MAJOR (inp->i_rdev), MINOR (inp->i_rdev));
				return -EINVAL;
			}
			rc = dasd_format (device, &fdata);
			break;
		}
	case BIODASDRSRV:{
            ccw_req_t *req;
            if (!capable(CAP_SYS_ADMIN)) {
                rc = -EACCES;
                break;
            }
            req = device->discipline->reserve (device);
            rc = sleep_on_req (req);
            dasd_free_request (req);
            break;
        }
	case BIODASDRLSE:{
            ccw_req_t *req;
            if (!capable(CAP_SYS_ADMIN)) {
                rc = -EACCES;
                break;
            }
            req = device->discipline->release (device);
            rc = sleep_on_req (req);
            dasd_free_request (req);
            break;
        }
	case BIODASDSLCK:{
			printk (KERN_WARNING PRINTK_HEADER
				"Unsupported ioctl BIODASDSLCK\n");
			break;
		}
	default:{

			dasd_ioctl_list_t *old = dasd_find_ioctl(no);
			if ( old ) {
				rc = old->handler(inp,no,data);
			} else {
				DASD_MESSAGE (KERN_INFO, device,
					    "ioctl 0x%08x=%s'0x%x'%d(%d) data %8lx\n",
					      no,
					      _IOC_DIR (no) == _IOC_NONE ? "0" :
					      _IOC_DIR (no) == _IOC_READ ? "r" :
					      _IOC_DIR (no) == _IOC_WRITE ? "w" :
					   _IOC_DIR (no) == (_IOC_READ | _IOC_WRITE) ?
					      "rw" : "u",
					 _IOC_TYPE (no), _IOC_NR (no), _IOC_SIZE (no),
					      data);
				rc = -EINVAL;
			}	
			break;
		}
	}
	return rc;
}

/* SECTION: The members of the struct file_operations */

static int
dasd_ioctl (struct inode *inp, struct file *filp,
	    unsigned int no, unsigned long data)
{
	int rc = 0;
	if ((!inp) || !(inp->i_rdev)) {
		return -EINVAL;
	}
	rc = do_dasd_ioctl (inp, no, data);
	return rc;
}

static int
dasd_open (struct inode *inp, struct file *filp)
{
	int rc = 0;
	dasd_device_t *device;

	if ((!inp) || !(inp->i_rdev)) {
		return -EINVAL;
	}
	if (dasd_probeonly) {
		printk ("\n" KERN_INFO PRINTK_HEADER "No access to device (%d:%d) due to probeonly mode\n", MAJOR (inp->i_rdev), MINOR (inp->i_rdev));
		return -EPERM;
	}
	device = dasd_device_from_kdev (inp->i_rdev);
	if (device == NULL) {
		printk (KERN_WARNING PRINTK_HEADER
			"No device registered as (%d:%d)\n", MAJOR (inp->i_rdev), MINOR (inp->i_rdev));
		return -ENODEV;
	}
	if (device->level < DASD_DEVICE_LEVEL_RECOGNIZED ||
	    device->discipline == NULL) {
		DASD_MESSAGE (KERN_WARNING, device,
			      " %s", " Cannot open unrecognized device\n");
		return -EINVAL;
	}
        spin_lock(&dasd_open_count_lock);
        if (device->open_count == -1) {
            spin_unlock (&dasd_open_count_lock);
            return -EBUSY;
        }
#ifdef MODULE
	MOD_INC_USE_COUNT;
#endif				/* MODULE */
	device->open_count++;
        spin_unlock (&dasd_open_count_lock);
	return rc;
}

static int
dasd_release (struct inode *inp, struct file *filp)
{
	int rc = 0;
	dasd_device_t *device;

	if ((!inp) || !(inp->i_rdev)) {
		return -EINVAL;
	}
	device = dasd_device_from_kdev (inp->i_rdev);
	if (device == NULL) {
		printk (KERN_WARNING PRINTK_HEADER
			"No device registered as %d:%d\n",
			MAJOR (inp->i_rdev), MINOR (inp->i_rdev));
		return -EINVAL;
	}
        spin_lock(&dasd_open_count_lock);
	if (device->open_count--) {
#ifdef MODULE
		MOD_DEC_USE_COUNT;
#endif				/* MODULE */
	} 
	fsync_dev(inp->i_rdev); /* sync the device */
	if (device->open_count == 0) /* finally invalidate buffers */
		invalidate_buffers(inp->i_rdev);
        spin_unlock(&dasd_open_count_lock);
	return rc;
}

static struct
block_device_operations dasd_device_operations =
{
	open:dasd_open,
	release:dasd_release,
	ioctl:dasd_ioctl,
#if ! (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
	read:block_read,
	write:block_write,
	fsync:block_fsync,
#endif				/* LINUX_IS_24 */
};

/* SECTION: Management of device list */
int
dasd_fillgeo(int kdev,struct hd_geometry *geo)
{
	dasd_device_t *device = dasd_device_from_kdev (kdev);
	if (!device->discipline->fill_geometry)
		return -EINVAL;

	device->discipline->fill_geometry (device, geo);
	geo->start = device->major_info->
			gendisk.part[MINOR(kdev)].start_sect;

	/* This is a hack.  dasdfmt and ibm.c expect geo.start 
	   to contain the block number of the label block when
	   it calls HDIO_GETGEO on the first partition. */
	if (geo->start == 0)
                geo->start = device->sizes.pt_block;

	return 0;
} 


/* This one is needed for naming 18000+ possible dasd devices */
int
dasd_device_name (char *str, int index, int partition, struct gendisk *hd)
{
	int len = 0;
	char first, second, third;

	if (hd) {
		major_info_t *major_info=NULL;
		struct list_head *l;

		list_for_each(l,&dasd_major_info[0].list) {
			major_info = list_entry(l,major_info_t,list); 
			if (&major_info->gendisk == hd) {
				break;
			}
			index += DASD_PER_MAJOR;
		}
		if (major_info == &dasd_major_info[0]) {
			return -EINVAL;
		}
	}
	third = index % 26;
	second = ((index-26) / 26) % 26;
	first = (((index-702) / 26) / 26) % 26;

	len = sprintf (str, "dasd");
	if (index>701) {
		len += sprintf (str + len, "%c", first + 'a');
	}
	if (index>25) {
		len += sprintf (str + len, "%c", second + 'a');
	}
	len += sprintf (str + len, "%c", third + 'a');
	if (partition) {
		if (partition > 9) {
			return -EINVAL;
		} else {
			len += sprintf (str + len, "%d", partition);
		}
	}
	str[len] = '\0';
	return 0;
}

#ifdef CONFIG_DASD_DYNAMIC
static void
dasd_plug_device (dasd_device_t *device)
{
        device->request_queue.plugged = 1; /* inhibit further calls of request_fn */
}

static void
dasd_unplug_device (dasd_device_t *device)
{
        generic_unplug_device(&device->request_queue);
}

static void
dasd_not_oper_handler (int irq, int status)
{
	dasd_device_t *device = NULL;
	major_info_t *major_info = NULL;
	struct list_head *l;
	int i, devno = -ENODEV;

        /* find out devno of leaving device: CIO has already deleted this information ! */
	list_for_each(l,&dasd_major_info[0].list) {
		major_info=list_entry(l, major_info_t,list);	
		for (i = 0; i < DASD_PER_MAJOR; i++) {
			device = major_info->dasd_device[i];
			if (device &&
			    device->devinfo.irq == irq) {
				devno = device->devinfo.devno;
				break;
			}
		}
		if (devno != -ENODEV)
			break;
	}
	if (devno < 0) {
		printk (KERN_WARNING PRINTK_HEADER
			"not_oper_handler called on irq %d no devno!\n", irq);
		return;
	}

	if (device->open_count != 0) {
                DASD_MESSAGE(KERN_ALERT,device,"%s",
                             "open device has gone. please repair!");
                dasd_set_device_level (irq, DASD_DEVICE_LEVEL_ANALYSED, 
                                       NULL, 0);
	} else {
                DASD_MESSAGE(KERN_INFO,device,"%s","device has gone");
                dasd_set_device_level (irq, DASD_DEVICE_LEVEL_UNKNOWN, 
                                       NULL, 0);
        }
}

static int
dasd_enable_single_volume (int irq)
{
	int rc = 0;
	dasd_set_device_level (irq, DASD_DEVICE_LEVEL_ONLINE,
			       NULL, 0);
	printk (KERN_INFO PRINTK_HEADER "waiting for response...\n");
	{
		static wait_queue_head_t wait_queue;
		init_waitqueue_head (&wait_queue);
		interruptible_sleep_on_timeout (&wait_queue, (5 * HZ) >> 1);
	}
	dasd_set_device_level (irq, DASD_DEVICE_LEVEL_ONLINE, NULL, 0);
	return rc;
}

int
dasd_oper_handler (int irq, devreg_t * devreg)
{
	int devno;
	int rc;
	devno = get_devno_by_irq (irq);
        printk (KERN_WARNING PRINTK_HEADER "Oper handler called\n");
	if (devno == -ENODEV) {
          printk (KERN_WARNING PRINTK_HEADER "NODEV\n");
		return -ENODEV;
        }
	if (dasd_autodetect) {
		dasd_add_range (devno, 0);
	}
	rc = dasd_enable_single_volume (irq);
	return rc;
}
#endif	/* CONFIG_DASD_DYNAMIC */

/* 
 * function dasd_set_device_level 
 */
static int
dasd_set_device_level (unsigned int irq, int desired_level,
		       dasd_discipline_t * discipline, int flags)
{
	int rc = 0;
	int devno;
	dasd_device_t **device_addr, *device;
	int current_level;
	major_info_t *major_info = NULL;
	struct list_head *l;
	int i, minor, major;
	ccw_req_t *cqr = NULL;
	struct gendisk *dd;

	devno = get_devno_by_irq (irq);
	if (devno < 0) { /* e.g. when device has been detached before */
		/* search in device list */
		list_for_each(l,&dasd_major_info[0].list) {
			major_info = list_entry(l,major_info_t,list);	
			for (i = 0; i < DASD_PER_MAJOR; i++) {
				device = major_info->dasd_device[i];
				if (device && device->devinfo.irq == irq) {
					devno = device->devinfo.devno;
					break;
				}
			}
			if (devno == -ENODEV)
				return -ENODEV;
		}
	}
	if (dasd_devindex_from_devno (devno) < 0) {
		return -ENODEV;
	}
	while ((device_addr = dasd_device_from_devno (devno)) == NULL) {
		if ((rc = dasd_register_major (NULL)) > 0) {
			printk (KERN_INFO PRINTK_HEADER
				"Registered to major number: %u\n", rc);
		} else {
			printk (KERN_WARNING PRINTK_HEADER
				"Couldn't register to another major no\n");
			return -ERANGE;
		}
	}
	device = *device_addr;
	if (!device) {		/* allocate device descriptor */
		device = kmalloc (sizeof (dasd_device_t), GFP_ATOMIC);
		if (!device) {
			printk (KERN_WARNING PRINTK_HEADER " No memory for device descriptor\n");
			goto nomem;
		}
		memset (device, 0, sizeof (dasd_device_t));
		*device_addr = device;
	}
	list_for_each(l,&dasd_major_info[0].list) {
		int i;
		major_info = list_entry(l,major_info_t,list);	
		for (i = 0; i < DASD_PER_MAJOR; i++) {
			if (major_info->dasd_device[i] == device) {
				device->kdev = MKDEV (major_info->gendisk.major, i << DASD_PARTN_BITS);
				break;
			}
		}
		if (i < DASD_PER_MAJOR)
			break;
	}
	if (major_info == &dasd_major_info[0]) {
		return -ENODEV;
	}
	minor = MINOR (device->kdev);
	major = MAJOR (device->kdev);
	current_level = device->level;
	if (desired_level > current_level) {
		switch (current_level) {
		case DASD_DEVICE_LEVEL_UNKNOWN:	/* Find a discipline */
                        device->major_info = major_info;
                        dasd_device_name (device->name,
                                          ((long) device_addr -
                                           (long) device->major_info->dasd_device) /
                                          sizeof (dasd_device_t *),
                                          0, &major_info->gendisk);
			rc = get_dev_info_by_irq (irq, &device->devinfo);
			if (rc < 0) {
				break;
			}
			discipline = dasd_find_discipline (device);
			if (discipline && !rc) {
                                DASD_MESSAGE (KERN_INFO, device,
                                              "%s", " ");
			} else {
                                break;
			}
			device->discipline = discipline;
                        device->debug_area = debug_register(device->name,0,2,3*sizeof(long));
                        debug_register_view(device->debug_area,&debug_sprintf_view);
                        debug_register_view(device->debug_area,&debug_hex_ascii_view);
			if (device->discipline->int_handler) {
#ifdef CONFIG_DASD_DYNAMIC
				s390_request_irq_special (irq,
                                                          device->discipline->int_handler,
                                                          dasd_not_oper_handler,
							  0,
							  DASD_NAME,
							  &device->dev_status);
#else				/* !defined(CONFIG_DASD_DYNAMIC) */
				request_irq (irq,
					     device->discipline->int_handler,
					     0,
					     DASD_NAME,
					     &device->dev_status);
#endif				/* CONFIG_DASD_DYNAMIC */
			}
			device->proc_dir = (struct proc_dir_entry *)
			    kmalloc (sizeof (struct proc_dir_entry), GFP_KERNEL);
			if (device->proc_dir) {
				memset (device->proc_dir, 0, sizeof (struct proc_dir_entry));
				device->proc_info = (struct proc_dir_entry *)
				    kmalloc (sizeof (struct proc_dir_entry), GFP_KERNEL);
				if (device->proc_info) {
					memset (device->proc_info, 0,
					      sizeof (struct proc_dir_entry));
				}
				device->proc_stats = (struct proc_dir_entry *)
				    kmalloc (sizeof (struct proc_dir_entry), GFP_KERNEL);
				if (device->proc_stats) {
					memset (device->proc_stats, 0,
					      sizeof (struct proc_dir_entry));
				}
			}
			init_waitqueue_head (&device->wait_q);
			check_then_set (&device->level,
				       DASD_DEVICE_LEVEL_UNKNOWN,
				       DASD_DEVICE_LEVEL_RECOGNIZED);
			if (desired_level == DASD_DEVICE_LEVEL_RECOGNIZED)
				break;
		case DASD_DEVICE_LEVEL_RECOGNIZED:	/* Fallthrough ?? */
			if (device->discipline->init_analysis) {
				cqr = device->discipline->init_analysis (device);
				if (cqr != NULL) {
					dasd_chanq_enq (&device->queue, cqr);
					if (device->discipline->start_IO) {
						long flags;
						s390irq_spin_lock_irqsave (irq, flags);
						device->discipline->start_IO (cqr);
						check_then_set (&device->level,
                                                                DASD_DEVICE_LEVEL_RECOGNIZED,
                                                                DASD_DEVICE_LEVEL_ANALYSIS_PENDING);
						s390irq_spin_unlock_irqrestore (irq, flags);
					}
				}
			} else {
				check_then_set (&device->level, DASD_DEVICE_LEVEL_RECOGNIZED,
                                                DASD_DEVICE_LEVEL_ANALYSIS_PREPARED);
			}
			if (desired_level >= DASD_DEVICE_LEVEL_ANALYSIS_PENDING)
				break;
		case DASD_DEVICE_LEVEL_ANALYSIS_PENDING:	/* Fallthrough ?? */
			return -EAGAIN;
		case DASD_DEVICE_LEVEL_ANALYSIS_PREPARED:	/* Re-entering here ! */
			if (device->discipline->do_analysis)
                                if (device->discipline->do_analysis (device))
                                        return -ENODEV;
			switch (device->sizes.bp_block) {
			case 512:
			case 1024:
			case 2048:
			case 4096:
				break;
			default:
				{
					printk (KERN_INFO PRINTK_HEADER
						"/dev/%s (devno 0x%04X): Detected invalid blocksize of %d bytes"
						" Did you format the drive?\n",
						device->name, devno, device->sizes.bp_block);
					return -EMEDIUMTYPE;
				}
			}
			blk_init_queue (&device->request_queue, do_dasd_request);
			blk_queue_headactive (&device->request_queue, 0);
			elevator_init(&device->request_queue.elevator, ELEVATOR_NOOP);
			for (i = 0; i < (1 << DASD_PARTN_BITS); i++) {
                                if (i == 0)
                                        blk_size[major][minor] = (device->sizes.blocks << device->sizes.s2b_shift) >> 1;
				else
					blk_size[major][minor + i] = 0;
				hardsect_size[major][minor + i] = device->sizes.bp_block;
				blksize_size[major][minor + i] = device->sizes.bp_block;
				if (blksize_size[major][minor + i] < 1024)
					blksize_size[major][minor + i] = 1024;

				max_sectors[major][minor + i] =
                                        device->discipline->max_blocks << device->sizes.s2b_shift;
			}
			check_then_set (&device->level,
				       DASD_DEVICE_LEVEL_ANALYSIS_PREPARED,
				       DASD_DEVICE_LEVEL_ANALYSED);
			dd = &major_info->gendisk;
			dd->sizes[minor] = (device->sizes.blocks <<
					    device->sizes.s2b_shift) >> 1;
                        dd->part[minor].start_sect = 0;
			{
				char buffer[5];
				sprintf(buffer,"%04X",device->devinfo.devno);
				dd->de_arr[minor>>DASD_PARTN_BITS] = devfs_mk_dir(dasd_devfs_handle,buffer,NULL);
			}
#if !(LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
#ifndef MODULE
			if (flags & 0x80)
#endif
#endif				/* KERNEL_VERSION */
				dasd_partn_detect (device);
			if (desired_level == DASD_DEVICE_LEVEL_ANALYSED)
				break;
		case DASD_DEVICE_LEVEL_ANALYSED:	/* Fallthrough ?? */
                        dasd_unplug_device(device);
			check_then_set (&device->level,
                                        DASD_DEVICE_LEVEL_ANALYSED,
                                        DASD_DEVICE_LEVEL_ONLINE);
                        
			if (desired_level == DASD_DEVICE_LEVEL_ONLINE)
                                break;
		case DASD_DEVICE_LEVEL_ONLINE:	
                        break;
		default:
			printk (KERN_WARNING PRINTK_HEADER
				"Internal error in " __FILE__ " on line %d."
				" validate_dasd called from %p with "
				" desired_level = %d, current_level =%d"
				" Pls send this message and your System.map to"
				" linux390@de.ibm.com\n",
				__LINE__, __builtin_return_address (0),
				desired_level, current_level);
			break;
		}
	} else if (desired_level < current_level) {	/* donwgrade device status */
		switch (current_level) {
		case DASD_DEVICE_LEVEL_ONLINE:	/* Fallthrough ?? */
                        dasd_plug_device(device); 
			check_then_set (&device->level,
                                        DASD_DEVICE_LEVEL_ONLINE,
                                        DASD_DEVICE_LEVEL_ANALYSED);
			if (desired_level == DASD_DEVICE_LEVEL_ANALYSED)
                                break;
                case DASD_DEVICE_LEVEL_ANALYSED:	/* Fallthrough ?? */
			for (i = 0; i < (1 << DASD_PARTN_BITS); i++) {
                                __invalidate_buffers(MKDEV(major,minor),1);
                                blk_size[major][minor] = 0;
				hardsect_size[major][minor + i] = 0;
				blksize_size[major][minor + i] = 0;
				max_sectors[major][minor + i] = 0;
			}
			memset (&device->sizes, 0, sizeof (dasd_sizes_t));
			blk_cleanup_queue (&device->request_queue);
                        check_then_set (&device->level,
                                        DASD_DEVICE_LEVEL_ANALYSED,
                                        DASD_DEVICE_LEVEL_ANALYSIS_PREPARED);
                        if (desired_level == DASD_DEVICE_LEVEL_ANALYSIS_PREPARED)
                                break;
		case DASD_DEVICE_LEVEL_ANALYSIS_PREPARED:
			check_then_set (&device->level,
				       DASD_DEVICE_LEVEL_ANALYSIS_PREPARED,
				       DASD_DEVICE_LEVEL_ANALYSIS_PENDING);
			if (desired_level == DASD_DEVICE_LEVEL_ANALYSIS_PENDING)
				break;
		case DASD_DEVICE_LEVEL_ANALYSIS_PENDING:	/* Fallthrough ?? */
			check_then_set (&device->level,
				       DASD_DEVICE_LEVEL_ANALYSIS_PENDING,
				       DASD_DEVICE_LEVEL_RECOGNIZED);
			if (desired_level == DASD_DEVICE_LEVEL_RECOGNIZED)
				break;
		case DASD_DEVICE_LEVEL_RECOGNIZED:	/* Fallthrough ?? */
			if (device->discipline->int_handler) {
				free_irq (irq, &device->dev_status);
			}
			device->discipline = NULL;
                        debug_unregister(device->debug_area);
			check_then_set (&device->level,
				       DASD_DEVICE_LEVEL_RECOGNIZED,
				       DASD_DEVICE_LEVEL_UNKNOWN);
                        *device_addr = NULL;
                        kfree(device);
			if (desired_level == DASD_DEVICE_LEVEL_UNKNOWN)
				break;
		case DASD_DEVICE_LEVEL_UNKNOWN:
			break;
		default:
			printk (KERN_WARNING PRINTK_HEADER
				"Internal error in " __FILE__ " on line %d."
				" validate_dasd called from %p with "
				" desired_level = %d, current_level =%d"
				" Pls send this message and your System.map to"
				" linux390@de.ibm.com\n",
				__LINE__, __builtin_return_address (0),
				desired_level, current_level);
			break;
		}
	}
	if (rc) {
		goto exit;
	}
      nomem:
	rc = -ENOMEM;
      exit:
	return 0;
}

/* SECTION: Procfs stuff */
typedef struct {
	char *data;
	int len;
} tempinfo_t;

void dasd_fill_inode (struct inode* inode, int fill) {
    if (fill)
        MOD_INC_USE_COUNT;
    else
        MOD_DEC_USE_COUNT;
}

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
static struct proc_dir_entry *dasd_proc_root_entry = NULL;
#else
static struct proc_dir_entry dasd_proc_root_entry =
{
	low_ino:0,
	namelen:4,
	name:"dasd",
	mode:S_IFDIR | S_IRUGO | S_IXUGO | S_IWUSR | S_IWGRP,
	nlink:1,
	uid:0,
	gid:0,
	size:0,
        fill_inode:dasd_fill_inode
};
#endif				/* KERNEL_VERSION */
static struct proc_dir_entry *dasd_devices_entry;
static struct proc_dir_entry *dasd_statistics_entry;

static int
dasd_devices_open (struct inode *inode, struct file *file)
{
	int rc = 0;
	int size = 1;
	int len = 0;
	major_info_t *temp = dasd_major_info;
	struct list_head *l;
	tempinfo_t *info;
	int i;

	info = (tempinfo_t *) vmalloc (sizeof (tempinfo_t));
	if (info == NULL) {
		printk (KERN_WARNING "No memory available for data\n");
		return -ENOMEM;
	} else {
		file->private_data = (void *) info;
	}
	list_for_each(l,&dasd_major_info[0].list) {
		temp = list_entry(l,major_info_t,list);	
		for (i = 0; i < 1 << (MINORBITS - DASD_PARTN_BITS); i++) {
			dasd_device_t *device = temp->dasd_device[i];
			if (device) {
				size += 128;
			}
		}
	}
	temp = dasd_major_info;
	info->data = (char *) vmalloc (size);	/* FIXME! determine space needed in a better way */
	if (size && info->data == NULL) {
		printk (KERN_WARNING "No memory available for data\n");
		vfree (info);
		return -ENOMEM;
	}
	list_for_each(l,&dasd_major_info[0].list) {
		temp = list_entry(l,major_info_t,list);	
		for (i = 0; i < 1 << (MINORBITS - DASD_PARTN_BITS); i++) {
			dasd_device_t *device = temp->dasd_device[i];
			if (device) {
				len += sprintf (info->data + len,
						"%04X(%s) at (%d:%d) is %7s:",
						device->devinfo.devno,
						device->discipline ? device->discipline->name : "none",
				    temp->gendisk.major, i << DASD_PARTN_BITS,
						device->name);
				switch (device->level) {
				case DASD_DEVICE_LEVEL_UNKNOWN:
					len += sprintf (info->data + len, "unknown\n");
					break;
				case DASD_DEVICE_LEVEL_RECOGNIZED:
					len += sprintf (info->data + len, "passive");
					len += sprintf (info->data + len, " at blocksize: %d, %ld blocks, %ld MB\n",
							device->sizes.bp_block,
							device->sizes.blocks,
							((device->sizes.bp_block >> 9) * device->sizes.blocks) >> 11);
					break;
				case DASD_DEVICE_LEVEL_ANALYSIS_PENDING:
					len += sprintf (info->data + len, "busy   \n");
					break;
				case DASD_DEVICE_LEVEL_ANALYSIS_PREPARED:
					len += sprintf (info->data + len, "n/f    \n");
					break;
				case DASD_DEVICE_LEVEL_ANALYSED:
					len += sprintf (info->data + len, "active ");
					len += sprintf (info->data + len, " at blocksize: %d, %ld blocks, %ld MB\n",
							device->sizes.bp_block,
							device->sizes.blocks,
							((device->sizes.bp_block >> 9) * device->sizes.blocks) >> 11);
					break;
				case DASD_DEVICE_LEVEL_ONLINE:
					len += sprintf (info->data + len, "active ");
					len += sprintf (info->data + len, " at blocksize: %d, %ld blocks, %ld MB\n",
							device->sizes.bp_block,
							device->sizes.blocks,
							((device->sizes.bp_block >> 9) * device->sizes.blocks) >> 11);
					break;
				default:
					len += sprintf (info->data + len, "no stat\n");
					break;
				}
			}
		}
	}
	info->len = len;
#ifdef MODULE
        MOD_INC_USE_COUNT;
#endif
	return rc;
}

#define MIN(a,b) ((a)<(b)?(a):(b))

static ssize_t
dasd_devices_read (struct file *file, char *user_buf, size_t user_len, loff_t * offset)
{
	loff_t len;
	tempinfo_t *p_info = (tempinfo_t *) file->private_data;

	if (*offset >= p_info->len) {
		return 0;	/* EOF */
	} else {
		len = MIN (user_len, (p_info->len - *offset));
		if (copy_to_user (user_buf, &(p_info->data[*offset]), len))
			return -EFAULT;
		(*offset) += len;
		return len;	/* number of bytes "read" */
	}
}

static ssize_t
dasd_devices_write (struct file *file, const char *user_buf, size_t user_len, loff_t * offset)
{
	char *buffer = vmalloc (user_len+1);
	int off = 0;
	char *temp;
        int irq;
        int j,target;
        dasd_range_t *rptr, range;

	if (buffer == NULL)
		return -ENOMEM;
	if (copy_from_user (buffer, user_buf, user_len)) {
		vfree(buffer);
		return -EFAULT;
	}
	buffer[user_len] = 0;
	printk (KERN_INFO PRINTK_HEADER "Now executing %s\n", buffer);
        if (strncmp ( buffer, "set ",4) &&
            strncmp ( buffer, "add ",4)){
                printk (KERN_WARNING PRINTK_HEADER 
                        "/proc/dasd/devices: only 'set' and 'add' are supported verbs");
                return -EINVAL;
        }
        off += 4;
        while (!isalnum(buffer[off])) off++;
	if (!strncmp (buffer + off, "device", strlen ("device"))) {
		off += strlen("device");
		while (!isalnum(buffer[off])) off++;
        }
	if (!strncmp (buffer + off, "range=", strlen ("range="))) {
		off += strlen("range=");
		while (!isalnum(buffer[off])) off++;
        }
        temp = buffer+off;
        range.from = dasd_strtoul (temp, &temp);
        range.to = range.from;
        if (*temp == '-') {
                temp++;
                range.to = dasd_strtoul (temp, &temp);
        }
        off = (long)temp - (long)buffer;
        if ( !strncmp ( buffer, "add",strlen("add"))) {
                rptr = dasd_add_range (range.from, range.to);
        } else {
                rptr = &range;
        }
        while (!isalnum(buffer[off])) off++;
        printk (KERN_INFO PRINTK_HEADER 
                "varying device range %04X-%04X\n", rptr->from, rptr->to);
        if ( !strncmp ( buffer, "add",strlen("add")) ||
             !strncmp ( buffer+off, "on",strlen("on")) ) {
                target = DASD_DEVICE_LEVEL_ONLINE;
                for (j = rptr->from; j <= rptr->to; j++) {
			irq = get_irq_by_devno (j);
			if (irq >= 0) {
				dasd_set_device_level (irq, DASD_DEVICE_LEVEL_ONLINE, NULL, 0);
			}
		}
		printk (KERN_INFO PRINTK_HEADER "waiting for responses...\n");
		{
			static wait_queue_head_t wait_queue;
			init_waitqueue_head (&wait_queue);
			interruptible_sleep_on_timeout (&wait_queue, (5 * HZ) );
		}
        } else if ( !strncmp ( buffer+off, "off",strlen("off"))) {
                target = DASD_DEVICE_LEVEL_UNKNOWN;
        } else {
                printk (KERN_WARNING PRINTK_HEADER 
                        "/proc/dasd/devices: parse error in '%s'", buffer);
                vfree (buffer);
                return -EINVAL;
                
        }
        for (j = rptr->from; j <= rptr->to; j++) {
                irq = get_irq_by_devno (j);
                if (irq >= 0) {
                        dasd_set_device_level (irq, target, NULL, 0);
                }
        }
	return user_len;
}

static int
dasd_devices_close (struct inode *inode, struct file *file)
{
	int rc = 0;
	tempinfo_t *p_info = (tempinfo_t *) file->private_data;
	if (p_info) {
		if (p_info->data)
			vfree (p_info->data);
		vfree (p_info);
	}
#ifdef MODULE
        MOD_DEC_USE_COUNT;
#endif
	return rc;
}

static struct file_operations dasd_devices_file_ops =
{
	read:dasd_devices_read,	/* read */
	write:dasd_devices_write,	/* write */
	open:dasd_devices_open,	/* open */
	release:dasd_devices_close,	/* close */
};

static struct inode_operations dasd_devices_inode_ops =
{
#if !(LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
	default_file_ops:&dasd_devices_file_ops		/* file ops */
#endif				/* LINUX_IS_24 */
};

static int
dasd_statistics_open (struct inode *inode, struct file *file)
{
	int rc = 0;
	int len = 0;
	tempinfo_t *info;
	int shift, i, help = 0;

	info = (tempinfo_t *) vmalloc (sizeof (tempinfo_t));
	if (info == NULL) {
		printk (KERN_WARNING "No memory available for data\n");
		return -ENOMEM;
	} else {
		file->private_data = (void *) info;
	}
	info->data = (char *) vmalloc (PAGE_SIZE);	/* FIXME! determine space needed in a better way */
	if (info->data == NULL) {
		printk (KERN_WARNING "No memory available for data\n");
		vfree (info);
		file->private_data = NULL;
		return -ENOMEM;
	}
	for (shift = 0, help = dasd_global_profile.dasd_io_reqs;
	     help > 8192;
	     help = help >> 1, shift++) ;
	len = sprintf (info->data, "%ld dasd I/O requests\n", dasd_global_profile.dasd_io_reqs);
	len += sprintf (info->data + len, "__<4 ___8 __16 __32 __64 _128 _256 _512 __1k __2k __4k __8k _16k _32k _64k 128k\n");
	len += sprintf (info->data + len, "_256 _512 __1M __2M __4M __8M _16M _32M _64M 128M 256M 512M __1G __2G __4G _>4G\n");
	len += sprintf (info->data + len, "Histogram of sizes (512B secs)\n");
	for (i = 0; i < 16; i++) {
		len += sprintf (info->data + len, "%4ld ", dasd_global_profile.dasd_io_secs[i] >> shift);
	}
	len += sprintf (info->data + len, "\n");
	len += sprintf (info->data + len, "Histogram of I/O times\n");
	for (i = 0; i < 16; i++) {
		len += sprintf (info->data + len, "%4ld ", dasd_global_profile.dasd_io_times[i] >> shift);
	}
	len += sprintf (info->data + len, "\n");
	for (; i < 32; i++) {
		len += sprintf (info->data + len, "%4ld ", dasd_global_profile.dasd_io_times[i] >> shift);
	}
	len += sprintf (info->data + len, "\n");
	len += sprintf (info->data + len, "Histogram of I/O times per sector\n");
	for (i = 0; i < 16; i++) {
		len += sprintf (info->data + len, "%4ld ", dasd_global_profile.dasd_io_timps[i] >> shift);
	}
	len += sprintf (info->data + len, "\n");
	for (; i < 32; i++) {
		len += sprintf (info->data + len, "%4ld ", dasd_global_profile.dasd_io_timps[i] >> shift);
	}
	len += sprintf (info->data + len, "\n");
	len += sprintf (info->data + len, "Histogram of I/O time till ssch\n");
	for (i = 0; i < 16; i++) {
		len += sprintf (info->data + len, "%4ld ", dasd_global_profile.dasd_io_time1[i] >> shift);
	}
	len += sprintf (info->data + len, "\n");
	for (; i < 32; i++) {
		len += sprintf (info->data + len, "%4ld ", dasd_global_profile.dasd_io_time1[i] >> shift);
	}
	len += sprintf (info->data + len, "\n");
	len += sprintf (info->data + len, "Histogram of I/O time between ssch and irq\n");
	for (i = 0; i < 16; i++) {
		len += sprintf (info->data + len, "%4ld ", dasd_global_profile.dasd_io_time2[i] >> shift);
	}
	len += sprintf (info->data + len, "\n");
	for (; i < 32; i++) {
		len += sprintf (info->data + len, "%4ld ", dasd_global_profile.dasd_io_time2[i] >> shift);
	}
	len += sprintf (info->data + len, "\n");
	len += sprintf (info->data + len, "Histogram of I/O time between ssch and irq per sector\n");
	for (i = 0; i < 16; i++) {
		len += sprintf (info->data + len, "%4ld ", dasd_global_profile.dasd_io_time2ps[i] >> shift);
	}
	len += sprintf (info->data + len, "\n");
	for (; i < 32; i++) {
		len += sprintf (info->data + len, "%4ld ", dasd_global_profile.dasd_io_time2ps[i] >> shift);
	}
	len += sprintf (info->data + len, "\n");
	len += sprintf (info->data + len, "Histogram of I/O time between irq and end\n");
	for (i = 0; i < 16; i++) {
		len += sprintf (info->data + len, "%4ld ", dasd_global_profile.dasd_io_time3[i] >> shift);
	}
	len += sprintf (info->data + len, "\n");
	for (; i < 32; i++) {
		len += sprintf (info->data + len, "%4ld ", dasd_global_profile.dasd_io_time3[i] >> shift);
	}
	len += sprintf (info->data + len, "\n");
	info->len = len;
#ifdef MODULE
        MOD_INC_USE_COUNT;
#endif
	return rc;
}

static struct file_operations dasd_statistics_file_ops =
{
	read:dasd_devices_read,	/* read */
	open:dasd_statistics_open,	/* open */
	release:dasd_devices_close,	/* close */
};

static struct inode_operations dasd_statistics_inode_ops =
{
#if !(LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
	default_file_ops:&dasd_statistics_file_ops	/* file ops */
#endif				/* LINUX_IS_24 */
};

int
dasd_proc_init (void)
{
	int rc = 0;
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
	dasd_proc_root_entry = proc_mkdir ("dasd", &proc_root);
	dasd_devices_entry = create_proc_entry ("devices",
						S_IFREG | S_IRUGO | S_IWUSR,
						dasd_proc_root_entry);
	dasd_devices_entry->proc_fops = &dasd_devices_file_ops;
	dasd_devices_entry->proc_iops = &dasd_devices_inode_ops;
	dasd_statistics_entry = create_proc_entry ("statistics",
						   S_IFREG | S_IRUGO | S_IWUSR,
						   dasd_proc_root_entry);
	dasd_statistics_entry->proc_fops = &dasd_statistics_file_ops;
	dasd_statistics_entry->proc_iops = &dasd_statistics_inode_ops;
#else
	proc_register (&proc_root, &dasd_proc_root_entry);
	dasd_devices_entry = (struct proc_dir_entry *) kmalloc (sizeof (struct proc_dir_entry), GFP_ATOMIC);
	if (dasd_devices_entry) {
		memset (dasd_devices_entry, 0, sizeof (struct proc_dir_entry));
		dasd_devices_entry->name = "devices";
		dasd_devices_entry->namelen = strlen ("devices");
		dasd_devices_entry->low_ino = 0;
		dasd_devices_entry->mode = (S_IFREG | S_IRUGO | S_IWUSR);
		dasd_devices_entry->nlink = 1;
		dasd_devices_entry->uid = 0;
		dasd_devices_entry->gid = 0;
		dasd_devices_entry->size = 0;
		dasd_devices_entry->get_info = NULL;
		dasd_devices_entry->ops = &dasd_devices_inode_ops;
		proc_register (&dasd_proc_root_entry, dasd_devices_entry);
	}
	dasd_statistics_entry = (struct proc_dir_entry *) kmalloc (sizeof (struct proc_dir_entry), GFP_ATOMIC);
	if (dasd_statistics_entry) {
		memset (dasd_statistics_entry, 0, sizeof (struct proc_dir_entry));
		dasd_statistics_entry->name = "statistics";
		dasd_statistics_entry->namelen = strlen ("statistics");
		dasd_statistics_entry->low_ino = 0;
		dasd_statistics_entry->mode = (S_IFREG | S_IRUGO | S_IWUSR);
		dasd_statistics_entry->nlink = 1;
		dasd_statistics_entry->uid = 0;
		dasd_statistics_entry->gid = 0;
		dasd_statistics_entry->size = 0;
		dasd_statistics_entry->get_info = NULL;
		dasd_statistics_entry->ops = &dasd_statistics_inode_ops;
		proc_register (&dasd_proc_root_entry, dasd_statistics_entry);
	}
#endif				/* LINUX_IS_24 */
	return rc;
}

void
dasd_proc_cleanup (void)
{
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
	remove_proc_entry ("devices", dasd_proc_root_entry);
	remove_proc_entry ("statistics", dasd_proc_root_entry);
	remove_proc_entry ("dasd", &proc_root);
#else
	proc_unregister (&dasd_proc_root_entry, dasd_statistics_entry->low_ino);
	kfree (dasd_statistics_entry);
	proc_unregister (&dasd_proc_root_entry, dasd_devices_entry->low_ino);
	kfree (dasd_devices_entry);
	proc_unregister (&proc_root, dasd_proc_root_entry.low_ino);
#endif				/* LINUX_IS_24 */
}

/* SECTION: Initializing the driver */
int __init
dasd_init (void)
{
	int rc = 0;
	int irq;
	int j;
	major_info_t *major_info=NULL;
	struct list_head *l;
	dasd_range_t *range;

	printk (KERN_INFO PRINTK_HEADER "initializing...\n");
        dasd_debug_area = debug_register(DASD_NAME,0,2,3*sizeof(long));
        debug_register_view(dasd_debug_area,&debug_sprintf_view);
        debug_register_view(dasd_debug_area,&debug_hex_ascii_view);
        
        if ( dasd_debug_area == NULL ) {
                goto failed;
        }
        DASD_DRIVER_DEBUG_EVENT(0,dasd_init,"%s","ENTRY");
	dasd_devfs_handle = devfs_mk_dir(NULL,DASD_NAME,NULL);
        if ( dasd_devfs_handle < 0 ) {
                DASD_DRIVER_DEBUG_EVENT(1,dasd_init,"%s","no devfs");
                goto failed;
        }
	list_for_each(l,&dasd_major_info[0].list) {
		major_info=list_entry(l,major_info_t,list);
                if ((rc = dasd_register_major (major_info)) > 0) {
                        DASD_DRIVER_DEBUG_EVENT(1,dasd_init,
                                                "major %d: success",major_info->gendisk.major);
                        printk (KERN_INFO PRINTK_HEADER
                                "Registered successfully to major no %u\n", major_info->gendisk.major);
		} else {
                        DASD_DRIVER_DEBUG_EVENT(1,dasd_init,
                                                "major %d: failed",major_info->gendisk.major);
			printk (KERN_WARNING PRINTK_HEADER
				"Couldn't register successfully to major no %d\n", major_info->gendisk.major);
			/* revert registration of major infos */
                        goto failed;
		}
	}
#ifndef MODULE
	dasd_split_parm_string (dasd_parm_string);
#endif				/* ! MODULE */
	dasd_parse (dasd);
	dasd_init_emergency_req ();

	rc = dasd_proc_init ();
	if (rc) {
                DASD_DRIVER_DEBUG_EVENT(1,dasd_init,
                                        "%s","no proc-FS");
                goto failed;
	}
	genhd_dasd_name = dasd_device_name;
	genhd_dasd_fillgeo = dasd_fillgeo;

#ifdef CONFIG_DASD_ECKD
	rc = dasd_eckd_init ();
	if (rc == 0) {
                DASD_DRIVER_DEBUG_EVENT(1,dasd_init,
                                        "ECKD discipline %s","success");
		printk (KERN_INFO PRINTK_HEADER
			"Registered ECKD discipline successfully\n");
	} else {
                DASD_DRIVER_DEBUG_EVENT(1,dasd_init,
                                        "ECKD discipline %s","failed");
                goto failed;
	}
#endif				/* CONFIG_DASD_ECKD */
#ifdef CONFIG_DASD_FBA
	rc = dasd_fba_init ();
	if (rc == 0) {
                DASD_DRIVER_DEBUG_EVENT(1,dasd_init,
                                        "FBA discipline %s","success");

		printk (KERN_INFO PRINTK_HEADER
			"Registered FBA discipline successfully\n");
	} else {
                DASD_DRIVER_DEBUG_EVENT(1,dasd_init,
                                        "FBA discipline %s","failed");
                goto failed;
	}
#endif				/* CONFIG_DASD_FBA */
#ifdef CONFIG_DASD_DIAG
	if (MACHINE_IS_VM) {
		rc = dasd_diag_init ();
		if (rc == 0) {
                        DASD_DRIVER_DEBUG_EVENT(1,dasd_init,
                                                "DIAG discipline %s","success");
			printk (KERN_INFO PRINTK_HEADER
				"Registered DIAG discipline successfully\n");
		} else {
                        DASD_DRIVER_DEBUG_EVENT(1,dasd_init,
                                                "DIAG discipline %s","failed");
                        goto failed;
		}
	}
#endif				/* CONFIG_DASD_DIAG */
	rc = 0;
	if (dasd_autodetect) { /* update device range to all devices */
		for (irq = get_irq_first (); irq != -ENODEV; 
                     irq = get_irq_next (irq)) {
			int devno = get_devno_by_irq (irq);
			int index = dasd_devindex_from_devno (devno);
			if (index == -ENODEV) {		/* not included in ranges */
                                DASD_DRIVER_DEBUG_EVENT(2,dasd_init,
                                                        "add %04X to range",
                                                        devno);
				dasd_add_range (devno, 0);
                        }
                }
        }
	for (range = dasd_range_head; range; range = range->next) {
		for (j = range->from; j <= range->to; j++) {
			irq = get_irq_by_devno (j);
                        if (irq >= 0)
                                DASD_DRIVER_DEBUG_EVENT(2,dasd_init,
                                                        "1st step in initialization irq 0x%x",irq);
                        dasd_set_device_level (irq, DASD_DEVICE_LEVEL_ONLINE,
                                               NULL, 0);
		}
	}
	printk (KERN_INFO PRINTK_HEADER "waiting for responses...\n");
	{
		static wait_queue_head_t wait_queue;
		init_waitqueue_head (&wait_queue);
		interruptible_sleep_on_timeout (&wait_queue,
						(5 * HZ) );
	}
	for (range = dasd_range_head; range; range = range->next) {
		for (j = range->from; j <= range->to; j++) {
			irq = get_irq_by_devno (j);
			if (irq >= 0) {
                                DASD_DRIVER_DEBUG_EVENT(2,dasd_init,
                                                        "2nd step in initialization irq 0x%x",irq);
				dasd_set_device_level (irq, DASD_DEVICE_LEVEL_ONLINE,
						       NULL, 0);
			}
		}
	}
        goto out;
 failed:
	printk (KERN_INFO PRINTK_HEADER "initialization not performed due to errors\n");
        cleanup_dasd();
 out:
        DASD_DRIVER_DEBUG_EVENT(0,dasd_init,"%s","LEAVE");
	printk (KERN_INFO PRINTK_HEADER "initialization finished\n");
	return rc;
}

static void
cleanup_dasd (void)
{
	int i,j,rc;
	int irq;
	major_info_t *major_info=NULL;
	struct list_head *l;
	dasd_range_t *range, *next;

	printk (KERN_INFO PRINTK_HEADER "shutting down\n");
        DASD_DRIVER_DEBUG_EVENT(0,"cleanup_dasd","%s","ENTRY");
	for (range = dasd_range_head; range; range = range->next) {
		for (j = range->from; j <= range->to; j++) {
			irq = get_irq_by_devno (j);
			if (irq >= 0) {
                                DASD_DRIVER_DEBUG_EVENT(2,"cleanup_dasd",
                                                        "shutdown irq 0x%x",irq);
				dasd_set_device_level (irq, DASD_DEVICE_LEVEL_UNKNOWN,
						       NULL, 0);
			}
		}
	}
#ifdef CONFIG_DASD_DIAG
	if (MACHINE_IS_VM) {
		dasd_diag_cleanup ();
                DASD_DRIVER_DEBUG_EVENT(1,"cleanup_dasd",
                                        "DIAG discipline %s","success");
                printk (KERN_INFO PRINTK_HEADER
                        "De-Registered DIAG discipline successfully\n");
	}
#endif				/* CONFIG_DASD_DIAG */
#ifdef CONFIG_DASD_FBA
	dasd_fba_cleanup ();
        DASD_DRIVER_DEBUG_EVENT(1,"cleanup_dasd",
                                "FBA discipline %s","success");
        printk (KERN_INFO PRINTK_HEADER
                "De-Registered FBA discipline successfully\n");
#endif				/* CONFIG_DASD_FBA */
#ifdef CONFIG_DASD_ECKD
	dasd_eckd_cleanup ();
        DASD_DRIVER_DEBUG_EVENT(1,"cleanup_dasd",
                                "ECKD discipline %s","success");
        printk (KERN_INFO PRINTK_HEADER
                "De-Registered ECKD discipline successfully\n");
#endif				/* CONFIG_DASD_ECKD */
        
	dasd_proc_cleanup ();
	dasd_cleanup_emergency_req ();

	list_for_each(l,&dasd_major_info[0].list) {
		major_info=list_entry(l,major_info_t,list);	
		for (i = 0; i < DASD_PER_MAJOR; i++) {
			kfree (major_info->dasd_device[i]);
		}
		if ((major_info -> flags & DASD_MAJOR_INFO_REGISTERED) &&
		    (rc = dasd_unregister_major (major_info)) == 0) {
                        DASD_DRIVER_DEBUG_EVENT(1,"cleanup_dasd",
                                                "major %d: success",major_info->gendisk.major);
			printk (KERN_INFO PRINTK_HEADER
				"Unregistered successfully from major no %u\n", major_info->gendisk.major);
		} else {
                        DASD_DRIVER_DEBUG_EVENT(1,"cleanup_dasd",
                                                "major %d: failed",major_info->gendisk.major);
			printk (KERN_WARNING PRINTK_HEADER
				"Couldn't unregister successfully from major no %d rc = %d\n", major_info->gendisk.major, rc);
		}
	}


	range = dasd_range_head;
	while (range) {
		next = range->next;
                dasd_remove_range (range);
		if (next == NULL)
			break;
		else
			range = next;
	}
	dasd_range_head = NULL;
        
#ifndef MODULE
        for( j = 0; j < 256; j++ )
                if ( dasd[j] ) {
                        kfree(dasd[j]);
                        dasd[j] = NULL;
                }
#endif /* MODULE */
        if (dasd_devfs_handle) 
                devfs_unregister(dasd_devfs_handle);
        if (dasd_debug_area != NULL )
                debug_unregister(dasd_debug_area);

	printk (KERN_INFO PRINTK_HEADER "shutdown completed\n");
        DASD_DRIVER_DEBUG_EVENT(0,"cleanup_dasd","%s","LEAVE");
}

#ifdef MODULE
int
init_module (void)
{
	int rc = 0;
	return dasd_init ();
	return rc;
}

void
cleanup_module (void)
{
	cleanup_dasd ();
	return;
}
#endif

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 4 
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -4
 * c-argdecl-indent: 4
 * c-label-offset: -4
 * c-continued-statement-offset: 4
 * c-continued-brace-offset: 0
 * indent-tabs-mode: nil
 * tab-width: 8
 * End:
 */
