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

static struct class_device_attribute *scsi_sysfs_shost_attrs[] = {
	&class_device_attr_unique_id,
	&class_device_attr_host_busy,
	&class_device_attr_cmd_per_lun,
	&class_device_attr_sg_tablesize,
	&class_device_attr_unchecked_isa_dma,
	NULL
};

static void scsi_host_cls_release(struct class_device *class_dev)
{
	struct Scsi_Host *shost;

	shost = class_to_shost(class_dev);
	put_device(&shost->shost_gendev);
}

static void scsi_host_dev_release(struct device *dev)
{
	struct Scsi_Host *shost;
	struct device *parent;

	parent = dev->parent;
	shost = dev_to_shost(dev);
	scsi_free_shost(shost);
	put_device(parent);
}

struct class shost_class = {
	.name		= "scsi_host",
	.release	= scsi_host_cls_release,
};

static struct class sdev_class = {
	.name		= "scsi_device",
};

/* all probing is done in the individual ->probe routines */
static int scsi_bus_match(struct device *dev, struct device_driver *gendrv)
{
	return 1;
}

struct bus_type scsi_bus_type = {
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
		goto bus_unregister;
	error = class_register(&sdev_class);
	if (error)
		goto class_unregister;
	return 0;

 class_unregister:
	class_unregister(&shost_class);
 bus_unregister:
	bus_unregister(&scsi_bus_type);
	return error;
}

