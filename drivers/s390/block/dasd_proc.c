/*
 * File...........: linux/drivers/s390/block/dasd_proc.c
 * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
 *		    Horst Hummel <Horst.Hummel@de.ibm.com>
 *		    Carsten Otte <Cotte@de.ibm.com>
 *		    Martin Schwidefsky <schwidefsky@de.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * (C) IBM Corporation, IBM Deutschland Entwicklung GmbH, 1999-2002
 *
 * /proc interface for the dasd driver.
 *
 * 05/04/02 split from dasd.c, code restructuring.
 */

#include <linux/config.h>
#include <linux/version.h>
#include <linux/ctype.h>
#include <linux/vmalloc.h>

#include <asm/debug.h>
#include <asm/irq.h>
#include <asm/uaccess.h>

/* This is ugly... */
#define PRINTK_HEADER "dasd_proc:"

#include "dasd_int.h"

typedef struct {
	char *data;
	int len;
} tempinfo_t;

static struct proc_dir_entry *dasd_proc_root_entry = NULL;
static struct proc_dir_entry *dasd_devices_entry;
static struct proc_dir_entry *dasd_statistics_entry;

static ssize_t
dasd_generic_read(struct file *file, char *user_buf, size_t user_len,
		  loff_t * offset)
{
	loff_t len;
	tempinfo_t *p_info = (tempinfo_t *) file->private_data;

	if (*offset >= p_info->len)
		return 0;	/* EOF */
	len = p_info->len - *offset;
	if (user_len < len)
		len = user_len;
	if (copy_to_user(user_buf, &(p_info->data[*offset]), len))
		return -EFAULT;
	(*offset) += len;
	return len;		/* number of bytes "read" */
}

static int
dasd_generic_close (struct inode *inode, struct file *file)
{
	tempinfo_t *info;

	info = (tempinfo_t *) file->private_data;
	file->private_data = NULL;
	if (info) {
		if (info->data)
			vfree (info->data);
		kfree (info);
	}
	MOD_DEC_USE_COUNT;
	return 0;
}

static inline char *
dasd_get_user_string(const char *user_buf, size_t user_len)
{
	char *buffer;

	buffer = kmalloc(user_len + 1, GFP_KERNEL);
	if (buffer == NULL)
		return ERR_PTR(-ENOMEM);
	if (copy_from_user(buffer, user_buf, user_len) != 0) {
		kfree(buffer);
		return ERR_PTR(-EFAULT);
	}
	/* got the string, now strip linefeed. */
	if (buffer[user_len - 1] == '\n')
		buffer[user_len - 1] = 0;
	else
		buffer[user_len] = 0;
	return buffer;
}

static ssize_t
dasd_devices_write(struct file *file, const char *user_buf,
		   size_t user_len, loff_t * offset)
{
	char *buffer, *str;
	int add_or_set;
	int from, to, features;

	buffer = dasd_get_user_string(user_buf, user_len);
	MESSAGE(KERN_INFO, "/proc/dasd/devices: '%s'", buffer);

	/* Scan for "add " or "set ". */
	for (str = buffer; isspace(*str); str++);
	if (strncmp(str, "add", 3) == 0 && isspace(str[3]))
		add_or_set = 0;
	else if (strncmp(str, "set", 3) == 0 && isspace(str[3]))
		add_or_set = 1;
	else
		goto out_error;
	for (str = str + 4; isspace(*str); str++);

	/* Scan for "device " and "range=" and ignore it. This is sick. */
	if (strncmp(str, "device", 6) == 0 && isspace(str[6]))
		for (str = str + 6; isspace(*str); str++);
	if (strncmp(str, "range=", 6) == 0) 
		for (str = str + 6; isspace(*str); str++);

	/* Scan device number range and feature string. */
	to = from = dasd_devno(str, &str);
	if (*str == '-') {
		str++;
		to = dasd_devno(str, &str);
	}
	features = dasd_feature_list(str, &str);
	/* Negative numbers in from/to/features indicate errors */
	if (from < 0 || to < 0 || from > 65546 || to > 65536 || features < 0)
		goto out_error;

	if (add_or_set == 0) {
		dasd_add_range(from, to, features);
		dasd_enable_devices(from, to);
	} else {
		for (; isspace(*str); str++);
		if (strcmp(str, "on") == 0)
			dasd_enable_devices(from, to);
		else if (strcmp(str, "off") == 0)
			dasd_disable_devices(from, to);
		else
			goto out_error;
	}
	kfree(buffer);
	return user_len;
out_error:
	MESSAGE(KERN_WARNING,
		"/proc/dasd/devices: range parse error in '%s'",
		buffer);
	kfree(buffer);
	return -EINVAL;
}

