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

#include <scsi/scsi_host.h>
#include <scsi/scsi_transport.h>
#include "scsi.h"

#include "scsi_priv.h"
#include "scsi_logging.h"

static struct {
	enum scsi_device_state	value;
	char			*name;
} sdev_states[] = {
	{ SDEV_CREATED, "created" },
	{ SDEV_RUNNING, "running" },
	{ SDEV_CANCEL, "cancel" },
	{ SDEV_DEL, "deleted" },
	{ SDEV_QUIESCE, "quiesce" },
	{ SDEV_OFFLINE,	"offline" },
};

const char *scsi_device_state_name(enum scsi_device_state state)
{
	int i;
	char *name = NULL;

	for (i = 0; i < sizeof(sdev_states)/sizeof(sdev_states[0]); i++) {
		if (sdev_states[i].value == state) {
			name = sdev_states[i].name;
			break;
		}
	}
	return name;
}

static int check_set(unsigned int *val, char *src)
{
	char *last;

	if (strncmp(src, "-", 20) == 0) {
		*val = SCAN_WILD_CARD;
	} else {
		/*
		 * Doesn't check for int overflow
		 */
		*val = simple_strtoul(src, &last, 0);
		if (*last != '\0')
			return 1;
	}
	return 0;
}

static int scsi_scan(struct Scsi_Host *shost, const char *str)
{
	char s1[15], s2[15], s3[15], junk;
	unsigned int channel, id, lun;
	int res;

	res = sscanf(str, "%10s %10s %10s %c", s1, s2, s3, &junk);
	if (res != 3)
		return -EINVAL;
	if (check_set(&channel, s1))
		return -EINVAL;
	if (check_set(&id, s2))
		return -EINVAL;
	if (check_set(&lun, s3))
		return -EINVAL;
	res = scsi_scan_host_selected(shost, channel, id, lun, 1);
	return res;
}

/*
 * shost_show_function: macro to create an attr function that can be used to
 * show a non-bit field.
 */
#define shost_show_function(name, field, format_string)			\
static ssize_t								\
show_##name (struct class_device *class_dev, char *buf)			\
{									\
	struct Scsi_Host *shost = class_to_shost(class_dev);		\
	return snprintf (buf, 20, format_string, shost->field);		\
}

/*
 * shost_rd_attr: macro to create a function and attribute variable for a
 * read only field.
 */
#define shost_rd_attr2(name, field, format_string)			\
	shost_show_function(name, field, format_string)			\
