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

/**
 * scsi_host_class_name_show - copy out the SCSI host name
 * @dev:		device to check
 * @page:		copy data into this area
 * @count:		number of bytes to copy
 * @off:		start at this offset in page
 * Return:
 *     number of bytes written into page.
 **/
static ssize_t scsi_host_class_name_show(struct device *dev, char *page,
	size_t count, loff_t off)
{
	struct Scsi_Host *shost;

	if (off)
		return 0;

	shost = to_scsi_host(dev);

	if (!shost)
		return 0;
	
	return snprintf(page, count, "scsi%d\n", shost->host_no);
}

DEVICE_ATTR(class_name, S_IRUGO, scsi_host_class_name_show, NULL);

static int scsi_host_class_add_dev(struct device * dev)
{
	device_create_file(dev, &dev_attr_class_name);
	return 0;
}

static void scsi_host_class_rm_dev(struct device * dev)
{
	device_remove_file(dev, &dev_attr_class_name);
}

struct device_class shost_devclass = {
	.name		= "scsi-host",
	.add_device	= scsi_host_class_add_dev,
	.remove_device	= scsi_host_class_rm_dev,
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
	bus_register(&scsi_bus_type);
	devclass_register(&shost_devclass);

	return 0;
}

void scsi_sysfs_unregister(void)
{
	devclass_unregister(&shost_devclass);
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


/**
 * scsi_device_type_read - copy out the SCSI type
 * @dev:		device to check
 * @page:		copy data into this area
 * @count:		number of bytes to copy
 * @off:		start at this offset in page
 *
 * Return:
 *     number of bytes written into page.
 **/
static ssize_t scsi_device_type_read(struct device *dev, char *page,
	size_t count, loff_t off)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	const char *type;

	if (off)
		return 0;

	if ((sdev->type > MAX_SCSI_DEVICE_CODE) ||
	    (scsi_device_types[(int)sdev->type] == NULL))
		type = "Unknown";
	else
		type = scsi_device_types[(int)sdev->type];

	return snprintf(page, count, "%s\n", type);
}

/*
 * Create dev_attr_type. This is different from the dev_attr_type in scsi
 * upper level drivers.
 */
static DEVICE_ATTR(type,S_IRUGO,scsi_device_type_read,NULL);

/**
 * scsi_device_register - register a scsi device with the scsi bus
 * @sdev:	scsi_device to register
 *
 * Return value:
 * 	0 on Success / non-zero on Failure
 **/
int scsi_device_register(struct scsi_device *sdev)
{
	int error = 0;

	sprintf(sdev->sdev_driverfs_dev.bus_id,"%d:%d:%d:%d",
		sdev->host->host_no, sdev->channel, sdev->id, sdev->lun);
	sdev->sdev_driverfs_dev.parent = sdev->host->host_gendev;
	sdev->sdev_driverfs_dev.bus = &scsi_bus_type;

	error = device_register(&sdev->sdev_driverfs_dev);
	if (error)
		return error;

	error = device_create_file(&sdev->sdev_driverfs_dev, &dev_attr_type);
	if (error)
		device_unregister(&sdev->sdev_driverfs_dev);

	return error;
}

/**
 * scsi_device_unregister - unregister a device from the scsi bus
 * @sdev:	scsi_device to unregister
 **/
void scsi_device_unregister(struct scsi_device *sdev)
{
	device_remove_file(&sdev->sdev_driverfs_dev, &dev_attr_type);
	device_unregister(&sdev->sdev_driverfs_dev);
}