void scsi_sysfs_unregister(void)
{
	class_unregister(&sdev_class);
	class_unregister(&shost_class);
	bus_unregister(&scsi_bus_type);
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
store_rescan_field (struct device *dev, const char *buf, size_t count) 
{
	scsi_rescan_device(dev);
	return count;
}

static DEVICE_ATTR(rescan, S_IWUSR, NULL, store_rescan_field)

/* Default template for device attributes.  May NOT be modified */
static struct device_attribute *scsi_sysfs_sdev_attrs[] = {
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

static struct device_attribute *attr_overridden(
		struct device_attribute **attrs,
		struct device_attribute *attr)
{
	int i;

	if (!attrs)
		return NULL;
	for (i = 0; attrs[i]; i++)
		if (!strcmp(attrs[i]->attr.name, attr->attr.name))
			return attrs[i];
	return NULL;
}

static int attr_add(struct device *dev, struct device_attribute *attr)
{
	struct device_attribute *base_attr;

	/*
	 * Spare the caller from having to copy things it's not interested in.
	 */
	base_attr = attr_overridden(scsi_sysfs_sdev_attrs, attr);
	if (base_attr) {
		/* extend permissions */
		attr->attr.mode |= base_attr->attr.mode;

		/* override null show/store with default */
		if (!attr->show)
			attr->show = base_attr->show;
		if (!attr->store)
			attr->store = base_attr->store;
	}

	return device_create_file(dev, attr);
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

	device_initialize(&sdev->sdev_gendev);
	sprintf(sdev->sdev_gendev.bus_id,"%d:%d:%d:%d",
		sdev->host->host_no, sdev->channel, sdev->id, sdev->lun);
	sdev->sdev_gendev.parent = &sdev->host->shost_gendev;
	sdev->sdev_gendev.bus = &scsi_bus_type;
	sdev->sdev_gendev.release = scsi_device_release;

	class_device_initialize(&sdev->sdev_classdev);
	sdev->sdev_classdev.dev = &sdev->sdev_gendev;
	sdev->sdev_classdev.class = &sdev_class;
	snprintf(sdev->sdev_classdev.class_id, BUS_ID_SIZE, "%d:%d:%d:%d",
		sdev->host->host_no, sdev->channel, sdev->id, sdev->lun);

	error = device_add(&sdev->sdev_gendev);
	if (error) {
		printk(KERN_INFO "error 1\n");
		return error;
	}
	error = class_device_add(&sdev->sdev_classdev);
	if (error) {
		printk(KERN_INFO "error 2\n");
		device_unregister(&sdev->sdev_gendev);
		return error;
	}

	if (sdev->host->hostt->sdev_attrs) {
		for (i = 0; sdev->host->hostt->sdev_attrs[i]; i++) {
			error = attr_add(&sdev->sdev_gendev,
					sdev->host->hostt->sdev_attrs[i]);
			if (error)
				scsi_device_unregister(sdev);
		}
	}
	
	for (i = 0; scsi_sysfs_sdev_attrs[i]; i++) {
		if (!attr_overridden(sdev->host->hostt->sdev_attrs,
					scsi_sysfs_sdev_attrs[i])) {
			error = device_create_file(&sdev->sdev_gendev,
					scsi_sysfs_sdev_attrs[i]);
			if (error)
				scsi_device_unregister(sdev);
		}
	}

	return error;
}

/**
 * scsi_device_unregister - unregister a device from the scsi bus
 * @sdev:	scsi_device to unregister
 **/
void scsi_device_unregister(struct scsi_device *sdev)
{
	class_device_unregister(&sdev->sdev_classdev);
	device_unregister(&sdev->sdev_gendev);
}

int scsi_register_driver(struct device_driver *drv)
{
	drv->bus = &scsi_bus_type;

	return driver_register(drv);
}

int scsi_register_interface(struct class_interface *intf)
{
	intf->class = &sdev_class;

	return class_interface_register(intf);
}


void scsi_sysfs_init_host(struct Scsi_Host *shost)
{
	device_initialize(&shost->shost_gendev);
	snprintf(shost->shost_gendev.bus_id, BUS_ID_SIZE, "host%d",
		shost->host_no);
	snprintf(shost->shost_gendev.name, DEVICE_NAME_SIZE, "%s",
		shost->hostt->proc_name);
	shost->shost_gendev.release = scsi_host_dev_release;

	class_device_initialize(&shost->shost_classdev);
	shost->shost_classdev.dev = &shost->shost_gendev;
	shost->shost_classdev.class = &shost_class;
	snprintf(shost->shost_classdev.class_id, BUS_ID_SIZE, "host%d",
		  shost->host_no);
}

static struct class_device_attribute *class_attr_overridden(
		struct class_device_attribute **attrs,
		struct class_device_attribute *attr)
{
	int i;

	if (!attrs)
		return NULL;
	for (i = 0; attrs[i]; i++)
		if (!strcmp(attrs[i]->attr.name, attr->attr.name))
			return attrs[i];
	return NULL;
}

static int class_attr_add(struct class_device *classdev,
		struct class_device_attribute *attr)
{
	struct class_device_attribute *base_attr;

	/*
	 * Spare the caller from having to copy things it's not interested in.
	 */
	base_attr = class_attr_overridden(scsi_sysfs_shost_attrs, attr);
	if (base_attr) {
		/* extend permissions */
		attr->attr.mode |= base_attr->attr.mode;

		/* override null show/store with default */
		if (!attr->show)
			attr->show = base_attr->show;
		if (!attr->store)
			attr->store = base_attr->store;
	}

	return class_device_create_file(classdev, attr);
}

/**
 * scsi_sysfs_add_host - add scsi host to subsystem
 * @shost:     scsi host struct to add to subsystem
 * @dev:       parent struct device pointer
 **/
int scsi_sysfs_add_host(struct Scsi_Host *shost, struct device *dev)
{
	int error, i;

	if (!shost->shost_gendev.parent)
		shost->shost_gendev.parent = dev ? dev : &legacy_bus;

	error = device_add(&shost->shost_gendev);
	if (error)
		return error;

	set_bit(SHOST_ADD, &shost->shost_state);
	get_device(shost->shost_gendev.parent);

	error = class_device_add(&shost->shost_classdev);
	if (error)
		goto clean_device;

	get_device(&shost->shost_gendev);
	if (shost->hostt->shost_attrs) {
		for (i = 0; shost->hostt->shost_attrs[i]; i++) {
			error = class_attr_add(&shost->shost_classdev,
					shost->hostt->shost_attrs[i]);
			if (error)
				goto clean_class;
		}
	}

	for (i = 0; scsi_sysfs_shost_attrs[i]; i++) {
		if (!class_attr_overridden(shost->hostt->shost_attrs,
					scsi_sysfs_shost_attrs[i])) {
			error = class_device_create_file(&shost->shost_classdev,
					scsi_sysfs_shost_attrs[i]);
			if (error)
				goto clean_class;
		}
	}

	return error;

clean_class:
	class_device_del(&shost->shost_classdev);
clean_device:
	device_del(&shost->shost_gendev);

	return error;
}

/**
 * scsi_sysfs_remove_host - remove scsi host from subsystem
 * @shost:     scsi host to remove from subsystem
 **/
void scsi_sysfs_remove_host(struct Scsi_Host *shost)
{
	unsigned long flags;
	spin_lock_irqsave(shost->host_lock, flags);
	set_bit(SHOST_DEL, &shost->shost_state);
	spin_unlock_irqrestore(shost->host_lock, flags);

	class_device_unregister(&shost->shost_classdev);
	device_del(&shost->shost_gendev);
}