static CLASS_DEVICE_ATTR(name, S_IRUGO, show_##name, NULL)

#define shost_rd_attr(field, format_string) \
shost_rd_attr2(field, field, format_string)

/*
 * Create the actual show/store functions and data structures.
 */

static ssize_t store_scan(struct class_device *class_dev, const char *buf,
			  size_t count)
{
	struct Scsi_Host *shost = class_to_shost(class_dev);
	int res;

	res = scsi_scan(shost, buf);
	if (res == 0)
		res = count;
	return res;
};
static CLASS_DEVICE_ATTR(scan, S_IWUSR, NULL, store_scan);

shost_rd_attr(unique_id, "%u\n");
shost_rd_attr(host_busy, "%hu\n");
shost_rd_attr(cmd_per_lun, "%hd\n");
shost_rd_attr(sg_tablesize, "%hu\n");
shost_rd_attr(unchecked_isa_dma, "%d\n");
shost_rd_attr2(proc_name, hostt->proc_name, "%s\n");

static struct class_device_attribute *scsi_sysfs_shost_attrs[] = {
	&class_device_attr_unique_id,
	&class_device_attr_host_busy,
	&class_device_attr_cmd_per_lun,
	&class_device_attr_sg_tablesize,
	&class_device_attr_unchecked_isa_dma,
	&class_device_attr_proc_name,
	&class_device_attr_scan,
	NULL
};

static void scsi_device_cls_release(struct class_device *class_dev)
{
	struct scsi_device *sdev;

	sdev = class_to_sdev(class_dev);
	put_device(&sdev->sdev_gendev);
}

void scsi_device_dev_release(struct device *dev)
{
	struct scsi_device *sdev;
	struct device *parent;
	unsigned long flags;

	parent = dev->parent;
	sdev = to_scsi_device(dev);

	spin_lock_irqsave(sdev->host->host_lock, flags);
	list_del(&sdev->siblings);
	list_del(&sdev->same_target_siblings);
	list_del(&sdev->starved_entry);
	if (sdev->single_lun && --sdev->sdev_target->starget_refcnt == 0)
		kfree(sdev->sdev_target);
	spin_unlock_irqrestore(sdev->host->host_lock, flags);

	if (sdev->request_queue)
		scsi_free_queue(sdev->request_queue);

	kfree(sdev->inquiry);
	kfree(sdev);

	put_device(parent);
}

struct class sdev_class = {
	.name		= "scsi_device",
	.release	= scsi_device_cls_release,
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
	if (!error) {
		error = class_register(&sdev_class);
		if (error)
			bus_unregister(&scsi_bus_type);
	}

	return error;
}

void scsi_sysfs_unregister(void)
{
	class_unregister(&sdev_class);
	bus_unregister(&scsi_bus_type);
}

/*
 * sdev_show_function: macro to create an attr function that can be used to
 * show a non-bit field.
 */
#define sdev_show_function(field, format_string)				\
static ssize_t								\
sdev_show_##field (struct device *dev, char *buf)				\
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
static DEVICE_ATTR(field, S_IRUGO, sdev_show_##field, NULL)


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
static DEVICE_ATTR(field, S_IRUGO | S_IWUSR, sdev_show_##field, sdev_store_##field)

/* Currently we don't export bit fields, but we might in future,
 * so leave this code in */
#if 0
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
static DEVICE_ATTR(field, S_IRUGO | S_IWUSR, sdev_show_##field, sdev_store_##field)

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
#endif
/*
 * Create the actual show/store functions and data structures.
 */
sdev_rd_attr (device_blocked, "%d\n");
sdev_rd_attr (queue_depth, "%d\n");
sdev_rd_attr (type, "%d\n");
sdev_rd_attr (scsi_level, "%d\n");
sdev_rd_attr (vendor, "%.8s\n");
sdev_rd_attr (model, "%.16s\n");
sdev_rd_attr (rev, "%.4s\n");

static ssize_t
store_rescan_field (struct device *dev, const char *buf, size_t count) 
{
	scsi_rescan_device(dev);
	return count;
}
static DEVICE_ATTR(rescan, S_IWUSR, NULL, store_rescan_field)

static ssize_t sdev_store_delete(struct device *dev, const char *buf,
				 size_t count)
{
	scsi_remove_device(to_scsi_device(dev));
	return count;
};
static DEVICE_ATTR(delete, S_IWUSR, NULL, sdev_store_delete);

static ssize_t
store_state_field(struct device *dev, const char *buf, size_t count)
{
	int i;
	struct scsi_device *sdev = to_scsi_device(dev);
	enum scsi_device_state state = 0;

	for (i = 0; i < sizeof(sdev_states)/sizeof(sdev_states[0]); i++) {
		const int len = strlen(sdev_states[i].name);
		if (strncmp(sdev_states[i].name, buf, len) == 0 &&
		   buf[len] == '\n') {
			state = sdev_states[i].value;
			break;
		}
	}
	if (!state)
		return -EINVAL;

	if (scsi_device_set_state(sdev, state))
		return -EINVAL;
	return count;
}

static ssize_t
show_state_field(struct device *dev, char *buf)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	const char *name = scsi_device_state_name(sdev->sdev_state);

	if (!name)
		return -EINVAL;

	return snprintf(buf, 20, "%s\n", name);
}

DEVICE_ATTR(state, S_IRUGO | S_IWUSR, show_state_field, store_state_field);


/* Default template for device attributes.  May NOT be modified */
static struct device_attribute *scsi_sysfs_sdev_attrs[] = {
	&dev_attr_device_blocked,
	&dev_attr_queue_depth,
	&dev_attr_type,
	&dev_attr_scsi_level,
	&dev_attr_vendor,
	&dev_attr_model,
	&dev_attr_rev,
	&dev_attr_rescan,
	&dev_attr_delete,
	&dev_attr_state,
	NULL
};


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
 * scsi_sysfs_add_sdev - add scsi device to sysfs
 * @sdev:	scsi_device to add
 *
 * Return value:
 * 	0 on Success / non-zero on Failure
 **/
int scsi_sysfs_add_sdev(struct scsi_device *sdev)
{
	struct class_device_attribute **attrs;
	int error, i;

	if ((error = scsi_device_set_state(sdev, SDEV_RUNNING)) != 0)
		return error;

	error = device_add(&sdev->sdev_gendev);
	if (error) {
		printk(KERN_INFO "error 1\n");
		return error;
	}

	error = class_device_add(&sdev->sdev_classdev);
	if (error) {
		printk(KERN_INFO "error 2\n");
		goto clean_device;
	}
	/* take a reference for the sdev_classdev; this is
	 * released by the sdev_class .release */
	get_device(&sdev->sdev_gendev);

	if (sdev->transport_classdev.class) {
		error = class_device_add(&sdev->transport_classdev);
		if (error)
			goto clean_device2;
		/* take a reference for the transport_classdev; this
		 * is released by the transport_class .release */
		get_device(&sdev->sdev_gendev);
		
	}

	if (sdev->host->hostt->sdev_attrs) {
		for (i = 0; sdev->host->hostt->sdev_attrs[i]; i++) {
			error = attr_add(&sdev->sdev_gendev,
					sdev->host->hostt->sdev_attrs[i]);
			if (error) {
				scsi_remove_device(sdev);
				goto out;
			}
		}
	}
	
	for (i = 0; scsi_sysfs_sdev_attrs[i]; i++) {
		if (!attr_overridden(sdev->host->hostt->sdev_attrs,
					scsi_sysfs_sdev_attrs[i])) {
			error = device_create_file(&sdev->sdev_gendev,
					scsi_sysfs_sdev_attrs[i]);
			if (error) {
				scsi_remove_device(sdev);
				goto out;
			}
		}
	}

 	if (sdev->transport_classdev.class) {
 		attrs = sdev->host->transportt->attrs;
 		for (i = 0; attrs[i]; i++) {
 			error = class_device_create_file(&sdev->transport_classdev,
 							 attrs[i]);
 			if (error) {
 				scsi_remove_device(sdev);
				goto out;
			}
 		}
 	}

 out:
	return error;

 clean_device2:
	class_device_del(&sdev->sdev_classdev);
 clean_device:
	scsi_device_set_state(sdev, SDEV_CANCEL);

	device_del(&sdev->sdev_gendev);
	put_device(&sdev->sdev_gendev);

	return error;
}

/**
 * scsi_remove_device - unregister a device from the scsi bus
 * @sdev:	scsi_device to unregister
 **/
void scsi_remove_device(struct scsi_device *sdev)
{
	if (scsi_device_set_state(sdev, SDEV_CANCEL) != 0)
		return;

	class_device_unregister(&sdev->sdev_classdev);
	if (sdev->transport_classdev.class)
		class_device_unregister(&sdev->transport_classdev);
	device_del(&sdev->sdev_gendev);
	scsi_device_set_state(sdev, SDEV_DEL);
	if (sdev->host->hostt->slave_destroy)
		sdev->host->hostt->slave_destroy(sdev);
	if (sdev->host->transportt->cleanup)
		sdev->host->transportt->cleanup(sdev);
	put_device(&sdev->sdev_gendev);
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
int scsi_sysfs_add_host(struct Scsi_Host *shost)
{
	int error, i;

	if (shost->hostt->shost_attrs) {
		for (i = 0; shost->hostt->shost_attrs[i]; i++) {
			error = class_attr_add(&shost->shost_classdev,
					shost->hostt->shost_attrs[i]);
			if (error)
				return error;
		}
	}

	for (i = 0; scsi_sysfs_shost_attrs[i]; i++) {
		if (!class_attr_overridden(shost->hostt->shost_attrs,
					scsi_sysfs_shost_attrs[i])) {
			error = class_device_create_file(&shost->shost_classdev,
					scsi_sysfs_shost_attrs[i]);
			if (error)
				return error;
		}
	}

	return 0;
}

/* A blank transport template that is used in drivers that don't
 * yet implement Transport Attributes */
struct scsi_transport_template blank_transport_template = { 0, };
