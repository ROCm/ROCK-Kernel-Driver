/*
 * File...........: linux/drivers/s390/block/dasd.c
 * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
 *                  Horst Hummel <Horst.Hummel@de.ibm.com> 
 *                  Carsten Otte <Cotte@de.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * (C) IBM Corporation, IBM Deutschland Entwicklung GmbH, 1999-2001
 *
 * History of changes (starts July 2000)
 * 11/09/00 complete redesign after code review
 * 02/01/01 added dynamic registration of ioctls
 *          fixed bug in registration of new majors
 *          fixed handling of request during dasd_end_request
 *          fixed handling of plugged queues
 *          fixed partition handling and HDIO_GETGEO
 *          fixed traditional naming scheme for devices beyond 702
 *          fixed some race conditions related to modules
 *          added devfs suupport
 * 03/06/01 refined dynamic attach/detach for leaving devices which are online.
 * 03/09/01 refined dynamic modifiaction of devices
 * 03/12/01 moved policy in dasd_format to dasdfmt (renamed BIODASDFORMAT)
 * 03/19/01 added BIODASDINFO-ioctl
 *          removed 2.2 compatibility
 * 04/27/01 fixed PL030119COT (dasd_disciplines does not work)
 * 04/30/01 fixed PL030146HSM (module locking with dynamic ioctls)
 *          fixed PL030130SBA (handling of invalid ranges)
 * 05/02/01 fixed PL030145SBA (killing dasdmt)
 *          fixed PL030149SBA (status of 'accepted' devices)
 *          fixed PL030146SBA (BUG in ibm.c after adding device)
 *          added BIODASDPRRD ioctl interface
 * 05/11/01 fixed  PL030164MVE (trap in probeonly mode)
 * 05/15/01 fixed devfs support for unformatted devices
 * 06/26/01 hopefully fixed PL030172SBA,PL030234SBA
 * 07/09/01 fixed PL030324MSH (wrong statistics output)
 * 07/16/01 merged in new fixes for handling low-mem situations
 */

#include <linux/config.h>
#include <linux/version.h>
#include <linux/kmod.h>
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
#include <linux/wait.h>

#include <asm/ccwcache.h>
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
#include <asm/dasd.h>

#include "dasd_int.h"

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
MODULE_PARM (dasd_disciplines, "1-" __MODULE_STRING (8) "s");
EXPORT_SYMBOL (dasd_chanq_enq_head);
EXPORT_SYMBOL (dasd_debug_area);
EXPORT_SYMBOL (dasd_chanq_enq);
EXPORT_SYMBOL (dasd_chanq_deq);
EXPORT_SYMBOL (dasd_discipline_add);
EXPORT_SYMBOL (dasd_discipline_del);
EXPORT_SYMBOL (dasd_start_IO);
EXPORT_SYMBOL (dasd_term_IO);
EXPORT_SYMBOL (dasd_schedule_bh);
EXPORT_SYMBOL (dasd_int_handler);
EXPORT_SYMBOL (dasd_oper_handler);
EXPORT_SYMBOL (dasd_alloc_request);
EXPORT_SYMBOL (dasd_free_request);
EXPORT_SYMBOL (dasd_ioctl_no_register);
EXPORT_SYMBOL (dasd_ioctl_no_unregister);
EXPORT_SYMBOL (dasd_default_erp_action);
EXPORT_SYMBOL (dasd_default_erp_postaction);
EXPORT_SYMBOL (dasd_sleep_on_req);
EXPORT_SYMBOL (dasd_set_normalized_cda);

/* SECTION: Constant definitions to be used within this file */

#define PRINTK_HEADER DASD_NAME":"
#undef  DASD_PROFILE            /* fill profile information - used for */
                                /* statistics and perfomance           */

#define DASD_MIN_SIZE_FOR_QUEUE 32
#undef CONFIG_DYNAMIC_QUEUE_MIN_SIZE
#define DASD_CHANQ_MAX_SIZE 6

/* SECTION: prototypes for static functions of dasd.c */

static request_fn_proc do_dasd_request;
static int dasd_set_device_level (unsigned int, dasd_discipline_t *, int);
static request_queue_t *dasd_get_queue (kdev_t kdev);
static void cleanup_dasd (void);
static void dasd_plug_device (dasd_device_t * device);
static int dasd_fillgeo (int kdev, struct hd_geometry *geo);
static void dasd_enable_ranges (dasd_range_t *, dasd_discipline_t *, int); 
static void dasd_disable_ranges (dasd_range_t *, dasd_discipline_t *, int, int); 
static void dasd_enable_single_device ( unsigned long);
static inline int dasd_state_init_to_ready(dasd_device_t*);
static inline void dasd_setup_partitions ( dasd_device_t *);
static inline void dasd_destroy_partitions ( dasd_device_t *);
static inline int dasd_setup_blkdev(dasd_device_t*);
static void dasd_deactivate_queue (dasd_device_t *);
static inline int dasd_disable_blkdev(dasd_device_t*);
static void dasd_flush_chanq ( dasd_device_t * device, int destroy ); 
static void dasd_flush_request_queues ( dasd_device_t * device, int destroy );
static struct block_device_operations dasd_device_operations;
static inline dasd_device_t ** dasd_device_from_devno (int);
static void dasd_process_queues (dasd_device_t * device);
/* SECTION: static variables of dasd.c */

static devfs_handle_t dasd_devfs_handle;
static wait_queue_head_t dasd_init_waitq;
static atomic_t dasd_init_pending = ATOMIC_INIT (0);

#ifdef CONFIG_DASD_DYNAMIC

/* SECTION: managing dynamic configuration of dasd_driver */

static struct list_head dasd_devreg_head = LIST_HEAD_INIT (dasd_devreg_head);

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

static dasd_range_t dasd_range_head =
    { list:LIST_HEAD_INIT (dasd_range_head.list) };
static spinlock_t range_lock = SPIN_LOCK_UNLOCKED;

/*
 * function: dasd_create_range
 * creates a dasd_range_t according to the arguments
 * FIXME: no check is performed for reoccurrence of a devno
 */
static inline dasd_range_t *
dasd_create_range (int from, int to, int features)
{
	dasd_range_t *range = NULL;
        int i;

	if ( from > to ) {
                printk (KERN_WARNING PRINTK_HEADER 
                        "Adding device range %04x-%04x: range invalid, ignoring.\n",
                        from,
                        to);

		return NULL;
	}
	for (i=from;i<=to;i++) {
                if (dasd_device_from_devno(i)) {
                        printk (KERN_WARNING PRINTK_HEADER 
                                "device range %04x-%04x: device %04x is already in a range.\n",
                                from,
                                to,
                                i);
                }
        }
	range = (dasd_range_t *) kmalloc (sizeof (dasd_range_t), GFP_KERNEL);
	if (range == NULL)
		return NULL;
	memset (range, 0, sizeof (dasd_range_t));
	range->from = from;
        range->to = to;
        range->features = features;
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
	long flags;

	spin_lock_irqsave (&range_lock, flags);
	list_add_tail (&range->list, &dasd_range_head.list);
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
	unsigned long flags;

	spin_lock_irqsave (&range_lock, flags);
	list_del (&range->list);
	spin_unlock_irqrestore (&range_lock, flags);
}

/*
 * function: dasd_add_range
 * creates a dasd_range_t according to the arguments and
 * appends it to the list of ranges
 * additionally a devreg_t is created and added to the list of devregs
 */
static inline dasd_range_t *
dasd_add_range (int from, int to, int features)
{
	dasd_range_t *range;

	range = dasd_create_range (from, to, features);
	if (!range)
		return NULL;

	dasd_append_range (range);
#ifdef CONFIG_DASD_DYNAMIC
	/* allocate and chain devreg infos for the devnos... */
	{
		int i;
		for (i = range->from; i <= range->to; i++) {
			dasd_devreg_t *reg = dasd_create_devreg (i);
			s390_device_register (&reg->devreg);
			list_add (&reg->list, &dasd_devreg_head);
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
				reg = list_entry (l, dasd_devreg_t, list);
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
	struct list_head *l;

	spin_lock_irqsave (&range_lock, flags);
	list_for_each (l, &dasd_range_head.list) {
		temp = list_entry (l, dasd_range_t, list);
		if (devno >= temp->from && devno <= temp->to) {
			spin_unlock_irqrestore (&range_lock, flags);
			return devindex + devno - temp->from;
		}
		devindex += temp->to - temp->from + 1;
	}
	spin_unlock_irqrestore (&range_lock, flags);
	return -ENODEV;
}

/*
 * function: dasd_devno_from_devindex
 */
static int
dasd_devno_from_devindex (int devindex)
{
	dasd_range_t *temp;
	unsigned long flags;
	struct list_head *l;

	spin_lock_irqsave (&range_lock, flags);
	list_for_each (l, &dasd_range_head.list) {
		temp = list_entry (l, dasd_range_t, list);
                if ( devindex < temp->to - temp->from + 1) {
			spin_unlock_irqrestore (&range_lock, flags);
			return temp->from + devindex;
		}
		devindex -= temp->to - temp->from + 1;
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
char *dasd[256];
char *dasd_disciplines[8];

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
	while (tmp != NULL && *tmp != '\0') {
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
		if (dasd[count] == NULL) {
			printk (KERN_WARNING PRINTK_HEADER
				"can't store dasd= parameter no %d\n",
				count + 1);
			break;
		}
		memset (dasd[count], 0, len * sizeof (char));
		memcpy (dasd[count], tmp, len * sizeof (char));
		count++;
		tmp = end;
	};
}

/*
 * dasd_parm_string holds a concatenated version of all 'dasd=' parameters
 * supplied in the parmline, which is later to be split by
 * dasd_split_parm_string
 * FIXME: why first concatenate then split ?
 */
static char dasd_parm_string[1024] __initdata = { 0, };

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
	dasd_setup (str, &dummy);
	return 1;
}

int __init
dasd_disciplines_setup (char *str)
{
	return 1;
}

__setup ("dasd=", dasd_call_setup);
__setup ("dasd_disciplines=", dasd_disciplines_setup);

#endif				/* MODULE */

/*
 * function: dasd_strtoul
 * provides a wrapper to simple_strtoul to strip leading '0x' and
 * interpret any argument to dasd=[range,...] as hexadecimal
 */
static inline int
dasd_strtoul (char *str, char **stra, int* features)
{
        char *temp=str;
        char *buffer;
        int val,i,start;

        buffer=(char*)kmalloc((strlen(str)+1)*sizeof(char),GFP_ATOMIC);
        if (buffer==NULL) {
            printk (KERN_WARNING PRINTK_HEADER
                    "can't parse dasd= parameter %s due to low memory\n",
                    str);
        }

        /* remove leading '0x' */
        if (*temp == '0') {
                temp++;         /* strip leading zero */
                if (*temp == 'x')
                        temp++; /* strip leading x */
        }

        /* copy device no to buffer and convert to decimal */
        for (i=0; temp[i]!='\0' && temp[i]!='(' && 
                  temp[i]!='-'  && temp[i]!=' '; i++){
                if (isxdigit(temp[i])) {
                        buffer[i]=temp[i];
                } else {
                        return -EINVAL;
                }
        }

        buffer[i]='\0';

        val = simple_strtoul (buffer, &buffer, 16);

        /* check for features - e.g. (ro) ; the '\0', ')' and '-' stops check */
        *features = DASD_DEFAULT_FEATURES;

        if (temp[i]=='(') {

                while (temp[i]!='\0' && temp[i]!=')'&&temp[i]!='-') { 
                        start=++i;      
        
                        /* move next feature to buffer */
                        for (;temp[i]!='\0'&&temp[i]!=':'&&temp[i]!=')'&&temp[i]!='-';i++)
                                buffer[i-start]=temp[i];
                        buffer[i-start]='\0';

                        if (strlen(buffer)) { 
                                if (!strcmp(buffer,"ro")) { /* handle 'ro' feature */
                                        (*features) |= DASD_FEATURE_READONLY;
                                        break;
                                }
                                printk (KERN_WARNING PRINTK_HEADER 
                                        "unsupported feature: %s, ignoring setting",
                                        buffer);
                        }
                }
        }

        *stra = temp+i;
        return val;
}

/*
 * function: dasd_parse
 * examines the strings given in the string array str and
 * creates and adds the ranges to the apropriate lists
 */
static int
dasd_parse (char **str)
{
	char *temp;
	int from, to;
        int features;
        int rc = 0;

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
			from = dasd_strtoul (temp, &temp, &features);
                        to = from;
			if (*temp == '-') {
				temp++;
				to = dasd_strtoul (temp, &temp, &features);
			}
                        if (from == -EINVAL ||
                            to   == -EINVAL    ) {
                                rc = -EINVAL;
                                break;
                        } else {
                                dasd_add_range (from, to ,features);
                        }
                }
		str++;
	}

        return rc;
}

/* SECTION: Dealing with devices registered to multiple major numbers */

static spinlock_t dasd_major_lock = SPIN_LOCK_UNLOCKED;

static major_info_t dasd_major_info[] = {
	{
	      list:LIST_HEAD_INIT (dasd_major_info[1].list)
	 },
	{
	      list:LIST_HEAD_INIT (dasd_major_info[0].list),
	      gendisk:{
	  INIT_GENDISK (94, DASD_NAME, DASD_PARTN_BITS, DASD_PER_MAJOR)
	  },
      flags:DASD_MAJOR_INFO_IS_STATIC}
};

static major_info_t *
get_new_major_info (void)
{
	major_info_t *major_info = NULL;

	major_info = kmalloc (sizeof (major_info_t), GFP_KERNEL);
	if (major_info) {
		static major_info_t temp_major_info = {
			gendisk:{
				 INIT_GENDISK (0, DASD_NAME, DASD_PARTN_BITS,
					       DASD_PER_MAJOR)}
		};
		memcpy (major_info, &temp_major_info, sizeof (major_info_t));
	}
	return major_info;
}

/*
 * register major number
 * is called with the 'static' major_info during init of the driver or 'NULL' to
 * allocate an additional dynamic major.
 */