static inline int
dasd_devices_print(dasd_devmap_t *devmap, char *str)
{
	dasd_device_t *device;
	char *substr;
	int major, minor;
	int len;

	device = dasd_get_device(devmap);
	if (IS_ERR(device))
		return 0;
	/* Print device number. */
	len = sprintf(str, "%04x", devmap->devno);
	/* Print discipline string. */
	if (device != NULL && device->discipline != NULL)
		len += sprintf(str + len, "(%s)", device->discipline->name);
	else
		len += sprintf(str + len, "(none)");
	/* Print kdev. */
	major = MAJOR(device->bdev->bd_dev);
	minor = MINOR(device->bdev->bd_dev);
	len += sprintf(str + len, " at (%3d:%3d)", major, minor);
	/* Print device name. */
	len += sprintf(str + len, " is %-7s", device->name);
	/* Print devices features. */
	substr = (devmap->features & DASD_FEATURE_READONLY) ? "(ro)" : " ";
	len += sprintf(str + len, "%4s: ", substr);
	/* Print device status information. */
	if (device == NULL) {
		len += sprintf(str + len, "unknown");
		dasd_put_device(devmap);
		return len;
	}
	switch (device->state) {
	case DASD_STATE_NEW:
		len += sprintf(str + len, "new");
		break;
	case DASD_STATE_KNOWN:
		len += sprintf(str + len, "detected");
		break;
	case DASD_STATE_BASIC:
		len += sprintf(str + len, "basic");
		break;
	case DASD_STATE_ACCEPT:
		len += sprintf(str + len, "accepted");
		break;
	case DASD_STATE_READY:
	case DASD_STATE_ONLINE:
		if (device->state < DASD_STATE_ONLINE)
			len += sprintf(str + len, "fenced ");
		else
			len += sprintf(str + len, "active ");
		if (dasd_check_blocksize(device->bp_block))
			len += sprintf(str + len, "n/f	 ");
		else
			len += sprintf(str + len,
				       "at blocksize: %d, %ld blocks, %ld MB",
				       device->bp_block, device->blocks,
				       ((device->bp_block >> 9) *
					device->blocks) >> 11);
		break;
	default:
		len += sprintf(str + len, "no stat");
		break;
	}
	dasd_put_device(devmap);
	return len;
}

static int
dasd_devices_open(struct inode *inode, struct file *file)
{
	tempinfo_t *info;
	int size, len;
	int devindex;

	MOD_INC_USE_COUNT;
	info = (tempinfo_t *) kmalloc(sizeof (tempinfo_t), GFP_KERNEL);
	if (info == NULL) {
		MESSAGE(KERN_WARNING, "%s",
			"No memory available for data (tempinfo)");
		MOD_DEC_USE_COUNT;
		return -ENOMEM;

	}

	size = dasd_max_devindex * 128;
	info->data = (char *) vmalloc (size);
	if (size && info->data == NULL) {
		MESSAGE(KERN_WARNING, "%s",
			"No memory available for data (info->data)");
		kfree(info);
		MOD_DEC_USE_COUNT;
		return -ENOMEM;
	}
	file->private_data = (void *) info;

	DBF_EVENT(DBF_NOTICE,
		  "procfs-area: %p, size 0x%x allocated", info->data, size);

	len = 0;
	for (devindex = 0; devindex < dasd_max_devindex; devindex++) {
		dasd_devmap_t *devmap;

		devmap = dasd_devmap_from_devindex(devindex);
		len += dasd_devices_print(devmap, info->data + len);
		if (dasd_probeonly)
			len += sprintf(info->data + len, "(probeonly)");
		len += sprintf(info->data + len, "\n");
	}
	info->len = len;
	if (len > size) {
		printk("len = %i, size = %i\n", len, size);
		BUG();
	}
	return 0;
}

