/*
 * scsi_sysfs.c
 *
 * SCSI sysfs interface routines.
 *
 * Created to pull SCSI mid layer sysfs routines into one file.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/device.h>
#include "scsi.h"
#include "hosts.h"

#include "scsi_priv.h"

/*
 * shost_show_function: macro to create an attr function that can be used to
 * show a non-bit field.
 */
#define shost_show_function(field, format_string)			\
static ssize_t								\
show_##field (struct class_device *class_dev, char *buf)		\
{									\
	struct Scsi_Host *shost = class_to_shost(class_dev);		\
	return snprintf (buf, 20, format_string, shost->field);	\
}

/*
 * shost_rd_attr: macro to create a function and attribute variable for a
 * read only field.
 */
#define shost_rd_attr(field, format_string)				\
	shost_show_function(field, format_string)			\
static CLASS_DEVICE_ATTR(field, S_IRUGO, show_##field, NULL)

/*
 * Create the actual show/store functions and data structures.
 */
shost_rd_attr(unique_id, "%u\n");
shost_rd_attr(host_busy, "%hu\n");
shost_rd_attr(cmd_per_lun, "%hd\n");
shost_rd_attr(sg_tablesize, "%hu\n");
shost_rd_attr(unchecked_isa_dma, "%d\n");

struct class_device_attribute *scsi_sysfs_shost_attrs[] = {
	&class_device_attr_unique_id,
	&class_device_attr_host_busy,
	&class_device_attr_cmd_per_lun,
	&class_device_attr_sg_tablesize,
	&class_device_attr_unchecked_isa_dma,
	NULL
};

static struct class shost_class = {
	.name		= "scsi_host",
};

/**
 * scsi_bus_match:
 * @dev:
 * @dev_driver:
 *
 * Return value:
 **/
static int scsi_bus_match(struct device *dev, 
                          struct device_driver *dev_driver)
{
        if (!strcmp("sg", dev_driver->name)) {
                if (strstr(dev->bus_id, ":gen"))
                        return 1;
        } else if (!strcmp("st",dev_driver->name)) {
                if (strstr(dev->bus_id,":mt"))
                        return 1;
        } else if (!strcmp("sd", dev_driver->name)) {
                if ((!strstr(dev->bus_id, ":gen")) && 
		    (!strstr(dev->bus_id, ":mt"))) { 
                        return 1;
                }
	}
        return 0;
}


static struct bus_type scsi_bus_type = {
        .name		= "scsi",
        .match		= scsi_bus_match,
};


int scsi_sysfs_register(void)
{
	int error;

	error = bus_register(&scsi_bus_type);
	if (error)
		return error;
	error = class_register(&shost_class);
	if (error)
		return error;

	return error;
}

void scsi_sysfs_unregister(void)
{
	class_unregister(&shost_class);
	bus_unregister(&scsi_bus_type);
}

/**
 * scsi_upper_driver_register - register upper level driver.
 * @sdev_tp:	Upper level driver to register with the scsi bus.
 *
 * Return value:
 * 	0 on Success / non-zero on Failure
 **/
int scsi_upper_driver_register(struct Scsi_Device_Template *sdev_tp)
{
	int error = 0;

	sdev_tp->scsi_driverfs_driver.bus = &scsi_bus_type;
	error = driver_register(&sdev_tp->scsi_driverfs_driver);

	return error;
}

/**
 * scsi_upper_driver_unregister - unregister upper level driver 
 * @sdev_tp:	Upper level driver to unregister with the scsi bus.
 *
 **/
void scsi_upper_driver_unregister(struct Scsi_Device_Template *sdev_tp)
{
	driver_unregister(&sdev_tp->scsi_driverfs_driver);
}


/*
 * sdev_show_function: macro to create an attr function that can be used to
 * show a non-bit field.
 */
#define sdev_show_function(field, format_string)				\
static ssize_t								\
show_##field (struct device *dev, char *buf)				\
{									\
	struct scsi_device *sdev;					\
	sdev = to_scsi_device(dev);					\
	return snprintf (buf, 20, format_string, sdev->field);		\
}									\

/*
 * sdev_rd_attr: macro to create a function and attribute variable for a
 * read only field.
 */
#define sdev_rd_attr(field, format_string)				\
	sdev_show_function(field, format_string)				\
static DEVICE_ATTR(field, S_IRUGO, show_##field, NULL)


/*
 * sdev_rd_attr: create a function and attribute variable for a
 * read/write field.
 */
#define sdev_rw_attr(field, format_string)				\
	sdev_show_function(field, format_string)				\
									\
static ssize_t								\
sdev_store_##field (struct device *dev, const char *buf, size_t count)	\
{									\
	struct scsi_device *sdev;					\
	sdev = to_scsi_device(dev);					\
	snscanf (buf, 20, format_string, &sdev->field);			\
	return count;							\
}									\
static DEVICE_ATTR(field, S_IRUGO | S_IWUSR, show_##field, sdev_store_##field)

/*
 * sdev_rd_attr: create a function and attribute variable for a
 * read/write bit field.
 */
#define sdev_rw_attr_bit(field)						\
	sdev_show_function(field, "%d\n")					\
									\
static ssize_t								\
sdev_store_##field (struct device *dev, const char *buf, size_t count)	\
{									\
	int ret;							\
	struct scsi_device *sdev;					\
	ret = scsi_sdev_check_buf_bit(buf);				\
	if (ret >= 0)	{						\
		sdev = to_scsi_device(dev);				\
		sdev->field = ret;					\
		ret = count;						\
	}								\
	return ret;							\
}									\
static DEVICE_ATTR(field, S_IRUGO | S_IWUSR, show_##field, sdev_store_##field)

/*
 * scsi_sdev_check_buf_bit: return 0 if buf is "0", return 1 if buf is "1",
 * else return -EINVAL.
 */
static int scsi_sdev_check_buf_bit(const char *buf)
{
	if ((buf[1] == '\0') || ((buf[1] == '\n') && (buf[2] == '\0'))) {
		if (buf[0] == '1')
			return 1;
		else if (buf[0] == '0')
			return 0;
		else 
			return -EINVAL;
	} else
		return -EINVAL;
}

/*
 * Create the actual show/store functions and data structures.
 */
sdev_rd_attr (device_blocked, "%d\n");
sdev_rd_attr (queue_depth, "%d\n");
sdev_rd_attr (type, "%d\n");
sdev_rd_attr (scsi_level, "%d\n");
sdev_rd_attr (access_count, "%d\n");
sdev_rd_attr (vendor, "%.8s\n");
sdev_rd_attr (model, "%.16s\n");
sdev_rd_attr (rev, "%.4s\n");
sdev_rw_attr_bit (online);

static ssize_t
show_rescan_field (struct device *dev, char *buf)
{
	return 0; 
}

static ssize_t
store_rescan_field (struct device *dev, const char *buf, size_t count) 
{
	scsi_rescan_device(to_scsi_device(dev));
	return 0;
}

static DEVICE_ATTR(rescan, S_IRUGO | S_IWUSR, show_rescan_field, store_rescan_field)

/* Default template for device attributes.  May NOT be modified */
struct device_attribute *scsi_sysfs_sdev_attrs[] = {
	&dev_attr_device_blocked,
	&dev_attr_queue_depth,
	&dev_attr_type,
	&dev_attr_scsi_level,
	&dev_attr_access_count,
	&dev_attr_vendor,
	&dev_attr_model,
	&dev_attr_rev,
	&dev_attr_online,
	&dev_attr_rescan,
	NULL
};

static void scsi_device_release(struct device *dev)
{
	struct scsi_device *sdev;

	sdev = to_scsi_device(dev);
	if (!sdev)
		return;
	scsi_free_sdev(sdev);
}

/**
 * scsi_device_register - register a scsi device with the scsi bus
 * @sdev:	scsi_device to register
 *
 * Return value:
 * 	0 on Success / non-zero on Failure
 **/
int scsi_device_register(struct scsi_device *sdev)
{
	int error = 0, i;

	sprintf(sdev->sdev_driverfs_dev.bus_id,"%d:%d:%d:%d",
		sdev->host->host_no, sdev->channel, sdev->id, sdev->lun);
	sdev->sdev_driverfs_dev.parent = &sdev->host->host_gendev;
	sdev->sdev_driverfs_dev.bus = &scsi_bus_type;
	sdev->sdev_driverfs_dev.release = scsi_device_release;

	error = device_register(&sdev->sdev_driverfs_dev);
	if (error)
		return error;

	for (i = 0; !error && sdev->host->hostt->sdev_attrs[i] != NULL; i++)
		error = device_create_file(&sdev->sdev_driverfs_dev,
					   sdev->host->hostt->sdev_attrs[i]);

	if (error)
		scsi_device_unregister(sdev);

	return error;
}

/**
 * scsi_device_unregister - unregister a device from the scsi bus
 * @sdev:	scsi_device to unregister
 **/
void scsi_device_unregister(struct scsi_device *sdev)
{
	int i;

	for (i = 0; sdev->host->hostt->sdev_attrs[i] != NULL; i++)
		device_remove_file(&sdev->sdev_driverfs_dev, sdev->host->hostt->sdev_attrs[i]);
	device_unregister(&sdev->sdev_driverfs_dev);
}

static void scsi_host_release(struct device *dev)
{
	struct Scsi_Host *shost;

	shost = dev_to_shost(dev);
	if (!shost)
		return;

	scsi_free_shost(shost);
}

void scsi_sysfs_init_host(struct Scsi_Host *shost)
{
	device_initialize(&shost->host_gendev);
	snprintf(shost->host_gendev.bus_id, BUS_ID_SIZE, "host%d",
		shost->host_no);
	snprintf(shost->host_gendev.name, DEVICE_NAME_SIZE, "%s",
		shost->hostt->proc_name);
	shost->host_gendev.release = scsi_host_release;

	class_device_initialize(&shost->class_dev);
	shost->class_dev.dev = &shost->host_gendev;
	shost->class_dev.class = &shost_class;
	snprintf(shost->class_dev.class_id, BUS_ID_SIZE, "host%d",
		  shost->host_no);
}

/**
 * scsi_sysfs_add_host - add scsi host to subsystem
 * @shost:     scsi host struct to add to subsystem
 * @dev:       parent struct device pointer
 **/
int scsi_sysfs_add_host(struct Scsi_Host *shost, struct device *dev)
{
	int i, error;

	if (!shost->host_gendev.parent)
		shost->host_gendev.parent = (dev) ? dev : &legacy_bus;

	error = device_add(&shost->host_gendev);
	if (error)
		return error;

	error = class_device_add(&shost->class_dev);
	if (error)
		goto clean_device;

	for (i = 0; !error && shost->hostt->shost_attrs[i] != NULL; i++)
		error = class_device_create_file(&shost->class_dev,
					   shost->hostt->shost_attrs[i]);
	if (error)
		goto clean_class;

	return error;

clean_class:
	class_device_del(&shost->class_dev);
clean_device:
	device_del(&shost->host_gendev);

	return error;
}

/**
 * scsi_sysfs_remove_host - remove scsi host from subsystem
 * @shost:     scsi host to remove from subsystem
 **/
void scsi_sysfs_remove_host(struct Scsi_Host *shost)
{
	class_device_del(&shost->class_dev);
	device_del(&shost->host_gendev);
}

/** scsi_sysfs_modify_shost_attribute - modify or add a host class attribute
 *
 * @class_attrs:host class attribute list to be added to or modified
 * @attr:	individual attribute to change or added
 *
 * returns zero if successful or error if not
 **/
int scsi_sysfs_modify_shost_attribute(struct class_device_attribute ***class_attrs,
				      struct class_device_attribute *attr)
{
	int modify = 0;
	int num_attrs;

	if(*class_attrs == NULL)
		*class_attrs = scsi_sysfs_shost_attrs;

	for(num_attrs=0; (*class_attrs)[num_attrs] != NULL; num_attrs++)
		if(strcmp((*class_attrs)[num_attrs]->attr.name, attr->attr.name) == 0)
			modify = num_attrs;

	if(*class_attrs == scsi_sysfs_shost_attrs || !modify) {
		struct class_device_attribute **tmp_attrs = kmalloc(sizeof(struct class_device_attribute)*(num_attrs + (modify ? 0 : 1)), GFP_KERNEL);
		if(tmp_attrs == NULL)
			return -ENOMEM;
		memcpy(tmp_attrs, *class_attrs, sizeof(struct class_device_attribute)*num_attrs);
		if(*class_attrs != scsi_sysfs_shost_attrs)
			kfree(*class_attrs);
		*class_attrs = tmp_attrs;
	}
	if(modify) {
		/* spare the caller from having to copy things it's
		 * not interested in */
		struct class_device_attribute *old_attr =
			(*class_attrs)[modify];
		/* extend permissions */
		attr->attr.mode |= old_attr->attr.mode;

		/* override null show/store with default */
		if(attr->show == NULL)
			attr->show = old_attr->show;
		if(attr->store == NULL)
			attr->store = old_attr->store;
		(*class_attrs)[modify] = attr;
	} else {
		(*class_attrs)[num_attrs] = attr;
	}

	return 0;
}
EXPORT_SYMBOL(scsi_sysfs_modify_shost_attribute);

/** scsi_sysfs_modify_sdev_attribute - modify or add a host device attribute
 *
 * @dev_attrs:	pointer to the attribute list to be added to or modified
 * @attr:	individual attribute to change or added
 *
 * returns zero if successful or error if not
 **/
int scsi_sysfs_modify_sdev_attribute(struct device_attribute ***dev_attrs,
				     struct device_attribute *attr)
{
	int modify = 0;
	int num_attrs;

	if(*dev_attrs == NULL)
		*dev_attrs = scsi_sysfs_sdev_attrs;

	for(num_attrs=0; (*dev_attrs)[num_attrs] != NULL; num_attrs++)
		if(strcmp((*dev_attrs)[num_attrs]->attr.name, attr->attr.name) == 0)
			modify = num_attrs;

	if(*dev_attrs == scsi_sysfs_sdev_attrs || !modify) {
		struct device_attribute **tmp_attrs = kmalloc(sizeof(struct device_attribute)*(num_attrs + (modify ? 0 : 1)), GFP_KERNEL);
		if(tmp_attrs == NULL)
			return -ENOMEM;
		memcpy(tmp_attrs, *dev_attrs, sizeof(struct device_attribute)*num_attrs);
		if(*dev_attrs != scsi_sysfs_sdev_attrs)
			kfree(*dev_attrs);
		*dev_attrs = tmp_attrs;
	}
	if(modify) {
		/* spare the caller from having to copy things it's
		 * not interested in */
		struct device_attribute *old_attr =
			(*dev_attrs)[modify];
		/* extend permissions */
		attr->attr.mode |= old_attr->attr.mode;

		/* override null show/store with default */
		if(attr->show == NULL)
			attr->show = old_attr->show;
		if(attr->store == NULL)
			attr->store = old_attr->store;
		(*dev_attrs)[modify] = attr;
	} else {
		(*dev_attrs)[num_attrs] = attr;
	}

	return 0;
}
EXPORT_SYMBOL(scsi_sysfs_modify_sdev_attribute);