static int
dasd_register_major (major_info_t * major_info)
{
	int rc = 0;
	int major;
	unsigned long flags;

        /* allocate dynamic major */
	if (major_info == NULL) {
		major_info = get_new_major_info ();
		if (!major_info) {
			printk (KERN_WARNING PRINTK_HEADER
				"Cannot get memory to allocate another major number\n");
			return -ENOMEM;
		}
	}

	major = major_info->gendisk.major;

        /* init devfs array */
	major_info->gendisk.de_arr = (devfs_handle_t *)
	    kmalloc (DASD_PER_MAJOR * sizeof (devfs_handle_t), GFP_KERNEL);
	memset (major_info->gendisk.de_arr, 0,
		DASD_PER_MAJOR * sizeof (devfs_handle_t));

        /* init flags */
	major_info->gendisk.flags = (char *)
	    kmalloc (DASD_PER_MAJOR * sizeof (char), GFP_KERNEL);
	memset (major_info->gendisk.flags, 0, DASD_PER_MAJOR * sizeof (char));

        /* register blockdevice */
	rc = devfs_register_blkdev (major, DASD_NAME, &dasd_device_operations);
	if (rc < 0) {
		printk (KERN_WARNING PRINTK_HEADER
			"Cannot register to major no %d, rc = %d\n",
                        major, 
                        rc);
		goto out_reg_blkdev; 
	} else {
		major_info->flags |= DASD_MAJOR_INFO_REGISTERED;
	}

	/* Insert the new major info into dasd_major_info if needed (dynamic major) */
	if (!(major_info->flags & DASD_MAJOR_INFO_IS_STATIC)) {
		spin_lock_irqsave (&dasd_major_lock, flags);
		list_add_tail (&major_info->list, &dasd_major_info[0].list);
		spin_unlock_irqrestore (&dasd_major_lock, flags);
	}

	if (major == 0) {
		major = rc;
		rc = 0;
	}

        /* init array of devices */
	major_info->dasd_device =
	    (dasd_device_t **) kmalloc (DASD_PER_MAJOR *
					sizeof (dasd_device_t *), GFP_ATOMIC);
	if (!major_info->dasd_device)
		goto out_devices;
	memset (major_info->dasd_device, 0,
		DASD_PER_MAJOR * sizeof (dasd_device_t *));

        /* init blk_size */
	blk_size[major] =
	    (int *) kmalloc ((1 << MINORBITS) * sizeof (int), GFP_ATOMIC);
	if (!blk_size[major])
		goto out_blk_size;
	memset (blk_size[major], 0, (1 << MINORBITS) * sizeof (int));

        /* init blksize_size */
	blksize_size[major] =
	    (int *) kmalloc ((1 << MINORBITS) * sizeof (int), GFP_ATOMIC);
	if (!blksize_size[major])
		goto out_blksize_size;
	memset (blksize_size[major], 0, (1 << MINORBITS) * sizeof (int));

        /* init_hardsect_size */
	hardsect_size[major] =
	    (int *) kmalloc ((1 << MINORBITS) * sizeof (int), GFP_ATOMIC);
	if (!hardsect_size[major])
		goto out_hardsect_size;
	memset (hardsect_size[major], 0, (1 << MINORBITS) * sizeof (int));

        /* init max_sectors */
	max_sectors[major] =
	    (int *) kmalloc ((1 << MINORBITS) * sizeof (int), GFP_ATOMIC);
	if (!max_sectors[major])
		goto out_max_sectors;
	memset (max_sectors[major], 0, (1 << MINORBITS) * sizeof (int));

	/* finally do the gendisk stuff */
	major_info->gendisk.part = kmalloc ((1 << MINORBITS) *
					    sizeof (struct hd_struct),
					    GFP_ATOMIC);
	if (!major_info->gendisk.part)
		goto out_gendisk;
	memset (major_info->gendisk.part, 0, (1 << MINORBITS) *
		sizeof (struct hd_struct));

	INIT_BLK_DEV (major, do_dasd_request, dasd_get_queue, NULL);

	major_info->gendisk.sizes = blk_size[major];
	major_info->gendisk.major = major;
	add_gendisk (&major_info->gendisk);
	return major;

        /* error handling - free the prior allocated memory */  
      out_gendisk:
	kfree (max_sectors[major]);
	max_sectors[major] = NULL;

      out_max_sectors:
	kfree (hardsect_size[major]);
	hardsect_size[major] = NULL;

      out_hardsect_size:
	kfree (blksize_size[major]);
	blksize_size[major] = NULL;

      out_blksize_size:
	kfree (blk_size[major]);
	blk_size[major] = NULL;

      out_blk_size:
	kfree (major_info->dasd_device);

      out_devices:
	/* Delete the new major info from dasd_major_info list if needed (dynamic) +*/
	if (!(major_info->flags & DASD_MAJOR_INFO_IS_STATIC)) {
		spin_lock_irqsave (&dasd_major_lock, flags);
		list_del (&major_info->list);
		spin_unlock_irqrestore (&dasd_major_lock, flags);
	}

        /* unregister blockdevice */
	rc = devfs_unregister_blkdev (major, DASD_NAME);
	if (rc < 0) {
		printk (KERN_WARNING PRINTK_HEADER
			"Unable to unregister from major no %d, rc = %d\n", 
                        major,
			rc);
	} else {
		major_info->flags &= ~DASD_MAJOR_INFO_REGISTERED;
	}

      out_reg_blkdev:
        kfree (major_info->gendisk.flags);
        kfree (major_info->gendisk.de_arr);

	/* Delete the new major info from dasd_major_info if needed */
	if (!(major_info->flags & DASD_MAJOR_INFO_IS_STATIC)) {
		kfree (major_info);
	}

	return -ENOMEM;
}

static int
dasd_unregister_major (major_info_t * major_info)
{
	int rc = 0;
	int major;
	unsigned long flags;

	if (major_info == NULL) {
		return -EINVAL;
	}
	major = major_info->gendisk.major;
	INIT_BLK_DEV (major, NULL, NULL, NULL);

	del_gendisk (&major_info->gendisk);

	kfree (major_info->dasd_device);
	kfree (major_info->gendisk.part);

	kfree (blk_size[major]);
	kfree (blksize_size[major]);
	kfree (hardsect_size[major]);
	kfree (max_sectors[major]);

	blk_size[major]      = NULL;
	blksize_size[major]  = NULL;
	hardsect_size[major] = NULL;
	max_sectors[major]   = NULL;

	rc = devfs_unregister_blkdev (major, DASD_NAME);
	if (rc < 0) {
		printk (KERN_WARNING PRINTK_HEADER
			"Cannot unregister from major no %d, rc = %d\n",
                        major,
			rc);
		return rc;
	} else {
		major_info->flags &= ~DASD_MAJOR_INFO_REGISTERED;
	}

	kfree (major_info->gendisk.flags);
	kfree (major_info->gendisk.de_arr);

	/* Delete the new major info from dasd_major_info if needed */
	if (!(major_info->flags & DASD_MAJOR_INFO_IS_STATIC)) {
		spin_lock_irqsave (&dasd_major_lock, flags);
		list_del (&major_info->list);
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
	list_for_each (l, &dasd_major_info[0].list) {
		major_info = list_entry (l, major_info_t, list);
		if (major_info->gendisk.major == MAJOR (kdev))
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
	list_for_each (l, &dasd_major_info[0].list) {
		major_info = list_entry (l, major_info_t, list);
		if (devindex < DASD_PER_MAJOR) {
			spin_unlock_irqrestore (&dasd_major_lock, flags);
			return &major_info->dasd_device[devindex];
		}
		devindex -= DASD_PER_MAJOR;
	}
	spin_unlock_irqrestore (&dasd_major_lock, flags);
	return NULL;
}

/*
 * function: dasd_features_from_devno
 * finds the device range corresponding to the devno
 * supplied as argument in the major_info structures and returns
 * the features set for it
 */

static int
dasd_features_from_devno (int devno)
{
	dasd_range_t *temp;
	int devindex = 0;
	unsigned long flags;
	struct list_head *l;

	spin_lock_irqsave (&range_lock, flags);
	list_for_each (l, &dasd_range_head.list) {
		temp = list_entry (l, dasd_range_t, list);
		if (devno >= temp->from && devno <= temp->to) {
			spin_unlock_irqrestore (&range_lock, flags);
			return temp->features;
		}
		devindex += temp->to - temp->from + 1;
	}
	spin_unlock_irqrestore (&range_lock, flags);
	return -ENODEV;
}



/* SECTION: managing dasd disciplines */

/* anchor and spinlock for list of disciplines */
static struct list_head dasd_disc_head = LIST_HEAD_INIT(dasd_disc_head);
static spinlock_t discipline_lock = SPIN_LOCK_UNLOCKED;

/*
 * function dasd_discipline_enq
 * chains the discpline given as argument to the head of disiplines
 * head chaining policy is required to allow module disciplines to
 * be preferred against those, who are statically linked
 */
static inline void
dasd_discipline_enq (dasd_discipline_t * d)
{
    list_add(&d->list, &dasd_disc_head);
}

/*
 * function dasd_discipline_deq
 * removes the discipline given as argument from the list of disciplines
 */
static inline void
dasd_discipline_deq (dasd_discipline_t * d)
{
        list_del(&d->list);
}

void
dasd_discipline_add (dasd_discipline_t * d)
{
        unsigned long flags;
        MOD_INC_USE_COUNT;
	spin_lock_irqsave (&discipline_lock,flags);
        dasd_discipline_enq (d);
	spin_unlock_irqrestore (&discipline_lock,flags);
        dasd_enable_ranges (&dasd_range_head, d, DASD_STATE_ONLINE);
}

void dasd_discipline_del (dasd_discipline_t * d)
{
        unsigned long flags;
	spin_lock_irqsave (&discipline_lock,flags);
        dasd_disable_ranges(&dasd_range_head, d, DASD_STATE_DEL, 1);
        dasd_discipline_deq (d);
	spin_unlock_irqrestore (&discipline_lock,flags);
        MOD_DEC_USE_COUNT;
}

static inline dasd_discipline_t *
dasd_find_disc (dasd_device_t * device, dasd_discipline_t *d)
{
        dasd_discipline_t *t;
        struct list_head *l = d ? &d->list : dasd_disc_head.next;
        do {
                t = list_entry(l,dasd_discipline_t,list);
                if ( ( t->id_check == NULL ||
                       t->id_check (&device->devinfo) == 0 ) &&
                     ( t->check_characteristics == NULL ||
                       t->check_characteristics (device) == 0 ) )
                        break;
                l = l->next;
                if ( d || 
                     l == &dasd_disc_head ) {
                        t = NULL;
                        break;
                }
         } while ( 1 );
	return t;
}

/* SECTION: profiling stuff */

static dasd_profile_info_t dasd_global_profile;

#ifdef DASD_PROFILE
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
	long strtime, irqtime, endtime, tottime; /* in microsecnds*/
	long tottimeps, sectors;
	dasd_device_t *device = cqr->device;

	if (!cqr->req)		/* safeguard against abnormal cqrs */
		return;

        if ((!cqr->buildclk) ||
            (!cqr->startclk) ||
            (!cqr->stopclk ) ||
            (!cqr->endclk  ) ||
            (!(sectors = ((struct request *) (cqr->req))->nr_sectors)))
                return;

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
	dasd_global_profile.dasd_io_sects+=sectors;
	device->profile.dasd_io_sects+=sectors;
	dasd_profile_add_counter (sectors, dasd_io_secs, device);
	dasd_profile_add_counter (tottime, dasd_io_times, device);
	dasd_profile_add_counter (tottimeps, dasd_io_timps, device);
	dasd_profile_add_counter (strtime, dasd_io_time1, device);
	dasd_profile_add_counter (irqtime, dasd_io_time2, device);
	dasd_profile_add_counter (irqtime / sectors, dasd_io_time2ps, device);
	dasd_profile_add_counter (endtime, dasd_io_time3, device);
}
#endif

/* SECTION: All the gendisk stuff */


/* SECTION: Managing wrappers for ccwcache */

/*
 * function dasd_alloc_request
 * tries to return space for a channel program of length cplength with
 * additional data of size datasize.
 * If the ccwcache cannot fulfill the request it tries the emergeny requests
 * before giving up finally
 * FIXME: initialization of ccw_req_t should be done by function of ccwcache
 */
ccw_req_t *
dasd_alloc_request (char *magic, int cplength, int datasize, dasd_device_t* device)
{
	ccw_req_t *rv = NULL;

	if ((rv = ccw_alloc_request (magic, cplength, datasize)) != NULL) {
		return rv;
	}
	if ((((sizeof (ccw_req_t) + 7) & -8) +
	     cplength * sizeof (ccw1_t) + datasize) > PAGE_SIZE) {
		BUG ();
		}
        if (device->lowmem_cqr==NULL) {
                DASD_DRIVER_DEBUG_EVENT (2, dasd_alloc_request,
                                         "(%04x) Low memory! Using emergency request %p.",
                                         device->devinfo.devno,
                                         device->lowmem_ccws);

                device->lowmem_cqr=device->lowmem_ccws;
                rv = device->lowmem_ccws;
		memset (rv, 0, PAGE_SIZE);
		strncpy ((char *) (&rv->magic), magic, 4);
		ASCEBC ((char *) (&rv->magic), 4);
		rv->cplength = cplength;
		rv->datasize = datasize;
		rv->data = (void *) ((long) rv + PAGE_SIZE - datasize);
		rv->cpaddr = (ccw1_t *) ((long) rv + sizeof (ccw_req_t));
        } else {
                DASD_DRIVER_DEBUG_EVENT (2, dasd_alloc_request,
                                         "(%04x) Refusing emergency mem for request "
                                         "NULL, already in use at %p.",
                                         device->devinfo.devno,
                                         device->lowmem_ccws);
	}
	return rv;
}

/*
 * function dasd_free_request
 * returns a ccw_req_t to the appropriate cache or emergeny request line
 */
void
dasd_free_request (ccw_req_t * request, dasd_device_t* device)
{
#ifdef CONFIG_ARCH_S390X
        ccw1_t* ccw;
        /* clear any idals used for chain */
        ccw=request->cpaddr-1;
        do {
                ccw++;
                if ((ccw->cda < (unsigned long) device->lowmem_idals           ) || 
                    (ccw->cda >= (unsigned long) device->lowmem_idals+PAGE_SIZE)   )
                        clear_normalized_cda (ccw);
                else {
                        if (device->lowmem_idal_ptr != device->lowmem_idals)
                                DASD_MESSAGE (KERN_WARNING, device,
                                              "Freeing emergency idals from request at %p.",
                                              request);
                        device->lowmem_idal_ptr = device->lowmem_idals;
                        device->lowmem_cqr=NULL;
                }
        } while ((ccw->flags & CCW_FLAG_CC) || 
                 (ccw->flags & CCW_FLAG_DC)   );
#endif
        if (request != device->lowmem_ccws) { 
                /* compare to lowmem_ccws to protect usage of lowmem_cqr for IDAL only ! */
		ccw_free_request (request);
        } else {
                DASD_MESSAGE (KERN_WARNING, device,
                              "Freeing emergency request at %p",
                              request);
                device->lowmem_cqr=NULL;
	}
}

int
dasd_set_normalized_cda (ccw1_t * cp, unsigned long address, 
                         ccw_req_t* request, dasd_device_t* device )
{
#ifdef CONFIG_ARCH_S390X
	int nridaws;
        int count = cp->count;
        
        if (set_normalized_cda (cp, address)!=-ENOMEM) {
                return 0;
        }

        if ((device->lowmem_cqr!=NULL) && (device->lowmem_cqr!=request)) {
                DASD_MESSAGE (KERN_WARNING, device, 
                              "Refusing emergency idals for request %p, memory"
                              " is already in use for request %p",
                              request,
                              device->lowmem_cqr);
                return -ENOMEM;
        }
        device->lowmem_cqr=request;
        if (device->lowmem_idal_ptr == device->lowmem_idals) {
            DASD_MESSAGE (KERN_WARNING,device, 
                          "Low memory! Using emergency IDALs for request %p.\n",
                          request);
        }
        nridaws = ((address & (IDA_BLOCK_SIZE-1)) + count + 
		   (IDA_BLOCK_SIZE-1)) >> IDA_SIZE_LOG;
	if ( device->lowmem_idal_ptr>=device->lowmem_idals + PAGE_SIZE ) {
		/* Ouch! No Idals left for emergency request */
		BUG();
	}
	cp->flags |= CCW_FLAG_IDA;
	cp->cda = (__u32)(unsigned long)device->lowmem_idal_ptr;
        do {
		*((long*)device->lowmem_idal_ptr) = address;
		address = (address & -(IDA_BLOCK_SIZE)) + (IDA_BLOCK_SIZE);
		nridaws --;
                device->lowmem_idal_ptr += sizeof(unsigned long);
        } while ( nridaws > 0 );
#else 
        cp -> cda = address;
#endif
	return 0;
}


/* SECTION: (de)queueing of requests to channel program queues */

/*
 * function dasd_chanq_enq
 * appends the cqr given as argument to the queue
 * has to be called with the queue lock (namely the s390_irq_lock) acquired
 */
inline void
dasd_chanq_enq (dasd_chanq_t * q, ccw_req_t * cqr)
{
	if (q->head != NULL) {
		q->tail->next = cqr;
	} else
		q->head = cqr;
	cqr->next = NULL;
	q->tail = cqr;
	check_then_set (&cqr->status, 
                        CQR_STATUS_FILLED, 
                        CQR_STATUS_QUEUED);

       
#ifdef DASD_PROFILE
        /* save profile information for non erp cqr */
        if (cqr->refers == NULL) {
                unsigned int  counter = 0;
                ccw_req_t     *ptr;
                dasd_device_t *device = cqr->device;

                /* count the length of the chanq for statistics */
                for (ptr = q->head; 
                     ptr->next != NULL && counter <=31; 
                     ptr = ptr->next) {
                        counter++;
                }                
                
                dasd_global_profile.dasd_io_nr_req[counter]++;
                device->profile.dasd_io_nr_req[counter]++;
        }
#endif 
}

/*
 * function dasd_chanq_enq_head
 * chains the cqr given as argument to the queue head
 * has to be called with the queue lock (namely the s390_irq_lock) acquired
 */
inline void
dasd_chanq_enq_head (dasd_chanq_t * q, ccw_req_t * cqr)
{
	cqr->next = q->head;
	q->head = cqr;
	if (q->tail == NULL)
		q->tail = cqr;
	check_then_set (&cqr->status, CQR_STATUS_FILLED, CQR_STATUS_QUEUED);
}

/*
 * function dasd_chanq_deq
 * dechains the cqr given as argument from the queue
 * has to be called with the queue lock (namely the s390_irq_lock) acquired
 */
inline void
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
			return;
		prev->next = cqr->next;
		if (prev->next == NULL)
			q->tail = prev;
	}
	cqr->next = NULL;
}