static struct file_operations dasd_devices_file_ops = {
	read:dasd_generic_read,		/* read */
	write:dasd_devices_write,	/* write */
	open:dasd_devices_open,		/* open */
	release:dasd_generic_close,	/* close */
};

static struct inode_operations dasd_devices_inode_ops = {
};

static inline char *
dasd_statistics_array(char *str, int *array, int shift)
{
	int i;

	for (i = 0; i < 32; i++) {
		str += sprintf(str, "%7d ", array[i] >> shift);
		if (i == 15)
			str += sprintf(str, "\n");
	}
	str += sprintf(str,"\n");
	return str;
}

static int
dasd_statistics_open(struct inode *inode, struct file *file)
{
	tempinfo_t *info;
#ifdef CONFIG_DASD_PROFILE
	dasd_profile_info_t *prof;
	char *str;
	int shift;
#endif

	MOD_INC_USE_COUNT;
	info = (tempinfo_t *) kmalloc(sizeof (tempinfo_t), GFP_KERNEL);
	if (info == NULL) {
		MESSAGE(KERN_WARNING, "%s",
			"No memory available for data (tempinfo)");
		MOD_DEC_USE_COUNT;
		return -ENOMEM;
	}

	file->private_data = (void *) info;
	/* FIXME! determine space needed in a better way */
	info->data = (char *) vmalloc (PAGE_SIZE);

	if (info->data == NULL) {
		MESSAGE(KERN_WARNING, "%s",
			"No memory available for data (info->data)");
		kfree(info);
		file->private_data = NULL;
		MOD_DEC_USE_COUNT;
		return -ENOMEM;
	}
#ifdef CONFIG_DASD_PROFILE
	/* check for active profiling */
	if (dasd_profile_level == DASD_PROFILE_OFF) {
		info->len = sprintf(info->data,
				    "Statistics are off - they might be "
				    "switched on using 'echo set on > "
				    "/proc/dasd/statistics'\n");
		return 0;
	}

	prof = &dasd_global_profile;
	/* prevent couter 'overflow' on output */
	for (shift = 0; (prof->dasd_io_reqs >> shift) > 9999999; shift++);

	str = info->data;
	str += sprintf(str, "%d dasd I/O requests\n", prof->dasd_io_reqs);
	str += sprintf(str, "with %d sectors(512B each)\n",
		       prof->dasd_io_sects);
	str += sprintf(str,
		       "   __<4	   ___8	   __16	   __32	   __64	   _128	"
		       "   _256	   _512	   __1k	   __2k	   __4k	   __8k	"
		       "   _16k	   _32k	   _64k	   128k\n");
	str += sprintf(str,
		       "   _256	   _512	   __1M	   __2M	   __4M	   __8M	"
		       "   _16M	   _32M	   _64M	   128M	   256M	   512M	"
		       "   __1G	   __2G	   __4G " "   _>4G\n");

	str += sprintf(str, "Histogram of sizes (512B secs)\n");
	str = dasd_statistics_array(str, prof->dasd_io_secs, shift);
	str += sprintf(str, "Histogram of I/O times (microseconds)\n");
	str = dasd_statistics_array(str, prof->dasd_io_times, shift);
	str += sprintf(str, "Histogram of I/O times per sector\n");
	str = dasd_statistics_array(str, prof->dasd_io_timps, shift);
	str += sprintf(str, "Histogram of I/O time till ssch\n");
	str = dasd_statistics_array(str, prof->dasd_io_time1, shift);
	str += sprintf(str, "Histogram of I/O time between ssch and irq\n");
	str = dasd_statistics_array(str, prof->dasd_io_time2, shift);
	str += sprintf(str, "Histogram of I/O time between ssch "
			    "and irq per sector\n");
	str = dasd_statistics_array(str, prof->dasd_io_time2ps, shift);
	str += sprintf(str, "Histogram of I/O time between irq and end\n");
	str = dasd_statistics_array(str, prof->dasd_io_time3, shift);
	str += sprintf(str, "# of req in chanq at enqueuing (1..32) \n");
	str = dasd_statistics_array(str, prof->dasd_io_nr_req, shift);

	info->len = str - info->data;
#else
	info->len = sprintf(info->data,
			    "Statistics are not activated in this kernel\n");
#endif
	return 0;
}