/* SECTION: Managing the device queues etc. */

/*
 * DASD_TERM_IO
 *
 * attempts to terminate the the current IO and set it to failed if termination
 * was successful.
 * returns an appropriate return code
 */
int
dasd_term_IO (ccw_req_t * cqr)
{
	int rc = 0;
	dasd_device_t *device = cqr->device;
	int irq;
        int retries = 0;

	if (!cqr) {
		BUG ();
	}
	irq = device->devinfo.irq;
	if (strncmp ((char *) &cqr->magic, device->discipline->ebcname, 4)) {
		DASD_MESSAGE (KERN_WARNING, device,
			      " ccw_req_t 0x%08x magic doesn't match"
			      " discipline 0x%08x\n",
			      cqr->magic,
			      *(unsigned int *) device->discipline->name);
		return -EINVAL;
	}
        
        while ((retries < 5                    ) &&
               (cqr->status == CQR_STATUS_IN_IO)   ) {

                if ( retries < 2 )
                        rc = halt_IO(irq, (long)cqr, 
                                     cqr->options | DOIO_WAIT_FOR_INTERRUPT);
                else
                        rc = clear_IO(irq, (long)cqr, 
                                      cqr->options | DOIO_WAIT_FOR_INTERRUPT);

                switch (rc) {
                case 0:         /* termination successful */
                        check_then_set (&cqr->status,
                                        CQR_STATUS_IN_IO, 
                                        CQR_STATUS_FAILED);
                        
                        asm volatile ("STCK %0":"=m" (cqr->stopclk));
                        break;
                case -ENODEV:
                        DASD_MESSAGE (KERN_WARNING, device, "%s",
                                      "device gone, retry\n");
                        break;
                case -EIO:
                        DASD_MESSAGE (KERN_WARNING, device, "%s",
                                      "I/O error, retry\n");
                        break;
                case -EBUSY:
                        DASD_MESSAGE (KERN_WARNING, device, "%s",
                                      "device busy, retry later\n");
                        break;
                default:
                        DASD_MESSAGE (KERN_ERR, device,
                                      "line %d unknown RC=%d, please report"
                                      " to linux390@de.ibm.com\n", 
                                      __LINE__, 
                                      rc);
                        BUG ();
                        break;
                }

                retries ++;
        }
	return rc;
}

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
			      " ccw_req_t 0x%08x magic doesn't match"
			      " discipline 0x%08x\n",
			      cqr->magic,
			      *(unsigned int *) device->discipline->name);
		return -EINVAL;
	}

	asm volatile ("STCK %0":"=m" (now));
        cqr->startclk = now;

	rc = do_IO (irq, cqr->cpaddr, (long) cqr, cqr->lpm, cqr->options);

	switch (rc) {
	case 0:
                if (cqr->options & DOIO_WAIT_FOR_INTERRUPT) {
                        /* request already finished (synchronous IO) */
                        DASD_MESSAGE (KERN_ERR, device, "%s",
                                      " do_IO finished request... "
                                      "DOIO_WAIT_FOR_INTERRUPT was set");
                        check_then_set (&cqr->status,
                                        CQR_STATUS_QUEUED, 
                                        CQR_STATUS_DONE);

                        cqr->stopclk = now;
                        dasd_schedule_bh (device);
                        
                } else {
                        check_then_set (&cqr->status,
                                        CQR_STATUS_QUEUED, 
                                        CQR_STATUS_IN_IO);
                }
		break;
	case -EBUSY:
		DASD_MESSAGE (KERN_WARNING, device, "%s",
			      "device busy, retry later\n");
		break;
	case -ETIMEDOUT: 
		DASD_MESSAGE (KERN_WARNING, device, "%s",
			      "request timeout - terminated\n");
	case -ENODEV:
	case -EIO:
		check_then_set (&cqr->status,
				CQR_STATUS_QUEUED, 
                                CQR_STATUS_FAILED);

                cqr->stopclk = now;
                dasd_schedule_bh (device);
		break;
	default:
		DASD_MESSAGE (KERN_ERR, device,
			      "line %d unknown RC=%d, please report"
			      " to linux390@de.ibm.com\n", __LINE__, rc);
		BUG ();
		break;
	}

	return rc;
}

/*
 * function dasd_sleep_on_req
 * attempts to start the IO and waits for completion
 * FIXME: replace handmade sleeping by wait_event
 */
int
dasd_sleep_on_req (ccw_req_t * req)
{
	unsigned long flags;
	int cs;
	int rc = 0;
	dasd_device_t *device = (dasd_device_t *) req->device;

        if ( signal_pending(current) ) {
                return -ERESTARTSYS;
        }
	s390irq_spin_lock_irqsave (device->devinfo.irq, flags);
	dasd_chanq_enq (&device->queue, req);
	/* let the bh start the request to keep them in order */
	dasd_schedule_bh (device);
	do {
		s390irq_spin_unlock_irqrestore (device->devinfo.irq, flags);
		wait_event ( device->wait_q,
			     (((cs = req->status) == CQR_STATUS_DONE) ||
			     (cs == CQR_STATUS_FAILED) ||
                             signal_pending(current)));
		s390irq_spin_lock_irqsave (device->devinfo.irq, flags);
                if ( signal_pending(current) ) {
                        rc = -ERESTARTSYS;
		     	if (req->status == CQR_STATUS_IN_IO ) 
                        	device->discipline->term_IO(req);
                        break;
                } else if ( req->status == CQR_STATUS_FAILED) {
                        rc = -EIO;
                        break;
                }
	} while (cs != CQR_STATUS_DONE && cs != CQR_STATUS_FAILED);
	s390irq_spin_unlock_irqrestore (device->devinfo.irq, flags);
	return rc;
}				/* end dasd_sleep_on_req */

/*
 * function dasd_end_request
 * posts the buffer_cache about a finalized request
 * FIXME: for requests splitted to serveral cqrs
 */
static inline void
dasd_end_request (struct request *req, int uptodate)
{
	while (end_that_request_first (req, uptodate, DASD_NAME)) {
	}
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

	if (!device) {
		return NULL;
	}

	return device->request_queue;
}

/*
 * function dasd_check_expire_time
 * check the request given as argument for expiration
 * and returns 0 if not yet expired, EIO else
 */
static inline int
dasd_check_expire_time (ccw_req_t * cqr)
{
	unsigned long long now;
	int rc = 0;

	asm volatile ("STCK %0":"=m" (now));
	if (cqr->expires && cqr->expires + cqr->startclk < now) {
		DASD_MESSAGE (KERN_ERR, ((dasd_device_t *) cqr->device),
			      "IO timeout 0x%08lx%08lx usecs in req %p\n",
			      (long) (cqr->expires >> 44),
			      (long) (cqr->expires >> 12), cqr);
		cqr->expires <<= 1;
                rc = -EIO;
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
#ifdef DASD_PROFILE
		dasd_profile_add (cqr);
#endif
		dasd_end_request (cqr->req, (cqr->status == CQR_STATUS_DONE));
		/* free request if nobody is waiting on it */
		dasd_free_request (cqr, cqr->device);
	} else {
                if ( cqr == device->init_cqr && /* bring late devices online */
                     device->level <= DASD_STATE_ONLINE ) { 
                        device->timer.function = dasd_enable_single_device; 
                        device->timer.data     = (unsigned long) device;
                        device->timer.expires  = jiffies;
                        add_timer(&device->timer);
                }
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
dasd_process_queues (dasd_device_t * device)
{
	unsigned long flags;
	struct request *req;
	request_queue_t *queue = device->request_queue;
	dasd_chanq_t *qp = &device->queue;
	int irq = device->devinfo.irq;
	ccw_req_t *final_requests = NULL;
	static int chanq_min_size = DASD_MIN_SIZE_FOR_QUEUE;
	int chanq_max_size = DASD_CHANQ_MAX_SIZE;
	ccw_req_t *cqr = NULL, *temp;
	dasd_erp_postaction_fn_t erp_postaction;


	s390irq_spin_lock_irqsave (irq, flags);

	/* First we dechain the requests, processed with completed status */
	while (qp->head &&
	       ((qp->head->status == CQR_STATUS_DONE  ) ||
		(qp->head->status == CQR_STATUS_FAILED) ||
		(qp->head->status == CQR_STATUS_ERROR )   )) {

		dasd_erp_action_fn_t erp_action;
		ccw_req_t            *erp_cqr = NULL;

		/*  preprocess requests with CQR_STATUS_ERROR */
		if (qp->head->status == CQR_STATUS_ERROR) {

                        qp->head->retries--; 

			if (qp->head->dstat->flag & DEVSTAT_HALT_FUNCTION) {

                                check_then_set (&qp->head->status,
                                                CQR_STATUS_ERROR,
                                                CQR_STATUS_FAILED);

                                asm volatile ("STCK %0":"=m" (qp->head->stopclk));

                        } else if ((device->discipline->erp_action == NULL                          ) ||
                                   ((erp_action = device->discipline->erp_action (qp->head)) == NULL)   ) {
                                
				erp_cqr = dasd_default_erp_action (qp->head);

			} else { /* call discipline ERP action */

                                erp_cqr = erp_action (qp->head);
                        }
                        continue;

		} else if (qp->head->refers) {	/* we deal with a finished ERP */

			if (qp->head->status == CQR_STATUS_DONE) {

                                DASD_MESSAGE (KERN_DEBUG, device, "%s",
                                              "ERP successful");
			} else {

                                DASD_MESSAGE (KERN_ERR, device, "%s",
                                              "ERP unsuccessful");
			}

			if ((device->discipline->erp_postaction == NULL                              )||
			    ((erp_postaction = device->discipline->erp_postaction (qp->head)) == NULL)  ) {

                                dasd_default_erp_postaction (qp->head);

			} else {  /* call ERP postaction of discipline */

                                erp_postaction (qp->head);
                        }

			continue;
		}

		/* dechain request now */
		if (final_requests == NULL)
			final_requests = qp->head;

		cqr      = qp->head;
		qp->head = qp->head->next;

		if (qp->head == NULL)
			qp->tail = NULL;

	} /* end while over completed requests */

	if (cqr)
		cqr->next = NULL;
	/* Now clean the requests with final status */
	while (final_requests) { 
		temp = final_requests;
		final_requests = temp->next;
		dasd_finalize_request (temp);
	}
	/* Now we try to fetch requests from the request queue */
	for (temp = cqr; temp != NULL; temp = temp->next)
		if (temp->status == CQR_STATUS_QUEUED)
			chanq_max_size--;
	while ((atomic_read(&device->plugged) == 0) &&
               (!queue->plugged) &&
	       (!list_empty (&queue->queue_head)) &&
	       (req = dasd_next_request (queue)) != NULL) {
		/* queue empty or certain critera fulfilled -> transfer */
		if (qp->head == NULL ||
		    chanq_max_size > 0 || (req->nr_sectors >= chanq_min_size)) {
			ccw_req_t *cqr = NULL;
                        if (is_read_only(device->kdev) && req->cmd == WRITE) {

                                DASD_DRIVER_DEBUG_EVENT (3, dasd_int_handler,
                                                         "(%04x) Rejecting write request %p\n",
                                                         device->devinfo.devno,
                                                         req);

                                dasd_end_request (req, 0);
                                dasd_dequeue_request (queue,req);
                        } else {
                            /* relocate request according to partition table */
                            req->sector +=
                                device->major_info->gendisk.
                                part[MINOR (req->rq_dev)].start_sect;
                            cqr = device->discipline->build_cp_from_req (device, req);
                            if (cqr == NULL) {

                                    DASD_DRIVER_DEBUG_EVENT (3, dasd_int_handler,
                                                             "(%04x) CCW creation failed "
                                                             "on request %p\n",
                                                             device->devinfo.devno,
                                                             req);
                                    /* revert relocation of request */
                                    req->sector -=
                                        device->major_info->gendisk.
                                        part[MINOR (req->rq_dev)].start_sect;
                                    break;	/* terminate request queue loop */
                                    
                            }
#ifdef CONFIG_DYNAMIC_QUEUE_MIN_SIZE
                            chanq_min_size =
                                (chanq_min_size + req->nr_sectors) >> 1;
#endif				/* CONFIG_DYNAMIC_QUEUE_MIN_SIZE */
                            dasd_dequeue_request (queue, req);
                            dasd_chanq_enq (qp, cqr);
                        }
		} else {	/* queue not empty OR criteria not met */
			break;	/* terminate request queue loop */
		}
	}
	/* we process the requests with non-final status */
	if (qp->head) {
		switch (qp->head->status) {
		case CQR_STATUS_QUEUED:
			/* try to start the first I/O that can be started */
			if (device->discipline->start_IO == NULL)
				BUG ();
                        device->discipline->start_IO(qp->head);
			break;
		case CQR_STATUS_IN_IO:
			/* Check, if to invoke the missing interrupt handler */
			if (dasd_check_expire_time (qp->head)) {
				/* to be filled with MIH */
			}
			break;

		case CQR_STATUS_PENDING:
			/* just wait */
			break;
		default:
			BUG ();
		}
	}
	s390irq_spin_unlock_irqrestore (irq, flags);

} /* end dasd_process_queues */

/*
 * function dasd_run_bh
 * acquires the locks needed and then runs the bh
 */
static void
dasd_run_bh (dasd_device_t * device)
{
	long flags;
	spin_lock_irqsave (&io_request_lock, flags);
	atomic_set (&device->bh_scheduled, 0);
	dasd_process_queues (device);
	spin_unlock_irqrestore (&io_request_lock, flags);
}

/*
 * function dasd_schedule_bh
 * schedules the request_fn to run with next run_bh cycle
 */
void
dasd_schedule_bh (dasd_device_t * device)
{
	/* Protect against rescheduling, when already running */
	if (atomic_compare_and_swap (0, 1, &device->bh_scheduled)) {
		return;
	}

	INIT_LIST_HEAD (&device->bh_tq.list);
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
static void
do_dasd_request (request_queue_t * queue)
{
        dasd_device_t *device = (dasd_device_t *)queue->queuedata;
	dasd_process_queues (device);
}

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
dasd_handle_state_change_pending (devstat_t * stat)
{
	dasd_device_t **device_addr;
	ccw_req_t *cqr;

	device_addr = dasd_device_from_devno (stat->devno);

	if (device_addr == NULL) {

		printk (KERN_DEBUG PRINTK_HEADER
			"unable to find device for state change pending "
			"interrupt: devno%04x\n", 
                        stat->devno);
                return;
	} 

        /* re-activate first request in queue */
        cqr = (*device_addr)->queue.head;
        
        if (cqr->status == CQR_STATUS_PENDING) {
                
                DASD_MESSAGE (KERN_DEBUG, (*device_addr), "%s",
                              "device request queue restarted by "
                              "state change pending interrupt\n");
                
                del_timer (&(*device_addr)->timer);
                
                check_then_set (&cqr->status,
                                CQR_STATUS_PENDING, CQR_STATUS_QUEUED);
                
                dasd_schedule_bh (*device_addr);
                
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
	ccw_req_t *cqr;
	dasd_device_t *device;
        unsigned long long now;
	dasd_era_t era = dasd_era_none; /* default is everything is okay */
	devstat_t *stat = (devstat_t *)ds;

        DASD_DRIVER_DEBUG_EVENT (6, dasd_int_handler,
                                 "Interrupt: IRQ %02x, stat %02x, devno %04x",
                                 irq,
                                 stat->dstat,
                                 stat->devno);
        asm volatile ("STCK %0":"=m" (now));
        if (stat == NULL) {
                BUG();
	}

        /* first of all check for state change pending interrupt */
        if ((stat->dstat & DEV_STAT_ATTENTION ) && 
            (stat->dstat & DEV_STAT_DEV_END   ) &&
            (stat->dstat & DEV_STAT_UNIT_EXCEP)   ) {
                DASD_DRIVER_DEBUG_EVENT (2, dasd_int_handler,
                                         "State change Interrupt: %04x",
                                         stat->devno);
                dasd_handle_state_change_pending (stat);
                return;
        }

	ip = stat->intparm;
	if (!ip) {		/* no intparm: unsolicited interrupt */
                DASD_DRIVER_DEBUG_EVENT (2, dasd_int_handler,
                                         "Unsolicited Interrupt: %04x",
                                         stat->devno);
		printk (KERN_DEBUG PRINTK_HEADER
                        "unsolicited interrupt: irq 0x%x devno %04x\n",
                        irq,
                        stat->devno);
		return;
	}
	if (ip & 0x80000001) {
                DASD_DRIVER_DEBUG_EVENT (2, dasd_int_handler,
                                         "spurious Interrupt: %04x",
                                         stat->devno);
		printk (KERN_DEBUG PRINTK_HEADER
                        "spurious interrupt: irq 0x%x devno %04x, parm %08x\n",
                        irq,
                        stat->devno,ip);
		return;
	}

	cqr = (ccw_req_t *)(long)ip;

        /* check status - the request might have been killed because of dyn dettach */
	if (cqr->status != CQR_STATUS_IN_IO) {
                DASD_DRIVER_DEBUG_EVENT (2, dasd_int_handler,
                                         "invalid status %02x on device %04x",
                                         cqr->status,
                                         stat->devno);

		printk (KERN_DEBUG PRINTK_HEADER
                        "invalid status: irq 0x%x devno %04x, status %02x\n",
                        irq,
                        stat->devno,
                        cqr->status);
		return;
	}

	device = (dasd_device_t *) cqr->device;
	if (device == NULL || 
            device != ds-offsetof(dasd_device_t,dev_status)) {
                BUG();
	}
	if (device->devinfo.irq != irq) {
                BUG();
	}
	if (strncmp (device->discipline->ebcname, (char *) &cqr->magic, 4)) {
                BUG();
	}

        /* first of all lets try to find out the appropriate era_action */
        DASD_DEVICE_DEBUG_EVENT (4, device," Int: CS/DS 0x%04x",
                                 ((stat->cstat<<8)|stat->dstat));

	/* first of all lets try to find out the appropriate era_action */
	if (stat->flag & DEVSTAT_FLAG_SENSE_AVAIL ||
	    stat->dstat & ~(DEV_STAT_CHN_END | DEV_STAT_DEV_END)) {
		/* anything abnormal ? */
		if (device->discipline->examine_error == NULL ||
		    stat->flag & DEVSTAT_HALT_FUNCTION) {
			era = dasd_era_fatal;
		} else {
			era = device->discipline->examine_error (cqr, stat);
		}
                DASD_DRIVER_DEBUG_EVENT (1, dasd_int_handler," era_code %d",
                                         era);
	}
        if ( era == dasd_era_none ) {
                check_then_set(&cqr->status, 
                               CQR_STATUS_IN_IO, 
                               CQR_STATUS_DONE);

                cqr->stopclk=now;
		/* start the next queued request if possible -> fast_io */
                if (cqr->next &&
                    cqr->next->status == CQR_STATUS_QUEUED) {
                        if (device->discipline->start_IO (cqr->next) != 0) {
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

#ifdef ERP_DEBUG
		/* dump sense data */
		if (device->discipline            && 
                    device->discipline->dump_sense  ) {

                        device->discipline->dump_sense (device, 
                                                        cqr);
		}
#endif

		switch (era) {
		case dasd_era_fatal:
			check_then_set (&cqr->status, 
                                        CQR_STATUS_IN_IO,
					CQR_STATUS_FAILED);

                        cqr->stopclk = now;
			break;
		case dasd_era_recover:
			check_then_set (&cqr->status, 
                                        CQR_STATUS_IN_IO,
					CQR_STATUS_ERROR);
			break;
		default:
			BUG ();
		}
	}
        if ( cqr == device->init_cqr &&
             ( cqr->status == CQR_STATUS_DONE ||
               cqr->status == CQR_STATUS_FAILED )){
                dasd_state_init_to_ready(device);
                if ( atomic_read(&dasd_init_pending) == 0)
                        wake_up (&dasd_init_waitq);
        }
	dasd_schedule_bh (device);

} /* end dasd_int_handler */

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
dasd_default_erp_action (ccw_req_t * cqr)
{

        dasd_device_t *device = cqr->device;
	ccw_req_t     *erp    = dasd_alloc_request ((char *) &cqr->magic, 1, 0, cqr->device);

	printk (KERN_DEBUG PRINTK_HEADER "Default ERP called... \n");

	if (!erp) {

                DASD_MESSAGE (KERN_ERR, device, "%s",
                              "Unable to allocate ERP request");
                
                check_then_set (&cqr->status,
                                CQR_STATUS_ERROR,
                                CQR_STATUS_FAILED);

                asm volatile ("STCK %0":"=m" (cqr->stopclk));

                return cqr;
	}

	erp->cpaddr->cmd_code = CCW_CMD_TIC;
	erp->cpaddr->cda = (__u32) (addr_t) cqr->cpaddr;
	erp->function = dasd_default_erp_action;
	erp->refers = cqr;
	erp->device = cqr->device;
	erp->magic = cqr->magic;
	erp->retries = 16;

	erp->status = CQR_STATUS_FILLED;

        dasd_chanq_enq_head (&device->queue,
                             erp);

	return erp;

} /* end dasd_default_erp_action */

/*
 * DEFAULT_ERP_POSTACTION
 *
 * DESCRIPTION
 *   Frees all ERPs of the current ERP Chain and set the status
 *   of the original CQR either to CQR_STATUS_DONE if ERP was successful
 *   or to CQR_STATUS_FAILED if ERP was NOT successful.
 *   NOTE: This function is only called if no discipline postaction
 *         is available
 *
 * PARAMETER
 *   erp                current erp_head
 *
 * RETURN VALUES
 *   cqr                pointer to the original CQR
 */
ccw_req_t *
dasd_default_erp_postaction (ccw_req_t *erp)
{

	ccw_req_t     *cqr      = NULL, 
                      *free_erp = NULL;
	dasd_device_t *device   = erp->device;
	int           success;

	if (erp->refers   == NULL || 
            erp->function == NULL   ) {

		BUG ();
	}

	if (erp->status == CQR_STATUS_DONE)
		success = 1;
	else
		success = 0;

	/* free all ERPs - but NOT the original cqr */
	while (erp->refers != NULL) {

		free_erp = erp;
		erp      = erp->refers;

		/* remove the request from the device queue */
		dasd_chanq_deq (&device->queue,
                                free_erp);

		/* free the finished erp request */
		dasd_free_request (free_erp, free_erp->device);
	}

	/* save ptr to original cqr */
	cqr = erp;

	/* set corresponding status to original cqr */
	if (success) {

		check_then_set (&cqr->status, 
                                CQR_STATUS_ERROR,
				CQR_STATUS_DONE);
	} else {

		check_then_set (&cqr->status,
				CQR_STATUS_ERROR, 
                                CQR_STATUS_FAILED);

                asm volatile ("STCK %0":"=m" (cqr->stopclk));
	}

	return cqr;

} /* end default_erp_postaction */

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
	int openct = atomic_read (&device->open_count);

	if (openct > 1) {
		DASD_MESSAGE (KERN_WARNING, device, "%s",
			      "dasd_format: device is open! expect errors.");
	}
	DASD_MESSAGE (KERN_INFO, device,
		      "formatting units %d to %d (%d B blocks) flags %d",
		      fdata->start_unit, 
                      fdata->stop_unit,
		      fdata->blksize, 
                      fdata->intensity);
	while ((!rc) && (fdata->start_unit <= fdata->stop_unit)) {
                ccw_req_t *req;
                dasd_format_fn_t ffn = device->discipline->format_device;
		ffn = device->discipline->format_device;
		if (ffn == NULL)
			break;
		req = ffn (device, fdata);
		if (req == NULL) {
			rc = -ENOMEM;
			break;
		}
		if ((rc = dasd_sleep_on_req (req)) != 0) {
			DASD_MESSAGE (KERN_WARNING, device,
				      " Formatting of unit %d failed with rc = %d\n",
				      fdata->start_unit, rc);
			break;
		} 
		dasd_free_request (req, device);	/* request is no longer used */
	        if ( signal_pending(current) ) {
			rc = -ERESTARTSYS;
			break;		
                }
		fdata->start_unit++;
	}
	return rc;
}				/* end dasd_format */

static struct list_head dasd_ioctls = LIST_HEAD_INIT (dasd_ioctls);

static dasd_ioctl_list_t *
dasd_find_ioctl (int no)
{
	struct list_head *curr;
	list_for_each (curr, &dasd_ioctls) {
		if (list_entry (curr, dasd_ioctl_list_t, list)->no == no) {
			return list_entry (curr, dasd_ioctl_list_t, list);
		}
	}
	return NULL;
}

int
dasd_ioctl_no_register (struct module *owner, int no, dasd_ioctl_fn_t handler)
{
	dasd_ioctl_list_t *new;
	if (dasd_find_ioctl (no))
		return -EBUSY;
	new = kmalloc (sizeof (dasd_ioctl_list_t), GFP_KERNEL);
	if (new == NULL)
		return -ENOMEM;
	new->owner = owner;
	new->no = no;
	new->handler = handler;
	list_add (&new->list, &dasd_ioctls);
	MOD_INC_USE_COUNT;
	return 0;
}

int
dasd_ioctl_no_unregister (struct module *owner, int no, dasd_ioctl_fn_t handler)
{
	dasd_ioctl_list_t *old = dasd_find_ioctl (no);
	if (old == NULL)
		return -ENOENT;
	if (old->no != no || old->handler != handler || owner != old->owner )
		return -EINVAL;
	list_del (&old->list);
	kfree (old);
	MOD_DEC_USE_COUNT;
	return 0;
}

static int
dasd_revalidate (dasd_device_t * device)
{
        int rc = 0;
	int i;
	kdev_t kdev = device->kdev;
	int openct = atomic_read (&device->open_count);
	int start = MINOR (kdev);
	if (openct != 1) {
		DASD_MESSAGE (KERN_WARNING, device, "%s",
			      "BLKRRPART: device is open! expect errors.");
	}
	for (i = (1 << DASD_PARTN_BITS) - 1; i >= 0; i--) {
                int major = device->major_info->gendisk.major;
		invalidate_device(MKDEV (major, start+i), 1);
	}
        dasd_destroy_partitions(device);
        dasd_setup_partitions(device);
        return rc;

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
			MAJOR (inp->i_rdev), 
                        MINOR (inp->i_rdev));
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
		" devno 0x%04x on irq %d) with data %8lx\n",
		no,
		_IOC_DIR (no) == _IOC_NONE ? "0" :
		_IOC_DIR (no) == _IOC_READ ? "r" :
		_IOC_DIR (no) == _IOC_WRITE ? "w" :
		_IOC_DIR (no) == (_IOC_READ | _IOC_WRITE) ? "rw" : "u",
		_IOC_TYPE (no),
                _IOC_NR (no),
                _IOC_SIZE (no),
		device->name, 
                MAJOR (inp->i_rdev), 
                MINOR (inp->i_rdev),
		device->devinfo.devno, 
                device->devinfo.irq, 
                data);
#endif
	switch (no) {
        case DASDAPIVER: {
			int ver = DASD_API_VERSION;
			rc = put_user(ver, (int *) data);
			break;
        }
	case BLKRRPART:{
			if (!capable (CAP_SYS_ADMIN)) {
				rc = -EACCES;
				break;
			}
			rc = dasd_revalidate (device);
			break;
		}
	case HDIO_GETGEO:{
			struct hd_geometry geo = { 0, };
			rc = dasd_fillgeo (inp->i_rdev, &geo);
			if (rc)
				break;

			rc = copy_to_user ((struct hd_geometry *) data, &geo,
					   sizeof (struct hd_geometry));
			if (rc)
				rc = -EFAULT;
			break;
		}
	case BIODASDDISABLE:{
			if (!capable (CAP_SYS_ADMIN)) {
				rc = -EACCES;
				break;
			}
                        if ( device->level > DASD_STATE_ACCEPT) {
                                dasd_deactivate_queue(device);
                                if ( device->request_queue)
                                        dasd_flush_request_queues(device,0);
                                dasd_flush_chanq(device,0);
                                dasd_disable_blkdev(device);
                                dasd_set_device_level (device->devinfo.devno, 
                                                       device->discipline, 
                                                       DASD_STATE_ACCEPT);
                        }
                        break;
        }
	case BIODASDENABLE:{
                        dasd_range_t range = { 
                                from: device->devinfo.devno,
                                to: device->devinfo.devno 
                        };
			if (!capable (CAP_SYS_ADMIN)) {
				rc = -EACCES;
				break;
			}
                        dasd_enable_ranges (&range, device->discipline, 0);
                        break;
        }
	case BIODASDFMT:{
			/* fdata == NULL is no longer a valid arg to dasd_format ! */
			int partn = MINOR (inp->i_rdev) &
			    ((1 << major_info->gendisk.minor_shift) - 1);
			format_data_t fdata;

			if (!capable (CAP_SYS_ADMIN)) {
				rc = -EACCES;
				break;
			}
                        if (dasd_features_from_devno(device->devinfo.devno)&DASD_FEATURE_READONLY) {
                                rc = -EROFS;
                                break;
                        }
			if (!data) {
				rc = -EINVAL;
				break;
			}
			rc = copy_from_user (&fdata, (void *) data,
					     sizeof (format_data_t));
			if (rc) {
				rc = -EFAULT;
				break;
			}
			if (partn != 0) {
				DASD_MESSAGE (KERN_WARNING, device, "%s",
					      "Cannot low-level format a partition");
				return -EINVAL;
			}
			rc = dasd_format (device, &fdata);
			break;
		}
	case BIODASDPRRST:{     /* reset device profile information */
			if (!capable (CAP_SYS_ADMIN)) {
				rc = -EACCES;
				break;
			}
			memset (&device->profile, 0,
				sizeof (dasd_profile_info_t));
			break;
		}
	case BIODASDPRRD:{      /* retrun device profile information */
			rc = copy_to_user((long *)data,
					  (long *)&device->profile,
					  sizeof(dasd_profile_info_t));
			if (rc)
				rc = -EFAULT;
			break;
		}
	case BIODASDRSRV:{      /* reserve */
			ccw_req_t *req;
			if (!capable (CAP_SYS_ADMIN)) {
				rc = -EACCES;
				break;
			}
			req = device->discipline->reserve (device);
			rc = dasd_sleep_on_req (req);
			dasd_free_request (req, device);
			break;
		}
	case BIODASDRLSE:{      /* release */
			ccw_req_t *req;
			if (!capable (CAP_SYS_ADMIN)) {
				rc = -EACCES;
				break;
			}
			req = device->discipline->release (device);
			rc = dasd_sleep_on_req (req);
			dasd_free_request (req, device);
			break;
		}
	case BIODASDSLCK:{      /* steal lock - unconditional reserve */
			ccw_req_t *req;
			if (!capable (CAP_SYS_ADMIN)) {
				rc = -EACCES;
				break;
			}
			req = device->discipline->steal_lock (device);
			rc = dasd_sleep_on_req (req);
			dasd_free_request (req, device);
			break;
		}
	case BIODASDINFO:{
			dasd_information_t dasd_info;
			unsigned long flags;
			rc = device->discipline->fill_info (device, &dasd_info);
                        dasd_info.label_block = device->sizes.pt_block;
			dasd_info.devno = device->devinfo.devno;
			dasd_info.schid = device->devinfo.irq;
			dasd_info.cu_type = device->devinfo.sid_data.cu_type;
			dasd_info.cu_model = device->devinfo.sid_data.cu_model;
			dasd_info.dev_type = device->devinfo.sid_data.dev_type;
			dasd_info.dev_model = device->devinfo.sid_data.dev_model;
			dasd_info.open_count =
			    atomic_read (&device->open_count);
			dasd_info.status = device->level;
			if (device->discipline) {
				memcpy (dasd_info.type,
					device->discipline->name, 4);
			} else {
				memcpy (dasd_info.type, "none", 4);
			}
			dasd_info.req_queue_len = 0;
			dasd_info.chanq_len = 0;
			if (device->request_queue->request_fn) {
				struct list_head *l;
				ccw_req_t *cqr = device->queue.head;
				spin_lock_irqsave (&io_request_lock, flags);
				list_for_each (l,
					       &device->request_queue->
					       queue_head) {
					dasd_info.req_queue_len++;
				}
				spin_unlock_irqrestore (&io_request_lock,
							flags);
				s390irq_spin_lock_irqsave (device->devinfo.irq,
							   flags);
				while (cqr) {
					cqr = cqr->next;
					dasd_info.chanq_len++;
				}
				s390irq_spin_unlock_irqrestore (device->devinfo.
								irq, flags);
			}
			rc =
			    copy_to_user ((long *) data, (long *) &dasd_info,
					  sizeof (dasd_information_t));
			if (rc)
				rc = -EFAULT;
			break;
		}
#if 0 /* needed for XFS */
	case BLKBSZSET:{
		int bsz;
		rc = copy_from_user ((long *)&bsz,(long *)data,sizeof(int));
		if ( rc ) {
			rc = -EFAULT;
		} else {
			if ( bsz >= device->sizes.bp_block )
				rc = blk_ioctl (inp->i_rdev, no, data);
			else
				rc = -EINVAL; 
		}
		break;
		}
#endif /* 0 */
	case BLKGETSIZE:
	case BLKGETSIZE64:
	case BLKSSZGET:
	case BLKROSET:
	case BLKROGET:
	case BLKRASET:
	case BLKRAGET:
	case BLKFLSBUF:
	case BLKPG:
	case BLKELVGET:
	case BLKELVSET:
		return blk_ioctl (inp->i_rdev, no, data);
		break;
	default:{

			dasd_ioctl_list_t *old = dasd_find_ioctl (no);
			if (old) {
				if ( old->owner )
					__MOD_INC_USE_COUNT(old->owner);
				rc = old->handler (inp, no, data);
				if ( old->owner )
					__MOD_DEC_USE_COUNT(old->owner);
			} else {
				DASD_MESSAGE (KERN_INFO, device,
					      "ioctl 0x%08x=%s'0x%x'%d(%d) data %8lx\n",
					      no,
					      _IOC_DIR (no) == _IOC_NONE ? "0" :
					      _IOC_DIR (no) == _IOC_READ ? "r" :
					      _IOC_DIR (no) == _IOC_WRITE ? "w" : 
                                              _IOC_DIR (no) == 
                                              (_IOC_READ | _IOC_WRITE) ? "rw" : "u",
                                              _IOC_TYPE (no),
					      _IOC_NR (no), 
                                              _IOC_SIZE (no),
					      data);
				rc = -ENOTTY;
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
        unsigned long flags;
	dasd_device_t *device;

	if ((!inp) || !(inp->i_rdev)) {
		rc = -EINVAL;
                goto fail;
	}
	if (dasd_probeonly) {
		printk ("\n" KERN_INFO PRINTK_HEADER
			"No access to device (%d:%d) due to probeonly mode\n",
			MAJOR (inp->i_rdev), 
                        MINOR (inp->i_rdev));
		rc = -EPERM;
                goto fail;
	}
        spin_lock_irqsave(&discipline_lock,flags);
	device = dasd_device_from_kdev (inp->i_rdev);
	if (!device) {
		printk (KERN_WARNING PRINTK_HEADER
			"No device registered as (%d:%d)\n",
			MAJOR (inp->i_rdev), 
                        MINOR (inp->i_rdev));
		rc = -ENODEV;
                goto unlock;
	}
	if (device->level <= DASD_STATE_ACCEPT ) {
		DASD_MESSAGE (KERN_WARNING, device, " %s", 
                              " Cannot open unrecognized device\n");
		rc = -ENODEV;
                goto unlock;
	}
	if (atomic_inc_return (&device->open_count) == 1 ) {
                if ( device->discipline->owner )
                        __MOD_INC_USE_COUNT(device->discipline->owner);
        }
 unlock:
        spin_unlock_irqrestore(&discipline_lock,flags);
 fail:
	return rc;
}

/*
 * DASD_RELEASE
 *
 * DESCRIPTION
 */
static int
dasd_release (struct inode *inp, struct file *filp)
{
	int rc = 0;
        int count;
	dasd_device_t *device;

	if ((!inp) || !(inp->i_rdev)) {
		rc = -EINVAL;
                goto out;
	}
	device = dasd_device_from_kdev (inp->i_rdev);
	if (!device) {
		printk (KERN_WARNING PRINTK_HEADER
			"No device registered as %d:%d\n",
			MAJOR (inp->i_rdev), 
                        MINOR (inp->i_rdev));
		rc = -EINVAL;
                goto out;
	}

	if (device->level < DASD_STATE_ACCEPT ) {
		DASD_MESSAGE (KERN_WARNING, device, " %s",
                              " Cannot release unrecognized device\n");
		rc = -ENODEV;
                goto out;
	}
        count = atomic_dec_return (&device->open_count);
        if ( count == 0) {
                invalidate_buffers (inp->i_rdev);
                if ( device->discipline->owner )
                        __MOD_DEC_USE_COUNT(device->discipline->owner);
                MOD_DEC_USE_COUNT;
	} else if ( count == -1 ) { /* paranoia only */
                atomic_set (&device->open_count,0);
                printk (KERN_WARNING PRINTK_HEADER
                        "release called with open count==0\n");
        }
 out:
	return rc;
}

static struct
block_device_operations dasd_device_operations =
{
	owner:THIS_MODULE,
	open:dasd_open,
	release:dasd_release,
	ioctl:dasd_ioctl,
};

/* SECTION: Management of device list */
int
dasd_fillgeo(int kdev,struct hd_geometry *geo)
{
	dasd_device_t *device = dasd_device_from_kdev (kdev);

	if (!device)
                return -EINVAL;

        if (!device->discipline->fill_geometry)
		return -EINVAL;

	device->discipline->fill_geometry (device, geo);
	geo->start = device->major_info->gendisk.part[MINOR(kdev)].start_sect 
		     >> device->sizes.s2b_shift;;
        return 0;
} 


/* This one is needed for naming 18000+ possible dasd devices */
int
dasd_device_name (char *str, int index, int partition, struct gendisk *hd)
{
	int len = 0;
	char first, second, third;

	if (hd) {
		major_info_t *major_info = NULL;
		struct list_head *l;

		list_for_each (l, &dasd_major_info[0].list) {
			major_info = list_entry (l, major_info_t, list);
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
	second = ((index - 26) / 26) % 26;
	first = (((index - 702) / 26) / 26) % 26;

	len = sprintf (str, "dasd");
	if (index > 701) {
		len += sprintf (str + len, "%c", first + 'a');
	}
	if (index > 25) {
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

static void
dasd_plug_device (dasd_device_t * device)
{
	atomic_set(&device->plugged,1);	
}

static void
dasd_unplug_device (dasd_device_t * device)
{
	atomic_set(&device->plugged,0);	
        dasd_schedule_bh(device);
}

static void
dasd_flush_chanq ( dasd_device_t * device, int destroy ) 
{
        ccw_req_t *cqr;
        unsigned long flags;
        if ( destroy ) {
                s390irq_spin_lock_irqsave (device->devinfo.irq, flags);
                cqr = device->queue.head;
                while ( cqr != NULL ) {
                        if ( cqr->status == CQR_STATUS_IN_IO )
                                device->discipline->term_IO (cqr);
                        if ( cqr->status != CQR_STATUS_DONE ||
                             cqr->status != CQR_STATUS_FAILED ) {

                                cqr->status = CQR_STATUS_FAILED;
                                asm volatile ("STCK %0":"=m" (cqr->stopclk));

                        }
                        dasd_schedule_bh(device);
                        cqr = cqr->next;
                }
                s390irq_spin_unlock_irqrestore (device->devinfo.irq, flags);
        }
        wait_event( device->wait_q, device->queue.head == NULL );
}

static void
dasd_flush_request_queues ( dasd_device_t * device, int destroy )
{
        int i;
        int major = MAJOR(device->kdev);
        int minor = MINOR(device->kdev);
        for ( i = 0; i < (1 << DASD_PARTN_BITS); i ++) {
                if ( destroy )
                        destroy_buffers(MKDEV(major,minor+i)); 
                else
                        invalidate_buffers(MKDEV(major,minor+i)); 
        } 
}

static int
dasd_disable_volume ( dasd_device_t * device, int force ) 
{
        int rc = 0;
        int target  = DASD_STATE_KNOWN;
        int count = atomic_read (&device->open_count);
        
	if ( count ) {
		DASD_MESSAGE (KERN_EMERG, device, "%s",
			      "device has vanished although it was open!");
        }
        if ( force ) {
                dasd_deactivate_queue(device);
                dasd_flush_chanq(device,force);
                dasd_flush_request_queues(device,force);
                dasd_disable_blkdev(device);
                target = DASD_STATE_DEL;
        }

        /* unregister partitions ('ungrok_partitions') */
        devfs_register_partitions(&device->major_info->gendisk,
                                  MINOR(device->kdev),1);
        
        DASD_MESSAGE (KERN_WARNING, device, 
                      "disabling device, target state: %d",target);

        dasd_set_device_level (device->devinfo.devno, 
                               device->discipline, 
                               target);
        return rc;
}

static void
dasd_disable_ranges (dasd_range_t *range, 
                     dasd_discipline_t *d,
                     int all, int force ) 
{
        dasd_range_t *rrange;
        int j;

        if (range == &dasd_range_head) {
                rrange = list_entry (range->list.next, 
                                     dasd_range_t, list);
        } else {
                rrange = range;
        }
        do {
                for (j = rrange->from; j <= rrange->to; j++) {
                        dasd_device_t **dptr;
                        dasd_device_t *device;
                        dptr = dasd_device_from_devno(j);
                        if ( dptr == NULL ) {
                            continue;
                        }
                        device = *dptr;
                        if (device == NULL ||
                            (d != NULL &&
                             device -> discipline != d))
                                continue;
                        
                        dasd_disable_volume(device, force);
                }
                rrange = list_entry (rrange->list.next, dasd_range_t, list);
        } while ( all && rrange && rrange != range );
}

static void 
dasd_enable_single_device ( unsigned long arg ) {
        dasd_device_t * device =(dasd_device_t *) arg;
        int devno = device->devinfo.devno;
        dasd_range_t range = { from: devno, to:devno };
        dasd_enable_ranges (&range,NULL,0);
}

static void
dasd_enable_ranges (dasd_range_t *range, dasd_discipline_t *d, int all ) 
{
        int retries = 0;
	int j;
        kdev_t tempdev;
	dasd_range_t *rrange;

	if (range == NULL)
		return;
        
        do {
                if (range == &dasd_range_head) {
                        rrange = list_entry (range->list.next, 
                                             dasd_range_t, list);
                } else {
                        rrange = range;
                }
                do {
                        for (j = rrange->from; j <= rrange->to; j++) {
                                if ( dasd_devindex_from_devno(j) < 0 )
                                        continue;
                                dasd_set_device_level (j, d, DASD_STATE_ONLINE);
                        }
                        rrange = list_entry (rrange->list.next, dasd_range_t, list);
                } while ( all && rrange && rrange != range );

                if (atomic_read (&dasd_init_pending) == 0) /* we are done, exit loop */
                        break;

                if ( retries == 0 ) {
                        printk (KERN_INFO PRINTK_HEADER
                                "waiting for responses...\n");
                } else if ( retries < 5 ) {
                        printk (KERN_INFO PRINTK_HEADER
                                "waiting a little bit longer...\n");
                } else {
                        printk (KERN_INFO PRINTK_HEADER
                                "giving up, enable late devices manually!\n");
                        break;
                }
                interruptible_sleep_on_timeout (&dasd_init_waitq, (1 * HZ));
                retries ++;
        } while (1);
        /* now setup block devices */

        /* Now do block device and partition setup */
        if (range == &dasd_range_head) {
                rrange = list_entry (range->list.next, 
                                     dasd_range_t, list);
        } else {
                rrange = range;
        }
        do {
                for (j = rrange->from; j <= rrange->to; j++) {
                        dasd_device_t **dptr;
                        dasd_device_t *device;
                        if ( dasd_devindex_from_devno(j) < 0 )
                                continue;
                        dptr = dasd_device_from_devno(j);
                        device = *dptr;
                        if (device == NULL )
                                continue;
                        if ( ((d == NULL && device->discipline != NULL) ||
                              (device->discipline == d )) &&
                             device->level >= DASD_STATE_READY &&
                             device->request_queue == NULL ) {
                                if (dasd_features_from_devno(j)&DASD_FEATURE_READONLY) {
                                        for (tempdev=device->kdev;
                                             tempdev<(device->kdev +(1 << DASD_PARTN_BITS));
                                             tempdev++)
                                                set_device_ro (tempdev, 1);

                                        printk (KERN_WARNING PRINTK_HEADER 
                                                "setting read-only mode for device /dev/%s\n",
                                                device->name);
                                }
                                dasd_setup_blkdev(device);
                                dasd_setup_partitions(device);
                        }
                }
                rrange = list_entry (rrange->list.next, dasd_range_t, list);
        } while ( all && rrange && rrange != range );
}

#ifdef CONFIG_DASD_DYNAMIC
/*
 * DASD_NOT_OPER_HANDLER
 *
 * DESCRIPTION
 *   handles leaving devices
 */
static void
dasd_not_oper_handler (int irq, int status)
{
	dasd_device_t *device = NULL;
	major_info_t *major_info = NULL;
	struct list_head *l;
	int i, devno = -ENODEV;

	/* find out devno of leaving device: CIO has already deleted this information ! */
	list_for_each (l, &dasd_major_info[0].list) {
		major_info = list_entry (l, major_info_t, list);
		for (i = 0; i < DASD_PER_MAJOR; i++) {
			device = major_info->dasd_device[i];
			if (device && device->devinfo.irq == irq) {
				devno = device->devinfo.devno;
				break;
			}
		}
		if (devno != -ENODEV)
			break;
	}

	DASD_DRIVER_DEBUG_EVENT (5, dasd_not_oper_handler, 
                                 "called for devno %04x", 
                                 devno);

	if (devno < 0) {
		printk (KERN_WARNING PRINTK_HEADER
			"not_oper_handler called on irq 0x%04x no devno!\n", 
                        irq);
		return;
	}
        dasd_disable_volume(device, 1);
}

/*
 * DASD_OPER_HANDLER
 *
 * DESCRIPTION
 *   called by the machine check handler to make an device operational
 */
int
dasd_oper_handler (int irq, devreg_t * devreg)
{
	int devno;
	int rc = 0;
	major_info_t *major_info = NULL;
        dasd_range_t *rptr,range;
        dasd_device_t *device = NULL;
	struct list_head *l;
        int i;

	devno = get_devno_by_irq (irq);
	if (devno == -ENODEV) {
		rc = -ENODEV;
                goto out;
	}

	DASD_DRIVER_DEBUG_EVENT (5, dasd_oper_handler, 
                                 "called for devno %04x", 
                                 devno);

	/* find out devno of device */
	list_for_each (l, &dasd_major_info[0].list) {
		major_info = list_entry (l, major_info_t, list);
		for (i = 0; i < DASD_PER_MAJOR; i++) {
			device = major_info->dasd_device[i];
			if (device && device->devinfo.irq == irq) {
				devno = device->devinfo.devno;
				break;
			}
		}
		if (devno != -ENODEV)
			break;
	}
	if (devno < 0) {
                BUG();
	}
        if ( device &&
             device->level == DASD_STATE_READY ) {
            dasd_set_device_level (device->devinfo.devno, 
                                   device->discipline, DASD_STATE_ONLINE);

        } else {
            if (dasd_autodetect) {
		rptr = dasd_add_range (devno, devno, DASD_DEFAULT_FEATURES);
                if ( rptr == NULL ) {
                    rc = -ENOMEM;
                    goto out;
                }
            } else {
                range.from = devno;
                range.to = devno;
                rptr = &range;
            }
            dasd_enable_ranges (rptr, NULL, 0);
        }
 out:
	return rc;
}
#endif				/* CONFIG_DASD_DYNAMIC */

static inline dasd_device_t **
dasd_find_device_addr ( int devno ) 
{
        dasd_device_t **device_addr;

	DASD_DRIVER_DEBUG_EVENT (1, dasd_find_device_addr, 
                                 "devno %04x", 
                                 devno);
	if ( dasd_devindex_from_devno (devno) < 0 ) {
                DASD_DRIVER_DEBUG_EXCEPTION (1, dasd_find_device_addr, 
                                             "no dasd: devno %04x",
                                             devno);
		return NULL;
	}
        /* allocate major numbers on demand  for new devices */
	while ((device_addr = dasd_device_from_devno (devno)) == NULL) {
                int rc;

		if ((rc = dasd_register_major (NULL)) <= 0) {

                        DASD_DRIVER_DEBUG_EXCEPTION (1, dasd_find_device_addr, 
                                                     "%s",
                                                     "out of major numbers!");
                        break;
		}
	}
        return device_addr;
}

static inline int
dasd_state_del_to_new (dasd_device_t **addr ) 
{
        dasd_device_t* device;
        int rc = 0;
	if (*addr == NULL) { /* allocate device descriptor on demand for new device */
                device = kmalloc (sizeof (dasd_device_t), GFP_ATOMIC);
		if (device == NULL ) {
			rc = -ENOMEM;
                        goto out;
		}
		memset (device, 0, sizeof (dasd_device_t));
                *addr = device;
                device->lowmem_ccws = (void*)get_free_page (GFP_ATOMIC|GFP_DMA);
                if (device->lowmem_ccws == NULL) {
                        rc = -ENOMEM;
                        goto noccw;
	}
#ifdef CONFIG_ARCH_S390X
                device->lowmem_idals =
                    device->lowmem_idal_ptr = (void*) get_free_page (GFP_ATOMIC|GFP_DMA);
                if (device->lowmem_idals == NULL) {
                        rc = -ENOMEM;
                        goto noidal;
                }                
#endif
}
        goto out;
#ifdef CONFIG_ARCH_S390X
 noidal:
        free_page ((long) device->lowmem_ccws);
#endif
 noccw:
        kfree(device);
 out:
        return rc;
}

static inline int
dasd_state_new_to_del (dasd_device_t **addr )
{
        dasd_device_t *device = *addr;
        if (device && device->private) {
                kfree(device->private);
                device->private = NULL;
        }
#ifdef CONFIG_ARCH_S390X
        free_page ((long)(device->lowmem_idals));
#endif
        free_page((long)(device->lowmem_ccws));
        kfree(device);
        *addr = NULL; 
        return 0;
}

static inline int
dasd_state_new_to_known (dasd_device_t **dptr, int devno, dasd_discipline_t *disc) 
{
        int rc = 0;
	umode_t devfs_perm  = S_IFBLK | S_IRUSR | S_IWUSR;
        struct list_head *l;
        major_info_t *major_info = NULL;
        int i;
        dasd_device_t *device = *dptr;
        devfs_handle_t dir;
        char buffer[5];
        

	list_for_each (l, &dasd_major_info[0].list) {
                major_info = list_entry (l, major_info_t, list);
		for (i = 0; i < DASD_PER_MAJOR; i++) {
			if (major_info->dasd_device[i] == device) {
				device->kdev = MKDEV (major_info->gendisk.major,
                                                      i << DASD_PARTN_BITS);
				break;
			}
		}
		if (i < DASD_PER_MAJOR) /* we found one */
			break;
	}
        if ( major_info == NULL || major_info == &dasd_major_info[0] ) 
                BUG();

        device->major_info = major_info;
        dasd_device_name (device->name,
                          (((long)dptr -
                            (long)device->major_info->dasd_device) /
                           sizeof (dasd_device_t *)),
                          0, &device->major_info->gendisk);
        init_waitqueue_head (&device->wait_q);
        
        rc = get_dev_info_by_devno (devno, &device->devinfo);
        if ( rc ) {
                goto out;
        }

	DASD_DRIVER_DEBUG_EVENT (5, dasd_state_new_to_known, 
                                 "got devinfo CU-type %04x and dev-type %04x", 
                                 device->devinfo.sid_data.cu_type,
                                 device->devinfo.sid_data.dev_type);


        if ( devno != device->devinfo.devno )
                BUG();
        device->discipline = dasd_find_disc (device, disc);
        if ( device->discipline == NULL ) {
                rc = -ENODEV;
                goto out;
        }
        sprintf (buffer, "%04x", 
                 device->devinfo.devno);
        dir = devfs_mk_dir (dasd_devfs_handle, buffer, device);
        device->major_info->gendisk.de_arr[MINOR(device->kdev)
                                          >> DASD_PARTN_BITS] = dir;
	if (dasd_features_from_devno(device->devinfo.devno)&DASD_FEATURE_READONLY) {
	        devfs_perm &= ~(S_IWUSR);
	}
        device->devfs_entry = devfs_register (dir,"device",DEVFS_FL_DEFAULT,
                                              MAJOR(device->kdev),
                                              MINOR(device->kdev),
                                              devfs_perm,
                                              &dasd_device_operations,NULL);
        device->level = DASD_STATE_KNOWN;
 out:
        return rc;
}

static inline int
dasd_state_known_to_new (dasd_device_t *device ) 
{
        int rc = 0;
        /* don't reset to zeros because of persistent data durich detach/attach! */
        devfs_unregister(device->devfs_entry);
        devfs_unregister(device->major_info->gendisk.de_arr[MINOR(device->kdev) >> DASD_PARTN_BITS]);

        return rc;
}

static inline int
dasd_state_known_to_accept (dasd_device_t *device) 
{
        int rc = 0;
        device->debug_area = debug_register (device->name, 0, 2, 
                                             3 * sizeof (long));
        debug_register_view (device->debug_area, &debug_sprintf_view);
        debug_register_view (device->debug_area, &debug_hex_ascii_view);
        DASD_DEVICE_DEBUG_EVENT (0, device,"%p debug area created",
                                 device);
        
        if (device->discipline->int_handler) {
                rc = s390_request_irq_special (device->devinfo.irq,
                                               device->discipline->int_handler,
                                               dasd_not_oper_handler,
                                               0, DASD_NAME,
                                               &device->dev_status);
                if ( rc ) {
                        printk("No request IRQ\n");
                        goto out;
                }
        }
        device->level = DASD_STATE_ACCEPT;
 out:
        return rc;
}

static inline int
dasd_state_accept_to_known (dasd_device_t *device ) 
{
        if ( device->discipline == NULL )
                goto out;
        if (device->discipline->int_handler) {
                free_irq (device->devinfo.irq, &device->dev_status);
        }
        DASD_DEVICE_DEBUG_EVENT (0, device,"%p debug area deleted",
                                 device);
        if ( device->debug_area != NULL )
                debug_unregister (device->debug_area);
        device->discipline = NULL;
        device->level = DASD_STATE_KNOWN;
 out:
        return 0;
}

static inline int
dasd_state_accept_to_init (dasd_device_t *device) 
{
        int rc = 0;
        unsigned long flags;

        if ( device->discipline->init_analysis ) {
                device->init_cqr=device->discipline->init_analysis (device);
                if ( device->init_cqr != NULL ) {
                        if ( device->discipline->start_IO == NULL )
                                BUG();
                        atomic_inc (&dasd_init_pending);
                        s390irq_spin_lock_irqsave (device->devinfo.irq, 
                                                   flags);
                        rc = device->discipline->start_IO (device->init_cqr);
                        s390irq_spin_unlock_irqrestore(device->devinfo.irq, 
                                                       flags);
                        if ( rc )
                                goto out;
                        device->level = DASD_STATE_INIT;
                } else {
                        rc = -ENOMEM;
                }
        } else {
                rc = dasd_state_init_to_ready ( device ); 
        }
 out:
        return rc;
}

static inline int
dasd_state_init_to_ready (dasd_device_t *device ) 
{
        int rc = 0;    
        if (device->discipline->do_analysis != NULL)
                if ( device->discipline->do_analysis (device) == 0 ) 
                        switch (device->sizes.bp_block) {
                        case 512:
                        case 1024:
                        case 2048:
                        case 4096:
                                break;
                        default:
                                rc = -EMEDIUMTYPE;
                        }
        if ( device->init_cqr ) {
                /* This pointer is no longer needed, BUT dont't free the       */ 
                /* memory, because this is done in bh for finished request!!!! */
                atomic_dec(&dasd_init_pending);
                device->init_cqr = NULL; 
        }
        device->level = DASD_STATE_READY;
        return rc;
}

static inline int
dasd_state_ready_to_accept (dasd_device_t *device ) 
{
        int rc = 0;
        unsigned long flags;

        s390irq_spin_lock_irqsave (device->devinfo.irq, flags);
        if ( device->init_cqr != NULL &&  atomic_read(&dasd_init_pending) != 0 ) {
                if ( device->discipline->term_IO == NULL )
                        BUG();
                device->discipline->term_IO (device->init_cqr);
                atomic_dec (&dasd_init_pending);
                dasd_free_request (device->init_cqr, device);
                device->init_cqr = NULL;
        }
        s390irq_spin_unlock_irqrestore(device->devinfo.irq, flags);
        memset(&device->sizes,0,sizeof(dasd_sizes_t));
        device->level = DASD_STATE_ACCEPT;
        return rc;
}

static inline int
dasd_state_ready_to_online (dasd_device_t *device ) 
{
        int rc = 0;
        dasd_unplug_device (device);
        device->level = DASD_STATE_ONLINE;
        return rc;
}

static inline int
dasd_state_online_to_ready (dasd_device_t *device ) 
{
        int rc = 0;
        dasd_plug_device (device);
        device->level = DASD_STATE_READY;
        return rc;
}

static inline int
dasd_setup_blkdev (dasd_device_t *device ) 
{
        int rc = 0;
        int i;
        int major = MAJOR(device->kdev);
        int minor = MINOR(device->kdev);

        for (i = 0; i < (1 << DASD_PARTN_BITS); i++) {
                if (i == 0)
                        device->major_info->gendisk.sizes[minor] =
                                (device->sizes.blocks << device->
                                 sizes.s2b_shift) >> 1;
                else
                        device->major_info->gendisk.sizes[minor + i] = 0;
                hardsect_size[major][minor + i] = device->sizes.bp_block;
                blksize_size[major][minor + i] = device->sizes.bp_block;
                max_sectors[major][minor + i] =
                        device->discipline->max_blocks << 
                        device->sizes.s2b_shift;
		device->major_info->gendisk.part[minor+i].start_sect = 0;
		device->major_info->gendisk.part[minor+i].nr_sects = 0;
        }
        device->request_queue = kmalloc(sizeof(request_queue_t),GFP_KERNEL);
        device->request_queue->queuedata = device;
        blk_init_queue (device->request_queue, do_dasd_request);
        blk_queue_headactive (device->request_queue, 0);
        elevator_init (&(device->request_queue->elevator),ELEVATOR_NOOP);
        return rc;
}

static void
dasd_deactivate_queue (dasd_device_t *device)
{
        int i;
        int minor = MINOR(device->kdev);

        for (i = 0; i < (1 << DASD_PARTN_BITS); i++) {
                device->major_info->gendisk.sizes[minor + i] = 0;
        }
}

static inline int
dasd_disable_blkdev (dasd_device_t *device ) 
{
        int i;
        int major = MAJOR(device->kdev);
        int minor = MINOR(device->kdev);

        for (i = 0; i < (1 << DASD_PARTN_BITS); i++) {
                destroy_buffers(MKDEV(major,minor+i));
                device->major_info->gendisk.sizes[minor + i] = 0;
                hardsect_size[major][minor + i] = 0;
                blksize_size[major][minor + i] = 0;
                max_sectors[major][minor + i] = 0;
        }
        if (device->request_queue) {
            blk_cleanup_queue (device->request_queue);
            kfree(device->request_queue);
            device->request_queue = NULL;
        }
        return 0;
}


/*
 * function dasd_setup_partitions
 * calls the function in genhd, which is appropriate to setup a partitioned disk
 */
static inline void
dasd_setup_partitions ( dasd_device_t * device ) 
{
	register_disk (&device->major_info->gendisk,
                       device->kdev,
		       1 << DASD_PARTN_BITS,
		       &dasd_device_operations,
		       (device->sizes.blocks << device->sizes.s2b_shift));
}

static inline void
dasd_destroy_partitions ( dasd_device_t * device ) 
{
        int i;
        int minor = MINOR(device->kdev);
        
        for (i = 0; i < (1 << DASD_PARTN_BITS); i++) {
                device->major_info->gendisk.part[minor+i].start_sect = 0;
                device->major_info->gendisk.part[minor+i].nr_sects   = 0;
        }
        devfs_register_partitions(&device->major_info->gendisk,
                                  MINOR(device->kdev),1);
}

static inline void
dasd_resetup_partitions ( dasd_device_t * device ) 
{
    BUG();
    dasd_destroy_partitions ( device ) ;
    dasd_setup_partitions ( device ) ;
}

/*
 * function dasd_set_device_level
 */
static int
dasd_set_device_level (unsigned int devno, 
                       dasd_discipline_t * discipline,
                       int to_state)
{
	int rc = 0;
        dasd_device_t **device_addr;
        dasd_device_t *device;
        int from_state;

        device_addr = dasd_find_device_addr ( devno );
        if ( device_addr == NULL ) {
                rc = -ENODEV;
                goto out;
        }
        device = *device_addr;

        if ( device == NULL ) {
                from_state = DASD_STATE_DEL;
                if ( to_state == DASD_STATE_DEL )
                        goto out;
        } else {
                from_state = device->level;
        }

        DASD_DRIVER_DEBUG_EVENT (3, dasd_set_device_level,
                                 "devno %04x; from %i to %i",
                                 devno,
                                 from_state,
                                 to_state);

        if ( from_state == to_state )
                goto out;

        if ( to_state < from_state )
                goto shutdown;

        /* First check for bringup */
        if ( from_state <= DASD_STATE_DEL &&
             to_state >= DASD_STATE_NEW ) { 
                rc = dasd_state_del_to_new(device_addr);
                if ( rc ) {
                        goto bringup_fail;
                }
                device = *device_addr;
        }
        if ( from_state <= DASD_STATE_NEW &&
             to_state >= DASD_STATE_KNOWN ) { 
                rc = dasd_state_new_to_known( device_addr, devno, discipline );
                if ( rc ) {
                        goto bringup_fail;
                }
        }
        if ( from_state <= DASD_STATE_KNOWN &&
             to_state >= DASD_STATE_ACCEPT ) { 
                rc = dasd_state_known_to_accept(device);
                if ( rc ) {
                        goto bringup_fail;
                }
        }
        if ( dasd_probeonly ) {
            goto out;
        }
        if ( from_state <= DASD_STATE_ACCEPT &&
             to_state >= DASD_STATE_INIT ) { 
                rc = dasd_state_accept_to_init(device);
                if ( rc ) {
                        goto bringup_fail;
                }
        }
        if ( from_state <= DASD_STATE_INIT &&
             to_state >= DASD_STATE_READY ) { 
                rc = -EAGAIN;
                goto out;
        }
        if ( from_state <= DASD_STATE_READY &&
             to_state >= DASD_STATE_ONLINE ) { 
                rc = dasd_state_ready_to_online(device);
                if ( rc ) {
                        goto bringup_fail;
                }
        }
        goto out;
 bringup_fail:   /* revert changes */
#if 0
        printk (KERN_DEBUG PRINTK_HEADER
                "failed to set device from state %d to %d at "
                "level %d rc %d. Reverting...\n",
                from_state,
                to_state,
                device->level,
                rc);
#endif
        to_state = from_state;
        from_state = device->level;
        
        /* now do a shutdown */
 shutdown: 
        if ( from_state >= DASD_STATE_ONLINE &&
             to_state <= DASD_STATE_READY ) 
                if (dasd_state_online_to_ready(device))
                        BUG();

        if ( from_state >= DASD_STATE_READY &&
             to_state <= DASD_STATE_ACCEPT ) 
                if ( dasd_state_ready_to_accept(device))
                        BUG();

        if ( from_state >= DASD_STATE_ACCEPT &&
             to_state <= DASD_STATE_KNOWN ) 
                if ( dasd_state_accept_to_known(device))
                        BUG();

        if ( from_state >= DASD_STATE_KNOWN &&
             to_state <= DASD_STATE_NEW ) 
                if ( dasd_state_known_to_new(device))
                        BUG();

        if ( from_state >= DASD_STATE_NEW &&
             to_state <= DASD_STATE_DEL) 
                if (dasd_state_new_to_del(device_addr))
                        BUG();
        goto out;
 out:
        return rc;
}

/* SECTION: Procfs stuff */
typedef struct {
	char *data;
	int len;
} tempinfo_t;

void
dasd_fill_inode (struct inode *inode, int fill)
{
	if (fill)
		MOD_INC_USE_COUNT;
	else
		MOD_DEC_USE_COUNT;
}

static struct proc_dir_entry *dasd_proc_root_entry = NULL;
static struct proc_dir_entry *dasd_devices_entry;
static struct proc_dir_entry *dasd_statistics_entry;

static int
dasd_devices_open (struct inode *inode, struct file *file)
{
	int rc = 0;
	int size = 1;
	int len = 0;
	major_info_t *temp = NULL;
	struct list_head *l;
	tempinfo_t *info;
	int i;
        unsigned long flags;
        int index = 0;

        MOD_INC_USE_COUNT;
        spin_lock_irqsave(&discipline_lock,flags);
	info = (tempinfo_t *) vmalloc (sizeof (tempinfo_t));
	if (info == NULL) {
                printk (KERN_WARNING "No memory available for data\n");
                MOD_DEC_USE_COUNT;
                return -ENOMEM;
	} else {
		file->private_data = (void *) info;
	}
	list_for_each (l, &dasd_major_info[0].list) {
                size += 128 * 1 << (MINORBITS - DASD_PARTN_BITS);
	}
	info->data = (char *) vmalloc (size);	
	DASD_DRIVER_DEBUG_EVENT (1, dasd_devices_open, "area: %p, size 0x%x",
				 info->data, 
                                 size);
	if (size && info->data == NULL) {
		printk (KERN_WARNING "No memory available for data\n");
		vfree (info);
                MOD_DEC_USE_COUNT;
		return -ENOMEM;
	}
	list_for_each (l, &dasd_major_info[0].list) {
		temp = list_entry (l, major_info_t, list);
		for (i = 0; i < 1 << (MINORBITS - DASD_PARTN_BITS); i++) {
			dasd_device_t *device;
                        int devno = dasd_devno_from_devindex(index+i);
                        int features;

                        if ( devno == -ENODEV )
                                continue;

                        features = dasd_features_from_devno(devno);
                        if (features < DASD_DEFAULT_FEATURES)
                                features = DASD_DEFAULT_FEATURES;

                        device = temp->dasd_device[i];
			if (device) {

				len += sprintf (info->data + len,
						"%04x(%s) at (%3d:%3d) is %-7s%4s: ",
						device->devinfo.devno,
						device->discipline ?
						device->
						discipline->name : "none",
						temp->gendisk.major,
						i << DASD_PARTN_BITS,
						device->name,
                                                (features & DASD_FEATURE_READONLY) ? 
                                                "(ro)" : " ");
                                
				switch (device->level) {
				case DASD_STATE_NEW:
                                        len +=
					    sprintf (info->data + len,
						     "new");
                                        break;
				case DASD_STATE_KNOWN:
					len +=
					    sprintf (info->data + len,
						     "detected");
					break;
				case DASD_STATE_ACCEPT:
                                        len += sprintf (info->data + len,"accepted");
					break;
				case DASD_STATE_INIT:
					len +=
					    sprintf (info->data + len,
						     "busy   ");
					break;
				case DASD_STATE_READY:
				case DASD_STATE_ONLINE:
                                    if ( atomic_read(&device->plugged) )
                                        len +=
                                            sprintf (info->data + len,
                                                     "fenced ");
                                    else
                                        len +=
                                            sprintf (info->data + len,
                                                     "active ");
                                    if ( device->sizes.bp_block == 512 ||
                                         device->sizes.bp_block == 1024 ||
                                         device->sizes.bp_block == 2048 ||
                                         device->sizes.bp_block == 4096 )
					len +=
					    sprintf (info->data + len,
						     "at blocksize: %d, %ld blocks, %ld MB",
						     device->sizes.bp_block,
						     device->sizes.blocks,
						     ((device->
						       sizes.bp_block >> 9) *
						      device->sizes.
						      blocks) >> 11);
                                    else
                                        len +=
                                            sprintf (info->data + len,
                                                     "n/f    ");
					break;
				default:
					len +=
					    sprintf (info->data + len,
						     "no stat");
					break;
				}
			} else {
                                char buffer[7];
                                dasd_device_name (buffer, i, 0, &temp->gendisk);
                                if ( devno < 0  ) {
                                        len += sprintf (info->data + len,
                                                        "none");
                                } else {
                                        len += sprintf (info->data + len,
                                                        "%04x",devno);
                                }
                                len += sprintf (info->data + len,
                                                "(none) at (%3d:%3d) is %-7s%4s: unknown",
						temp->gendisk.major,
						i << DASD_PARTN_BITS,
						buffer,
                                                (features & DASD_FEATURE_READONLY) ? 
                                                "(ro)" : " ");
                        }
                        if ( dasd_probeonly )
                            len += sprintf(info->data + len,"(probeonly)");
                        len += sprintf(info->data + len,"\n");
		}
                index += 1 << (MINORBITS - DASD_PARTN_BITS);
	}
	info->len = len;
        spin_unlock_irqrestore(&discipline_lock,flags);
	return rc;
}

#define MIN(a,b) ((a)<(b)?(a):(b))

static ssize_t
dasd_generic_read (struct file *file, char *user_buf, size_t user_len,
		   loff_t * offset)
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
dasd_devices_write (struct file *file, const char *user_buf,
		    size_t user_len, loff_t * offset)
{
	char *buffer = vmalloc (user_len+1);
	int off = 0;
	char *temp;
	dasd_range_t range;
        int features;

	if (buffer == NULL)
		return -ENOMEM;
	if (copy_from_user (buffer, user_buf, user_len)) {
		vfree (buffer);
		return -EFAULT;
	}

        /* replace LF with '\0' */
        if (buffer[user_len -1] == '\n') {
                buffer[user_len -1] = '\0';
        } else {
                buffer[user_len] = '\0';
        }

	printk (KERN_INFO PRINTK_HEADER "/proc/dasd/devices: '%s'\n", buffer);
	if (strncmp (buffer, "set ", 4) && strncmp (buffer, "add ", 4)) {
		printk (KERN_WARNING PRINTK_HEADER
			"/proc/dasd/devices: only 'set' and 'add' are supported verbs\n");
		return -EINVAL;
	}
	off += 4;
	while (buffer[off] && !isalnum (buffer[off]))
		off++;
	if (!strncmp (buffer + off, "device", strlen ("device"))) {
		off += strlen ("device");
		while (buffer[off] && !isalnum (buffer[off]))
			off++;
	}
	if (!strncmp (buffer + off, "range=", strlen ("range="))) {
		off += strlen ("range=");
		while (buffer[off] && !isalnum (buffer[off]))
			off++;
	}
	
	temp = buffer + off;
	range.from = dasd_strtoul (temp, &temp, &features);
	range.to = range.from;

	if (*temp == '-') {
		temp++;
		range.to = dasd_strtoul (temp, &temp, &features);
	}

        if (range.from == -EINVAL ||
            range.to   == -EINVAL   ) {
                
                printk (KERN_WARNING PRINTK_HEADER
                        "/proc/dasd/devices: range parse error in '%s'\n", 
                        buffer);
        } else {
                off = (long) temp - (long) buffer;
                if (!strncmp (buffer, "add", strlen ("add"))) {
                        dasd_add_range (range.from, range.to, features);
                        dasd_enable_ranges (&range, NULL, 0);
                } else { 
                        while (buffer[off] && !isalnum (buffer[off]))
                                off++;
                        if (!strncmp (buffer + off, "on", strlen ("on"))) {
                                dasd_enable_ranges (&range, NULL, 0);
                        } else if (!strncmp (buffer + off, "off", strlen ("off"))) {
                                dasd_disable_ranges (&range, NULL, 0, 1);
                        } else {
                                printk (KERN_WARNING PRINTK_HEADER
                                        "/proc/dasd/devices: parse error in '%s'\n",
                                        buffer);
                        }
                }
        }

	vfree (buffer);
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
	MOD_DEC_USE_COUNT;
	return rc;
}

static struct file_operations dasd_devices_file_ops = {
	read:dasd_generic_read,	/* read */
	write:dasd_devices_write,	/* write */
	open:dasd_devices_open,	/* open */
	release:dasd_devices_close,	/* close */
};

static struct inode_operations dasd_devices_inode_ops = {
};

static int
dasd_statistics_open (struct inode *inode, struct file *file)
{
	int rc = 0;
	int len = 0;
	tempinfo_t *info;
	int shift, i, help = 0;

        MOD_INC_USE_COUNT;
	info = (tempinfo_t *) vmalloc (sizeof (tempinfo_t));
	if (info == NULL) {
		printk (KERN_WARNING "No memory available for data\n");
                MOD_DEC_USE_COUNT;
		return -ENOMEM;
	} else {
		file->private_data = (void *) info;
	}
	info->data = (char *) vmalloc (PAGE_SIZE);	/* FIXME! determine space needed in a better way */
	if (info->data == NULL) {
		printk (KERN_WARNING "No memory available for data\n");
		vfree (info);
		file->private_data = NULL;
                MOD_DEC_USE_COUNT;
		return -ENOMEM;
	}
        
        /* prevent couter 'ouverflow' on output */
	for (shift = 0, help = dasd_global_profile.dasd_io_reqs;
	     help > 9999999; help = help >> 1, shift++) ;

	len = sprintf (info->data, "%d dasd I/O requests\n",
                       dasd_global_profile.dasd_io_reqs);
	len += sprintf (info->data + len, "with %d sectors(512B each)\n",
                        dasd_global_profile.dasd_io_sects);

	len += sprintf (info->data + len,
                        "   __<4    ___8    __16    __32    __64 "
                        "   _128    _256    _512    __1k    __2k "
                        "   __4k    __8k    _16k    _32k    _64k "
                        "   128k\n");

	len += sprintf (info->data + len,
                        "   _256    _512    __1M    __2M    __4M "
                        "   __8M    _16M    _32M    _64M    128M "
                        "   256M    512M    __1G    __2G    __4G "
                        "   _>4G\n");

	len += sprintf (info->data + len, "Histogram of sizes (512B secs)\n");
	for (i = 0; i < 16; i++) {
		len += sprintf (info->data + len, "%7d ",
                                dasd_global_profile.dasd_io_secs[i] >> shift);
	}
	len += sprintf (info->data + len, "\n");

	len += sprintf (info->data + len, "Histogram of I/O times (microseconds)\n");
	for (i = 0; i < 16; i++) {
		len += sprintf (info->data + len, "%7d ",
                                dasd_global_profile.dasd_io_times[i] >> shift);
	}
	len += sprintf (info->data + len, "\n");
	for (; i < 32; i++) {
		len += sprintf (info->data + len, "%7d ",
                                dasd_global_profile.dasd_io_times[i] >> shift);
	}
	len += sprintf (info->data + len, "\n");

	len += sprintf (info->data + len, "Histogram of I/O times per sector\n");
	for (i = 0; i < 16; i++) {
		len += sprintf (info->data + len, "%7d ",
                                dasd_global_profile.dasd_io_timps[i] >> shift);
	}
	len += sprintf (info->data + len, "\n");
	for (; i < 32; i++) {
		len += sprintf (info->data + len, "%7d ",
                                dasd_global_profile.dasd_io_timps[i] >> shift);
	}
	len += sprintf (info->data + len, "\n");

	len += sprintf (info->data + len, "Histogram of I/O time till ssch\n");
	for (i = 0; i < 16; i++) {
		len += sprintf (info->data + len, "%7d ",
                                dasd_global_profile.dasd_io_time1[i] >> shift);
	}
	len += sprintf (info->data + len, "\n");
	for (; i < 32; i++) {
		len += sprintf (info->data + len, "%7d ",
                                dasd_global_profile.dasd_io_time1[i] >> shift);
	}
	len += sprintf (info->data + len, "\n");

	len += sprintf (info->data + len,
                        "Histogram of I/O time between ssch and irq\n");
	for (i = 0; i < 16; i++) {
		len += sprintf (info->data + len, "%7d ",
                                dasd_global_profile.dasd_io_time2[i] >> shift);
	}
	len += sprintf (info->data + len, "\n");
	for (; i < 32; i++) {
		len += sprintf (info->data + len, "%7d ",
                                dasd_global_profile.dasd_io_time2[i] >> shift);
	}
	len += sprintf (info->data + len, "\n");

	len += sprintf (info->data + len,
                        "Histogram of I/O time between ssch and irq per sector\n");
	for (i = 0; i < 16; i++) {
		len += sprintf (info->data + len, "%7d ",
                                dasd_global_profile.dasd_io_time2ps[i] >> shift);
	}
	len += sprintf (info->data + len, "\n");
	for (; i < 32; i++) {
		len += sprintf (info->data + len, "%7d ",
                                dasd_global_profile.dasd_io_time2ps[i] >> shift);
	}
	len += sprintf (info->data + len, "\n");

	len += sprintf (info->data + len,
                        "Histogram of I/O time between irq and end\n");
	for (i = 0; i < 16; i++) {
		len +=
		    sprintf (info->data + len, "%7d ",
			     dasd_global_profile.dasd_io_time3[i] >> shift);
	}
	len += sprintf (info->data + len, "\n");
	for (; i < 32; i++) {
		len += sprintf (info->data + len, "%7d ",
                                dasd_global_profile.dasd_io_time3[i] >> shift);
	}
	len += sprintf (info->data + len, "\n");

	len += sprintf (info->data + len,
                        "# of req in chanq at enqueuing (1..32) \n");
	for (i = 0; i < 16; i++) {
		len += sprintf (info->data + len, "%7d ",
                                dasd_global_profile.dasd_io_nr_req[i] >> shift);
	}
	len += sprintf (info->data + len, "\n");
	for (; i < 32; i++) {
		len += sprintf (info->data + len, "%7d ",
                                dasd_global_profile.dasd_io_nr_req[i] >> shift);
	}
	len += sprintf (info->data + len, "\n");

	info->len = len;
	return rc;
}

static ssize_t
dasd_statistics_write (struct file *file, const char *user_buf,
		       size_t user_len, loff_t * offset)
{
	char *buffer = vmalloc (user_len);

	if (buffer == NULL)
		return -ENOMEM;
	if (copy_from_user (buffer, user_buf, user_len)) {
		vfree (buffer);
		return -EFAULT;
	}
	buffer[user_len] = 0;
	printk (KERN_INFO PRINTK_HEADER "/proc/dasd/statictics: '%s'\n",
		buffer);
	if (strncmp (buffer, "reset", 4)) {
		memset (&dasd_global_profile, 0, sizeof (dasd_profile_info_t));
	}
	return user_len;
}

static struct file_operations dasd_statistics_file_ops = {
	read:dasd_generic_read,	/* read */
	open:dasd_statistics_open,	/* open */
	write:dasd_statistics_write,	/* write */
	release:dasd_devices_close,	/* close */
};

static struct inode_operations dasd_statistics_inode_ops = {
};

int
dasd_proc_init (void)
{
	int rc = 0;
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
	return rc;
}

void
dasd_proc_cleanup (void)
{
	remove_proc_entry ("devices", dasd_proc_root_entry);
	remove_proc_entry ("statistics", dasd_proc_root_entry);
	remove_proc_entry ("dasd", &proc_root);
}

int
dasd_request_module ( void *name ) {
	int rc = -ERESTARTSYS;
    	strcpy(current->comm, name);
   	daemonize();
   	while ( current->fs->root == NULL ) { /* wait for root-FS */
        	DECLARE_WAIT_QUEUE_HEAD(wait);
        	sleep_on_timeout(&wait,HZ); /* wait in steps of one second */
	} 
	while ( (rc=request_module(name)) != 0 ) {
        	DECLARE_WAIT_QUEUE_HEAD(wait);
		printk ( KERN_INFO "request_module returned %d for %s\n",
                         rc,
                         (char*)name);
        	sleep_on_timeout(&wait,5* HZ); /* wait in steps of 5 seconds */
    	}
    	return rc;
}


/* SECTION: Initializing the driver */
int __init
dasd_init (void)
{
	int rc = 0;
	int irq;
	major_info_t *major_info = NULL;
	struct list_head *l;

	printk (KERN_INFO PRINTK_HEADER "initializing...\n");
	dasd_debug_area = debug_register (DASD_NAME, 0, 2, 5 * sizeof (long));
	debug_register_view (dasd_debug_area, &debug_sprintf_view);
	debug_register_view (dasd_debug_area, &debug_hex_ascii_view);

	init_waitqueue_head (&dasd_init_waitq);

	if (dasd_debug_area == NULL) {
		goto failed;
	}
	DASD_DRIVER_DEBUG_EVENT (0, dasd_init, "%s", 
                                 "ENTRY");
	dasd_devfs_handle = devfs_mk_dir (NULL, DASD_NAME, NULL);
	if (dasd_devfs_handle < 0) {
		DASD_DRIVER_DEBUG_EVENT (1, dasd_init, "%s", 
                                         "no devfs");
		goto failed;
	}
	list_for_each (l, &dasd_major_info[0].list) {
		major_info = list_entry (l, major_info_t, list);
		if ((rc = dasd_register_major (major_info)) > 0) {
			DASD_DRIVER_DEBUG_EVENT (1, dasd_init,
						 "major %d: success",
						 major_info->gendisk.major);
			printk (KERN_INFO PRINTK_HEADER
				"Registered successfully to major no %u\n",
				major_info->gendisk.major);
		} else {
			DASD_DRIVER_DEBUG_EVENT (1, dasd_init,
						 "major %d: failed",
						 major_info->gendisk.major);
			printk (KERN_WARNING PRINTK_HEADER
				"Couldn't register successfully to major no %d\n",
				major_info->gendisk.major);
			/* revert registration of major infos */
			goto failed;
		}
	}
#ifndef MODULE
	dasd_split_parm_string (dasd_parm_string);
#endif				/* ! MODULE */
	rc = dasd_parse (dasd);
	if (rc) {
		DASD_DRIVER_DEBUG_EVENT (1, dasd_init, "%s",
                                         "invalid range found");
		goto failed;
	}

	rc = dasd_proc_init ();
	if (rc) {
		DASD_DRIVER_DEBUG_EVENT (1, dasd_init, "%s", "no proc-FS");
		goto failed;
	}
	genhd_dasd_name = dasd_device_name;

	if (dasd_autodetect) {	/* update device range to all devices */
		for (irq = get_irq_first (); irq != -ENODEV;
		     irq = get_irq_next (irq)) {
			int devno = get_devno_by_irq (irq);
			int index = dasd_devindex_from_devno (devno);
			if (index == -ENODEV) {	/* not included in ranges */
				DASD_DRIVER_DEBUG_EVENT (2, dasd_init,
							 "add %04x to range",
							 devno);
				dasd_add_range (devno, devno, DASD_DEFAULT_FEATURES);
			}
		}
	}

	if (MACHINE_IS_VM) {
#ifdef CONFIG_DASD_DIAG
		rc = dasd_diag_init ();
		if (rc == 0) {
			DASD_DRIVER_DEBUG_EVENT (1, dasd_init,
						 "DIAG discipline %s",
						 "success");
			printk (KERN_INFO PRINTK_HEADER
				"Registered DIAG discipline successfully\n");
		} else {
			DASD_DRIVER_DEBUG_EVENT (1, dasd_init,
						 "DIAG discipline %s",
						 "failed");
			goto failed;
		}
#endif /* CONFIG_DASD_DIAG */
#if defined(CONFIG_DASD_DIAG_MODULE) && defined(CONFIG_DASD_AUTO_DIAG)
                kernel_thread(dasd_request_module,"dasd_diag_mod",SIGCHLD);
#endif /* CONFIG_DASD_AUTO_DIAG */
	}
#ifdef CONFIG_DASD_ECKD
	rc = dasd_eckd_init ();
	if (rc == 0) {
		DASD_DRIVER_DEBUG_EVENT (1, dasd_init,
					 "ECKD discipline %s", "success");
		printk (KERN_INFO PRINTK_HEADER
			"Registered ECKD discipline successfully\n");
	} else {
		DASD_DRIVER_DEBUG_EVENT (1, dasd_init,
					 "ECKD discipline %s", "failed");
		goto failed;
	}
#endif /* CONFIG_DASD_ECKD */
#if defined(CONFIG_DASD_ECKD_MODULE) && defined(CONFIG_DASD_AUTO_ECKD)
        kernel_thread(dasd_request_module,"dasd_eckd_mod",SIGCHLD);
#endif /* CONFIG_DASD_AUTO_ECKD */
#ifdef CONFIG_DASD_FBA
	rc = dasd_fba_init ();
	if (rc == 0) {
		DASD_DRIVER_DEBUG_EVENT (1, dasd_init,
					 "FBA discipline %s", "success");

		printk (KERN_INFO PRINTK_HEADER
			"Registered FBA discipline successfully\n");
	} else {
		DASD_DRIVER_DEBUG_EVENT (1, dasd_init,
					 "FBA discipline %s", "failed");
		goto failed;
	}
#endif /* CONFIG_DASD_FBA */
#if defined(CONFIG_DASD_FBA_MODULE) && defined(CONFIG_DASD_AUTO_FBA)
        kernel_thread(dasd_request_module,"dasd_fba_mod",SIGCHLD);
#endif /* CONFIG_DASD_AUTO_FBA */
        {
                char **disc=dasd_disciplines;
                while (*disc) {
                        kernel_thread(dasd_request_module,*disc,SIGCHLD);
                        disc++;
                }
        }

	rc = 0;
	goto out;
      failed:
	printk (KERN_INFO PRINTK_HEADER
		"initialization not performed due to errors\n");
	cleanup_dasd ();
      out:
	DASD_DRIVER_DEBUG_EVENT (0, dasd_init, "%s", "LEAVE");
	printk (KERN_INFO PRINTK_HEADER "initialization finished\n");
	return rc;
}

static void
cleanup_dasd (void)
{
	int i,rc=0;
	major_info_t *major_info = NULL;
	struct list_head *l,*n;
	dasd_range_t *range;

	printk (KERN_INFO PRINTK_HEADER "shutting down\n");
        DASD_DRIVER_DEBUG_EVENT(0,"cleanup_dasd","%s","ENTRY");
	dasd_disable_ranges (&dasd_range_head, NULL, 1, 1);
        if (MACHINE_IS_VM) {
#ifdef CONFIG_DASD_DIAG
                dasd_diag_cleanup ();
                DASD_DRIVER_DEBUG_EVENT (1, "cleanup_dasd",
                                         "DIAG discipline %s", "success");
                printk (KERN_INFO PRINTK_HEADER
			"De-Registered DIAG discipline successfully\n");
#endif /* CONFIG_DASD_ECKD_BUILTIN */
	}
#ifdef CONFIG_DASD_FBA
	dasd_fba_cleanup ();
	DASD_DRIVER_DEBUG_EVENT (1, "cleanup_dasd",
				 "FBA discipline %s", "success");
	printk (KERN_INFO PRINTK_HEADER
		"De-Registered FBA discipline successfully\n");
#endif /* CONFIG_DASD_ECKD_BUILTIN */
#ifdef CONFIG_DASD_ECKD
	dasd_eckd_cleanup ();
	DASD_DRIVER_DEBUG_EVENT (1, "cleanup_dasd",
				 "ECKD discipline %s", "success");
	printk (KERN_INFO PRINTK_HEADER
		"De-Registered ECKD discipline successfully\n");
#endif /* CONFIG_DASD_ECKD_BUILTIN */
        
	dasd_proc_cleanup ();
        
	list_for_each_safe (l, n, &dasd_major_info[0].list) {
		major_info = list_entry (l, major_info_t, list);
		for (i = 0; i < DASD_PER_MAJOR; i++) {
			kfree (major_info->dasd_device[i]);
		}
		if ((major_info->flags & DASD_MAJOR_INFO_REGISTERED) &&
		    (rc = dasd_unregister_major (major_info)) == 0) {
			DASD_DRIVER_DEBUG_EVENT (1, "cleanup_dasd",
						 "major %d: success",
						 major_info->gendisk.major);
			printk (KERN_INFO PRINTK_HEADER
				"Unregistered successfully from major no %u\n",
				major_info->gendisk.major);
		} else {
			DASD_DRIVER_DEBUG_EVENT (1, "cleanup_dasd",
						 "major %d: failed",
						 major_info->gendisk.major);
			printk (KERN_WARNING PRINTK_HEADER
				"Couldn't unregister successfully from major no %d rc = %d\n",
				major_info->gendisk.major, rc);
  		}
  	}
	list_for_each_safe (l, n, &dasd_range_head.list) {
		range = list_entry (l, dasd_range_t, list);
                dasd_remove_range(range);
        }

#ifndef MODULE
        for( i = 0; i < 256; i++ )
                if ( dasd[i] ) {
                        kfree(dasd[i]);
                        dasd[i] = NULL;
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
	rc = dasd_init ();
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