static ssize_t
dasd_statistics_write(struct file *file, const char *user_buf,
		      size_t user_len, loff_t * offset)
{
#ifdef CONFIG_DASD_PROFILE
	char *buffer, *str;

	buffer = dasd_get_user_string(user_buf, user_len);
	MESSAGE(KERN_INFO, "/proc/dasd/statictics: '%s'", buffer);

	/* check for valid verbs */
	for (str = buffer; isspace(*str); str++);
	if (strncmp(str, "set", 3) == 0 && isspace(str[3])) {
		/* 'set xxx' was given */
		for (str = str + 4; isspace(*str); str++);
		if (strcmp(str, "on") == 0) {
			/* switch on statistics profiling */
			dasd_profile_level = DASD_PROFILE_ON;
			MESSAGE(KERN_INFO, "%s", "Statictics switched on");
		} else if (strcmp(str, "off") == 0) {
			/* switch off and reset statistics profiling */
			memset(&dasd_global_profile,
			       0, sizeof (dasd_profile_info_t));
			dasd_profile_level = DASD_PROFILE_OFF;
			MESSAGE(KERN_INFO, "%s", "Statictics switched off");
		} else
			goto out_error;
	} else if (strncmp(str, "reset", 5) == 0) {
		/* reset the statistics */
		memset(&dasd_global_profile, 0, sizeof (dasd_profile_info_t));
		MESSAGE(KERN_INFO, "%s", "Statictics reset");
	} else
		goto out_error;
	kfree(buffer);
	return user_len;
out_error:
	MESSAGE(KERN_WARNING, "%s",
		"/proc/dasd/statistics: only 'set on', 'set off' "
		"and 'reset' are supported verbs");
	kfree(buffer);
	return -EINVAL;
#else
	MESSAGE(KERN_WARNING, "%s",
		"/proc/dasd/statistics: is not activated in this kernel");
	return user_len;
#endif				/* CONFIG_DASD_PROFILE */
}

static struct file_operations dasd_statistics_file_ops = {
	read:	dasd_generic_read,	/* read */
	write:	dasd_statistics_write,	/* write */
	open:	dasd_statistics_open,	/* open */
	release:dasd_generic_close,	/* close */
};

static struct inode_operations dasd_statistics_inode_ops = {
};

int
dasd_proc_init(void)
{
	dasd_proc_root_entry = proc_mkdir("dasd", &proc_root);
	dasd_devices_entry = create_proc_entry("devices",
					       S_IFREG | S_IRUGO | S_IWUSR,
					       dasd_proc_root_entry);
	dasd_devices_entry->proc_fops = &dasd_devices_file_ops;
	dasd_devices_entry->proc_iops = &dasd_devices_inode_ops;
	dasd_statistics_entry = create_proc_entry("statistics",
						  S_IFREG | S_IRUGO | S_IWUSR,
						  dasd_proc_root_entry);
	dasd_statistics_entry->proc_fops = &dasd_statistics_file_ops;
	dasd_statistics_entry->proc_iops = &dasd_statistics_inode_ops;
	return 0;
}

void
dasd_proc_exit(void)
{
	remove_proc_entry("devices", dasd_proc_root_entry);
	remove_proc_entry("statistics", dasd_proc_root_entry);
	remove_proc_entry("dasd", &proc_root);
}
